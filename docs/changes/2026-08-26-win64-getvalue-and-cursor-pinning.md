# 2026-08-26 — Jump/place fixed (`GetValue`), and the cursor pinned to the window

Follow-on from [`2026-08-26-windows-keyboard-mouse-input.md`](2026-08-26-windows-keyboard-mouse-input.md),
fixing two things the owner hit as soon as the build was actually played.

## 1. Space did not jump — `GetValue` was never shadowed

`C_Win64Input` shadowed `ButtonPressed`, `ButtonReleased` and `ButtonDown`, but the
4J input facade has a *fourth*, independent query — `GetValue(iPad, action, bRepeat)`
— and that is the one several gameplay reads actually use:

| Call site | Action |
|---|---|
| `Input.cpp:107` | `MINECRAFT_ACTION_JUMP` |
| `Minecraft.cpp:3232` | `MINECRAFT_ACTION_USE` (place / use, with repeat) |
| `Minecraft.cpp:3172/3176` | `MINECRAFT_ACTION_LEFT/RIGHT_SCROLL` (hotbar, with repeat) |
| `Minecraft.cpp:3448` | `ACTION_MENU_CANCEL` |
| `Common/Tutorial/*.cpp` | tutorial task completion checks |

Unshadowed, every one of those fell through to the pad-only library implementation
and returned 0 forever. The `MINECRAFT_ACTION_JUMP` → `VK_SPACE` binding existed and
was correct; nothing ever asked it. Right-click placement and mouse-wheel hotbar
cycling were dead for the same reason — the owner only noticed jump first.

`C_Win64Input::GetValue` now mirrors the existing shadows: base implementation first
(so a connected pad is untouched), then the keyboard/mouse fallback.

The two `bRepeat` modes are distinct and both are needed:

- `bRepeat == false` — level query, "is this active right now". Jump uses this, which
  is why holding space has to keep reporting active rather than firing once.
- `bRepeat == true` — edge query with auto-repeat, so holding right-click keeps
  placing blocks. Implemented with a new per-key held-tick counter
  (`s_keyHeldTicks`), `REPEAT_DELAY_TICKS = 14`, `REPEAT_RATE_TICKS = 4`.

Wheel actions are special-cased: a notch is inherently an edge, so under `bRepeat` it
reports only on the notch's first tick. Reporting it on every tick of the
`WHEEL_HOLD_TICKS` window would scroll the hotbar twice per notch.

Return value is `255`, not `1` — `USE` and `JUMP` map to analog triggers on a pad, so
a full-scale value is the safe answer. Every caller only tests `> 0`.

## 2. The cursor wandered off the window (and onto other monitors)

`ApplyCapture()` called `ClipCursor` once, on capture and focus changes only. That is
not enough: Windows silently drops the clip rectangle whenever another window
activates, on display or DPI changes, and when a system dialog appears — and never
restores it. It is also expressed in **screen** coordinates, so it goes stale the
moment the window moves or resizes, and nothing was listening for that.

Once the clip was gone the cursor was free to drift while the player kept turning
(look is raw-input driven, so it does not stop at the screen edge), and the next click
landed in whatever window it was sitting over — on another monitor, exactly as
reported.

Fixes, in `Win64KeyboardMouse.cpp`:

- `PinCursorToCentre()` — warps the cursor back to the client-rect centre. Called from
  `C_Win64Input::Tick()` every tick while captured and focused, so position is
  self-correcting rather than dependent on the clip surviving. Safe because look comes
  from `WM_INPUT` deltas, which report physical device movement; a warp contributes no
  motion, so there is nothing to filter out.
- `GetClientRectOnScreen()` — factored out, and now actually checks its return codes
  and rejects a degenerate rect instead of feeding garbage to `ClipCursor`.
- `Win64Input::OnWindowMoved()` — re-establishes the clip.
- `Win64Input::WantsHiddenCursor()` — lets `WM_SETCURSOR` be answered correctly.

And in `Windows64_Minecraft.cpp`'s `WndProc`:

- `WM_MOVE`, `WM_SIZE`, `WM_EXITSIZEMOVE`, `WM_DISPLAYCHANGE` → `OnWindowMoved()`.
- `WM_SETCURSOR` → `SetCursor(NULL)` and return `TRUE` while captured, so the window
  class cursor is not restored over the hidden one on every mouse move.

## Files

- `Minecraft.Client/Windows64/Win64KeyboardMouse.h`
- `Minecraft.Client/Windows64/Win64KeyboardMouse.cpp`
- `Minecraft.Client/Windows64/Windows64_Minecraft.cpp`

No shared or console-platform file touched; everything is inside `_WINDOWS64`.

## Verified

- `MinecraftPC.sln` `Release|x64` builds clean, exe produced and staged.
- **Confirmed fixed at runtime by the owner (2026-08-26).** Space jumps, and the
  cursor stays pinned to the window instead of escaping onto another monitor.

The repeat constants (`REPEAT_DELAY_TICKS`, `REPEAT_RATE_TICKS`) were guesses in ticks
and were not separately tuned; they are the knob if held right-click ever turns out to
place blocks too fast or too slow.

## Watch out

F12 is still the capture escape hatch. With per-tick pinning the cursor is now
genuinely stuck to the window centre while captured, so that matters more than before.
