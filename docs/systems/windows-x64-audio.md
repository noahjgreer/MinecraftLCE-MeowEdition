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

Sources for the staged files:

- `Minecraft.msscmp` — `Minecraft.Client/Durango/Sound/Minecraft.msscmp` (12,276,954 B).
  Two other candidates exist and are **not** byte-identical:
  `Minecraft.Client/DurangoMedia/Sound/Minecraft.msscmp` (12,420,314 B, the packaged
  Durango media copy) and `Minecraft.Client/Minecraft.msscmp` (11,622,790 B). The
  `Durango/Sound/` one was chosen because the code's relative path names that exact
  directory. If sounds are missing or wrong, try the `DurangoMedia` copy next.
- redist modules — `Minecraft.Client/Windows64/Miles/lib/redist64/`.
- `mss64.dll` — shipped in `Builds/Windows-x64-Release/`; there is no copy under
  `Minecraft.Client/Windows64/Miles/lib/`, so do not delete it.

`Effects.msscmp` at the root of the build folder is a **Miles SDK sample bank**, not a
game asset — nothing loads it. Harmless, but it is not a substitute for
`Minecraft.msscmp`.

## Known gaps

- **No post-build staging step.** `Minecraft.Client.vcxproj` puts the exe in
  `Minecraft.Client/bin/x64/Release/` and copies none of the above. The runnable tree
  in `Builds/Windows-x64-Release/` is assembled by hand, which is exactly how the
  soundbank went missing. A post-build copy (or a staging script) would prevent a
  repeat.
- `AIL_set_redist_directory` / `m_szSoundPath` being CWD-relative means launching the
  exe from anywhere other than its own folder silences the game. Making these
  exe-relative (`GetModuleFileName` + strip) would be the robust fix.
- The `Durango\Sound\` path on a Windows build is a 4J leftover from sharing assets
  with the Xbox One target. Renaming it is cosmetic but would need the constant and
  the staged tree changed together.

## Not verified

Only the file layout was checked, statically. Whether Miles now initialises and the
game actually produces sound has to be confirmed by running it.
