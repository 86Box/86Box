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

/* NE2100 board layout: a 16-byte station address PROM at the card base,
   the chip registers above it (Linux lance.c, FreeBSD if_lnc). */
#define NE2100_PROM_SIZE    0x0010
#define NE2100_RDP_OFFSET   0x0010
#define NE2100_RAP_OFFSET   0x0012
#define NE2100_RESET_OFFSET 0x0014

typedef struct ne2100_t {
    lance_t *lance;

    uint8_t  prom[NE2100_PROM_SIZE];
    uint16_t reset_latch;
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

static uint8_t
ne2100_prom_read(uint16_t addr, void *priv)
{
    ne2100_t *dev = (ne2100_t *) priv;

    return dev->prom[addr & (NE2100_PROM_SIZE - 1)];
}

static uint16_t
ne2100_prom_readw(uint16_t addr, void *priv)
{
    ne2100_t *dev = (ne2100_t *) priv;
    int       off = (addr & (NE2100_PROM_SIZE - 1)) & ~1;

    return (uint16_t) (dev->prom[off] | (dev->prom[(off + 1) & (NE2100_PROM_SIZE - 1)] << 8));
}

static uint16_t
ne2100_reset_read(uint16_t addr, void *priv)
{
    ne2100_t *dev = (ne2100_t *) priv;

    (void) addr;

    return dev->reset_latch;
}

static void
ne2100_reset_write(uint16_t addr, uint16_t val, void *priv)
{
    ne2100_t *dev = (ne2100_t *) priv;

    (void) addr;

    /* The board reset line is level driven; the drivers read this back and
       write it again to release the chip, which is already out of reset. */
    dev->reset_latch = val;
}

static void *
ne2100_init(const device_t *info)
{
    ne2100_t *dev;
    lance_t  *lance;
    uint16_t  prom_sum;
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

    /* The NE2100 wires the LANCE interrupt pin straight to the ISA IRQ
       jumper, so there is no board-level gate to open. */
    lance_set_irq_gate(lance, 1);

    /* Station address PROM, 16 bytes: six address bytes, six unused bytes,
       the checksum word at 0x0C, and the 0x57 0x57 board signature at 0x0E/0x0F
       that the Linux lance.c driver probes for. Novell's drivers verify that
       the sum of the other 14 bytes matches this word. */
    memcpy(dev->prom, lance->mac, 6);
    memset(dev->prom + 6, 0x00, 6);
    dev->prom[14] = 0x57;
    dev->prom[15] = 0x57;
    prom_sum      = 0;
    for (int i = 0; i < 12; i++)
        prom_sum += dev->prom[i];
    prom_sum += dev->prom[14] + dev->prom[15];
    dev->prom[12] = (uint8_t) (prom_sum & 0xff);
    dev->prom[13] = (uint8_t) (prom_sum >> 8);

    io_sethandler(lance->base, NE2100_PROM_SIZE,
                  ne2100_prom_read, ne2100_prom_readw, NULL,
                  NULL, NULL, NULL, dev);
    io_sethandler(lance->base + NE2100_RDP_OFFSET, 0x0002,
                  NULL, lance_rdp_read, NULL,
                  NULL, lance_rdp_write, NULL, lance);
    io_sethandler(lance->base + NE2100_RAP_OFFSET, 0x0002,
                  NULL, lance_rap_read, NULL,
                  NULL, lance_rap_write, NULL, lance);
    io_sethandler(lance->base + NE2100_RESET_OFFSET, 0x0002,
                  NULL, ne2100_reset_read, NULL,
                  NULL, ne2100_reset_write, NULL, dev);

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

const device_t ne2100_novell_device = {
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
