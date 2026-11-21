#!/bin/sh
# make-drums.sh -- AMY's Gamma9001 drum banks (all the kits but the TR-808,
# which is built into the app) as one file for the app's romfs.
#
#   sh tools/make-drums.sh third_party/amy romfs/drums.bin
#
# Runs AMY's own generator (python3) on a scratch copy, so the submodule is
# never touched. Does nothing if the file is already there.
set -e
AMY="$1"
OUT="$2"
[ -f "$OUT" ] && exit 0
TMP=$(mktemp -d)
cp -R "$AMY/amy" "$AMY/sounds" "$TMP/"
mkdir -p "$TMP/src" "$TMP/build"
(cd "$TMP" && python3 -m amy.headers gamma9001 >/dev/null)
# Its headers must be the ones AMY ships (or the blob won't match the map).
cmp -s "$TMP/src/pcm_gamma9001.h" "$AMY/src/pcm_gamma9001.h" || { echo "drums.bin doesn't match AMY's map" >&2; exit 1; }
mkdir -p "$(dirname "$OUT")"
cp "$TMP/build/drums.bin" "$OUT"
rm -r "$TMP"
echo "built $OUT"
