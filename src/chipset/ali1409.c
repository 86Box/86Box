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
    uint8_t index;
    uint8_t cfg_locked;
    uint8_t regs[256];
    uint8_t shadow[4];
    uint8_t last_reg;
} ali1409_t;

/*
   This here is because from the two BIOS'es I used to reverse engineer this,
   it is unclear which of the two interpretations of the shadow RAM register
   operation is correct.
   The 16 kB interpretation appears to work fine right now but it may be wrong,
   so I left the 32 kB interpretation in as well.
 */
#ifdef INTERPRETATION_32KB
#define SHADOW_SIZE 0x00008000
#else
#define SHADOW_SIZE 0x00004000
#endif

static void
ali1409_shadow_recalc(ali1409_t *dev)
{
    uint32_t base = 0x000c0000;

    for (uint8_t i = 0; i < 4; i++) {
        uint8_t reg = 0x08 + i;

#ifdef INTERPRETATION_32KB
        for (uint8_t j = 0; j < 4; j += 2) {
            uint8_t mask = (0x03 << j);
#else
        for (uint8_t j = 0; j < 4; j++) {
            uint8_t mask = (0x01 << j);
#endif
            uint8_t r_on  = dev->regs[reg] & 0x10;
            uint8_t w_on  = dev->regs[reg] & 0x20;
            uint8_t val   = dev->regs[reg] & mask;
            uint8_t xor   = (dev->shadow[i] ^ dev->regs[reg]) & (mask | 0x30);
            int     read  = r_on ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
            int     write = w_on ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;

            if (xor) {
#ifdef INTERPRETATION_32KB
                switch (val >> j) {
                    case 0x00:
                        mem_set_mem_state_both(base, SHADOW_SIZE, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
                        break;
                    case 0x01:
                        mem_set_mem_state_both(base, SHADOW_SIZE, MEM_READ_EXTANY | write);
                        break;
                    case 0x02:
                        mem_set_mem_state_both(base, SHADOW_SIZE, read | write);
                        break;
                    case 0x03:
                        mem_set_mem_state_both(base, SHADOW_SIZE, read | MEM_WRITE_EXTANY);
                        break;
                }
#else
                switch (val >> j) {
                    case 0x00:
                        mem_set_mem_state_both(base, SHADOW_SIZE, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
                        break;
                    case 0x01:
                        mem_set_mem_state_both(base, SHADOW_SIZE, read | write);
                        break;
                }
#endif
            }

            base += SHADOW_SIZE;
        }

        dev->shadow[i] = dev->regs[reg];
    }

    flushmmucache_nopc();
}

static void
ali1409_write(uint16_t addr, uint8_t val, void *priv)
{
    ali1409_t *dev = (ali1409_t *) priv;
    ali1409_log("[%04X:%08X] [W] %04X = %02X\n", CS, cpu_state.pc, addr, val);

    if (addr & 0x0001) {
        if (dev->cfg_locked) {
            if ((dev->last_reg == 0x14) && (val == 0x09))
                dev->cfg_locked = 0;

            dev->last_reg = val;
            return;
        }

        /* It appears writing anything at all to register 0xFF locks it again. */
        if (dev->index == 0xff)
            dev->cfg_locked = 1;
        else if (dev->index < 0x44) {
            ali1409_log("[%04X:%08X] [W] Register %02X = %02X\n", CS, cpu_state.pc, dev->index, val);

            if (dev->index < 0x10) { 
                dev->regs[dev->index] = val;

                /*
                   There are still a lot of unknown here, but unfortunately, this is
                   as far as I have been able to come with two BIOS'es that are
                   available (the Acer 100T and an AMI Color dated 07/07/91).
                 */
                switch (dev->index) {
                    case 0x02:
                        /*
                           - Bit 7: The RAS address hold time:
                               - 0: 1/2 T;
                               - 1: 1 T.
                           - Bits 6-4: The RAS precharge time:
                               - 0, 0, 0: 1.5 T;
                               - 0, 0, 1: 2 T;
                               - 0, 1, 0: 2.5 T;
                               - 0, 1, 1: 3 T;
                               - 1, 0, 0: 3.5 T;
                               - 1, 0, 1: 4 T;
                               - 1, 1, 0: Reserved;
                               - 1, 1, 1: Reserved.
                           - Bit 3: Early miss cycle:
                               - 0: Disabled;
                               - 1: Enabled.
                         */
                        break;
                    case 0x03:
                        /*
                           - Bit 6: CAS pulse for read cycle:
                               - 0: 1 T;
                               - 1: 1.5 T or 2 T.
                               I can not get the 2.5 T or 3 T setting to apply so
                               I have no idea what bit governs that.
                           - Bits 5, 4: CAS pulse for write cycle:
                               - 0, 0: 0.5 T or 1 T;
                               - 0, 1: 1.5 T or 2 T;
                               - 1, 0: 2.5 T or 3 T;
                               - 1, 1: Reserved.
                           - Bit 3: CAS active for read cycle:
                               - 0: Disabled;
                               - 1: Enabled.
                           - Bit 2: CAS active for write cycle:
                               - 0: Disabled;
                               - 1: Enabled.
                         */
                        break;
                    case 0x06:
                        /*
                           - Bits 6-4: Clock divider:
                               - 0, 0, 0: / 2;
                               - 0, 0, 1: / 4;
                               - 0, 1, 0: / 8;
                               - 0, 1, 1: Reserved;
                               - 1, 0, 0: / 3;
                               - 1, 0, 1: / 6;
                               - 1, 1, 0: / 5;
                               - 1, 1, 1: / 10.
                         */
                        switch ((val >> 4) & 7) {
                            default:
                            case 3: /* Reserved */
                                cpu_set_isa_speed(7159091);
                                break;

                            case 0:
                                cpu_set_isa_speed(cpu_busspeed / 2);
                                break;

                            case 1:
                                cpu_set_isa_speed(cpu_busspeed / 4);
                                break;

                            case 2:
                                cpu_set_isa_speed(cpu_busspeed / 8);
                                break;

                            case 4:
                                cpu_set_isa_speed(cpu_busspeed / 3);
                                break;

                            case 5:
                                cpu_set_isa_speed(cpu_busspeed / 6);
                                break;

                            case 6:
                                cpu_set_isa_speed(cpu_busspeed / 5);
                                break;

                            case 7:
                                cpu_set_isa_speed(cpu_busspeed / 10);
                                break;
                        }
                        break;
                    case 0x08 ... 0x0b:
                        ali1409_shadow_recalc(dev);
                        break;
                    case 0x0c:
                        /*
                           This appears to be turbo in bit 4 (1 = on, 0 = off),
                           and bus speed in the rest of the bits.
                         */
                        break;
                    case 0x0d:
                        cpu_cache_ext_enabled = !!(val & 0x08);
                        cpu_update_waitstates();
                        break;
                }
            }
        }
    } else
        dev->index = val;
}


static uint8_t
ali1409_read(uint16_t addr, void *priv)
{
    const ali1409_t *dev = (ali1409_t *) priv;
    uint8_t          ret = 0xff;

    if (dev->cfg_locked)
         ret = 0xff;
    else if (addr & 0x0001) {
        if (dev->index < 0x44)
            ret = dev->regs[dev->index];
    } else
        ret = dev->index;

    ali1409_log("[%04X:%08X] [R] %04X = %02X\n", CS, cpu_state.pc, addr, ret);

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

    ali1409_log("Bus speed: %i\n", cpu_busspeed);

    io_sethandler(0x0022, 0x0002, ali1409_read, NULL, NULL, ali1409_write, NULL, NULL, dev);

    dev->regs[0x0f] = 0x08;

    cpu_set_isa_speed(7159091);

    cpu_cache_ext_enabled = 0;
    cpu_update_waitstates();

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

