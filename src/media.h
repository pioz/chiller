#ifndef MEDIA_H
#define MEDIA_H

/* System media controls: the play/pause and next keys, headphone buttons and the Now Playing
 * widget. Commands are delivered on the main thread, from inside media_poll(). */

typedef enum { MEDIA_PLAY, MEDIA_PAUSE, MEDIA_TOGGLE, MEDIA_NEXT } MediaCommand;

void media_start(void (*on_command)(MediaCommand));
void media_poll(void);
void media_update(int playing, const char *title, const char *album); /* cheap when unchanged */
void media_stop(void);

#endif
