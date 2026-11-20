// theme.h -- the 90s colour themes. Colours are 0xRRGGBB.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define THEME_PALETTE 5

typedef struct {
    const char *name;
    bool sunset;        // top screen uses the vaporwave sunset scene
    uint32_t sky_a;     // top background gradient, top-left...
    uint32_t sky_b;     // ...to bottom-right
    uint32_t bg_a;      // bottom background gradient, top...
    uint32_t bg_b;      // ...to bottom
    bool grid;          // graph-paper lines behind everything
    uint32_t grid_rgb;
    bool spots;         // leopard spots dominate the patterns
    uint32_t ink;       // outlines, hard shadows, text on cards
    uint32_t on_bg;     // text and waveform drawn straight on the background
    uint32_t fleck;     // speckle colour
    uint32_t card;      // sticker card face
    uint32_t pal[THEME_PALETTE];
} Theme;

int theme_count(void);
const Theme *theme_get(int index);
