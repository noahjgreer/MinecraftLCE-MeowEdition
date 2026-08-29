# 2026-08-28 — Opening an existing world

Worlds became distinct after the previous change, but selecting one in the load list did
nothing. `anvil.log` showed the click was reaching the new code:

```
[anvil] current world set to "Mrew World"
```

…and then nothing. No `loadLevel reached`, no chunk activity, no crash.

## Why

The selection handler called only `g_NetworkManager.HostGame(...)`. That is not what
starts a game. `UIScene_CreateWorldMenu::CreateGame` shows the real sequence: after
`HostGame()` it builds a `NetworkGameInitData`, stores it in a `LoadingInputParams` with
`CGameNetworkManager::RunNetworkGameThreadProc`, and navigates to
`eUIScene_FullscreenProgress`. **That scene is what runs the server thread.** Without it,
`HostGame()` sets up hosting state and the game simply sits there.

The mask passed was wrong too: `HostGame(0, ...)` with no local users, where the create
path builds `dwLocalUsersMask` from every signed-in pad.

## Why not reuse UIScene_LoadMenu

That is the console route for loading, but it is built entirely around C4JStorage: it
pulls a save thumbnail and then the save *blob* through `StorageManager.LoadSaveData()`
before hosting. That layer is disabled on Windows, and a directory-based world has no blob
to load, so there is nothing there to reuse.

`OpenAnvilWorld()` therefore mirrors `CreateGame`'s tail instead, minus world creation.

## What is passed, and what is not

`Level::Level()` does:

```cpp
levelData = levelStorage->prepareLevel();
isNew = levelData == NULL;
...
if (levelData == NULL) levelData = new LevelData(levelSettings, levelName);
else                   levelData->setLevelName(levelName);
```

So for a world that exists, the stored `level.dat` supplies seed, world size, spawn,
time and weather, and the `LevelSettings` built from the init data is discarded. Passing
`seed = 0` and default sizes is therefore harmless.

The **game type is the exception**: `MinecraftServer::initServer` reads it from the host
options before any of that, so `OpenAnvilWorld` loads the world's `level.dat` up front via
the new `AnvilLevelStorageSource::loadLevelDataFor()` and sets
`eGameHostOption_GameType` from it. Otherwise a survival world would open in whatever mode
was last used.

## Also fixed: the log was 807 KB

`level.dat` is rewritten on every autosave, and each write logged a line. It now logs the
first write for a given world and any failure. Chunk load tracing was added (capped at
three lines) since the load path had none.

## Verified

- `MinecraftPC.sln` `Release|x64` builds clean, lib and exe.
- The `Level::Level()` precedence above was read directly, not assumed — it is what makes
  passing a zero seed safe.
- `m_iDefaultButtonsC` index arithmetic re-confirmed.
- Escape audit clean.

## Unverified

Not run. `OpenAnvilWorld` is a faithful copy of the create path's tail, but it has never
executed, and the surrounding scene machinery (`FullscreenProgress`, the completion data,
`FakeLocalPlayerJoined`) cannot be exercised here.

If a world still fails to open, `anvil.log` will now distinguish the cases: a
`prepareLevel ... -> loaded` line means the world was found and read, and
`loaded chunk (x,z)` lines mean the rewritten chunk loader ran.
