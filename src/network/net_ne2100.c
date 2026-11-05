/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          Implementation of the following network controllers:
 *              - Novell NE2100/NE1500T (ISA 16-bit).
 *
 * Based on @(#)am79c90.cpp Patrick Mackinlay (MAME)
 *
 * Authors: WNT50
 *          Patrick Mackinlay
 *
 *          Copyright 2026 WNT50.
 *          Copyright 2018-2025 Patrick Mackinlay.
 *          Portions Copyright (C) 2018  MAME Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/random.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/net_lance.h>

typedef struct ne2100_t {
    lance_t *lance;
} ne2100_t;

#ifdef ENABLE_NE2100_LOG
int ne2100_do_log = ENABLE_NE2100_LOG;

static void
ne2100_log(const char *fmt, ...)
{
    va_list ap;

    if (ne2100_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ne2100_log(fmt, ...)
#endif

static void *
ne2100_init(const device_t *info)
{
    ne2100_t *dev;
    lance_t  *lance;
    uint16_t  base;
    int       irq;
    int       mac;

    (void) info;

    dev = (ne2100_t *) calloc(1, sizeof(ne2100_t));
    if (dev == NULL)
        return NULL;

    lance = (lance_t *) device_add(&lance_device);
    if (lance == NULL) {
        free(dev);
        return NULL;
    }
    dev->lance = lance;

    base = device_get_config_hex16("base");
    irq  = device_get_config_int("irq");

    lance->base    = base;
    lance->irq_num = irq;

    /* Same Novell OUI as the other cards in the tree; the last three
       bytes identify this particular card. */
    mac = device_get_config_mac("mac", -1);
    lance->mac[0] = 0x00;
    lance->mac[1] = 0x00;
    lance->mac[2] = 0xd8;
    if (mac & 0xff000000) {
        lance->mac[3] = random_generate();
        lance->mac[4] = random_generate();
        lance->mac[5] = random_generate();
        mac           = (((int) lance->mac[3]) << 16);
        mac |= (((int) lance->mac[4]) << 8);
        mac |= ((int) lance->mac[5]);
        device_set_config_mac("mac", mac);
    } else {
        lance->mac[3] = (mac >> 16) & 0xff;
        lance->mac[4] = (mac >> 8) & 0xff;
        lance->mac[5] = (mac & 0xff);
    }

    lance_reset(lance);

    /* RDP is a single 16-bit port; it must not be registered over 0x0C62,
       or the RAP handler at that address is shadowed by the wider range. */
    io_sethandler(lance->base, 0x0002,
                  NULL, lance_rdp_read, NULL,
                  NULL, lance_rdp_write, NULL, lance);
    io_sethandler(lance->base + 0x0004, 0x0002,
                  NULL, lance_rdp_read, NULL,
                  NULL, lance_rdp_write, NULL, lance);
    io_sethandler(lance->base + 0x0002, 0x0002,
                  NULL, lance_rap_read, NULL,
                  NULL, lance_rap_write, NULL, lance);
    io_sethandler(lance->base + 0x0006, 0x0002,
                  NULL, lance_rap_read, NULL,
                  NULL, lance_rap_write, NULL, lance);

    lance->card = network_attach(lance, lance->mac, lance_rx, NULL);

    return dev;
}

static void
ne2100_close(void *priv)
{
    ne2100_t *dev = (ne2100_t *) priv;

    if (dev == NULL)
        return;

    device_close(&lance_device);

    free(dev);
}

// clang-format off
static const device_config_t ne2100_config[] = {
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x300,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x300", .value = 0x300 },
            { .description = "0x320", .value = 0x320 },
            { .description = "0x340", .value = 0x340 },
            { .description = "0x360", .value = 0x360 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 3,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "IRQ 2/9", .value = 9 },
            { .description = "IRQ 3",   .value = 3 },
            { .description = "IRQ 4",   .value = 4 },
            { .description = "IRQ 5",   .value = 5 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "mac",
        .description    = "MAC Address",
        .type           = CONFIG_MAC,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t ne2100_device = {
    .name          = "Novell NE2100/NE1500T",
    .internal_name = "novell_ne2100",
    .flags         = DEVICE_ISA16,
    .local         = 0,
    .init          = ne2100_init,
    .close         = ne2100_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ne2100_config
};
