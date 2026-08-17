#!/usr/bin/env sh
# Derive the assets BTScan ships from the master art in this directory.
#
# The masters are large and are never loaded by the application: Zune's Dtpic
# draws a picture at its native size -- there is no scaling attribute -- and
# decoding a 1254x1254 truecolour PNG through datatypes on a JIT'd m68k is a
# cost paid on every window open. So the sizes are decided here, once, and the
# results are committed next to the program that loads them.
#
# Needs ImageMagick and python3, on the build host only.
set -eu

here=$(dirname "$0")
out="$here/../ports/aros/btscan"

# The masthead. 288x192 keeps the wordmark legible beside three lines of text
# without the art taking over the window.
convert "$here/btscan-banner.png" -resize 288x192 -strip \
    -define png:compression-level=9 "$out/BTScan-banner.png"

# The icon. From the cutout master, because an icon sits on the Workbench
# backdrop and an opaque square would read as a sticker; -trim first so the
# character fills the 64x64 rather than the master's generous margin.
convert "$here/btscan-mascot-cutout.png" -trim +repage -resize 64x64 \
    -background none -gravity center -extent 64x64 -strip \
    -define png:compression-level=9 "$here/.icon.tmp.png"
python3 "$here/mkicon.py" "$here/.icon.tmp.png" "$out/BTScan.info"
rm -f "$here/.icon.tmp.png"

echo "assets written to $out"
