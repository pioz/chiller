#ifndef APP_H
#define APP_H

#include <stdint.h>

/* App logic shared by every front end (terminal, iOS): scenes, layers and their cycles.
 * Every setter pushes the new parameters to the synth. Call from one thread only. */

#define APP_SCENES 5

void app_init(uint32_t seed, int scene);

/* actions */
void app_apply_scene(int scene);
void app_press(int layer); /* toggle, or cycle for binaural (bands) and noise (colors) */
void app_new_variation(void);
void app_change_volume(float delta);
void app_change_brightness(float delta);
void app_set_muted(int muted); /* fades the output without touching the volume setting */

/* state */
int app_scene(void);
const char *app_scene_name(int scene);
char app_layer_key(int layer);
const char *app_layer_name(int layer);
int app_layer_on(int layer);
const char *app_layer_detail(int layer); /* "theta 6.0 Hz", "pink" or "" */
float app_volume(void);
float app_brightness(void);
const char *app_key_name(void); /* e.g. "E minor" */

/* live status from the audio thread, refreshed by app_poll() */
void app_poll(void);
float app_level(int layer); /* actual, smoothed level */
const char *app_chord(void);
double app_breath_phase(void); /* 0..1: first half inhale, second half exhale */

#endif
