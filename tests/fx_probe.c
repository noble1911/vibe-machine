// fx_probe -- exercises fx.c on the build machine with a known test tone.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fx.h"
#include "punch.h"

#define RATE 44100
#define BLOCK 128

static long s_t;  // test-signal clock, in frames

// A low and a high sine plus a slow ramp, so every repeat window differs.
static void test_tone(int16_t *buf, int frames) {
    for (int i = 0; i < frames; i++, s_t++) {
        double t = (double)s_t / RATE;
        double v = 0.3 * sin(2 * M_PI * 220 * t) + 0.2 * sin(2 * M_PI * 3520 * t) + 0.1 * sin(2 * M_PI * 0.7 * t);
        buf[2 * i] = buf[2 * i + 1] = (int16_t)(v * 32767);
    }
}

typedef struct {
    double rms, bright;
    int distinct;
} M;

static int16_t s_out[RATE * 4 * 2];

// Run `secs` of audio through the effects; keep the output in s_out.
static M run(double secs) {
    int frames = (int)(secs * RATE);
    double sq = 0, ab = 0, sl = 0;
    int prev = 0;
    static unsigned char seen[65536];
    memset(seen, 0, sizeof(seen));
    int distinct = 0;
    for (int done = 0; done < frames; done += BLOCK) {
        int16_t *b = s_out + 2 * done;
        test_tone(b, BLOCK);
        fx_process(b, BLOCK);
        for (int i = 0; i < BLOCK; i++) {
            int x = b[2 * i];
            if (isnan((double)x)) abort();
            sq += (double)x * x;
            ab += abs(x);
            sl += abs(x - prev);
            prev = x;
            if (!seen[x + 32768]) {
                seen[x + 32768] = 1;
                distinct++;
            }
        }
    }
    M m = {sqrt(sq / frames) / 32768.0, ab > 0 ? sl / ab : 0, distinct};
    return m;
}

static int fails;
static void check(int ok, const char *what) {
    printf("%-58s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

// ------------------------------------------------------------ PUNCH ----
// A pretend sequencer drives punch_clock() the way audio_out.c does: a 1/16
// fires at the start of the block in which its time has come.

#define P_SECS 12
static int16_t p_in[RATE * P_SECS * 2], p_out[RATE * P_SECS * 2];
static long p_n;  // frames run so far
static double p_pos;
static int p_step = -1;
static bool p_playing = true;
static float p_bpm = 120.0f;
static uint16_t p_pat[16];  // a bar of sequenced punch-ins
static long p_tick_at[2048];
static int p_tick_step[2048], p_nticks;
enum { SIG_NOISE, SIG_SINE110, SIG_SINE220, SIG_SINE3520, SIG_SILENCE };
static int p_sig = SIG_NOISE;
static uint32_t p_rng = 12345u;

static void p_signal(int16_t *b, long t0, int frames) {
    for (int i = 0; i < frames; i++) {
        double t = (double)(t0 + i) / RATE, l = 0, r = 0;
        switch (p_sig) {
            case SIG_NOISE:  // never repeats: any slice of it is unique
                p_rng ^= p_rng << 13, p_rng ^= p_rng >> 17, p_rng ^= p_rng << 5;
                l = ((int)(p_rng & 0xFFFF) - 32768) / 32768.0 * 0.3;
                r = ((int)(p_rng >> 16) - 32768) / 32768.0 * 0.3;
                break;
            case SIG_SINE110: l = r = 0.25 * sin(2 * M_PI * 110 * t); break;
            case SIG_SINE220: l = r = 0.4 * sin(2 * M_PI * 220 * t); break;
            case SIG_SINE3520: l = r = 0.4 * sin(2 * M_PI * 3520 * t); break;
            default: break;
        }
        b[2 * i] = (int16_t)lrint(l * 32767);
        b[2 * i + 1] = (int16_t)lrint(r * 32767);
    }
}

static void p_run(double secs) {
    const long frames = (long)(secs * RATE);
    const double S = RATE * 60.0 / p_bpm / 4.0;
    for (long done = 0; done < frames && p_n + BLOCK <= (long)RATE * P_SECS; done += BLOCK) {
        bool tick = false;
        if (p_playing && p_pos >= S) {
            p_pos -= S;
            tick = true;
            p_step = (p_step + 1) & 15;
            if (p_nticks < 2048) p_tick_at[p_nticks] = p_n, p_tick_step[p_nticks++] = p_step;
        }
        punch_clock(tick, p_step, p_playing, tick ? p_pat[p_step] : 0, p_bpm);
        int16_t *in = p_in + 2 * p_n, *out = p_out + 2 * p_n;
        p_signal(in, p_n, BLOCK);
        memcpy(out, in, BLOCK * 2 * sizeof(int16_t));
        punch_process(out, BLOCK);
        p_n += BLOCK;
        if (p_playing) p_pos += BLOCK;
    }
}

static void p_reset(int sig) {
    punch_init(RATE);
    memset(p_pat, 0, sizeof(p_pat));
    p_n = 0;
    p_bpm = 120.0f;
    p_pos = RATE * 60.0 / p_bpm / 4.0;  // the first block fires step 0
    p_step = -1;
    p_nticks = 0;
    p_playing = true;
    p_sig = sig;
}

static long p_first_tick_after(long frame) {
    for (int i = 0; i < p_nticks; i++)
        if (p_tick_at[i] >= frame) return p_tick_at[i];
    return -1;
}
static int p_tick_index(long frame) {
    for (int i = 0; i < p_nticks; i++)
        if (p_tick_at[i] == frame) return i;
    return -1;
}
// First frame at or after `from` where the output isn't the input.
static long p_first_diff(long from) {
    for (long i = from; i < p_n; i++)
        if (p_out[2 * i] != p_in[2 * i] || p_out[2 * i + 1] != p_in[2 * i + 1]) return i;
    return -1;
}
static bool p_same(const int16_t *a, const int16_t *b, long frames) { return memcmp(a, b, (size_t)frames * 4) == 0; }
static double p_rms(const int16_t *b, long frames) {
    double sq = 0;
    for (long i = 0; i < frames * 2; i++) sq += (double)b[i] * b[i];
    return sqrt(sq / (frames * 2)) / 32768.0;
}
// Power of `hz` in a stretch of the left channel (Goertzel).
static double p_power(const int16_t *b, long frames, double hz) {
    const double w = 2 * cos(2 * M_PI * hz / RATE);
    double s1 = 0, s2 = 0;
    for (long i = 0; i < frames; i++) {
        const double s0 = b[2 * i] / 32768.0 + w * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return (s1 * s1 + s2 * s2 - w * s1 * s2) / ((double)frames * frames);
}
static int p_crossings(const int16_t *b, long frames) {
    int n = 0;
    for (long i = 1; i < frames; i++) n += (b[2 * i - 2] < 0) != (b[2 * i] < 0);
    return n;
}
static double p_max_step(const int16_t *b, long frames) {
    double m = 0;
    for (long i = 1; i < frames; i++)
        for (int c = 0; c < 2; c++) {
            const double d = fabs((double)b[2 * i + c] - b[2 * i - 2 + c]) / 32768.0;
            if (d > m) m = d;
        }
    return m;
}

static void punch_checks(void) {
    const long beat = lrint(RATE * 60.0 / 120.0), S16 = beat / 4;  // 22050, 5512
    const long F = (long)(0.006f * RATE);
    char what[96];

    // Nothing held: bit for bit.
    p_reset(SIG_NOISE);
    p_run(0.7);
    check(p_same(p_in, p_out, p_n), "PUNCH: bypass passes audio through unchanged");

    // A press lands on the next 1/16, not when the stylus came down. (The
    // crossfade starts there: its first frame is still all dry.)
    while (p_n < p_tick_at[p_nticks - 1] + S16 / 3) p_run((double)BLOCK / RATE);
    const long press = p_n;  // (a third of the way through a 16th)
    punch_set_live(PUNCH_BIT(PUNCH_REVERSE));
    p_run(0.9);
    const long tick = p_first_tick_after(press), dev = p_first_diff(press);
    printf("  pressed at %ld, next 1/16 at %ld, first change at %ld\n", press, tick, dev);
    check(tick > press && (dev == tick || dev == tick + 1), "PUNCH: engages exactly on the next 1/16");
    // REVERSE: the beat before, backwards (after the short fade in).
    bool rev = true;
    for (long j = F; j < beat - 1 && rev; j++)
        rev = memcmp(&p_out[2 * (tick + j)], &p_in[2 * (tick - 1 - j)], 4) == 0;
    check(rev, "PUNCH: REVERSE plays the last beat backwards, sample for sample");
    punch_set_live(0);
    p_run(0.3);
    long rel = p_first_tick_after(tick + 1);
    while (rel >= 0 && rel < tick + (long)(0.9 * RATE)) rel = p_first_tick_after(rel + 1);
    check(rel > 0 && p_same(&p_in[2 * (rel + F)], &p_out[2 * (rel + F)], p_n - rel - F),
          "PUNCH: lets go on the 1/16 too, back to the dry signal");

    // LOOPs and TRIPLET: what plays from the 1/16 on, repeated at exactly
    // the note length, for as long as you hold (longer than the ring).
    static const struct {
        int fx;
        double note;
    } loops[] = {{PUNCH_LOOP4, 1.0}, {PUNCH_LOOP8, 0.5}, {PUNCH_LOOP16, 0.25}, {PUNCH_LOOP32, 0.125}, {PUNCH_TRIPLET, 1.0 / 3.0}};
    for (unsigned i = 0; i < sizeof(loops) / sizeof(loops[0]); i++) {
        p_reset(SIG_NOISE);
        p_run(0.3);
        const long at = p_n;
        punch_set_live(PUNCH_BIT(loops[i].fx));
        p_run(5.0);
        punch_set_live(0);
        const long e = p_first_tick_after(at), L = lrintf((float)(beat * loops[i].note));
        bool live = p_same(&p_in[2 * e], &p_out[2 * e], L);  // the first cycle: as it plays
        bool copy = p_same(&p_in[2 * e + 2 * F], &p_out[2 * (e + L + F)], L - F);  // then again
        bool again = p_same(&p_out[2 * (e + L + F)], &p_out[2 * (e + 2 * L + F)], 2 * L);
        const long late = e + (long)(3.2 * RATE);  // after the ring has gone round once
        bool held = p_same(&p_out[2 * late], &p_out[2 * (late + L)], L);
        snprintf(what, sizeof(what), "PUNCH: %s repeats every %ld frames, exactly", punch_name(loops[i].fx), L);
        check(live && copy && again && held, what);
    }

    // SHUFFLE: the bar before, its 16ths in a new order.
    p_reset(SIG_NOISE);
    p_run(2.05);
    long at = p_n;
    punch_set_live(PUNCH_BIT(PUNCH_SHUFFLE));
    p_run(4.3);
    punch_set_live(0);
    long e = p_first_tick_after(at);
    int perm[16], used = 0, moved = 0, found = 0;
    for (int k = 0; k < 16; k++) {
        perm[k] = -1;
        for (int q = 0; q < 16; q++)
            if (p_same(&p_out[2 * (e + k * S16 + F)], &p_in[2 * (e - 16 * S16 + q * S16 + F)], S16 - 2 * F)) perm[k] = q;
        if (perm[k] >= 0) found++, used |= 1 << perm[k], moved += perm[k] != k;
    }
    printf("  shuffle:");
    for (int k = 0; k < 16; k++) printf(" %d", perm[k]);
    printf("\n");
    check(found == 16 && used == 0xFFFF && moved > 0 && perm[0] == 0,
          "PUNCH: SHUFFLE replays the last bar's 16ths, permuted");
    check(p_same(&p_out[2 * (e + F)], &p_out[2 * (e + 16 * S16 + F)], 16 * S16 - F),
          "PUNCH: ...and the new bar repeats exactly");

    // SCRATCH: back and forth over the last 16th, the same every beat.
    p_reset(SIG_SINE220);
    p_run(0.3);
    at = p_n;
    punch_set_live(PUNCH_BIT(PUNCH_SCRATCH));
    p_run(2.0);
    punch_set_live(0);
    e = p_first_tick_after(at);
    double moved_rms = 0;
    for (long i = e + F; i < e + beat; i++) moved_rms += fabs((double)p_out[2 * i] - p_in[2 * i]);
    moved_rms /= (double)(beat - F) * 32768.0;
    check(moved_rms > 0.05 && p_same(&p_out[2 * (e + beat + F)], &p_out[2 * (e + 2 * beat + F)], beat - 2 * F),
          "PUNCH: SCRATCH wobbles the record, the same every beat");

    // OCTAVE UP / DOWN: a 220 Hz tone comes out at 440 / 110.
    p_reset(SIG_SINE220);
    p_run(0.3);
    punch_set_live(PUNCH_BIT(PUNCH_OCT_UP));
    p_run(1.0);
    long w0 = p_n - (long)(0.6 * RATE);
    double up440 = p_power(&p_out[2 * w0], (long)(0.5 * RATE), 440), up220 = p_power(&p_out[2 * w0], (long)(0.5 * RATE), 220);
    punch_set_live(PUNCH_BIT(PUNCH_OCT_DOWN));
    p_run(1.0);
    w0 = p_n - (long)(0.6 * RATE);
    double dn110 = p_power(&p_out[2 * w0], (long)(0.5 * RATE), 110), dn220 = p_power(&p_out[2 * w0], (long)(0.5 * RATE), 220);
    punch_set_live(0);
    printf("  octave up: 440 Hz %.2e vs 220 Hz %.2e; down: 110 Hz %.2e vs 220 Hz %.2e\n", up440, up220, dn110, dn220);
    check(up440 > 20 * up220, "PUNCH: OCTAVE UP plays at 2x (220 Hz -> 440 Hz)");
    check(dn110 > 20 * dn220, "PUNCH: OCTAVE DOWN plays at 0.5x (220 Hz -> 110 Hz)");

    // RISER: the pitch climbs over the bar.
    p_reset(SIG_SINE220);
    p_run(0.3);
    at = p_n;
    punch_set_live(PUNCH_BIT(PUNCH_RISER));
    p_run(2.2);
    punch_set_live(0);
    e = p_first_tick_after(at);
    const int early = p_crossings(&p_out[2 * (e + F * 4)], beat / 2), lateish = p_crossings(&p_out[2 * (e + 4 * beat - beat / 2)], beat / 2);
    printf("  riser: zero crossings per half beat %d -> %d\n", early, lateish);
    check(lateish > early * 17 / 10, "PUNCH: RISER climbs towards an octave over the bar");

    // GATE: open for 3/4 of the pattern's 16ths, shut otherwise, on the grid.
    p_reset(SIG_SINE220);
    p_run(0.3);
    at = p_n;
    punch_set_live(PUNCH_BIT(PUNCH_GATE));
    p_run(2.3);
    punch_set_live(0);
    const double dry = 0.4 / sqrt(2.0);
    int gate_ok = 0, gate_n = 0;
    for (int i = p_tick_index(p_first_tick_after(at)); i >= 0 && i + 1 < p_nticks && gate_n < 16; i++, gate_n++) {
        const long t0 = p_tick_at[i], len = p_tick_at[i + 1] - t0;
        const bool open = (PUNCH_GATE_PATTERN >> p_tick_step[i]) & 1u;
        const double head = p_rms(&p_out[2 * (t0 + len / 10)], len / 2), tail = p_rms(&p_out[2 * (t0 + len * 85 / 100)], len * 15 / 100);
        gate_ok += open ? (head > 0.85 * dry && tail < 0.05 * dry) : (head < 0.05 * dry && tail < 0.05 * dry);
    }
    printf("  gate: %d of %d 16ths right\n", gate_ok, gate_n);
    check(gate_n == 16 && gate_ok == 16, "PUNCH: GATE chops to its pattern, locked to the 16ths");

    // KILL LO / KILL HI.
    double kill[2][2];
    for (int f = 0; f < 2; f++)
        for (int s = 0; s < 2; s++) {
            p_reset(s ? SIG_SINE3520 : SIG_SINE110);
            punch_set_live(PUNCH_BIT(f ? PUNCH_KILL_HIGH : PUNCH_KILL_LOW));
            p_run(0.6);
            kill[f][s] = p_rms(&p_out[2 * (p_n - RATE / 4)], RATE / 4) / p_rms(&p_in[2 * (p_n - RATE / 4)], RATE / 4);
            punch_set_live(0);
        }
    printf("  kill lo: 110 Hz x%.3f, 3520 Hz x%.3f; kill hi: 110 Hz x%.3f, 3520 Hz x%.3f\n", kill[0][0], kill[0][1], kill[1][0], kill[1][1]);
    check(kill[0][0] < 0.1 && kill[0][1] > 0.9, "PUNCH: KILL LO takes the bass out, leaves the top");
    check(kill[1][1] < 0.1 && kill[1][0] > 0.85, "PUNCH: KILL HI takes the top out, leaves the bass");

    // LO-FI: duller, wobbling, dusty.
    p_reset(SIG_SINE3520);
    punch_set_live(PUNCH_BIT(PUNCH_LOFI));
    p_run(0.8);
    const double dull = p_rms(&p_out[2 * (p_n - RATE / 2)], RATE / 2) / p_rms(&p_in[2 * (p_n - RATE / 2)], RATE / 2);
    p_sig = SIG_SILENCE;
    p_run(1.0);
    int dust = 0;
    for (long i = p_n - RATE; i < p_n; i++) dust += abs(p_out[2 * i]) > 150;
    punch_set_live(0);
    printf("  lo-fi: 3520 Hz x%.2f, %d dusty samples in a second of silence\n", dull, dust);
    check(dull < 0.8 && dust > 3, "PUNCH: LO-FI dulls the top and crackles");

    // DUB: an echo a dotted 1/8 later, bouncing left then right, ringing on
    // after you let go, then gone (and bit-exact bypass again).
    p_reset(SIG_SILENCE);
    p_run(0.2);
    punch_set_live(PUNCH_BIT(PUNCH_DUB));
    p_run(0.2);  // (it engages on the next 16th)
    {   // a click, while the send is up
        int16_t b[BLOCK * 2] = {0};
        b[0] = b[1] = 16000;
        punch_clock(false, p_step, p_playing, 0, p_bpm);
        memcpy(&p_in[2 * p_n], b, sizeof(b));
        punch_process(b, BLOCK);
        memcpy(&p_out[2 * p_n], b, sizeof(b));
        p_n += BLOCK;
        p_pos += BLOCK;
    }
    const long ck = p_n - BLOCK;
    p_run(0.05);
    punch_set_live(0);
    p_run(2.0);
    const long D = lrintf(beat * 0.75f);
    const double e1l = abs(p_out[2 * (ck + D)]) / 16000.0, e1r = abs(p_out[2 * (ck + D) + 1]) / 16000.0;
    const double e2r = p_rms(&p_out[2 * (ck + 2 * D - 20)], 60);
    const double ring = p_rms(&p_out[2 * (ck + 3 * D - 200)], 400);
    printf("  dub: first echo L %.2f R %.2f; second (right) rms %.4f; third rms %.4f\n", e1l, e1r, e2r, ring);
    check(e1l > 0.5 && e1r < 0.01 && e2r > 0.002 && ring > 0.0005,
          "PUNCH: DUB throws into a dotted-1/8 ping-pong echo that rings on");
    p_run(6.0);
    const long q = p_n;
    p_sig = SIG_NOISE;
    p_run(0.3);
    check(p_same(&p_in[2 * q], &p_out[2 * q], p_n - q), "PUNCH: ...and once it has rung out, bypass is bit-exact again");

    // Sequenced: steps 4 and 5 hold REVERSE; it starts on step 4's 1/16
    // and is gone from step 6's.
    p_reset(SIG_NOISE);
    p_pat[4] = p_pat[5] = PUNCH_BIT(PUNCH_REVERSE);
    p_run(1.2);
    long s4 = -1, s6 = -1;
    for (int i = 0; i < p_nticks; i++) {
        if (p_tick_step[i] == 4 && s4 < 0) s4 = p_tick_at[i];
        if (p_tick_step[i] == 6 && s6 < 0) s6 = p_tick_at[i];
    }
    const long d4 = p_first_diff(0);
    check(s4 > 0 && (d4 == s4 || d4 == s4 + 1) && p_same(&p_in[2 * (s6 + F)], &p_out[2 * (s6 + F)], RATE / 4),
          "PUNCH: a sequenced step engages on its 1/16 and lets go after");

    // A tap shorter than a 16th still gets its 16th.
    p_reset(SIG_NOISE);
    p_run(0.2 + 300.0 / RATE);
    at = p_n;
    punch_set_live(PUNCH_BIT(PUNCH_REVERSE));
    p_run(256.0 / RATE);
    punch_set_live(0);
    p_run(0.4);
    e = p_first_tick_after(at);
    const long dt = p_first_diff(at);
    check((dt == e || dt == e + 1) && p_first_diff(e + S16 + F) < 0, "PUNCH: a quick tap plays one 16th");

    // Stopped: no grid to wait for, it starts with you.
    p_reset(SIG_NOISE);
    p_playing = false;
    p_run(0.2);
    at = p_n;
    punch_set_live(PUNCH_BIT(PUNCH_REVERSE));
    p_run(0.3);
    punch_set_live(0);
    const long ds = p_first_diff(at);
    check(ds == at || ds == at + 1, "PUNCH: with the sequencer stopped it engages at once");

    // Several at once, in order: the repeater, then the GATE over it.
    p_reset(SIG_SINE220);
    p_run(0.3);
    at = p_n;
    punch_set_live(PUNCH_BIT(PUNCH_LOOP16) | PUNCH_BIT(PUNCH_GATE) | PUNCH_BIT(PUNCH_KILL_HIGH));
    p_run(2.3);
    punch_set_live(0);
    int shut = 0, shut_ok = 0, open_ok = 0, opens = 0;
    for (int i = p_tick_index(p_first_tick_after(at)) + 1; i >= 1 && i + 1 < p_nticks && shut + opens < 16; i++) {
        const long t0 = p_tick_at[i], len = p_tick_at[i + 1] - t0;
        const double r = p_rms(&p_out[2 * (t0 + len / 10)], len / 2);
        if ((PUNCH_GATE_PATTERN >> p_tick_step[i]) & 1u) opens++, open_ok += r > 0.3 * dry;
        else shut++, shut_ok += r < 0.05 * dry;
    }
    check(shut > 0 && shut == shut_ok && open_ok == opens, "PUNCH: LOOP + GATE + KILL HI together: the loop, gated");

    // No clicks: every effect, in and out, on a low sine where any jump
    // would stand out (the dry sine moves at most 0.004 per sample).
    for (int k = 0; k < PUNCH_COUNT; k++) {
        p_reset(SIG_SINE110);
        p_run(0.25);
        at = p_n;
        punch_set_live(PUNCH_BIT(k));
        p_run(1.15);
        punch_set_live(0);
        p_run(0.6);
        const double m = p_max_step(&p_out[2 * at], p_n - at), lim = k == PUNCH_LOFI ? 0.03 : 0.02;
        snprintf(what, sizeof(what), "PUNCH: %s in and out without a click (max step %.4f)", punch_name(k), m);
        check(m < lim, what);
    }
}

int main(void) {
    fx_init(RATE);
    fx_set_tempo(120);

    M dry = run(0.5);
    // Bypass must be bit-exact (fx off from the start).
    int16_t ref[BLOCK * 2];
    long t0 = s_t;
    test_tone(ref, BLOCK);
    s_t = t0;
    M again = run((double)BLOCK / RATE);
    check(memcmp(ref, s_out, sizeof(ref)) == 0 && again.rms > 0, "bypass passes audio through unchanged");

    fx_set(FX_SWEEP, true, 0.05f, 0.2f);
    run(0.1);
    M lp = run(0.5);
    fx_set(FX_SWEEP, true, 0.95f, 0.2f);
    run(0.1);
    M hp = run(0.5);
    fx_set(FX_SWEEP, false, 0.5f, 0.2f);
    run(0.1);
    printf("  brightness dry %.3f  low-pass %.3f  high-pass %.3f\n", dry.bright, lp.bright, hp.bright);
    check(lp.bright < dry.bright * 0.6, "SWEEP low-pass darkens the sound");
    check(hp.bright > dry.bright * 1.4, "SWEEP high-pass brightens the sound");

    fx_set(FX_CRUSH, true, 1.0f, 1.0f);
    run(0.1);
    M cr = run(0.5);
    fx_set(FX_CRUSH, false, 1.0f, 1.0f);
    run(0.1);
    printf("  distinct sample values dry %d  crushed %d\n", dry.distinct, cr.distinct);
    check(cr.distinct < 16, "CRUSH reduces the signal to a handful of levels");

    // STUTTER at 1/32 note, 120 BPM: a repeat every 0.0625 s = 2756 frames.
    fx_set(FX_STUTTER, true, 1.0f, 0.0f);
    run(0.2);
    run(0.5);
    const int L = (int)(RATE * 60.0 / 120 * 0.125);
    double diff = 0;
    for (int i = 4000; i < 8000; i++) diff += abs(s_out[2 * i] - s_out[2 * (i + L)]);
    diff /= 4000 * 32768.0;
    fx_set(FX_STUTTER, false, 1.0f, 0.0f);
    run(0.2);
    printf("  stutter: mean |x[n] - x[n+%d]| = %.5f\n", L, diff);
    check(diff < 0.002, "STUTTER repeats exactly at the tempo-synced length");

    fx_set(FX_TAPE, true, 0.0f, 0.0f);  // fastest stop: 0.15 s
    run(0.4);
    M stopped = run(0.3);
    FxState st = fx_state();
    check(stopped.rms < 0.01 && st.tape_speed < 0.01, "TAPE grinds to a halt");
    fx_set(FX_TAPE, false, 0.0f, 0.0f);
    run(0.5);
    M back = run(0.5);
    printf("  tape: rms stopped %.4f  recovered %.4f (dry %.4f)\n", stopped.rms, back.rms, dry.rms);
    check(fabs(back.rms - dry.rms) < dry.rms * 0.1 && fx_state().tape_speed >= 1.0f, "TAPE spins back up to live");

    punch_checks();

    {   // FX automation (audio thread): a step turns its effect on, a step
        // without one lets it go, and an effect your stylus holds is left alone.
        fx_hand(0);
        fx_auto_step(FX_SWEEP + 1, 0.2f, 0.7f, true);
        FxState a = fx_state();
        fx_auto_step(0, 0, 0, true);
        FxState b = fx_state();
        check(a.on[FX_SWEEP] && fabsf(a.x[FX_SWEEP] - 0.2f) < 1e-6f && !b.on[FX_SWEEP],
              "FX automation: a step turns its effect on, an empty step lets it go");
        fx_set(FX_CRUSH, true, 0.5f, 0.5f);  // you're holding CRUSH...
        fx_hand(1u << FX_CRUSH);
        fx_auto_step(FX_CRUSH + 1, 0.9f, 0.9f, true);  // ...a recorded CRUSH move doesn't take it over
        FxState c = fx_state();
        fx_auto_step(0, 0, 0, false);                  // ...nor does stopping let it go
        FxState d = fx_state();
        check(c.on[FX_CRUSH] && fabsf(c.x[FX_CRUSH] - 0.5f) < 1e-6f && d.on[FX_CRUSH],
              "FX automation: your stylus always wins");
        fx_hand(0);
        fx_set(FX_CRUSH, false, 0.5f, 0.5f);
    }
    printf(fails ? "\n%d FX problem(s)\n" : "\nall FX OK\n", fails);
    return fails ? 1 : 0;
}
