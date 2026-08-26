# Windows x64 build on modern MSVC (v143)

How the `x64` target builds under Visual Studio 2022 instead of the VS2012 (v110)
toolset the tree originally shipped with.

## Why this exists

`MinecraftPC.sln` targets `PlatformToolset v110`. VS2012 is not installable on a
current machine in practice, so the only realistically buildable target — Windows
x64 — could not be built at all. This documents the retarget to `v143`.

**Only the `x64` configurations were retargeted.** `Win32`, `Durango` and `ARM64EC`
still say `v110`, and the console toolsets (`2010-01`, `SNC`, `SPU`, `Clang`) in
`MinecraftConsoles.sln` are untouched. Nothing here affects the 7th-gen builds.

## What actually broke

Strikingly little. Of 705 translation units in `Minecraft.World` and ~254 in
`Minecraft.Client`, only **four files** failed to compile, across two root causes.

### 1. `hash_value` removed from the MSVC STL

`<xhash>` used to provide a `hash_value()` helper. It was removed after VS2013.
Used by `Hasher.cpp` (unqualified) and `Player.cpp` (as `std::hash_value`).

Shimmed in `Minecraft.World/x64headers/extraX64.h`, guarded by
`_MSC_VER >= 1900` so older toolsets keep using the original. `Player.cpp` now
calls it unqualified, which resolves to `std::hash_value` on v110 via the
`using namespace std;` already in that header — so both toolsets still work.

Note `Hasher` has **no callers anywhere in the tree**; it is dead code that is
still compiled. `Player::hash_fnct` only feeds an in-memory hash container, and
its value already differs per platform (PS3 uses `boost::hash_value`), so the
exact hash algorithm is not load-bearing.

### 2. `va_start` on a reference parameter

`I18n::get(const wstring& id, ...)` and `Language::getElement(const wstring&, ...)`
called `va_start` on a reference parameter. That has always been undefined, and
modern MSVC now rejects it with a `static_assert`. 4J hit the same wall on PS
Vita and worked around it with `#ifdef __PSVITA__ return L"";`.

**We did not copy the Vita workaround.** The varargs chain dead-ends in
`Language::getElement(elementId, va_list)`, which is a 4J TODO stub that ignores
`args` and returns `elementId`. So today these calls return the raw untranslated
key. Stubbing to `L""` would have regressed the two real callers
(`AchievementScreen.cpp:408`, `Options.cpp:197`) from showing a key to showing
nothing. Instead the last named parameter was changed to by-value `wstring`,
which makes `va_start` legal and keeps behaviour byte-identical on every platform.

## The real blocker: prebuilt VS2012 middleware

All source compiles cleanly. The genuine wall is at link time.

`Minecraft.Client/Windows64/4JLibs/libs/` holds `4J_Input.lib`, `4J_Storage.lib`
and `4J_Render_PC.lib` — **prebuilt static libs with no source in the tree**,
compiled with VS2012 (`_MSC_VER` 1700). Two separate problems:

**a) `/FAILIFMISMATCH` guard.** Each object carries a
`FAILIFMISMATCH:"_MSC_VER=1700"` directive, producing `LNK2038` against our 1900+
objects. `RuntimeLibrary=MT_StaticRelease` and `_ITERATOR_DEBUG_LEVEL=0` already
match — `_MSC_VER` is the only disagreement.

**b) CRT symbols deleted by the UCRT.** The libs reference `__iob_func` and
`const char *std::_Winerror_map(int)`, both removed in VS2015.

(b) is handled by `Minecraft.Client/Windows64/Win64CrtCompat.cpp`, an x64-only
shim. `_Winerror_map` cannot simply be redefined — the modern STL declares a
`_Winerror_map` with a *different return type*, so the old decorated name is bound
via `#pragma comment(linker, "/alternatename:...")` instead.

(a) is currently handled by **binary-patching the vendored `.lib` files**: the
string `FAILIFMISMATCH:"_MSC_VER=1700"` is rewritten to
`FAILIFMISMATCH:"_MSC_VXR=1700"`, same byte length, so the key no longer collides
with anything. See the gotcha below — this needs an owner decision.

## Gotchas

- **Vendored binaries are modified in-tree, deliberately.** The owner has confirmed
  this is acceptable — patching prebuilt middleware is considered part of the point
  of this fork, and CLAUDE.md now says so. The patch is byte-length-preserving and
  reversible with `git checkout -- Minecraft.Client/Windows64/4JLibs/libs/`, which
  will also stop the build linking until it is redone. Because the edit is invisible
  in a diff, the exact byte change is recorded above and in the change entry so it
  can be reproduced from the notes alone.
- **Cross-CRT STL is a real runtime risk.** `4J_Storage.h` and `4J_Profile.h` pass
  `std::string`, `std::wstring` and `std::vector` across the ABI boundary between
  VS2012-compiled code and VS2022-compiled code. This links, but passing STL types
  across CRT versions is officially unsupported. If the game misbehaves in save/DLC
  handling or profile display names, suspect this first.
- Build via `MinecraftPC.sln`, **not** the `.vcxproj` directly. The link line uses
  `$(SolutionDir)` to find `Minecraft.World.lib`; building the project alone
  resolves that to the wrong directory and fails with `LNK1181`.
- 10 files in `Minecraft.World` are `ExcludedFromBuild` for x64 (`ZonedChunkStorage`,
  `ConsoleSaveFileSplit`, `MemoryLevelStorage*`, `SkyIslandDimension`,
  `DurangoStats`, …). They do **not** compile and are not expected to. Do not
  "fix" them without checking the exclusion list first.

## Build command

```
MSBuild.exe MinecraftPC.sln -p:Configuration=Release -p:Platform=x64 -m
```

Output: `Minecraft.Client/bin/x64/Release/Minecraft.Client.exe`, plus `Common/`,
`Windows64Media/` and `music/` staged next to it by post-build steps.

Runtime also needs `iggy_w64.dll` and `mss64.dll` (both in `x64/Release/`),
`Effects.msscmp`, and the Miles `.asi`/`.flt` files from `Minecraft.Client/redist64/`.
