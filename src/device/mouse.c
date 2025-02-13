/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Common driver module for MOUSE devices.
 *
 * TODO:    Add the Genius bus- and serial mouse.
 *          Remove the '3-button' flag from mouse types.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2017-2018 Fred N. van Kempen.
 */
#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/gdbstub.h>
#include <86box/mouse.h>
#include <86box/video.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>

typedef struct mouse_t {
    const device_t *device;
} mouse_t;

int mouse_type = 0;
int mouse_input_mode;
int mouse_timed = 1;
int mouse_tablet_in_proximity = 0;
int tablet_tool_type          = 1; /* 0 = Puck/Cursor, 1 = Pen */

double mouse_x_abs;
double mouse_y_abs;

double mouse_sensitivity = 1.0;

pc_timer_t mouse_timer; /* mouse event timer */

static const device_t mouse_none_device = {
    .name          = "None",
    .internal_name = "none",
    .flags         = 0,
    .local         = MOUSE_TYPE_NONE,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

static const device_t mouse_internal_device = {
    .name          = "Internal",
    .internal_name = "internal",
    .flags         = 0,
    .local         = MOUSE_TYPE_INTERNAL,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

static mouse_t mouse_devices[] = {
    // clang-format off
    { &mouse_none_device         },
    { &mouse_internal_device     },
    { &mouse_logibus_device      },
    { &mouse_msinport_device     },
#ifdef USE_GENIBUS
    { &mouse_genibus_device      },
#endif
    { &mouse_mssystems_device    },
    { &mouse_msserial_device     },
    { &mouse_ltserial_device     },
    { &mouse_ps2_device          },
#ifdef USE_WACOM
    { &mouse_wacom_device        },
    { &mouse_wacom_artpad_device },
#endif
    { &mouse_mtouch_device       },
    { NULL                       }
    // clang-format on
};

static _Atomic double  mouse_x;
static _Atomic double  mouse_y;
static atomic_int      mouse_z;
static atomic_int      mouse_buttons;

static int             mouse_delta_b;
static int             mouse_old_b;

static void           *mouse_priv;
static int             mouse_nbut;
static int             mouse_raw;
static int (*mouse_dev_poll)(void *priv);
static void (*mouse_poll_ex)(void) = NULL;

static double          sample_rate = 200.0;

#ifdef ENABLE_MOUSE_LOG
int mouse_do_log = ENABLE_MOUSE_LOG;

static void
mouse_log(const char *fmt, ...)
{
    va_list ap;

    if (mouse_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define mouse_log(fmt, ...)
#endif

void
mouse_clear_x(void)
{
    mouse_x = 0.0;
}

void
mouse_clear_y(void)
{
    mouse_y = 0.0;
}

void
mouse_clear_coords(void)
{
    mouse_clear_x();
    mouse_clear_y();

    mouse_z = 0;
}

void
mouse_clear_buttons(void)
{
    mouse_buttons  = 0x00;
    mouse_old_b    = 0x00;

    mouse_delta_b  = 0x00;
}

static double
mouse_scale_coord_x(double x, int mul)
{
    double ratio = 1.0;

    if (!mouse_raw)        
        ratio = ((double) monitors[0].mon_unscaled_size_x) / monitors[0].mon_res_x;

    if (mul)
        x *= ratio;
    else
        x /= ratio;

    return x;
}

static double
mouse_scale_coord_y(double y, int mul)
{
    double ratio = 1.0;

    if (!mouse_raw)        
        ratio = ((double) monitors[0].mon_efscrnsz_y) / monitors[0].mon_res_y;

    if (mul)
        y *= ratio;
    else
        y /= ratio;

    return y;
}

void
mouse_subtract_x(int *delta_x, int *o_x, int min, int max, int abs)
{
    double real_x = atomic_load(&mouse_x);
    double smax_x;
    double rsmin_x;
    double smin_x;
    int    ds_x;
    int    scaled_x;

    rsmin_x = mouse_scale_coord_x(min, 0);
    if (abs) {
        smax_x = mouse_scale_coord_x(max, 0) + ABS(rsmin_x);
        max += ABSD(min);
        real_x += rsmin_x;
        smin_x = 0;
    } else {
        smax_x = mouse_scale_coord_x(max, 0);
        smin_x = rsmin_x;
    }

    smax_x = floor(smax_x);
    smin_x = ceil(smin_x);

    /* Default the X overflow to 1. */
    if (o_x != NULL)
        *o_x = 1;

    ds_x = mouse_scale_coord_x(real_x, 1);

    if (ds_x >= 0.0)
        scaled_x = (int) floor(mouse_scale_coord_x(real_x, 1));
    else
        scaled_x = (int) ceil(mouse_scale_coord_x(real_x, 1));

    if (real_x > smax_x) {
        if (abs) {
            *delta_x = scaled_x;
            real_x -= mouse_scale_coord_x((double) scaled_x, 0);
        } else {
            *delta_x = max;
            real_x -= smax_x;
       }
    } else if (real_x < smin_x) {
        if (abs) {
            *delta_x = scaled_x;
            real_x -= mouse_scale_coord_x((double) scaled_x, 0);
        } else {
            *delta_x = min;
            real_x += ABSD(smin_x);
        }
    } else {
        *delta_x = scaled_x;
        real_x -= mouse_scale_coord_x((double) scaled_x, 0);
        if (o_x != NULL)
            *o_x = 0;
    }

    if (abs)
        real_x -= rsmin_x;

    atomic_store(&mouse_x, real_x);
}

/* It appears all host platforms give us y in the Microsoft format
   (positive to the south), so for all non-Microsoft report formsts,
   we have to invert that. */
void
mouse_subtract_y(int *delta_y, int *o_y, int min, int max, int invert, int abs)
{
    double real_y = atomic_load(&mouse_y);
    double smax_y;
    double rsmin_y;
    double smin_y;
    int    ds_y;
    int    scaled_y;

    if (invert)
        real_y = -real_y;

    rsmin_y = mouse_scale_coord_y(min, 0);
    if (abs) {
        smax_y = mouse_scale_coord_y(max, 0) + ABS(rsmin_y);
        max += ABSD(min);
        real_y += rsmin_y;
        smin_y = 0;
    } else {
        smax_y = mouse_scale_coord_y(max, 0);
        smin_y = rsmin_y;
    }

    smax_y = floor(smax_y);
    smin_y = ceil(smin_y);

    /* Default Y overflow to 1. */
    if (o_y != NULL)
        *o_y = 1;

    ds_y = mouse_scale_coord_x(real_y, 1);

    if (ds_y >= 0.0)
        scaled_y = (int) floor(mouse_scale_coord_x(real_y, 1));
    else
        scaled_y = (int) ceil(mouse_scale_coord_x(real_y, 1));

    if (real_y > smax_y) {
        if (abs) {
            *delta_y = scaled_y;
            real_y -= mouse_scale_coord_y((double) scaled_y, 0);
        } else {
            *delta_y = max;
            real_y -= smax_y;
       }
    } else if (real_y < smin_y) {
        if (abs) {
            *delta_y = scaled_y;
            real_y -= mouse_scale_coord_y((double) scaled_y, 0);
        } else {
            *delta_y = min;
            real_y += ABSD(smin_y);
        }
    } else {
        *delta_y = scaled_y;
        real_y -= mouse_scale_coord_y((double) scaled_y, 0);
        if (o_y != NULL)
            *o_y = 0;
    }

    if (abs)
        real_y -= rsmin_y;

    if (invert)
        real_y = -real_y;

    atomic_store(&mouse_y, real_y);
}

/* It appears all host platforms give us y in the Microsoft format
   (positive to the south), so for all non-Microsoft report formsts,
   we have to invenrt that. */
void
mouse_subtract_coords(int *delta_x, int *delta_y, int *o_x, int *o_y,
                      int min, int max, int invert, int abs)
{
    mouse_subtract_x(delta_x, o_x, min, max, abs);
    mouse_subtract_y(delta_y, o_y, min, max, invert, abs);
}

int
mouse_wheel_moved(void)
{
    int ret = !!(atomic_load(&mouse_z));

    return ret;
}

int
mouse_moved(void)
{
    int moved_x = !!((int) floor(ABSD(mouse_scale_coord_x(atomic_load(&mouse_x), 1))));
    int moved_y = !!((int) floor(ABSD(mouse_scale_coord_y(atomic_load(&mouse_y), 1))));

    /* Convert them to integer so we treat < 1.0 and > -1.0 as 0. */
    int ret = (moved_x || moved_y);

    return ret;
}

int
mouse_state_changed(void)
{
    int b;
    int b_mask    = (1 << mouse_nbut) - 1;
    int wheel     = (mouse_nbut >= 4);
    int ret;

    b = atomic_load(&mouse_buttons);
    mouse_delta_b = (b ^ mouse_old_b);
    mouse_old_b   = b;

    ret = mouse_moved() || ((atomic_load(&mouse_z) != 0) && wheel) || (mouse_delta_b & b_mask);

    return ret;
}

int
mouse_mbut_changed(void)
{
    return !!(mouse_delta_b & 0x04);
}

static void
mouse_timer_poll(UNUSED(void *priv))
{
    /* Poll at the specified sample rate. */
    timer_on_auto(&mouse_timer, 1000000.0 / sample_rate);

#ifdef USE_GDBSTUB /* avoid a KBC FIFO overflow when CPU emulation is stalled */
    if (gdbstub_step == GDBSTUB_EXEC) {
#endif
        if (mouse_timed)
            mouse_process();
#ifdef USE_GDBSTUB /* avoid a KBC FIFO overflow when CPU emulation is stalled */
    }
#endif
}

static void
atomic_double_add(_Atomic double *var, double val)
{
    double temp = atomic_load(var);

    temp += val;

    atomic_store(var, temp);
}

void
mouse_scale_fx(double x)
{
    atomic_double_add(&mouse_x, ((double) x) * mouse_sensitivity);
}

void
mouse_scale_fy(double y)
{
    atomic_double_add(&mouse_y, ((double) y) * mouse_sensitivity);
}

void
mouse_scale_x(int x)
{
    atomic_double_add(&mouse_x, ((double) x) * mouse_sensitivity);
}

void
mouse_scale_y(int y)
{
    atomic_double_add(&mouse_y, ((double) y) * mouse_sensitivity);
}

void
mouse_scalef(double x, double y)
{
    mouse_scale_fx(x);
    mouse_scale_fy(y);
}

void
mouse_scale(int x, int y)
{
    mouse_scale_x(x);
    mouse_scale_y(y);
}

void
mouse_scale_axis(int axis, int val)
{
    if (axis == 1)
        mouse_scale_y(val);
    else if (axis == 0)
        mouse_scale_x(val);
}

void
mouse_set_z(int z)
{
    atomic_fetch_add(&mouse_z, z);
}

void
mouse_clear_z(void)
{
    atomic_store(&mouse_z, 0);
}

void
mouse_subtract_z(int *delta_z, int min, int max, int invert)
{
    int z = atomic_load(&mouse_z);
    int real_z = invert ? -z : z;

    if (real_z > max) {
        *delta_z = max;
        real_z -= max;
    } else if (real_z < min) {
        *delta_z = min;
        real_z += ABS(min);
    } else {
        *delta_z = real_z;
        real_z = 0;
    }

    atomic_store(&mouse_z, invert ? -real_z : real_z);
}

void
mouse_set_buttons_ex(int b)
{
    atomic_store(&mouse_buttons, b);
}

int
mouse_get_buttons_ex(void)
{
    return atomic_load(&mouse_buttons);
}

void
mouse_set_sample_rate(double new_rate)
{
    mouse_timed = (new_rate > 0.0);

    timer_stop(&mouse_timer);

    sample_rate = new_rate;
    if (mouse_timed)
        timer_on_auto(&mouse_timer, 1000000.0 / sample_rate);
}

/* Callback from the hardware driver. */
void
mouse_set_buttons(int buttons)
{
    mouse_nbut = buttons;
}

void
mouse_get_abs_coords(double *x_abs, double *y_abs)
{
    *x_abs = mouse_x_abs;
    *y_abs = mouse_y_abs;
}

void
mouse_process(void)
{
    if ((mouse_input_mode >= 1) && mouse_poll_ex)
        mouse_poll_ex();
    else if ((mouse_input_mode == 0) && (mouse_dev_poll != NULL))
        mouse_dev_poll(mouse_priv);
}

void
mouse_set_poll_ex(void (*poll_ex)(void))
{
    mouse_poll_ex = poll_ex;
}

void
mouse_set_poll(int (*func)(void *), void *arg)
{
    mouse_dev_poll = func;
    mouse_priv     = arg;
}

const char *
mouse_get_name(int mouse)
{
    return (mouse_devices[mouse].device->name);
}

const char *
mouse_get_internal_name(int mouse)
{
    return device_get_internal_name(mouse_devices[mouse].device);
}

int
mouse_get_from_internal_name(char *s)
{
    int c = 0;

    while (mouse_devices[c].device != NULL) {
        if (!strcmp((char *) mouse_devices[c].device->internal_name, s))
            return c;
        c++;
    }

    return 0;
}

int
mouse_has_config(int mouse)
{
    if (mouse_devices[mouse].device == NULL)
        return 0;

    return (mouse_devices[mouse].device->config ? 1 : 0);
}

const device_t *
mouse_get_device(int mouse)
{
    return (mouse_devices[mouse].device);
}

int
mouse_get_buttons(void)
{
    return mouse_nbut;
}

/* Return number of MOUSE types we know about. */
int
mouse_get_ndev(void)
{
    return ((sizeof(mouse_devices) / sizeof(mouse_t)) - 1);
}

void
mouse_set_raw(int raw)
{
    mouse_raw = raw;
}

void
mouse_reset(void)
{
    if (mouse_priv != NULL)
        return; /* Mouse already initialized. */

    mouse_log("MOUSE: reset(type=%d, '%s')\n",
              mouse_type, mouse_devices[mouse_type].device->name);

    /* Clear local data. */
    mouse_clear_coords();
    mouse_clear_buttons();
    mouse_input_mode                  = 0;
    mouse_timed                 = 1;

    /* If no mouse configured, we're done. */
    if (mouse_type == 0)
        return;

    timer_add(&mouse_timer, mouse_timer_poll, NULL, 0);

    /* Poll at 100 Hz, the default of a PS/2 mouse. */
    sample_rate = 100.0;
    timer_on_auto(&mouse_timer, 1000000.0 / sample_rate);

    if ((mouse_type > 1) && (mouse_devices[mouse_type].device != NULL))
        mouse_priv = device_add(mouse_devices[mouse_type].device);
}

void
mouse_close(void)
{
    mouse_priv     = NULL;
    mouse_nbut     = 0;
    mouse_dev_poll = NULL;

    timer_stop(&mouse_timer);
}

/* Initialize the mouse module. */
void
mouse_init(void)
{
    /* Initialize local data. */
    mouse_clear_coords();
    mouse_clear_buttons();

    mouse_type     = MOUSE_TYPE_NONE;
    mouse_priv     = NULL;
    mouse_nbut     = 0;
    mouse_dev_poll = NULL;
}
