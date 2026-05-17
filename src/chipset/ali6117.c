/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the ALi M6117 SoC.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2020 RichardG.
 */
#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/pic.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/device.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/plat_fallthrough.h>

typedef struct ali6117_t {
    uint32_t local;

    /* Main registers (port 22h/23h) */
    uint8_t unlocked;
    uint8_t mode;
    uint8_t reg_offset;
    uint8_t regs[256];

    mem_mapping_t ram_mapping;
} ali6117_t;

/* Total size, Bank 0 size, Bank 1 size, Bank 2 size, Bank 3 size. */
static uint32_t ali6117_modes[32][13] = {
// clang-format off
    {  1024,   512,   512,     0,    0,  9,  9,  9,  9,  0,  0,  0,  0 }, /* 00 */
    {  2048,   512,   512,   512,  512,  9,  9,  9,  9,  9,  9,  9,  9 }, /* 01 */
    {  3072,   512,   512,  2048,    0,  9,  9,  9,  9, 10, 10,  0,  0 }, /* 02 */
    {  5120,   512,   512,  2048, 2048,  9,  9,  9,  9, 10, 10, 10, 10 }, /* 03 */
    {  9216,   512,   512,  8192,    0,  9,  9,  9,  9, 11, 11,  0,  0 }, /* 04 */
    {  1024,  1024,     0,     0,    0, 10,  9,  0,  0,  0,  0,  0,  0 }, /* 05 */
    {  2048,  1024,  1024,     0,    0, 10,  9, 10,  9,  0,  0,  0,  0 }, /* 06 */
    {  4096,  1024,  1024,  2048,    0, 10,  9, 10,  9, 10, 10,  0,  0 }, /* 07 */
    {  6144,  1024,  1024,  2048, 2048, 10,  9, 10,  9, 10, 10, 10, 10 }, /* 08 */
    { 10240,  1024,  1024,  8192,    0, 10,  9, 10,  9, 11, 11,  0,  0 }, /* 09 */
    { 18432,  1024,  1024,  8192, 8192, 10,  9, 10,  9, 11, 11, 11, 11 }, /* 0A */
    {  3072,  1024,  2048,     0,    0, 10,  9, 10, 10,  0,  0,  0,  0 }, /* 0B */
    {  5120,  1024,  2048,  2048,    0, 10,  9, 10, 10, 10, 10,  0,  0 }, /* 0C */
    {  9216,  1024,  8192,     0,    0, 10,  9, 11, 11,  0,  0,  0,  0 }, /* 0D */
    {  2048,  2048,     0,     0,    0, 10, 10,  0,  0,  0,  0,  0,  0 }, /* 0E */
    {  4096,  2048,  2048,     0,    0, 10, 10, 10, 10,  0,  0,  0,  0 }, /* 0F */
    {  6144,  2048,  2048,  2048,    0, 10, 10, 10, 10, 10, 10,  0,  0 }, /* 10 */
    {  8192,  2048,  2048,  2048, 2048, 10, 10, 10, 10, 10, 10, 10, 10 }, /* 11 */
    { 12288,  2048,  2048,  8192,    0, 10, 10, 10, 10, 11, 11,  0,  0 }, /* 12 */
    { 20480,  2048,  2048,  8192, 8192, 10, 10, 10, 10, 11, 11, 11, 11 }, /* 13 */
    { 10240,  2048,  8192,     0,    0, 10, 10, 11, 11,  0,  0,  0,  0 }, /* 14 */
    { 18432,  2048,  8192,  8192,    0, 10, 10, 11, 11, 11, 11,  0,  0 }, /* 15 */
    { 26624,  2048,  8192,  8192, 8192, 10, 10, 11, 11, 11, 11, 11, 11 }, /* 16 */
    {  4096,  4096,     0,     0,    0, 11, 10,  0,  0,  0,  0,  0,  0 }, /* 17 */
    {  8192,  4096,  4096,     0,    0, 11, 10, 11, 10,  0,  0,  0,  0 }, /* 18 */
    { 24576,  4096,  4096,  8192, 8192, 11, 10, 11, 10, 11, 11, 11, 11 }, /* 19 */
    { 12288,  4096,  8192,     0,    0, 11, 10, 11, 11,  0,  0,  0,  0 }, /* 1A */
    {  8192,  8192,     0,     0,    0, 11, 11,  0,  0,  0,  0,  0,  0 }, /* 1B */
    { 16384,  8192,  8192,     0,    0, 11, 11, 11, 11,  0,  0,  0,  0 }, /* 1C */
    { 24576,  8192,  8192,  8192,    0, 11, 11, 11, 11, 11, 11,  0,  0 }, /* 1D */
    { 32768,  8192,  8192,  8192, 8192, 11, 11, 11, 11, 11, 11, 11, 11 }, /* 1E */
    { 65536, 32768, 32768,     0,    0, 12, 12, 12, 12,  0,  0,  0,  0 }  /* 1F */
// clang-format on
};

#ifdef ENABLE_ALI6117_LOG
int ali6117_do_log = ENABLE_ALI6117_LOG;

static void
ali6117_log(const char *fmt, ...)
{
    va_list ap;

    if (ali6117_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ali6117_log(fmt, ...)
#endif

static void
ali6117_recalcmapping(ali6117_t *dev)
{
    uint32_t base;
    uint32_t size;
    int      state;

    shadowbios       = 0;
    shadowbios_write = 0;

    if ((dev->local == 0x08) || !(dev->regs[0x3c] & 0x08)) {
        ali6117_log("ALI6117: Shadowing for A0000-BFFFF (reg 12 bit 1) = off\n");
        mem_set_mem_state(0xa0000, 0x20000, MEM_WRITE_EXTERNAL | MEM_READ_EXTERNAL);
    } else {
        ali6117_log("ALI6117: Shadowing for A0000-BFFFF (reg 12 bit 1) = %s\n", (dev->regs[0x12] & 0x02) ? "on" : "off");
        mem_set_mem_state(0xa0000, 0x20000, (dev->regs[0x12] & 0x02) ? (MEM_WRITE_INTERNAL | MEM_READ_INTERNAL) : (MEM_WRITE_EXTANY | MEM_READ_EXTANY));
    }

    for (uint8_t reg = 0; reg <= 1; reg++) {
        for (uint8_t bitpair = 0; bitpair <= 3; bitpair++) {
            size = 0x8000;
            base = 0xc0000 + (size * ((reg * 4) + bitpair));
            ali6117_log("ALI6117: Shadowing for %05X-%05X (reg %02X bp %d wmask %02X rmask %02X) =", base, base + size - 1, 0x14 + reg, bitpair, 1 << ((bitpair * 2) + 1), 1 << (bitpair * 2));

            state = 0;
            if (dev->regs[0x14 + reg] & (1 << ((bitpair * 2) + 1))) {
                ali6117_log(" w on");
                state |= MEM_WRITE_INTERNAL;
                if (base >= 0xe0000)
                    shadowbios_write |= 1;
            } else {
                ali6117_log(" w off");
                state |= MEM_WRITE_EXTANY;
            }
            if (dev->regs[0x14 + reg] & (1 << (bitpair * 2))) {
                ali6117_log("; r on\n");
                state |= MEM_READ_INTERNAL;
                if (base >= 0xe0000)
                    shadowbios |= 1;
            } else {
                ali6117_log("; r off\n");
                state |= MEM_READ_EXTANY;
            }

            mem_set_mem_state(base, size, state);
        }
    }

    flushmmucache_nopc();
}

static void
ali6117_bank_addr(const ali6117_t *dev, const uint32_t phys, uint32_t *bank, uint32_t *addr)
{
    uint32_t row   = 0x00000000;
    uint32_t col   = 0x00000000;
    uint32_t m_row = 0x00000000;
    uint32_t m_col = 0x00000000;

    switch (dev->regs[0x10] & 0xf8) {
        default:
            *bank = 0xffffffff;
            *addr = 0xffffffff;
            break;
        case 0xe8:
            *bank = (phys >> 12) & 0x03;
            row   = ((phys >> 21) & 0x07) |
                    (((phys >> 14) & 0x3f) << 3) |
                    (((phys >> 20) & 0x01) << 9) |
                    (((phys >> 24) & 0x01) << 10);
            col   = ((phys >> 2) & 0xff) |
                    (((phys >> 1) & 0x01) << 8) |
                    (((phys >> 10) & 0x03) << 9);
            m_row = (1 << ali6117_modes[dev->mode][5 + (*bank * 2)]) - 1;
            m_col = (1 << ali6117_modes[dev->mode][6 + (*bank * 2)]) - 1;
            if (m_row > 0) {
                row  &= m_row;
                col  &= m_col;
                *addr = (row << 11) | col | (phys & 0x01);
                /* For easier array access. */
                (*bank)++;
            } else {
                /* Invalid bank. */
                *bank = 0xffffffff;
                *addr = 0xffffffff;
            }
            break;
        case 0xf8:
            *bank = (phys >> 13) & 0x01;
            row   = ((phys >> 21) & 0x07) |
                    (((phys >> 14) & 0x3f) << 3) |
                    (((phys >> 20) & 0x01) << 9) |
                    (((phys >> 24) & 0x03) << 10);
            col   = ((phys >> 2) & 0xff) |
                    (((phys >> 1) & 0x01) << 8) |
                    (((phys >> 10) & 0x07) << 9);
            m_row = (1 << ali6117_modes[dev->mode][5 + (*bank * 2)]) - 1;
            m_col = (1 << ali6117_modes[dev->mode][6 + (*bank * 2)]) - 1;
            if (m_row > 0) {
                row  &= m_row;
                col  &= m_col;
                *addr = (row << 12) | col | (phys & 0x01);
                /* For easier array access. */
                (*bank)++;
            } else {
                /* Invalid bank. */
                *bank = 0xffffffff;
                *addr = 0xffffffff;
            }
            break;
    }
}

static uint8_t
ali6117_read_ram(uint32_t addr, void *priv)
{
    const ali6117_t *dev = (ali6117_t *) priv;
    uint8_t ret = 0xff;

    uint32_t bank  = 0xffffffff;
    uint32_t baddr = 0xffffffff;

    ali6117_bank_addr(dev, addr, &bank, &baddr);

    if (bank != 0xffffffff) {
        const uint32_t low = (bank == 1) ? 0x00000000 :
                                           (ali6117_modes[dev->mode][bank - 1] << 10);
        ret = ram[low + baddr];
    }

    return ret;
}

static uint16_t
ali6117_read_ramw(uint32_t addr, void *priv)
{
    const ali6117_t *dev = (ali6117_t *) priv;
    uint16_t ret = 0xffff;

    uint32_t bank  = 0xffffffff;
    uint32_t baddr = 0xffffffff;

    if (addr & 0x00000001)
        ret = ali6117_read_ram(addr, priv) |
              (ali6117_read_ram(addr + 1, priv) << 8);
    else {
        ali6117_bank_addr(dev, addr, &bank, &baddr);

        if (bank != 0xffffffff) {
            const uint32_t low = (bank == 1) ? 0x00000000 :
                                               (ali6117_modes[dev->mode][bank - 1] << 10);
            ret = *(uint16_t *) &(ram[low + baddr]);
        }
    }

    return ret;
}

static void
ali6117_write_ram(uint32_t addr, uint8_t val, void *priv)
{
    const ali6117_t *dev = (ali6117_t *) priv;

    uint32_t bank  = 0xffffffff;
    uint32_t baddr = 0xffffffff;

    ali6117_bank_addr(dev, addr, &bank, &baddr);

    if (bank != 0xffffffff) {
        const uint32_t low = (bank == 1) ? 0x00000000 :
                                           (ali6117_modes[dev->mode][bank - 1] << 10);
        ram[low + baddr] = val;
    }
}

static void
ali6117_write_ramw(uint32_t addr, uint16_t val, void *priv)
{
    const ali6117_t *dev = (ali6117_t *) priv;

    uint32_t bank  = 0xffffffff;
    uint32_t baddr = 0xffffffff;

    if (addr & 0x00000001) {
        ali6117_write_ram(addr, val & 0xff, priv);
        ali6117_write_ram(addr + 1, val >> 8, priv);
    } else {
        ali6117_bank_addr(dev, addr, &bank, &baddr);

        if (bank != 0xffffffff) {
            const uint32_t low = (bank == 1) ? 0x00000000 :
                                               (ali6117_modes[dev->mode][bank - 1] << 10);
            *(uint16_t *) &(ram[low + baddr]) = val;
        }
    }
}

static void
ali6117_banked_mapping(ali6117_t *dev)
{
    mem_mapping_disable(&ram_low_mapping);
    mem_mapping_disable(&ram_mid_mapping);
    mem_mapping_disable(&ram_high_mapping);
}

static void
ali6117_default_mapping(ali6117_t *dev)
{
    mem_mapping_disable(&dev->ram_mapping);
    mem_mapping_enable(&ram_low_mapping);
    mem_mapping_enable(&ram_mid_mapping);
    mem_mapping_enable(&ram_high_mapping);

    if (mem_size > 1024)
        mem_set_mem_state_both(0x00100000, (mem_size << 10) - 0x00100000,
                               MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
}

static void
ali6117_bank_recalc(ali6117_t *dev)
{
    uint8_t mode      = (dev->regs[0x10] >> 4) |
                        ((dev->regs[0x10] & 0x08) << 1);
    uint32_t is_m6117 = (dev->local != 0x08);

    if (!is_m6117)
        mode &= 0x0f;

    uint32_t max_mem  = (dev->local == 0x08) ? 0x01000000 : 0x04000000;
    uint32_t rom_sz   = (dev->regs[0x10] & 0x01) ? 0x00020000 : 0x00010000;
    uint32_t mb_16    = (ali6117_modes[mode][0] >= 16384);
    uint32_t dram_on  = dev->regs[0x20] & 0x80;
    uint32_t mb15_on  = is_m6117 && !(dev->regs[0x10] & 0x04);
    uint32_t hole     = !dram_on || !mb_16 || !mb15_on;
    uint32_t is_flash = dev->regs[0x20] & 0x04;

    mem_set_mem_state_both(0x00100000, (max_mem - 0x00100000),
                           MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);

    if (dram_on)  switch (mode) {
        case 0x1e:
            ali6117_log("ALI6117: 32 MB %s mapping\n",
                        (dev->mode == mode) ? "default" : "banked");
            if (dev->mode == mode)
                ali6117_default_mapping(dev);
            else {
                ali6117_banked_mapping(dev);
                mem_mapping_set_addr(&dev->ram_mapping, 0x00000000, 0x02000000);
                mem_set_mem_state_both(0x00100000, 0x01f00000,
                                       MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
            }
            break;
        case 0x1f:
            ali6117_log("ALI6117: 64 MB %s mapping\n",
                        (dev->mode == mode) ? "default" : "banked");
            if (dev->mode == mode)
                ali6117_default_mapping(dev);
            else {
                ali6117_banked_mapping(dev);
                mem_mapping_set_addr(&dev->ram_mapping, 0x00000000, 0x04000000);
                mem_set_mem_state_both(0x00100000, 0x03f00000,
                                       MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
            }
            break;
        default:
            ali6117_log("ALI6117: Default mapping (%5i kB)\n", ali6117_modes[mode][0]);
            ali6117_default_mapping(dev);
            break;
    } else {
        ali6117_log("ALI6117: Mapping with DRAM controller disabled\n");
        mem_set_mem_state_both(0x00000000, 0x000a0000,
                       MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
    }

    if (dram_on) {
        mem_set_mem_state_both(0x00000000, 0x000a0000,
                               MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);

        ali6117_log("ALI6117: 15-16 MB on-board memory %sabled\n",
                    mb15_on ? "en" : "dis");
        if (!mb15_on)
            mem_set_mem_state_both(0x00f00000, 0x00100000,
                                   MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
    }

    if (hole) {
        if (is_flash) {
            ali6117_log("ALI6117: Flash mapping at: %08X-%08X\n",
                        0x01000000 - rom_sz, 0x01000000 - 1);
            mem_set_mem_state_both(0x01000000 - rom_sz, rom_sz,
                                   MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        } else {
            ali6117_log("ALI6117: ROM mapping at: %08X-%08X\n",
                        0x01000000 - rom_sz, 0x01000000 - 1);
            mem_set_mem_state_both(0x01000000 - rom_sz, rom_sz,
                                   MEM_READ_EXTANY | MEM_WRITE_DISABLED);
        }
    } else {
        ali6117_log("ALI6117: No ROM mapping at: %08X-%08X\n",
                    0x01000000 - rom_sz, 0x01000000 - 1);
    }

    if (is_flash)
        mem_set_mem_state_both(max_mem - rom_sz, rom_sz,
                               MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    else
        mem_set_mem_state_both(max_mem - rom_sz, rom_sz,
                               MEM_READ_EXTANY | MEM_WRITE_DISABLED);

    flushmmucache();
}

static void
ali6117_reg_write(uint16_t addr, uint8_t val, void *priv)
{
    ali6117_t *dev = (ali6117_t *) priv;

    ali6117_log("ALI6117: reg_write(%04X, %02X)\n", addr, val);

    if (addr == 0x22)
        dev->reg_offset = val;
    else if (dev->reg_offset == 0x13)
        dev->unlocked = (val == 0xc5);
    else if (dev->unlocked) {
        ali6117_log("ALI6117: regs[%02X] = %02X\n", dev->reg_offset, val);

        if (!(dev->local & 0x08) || (dev->reg_offset < 0x30))
            switch (dev->reg_offset) {
                case 0x30:
                case 0x34:
                case 0x35:
                case 0x3e:
                case 0x3f:
                case 0x46:
                case 0x4c:
                case 0x6a:
                case 0x73:
                    return; /* read-only registers */

                case 0x10:
                    refresh_at_enable          = !(val & 0x02) || !!(dev->regs[0x20] & 0x80);
                    dev->regs[dev->reg_offset] = val;

                    ali6117_bank_recalc(dev);
                    break;

                case 0x12:
                    val &= 0xf7;
                    fallthrough;

                case 0x14:
                case 0x15:
                    dev->regs[dev->reg_offset] = val;
                    ali6117_recalcmapping(dev);
                    break;

                case 0x1e:
                    val &= 0x07;

                    switch (val) {
                        /* Half PIT clock. */
                        case 0x0:
                            cpu_set_isa_speed(7159091);
                            break;

                        /* Divisors on the input clock PCLK2, which is double the CPU clock. */
                        case 0x1:
                            cpu_set_isa_speed((int) round(cpu_busspeed / 1.5));
                            break;

                        case 0x2:
                            cpu_set_isa_speed((int) round(cpu_busspeed / 2.0));
                            break;

                        case 0x3:
                            cpu_set_isa_speed((int) round(cpu_busspeed / 2.5));
                            break;

                        case 0x4:
                            cpu_set_isa_speed((int) round(cpu_busspeed / 3.0));
                            break;

                        case 0x5:
                            cpu_set_isa_speed((int) round(cpu_busspeed / 4.0));
                            break;

                        case 0x6:
                            cpu_set_isa_speed((int) round(cpu_busspeed / 5.0));
                            break;

                        case 0x7:
                            cpu_set_isa_speed((int) round(cpu_busspeed / 6.0));
                            break;

                        default:
                            break;
                    }
                    break;

                case 0x20:
                    val &= 0xbf;
                    refresh_at_enable = !(dev->regs[0x10] & 0x02) || !!(val & 0x80);
                    dev->regs[dev->reg_offset] = val;
                    ali6117_bank_recalc(dev);
                    break;

                case 0x31:
                    /* TODO: fast gate A20 (bit 0) */
                    val &= 0x21;
                    break;

                case 0x32:
                    val &= 0xc1;
                    break;

                case 0x33:
                    val &= 0xfd;
                    break;

                case 0x36:
                    val &= 0xf0;
                    val |= dev->regs[dev->reg_offset];
                    break;

                case 0x37:
                    val &= 0xf5;
                    break;

                case 0x3c:
                    val &= 0x8f;
                    ide_pri_disable();
                    ide_set_base(1, (val & 0x01) ? 0x170 : 0x1f0);
                    ide_set_side(1, (val & 0x01) ? 0x376 : 0x3f6);
                    ide_pri_enable();

                    dev->regs[dev->reg_offset] = val;
                    ali6117_recalcmapping(dev);
                    break;

                case 0x44:
                case 0x45:
                    val &= 0x3f;
                    break;

                case 0x4a:
                    val &= 0xfe;
                    break;

                case 0x55:
                    val &= 0x03;
                    break;

                case 0x56:
                    val &= 0xc7;
                    break;

                case 0x58:
                    val &= 0xc3;
                    break;

                case 0x59:
                    val &= 0x60;
                    break;

                case 0x5b:
                    val &= 0x1f;
                    break;

                case 0x64:
                    val &= 0xf7;
                    break;

                case 0x66:
                    val &= 0xe3;
                    break;

                case 0x67:
                    val &= 0xdf;
                    break;

                case 0x69:
                    val &= 0x50;
                    break;

                case 0x6b:
                    val &= 0x7f;
                    break;

                case 0x6e:
                case 0x6f:
                    val &= 0x03;
                    break;

                case 0x71:
                    val &= 0x1f;
                    break;

                default:
                    break;
            }

        dev->regs[dev->reg_offset] = val;
    }
}

static uint8_t
ali6117_reg_read(uint16_t addr, void *priv)
{
    const ali6117_t *dev = (ali6117_t *) priv;
    uint8_t    ret;

    if (addr == 0x22)
        ret = dev->reg_offset;
    else {
        ret = dev->regs[dev->reg_offset];
        if (dev->reg_offset == 0x10)
            ret = (ret & 0x0f) | 0x10;
        ali6117_log("ALI6117: reg_read(%02X) = %02X\n", dev->reg_offset, ret);
    }

    // ali6117_log("ALI6117: reg_read(%04X) = %02X\n", dev->reg_offset, ret);
    return ret;
}

static void
ali6117_reset(void *priv)
{
    ali6117_t *dev = (ali6117_t *) priv;

    ali6117_log("ALI6117: reset()\n");

    memset(dev->regs, 0, sizeof(dev->regs));
    dev->regs[0x11] = 0xf8;
    dev->regs[0x12] = 0x20;
    dev->regs[0x17] = 0xff;
    dev->regs[0x18] = 0xf0;
    dev->regs[0x1a] = 0xff;
    dev->regs[0x1b] = 0xf0;
    dev->regs[0x1d] = 0xff;
    dev->regs[0x20] = 0x80;
    if (dev->local & 0x08) {
        dev->regs[0x30] = 0x08;
        dev->regs[0x31] = 0x01;
        dev->regs[0x34] = 0x04;              /* enable internal RTC */
        dev->regs[0x35] = 0x20;              /* enable internal KBC */
        dev->regs[0x36] = dev->local & 0x07; /* M6117D ID */
    }

    cpu_set_isa_speed(7159091);

    refresh_at_enable = 1;

    ali6117_bank_recalc(dev);
    ali6117_recalcmapping(dev);
}

static void
ali6117_setup(ali6117_t *dev)
{
    ali6117_log("ALI6117: setup()\n");

    /* Main register interface */
    io_sethandler(0x22, 2,
                  ali6117_reg_read, NULL, NULL, ali6117_reg_write, NULL, NULL, dev);
}

static void
ali6117_close(void *priv)
{
    ali6117_t *dev = (ali6117_t *) priv;

    ali6117_log("ALI6117: close()\n");

    io_removehandler(0x22, 2,
                     ali6117_reg_read, NULL, NULL, ali6117_reg_write, NULL, NULL, dev);

    free(dev);
}

static void *
ali6117_init(const device_t *info)
{
    uint32_t last_match = 0;

    ali6117_log("ALI6117: init()\n");

    ali6117_t *dev = (ali6117_t *) calloc(1, sizeof(ali6117_t));

    dev->local = info->local;

    if (!(dev->local & 0x08))
        device_add(&ide_isa_device);

    ali6117_setup(dev);

    for (int8_t i = 31; i >= 0; i--) {
        if ((mem_size >= ali6117_modes[i][0]) && (ali6117_modes[i][0] > last_match)) {
            last_match = ali6117_modes[i][0];
            dev->mode  = i;
        }
    }

    if (mem_size == 8192)
        dev->mode = 0x18;

    if (mem_size == 2048)
        dev->mode = 0x06;

    mem_mapping_add(&dev->ram_mapping,
               0x00000000,
               (mem_size + 384) << 10,
               ali6117_read_ram,
               ali6117_read_ramw,
               NULL,
               ali6117_write_ram,
               ali6117_write_ramw,
               NULL,
               ram,
               MEM_MAPPING_INTERNAL,
               dev);
    mem_mapping_disable(&dev->ram_mapping);

    ali6117_reset(dev);

    if (!(dev->local & 0x08))
        pic_elcr_io_handler(0);

    return dev;
}

const device_t ali1217_device = {
    .name          = "ALi M1217",
    .internal_name = "ali1217",
    .flags         = DEVICE_ISA16,
    .local         = 0x8,
    .init          = ali6117_init,
    .close         = ali6117_close,
    .reset         = ali6117_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ali6117d_device = {
    .name          = "ALi M6117D",
    .internal_name = "ali6117d",
    .flags         = DEVICE_ISA16,
    .local         = 0x2,
    .init          = ali6117_init,
    .close         = ali6117_close,
    .reset         = ali6117_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
