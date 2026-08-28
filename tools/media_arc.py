#!/usr/bin/env python3
"""Read and repack the LCE media archives (Common/media/Media*.arc).

The game loads these, not the loose files beside them -- see
docs/systems/media-archives.md. Format is defined by
Minecraft.Client/ArchiveFile.cpp; everything is big-endian, Java
DataInputStream style:

    int32   file count
    per file:
        uint16  name length
        bytes   UTF-8 name  ('*' prefix means the payload is compressed)
        int32   offset from start of file
        int32   size

Payloads follow the header contiguously, in index order, with no padding.

Usage:
    media_arc.py list    <archive>
    media_arc.py extract <archive> <name> <outfile>
    media_arc.py replace <archive> <name> <infile>   # repacks in place

Note that names inside the archive are not always the on-disk names --
platformskinHD.swf is stored as skinHDWin.swf, for instance.
"""

import struct
import sys


def read_index(data):
    """Return [(name, offset, size, compressed)] plus the header length."""
    count, = struct.unpack_from(">I", data, 0)
    pos = 4
    entries = []
    for _ in range(count):
        length, = struct.unpack_from(">H", data, pos)
        pos += 2
        raw = data[pos:pos + length]
        pos += length
        offset, size = struct.unpack_from(">ii", data, pos)
        pos += 8
        entries.append((raw, offset, size))
    return entries, pos


def cmd_list(path):
    data = open(path, "rb").read()
    entries, header_end = read_index(data)
    print("%d files, header ends at %d, archive is %d bytes"
          % (len(entries), header_end, len(data)))
    for raw, offset, size in sorted(entries, key=lambda e: -e[2]):
        name = raw.decode("utf-8")
        flag = " (compressed)" if name.startswith("*") else ""
        print("  %-44s %10d %10d%s" % (name.lstrip("*"), offset, size, flag))


def find(entries, name):
    for index, (raw, offset, size) in enumerate(entries):
        if raw.decode("utf-8").lstrip("*") == name:
            return index, offset, size
    raise SystemExit("not in archive: %s" % name)


def cmd_extract(path, name, outfile):
    data = open(path, "rb").read()
    entries, _ = read_index(data)
    _, offset, size = find(entries, name)
    if name.startswith("*"):
        raise SystemExit("%s is compressed; decompression is not implemented "
                         "(no archive in this tree uses it)" % name)
    open(outfile, "wb").write(data[offset:offset + size])
    print("wrote %s (%d bytes)" % (outfile, size))


def cmd_replace(path, name, infile):
    data = open(path, "rb").read()
    entries, header_end = read_index(data)
    target, _, old_size = find(entries, name)
    payload = open(infile, "rb").read()

    blobs = [data[offset:offset + size] for _, offset, size in entries]
    blobs[target] = payload

    # Every offset after the replaced entry moves, so rebuild the whole index.
    header = bytearray(struct.pack(">I", len(entries)))
    body = bytearray()
    cursor = header_end
    for (raw, _, _), blob in zip(entries, blobs):
        header += struct.pack(">H", len(raw)) + raw
        header += struct.pack(">ii", cursor, len(blob))
        body += blob
        cursor += len(blob)

    # Names are unchanged, so the header must come out exactly the same length.
    assert len(header) == header_end, (len(header), header_end)

    open(path, "wb").write(bytes(header) + bytes(body))
    print("%s: %d -> %d bytes; archive %d -> %d bytes"
          % (name, old_size, len(payload), len(data), header_end + len(body)))


def main():
    args = sys.argv[1:]
    if len(args) == 2 and args[0] == "list":
        cmd_list(args[1])
    elif len(args) == 4 and args[0] == "extract":
        cmd_extract(args[1], args[2], args[3])
    elif len(args) == 4 and args[0] == "replace":
        cmd_replace(args[1], args[2], args[3])
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
