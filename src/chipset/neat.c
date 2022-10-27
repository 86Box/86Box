/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of C&T CS8121 ("NEAT") 82C206/211/212/215 chipset.
 *
 * Note:	The datasheet mentions that the chipset supports up to 8MB
 *		of DRAM. This is intepreted as 'being able to refresh up to
 *		8MB of DRAM chips', because it works fine with bus-based
 *		memory expansion.
 *
 *
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2018 Fred N. van Kempen.
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
#include <86box/chipset.h>

#define NEAT_DEBUG  0

#define EMS_MAXPAGE 4
#define EMS_PGSIZE  16384

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

#define REG_RB11       0x6f /* Miscellaneous */
#define RB11_MASK      0xe6 /* 111R R11R */
#define RB11_GA20      0x02 /*  gate for A20 */
#define RB11_RASTMO    0x04 /*  enable RAS timeout counter */
#define RB11_EMSLEN    0xe0 /*  EMS memory chunk size */
#define RB11_EMSLEN_SH 5

typedef struct {
    int8_t        enabled; /* 1=ENABLED */
    char          pad;
    uint16_t      page;    /* selected page in EMS block */
    uint32_t      start;   /* start of EMS in RAM */
    uint8_t      *addr;    /* start addr in EMS RAM */
    mem_mapping_t mapping; /* mapping entry for page */
} emspage_t;

typedef struct {
    uint8_t regs[128]; /* all the CS8221 registers */
    uint8_t indx;      /* programmed index into registers */

    char pad;

    uint16_t ems_base, /* configured base address */
        ems_oldbase;
    uint32_t ems_frame, /* configured frame address */
        ems_oldframe;
    uint16_t ems_size,          /* EMS size in KB */
        ems_pages;              /* EMS size in pages */
    emspage_t ems[EMS_MAXPAGE]; /* EMS page registers */
} neat_t;

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
    neat_t *dev = (neat_t *) priv;
    uint8_t ret = 0xff;

    /* Grab the data. */
    ret = *(uint8_t *) (dev->ems[((addr & 0xffff) >> 14)].addr + (addr & 0x3fff));

    return (ret);
}

/* Read one word from paged RAM. */
static uint16_t
ems_readw(uint32_t addr, void *priv)
{
    neat_t  *dev = (neat_t *) priv;
    uint16_t ret = 0xffff;

    /* Grab the data. */
    ret = *(uint16_t *) (dev->ems[((addr & 0xffff) >> 14)].addr + (addr & 0x3fff));

    return (ret);
}

/* Write one byte to paged RAM. */
static void
ems_writeb(uint32_t addr, uint8_t val, void *priv)
{
    neat_t *dev = (neat_t *) priv;

    /* Write the data. */
    *(uint8_t *) (dev->ems[((addr & 0xffff) >> 14)].addr + (addr & 0x3fff)) = val;
}

/* Write one word to paged RAM. */
static void
ems_writew(uint32_t addr, uint16_t val, void *priv)
{
    neat_t *dev = (neat_t *) priv;

    /* Write the data. */
    *(uint16_t *) (dev->ems[((addr & 0xffff) >> 14)].addr + (addr & 0x3fff)) = val;
}

/* Re-calculate the active-page physical address. */
static void
ems_recalc(neat_t *dev, emspage_t *ems)
{
    if (ems->page >= dev->ems_pages) {
        /* That page does not exist. */
        ems->enabled = 0;
    }

    /* Pre-calculate the page address in EMS RAM. */
    ems->addr = ram + ems->start + (ems->page * EMS_PGSIZE);

    if (ems->enabled) {
        /* Update the EMS RAM address for this page. */
        mem_mapping_set_exec(&ems->mapping, ems->addr);

        /* Enable this page. */
        mem_mapping_enable(&ems->mapping);

#if NEAT_DEBUG > 1
        neat_log("NEAT EMS: page %d set to %08lx, %sabled)\n",
                 ems->page, ems->addr - ram, ems->enabled ? "en" : "dis");
#endif
    } else {
        /* Disable this page. */
        mem_mapping_disable(&ems->mapping);
    }
}

static void
ems_write(uint16_t port, uint8_t val, void *priv)
{
    neat_t    *dev = (neat_t *) priv;
    emspage_t *ems;
    int        vpage;

#if NEAT_DEBUG > 1
    neat_log("NEAT: ems_write(%04x, %02x)\n", port, val);
#endif

    /* Get the viewport page number. */
    vpage = (port / EMS_PGSIZE);
    ems   = &dev->ems[vpage];

    switch (port & 0x000f) {
        case 0x0008:
        case 0x0009:
            ems->enabled = !!(val & 0x80);
            ems->page &= 0x0180;       /* clear lower bits */
            ems->page |= (val & 0x7f); /* add new bits */
            ems_recalc(dev, ems);
            break;
    }
}

static uint8_t
ems_read(uint16_t port, void *priv)
{
    neat_t *dev = (neat_t *) priv;
    uint8_t ret = 0xff;
    int     vpage;

    /* Get the viewport page number. */
    vpage = (port / EMS_PGSIZE);

    switch (port & 0x000f) {
        case 0x0008: /* page number register */
            ret = dev->ems[vpage].page & 0x7f;
            if (dev->ems[vpage].enabled)
                ret |= 0x80;
            break;
    }

#if NEAT_DEBUG > 1
    neat_log("NEAT: ems_read(%04x) = %02x\n", port, ret);
#endif

    return (ret);
}

/* Initialize the EMS module. */
static void
ems_init(neat_t *dev, int en)
{
    int i;

    /* Remove if needed. */
    if (!en) {
        if (dev->ems_base > 0)
            for (i = 0; i < EMS_MAXPAGE; i++) {
                /* Disable for now. */
                mem_mapping_disable(&dev->ems[i].mapping);

                /* Remove I/O handler. */
                io_removehandler(dev->ems_base + (i * EMS_PGSIZE), 2,
                                 ems_read, NULL, NULL, ems_write, NULL, NULL, dev);
            }

#ifdef ENABLE_NEAT_LOG
        neat_log("NEAT: EMS disabled\n");
#endif

        return;
    }

    /* Get configured I/O address. */
    i             = (dev->regs[REG_RB9] & RB9_BASE) >> RB9_BASE_SH;
    dev->ems_base = 0x0208 + (0x10 * i);

    /* Get configured frame address. */
    i              = (dev->regs[REG_RB9] & RB9_FRAME) >> RB9_FRAME_SH;
    dev->ems_frame = 0xC0000 + (EMS_PGSIZE * i);

    /*
     * For each supported page (we can have a maximum of 4),
     * create, initialize and disable the mappings, and set
     * up the I/O control handler.
     */
    for (i = 0; i < EMS_MAXPAGE; i++) {
        /* Create and initialize a page mapping. */
        mem_mapping_add(&dev->ems[i].mapping,
                        dev->ems_frame + (EMS_PGSIZE * i), EMS_PGSIZE,
                        ems_readb, ems_readw, NULL,
                        ems_writeb, ems_writew, NULL,
                        ram, MEM_MAPPING_EXTERNAL,
                        dev);

        /* Disable for now. */
        mem_mapping_disable(&dev->ems[i].mapping);

        /* Set up an I/O port handler. */
        io_sethandler(dev->ems_base + (i * EMS_PGSIZE), 2,
                      ems_read, NULL, NULL, ems_write, NULL, NULL, dev);

        /*
         * TODO: update the 'high_mem' mapping to reflect that we now
         * have NN MB less extended memory available..
         */
    }

    neat_log("NEAT: EMS enabled, I/O=%04xH, Frame=%05XH\n",
             dev->ems_base, dev->ems_frame);
}

static void
neat_write(uint16_t port, uint8_t val, void *priv)
{
    neat_t *dev = (neat_t *) priv;
    uint8_t xval, *reg;
    int     i;

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
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RB1=%02x(%02x)\n", val, *reg);
#endif
                    break;

                case REG_RB2:
                    val &= RB2_MASK;
                    *reg = (*reg & ~RB2_MASK) | val;
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RB2=%02x(%02x)\n", val, *reg);
#endif
                    break;

                case REG_RB3:
                    val &= RB3_MASK;
                    *reg = (*reg & ~RB3_MASK) | val;
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RB3=%02x(%02x)\n", val, *reg);
#endif
                    break;

                case REG_RB4:
                    val &= RB4_MASK;
                    *reg = (*reg & ~RB4_MASK) | val;
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RB4=%02x(%02x)\n", val, *reg);
#endif
                    break;

                case REG_RB5:
                    val &= RB5_MASK;
                    *reg = (*reg & ~RB5_MASK) | val;
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
                    *reg = val;
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RB7=%02x(%02x)\n", val, *reg);
#endif
                    if (val & RB7_EMSEN)
                        ems_init(dev, 1);
                    else if (xval & RB7_EMSEN)
                        ems_init(dev, 0);

                    if (xval & RB7_UMAREL) {
                        if (val & RB7_UMAREL)
                            mem_remap_top(384);
                        else
                            mem_remap_top(0);
                    }
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
                    if (dev->regs[REG_RB7] & RB7_EMSEN) {
                        ems_init(dev, 0);
                        ems_init(dev, 1);
                    }
                    break;

                case REG_RB10:
                    val &= RB10_MASK;
                    *reg = (*reg & ~RB10_MASK) | val;
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RB10=%02x(%02x)\n", val, *reg);
#endif

                    dev->ems[3].start = ((val & RB10_P3EXT) >> RB10_P3EXT_SH) << 21;
                    dev->ems[2].start = ((val & RB10_P2EXT) >> RB10_P2EXT_SH) << 21;
                    dev->ems[1].start = ((val & RB10_P1EXT) >> RB10_P1EXT_SH) << 21;
                    dev->ems[0].start = ((val & RB10_P0EXT) >> RB10_P0EXT_SH) << 21;
                    for (i = 0; i < EMS_MAXPAGE; i++)
                        ems_recalc(dev, &dev->ems[i]);
                    break;

                case REG_RB11:
                    val &= RB11_MASK;
                    *reg = (*reg & ~RB11_MASK) | val;
#if NEAT_DEBUG > 1
                    neat_log("NEAT: RB11=%02x(%02x)\n", val, *reg);
#endif
                    i = (val & RB11_EMSLEN) >> RB11_EMSLEN_SH;
                    switch (i) {
                        case 0: /* "less than 2MB" */
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
                    }
                    dev->ems_pages = (dev->ems_size << 10) / EMS_PGSIZE;
                    if (dev->regs[REG_RB7] & RB7_EMSEN) {
                        neat_log("NEAT: EMS %iKB (%i pages)\n",
                                 dev->ems_size, dev->ems_pages);
                    }
                    break;

                default:
                    neat_log("NEAT: inv write to reg %02x (%02x)\n",
                             dev->indx, val);
                    break;
            }
            break;
    }
}

static uint8_t
neat_read(uint16_t port, void *priv)
{
    neat_t *dev = (neat_t *) priv;
    uint8_t ret = 0xff;

    switch (port) {
        case 0x22:
            ret = dev->indx;
            break;

        case 0x23:
            ret = dev->regs[dev->indx];
            break;

        default:
            break;
    }

#if NEAT_DEBUG > 2
    neat_log("NEAT: read(%04x) = %02x\n", port, ret);
#endif

    return (ret);
}

static void
neat_close(void *priv)
{
    neat_t *dev = (neat_t *) priv;

    free(dev);
}

static void *
neat_init(const device_t *info)
{
    neat_t *dev;
    int     i;

    /* Create an instance. */
    dev = (neat_t *) malloc(sizeof(neat_t));
    memset(dev, 0x00, sizeof(neat_t));

    /* Initialize some of the registers to specific defaults. */
    for (i = REG_RA0; i <= REG_RB11; i++) {
        dev->indx = i;
        neat_write(0x0023, 0x00, dev);
    }

    /*
     * Based on the value of mem_size, we have to set up
     * a proper DRAM configuration (so that EMS works.)
     *
     * TODO: We might also want to set 'valid' waitstate
     *       bits, based on our cpu speed.
     */
    i = 0;
    switch (mem_size) {
        case 512: /* 512KB */
            /* 256K, 0, 0, 0 */
            dev->regs[REG_RB6] &= ~RB6_BANKS;               /* one bank */
            dev->regs[REG_RB6] |= (RTYPE_256K << RTYPE_SH); /* 256K */
            dev->regs[REG_RB8] &= ~RB8_BANKS;               /* one bank */
            dev->regs[REG_RB8] |= (RTYPE_NONE << RTYPE_SH); /* NONE */
            i = 2;
            break;

        case 640: /* 640KB */
            /* 256K, 64K, 0, 0 */
            dev->regs[REG_RB6] |= RB6_BANKS;                 /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_MIXED << RTYPE_SH); /* mixed */
            dev->regs[REG_RB8] &= ~RB8_BANKS;                /* one bank */
            dev->regs[REG_RB8] |= (RTYPE_NONE << RTYPE_SH);  /* NONE */
            i = 4;
            break;

        case 1024: /* 1MB */
            /* 256K, 256K, 0, 0 */
            dev->regs[REG_RB6] |= RB6_BANKS;                /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_256K << RTYPE_SH); /* 256K */
            dev->regs[REG_RB8] &= ~RB8_BANKS;               /* one bank */
            dev->regs[REG_RB8] |= (RTYPE_NONE << RTYPE_SH); /* NONE */
            i = 5;
            break;

        case 1536: /* 1.5MB */
            /* 256K, 256K, 256K, 0 */
            dev->regs[REG_RB6] |= RB6_BANKS;                /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_256K << RTYPE_SH); /* 256K */
            dev->regs[REG_RB8] &= ~RB8_BANKS;               /* one bank */
            dev->regs[REG_RB8] |= (RTYPE_256K << RTYPE_SH); /* 256K */
            i = 7;
            break;

        case 1664: /* 1.64MB */
            /* 256K, 64K, 256K, 256K */
            dev->regs[REG_RB6] |= RB6_BANKS;                 /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_MIXED << RTYPE_SH); /* mixed */
            dev->regs[REG_RB8] |= RB8_BANKS;                 /* two banks */
            dev->regs[REG_RB8] |= (RTYPE_256K << RTYPE_SH);  /* 256K */
            i = 10;
            break;

        case 2048: /* 2MB */
#if 1
            /* 256K, 256K, 256K, 256K */
            dev->regs[REG_RB6] |= RB6_BANKS;                /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_256K << RTYPE_SH); /* 256K */
            dev->regs[REG_RB8] |= RB8_BANKS;                /* two banks */
            dev->regs[REG_RB8] |= (RTYPE_256K << RTYPE_SH); /* 256K */
            dev->regs[REG_RB8] |= RB8_4WAY;                 /* 4way intl */
            i = 11;
#else
            /* 1M, 0, 0, 0 */
            dev->regs[REG_RB6] &= ~RB6_BANKS;               /* one bank */
            dev->regs[REG_RB6] |= (RTYPE_1M << RTYPE_SH);   /* 1M */
            dev->regs[REG_RB8] &= ~RB8_BANKS;               /* one bank */
            dev->regs[REG_RB8] |= (RTYPE_NONE << RTYPE_SH); /* NONE */
            i = 3;
#endif
            break;

        case 3072: /* 3MB */
            /* 256K, 256K, 1M, 0 */
            dev->regs[REG_RB6] |= RB6_BANKS;                /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_256K << RTYPE_SH); /* 256K */
            dev->regs[REG_RB8] &= ~RB8_BANKS;               /* one bank */
            dev->regs[REG_RB8] |= (RTYPE_1M << RTYPE_SH);   /* 1M */
            i = 8;
            break;

        case 4096: /* 4MB */
            /* 1M, 1M, 0, 0 */
            dev->regs[REG_RB6] |= RB6_BANKS;                /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_1M << RTYPE_SH);   /* 1M */
            dev->regs[REG_RB8] &= ~RB8_BANKS;               /* one bank */
            dev->regs[REG_RB8] |= (RTYPE_NONE << RTYPE_SH); /* NONE */
            i = 6;
            break;

        case 4224: /* 4.64MB */
            /* 256K, 64K, 1M, 1M */
            dev->regs[REG_RB6] |= RB6_BANKS;                 /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_MIXED << RTYPE_SH); /* mixed */
            dev->regs[REG_RB8] |= RB8_BANKS;                 /* two banks */
            dev->regs[REG_RB8] |= (RTYPE_1M << RTYPE_SH);    /* 1M */
            i = 12;
            break;

        case 5120: /* 5MB */
            /* 256K, 256K, 1M, 1M */
            dev->regs[REG_RB6] |= RB6_BANKS;                /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_256K << RTYPE_SH); /* 256K */
            dev->regs[REG_RB8] |= RB8_BANKS;                /* two banks */
            dev->regs[REG_RB8] |= (RTYPE_1M << RTYPE_SH);   /* 1M */
            i = 13;
            break;

        case 6144: /* 6MB */
            /* 1M, 1M, 1M, 0 */
            dev->regs[REG_RB6] |= RB6_BANKS;              /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_1M << RTYPE_SH); /* 1M */
            dev->regs[REG_RB8] &= ~RB8_BANKS;             /* one bank */
            dev->regs[REG_RB8] |= (RTYPE_1M << RTYPE_SH); /* 1M */
            i = 9;
            break;

        case 8192: /* 8MB */
            /* 1M, 1M, 1M, 1M */
            dev->regs[REG_RB6] |= RB6_BANKS;              /* two banks */
            dev->regs[REG_RB6] |= (RTYPE_1M << RTYPE_SH); /* 1M */
            dev->regs[REG_RB8] |= RB8_BANKS;              /* two banks */
            dev->regs[REG_RB8] |= (RTYPE_1M << RTYPE_SH); /* 1M */
            dev->regs[REG_RB8] |= RB8_4WAY;               /* 4way intl */
            i = 14;
            break;

        default:
            neat_log("NEAT: **INVALID DRAM SIZE %iKB !**\n", mem_size);
    }
    if (i > 0) {
        neat_log("NEAT: using DRAM mode #%i (mem=%iKB)\n", i, mem_size);
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
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
