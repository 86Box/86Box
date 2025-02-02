/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the VLSI VL82c113 Combination I/O Chip.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2024 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/nvr.h>
#include <86box/sio.h>
#include <86box/plat_unused.h>

typedef struct vl82c113_t {
    uint8_t       index;
    uint8_t       nvr_enabled;
    uint8_t       regs[3];
    uint8_t       pad;
    uint16_t      nvr_base;
    int           cur_reg;
    nvr_t        *nvr;
    void         *kbc;
} vl82c113_t;

static void
vl82c113_nvr_handler(vl82c113_t *dev)
{
    const uint8_t  nvr_enabled = (dev->regs[0x00]) & 0x01;
    const uint16_t nvr_base    = ((dev->regs[0x01] << 8) | dev->regs[0x00]) & 0xfffe;

    if ((nvr_enabled != dev->nvr_enabled) || (nvr_base != dev->nvr_base)) {
        if (dev->nvr_enabled && (dev->nvr_base != 0x0000))
            nvr_at_handler(0, dev->nvr_base, dev->nvr);

        dev->nvr_enabled           = nvr_enabled;
        dev->nvr_base              = nvr_base;

        if (dev->nvr_enabled && (dev->nvr_base != 0x0000))
            nvr_at_handler(1, dev->nvr_base, dev->nvr);
    }
}

static void
vl82c113_out(uint16_t port, uint8_t val, void *priv)
{
    vl82c113_t *dev = (vl82c113_t *) priv;

    if (port == 0xec)
        dev->index = val;
    else if ((dev->index >= 0x1b) && (dev->index <= 0x1d)) {
        const uint8_t index  = dev->index - 0x1b;
        const uint8_t valxor = dev->regs[index] ^ val;

        dev->regs[index] = val;

        switch (index) {
            default:
                break;

            case 0x00:
            case 0x01:
                if (valxor)
                    vl82c113_nvr_handler(dev);
                break;

            case 0x02:
                if (valxor & 0x02)
                    kbc_at_set_ps2(dev->kbc, !(val & 0x02));
                break;
        }
    }
}

static uint8_t
vl82c113_in(uint16_t port, void *priv)
{
    const vl82c113_t *dev = (vl82c113_t *) priv;
    uint8_t           ret = 0xff;

    if (port == 0xed) {
        if ((dev->index >= 0x1b) && (dev->index <= 0x1d))
            ret = dev->regs[dev->index - 0x1b];
        else if (dev->index == 0x1f)
            /* REVID */
            ret = 0xc0;
    }

    return ret;
}

static void
vl82c113_reset(void *priv)
{
    vl82c113_t *dev = (vl82c113_t *) priv;

    memset(dev->regs, 0x00, sizeof(dev->regs));

    dev->regs[0x00] = 0x71;
    dev->regs[0x01] = 0x00;

    dev->regs[0x02] = 0xc3;

    kbc_at_set_ps2(dev->kbc, 0);

    vl82c113_nvr_handler(dev);
}

static void
vl82c113_close(void *priv)
{
    vl82c113_t *dev = (vl82c113_t *) priv;

    free(dev);
}

static void *
vl82c113_init(UNUSED(const device_t *info))
{
    vl82c113_t *dev  = (vl82c113_t *) calloc(1, sizeof(vl82c113_t));

    dev->nvr         = device_add(&at_nvr_device);

    dev->nvr_enabled = 1;
    dev->nvr_base    = 0x0070;

    /* Commands are standard. */
    dev->kbc         = device_add(&keyboard_at_device);

    vl82c113_reset(dev);

    io_sethandler(0x00ec, 0x0002, vl82c113_in, NULL, NULL, vl82c113_out, NULL, NULL, dev);

    return dev;
}

const device_t vl82c113_device = {
    .name          = "VLSI VL82c113 Combination I/O",
    .internal_name = "vl82c113",
    .flags         = 0,
    .local         = 0,
    .init          = vl82c113_init,
    .close         = vl82c113_close,
    .reset         = vl82c113_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

