#ifndef _UNIX_OSD_H
#define _UNIX_OSD_H

#include <SDL.h>

extern void osd_init();
extern void osd_deinit();

extern int osd_open(SDL_Event event);
extern int osd_close(SDL_Event event);
extern int osd_handle(SDL_Event event);

extern void osd_ui_sb_update_icon_state(int tag, int state);
extern void osd_ui_sb_update_icon(int tag, int active);
extern void osd_ui_sb_update_icon_write(int tag, int active);

#endif /*_UNIX_OSD_H*/

