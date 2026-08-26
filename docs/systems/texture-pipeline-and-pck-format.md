# Texture pipeline and the `.pck` archive format

Covers where LCE textures actually live, why the atlases are "prestitched", and the
byte layout of the `.pck` DLC archives. Two tools in [`tools/`](../../tools/) act on
what is described here.

## Where textures live

Base-game art is **loose PNGs**, not packed. `Minecraft.Client/Common/res/` holds
~479 of them in the old Java layout (`gui/`, `item/`, `mob/`, `armor/`,
`environment/`, `misc/`, plus `terrain.png` / `items.png` / `particles.png`).
`Common/res/TitleUpdate/res/` is the title-update overlay of the same tree and is the
one the shipping build resolves against.

Lookup is by Java-style path string — `Textures.cpp` matches on literals like
`gui/icons.png` and `environment/clouds.png`, and
`DefaultTexturePack::getResourceImplementation` only prefixes a platform drive
(`UPDATE:\res`, `GAME:\res\TitleUpdate\res`, …) onto that name.

**Iggy/Scaleform `.swf` files are UI, not art.** `Common/Media/*.swf` are the menu
screens (`ChestMenu720.swf`, `AnvilMenu1080.swf`, …), one per screen per resolution
and split-screen variant. Nothing block- or item-related is in them.

**`.xzp` is also not textures.** Magic `XUIZ` — a Microsoft XUI resource package
holding `skin_Minecraft.xur` and `HTMLColours.col`, loaded on Xbox via
`XuiResourceLoadAll` with `section://` locators (`AbstractTexturePack.cpp`). It is
the UI theme.

## Prestitched atlases vs. the runtime stitcher

There are two `IconRegister` implementations, and **the Java one is dead code**:

| | [`TextureMap`](../../Minecraft.Client/TextureMap.cpp) | [`PreStitchedTextureMap`](../../Minecraft.Client/PreStitchedTextureMap.cpp) |
|---|---|---|
| Origin | stock Java `TextureMap` | 4J replacement |
| Stitching | runtime, via `Stitcher` | none — hardcoded UVs |
| Compiled | yes | yes |
| Instantiated | **no** | yes |

`PreStitchedTextureMap`'s header says it plainly: *"4J Added this class to stop
having to do texture stitching at runtime"* — a console load-time optimization.

`Textures.cpp:238-239` builds both maps with Java 1.5 per-file paths that are mostly
unused:

```cpp
terrain = new PreStitchedTextureMap(Icon::TYPE_TERRAIN, L"terrain", L"textures/blocks/", missingNo, true);
items   = new PreStitchedTextureMap(Icon::TYPE_ITEM,    L"items",   L"textures/items/",  missingNo, true);
```

`PreStitchedTextureMap::loadUVs()` is a ~565-line hardcoded table mapping each icon
name to a fixed cell of the 16×16 atlas grid:

```cpp
new SimpleIcon(L"apple", slotSize*10, slotSize*0, slotSize*(10+1), slotSize*(0+1))
```

Only **animated** textures were given per-file treatment, because an animation strip
cannot live in a single static cell. Before this work `textures/blocks/` held just 14
files — `fire_0`, `fire_1`, `lava`, `lava_flow`, `portal`, `water`, `water_flow`,
each with a `.txt` frame script — and `textures/items/` held `clock` and `compass`.

### Gotchas in the UV table

- **Not every icon is 1×1.** `water_flow` and `lava_flow` use `+2` (2×2 cells,
  32×32). Everything else is `+1`.
- **Names may contain spaces** — `record_where are we now`.
- **Cells are shared.** `fire_1` points at the same cell as `fire_0`;
  `potatoes_0..2` share cells with `carrots_0..2`; `glassBottle` shares with
  `potion`. These look like authoring slips, but they are what shipped.
- 4 of 256 terrain cells and 45 of 256 items cells are unreferenced.

## `.pck` archive format

Authoritative source: `processDLCDataFile` in
[`Common/DLC/DLCManager.cpp`](../../Minecraft.Client/Common/DLC/DLCManager.cpp)
(which carries 4J's own format comment), with struct layouts from
`4JLibs/inc/4J_Storage.h`.

```
u32 version                       (>= CURRENT_DLC_VERSION_NUM = 2; shipped packs are 3)
u32 paramTypeCount
paramTypeCount * DLC_FILE_PARAM   global param-name -> id mapping
u32 fileCount
fileCount * DLC_FILE_DETAILS      the file table
per file, in table order:
    u32 paramCount
    paramCount * DLC_FILE_PARAM
    uiFileSize bytes of blob

DLC_FILE_PARAM   { u32 dwType; u32 dwWchCount; WCHAR wchData[1]; }
DLC_FILE_DETAILS { u32 uiFileSize; u32 dwType; u32 dwWchCount; WCHAR wchFile[1]; }
```

Two things that will bite anyone writing a parser:

1. **Struct stride is `sizeof(struct) + dwWchCount * sizeof(WCHAR)`**, and the game
   walks it exactly that way. Because of the `[1]` array element plus 4-byte
   alignment padding, `sizeof(DLC_FILE_PARAM)` is 12 and `sizeof(DLC_FILE_DETAILS)`
   is 16 — so each record carries 4 bytes of slack after its header. `dwWchCount` is
   a WCHAR count and does **not** include a null terminator.
2. **Shipped console packs are big-endian.** Xbox 360 and PS3 are PowerPC, and the
   packs were authored in target byte order, while `processDLCDataFile` reads them
   with native `*(unsigned int *)` loads. Confirmed by the tree itself: the
   `Tutorial_Durango.pck` and `Tutorial_Orbis.pck` variants are little-endian while
   `Tutorial.pck` is big-endian. Detect from the version field; do not assume.

`dwType` is `DLCManager::EDLCType` — `0 Skin, 1 Cape, 2 Texture, 3 UIData,
4 PackConfig, 5 TexturePack, 6 LocalisationData, 7 GameRules, 8 Audio,
9 ColourTable, 10 GameRulesHeader`.

Packs **nest**: `DLC/Halo/TexturePack.pck` contains `audio.pck` and
`x16/x16Info.pck` as blobs. Names inside a pack are pack-relative paths with `/`
separators (`res/textures/blocks/fire_0.png`), mirroring the loose `res` tree.

The 16 packs in the tree parse with zero trailing bytes, which is a good structural
check that the strides above are right.

## Tools

- [`tools/atlas_slice.py`](../../tools/atlas_slice.py) — parses `loadUVs()` and cuts
  `terrain.png` / `items.png` into per-icon PNGs. `--verify` re-stitches and compares
  every claimed cell byte-for-byte against the source atlas. It refuses to run if any
  `SimpleIcon` call fails to parse, so an unhandled arg shape can never silently drop
  an icon.
- [`tools/pck_extract.py`](../../tools/pck_extract.py) — `--list` / `--out DIR` for
  any `.pck`, with endian autodetect and path-traversal refusal.

Both need Pillow and a CPython that has it; on this machine `py -3` does,
the msys2 `python` does not.

## Mipmaps

**Traced 2026-08-26. Verdict: mipmaps are not a blocker for the runtime stitcher.**
Both paths end up with a full mip chain; they just build it differently.

### Where mip data comes from

`BufferedImage` loads mip levels **from disk by filename convention**
(`BufferedImage.cpp:152`): for `foo.png` it tries `fooMipMapLevel2.png`,
`fooMipMapLevel3.png`, … into `data[1..9]`, leaving NULL where the file is absent.
So authored mips are just files sitting next to the base texture.

`Texture::transferFromImage` (`Texture.cpp:620`) then fills the chain: for each level
it uses `image->getData(level)` **if present**, and otherwise **generates** the level
by 2×2 box downsample of the level above (`crispBlend`). So:

> mip level = authored file if it exists, else generated at load time.

Nothing is generated offline, and nothing is generated at build time.

### Prestitched path

`PreStitchedTextureMap::stitch()` loads the whole atlas as one `BufferedImage` and
calls `stitchResult->transferFromImage(image)`. `terrain.png` has authored
`terrainMipMapLevel2.png` (128×128) and `terrainMipMapLevel3.png` (64×64), so terrain
levels 1–2 are 4J's authored art and levels 3–4 are generated. **`items.png` has no
mip atlas at all**, so the items map is already running entirely on generated mips
today.

### Runtime-stitcher path

Different route, same outcome:

1. `TextureManager::createTextures(filename, mipmap)` builds one `Texture` per source
   PNG. Because an image is supplied, `Texture::_init` calls `transferFromImage`, so
   **every tile gets its own full mip chain** (authored per-tile mip files if any,
   else generated).
2. `Stitcher::constructTexture` creates the atlas via `createTexture(… image=NULL …)`,
   which allocates zeroed buffers for each mip level.
3. `Texture::blit` (`Texture.cpp:374`) **loops over mip levels** — `for level < m_iMipLevels`,
   `srcBuffer = source->getData(level)`, shifting the destination by `x >> level` —
   and copies each level, breaking when the source has no data at that level.

Since step 1 guarantees the source tiles have mip data, step 3 composes a correct
atlas mip chain. Per-tile downsampling is arguably *better* than atlas-wide, since it
cannot bleed colour across cell boundaries.

### Gotchas

- **`Stitcher::MAX_MIPLEVEL = 0`** with the comment *"This should be 4 again later
  when we ACTUALLY mipmap"* looks alarming but is **not** about mip generation — it
  only feeds `MIN_TEXEL`, the packing granularity. Mip generation is entirely in
  `Texture`.
- **`Texture::fill` only writes `data[0]`** (`Texture.cpp:211`). The red `0xffff0000`
  fill the stitcher paints over unused atlas space therefore exists at level 0 only;
  higher levels stay zeroed.
- **Switching to `TextureMap` silently abandons 4J's authored terrain mips.**
  `terrainMipMapLevel2/3.png` are keyed to the whole atlas and nothing would read
  them; terrain mips would become generated. They can be preserved by slicing the mip
  atlases with the same UV table into per-tile `<name>MipMapLevel2.png` files — the
  mip atlases use the identical 16×16 grid at 8px and 4px cells.
- **Bug at `Texture.cpp:137`:** `int hh = height >> height;` should be
  `height >> level`. In the `image == NULL` branch this over-allocates every mip
  buffer (and is UB for shift counts ≥ bit width). It is harmless in practice — `blit`
  indexes consistently with its own `ww`, so writes land correctly and it only wastes
  memory — and it is **pre-existing and equally on both paths**, not a regression from
  any stitcher switch.

## Two res trees

There are two copies of the resource tree and **no build step syncs them**:

- `Minecraft.Client/Common/res/` — the source tree.
- `Builds/Windows-x64-Release/Common/res/` — the staged copy the built game reads.

They are otherwise byte-identical. Textures are plain runtime data: they are **not
compiled into the executable**, and the `.vcxproj` has no copy step for `res` (its
only post-build events are Vita SDK modules). Editing a texture therefore needs no
rebuild — but it does need the edit to reach the staged tree.
