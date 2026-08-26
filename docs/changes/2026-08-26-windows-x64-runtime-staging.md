# 2026-08-26 — Stage the full runtime into the build, and package it for sharing

Follow-on from `2026-08-26-windows-x64-silent-audio.md`, which fixed the silent-audio
symptom by hand. This makes the build produce a complete, runnable, shareable tree on
its own, so the same class of bug cannot come back.

## Why

`Minecraft.Client\bin\x64\Release\` could not run at all. `postbuild.ps1` staged the
content directories (`Common\`, `Windows64Media\`, `music\`) but not:

- `mss64.dll` and `iggy_w64.dll` — hard loader imports; without them the process does
  not start,
- `redist64\` — the Miles provider modules,
- `Durango\Sound\Minecraft.msscmp` — the soundbank.

The only runnable tree, `Builds\Windows-x64-Release\`, was assembled by hand, which is
how the soundbank came to be missing in the first place. With a build about to be
shared with other people, hand-assembly is the wrong mechanism.

## Changes

### `Minecraft.Client\postbuild.ps1`

- Stages `Durango\Sound\` (soundbank) and `redist64\` (Miles providers) from
  `Minecraft.Client\`.
- Stages `mss64.dll` and `iggy_w64.dll` from `$(SolutionDir)x64\Release\`.
- Ends with a required-file check that prints a loud `INCOMPLETE BUILD` warning naming
  anything missing, instead of quietly producing something that cannot run.
- Warns when a content source directory is absent rather than skipping in silence —
  that silence is what hid the dead `Windows64\Sound` entry (the code has always
  wanted `Durango\Sound`; `Windows64\Sound` does not exist and never did).
- Removed the `Windows64\GameHDD` copy. It is where saves are written at runtime;
  there is nothing to copy into it, and it is still created empty.
- New `-SolutionDir` parameter, with a fallback deriving it from `-ProjectDir` so a
  stale `PostBuildEvent` command line degrades instead of failing.

### `Minecraft.Client\Minecraft.Client.vcxproj`

The five `PostBuildEvent` command lines now pass `-SolutionDir`. All three arguments
are quoted, each with a trailing `.`:

```
-OutDir "$(OutDir)." -ProjectDir "$(ProjectDir)." -SolutionDir "$(SolutionDir)."
```

The `.` is deliberate. These MSBuild properties end in a backslash, and `"…\"` would
escape the closing quote and corrupt argument parsing. Do not "clean up" the dots.

Affects `Debug|x64`, `Debug|ARM64EC`, `Debug|Win32`, `Release|x64`, `Release|ARM64EC`
— the only configurations that invoke the script. No console configuration touched.

### `Minecraft.Client\Windows64\Windows64_Minecraft.cpp`

Added `setWorkingDirectoryToExe()`, called first thing in `_tWinMain`. Every asset
path on this platform resolves against the **working directory**, not the module.
Explorer happens to set it correctly, but a shortcut with a different "Start in", a
`.bat` launcher, or a debugger with a stale working directory all break assets
silently — and the loudest symptom is total silence, because a soundbank that cannot
be found makes `SoundEngine::init()` shut the whole Miles driver down. This matters
much more once other people are launching the build.

Windows-only file, no `#ifdef` added, console targets untouched.

### `package-win64.ps1` (new, repo root)

Mirrors the build output into `Builds\Windows-x64-<cfg>\`, minus `.pch`/`.pdb`/`.ilk`
and local save data, then verifies 15 required paths and fails if any are absent.

```
.\package-win64.ps1            # -> Builds\Windows-x64-Release  (2396 files, 492 MB)
.\package-win64.ps1 -Zip       # also writes the .zip
```

Two robocopy traps are handled explicitly, both of which silently leaked files:

- `/MIR` will not delete a file matched by `/XF`, so a stale 58MB `Minecraft.Client.pdb`
  from an earlier hand-built package survived every mirror.
- `/XD` behaves the same way, so `Windows64\GameHDD` survived too — meaning **every
  package would have carried this machine's save data** to whoever received it.

Both are now swept after the mirror. The script also ends with `exit 0`, because
robocopy leaves a non-zero `$LASTEXITCODE` behind on success (3 = copied + extras
removed) which otherwise looks like a failed script.

## Corrections to the previous note

The earlier fix staged `redist64` from `Minecraft.Client\Windows64\Miles\lib\redist64\`.
**That was the wrong set.** Three different Miles redists exist in the tree, and that
one is a different Miles version from the shipping `mss64.dll` (FileVersion 9.3m) —
its `binkawin64.asi` is 110,592 bytes against 110,080 in the other two. Miles refuses
providers that do not match the driver. The correct source is
`Minecraft.Client\redist64\`, which is what the scripts now use, and
`Builds\Windows-x64-Release\` has been corrected. See the table in
`docs\systems\windows-x64-audio.md`.

## Verified

- `MinecraftPC.sln` at `Release|x64` builds: **0 errors**, 34 warnings, all pre-existing
  `LNK4099` vc110.pdb notices from the VS2012 middleware.
- Post-build staging runs from MSBuild and reports `Runtime staging complete`.
- Deleting `Durango\`, `redist64\`, `mss64.dll` and `iggy_w64.dll` from the output and
  re-running the script restores all four — staging does not depend on leftovers.
- Staged file checksums match their sources (md5).
- `package-win64.ps1` produces 2396 files / 491.9 MB, no `.pdb`, empty `GameHDD`.
- `dumpbin /dependents` closure on the package: the exe's only non-OS imports are
  `mss64.dll` and `iggy_w64.dll`; those import only `KERNEL32`, `USER32`, `WINMM`.
  No `VCRUNTIME`/`MSVCP` — `/MT` links the CRT statically, so **recipients do not need
  the VC++ redistributable**.
- Both `iggy_w64.dll` copies in the tree satisfy all 41 Iggy symbols the exe imports
  (checked against `dumpbin /exports`); the 203-export `x64\Release` one is shipped.

## Not verified

**Nothing here confirms the game runs, renders, or makes sound.** No agent can check
that. The audio fix in particular is still unconfirmed — the soundbank is present and
the Miles redist now matches the driver, but only running it proves anything.

If it is still silent, the next thing to try is the other soundbank,
`Minecraft.Client\DurangoMedia\Sound\Minecraft.msscmp` (12,420,314 B vs the
12,276,954 B one staged) — a different build of the bank, not a duplicate.

## Watch out for next

- `x64\Release\` is a **source** directory despite the name, and is the only place
  holding `mss64.dll`. Do not add it to `.gitignore` or clean it as build output.
- Dedicated-server work will want a headless equivalent of this staging: a server does
  not need `redist64`, `music\` or the soundbank at all, and forcing it to carry 492MB
  of client assets would be a mistake.

## Which directory to actually run (added 2026-08-26)

This bit two people already, so, plainly:

| Directory | Written by | When |
|---|---|---|
| `Minecraft.Client\bin\x64\Release\` | the build (`postbuild.ps1`) | every build |
| `Builds\Windows-x64-Release\` | `package-win64.ps1` | **only when run by hand** |

The build never touches `Builds\`. It is the *share* artefact, not the *run* artefact.
`bin\x64\Release\` is a complete runnable tree on its own — that is the entire point of
the staging change above — so **run from there** while developing.

The failure mode is quiet and convincing: you rebuild, launch `Builds\`, and test an
old executable. It looks exactly like a fix that did not work. Check the exe timestamp
against the build before concluding anything about a change.

**`package-win64.ps1` deletes `Windows64\GameHDD` in the destination.** That is
deliberate — it is local save data and must not ship to whoever the build is sent to —
but it means refreshing `Builds\` destroys any world you have been playing there.
Another reason to keep play sessions in `bin\x64\Release\`, whose `GameHDD` nothing
sweeps.
