# 2026-08-27 — Native UI, slice 1: the title screen off Flash

## What changed

First step of moving the UI off Iggy/Flash onto the Java-derived `Screen`
widget stack (plain C++ drawing `gui.png`). The main menu is now a native
`TitleScreen` instead of the Flash `eUIScene_MainMenu`, behind a new feature
macro `_MEOW_NATIVE_UI` (x64 only).

See [`docs/systems/native-ui-migration.md`](../systems/native-ui-migration.md)
for the architecture, the porting recipe, and why the old system was dead.

## Why

The owner's goal is to drop Iggy entirely and use barebones assets — direct C++
and PNGs. The key finding is that **the target system already exists in the
tree**: 4J ported Java Edition's `Screen`/`Button`/`GuiComponent` stack, built
Iggy over the top, and never removed it. It still compiles, `GameRenderer` still
renders `mc->screen` every frame, and `gui.png` is still shipped. So this is a
revival, not a from-scratch UI.

## Files

**Build flag**
- `Minecraft.Client/Minecraft.Client.vcxproj` — added `_MEOW_NATIVE_UI` to
  `Debug|x64` and `Release|x64` only. `Win32`, `ARM64EC`, Durango and every
  console config are untouched.

**The intercept**
- `Minecraft.Client/Common/UI/UIController.h/.cpp` — new
  `NavigateToNativeScreen(iPad, scene)`, called first thing by
  `NavigateToScene`. Handles `eUIScene_MainMenu`; everything else falls through
  to the Flash path unchanged.

**Restored rendering**
- `Minecraft.Client/TitleScreen.cpp` — `render()` was `#if 0`'d out; put back.
  Two changes from the original: the logo is bound by resource path via
  `Textures::bindTexture(L"title/mclogo.png")` rather than the removed
  `loadTexture(L"/title/mclogo.png")` overload, and the rotating splash text is
  skipped when `splash` is empty (it always is — `res/title/splashes.txt` is not
  in this tree, and the original still paid for a matrix push and trig to draw
  nothing).

**Input**
- `Minecraft.Client/Screen.h/.cpp` — implemented `updateEvents()` and added
  `getPointerPos()`. Was a stub written against LWJGL's event queues.
- `Minecraft.Client/stubs.h` — `Mouse::getX/getY/isButtonDown` are declared
  rather than returning `0` under `_WINDOWS64`.
- `Minecraft.Client/Windows64/Win64KeyboardMouse.cpp` — defines them against the
  real cursor.

**Ticking**
- `Minecraft.Client/Minecraft.h/.cpp` — new `tickScreenNoPlayer()`.
- `Minecraft.Client/Windows64/Windows64_Minecraft.cpp` — call it each frame
  beside `ui.tick()`.

## Three separate reasons the old UI was dead

Worth recording, because each would have looked like "the screen system doesn't
work" on its own:

1. `TitleScreen::render` (and most sibling `render` bodies) were `#if 0`'d out.
2. `Screen::updateEvents`/`mouseEvent` were stubbed — no input could reach a
   `Button`.
3. `Minecraft::tick()` runs **once per local player** and is the only caller of
   `screen->updateEvents()`/`screen->tick()`. With no world loaded there is no
   player, so the title screen was never ticked at all.

Plus `Mouse::getX/getY` returning `0`, which made every hover test ask about the
top-left corner.

## Follow-up fix: the menu stalled after "OK"

Reported by the owner on first run: pressing OK on the startup save message
stalled the game. It was this change.

`GameRenderer::render` — the only thing that calls `Screen::render` — is reached
only through `Minecraft::run_middle()`, which the Windows main loop gates on
`app.GetGameStarted()` (`Windows64/Windows64_Minecraft.cpp`). **Before a world is
loaded that branch never runs.** The pre-game frame is only:

    RenderManager.Clear(...)   // to opaque black
    ui.tick(); ui.render();    // the Flash scenes
    RenderManager.Present();

So once the intercept replaced the Flash main menu with a native `TitleScreen`,
there was no Flash scene to draw *and* nothing that draws a `Screen` — a black,
unresponsive window every frame. Not a hang: the loop was still turning.

Fix: `Minecraft::renderScreenNoLevel()`, called each frame from the same block
as `tickScreenNoPlayer()`, just after `ui.render()`. It delegates to
`gameRenderer->render(timer->a, false)`, which takes its existing `level == NULL`
branch (viewport + GUI ortho, no world, no HUD, then `mc->screen->render`) —
so this reuses the real path rather than duplicating render setup. `bFirst` is
`false` because it only gates `updateLightTexture`, which is a world concern.

Files: `Minecraft.h/.cpp`, `Windows64/Windows64_Minecraft.cpp`.

**Lesson for the next slice:** ticking and rendering a `Screen` both need a
pre-game home. Neither is reachable through the normal in-game paths, and they
fail differently — no tick means dead input, no render means a black screen.

## Follow-up fix 2: the "hang" was a hidden assert dialog

The stall above was never fully fixed by the render call - the owner reported it
again, correctly noting the panorama and the save-message arrow both stopped
animating. That ruled out "loop still running, nothing drawn": the pre-game frame
clears to black, so a live loop would have shown black, not a frozen dialog.

Two real bugs, found by adding a flushed per-step log (`Windows64/MeowLog.*`,
temporary scaffolding) rather than by reading code:

**1. The native screen was drawn from frame 1, over the Flash intro.**
`Minecraft.cpp` constructs a `TitleScreen` during startup, long before it is
meant to be seen. `renderScreenNoLevel` only tested `screen != NULL &&
level == NULL`, both true immediately, so it ran the whole `GameRenderer` on the
first loop iteration - before the renderer had ever run, and on top of the intro.
Fixed with `Minecraft::m_bNativeScreenActive`, set only when the
`NavigateToNativeScreen` intercept actually fires. `screen != NULL` was never the
right question.

**2. `meowlogo.png` was in the wrong resource directory.**
`Textures::readImage` sends any image that is not a TU or "original" image to
`1_2_2/` + name. The asset existed only as `Common/res/title/meowlogo.png`, so
the load failed, returned a zero-sized image, and `intArray rawPixels(w*h)`
tripped `assert(elements!=0)`.

**Why that presented as a hang:** the x64 configs define no `NDEBUG`, so asserts
are live in Release. The assert dialog is modal - it blocks the main thread in
its own message loop - and behind a fullscreen window it is invisible. The result
is a frozen last frame with no animation and no response, indistinguishable from
a deadlock. It only became visible once the frame-1 bug made it fire during
startup, before the game window covered it.

Fixes: copied the asset to `Common/res/1_2_2/title/`, and added a guard in
`Textures::loadTexture(BufferedImage*, int, bool, bool)` so a missing or
zero-sized image logs and skips the upload instead of asserting. That guard
matters beyond this bug - the migration will keep adding PNGs, and every
misplaced one would otherwise present as an unexplained hang.

Also removed from the intercept, as wrong independently of the crash:
`CloseUIScenes` (redundant - `NavigateToHomeMenu` already calls
`CloseAllPlayersScenes`, synchronously, and doing it again re-enters from inside
a scene's own ActionScript callback) and `SetMenuDisplayed(iPad, false)` (tells
the input layer no menu is up, which puts the cursor into mouse-look mode:
hidden, clipped, parked on the window centre - exactly wrong for a menu).

**Lesson:** when this codebase appears to hang, check for a hidden modal assert
before looking for an infinite loop. And a step-log beats reading code - three
rounds of plausible reasoning about locks and re-entrancy were all wrong, and one
instrumented run named the exact line.

## Verified

- **Builds.** `MinecraftPC.sln` `Release|x64` on VS2022 (v143): 0 errors,
  33 warnings (unchanged from before). Confirmed `_MEOW_NATIVE_UI` is on the
  compiler command line for 459 translation units via
  `bin/intdir/x64/Release/Minecraft.Client.tlog/CL.command.1.tlog`, and that the
  restored `TitleScreen::render` is in the linked exe (its copyright string is
  present, and it only existed inside the `#if 0` before).
- Traced the Windows boot chain statically: `UIScene_Intro` →
  `eUIScene_SaveMessage` → `NavigateToHomeMenu` → `NavigateToScene(eUIScene_MainMenu)`
  → intercepted. The chain closes.

## NOT verified

**Nothing here has been run.** An agent cannot verify runtime behaviour in this
workspace. Specifically unconfirmed:

- That the stall is now gone, and that the title screen actually appears
  *instead of* the Flash menu rather than on top of or underneath it. `CloseUIScenes` is called with
  the pad index; if the intro scene lives in a different `EUIGroup` than that
  resolves to, the Flash scene may survive underneath.
- That clicks land on the right button. The client-pixel → GUI-space conversion
  in `Screen::getPointerPos` mirrors `UIController::TickMousePointer`, but the
  Y convention is asserted from first principles (Windows top-down, no flip),
  not observed.
- That the pad still navigates the menu. `Screen`/`Button` have **no pad input
  path at all** — the Java original was mouse-only. On the title screen the pad
  currently does nothing. This needs adding before more menus are ported.
- Whether `SetMenuDisplayed(iPad, false)` is right. Lots of game logic keys off
  `GetMenuDisplayed`; telling it no menu is up while a native screen *is* up may
  have side effects (input capture, autosave, pause).
- The buttons wired in `TitleScreen::buttonClicked` lead to `OptionsScreen`,
  `SelectWorldScreen` and `JoinMultiplayerScreen`, which are themselves
  unrevived 1.2.2-era screens. Expect them to render blank or misbehave until
  ported.

## Watch out for

- `Minecraft::tick` dereferences `player` under `if (screen != NULL)` with no
  null check. It is only safe because the function never runs without a player.
  Do not "simplify" screen handling into it.
- To back this out entirely, remove `_MEOW_NATIVE_UI` from the two vcxproj
  lines — every other change is either inert (guarded) or a strict improvement
  (a real `Mouse::getX`).
