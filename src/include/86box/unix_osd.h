#ifndef _UNIX_OSD_H
#define _UNIX_OSD_H

#include <SDL.h>

extern int osd_open(SDL_Event event);
extern int osd_close(SDL_Event event);
extern int osd_handle(SDL_Event event);

#endif /*_UNIX_OSD_H*/

