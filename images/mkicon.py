#!/usr/bin/env python3
"""Turn a PNG into a classic Amiga Workbench icon.

Not a PNG icon. icon.library can read those -- diskobjPNGio.c hands the file to
the png datatype -- but on the image we build, picture datatypes are not
loading (AI_context/issues/ISSUE-0031.md), so an icon written that way is one
Workbench cannot open. Why they are not loading is not known, and nothing here
should be read as blaming the target: the classic format is chosen because it
is what every icon in this distribution already uses, and it depends on nothing
but icon.library.

Four colours, which is not a limitation so much as the platform: pen 0 is the
Workbench background, 1 black, 2 white, 3 blue, and those are the pens every
screen has. Blue character, white face, black outline reads well in them.

Needs Pillow-free input: the PNG is decoded with ImageMagick by the caller and
handed here as raw RGBA.
"""
import struct
import subprocess
import sys

WB_DISKMAGIC = 0xE310
WB_DISKVERSION = 1
WBTOOL = 3
GADGIMAGE = 0x0004
# GACT_RELVERIFY. Without it the gadget never reports the button being
# released, so a double-click is never a selection and the icon sits there
# doing nothing. Every icon in the distribution sets it -- Calculator and Clock
# match this generator field for field apart from exactly this one, which is
# how it was found.
GACT_RELVERIFY = 0x0001

# Pen 0 is left transparent-ish by being the screen's own background, so the
# quantiser must never choose it for the character -- it is the "outside".
PALETTE = [
    (170, 170, 170),  # 0  background   (Workbench grey)
    (0, 0, 0),        # 1  black        outlines
    (255, 255, 255),  # 2  white        face, highlights
    (60, 100, 200),   # 3  blue         body
]


def load_rgba(path, size):
    """Scale to size x size and return raw RGBA bytes, via ImageMagick."""
    out = subprocess.run(
        ["convert", path, "-trim", "+repage",
         "-resize", "%dx%d" % (size, size),
         "-background", "none", "-gravity", "center",
         "-extent", "%dx%d" % (size, size),
         "RGBA:-"],
        check=True, stdout=subprocess.PIPE).stdout
    assert len(out) == size * size * 4, len(out)
    return out


def quantise(rgba, size):
    """Nearest pen per pixel; anything transparent becomes pen 0."""
    pens = []
    for i in range(size * size):
        r, g, b, a = rgba[i * 4:i * 4 + 4]
        if a < 128:
            pens.append(0)
            continue
        best, best_d = 1, None
        for pen in (1, 2, 3):
            pr, pg, pb = PALETTE[pen]
            d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
            if best_d is None or d < best_d:
                best, best_d = pen, d
        pens.append(best)
    return pens


def planes(pens, size, depth):
    """Planar bitmap: depth planes, each row padded to a whole 16-bit word."""
    words = (size + 15) // 16
    data = bytearray()
    for plane in range(depth):
        for y in range(size):
            row = 0
            for x in range(size):
                if pens[y * size + x] >> plane & 1:
                    row |= 1 << (words * 16 - 1 - x)
            data += row.to_bytes(words * 2, "big")
    return bytes(data), words


def build(src, dst, size=46, stack=16384):
    depth = 2
    pens = quantise(load_rgba(src, size), size)
    bitmap, words = planes(pens, size, depth)

    out = bytearray()
    # struct DiskObject
    out += struct.pack(">HH", WB_DISKMAGIC, WB_DISKVERSION)
    # struct Gadget, 44 bytes
    out += struct.pack(">I", 0)                       # ga_Next
    out += struct.pack(">hhhh", 0, 0, size, size)     # left, top, width, height
    out += struct.pack(">HHH", GADGIMAGE, GACT_RELVERIFY, 1)
    out += struct.pack(">I", 1)                       # ga_GadgetRender != NULL
    out += struct.pack(">I", 0)                       # ga_SelectRender
    out += struct.pack(">I", 0)                       # ga_GadgetText
    out += struct.pack(">i", 0)                       # ga_MutualExclude
    out += struct.pack(">I", 0)                       # ga_SpecialInfo
    out += struct.pack(">H", 0)                       # ga_GadgetID
    out += struct.pack(">I", 0)                       # ga_UserData
    # rest of DiskObject
    out += struct.pack(">BB", WBTOOL, 0)              # do_Type, pad
    out += struct.pack(">I", 0)                       # do_DefaultTool
    out += struct.pack(">I", 0)                       # do_ToolTypes
    out += struct.pack(">II", 0x80000000, 0x80000000) # NO_ICON_POSITION
    out += struct.pack(">I", 0)                       # do_DrawerData
    out += struct.pack(">I", 0)                       # do_ToolWindow
    out += struct.pack(">i", stack)                   # do_StackSize
    # struct Image, 20 bytes, then its planar data
    out += struct.pack(">hhhh", 0, 0, size, size)
    out += struct.pack(">h", depth)
    out += struct.pack(">I", 1)                       # im_ImageData != NULL
    out += struct.pack(">BB", (1 << depth) - 1, 0)    # PlanePick, PlaneOnOff
    out += struct.pack(">I", 0)                       # im_Next
    out += bitmap

    open(dst, "wb").write(bytes(out))
    print("%s -> %s (%dx%d, %d planes, %d bytes)"
          % (src, dst, size, size, depth, len(out)))


if __name__ == "__main__":
    build(sys.argv[1], sys.argv[2])
