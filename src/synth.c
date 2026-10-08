#include "synth.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define TAU 6.28318530717958647692f
#define SR ((float)SAMPLE_RATE)
#define N_MODES 5
#define N_PROGS 4
#define MAX_PAD 16
#define MAX_BELLS 10
#define MAX_DROPS 32
#define N_COMBS 8
#define N_APS 4
#define WHITE_GAIN 0.3f /* white is harsher: a bit quieter than pink and brown */
#define BREATH_SECONDS 10.0 /* 6 cycles per minute: HRV resonance frequency */

/* ------------------------------------------------------------------ tables */

static const int MODES[N_MODES][7] = {
    {0, 2, 4, 5, 7, 9, 11}, /* ionian */
    {0, 2, 4, 6, 7, 9, 11}, /* lydian */
    {0, 2, 3, 5, 7, 9, 10}, /* dorian */
    {0, 2, 3, 5, 7, 8, 10}, /* aeolian */
    {0, 2, 4, 5, 7, 9, 10}, /* mixolydian */
};
static const char *MODE_NAMES[N_MODES] = {"major", "lydian", "dorian", "minor", "mixolydian"};

/* Scale degrees (0 = tonic). Each mode avoids its diminished degree. */
static const int PROGS[N_MODES][N_PROGS][4] = {
    {{0, 5, 3, 4}, {0, 3, 5, 3}, {0, 4, 5, 3}, {3, 0, 5, 4}},
    {{0, 1, 0, 1}, {0, 4, 1, 5}, {0, 1, 5, 4}, {0, 2, 1, 0}},
    {{0, 3, 0, 3}, {0, 6, 3, 0}, {0, 2, 3, 6}, {0, 3, 6, 0}},
    {{0, 5, 2, 6}, {0, 3, 5, 6}, {0, 5, 3, 4}, {0, 6, 5, 6}},
    {{0, 6, 3, 0}, {0, 3, 6, 3}, {0, 4, 3, 0}, {0, 6, 4, 3}},
};

static const char *NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/* Freeverb lengths at 44.1 kHz */
static const int COMB_LEN[N_COMBS] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const int AP_LEN[N_APS] = {556, 441, 341, 225};
#define STEREO_SPREAD 23

/* ------------------------------------------------------------------ types */

typedef struct {
    float freq, amp, target, gl, gr;
    float ph[3];
    float lfo, lfo_rate;
    int on;
} PadVoice;

typedef struct {
    float pc, pm, fc, fm, amp, decay, index, idecay, gl, gr;
    int age, on;
} Bell;

typedef struct {
    float ph, freq, amp, decay, gl, gr;
    int on;
} Drop;

typedef struct {
    float buf[1700];
    int len, idx;
    float store;
} Comb;

typedef struct {
    float buf[600];
    int len, idx;
} Allpass;

/* ------------------------------------------------------------------ state */

/* Shared with the control thread, protected by g_lock */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static SynthParams g_params;
static Variation g_var;
static int g_var_pending;
static SynthStatus g_status;

/* Private to the audio thread */
static struct {
    uint32_t rng;
    SynthParams p;
    Variation var;
    uint64_t n;

    float lvl[L_COUNT], master, bright, beat, carrier, startup;
    float color[NOISE_COLORS]; /* smoothed weights of the noise colors */

    PadVoice pad[MAX_PAD];
    int step;
    uint64_t chord_start;
    float lp[2][2];
    char chord[16];

    float bph[2];

    float pink[2][7], brown[2];

    float ocean[2][2], wave_amp, wave_amp_s;
    uint64_t wave_cycle;

    float rain_lo[2], rain_hi[2];
    Drop drops[MAX_DROPS];

    Bell bells[MAX_BELLS];

    Comb comb[2][N_COMBS];
    Allpass ap[2][N_APS];
} S;

/* ------------------------------------------------------------------ utilities */

static inline uint32_t xs32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

static inline float frand(uint32_t *s) { return (float)(xs32(s) >> 8) * (1.0f / 16777216.0f); }
static inline float nrand(uint32_t *s) { return frand(s) * 2.0f - 1.0f; }
static inline float mtof(int m) { return 440.0f * powf(2.0f, (float)(m - 69) / 12.0f); }
static inline float smooth(float cur, float target, float k) { return cur + (target - cur) * k; }

static inline void pan_gains(float pan, float *gl, float *gr)
{
    float a = (pan + 1.0f) * (TAU / 8.0f);
    *gl = cosf(a);
    *gr = sinf(a);
}

static int scale_note(int deg)
{
    return MODES[S.var.mode][deg % 7] + 12 * (deg / 7);
}

/* ------------------------------------------------------------------ pad */

static void spawn_pad(int midi, float vol, float pan)
{
    PadVoice *v = NULL;
    for (int i = 0; i < MAX_PAD && !v; i++)
        if (!S.pad[i].on) v = &S.pad[i];
    if (!v) { /* steal the quietest voice among those releasing */
        float best = 1e9f;
        for (int i = 0; i < MAX_PAD; i++)
            if (S.pad[i].target == 0.0f && S.pad[i].amp < best) best = S.pad[i].amp, v = &S.pad[i];
        if (!v) return;
    }
    v->freq = mtof(midi);
    v->amp = 0.0f;
    v->target = vol;
    for (int k = 0; k < 3; k++) v->ph[k] = frand(&S.rng);
    v->lfo = frand(&S.rng);
    v->lfo_rate = 0.04f + 0.08f * frand(&S.rng);
    pan_gains(pan, &v->gl, &v->gr);
    v->on = 1;
}

static void start_chord(void)
{
    for (int i = 0; i < MAX_PAD; i++) S.pad[i].target = 0.0f;

    int root = S.var.root;
    int d = PROGS[S.var.mode][S.var.prog][S.step];

    int bass = root - 12 + scale_note(d);
    if (bass > root - 6) bass -= 12;
    spawn_pad(bass, 1.0f, 0.0f);

    static const int UPPER[4] = {2, 4, 6, 8}; /* third, fifth, seventh, ninth */
    for (int j = 0; j < 4; j++) {
        int n = root + scale_note(d + UPPER[j]);
        while (n > root + 19) n -= 12;
        spawn_pad(n, 0.55f, (j % 2 ? 0.5f : -0.5f) * (0.4f + 0.6f * frand(&S.rng)));
    }

    int base = scale_note(d);
    int third = scale_note(d + 2) - base, seventh = scale_note(d + 6) - base;
    const char *q = third == 4 ? (seventh == 11 ? "maj7" : "7") : "m7";
    snprintf(S.chord, sizeof S.chord, "%s%s", NOTE_NAMES[(root + base) % 12], q);
    S.chord_start = S.n;
}

/* ------------------------------------------------------------------ chimes */

static void spawn_bell(void)
{
    Bell *b = NULL;
    for (int i = 0; i < MAX_BELLS && !b; i++)
        if (!S.bells[i].on) b = &S.bells[i];
    if (!b) return;
    static const int PENTA[5] = {0, 1, 2, 4, 5};
    int deg = PENTA[xs32(&S.rng) % 5] + 7 * (int)(xs32(&S.rng) % 2);
    b->fc = mtof(S.var.root + 24 + scale_note(deg));
    b->fm = b->fc * 3.007f;
    b->pc = b->pm = 0.0f;
    b->amp = 0.35f + 0.5f * frand(&S.rng);
    b->decay = expf(-1.0f / ((3.0f + 4.0f * frand(&S.rng)) * SR));
    b->index = 1.3f;
    b->idecay = expf(-1.0f / (0.7f * SR));
    pan_gains(nrand(&S.rng) * 0.8f, &b->gl, &b->gr);
    b->age = 0;
    b->on = 1;
}

/* ------------------------------------------------------------------ rain */

static void spawn_drop(void)
{
    Drop *d = NULL;
    for (int i = 0; i < MAX_DROPS && !d; i++)
        if (!S.drops[i].on) d = &S.drops[i];
    if (!d) return;
    float r = frand(&S.rng);
    d->ph = 0.0f;
    d->freq = 1200.0f + 3300.0f * frand(&S.rng);
    d->amp = 0.05f + 0.45f * r * r;
    d->decay = expf(-1.0f / ((0.004f + 0.012f * frand(&S.rng)) * SR));
    pan_gains(nrand(&S.rng) * 0.9f, &d->gl, &d->gr);
    d->on = 1;
}

/* ------------------------------------------------------------------ reverb */

static void reverb_init(void)
{
    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < N_COMBS; i++) S.comb[c][i].len = COMB_LEN[i] + c * STEREO_SPREAD;
        for (int i = 0; i < N_APS; i++) S.ap[c][i].len = AP_LEN[i] + c * STEREO_SPREAD;
    }
}

static inline float comb_proc(Comb *c, float in)
{
    const float feedback = 0.9f, damp = 0.35f;
    float out = c->buf[c->idx];
    c->store = out * (1.0f - damp) + c->store * damp;
    c->buf[c->idx] = in + c->store * feedback;
    if (++c->idx >= c->len) c->idx = 0;
    return out;
}

static inline float ap_proc(Allpass *a, float in)
{
    float b = a->buf[a->idx];
    a->buf[a->idx] = in + b * 0.5f;
    if (++a->idx >= a->len) a->idx = 0;
    return b - in;
}

static inline float reverb(int c, float in)
{
    float out = 0.0f;
    for (int i = 0; i < N_COMBS; i++) out += comb_proc(&S.comb[c][i], in);
    for (int i = 0; i < N_APS; i++) out = ap_proc(&S.ap[c][i], out);
    return out;
}

/* ------------------------------------------------------------------ API */

void synth_init(uint32_t seed)
{
    memset(&S, 0, sizeof S);
    S.rng = seed ? seed : 0x9e3779b9u;
    reverb_init();
    S.var = synth_random_variation(&S.rng);
    S.carrier = S.var.carrier_hz;
    S.beat = 6.0f;
    S.bright = 0.3f;
    S.wave_amp = S.wave_amp_s = 0.8f;
    S.color[NOISE_PINK] = 1.0f;
    start_chord();
}

void synth_set_params(const SynthParams *p)
{
    pthread_mutex_lock(&g_lock);
    g_params = *p;
    pthread_mutex_unlock(&g_lock);
}

void synth_set_variation(const Variation *v)
{
    pthread_mutex_lock(&g_lock);
    g_var = *v;
    g_var_pending = 1;
    pthread_mutex_unlock(&g_lock);
}

void synth_get_status(SynthStatus *s)
{
    pthread_mutex_lock(&g_lock);
    *s = g_status;
    pthread_mutex_unlock(&g_lock);
}

Variation synth_random_variation(uint32_t *rng)
{
    Variation v;
    v.root = 48 + (int)(xs32(rng) % 8); /* C3..G3 */
    v.mode = (int)(xs32(rng) % N_MODES);
    v.prog = (int)(xs32(rng) % N_PROGS);
    v.chord_seconds = 10.0f + 6.0f * frand(rng);
    v.carrier_hz = 140.0f + 80.0f * frand(rng);
    return v;
}

void synth_variation_name(const Variation *v, char *buf, size_t n)
{
    snprintf(buf, n, "%s %s", NOTE_NAMES[v->root % 12], MODE_NAMES[v->mode]);
}

double synth_breath_seconds(void) { return BREATH_SECONDS; }

void synth_render(float *out, int frames)
{
    /* Sync without ever blocking the audio thread */
    if (pthread_mutex_trylock(&g_lock) == 0) {
        S.p = g_params;
        if (g_var_pending) {
            S.var = g_var;
            g_var_pending = 0;
            S.step = 0;
            start_chord();
        }
        memcpy(g_status.level, S.lvl, sizeof S.lvl);
        g_status.master = S.master;
        g_status.seconds = (double)S.n / SR;
        memcpy(g_status.chord, S.chord, sizeof S.chord);
        pthread_mutex_unlock(&g_lock);
    }

    const float k_lvl = 1.0f / (2.5f * SR);
    const float k_master = 1.0f / (0.4f * SR);
    const float k_bright = 1.0f / (1.5f * SR);
    const float k_beat = 1.0f / (4.0f * SR);
    const float k_pad = 1.0f / (3.0f * SR);

    /* Pad filter: updated per block, breathing with a very slow LFO */
    double t0 = (double)S.n / SR;
    float filt_lfo = 0.85f + 0.15f * sinf(TAU * (float)fmod(t0 * 0.03, 1.0));
    float cutoff = (250.0f + 3500.0f * S.bright * S.bright) * filt_lfo;
    float a_pad = 1.0f - expf(-TAU * cutoff / SR);
    const float a_rain_lo = 1.0f - expf(-TAU * 500.0f / SR);
    const float a_rain_hi = 1.0f - expf(-TAU * 4500.0f / SR);
    float gust = 0.75f + 0.25f * sinf(TAU * (float)fmod(t0 * 0.031, 1.0));

    for (int f = 0; f < frames; f++) {
        for (int i = 0; i < L_COUNT; i++) S.lvl[i] = smooth(S.lvl[i], S.p.level[i], k_lvl);
        S.master = smooth(S.master, S.p.master, k_master);
        S.bright = smooth(S.bright, S.p.brightness, k_bright);
        S.beat = smooth(S.beat, S.p.beat_hz, k_beat);
        S.carrier = smooth(S.carrier, S.var.carrier_hz, k_beat);
        for (int i = 0; i < NOISE_COLORS; i++)
            S.color[i] = smooth(S.color[i], S.p.noise_color == i ? 1.0f : 0.0f, k_lvl);
        if (S.startup < 1.0f) S.startup += 1.0f / (6.0f * SR);

        float L = 0.0f, R = 0.0f, send = 0.0f;

        /* --- harmonic pad: slow chords with slightly detuned voices --- */
        if (S.n - S.chord_start >= (uint64_t)(S.var.chord_seconds * SR)) {
            S.step = (S.step + 1) % 4;
            start_chord();
        }
        float pl = 0.0f, pr = 0.0f;
        for (int i = 0; i < MAX_PAD; i++) {
            PadVoice *v = &S.pad[i];
            if (!v->on) continue;
            v->amp = smooth(v->amp, v->target, k_pad);
            if (v->target == 0.0f && v->amp < 1e-4f) { v->on = 0; continue; }
            static const float DETUNE[3] = {1.0f, 1.0035f, 0.9965f};
            float s = 0.0f;
            for (int k = 0; k < 3; k++) {
                v->ph[k] += v->freq * DETUNE[k] / SR;
                if (v->ph[k] >= 1.0f) v->ph[k] -= 1.0f;
                s += sinf(TAU * v->ph[k]) + 0.2f * sinf(2.0f * TAU * v->ph[k]);
            }
            v->lfo += v->lfo_rate / SR;
            if (v->lfo >= 1.0f) v->lfo -= 1.0f;
            s *= v->amp * (0.85f + 0.15f * sinf(TAU * v->lfo));
            pl += s * v->gl;
            pr += s * v->gr;
        }
        S.lp[0][0] += (pl - S.lp[0][0]) * a_pad;
        S.lp[0][1] += (S.lp[0][0] - S.lp[0][1]) * a_pad;
        S.lp[1][0] += (pr - S.lp[1][0]) * a_pad;
        S.lp[1][1] += (S.lp[1][0] - S.lp[1][1]) * a_pad;
        float g_pad = S.lvl[L_PAD] * 0.11f;
        L += S.lp[0][1] * g_pad;
        R += S.lp[1][1] * g_pad;
        send += (S.lp[0][1] + S.lp[1][1]) * g_pad * 0.35f;

        /* --- binaural beats: two pure sine waves, one per ear --- */
        if (S.lvl[L_BINAURAL] > 1e-4f) {
            S.bph[0] += (S.carrier - S.beat * 0.5f) / SR;
            S.bph[1] += (S.carrier + S.beat * 0.5f) / SR;
            for (int c = 0; c < 2; c++)
                if (S.bph[c] >= 1.0f) S.bph[c] -= 1.0f;
            float g = S.lvl[L_BINAURAL] * 0.06f;
            L += sinf(TAU * S.bph[0]) * g;
            R += sinf(TAU * S.bph[1]) * g;
        }

        /* --- pink / brown / white noise, decorrelated between channels --- */
        for (int c = 0; c < 2; c++) {
            float w = nrand(&S.rng), *b = S.pink[c];
            b[0] = 0.99886f * b[0] + w * 0.0555179f;
            b[1] = 0.99332f * b[1] + w * 0.0750759f;
            b[2] = 0.96900f * b[2] + w * 0.1538520f;
            b[3] = 0.86650f * b[3] + w * 0.3104856f;
            b[4] = 0.55000f * b[4] + w * 0.5329522f;
            b[5] = -0.7616f * b[5] - w * 0.0168980f;
            float pink = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362f) * 0.11f;
            b[6] = w * 0.115926f;
            S.brown[c] = (S.brown[c] + 0.02f * w) / 1.02f;
            float brown = S.brown[c] * 3.5f;
            float white = w * WHITE_GAIN;
            float x = (pink * S.color[NOISE_PINK] + brown * S.color[NOISE_BROWN] + white * S.color[NOISE_WHITE]) *
                      S.lvl[L_NOISE] * 0.22f;
            if (c == 0) L += x; else R += x;
        }

        /* --- ocean: a wave every 10 s, also driving the breathing guide --- */
        double t = (double)S.n / SR;
        uint64_t cycle = (uint64_t)(t / BREATH_SECONDS);
        if (cycle != S.wave_cycle) {
            S.wave_cycle = cycle;
            S.wave_amp = 0.6f + 0.4f * frand(&S.rng);
        }
        S.wave_amp_s = smooth(S.wave_amp_s, S.wave_amp, 1.0f / (2.0f * SR));
        if (S.lvl[L_OCEAN] > 1e-4f) {
            for (int c = 0; c < 2; c++) {
                float ph = (float)fmod(t / BREATH_SECONDS + c * 0.035, 1.0);
                float env = 0.5f - 0.5f * cosf(TAU * ph);
                env *= sqrtf(env);
                float a = 1.0f - expf(-TAU * (120.0f + 1600.0f * env) / SR);
                float *o = S.ocean[c];
                o[0] += (nrand(&S.rng) - o[0]) * a;
                o[1] += (o[0] - o[1]) * a;
                float x = o[1] * (0.12f + 0.88f * env) * S.wave_amp_s * S.lvl[L_OCEAN] * 1.1f;
                if (c == 0) L += x; else R += x;
            }
        }

        /* --- rain: filtered hiss + "chirping" drops --- */
        if (S.lvl[L_RAIN] > 1e-4f) {
            if (frand(&S.rng) < 35.0f / SR) spawn_drop();
            for (int c = 0; c < 2; c++) {
                float w = nrand(&S.rng);
                S.rain_lo[c] += (w - S.rain_lo[c]) * a_rain_lo;
                S.rain_hi[c] += ((w - S.rain_lo[c]) - S.rain_hi[c]) * a_rain_hi;
                float x = S.rain_hi[c] * gust * S.lvl[L_RAIN] * 0.2f;
                if (c == 0) L += x; else R += x;
            }
        }
        for (int i = 0; i < MAX_DROPS; i++) {
            Drop *d = &S.drops[i];
            if (!d->on) continue;
            float x = sinf(TAU * d->ph) * d->amp * S.lvl[L_RAIN] * 0.14f;
            d->ph += d->freq / SR;
            if (d->ph >= 1.0f) d->ph -= 1.0f;
            d->freq *= 1.00015f;
            d->amp *= d->decay;
            if (d->amp < 1e-4f) d->on = 0;
            L += x * d->gl;
            R += x * d->gr;
            send += x * 0.5f;
        }

        /* --- sparse FM chimes on the pentatonic scale of the key --- */
        if (S.lvl[L_CHIMES] > 0.01f && frand(&S.rng) < 1.0f / (3.5f * SR)) spawn_bell();
        for (int i = 0; i < MAX_BELLS; i++) {
            Bell *b = &S.bells[i];
            if (!b->on) continue;
            float att = b->age < 400 ? (float)b->age / 400.0f : 1.0f;
            float x = sinf(TAU * b->pc + b->index * sinf(TAU * b->pm)) * b->amp * att;
            x *= S.lvl[L_CHIMES] * 0.13f;
            b->pc += b->fc / SR;
            if (b->pc >= 1.0f) b->pc -= 1.0f;
            b->pm += b->fm / SR;
            if (b->pm >= 1.0f) b->pm -= 1.0f;
            b->amp *= b->decay;
            b->index *= b->idecay;
            b->age++;
            if (b->amp < 1e-4f) b->on = 0;
            L += x * b->gl;
            R += x * b->gr;
            send += x * 1.2f;
        }

        /* --- reverb, master, soft limiter --- */
        float in = send * 0.015f;
        L += reverb(0, in) * 0.9f;
        R += reverb(1, in) * 0.9f;

        float g = S.master * S.startup * 1.5f;
        out[2 * f] = tanhf(L * g);
        out[2 * f + 1] = tanhf(R * g);
        S.n++;
    }
}
