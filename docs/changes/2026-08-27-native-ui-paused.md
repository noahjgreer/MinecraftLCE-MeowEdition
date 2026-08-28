# 2026-08-27 — Native UI migration paused; Flash/Iggy is the active UI again

## Status

**Paused at the owner's request.** The Iggy/Flash UI is once again what the game
runs. The native-UI work is still in the tree but inert.

The switch is one preprocessor define, `_MEOW_NATIVE_UI`, which has been removed
from the `Debug|x64` and `Release|x64` `PreprocessorDefinitions` in
`Minecraft.Client/Minecraft.Client.vcxproj`. **Adding it back to those two lines
turns the whole thing on again**; nothing else needs changing.

Verified: builds clean at `Release|x64` (0 errors, 33 warnings, unchanged), and
`_MEOW_NATIVE_UI` no longer appears on any compiler command line. Not run.

## Why it was paused

The work reproduced Java Edition's menus rather than LCE's. The Java `Screen`
classes were already in the tree and compiled, so they were the fastest route to
something working - but they carry Java Edition's *design*, and every increment
made the result look less like the console UI the owner wanted to keep. By the
time that was understood, the direction had been wrong for several iterations.

The fix for that was identified and partly built (see "What was learned"), but
the owner chose to stop rather than continue.

## What is inert but still present

Behind `_MEOW_NATIVE_UI`:

- `UIController::NavigateToNativeScreen` - the single choke point that swapped a
  Flash scene for a native `Screen`. Compiled out.
- `TitleScreen` rebuilt to the real LCE main menu (six buttons at the console's
  coordinates, `LCEButton`, stage-space layout helpers, cube panorama).
- `Minecraft::m_bNativeScreenActive`, `tickScreenNoPlayer`, `renderScreenNoLevel`
  - all early-return when the flag is off.

`TitleScreen` is still constructed at startup by pre-existing 4J code
(`Minecraft.cpp`, `setScreen(new TitleScreen())`), as it always was. Its
`render()` is never reached, because nothing draws `Minecraft::screen` before a
world loads.

## What was kept, because it is a fix rather than an experiment

These are live regardless of the flag:

- **`Textures::loadTexture` zero-size guard.** A texture that fails to load used
  to trip `assert(elements!=0)` in `arrayWithLength`. The x64 configs define no
  `NDEBUG`, so that is a *modal dialog in Release* - invisible behind a
  fullscreen window and blocking the main thread, i.e. indistinguishable from a
  hang. Now logs and skips the upload.
- **`Mouse::getX/getY/isButtonDown`** in `stubs.h` are real on `_WINDOWS64`
  (defined in `Win64KeyboardMouse.cpp`) instead of returning 0.
- **`-flashui`** (`Win64CommandLine::WantsFlashUI`) and **`UIScene::DumpLayout`**
  - deliberately *not* gated on `_MEOW_NATIVE_UI`, so the layout dumper still
  works with the native UI off. This is the most reusable thing produced here.
- `Screen`/`Button` gained pad input, focus, and a back action. Dead code while
  no native screen is used, but correct and worth keeping.

## What was learned (the part worth keeping)

Recorded in [`docs/systems/native-ui-migration.md`](../systems/native-ui-migration.md);
the short version:

1. **The Java `Screen` stack is live, not dead.** It compiles, `GameRenderer`
   still renders `mc->screen` every frame, and `gui.png` still ships. It is
   viable plumbing - it is just the wrong design.
2. **The LCE layout is fully recoverable.** The menu `.swf` files are ordinary
   zlib SWF with **no bitmaps** - just ActionScript and vector shapes, each
   control placed by a matrix. `tools/swf_layout.py` reads positions out of them;
   `-flashui` + `UIScene::DumpLayout` gets the sizes that only exist at runtime.
   Captured results: [`docs/reference/lce-menu-layouts.txt`](../reference/lce-menu-layouts.txt)
   (11 scenes, including the main menu: buttons 674x60 at x=623, y=375+75n in a
   1920x1080 stage).
3. **Three things fail in ways that look like a hang** and cost the most time
   here: a hidden modal assert; an uncleared backbuffer (the per-frame clear is
   in a dead `#if 0`, so with the Flash scenes gone the last frame just
   persists); and a menu with no working input path. None of them stop the loop.
4. **`Language::getElement` is a stub** returning its argument. Labels come from
   `app.GetString(IDS_*)`.

## If this is picked up again

Start from the reference layouts, not from the Java screens. The remaining
unknowns were button **colours and font** - the console's own, which had not been
captured. A screenshot of the real Flash main menu under `-flashui` would close
that gap.
