# Windows x64 audio (Miles Sound System)

## How it works

Audio on `_WINDOWS64` is the shared `Minecraft.Client/Common/Audio/SoundEngine.cpp`
(the `#else` half — the `#ifdef _XBOX` half at the top is a set of empty stubs, so
Xbox 360 is silent by design in this file and uses `Xbox/Audio/SoundEngine.cpp`
instead). It drives RAD's **Miles Sound System 9**, linked against
`Minecraft.Client/Windows64/Miles/lib/mss64.lib` — an *import* library, so the built
exe has a hard `mss64.dll` import.

Three runtime path constants are set for `_WINDOWS64` at the top of the file:

| Constant | Value | Used for |
|---|---|---|
| `m_szSoundPath`  | `Durango\Sound\` | the sound-effect bank |
| `m_szMusicPath`  | `music\`         | streamed `.binka` music |
| `m_szRedistName` | `redist64`       | Miles provider modules |

All three are **relative to the process working directory**, not to the exe.

### `SoundEngine::init()`

1. `AIL_set_redist_directory("redist64")` — tells Miles where to find its loadable
   providers (`.flt` filters, `binkawin64.asi`).
2. `AIL_startup()`, then `AIL_open_digital_driver(44100, 16, MSS_MC_USE_SYSTEM_CONFIG, 0)`.
3. `AIL_startup_event_system(...)`.
4. `AIL_add_soundbank("Durango\Sound\Minecraft.msscmp", 0)`.

**Steps 2, 3 and 4 each bail out by calling `AIL_close_digital_driver()` +
`AIL_shutdown()` and returning.** There is no partial-audio fallback: if the
soundbank is missing, the whole Miles driver is torn down and the game is
completely silent — music included, even though music does not come from the bank.
That failure mode is the one to suspect first for "no audio at all".

### Music streaming

`playMusicTick()` builds `music/<track>.binka` (game music) or `music/cds/<track>.binka`
(records) and hands it to `AIL_open_stream()` on a worker thread
(`OpenStreamThreadProc`). Decoding `.binka` requires **`binkawin64.asi`**, which Miles
loads from the redist directory. The `music/` tree in the source lives at
`Minecraft.Client/music/{music,cds}/` and must be staged as `music/music/` +
`music/cds/` — note the doubled `music` level, which is correct.

## Required runtime layout

Next to `Minecraft.Client.exe` (and with that directory as the CWD):

```
Minecraft.Client.exe
mss64.dll                     <- import; exe will not even start without it
iggy_w64.dll                  <- import (UI/Flash), same
Durango/Sound/Minecraft.msscmp
redist64/binkawin64.asi
redist64/mss64dolby.flt
redist64/mss64ds3d.flt
redist64/mss64dsp.flt
redist64/mss64eax.flt
redist64/mss64srs.flt
music/music/*.binka
music/cds/*.binka
Common/, Windows64Media/, ...
```

Sources for the staged files, all git-tracked:

| Staged as | Source in the tree |
|---|---|
| `Durango/Sound/Minecraft.msscmp` | `Minecraft.Client/Durango/Sound/Minecraft.msscmp` |
| `redist64/*` | `Minecraft.Client/redist64/` |
| `mss64.dll`, `iggy_w64.dll` | `x64/Release/` |

### Careful: three different Miles redists are in the tree

`binkawin64.asi` and the five `.flt` files exist in **three** places, and they are
not the same files:

| Path | Sizes | Verdict |
|---|---|---|
| `Minecraft.Client/redist64/` | asi 110,080 | **use this** |
| `x64/Release/redist64/` | asi 110,080 | same version, different build |
| `Minecraft.Client/Windows64/Miles/lib/redist64/` | asi 110,592 | **different Miles version — do not use** |

`mss64.dll` reports FileVersion **9.3m**. Miles refuses to load provider modules
that do not match the driver, so the `Windows64/Miles/lib/redist64/` set — the one
that came with the SDK headers — is the wrong one to ship despite living in the most
plausible-looking directory. The first two sets have identical file sizes to each
other, so they are the same Miles version; `Minecraft.Client/redist64/` is used
because it is the per-platform packaging directory, matching how `Windows64Media/`
relates to `Windows64/`.

`mss64.dll` exists **only** under `x64/Release/`. Despite the name, that folder is
not build output — `OutDir` and `IntDir` both live under `Minecraft.Client\bin\`.
It is 4J's runtime-support staging folder and it is git-tracked.

There is also a second `iggy_w64.dll` at `Windows64/Iggy/lib/redist64/` with 201
exports versus the 203 in `x64/Release/`. Both satisfy all 41 Iggy symbols the exe
imports, so either works; the `x64/Release` copy is preferred as the one that has
been shipping. The extra two exports are `IggyFontSetFallbackFontUTF16/UTF8`.

`Effects.msscmp` is a **Miles SDK sample bank**, not a game asset — nothing loads it.
It is not a substitute for `Minecraft.msscmp`.

## No VC++ redistributable needed

`Release|x64` compiles with `RuntimeLibrary=MultiThreaded` (`/MT`), so the CRT is
linked statically. `dumpbin /dependents` on the exe confirms it: the only non-OS
imports are `mss64.dll` and `iggy_w64.dll`, and those two import nothing beyond
`KERNEL32`, `USER32` and `WINMM`. A build can be handed to someone with a stock
Windows install and nothing else.

## Staging is automatic

`Minecraft.Client/postbuild.ps1` stages everything above into `$(OutDir)` on every
x64/ARM64EC/Win32 build, and prints an explicit `INCOMPLETE BUILD` warning listing
anything still missing. `package-win64.ps1` at the repo root then mirrors `$(OutDir)`
into `Builds/Windows-x64-Release/`, stripping `.pch`/`.pdb` and local save data, and
refuses to report success if a required file is absent.

## Remaining rough edges

- The `Durango\Sound\` path on a Windows build is a 4J leftover from sharing sound
  assets with the Xbox One target. Renaming it is cosmetic but needs `m_szSoundPath`
  and the staged tree changed together.
- `SoundEngine::init()` still has no partial-audio fallback. If a future asset change
  breaks the bank, the symptom will again be *total* silence rather than missing
  effects, which is a misleading thing to have to debug twice.

## Not verified

The file layout, the dependency closure and the staging scripts were checked
statically and by running the build. **Whether the game actually produces sound has
not been confirmed** — that needs someone to run it.
