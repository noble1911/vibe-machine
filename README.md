# Vibe Machine

**A pocket synth and groovebox for the Nintendo 3DS** (homebrew). The
long-term goal is a playful portable instrument with a handful of synth
engines, a drum sampler, sequencers, a 4-track tape recorder and lively
animated graphics, built to fit the original 3DS.

> **Status: early prototype.** It plays 256 Juno-6 and DX7 patches from
> the buttons and touchscreen, with chords, drums, four effect sliders, a
> live waveform and a DSP load meter, a 16-step sequencer with live
> recording, eight original synth engines (one plays your microphone) with an XY "creature pad", and
> four performance FX, and a 4-track tape recorder.

**The look:** loud 90s Memphis. Nine themes (Prism, Graph Paper,
Midnight, Teal Pop, Lime Rave, Grape Soda, Leopard, Sunshine and Vapor
Sunset): layered see-through triangles, squiggles, bolts, halftone and
striped shapes with hard drop shadows, and a burst of them on every note.
Tap **THEME** on the touchscreen to switch. Turn up the **3D slider**: the
back layers sink into the screen and the note bursts fly out of it.

![The nine themes running in the Azahar emulator](docs/themes.png)

Sound comes from [AMY](https://github.com/shorepine/amy), a fixed-point
synthesis library (MIT) that also runs on the ESP32 and RP2040.

## Controls

| Input | Action |
|---|---|
| D-pad ← ↓ → ↑, then Y B A X | Eight keys: C major scale, C to C |
| Hold **L** | Chord mode: each key plays its diatonic triad |
| Hold **R** | Drum mode: kick, snare, hats, clap, rim, tom, crash |
| Circle pad ↑ / ↓ | Octave up / down |
| Touch sliders | TONE (tilt EQ), ECHO, VERB (reverb), VOL |
| Touch keys | Play (slide across keys to glide) |
| Touch THEME | Next colour theme |
| START | Play / stop the sequencer (hold START to quit) |
| Touch SOUND / SEQ tabs | Switch bottom-screen page |
| SEQ page: tap or drag cells | Paint steps (the synth row uses the last notes you played) |
| SEQ page: ▶ ● − BPM + CLR | Play/stop, arm recording, tempo, clear |
| SYNTH page: tap a tile | Pick a sound: JUNO / DX7 banks or PLUCK, SQUELCH, BELL, BREEZE, SUPERSAW, FOLD, WAVY, MIC |
| SYNTH page: drag the creature | Its X and Y are the sound's two macros (e.g. CUTOFF / RESO) |
| SYNTH page: tap a dot (banks) | Load that patch; slide across dots to audition |
| SELECT / circle pad ← → | Next sound (inside a bank, the circle pad steps patches) |
| FX page: pick CRUSH / SWEEP / STUTTER / TAPE, touch the pad | Effect on while touching; X and Y shape it |
| FX page: HOLD | Effects stay on after you lift the stylus (tap an effect again to drop it) |
| SEQ page: dice | Roll a fresh groove (weighted: kick on the one, snares on 2 and 4, synth in key) |
| SEQ page: accent row (thin row above the drums) | Accented steps hit harder |
| SEQ page: A–H | Tap: switch pattern (at the next bar while playing). Hold: copy the current pattern there |
| SEQ page: SONG | Chain the filled slots in order, bar by bar |
| SEQ page: SWING strip | 0–50% swing on the 16ths |
| SOUND page: SONGS | Save, load and manage named songs (see below) |
| SYNTH page: dice | Roll the creature's macros, or a random patch in a bank |
| SYNTH page, MIC sound: hold REC | Record up to 3 s from the 3DS microphone; then play it across the keys (C = original pitch). X picks the start point in the take, Y the tone |
| TAPE page: tap a track / M / hold a track | Arm it for recording / mute it / wipe it |
| TAPE page: ⏮ ▶ ● | Rewind, play, arm recording (records what you play, FX included, onto the armed track) |
| TAPE page: drag the tape window | Scratch the tape like a record |
| TAPE page: speed strip | −2× (reverse) to +2×, with detents at reverse, stop and normal |
| TAPE page: SEQ LINK | Tape play/rewind also start the sequencer, so takes land on the beat |

## Running it

### In the Azahar emulator (easiest)

1. Download `vibe-machine.3dsx` from the latest
   [Actions build](../../actions) (or a release), or build it yourself.
2. Azahar emulates the DSP but still needs a firmware file to exist on its
   virtual SD card. Create an **emulator-only** placeholder once:
   ```sh
   sh tools/azahar-sd-setup.sh
   ```
   Without it the app runs silently and shows "NO AUDIO".
3. Open `vibe-machine.3dsx` in Azahar (File → Load File).

The LOAD meter measures render time against real time. In the emulator it
isn't meaningful; only a real console gives a true figure.

### On a 3DS

Needs custom firmware (Luma3DS) and the Homebrew Launcher. Copy
`vibe-machine.3dsx` to `sdmc:/3ds/`. Sound needs the console's DSP firmware
at `sdmc:/3ds/dspfirm.cdc`; run the **DSP1** homebrew once to dump it from
your own console. Do not use the emulator placeholder on real hardware.

### Songs

Tap **SONGS** (the floppy disk on the SOUND page) for the song browser.
**SAVE AS** asks for a name on the 3DS keyboard (it suggests a random one,
like *NEON GECKO 7*), **SAVE** overwrites the current song, **LOAD** opens
the selected one, and **DELETE** / **NEW** need a hold, as does **LOAD**
when you have unsaved changes. The song's name sits under the title on the
top screen, with `*` while there are unsaved changes.

A song is a folder, `sdmc:/3ds/vibe-machine/songs/<NAME>/`: `song.vms`
(all 8 patterns, song mode, swing, tempo, sound and macros, sliders, theme,
FX) plus `tape1.wav` … `tape4.wav` and `mic.wav`.

### Autosave

Everything (pattern, sound, macros, sliders, tempo, theme) is saved to
`sdmc:/3ds/vibe-machine/state.vms` a moment after you change it and again
when you quit, and restored at launch.
Tape tracks are saved as `tape1.wav` … `tape4.wav` (mono, 22.05 kHz) when
the tape stops and on quit, and your MIC take as `mic.wav`, all in the same
folder, so you can pull them off the SD card into any audio editor, or drop
your own mono 16-bit WAVs in (tape tracks must be 22,050 Hz).

## Building

Requires [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the
3DS packages:

```sh
sudo dkp-pacman -S 3ds-dev
git clone --recursive https://github.com/noble1911/vibe-machine.git
cd vibe-machine
make
```

CI builds every push with the `devkitpro/devkitarm` container
([workflow](.github/workflows/build.yml)); pushing a `v*` tag publishes a
release.

## Layout

| Path | What |
|---|---|
| `source/main.c` | Frame loop; buttons and touch → notes and parameters |
| `source/engine.c` | Thread-safe wrapper around AMY (the only file that talks to AMY) |
| `source/audio_out.c` | ndsp streaming on a dedicated audio thread, load and scope stats |
| `source/music.c` | Scales, chords and the drum map (pure logic) |
| `source/seq.c` | 16-step sequencer, 8 slots, song, swing, accents; clock on the audio thread (also builds on a PC for `tests/seq_probe`) |
| `source/sounds.c` | The synth engines: AMY patch strings + two macros each |
| `source/fx.c` | Performance FX on the master output (audio thread) |
| `source/songs.c` | Named songs: folders, listing, name cleaning, random names |
| `source/tape.c` | 4-track tape (60 s, 22.05 kHz mono per track), varispeed / reverse / scrub |
| `tests/` | Host probes: build AMY natively, check every sound and FX (`make -C tests`) |
| `source/ui*.c` | Both screens (`ui_top.c`, `ui_bottom.c`), touch layout, text |
| `source/shapes.c`, `motif.c`, `theme.c` | Memphis drawing primitives, motifs and the theme table |
| `source/amy_platform_3ds.c` | Platform hooks AMY expects |
| `source/patch_names.h` | Generated by `tools/gen_patch_names.py` |
| `third_party/amy` | AMY, pinned as a git submodule |

## Credits and licence

Vibe Machine is MIT licensed (see [LICENSE](LICENSE)). It bundles
[AMY](https://github.com/shorepine/amy) by Brian Whitman and contributors,
also MIT; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

This is an independent homebrew project, not affiliated with or endorsed by
Nintendo or any synthesizer maker. Patch names refer to the factory
patches AMY recreates.
