# Dedicated server mode for Windows x64

Date: 2026-08-26

Follows `2026-08-26-direct-connect-stage1.md`. Background and the two findings that
made this tractable: `docs/systems/dedicated-server.md`.

## What this delivers

```
Minecraft.Client.exe -dedicated [-world <name>] [-port <n>] [-seed <n>] [-maxplayers <n>]
```

A hidden-window, console-logging server that creates or reloads a world on disk and
accepts direct-connect clients. Ctrl+C saves and stops.

## Why it was much smaller than expected

**The server never needed a local player.** `StartNetworkGame` braids two jobs
together - starting the server thread, and putting the hosting machine's own player
into the resulting world. `ServerThreadProc` -> `MinecraftServer::main` is entirely
self-contained. `CGameNetworkManager::StartDedicatedServer` is job one alone, so no
player slot is spent on the host.

**The world storage was already file-based.** `MinecraftServer::initServer` reads
`server.properties` and `loadLevel` uses `McRegionLevelStorageSource(File(L"."))` -
the Java dedicated server's own arrangement, still intact under 4J's changes. No
`StorageManager` save-slot machinery was needed; that is the *client's* layer.

## Files added

- `Windows64/Win64DedicatedServer.{h,cpp}` - console, logging, orchestration, Ctrl+C.

## Files changed

- **`GameNetworkManager.{h,cpp}`** - added `StartDedicatedServer`.
- **`Win64CommandLine.{h,cpp}`** - `-dedicated`, `-world`, `-seed`, `-maxplayers`.
- **`Windows64_Minecraft.cpp`** - console up before anything logs; `nCmdShow = SW_HIDE`.
- **`Minecraft.cpp`** - `Start()` at the end of `init()`, `Tick()` in the frame loop.
- **`Common/Consoles_App.cpp`** - `DebugPrintf` mirrored to the console in dedicated
  mode.

## The logging is mostly free

`CMinecraftApp::DebugPrintf` was already writing the interesting things - level
load, the `*** SERVER SETTINGS ***` block, `Sockets: added peer ...` - to
`OutputDebugStringA`, where only a debugger could see them. `_FINAL_BUILD` is **not**
defined in `Release|x64`, so those calls are live. Mirroring that one function to
stdout surfaced the whole existing trace with no new logging code.

## Notes

- **`bOnlineGame` must be true** in the `HostGame` call or
  `CPlatformNetworkManagerSockets::StartListening()` is never reached and nothing can
  connect. This is the single most likely cause if the server appears up but refuses
  connections.
- **`FakeLocalPlayerJoined()` is deliberately not called.**
- **The host `INetworkPlayer` still exists** and must: `Socket`'s outbound path does
  `hostPlayer->SendData(socketPlayer, ...)`. It simply never gets a
  `ClientConnection`, so it never becomes a player in the world. It *is* counted by
  `GetPlayerCount()`, which is why `Tick()` subtracts one for the status line.
- **`AttachConsole(ATTACH_PARENT_PROCESS)` before `AllocConsole`** so running from an
  existing terminal logs into that terminal rather than opening a second window.
- **Ctrl+C does not save on the handler thread.** It sets a flag; `Tick()` does the
  work on the game thread. `CTRL_CLOSE_EVENT` blocks in the handler (up to ~10s)
  because Windows kills the process shortly after that one returns.
- `GameNetworkManager.h` has a long implicit `public:` region. Adding an access
  specifier inside it silently makes the rest private and breaks `Minecraft.World`.
  This happened once while adding `StartDedicatedServer`.

## Status

**Compiles and links clean at `Release|x64`; exe rebuilt and staged.**

**Not runtime-verified.** No server has been started with this code. In particular:

1. **Does the world actually persist?** The load path is confirmed file-based. The
   *save* path is not: the tick loop calls both `level->save(true, progressRenderer)`
   and `levels[0]->saveToDisc(progressRenderer, ...)`, and which of those writes
   region files versus the console save blob through `StorageManager` is still
   unestablished. **First thing to check: does `<level-name>/` appear in the working
   directory with `region/` inside it?** If yes, persistence works as-is. If not, the
   save path needs the same treatment the load path turned out not to need.
2. **`progressRenderer` is called from the server thread** during level load. That is
   how it works on console too, but this build has never done it without a visible
   window, and `ProgressRenderer` touches D3D.
3. **The UI still runs.** The intro and main menu scenes still tick behind the hidden
   window; they were left alone deliberately to avoid disturbing startup. If they
   cause trouble, skipping `UIScene_Intro`'s navigation is the place to start.

## Trying it

```
cd Minecraft.Client\bin\x64\Release
Minecraft.Client.exe -dedicated -world TestWorld -port 25565
```

Then from another machine (or the same one), launch the client normally and use
**Join Server** on the main menu, or:

```
Minecraft.Client.exe -server <host>:25565 -name Noah
```

The working directory matters: the world is written relative to it, and the runtime
staging already pins the working directory to the exe folder
(`setWorkingDirectoryToExe`).

---

# Addendum: runtime debugging, and two corrections

The first run crashed. Diagnosing it turned up a bug that also broke the *client*
direct-connect work, and disproved two claims made above. Corrected below; the text
before this line is left as written so the reasoning is still visible.

## Correction 1: `g_NetworkManager.DoWork()` was never called on Windows x64

`Windows64_Minecraft.cpp` has two calls. One was commented out; the other is inside
an `#if 0` block. **So the platform network manager was never serviced on this
platform at all.**

For the sockets transport `DoWork()` *is* the accept loop
(`AcceptPendingConnections` / `DropDeadPeers`). A host would open its listening
socket and then never accept anybody. This affected the client "Join Server" work
from the previous change just as much as the dedicated server - it could never have
worked. The live call is now enabled.

## Correction 2: the world is NOT plain McRegion files on disk

Stated earlier that persistence would come free because `loadLevel` uses
`McRegionLevelStorageSource(File(L"."))`. That is only half the story - inside
`loadLevel` the storage is built as:

```cpp
storage = shared_ptr<McRegionLevelStorage>(
    new McRegionLevelStorage(new ConsoleSaveFileOriginal( L"" ), File(L"."), name, true));
```

McRegion layout wrapped around a **console save blob held in memory**, which
`StorageManager` writes out. The log line `Save data compressed from 4871479 to
2681805` is that blob. So the save path does go through `StorageManager` after all;
only the *load* is filesystem-shaped. Persistence across restarts is therefore still
the open question - see Status.

## Correction 3: `Settings` was a stub, so `server.properties` was never read

`Settings::Settings(File*)` had an empty body and `saveProperties()` did nothing.
Every `getString`/`getInt`/`getBoolean` fell straight through to its default. The
server always loaded the level called `"world"` no matter what it was told, and
`-world` did nothing.

**`Settings` is now implemented** - `key=value`, `#`/`!` comments, whitespace
trimmed, `true`/`false` accepted as well as `0`/`1` (`_fromString<bool>` only reads
the latter, so every `true` in a properties file would have read as false). It
writes back, so a first run produces a complete file:

```
#Minecraft server properties
gamemode=0
max-build-height=256
level-type=default
spawn-animals=true
spawn-npcs=true
level-name=TestWorld
spawn-monsters=1
max-players=20
allow-nether=1
```

`-world` rewrites `level-name` in place, preserving other hand-edited keys. Without
`-world`, whatever is in the file wins.

## The crash, and where the server must be started from

`0xC0000005` in `new ServerLevel(...)` for level 0. Two placement problems:

1. **`Minecraft::init()` is too early.** The stock path reaches the server from a UI
   scene long after the game is up. Starting inside `init()` faults in the
   `ServerLevel` constructor.
2. **`Minecraft::run_middle()` is too late** - it is only called
   `if(app.GetGameStarted())`, and on a dedicated server nothing sets that until the
   server has started. Chicken and egg.

The server is now started from `Win64DedicatedServer::Tick()`, called from the live
message loop in `Windows64_Minecraft.cpp` **outside** the `GetGameStarted()` gate,
gated on `Minecraft::GetInstance()->progressRenderer` existing plus a 60-frame
settle. That works.

## Logging

`server.log` is written next to the exe, flushed per line so a crash still leaves the
trail, and opened with `_fsopen(..., _SH_DENYWR)` so it can be tailed while running.
stdout is left alone when it is already redirected, so `> out.txt` still works.

Coarse breadcrumbs were added to `MinecraftServer::initServer` / `loadLevel` at the
points where 4J had commented-out `logger.info` calls. They are what located the
crash and are worth keeping.

## What is now verified at runtime

Actually observed, not inferred:

- Server starts, generates a world, and stays up.
- `Server ready, listening on port 25565`.
- **A TCP client connecting to 25565 is accepted**: `Sockets: added peer "127.0.0.1"
  smallId 2`, `Players online: 1/8`.
- **Disconnect is detected**: `Sockets: removing peer`, `Players online: 0/8`.
- `-world TestWorld` is honoured end to end.

## What is still NOT verified

- **Whether the world persists across restarts.** The save blob is built in memory
  but is only written on save/shutdown, and the test runs were hard-killed. **Start
  the server, press Ctrl+C, and check whether a save appears and reloads.** This is
  the last unknown of substance.
- **A real Minecraft client joining.** Only a raw TCP socket has been tested, which
  proves the accept path but not the login handshake, world transfer or gameplay.
