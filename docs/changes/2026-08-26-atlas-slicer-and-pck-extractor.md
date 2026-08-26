# 2026-08-26 — Atlas slicer and `.pck` extractor

## Why

Goal is to move texture authoring to editable per-file PNGs in a Java-style
`textures/blocks/` + `textures/items/` tree, and to get at the art inside the DLC
packs. Both were assumed to require reverse-engineering an Iggy Flash blob; neither
did. See [texture pipeline and `.pck` format](../systems/texture-pipeline-and-pck-format.md)
for the findings that made this cheap.

## What changed

**New: [`tools/atlas_slice.py`](../../tools/atlas_slice.py).** Parses the hardcoded UV
table in `PreStitchedTextureMap::loadUVs()` and uses it as the cutting guide to slice
`terrain.png` and `items.png` into per-icon PNGs.

**New: [`tools/pck_extract.py`](../../tools/pck_extract.py).** Lists and extracts 4J
`.pck` DLC archives.

**Generated: 462 PNGs** written into
`Minecraft.Client/Common/res/TitleUpdate/res/textures/` — 250 under `blocks/`, 212
under `items/`. The 14 pre-existing animated PNGs and their `.txt` frame scripts were
**not** touched (the slicer skips any name in `texturesToAnimate` that already has a
file, since an animation strip must not be clobbered by a single 16×16 cell).

**No C++ was modified.** `Textures.cpp` still constructs `PreStitchedTextureMap`, so
runtime behaviour is unchanged and every platform still uses the prestitched path.
Flipping to the runtime stitcher is the next step and is deliberately not part of
this change.

## Verification

- `atlas_slice.py --verify` re-stitches the sliced tiles and compares **every claimed
  cell byte-for-byte** against the source atlas: 252/256 terrain cells and 211/256
  items cells claimed, **0 mismatched**. This is the real proof the UV table was read
  correctly.
- All 462 `SimpleIcon` entries parse. The tool hard-errors if the count of
  `new SimpleIcon(` in a block exceeds the number the regex matched — added after an
  early version *silently* skipped `water_flow` and `lava_flow` (they use `+2`, not
  `+1`) and a record name containing spaces.
- All 16 `.pck` files in the tree parse with **zero trailing bytes**.
- All 376 extracted PNGs open cleanly in Pillow; 0 unreadable.

**Not verified:** nothing was compiled and nothing was run. The sliced PNGs have
never been loaded by the game — no code path reads them yet. Whether the game still
renders correctly is unconfirmed and remains the owner's to check.

## Watch out for

- **Shared atlas cells.** `fire_1`/`fire_0`, `potatoes_0..2`/`carrots_0..2`, and
  `glassBottle`/`potion` occupy the same cells, so those sliced PNGs are byte-identical
  pairs. When the runtime stitcher is switched on these become *independent* files —
  editing one will no longer change the other. That is a genuine behaviour change and
  probably a desirable one, but it should be a conscious choice.
- **Filenames with spaces**, e.g. `record_where are we now.png`. Kept verbatim so the
  icon name ↔ filename mapping stays lossless. Quote paths in scripts.
- **Mipmaps: traced, not a blocker.** `Texture::transferFromImage` uses an authored
  `*MipMapLevel2.png` if one exists and otherwise *generates* the level by 2x2 box
  downsample, and `Texture::blit` copies mip levels into the atlas — so the runtime
  stitcher composes a correct mip chain from per-tile textures. Full detail in the
  [systems doc](../systems/texture-pipeline-and-pck-format.md#mipmaps). One caveat:
  switching to `TextureMap` abandons 4J's authored `terrainMipMapLevel2/3.png` in
  favour of generated mips, unless the mip atlases are sliced too.
- Both tools need Pillow. `py -3` has it; the msys2 `python` on PATH does not.
- The generated PNGs are new untracked files. `git clean` in that directory will
  remove them; re-run `py -3 tools/atlas_slice.py --write` to regenerate.

## Next

1. ~~Trace mipmap generation in `TextureMap` / `Stitcher`.~~ Done — see above.
2. Optionally extend the slicer to cut `terrainMipMapLevel2/3.png` into per-tile
   `<name>MipMapLevel2/3.png`, preserving 4J's authored mips instead of regenerating.
3. Switch `Textures.cpp:238-239` to `TextureMap` behind a feature macro, leaving the
   prestitched path in place for the console targets.
4. A `.pck` repacker, so edited textures can go back into DLC packs.
