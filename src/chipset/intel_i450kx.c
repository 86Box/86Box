/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Intel 450KX Mars and 450GX Orion
 *          chipsets: the 82454KX/GX PCI bridge (PB) and the 82453KX/GX
 *          memory controller (MC, the GX's DC). Register defaults are the
 *          data sheet's (Intel 290523-001) as corrected by the
 *          specification update (Intel 243109-015, Documentation
 *          Changes 1).
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Tiseno100,
 *
 *          Copyright 2021-2024 Miran Grca.
 *          Copyright 2021 Tiseno100.
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
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/plat_unused.h>
#include <86box/smram.h>
#include <86box/spd.h>
#include <86box/chipset.h>

#ifdef ENABLE_450KX_LOG
int i450kx_do_log = ENABLE_450KX_LOG;

static void
i450kx_log(const char *fmt, ...)
{
    va_list ap;

    if (i450kx_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define i450kx_log(fmt, ...)
#endif

/* Which chip set. */
#define I450_KX 0
#define I450_GX 1

/* TODO: Finish the bus index stuff. */
typedef struct i450kx_t {
    smram_t *smram[2];

    uint8_t bus_index;
    uint8_t pb_slot;
    uint8_t mc_slot;
    uint8_t gx;

    uint8_t pb_pci_conf[256];
    uint8_t mc_pci_conf[256];

    uint8_t mem_state[2][256];
} i450kx_t;

#ifdef ENABLE_450KX_LOG
static uint32_t
i450kx_apicr_base(const uint8_t *regs)
{
    return ((uint32_t) regs[0xa6] << 24) |
           ((uint32_t) regs[0xa5] << 16) |
           ((uint32_t) (regs[0xa4] & 0xfe) << 8);
}

static void
i450kx_apicr_log(const char *unit, const uint8_t *regs)
{
    i450kx_log("i450KX-%s: APICR enable=%u base=%08x raw=%02x %02x %02x %02x\n",
               unit, regs[0xa4] & 0x01, i450kx_apicr_base(regs),
               regs[0xa4], regs[0xa5], regs[0xa6], regs[0xa7]);
}
#else
#    define i450kx_apicr_log(unit, regs)
#endif

static void
i450kx_map(i450kx_t *dev, int bus, uint32_t addr, uint32_t size, int state)
{
    uint32_t base      = addr >> 12;
    int      states[4] = { MEM_READ_EXTANY | MEM_WRITE_EXTANY, MEM_READ_INTERNAL | MEM_WRITE_EXTANY,
                           MEM_READ_EXTANY | MEM_WRITE_INTERNAL, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL };

    state &= 3;
    if (dev->mem_state[bus][base] != state) {
        if (bus)
            mem_set_mem_state_bus_both(addr, size, states[state]);
        else
            mem_set_mem_state_cpu_both(addr, size, states[state]);
        dev->mem_state[bus][base] = state;
        flushmmucache_nopc();
    }
}

static void
i450kx_smram_recalc(i450kx_t *dev, int bus)
{
    const uint8_t *regs = bus ? dev->pb_pci_conf : dev->mc_pci_conf;
    uint32_t       addr;
    uint32_t       size;
    int            enable = bus ? !(regs[0x57] & 0x08) : (regs[0x57] & 0x08);

    smram_disable(dev->smram[bus]);

    addr = ((uint32_t) regs[0xb8] << 16) | ((uint32_t) regs[0xb9] << 24);
    size = (((uint32_t) ((regs[0xbb] >> 4) & 0x0f)) << 16) + 0x00010000;

    if ((addr != 0x00000000) && enable) {
        if (bus)
            smram_enable_ex(dev->smram[bus], addr, addr, size, 0, 0, 0, enable);
        else
            smram_enable_ex(dev->smram[bus], addr, addr, size, 0, 0, enable, 0);
    }

    flushmmucache();
}

static void
i450kx_vid_buf_recalc(i450kx_t *dev, int bus)
{
    const uint8_t *regs = bus ? dev->pb_pci_conf : dev->mc_pci_conf;

    int state = (regs[0x58] & 0x02) ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) :
                                      (MEM_READ_EXTANY | MEM_WRITE_EXTANY);

    if (bus)
        mem_set_mem_state_bus_both(0x000a0000, 0x00020000, state);
    else
        mem_set_mem_state_cpu_both(0x000a0000, 0x00020000, state);

    flushmmucache_nopc();
}

static void
pb_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    i450kx_t *dev = (i450kx_t *) priv;
    const int gx  = (dev->gx != I450_KX);

    if (func == 0) {
        i450kx_log("[%04X:%08X] i450KX-PB: [W] dev->pb_pci_conf[%02X] = %02X\n", CS, cpu_state.pc,
                   addr, val);

        switch (addr) {
            case 0x04: /* PCICMD: bus master (2) hardwired to 1 */
                dev->pb_pci_conf[addr] = (dev->pb_pci_conf[addr] & 0x04) | (val & 0x53);
                break;
            case 0x05:
                dev->pb_pci_conf[addr] = val & 0x01;
                break;

            case 0x07: /* PCISTS: R/WC */
                dev->pb_pci_conf[addr] &= ~(val & 0xf9);
                break;

            case 0x0d:
                dev->pb_pci_conf[addr] = val;
                break;

            /* BIST (0Fh): "Writes to this register have no effect". */

            case 0x40:
            case 0x41:
                dev->pb_pci_conf[addr] = val;
                break;
            case 0x43:
                dev->pb_pci_conf[addr] = val & 0x80;
                break;

            case 0x48:
                dev->pb_pci_conf[addr] = val & 0x06;
                break;

            case 0x4a:
            case 0x4b:
                dev->pb_pci_conf[addr] = val;
#if 0
                if (addr == 0x4a)
                    pci_remap_bus(dev->bus_index, val);
#endif
                break;

            case 0x4c:
                /* PBC. Bits 1:0 are the 82454GX's arbitration mode, and "must
                   be set to 01" on the 82454KX. */
                if (gx)
                    dev->pb_pci_conf[addr] = val & 0xdb;
                else
                    dev->pb_pci_conf[addr] = (dev->pb_pci_conf[addr] & 0x01) | (val & 0xd8);
                break;

            case 0x51:
                dev->pb_pci_conf[addr] = val;
                break;

            case 0x53:
                dev->pb_pci_conf[addr] = val & 0x02;
                break;

            case 0x54:
                dev->pb_pci_conf[addr] = val & 0x7b;
                break;
            case 0x55:
                dev->pb_pci_conf[addr] = val & 0x03;
                break;

            case 0x57:
                dev->pb_pci_conf[addr] = val & 0x08;
                i450kx_smram_recalc(dev, 1);
                break;

            case 0x58:
                dev->pb_pci_conf[addr] = val & 0x02;
                i450kx_vid_buf_recalc(dev, 1);
                break;

            case 0x59: /* PAM0 */
                if ((dev->pb_pci_conf[0x59] ^ val) & 0x0f)
                    i450kx_map(dev, 1, 0x80000, 0x20000, val & 0x0f);
                if ((dev->pb_pci_conf[0x59] ^ val) & 0xf0) {
                    i450kx_map(dev, 1, 0xf0000, 0x10000, val >> 4);
                    shadowbios = (val & 0x10);
                }
                dev->pb_pci_conf[0x59] = val & 0x33;
                break;
            case 0x5a: /* PAM1 */
                if ((dev->pb_pci_conf[0x5a] ^ val) & 0x0f)
                    i450kx_map(dev, 1, 0xc0000, 0x04000, val & 0xf);
                if ((dev->pb_pci_conf[0x5a] ^ val) & 0xf0)
                    i450kx_map(dev, 1, 0xc4000, 0x04000, val >> 4);
                dev->pb_pci_conf[0x5a] = val & 0x33;
                break;
            case 0x5b: /*PAM2 */
                if ((dev->pb_pci_conf[0x5b] ^ val) & 0x0f)
                    i450kx_map(dev, 1, 0xc8000, 0x04000, val & 0xf);
                if ((dev->pb_pci_conf[0x5b] ^ val) & 0xf0)
                    i450kx_map(dev, 1, 0xcc000, 0x04000, val >> 4);
                dev->pb_pci_conf[0x5b] = val & 0x33;
                break;
            case 0x5c: /*PAM3 */
                if ((dev->pb_pci_conf[0x5c] ^ val) & 0x0f)
                    i450kx_map(dev, 1, 0xd0000, 0x04000, val & 0xf);
                if ((dev->pb_pci_conf[0x5c] ^ val) & 0xf0)
                    i450kx_map(dev, 1, 0xd4000, 0x04000, val >> 4);
                dev->pb_pci_conf[0x5c] = val & 0x33;
                break;
            case 0x5d: /* PAM4 */
                if ((dev->pb_pci_conf[0x5d] ^ val) & 0x0f)
                    i450kx_map(dev, 1, 0xd8000, 0x04000, val & 0xf);
                if ((dev->pb_pci_conf[0x5d] ^ val) & 0xf0)
                    i450kx_map(dev, 1, 0xdc000, 0x04000, val >> 4);
                dev->pb_pci_conf[0x5d] = val & 0x33;
                break;
            case 0x5e: /* PAM5 */
                if ((dev->pb_pci_conf[0x5e] ^ val) & 0x0f)
                    i450kx_map(dev, 1, 0xe0000, 0x04000, val & 0xf);
                if ((dev->pb_pci_conf[0x5e] ^ val) & 0xf0)
                    i450kx_map(dev, 1, 0xe4000, 0x04000, val >> 4);
                dev->pb_pci_conf[0x5e] = val & 0x33;
                break;
            case 0x5f: /* PAM6 */
                if ((dev->pb_pci_conf[0x5f] ^ val) & 0x0f)
                    i450kx_map(dev, 1, 0xe8000, 0x04000, val & 0xf);
                if ((dev->pb_pci_conf[0x5f] ^ val) & 0xf0)
                    i450kx_map(dev, 1, 0xec000, 0x04000, val >> 4);
                dev->pb_pci_conf[0x5f] = val & 0x33;
                break;

            case 0x70:
                dev->pb_pci_conf[addr] = val & 0xf8;
                break;

            case 0x71: /* ERRSTS: R/WC */
                dev->pb_pci_conf[addr] &= ~(val & 0x71);
                break;

            case 0x78:
                dev->pb_pci_conf[addr] = val & 0xf0;
                break;
            case 0x79:
                dev->pb_pci_conf[addr] = val & 0xfc;
                break;
            case 0x7a:
                dev->pb_pci_conf[addr] = val;
                break;
            case 0x7b:
                dev->pb_pci_conf[addr] = val & 0x0f;
                break;

            case 0x7c:
                dev->pb_pci_conf[addr] = val & 0x9f;
                break;
            case 0x7d:
                dev->pb_pci_conf[addr] = val & 0x1a;
                break;
            case 0x7e:
                dev->pb_pci_conf[addr] = val & 0xf0;
                break;
            case 0x7f:
                dev->pb_pci_conf[addr] = val;
                break;

            case 0x88:
            case 0x89:
                dev->pb_pci_conf[addr] = val;
                break;
            case 0x8b:
                dev->pb_pci_conf[addr] = val & 0x80;
                break;
            case 0x8c:
            case 0x8d:
                dev->pb_pci_conf[addr] = val;
                break;

            /* IOSR1 (98h-9Bh) and IOSR2 (A0h-A3h), the 82454GX's: the start
               address in bits 15:4, the end in bits 31:20, the enable in bit
               0. Reserved on the 82454KX. */
            case 0x98:
            case 0xa0:
                if (gx)
                    dev->pb_pci_conf[addr] = val & 0xf1;
                break;
            case 0x99:
            case 0xa1:
            case 0x9b:
            case 0xa3:
                if (gx)
                    dev->pb_pci_conf[addr] = val;
                break;
            case 0x9a:
            case 0xa2:
                if (gx)
                    dev->pb_pci_conf[addr] = val & 0xf0;
                break;

            case 0x9c:
                dev->pb_pci_conf[addr] = val & 0x01;
                break;

            /* APICR: the enable in bit 0, the highest unit ID in bits 7:4,
               the starting unit ID in bits 11:8, the base in bits 27:12. */
            case 0xa4:
                dev->pb_pci_conf[addr] = val & 0xf1;
                i450kx_apicr_log("PB", dev->pb_pci_conf);
                break;
            case 0xa5:
            case 0xa6:
                dev->pb_pci_conf[addr] = val;
                i450kx_apicr_log("PB", dev->pb_pci_conf);
                break;
            case 0xa7:
                dev->pb_pci_conf[addr] = val & 0x0f;
                i450kx_apicr_log("PB", dev->pb_pci_conf);
                break;

            case 0xb0:
                dev->pb_pci_conf[addr] = val & 0xe0;
                break;
            case 0xb1: /* CONFVR: bits 10 and 8 the 82454GX's */
                dev->pb_pci_conf[addr] = val & (gx ? 0x1f : 0x1a);
                break;

            case 0xb8:
            case 0xb9:
                dev->pb_pci_conf[addr] = val;
                i450kx_smram_recalc(dev, 1);
                break;
            case 0xbb:
                dev->pb_pci_conf[addr] = val & 0xf0;
                i450kx_smram_recalc(dev, 1);
                break;

            case 0xbc:
                dev->pb_pci_conf[addr] = val & 0x11;
                break;

            /* EXERRCMD: bits 1 and 5 reserved; 11:10 the 82454GX's. */
            case 0xc0:
                dev->pb_pci_conf[addr] = val & 0xdd;
                break;
            case 0xc1:
                dev->pb_pci_conf[addr] = val & (gx ? 0x3f : 0x33);
                break;

            /* EXERRSTS: R/WC; bits 11 and 17 the 82454GX's. */
            case 0xc4:
                dev->pb_pci_conf[addr] &= ~(val & 0x0d);
                break;
            case 0xc5:
                dev->pb_pci_conf[addr] &= ~(val & (gx ? 0x0a : 0x02));
                break;
            case 0xc6:
                dev->pb_pci_conf[addr] &= ~(val & (gx ? 0x1f : 0x1d));
                break;

            case 0xc8:
                dev->pb_pci_conf[addr] = val & 0x1f;
                break;

            case 0xca:
            case 0xcb:
                dev->pb_pci_conf[addr] = val;
                break;

            default:
                break;
        }
    }
}

static uint8_t
pb_read(int func, int addr, UNUSED(int len), void *priv)
{
    const i450kx_t *dev = (i450kx_t *) priv;
    uint8_t   ret = 0xff;

    if (func == 0) {
        ret = dev->pb_pci_conf[addr];

        i450kx_log("[%04X:%08X] i450KX-PB: [R] dev->pb_pci_conf[%02X] = %02X\n", CS, cpu_state.pc,
                   addr, ret);
    }

    return ret;
}

/* The DRAM row limits (DRL, 16 bits each at 60h-6Fh, in 4 Mbyte units)
   from the memory fitted: 7 bits on the 450KX, 11 on the 450GX (2.3.13). */
static void
mc_fill_drbs(i450kx_t *dev)
{
    const int gx = (dev->gx != I450_KX);

    spd_write_drbs_interleaved(dev->mc_pci_conf, 0x60, 0x6f, 4);
    for (uint8_t i = 0x60; i <= 0x6f; i++) {
        if (i & 0x01)
            dev->mc_pci_conf[i] &= gx ? 0x07 : 0x00;
        else if (!gx)
            dev->mc_pci_conf[i] &= 0x7f;
    }
}

static void
mc_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    i450kx_t *dev = (i450kx_t *) priv;

    if (func == 0) {
        i450kx_log("[%04X:%08X] i450KX-MC: [W] dev->mc_pci_conf[%02X] = %02X\n", CS, cpu_state.pc,
                   addr, val);

        switch (addr) {
            /* BASEADD, the 450GX's: the MC's base in 1 Mbyte units. */
            case 0x40:
            case 0x41:
                if (dev->gx != I450_KX)
                    dev->mc_pci_conf[addr] = val;
                break;

            case 0x4c:
                dev->mc_pci_conf[addr] = val & 0xdf;
                break;
            case 0x4d:
                /* CMD bits 14:13 are the 450GX's interleaves 3 and 2. */
                dev->mc_pci_conf[addr] = val & ((dev->gx != I450_KX) ? 0xff : 0x9f);
                break;

            case 0x57:
                dev->mc_pci_conf[addr] = val & 0x08;
                i450kx_smram_recalc(dev, 0);
                break;

            case 0x58:
                dev->mc_pci_conf[addr] = val & 0x02;
                i450kx_vid_buf_recalc(dev, 0);
                break;

            case 0x59: /* PAM0 */
                if ((dev->mc_pci_conf[0x59] ^ val) & 0x0f)
                    i450kx_map(dev, 0, 0x80000, 0x20000, val & 0x0f);
                if ((dev->mc_pci_conf[0x59] ^ val) & 0xf0) {
                    i450kx_map(dev, 0, 0xf0000, 0x10000, val >> 4);
                    shadowbios = (val & 0x10);
                }
                dev->mc_pci_conf[0x59] = val & 0x33;
                break;
            case 0x5a: /* PAM1 */
                if ((dev->mc_pci_conf[0x5a] ^ val) & 0x0f)
                    i450kx_map(dev, 0, 0xc0000, 0x04000, val & 0xf);
                if ((dev->mc_pci_conf[0x5a] ^ val) & 0xf0)
                    i450kx_map(dev, 0, 0xc4000, 0x04000, val >> 4);
                dev->mc_pci_conf[0x5a] = val & 0x33;
                break;
            case 0x5b: /*PAM2 */
                if ((dev->mc_pci_conf[0x5b] ^ val) & 0x0f)
                    i450kx_map(dev, 0, 0xc8000, 0x04000, val & 0xf);
                if ((dev->mc_pci_conf[0x5b] ^ val) & 0xf0)
                    i450kx_map(dev, 0, 0xcc000, 0x04000, val >> 4);
                dev->mc_pci_conf[0x5b] = val & 0x33;
                break;
            case 0x5c: /*PAM3 */
                if ((dev->mc_pci_conf[0x5c] ^ val) & 0x0f)
                    i450kx_map(dev, 0, 0xd0000, 0x04000, val & 0xf);
                if ((dev->mc_pci_conf[0x5c] ^ val) & 0xf0)
                    i450kx_map(dev, 0, 0xd4000, 0x04000, val >> 4);
                dev->mc_pci_conf[0x5c] = val & 0x33;
                break;
            case 0x5d: /* PAM4 */
                if ((dev->mc_pci_conf[0x5d] ^ val) & 0x0f)
                    i450kx_map(dev, 0, 0xd8000, 0x04000, val & 0xf);
                if ((dev->mc_pci_conf[0x5d] ^ val) & 0xf0)
                    i450kx_map(dev, 0, 0xdc000, 0x04000, val >> 4);
                dev->mc_pci_conf[0x5d] = val & 0x33;
                break;
            case 0x5e: /* PAM5 */
                if ((dev->mc_pci_conf[0x5e] ^ val) & 0x0f)
                    i450kx_map(dev, 0, 0xe0000, 0x04000, val & 0xf);
                if ((dev->mc_pci_conf[0x5e] ^ val) & 0xf0)
                    i450kx_map(dev, 0, 0xe4000, 0x04000, val >> 4);
                dev->mc_pci_conf[0x5e] = val & 0x33;
                break;
            case 0x5f: /* PAM6 */
                if ((dev->mc_pci_conf[0x5f] ^ val) & 0x0f)
                    i450kx_map(dev, 0, 0xe8000, 0x04000, val & 0xf);
                if ((dev->mc_pci_conf[0x5f] ^ val) & 0xf0)
                    i450kx_map(dev, 0, 0xec000, 0x04000, val >> 4);
                dev->mc_pci_conf[0x5f] = val & 0x33;
                break;

            case 0x60 ... 0x6f:
                if (dev->gx != I450_KX)
                    dev->mc_pci_conf[addr] = (addr & 0x01) ? (val & 0x07) : val;
                else
                    dev->mc_pci_conf[addr] = (addr & 0x01) ? 0x00 : (val & 0x7f);
                mc_fill_drbs(dev);
                break;

            case 0x74 ... 0x77:
                dev->mc_pci_conf[addr] = val;
                break;

            case 0x78:
                dev->mc_pci_conf[addr] = val & 0xf0;
                break;
            case 0x79:
                dev->mc_pci_conf[addr] = val & 0xfe;
                break;
            case 0x7a:
                dev->mc_pci_conf[addr] = val;
                break;
            case 0x7b:
                dev->mc_pci_conf[addr] = val & 0x0f;
                break;

            case 0x7c:
                dev->mc_pci_conf[addr] = val & 0x1f;
                break;
            case 0x7d:
                dev->mc_pci_conf[addr] = val & 0x0c;
                break;
            case 0x7e:
                dev->mc_pci_conf[addr] = val & 0xf0;
                break;
            case 0x7f:
                dev->mc_pci_conf[addr] = val;
                break;

            case 0x88:
            case 0x89:
                dev->mc_pci_conf[addr] = val;
                break;
            case 0x8b:
                /* HMGSA: bit 31 the enable, bit 30 Reclaim Enable (specification
                   update, Documentation Changes 1). */
                dev->mc_pci_conf[addr] = val & 0xc0;
                break;

            case 0x8c:
            case 0x8d:
                dev->mc_pci_conf[addr] = val;
                break;

            case 0xa4:
                dev->mc_pci_conf[addr] = val & 0x01;
                i450kx_apicr_log("MC", dev->mc_pci_conf);
                break;
            case 0xa5:
                dev->mc_pci_conf[addr] = val & 0xf0;
                i450kx_apicr_log("MC", dev->mc_pci_conf);
                break;
            case 0xa6:
                dev->mc_pci_conf[addr] = val;
                i450kx_apicr_log("MC", dev->mc_pci_conf);
                break;
            case 0xa7:
                dev->mc_pci_conf[addr] = val & 0x0f;
                i450kx_apicr_log("MC", dev->mc_pci_conf);
                break;

            /* UERRADD (A8h-ABh) is read-only (2.3.21). */

            case 0xac ... 0xae:
                dev->mc_pci_conf[addr] = val;
                break;
            case 0xaf:
                dev->mc_pci_conf[addr] = val & 0x7f;
                break;

            case 0xb8:
            case 0xb9:
                dev->mc_pci_conf[addr] = val;
                i450kx_smram_recalc(dev, 0);
                break;
            case 0xbb:
                dev->mc_pci_conf[addr] = val & 0xf0;
                i450kx_smram_recalc(dev, 0);
                break;

            case 0xbc:
                dev->mc_pci_conf[addr] = val & 0x01;
                break;

            case 0xc0:
                dev->mc_pci_conf[addr] = val & 0x07;
                break;

            case 0xc2:
                dev->mc_pci_conf[addr] &= ~(val & 0x03);
                break;

            case 0xc4:
                dev->mc_pci_conf[addr] = val & 0xbf;
                break;
            case 0xc5: /* SERRCMD bits 9:8, the 450GX's */
                dev->mc_pci_conf[addr] = val & ((dev->gx != I450_KX) ? 0x03 : 0x00);
                break;

            case 0xc6: /* SERRSTS: bits 2:1 the 450GX's */
                dev->mc_pci_conf[addr] &= ~(val & ((dev->gx != I450_KX) ? 0x1f : 0x19));
                break;

            case 0xc8:
                dev->mc_pci_conf[addr] = val & 0x1f;
                break;
            case 0xca:
            case 0xcb:
                dev->mc_pci_conf[addr] = val;
                break;

            default:
                break;
        }
    }
}

static uint8_t
mc_read(int func, int addr, UNUSED(int len), void *priv)
{
    const i450kx_t *dev = (i450kx_t *) priv;
    uint8_t   ret = 0xff;

    if (func == 0) {
        ret = dev->mc_pci_conf[addr];

        i450kx_log("[%04X:%08X] i450KX-MC: [R] dev->mc_pci_conf[%02X] = %02X\n", CS, cpu_state.pc,
                   addr, ret);
    }

    return ret;
}

static void
i450kx_reset(void *priv)
{
    i450kx_t *dev = (i450kx_t *) priv;
    uint32_t  i;

    /* Defaults PB */
    dev->pb_pci_conf[0x00] = 0x86;
    dev->pb_pci_conf[0x01] = 0x80;
    dev->pb_pci_conf[0x02] = 0xc4;
    dev->pb_pci_conf[0x03] = 0x84;
    dev->pb_pci_conf[0x04] = 0x07;
    dev->pb_pci_conf[0x05] = 0x00;
    dev->pb_pci_conf[0x06] = 0x40;
    dev->pb_pci_conf[0x07] = 0x02;
    dev->pb_pci_conf[0x08] = 0x02;
    dev->pb_pci_conf[0x09] = 0x00;
    dev->pb_pci_conf[0x0a] = 0x00;
    dev->pb_pci_conf[0x0b] = 0x06;
    dev->pb_pci_conf[0x0c] = 0x08;
    dev->pb_pci_conf[0x0d] = 0x20;
    dev->pb_pci_conf[0x0e] = 0x00;
    dev->pb_pci_conf[0x0f] = 0x00;
    dev->pb_pci_conf[0x40] = 0x00;
    dev->pb_pci_conf[0x41] = 0x00;
    dev->pb_pci_conf[0x42] = 0x00;
    dev->pb_pci_conf[0x43] = 0x00;
    dev->pb_pci_conf[0x48] = 0x06;
    dev->pb_pci_conf[0x49] = 0x19;
    dev->pb_pci_conf[0x4a] = 0x00;
    dev->pb_pci_conf[0x4b] = 0x00;
    dev->pb_pci_conf[0x4c] = 0x19;
    dev->pb_pci_conf[0x51] = 0x80;
    dev->pb_pci_conf[0x53] = 0x00;
    dev->pb_pci_conf[0x54] = 0x00;
    dev->pb_pci_conf[0x55] = 0x00;
    dev->pb_pci_conf[0x57] = 0x00;
    dev->pb_pci_conf[0x58] = 0x02;
    dev->pb_pci_conf[0x70] = 0x00;
    dev->pb_pci_conf[0x71] = 0x00;
    dev->pb_pci_conf[0x78] = 0x00;
    dev->pb_pci_conf[0x79] = 0x00;
    dev->pb_pci_conf[0x7a] = 0x00;
    dev->pb_pci_conf[0x7b] = 0x00;
    dev->pb_pci_conf[0x7c] = 0x00;
    dev->pb_pci_conf[0x7d] = 0x00;
    dev->pb_pci_conf[0x7e] = 0x00;
    dev->pb_pci_conf[0x7f] = 0x00;
    dev->pb_pci_conf[0x88] = 0x00;
    dev->pb_pci_conf[0x89] = 0x00;
    dev->pb_pci_conf[0x8a] = 0x00;
    dev->pb_pci_conf[0x8b] = 0x00;
    dev->pb_pci_conf[0x8c] = 0x00;
    dev->pb_pci_conf[0x8d] = 0x00;
    dev->pb_pci_conf[0x8e] = 0x00;
    dev->pb_pci_conf[0x8f] = 0x00;
    if (dev->gx != I450_KX) {
        /* IOSR1 and IOSR2: FFF0 0001h, the compatibility PB's. */
        dev->pb_pci_conf[0x98] = dev->pb_pci_conf[0xa0] = 0x01;
        dev->pb_pci_conf[0x99] = dev->pb_pci_conf[0xa1] = 0x00;
        dev->pb_pci_conf[0x9a] = dev->pb_pci_conf[0xa2] = 0xf0;
        dev->pb_pci_conf[0x9b] = dev->pb_pci_conf[0xa3] = 0xff;
    }
    dev->pb_pci_conf[0x9c] = 0x00;
    dev->pb_pci_conf[0xa4] = 0x01;
    dev->pb_pci_conf[0xa5] = 0xc0;
    dev->pb_pci_conf[0xa6] = 0xfe;
    dev->pb_pci_conf[0xa7] = 0x00;
    /* Note: Do NOT reset these two registers on programmed (TRC) hard reset! */
#if 0
    dev->pb_pci_conf[0xb0] = 0x00;
    dev->pb_pci_conf[0xb1] = 0x00;
#endif
    /* CSCONFV captures the values the PB drives on A[12:5]# out of CONFVR at every
       rising edge of RESET#. */
    dev->pb_pci_conf[0xb4] = dev->pb_pci_conf[0xb0];
    dev->pb_pci_conf[0xb5] = dev->pb_pci_conf[0xb1];
    dev->pb_pci_conf[0xb8] = 0x05;
    dev->pb_pci_conf[0xb9] = 0x00;
    dev->pb_pci_conf[0xba] = 0x00;
    dev->pb_pci_conf[0xbb] = 0x00;
    dev->pb_pci_conf[0xbc] = 0x01;
    dev->pb_pci_conf[0xc0] = 0x10; /* EXERRCMD: 0000 0010h */
    dev->pb_pci_conf[0xc1] = 0x00;
    dev->pb_pci_conf[0xc2] = 0x00;
    dev->pb_pci_conf[0xc3] = 0x00;
    dev->pb_pci_conf[0xc4] = 0x00;
    dev->pb_pci_conf[0xc5] = 0x00;
    dev->pb_pci_conf[0xc6] = 0x00;
    dev->pb_pci_conf[0xc7] = 0x00;
    dev->pb_pci_conf[0xc8] = 0x03;
    dev->pb_pci_conf[0xc9] = 0x00;
    dev->pb_pci_conf[0xca] = 0x00;
    dev->pb_pci_conf[0xcb] = 0x00;

#if 0
    pci_remap_bus(dev->bus_index, 0x00);
#endif
    i450kx_smram_recalc(dev, 1);
    i450kx_vid_buf_recalc(dev, 1);
    pb_write(0, 0x59, 1, 0x30, dev);
    for (i = 0x5a; i <= 0x5f; i++)
        pb_write(0, i, 1, 0x33, dev);

    /* Defaults MC */
    dev->mc_pci_conf[0x00] = 0x86;
    dev->mc_pci_conf[0x01] = 0x80;
    dev->mc_pci_conf[0x02] = 0xc5;
    dev->mc_pci_conf[0x03] = 0x84;
    dev->mc_pci_conf[0x04] = 0x00;
    dev->mc_pci_conf[0x05] = 0x00;
    dev->mc_pci_conf[0x06] = 0x80;
    dev->mc_pci_conf[0x07] = 0x00;
    dev->mc_pci_conf[0x08] = 0x04;
    dev->mc_pci_conf[0x09] = 0x00;
    dev->mc_pci_conf[0x0a] = 0x00;
    dev->mc_pci_conf[0x0b] = 0x05;
    dev->mc_pci_conf[0x40] = 0x00; /* BASEADD (450GX): MC #0 at 0 */
    dev->mc_pci_conf[0x41] = 0x00;
    dev->mc_pci_conf[0x42] = 0x00;
    dev->mc_pci_conf[0x43] = 0x00;
    dev->mc_pci_conf[0x49] = 0x14;
    dev->mc_pci_conf[0x4c] = 0x0b;
    dev->mc_pci_conf[0x4d] = 0x08;
    dev->mc_pci_conf[0x4e] = 0x00;
    dev->mc_pci_conf[0x4f] = 0x00;
    dev->mc_pci_conf[0x57] = 0x00;
    dev->mc_pci_conf[0x58] = 0x00;
    dev->mc_pci_conf[0x74] = 0x00;
    dev->mc_pci_conf[0x75] = 0x00;
    dev->mc_pci_conf[0x76] = 0x00;
    dev->mc_pci_conf[0x77] = 0x00;
    dev->mc_pci_conf[0x78] = 0x10;
    dev->mc_pci_conf[0x79] = 0x00;
    dev->mc_pci_conf[0x7a] = 0x00;
    dev->mc_pci_conf[0x7b] = 0x00;
    dev->mc_pci_conf[0x7c] = 0x00;
    dev->mc_pci_conf[0x7d] = 0x00;
    dev->mc_pci_conf[0x7e] = 0x10;
    dev->mc_pci_conf[0x7f] = 0x00;
    dev->mc_pci_conf[0x88] = 0x00;
    dev->mc_pci_conf[0x89] = 0x00;
    dev->mc_pci_conf[0x8a] = 0x00;
    dev->mc_pci_conf[0x8b] = 0x00;
    dev->mc_pci_conf[0x8c] = 0x00;
    dev->mc_pci_conf[0x8d] = 0x00;
    dev->mc_pci_conf[0x8e] = 0x00;
    dev->mc_pci_conf[0x8f] = 0x00;
    dev->mc_pci_conf[0xa4] = 0x01;
    dev->mc_pci_conf[0xa5] = 0xc0;
    dev->mc_pci_conf[0xa6] = 0xfe;
    dev->mc_pci_conf[0xa7] = 0x00;
    dev->mc_pci_conf[0xa8] = 0x00;
    dev->mc_pci_conf[0xa9] = 0x00;
    dev->mc_pci_conf[0xaa] = 0x00;
    dev->mc_pci_conf[0xab] = 0x00;
    dev->mc_pci_conf[0xac] = 0x16;
    dev->mc_pci_conf[0xad] = 0x35;
    dev->mc_pci_conf[0xae] = 0xdf;
    dev->mc_pci_conf[0xaf] = 0x30;
    dev->mc_pci_conf[0xb8] = 0x0a;
    dev->mc_pci_conf[0xb9] = 0x00;
    dev->mc_pci_conf[0xba] = 0x00;
    dev->mc_pci_conf[0xbb] = 0x00;
    dev->mc_pci_conf[0xbc] = 0x01;
    dev->mc_pci_conf[0xc0] = 0x00;
    dev->mc_pci_conf[0xc1] = 0x00;
    dev->mc_pci_conf[0xc2] = 0x00;
    dev->mc_pci_conf[0xc3] = 0x00;
    dev->mc_pci_conf[0xc4] = 0x00;
    dev->mc_pci_conf[0xc5] = 0x00;
    dev->mc_pci_conf[0xc6] = 0x00;
    dev->mc_pci_conf[0xc7] = 0x00;

    i450kx_smram_recalc(dev, 0);
    i450kx_vid_buf_recalc(dev, 0);
    mc_write(0, 0x59, 1, 0x03, dev);
    for (i = 0x5a; i <= 0x5f; i++)
        mc_write(0, i, 1, 0x00, dev);
    /* DRL[0:7] 0001h: every row limit at 4 Mbytes until POST sizes them. */
    for (i = 0x60; i <= 0x6f; i++)
        dev->mc_pci_conf[i] = (i & 0x01) ? 0x00 : 0x01;
}

static void
i450kx_close(void *priv)
{
    i450kx_t *dev = (i450kx_t *) priv;

    smram_del(dev->smram[1]);
    smram_del(dev->smram[0]);
    free(dev);
}

static void *
i450kx_init(const device_t *info)
{
    i450kx_t *dev = (i450kx_t *) calloc(1, sizeof(i450kx_t));

    dev->gx = info->local;

    pci_add_card(PCI_ADD_NORTHBRIDGE, pb_read, pb_write, dev, &dev->pb_slot);        /* Device 19h: Intel 450KX PCI Bridge PB */
    pci_add_card(PCI_ADD_NORTHBRIDGE_SEC, mc_read, mc_write, dev, &dev->mc_slot);    /* Device 14h: Intel 450KX Memory Controller MC */

    dev->smram[0] = smram_add();
    dev->smram[1] = smram_add();

    cpu_cache_int_enabled = 1;
    cpu_cache_ext_enabled = 1;
    cpu_update_waitstates();

    i450kx_reset(dev);

    return dev;
}

const device_t i450kx_device = {
    .name          = "Intel 450KX (Mars)",
    .internal_name = "i450kx",
    .flags         = DEVICE_PCI,
    .local         = I450_KX,
    .init          = i450kx_init,
    .close         = i450kx_close,
    .reset         = i450kx_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t i450gx_device = {
    .name          = "Intel 450GX (Orion)",
    .internal_name = "i450gx",
    .flags         = DEVICE_PCI,
    .local         = I450_GX,
    .init          = i450kx_init,
    .close         = i450kx_close,
    .reset         = i450kx_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
