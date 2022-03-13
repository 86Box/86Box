/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Olivetti EVA (98/86) Gate Array.
 *
 *      Note: This chipset has no datasheet, everything were done via
 *      reverse engineering the BIOS of various machines using it.
 *
 * Authors: EngiNerd <webmaster.crrc@yahoo.it>
 *
 *		Copyright 2020-2021 EngiNerd
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
#include <86box/chipset.h>
#include <86box/video.h>
#include <86box/mem.h>

typedef struct
{
    uint8_t	reg_065;
    uint8_t	reg_067;
    uint8_t reg_069;
} olivetti_eva_t;

#ifdef ENABLE_OLIVETTI_EVA_LOG
int olivetti_eva_do_log = ENABLE_OLIVETTI_EVA_LOG;
static void
olivetti_eva_log(const char *fmt, ...)
{
    va_list ap;

    if (olivetti_eva_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
    	va_end(ap);
    }
}
#else
#define olivetti_eva_log(fmt, ...)
#endif

static void
olivetti_eva_write(uint16_t addr, uint8_t val, void *priv)
{
    olivetti_eva_t *dev = (olivetti_eva_t *) priv;
    olivetti_eva_log("Olivetti EVA Gate Array: Write %02x at %02x\n", val, addr);

    switch (addr) {
        case 0x065:
            dev->reg_065 = val;
            break;
        case 0x067:
            dev->reg_067 = val;
            break;
        case 0x069:
            dev->reg_069 = val;
            /*
             * Unfortunately, if triggered, the BIOS remapping function fails causing
             * a fatal error. Therefore, this code section is currently commented.
             */
            // if (val & 1){
            //     /*
            //      * Set the register to 7 or above for the BIOS to trigger the
            //      * memory remapping function if shadowing is active.
            //      */
            //     dev->reg_069 = 0x7;
            // }
            // if (val & 8) {
            //     /*
            //      * Activate shadowing for region e0000-fffff
            //      */
            //     mem_remap_top(256);
            //     mem_set_mem_state_both(0xa0000, 0x60000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
            // }
            break;
    }
}

static uint8_t
olivetti_eva_read(uint16_t addr, void *priv)
{
    olivetti_eva_t *dev = (olivetti_eva_t *) priv;
    uint8_t ret = 0xff;
    switch (addr) {
        case 0x065:
            ret = dev->reg_065;
            break;
        case 0x067:
            /* never happens */
            ret = dev->reg_067;
            break;
        case 0x069:
            ret = dev->reg_069;
            break;
    }
    olivetti_eva_log("Olivetti EVA Gate Array: Read %02x at %02x\n", ret, addr);
    return ret;
}


static void
olivetti_eva_close(void *priv)
{
    olivetti_eva_t *dev = (olivetti_eva_t *) priv;

    free(dev);
}

static void *
olivetti_eva_init(const device_t *info)
{
    olivetti_eva_t *dev = (olivetti_eva_t *) malloc(sizeof(olivetti_eva_t));
    memset(dev, 0, sizeof(olivetti_eva_t));

    /* GA98 registers */
    dev->reg_065 = 0x00;

    /* RAM page registers: never read, only set */
    dev->reg_067 = 0x00;

    /* RAM enable registers */
    dev->reg_069 = 0x0;

    io_sethandler(0x0065, 0x0001, olivetti_eva_read, NULL, NULL, olivetti_eva_write, NULL, NULL, dev);
    io_sethandler(0x0067, 0x0001, olivetti_eva_read, NULL, NULL, olivetti_eva_write, NULL, NULL, dev);
    io_sethandler(0x0069, 0x0001, olivetti_eva_read, NULL, NULL, olivetti_eva_write, NULL, NULL, dev);

    /* When shadowing is not enabled in BIOS, all upper memory is available as XMS */
    mem_remap_top(384);

    /*
     * Default settings when NVRAM is cleared activate shadowing.
     * Thus, to avoid boot errors, remap only 256k from UMB to XMS.
     * Remove this block once BIOS memory remapping works.
     */
    mem_remap_top(256);

    return dev;
}

const device_t olivetti_eva_device = {
    .name = "Olivetti EVA Gate Array",
    .internal_name = "olivetta_eva",
    .flags = 0,
    .local = 0,
    .init = olivetti_eva_init,
    .close = olivetti_eva_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
