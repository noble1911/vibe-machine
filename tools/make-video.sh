#!/bin/sh
# make-video.sh -- turn a story recording into the demo video.
#
#   make clean && make STORY=1 RECORD=1     # the story build that records itself
#   (run vibe-machine.3dsx in Azahar until sdmc:/3ds/vibe-machine-story/record/done appears)
#   sh tools/make-video.sh "<azahar sdmc>/3ds/vibe-machine-story/record" docs/media
#
# The app writes both screens' raw framebuffers every few frames, with the
# time of each by its sound's clock, and the sound itself. This lines the
# frames up with the sound (repeating frames where the app was slow),
# rotates them upright, stacks the screens like a 3DS, scales them up with
# crisp pixels and adds the sound. Needs ffmpeg and python3.
set -e
REC="$1"
OUT="${2:-.}"
FPS=15
mkdir -p "$OUT"
TMP=$(mktemp -d)
python3 - "$REC" "$TMP" "$FPS" <<'PY'
import struct, sys, os
rec, tmp, fps = sys.argv[1], sys.argv[2], int(sys.argv[3])
times = open(os.path.join(rec, "times.bin"), "rb").read()
t = struct.unpack("<%dI" % (len(times) // 4), times)
sizes = {"top": 240 * 400 * 3, "bottom": 240 * 320 * 3}
step = 44100 / fps
for name, size in sizes.items():
    src = open(os.path.join(rec, name + ".raw"), "rb")
    dst = open(os.path.join(tmp, name + ".raw"), "wb")
    n = min(len(t), os.path.getsize(src.name) // size)
    frame, i, out = None, 0, 0
    while i < n:
        # Output frame `out` shows the last captured frame at or before its time.
        want = out * step
        while i < n and t[i] <= want:
            src.seek(i * size)
            frame = src.read(size)
            i += 1
        if frame is None:
            frame = b"\0" * size
        dst.write(frame)
        out += 1
        if i >= n:
            break
    print(name, "captured", n, "->", out, "frames at", fps, "fps")
PY
ffmpeg -y -loglevel error \
    -f rawvideo -pix_fmt bgr24 -s 240x400 -framerate $FPS -i "$TMP/top.raw" \
    -f rawvideo -pix_fmt bgr24 -s 240x320 -framerate $FPS -i "$TMP/bottom.raw" \
    -i "$REC/audio.wav" \
    -filter_complex "[0:v]transpose=2[t];[1:v]transpose=2,pad=400:240:40:0:color=0x111111[b];[t][b]vstack,scale=800:960:flags=neighbor,format=yuv420p[v]" \
    -map "[v]" -map 2:a -c:v libx264 -crf 34 -preset slower -tune animation -r 15 -c:a aac -b:a 160k -shortest -movflags +faststart \
    "$OUT/zero-to-song.mp4"
rm -rf "$TMP"
echo "wrote $OUT/zero-to-song.mp4"
