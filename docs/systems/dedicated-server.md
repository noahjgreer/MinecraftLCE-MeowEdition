# Dedicated server (Windows x64)

Status: **implemented and headless.** 2026-08-27. The server runs silent, with no
window, no UI and no rendering; it accepts TCP connections, takes `stop` on stdin,
saves and exits cleanly.

**World persistence does NOT work, and the reason is now known:**
`StorageManager.Init(...)` sits inside an `#if 0` in `Windows64_Minecraft.cpp`
(line 945, block 908-955), so `StorageManager` is never initialised on this
platform at all. The save blob is built and written nowhere. This affects client
saving on Windows x64 equally - it is not a server-specific bug.

Read `docs/changes/2026-08-27-headless-dedicated-server.md` first; it supersedes
much of the detail below. `docs/changes/2026-08-26-dedicated-server.md` has the
original bring-up.

Goal: `Minecraft.Client.exe -dedicated -world "Server World" -port 25565` runs a
headless, logging, world-saving server that friends join by address.

Prerequisite reading: `docs/systems/dedicated-server-and-direct-connect.md`.

## The good news: the server does not need a player

The obvious reading of `MinecraftServer` is that it is hopelessly welded to the
client - it lives in `Minecraft.Client`, and `Socket.cpp` even includes
`..\Minecraft.Client\ServerConnection.h`. That coupling is real but it is a
*build-time* coupling, not a runtime one.

`CGameNetworkManager::StartNetworkGame` does two jobs braided together:

```cpp
if( g_NetworkManager.IsHost() )
{
    ServerStoppedCreate(true);
    ServerReadyCreate(true);
    C4JThread* thread = new C4JThread(&CGameNetworkManager::ServerThreadProc, ...);
    thread->Run();          // <-- job 1: the server
    ServerReadyWait();
}
...
connection = new ClientConnection(minecraft, NULL);   // <-- job 2: the host's own player
connection->send( new PreLoginPacket(minecraft->user->name) );
do { connection->tick(); Sleep(50); } while ( ... );
```

**Job 1 is the entire server.** `ServerThreadProc` sets up its own thread-local
pools (`AABB`, `Vec3`, `IntCache`, `Tile`) and calls `MinecraftServer::main`, which
owns the levels, the `ServerConnection` accept path, the tick loop and saving. It
listens and accepts and saves whether or not any local player exists.

Job 2 is just the hosting machine joining its own server. Drop it and you have a
dedicated server with **no ghost player occupying a slot**.

`CGameNetworkManager::StartDedicatedServer(LPVOID lpParameter)` is job 1 alone,
plus `_StartGame()` (which the host's join would normally have triggered) and
`app.SetGameStarted(true)`.

## Client dependencies that survive

`MinecraftServer.cpp` still reaches into the client, but far less than feared:

| What | Uses | Notes |
|---|---|---|
| `app.*` | ~50 | Config and game-host options, not rendering |
| `Minecraft::GetInstance()->progressRenderer` | 9 | Save progress reporting |
| `Minecraft::GetInstance()->gameRenderer` | 1 | `DisableUpdateThread()` only |
| `Minecraft::GetInstance()->options` | 1 | `difficulty` |
| `StorageManager` / `ProfileManager` | ~8 | Prebuilt 4J libs |

So a `Minecraft` instance must still exist and be initialised - `progressRenderer`
and `gameRenderer` are created in `Minecraft::init()`. The plan is therefore **not**
to strip the client out, but to run the normal startup with the window hidden and
branch to the server instead of the main menu.

That is cheaper than it sounds, because of a fact established earlier:
`GameRenderer::render` only runs inside `if (setLocalPlayerIdx(i))`. **With no local
player, the game renders nothing anyway** - the render path costs almost nothing on
a dedicated server without any special-casing.

## CORRECTION: the world storage is not plain files

An earlier revision of this document claimed persistence came free because the load
path is filesystem-shaped. That was wrong - see
`docs/changes/2026-08-26-dedicated-server.md`. `loadLevel` wraps a
`ConsoleSaveFileOriginal` blob in `McRegionLevelStorage`, and `StorageManager` writes
that blob out. Also, `Settings` was a stub, so `server.properties` was never read at
all until this fork implemented it. The original (wrong) reasoning follows.

## The second piece of good news: the server's world storage is file-based

The fear was that world persistence would mean driving `StorageManager`
(`4J_Storage.lib`, prebuilt, callback-driven, normally pumped by the UI scenes)
headless. It does not, at least not for loading.

`MinecraftServer::initServer` is still, underneath 4J's changes, the Java dedicated
server:

```cpp
settings = new Settings(new File(L"server.properties"));
...
wstring levelName = settings->getString(L"level-name", L"world");
...
m_bLoaded = loadLevel(new McRegionLevelStorageSource(File(L".")), levelName, seed, ...);
```

**`server.properties` is already read, and the world is McRegion files relative to
the working directory** - exactly like `minecraft_server.jar`. `StorageManager`'s
save slots are the *client's* layer on top, not the server's own storage.

Settings already honoured from `server.properties`:
`level-name`, `gamemode`, `max-build-height`, `spawn-animals`, `spawn-npcs`.

So a dedicated server does not need a save slot chosen for it. It needs a working
directory and a `server.properties`.

**The one remaining uncertainty is the save path, not the load path.** The tick loop
calls both `level->save(true, progressRenderer)` and
`levels[0]->saveToDisc(progressRenderer, ...)`, and it is not yet established which
of those writes region files and which writes the console save blob through
`StorageManager` (`StorageManager.GetSaveDisabled()` is consulted). The first real
run answers this: if `<level-name>/` fills with `region/` files, persistence works
as-is.

## The driver (implemented)

1. `Windows64/Win64DedicatedServer.{h,cpp}` - `AllocConsole`, logging, orchestration.
2. `_tWinMain`: when `-dedicated`, pass `SW_HIDE` to `InitInstance` so no game
   window appears. D3D still initialises against the hidden window; that is fine and
   costs nothing, because with no local player `GameRenderer::render` never runs.
3. Write `server.properties` from the command line if absent (`level-name` from
   `-world`), then after `Minecraft::init()` call
   `g_NetworkManager.HostGame(mask, /*bOnlineGame*/ true, /*bIsPrivate*/ false, ...)`
   followed by `StartDedicatedServer(param)`. Do **not** call `FakeLocalPlayerJoined`.
4. Log joins, leaves, chat and saves to the console.
5. `SetConsoleCtrlHandler` for Ctrl+C -> `MinecraftServer::HaltServer()` and wait for
   the save to finish.

## Command line (parsed; driver not wired)

```
-dedicated                 run as a server
-world <name>              save name (default "Server World")
-seed <n>                  seed for a newly created world
-port <n>                  listen port (default 25565)
-maxplayers <n>            capped at MINECRAFT_NET_MAX_PLAYERS
```

## Watch out

- `HostGame(..., bOnlineGame, ...)` must be **true** or
  `CPlatformNetworkManagerSockets::StartListening()` is never called and nothing can
  connect.
- `g_NetworkManager.DoWork()` - which is what accepts connections - is called from
  the Windows64 main loop (`Windows64_Minecraft.cpp`), not from `Minecraft.cpp`. The
  dedicated path must keep that loop running.
- Do **not** call `FakeLocalPlayerJoined()` on a dedicated server.
- `CPlatformNetworkManagerSockets::HostGame` still creates a local/host
  `INetworkPlayer`. That is deliberate and must stay: `Socket`'s outbound path does
  `hostPlayer->SendData(socketPlayer, ...)`, so a host identity object has to exist.
  It simply never gets a `ClientConnection`, so it never becomes a player in the world.
- `GameNetworkManager.h` has a long implicit `public:` region. Adding an access
  specifier in the middle of it silently makes the rest of the class private and
  breaks `Minecraft.World` (which touches `IsHost`, `GetHostPlayer`,
  `GetPlayerBySmallId`). This already happened once.
