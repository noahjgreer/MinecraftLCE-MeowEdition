# 2026-08-27 — Meow Edition title logo

## What changed

The native `TitleScreen` now draws a new "MINECRAFT / LEGACY CONSOLE EDITION"
logo instead of the stock two-row `title/mclogo.png` atlas.

New asset: `Minecraft.Client/Common/res/title/meowlogo.png` — a 1024x256 RGBA
sheet with the 700x123 logo in the top-left corner and the rest transparent.
The source art is `Workspace/Title/title.png` (700x123, unpadded); it was padded
to power-of-two dimensions rather than shipped as-is, because the rest of
`Common/res` is POT and the DX9-derived `C4JRender` texture path is not trusted
with NPOT uploads.

Padding was done with `System.Drawing` (no resampling, nearest-neighbour, no
colour conversion) — the logo pixels are byte-identical to the source.

## Why the draw call is not `blit`

`GuiComponent::blit` hardcodes `1/256` as the texel size **and** uses the source
rect's `w`/`h` as the destination size, so it can only draw a sub-rect of a
256x256 texture at 1:1. Drawing a 700x123 region of a 1024x256 sheet scaled down
to 274x48 needs independent UVs and destination size, so `TitleScreen::render`
emits the quad through `Tesselator` directly. `blitOffset` is still used for Z so
it layers with the rest of the screen the same way.

274x48 was chosen to match the footprint of the logo it replaces (274x44) while
preserving the new art's 5.69:1 aspect.

## Files

- `Minecraft.Client/Common/media/MediaWindows64.arc` — repacked; `skinHDWin.swf`
  and `skinWin.swf` entries carry the new logo bitmap. **This is the one that
  changes what you see.**
- `Minecraft.Client/Common/media/skinHDWin.swf.orig`, `skinWin.swf.orig` — new;
  pristine pre-patch backups, since nothing here is in git
- `Minecraft.Client/Common/Media/platformskin.swf`, `platformskinHD.swf` —
  patched to match the archive; not read at runtime
- `Minecraft.Client/Common/res/title/meowlogo.png` — new; used only by the native
  `TitleScreen`, which is currently compiled out
- `Minecraft.Client/TitleScreen.cpp` — `render()` logo block (dead code for now)

`title/mclogo.png` is left in the tree untouched; nothing references it now.

## Where the logo actually was

It took three attempts. The dead ends are recorded because each looked correct.

1. **`Common/res/title/mclogo.png`** — the native `TitleScreen` asset. Wrong,
   because `_MEOW_NATIVE_UI` is not defined (see below), so that screen never
   renders.
2. **`Common/Media/platformskinHD.swf`** on disk — the right *symbol*, the wrong
   *copy*. The loose SWFs under `Common/Media/` are not read at runtime.
3. **`Common/media/MediaWindows64.arc`** — correct.

`CMinecraftApp::loadMediaArchive()` (`Common/Consoles_App.cpp:4099`) loads
`Common\Media\MediaWindows64.arc` and every menu asset is served from it. The
loose `.swf` files beside it are build inputs and are ignored by the game.

Inside the archive the platform skin is **renamed**, which is why grepping for
`platformskin` found nothing:

```
UIComponent_Logo::getMoviePath()   ->  "ComponentLogo1080"
  ComponentLogo1080.swf   (447 bytes, no bitmaps of its own)
    -> ImportAssets2: symbol "MenuTitle" from "platformskinHD.swf"
       -> archive entry skinHDWin.swf   (HD)
       -> archive entry skinWin.swf     (SD)
```

### Resolve the symbol; do not guess from dimensions

The last dead end, and the important lesson. `Common/Media/Graphics/MenuTitle.png`
is 571x138, and both skins contain a 571x138 bitmap, so that looked like the
match. It is not — in the HD skin that is `MenuTitleSmall`.

The mapping lives in the SWF's `SymbolClass` tag (code 76), which binds character
ids to names:

| Skin | `MenuTitle` | `MenuTitleSmall` |
|---|---|---|
| `skinHDWin.swf` | **id 180, 857x207** | id 100, 571x138 |
| `skinWin.swf` | **id 107, 571x138** | — |

At 1080p the game draws `MenuTitle` from the HD skin, i.e. **857x207, id 180** —
the variant with uppercase red "WINDOWS" over "EDITION". The 571x138 one has
*lowercase* "windows"; that difference in the screenshot was the tell, and was
misread twice as a scaling artefact.

All three were replaced, so the logo is consistent at every resolution and in
split screen. `Logo`/`LogoSmall` are separate symbols and are not bitmaps; they
were left alone.

### The archive format

`ArchiveFile.cpp` — big-endian, Java `DataInputStream` style:

```
int32   file count
per file:  uint16 name length, UTF-8 name, int32 offset, int32 size
           (a name starting with '*' means the payload is compressed)
```

`MediaWindows64.arc` holds 348 files, header ends at 10844, and every entry is
stored **contiguously in index order with no padding and none compressed**. So a
resize means rewriting every subsequent offset — the whole archive was repacked
in index order and all 348 offsets recomputed.

### The bitmap patch

Each tag was replaced in place, keeping its char id, format (5 = 32-bit ARGB) and
dimensions, so no `DefineShape`/`ImportAssets` tag needed touching. New art is
`Workspace/Title/title.png` scaled to the full width of each slot with aspect
preserved (HighQualityBicubic) and centred vertically on a transparent canvas,
then converted from straight-alpha BGRA to **premultiplied** ARGB, which is what
`DefineBitsLossless2` format 5 stores.

| Char | Size | Art drawn | Payload |
|---|---|---|---|
| `skinHDWin` id 180 | 857x207 | 857x151 at y=28 | 28444 -> 75332 |
| `skinHDWin` id 100 | 571x138 | 571x100 at y=19 | 45493 -> 40226 |
| `skinWin` id 107 | 571x138 | 571x100 at y=19 | 45493 -> 40226 |

Both patched SWFs were rebuilt from the `.orig` backups, not from the earlier
half-patched files, so the result depends on no intermediate state.

Tag lengths changed, so this is *not* a byte-length-preserving patch: tags were
re-emitted in long form, each SWF body reassembled and recompressed, and the
`CWS` header's uncompressed-length field rewritten. `id 180` grew (the new art
has more detail than the flat original), so the archive grew: 22628631 ->
22665004 bytes, md5 `dd49626d55b2cca07b03e80eda4e962c`.

Verified by re-parsing the rebuilt archive the way `ArchiveFile.cpp` does (348
files, contiguous, zero tail), then for each skin re-parsing it out of the
archive (271 / 277 tags, zero trailing bytes, SWF length field consistent),
resolving `MenuTitle` through `SymbolClass` again, and decoding *that* character
back to a PNG that matches the intended art. Checking the symbol rather than a
hardcoded id is the step that would have caught the id-100 mistake.

### Restoring the originals

**There is no git safety net here** — `MediaWindows64.arc` and the loose
`platformskin*.swf` are all **untracked**, so `git checkout --` does nothing for
them. (An earlier note in this file claimed otherwise; that was wrong.)

The pristine pre-patch SWFs are therefore kept beside the archive as
`Minecraft.Client/Common/media/skinHDWin.swf.orig` and `skinWin.swf.orig`
(md5 `e17a2b0884849f36793518a6906cab56` and `abb16870e7f413e263555a051ae19746`).
Repacking those two entries back into the archive restores the original logo.

## Staged copies

The archive is not regenerated from `Common/media` by any build step, so the
patched `MediaWindows64.arc` was copied by hand to:

- `Minecraft.Client/bin/x64/Release/Common/media/`
- `Builds/Windows-x64-Release/Common/Media/`

all three md5 `dd49626d55b2cca07b03e80eda4e962c`. The loose
`Common/Media/platformskin*.swf` were patched to match, so loose and archived
copies agree, but nothing reads them.

## `_MEOW_NATIVE_UI` — staying off, by the owner's decision

The macro is absent from every configuration in `Minecraft.Client.vcxproj`, as
`docs/changes/2026-08-27-native-ui-paused.md` describes. The owner has confirmed
they want to **stay on the Flash/Iggy renderer for now** and return to the native
UI overhaul later, so it was left off deliberately.

Consequence for this change: the logo edit made to `TitleScreen::render` (drawing
`title/meowlogo.png`) is **currently dead code**. It is correct and stays in the
tree for when the native UI is picked back up; the archive patch above is what
actually changes what you see today. The two are independent — both now show the
Meow Edition logo.

## Unverified

**Not run.** Builds clean at `Release|x64` (MSBuild, VS2022), and the archive and
both SWFs re-parse correctly with an independent parser, but only running the
game proves Iggy accepts the re-encoded bitmap and the repacked archive.

Things to watch:

- Iggy's tolerance of the re-encoded `DefineBitsLossless2` tag is untested.
- Only the `MenuTitle` symbol was touched. If any old logo turns up elsewhere,
  check the other `ComponentLogo` resolution variants and `MainMenu*.swf`.
- 348 offsets were rewritten. A mistake there would corrupt an unrelated asset,
  not the logo — so if something *else* in the menus breaks, suspect the repack.
- The SD path (`skinWin.swf`) was patched but not exercised; which of HD/SD is
  used depends on resolution.
