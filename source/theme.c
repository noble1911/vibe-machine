// theme.c -- see theme.h. Palettes are inspired by 90s Memphis patterns.

#include "theme.h"

static const Theme kThemes[] = {
    {
        .name = "PRISM",
        .sky_a = 0x2B1E8C, .sky_b = 0x18B6E0,
        .bg_a = 0x2A1A7A, .bg_b = 0x1E8FD0,
        .ink = 0x140A33, .on_bg = 0xFFFFFF, .fleck = 0xFFFFFF, .card = 0xFFF6FB,
        .pal = {0xFF2E93, 0x19D3F2, 0x2B4BD8, 0xFFA23A, 0x9B3BE0},
    },
    {
        .name = "GRAPH PAPER",
        .sky_a = 0xF8F8F8, .sky_b = 0xECECEC,
        .bg_a = 0xF8F8F8, .bg_b = 0xECECEC,
        .grid = true, .grid_rgb = 0xC9C9D6,
        .ink = 0x1C1C28, .on_bg = 0x1C1C28, .fleck = 0x1C1C28, .card = 0xFFFFFF,
        .pal = {0xFF5FA2, 0x3FD1D1, 0xE3F23A, 0x1F6FD1, 0xA88BEB},
    },
    {
        .name = "MIDNIGHT",
        .sky_a = 0x121212, .sky_b = 0x1E1530,
        .bg_a = 0x111111, .bg_b = 0x1C1C1C,
        .ink = 0x000000, .on_bg = 0xFFFFFF, .fleck = 0xFFFFFF, .card = 0xFFFFFF,
        .pal = {0xFF2E9A, 0x22D3EE, 0xFFE600, 0x7CFF3B, 0xB07CFF},
    },
    {
        .name = "TEAL POP",
        .sky_a = 0x3CCBCB, .sky_b = 0x2AA7B8,
        .bg_a = 0x3CC8C8, .bg_b = 0x2EB0B5,
        .ink = 0x111111, .on_bg = 0x111111, .fleck = 0xFFFFFF, .card = 0xFFFFFF,
        .pal = {0xFFD23F, 0xFF3E7F, 0x111111, 0xFFFFFF, 0x7B4BC4},
    },
    {
        .name = "LIME RAVE",
        .sky_a = 0x9BE53A, .sky_b = 0x6FD12A,
        .bg_a = 0x9BE53A, .bg_b = 0x78D42E,
        .ink = 0x111111, .on_bg = 0x111111, .fleck = 0xFFFFFF, .card = 0xFFFFFF,
        .pal = {0xFF2E9A, 0x1E5BFF, 0x111111, 0xFFE600, 0x2CD5E8},
    },
    {
        .name = "GRAPE SODA",
        .sky_a = 0x8E62BC, .sky_b = 0x6B3FA0,
        .bg_a = 0x8B62B8, .bg_b = 0x704BA3,
        .ink = 0x111111, .on_bg = 0xFFFFFF, .fleck = 0xFFFFFF, .card = 0xFFFFFF,
        .pal = {0x111111, 0xFFD23F, 0x4FE3E8, 0xFF4FA3, 0xFFFFFF},
    },
    {
        .name = "LEOPARD",
        .sky_a = 0xFF4FA3, .sky_b = 0xE0338A,
        .bg_a = 0xFF5AAA, .bg_b = 0xE53C90,
        .spots = true,
        .ink = 0x2A1035, .on_bg = 0x2A1035, .fleck = 0xFFFFFF, .card = 0xFFFFFF,
        .pal = {0x2CC5C8, 0x2A1035, 0x7A2FA8, 0xFFD23F, 0xFFFFFF},
    },
    {
        .name = "SUNSHINE",
        .sky_a = 0xFFD23F, .sky_b = 0xFFB21F,
        .bg_a = 0xFFCB3C, .bg_b = 0xFFB52A,
        .ink = 0x111111, .on_bg = 0x111111, .fleck = 0xFFFFFF, .card = 0xFFFFFF,
        .pal = {0x111111, 0x1F6FD1, 0xFF3E7F, 0x2CC5C8, 0xFFFFFF},
    },
    {
        .name = "VAPOR SUNSET",
        .sunset = true,
        .sky_a = 0x120A2A, .sky_b = 0xFF71CE,
        .bg_a = 0x120A2A, .bg_b = 0x2B0F4A,
        .ink = 0x0B0618, .on_bg = 0xFFFFFF, .fleck = 0xD9C6F5, .card = 0xD9C6F5,
        .pal = {0xFF71CE, 0x01CDFE, 0x05FFA1, 0xB967FF, 0xFFFB96},
    },
};

int theme_count(void) { return (int)(sizeof(kThemes) / sizeof(kThemes[0])); }

const Theme *theme_get(int index) {
    int n = theme_count();
    index %= n;
    if (index < 0) index += n;
    return &kThemes[index];
}
