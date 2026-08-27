# Mouse-driven menus and a real mouse in the inventory

**Date:** 2026-08-27
**Author:** Claude Opus 5 (agent), for the owner
**Scope:** `Minecraft.Client/Common/UI`, Windows64 platform layer

## What changed

Two things the owner asked for, which turned out to be one problem with two
answers.

**1. The inventory pointer is now the mouse.** The container menus (inventory,
chest, furnace, dispenser, brewing stand, enchanting, anvil, creative) already
had a free-moving on-screen pointer with slot hit-testing behind it. What they
did not have was a pointing device: `IUIScene_AbstractContainerMenu::onMouseTick`
drove that pointer from the left stick as a rate control, with snap-to-slot and
tap-to-jump layered on top to make a stick bearable. The mouse position is now
written straight into `m_pointerPos` in movie coordinates, and both of those
crutches are switched off while it is.

Left click is `ACTION_MENU_A` (take/place a stack) and right click is
`ACTION_MENU_X` (split/place one), which is Java Edition's mapping onto LCE's
existing actions.

**2. Menus can be navigated with the mouse.** Main menu, pause menu, settings,
world select, crafting - anything built out of `UIControl`s. Moving the mouse
moves menu focus to whatever is under it; left click confirms, right click goes
back, the wheel scrolls lists.

The OS cursor is now managed as three modes rather than "captured or not":

| Mode | Cursor | Used when |
|---|---|---|
| `ePointerMode_Look` | hidden, clipped, parked on centre | gameplay; raw deltas turn the player |
| `ePointerMode_MenuCursor` | visible, free | a control-based menu is up, or capture was released with F12 |
| `ePointerMode_HiddenCursor` | hidden, clipped, free | a container menu is up - the scene draws its own pointer |

## Why

The owner asked for the inventory cursor to be replaced by the mouse, and for
the same pointer to work in the main menu and other menus so a player with a
mouse is not forced onto button navigation.

## How, and why this shape

The Vita port had already solved menu pointing. Its touchscreen path reads
control bounds back out of Flash (`UIControl::UpdateControl`), asks the scene's
ActionScript to move focus (`UIScene::SetFocusToElement`, which calls the
movie's `SetFocus`), and resolves a second hit inside multi-row controls
(`UIControl_ButtonList::SetTouchFocus`). All of it was written behind
`#ifdef __PSVITA__` purely because a touchscreen was the only pointing device
4J ever shipped this game with.

So rather than writing a second parallel hit-test, those small shared pieces
moved behind a new macro. **Checked before relying on it:** the ActionScript
side is not Vita-only. `SetFocus` is present in 97 of the 110 `*1080.swf`
movies - every interactive one; the 13 without it are non-interactive
components (HUD, logo, backgrounds, tutorial popup). `SetTouchFocus` and
`CanTouchTrigger` are in `MainMenu1080.swf` too, not only in `MainMenuVita.swf`.

Two macros, defined in the new `Common/UI/UIPointer.h`:

- `_UI_POINTER_SUPPORT` - Vita **and** Windows x64. The platform-agnostic
  plumbing that was already there.
- `_UI_MOUSE_POINTER` - Windows x64 only. The mouse driver and its policy. Vita
  keeps its own `HandleTouchInput`, which has drag and gesture semantics a mouse
  does not want.

Neither is defined for the console targets, so their preprocessed output is
unchanged.

### Clicking is not synthesised

`UIController::TickMousePointer` only sets focus. The click itself is an
ordinary `ACTION_MENU_A`/`_B`/`_X` produced by the mouse buttons in
`Win64KeyboardMouse.cpp` and dispatched down the same path the pad uses. That
means every scene handles a mouse click exactly as it handles the pad, with no
second code path to keep in step, and it is why `TickMousePointer` runs at the
*top* of `handleInput` - focus has to already be right by the time the click is
read a few lines later.

### The mouse-button bindings are conditional, not in the table

They are not in `s_bindings[]` because a static table cannot express what they
need: they apply only while a menu is actually up (so a click during gameplay is
a swing, not a menu confirm), and the right button means "split stack" in a
container menu and "back" everywhere else. `MouseKeyForMenuAction` handles both.

### "Is a container menu displayed" was the wrong question

The obvious predicate for hiding the cursor was
`UIController::IsContainerMenuDisplayed`, and it is wrong: the crafting menus
are flagged as container menus for the autosave and input rules, but they are
ordinary control-based scenes with no pointer of their own. Using that flag left
the crafting menu with a hidden cursor and nothing driving it. The predicate is
now `UIScene::hasOwnPointer()`, overridden to `true` only by
`UIScene_AbstractContainerMenu`.

## Files touched

New:

- `Minecraft.Client/Common/UI/UIPointer.h` - the two feature macros, and why
  there are two.

Guard swaps, `__PSVITA__` to `_UI_POINTER_SUPPORT` (no behaviour change on Vita):

- `Common/UI/UIControl.h` / `.cpp` - `UpdateControl`, `setHidden`, `getHidden`
- `Common/UI/UIScene.h` / `.cpp` - `GetParentLayer`, `GetParentLayerGroup`,
  `GetControls`, `SetFocusToElement`, `UpdateSceneControls`
- `Common/UI/UIGroup.h` / `.cpp`, `Common/UI/UILayer.h` / `.cpp` - `GetGroup`,
  `m_iLayer`, `getCurrentScene`
- `Common/UI/UIControl_ButtonList.h` / `.cpp` - `SetTouchFocus`,
  `CanTouchTrigger`

In `UIScene::removeObject` the block was split: re-reading control positions and
marking the control hidden are needed by any pointer hit-test and now run on
both platforms; only `ui.TouchBoxRebuild` stays Vita-only, because the mouse
path hit-tests live and has no cache to rebuild.

New code:

- `Common/UI/UIScene.h` - `getMovieWidth/Height`, and the new virtual
  `hasOwnPointer()`
- `Common/UI/UIController.h` / `.cpp` - `TickMousePointer`, `PointerHitsControl`,
  `IsPointerFocusable`; called from the top of `handleInput`
- `Common/UI/UIScene_AbstractContainerMenu.h` / `.cpp` -
  `UpdatePointerFromMouse`, called from `tick()` before `onMouseTick`; the
  client-pixels to movie-coordinates conversion lives here because this is the
  only place with both spaces in scope
- `Common/UI/IUIScene_AbstractContainerMenu.h` / `.cpp` - `m_bPointerFromMouse`,
  and the block in `onMouseTick` that suppresses snap-to-slot and tap detection
- `Windows64/Win64KeyboardMouse.h` / `.cpp` - pointer modes,
  `UpdatePointerMode`, absolute position from `WM_MOUSEMOVE`,
  `MouseKeyForMenuAction`; `ApplyCapture` became `RecomputeMode` plus
  `ApplyPointerMode`
- `Windows64/Windows64_Minecraft.cpp` - one new `WM_MOUSEMOVE` case

## Verification

**Built.** `MSBuild MinecraftPC.sln -p:Configuration=Release -p:Platform=x64`
links `Minecraft.Client.exe` with 0 errors and only the pre-existing vendored
`LNK4099` PDB warnings. Note the project compiles at `/W0`, so a clean compile
is weaker evidence here than it looks - it proves the code parses and links,
not that the compiler had nothing to say.

**Runtime behaviour is entirely unverified.** No agent can run this game. In
particular nobody has confirmed that the hit-test coordinates line up on screen,
that `SetFocus` does what its presence in the SWF suggests, or that the cursor
mode transitions feel right.

**Verified by inspecting the assets, not only the code:** the SWF export check
described above (`SetFocus` in 97 of 110 `*1080.swf`).

## Platform impact

- **Windows x64** - all the new behaviour.
- **PS Vita** - should be bit-identical. Every guard swapped is `__PSVITA__` to
  a macro that is defined on Vita. The only structural edit on the Vita path is
  the nesting in `UIScene::removeObject`, which still calls `TouchBoxRebuild`.
- **Xbox 360 / PS3 / Durango / Orbis** - neither macro is defined, so nothing in
  the changed regions is compiled. `hasOwnPointer()` and `getMovieWidth/Height`
  are inside `#ifdef _UI_POINTER_SUPPORT`, so they do not even exist there.

None of the console targets can be compiled here to prove it.

## Follow-ups and risks

- **Coordinate mapping assumes the scene fills the window.** Client pixels are
  scaled to movie coordinates by `movie/client`. That is true for a single
  fullscreen player, which is who has a keyboard and mouse; splitscreen guests
  keep the stick pointer and are not pointed at. If the hover boxes turn out to
  be offset or scaled wrong, this ratio is the first thing to look at, and the
  second is whether the swapchain backbuffer really matches the client rect.
- **Hover boxes are axis-aligned Flash bounds.** A control whose art is smaller
  than its box will hover early. The Vita has the same property.
- **`getVisible()` does not know about hidden parents.** A control inside a
  panel that ActionScript faded out may still be hoverable. Again, inherited
  from the Vita path.
- **Right click in the crafting menus is "back", not "craft one".** The crafting
  menus are control-based, so they get the generic mapping. If `ACTION_MENU_X`
  means something useful there, `MouseKeyForMenuAction` is where to change it.
- **Left mouse is bound to both `MINECRAFT_ACTION_ACTION` and `ACTION_MENU_A`,**
  and right mouse to both `MINECRAFT_ACTION_USE` and a menu action. The pad has
  the same overlap (A jumps and confirms), so the game's existing
  menu-displayed gating should cover it - but this is the most likely place for
  a "clicking a menu button also swung my pickaxe" bug to show up.
- **The button glyphs still show controller art.** Unchanged and still blocked;
  see `docs/systems/windows-keyboard-mouse-input.md`.
