# Sliders can be dragged with the mouse

**Date:** 2026-08-27
**Author:** Claude Opus 5 (agent), for the owner
**Scope:** `Minecraft.Client/Common/UI/UIController.*`, `Windows64/Win64KeyboardMouse.*`

## What changed

Menu sliders (audio volume, gamma, interface opacity, sensitivity, difficulty,
autosave interval, world size, UI size) now respond to the mouse. Previously the
mouse could only move focus *to* a slider; changing its value still needed
left/right on the pad or the arrow keys.

- Clicking anywhere along a slider jumps it to that position.
- Holding the left button and moving drags it continuously.
- The drag is released with the button, not by leaving the slider - dragging
  above, below or past either end keeps moving it, and past an end pins it,
  which is how every other slider a player has used behaves.

## How

This is not a new mechanism, only a new caller for the one the Vita touchscreen
already used. `UIControl_Slider::SetSliderTouchPos(float)` calls the Flash side's
`SetRelativeSliderPos`, and ActionScript quantises the 0..1 position to the
slider's step count and calls `handleSliderMove` back into C++ - the same
callback the pad path ends up at. So no C++ scene code needed touching, and the
value ends up in exactly the same place regardless of which input moved it.

New in `UIController` (all inside `#ifdef _UI_MOUSE_POINTER`):

- `DragSliderToPointer(UIControl_Slider *, S32 iOffsetX, S32 x)` - re-reads the
  control's position from Flash, divides by `GetRealWidth()` (sliders are masked
  rather than scaled, so `getWidth()` is the wrong number - the Vita touchbox
  builder special-cases this the same way), clamps to 0..1 and sends it.
- `m_pMouseSliderControl` - the slider the press landed on, or NULL. Held apart
  from `m_pMouseHoverControl` precisely because a drag outlives the pointer
  leaving the bounds. Cleared when the button comes up and when the scene
  changes.

In `TickMousePointer`:

- The early-out was `if(!ConsumePointerMoved()) return;`. It now also runs on the
  tick the left button goes down, so a click with no movement still jumps the
  slider.
- An in-progress drag is handled before the hit-test and returns - while
  dragging, the pointer does not move focus to whatever it happens to be over.
- A slider under the pointer sets focus first (so the value change lands on the
  slider the menu agrees is current) and then, only on a fresh press, starts the
  drag.

### Reading the button as a button

`Win64Input::IsMouseButtonDown(int)` and `MouseButtonWentDown(int)` are new, and
are the only place the UI reads a mouse button other than through the action
mapping. That is deliberate rather than an inconsistency: everything else a click
does is something a pad can also do, so it belongs in `MouseKeyForMenuAction` as
`ACTION_MENU_A`. A drag is a press, a stream of positions, and a release, and the
position only means anything relative to the control that was under the press -
there is no action to map it to.

## Files touched

- `Common/UI/UIController.h` - `m_pMouseSliderControl`, `DragSliderToPointer`,
  forward declaration of `UIControl_Slider`
- `Common/UI/UIController.cpp` - the above, plus the `TickMousePointer` changes
- `Windows64/Win64KeyboardMouse.h` / `.cpp` - `IsMouseButtonDown`,
  `MouseButtonWentDown`

## Verification

**Built.** `MSBuild MinecraftPC.sln -p:Configuration=Release -p:Platform=x64`
links with 0 errors and only the pre-existing vendored `LNK4099` PDB warnings.
The project compiles at `/W0`, so a clean compile proves less than it looks.

**Runtime behaviour is unverified** - no agent can run this game. Specifically
unconfirmed: that `SetRelativeSliderPos` takes a 0..1 relative position (it is
named for it and the Vita path feeds it exactly this expression, but the SWF
source is not in the tree), and that `GetRealWidth()` is the width the visible
track is drawn at rather than including the label.

## Platform impact

Windows x64 only - every line is inside `_UI_MOUSE_POINTER`. Vita's touch path
is untouched; the console targets do not compile any of it.

## Watch out for

- **The click is also `ACTION_MENU_A`.** Pressing left over a slider both starts
  the drag and sends a menu confirm to the scene. On the pad, A on a focused
  slider is not a value change, so this should be inert - but if clicking a
  slider turns out to also activate the menu (advance a page, close a dialog),
  the fix is to suppress `ACTION_MENU_A` while a slider drag is live, which is
  what `m_pMouseSliderControl` is there to answer.
- **Hit bounds are the axis-aligned Flash box.** If a slider's box includes its
  label, the left end of the track will not be at `x1` and the value will read
  offset. `GetRealWidth()` is the first thing to check; `iOffsetX` (the main
  panel offset) is the second.
- No sound is played for a mouse-driven change. `handleSliderMove` plays
  `eSFX_Scroll` per step, so this comes for free if ActionScript calls back per
  step - and will be a machine-gun click if it does so faster than the pad does.
