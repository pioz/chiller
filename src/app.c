#include "app.h"
#include "synth.h"

#include <math.h>
#include <stdio.h>

/* ------------------------------------------------------------------ scenes */

typedef struct {
    float hz;
    const char *name;
} Band;

static const Band BANDS[] = {
    {2.5f, "delta"}, /* deep sleep */
    {6.0f, "theta"}, /* meditation, drowsiness */
    {10.0f, "alpha"}, /* relaxed wakefulness */
};
#define N_BANDS 3

typedef struct {
    const char *name;
    float level[L_COUNT]; /* pad, binaural, noise, ocean, rain, chimes */
    int band;
    int noise;
    float brightness;
} Scene;

static const Scene SCENES[APP_SCENES] = {
    {"Deep sleep", {0.70f, 0.50f, 0.60f, 0.60f, 0.00f, 0.00f}, 0, NOISE_BROWN, 0.12f},
    {"Meditation", {0.80f, 0.50f, 0.20f, 0.30f, 0.00f, 0.60f}, 1, NOISE_PINK, 0.35f},
    {"Relax", {0.60f, 0.40f, 0.35f, 0.00f, 0.50f, 0.30f}, 2, NOISE_PINK, 0.50f},
    {"Ocean", {0.35f, 0.30f, 0.15f, 1.00f, 0.00f, 0.25f}, 2, NOISE_BROWN, 0.30f},
    {"Night rain", {0.50f, 0.30f, 0.25f, 0.00f, 1.00f, 0.00f}, 1, NOISE_PINK, 0.25f},
};

static const struct {
    char key;
    const char *name;
} LAYERS[L_COUNT] = {
    {'p', "harmonic pad"}, {'b', "binaural"}, {'n', "noise"},
    {'o', "ocean"},        {'r', "rain"},     {'c', "chimes"},
};

static const char *NOISE_NAMES[NOISE_COLORS] = {"pink", "brown", "white"};

/* ------------------------------------------------------------------ state */

static SynthParams params;
static Variation variation;
static float remembered[L_COUNT];
static int band;
static int scene;
static int muted;
static uint32_t rng;
static SynthStatus status;

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

static void push(void)
{
    SynthParams p = params;
    if (muted) p.master = 0.0f;
    synth_set_params(&p);
}

static void toggle_layer(int l)
{
    if (params.level[l] > 0.01f) {
        remembered[l] = params.level[l];
        params.level[l] = 0.0f;
    } else {
        params.level[l] = remembered[l];
    }
}

/* off -> delta -> theta -> alpha -> off */
static void cycle_binaural(void)
{
    if (params.level[L_BINAURAL] <= 0.01f) {
        band = 0;
        params.level[L_BINAURAL] = remembered[L_BINAURAL];
    } else if (band < N_BANDS - 1) {
        band++;
    } else {
        toggle_layer(L_BINAURAL);
    }
    params.beat_hz = BANDS[band].hz;
}

/* off -> pink -> brown -> white -> off */
static void cycle_noise(void)
{
    if (params.level[L_NOISE] <= 0.01f) {
        params.noise_color = NOISE_PINK;
        params.level[L_NOISE] = remembered[L_NOISE];
    } else if (params.noise_color != NOISE_WHITE) {
        params.noise_color++;
    } else {
        toggle_layer(L_NOISE);
    }
}

/* ------------------------------------------------------------------ actions */

void app_init(uint32_t seed, int first_scene)
{
    rng = seed ? seed : 1;
    synth_init(rng);
    params.master = 0.7f;
    app_new_variation();
    app_apply_scene(first_scene);
}

void app_apply_scene(int i)
{
    if (i < 0 || i >= APP_SCENES) return;
    const Scene *s = &SCENES[i];
    scene = i;
    for (int l = 0; l < L_COUNT; l++) {
        params.level[l] = s->level[l];
        remembered[l] = s->level[l] > 0.01f ? s->level[l] : 0.5f;
    }
    band = s->band;
    params.beat_hz = BANDS[band].hz;
    params.noise_color = s->noise;
    params.brightness = s->brightness;
    push();
}

void app_press(int layer)
{
    if (layer < 0 || layer >= L_COUNT) return;
    if (layer == L_BINAURAL) cycle_binaural();
    else if (layer == L_NOISE) cycle_noise();
    else toggle_layer(layer);
    push();
}

void app_new_variation(void)
{
    variation = synth_random_variation(&rng);
    synth_set_variation(&variation);
}

void app_change_volume(float delta)
{
    params.master = clampf(params.master + delta, 0.0f, 1.0f);
    push();
}

void app_change_brightness(float delta)
{
    params.brightness = clampf(params.brightness + delta, 0.0f, 1.0f);
    push();
}

void app_set_muted(int m)
{
    muted = m;
    push();
}

/* ------------------------------------------------------------------ state */

int app_scene(void) { return scene; }

const char *app_scene_name(int i) { return i >= 0 && i < APP_SCENES ? SCENES[i].name : ""; }

char app_layer_key(int l) { return l >= 0 && l < L_COUNT ? LAYERS[l].key : '\0'; }

const char *app_layer_name(int l) { return l >= 0 && l < L_COUNT ? LAYERS[l].name : ""; }

int app_layer_on(int l) { return l >= 0 && l < L_COUNT && params.level[l] > 0.01f; }

const char *app_layer_detail(int l)
{
    static char buf[32];
    if (!app_layer_on(l)) return "";
    if (l == L_BINAURAL) {
        snprintf(buf, sizeof buf, "%s %.1f Hz", BANDS[band].name, BANDS[band].hz);
        return buf;
    }
    if (l == L_NOISE) return NOISE_NAMES[params.noise_color];
    return "";
}

float app_volume(void) { return params.master; }

float app_brightness(void) { return params.brightness; }

const char *app_key_name(void)
{
    static char buf[32];
    synth_variation_name(&variation, buf, sizeof buf);
    return buf;
}

void app_poll(void) { synth_get_status(&status); }

float app_level(int l) { return l >= 0 && l < L_COUNT ? status.level[l] : 0.0f; }

const char *app_chord(void) { return status.chord; }

double app_breath_phase(void)
{
    double cycle = synth_breath_seconds();
    return fmod(status.seconds, cycle) / cycle;
}
