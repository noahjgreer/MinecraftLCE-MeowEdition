# Dedicated server & direct connect (design)

Status: **stages 1-3 implemented, not runtime-verified.** Written 2026-08-26.
See `docs/changes/2026-08-26-direct-connect-stage1.md` for what actually landed,
the bugs found on the way, and what to watch for when first running it.

Goal, as set by the owner: a "Join Server" entry in the main menu where the player
types an IP, a port and a display name, and connects to a host running the same
Windows x64 build. Console (QNET/SQR) paths must keep working untouched.

## The seam

The important discovery is that **the whole game above the transport is already
transport-agnostic**, and the transport surface is only two functions wide.

```
PendingConnection / PlayerConnection / ClientConnection    <- packet handlers
                        |
                    Connection            <- framing, read/write threads
                        |
             InputStream / OutputStream
                        |
                     Socket               <- THE SEAM
                    /        \
        pushDataToQueue      SocketOutputStreamNetwork::writeWithFlags
         (inbound bytes)      (outbound bytes)
                    \        /
              INetworkPlayer::SendData
                        |
        QNET (Xbox)  |  SQR (Sony)  |  *** nothing on Windows x64 ***
```

`Connection` (`Minecraft.World/Connection.cpp`) speaks only `DataInputStream` /
`DataOutputStream`. It has no idea what carries the bytes. Everything above it —
the ~100 packet types, `PendingConnection`'s login handshake, `PlayerList`,
`ServerPlayer` — is therefore already usable over TCP with no changes.

### `Socket` is two classes wearing one hat

`Minecraft.World/Socket.cpp` has two entirely separate implementations selected by
`m_hostServerConnection` / `m_hostLocal`:

- **Local/loopback** (`SocketInputStreamLocal`, `SocketOutputStreamLocal`) — a pair
  of static `std::queue<byte>` in `s_hostQueue[2]`. This is the integrated server
  talking to the local player. It works today and is what every single-player and
  splitscreen session uses.
- **Network** (`SocketInputStreamNetwork`, `SocketOutputStreamNetwork`) — per-socket
  `m_queueNetwork[2]`. Inbound bytes are *pushed in from outside* by
  `Socket::pushDataToQueue()`; outbound bytes go out through
  `SocketOutputStreamNetwork::writeWithFlags()`, which calls
  `INetworkPlayer::SendData()`.

The network half is fully written and correct. It is inert on Windows x64 only
because nothing implements `INetworkPlayer::SendData` and nothing ever calls
`pushDataToQueue`. **We do not need to modify `Socket` at all** — we need to supply
the two ends it is already asking for.

### What is missing on x64

`Minecraft.World/x64headers/extraX64.h` declares `IQNet` / `IQNetPlayer` as empty
class shells with no implementation. `CPlatformNetworkManagerStub` is written
against those shells, so on Windows x64 the platform network layer compiles and
does nothing.

## Plan

### Stage 1 — TCP transport
New files under `Minecraft.Client/Common/Network/Sockets/`:

- `TcpLink` — a thin Winsock wrapper: connect, listen/accept, a receive thread per
  link that calls `Socket::pushDataToQueue()`, and a send path. Length-prefixed
  framing is **not** needed; `Connection` already frames its own packets and TCP
  preserves stream order, which is exactly the guarantee the QNET calls asked for
  (`QNET_SENDDATA_RELIABLE | QNET_SENDDATA_SEQUENTIAL`).
- `NetworkPlayerSockets : INetworkPlayer` — one per connected peer. `SendData()`
  writes to the peer's `TcpLink`. Identity fields (display name, smallId) live here.
- `PlatformNetworkManagerSockets : CPlatformNetworkManager` — modelled on
  `PlatformNetworkManagerStub` (646 lines, most of its 56 overrides are trivial
  `return false`). Owns the listen socket when hosting, the single outbound link
  when joining, assigns smallIds, and drives `g_NetworkManager.PlayerJoining()` +
  `CreateSocket()` on accept — the same sequence `PlatformNetworkManagerXbox` uses.

Selected for `_WINDOWS64` only; the stub stays in place for every other non-console
target so nothing else changes.

### Stage 2 — identity without Xbox Live
The login path currently identifies players by `PlayerUID` (XUID):
`PendingConnection::handleAcceptedLogin` picks `m_offlineXuid`, falling back to
`m_onlineXuid`, and `PlayerList::getPlayerForLogin` keys off it.

Per the owner's decision, identity is a **plain display name**. Approach: derive a
synthetic stable `PlayerUID` by hashing the display name, so the existing XUID-keyed
machinery (bans, UGC lists, `GetPlayerBySmallId`) keeps working unmodified, and
carry the name in the already-existing `LoginPacket::userName` /
`PreLoginPacket::loginKey`. Note `PendingConnection::handlePreLogin` already does
`name = packet->loginKey`, so the name is on the wire today.

This is deliberately not secure — anyone can claim any name. That is acceptable for
a friends-only server and should be written down as such, not quietly forgotten.

### Stage 3 — UI

**Two constraints discovered during implementation, both load-bearing:**

- **No `Screen` renders before a game starts.** `GameRenderer::render` (which calls
  `mc->screen->render`) only runs inside `if (setLocalPlayerIdx(i))` in
  `Minecraft::run_middle`. No local player at the menu means no render pass at all.
  This is why `TitleScreen::render` is `#if 0`.
- **`InputManager.RequestKeyboard` has no PC implementation.** It is the console
  OSK, living in the prebuilt `4J_Input.lib`.

So menu-level UI on this platform is either an Iggy scene or a native window.

**Superseded:** text entry used to be `Win64TextPrompt`, a modal Win32 popup.
`RequestKeyboard` now has a PC implementation, so "Join Server" asks for its
address and name inside the game and `Win64TextPrompt` has been deleted. See
`windows-text-entry.md`.

**The main menu can repurpose a button but not add one.** Labels come from
`app.GetString(...)` string resources; only element names (`"Button1"`..`"Button6"`)
live in the Flash asset. Leaderboards is now "Join Server" on `_WINDOWS64`.

Original notes, still accurate for the pieces that were reused:
Mostly already present and merely disconnected:

- `ConnectScreen(minecraft, ip, port)` exists, is compiled, and already does
  `new ClientConnection(minecraft, ip, port)` then sends a `PreLoginPacket` with
  `minecraft->user->name`.
- `ClientConnection(Minecraft*, const wstring& ip, int port)` exists but its whole
  body is `assert(FALSE)` + `#if 0`. Stage 1 refills it.
- `minecraft->user` is a `User` with a `name`, already defaulted to
  `L"Player" + <millis % 1000>` at `Minecraft.cpp:4404`.
- `JoinMultiplayerScreen` and `NameEntryScreen` are compiled and available.

So Stage 3 is: a "Join Server" button on the main menu, a small text-entry screen
for address/port/name, then hand off to the existing `ConnectScreen`.

### Stage 4 — headless build
Only worth doing once 1-3 work in a client-hosted game. Requires decoupling
`Minecraft.World` from `Minecraft.Client` (`MinecraftServer` and `ServerConnection`
currently live in the client, and `Socket.cpp` includes
`..\Minecraft.Client\ServerConnection.h`). Out of scope until direct connect works.

## Risks

- **Blocking reads.** `SocketInputStreamNetwork::read()` spins with `Sleep(1)` until
  bytes are available and only exits when `m_streamOpen` goes false. A peer that
  vanishes must close the stream or `Connection`'s read thread will spin forever.
- **`Socket::getPlayer()`** resolves through `g_NetworkManager.GetPlayerBySmallId()`
  every call, so smallId assignment has to be right before any traffic flows.
- **Non-blocking accept.** `CPlatformNetworkManager::DoWork()` is called from the
  game tick; accept must not block it.
- **Nothing here is runtime-verifiable by an agent.** Building proves it links, not
  that it connects. All of this needs the owner to actually run two instances.
