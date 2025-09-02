/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Dell 486 and 586 Jumper Readout.
 *
 *          Register 0x02:
 *              - Bit 0: ATX power: 1 = off, 0 = on.
 *
 *          Register 0x05:
 *              - Appears to be: 0x02 = On-board audio enabled;
 *                               0x07 = On-board audio disabled.
 *
 *          Register 0x07:
 *              - Bit 0: On-board NIC: 1 = present, 0 = absent;
 *              - Bit 1: On-board audio: 1 = present, 0 = absent;
 *              - Bits 4-2:
 *                  - 0, 0, 0 = GXL;
 *                  - 0, 0, 1 = GL+;
 *                  - 0, 1, 0 = GXMT;
 *                  - 0, 1, 1 = GMT+;
 *                  - 1, 0, 0 = GXM;
 *                  - 1, 0, 1 = GM+;
 *                  - 1, 1, 0 = WS;
 *                  - 1, 1, 1 = GWS+.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2025 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/sound.h>
#include <86box/chipset.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>

typedef struct dell_jumper_t {
    uint8_t index;
    uint8_t regs[256];
} dell_jumper_t;

#ifdef ENABLE_DELL_JUMPER_LOG
int dell_jumper_do_log = ENABLE_DELL_JUMPER_LOG;

static void
dell_jumper_log(const char *fmt, ...)
{
    va_list ap;

    if (dell_jumper_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define dell_jumper_log(fmt, ...)
#endif

static void
dell_jumper_write(uint16_t addr, uint8_t val, void *priv)
{
    dell_jumper_t *dev = (dell_jumper_t *) priv;

    dell_jumper_log("Dell Jumper: Write %02x\n", val);

    if (addr & 1)  switch (dev->index) {
        default:
            dev->regs[dev->index] = val;
            break;
        case 0x02:
            dev->regs[dev->index] = val;
            if (val & 0x04)
                /* Soft power off. */
                plat_power_off();
            break;
        case 0x05:
            dev->regs[dev->index] = (dev->regs[dev->index] & 0x02) | (val & 0xfd);
            if (machine_snd != NULL)  switch (val & 0x05) {
                default:
                case 0x05:
                    sb_vibra16s_onboard_relocate_base(0x0000, machine_snd);
                    break;
                case 0x00:
                    sb_vibra16s_onboard_relocate_base(0x0220, machine_snd);
                    break;
            }
            break;
        case 0x07:
            break;
    } else
        dev->index = val;
}

static uint8_t
dell_jumper_read(uint16_t addr, void *priv)
{
    const dell_jumper_t *dev = (dell_jumper_t *) priv;
    uint8_t                  ret = 0xff;

    dell_jumper_log("Dell Jumper: Read %02x\n", dev->jumper);

    if (addr & 1)
        ret = dev->regs[dev->index];
    else
        ret = dev->index;

    return ret;
}

static void
dell_jumper_reset(void *priv)
{
    dell_jumper_t *dev = (dell_jumper_t *) priv;

    dev->index = 0x00;
    memset(dev->regs, 0x00, 256);

    if (sound_card_current[0] == SOUND_INTERNAL)
        /* GXL, on-board audio present, on-board NIC absent. */
        dev->regs[0x07] = 0x02;
    else
        /* GXL, on-board audio absent, on-board NIC absent. */
        dev->regs[0x07] = 0x00;
}

static void
dell_jumper_close(void *priv)
{
    dell_jumper_t *dev = (dell_jumper_t *) priv;

    free(dev);
}

static void *
dell_jumper_init(const device_t *info)
{
    dell_jumper_t *dev = (dell_jumper_t *) calloc(1, sizeof(dell_jumper_t));

    dell_jumper_reset(dev);

    io_sethandler(0x00e8, 0x0002, dell_jumper_read, NULL, NULL, dell_jumper_write, NULL, NULL, dev);

    return dev;
}

const device_t dell_jumper_device = {
    .name          = "Dell Jumper Readout",
    .internal_name = "dell_jumper",
    .flags         = 0,
    .local         = 0,
    .init          = dell_jumper_init,
    .close         = dell_jumper_close,
    .reset         = dell_jumper_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
