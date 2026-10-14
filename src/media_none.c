/* No system media controls on this platform (Linux would need MPRIS over D-Bus). */
#include "media.h"

void media_start(void (*on_command)(MediaCommand)) { (void)on_command; }
void media_poll(void) {}
void media_update(int playing, const char *title, const char *album)
{
    (void)playing, (void)title, (void)album;
}
void media_stop(void) {}
