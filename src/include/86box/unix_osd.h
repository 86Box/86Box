#ifndef _UNIX_OSD_H
#define _UNIX_OSD_H

#include <SDL.h>

// state management
extern void osd_init();
extern void osd_deinit();
extern int osd_open(SDL_Event event);
extern int osd_close(SDL_Event event);

// keyboard event handler
extern int osd_handle(SDL_Event event);

// draw the osd interface, if it's open
extern void osd_present();

// future ui
extern void osd_ui_sb_update_icon_state(int tag, int state);
extern void osd_ui_sb_update_icon(int tag, int active);
extern void osd_ui_sb_update_icon_write(int tag, int active);
extern void osd_ui_sb_update_icon_wp(int tag, int state);

#endif /*_UNIX_OSD_H*/

