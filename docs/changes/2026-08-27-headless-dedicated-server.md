# Headless dedicated server: silence, no UI, stdin console, clean shutdown

Date: 2026-08-27

Follows `2026-08-26-dedicated-server.md`. That change made `-dedicated` start a
server; this one makes it behave like a server rather than like a muted client.

## The problem

`-dedicated` only did two things: attach a console, and pass `SW_HIDE`. Everything
else in the client boot path still ran. Concretely, the "server" was:

- initialising Miles and loading soundbanks,
- running a full `eUIScene_Intro` -> main menu scene stack behind the hidden window
  (`UIController::init` ends with `NavigateToScene(0, eUIScene_Intro)`),
- **playing the menu music to nobody** - via `soundEngine->tick()` and
  `playMusicTick()` in the frame loop, which nothing gated,
- calling `run_middle()` (the client's per-frame game work), and
- rendering and presenting a frame every iteration, so it was VSync-paced and
  holding a GPU.

The owner's description was exactly right: it ran like a Minecraft client in the
background that happened to host a server.

## What changed

### Audio

`SoundEngine::s_bDisabled` (new, `Common/Audio/SoundEngine.h`) is a runtime version
of 4J's own `__DISABLE_MILES__` compile switch. Guarded at the same points 4J
guarded, plus the play entry points: `init`, `tick`, `playMusicUpdate`, `play`,
`playUI`, `playStreaming`. Set in `Win64DedicatedServer::Initialise()`, which runs
before the `Minecraft` constructor - that is where `soundEngine->init(NULL)` is
called from, so the flag has to be set that early.

Defaults to false, so no other platform changes behaviour.

### UI

`ui.init(...)` is skipped entirely on a dedicated server, and every subsequent
`ui.*` call in the frame loop is gated to match: `ui.tick`/`ui.render`,
`ui.CheckMenuDisplayed`, and the trial-timer block.

Note on the trial block: it had to be gated as a whole statement, not with a
`!bDedicated &&` on the `if`, because its **else** arm calls `ui.ShowTrialTimer` -
a bare conjunct would have routed the server straight into it.

### Rendering and pacing

No `RenderManager.StartFrame()` / `Present()`, and no `run_middle()`.

`run_middle()` is not what drives the server - `MinecraftServer::run` has its own
thread and tick loop - and its first branch reaches `ui.IsPauseMenuDisplayed()`,
which faults with no UI. Everything the server needs from the main loop happens
above it: the message pump, `StorageManager.Tick()`, `g_NetworkManager.DoWork()`
(the accept path) and `Win64DedicatedServer::Tick()`.

Dropping `Present()` also removes the loop's only pacing - it was what blocked on
VSync - so the dedicated path sleeps 20ms (one Minecraft tick) instead. Without
that it spins a core flat.

Measured: ~4% of one core at idle, most of which is the initial worldgen, versus
frame-rate pegged before.

### `CGameNetworkManager::s_bHeadless` (new)

`ServerStoppedWait()` pumps `ui.tick()/ui.render()/RenderManager.Present()` while
waiting for the server thread. With no UI that faulted in
`UIGroup::getCurrentScene` - a genuine 0xC0000005 on every clean shutdown.

That code is shared, so per the networking layering rule in CLAUDE.md it gets a
platform-independent flag rather than an `#ifdef _WINDOWS64`. **The
`StorageManager.Tick()` in that same loop must stay** - the comment above it is
right, it is what lets the save progress so the server thread can finish.

The flag is declared inside `GameNetworkManager.h`'s long implicit `public:`
region with no new access specifier, for the reason the previous change documented.

### stdin console (new feature)

4J left the shape of the Java console in place - `MinecraftServer::handleConsoleInput`
and `handleConsoleInputs` are still there - but the reader thread that fed it is
commented out at the top of `initServer`. This adds that thread.

Commands: `stop` / `end` / `quit`, `players` / `list`, `help`.

`stop` is handled locally rather than queued, so it still works if the server
thread is wedged. Anything else is passed to `handleConsoleInput` **and currently
discarded** - `handleConsoleInputs()` drains the queue without dispatching, because
4J removed the `commands->handleCommand(input)` call. Wiring
`ServerCommandDispatcher` in is the obvious follow-up.

Also strips a leading UTF-8 BOM: a redirected pipe from PowerShell prepends one to
the first line, which turned `stop` into an unknown command.

### Clean exit

`Shutdown()` now calls `PostQuitMessage(0)`. `Minecraft::stop()` only clears
`Minecraft::running`, which the dedicated path never reads - the loop runs until
`WM_QUIT`, which on a client arrives from the window closing. With no window
nothing ever posted it, so the process sat idle forever after a successful save.

### Crash handler (new)

`SetUnhandledExceptionFilter` -> a dbghelp `StackWalk64` that resolves the faulting
stack against the PDB next to the exe and writes it to `server.log`. A dedicated
server runs unattended and a silent 0xC0000005 with nothing in the log is useless.

This immediately located the `ServerStoppedWait` fault above, symbol and line
number, with no debugger involved.

## Files changed

- `Common/Audio/SoundEngine.{h,cpp}` - `s_bDisabled` and its guards.
- `Common/Network/GameNetworkManager.{h,cpp}` - `s_bHeadless`, gating in `ServerStoppedWait`.
- `Windows64/Windows64_Minecraft.cpp` - the audio/UI/render/`run_middle` gates.
- `Windows64/Win64DedicatedServer.{h,cpp}` - stdin console, crash handler,
  `RequestStop()`, `PostQuitMessage`, `CONIN$` reopen.

## Verified at runtime

Built at `Release|x64` and actually run, repeatedly:

- Silent. No Miles startup, no soundbank load, no menu music.
- No window, no UI scene stack.
- Stable across a 90s+ soak; ~4% of one core, ~210MB working set.
- `TCP CONNECT OK` -> `Sockets: added peer "127.0.0.1" smallId 2` ->
  `Players online: 1/8`, and clean removal on disconnect.
- `stop` on stdin -> `Console: stop` -> `Stopping server and saving world...` ->
  `World saved. Goodbye.` -> **process exits 0.1s later**, no crash.
- The crash handler produces a correct symbolised stack (proven on the
  `ServerStoppedWait` fault before it was fixed).

## NOT done / still open

**1. The world still does not persist. Root cause found.**

`StorageManager.Init(...)` is at `Windows64_Minecraft.cpp:945`, **inside an `#if 0`
that spans lines 908-955**. So `StorageManager` is never initialised on the Windows
x64 build at all. The save blob is built - `Save data compressed from 4875608 to
2689037` appears in the log - and then written nowhere. No world files are produced
by either the initial save or the shutdown save.

This is not server-specific; it affects client saving on this platform equally.

Fixing it means bringing `StorageManager` up on Windows x64, which is not a
one-liner: `4J_Storage.lib` is prebuilt VS2012 middleware, callback-driven,
normally pumped by the UI scenes, and expects console save-slot semantics
(`ProfileManager` sign-in, a storage device). CLAUDE.md also flags `4J_Storage.h`
as a cross-CRT STL risk. A dedicated server arguably wants to bypass that layer
entirely and write the `ConsoleSaveFileOriginal` blob to a plain file.

There is a second gate even if StorageManager works: `MinecraftServer::stopServer`
wraps its save in `ProfileManager.IsSignedIn(GetPrimaryPad()) && IsFullVersion() &&
!StorageManager.GetSaveDisabled()`. A dedicated server has no signed-in profile.

**2. D3D is still initialised.** `InitDevice()` still runs against the hidden
window, and textures are still loaded. The frame loop no longer draws or presents,
so the cost is startup-only, but this is not yet a device-free process. Removing it
is entangled with texture loading and is really part of a separate-exe effort.

**3. A real client joining is still unverified.** Only a raw TCP socket has been
tested, which proves the accept path but not the login handshake or gameplay.

**4. Exit code is 808, not 0.** Harmless but untidy for scripting; `WinMain`'s
return value was not investigated.
