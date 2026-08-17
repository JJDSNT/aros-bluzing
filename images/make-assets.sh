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

# The icon, in the classic Workbench format rather than as a PNG. icon.library
# reads PNG icons through the png datatype, and picture datatypes are not
# loading on the image we build (ISSUE-0031), so a PNG icon here is one nobody
# can open. The classic format needs only icon.library.
python3 "$here/mkicon.py" "$here/btscan-mascot-cutout.png" "$out/BTScan.info"

echo "assets written to $out"
