# Documentation

Shared long-term memory for this repository. Written by and for agents (and the
owner). Read what's relevant before starting; update it when you finish.

## How to use this folder

**Before working:** skim this index, then read any `systems/` file covering the area
you're touching, plus recent `changes/` entries that mention the same files.

**After working:** add or update the docs your work invalidated. A change that is not
written down here is a change the next agent will have to rediscover.

### `systems/` — how things work

One file per subsystem. Write one whenever you spend real effort understanding
something non-obvious — the layering of a subsystem, why something is done a strange
way, a constraint that isn't visible from the code.

Aim for: what the subsystem is responsible for, the key types and files, how data
flows through it, the platform-specific parts, and the gotchas. Cite files as
relative paths so they stay clickable.

Prefer extending an existing file over adding a near-duplicate.

### `changes/` — what was done

One file per meaningful change, named `YYYY-MM-DD-short-slug.md`. Use
[`_TEMPLATE.md`](changes/_TEMPLATE.md). Skip this for trivial edits (typos,
formatting); do write one for anything behavioral, architectural, or build-affecting.

Be honest about what was *not* verified. Nothing in this workspace can be compiled or
run by an agent, so almost every entry should say so explicitly.

## Index

### Systems
- [Windows x64 build on modern MSVC (v143)](systems/windows-x64-modern-msvc-build.md) — how the x64 target builds under VS2022, the four files that needed fixing, and the prebuilt VS2012 middleware that fights the modern CRT.
- [Keyboard and mouse input (Windows x64)](systems/windows-keyboard-mouse-input.md) — how kb/m reaches a joypad-only game via a subclass of the prebuilt input manager, the three cursor modes, how menus and the inventory are pointed at, and why the button glyphs are still controller art.
- [Text entry (Windows x64)](systems/windows-text-entry.md) — why every text field in the game did nothing on PC, how typing now goes straight into the on-screen field, and the in-game keyboard scene 4J left unfinished.
- [Texture pipeline and the `.pck` format](systems/texture-pipeline-and-pck-format.md) — where textures actually live (not in Iggy), the dead runtime stitcher vs. 4J's hardcoded UV table, and the byte layout of the DLC archives.
- [Dedicated server (Windows x64)](systems/dedicated-server.md) — why the server needs no local player, the client dependencies that survive, and the async save-storage problem that is the remaining work.
- [Dedicated server & direct connect (design)](systems/dedicated-server-and-direct-connect.md) — the two-function transport seam under `Socket`, why the packet layer is already TCP-ready, and the staged plan for an IP/port "Join Server" flow.
- [Audio on Windows x64 (Miles)](systems/windows-x64-audio.md) — the Miles init chain, the CWD-relative soundbank/redist/music paths, and the required runtime file layout.
- [Profile / settings persistence (Windows x64)](systems/windows-x64-profile-persistence.md) — where settings actually live (the `GAME_SETTINGS` blob, not `Options`), and the `profile.dat` store that makes them survive a restart.

### Changes
- [Dedicated server mode](changes/2026-08-26-dedicated-server.md) — `-dedicated` runs a hidden-window, console-logging, world-saving server; why it needed no local player and no save-slot machinery.
- [Headless dedicated server](changes/2026-08-27-headless-dedicated-server.md) — the server stopped being a muted client: no audio, no UI, no rendering, a stdin `stop` console, a crash handler, and the reason worlds still do not persist.
- [Direct connect over TCP (stages 1-3)](changes/2026-08-26-direct-connect-stage1.md) — hosting on a port, joining by address, name-based identity, the F6 "Join a Server" screen and `-server`/`-name` on the command line.
- [TCP transport for Windows x64 (stage 1)](changes/2026-08-26-tcp-transport-stage1.md) — the Winsock `TcpLink`/`TcpListener` that fills the two-function transport seam under `Socket`; compiles, not yet wired to anything.
- [2026-08-26 — Retarget Windows x64 to MSVC v143 and get it linking](changes/2026-08-26-retarget-x64-to-v143.md) — first build of `Minecraft.Client.exe` without VS2012; includes an unresolved vendored-binary question.
- [2026-08-26 — Keyboard and mouse input for the Windows x64 build](changes/2026-08-26-windows-keyboard-mouse-input.md) — the Windows port never had any; also fixes the never-generated platform UI skin.
- [2026-08-26 — Windows x64 build had no audio at all](changes/2026-08-26-windows-x64-silent-audio.md) — a missing soundbank tore down the whole Miles driver; staging fix, no source change.
- [2026-08-26 — Stage the full runtime into the build, and package it for sharing](changes/2026-08-26-windows-x64-runtime-staging.md) — post-build now emits a complete runnable tree; adds package-win64.ps1 and an exe-relative working directory.
- [2026-08-26 — Atlas slicer and `.pck` extractor](changes/2026-08-26-atlas-slicer-and-pck-extractor.md) — two new tools in `tools/`; slices the atlases into 462 per-icon PNGs with a byte-exact re-stitch check. No C++ changed.
- [2026-08-26 — Jump/place fixed (`GetValue`), and the cursor pinned to the window](changes/2026-08-26-win64-getvalue-and-cursor-pinning.md) — a fourth, unshadowed input query is why space, right-click and the scroll wheel did nothing.
- [2026-08-26 — No depth buffer on Windows x64: an uninitialised DSV descriptor](changes/2026-08-26-win64-depth-buffer-uninitialised-dsv.md) — `Flags` was never assigned, so depth writes were dropped or the view failed to create; also clears the atlas slicer of blame.
- [2026-08-26 — Java-Edition-style mouse look (Windows x64)](changes/2026-08-26-java-style-mouse-look.md) — mouse look was being driven through the controller stick path; it now turns the player directly, once per frame.
- [2026-08-27 — Mouse-driven menus and a real mouse in the inventory](changes/2026-08-27-mouse-driven-menus-and-inventory-pointer.md) — the Vita's touch plumbing generalised into a mouse pointer; menu focus follows the cursor and the inventory pointer is the mouse.
- [2026-08-27 – Sliders can be dragged with the mouse](changes/2026-08-27-mouse-slider-dragging.md) – clicking or dragging a menu slider sets its value, reusing the Vita touch path's SetRelativeSliderPos.
- [2026-08-27 — Text entry happens inside the game](changes/2026-08-27-in-game-text-entry.md) — RequestKeyboard works on Windows at last; the "Join Server" Win32 popup is gone and UIScene_Keyboard is finished.
- [2026-08-27 – Number keys select a hotbar slot](changes/2026-08-27-number-key-hotbar-selection.md) – 1-9 pick a hotbar slot directly on Windows x64; the wheel already cycled it.
- [2026-08-26 - Client hung on the 4J logo](changes/2026-08-26-client-hang-on-logo.md) - an unguarded Win64DedicatedServer::Tick() silently started a dedicated server 60 frames into every client launch.
- [2026-08-27 — Settings persist across restarts](changes/2026-08-27-settings-persistence.md) — the x64 profile stub never wrote anything; options now round-trip through `profile.dat`.

## Ground rules

- **Agents can compile this project — but only the Windows x64 target.** VS2022 is
  installed and MSBuild is drivable from the agent shell. So a build claim must be
  backed by an actual build; if you did not run one, say "unverified". The console
  targets still cannot be built (no SDKs), and **runtime behaviour still cannot be
  verified by an agent at all** — never claim the game runs or plays correctly.
- **Record uncertainty.** "I believe X, but did not confirm" is more useful to the
  next agent than a confident guess.
- **Keep it current.** If you find a doc that is now wrong, fix it as part of your
  work and note the correction in your `changes/` entry.
- **This is private source.** Do not paste source contents into external services when
  researching, and do not write anything here intended for public distribution.