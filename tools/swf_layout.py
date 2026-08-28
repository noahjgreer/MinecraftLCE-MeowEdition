#!/usr/bin/env python3
"""
Dump the authored layout of an LCE menu .swf.

Why this exists
---------------
The native UI migration (docs/systems/native-ui-migration.md) is replacing the
Iggy/Flash menus with plain C++ and PNGs, and the goal is to keep the console
layout exactly, not to invent a new one. Every menu's geometry - where each
button sits, how big it is, what the stage size is - lives only inside the .swf,
which is why a hand-written screen ends up looking like Java Edition instead of
LCE.

These files turn out to be ordinary zlib-compressed SWF ("CWS") containing no
bitmaps at all: just ActionScript (DoABC) and vector shapes, with each control
positioned by a PlaceObject2/3 matrix. So the authored coordinates can be read
straight out of them.

Usage
-----
    python tools/swf_layout.py <file.swf> [...]
    python tools/swf_layout.py Builds/Windows-x64-Release/Common/Media/MainMenu720.swf

Coordinates are printed in pixels (SWF stores twips - 20 per pixel), in the
movie's own coordinate space. That is the same space UIScene::getMovieWidth /
getMovieHeight report, so the numbers can be used directly by a native Screen
that scales movie space to render space the way UIControl does.
"""

import struct
import sys
import zlib

TWIPS = 20.0

TAG_NAMES = {
    0: "End", 1: "ShowFrame", 2: "DefineShape", 9: "SetBackgroundColor",
    22: "DefineShape2", 26: "PlaceObject2", 32: "DefineShape3", 39: "DefineSprite",
    43: "FrameLabel", 69: "FileAttributes", 70: "PlaceObject3", 76: "SymbolClass",
    82: "DoABC", 83: "DefineShape4", 86: "DefineSceneAndFrameLabelData",
}


class Bits:
    """MSB-first bit reader, which is how SWF packs RECT and MATRIX."""

    def __init__(self, data, pos=0):
        self.d = data
        self.byte = pos
        self.bit = 0

    def align(self):
        if self.bit:
            self.bit = 0
            self.byte += 1

    def ub(self, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | ((self.d[self.byte] >> (7 - self.bit)) & 1)
            self.bit += 1
            if self.bit == 8:
                self.bit = 0
                self.byte += 1
        return v

    def sb(self, n):
        if n == 0:
            return 0
        v = self.ub(n)
        if v & (1 << (n - 1)):          # sign-extend
            v -= (1 << n)
        return v

    def fb(self, n):
        """Signed 16.16 fixed point."""
        return self.sb(n) / 65536.0


def read_rect(b):
    nb = b.ub(5)
    xmin, xmax, ymin, ymax = b.sb(nb), b.sb(nb), b.sb(nb), b.sb(nb)
    b.align()
    return xmin / TWIPS, xmax / TWIPS, ymin / TWIPS, ymax / TWIPS


def read_matrix(b):
    """Returns (scaleX, scaleY, translateX_px, translateY_px)."""
    sx = sy = 1.0
    if b.ub(1):
        n = b.ub(5)
        sx, sy = b.fb(n), b.fb(n)
    if b.ub(1):                          # rotate/skew - read past it
        n = b.ub(5)
        b.fb(n), b.fb(n)
    n = b.ub(5)
    tx, ty = b.sb(n), b.sb(n)
    b.align()
    return sx, sy, tx / TWIPS, ty / TWIPS


def read_string(d, i):
    j = d.index(b"\0", i)
    return d[i:j].decode("utf-8", "replace"), j + 1


def decompress(path):
    raw = open(path, "rb").read()
    sig = raw[:3]
    if sig == b"CWS":
        return raw[:8] + zlib.decompress(raw[8:])
    if sig == b"FWS":
        return raw
    raise ValueError("not a SWF (signature %r)" % sig)


def iter_tags(body, i):
    while i < len(body) - 1:
        (rec,) = struct.unpack_from("<H", body, i)
        i += 2
        code, length = rec >> 6, rec & 0x3F
        if length == 0x3F:
            (length,) = struct.unpack_from("<I", body, i)
            i += 4
        yield code, body[i:i + length]
        if code == 0:
            return
        i += length


def parse_place(data, is_v3):
    """PlaceObject2/3 -> (name, char_id, matrix) with anything unknown skipped."""
    i = 0
    flags = data[i]
    i += 1
    if is_v3:
        flags2 = data[i]
        i += 1
        has_class_name = flags2 & 0x08
    else:
        has_class_name = False

    i += 2                                              # depth
    if has_class_name:
        _, i = read_string(data, i)

    char_id = None
    if flags & 0x02:                                    # HasCharacter
        (char_id,) = struct.unpack_from("<H", data, i)
        i += 2

    matrix = None
    if flags & 0x04:                                    # HasMatrix
        b = Bits(data, i)
        matrix = read_matrix(b)
        i = b.byte

    if flags & 0x08:                                    # HasColorTransform
        b = Bits(data, i)
        has_add, has_mul = b.ub(1), b.ub(1)
        n = b.ub(4)
        for _ in range((4 if has_mul else 0) + (4 if has_add else 0)):
            b.sb(n)
        b.align()
        i = b.byte

    if flags & 0x10:                                    # HasRatio
        i += 2

    name = None
    if flags & 0x20:                                    # HasName
        name, i = read_string(data, i)

    return name, char_id, matrix


def dump(path):
    body = decompress(path)
    b = Bits(body, 8)
    stage = read_rect(b)
    i = b.byte + 4                                      # frameRate + frameCount

    print("=== %s" % path)
    print("    stage %.0f x %.0f px" % (stage[1] - stage[0], stage[3] - stage[2]))

    symbols = {}
    placements = []

    def walk(tags, depth):
        for code, data in tags:
            if code in (26, 70):
                try:
                    name, cid, mat = parse_place(data, code == 70)
                except Exception as exc:
                    print("    (place parse failed: %s)" % exc)
                    continue
                if name or mat:
                    placements.append((depth, name, cid, mat))
            elif code == 39 and depth == 0:             # DefineSprite: recurse
                cid, frames = struct.unpack_from("<HH", data, 0)
                walk(iter_tags(data, 4), depth + 1)
            elif code == 76:                            # SymbolClass
                (count,) = struct.unpack_from("<H", data, 0)
                j = 2
                for _ in range(count):
                    (sid,) = struct.unpack_from("<H", data, j)
                    j += 2
                    nm, j = read_string(data, j)
                    symbols[sid] = nm

    walk(iter_tags(body, i), 0)

    if symbols:
        print("    classes: %s" % ", ".join(sorted(symbols.values())))

    if not placements:
        print("    (no placements found)")
        return

    print("    %-6s %-22s %-6s %9s %9s %7s %7s"
          % ("depth", "name", "char", "x", "y", "sx", "sy"))
    for depth, name, cid, mat in placements:
        sx, sy, tx, ty = mat if mat else (1.0, 1.0, 0.0, 0.0)
        print("    %-6d %-22s %-6s %9.1f %9.1f %7.3f %7.3f"
              % (depth, name or "-", cid if cid is not None else "-", tx, ty, sx, sy))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    for p in sys.argv[1:]:
        try:
            dump(p)
        except Exception as exc:
            print("=== %s\n    ERROR: %s" % (p, exc))
    return 0


if __name__ == "__main__":
    sys.exit(main())
