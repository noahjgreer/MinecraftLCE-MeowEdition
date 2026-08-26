# 2026-08-26 — Keyboard and mouse input for the Windows x64 build

## What changed

The Windows x64 build now accepts keyboard and mouse input. It previously had
none at all — not disabled, absent.

Full design and rationale in
[`systems/windows-keyboard-mouse-input.md`](../systems/windows-keyboard-mouse-input.md).

## Why

Reported by the owner after first running the build: keyboard did nothing and
the on-screen prompts expected a controller. Investigation confirmed the
Windows64 port never had a keyboard/mouse path — the WndProc was untouched
Visual Studio boilerplate and all input went through the joypad-only 4J_Input
middleware.

## Files

**New**
- `Minecraft.Client/Windows64/Win64KeyboardMouse.h`
- `Minecraft.Client/Windows64/Win64KeyboardMouse.cpp` — key/mouse state, the
  action binding table, and `C_Win64Input`

**Modified**
- `Minecraft.Client/Windows64/4JLibs/inc/4J_Input.h` — appended
  `#define InputManager Win64InputManager` (after the existing `extern`, which
  must keep naming the library's own symbol)
- `Minecraft.Client/Windows64/Windows64_Minecraft.cpp` — WndProc now handles
  `WM_KEYDOWN/UP`, `WM_SYSKEYDOWN/UP`, the mouse buttons, `WM_MOUSEWHEEL`,
  `WM_INPUT` and `WM_SETFOCUS/WM_KILLFOCUS`; `Win64Input::Initialise` is called
  after the window is created
- `Minecraft.Client/Minecraft.Client.vcxproj` — new `ClCompile` item (x64 only,
  cloned from the `Windows64_App.cpp` exclusion set)
- `Minecraft.Client/postbuild.ps1` — installs the platform skin (see below)
- `.gitignore` — ignore the two generated platform skin files

Note the header edit is inside a **vendored** tree (`4JLibs`). That is now
explicitly permitted by CLAUDE.md. It is a source header we already own a copy
of, not a binary patch, so it shows up normally in a diff.

## Approach

`4J_Input.lib` is prebuilt with no source. The key enabling discovery is that
`C_4JInput` is only a facade over a *separate* library global
(`InternalInputManager`, class `CInput`) and holds no state of its own —
confirmed by dumping the library's symbols. So a subclass can shadow a handful
of methods, and the base implementations still behave identically.

`C_Win64Input` shadows `Tick`, `ButtonPressed`, `ButtonReleased`, `ButtonDown`,
`GetJoypadStick_LX/LY/RX/RY` and `IsPadConnected`. Each calls the base first and
only then falls back to keyboard state, so **a connected pad behaves exactly as
before**. Nothing engages until a real key or mouse event arrives.

## Verified

- Clean build of `MinecraftPC.sln` `Release|x64`, zero errors, exe produced.
- The `InputManager` redirect is proven **at compile time**: a `static_assert` in
  `Win64KeyboardMouse.cpp` asserts `decltype(InputManager)` is `C_Win64Input` in
  a TU that reaches it the same way the ~70 real call sites do (via `stdafx.h`
  → PCH). The build passing is the proof; if the macro ever stops reaching TUs,
  the build breaks rather than silently reverting to a pad-only game.
- 1140 translation units recompiled after the header change, so the macro
  genuinely propagated rather than leaving stale objects.

## Unverified

**The game has not been run.** Everything below needs the owner at a keyboard:

- Whether any key actually moves the player. The action enum values are mapped by
  name, but whether e.g. `MINECRAFT_ACTION_FORWARD` is really consumed as a
  digital action or whether movement is purely stick-driven was not confirmed —
  both paths are fed, deliberately, to cover either case.
- **The four sign constants** (`LOOK_X_SIGN`, `LOOK_Y_SIGN`, `MOVE_X_SIGN`,
  `MOVE_Y_SIGN`) were chosen by reasoning, not observation. Inverted look or
  movement is the expected failure and flipping the relevant constant is the
  intended fix.
- Mouse look feel. A mouse delta is being fed through a stick axis, which is a
  rate, so turn speed is somewhat framerate-dependent. `MOUSE_LOOK_RANGE` and
  `MOUSE_SENSITIVITY` are the tuning knobs.
- Cursor capture behaviour across alt-tab and fullscreen. F12 is a deliberate
  escape hatch so the player cannot get trapped.
- Whether `ACTION_MENU_X`/`Y` bound to the X/Y keys is discoverable or sensible.

## Not done — button glyphs (blocked)

This was scoped in and could not be completed; the blocker is a missing
toolchain, not missing effort.

The prompts are an Iggy Flash movie exposing only `SetToolTip`, `SetOpacity`,
`SetABSwap`, `UpdateLayout` — none of which can change a slot's icon. The art
comes from `skinHDWin.swf`, whose exports are **controller-only**
(`ButtonA`, `ButtonDpadL`, `L1_Small`, …). 4J never authored keyboard keycap
art for the Windows skin. Changing it means authoring new symbols into a 6.8 MB
SWF with Flash/Animate plus the Iggy pipeline, neither of which is in this tree.
Options are laid out in the systems doc.

## Watch out

1. `stdafx.h` including `Windows64\4JLibs\inc\4J_Input.h` is load-bearing — it is
   what puts the redirect macro into the PCH. The `static_assert` guards it.
2. `Win64KeyboardMouse.h` includes `"4JLibs/inc/4J_Input.h"` with an explicit
   path. An earlier version used `"4J_Input.h"` and only compiled by accident,
   via MSVC's include-stack fallback. Do not shorten it again.
3. The include is circular (`4J_Input.h` → `Win64KeyboardMouse.h` →
   `4J_Input.h`). It works because of `#pragma once`, but the ordering matters:
   the macro is defined after the class and the `extern`.

## Related bug fixed

`platformskin.swf` / `platformskinHD.swf` did not exist anywhere in the tree.
4J generated them by hand via `Windows64Media/Media/CopyPlatformSkin.bat`, which
had never been run here, so `skinHD.swf`'s `ImportAssets2` of `platformskinHD.swf`
resolved to nothing — losing the platform button art, logo, panorama backgrounds
and credits background. `postbuild.ps1` now does this automatically.

Runtime impact **unverified** — Iggy may have tolerated the missing import.
