#ifndef _UNIX_OSD_H
#define _UNIX_OSD_H

#ifdef __cplusplus
extern "C" {
#endif

// state management
extern void osd_init(void);
extern void osd_deinit(void);
extern int osd_open(SDL_Event event);
extern int osd_close(SDL_Event event);

// Complete a close the OSD requested while drawing. Returns 1 when it closed,
// so the caller can drop its own "OSD is open" state in the same step.
extern int osd_take_pending_close(void);

// keyboard event handler
extern int osd_handle(SDL_Event event);

// draw the osd interface, if it's open
extern void osd_present(int output_w, int output_h);

// future ui
extern void osd_ui_sb_update_icon_state(int tag, int state);
extern void osd_ui_sb_update_icon(int tag, int active);
extern void osd_ui_sb_update_icon_write(int tag, int active);
extern void osd_ui_sb_update_icon_wp(int tag, int state);

#ifdef __cplusplus
}
#endif

#endif /*_UNIX_OSD_H*/
