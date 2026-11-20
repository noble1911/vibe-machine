// drawwave.c -- see drawwave.h.

#include "drawwave.h"

#include <math.h>
#include <stdlib.h>

void draw_preset(DrawWave *w, int shape) {
    for (int i = 0; i < DRAW_POINTS; i++) {
        const float t = (float)i / DRAW_POINTS;
        float v;
        switch (shape) {
            case 1: v = 1.0f - 2.0f * t; break;                                // saw
            case 2: v = t < 0.5f ? 0.8f : -0.8f; break;                        // square
            case 3: v = t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t; break;  // triangle
            default: v = sinf(2.0f * (float)M_PI * t); break;                // sine
        }
        w->pt[i] = (int8_t)lrintf(v * 127.0f);
    }
}

void draw_stroke(DrawWave *w, float x0, float y0, float x1, float y1) {
    if (x1 < x0) {
        float tx = x0, ty = y0;
        x0 = x1;
        y0 = y1;
        x1 = tx;
        y1 = ty;
    }
    int a = (int)lrintf(x0 * (DRAW_POINTS - 1)), b = (int)lrintf(x1 * (DRAW_POINTS - 1));
    a = a < 0 ? 0 : a > DRAW_POINTS - 1 ? DRAW_POINTS - 1 : a;
    b = b < 0 ? 0 : b > DRAW_POINTS - 1 ? DRAW_POINTS - 1 : b;
    for (int i = a; i <= b; i++) {
        const float f = b > a ? (float)(i - a) / (b - a) : 0.0f;
        float y = y0 + (y1 - y0) * f;
        y = y < -1.0f ? -1.0f : y > 1.0f ? 1.0f : y;
        w->pt[i] = (int8_t)lrintf(y * 127.0f);
    }
}

void draw_render(const DrawWave *w, int16_t out[DRAW_CYCLE]) {
    static float tmp[DRAW_CYCLE];
    double mean = 0.0;
    for (int i = 0; i < DRAW_CYCLE; i++) {
        // Catmull-Rom through the points, wrapping round (it's a loop).
        const float pos = (float)i * DRAW_POINTS / DRAW_CYCLE;
        const int k = (int)pos;
        const float f = pos - k;
        const float p0 = w->pt[(k + DRAW_POINTS - 1) % DRAW_POINTS], p1 = w->pt[k % DRAW_POINTS],
                    p2 = w->pt[(k + 1) % DRAW_POINTS], p3 = w->pt[(k + 2) % DRAW_POINTS];
        const float v = p1 + 0.5f * f * (p2 - p0 + f * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + f * (3.0f * (p1 - p2) + p3 - p0)));
        tmp[i] = v / 127.0f;
        mean += tmp[i];
    }
    mean /= DRAW_CYCLE;
    float peak = 0.0f;
    for (int i = 0; i < DRAW_CYCLE; i++) {
        tmp[i] -= (float)mean;
        if (fabsf(tmp[i]) > peak) peak = fabsf(tmp[i]);
    }
    const float g = peak > 0.001f ? 0.9f / peak : 0.0f;  // flat: silence
    for (int i = 0; i < DRAW_CYCLE; i++) out[i] = (int16_t)lrintf(tmp[i] * g * 32767.0f);
}
