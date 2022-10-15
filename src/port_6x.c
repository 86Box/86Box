/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of Ports 61, 62, and 63 used by various
 *		machines.
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2021 Miran Grca.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/m_xt_xi8088.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sound.h>
#include <86box/snd_speaker.h>
#include <86box/pit.h>
#include <86box/ppi.h>
#include <86box/video.h>
#include <86box/port_6x.h>

#define PS2_REFRESH_TIME (16 * TIMER_USEC)

#define PORT_6X_TURBO    1
#define PORT_6X_EXT_REF  2
#define PORT_6X_MIRROR   4
#define PORT_6X_SWA      8

static void
port_6x_write(uint16_t port, uint8_t val, void *priv)
{
    port_6x_t *dev = (port_6x_t *) priv;

    port &= 3;

    if ((port == 3) && (dev->flags & PORT_6X_MIRROR))
        port = 1;

    switch (port) {
        case 1:
            ppi.pb = (ppi.pb & 0x10) | (val & 0x0f);

            speaker_update();
            speaker_gated  = val & 1;
            speaker_enable = val & 2;
            if (speaker_enable)
                was_speaker_enable = 1;
            pit_devs[0].set_gate(pit_devs[0].data, 2, val & 1);

            if (dev->flags & PORT_6X_TURBO)
                xi8088_turbo_set(!!(val & 0x04));
            break;
    }
}

static uint8_t
port_61_read_simple(uint16_t port, void *priv)
{
    uint8_t ret = ppi.pb & 0x1f;

    if (ppispeakon)
        ret |= 0x20;

    return (ret);
}

static uint8_t
port_61_read(uint16_t port, void *priv)
{
    port_6x_t *dev = (port_6x_t *) priv;
    uint8_t    ret = 0xff;

    if (dev->flags & PORT_6X_EXT_REF) {
        ret = ppi.pb & 0x0f;

        if (dev->refresh)
            ret |= 0x10;
    } else
        ret = ppi.pb & 0x1f;

    if (ppispeakon)
        ret |= 0x20;

    if (dev->flags & PORT_6X_TURBO)
        ret = (ret & 0xfb) | (xi8088_turbo_get() ? 0x04 : 0x00);

    return (ret);
}

static uint8_t
port_62_read(uint16_t port, void *priv)
{
    uint8_t ret = 0xff;

    /* SWA on Olivetti M240 mainboard (off=1) */
    ret = 0x00;
    if (ppi.pb & 0x8) {
        /* Switches 4, 5 - floppy drives (number) */
        int i, fdd_count = 0;
        for (i = 0; i < FDD_NUM; i++) {
            if (fdd_get_flags(i))
                fdd_count++;
        }
        if (!fdd_count)
            ret |= 0x00;
        else
            ret |= ((fdd_count - 1) << 2);
        /* Switches 6, 7 - monitor type */
        if (video_is_mda())
            ret |= 0x3;
        else if (video_is_cga())
            ret |= 0x2; /* 0x10 would be 40x25 */
        else
            ret |= 0x0;
    } else {
        /* bit 2 always on */
        ret |= 0x4;
        /* Switch 8 - 8087 FPU. */
        if (hasfpu)
            ret |= 0x02;
    }

    return (ret);
}

static void
port_6x_refresh(void *priv)
{
    port_6x_t *dev = (port_6x_t *) priv;

    dev->refresh = !dev->refresh;
    timer_advance_u64(&dev->refresh_timer, PS2_REFRESH_TIME);
}

static void
port_6x_close(void *priv)
{
    port_6x_t *dev = (port_6x_t *) priv;

    timer_disable(&dev->refresh_timer);

    free(dev);
}

void *
port_6x_init(const device_t *info)
{
    port_6x_t *dev = (port_6x_t *) malloc(sizeof(port_6x_t));
    memset(dev, 0, sizeof(port_6x_t));

    dev->flags = info->local & 0xff;

    if (dev->flags & (PORT_6X_TURBO | PORT_6X_EXT_REF)) {
        io_sethandler(0x0061, 1, port_61_read, NULL, NULL, port_6x_write, NULL, NULL, dev);

        if (dev->flags & PORT_6X_EXT_REF)
            timer_add(&dev->refresh_timer, port_6x_refresh, dev, 1);

        if (dev->flags & PORT_6X_MIRROR)
            io_sethandler(0x0063, 1, port_61_read, NULL, NULL, port_6x_write, NULL, NULL, dev);
    } else {
        io_sethandler(0x0061, 1, port_61_read_simple, NULL, NULL, port_6x_write, NULL, NULL, dev);

        if (dev->flags & PORT_6X_MIRROR)
            io_sethandler(0x0063, 1, port_61_read_simple, NULL, NULL, port_6x_write, NULL, NULL, dev);
    }

    if (dev->flags & PORT_6X_SWA)
        io_sethandler(0x0062, 1, port_62_read, NULL, NULL, NULL, NULL, NULL, dev);

    return dev;
}

const device_t port_6x_device = {
    .name          = "Port 6x Registers",
    .internal_name = "port_6x",
    .flags         = 0,
    .local         = 0,
    .init          = port_6x_init,
    .close         = port_6x_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t port_6x_xi8088_device = {
    .name          = "Port 6x Registers (Xi8088)",
    .internal_name = "port_6x_xi8088",
    .flags         = 0,
    .local         = PORT_6X_TURBO | PORT_6X_EXT_REF | PORT_6X_MIRROR,
    .init          = port_6x_init,
    .close         = port_6x_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t port_6x_ps2_device = {
    .name          = "Port 6x Registers (IBM PS/2)",
    .internal_name = "port_6x_ps2",
    .flags         = 0,
    .local         = PORT_6X_EXT_REF,
    .init          = port_6x_init,
    .close         = port_6x_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t port_6x_olivetti_device = {
    .name          = "Port 6x Registers (Olivetti)",
    .internal_name = "port_6x_olivetti",
    .flags         = 0,
    .local         = PORT_6X_SWA,
    .init          = port_6x_init,
    .close         = port_6x_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
