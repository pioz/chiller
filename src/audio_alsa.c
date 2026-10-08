#include "audio.h"
#include "synth.h"

#include <alsa/asoundlib.h>
#include <pthread.h>

#define PERIOD_FRAMES 1024
#define LATENCY_US 100000

static snd_pcm_t *pcm;
static pthread_t thread;
static volatile int running;

/* ALSA has no pull callback: a thread renders and writes blocking periods */
static void *playback(void *arg)
{
    (void)arg;
    float buf[PERIOD_FRAMES * 2];
    while (running) {
        synth_render(buf, PERIOD_FRAMES);
        const float *p = buf;
        snd_pcm_uframes_t left = PERIOD_FRAMES;
        while (left > 0 && running) {
            snd_pcm_sframes_t n = snd_pcm_writei(pcm, p, left);
            if (n < 0) { /* underrun or suspend: recover and retry */
                if (snd_pcm_recover(pcm, (int)n, 1) < 0) {
                    running = 0;
                    break;
                }
                continue;
            }
            p += n * 2;
            left -= (snd_pcm_uframes_t)n;
        }
    }
    return NULL;
}

int audio_start(void)
{
    if (snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) return -1;
    /* soft_resample=1: ALSA's plug layer converts rate and format if the device needs it */
    if (snd_pcm_set_params(pcm, SND_PCM_FORMAT_FLOAT, SND_PCM_ACCESS_RW_INTERLEAVED, 2, SAMPLE_RATE, 1,
                           LATENCY_US) < 0)
        goto fail;
    running = 1;
    if (pthread_create(&thread, NULL, playback, NULL) != 0) goto fail;
    return 0;

fail:
    running = 0;
    snd_pcm_close(pcm);
    pcm = NULL;
    return -1;
}

void audio_stop(void)
{
    if (!pcm) return;
    running = 0;
    pthread_join(thread, NULL);
    snd_pcm_drop(pcm);
    snd_pcm_close(pcm);
    pcm = NULL;
}
