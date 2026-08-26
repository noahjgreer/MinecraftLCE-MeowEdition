"""
Extract 4J DLC pack archives (.pck) - texture packs, GameRules, tutorials.

Format is the one documented in Minecraft.Client/Common/DLC/DLCManager.cpp
(processDLCDataFile), with struct layouts from 4JLibs/inc/4J_Storage.h:

    u32 version
    u32 paramTypeCount
    paramTypeCount * DLC_FILE_PARAM
    u32 fileCount
    fileCount * DLC_FILE_DETAILS
    per file:
        u32 paramCount
        paramCount * DLC_FILE_PARAM
        uiFileSize bytes of blob

    DLC_FILE_PARAM   { u32 dwType; u32 dwWchCount; WCHAR wchData[1]; }
    DLC_FILE_DETAILS { u32 uiFileSize; u32 dwType; u32 dwWchCount; WCHAR wchFile[1]; }

Both structs are walked as sizeof(struct) + dwWchCount*sizeof(WCHAR), so each
carries 4 bytes of trailing slack (the [1] element plus padding) after its
header. Names are counted in WCHARs and are NOT null-terminated within the
count.

The shipped console packs are big-endian (Xbox 360 / PS3 are PowerPC) even
though the game reads them with native little-endian loads, so endianness is
autodetected from the version field.

Usage:
    py -3 tools/pck_extract.py --list  <pack.pck>
    py -3 tools/pck_extract.py --out DIR <pack.pck> [more.pck ...]
"""
import argparse
import os
import struct
import sys

CURRENT_DLC_VERSION_NUM = 2
MAX_SANE_VERSION = 16

# DLCManager.h :: EDLCType
TYPE_NAMES = [
    "Skin", "Cape", "Texture", "UIData", "PackConfig", "TexturePack",
    "LocalisationData", "GameRules", "Audio", "ColourTable", "GameRulesHeader",
]

SIZEOF_PARAM = 12    # 4 + 4 + WCHAR[1], padded to 4-byte alignment
SIZEOF_DETAILS = 16  # 4 + 4 + 4 + WCHAR[1], padded to 4-byte alignment


class Reader(object):
    def __init__(self, data, endian):
        self.d = data
        self.e = endian
        self.p = 0

    def u32(self):
        v = struct.unpack_from(self.e + "I", self.d, self.p)[0]
        self.p += 4
        return v

    def peek_u32(self, off=0):
        return struct.unpack_from(self.e + "I", self.d, self.p + off)[0]

    def wstr(self, start, count):
        enc = "utf-16-be" if self.e == ">" else "utf-16-le"
        return self.d[start:start + count * 2].decode(enc, "replace")


def detect_endian(data):
    for e, label in ((">", "big"), ("<", "little")):
        v = struct.unpack_from(e + "I", data, 0)[0]
        if CURRENT_DLC_VERSION_NUM <= v <= MAX_SANE_VERSION:
            return e, label, v
    raise ValueError("no plausible version field at offset 0 in either endianness")


def read_params(r, count):
    """Read a run of DLC_FILE_PARAM; returns {dwType: string}."""
    out = {}
    for _ in range(count):
        base = r.p
        dwType = r.u32()
        n = r.u32()
        out[dwType] = r.wstr(base + 8, n)
        r.p = base + SIZEOF_PARAM + n * 2
    return out


def parse(data):
    endian, label, version = detect_endian(data)
    r = Reader(data, endian)
    r.u32()  # version
    param_types = read_params(r, r.u32())

    file_count = r.u32()
    entries = []
    for _ in range(file_count):
        base = r.p
        size = r.u32()
        dwType = r.u32()
        n = r.u32()
        name = r.wstr(base + 12, n)
        r.p = base + SIZEOF_DETAILS + n * 2
        entries.append({"size": size, "type": dwType, "name": name})

    # blob section follows the details table, in the same order
    for e in entries:
        e["params"] = read_params(r, r.u32())
        e["offset"] = r.p
        r.p += e["size"]
        if r.p > len(data):
            raise ValueError("entry %r runs past end of file (offset %d + size "
                             "%d > %d)" % (e["name"], e["offset"], e["size"], len(data)))

    return {"version": version, "endian": label, "param_types": param_types,
            "entries": entries, "trailing": len(data) - r.p}


def type_name(t):
    return TYPE_NAMES[t] if 0 <= t < len(TYPE_NAMES) else "Type%d" % t


def safe_join(root, name):
    """Join a pack-relative path to root, refusing traversal outside root."""
    rel = name.replace("\\", "/").lstrip("/")
    dest = os.path.normpath(os.path.join(root, rel))
    if os.path.commonpath([os.path.abspath(root), os.path.abspath(dest)]) != os.path.abspath(root):
        raise ValueError("refusing unsafe path in pack: %r" % name)
    return dest


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("packs", nargs="+")
    ap.add_argument("--list", action="store_true", help="list contents only")
    ap.add_argument("--out", help="extract into this directory (per-pack subdir)")
    args = ap.parse_args()
    if not args.list and not args.out:
        ap.error("pass --list or --out DIR")

    rc = 0
    for path in args.packs:
        data = open(path, "rb").read()
        try:
            info = parse(data)
        except Exception as exc:
            print("%s: FAILED - %s" % (path, exc))
            rc = 1
            continue

        print("%s  v%d %s-endian  %d files  %d param types%s"
              % (path, info["version"], info["endian"], len(info["entries"]),
                 len(info["param_types"]),
                 "" if info["trailing"] == 0 else
                 "  (%d trailing bytes)" % info["trailing"]))
        if info["param_types"]:
            print("   params: %s" % ", ".join(sorted(info["param_types"].values())))

        if args.list:
            for e in info["entries"]:
                print("   %-9s %8d  %s" % (type_name(e["type"]), e["size"], e["name"]))

        if args.out:
            root = os.path.join(args.out, os.path.splitext(os.path.basename(path))[0])
            written = 0
            for e in info["entries"]:
                name = e["name"] or ("_unnamed_%d" % written)
                dest = safe_join(root, name)
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                with open(dest, "wb") as fh:
                    fh.write(data[e["offset"]:e["offset"] + e["size"]])
                written += 1
            print("   extracted %d files to %s" % (written, root))
    return rc


if __name__ == "__main__":
    sys.exit(main())
