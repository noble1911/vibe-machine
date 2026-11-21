# Third-party notices

## AMY

- Source: https://github.com/shorepine/amy (git submodule at `third_party/amy`)
- Licence: MIT. The full text is in `third_party/amy/LICENSE` and is
  reproduced with binary distributions of Vibe Machine.
- Used for: all sound synthesis (Juno-6 and DX7 patch emulation, PCM drums,
  echo, chorus, reverb and EQ).

## Drum samples (AMY's Gamma9001 drum banks)

The drum kits (TR-808, TR-909, Linn 9000, MR-12, Tokyo Synthetics, Power,
Percussion) play the samples AMY ships in `third_party/amy/sounds/gamma9001/`,
packed at build time into `romfs/drums.bin` by AMY's own generator
(`tools/make-drums.sh`). They come with AMY; AMY's `docs/gamma9001/` describes
where they were gathered from (the Koblo Tokyo sample banks, an 808 SoundFont
and others). No separate licence for the samples is stated there.
