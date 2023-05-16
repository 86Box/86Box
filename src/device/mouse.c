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
#include <stdarg.h>
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

typedef struct {
    const device_t *device;
} mouse_t;

int mouse_type = 0;
int mouse_x;
int mouse_y;
int mouse_z;
int mouse_buttons;
int mouse_mode;
int mouse_tablet_in_proximity = 0;
int tablet_tool_type          = 1; /* 0 = Puck/Cursor, 1 = Pen */

double mouse_x_abs;
double mouse_y_abs;

pc_timer_t mouse_timer; /* mouse event timer */

static const device_t mouse_none_device = {
    .name          = "None",
    .internal_name = "none",
    .flags         = 0,
    .local         = MOUSE_TYPE_NONE,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    { .poll = NULL },
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
    { .poll = NULL },
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
#if 0
    { &mouse_genibus_device      },
#endif
    { &mouse_mssystems_device    },
    { &mouse_msserial_device     },
    { &mouse_ltserial_device     },
    { &mouse_ps2_device          },
    { &mouse_wacom_device        },
    { &mouse_wacom_artpad_device },
    { NULL                       }
    // clang-format on
};

static const device_t *mouse_curr;
static void           *mouse_priv;
static int             mouse_nbut;
static int (*mouse_dev_poll)(int x, int y, int z, int b, void *priv);
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

/* Initialize the mouse module. */
void
mouse_init(void)
{
    /* Initialize local data. */
    mouse_x = mouse_y = mouse_z = 0;
    mouse_buttons               = 0x00;

    mouse_type     = MOUSE_TYPE_NONE;
    mouse_curr     = NULL;
    mouse_priv     = NULL;
    mouse_nbut     = 0;
    mouse_dev_poll = NULL;
}

void
mouse_close(void)
{
    if (mouse_curr == NULL)
        return;

    mouse_curr     = NULL;
    mouse_priv     = NULL;
    mouse_nbut     = 0;
    mouse_dev_poll = NULL;

    timer_stop(&mouse_timer);
}

static void
mouse_timer_poll(void *priv)
{
    /* Poll at 255 Hz, maximum supported by PS/2 mic. */
    timer_on_auto(&mouse_timer, 1000000.0 / sample_rate);

#ifdef USE_GDBSTUB /* avoid a KBC FIFO overflow when CPU emulation is stalled */
    if (gdbstub_step == GDBSTUB_EXEC)
#endif
        mouse_process();
}

void
mouse_set_sample_rate(double new_rate)
{
    timer_stop(&mouse_timer);

    sample_rate = new_rate;
    timer_on_auto(&mouse_timer, 1000000.0 / sample_rate);
}

void
mouse_reset(void)
{
    if (mouse_curr != NULL)
        return; /* Mouse already initialized. */

    mouse_log("MOUSE: reset(type=%d, '%s')\n",
              mouse_type, mouse_devices[mouse_type].device->name);

    /* Clear local data. */
    mouse_x = mouse_y = mouse_z = 0;
    mouse_buttons               = 0x00;
    mouse_mode                  = 0;

    /* If no mouse configured, we're done. */
    if (mouse_type == 0)
        return;

    timer_add(&mouse_timer, mouse_timer_poll, NULL, 0);

    /* Poll at 100 Hz, the default of a PS/2 mouse. */
    sample_rate = 100.0;
    timer_on_auto(&mouse_timer, 1000000.0 / sample_rate);

    mouse_curr = mouse_devices[mouse_type].device;

    if (mouse_curr != NULL)
        mouse_priv = device_add(mouse_curr);
}

/* Callback from the hardware driver. */
void
mouse_set_buttons(int buttons)
{
    mouse_nbut = buttons;
}

void
mouse_set_poll_ex(void (*poll_ex)(void))
{
    mouse_poll_ex = poll_ex;
}

void
mouse_process(void)
{
    if (mouse_curr == NULL)
        return;

    if (mouse_poll_ex)
        mouse_poll_ex();
    else
        mouse_poll();

    if ((mouse_dev_poll != NULL) || (mouse_curr->poll != NULL)) {
        if (mouse_curr->poll != NULL)
            mouse_curr->poll(mouse_x, mouse_y, mouse_z, mouse_buttons, mouse_x_abs, mouse_y_abs, mouse_priv);
        else
            mouse_dev_poll(mouse_x, mouse_y, mouse_z, mouse_buttons, mouse_priv);

        /* Reset mouse deltas. */
        mouse_x = mouse_y = mouse_z = 0;
    }
}

void
mouse_set_poll(int (*func)(int, int, int, int, void *), void *arg)
{
    if (mouse_type != MOUSE_TYPE_INTERNAL)
        return;

    mouse_dev_poll = func;
    mouse_priv     = arg;
}

char *
mouse_get_name(int mouse)
{
    return ((char *) mouse_devices[mouse].device->name);
}

char *
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
