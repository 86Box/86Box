/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the ALi M1409 chipset.
 *
 * Note:    This chipset has no datasheet, everything were done via
 *          reverse engineering.
 *
 *
 *
 * Authors: Jose Phillips, <jose@latinol.com>
 *          Sarah Walker, <https://pcem-emulator.co.uk/>
 *
 *          Copyright 2024 Jose Phillips.
 *          Copyright 2008-2018 Sarah Walker.
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

#include <86box/apm.h>
#include <86box/mem.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/smram.h>
#include <86box/chipset.h>
#include <86box/plat_unused.h>
 
#ifdef ENABLE_ALI1409_LOG 
int ali1409_do_log = ENABLE_ALI1409_LOG;

static void
ali1409_log(const char *fmt, ...)
{
    va_list ap;

    if (ali1409_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ali1409_log(fmt, ...)
#endif

typedef struct ali_1409_t {
    uint8_t is_g;
    uint8_t index;
    uint8_t cfg_locked;
    uint8_t reg_57h;
    uint8_t regs[256];
    uint8_t last_reg;
} ali1409_t;


static void
ali1409_write(uint16_t addr, uint8_t val, void *priv)
{
    ali1409_t *dev = (ali1409_t *) priv;
    ali1409_log ("INPUT:addr %02x ,Value %02x \n" , addr , val);

    if (addr & 1) {
                if (dev->cfg_locked) {
                        if (dev->last_reg == 0x14 && val == 0x09)
                                dev->cfg_locked = 0;

                        dev->last_reg = val;
                        return;
                }

                if (dev->index == 0xff && val == 0xff)
                        dev->cfg_locked = 1;
                else {
                        ali1409_log("Write reg %02x %02x %08x\n", dev->index, val, cs);
                        dev->regs[dev->index] = val;

                        switch (dev->index) {
                        case 0xa:
                                switch ((val >> 4) & 3) {
                                case 0:
                                        mem_set_mem_state(0xe0000, 0x10000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
                                        break;
                                case 1:
                                        mem_set_mem_state(0xe0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_EXTERNAL);
                                        break;
                                case 2:
                                        mem_set_mem_state(0xe0000, 0x10000, MEM_READ_EXTERNAL | MEM_WRITE_INTERNAL);
                                        break;
                                case 3:
                                        mem_set_mem_state(0xe0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                                        break;
                                }
                                break;
                        case 0xb:
                                switch ((val >> 4) & 3) {
                                case 0:
                                        mem_set_mem_state(0xf0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
                                        break;
                                case 1:
                                        mem_set_mem_state(0xf0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_EXTANY);
                                        break;
                                case 2:
                                        mem_set_mem_state(0xf0000, 0x10000, MEM_READ_EXTANY| MEM_WRITE_INTERNAL);
                                        break;
                                case 3:
                                        mem_set_mem_state(0xf0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                                        break;
                                }
                                break;
                        }
                }
        } else
                dev->index = val;
}


static uint8_t
ali1409_read(uint16_t addr, void *priv)
{
    ali1409_log ("reading at %02X\n",addr);
    const ali1409_t *dev = (ali1409_t *) priv;
    uint8_t          ret = 0xff;

    if (dev->cfg_locked)
         ret = 0xff;
    if (addr & 1) {
        if ((dev->index >= 0xc0 || dev->index == 0x20) && cpu_iscyrix)
                 ret = 0xff; 
            ret = dev->regs[dev->index];
        } else
                ret = dev->index;
    return ret;
}



static void
ali1409_close(void *priv)
{
    ali1409_t *dev = (ali1409_t *) priv;

    free(dev);
}

static void *
ali1409_init(UNUSED(const device_t *info))
{
    ali1409_t *dev = (ali1409_t *) calloc(1, sizeof(ali1409_t));

    dev->cfg_locked = 1;

    /* M1409 Ports:
                22h	Index Port
                23h	Data Port
    */
   
    ali1409_log ("Bus speed: %i", cpu_busspeed);
   

    io_sethandler(0x0022, 0x0002, ali1409_read, NULL, NULL, ali1409_write, NULL, NULL, dev);
    io_sethandler(0x037f, 0x0001, ali1409_read, NULL, NULL, ali1409_write, NULL, NULL, dev);
    io_sethandler(0x03f3, 0x0001, ali1409_read, NULL, NULL, ali1409_write, NULL, NULL, dev);

    return dev;
}

const device_t ali1409_device = {
    .name          = "ALi M1409",
    .internal_name = "ali1409",
    .flags         = 0,
    .local         = 0,
    .init          = ali1409_init,
    .close         = ali1409_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

