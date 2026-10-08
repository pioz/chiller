#ifndef AUDIO_H
#define AUDIO_H

/* Opens the system audio output; the callback calls synth_render() */
int audio_start(void);
void audio_stop(void);

#endif
