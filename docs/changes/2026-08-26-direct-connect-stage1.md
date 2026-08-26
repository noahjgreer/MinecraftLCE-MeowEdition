# Direct connect over TCP for Windows x64 (stages 1-3)

Date: 2026-08-26

Follows on from `2026-08-26-tcp-transport-stage1.md`, which added `TcpLink` alone.
Design: `docs/systems/dedicated-server-and-direct-connect.md`.

## What this delivers

A Windows x64 build can now host a server on a TCP port and another can join it by
address, identified by a display name the player types. Console targets are
untouched - everything new is inside `#ifdef _WINDOWS64`.

Two ways in:

```
Minecraft.Client.exe -server 192.168.1.50 -port 25565 -name Noah
Minecraft.Client.exe -server 192.168.1.50:25565 -name "Noah G"
```

or press **F6** at the menu for the in-game "Join a Server" screen.

## The shape of it

The transport surface under `Socket` is only two functions wide, and both halves of
`Socket` were already written - the network half was simply never fed on this
platform. Nothing in `Socket.cpp`, `Connection.cpp` or any packet type was modified.

```
ConnectScreen -> ClientConnection -> Connection -> Socket
                                                    |
                            pushDataToQueue  <---  TcpLink receive thread
                            SendData         --->  TcpLink::Send
```

## Files added

| File | What it is |
|---|---|
| `Common/Network/Sockets/TcpLink.{h,cpp}` | Winsock wrapper; per-link receive thread, non-blocking `TcpListener` |
| `Common/Network/Sockets/NetworkPlayerSockets.{h,cpp}` | `INetworkPlayer` over a `TcpLink`; owns the display name and the derived UID |
| `Common/Network/Sockets/PlatformNetworkManagerSockets.{h,cpp}` | `CPlatformNetworkManager` for this platform; peer lifetime, smallIds, accept loop |
| `DirectConnectScreen.{h,cpp}` | The "Join a Server" screen - address, port, name |
| `Windows64/Win64CommandLine.{h,cpp}` | `-server` / `-port` / `-name` parsing |

## Files changed

- **`GameNetworkManager.h/.cpp`** - `_WINDOWS64` now selects
  `CPlatformNetworkManagerSockets` instead of the stub, and it is added to the
  friend list (the platform managers reach `PlayerJoining`, `CreateSocket` and
  `GetPrimaryPad`, all private).
- **`ClientConnection.cpp`** - the `(ip, port)` constructor was `assert(FALSE)` +
  `#if 0`; refilled. `handlePreLogin` now derives both XUIDs from the display name
  on `_WINDOWS64`. `tick()` guarded against a NULL connection.
- **`ConnectScreen.cpp`** - guarded the failed-connect path (see Bugs below) and
  routed cancel away from `TitleScreen`, whose `render` is entirely `#if 0` on this
  build, so returning to it would show a blank screen.
- **`Minecraft.cpp`** - restored `setScreen(new ConnectScreen(...))` at startup when
  `connectToIp` is set, applied `-name`/`-server`, added `Win64CheckDirectConnectRequest`.
- **`Win64KeyboardMouse.{h,cpp}`** - a WM_CHAR typed-character queue (text entry) and
  the F6 request flag.
- **`Windows64_Minecraft.cpp`** - `WM_CHAR` forwarding, `Win64CommandLine::Parse`.
- **`Minecraft.Client.vcxproj`** - new files; `PlatformNetworkManagerStub.cpp`
  excluded from the seven x64 configurations; `ConnectScreen.cpp` and
  `DisconnectedScreen.cpp` **un-excluded** from x64 (they had been excluded because
  nothing could reach them).

## Decisions worth knowing about

**Identity is the display name, hashed into a `PlayerUID`.**
`NetworkPlayerSockets::MakeUIDFromName` is FNV-1a over the lowercased name. The
login path is XUID-keyed throughout (`PendingConnection::handleAcceptedLogin`,
`PlayerList::getPlayerForLogin`, the ban list), so synthesising a stable UID keeps
all of it working with no changes. **This is trust-on-assertion: anyone can claim
any name.** Fine for a friends-only server; do not mistake it for authentication.

**No framing was added.** `Connection` frames its own packets and TCP preserves
stream order - the same guarantee the QNET calls asked for with
`QNET_SENDDATA_RELIABLE | QNET_SENDDATA_SEQUENTIAL`.

**The main menu could not gain a button.** `UIScene_MainMenu` maps its buttons to
named elements (`"Button1"`..`"Button6"`) inside a prebuilt Iggy/Flash asset. Adding
a seventh needs the `.gfx` re-authored, which is not possible from source. F6 is
raised in the window procedure instead, which no UI layer can swallow. The
alternative - repurposing an existing button such as Leaderboards - is available if
you would rather have a visible entry, and would need its label string changed too.

**Text entry is WM_CHAR, not the action model.** `WM_CHAR` is what applies keyboard
layout and shift state, and delivers backspace/tab/return/escape as characters, so
one queue covers the whole field. The joypad action model cannot type.

## Bugs found and fixed on the way

- **`ConnectScreen` null-dereferenced on a failed connect.** It did
  `connection->send(...)` unconditionally after construction. Console joins went
  through a platform session that had already succeeded, so "the host is simply not
  there" was never expressible. A direct connect hits it on the first typo.
- **Double connection on join.** `CGameNetworkManager::CreateSocket` builds a
  `ClientConnection` of its own when `( localPlayer && IsInGameplay() )` - the
  splitscreen join-in-progress path. `ConnectToServer` therefore stays in
  `eState_Lobby` across that call and only moves to `eState_Gameplay` afterwards.
- **`bind` resolves to `std::bind`** because the codebase pulls in `namespace std`
  globally. `::bind` in `TcpLink.cpp` is load-bearing.

## Status

**Compiles and links clean at `Release|x64`.** Link verified by building to
`-p:TargetName=Minecraft.Client.linktest` because a running instance held the real
exe; that artifact was deleted afterwards. Note that linking the `.vcxproj` directly
needs `-p:SolutionDir=<repo root>\` or `Minecraft.World.lib` is not found.

**Nothing here is runtime-verified. No connection has ever been established with
this code.** An agent cannot run the game. Everything below is unknown until you
try it.

## What to try first, and what to watch for

1. Two instances on one machine: start one, host an **online** (not local) game,
   then run the second with `-server 127.0.0.1 -name Tester`.
2. Then across the LAN, then via CONSOLE with port 25565 forwarded.

Watch for:

- **Whether hosting actually opens the port.** `StartListening()` is called from
  `HostGame()` only when `bOnlineGame` is true. If the PC build never passes that,
  the listener never opens and nothing can connect - and that is the most likely
  first failure. `app.DebugPrintf` logs "Sockets: hosting on port %d" when it works.
- **The join-in-progress path.** A peer accepted while `IsInGameplay()` goes through
  `addIncomingSocket` -> `ServerConnection::NewIncomingSocket`. A peer accepted
  before gameplay starts does not, and may need `_StartGame` to sweep them in.
- **Blocked reads on a dead peer.** `SocketInputStreamNetwork::read()` spins on
  `Sleep(1)` until the stream closes; `TcpLink::ReceiveLoop` calls `Socket::close()`
  when `recv` returns <= 0 to unwind it. If a dropped client hangs the host, this is
  where to look.
- **`Socket::getPlayer()`** resolves via `GetPlayerBySmallId` on every read and
  write. A peer removed while its socket is still live returns NULL there.

## Still open

- **Stage 4, the headless build.** Not started. `MinecraftServer` and
  `ServerConnection` live in `Minecraft.Client`, and `Socket.cpp` includes
  `..\Minecraft.Client\ServerConnection.h`, so `Minecraft.World` is not separable
  yet. Until then "the server" is a full client that happens to be hosting - which
  is what you would run on CONSOLE for now.
- The host never learns a joiner's chosen name at the `INetworkPlayer` level; peers
  are named by IP there. The name does reach the game correctly through
  `PreLoginPacket` -> `PendingConnection::name`, so player lists are right; only the
  network-layer debug output shows an address.
- No player-count or MOTD query, no discovery, no reconnect.

---

# Addendum, same day: the menu button, and why F6 never worked

The owner reported F6 did nothing. It could not have worked, and neither could any
`Screen`-based UI at the main menu. Both were replaced.

## Why no Screen can render before a game starts

`GameRenderer::render` is what calls `mc->screen->render`. In
`Minecraft::run_middle` it is only reached inside:

```cpp
for( int i = 0; i < XUSER_MAX_COUNT; i++ )
{
    if( setLocalPlayerIdx(i) )          // <- needs a local player
    {
        ...
        gameRenderer->render(timer->a, bFirst);
```

At the main menu there is no level and no local player, so `setLocalPlayerIdx`
never succeeds, `GameRenderer::render` never runs, and `mc->screen` is never
drawn. `setScreen` "worked" - the screen object existed and ticked - but nothing
put a pixel on the display.

**This is worth remembering: the entire Java-derived `Screen` UI is unavailable
until a game is running.** That is also why `TitleScreen::render` is `#if 0`.

## Why the console on-screen keyboard is not the answer either

`InputManager.RequestKeyboard` (used by `UIScene_CreateWorldMenu` for world names)
comes from the prebuilt `4J_Input.lib`, which ships without source and has no PC
implementation behind that entry point. `C_Win64Input` does not shadow it.

## What replaced them

**`Windows64/Win64TextPrompt.{h,cpp}`** - a modal native Win32 prompt (a popup with
an edit control and OK/Cancel, its own message pump, Enter/Escape accelerators). A
native window renders regardless of what the game's renderer is doing, which is the
property both other approaches lacked. It releases the mouse-look cursor capture on
the way in and clears the typed-character queue on the way out.

**The Leaderboards button is now "Join Server"** on `_WINDOWS64`, in
`UIScene_MainMenu`. This works because the label comes from `app.GetString(...)`, a
string resource, **not** from the Iggy asset - only the button's *element name*
(`"Button2"`) lives in the Flash file. So an existing button can be relabelled and
repointed freely; only *adding* a seventh is impossible. Leaderboards was the right
one to take: there is no platform leaderboard service on this build.

`RunJoinServer` prompts for address (accepting `host:port`) and name, calls
`ConnectToServer`, then hands over to the stock startup path:

```cpp
loadingParams->func = &CGameNetworkManager::RunNetworkGameThreadProc;
ui.NavigateToScene(..., eUIScene_FullscreenProgress, loadingParams);
```

which is exactly what `UIScene_LoadMenu` does. `StartNetworkGame` then sees
`IsHost()` false, picks up the local player's socket, builds the `ClientConnection`
and ticks it to completion behind the fullscreen progress scene - the same sequence
a console session join uses, with its progress UI, which *does* render at menu level.

Address and name persist for the session, so re-joining is two confirmations.

## Removed

- `DirectConnectScreen.{h,cpp}` - deleted; it could never have drawn.
- The F6 hotkey and `Win64Input::ConsumeJoinServerRequest`.
- The WM_CHAR typed-character queue was **kept** - `Win64TextPrompt` relies on the
  window procedure still forwarding characters, and clearing the queue after a
  prompt is what stops typed text leaking into the game.

## Gotcha for next time

In `UIScene_MainMenu::handlePress`, the comment says "if no sign in returned func,
assume this isn't required" - but the code below it only calls `RunAction` inside
`if (signInReturnedFunc != NULL)`. Leaving it NULL means the action never runs at
all. `RunJoinServer` is therefore invoked directly from the `case`.

## Status

**Compiles and links clean at `Release|x64`; exe rebuilt and staged.** Still not
runtime-verified - see the watch-list above, which all still applies. The most
likely remaining failure is unchanged: whether hosting passes `bOnlineGame` true so
`StartListening()` ever opens the port.
