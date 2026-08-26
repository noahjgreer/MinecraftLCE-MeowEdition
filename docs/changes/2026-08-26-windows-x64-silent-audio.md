# 2026-08-26 — Windows x64 build had no audio at all

## Symptom

The built `Minecraft.Client.exe` ran but produced no sound whatsoever — no effects,
no music.

## Cause

A **deployment** problem, not a code problem. The runnable tree
`Builds/Windows-x64-Release/` was missing two things `SoundEngine` needs:

1. `Durango/Sound/Minecraft.msscmp` — the Miles soundbank.
2. `redist64/` — the Miles provider modules. The five `.flt` files and
   `binkawin64.asi` were sitting **loose at the root** of the build folder instead of
   in the `redist64` subdirectory that `AIL_set_redist_directory("redist64")` names.

(1) is the fatal one. In `SoundEngine::init()`
(`Minecraft.Client/Common/Audio/SoundEngine.cpp`), a failed `AIL_add_soundbank()`
does not degrade gracefully — it calls `AIL_close_digital_driver()` and
`AIL_shutdown()` and returns, tearing down the entire Miles driver. So the missing
effects bank also killed `.binka` music streaming, which otherwise has nothing to do
with the bank. That is why the failure looked total rather than partial.

(2) would independently have broken music, since `.binka` decoding needs
`binkawin64.asi` loaded from the redist directory.

Both paths are resolved relative to the **process working directory**, so this also
means launching the exe from any other directory silences the game.

## What changed

No source changes. Files staged into `Builds/Windows-x64-Release/`:

- `Durango/Sound/Minecraft.msscmp` ← copied from
  `Minecraft.Client/Durango/Sound/Minecraft.msscmp` (12,276,954 B)
- `redist64/{binkawin64.asi, mss64dolby.flt, mss64ds3d.flt, mss64dsp.flt,
  mss64eax.flt, mss64srs.flt}` ← copied from
  `Minecraft.Client/Windows64/Miles/lib/redist64/`

The pre-existing loose copies at the build-folder root were left in place; they are
inert now but harmless. `mss64.dll` and `iggy_w64.dll` at the root were already
correct and must stay — the exe imports both directly.

To undo: delete `Builds/Windows-x64-Release/Durango/` and
`Builds/Windows-x64-Release/redist64/`.

## Superseded on the same day

Two things in this note were later corrected — see
`2026-08-26-windows-x64-runtime-staging.md`:

1. The `redist64` set staged here came from
   `Minecraft.Client\Windows64\Miles\lib\redist64\`, which is a **different Miles
   version** from the shipping `mss64.dll` (its `binkawin64.asi` is 110,592 bytes
   against 110,080) and would not have loaded. The correct source is
   `Minecraft.Client\redist64\`, and the build folder has been corrected.
2. The manual staging described below is now done automatically by
   `Minecraft.Client\postbuild.ps1`, and `package-win64.ps1` assembles the shareable
   tree. The "no post-build copy step" gap noted at the bottom is closed.

## Unverified

Everything here is static analysis of the file layout. **Whether the game now
actually plays audio has not been confirmed** — that needs a run. If it is still
silent, or effects work but some are missing, the next thing to try is the other
soundbank, `Minecraft.Client/DurangoMedia/Sound/Minecraft.msscmp` (12,420,314 B —
a different build of the bank, not a duplicate).

## Watch out for next

- `Minecraft.Client.vcxproj` has **no post-build copy step** for any of this. The exe
  lands in `Minecraft.Client/bin/x64/Release/`, which contains no soundbank, no
  redist folder and no DLLs — it cannot run at all from there. `Builds/` is assembled
  by hand, so the next rebuild can reintroduce exactly this bug. Adding a staging
  step is the real fix.
- `m_szSoundPath` is literally `"Durango\Sound\\"` on the Windows build — an
  inherited Xbox One path. Do not "tidy" it without moving the staged directory too.

See `docs/systems/windows-x64-audio.md` for the full subsystem description.
