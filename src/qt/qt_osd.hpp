/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Qt glue for the shared OSD core.
 */
#ifndef QT_QT_OSD_HPP
#define QT_QT_OSD_HPP

class QImage;

void qt_osd_shutdown(void);

bool qt_osd_is_visible(void);

void qt_osd_toggle(void);

void qt_osd_set_layout_scale_hint(float scale);

void qt_osd_render(int output_w, int output_h, float dpr);

const QImage *qt_osd_render_software(int logical_w, int logical_h, float dpr);

bool qt_osd_key(int qt_key, int qt_modifiers, bool down, bool repeat);

void qt_osd_mouse_pos(float x, float y);
void qt_osd_mouse_button(int qt_button, bool down);
void qt_osd_mouse_wheel(float dx, float dy);

#endif /* QT_QT_OSD_HPP */
