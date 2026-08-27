# 2026-08-27 — Settings persist across restarts (Windows x64)

## Problem

Every option (volumes, sensitivity, difficulty, HUD toggles, skin...) reset on
relaunch. The settings live in the `GAME_SETTINGS` blob owned by the profile
library; on x64 that library is stubbed, and `C_4JProfile::WriteToProfile` /
`ForceQueuedProfileWrites` were empty function bodies, so nothing was ever written
to disk.

## Change

New `Windows64/Win64ProfileStore.{h,cpp}` mirrors the four per-pad blobs to
`profile.dat` in the working directory (16-byte header + four raw blobs, blob size
validated on load).

- `Extrax64Stubs.cpp` — `C_4JProfile::Initialise` calls `Win64ProfileStore::Load`
  after applying defaults; `WriteToProfile` and `ForceQueuedProfileWrites` now call
  `Win64ProfileStore::Save`.
- `Common/Consoles_App.cpp` — `InitGameSettings` skips `SetDefaultOptions` for a pad
  restored from disk (it writes defaults directly into the blob and would have
  wiped the load). `_WINDOWS64` path only.
- `Windows64/Windows64_Minecraft.cpp` — one flush after the message loop, for
  changes that dirty the blob without calling `CheckGameSettingsChanged`.
- `Minecraft.Client.vcxproj` — added the new `.cpp`.

Both new files are inside `#ifdef _WINDOWS64`, and the `Consoles_App.cpp` edit is
inside the existing `#if defined _WINDOWS64` branch, so console targets are
untouched.

## Verified / unverified

- Built: `MinecraftPC.sln`, `Release|x64` — links clean.
- **Unverified at runtime.** Not confirmed: that `profile.dat` appears, that values
  round-trip, or that skipping `SetDefaultOptions` leaves the dashboard
  `PROFILESETTINGS` (invert-look / southpaw seeding) in an acceptable state on a
  second launch. Worth checking invert-look and southpaw specifically.

## Watch out for

- Changing `GAME_SETTINGS` invalidates existing `profile.dat` files — by design they
  are ignored rather than misread, and defaults come back. Bump `PROFILE_VERSION` if
  a more explicit break is wanted.
- Deleting `profile.dat` is the reset switch.
- Keybindings and the `Options`-class-only fields (view distance, FOV) are still not
  persisted. See `docs/systems/windows-x64-profile-persistence.md`.
