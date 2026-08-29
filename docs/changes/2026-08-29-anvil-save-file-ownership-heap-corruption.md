# 2026-08-29 - Opening a world crashed with heap corruption: stack-allocated save files

## Symptom

Selecting an existing world in the load menu killed the process immediately. No
in-game error, no log - the game has no crash logger on Windows x64, so the only
evidence is the Windows Error Reporting minidump in `%LOCALAPPDATA%\CrashDumps\`.

Symbolized (see "Reading a dump" below), every dump had the same faulting stack:

```
exception 0xC0000374  STATUS_HEAP_CORRUPTION   ntdll!RtlReportFatalFailure
  _free_base                                        free_base.cpp:105
  std::wstring::_Tidy_deallocate                    xstring:3090
  NativeSaveFile::`scalar deleting destructor'
  DirectoryLevelStorage::~DirectoryLevelStorage     DirectoryLevelStorage.cpp:177
  UIScene_LoadOrJoinMenu::OpenAnvilWorld            UIScene_LoadOrJoinMenu.cpp:1298
```

## Cause

`DirectoryLevelStorage` has always **owned** the `ConsoleSaveFile` it is handed -
its destructor ends in an unconditional `delete m_saveFile`. Every console call
site allocates one with `new`, so that rule was never written down anywhere.

The three Anvil entry points added on 2026-08-28 broke it. All of them put the
save file on the **stack**:

```cpp
NativeSaveFile saveFile(AnvilSavePaths::worldDirectory(worldName));   // stack
AnvilLevelStorage storage(&saveFile, baseDir, worldName, false);
return storage.prepareLevel();
```

`storage` destructs at the closing brace and calls `delete` on a stack address.
Freeing a non-heap pointer is what trips the CRT heap check; the `_Tidy_deallocate`
frame is the `wstring` member inside `NativeSaveFile` being released by that
bogus delete. It is not a "sometimes" bug - it fires every single time.

`getDataTagFor` had a different flavour of the same mistake: it forwarded the
**caller's** save file into a storage that then deleted it, leaving the caller
holding a dangling pointer to free a second time.

## Change

`DirectoryLevelStorage` gained an ownership flag - a trailing
`bool ownsSaveFile = true` constructor parameter and an `m_bOwnsSaveFile` member.
The destructor's `delete` is now conditional. The default preserves the existing
rule exactly, so no console call site changes behaviour and nothing in the 7th-gen
targets needed touching. `AnvilLevelStorage` forwards the flag.

Call sites in `AnvilLevelStorageSource.cpp`:

| Function | Before | After |
|---|---|---|
| `getLevelList` | stack `NativeSaveFile` | `new NativeSaveFile`, owned by the storage |
| `loadLevelDataFor` | stack `NativeSaveFile` | `new NativeSaveFile`, owned by the storage |
| `getDataTagFor` | deleted the caller's save file | passes `ownsSaveFile = false` |

`getLevelList` also had to switch `saveFile.getSizeOnDisk()` to `saveFile->`; the
call still happens while `storage` is alive, which is required - the storage frees
the save file when it destructs at the end of each loop iteration.

## Files

- `Minecraft.World/DirectoryLevelStorage.h` - ownership flag on the constructor, `m_bOwnsSaveFile`
- `Minecraft.World/DirectoryLevelStorage.cpp` - store the flag; conditional `delete`
- `Minecraft.World/AnvilLevelStorage.h` / `.cpp` - forward the flag
- `Minecraft.World/AnvilLevelStorageSource.cpp` - the three call sites above

## Status

Built: `MinecraftPC.sln` at `Release|x64` compiles and links clean.

**Unverified:** whether the world actually opens. An agent cannot run the game.
The heap corruption at this specific site is definitely gone - it was a
deterministic bad free, not a race - but anything further along the load path is
untested.

## Reading a dump

Worth recording, because there is no debugger installed and this is not obvious.

Windows Error Reporting already writes full minidumps to `%LOCALAPPDATA%\CrashDumps\`.
There is **no `cdb.exe`** on this machine - `C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\`
contains only `dbghelp.dll`, `dbgcore.dll`, `symsrv.dll` and `srcsrv.dll`. So:

1. Parse the minidump directly. The streams needed are `ModuleList` (4),
   `ThreadList` (3) and `Exception` (6). `MINIDUMP_MODULE` is 108 bytes with
   `ModuleNameRva` at offset 0x14; the exception stream's context descriptor is at
   +160, and in `CONTEXT_AMD64` `Rsp` is at 0x98 and `Rip` at 0xF8.
2. There is no unwind info in play, so scan the crashed thread's stack from `Rsp`
   for 8-byte values landing inside the exe's image range. That gives return
   addresses (with stale slots mixed in, but the frames nearest `Rsp` are real).
3. Resolve RVAs by loading `dbghelp.dll` via ctypes: `SymInitialize`,
   `SymLoadModuleEx` at a synthetic base, then `SymFromAddr` and
   `SymGetLineFromAddr64`.

**Gotcha that cost the most time:** `SymSetOptions` must *not* include
`SYMOPT_DEFERRED_LOADS` (0x4). With it set, `SymFromAddr` silently resolves nothing
and `SymGetModuleInfo64` reports `SymType == SymDeferred (5)` with an empty PDB
name. Use `SYMOPT_UNDNAME | SYMOPT_LOAD_LINES` (0x2 | 0x10) and symbols load.

Also check *which* exe the dump is from - the module list carries the full path.
Running out of `Builds/Windows-x64-Release` and out of `Minecraft.Client/bin/x64/Release`
produces different binaries, and symbolizing against the wrong one gives plausible
but wrong function names.
