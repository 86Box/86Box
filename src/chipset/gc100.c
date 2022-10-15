/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the G2 GC100/GC100A chipset.
 *		NOTE: As documentation is currently available only for the
 *		CG100 chipset, the GC100A chipset has been reverese-engineered.
 *		Thus, its behavior may not be fully accurate.
 *
 *		Authors: EngiNerd, <webmaster.crrc@yahoo.it>
 *
 *		Copyright 2020-2021 EngiNerd.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/nmi.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/hdc.h>
#include <86box/gameport.h>
#include <86box/ibm_5161.h>
#include <86box/keyboard.h>
#include <86box/rom.h>
#include <86box/machine.h>
#include <86box/chipset.h>
#include <86box/io.h>
#include <86box/video.h>

typedef struct
{
    uint8_t reg[0x10];
} gc100_t;

#ifdef ENABLE_GC100_LOG
int gc100_do_log = ENABLE_GC100_LOG;

static void
gc100_log(const char *fmt, ...)
{
    va_list ap;

    if (gc100_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define gc100_log(fmt, ...)
#endif

static uint8_t
get_fdd_switch_settings(void)
{
    int i, fdd_count = 0;

    for (i = 0; i < FDD_NUM; i++) {
        if (fdd_get_flags(i))
            fdd_count++;
    }

    if (!fdd_count)
        return 0x00;
    else
        return ((fdd_count - 1) << 6) | 0x01;
}

static uint8_t
get_videomode_switch_settings(void)
{
    if (video_is_mda())
        return 0x30;
    else if (video_is_cga())
        return 0x20; /* 0x10 would be 40x25 */
    else
        return 0x00;
}

static void
gc100_write(uint16_t port, uint8_t val, void *priv)
{
    gc100_t *dev  = (gc100_t *) priv;
    uint16_t addr = port & 0xf;

    dev->reg[addr] = val;

    switch (addr) {
        /* addr 0x2
         * bits 5-7: not used
         * bit 4: intenal memory wait states
         * bits 2-3: external memory wait states
         * bits 0-1: i/o access wait states
         */
        case 2:
            break;

        /* addr 0x3
         * bits 1-7: not used
         * bit 0: turbo 0 xt 1
         */
        case 3:
            if (val & 1)
                cpu_dynamic_switch(0);
            else
                cpu_dynamic_switch(cpu);
            break;

            /* addr 0x5
             * programmable dip-switches
             * bits 6-7: floppy drive number
             * bits 4-5: video mode
             * bits 2-3: memory size
             * bit 1: fpu
             * bit 0: not used
             */

            /* addr 0x6 */

            /* addr 0x7 */
    }

    gc100_log("GC100: Write %02x at %02x\n", val, port);
}

static uint8_t
gc100_read(uint16_t port, void *priv)
{
    gc100_t *dev  = (gc100_t *) priv;
    uint8_t  ret  = 0xff;
    uint16_t addr = port & 0xf;

    ret = dev->reg[addr];

    gc100_log("GC100: Read %02x at %02x\n", ret, port);

    switch (addr) {
        /* addr 0x2
         * bits 5-7: not used
         * bit 4: intenal memory wait states
         * bits 2-3: external memory wait states
         * bits 0-1: i/o access wait states
         */
        case 0x2:
            break;

        /* addr 0x3
         * bits 1-7: not used
         * bit 0: turbo 0 xt 1
         */
        case 0x3:
            break;

        /* addr 0x5
         * programmable dip-switches
         * bits 6-7: floppy drive number
         * bits 4-5: video mode
         * bits 2-3: memory size
         * bit 1: fpu
         * bit 0: not used
         */
        case 0x5:
            ret = ret & 0x0c;
            ret |= get_fdd_switch_settings();
            ret |= get_videomode_switch_settings();
            if (hasfpu)
                ret |= 0x02;
            break;

            /* addr 0x6 */

            /* addr 0x7 */
    }

    return ret;
}

static void
gc100_close(void *priv)
{
    gc100_t *dev = (gc100_t *) priv;

    free(dev);
}

static void *
gc100_init(const device_t *info)
{
    gc100_t *dev = (gc100_t *) malloc(sizeof(gc100_t));
    memset(dev, 0, sizeof(gc100_t));

    dev->reg[0x2] = 0xff;
    dev->reg[0x3] = 0x0;
    dev->reg[0x5] = 0x0;
    dev->reg[0x6] = 0x0;
    dev->reg[0x7] = 0x0;

    if (info->local) {
        /* GC100A */
        io_sethandler(0x0c2, 0x02, gc100_read, NULL, NULL, gc100_write, NULL, NULL, dev);
        io_sethandler(0x0c5, 0x03, gc100_read, NULL, NULL, gc100_write, NULL, NULL, dev);
    } else {
        /* GC100 */
        io_sethandler(0x022, 0x02, gc100_read, NULL, NULL, gc100_write, NULL, NULL, dev);
        io_sethandler(0x025, 0x01, gc100_read, NULL, NULL, gc100_write, NULL, NULL, dev);
    }

    return dev;
}

const device_t gc100_device = {
    .name          = "G2 GC100",
    .internal_name = "gc100",
    .flags         = 0,
    .local         = 0,
    .init          = gc100_init,
    .close         = gc100_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t gc100a_device = {
    .name          = "G2 GC100A",
    .internal_name = "gc100a",
    .flags         = 0,
    .local         = 1,
    .init          = gc100_init,
    .close         = gc100_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
