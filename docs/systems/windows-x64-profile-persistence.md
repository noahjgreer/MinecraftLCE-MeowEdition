# Windows x64 profile / settings persistence

## What holds the settings

Per-player settings do **not** live in `Minecraft.Client/Options.cpp`. That class is
the Java-derived leftover (it writes an `options.txt`-style file and is only used by
a few PC screens); the console codebase replaced it with the `GAME_SETTINGS` struct
in `Minecraft.Client/Common/App_structs.h`.

`GAME_SETTINGS` is a single 192-byte blob per pad holding volumes, sensitivity,
gamma, difficulty, control scheme, HUD/hand/clouds/fog toggles, UI size and opacity,
autosave frequency, language, selected skin and cape, favourite skins and the
tutorial-completion bits — mostly packed into `usBitmaskValues` / `uiBitmaskValues`.

Flow:

- `CMinecraftApp::InitGameSettings` (`Common/Consoles_App.cpp`) points
  `GameSettingsA[i]` at `ProfileManager.GetGameDefinedProfileData(i)` — the game
  never owns the storage, the profile library does.
- UI screens call `SetGameSettings(...)`, which sets `bSettingsChanged`.
- `CMinecraftApp::CheckGameSettingsChanged` sees the flag and calls
  `ProfileManager.WriteToProfile(pad, true, override)`.
- `CMinecraftApp::ApplyGameSettings` pushes the values back into `Minecraft::options`,
  the sound engine, `InputManager`, etc.

On console the 4J profile library persists that blob inside the platform user
profile. On x64 there is no such library: `C_4JProfile` is stubbed in
`Minecraft.Client/Extrax64Stubs.cpp`, `GetGameDefinedProfileData` returned a plain
heap buffer, and `WriteToProfile` / `ForceQueuedProfileWrites` were empty. Hence
every option reverted to its default on the next launch.

## The x64 store

[Win64ProfileStore.cpp](../../Minecraft.Client/Windows64/Win64ProfileStore.cpp)
mirrors the four blobs to `profile.dat` in the working directory (same convention as
the dedicated server's `server.properties`).

File format: a 16-byte header (magic `'M4JP'`, version, pad count, blob size) then
four raw `GAME_SETTINGS` blobs. The blob size is checked on load — if
`GAME_SETTINGS` ever changes shape the file is ignored and defaults are kept, and
the next save overwrites it. Deleting `profile.dat` resets all settings.

Wiring:

- `C_4JProfile::Initialise` applies the hard-coded x64 defaults as before, then calls
  `Win64ProfileStore::Load` over the top of them.
- `WriteToProfile` and `ForceQueuedProfileWrites` call `Win64ProfileStore::Save`
  (all four pads; the data is a few KB).
- `InitGameSettings` skips `SetDefaultOptions` for a pad that
  `Win64ProfileStore::HasSavedProfile` reports as restored — that call writes
  defaults straight into the blob and would otherwise undo the load immediately.
- `WinMain` flushes once after the message loop exits, for changes that set
  `bSettingsChanged` without anyone calling `CheckGameSettingsChanged`.

`bSettingsChanged` is the first field of the struct and is a transient dirty flag,
so it is forced back to `false` after loading.

## Gotchas

- Anything not in `GAME_SETTINGS` is still not persisted — notably keyboard/mouse
  bindings (`Win64KeyboardMouse`) and the `Options`-class fields such as view
  distance and FOV that no `SetGameSettings` enum covers.
- `Options::save()` / `Options::load()` remain broken independently of this: `save`
  omits newlines between most entries and `load` keeps the `:` in the parsed value.
  Nothing in the console path calls them for these settings, so they were left alone.
- The store keys nothing on identity — all four pad slots come back regardless of
  who is "signed in", which matches the stub's single fake profile.
