# Native UI migration (off Iggy/Flash, onto C++ + PNG)

**Goal:** replace the Iggy (Scaleform-style Flash) UI with the Java-derived
`Screen`/`Button` widget stack that is already in this tree, so menus are plain
C++ drawing `gui.png` instead of compiled `.swf` movies. End state is full
removal of `Minecraft.Client/Common/UI`, the `.swf` assets, and the Iggy
middleware.

## The goal is the LCE look, not the Java look

**Read this before porting a screen.** The target is the console layout and feel,
reimplemented without Iggy - not Java Edition's menus. Reviving the in-tree Java
`Screen` subclasses as-is gets you Java Edition's design (centred 200x20 buttons,
dirt background, Java's panorama framing), which is the wrong answer even though
it is the fastest thing to get on screen. Use the Java classes as **plumbing**
(rendering and input), and take the **design from the .swf**.

### Getting the real layout out of the .swf

The menu `.swf` files are ordinary zlib-compressed SWF ("CWS") containing **no
bitmaps at all** - just ActionScript (DoABC) and vector shapes, with each control
positioned by a PlaceObject2/3 matrix. So the authored geometry can be read
directly:

    python tools/swf_layout.py Builds/Windows-x64-Release/Common/Media/MainMenu720.swf

The LCE menu grid, from `MainMenu720.swf` (and identical in `PauseMenu720.swf`):

| control | x | y | scaleX |
|---|---|---|---|
| Button1..6 | 415 | 250, 300, 350, 400, 450, 500 | 1.125 |
| iggy_Splash | 612 | 126 | 1.0 |
| Timer | 565 | 285 | 1.0 |

Authored stage is **1280x720**; the 1080 variants are the same layout scaled 1.5x
(x=623, y=375 + 75n). Movie space is what `UIScene::getMovieWidth/Height` report,
so a native screen can use these numbers directly and scale to the render target
the way `UIControl` does.

What the `.swf` does **not** give you is button width/height: the buttons are
placed with no character id and are instantiated from the AS3 library at
runtime. Those come from the **runtime dump**:

    Minecraft.Client.exe -flashui

That keeps the original Flash menus (the native intercept bails out early) and
`UIScene::DumpLayout` writes every control's name/x/y/width/height to
meow_debug.log about a second after each scene settles. Captured results live in
[`docs/reference/lce-menu-layouts.txt`](../reference/lce-menu-layouts.txt) - the
main menu's buttons are **674x60**, and the column is simply centred
(623 + 674/2 == 960).

**Work in stage space, not GUI-scale space.** LCE authored everything at
1920x1080, so a ported screen should map that stage onto its own coordinates -
see `TitleScreen::lceX/lceY/lceLen`, which scale uniformly off height and centre
horizontally, letterboxing a non-16:9 window rather than stretching. Laying a
screen out in `width/2 - 100` terms is how you end up with Java Edition's
proportions again.

Buttons are drawn, not blitted: `LCEButton` (in `TitleScreen.h` for now) paints a
translucent panel with a border, because the Java `Button` blits the 200x20
widget out of `gui.png` and that art *is* the Java look.

The real main menu is six buttons - Play Game, Leaderboards (repurposed as
"Join Server" on Windows), Achievements, Help & Options, DLC/Unlock, Exit Game -
plus separate Panorama and Logo components. See `Common/UI/UIScene_MainMenu.cpp`
for the authoritative list and the `IDS_*` label for each.

## Why this is smaller than it looks

4J ported Java Edition's screen system when they ported the game, then built the
Iggy UI over the top of it and stopped calling most of the original. **The old
system was never deleted, and it still compiles into the build.**

Present and live:

- `Screen.h/.cpp`, `Button.h/.cpp`, `GuiComponent.h/.cpp` (`fill`, `blit`,
  `drawCenteredString`), `Font.cpp`
- 28 `*Screen.cpp` subclasses in the vcxproj — TitleScreen, OptionsScreen,
  PauseScreen, SelectWorldScreen, InventoryScreen, ControlsScreen,
  JoinMultiplayerScreen, AbstractContainerScreen, …
- `Minecraft::setScreen` (`Minecraft.cpp`), and `Minecraft::screen`
- `GameRenderer.cpp` still renders `mc->screen` every frame
- `Common/res/gui/gui.png` — the widget sheet `Button::render` blits
- `glWrapper.cpp` implements the GL calls these use for real (not stubs), on
  top of the D3D renderer

`GuiComponent::blit` even carries 4J's own DX9 half-texel correction, so the
widget art lands on pixel centres correctly at non-integral GUI scales.

## Why it was dead

Three separate reasons, all now addressed for the first slice:

1. **Most `render()` bodies were `#if 0`'d out** when the Flash menu replaced
   them. `TitleScreen::render` was one of these.
2. **`Screen::updateEvents` / `Screen::mouseEvent` were stubbed.** They were
   written against LWJGL's `Mouse`/`Keyboard` event queues, which do not exist
   here, so no input ever reached a `Button`.
3. **`Minecraft::tick()` only runs once per local player**
   (`Minecraft.cpp`, in the `setLocalPlayerIdx(idx)` loop), and it is the only
   caller of `screen->updateEvents()` and `screen->tick()`. Before a world is
   loaded there is no player, so the title screen was rendered but never ticked
   and never saw input.

Additionally `stubs.h` had `Mouse::getX/getY` returning `0`. `GameRenderer`
feeds those into `Screen::render` as the hover position, so every button was
being asked forever about the top-left corner — no hover state was possible.

## How the migration works

A single feature macro, **`_MEOW_NATIVE_UI`**, defined for `Debug|x64` and
`Release|x64` only (`Minecraft.Client.vcxproj`). The console targets and
`Win32`/`ARM64EC` do not define it and are unaffected.

The swap happens at one choke point:

    UIController::NavigateToScene()          Common/UI/UIController.cpp
      -> UIController::NavigateToNativeScreen()   [_MEOW_NATIVE_UI only]

`NavigateToNativeScreen` switches on the `EUIScene`. For a scene that has been
ported it tears down the Flash stack for that group (`CloseUIScenes`,
`SetMenuDisplayed(false)`), calls `Minecraft::setScreen(new …Screen())` and
returns `true`; `NavigateToScene` then returns without touching Iggy. For any
scene not yet ported it returns `false` and the Flash path runs exactly as
before.

This is deliberately a choke point rather than a set of edits at each call site:
every navigation in the game funnels through `NavigateToScene`, including
`NavigateToHomeMenu`, so one `case` label ports a menu no matter who asked for
it. **To port a menu: write/revive its `Screen` subclass and add a `case`.**
When the switch covers everything, `Common/UI` is dead code and can go.

### The Windows boot path

    UIScene_Intro (animation ends)
      -> eUIScene_SaveMessage                 (the non-Xbox branch)
      -> UIController::NavigateToHomeMenu()
      -> NavigateToScene(eUIScene_MainMenu)
      -> intercepted -> setScreen(new TitleScreen())

### Input and ticking

- `Screen::updateEvents` (under `_WINDOWS64`) samples `Win64Input` and
  synthesises press/release edges. It is edge-triggered off
  `MouseButtonWentDown` because `mouseClicked` plays a sound and fires
  `buttonClicked` — a held button must not repeat those.
- `Screen::getPointerPos` converts client pixels into the screen's GUI-scaled
  space (`width`/`height` from `ScreenSizeCalculator`). No Y flip: Windows
  client coordinates are already top-down, unlike the LWJGL original.
- `Mouse::getX/getY` in `stubs.h` are now real on `_WINDOWS64`, defined in
  `Windows64/Win64KeyboardMouse.cpp`. `getY` is **bottom-up**, because the
  `GameRenderer` call site applies the LWJGL flip and is shared with the
  console builds, which must not change.
- **`Minecraft::renderScreenNoLevel()`** is called every frame from the Windows
  main loop, after `ui.render()`. This is easy to miss and cost a stall once:
  `GameRenderer::render` is only reached through `Minecraft::run_middle()`,
  which the platform loop gates on `app.GetGameStarted()`. **Before a world is
  loaded, `GameRenderer` never runs at all.** The pre-game frame is only
  "clear to black -> `ui.render()` -> present", so a native `Screen` with no
  Flash scene behind it drew nothing and the menu looked frozen.
- `Minecraft::tickScreenNoPlayer()` is called every frame from the Windows main
  loop (`Windows64/Windows64_Minecraft.cpp`, beside `ui.tick()`). It returns
  immediately when a player exists, so in-game screens keep their single tick
  from `Minecraft::tick` and are not ticked twice.

## Gotchas

- **`Minecraft::tick` dereferences `player` under `if (screen != NULL)`**
  without a null check. It is only safe because that function never runs without
  a player. Do not move screen handling into it.
- The in-tree `*Screen.cpp` classes are **1.2.2-era**. The reference Java tree
  at `C:\Projects\Minecraft\Mods\DeeperDark\ref-decompiled\Minecraft\26.1.2` is
  a decade newer, with a different widget model (`AbstractWidget`,
  `GridLayout`). Backport selectively; do not import wholesale.
- **The panorama is six PNGs, not a movie.** The Flash menu's backdrop was
  `UIComponent_Panorama` (a `.swf`). The native `TitleScreen::renderPanorama`
  rebuilds the same effect from
  `Common/res/1_2_2/title/bg/panorama0..5.png` drawn as the inside of a cube
  under a 120-degree perspective camera. The soft focus is the Java trick of
  redrawing the cube at sub-pixel offsets with falling alpha - there is no
  render-to-texture in this path to blur with. Cost is draw calls, not pixels:
  each face is its own Tesselator begin/end, so the pass count is the dial to
  turn if it ever gets expensive.
- **Every screen needs a way out.** `Screen::keyPressed` has Escape handling,
  but nothing calls it - `Screen::keyboardEvent` is still an unported LWJGL
  stub. `Screen::tickMenuInput` handles `ACTION_MENU_B` and calls
  `setScreen(NULL)`, which with no level loaded returns to the title screen.
  Without this, entering any unported screen traps the player.
- **Labels come from `app.GetString(IDS_*)`, not `Language::getElement`.**
  `Language::getElement` is a 4J stub that returns the key it was handed
  (`Minecraft.World/Language.cpp`), because the Java localisation system was
  replaced wholesale by the `IDS_*` string table the Flash UI uses. A ported
  screen that calls `getElement` will display raw keys like
  "menu.singleplayer". Use the same IDs the equivalent Flash scene used and the
  labels arrive already translated. IDs are in `Windows64Media/strings.h`.
- **The keypress that opens a screen will press a button on it.** The Enter that
  dismissed the Flash save-message dialog was still held when the native title
  screen appeared, so it immediately activated the focused button and shot
  through to the next screen - which looked like the title screen never
  appearing at all. `Screen::m_bSwallowSelectUntilRelease` holds activation back
  until the select button is released; navigation stays live. The Flash UI has
  the same guard (`UIScene_SaveMessage::m_bIgnoreInput`).
- **Pad input had to be built from nothing.** `Screen`/`Button` came from the
  Java game and were mouse-only, so a controller could not drive a ported menu
  at all. `Screen::tickMenuInput` now reads the same `ACTION_MENU_UP/DOWN/A`
  actions the Flash UI uses, keeps a focused-button index, and `Button::focused`
  renders identically to mouse hover. The mouse still wins the highlight on any
  tick it actually moved.
- **A native Screen must claim to be a menu, or all input dies.**
  `UIController::TickMousePointer` calls `UpdatePointerMode(GetMenuDisplayed(iPad), ...)`,
  and `CloseAllPlayersScenes` sets MenuDisplayed false for every pad on its way
  out. Left alone, the cursor drops into mouse-look mode - hidden, clipped,
  parked on the window centre - and `IsPointerActive()` goes false, so
  `Screen::getPointerPos` bails and no click ever lands. `TickMousePointer` now
  ORs in `Minecraft::m_bNativeScreenActive`. Note `IsPointerActive()` also
  requires `s_inUse` ("has the player touched kb/mouse?"), so on a pure
  controller session the mouse path is legitimately inactive and the pad path
  is the only one.
- **Container menus are the hard case.** Their Flash scenes draw their own
  pointer and hit-test item slots that are not `UIControl`s at all (see the
  `hasOwnPointer` comment in `Common/UI/UIScene.h`). `AbstractContainerScreen.cpp`
  exists in-tree, so the Java machinery for them is there.
- **New image assets must go in `Common/res/1_2_2/`, not `Common/res/`.**
  This is the single most expensive thing to get wrong here. `Textures::readImage`
  sends anything that is not a "TU image" or an "original image" (see
  `TUImagePaths` / `OriginalImagesPaths` in `Textures.cpp`) to
  `1_2_2/` + name. That is why `gui/gui.png` exists in *both* places and the
  `1_2_2` copy is the one actually loaded. A file present only in
  `Common/res/title/` is **not found**.
- **Nothing clears the backbuffer before a world is loaded, and the frame is
  not safe to draw into after `ui.render()`.** Both the per-frame clear and the
  render-state save/restore around the UI render sit inside dead `#if 0` blocks
  in `Windows64/Windows64_Minecraft.cpp`. It never mattered because the Flash
  panorama repaints the whole screen every frame. Remove the Flash scenes and
  the last presented image simply persists - a frozen picture while the loop,
  the music and the input all keep running. This is the single most misleading
  failure mode in this subsystem: **a frozen picture here is not evidence of a
  hang.** `Minecraft::renderScreenNoLevel` therefore clears explicitly and calls
  `RenderManager.Set_matrixDirty()`, and the platform loop calls it *before*
  `ui.render()`, since Iggy draws through its own backend (gdraw) and leaves
  state our draws do not survive.
- **A missing texture used to look like a hang.** Not-found resolves to a
  zero-sized image, which trips `assert(elements!=0)` in `arrayWithLength`.
  The x64 configs define no `NDEBUG`, so **asserts are live in Release**, and
  the resulting modal dialog sits invisibly behind a fullscreen window while
  blocking the main thread - a frozen last frame, no animation, no response.
  `Textures::loadTexture(BufferedImage*, int, bool, bool)` now checks for this
  and logs instead. If something ever looks hung, suspect a hidden dialog
  before suspecting an infinite loop.
- `res/1_2_2/title/splashes.txt` **does** exist (an earlier note here claimed it
  did not). `TitleScreen::splash` is still hardcoded empty, so restoring the
  rotating splash text is a small, self-contained job.
- Textures with no `TEXTURE_NAME` enum entry are bound by resource path with
  `Textures::bindTexture(const wstring&)`, which routes through
  `loadTexture(TN_COUNT, name)`. Note that `TN_COUNT` makes `IsOriginalImage`
  and `IsTUImage` both false, so such a texture always takes the `1_2_2/` path.

## Scale of what remains

353 `.swf` files, 87 distinct menus (each built at 1080/720/480/Vita plus
split-screen variants), and 214 of the 229 files in `Common/UI` reference Iggy.
The layout and art of every menu currently exist **only** as compiled Flash, so
each ported menu needs its layout authored in C++ — which is what the Java
screen classes do natively (positions computed from `width`/`height`, art
blitted from `gui.png`).
