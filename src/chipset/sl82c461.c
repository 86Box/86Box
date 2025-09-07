/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Symphony SL82C461 (Haydn II) chipset.
 *
 * Symphony SL82C461 Configuration Registers (WARNING: May be inaccurate!):
 *
 *     - Register 00h:
 *         - Bit 6: External cache present (if clear, AMI BIOS'es will not
 *                                          allow enabling external cache).
 *
 *     - Register 01h:
 *         - Bit 0: Fast Gate A20 Enable (Handler mostly).
 *                  Is it? Enabling/disabling fast gate A20 doesn't appear
 *                  to do much to any register at all.
 *
 *     - Register 02h:
 *         - Bit 0: Optional Chipset Turbo Pin;
 *         - Bits 4-2:
 *             - 000 = CLK2/3;
 *             - 001 = CLK2/4;
 *             - 010 = CLK2/5;
 *             - 011 = 7.159 MHz (ATCLK2);
 *             - 100 = CLK2/6;
 *             - 110 = CLK2/2.5;
 *             - 111 = CLK2/2.
 *
 *     - Register 06h:
 *         - Bit 2: Decoupled Refresh Option.
 *
 *     - Register 08h:
 *         - Bits 3, 2: I/O Recovery Time (SYSCLK):
 *             - 0, 0 = 0;
 *             - 1, 1 = 12.
 *         - Bit 1: Extended ALE.
 *
 *     - Register 25h:
 *       Bit 7 here causes AMI 111192 CMOS Setup to return 7168 KB RAM
 *       instead of 6912 KB. This is 256 KB off. Relocation?
 *       Also, returning bit 5 clear instead of set, causes the AMI BIOS
 *       to set bits 0,1 of register 45h to 1,0 instead of 0,1.
 *
 *     - Register 2Dh:
 *         - Bit 7: Enable 256KB Memory Relocation;
 *         - Bit 6: Enable 384KB Memory Relocation, bit 7 must also be set.
 *
 *     - Register 2Eh:
 *         - Bit 7: CC000-CFFFF Shadow Read Enable;
 *         - Bit 6: CC000-CFFFF Shadow Write Enable;
 *         - Bit 5: C8000-CBFFF Shadow Read Enable;
 *         - Bit 4: C8000-CBFFF Shadow Write Enable;
 *         - Bit 3: C4000-C7FFF Shadow Read Enable;
 *         - Bit 2: C4000-C7FFF Shadow Write Enable;
 *         - Bit 1: C0000-C3FFF Shadow Read Enable;
 *         - Bit 0: C0000-C3FFF Shadow Write Enable.
 *
 *     - Register 2Fh:
 *         - Bit 7: DC000-DFFFF Shadow Read Enable;
 *         - Bit 6: DC000-DFFFF Shadow Write Enable;
 *         - Bit 5: D8000-DBFFF Shadow Read Enable;
 *         - Bit 4: D8000-DBFFF Shadow Write Enable;
 *         - Bit 3: D4000-D7FFF Shadow Read Enable;
 *         - Bit 2: D4000-D7FFF Shadow Write Enable;
 *         - Bit 1: D0000-D3FFF Shadow Read Enable;
 *         - Bit 0: D0000-D3FFF Shadow Write Enable.
 *
 *     - Register 30h:
 *         - Bit 7: E0000-EFFFF Shadow Read Enable;
 *         - Bit 6: E0000-EFFFF Shadow Write Enable.
 *
 *     - Register 31h:
 *         - Bit 7: F0000-FFFFF Shadow Read Enable;
 *         - Bit 6: F0000-FFFFF Shadow Write Enable.
 *
 *     - Register 33h (NOTE: Waitstates also affect register 32h):
 *         - Bits 3, 0:
 *             - 0,0 = 0 W/S;
 *             - 1,0 = 1 W/S;
 *             - 1,1 = 2 W/S.
 *
 *     - Register 40h:
 *         - Bit 3: External Cache Enabled (0 = yes, 1 = no);
 *                  I also see bits 5, 4, 3 of register 44h affected:
 *                      - 38h (so all 3 set) when cache is disabled;
 *                      - 00h (all 3 clear) when it's enabled.
 *
 *     - Register 45h:
 *         - Bit 3: Video Shadow RAM Cacheable;
 *         - Bit 4: Adapter Shadow RAM Cacheable;
 *         - Bit 5: BIOS Shadow RAM Cacheable.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Tiseno100,
 *
 *          Copyright 2025 Miran Grca.
 *          Copyright 2021-2025 Tiseno100.
 */
#include <math.h>
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
#include <86box/mem.h>
#include <86box/chipset.h>

typedef struct {
    uint8_t index;
    uint8_t regs[256];
    uint8_t shadow[4];
} sl82c461_t;

#ifdef ENABLE_SL82C461_LOG
int sl82c461_do_log = ENABLE_SL82C461_LOG;

static void
sl82c461_log(const char *fmt, ...)
{
    va_list ap;

    if (sl82c461_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define sl82c461_log(fmt, ...)
#endif

static void
sl82c461_recalcmapping(sl82c461_t *dev)
{
    int do_shadow = 0;

    for (uint32_t i = 0; i < 8; i += 2) {
        if ((dev->regs[0x2e] ^ dev->shadow[0x00]) & (3 << i)) {
            uint32_t base = 0x000c0000 + ((i >> 1) << 14);
            uint32_t read  = ((dev->regs[0x2e] >> i) & 0x02) ? MEM_READ_INTERNAL :
                                                              MEM_READ_EXTANY;
            uint32_t write = ((dev->regs[0x2e] >> i) & 0x01) ? MEM_WRITE_INTERNAL :
                                                               MEM_WRITE_EXTANY;

            mem_set_mem_state_both(base, 0x00004000, read | write);

            do_shadow++;
        }

        if ((dev->regs[0x2f] ^ dev->shadow[0x01]) & (3 << i)) {
            uint32_t base = 0x000d0000 + ((i >> 1) << 14);
            uint32_t read  = ((dev->regs[0x2f] >> i) & 0x02) ? MEM_READ_INTERNAL :
                                                              MEM_READ_EXTANY;
            uint32_t write = ((dev->regs[0x2f] >> i) & 0x01) ? MEM_WRITE_INTERNAL :
                                                               MEM_WRITE_EXTANY;

            mem_set_mem_state_both(base, 0x00004000, read | write);

            do_shadow++;
        }
    }

    if ((dev->regs[0x30] ^ dev->shadow[0x02]) & 0xc0) {
        uint32_t base = 0x000e0000;
        uint32_t read  = ((dev->regs[0x30] >> 6) & 0x02) ? MEM_READ_INTERNAL :
                                                           MEM_READ_EXTANY;
        uint32_t write = ((dev->regs[0x30] >> 6) & 0x01) ? MEM_WRITE_INTERNAL :
                                                           MEM_WRITE_EXTANY;

        mem_set_mem_state_both(base, 0x00010000, read | write);

        do_shadow++;
    }

    if ((dev->regs[0x31] ^ dev->shadow[0x03]) & 0xc0) {
        uint32_t base = 0x000f0000;
        uint32_t read  = ((dev->regs[0x31] >> 6) & 0x02) ? MEM_READ_INTERNAL :
                                                           MEM_READ_EXTANY;
        uint32_t write = ((dev->regs[0x31] >> 6) & 0x01) ? MEM_WRITE_INTERNAL :
                                                           MEM_WRITE_EXTANY;

        shadowbios = !!((dev->regs[0x31] >> 6) & 0x02);
        shadowbios_write = !!((dev->regs[0x31] >> 6) & 0x01);

        mem_set_mem_state_both(base, 0x00010000, read | write);

        do_shadow++;
    }

    if (do_shadow) {
        memcpy(dev->shadow, &(dev->regs[0x2e]), 4 * sizeof(uint8_t));
        flushmmucache_nopc();
    }
}

static void
sl82c461_write(uint16_t addr, uint8_t val, void *priv)
{
    sl82c461_t *dev = (sl82c461_t *) priv;

    sl82c461_log("[%04X:%08X] [W] %04X = %02X\n", CS, cpu_state.pc, addr, val);

    if (addr & 0x0001) {
        dev->regs[dev->index] = val;

        switch (dev->index) {
            case 0x01:
                /* NOTE: This is to be verified. */
                mem_a20_alt = val & 1;
                mem_a20_recalc();
                break;
            case 0x02: {
                double bus_clk;
                switch (val & 0x1c) {
                    case 0x00:
                         bus_clk = cpu_busspeed / 3.0;
                         break;
                    case 0x04:
                         bus_clk = cpu_busspeed / 4.0;
                         break;
                    case 0x08:
                         bus_clk = cpu_busspeed / 5.0;
                         break;
                    default:
                    case 0x0c:
                         bus_clk = 7159091.0;
                         break;
                    case 0x10:
                         bus_clk = cpu_busspeed / 6.0;
                         break;
                    case 0x18:
                         bus_clk = cpu_busspeed / 2.5;
                         break;
                    case 0x1c:
                         bus_clk = cpu_busspeed / 2.0;
                         break;
                }
                cpu_set_isa_speed((int) round(bus_clk));
                break;
            } case 0x2d:
                switch (val & 0xc0) {
                    case 0xc0:
                        mem_remap_top(384);
                        break;
                    case 0x80:
                        mem_remap_top(256);
                        break;
                    default:
                    case 0x00:
                        mem_remap_top(0);
                        break;
                }
                break;
            case 0x2e ... 0x31:
                sl82c461_recalcmapping(dev);
                break;
            case 0x33:
                switch (val & 0x09) {
                    default:
                    case 0x00:
                        cpu_waitstates = 0;
                        break;
                    case 0x08:
                        cpu_waitstates = 1;
                        break;
                    case 0x09:
                        cpu_waitstates = 2;
                        break;
                }
                cpu_update_waitstates();
                break;
            case 0x40:
                cpu_cache_ext_enabled = !(val & 0x08);
                cpu_update_waitstates();
                break;
        }
    } else
        dev->index = val;
}

static uint8_t
sl82c461_read(uint16_t addr, void *priv)
{
    sl82c461_t *dev = (sl82c461_t *) priv;
    uint8_t     ret = 0x00;

    if (addr & 0x0001)
        if (dev->index == 0x00)
            ret = dev->regs[dev->index] | 0x40;
        else
            ret = dev->regs[dev->index];
    else
        ret = dev->index;

    sl82c461_log("[%04X:%08X] [R] %04X = %02X\n", CS, cpu_state.pc, addr, ret);

    return ret;
}

static void
sl82c461_close(void *priv)
{
    sl82c461_t *dev = (sl82c461_t *) priv;

    free(dev);
}

static void *
sl82c461_init(const device_t *info)
{
    sl82c461_t *dev = (sl82c461_t *) calloc(1, sizeof(sl82c461_t));

    dev->regs[0x00] = 0x40;

    dev->regs[0x02] = 0x0c;
    dev->regs[0x40] = 0x08;

    memset(dev->shadow, 0xff, 4 * sizeof(uint8_t));

    mem_a20_alt = 0x00;
    mem_a20_recalc();

    cpu_set_isa_speed(7159091.0);

    sl82c461_recalcmapping(dev);

    cpu_waitstates = 0;
    cpu_cache_ext_enabled = 0;

    cpu_update_waitstates();

    io_sethandler(0x00a8, 2,
                  sl82c461_read, NULL, NULL,
                  sl82c461_write, NULL, NULL, dev);

    return dev;
}

const device_t sl82c461_device = {
    .name          = "Symphony SL82C461 (Haydn II)",
    .internal_name = "sis_85c471",
    .flags         = 0,
    .local         = 0,
    .init          = sl82c461_init,
    .close         = sl82c461_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
