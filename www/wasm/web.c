/* Entry points for the browser, on top of src/app.h. */
#include "app.h"
#include "synth.h"

#define MAX_FRAMES 4096

static float buffer[MAX_FRAMES * 2];

/* Interleaved stereo output of the last web_render() call. */
float *web_buffer(void) { return buffer; }

int web_render(int frames)
{
    if (frames > MAX_FRAMES) frames = MAX_FRAMES;
    synth_render(buffer, frames);
    return frames;
}

int web_sample_rate(void) { return SAMPLE_RATE; }
