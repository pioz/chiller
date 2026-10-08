#ifndef SYNTH_H
#define SYNTH_H

#include <stddef.h>
#include <stdint.h>

#define SAMPLE_RATE 44100

/* Sound layers mixed by the synthesizer */
enum { L_PAD, L_BINAURAL, L_NOISE, L_OCEAN, L_RAIN, L_CHIMES, L_COUNT };
enum { NOISE_PINK, NOISE_BROWN, NOISE_WHITE, NOISE_COLORS };

/* "Target" parameters: the synth always reaches them through slow transitions */
typedef struct {
    float level[L_COUNT]; /* 0..1 */
    float master;         /* 0..1 */
    float brightness;     /* 0..1, opening of the pad filter */
    float beat_hz;        /* binaural beat frequency */
    int noise_color;
} SynthParams;

/* Harmonic variation: key, mode, progression, tempo */
typedef struct {
    int root; /* MIDI note of the tonic (octave 3) */
    int mode;
    int prog;
    float chord_seconds;
    float carrier_hz; /* binaural beat carrier */
} Variation;

typedef struct {
    float level[L_COUNT]; /* current (smoothed) levels */
    float master;
    double seconds; /* audio clock */
    char chord[16];
} SynthStatus;

void synth_init(uint32_t seed);
void synth_set_params(const SynthParams *p);
void synth_set_variation(const Variation *v);
void synth_get_status(SynthStatus *s);
void synth_render(float *out, int frames); /* stereo interleaved */

Variation synth_random_variation(uint32_t *rng);
void synth_variation_name(const Variation *v, char *buf, size_t n);
double synth_breath_seconds(void); /* period of waves and breathing */

#endif
