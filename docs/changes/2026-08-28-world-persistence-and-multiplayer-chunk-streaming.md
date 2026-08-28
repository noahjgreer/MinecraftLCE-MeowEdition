# Dedicated server: world persistence, and the second player's world stream

Date: 2026-08-28

Two separate server bugs, both root-caused and fixed.

1. The world never persisted on Windows x64.
2. With more than one player connected, only one of them received the world;
   the others got a single chunk and then nothing.

Follows `2026-08-27-headless-dedicated-server.md`, which found (1)'s root cause
but did not fix it.

---

## 1. World persistence

### The problem

`StorageManager` (`C4JStorage`, from the prebuilt `4J_Storage.lib`) is never
initialised on this platform: `StorageManager.Init(...)` sits inside an `#if 0`
spanning lines 908-955 of `Windows64/Windows64_Minecraft.cpp`. It is inside that
block because the Win64 build of the library takes a **different `Init`
signature** from the Xbox one the call was written against - seven parameters
including a save-pack name and a group ID, versus the five in the call.

The consequence, in `ConsoleSaveFileOriginal`:

- **Loading**: `StorageManager.GetSaveSize()` always answered 0, which the
  constructor reads as "there is no save", so every launch generated a brand new
  world.
- **Saving**: `Flush()` built the blob correctly - `Save data compressed from
  4875608 to 2689037` is in the log - allocated the output buffer through
  `StorageManager.AllocateSaveData()`, and handed it to
  `StorageManager.SaveSaveData()`, which dropped it. Nothing was ever written.

This was never server-specific; client saving on Windows x64 was equally dead.

### What the save actually is

Worth stating plainly, because an earlier revision of
`docs/systems/dedicated-server.md` got this wrong and the header comment in
`Win64DedicatedServer.h` still repeated it: **the world is not a directory of
McRegion files.** `McRegionLevelStorage` is a *view* over a single
`ConsoleSaveFile` blob held in memory (`pvSaveMem`, a `VirtualAlloc` reserve).
`File(L".")` in the `McRegionLevelStorage` constructor is not a directory the
world lives in.

The blob's on-disk form is fixed by `ConsoleSaveFileOriginal`:

```
[int32  0]              save version / "this is compressed"
[int32  N]              decompressed size
[deflate stream]        the world
```

`Flush()` produces exactly those bytes, and the constructor parses exactly those
bytes. So a plain file round-trips through the existing code with no format
change at all.

### The fix

Rather than bring up `4J_Storage.lib` - callback-driven, VS2012, expecting a
signed-in profile, a storage device and console save-slot semantics, and flagged
in CLAUDE.md as a cross-CRT STL risk - the blob now goes to an ordinary file.
`docs/changes/2026-08-27-headless-dedicated-server.md` suggested this route and
it is the one taken.

**New: `Minecraft.Client/Windows64/Win64SaveFile.{h,cpp}`** - `SetWorldName`,
`GetSavePath`, `GetSize`, `Read`, `Write`. Nothing more; it is a replacement for
one job, not a storage layer.

- Saves land at `<level-name>/savegame.dat`, relative to the working directory,
  where `level-name` comes from `server.properties` on a dedicated server.
- `Write` goes to `savegame.dat.tmp`, `FlushFileBuffers`, then
  `MoveFileEx(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`. An
  interrupted save cannot leave a truncated world where a good one used to be.
- `GetSize()` returning 0 still means "no save, generate a new world", so
  first-run behaviour is unchanged.

**`Minecraft.World/ConsoleSaveFileOriginal.cpp`** - three `#ifdef _WINDOWS64`
branches, all alongside the existing per-platform arms:

- constructor: `Win64SaveFile::GetSize()` instead of `StorageManager.GetSaveSize()`
- constructor: `Win64SaveFile::Read()` instead of `StorageManager.GetSaveData()`
- `Flush()`: `Win64SaveFile::Write()` instead of `SetSaveImages` + `SaveSaveData`

`Flush()` also now allocates its own compression buffer with `new byte[]` and
deletes it, because `StorageManager.AllocateSaveData()` was handing back memory
from an uninitialised manager. That side-steps the pre-calculate-the-compressed-
size path (the allocation always succeeds), which costs a slightly larger
transient buffer and nothing else.

Thumbnail and metadata are gathered above the branch and simply unused on
Windows - they are console save-UI concepts and there is no save slot to attach
them to.

**`Minecraft.Client/MinecraftServer.cpp`** - `loadLevel` calls
`Win64SaveFile::SetWorldName(name)` before any `ConsoleSaveFile` is constructed.
This is the one place that knows the level name on both the client and the
dedicated server, and it must run first because the constructor is what reads
the blob back in.

### Autosave

A second gap: nothing on a dedicated server ever *triggered* a save. The client's
autosave is driven from the pause menu / XUI, which is not running headless, so
the only save was the one on shutdown - a crash cost the entire session.

`Win64DedicatedServer` now queues a save every 5 minutes
(`AUTOSAVE_INTERVAL_MS`), and `save` / `save-all` on the stdin console queues one
on demand. Both go through `app.SetXuiServerAction(pad,
eXuiServerAction_AutoSaveGame)`, which `MinecraftServer`'s tick loop drains and
performs **on the server thread** - the only thread allowed to walk the levels.
Doing the save inline from `Tick()` would race the tick.

Note `eXuiServerAction_AutoSaveGame` falls through to `eXuiServerAction_SaveGame`
on this platform: the dedicated autosave arm in `MinecraftServer.cpp` is
`#if defined(_XBOX_ONE) || defined(__ORBIS__)`. Both arms write the world, so the
fall-through is correct, just noisier (it broadcasts `UpdateProgressPacket`).

---

## 2. Only one player received the world

### The problem

`ServerPlayer::doChunkSendingTick` will not send a chunk unless

```cpp
MinecraftServer::canSendOnSlowQueue(connection->getNetworkPlayer())
```

which is

```cpp
if( player->GetSessionIndex() == s_slowQueuePlayerIndex && ... ) return true;
```

`s_slowQueuePlayerIndex` is advanced by `MinecraftServer::cycleSlowQueueIndex()`,
which walks the player list with `g_NetworkManager.GetPlayerByIndex()` and
`%= GetPlayerCount()`. **It can therefore only ever hold a value in
`[0, GetPlayerCount())` - a position in the platform manager's player array.**

`NetworkPlayerSockets`'s constructor did:

```cpp
m_sessionIndex = smallId;
```

`smallId` comes from `AllocateSmallId()`, which is **1-based and never reused**
(`return m_nextSmallId++`), while positions in `m_players` are 0-based and
compact when someone leaves. The two index spaces are unrelated.

On a dedicated server the host identity object takes `smallId` 1 and array slot
0, the first joiner gets `smallId` 2 / slot 1, the second joiner `smallId` 3 /
slot 2. With three entries the rotation produces `{0, 1, 2}`, skipping slot 0
because the host is `IsLocal()`. Session index **3 never comes up**, so the
second joiner's `canSendOnSlowQueue` is false on every tick, forever.

The one chunk it did receive is the login/teleport path in `PlayerList`, which
calls `doTick(true, /*dontDelayChunks*/ true, ...)` and bypasses the gate
entirely - hence "one chunk and nothing else loads".

This is a fork bug, not a 4J bug: on QNet and SQR the session index *is* the slot
`GetPlayerByIndex` addresses. The sockets transport was the layer that broke the
contract.

### The fix

Per the layering rule in CLAUDE.md, the fix is in the platform implementation,
not in the shared `MinecraftServer` code.

- `CPlatformNetworkManagerSockets::ReindexPlayers()` (new, private) stamps every
  player with its position in `m_players`, and is called under `m_playersLock`
  from both `AddPeer` and `RemovePeer` - positions shift when someone leaves, so
  it has to run on both.
- `NetworkPlayerSockets`'s constructor now defaults `m_sessionIndex` to **-1**,
  not `smallId`. A player the manager has not indexed yet must not accidentally
  match index 0.

`GetSessionIndex()` has exactly one consumer in the entire codebase -
`canSendOnSlowQueue` - so re-purposing it into the `GetPlayerByIndex` index space
has no other effect.

---

## Files changed

- `Minecraft.Client/Windows64/Win64SaveFile.{h,cpp}` - **new**.
- `Minecraft.World/ConsoleSaveFileOriginal.cpp` - `_WINDOWS64` load/save branches,
  own the compression buffer.
- `Minecraft.Client/MinecraftServer.cpp` - `Win64SaveFile::SetWorldName` in
  `loadLevel`, plus the include.
- `Minecraft.Client/Windows64/Win64DedicatedServer.{h,cpp}` - `RequestSave()`,
  `save` console command, 5-minute autosave, corrected header comment.
- `Minecraft.Client/Common/Network/Sockets/NetworkPlayerSockets.cpp` -
  `m_sessionIndex` defaults to -1.
- `Minecraft.Client/Common/Network/Sockets/PlatformNetworkManagerSockets.{h,cpp}`
  - `ReindexPlayers()`.
- `Minecraft.Client/Minecraft.Client.vcxproj` - `Win64SaveFile.cpp`.

No console-target files were touched. Every change in shared code is inside an
existing per-platform branch or in the sockets transport, which is `_WINDOWS64`-
only.

## Verified

- **Built.** `MinecraftPC.sln` at `Release|x64` on VS2022, clean, links.
  `ConsoleSaveFileOriginal.obj` and `Win64SaveFile.obj` both recompiled.

## NOT verified - this is the owner's to confirm

Runtime behaviour cannot be checked by an agent. Nothing below has been observed:

- That a world actually round-trips. The thing to look for is
  `Win64SaveFile: wrote N bytes to <level-name>\savegame.dat` in the log on
  shutdown, then that same world reappearing on the next launch rather than a
  freshly generated one. `Filesize - N, Adjusted size - M` on startup means the
  blob was read.
- That both players now stream the world. Two clients connected simultaneously,
  both walking, both loading terrain.
- Whether the autosave interval is sensible. 5 minutes was chosen, not measured;
  the save is synchronous on the server thread and will stall the tick for its
  duration. If it hitches noticeably, raise the interval.
- Whether `saveAll` of player data behaves with more than one remote player.

## Watch out

- **`Flush()` is reached only if several gates pass**, all of which happen to be
  satisfied today but none of which were designed for a headless server:
  `StorageManager.GetSaveDisabled()` in `ServerLevel::saveToDisc`, and
  `ProfileManager.IsSignedIn(GetPrimaryPad()) && IsFullVersion() &&
  !StorageManager.GetSaveDisabled()` in `MinecraftServer::stopServer`. These read
  an **uninitialised** `StorageManager`, so they are answering from zeroed global
  memory rather than from a real decision. If saving mysteriously stops, check
  these first.
- The save path is `<level-name>/savegame.dat`. Changing `level-name` in
  `server.properties` silently starts a new world rather than failing.
- `ReindexPlayers()` changes a player's session index when an *earlier* player
  disconnects. Nothing caches it today, but anything that starts caching a
  session index across ticks would be wrong.
