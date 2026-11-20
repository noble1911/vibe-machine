#!/bin/sh
# amy-dualcore.sh SRC DST -- a copy of AMY's source with its two-core
# rendering switched on for us (VM_AMY_DUALCORE), as AMY does for the
# ESP32 and RP2040. The submodule itself stays untouched at its pinned
# commit; the build compiles this copy. Files are only rewritten when they
# change, so a rebuild doesn't recompile AMY for nothing.
set -e
SRC="$1"
DST="$2"
mkdir -p "$DST"
for f in "$SRC"/*.c "$SRC"/*.h; do
    b=$(basename "$f")
    if [ "$b" = "amy.h" ]; then
        sed 's/^#if (defined (ESP_PLATFORM) ||/#if (defined (VM_AMY_DUALCORE) || defined (ESP_PLATFORM) ||/' "$f" > "$DST/.amy.h.new"
        grep -q 'defined (VM_AMY_DUALCORE)' "$DST/.amy.h.new" || { echo "amy-dualcore.sh: amy.h changed upstream, update the pattern" >&2; exit 1; }
        if cmp -s "$DST/.amy.h.new" "$DST/amy.h"; then rm "$DST/.amy.h.new"; else mv "$DST/.amy.h.new" "$DST/amy.h"; fi
    elif ! cmp -s "$f" "$DST/$b"; then
        cp -p "$f" "$DST/$b"
    fi
done
