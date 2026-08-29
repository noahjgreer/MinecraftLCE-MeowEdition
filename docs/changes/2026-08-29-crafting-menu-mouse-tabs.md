# Crafting menu: clicking tabs and recipes with the mouse (and not crafting)

**Date:** 2026-08-29
**Author:** Claude Opus 5 (agent), at the owner's request
**Scope:** Minecraft.Client Common/UI — `UIScene_CraftingMenu`

## What changed

In the crafting menu (`Crafting2x2Menu` / `Crafting3x3Menu` — the tabbed recipe
browser, *not* the creative item picker):

- Left-clicking a category tab switches category.
- Left-clicking a craftable in the horizontal recipe strip selects it.
- Left-clicking no longer crafts. Crafting is space / enter / pad A, as the
  button prompt says.

## Why

Owner report: the tabs could not be reached with the mouse, and clicking one
crafted the selected recipe instead. Both follow from the same thing.

This scene is a plain `UIScene`, not a container menu. It has no free pointer of
its own, and none of its controls are the button-ish types
`UIController::IsPointerFocusable` accepts, so `TickMousePointer` never finds a
hit in it and the mouse's only effect was the click itself — which arrives as
`ACTION_MENU_A` (`MouseKeyForMenuAction`), and `ACTION_MENU_A` on this scene is
"craft the selected recipe".

The Vita already had the missing half: `handleTouchInput` picks a tab or a
craftable from a screen position. What it does not have on desktop is the
touchbox geometry — `TouchPanel_0..6` and `TouchPanel_CraftingHSlots` exist only
in `Crafting*MenuVita.swf`. So the hit-test had to be rebuilt from controls the
desktop movies do have.

**Note on the previous attempt:** `2026-08-29-creative-menu-tab-clicking.md`
fixed the tab strip in the *creative item picker* (`UIScene_CreativeMenu`), which
is a different screen with the same symptom. Those changes stand on their own
merit but were not the ones this report was about.

## How the tab rectangles are derived

`CraftingTabs` is a direct child of `MainPanel` in every crafting movie
(1080/720/480, Split, and Vita — checked by parsing the SWFs). Inside it, the tab
graphic sits at depth 1 with one keyframe per tab, placed at a fixed pitch —
147px at 1080 for the six 2x2 tabs. `SetActiveTab` is a `gotoAndStop`.

So the clip's reported `width` is **one tab**, and the strip is that pitch
repeated `m_iMaxGroup2x2` / `m_iMaxGroup3x3` times from the clip's own `x`, which
does not move with the frame. That is what `TabIndexAt` uses. It is guarded: if a
movie ever reports the whole strip instead (width ≈ the panel), it divides by the
tab count and lands in the same place.

The horizontal recipe strip reuses the Vita's own formula — the slots are square,
so the pitch is the strip's height.

## Files touched

- `Minecraft.Client/Common/UI/UIScene_CraftingMenu.h` — maps `CraftingTabs` as
  `m_controlCraftingTabs` (for its bounds only; the movie still owns what it
  draws); `GetMainPanel()` widened from `__PSVITA__` to `_UI_POINTER_SUPPORT`;
  declares `SelectGroup` / `SelectHSlot` and the mouse hit-test.
- `Minecraft.Client/Common/UI/UIScene_CraftingMenu.cpp` — the tab-change and
  slot-change bodies lifted verbatim out of `handleTouchInput` into
  `SelectGroup` / `SelectHSlot`, which the Vita handler now calls; new
  `HandleMouseClick` / `TabIndexAt` / `HSlotIndexAt`; `handleInput` intercepts
  `ACTION_MENU_A` when the left mouse button is down.

Crafting is suppressed by testing `Win64Input::IsMouseButtonDown(0)` rather than
the action, so space and enter — which raise the same `ACTION_MENU_A` — are
untouched.

## Verification

Built: `MSBuild MinecraftPC.sln -p:Configuration=Release -p:Platform=x64` links
clean. **Runtime is unverified** — an agent cannot run the game.

The one thing worth checking first is the tab pitch, because it is inferred from
the SWF rather than read from a control that means "one tab". Run with
`-flashui`: the layout dump prints every scene control, so open the crafting menu
and read the `CraftingTabs` line. Expected at 1080 for the 2x2 menu: `w` about
147, not about 880. If it prints the full strip width the guard should already
handle it; if it prints something else again, that line is the number to fix
`TabIndexAt` against.

Then: each tab selects its category; clicking a recipe in the strip selects it;
space still crafts; the shoulder buttons still change tab; the pad is unaffected.

## Platform impact

Windows x64 only in effect. `SelectGroup`/`SelectHSlot` are a pure extraction —
the Vita calls them with the same values it used inline, so its touch behaviour
is byte-identical. `GetMainPanel` widening and the `CraftingTabs` mapping compile
on every Iggy platform but change nothing on them (nothing else calls
`GetMainPanel` off the mouse path, and a mapped control that is never read is
inert). The click interception is inside `#ifdef _UI_MOUSE_POINTER`.

## Follow-ups and risks

- The vertical variant list (the up/down column on the left) is still keyboard
  and pad only. Clicking it does nothing. The Vita drove it by dragging on the
  horizontal strip, which does not translate to a click.
- Clicking the crafting output slot does nothing either. If "click the result to
  craft" is wanted later, that is the natural place for it — but it was
  explicitly not asked for, and it would put crafting back on the left button.
- Tab hit rectangles are pitch-uniform; the drawn tabs have small gaps between
  them, so a click in a gap selects the nearer tab rather than missing. That is
  the friendlier behaviour, but it is a deliberate choice, not an accident.
