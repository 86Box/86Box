/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Backend-neutral OSD core declarations.
 */
#ifndef OSD_CORE_HPP
#define OSD_CORE_HPP

/* Frontend callbacks supplied by the active OSD host. */
typedef struct osd_host_t {
    void (*toggle_fullscreen)(void);
    void (*request_exit)(void);
} osd_host_t;

void osd_core_set_host(const osd_host_t *host);

float osd_core_scaled(float value);

float osd_core_layout_scale_for_output(int output_w, int output_h);

/* Shared OSD layout scale. Frontends may supply a backend-specific hint. */
void osd_core_set_layout_scale(float scale);

/* Shared bitmap font setup for crisp integer OSD scaling. */
int  osd_core_default_font_size(void);
void osd_core_rebuild_default_font(int pixel_size);

/* Apply the retro/CRT style to the current ImGui context. */
void osd_core_setup_style(void);

/* Machine/window title shown at the top of the main menu. */
void osd_core_set_title(const char *title);

/* Reset the view back to the main menu. */
void osd_core_reset_to_menu(void);

/* Build the current view's ImGui windows. Call between ImGui::NewFrame() and
 * ImGui::Render(). Returns false when the OSD should close. */
bool osd_core_build_ui(void);

/* Handle an Escape key release. Returns true when the OSD should close
 * (already at the main menu), false when it just stepped back a view. */
bool osd_core_escape(void);

/* Route the emulator log into the OSD's log ring. */
void osd_core_install_log_hook(void);
void osd_core_remove_log_hook(void);

/* Draw the persistent indicator layer. */
void osd_core_draw_indicators(void);

/* Draw the transient message overlay, if one is still on screen. */
void osd_core_draw_message(void);

/* Show a non-blocking message that expires after a fixed duration. The text is
 * a printf-style format string for any additional arguments. Posting a message
 * closes the OSD, so the machine keeps running while the message is read. */
void osd_core_show_message(const char *text, ...) __attribute__((format(printf, 1, 2)));

/* True when the core has something to draw with the OSD closed: an active
 * indicator layer, or a message still fading out. Frontends gate their
 * rendering on this. */
bool osd_core_needs_render(void);

#endif /* OSD_CORE_HPP */
