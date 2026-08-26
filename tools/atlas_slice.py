"""
Slice the LCE prestitched atlases (terrain.png / items.png) into per-icon PNGs
laid out Java-style under textures/blocks/ and textures/items/.

The cutting guide is the hardcoded UV table in
Minecraft.Client/PreStitchedTextureMap.cpp :: loadUVs(), which maps every icon
name to a fixed cell of the 16x16 atlas grid. We parse that table rather than
guessing, so the slice is exact.

Usage:
    py -3 tools/atlas_slice.py --verify
    py -3 tools/atlas_slice.py --write
"""
import argparse
import os
import re
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow required:  py -3 -m pip install Pillow")

REPO = os.path.dirname(os.path.abspath(os.path.join(__file__, "..")))
SRC = os.path.join(REPO, "Minecraft.Client", "PreStitchedTextureMap.cpp")
RES = os.path.join(REPO, "Minecraft.Client", "Common", "res", "TitleUpdate", "res")

GRID = 16

# new SimpleIcon(L"name",slotSize*A,slotSize*B,slotSize*(A+W),slotSize*(B+H))
# W/H are usually 1, but the flow textures (water_flow, lava_flow) are 2x2 cells.
ICON_RE = re.compile(
    r'new\s+SimpleIcon\(\s*L"([^"]+)"\s*,'
    r'\s*slotSize\s*\*\s*([0-9]+)\s*,'
    r'\s*slotSize\s*\*\s*([0-9]+)\s*,'
    r'\s*slotSize\s*\*\s*\(\s*([0-9]+)\s*\+\s*([0-9]+)\s*\)\s*,'
    r'\s*slotSize\s*\*\s*\(\s*([0-9]+)\s*\+\s*([0-9]+)\s*\)\s*\)'
)
COUNT_RE = re.compile(r'new\s+SimpleIcon\(')
ANIM_RE = re.compile(r'texturesToAnimate\.push_back\(\s*pair<wstring,\s*wstring>\(\s*L"([^"]+)"\s*,\s*L"([^"]+)"\s*\)\s*\)')
FLAG_RE = re.compile(r'texturesByName\[L"([A-Za-z0-9_]+)"\]->setFlags\(([^)]+)\)')


def load_uv_body():
    """Return (items_block, terrain_block) source text of loadUVs()."""
    text = open(SRC, "r", encoding="utf-8", errors="replace").read()
    start = text.index("void PreStitchedTextureMap::loadUVs()")
    split = text.index("if(iconType != Icon::TYPE_TERRAIN)", start)
    # the terrain branch is the `else` at brace depth 1 of the if/else pair
    depth = 0
    i = text.index("{", split)
    while True:
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                break
        i += 1
    items_block = text[split:i]
    end = text.index("\n}", i)
    terrain_block = text[i:end]
    return items_block, terrain_block


def parse_block(block, label):
    """name -> (u, v, w, h) in atlas cells."""
    icons, dupes = {}, []
    for m in ICON_RE.finditer(block):
        name = m.group(1)
        u0, v0, u1b, w, v1b, h = map(int, m.groups()[1:])
        if u1b != u0 or v1b != v0:
            sys.exit("%s: %s has inconsistent UV base (%d,%d vs %d,%d)"
                     % (label, name, u0, v0, u1b, v1b))
        if name in icons and icons[name] != (u0, v0, w, h):
            sys.exit("%s: %s declared twice with different cells" % (label, name))
        for other, cell in icons.items():
            if cell[:2] == (u0, v0) and other != name:
                dupes.append((name, other, u0, v0))
                break
        icons[name] = (u0, v0, w, h)

    # Every SimpleIcon in the block must have parsed. A silent skip here would
    # quietly drop an icon from the slice, so treat it as fatal.
    seen = len(COUNT_RE.findall(block))
    if seen != len(ICON_RE.findall(block)):
        sys.exit("%s: %d SimpleIcon calls but only %d parsed - unhandled arg "
                 "shape; fix ICON_RE before trusting the slice"
                 % (label, seen, len(ICON_RE.findall(block))))

    anims = {m.group(1): m.group(2) for m in ANIM_RE.finditer(block)}
    flags = {m.group(1): m.group(2).strip() for m in FLAG_RE.finditer(block)}
    return icons, anims, flags, dupes


MAPS = [
    ("terrain", "terrain.png", os.path.join("textures", "blocks")),
    ("items", "items.png", os.path.join("textures", "items")),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true", help="write sliced PNGs")
    ap.add_argument("--verify", action="store_true", help="re-stitch and compare to source atlas")
    ap.add_argument("--out", default=RES, help="output root (default: the TitleUpdate res tree)")
    args = ap.parse_args()
    if not (args.write or args.verify):
        ap.error("pass --write and/or --verify")

    items_block, terrain_block = load_uv_body()
    blocks = {"items": items_block, "terrain": terrain_block}

    rc = 0
    for label, atlas_name, subdir in MAPS:
        icons, anims, flags, dupes = parse_block(blocks[label], label)
        atlas = Image.open(os.path.join(RES, atlas_name)).convert("RGBA")
        cw, ch = atlas.width // GRID, atlas.height // GRID
        print("[%s] %s %dx%d  cell=%dx%d  icons=%d  animated=%d  flagged=%d"
              % (label, atlas_name, atlas.width, atlas.height, cw, ch,
                 len(icons), len(anims), len(flags)))
        for name, other, u, v in dupes:
            print("   note: %s shares cell (%d,%d) with %s" % (name, u, v, other))

        outdir = os.path.join(args.out, subdir)
        if args.write:
            os.makedirs(outdir, exist_ok=True)

        tiles = {}
        for name, (u, v, w, h) in sorted(icons.items()):
            box = (u * cw, v * ch, (u + w) * cw, (v + h) * ch)
            tile = atlas.crop(box)
            tiles[name] = (tile, box)
            if args.write:
                path = os.path.join(outdir, name + ".png")
                # animated icons already ship as per-file strips + .txt frame
                # scripts; never clobber those with a single atlas cell.
                if name in anims and os.path.exists(path):
                    continue
                tile.save(path)

        if args.verify:
            rebuilt = Image.new("RGBA", atlas.size, (0, 0, 0, 0))
            covered = set()
            for name, (tile, box) in tiles.items():
                rebuilt.paste(tile, box[:2])
                for du in range(box[0] // cw, box[2] // cw):
                    for dv in range(box[1] // ch, box[3] // ch):
                        covered.add((du, dv))
            # compare only the cells the UV table actually claims; unclaimed
            # atlas cells are unreferenced art and are expected to differ.
            bad = []
            for (u, v) in sorted(covered):
                b = (u * cw, v * ch, (u + 1) * cw, (v + 1) * ch)
                if atlas.crop(b).tobytes() != rebuilt.crop(b).tobytes():
                    bad.append((u, v))
            total = GRID * GRID
            print("   verify: %d/%d cells claimed, %d unclaimed, %d mismatched"
                  % (len(covered), total, total - len(covered), len(bad)))
            if bad:
                print("   MISMATCH at cells: %s" % bad[:20])
                rc = 1
            else:
                print("   OK - every claimed cell round-trips byte-exact")

        if args.write:
            print("   wrote %d PNGs to %s" % (len(tiles), outdir))
    return rc


if __name__ == "__main__":
    sys.exit(main())
