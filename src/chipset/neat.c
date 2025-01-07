/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of C&T CS8121 ("NEAT") 82C206/211/212/215 chipset.
 *
 * Note:    The datasheet mentions that the chipset supports up to 8MB
 *          of DRAM. This is intepreted as 'being able to refresh up to
 *          8MB of DRAM chips', because it works fine with bus-based
 *          memory expansion.
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2018 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/plat_unused.h>
#include <86box/chipset.h>

#define NEAT_DEBUG  0

#define EMS_MAXPAGE 4
#define EMS_PGSIZE  16384
#define EMS_PGMASK  16383

#define REG_MASK       0x0f

/* CS8221 82C211 controller registers. */
#define REG_RA0        0x60 /* PROCCLK selector */
#define RA0_MASK       0x34 /* RR11 X1XR */
#define RA0_READY      0x01 /*  local bus READY timeout */
#define RA0_RDYNMIEN   0x04 /*  local bus READY tmo NMI enable */
#define RA0_PROCCLK    0x10 /*  PROCCLK=BCLK (1) or CLK2IN (0) */
#define RA0_ALTRST     0x20 /*  alternate CPU reset (1) */
#define RA0_REV        0xc0 /*  chip revision ID */
#define RA0_REV_SH     6
#define RA0_REV_ID     2 /* faked revision# for 82C211 */

#define REG_RA1        0x61 /* Command Delay */
#define RA1_MASK       0xff /* 1111 1111 */
#define RA1_BUSDLY     0x03 /*  AT BUS command delay */
#define RA1_BUSDLY_SH  0
#define RA1_BUS8DLY    0x0c /*  AT BUS 8bit command delay */
#define RA1_BUS8DLY_SH 2
#define RA1_MEMDLY     0x30 /*  AT BUS 16bit memory delay */
#define RA1_MEMDLY_SH  4
#define RA1_QUICKEN    0x40 /*  Quick Mode enable */
#define RA1_HOLDDLY    0x80 /*  Hold Time Delay */

#define REG_RA2        0x62 /* Wait State / BCLK selector */
#define RA2_MASK       0x3f /* XX11 1111 */
#define RA2_BCLK       0x03 /*  BCLK select */
#define RA2_BCLK_SH    0
#define BCLK_IN2       0    /*  BCLK = CLK2IN/2 */
#define BCLK_IN        1    /*  BCLK = CLK2IN */
#define BCLK_AT        2    /*  BCLK = ATCLK */
#define RA2_AT8WS      0x0c /*  AT 8-bit wait states */
#define RA2_AT8WS_SH   2
#define AT8WS_2        0    /*  2 wait states */
#define AT8WS_3        1    /*  3 wait states */
#define AT8WS_4        2    /*  4 wait states */
#define AT8WS_5        4    /*  5 wait states */
#define RA2_ATWS       0x30 /*  AT 16-bit wait states */
#define RA2_ATWS_SH    4
#define ATWS_2         0 /*  2 wait states */
#define ATWS_3         1 /*  3 wait states */
#define ATWS_4         2 /*  4 wait states */
#define ATWS_5         4 /*  5 wait states */

/* CS8221 82C212 controller registers. */
#define REG_RB0        0x64 /* Version ID */
#define RB0_MASK       0x60 /* R11X XXXX */
#define RB0_REV        0x60 /*  Chip revsion number */
#define RB0_REV_SH     5
#define RB0_REV_ID     2    /* faked revision# for 82C212 */
#define RB0_VERSION    0x80 /*  Chip version (0=82C212) */

#define REG_RB1        0x65 /* ROM configuration */
#define RB1_MASK       0xff /* 1111 1111 */
#define RB1_ROMF0      0x01 /*  ROM F0000 enabled (0) */
#define RB1_ROME0      0x02 /*  ROM E0000 disabled (1) */
#define RB1_ROMD0      0x04 /*  ROM D0000 disabled (1) */
#define RB1_ROMC0      0x08 /*  ROM C0000 disabled (1) */
#define RB1_SHADOWF0   0x10 /*  Shadow F0000 R/W (0) */
#define RB1_SHADOWE0   0x20 /*  Shadow E0000 R/W (0) */
#define RB1_SHADOWD0   0x40 /*  Shadow D0000 R/W (0) */
#define RB1_SHADOWC0   0x80 /*  Shadow C0000 R/W (0) */

#define REG_RB2        0x66 /* Memory Enable 1 */
#define RB2_MASK       0x80 /* 1XXX XXXX */
#define RB2_TOP128     0x80 /*  top 128K is on sysboard (1) */

#define REG_RB3        0x67 /* Memory Enable 2 */
#define RB3_MASK       0xff /* 1111 1111 */
#define RB3_SHENB0     0x01 /*  enable B0000-B3FFF shadow (1) */
#define RB3_SHENB4     0x02 /*  enable B4000-B7FFF shadow (1) */
#define RB3_SHENB8     0x04 /*  enable B8000-BBFFF shadow (1) */
#define RB3_SHENBC     0x08 /*  enable BC000-BFFFF shadow (1) */
#define RB3_SHENA0     0x10 /*  enable A0000-A3FFF shadow (1) */
#define RB3_SHENA4     0x20 /*  enable A4000-A7FFF shadow (1) */
#define RB3_SHENA8     0x40 /*  enable A8000-ABFFF shadow (1) */
#define RB3_SHENAC     0x80 /*  enable AC000-AFFFF shadow (1) */

#define REG_RB4        0x68 /* Memory Enable 3 */
#define RB4_MASK       0xff /* 1111 1111 */
#define RB4_SHENC0     0x01 /*  enable C0000-C3FFF shadow (1) */
#define RB4_SHENC4     0x02 /*  enable C4000-C7FFF shadow (1) */
#define RB4_SHENC8     0x04 /*  enable C8000-CBFFF shadow (1) */
#define RB4_SHENCC     0x08 /*  enable CC000-CFFFF shadow (1) */
#define RB4_SHEND0     0x10 /*  enable D0000-D3FFF shadow (1) */
#define RB4_SHEND4     0x20 /*  enable D4000-D7FFF shadow (1) */
#define RB4_SHEND8     0x40 /*  enable D8000-DBFFF shadow (1) */
#define RB4_SHENDC     0x80 /*  enable DC000-DFFFF shadow (1) */

#define REG_RB5        0x69 /* Memory Enable 4 */
#define RB5_MASK       0xff /* 1111 1111 */
#define RB5_SHENE0     0x01 /*  enable E0000-E3FFF shadow (1) */
#define RB5_SHENE4     0x02 /*  enable E4000-E7FFF shadow (1) */
#define RB5_SHENE8     0x04 /*  enable E8000-EBFFF shadow (1) */
#define RB5_SHENEC     0x08 /*  enable EC000-EFFFF shadow (1) */
#define RB5_SHENF0     0x10 /*  enable F0000-F3FFF shadow (1) */
#define RB5_SHENF4     0x20 /*  enable F4000-F7FFF shadow (1) */
#define RB5_SHENF8     0x40 /*  enable F8000-FBFFF shadow (1) */
#define RB5_SHENFC     0x80 /*  enable FC000-FFFFF shadow (1) */

#define REG_RB6        0x6a /* Bank 0/1 Enable */
#define RB6_MASK       0xe0 /* 111R RRRR */
#define RB6_BANKS      0x20 /*  #banks used (1=two) */
#define RB6_RTYPE      0xc0 /*  DRAM chip size used */
#define RTYPE_SH       6
#define RTYPE_NONE     0 /*  Disabled */
#define RTYPE_MIXED    1 /*  64K/256K mixed (for 640K) */
#define RTYPE_256K     2 /*  256K (default) */
#define RTYPE_1M       3 /*  1M */

#define REG_RB7        0x6b /* DRAM configuration */
#define RB7_MASK       0xff /* 1111 1111 */
#define RB7_ROMWS      0x03 /*  ROM access wait states */
#define RB7_ROMWS_SH   0
#define ROMWS_0        0    /*  0 wait states */
#define ROMWS_1        1    /*  1 wait states */
#define ROMWS_2        2    /*  2 wait states */
#define ROMWS_3        3    /*  3 wait states (default) */
#define RB7_EMSWS      0x0c /*  EMS access wait states */
#define RB7_EMSWS_SH   2
#define EMSWS_0        0    /*  0 wait states */
#define EMSWS_1        1    /*  1 wait states */
#define EMSWS_2        2    /*  2 wait states */
#define EMSWS_3        3    /*  3 wait states (default) */
#define RB7_EMSEN      0x10 /* enable EMS (1=on) */
#define RB7_RAMWS      0x20 /*  RAM access wait state (1=1ws) */
#define RB7_UMAREL     0x40 /*  relocate 640-1024K to 1M */
#define RB7_PAGEEN     0x80 /*  enable Page/Interleaved mode */

#define REG_RB8        0x6c /* Bank 2/3 Enable */
#define RB8_MASK       0xf0 /* 1111 RRRR */
#define RB8_4WAY       0x10 /*  enable 4-way interleave mode */
#define RB8_BANKS      0x20 /*  enable 2 banks (1) */
#define RB8_RTYPE      0xc0 /*  DRAM chip size used */
#define RB8_RTYPE_SH   6

#define REG_RB9        0x6d /* EMS base address */
#define RB9_MASK       0xff /* 1111 1111 */
#define RB9_BASE       0x0f /*  I/O base address selection */
#define RB9_BASE_SH    0
#define RB9_FRAME      0xf0 /*  frame address selection */
#define RB9_FRAME_SH   4

#define REG_RB10       0x6e /* EMS address extension */
#define RB10_MASK      0xff /* 1111 1111 */
#define RB10_P3EXT     0x03 /*  page 3 extension */
#define RB10_P3EXT_SH  0
#define PEXT_0M        0    /*  page is at 0-2M */
#define PEXT_2M        1    /*  page is at 2-4M */
#define PEXT_4M        2    /*  page is at 4-6M */
#define PEXT_6M        3    /*  page is at 6-8M */
#define RB10_P2EXT     0x0c /*  page 2 extension */
#define RB10_P2EXT_SH  2
#define RB10_P1EXT     0x30 /*  page 1 extension */
#define RB10_P1EXT_SH  4
#define RB10_P0EXT     0xc0 /*  page 0 extension */
#define RB10_P0EXT_SH  6

#define REG_RB12       0x6f /* Miscellaneous */
#define RB12_MASK      0xe6 /* 111R R11R */
#define RB12_GA20      0x02 /*  gate for A20 */
#define RB12_RASTMO    0x04 /*  enable RAS timeout counter */
#define RB12_EMSLEN    0xe0 /*  EMS memory chunk size */
#define RB12_EMSLEN_SH 5

#define MEM_FLAG_REMAP      0x10
#define MEM_FLAG_EMS        0x08
#define MEM_FLAG_ROMCS      0x04
#define MEM_FLAG_READ       0x02
#define MEM_FLAG_WRITE      0x01
#define MEM_FMASK_REMAP     0x10
#define MEM_FMASK_EMS       0x08
#define MEM_FMASK_SHADOW    0x07

typedef struct ram_page_t {
    int8_t        enabled;              /* 1=ENABLED */
    char          pad;
    uint32_t      phys_base;
    uint32_t      virt_base;
    mem_mapping_t mapping;             /* mapping entry for page */
} ram_page_t;

typedef struct neat_t {
    uint8_t       mem_flags[32];
    uint8_t       regs[128];           /* all the CS8221 registers */
    uint8_t       indx;                /* programmed index into registers */

    char          pad;

    uint16_t      ems_base;            /* configured base address */
    uint32_t      ems_frame;           /* configured frame address */
    uint16_t      ems_size;            /* EMS size in KB */
    uint16_t      ems_pages;           /* EMS size in pages */

    uint32_t      remap_base;

    ram_page_t    ems[EMS_MAXPAGE];    /* EMS page registers */
    ram_page_t    shadow[32];          /* Shadow RAM pages */
} neat_t;

static uint8_t defaults[16] = { 0x0a, 0x45, 0xfc, 0x00, 0x00, 0xfe, 0x00, 0x00,
                                0x00, 0x00, 0xa0, 0x63, 0x10, 0x00, 0x00, 0x12 };

static uint8_t masks[4]     = { RB10_P0EXT, RB10_P1EXT, RB10_P2EXT, RB10_P3EXT };
static uint8_t shifts[4]    = { RB10_P0EXT_SH, RB10_P1EXT_SH, RB10_P2EXT_SH, RB10_P3EXT_SH };

#ifdef ENABLE_NEAT_LOG
int neat_do_log = ENABLE_NEAT_LOG;

static void
neat_log(const char *fmt, ...)
{
    va_list ap;

    if (neat_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define neat_log(fmt, ...)
#endif

/* Read one byte from paged RAM. */
static uint8_t
ems_readb(uint32_t addr, void *priv)
{
    ram_page_t *dev = (ram_page_t *) priv;
    uint8_t     ret = 0xff;
#ifdef ENABLE_NEAT_LOG
    uint32_t    old = addr;
#endif

    /* Grab the data. */
    addr = addr - dev->virt_base + dev->phys_base;

    if (addr < (mem_size << 10))
        ret = *(uint8_t *) &(ram[addr]);

    neat_log("[R08] %08X -> %08X (%08X): ret = %02X\n", old, addr, (mem_size << 10), ret);
    return ret;
}

/* Read one word from paged RAM. */
static uint16_t
ems_readw(uint32_t addr, void *priv)
{
    ram_page_t *dev = (ram_page_t *) priv;
    uint16_t    ret = 0xffff;
#ifdef ENABLE_NEAT_LOG
    uint32_t    old = addr;
#endif

    /* Grab the data. */
    addr = addr - dev->virt_base + dev->phys_base;

    if (addr < (mem_size << 10))
        ret = *(uint16_t *) &(ram[addr]);

    neat_log("[R16] %08X -> %08X (%08X): ret = %04X\n", old, addr, (mem_size << 10), ret);
    return ret;
}

/* Write one byte to paged RAM. */
static void
ems_writeb(uint32_t addr, uint8_t val, void *priv)
{
    ram_page_t *dev = (ram_page_t *) priv;
#ifdef ENABLE_NEAT_LOG
    uint32_t    old = addr;
#endif

    /* Write the data. */
    addr = addr - dev->virt_base + dev->phys_base;
    neat_log("[W08] %08X -> %08X (%08X): val = %02X\n", old, addr, (mem_size << 10), val);

    if (addr < (mem_size << 10))
        *(uint8_t *) &(ram[addr]) = val;
}

/* Write one word to paged RAM. */
static void
ems_writew(uint32_t addr, uint16_t val, void *priv)
{
    ram_page_t *dev = (ram_page_t *) priv;
#ifdef ENABLE_NEAT_LOG
    uint32_t    old = addr;
#endif

    /* Write the data. */
    addr = addr - dev->virt_base + dev->phys_base;
    neat_log("[W16] %08X -> %08X (%08X): val = %04X\n", old, addr, (mem_size << 10), val);

    if (addr < (mem_size << 10))
        *(uint16_t *) &(ram[addr]) = val;
}

static void
neat_mem_update_state(neat_t *dev, uint32_t addr, uint32_t size, uint8_t new_flags, uint8_t mask)
{
    if ((addr >= 0x00080000) && (addr < 0x00100000) &&
        ((new_flags ^ dev->mem_flags[(addr - 0x00080000) / EMS_PGSIZE]) & mask)) {
        dev->mem_flags[(addr - 0x00080000) / EMS_PGSIZE] &= ~mask;
        dev->mem_flags[(addr - 0x00080000) / EMS_PGSIZE] |= new_flags;

        new_flags = dev->mem_flags[(addr - 0x00080000) / EMS_PGSIZE];

        if (new_flags & MEM_FLAG_ROMCS) {
            neat_log("neat_mem_update_state(): %08X-%08X: %02X (ROMCS)\n", addr, addr + size - 1, new_flags);
            mem_set_mem_state(addr, size, MEM_READ_ROMCS | MEM_WRITE_ROMCS);
        } else if (new_flags & MEM_FLAG_REMAP) {
            neat_log("neat_mem_update_state(): %08X-%08X: %02X (REMAP)\n", addr, addr + size - 1, new_flags);
            mem_set_mem_state(addr, size, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
        } else if (new_flags & MEM_FLAG_EMS) {
            neat_log("neat_mem_update_state(): %08X-%08X: %02X (EMS)\n", addr, addr + size - 1, new_flags);
            mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        } else  switch (new_flags & (MEM_FLAG_READ | MEM_FLAG_WRITE)) {
            case 0:
                neat_log("neat_mem_update_state(): %08X-%08X: %02X (RE | WE)\n", addr, addr + size - 1, new_flags);
                mem_set_mem_state(addr, size, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
                break;
            case 1:
                neat_log("neat_mem_update_state(): %08X-%08X: %02X (RE | WI)\n", addr, addr + size - 1, new_flags);
                mem_set_mem_state(addr, size, MEM_READ_EXTERNAL | MEM_WRITE_INTERNAL);
                break;
            case 2:
                neat_log("neat_mem_update_state(): %08X-%08X: %02X (RI | WE)\n", addr, addr + size - 1, new_flags);
                mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_EXTERNAL);
                break;
            case 3:
                neat_log("neat_mem_update_state(): %08X-%08X: %02X (RI | WI)\n", addr, addr + size - 1, new_flags);
                mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                break;
            default:
                break;
        }
    }

    flushmmucache_nopc();
}

static void
shadow_recalc(neat_t *dev)
{
    for (uint8_t i = 8; i < 32; i++) {
        int romcs      = 0;
        int write      = 1;
        int shadow_reg = REG_RB3 + ((i - 8) >> 3);
        int shadow_bit = i & 7;
        int mem_flags;
        int read;

        if (i >= 16) {
            int rb1_romcs_bit = 7 - (i >> 2);
            int rb1_write_bit = rb1_romcs_bit + 4;

            romcs = !(dev->regs[REG_RB1] & (1 << rb1_romcs_bit));
            write = !(dev->regs[REG_RB1] & (1 << rb1_write_bit));
            neat_log("Shadow %08X-%08X: [%02X, %02X] %02X:%02X, %02X, %02X\n",
                     dev->shadow[i].virt_base, dev->shadow[i].virt_base + EMS_PGSIZE - 1,
                     dev->regs[REG_RB1], dev->regs[shadow_reg],
                     shadow_reg, shadow_bit,
                     rb1_romcs_bit, rb1_write_bit);
        } else {
            shadow_bit ^= 4;
            neat_log("Shadow %08X-%08X: [--, %02X] %02X:%02X, shadow bit ^= 4\n",
                     dev->shadow[i].virt_base, dev->shadow[i].virt_base + EMS_PGSIZE - 1,
                     dev->regs[shadow_reg],
                     shadow_reg, shadow_bit);
        }

        read  = dev->regs[shadow_reg] & (1 << shadow_bit);
        write = write && read;

        mem_flags  = romcs ? MEM_FLAG_ROMCS : 0x00;
        mem_flags |= read  ? MEM_FLAG_READ  : 0x00;
        mem_flags |= write ? MEM_FLAG_WRITE : 0x00;

        if ((mem_flags > 0x00) && !(mem_flags & MEM_FLAG_ROMCS))
            mem_mapping_set_addr(&(dev->shadow[i].mapping), dev->shadow[i].virt_base, EMS_PGSIZE);
        else
            mem_mapping_disable(&(dev->shadow[i].mapping));

        neat_mem_update_state(dev, dev->shadow[i].virt_base, EMS_PGSIZE, mem_flags, MEM_FMASK_SHADOW);
    }
}

/* Re-calculate the active-page physical address. */
static void
ems_recalc(neat_t *dev, ram_page_t *ems)
{
    uint32_t page  = ems->phys_base / EMS_PGSIZE;

    neat_log("ems_recalc(): %08X, %04X, %04X\n", ems->virt_base, page, dev->ems_pages);

    if ((dev->regs[REG_RB7] & RB7_EMSEN) && ems->enabled && (page < dev->ems_pages)) {
        neat_log("ems_recalc(): %08X-%08X -> %08X-%08X\n",
                 ems->virt_base, ems->virt_base + EMS_PGSIZE - 1,
                 ems->phys_base, ems->phys_base + EMS_PGSIZE - 1);
        mem_mapping_set_addr(&ems->mapping, ems->virt_base, EMS_PGSIZE);

        /* Update the EMS RAM address for this page. */
        mem_mapping_set_exec(&ems->mapping, ram + ems->phys_base);

        neat_mem_update_state(dev, ems->virt_base, EMS_PGSIZE, MEM_FLAG_EMS, MEM_FMASK_EMS);

#if NEAT_DEBUG > 1
        neat_log("NEAT EMS: page %d set to %08lx, %sabled)\n",
                 ems->page, ems->addr - ram, ems->enabled ? "en" : "dis");
#endif
    } else {
        /* Disable this page. */
        mem_mapping_disable(&ems->mapping);

        neat_mem_update_state(dev, ems->virt_base, EMS_PGSIZE, 0x00, MEM_FMASK_EMS);
    }
}

static void
ems_write(uint16_t port, uint8_t val, void *priv)
{
    neat_t     *dev           = (neat_t *) priv;
    ram_page_t *ems;
    int         vpage;
    int8_t      old_enabled;
    uint32_t    old_phys_base;
    int8_t      new_enabled;
    uint32_t    new_phys_base;

#if NEAT_DEBUG > 1
    neat_log("NEAT: ems_write(%04x, %02x)\n", port, val);
#endif

    /* Get the viewport page number. */
    vpage = (port / EMS_PGSIZE);
    ems   = &dev->ems[vpage];

    neat_log("Port: %04X, val: %02X\n", port, val);

    switch (port & 0x000f) {
        case 0x0008:
        case 0x0009:
            old_enabled    = ems->enabled;
            old_phys_base  = ems->phys_base;
            new_enabled    = !!(val & 0x80);
            new_phys_base  = (ems->phys_base & 0xffe00000) | ((val & 0x7f) * EMS_PGSIZE);

            if ((old_enabled != new_enabled) || (old_phys_base != new_phys_base)) {
                if (old_enabled && (old_enabled == new_enabled)) {
                    ems->enabled   = 0;
                    ems_recalc(dev, ems);
                }

                ems->enabled   = !!(val & 0x80);

                if (old_phys_base != new_phys_base)
                    ems->phys_base = (ems->phys_base & 0xffe00000) | ((val & 0x7f) * EMS_PGSIZE);

                ems_recalc(dev, ems);
            }
            break;
        default:
            break;
    }
}

static uint8_t
ems_read(uint16_t port, void *priv)
{
    const neat_t *dev = (neat_t *) priv;
    uint8_t       ret = 0xff;
    int           vpage;

    /* Get the viewport page number. */
    vpage = (port / EMS_PGSIZE);

    switch (port & 0x000f) {
        case 0x0008: /* page number register */
            ret = (dev->ems[vpage].phys_base / EMS_PGSIZE) & 0x7f;
            if (dev->ems[vpage].enabled)
                ret |= 0x80;
            break;
        default:
            break;
    }

    neat_log("Port: %04X, ret: %02X\n", port, ret);

#if NEAT_DEBUG > 1
    neat_log("NEAT: ems_read(%04x) = %02x\n", port, ret);
#endif

    return ret;
}

static void
ems_recalc_all(neat_t *dev)
{
    for (uint8_t i = 0; i < EMS_MAXPAGE; i++)
        ems_recalc(dev, &(dev->ems[i]));
}

static void
ems_update_virt_base(neat_t *dev)
{
    for (uint8_t i = 0; i < EMS_MAXPAGE; i++)
        dev->ems[i].virt_base = dev->ems_frame + (i * EMS_PGSIZE);
}

static void
ems_remove_handlers(neat_t *dev)
{
    for (uint8_t i = 0; i < EMS_MAXPAGE; i++) {
        neat_log("Removing I/O handler at %04X-%04X\n",
                 dev->ems_base + (i * EMS_PGSIZE), dev->ems_base + (i * EMS_PGSIZE) + 1);
        /* Clean up any previous I/O port handler. */
        io_removehandler(dev->ems_base + (i * EMS_PGSIZE), 2,
                         ems_read, NULL, NULL, ems_write, NULL, NULL, dev);
    }
}

static void
ems_set_handlers(neat_t *dev)
{
    for (uint8_t i = 0; i < EMS_MAXPAGE; i++) {
        neat_log("Setting up I/O handler at %04X-%04X\n",
                 dev->ems_base + (i * EMS_PGSIZE), dev->ems_base + (i * EMS_PGSIZE) + 1);
        /* Set up an I/O port handler. */
        io_sethandler(dev->ems_base + (i * EMS_PGSIZE), 2,
                      ems_read, NULL, NULL, ems_write, NULL, NULL, dev);
    }

    ems_recalc_all(dev);
}

static void
remap_update_states(neat_t *dev, uint8_t flag)
{
    for (uint8_t i = 0; i < 24; i++)
         neat_mem_update_state(dev, 0x000a0000 + (i * EMS_PGSIZE), EMS_PGSIZE, flag, MEM_FMASK_REMAP);
}

static void
remap_update(neat_t *dev, uint8_t val)
{
    if (dev->regs[REG_RB7] & RB7_UMAREL) {
        mem_remap_top_ex_nomid(0, (dev->remap_base >= 1024) ? dev->remap_base : 1024);

        remap_update_states(dev, 0x00);
        neat_log("0 kB at %08X\n", ((dev->remap_base >= 1024) ? dev->remap_base : 1024) << 10);
    }

    if (val & RB7_EMSEN)
        dev->remap_base = mem_size - dev->ems_size;
    else
        dev->remap_base = mem_size;
    neat_log("Total contiguous memory now: %i kB\n", dev->remap_base);

    if (dev->remap_base >= 640)
        mem_mapping_set_addr(&ram_low_mapping, 0x00000000, 0x000a0000);
    else
        mem_mapping_set_addr(&ram_low_mapping, 0x00000000, dev->remap_base << 10);

    if (dev->remap_base > 1024)
        mem_mapping_set_addr(&ram_high_mapping, 0x00100000, (dev->remap_base << 10) - 0x00100000);
    else
        mem_mapping_disable(&ram_high_mapping);

    if (val & RB7_UMAREL) {
        mem_remap_top_ex_nomid(384, (dev->remap_base >= 1024) ? dev->remap_base : 1024);

        remap_update_states(dev, MEM_FLAG_REMAP);
        neat_log("384 kB at %08X\n", ((dev->remap_base >= 1024) ? dev->remap_base : 1024) << 10);
    }
}

static void
neat_write(uint16_t port, uint8_t val, void *priv)
{
    neat_t  *dev = (neat_t *) priv;
    uint8_t  xval;
    uint8_t j;
    uint8_t *reg;
    int      i;

#if NEAT_DEBUG > 2
    neat_log("NEAT: write(%04x, %02x)\n", port, val);
#endif

    switch (port) {
        case 0x22:
            dev->indx = val;
            break;

        case 0x23:
            reg  = &dev->regs[dev->indx];
            xval = *reg ^ val;
            switch (dev->indx) {
                case REG_RA0:
                    val &= RA0_MASK;
                    *reg = (*reg & ~RA0_MASK) | val | (RA0_REV_ID << RA0_REV_SH);
                    if ((xval & 0x20) && (val & 0x20))
                        outb(0x64, 0xfe);
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RA0=%02x(%02x)\n", val, *reg);
#endif
                    break;

                case REG_RA1:
                    val &= RA1_MASK;
                    *reg = (*reg & ~RA1_MASK) | val;
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RA1=%02x(%02x)\n", val, *reg);
#endif
                    break;

                case REG_RA2:
                    val &= RA2_MASK;
                    *reg = (*reg & ~RA2_MASK) | val;
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RA2=%02x(%02x)\n", val, *reg);
#endif
                    break;

                case REG_RB0:
                    val &= RB0_MASK;
                    *reg = (*reg & ~RB0_MASK) | val | (RB0_REV_ID << RB0_REV_SH);
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RB0=%02x(%02x)\n", val, *reg);
#endif
                    break;

                case REG_RB1:
                    val &= RB1_MASK;
                    *reg = (*reg & ~RB1_MASK) | val;
                    shadow_recalc(dev);
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RB1=%02x(%02x)\n", val, *reg);
#endif
                    break;

                case REG_RB2:
                    val &= RB2_MASK;
                    *reg = (*reg & ~RB2_MASK) | val;
                    if (val & RB2_TOP128)
                        neat_mem_update_state(dev, 0x00080000, 0x00020000, MEM_FLAG_READ | MEM_FLAG_WRITE, MEM_FMASK_SHADOW);
                    else
                        neat_mem_update_state(dev, 0x00080000, 0x00020000, 0x00, MEM_FMASK_SHADOW);
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RB2=%02x(%02x)\n", val, *reg);
#endif
                    break;

                case REG_RB3:
                    val &= RB3_MASK;
                    *reg = (*reg & ~RB3_MASK) | val;
                    shadow_recalc(dev);
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RB3=%02x(%02x)\n", val, *reg);
#endif
                    break;

                case REG_RB4:
                    val &= RB4_MASK;
                    *reg = (*reg & ~RB4_MASK) | val;
                    shadow_recalc(dev);
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RB4=%02x(%02x)\n", val, *reg);
#endif
                    break;

                case REG_RB5:
                    val &= RB5_MASK;
                    *reg = (*reg & ~RB5_MASK) | val;
                    shadow_recalc(dev);
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RB5=%02x(%02x)\n", val, *reg);
#endif
                    break;

                case REG_RB6:
                    val &= RB6_MASK;
                    *reg = (*reg & ~RB6_MASK) | val;
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RB6=%02x(%02x)\n", val, *reg);
#endif
                    break;

                case REG_RB7:
                    val &= RB7_MASK;

                    if (xval & (RB7_EMSEN | RB7_UMAREL))
                        remap_update(dev, val);

                    dev->regs[REG_RB7] = val;

                    if (xval & RB7_EMSEN)
                        ems_remove_handlers(dev);

                    if ((xval & RB7_EMSEN) && (val & RB7_EMSEN))
                        ems_set_handlers(dev);

#if NEAT_DEBUG > 1
                    neat_log("NEAT: RB7=%02x(%02x)\n", val, *reg);
#endif
                    break;

                case REG_RB8:
                    val &= RB8_MASK;
                    *reg = (*reg & ~RB8_MASK) | val;
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RB8=%02x(%02x)\n", val, *reg);
#endif
                    break;

                case REG_RB9:
                    val &= RB9_MASK;
                    *reg = (*reg & ~RB9_MASK) | val;
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RB9=%02x(%02x)\n", val, *reg);
#endif

                    ems_remove_handlers(dev);

                    /* Get configured I/O address. */
                    j              = (dev->regs[REG_RB9] & RB9_BASE) >> RB9_BASE_SH;
                    dev->ems_base  = 0x0208 + (0x10 * j);

                    /* Get configured frame address. */
                    j              = (dev->regs[REG_RB9] & RB9_FRAME) >> RB9_FRAME_SH;
                    dev->ems_frame = 0xc0000 + (EMS_PGSIZE * j);

                    ems_update_virt_base(dev);

                    if (dev->regs[REG_RB7] & RB7_EMSEN)
                        ems_set_handlers(dev);
                    break;

                case REG_RB10:
                    val &= RB10_MASK;
                    *reg = (*reg & ~RB10_MASK) | val;
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RB10=%02x(%02x)\n", val, *reg);
#endif

                    for (uint8_t i = 0; i < EMS_MAXPAGE; i++) {
                        ram_page_t *ems = &(dev->ems[i]);
  
                        uint32_t old_phys_base = ems->phys_base & 0xffe00000;
                        uint32_t new_phys_base = (((val & masks[i]) >> shifts[i]) << 21);

                        if (new_phys_base != old_phys_base) {
                            int8_t old_enabled = ems->enabled;

                            if ((dev->regs[REG_RB7] & RB7_EMSEN) && old_enabled) {
                                ems->enabled = 0;
                                ems_recalc(dev, &(dev->ems[i]));
                            }

                            ems->phys_base = ems->phys_base - old_phys_base + new_phys_base;

                            if ((dev->regs[REG_RB7] & RB7_EMSEN) && old_enabled) {
                                ems->enabled = old_enabled;
                                ems_recalc(dev, &(dev->ems[i]));
                            }
                        }
                    }

                    neat_log("%08X, %08X, %08X, %08X\n",
                             dev->ems[0].phys_base, dev->ems[1].phys_base,
                             dev->ems[2].phys_base, dev->ems[3].phys_base);
                    break;

                case REG_RB12:
                    val &= RB12_MASK;
                    *reg = (*reg & ~RB12_MASK) | val;
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RB12=%02x(%02x)\n", val, *reg);
#endif
                    i = (val & RB12_EMSLEN) >> RB12_EMSLEN_SH;
                    switch (i) {
                        case 0: /* "less than 1MB" */
                            dev->ems_size = 512;
                            break;

                        case 1: /* 1 MB */
                        case 2: /* 2 MB */
                        case 3: /* 3 MB */
                        case 4: /* 4 MB */
                        case 5: /* 5 MB */
                        case 6: /* 6 MB */
                        case 7: /* 7 MB */
                            dev->ems_size = i << 10;
                            break;
                        default:
                            break;
                    }

                    if (dev->regs[REG_RB7] & RB7_EMSEN) {
                        remap_update(dev, dev->regs[REG_RB7]);

                        neat_log("NEAT: EMS %iKB\n",
                                 dev->ems_size);
                    }

                    mem_a20_key = val & RB12_GA20;
                    mem_a20_recalc();
                    break;

                default:
                    neat_log("NEAT: inv write to reg %02x (%02x)\n",
                             dev->indx, val);
                    break;
            }
            break;

        default:
            break;
    }
}

static uint8_t
neat_read(uint16_t port, void *priv)
{
    const neat_t *dev = (neat_t *) priv;
    uint8_t       ret = 0xff;

    switch (port) {
        case 0x22:
            ret = dev->indx;
            break;

        case 0x23:
            if ((dev->indx >= 0x60) && (dev->indx <= 0x6e))
                ret = dev->regs[dev->indx];
            else if (dev->indx == 0x6f)
                ret = (dev->regs[dev->indx] & 0xfd) | (mem_a20_key & 2);
            break;

        default:
            break;
    }

#if NEAT_DEBUG > 2
    neat_log("NEAT: read(%04x) = %02x\n", port, ret);
#endif

    return ret;
}

static void
neat_close(void *priv)
{
    neat_t *dev = (neat_t *) priv;

    free(dev);
}

static void *
neat_init(UNUSED(const device_t *info))
{
    neat_t *dev;
    uint8_t dram_mode = 0;
    uint8_t j;

    /* Create an instance. */
    dev = (neat_t *) calloc(1, sizeof(neat_t));

    /* Get configured I/O address. */
    j              = (dev->regs[REG_RB9] & RB9_BASE) >> RB9_BASE_SH;
    dev->ems_base  = 0x0208 + (0x10 * j);

    /* Get configured frame address. */
    j               = (dev->regs[REG_RB9] & RB9_FRAME) >> RB9_FRAME_SH;
    dev->ems_frame  = 0xc0000 + (EMS_PGSIZE * j);

    ems_update_virt_base(dev);

    dev->ems_pages  = (mem_size << 10) / EMS_PGSIZE;
    dev->remap_base = mem_size;

    mem_mapping_disable(&ram_mid_mapping);

    for (int i = 0; i < 24; i++) {
       if (i >= 20)
           neat_mem_update_state(dev, 0x000a0000 + (i * EMS_PGSIZE), EMS_PGSIZE, MEM_FLAG_ROMCS, MEM_FMASK_SHADOW);
       else {
           /* This is needed to actually trigger an update. */
           dev->mem_flags[i + 8] = MEM_FLAG_ROMCS;
           neat_mem_update_state(dev, 0x000a0000 + (i * EMS_PGSIZE), EMS_PGSIZE, 0x00, MEM_FMASK_SHADOW);
       }
    }

    /*
     * For each supported page (we can have a maximum of 4),
     * create, initialize and disable the mappings, and set
     * up the I/O control handler.
     */
    for (uint8_t i = 0; i < EMS_MAXPAGE; i++) {
        /* Create and initialize a page mapping. */
        mem_mapping_add(&dev->ems[i].mapping,
                        0x00000000, 0x00000000,
                        ems_readb, ems_readw, NULL,
                        ems_writeb, ems_writew, NULL,
                        ram + dev->ems[i].virt_base, MEM_MAPPING_INTERNAL,
                        &(dev->ems[i]));

        /* Disable for now. */
        mem_mapping_disable(&dev->ems[i].mapping);
    }

    for (uint8_t i = 0; i < 32; i++) {
        dev->shadow[i].virt_base = dev->shadow[i].phys_base =
            (i * EMS_PGSIZE) + 0x00080000;
        dev->shadow[i].enabled   = 1;

        /* Create and initialize a page mapping. */
        mem_mapping_add(&dev->shadow[i].mapping,
                        dev->shadow[i].virt_base, EMS_PGSIZE,
                        ems_readb, ems_readw, NULL,
                        ems_writeb, ems_writew, NULL,
                        ram + dev->shadow[i].virt_base, MEM_MAPPING_INTERNAL,
                        &(dev->shadow[i]));

        /* Disable for now. */
        mem_mapping_disable(&dev->shadow[i].mapping);
    }

    /* Initialize some of the registers to specific defaults. */
    for (uint8_t i = REG_RA0; i <= REG_RB12; i++) {
        dev->indx = i;
        neat_write(0x0023, defaults[i & REG_MASK], dev);
    }

    /*
     * Based on the value of mem_size, we have to set up
     * a proper DRAM configuration (so that EMS works.)
     *
     * TODO: We might also want to set 'valid' waitstate
     *       bits, based on our cpu speed.
     */
    switch (mem_size) {
        case 512: /* 512KB */
            /* 256K, 0, 0, 0 */
            dev->regs[REG_RB6] &= ~RB6_BANKS;               /* one bank */
            dev->regs[REG_RB6] |= (RTYPE_256K << RTYPE_SH); /* 256K */
            dev->regs[REG_RB8] &= ~RB8_BANKS;               /* one bank */
            dev->regs[REG_RB8] |= (RTYPE_NONE << RTYPE_SH); /* NONE */
            dram_mode = 2;
            break;

        case 640: /* 640KB */
            /* 256K, 64K, 0, 0 */
            dev->regs[REG_RB6] |= RB6_BANKS;                 /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_MIXED << RTYPE_SH); /* mixed */
            dev->regs[REG_RB8] &= ~RB8_BANKS;                /* one bank */
            dev->regs[REG_RB8] |= (RTYPE_NONE << RTYPE_SH);  /* NONE */
            dram_mode = 4;
            break;

        case 1024: /* 1MB */
            /* 256K, 256K, 0, 0 */
            dev->regs[REG_RB6] |= RB6_BANKS;                /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_256K << RTYPE_SH); /* 256K */
            dev->regs[REG_RB8] &= ~RB8_BANKS;               /* one bank */
            dev->regs[REG_RB8] |= (RTYPE_NONE << RTYPE_SH); /* NONE */
            dram_mode = 5;
            break;

        case 1536: /* 1.5MB */
            /* 256K, 256K, 256K, 0 */
            dev->regs[REG_RB6] |= RB6_BANKS;                /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_256K << RTYPE_SH); /* 256K */
            dev->regs[REG_RB8] &= ~RB8_BANKS;               /* one bank */
            dev->regs[REG_RB8] |= (RTYPE_256K << RTYPE_SH); /* 256K */
            dram_mode = 7;
            break;

        case 1664: /* 1.64MB */
            /* 256K, 64K, 256K, 256K */
            dev->regs[REG_RB6] |= RB6_BANKS;                 /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_MIXED << RTYPE_SH); /* mixed */
            dev->regs[REG_RB8] |= RB8_BANKS;                 /* two banks */
            dev->regs[REG_RB8] |= (RTYPE_256K << RTYPE_SH);  /* 256K */
            dram_mode = 10;
            break;

        case 2048: /* 2MB */
#if 1
            /* 256K, 256K, 256K, 256K */
            dev->regs[REG_RB6] |= RB6_BANKS;                /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_256K << RTYPE_SH); /* 256K */
            dev->regs[REG_RB8] |= RB8_BANKS;                /* two banks */
            dev->regs[REG_RB8] |= (RTYPE_256K << RTYPE_SH); /* 256K */
            dev->regs[REG_RB8] |= RB8_4WAY;                 /* 4way intl */
            dram_mode = 11;
#else
            /* 1M, 0, 0, 0 */
            dev->regs[REG_RB6] &= ~RB6_BANKS;               /* one bank */
            dev->regs[REG_RB6] |= (RTYPE_1M << RTYPE_SH);   /* 1M */
            dev->regs[REG_RB8] &= ~RB8_BANKS;               /* one bank */
            dev->regs[REG_RB8] |= (RTYPE_NONE << RTYPE_SH); /* NONE */
            dram_mode = 3;
#endif
            break;

        case 3072: /* 3MB */
            /* 256K, 256K, 1M, 0 */
            dev->regs[REG_RB6] |= RB6_BANKS;                /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_256K << RTYPE_SH); /* 256K */
            dev->regs[REG_RB8] &= ~RB8_BANKS;               /* one bank */
            dev->regs[REG_RB8] |= (RTYPE_1M << RTYPE_SH);   /* 1M */
            dram_mode = 8;
            break;

        case 4096: /* 4MB */
            /* 1M, 1M, 0, 0 */
            dev->regs[REG_RB6] |= RB6_BANKS;                /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_1M << RTYPE_SH);   /* 1M */
            dev->regs[REG_RB8] &= ~RB8_BANKS;               /* one bank */
            dev->regs[REG_RB8] |= (RTYPE_NONE << RTYPE_SH); /* NONE */
            dram_mode = 6;
            break;

        case 4224: /* 4.64MB */
            /* 256K, 64K, 1M, 1M */
            dev->regs[REG_RB6] |= RB6_BANKS;                 /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_MIXED << RTYPE_SH); /* mixed */
            dev->regs[REG_RB8] |= RB8_BANKS;                 /* two banks */
            dev->regs[REG_RB8] |= (RTYPE_1M << RTYPE_SH);    /* 1M */
            dram_mode = 12;
            break;

        case 5120: /* 5MB */
            /* 256K, 256K, 1M, 1M */
            dev->regs[REG_RB6] |= RB6_BANKS;                /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_256K << RTYPE_SH); /* 256K */
            dev->regs[REG_RB8] |= RB8_BANKS;                /* two banks */
            dev->regs[REG_RB8] |= (RTYPE_1M << RTYPE_SH);   /* 1M */
            dram_mode = 13;
            break;

        case 6144: /* 6MB */
            /* 1M, 1M, 1M, 0 */
            dev->regs[REG_RB6] |= RB6_BANKS;              /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_1M << RTYPE_SH); /* 1M */
            dev->regs[REG_RB8] &= ~RB8_BANKS;             /* one bank */
            dev->regs[REG_RB8] |= (RTYPE_1M << RTYPE_SH); /* 1M */
            dram_mode = 9;
            break;

        case 8192: /* 8MB */
            /* 1M, 1M, 1M, 1M */
            dev->regs[REG_RB6] |= RB6_BANKS;              /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_1M << RTYPE_SH); /* 1M */
            dev->regs[REG_RB8] |= RB8_BANKS;              /* two banks */
            dev->regs[REG_RB8] |= (RTYPE_1M << RTYPE_SH); /* 1M */
            dev->regs[REG_RB8] |= RB8_4WAY;               /* 4way intl */
            dram_mode = 14;
            break;

        default:
            neat_log("NEAT: **INVALID DRAM SIZE %iKB !**\n", mem_size);
    }
    if (dram_mode > 0) {
        neat_log("NEAT: using DRAM mode #%i (mem=%iKB)\n", dram_mode, mem_size);
    }

    /* Set up an I/O handler for the chipset. */
    io_sethandler(0x0022, 2,
                  neat_read, NULL, NULL, neat_write, NULL, NULL, dev);

    return dev;
}

const device_t neat_device = {
    .name          = "C&T CS8121 (NEAT)",
    .internal_name = "neat",
    .flags         = 0,
    .local         = 0,
    .init          = neat_init,
    .close         = neat_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
