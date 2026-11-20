#!/bin/sh
# Prepare Azahar's virtual SD card so Clamshell has sound in the emulator.
#
# libctru's ndsp loads the DSP firmware from sdmc:/3ds/dspfirm.cdc before it
# starts. Azahar's default HLE audio emulates the DSP and ignores the file's
# contents, but the file still has to exist. This script writes a zero-filled
# placeholder into the EMULATOR's SD folder only.
#
# Never copy this placeholder to a real 3DS SD card: real hardware needs the
# genuine firmware, dumped from your own console (run "DSP1" once).
set -eu

case "$(uname -s)" in
    Darwin) SDMC="$HOME/Library/Application Support/Azahar/sdmc" ;;
    *) SDMC="${XDG_DATA_HOME:-$HOME/.local/share}/azahar-emu/sdmc" ;;
esac
SDMC="${1:-$SDMC}"
TARGET="$SDMC/3ds/dspfirm.cdc"

if [ -e "$TARGET" ]; then
    echo "Already present: $TARGET"
    exit 0
fi

mkdir -p "$SDMC/3ds"
head -c 4096 /dev/zero > "$TARGET"
echo "Wrote emulator-only placeholder: $TARGET"
