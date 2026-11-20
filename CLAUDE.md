# Vibe Machine: notes for contributors and agents

A 3DS homebrew synth / groovebox. C11, devkitARM + libctru + citro2d, with
the AMY synth library as a pinned git submodule.

## Build and run

```sh
make                       # -> vibe-machine.3dsx (needs devkitPro 3ds-dev)
make clean
sh tools/azahar-sd-setup.sh   # once: emulator-only DSP placeholder
open -a /path/to/Azahar.app vibe-machine.3dsx
```

CI: `.github/workflows/build.yml` builds in `devkitpro/devkitarm` and
uploads `vibe-machine.3dsx` as an artifact; `v*` tags publish a release.

## Target hardware: original 3DS

- ARM11 at 268 MHz. Audio CPU is the constraint, not graphics. Design every
  feature for the original model; the New 3DS is a bonus, not the target.
- About 64 MB of app memory.
- The touchscreen is resistive and reports **one** point. No multi-touch
  chords: chords come from buttons (hold L).
- The DSP mixes 24 hardware channels. Plain sample playback (drums, tape)
  should eventually use extra ndsp channels instead of CPU.

## Threading rules

- `engine.c` is the **only** file that calls AMY. Everything goes through
  its functions, which take one `LightLock`.
- The audio thread (`audio_out.c`) runs on the app core at a higher priority
  than the UI. Never block it on the UI, the filesystem or the network.
- ndsp's callback only signals an event; the refill happens on our thread.
- The sequencer clock (`seq_advance`) runs on the audio thread once per AMY
  block (128 samples, ~2.9 ms), so steps follow the sample clock. It must
  never call into UI code; the UI polls `seq_status()` / `seq_snapshot()`
  once per frame instead.

## Conventions

- Keep logic that doesn't touch hardware (like `music.c`) free of 3DS and
  AMY includes, so it can be unit-tested on a PC later.
- After bumping the AMY submodule, run `python3 tools/gen_patch_names.py`
  and commit the regenerated `source/patch_names.h`.
- AMY's sample rate is fixed at 44.1 kHz upstream; ndsp resamples to the
  DSP's native ~32.7 kHz. Rendering at 32.7 kHz would save about 25% CPU,
  but needs an upstream change to AMY (`AMY_SAMPLE_RATE` cannot currently be
  overridden).
- LOAD numbers from Azahar are not real-hardware numbers.
- Visual style is loud 90s Memphis with switchable themes (`theme.c`).
  Draw with `shapes.c` / `motif.c` and theme colours, never hard-coded
  ones (the Vapor Sunset scene is the one exception). The top screen is
  stereoscopic; give every new layer a depth in `ui_top.c` (positive =
  behind the screen).
- `make DEMO=1` builds a silent attract mode that cycles themes, handy for
  screenshots in Azahar. Never ship it.
- Avoid citro2d's ellipse/circle primitive for large shapes: it fails to
  draw at large radii in Azahar. Build big round shapes as triangle fans.
- Original visual design only: no copying another product's graphics,
  characters or names.
