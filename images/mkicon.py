#!/usr/bin/env python3
"""Turn a PNG into an AROS/OS4 PNG icon by inserting the icOn chunk.

icon.library reads a .info that is a plain PNG carrying an ancillary "icOn"
chunk; without the chunk the image still loads but do_Type stays 0 (invalid),
which is not a tool Workbench will launch.  The chunk is a flat sequence of
big-endian (attribute, value) pairs -- see the parser in
external/aros/workbench/libs/icon/diskobjPNGio.c.
"""
import struct
import sys
import zlib

ATTR_STACKSIZE = 0x80001009
ATTR_TYPE = 0x8000100F
WBTOOL = 3


def chunks(data):
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos = 8
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        yield kind, data[pos:pos + 12 + length]
        pos += 12 + length


def make_chunk(kind, payload):
    return (struct.pack(">I", len(payload)) + kind + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))


def main(src, dst, stack=16384):
    data = open(src, "rb").read()
    icon = struct.pack(">II", ATTR_TYPE, WBTOOL) + struct.pack(
        ">II", ATTR_STACKSIZE, stack)
    out = bytearray(data[:8])
    for kind, raw in chunks(data):
        if kind == b"IEND":
            out += make_chunk(b"icOn", icon)
        out += raw
    open(dst, "wb").write(bytes(out))
    print("%s -> %s (%d bytes)" % (src, dst, len(out)))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
