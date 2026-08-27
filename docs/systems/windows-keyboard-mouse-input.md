# Keyboard and mouse input (Windows x64)

How keyboard and mouse reach the game on the Windows x64 build, and why it is
built the way it is.

## The starting position

The Windows64 port shipped with **no keyboard or mouse input of any kind**. Not
disabled — absent. Three independent confirmations:

- The window procedure in `Minecraft.Client/Windows64/Windows64_Minecraft.cpp`
  was the stock Visual Studio Win32 template: `WM_COMMAND`, `WM_PAINT`,
  `WM_DESTROY`, nothing else.
- There were no keyboard-reading Win32 APIs anywhere in the client outside the
  console platform directories — no `GetAsyncKeyState`, no `GetKeyboardState`,
  no raw input, no DirectInput.
- Every input read in the game goes through one global, `InputManager`
  (`C_4JInput`), whose entire public API is joypad-only: `ButtonPressed`,
  `ButtonDown`, `ButtonReleased`, `GetJoypadStick_*`, `IsPadConnected`.

Two red herrings worth knowing so nobody re-investigates them:

- `CKeyboard` inside `4J_Input.lib` is **not** gameplay input. It is only
  `RequestKeyboard` / `GetText` — the on-screen text-entry keyboard. (Those two
  are now shadowed as well, but for *text*, not for input: see
  `windows-text-entry.md`.)
- `KeyMapping` and `Options::keyUp/keyDown/...` are vestigial scaffolding from
  the original Java port. Nothing reads them for input.

## Why interception, and why it is safe

`4J_Input.lib` is prebuilt with no source, so `C_4JInput` cannot be extended.

What makes a clean interception possible: `C_4JInput` is only a **facade**. Its
methods operate on a *separate* library global — `InternalInputManager`, of
class `CInput` — not on their own `this` state. Verified by dumping the library
symbols: the lib defines `?InputManager@@3VC_4JInput@@A` but never references it
internally.

That means a subclass carries no state of its own, and calling a base method on
a subclass instance behaves exactly as calling it on the library's own global.
So:

- `Minecraft.Client/Windows64/Win64KeyboardMouse.h` declares `C_Win64Input`,
  publicly derived from `C_4JInput`, shadowing only the methods that need to
  merge keyboard/mouse state.
- `Windows64/4JLibs/inc/4J_Input.h` ends with
  `#define InputManager Win64InputManager`, redirecting all ~70 existing call
  sites without editing a single one.

The macro is deliberately placed **after** `extern C_4JInput InputManager;` so
that declaration still names the library's real symbol.

The shadowed methods are non-virtual. That is fine and intentional: every call
site resolves against the static type of the `Win64InputManager` object, so
shadowing is sufficient, and it avoids assuming anything about the library's
vtable layout.

### The guard that stops this silently dying

`stdafx.h` (line ~177) includes `Windows64\4JLibs\inc\4J_Input.h`, so the macro
lands in the precompiled header and reaches every translation unit. That is
load-bearing but invisible, so `Win64KeyboardMouse.cpp` carries:

```cpp
static_assert(std::is_same<decltype(InputManager), C_Win64Input>::value, ...);
```

If someone reorders `stdafx.h` includes or the macro stops being reached, the
build fails there instead of quietly reverting to a pad-only game.

## Data flow

```
WndProc (Windows64_Minecraft.cpp)
  WM_KEYDOWN/UP, WM_*BUTTON*, WM_MOUSEWHEEL, WM_INPUT, WM_SETFOCUS/KILLFOCUS
        │
        ▼
Win64Input::On*()            - live state in s_keyDown[]
        │
        ▼
C_Win64Input::Tick()         - latches per-tick keyboard snapshot
        │                      (mouse deltas are NOT consumed here)
        │
        ▼
ButtonPressed / ButtonDown / ButtonReleased / GetJoypadStick_*
        │
        ▼
~70 unchanged call sites (Minecraft.cpp, UIController.cpp, ...)
```

Notable design points:

- **Pad always wins.** Every shadowed method calls the base implementation first
  and only falls back to keyboard state. A connected controller behaves exactly
  as it did before this change.
- **Nothing happens until the keyboard is touched.** `s_inUse` stays false until
  a real key or mouse event arrives, so a pad-only session is untouched.
- **Player 1 only.** Splitscreen guests stay on pads.
- **Per-tick snapshot.** `Tick()` latches `s_keyDown` into this-tick/last-tick
  arrays so a key pressed and released between two ticks still registers, and
  every query within a tick agrees with every other.
- **Raw mouse input** (`WM_INPUT`) is used rather than `WM_MOUSEMOVE` plus
  cursor warping, so there is no self-inflicted delta to filter out.
- **Mouse motion is drained per frame, not per tick.** `Tick()` latches keys but
  deliberately leaves `s_mouseAccumX/Y` alone; the frame-rate turn owns them.
- **Focus loss clears all keys.** Otherwise a held W would walk forever while
  the game sat in the background.

## Controls

| Action | Key |
|---|---|
| Move | W A S D |
| Look | Mouse |
| Jump | Space |
| Attack / mine | Left mouse |
| Place / use | Right mouse |
| Inventory | E, Tab |
| Crafting | C |
| Drop | Q |
| Sneak | Shift |
| Pause | Esc |
| Debug info | F3 |
| Third person | F5 |
| Hotbar cycle | Mouse wheel |
| Hotbar slot 1-9 | Number row or numpad |
| Menu confirm / back | Enter / Esc |
| Menu navigate | Arrows, WASD, **or point with the mouse** |
| Menu confirm / back (mouse) | Left click / right click |
| Menu X / Y | X / Y |
| Container menu: take or place a stack | Left click |
| Container menu: split or place one | Right click |
| Release mouse capture | F12 |
| Type into a menu text field | just type — see `windows-text-entry.md` |

Bindings live in one table, `s_bindings[]` in `Win64KeyboardMouse.cpp`, so
rebinding is a data change.

## Pointer modes

Mouse-look and a usable menu pointer want opposite things from the OS cursor, so
the cursor is put into one of three modes, decided once a tick by
`Win64Input::UpdatePointerMode` (called from `UIController::TickMousePointer`)
rather than by whoever touched the mouse last.

| Mode | Cursor | When |
|---|---|---|
| `ePointerMode_Look` | hidden, clipped, parked on the window centre | gameplay. Raw `WM_INPUT` deltas turn the player |
| `ePointerMode_MenuCursor` | visible, free | a control-based menu is up, or capture was released with F12 |
| `ePointerMode_HiddenCursor` | hidden, clipped, free | a container menu is up, because that scene draws its own pointer in Flash |

`s_captured` is still the player's *intent* to be in mouse-look; the mode is
what that intent plus the current menu state resolves to. Only `Look` accumulates
look deltas and only `Look` parks the cursor; the other two track an absolute
client position from `WM_MOUSEMOVE`, so the OS acceleration and screen clamping
apply exactly once.

## Menus and the inventory are pointed at, not just clicked

Two separate mechanisms, both Windows-only, both leaning on plumbing the Vita
touchscreen port already had. See
`docs/changes/2026-08-27-mouse-driven-menus-and-inventory-pointer.md` for the
full reasoning.

- **Control-based scenes** (main menu, pause, settings, world select, crafting):
  `UIController::TickMousePointer` hit-tests the mouse against each control's
  live Flash bounds and calls `UIScene::SetFocusToElement`. It only ever sets
  focus - the click itself is an ordinary `ACTION_MENU_A`/`_B`/`_X` from the
  mouse buttons travelling the same path the pad uses.
- **Container menus** (inventory, chest, furnace, ...):
  `UIScene_AbstractContainerMenu::UpdatePointerFromMouse` writes the mouse
  position straight into `m_pointerPos`, and `m_bPointerFromMouse` tells
  `onMouseTick` to skip the snap-to-slot and tap-to-jump behaviour that exists
  only to make a thumbstick feel like a pointer.

- **Sliders** are the one control the pointer does more than focus. Clicking or
  dragging one calls `UIControl_Slider::SetSliderTouchPos` with the pointer's
  relative position along the track, which is the Vita touch path's mechanism -
  ActionScript quantises it and calls `handleSliderMove` back. The drag is held
  in `UIController::m_pMouseSliderControl` and released with the button, not by
  leaving the control. See
  `docs/changes/2026-08-27-mouse-slider-dragging.md`.

The shared pieces sit behind `_UI_POINTER_SUPPORT` (Vita + Windows) and the
Windows driver behind `_UI_MOUSE_POINTER`, both defined in
`Minecraft.Client/Common/UI/UIPointer.h`.

## Typing text is a separate path

`WM_CHAR`, not the binding table — because that is where Windows has already
applied the keyboard layout, the shift state and dead keys. While a menu text
field is open, `Win64Input::SetTextInputActive` makes the keyboard stop feeding
game and menu actions entirely, so typing a world name cannot also walk the
player. The pad is unaffected. See `windows-text-entry.md`.

## Gotchas

- **Everything here is unverified at runtime.** It compiles and the redirect is
  proven at compile time, but no agent can run the game. In particular the four
  sign constants at the top of `Win64KeyboardMouse.cpp` (`LOOK_X_SIGN`,
  `LOOK_Y_SIGN`, `MOVE_X_SIGN`, `MOVE_Y_SIGN`) were chosen by reasoning about
  the conventions, not by observing them. If look or movement is inverted, flip
  the relevant constant — that is the intended fix and why they are named
  constants rather than inline signs. For pitch specifically, note that
  `Win64ApplyMouseLook` in `Minecraft.cpp` *also* negates Y, to convert
  screen-space (down positive) to LCE's `Entity::turn` convention (up positive);
  that negation is the one to flip if mouse pitch is inverted.
- **Mouse look no longer goes through a stick axis.** It originally did, and it
  felt wrong for exactly the reason you would expect: a stick is a rate control
  and a mouse delta is a displacement. It is now a direct per-frame
  `Entity::turn`, modelled on Java Edition's `MouseHandler` — see
  `docs/changes/2026-08-26-java-style-mouse-look.md`. `GetJoypadStick_RX/_RY`
  are pad-only; the mouse path is `Win64Input::ConsumeLookDelta` ->
  `Win64ApplyMouseLook` (`Minecraft.cpp`).
- **On-screen prompts still show controller glyphs.** See below — that part is
  blocked, not forgotten.
- `MINECRAFT_ACTION_LEFT_SCROLL` / `RIGHT_SCROLL` are handled outside the
  binding table because they come from the wheel, which is an impulse rather
  than a held key. `WHEEL_HOLD_TICKS` keeps a notch "held" briefly so both
  `ButtonPressed` and `ButtonDown` can observe it.

## Why the button glyphs still show a controller — blocked

This was scoped as part of the work and could not be completed. The reason is a
missing toolchain, not missing effort.

The button hints are `UIComponent_Tooltips`, which is an **Iggy Flash movie**,
not code. It exposes exactly four methods to the game — `SetToolTip`,
`SetOpacity`, `SetABSwap`, `UpdateLayout` — and none of them can change which
icon a slot shows. The icon is baked into the movie per slot.

The art comes from `Common/Media/skinHD.swf`, which `ImportAssets2`-references
`platformskinHD.swf`, which is generated from
`Windows64Media/Media/skinHDWin.swf`. Dumping that file's exports shows only
controller art: `ButtonA`, `ButtonB`, `ButtonX`, `ButtonY`, `ButtonDpad*`,
`ButtonLeftBumper`, `ButtonLeftTrigger`, `A_Small`, `L1_Small`, and so on.
**There is no keyboard keycap art in the Windows skin.** 4J never authored any.

(`fourj.Buttons.FJ_KeyboardButtons_Keys_Normal` in `skinHD.swf` sounds relevant
but is the on-screen text-entry keyboard's key buttons, not prompt glyphs.)

So changing the glyphs means authoring new symbols into a 6.8 MB Flash SWF and
recompiling it, which needs Flash/Animate plus the Iggy (RAD Game Tools)
pipeline. Neither is in this tree.

Realistic options, cheapest first:

1. **Leave it.** The controls work; the prompts are cosmetically wrong.
2. **Text instead of glyphs.** `_SetTooltip` takes a string. Prefixing tooltip
   strings with `[E]`, `[Q]` etc. and hiding the icon would need a movie change
   to hide the icon, so it is only partly a code fix.
3. **SWF surgery.** Inject new `DefineBitsLossless2` + `DefineSprite` +
   export entries programmatically. Doable in principle — SWF is documented —
   but it means writing a small SWF authoring tool, and the result must satisfy
   Iggy's loader. High effort, real risk.
4. **Obtain the Iggy toolchain** and do it the way 4J did.

## Related bug fixed alongside

`platformskin.swf` / `platformskinHD.swf` **did not exist anywhere in the tree**
— neither source nor build output. 4J generated them by hand with
`Windows64Media/Media/CopyPlatformSkin.bat`, and it had evidently never been run
here, so `skinHD.swf`'s import of `platformskinHD.swf` resolved to nothing. That
means the platform button art, logo, panorama backgrounds and credits background
were all missing at runtime.

`Minecraft.Client/postbuild.ps1` now performs that copy automatically, so it
cannot be forgotten again. The two generated files are gitignored, since they
are derived from `Windows64Media/Media/skin*Win.swf`.

The runtime effect of this fix is **unverified** — it is possible Iggy tolerated
the missing import gracefully and the visible change is small.
