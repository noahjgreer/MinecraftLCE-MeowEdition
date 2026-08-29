# Creative menu tabs can be clicked with the mouse

**Date:** 2026-08-29
**Author:** Claude Opus 5 (agent), at the owner's request
**Scope:** Minecraft.Client Common/UI — container-menu pointer

## What changed

The creative inventory's eight category tabs (and the page slider next to them)
now respond to the mouse. Previously the menu opened on Building Blocks and no
amount of clicking a tab did anything; the only way to change tab was the
shoulder buttons (`ACTION_MENU_LEFT_SCROLL` / `RIGHT_SCROLL`).

Three separate things were in the way, all in the shared container-menu pointer
rather than in the creative scene:

1. **Stale control bounds.** `IUIScene_AbstractContainerMenu::onMouseTick` builds
   its section rectangles from `UIControl::getXPos/getYPos/getWidth/getHeight`,
   which are cached at `setupControl` time. ActionScript moves and sizes controls
   after that, so the cached values are only true for the frame they were read
   on — this is exactly why `UIController::TickMousePointer` calls
   `UpdateControl()` on every control before hit-testing it. The container menus
   never did. The two slot lists happen to be authored where they end up, so
   slots worked and hid the problem; the creative menu's `TouchPanel_0..7` tab
   panels are positioned by the movie, so their rectangles were never where the
   pointer was and no tab could even be *hovered*.

2. **Clicks on a section outside the background panel were treated as drops.**
   `handleInput` routed any non-slot click to `handleOutsideClicked` whenever
   `m_bPointerOutsideMenu` was set, and that flag is just "outside the
   BackgroundPanel rect". The creative tab strip is drawn above the panel, so
   even with correct bounds a tab click would have been swallowed as
   drop-carried-item-on-the-floor.

3. **The pointer was clamped to the panel.** `m_fPointerMin/Max` is the panel
   grown by one pointer width — as far off the panel as the stick ever needed to
   reach to drop an item. The hit-test runs on the unclamped position but the
   *drawn* pointer was clamped, so the cursor would have visibly stopped at the
   panel edge while the game thought it was on a tab.

## Why

Owner request: "when users open the creative crafting menu screen, they are stuck
on the first tab — make it so they can use the mouse to select the other tabs".

## Files touched

- `Minecraft.Client/Common/UI/UIScene_AbstractContainerMenu.cpp` — `tick()` now
  calls `UpdateSceneControls()` on the mouse path, before `onMouseTick` runs its
  hit-test, so section rectangles are live. Guarded by `m_bPointerFromMouse`, so
  the stick/touchpad paths and every console target are untouched.
- `Minecraft.Client/Common/UI/IUIScene_AbstractContainerMenu.cpp` —
  - click routing: `handleOutsideClicked` now requires `m_eCurrSection ==
    eSectionNone` as well as `m_bPointerOutsideMenu`. What makes a click a drop
    is that there is nothing under the pointer, not which side of the panel edge
    it fell on.
  - final pointer clamp skipped when `m_bPointerFromMouse`; the mouse is already
    confined to the window.

Nothing in `UIScene_CreativeMenu` / `IUIScene_CreativeMenu` needed changing —
`handleOtherClicked` already mapped `eSectionInventoryCreativeTab_0..7` to
`switchTab`, and `eSectionInventoryCreativeSlider` to `ScrollBar`. That code was
written for the Vita touchscreen and simply never received a hit.

## Verification

Built: `MSBuild MinecraftPC.sln -p:Configuration=Release -p:Platform=x64` links
clean. **Runtime is unverified** — an agent cannot run the game. What the owner
should check:

- Clicking each of the eight tabs switches category, with the focus SFX.
- Dragging / clicking the page slider to the right of the tabs still pages, and
  the shoulder buttons still cycle tabs.
- **Dropping a carried item by clicking outside the menu still works.** This is
  the one behaviour change with regression potential: it now requires the cursor
  to be over dead space rather than over any section. It should be unchanged in
  practice, since the sections are the widgets themselves.
- The pointer in the other container menus (chest, furnace, crafting, anvil,
  enchanting) still tracks the mouse and no longer stops at the panel edge.

## Platform impact

Windows x64 only in effect. Both edits are inside `#ifdef _UI_MOUSE_POINTER` or
gated on `m_bPointerFromMouse`, which is only ever set by
`UpdatePointerFromMouse` (itself `_WINDOWS64`-only). The routing change in
`handleInput` is not preprocessed out, but on a console `m_eCurrSection` is
`eSectionNone` in exactly the cases that previously reached
`handleOutsideClicked` with the stick pointer, so console behaviour is intended
to be identical — this is the one line a 7th-gen build would notice, and it is
the one worth re-reading if console drop-on-floor ever misbehaves.

## Follow-ups and risks

- `UpdateSceneControls()` per tick on the mouse path is a few dozen Iggy property
  reads per frame while a container menu is open. `TickMousePointer` already does
  the same for every other scene, so it is in line with existing cost, but if the
  inventory ever feels heavy this is a place to look (it could be narrowed to
  only the controls the hit-test actually uses).
- `m_fPanelMin/Max` and `m_fPointerMin/Max` are still computed once, in
  `PlatformInitialize`, from the same possibly-stale `BackgroundPanel` bounds. Now
  that clicks no longer depend on the panel rect this matters less, but the
  tooltips shown while "outside the menu" still hang off it.
