/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the IBM PC 730/750 Rapid Resume power
 *          controller, a preprogrammed Philips 83C750 (IBM US patent
 *          5,530,879).
 *
 *          Its program is not available, so the commands are taken
 *          from how the LQKT47A BIOS uses them. A command is sent as
 *          nibbles in bits 3-0 of port 48h with bit 4 set. Ch, Dh and
 *          Eh take a data byte; for the others, bits 7-6 select clear,
 *          set or read of a flag in the 80C51's bit-addressable RAM,
 *          and a read returns the flag in bit 0 of port 48h. Bit 0 of
 *          port 49h is the patent's PROC_RDY ANDed with its CMD_STATE#
 *          heartbeat.
 *
 * Authors: Avastrap2, <110710503+Avastrap2@users.noreply.github.com>
 *
 *          Copyright 2026 Avastrap2.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>

#define PWR_FLAG_POWER  0x0e /* The supply stays on while this is set. */
#define PWR_FLAG_SWITCH 0x31 /* The BIOS boots normally after this wake reason. */

typedef struct ibm_pc700_power_t {
    uint8_t  ram[64];
    uint8_t  ptr;
    uint8_t  cmd;
    uint8_t  data;
    uint8_t  nibbles;
    uint8_t  reply;
    uint64_t busy_end;
} ibm_pc700_power_t;

#ifdef ENABLE_IBM_PC700_POWER_LOG
int ibm_pc700_power_do_log = ENABLE_IBM_PC700_POWER_LOG;

static void
ibm_pc700_power_log(const char *fmt, ...)
{
    va_list ap;

    if (ibm_pc700_power_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ibm_pc700_power_log(fmt, ...)
#endif

static void
ibm_pc700_power_set_flag(ibm_pc700_power_t *dev, uint8_t flag)
{
    dev->ram[0x20 + ((flag >> 3) & 0x07)] |= (1 << (flag & 0x07));
}

static void
ibm_pc700_power_command(ibm_pc700_power_t *dev)
{
    uint8_t *flags = &dev->ram[0x20 + ((dev->cmd >> 3) & 0x07)];
    uint8_t  mask  = 1 << (dev->cmd & 0x07);

    switch (dev->cmd >> 6) {
        case 0x00:
            *flags &= ~mask;
            if (dev->cmd == PWR_FLAG_POWER) {
                ibm_pc700_power_log("IBM PC 700 power: Supply off\n");
                plat_power_off();
            }
            break;
        case 0x01:
            *flags |= mask;
            break;
        case 0x02:
            dev->reply = !!(*flags & mask);
            break;
        default:
            /* The BIOS never sends a two-nibble command with bits 7-6 set. */
            break;
    }
}

static void
ibm_pc700_power_write(uint16_t port, uint8_t val, void *priv)
{
    ibm_pc700_power_t *dev = (ibm_pc700_power_t *) priv;

    if (port & 0x0001) {
        /* Bit 0 holds the controller in reset. */
        if (val & 0x01)
            dev->nibbles = 0;
        return;
    }

    if (!(val & 0x10))
        return;

    dev->busy_end = tsc + ((10 * TIMER_USEC) >> 32);
    val &= 0x0f;

    switch (dev->nibbles++) {
        case 0:
            dev->cmd = val << 4;
            break;
        case 1:
            if ((dev->cmd == 0xc0) || (dev->cmd == 0xd0) || (dev->cmd == 0xe0))
                dev->data = val << 4;
            else {
                dev->cmd |= val;
                dev->nibbles = 0;
                ibm_pc700_power_command(dev);
            }
            break;
        default:
            dev->data |= val;
            dev->nibbles = 0;
            if (dev->cmd == 0xd0)
                dev->ptr = dev->data & 0x3f;
            else if (dev->cmd == 0xe0) {
                dev->ram[dev->ptr] = dev->data;
                dev->ptr           = (dev->ptr + 1) & 0x3f;
            }
            /*
             * What Ch does is not known: the BIOS sends Ch 4Ah, then reads
             * port 48h, after POST, and Ch 2Dh while relocating SMBASE.
             */
            break;
    }
}

static uint8_t
ibm_pc700_power_read(uint16_t port, void *priv)
{
    const ibm_pc700_power_t *dev = (ibm_pc700_power_t *) priv;
    uint64_t                 period;
    uint8_t                  ret;

    if (port & 0x0001) {
        /* CMD_STATE# goes low for about 1.5 us every 50-60 us. */
        period = (55 * TIMER_USEC) >> 32;
        ret    = (tsc >= dev->busy_end) && ((tsc % period) >= ((3 * TIMER_USEC) >> 33));
    } else
        ret = dev->reply;

    return 0xfe | ret;
}

static void
ibm_pc700_power_close(void *priv)
{
    ibm_pc700_power_t *dev = (ibm_pc700_power_t *) priv;

    free(dev);
}

static void *
ibm_pc700_power_init(UNUSED(const device_t *info))
{
    ibm_pc700_power_t *dev = (ibm_pc700_power_t *) calloc(1, sizeof(ibm_pc700_power_t));

    ibm_pc700_power_set_flag(dev, PWR_FLAG_POWER);
    ibm_pc700_power_set_flag(dev, PWR_FLAG_SWITCH);

    io_sethandler(0x0048, 0x0002,
                  ibm_pc700_power_read, NULL, NULL, ibm_pc700_power_write, NULL, NULL, dev);

    return dev;
}

const device_t ibm_pc700_power_device = {
    .name          = "IBM PC 730/750 Power Controller",
    .internal_name = "ibm_pc700_power",
    .flags         = 0,
    .local         = 0,
    .init          = ibm_pc700_power_init,
    .close         = ibm_pc700_power_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
