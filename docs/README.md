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
- [Keyboard and mouse input (Windows x64)](systems/windows-keyboard-mouse-input.md) — how kb/m reaches a joypad-only game via a subclass of the prebuilt input manager, the control map, and why the button glyphs are still controller art.
- [Texture pipeline and the `.pck` format](systems/texture-pipeline-and-pck-format.md) — where textures actually live (not in Iggy), the dead runtime stitcher vs. 4J's hardcoded UV table, and the byte layout of the DLC archives.
- [Audio on Windows x64 (Miles)](systems/windows-x64-audio.md) — the Miles init chain, the CWD-relative soundbank/redist/music paths, and the required runtime file layout.

### Changes
- [2026-08-26 — Retarget Windows x64 to MSVC v143 and get it linking](changes/2026-08-26-retarget-x64-to-v143.md) — first build of `Minecraft.Client.exe` without VS2012; includes an unresolved vendored-binary question.
- [2026-08-26 — Keyboard and mouse input for the Windows x64 build](changes/2026-08-26-windows-keyboard-mouse-input.md) — the Windows port never had any; also fixes the never-generated platform UI skin.
- [2026-08-26 — Windows x64 build had no audio at all](changes/2026-08-26-windows-x64-silent-audio.md) — a missing soundbank tore down the whole Miles driver; staging fix, no source change.
- [2026-08-26 — Atlas slicer and `.pck` extractor](changes/2026-08-26-atlas-slicer-and-pck-extractor.md) — two new tools in `tools/`; slices the atlases into 462 per-icon PNGs with a byte-exact re-stitch check. No C++ changed.

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
