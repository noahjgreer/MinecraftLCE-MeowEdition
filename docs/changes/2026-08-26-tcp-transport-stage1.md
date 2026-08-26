# TCP transport for Windows x64 (direct connect, stage 1)

Date: 2026-08-26

## Why

Goal is a "Join Server" main-menu flow where a player enters an IP, port and
display name and connects to another instance of the Windows x64 build.

Design and staging: `docs/systems/dedicated-server-and-direct-connect.md`. Read it
first — the short version is that everything above `Socket` is already
transport-agnostic, and the transport surface is only two functions wide:

- inbound  — someone must call `Socket::pushDataToQueue()`
- outbound — `INetworkPlayer::SendData()` must put bytes somewhere

On Windows x64 neither end exists: `IQNet`/`IQNetPlayer` are empty class shells in
`Minecraft.World/x64headers/extraX64.h`, so `CPlatformNetworkManagerStub` compiles
and does nothing.

## What changed

New, and nothing else was modified except the project file:

- `Minecraft.Client/Common/Network/Sockets/TcpLink.h`
- `Minecraft.Client/Common/Network/Sockets/TcpLink.cpp`

`TcpLink` is a Winsock wrapper: blocking `Connect()`, `Adopt()` for an accepted
socket, a per-link receive thread that pumps bytes straight into
`Socket::pushDataToQueue()`, and a locked `Send()` that loops until the whole buffer
is away. `TcpListener` is a non-blocking listener so `Accept()` can be polled from
`CPlatformNetworkManager::DoWork()` on the game thread without stalling it.

Both are wrapped entirely in `#ifdef _WINDOWS64`, so the translation unit is empty
on every console target and no `ExcludedFromBuild` entries were needed. 7th gen is
untouched.

Added to `Minecraft.Client.vcxproj` as a plain `ClCompile`/`ClInclude` pair.

## Notes for whoever picks this up

- **`::bind`.** The codebase pulls in `namespace std` globally, so an unqualified
  `bind()` resolves to `std::bind` and produces a wall of template errors. The
  global-scope qualifier is load-bearing; do not "tidy" it away.
- **`ws2_32.lib` is linked via `#pragma comment(lib, ...)`** inside the guarded
  region rather than by editing the x64 link line, to keep the change off the
  shared project settings.
- **No framing was added, deliberately.** `Connection` already frames its own
  packets and TCP preserves stream order, which is the same guarantee the QNET
  calls asked for with `QNET_SENDDATA_RELIABLE | QNET_SENDDATA_SEQUENTIAL`.
- **Dead-peer handling matters.** `SocketInputStreamNetwork::read()` spins on
  `Sleep(1)` until `m_streamOpen` goes false, so `ReceiveLoop()` calls
  `Socket::close()` when `recv` returns <= 0. Without that, `Connection`'s read
  thread spins forever on a dropped peer.
- `TCP_NODELAY` is set; Nagle would coalesce the per-tick movement packets.

## Status

**Compiles clean at `Release|x64`.** Not linked — the output exe was locked by a
running instance at the time, and the link failure was `LNK1104` on that lock, not
on this code. Nothing calls `TcpLink` yet, so it is dead code until stage 1's
`NetworkPlayerSockets` / `PlatformNetworkManagerSockets` land.

**Nothing here is runtime-verified.** No connection has ever been established with
this code. That needs two instances and the owner.

## Still to do (stage 1)

- `NetworkPlayerSockets : INetworkPlayer` — `SendData()` onto a `TcpLink`, holds the
  display name and smallId.
- `PlatformNetworkManagerSockets : CPlatformNetworkManager` — model on
  `PlatformNetworkManagerStub`; own the listener when hosting, drive
  `g_NetworkManager.PlayerJoining()` + `CreateSocket()` on accept the way
  `PlatformNetworkManagerXbox` does.
- Swap the single selection point at `GameNetworkManager.cpp:75` behind
  `#ifdef _WINDOWS64`.
