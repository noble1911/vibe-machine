// wav.c -- see wav.h.

#include "wav.h"

#include <stdio.h>
#include <string.h>

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static uint32_t get32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static void header(uint8_t h[44], uint32_t frames, int rate, int channels) {
    const uint32_t bytes = frames * 2u * (uint32_t)channels;
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36 + bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    put16(h + 20, 1);  // PCM
    put16(h + 22, (uint16_t)channels);
    put32(h + 24, (uint32_t)rate);
    put32(h + 28, (uint32_t)rate * 2u * (uint32_t)channels);
    put16(h + 32, (uint16_t)(2 * channels));
    put16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    put32(h + 40, bytes);
}

bool wav_write(const char *path, const int16_t *data, int frames, int rate) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    uint8_t h[44];
    header(h, (uint32_t)frames, rate, 1);
    // Samples are little-endian on the 3DS and every build machine.
    bool ok = fwrite(h, 1, 44, f) == 44 && fwrite(data, 2, (size_t)frames, f) == (size_t)frames;
    return fclose(f) == 0 && ok;
}

bool wav_stream_open(WavStream *w, const char *path, int rate, int channels) {
    memset(w, 0, sizeof(*w));
    w->f = fopen(path, "wb");
    if (!w->f) return false;
    setvbuf(w->f, NULL, _IOFBF, 32 * 1024);  // fewer, bigger SD writes
    w->rate = rate;
    w->channels = channels;
    uint8_t h[44];
    header(h, 0, rate, channels);  // sizes patched on close
    w->ok = fwrite(h, 1, 44, w->f) == 44;
    return w->ok;
}

bool wav_stream_write(WavStream *w, const int16_t *data, int frames) {
    if (!w->f || frames <= 0) return w->ok;
    const size_t n = (size_t)frames * (size_t)w->channels;
    w->ok &= fwrite(data, 2, n, w->f) == n;
    w->frames += (uint32_t)frames;
    return w->ok;
}

bool wav_stream_close(WavStream *w) {
    if (!w->f) return false;
    uint8_t h[44];
    header(h, w->frames, w->rate, w->channels);
    w->ok &= fseek(w->f, 0, SEEK_SET) == 0 && fwrite(h, 1, 44, w->f) == 44;
    w->ok &= fclose(w->f) == 0;
    w->f = NULL;
    return w->ok;
}

int wav_read(const char *path, int16_t *data, int max_frames, int *rate) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint8_t h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
        fclose(f);
        return -1;
    }
    int channels = 0, bits = 0, n = -1;
    uint8_t ch[8];
    while (fread(ch, 1, 8, f) == 8) {
        uint32_t len = get32(ch + 4);
        if (!memcmp(ch, "fmt ", 4)) {
            uint8_t fmt[16];
            if (len < 16 || fread(fmt, 1, 16, f) != 16) break;
            if (get16(fmt) != 1) break;  // PCM only
            channels = get16(fmt + 2);
            *rate = (int)get32(fmt + 4);
            bits = get16(fmt + 14);
            fseek(f, (long)(len - 16 + (len & 1)), SEEK_CUR);
        } else if (!memcmp(ch, "data", 4)) {
            if (bits != 16 || (channels != 1 && channels != 2)) break;
            int frames = (int)(len / (2u * channels));
            if (frames > max_frames) frames = max_frames;
            n = 0;
            for (int i = 0; i < frames; i++) {
                int16_t s[2];
                if (fread(s, 2, (size_t)channels, f) != (size_t)channels) break;
                data[n++] = channels == 2 ? (int16_t)((s[0] + s[1]) / 2) : s[0];
            }
            break;
        } else {
            fseek(f, (long)(len + (len & 1)), SEEK_CUR);
        }
    }
    fclose(f);
    return n;
}
