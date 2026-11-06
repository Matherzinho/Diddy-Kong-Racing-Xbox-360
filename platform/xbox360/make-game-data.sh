#!/bin/bash
# dkr360: makes the game's data from YOUR OWN cartridge dump: dkr.z64 (big-endian), into OUTDIR.
#   platform/xbox360/make-game-data.sh <rom: Diddy Kong Racing US/EU 1.0 or 1.1, .z64/.n64/.v64> <outdir>
# Golden Balloon reads everything from the ROM at runtime, so this only checks it and puts it in the N64's own byte
# order (dumps named .n64 are often .v64 inside: the first four bytes say which). The ROM isn't changed. Never share
# what comes out: it is the game.
set -euo pipefail
ROM="${1:?usage: make-game-data.sh <rom> <outdir>}"
OUT="${2:?usage: make-game-data.sh <rom> <outdir>}"
mkdir -p "$OUT"
[ ! -e "$OUT/dkr.z64" ] || { echo "$OUT/dkr.z64 already exists; nothing was overwritten" >&2; exit 1; }
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cat >"$TMP/convert.py" <<'EOF'
import hashlib, sys
data = bytearray(open(sys.argv[1], "rb").read())
magic = bytes(data[:4])
if magic == bytes.fromhex("37804012"):      # .v64: 16-bit words swapped
    data[0::2], data[1::2] = data[1::2], data[0::2]
elif magic == bytes.fromhex("40123780"):    # .n64: 32-bit words little-endian
    data[0::4], data[1::4], data[2::4], data[3::4] = data[3::4], data[2::4], data[1::4], data[0::4]
elif magic != bytes.fromhex("80371240"):
    sys.exit("this isn't an N64 ROM")
title = bytes(data[0x20:0x34]).decode("ascii", "replace").strip()
code = bytes(data[0x3B:0x3F]).decode("ascii", "replace")
if not code.startswith("NDY"):
    sys.exit(f"this isn't Diddy Kong Racing: {title!r} {code!r}")
# The decompilation's verification hashes (Golden Balloon docs/ROM_REVISIONS.md names the builds).
known = {
    "0cb115d8716dbbc2922fda38e533b9fe63bb9670": "us.v77 (USA 1.0)",
}
sha1 = hashlib.sha1(data).hexdigest()
revision = data[0x3F] & 0x0F
region = {"E": "USA", "P": "Europe", "J": "Japan"}.get(code[3], code[3])
print(f"ROM: {title}, {region} 1.{revision}, sha1 {sha1}" + (f" = {known[sha1]}" if sha1 in known else ""))
if code[3] == "J":
    print("warning: the Japanese ROM is the least tested one in Golden Balloon")
open(sys.argv[2], "wb").write(data)
EOF
python3 -I "$TMP/convert.py" "$ROM" "$OUT/dkr.z64"
ls -la "$OUT/dkr.z64"
