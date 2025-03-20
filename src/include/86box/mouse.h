/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the mouse driver.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2017-2019 Fred N. van Kempen.
 */

#ifndef EMU_MOUSE_H
#define EMU_MOUSE_H

#ifndef __cplusplus
/* Yes, a big no-no, but I'm saving myself time here. */
#include <stdatomic.h>
#endif

#define MOUSE_TYPE_NONE     0 /* no mouse configured */
#define MOUSE_TYPE_INTERNAL 1 /* achine has internal mouse */
#define MOUSE_TYPE_LOGIBUS  2 /* Logitech/ATI Bus Mouse */
#define MOUSE_TYPE_INPORT   3 /* Microsoft InPort Mouse */
#if 0
#    define MOUSE_TYPE_GENIBUS 4 /* Genius Bus Mouse */
#endif
#define MOUSE_TYPE_MSYSTEMS  5  /* Mouse Systems mouse */
#define MOUSE_TYPE_MICROSOFT 6  /* Microsoft 2-button Serial Mouse */
#define MOUSE_TYPE_MS3BUTTON 7  /* Microsoft 3-button Serial Mouse */
#define MOUSE_TYPE_MSWHEEL   8  /* Microsoft Serial Wheel Mouse */
#define MOUSE_TYPE_LOGITECH  9  /* Logitech 2-button Serial Mouse */
#define MOUSE_TYPE_LT3BUTTON 10 /* Logitech 3-button Serial Mouse */
#define MOUSE_TYPE_PS2       11 /* PS/2 series Bus Mouse */
#define MOUSE_TYPE_WACOM     12 /* WACOM tablet */
#define MOUSE_TYPE_WACOMARTP 13 /* WACOM tablet (ArtPad) */

#define MOUSE_TYPE_ONBOARD   0x80 /* Mouse is an on-board version of one of the above. */


#ifdef __cplusplus
extern "C" {
#endif

extern int    mouse_type;
extern int    mouse_input_mode; /* 2 = Absolute (Visible Crosshair), 1 = Absolute, 0 = Relative */
extern int    mouse_timed; /* 1 = Timed, 0 = Constant */
extern int    mouse_tablet_in_proximity;
extern double mouse_x_abs;
extern double mouse_y_abs;
extern int    tablet_tool_type;
extern double mouse_sensitivity;

#ifdef EMU_DEVICE_H
extern void           *mouse_ps2_init(const device_t *);

extern const device_t mouse_logibus_device;
extern const device_t mouse_logibus_onboard_device;
extern const device_t mouse_msinport_device;
#    ifdef USE_GENIBUS
extern const device_t mouse_genibus_device;
#    endif
extern const device_t mouse_mssystems_device;
extern const device_t mouse_msserial_device;
extern const device_t mouse_ltserial_device;
extern const device_t mouse_ps2_device;
#    ifdef USE_WACOM
extern const device_t mouse_wacom_device;
extern const device_t mouse_wacom_artpad_device;
#    endif
extern const device_t mouse_mtouch_device;
#endif

extern void            mouse_clear_x(void);
extern void            mouse_clear_y(void);
extern void            mouse_clear_coords(void);
extern void            mouse_clear_buttons(void);
extern void            mouse_subtract_x(int *delta_x, int *o_x, int min, int max, int abs);
extern void            mouse_subtract_y(int *delta_y, int *o_y, int min, int max, int invert, int abs);
extern void            mouse_subtract_coords(int *delta_x, int *delta_y, int *o_x, int *o_y,
                                             int min, int max, int invert, int abs);
extern  int            mouse_wheel_moved(void);
extern  int            mouse_moved(void);
extern  int            mouse_state_changed(void);
extern  int            mouse_mbut_changed(void);
extern void            mouse_scale_fx(double x);
extern void            mouse_scale_fy(double y);
extern void            mouse_scale_x(int x);
extern void            mouse_scale_y(int y);
extern void            mouse_scalef(double x, double y);
extern void            mouse_scale(int x, int y);
extern void            mouse_scale_axis(int axis, int val);
extern void            mouse_set_z(int z);
extern void            mouse_clear_z(void);
extern void            mouse_subtract_z(int *delta_z, int min, int max, int invert);
extern void            mouse_set_w(int w);
extern void            mouse_clear_w(void);
extern void            mouse_subtract_w(int *delta_w, int min, int max, int invert);
extern void            mouse_set_buttons_ex(int b);
extern int             mouse_get_buttons_ex(void);
extern void            mouse_set_sample_rate(double new_rate);
extern void            mouse_set_buttons(int buttons);
extern void            mouse_get_abs_coords(double *x_abs, double *y_abs);
extern void            mouse_process(void);
extern void            mouse_set_poll_ex(void (*poll_ex)(void));
extern void            mouse_set_poll(int (*f)(void *), void *);
extern const char *    mouse_get_name(int mouse);
extern const char *    mouse_get_internal_name(int mouse);
extern int             mouse_get_from_internal_name(char *s);
extern int             mouse_has_config(int mouse);
#ifdef EMU_DEVICE_H
extern const device_t *mouse_get_device(int mouse);
#endif
extern int             mouse_get_buttons(void);
extern int             mouse_get_ndev(void);
extern void            mouse_set_raw(int raw);
extern void            mouse_reset(void);
extern void            mouse_close(void);
extern void            mouse_init(void);

extern void            mouse_bus_set_irq(void *priv, int irq);

#ifdef __cplusplus
}
#endif

#endif /*EMU_MOUSE_H*/
