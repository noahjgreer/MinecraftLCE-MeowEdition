# Client hang on the 4J Studios logo

## Symptom

Running `Minecraft.Client.exe` with no arguments hung on the 4J Studios boot logo
and never reached the main menu. No error, no output in `server.log` or the
console. Introduced by `b1744ae2` ("Server testing").

## Cause

`Windows64_Minecraft.cpp` calls `Win64DedicatedServer::Tick()` from the frame
loop unconditionally - deliberately, since it is outside the
`app.GetGameStarted()` gate so a dedicated server can start before the game does.

But `Win64DedicatedServer::Tick()` did not check `IsEnabled()`. Every other entry
point in that file either checks it (`Initialise()` at its call site) or is inert
without it (`LogRaw` guards on `s_bLogLockReady`). `Tick()` did not, so on a plain
client launch it:

1. waited for `Minecraft::GetInstance()->progressRenderer` to exist,
2. counted `DEDICATED_START_DELAY_FRAMES` (60) frames - which lands while the
   4J logo is on screen,
3. called `Start()`, which wrote `server.properties`, set the host options,
   called `g_NetworkManager.HostGame(...)` (opening a listen socket) and then
   `StartDedicatedServer(param)`,
4. blocked the main thread in `StartDedicatedServer`'s `ServerReadyWait()` while
   the server thread loaded/generated a world, and called
   `app.SetGameStarted(true)` with no local player in existence.

It was silent because `Initialise()` only runs on a real server launch, so
`s_bLogLockReady` was false and every `Log()` call returned immediately - the
console was never allocated and `server.log` was never opened.

## Fix

One guard at the top of `Win64DedicatedServer::Tick()`:

```cpp
if (!IsEnabled()) return;
```

`IsEnabled()` is `Win64CommandLine::WantsDedicatedServer()`, so the whole
dedicated-server path is now inert unless `-server` was passed.

## Files

- `Minecraft.Client/Windows64/Win64DedicatedServer.cpp`

## Verified / unverified

- Built: `MinecraftPC.sln` at `Release|x64` links clean (warnings unchanged).
- **Runtime not verified by the agent.** The owner should confirm that a
  no-argument launch now reaches the main menu and that `-server` still starts.

## Watch out for

- A buggy client run may have left a `server.properties` and a world directory in
  `Minecraft.Client/bin/x64/Release/`. They are harmless but were not asked for.
- The same class of bug applies to anything else added to the frame loop for the
  server: gate on `Win64DedicatedServer::IsEnabled()` at the callee, not only at
  the call site, since the call site is intentionally unconditional.
