#!/bin/sh
# make-cia.sh -- pack the built app into a CIA installer (a HOME Menu title).
#
#   sh tools/make-cia.sh vibe-machine.elf vibe-machine.smdh romfs vibe-machine.cia
#
# Needs makerom (Project_CTR) and bannertool; set MAKEROM / BANNERTOOL if
# they aren't on PATH. The banner is the box art (cia/banner.png) with a
# little jingle (cia/banner.wav); cia/app.rsf says what the title may do.
# `make cia` runs this; so does CI (on a runner new enough for makerom).
set -e
ELF="$1"
SMDH="$2"
ROMFS="$3"
OUT="$4"
MAKEROM="${MAKEROM:-makerom}"
BANNERTOOL="${BANNERTOOL:-bannertool}"
DIR=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d)
"$BANNERTOOL" makebanner -i "$DIR/cia/banner.png" -a "$DIR/cia/banner.wav" -o "$TMP/banner.bnr"
"$MAKEROM" -f cia -o "$OUT" -elf "$ELF" -rsf "$DIR/cia/app.rsf" -icon "$SMDH" -banner "$TMP/banner.bnr" \
    -exefslogo -target t -DAPP_TITLE="Vibe Machine" -DAPP_UNIQUE_ID=0xFB5E1 -DAPP_ROMFS="$ROMFS"
rm -r "$TMP"
echo "built $OUT"
