/*
 * VARCem   Virtual ARchaeological Computer EMulator.
 *          An emulator of (mostly) x86-based PC systems and devices,
 *          using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *          spanning the era between 1981 and 1995.
 *
 *          Implementation of a memory expansion board for the ISA Bus.
 *
 *          Although modern systems use direct-connect local buses to
 *          connect the CPU with its memory, originally the main system
 *          bus(es) were used for that. Memory expension cards could add
 *          memory to the system through the ISA bus, using a variety of
 *          techniques.
 *
 *          The majority of these boards could provide some (additional)
 *          conventional (low) memory, extended (high) memory on 80286
 *          and higher systems, as well as EMS bank-switched memory.
 *
 *          This implementation uses the LIM 3.2 specifications for EMS.
 *
 *          With the EMS method, the system's standard memory is expanded
 *          by means of bank-switching. One or more 'frames' in the upper
 *          memory area (640K-1024K) are used as viewports into an array
 *          of RAM pages numbered 0 to N. Each page is defined to be 16KB
 *          in size, so, for a 1024KB board, 64 such pages are available.
 *          I/O control registers are used to set up the mappings. More
 *          modern boards even have multiple 'copies' of those registers,
 *          which can be switched very fast, to allow for multitasking.
 *
 * TODO:    The EV-159 is supposed to support 16b EMS transfers, but the
 *          EMM.sys driver for it doesn't seem to want to do that..
 *
 *          EV-125 (It supports backfill)
 *              https://theretroweb.com/expansioncard/documentation/50250.pdf
 *
 *          EV-158 (RAM 10000)
 *              http://web.archive.org/web/19961104093221/http://www.everex.com/supp/techlib/memmem.html
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Jasmine Iwanek <jriwanek@gmail.com>
 *
 *          Copyright 2018      Fred N. van Kempen.
 *          Copyright 2022-2025 Jasmine Iwanek.
 *
 *          Redistribution and  use  in source  and binary forms, with
 *          or  without modification, are permitted  provided that the
 *          following conditions are met:
 *
 *          1. Redistributions of  source  code must retain the entire
 *             above notice, this list of conditions and the following
 *             disclaimer.
 *
 *          2. Redistributions in binary form must reproduce the above
 *             copyright  notice,  this list  of  conditions  and  the
 *             following disclaimer in  the documentation and/or other
 *             materials provided with the distribution.
 *
 *          3. Neither the  name of the copyright holder nor the names
 *             of  its  contributors may be used to endorse or promote
 *             products  derived from  this  software without specific
 *             prior written permission.
 *
 * THIS SOFTWARE  IS  PROVIDED BY THE  COPYRIGHT  HOLDERS AND CONTRIBUTORS
 * "AS IS" AND  ANY EXPRESS  OR  IMPLIED  WARRANTIES,  INCLUDING, BUT  NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE  ARE  DISCLAIMED. IN  NO  EVENT  SHALL THE COPYRIGHT
 * HOLDER OR  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL,  EXEMPLARY,  OR  CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE  GOODS OR SERVICES;  LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED  AND ON  ANY
 * THEORY OF  LIABILITY, WHETHER IN  CONTRACT, STRICT  LIABILITY, OR  TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING  IN ANY  WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/machine.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/ui.h>
#include <86box/plat.h>
#include <86box/isamem.h>

#include "cpu.h"

#define ISAMEM_IBMXT_CARD      0
#define ISAMEM_GENXT_CARD      1
#define ISAMEM_RAMCARD_CARD    2
#define ISAMEM_SYSTEMCARD_CARD 3
#define ISAMEM_IBMAT_128K_CARD 4
#define ISAMEM_IBMAT_CARD      5
#define ISAMEM_GENAT_CARD      6
#define ISAMEM_P5PAK_CARD      7
#define ISAMEM_A6PAK_CARD      8
#define ISAMEM_EMS5150_CARD    9
#define ISAMEM_EV159_CARD      10
#define ISAMEM_RAMPAGEXT_CARD  11
#define ISAMEM_ABOVEBOARD_CARD 12
#define ISAMEM_BRXT_CARD       13
#define ISAMEM_BRAT_CARD       14
#define ISAMEM_EV165A_CARD     15
#define ISAMEM_LOTECH_EMS_CARD 16

#define ISAMEM_DEBUG           0

#define RAM_TOPMEM             (640 << 10)  /* end of low memory */
#define RAM_UMAMEM             (384 << 10)  /* upper memory block */
#define RAM_EXTMEM             (1024 << 10) /* start of high memory */

#define EMS_MAXSIZE            (2048 << 10) /* max EMS memory size */
#define EMS_EV159_MAXSIZE      (3072 << 10) /* max EMS memory size for EV-159 cards */
#define EMS_LOTECH_MAXSIZE     (4096 << 10) /* max EMS memory size for lotech cards */
#define EMS_PGSIZE             (16 << 10)   /* one page is this big */
#define EMS_MAXPAGE            4            /* number of viewport pages */

#define EXTRAM_CONVENTIONAL    0
#define EXTRAM_HIGH            1
#define EXTRAM_XMS             2

typedef struct emsreg_t {
    int8_t        enabled; /* 1=ENABLED */
    uint8_t       page;    /* page# in EMS RAM */
    uint8_t       frame;   /* (varies with board) */
    char          pad;
    uint8_t      *addr;    /* start addr in EMS RAM */
    mem_mapping_t mapping; /* mapping entry for page */
    uint8_t      *ram;
    uint8_t      *frame_val;
    uint16_t     *ems_size;
    uint16_t     *ems_pages;
    uint32_t     *frame_addr;
} emsreg_t;

typedef struct ext_ram_t {
    uint32_t base;
    uint8_t *ptr;
} ext_ram_t;

typedef struct memdev_t {
    const char *name;
    uint8_t     board    : 6; /* board type */
    uint8_t     reserved : 2;

    uint8_t  flags;
#define FLAG_CONFIG 0x01 /* card is configured */
#define FLAG_WIDE   0x10 /* card uses 16b mode */
#define FLAG_FAST   0x20 /* fast (<= 120ns) chips */
#define FLAG_EMS    0x40 /* card has EMS mode enabled */

    uint8_t  frame_val[2];

    uint16_t total_size;    /* configured size in KB */
    uint16_t base_addr[2];  /* configured I/O address */

    uint32_t start_addr;    /* configured memory start */
    uint32_t frame_addr[2]; /* configured frame address */

    uint16_t ems_size[2];   /* EMS size in KB */
    uint16_t ems_pages[2];  /* EMS size in pages */
    uint32_t ems_start[2];  /* start of EMS in RAM */

    uint8_t *ram; /* allocated RAM buffer */

    ext_ram_t ext_ram[3]; /* structures for the mappings */

    mem_mapping_t low_mapping;  /* mapping for low mem */
    mem_mapping_t high_mapping; /* mapping for high mem */

    emsreg_t ems[EMS_MAXPAGE * 2]; /* EMS controller registers */
} memdev_t;

#ifdef ENABLE_ISAMEM_LOG
int isamem_do_log = ENABLE_ISAMEM_LOG;

static void
isamem_log(const char *fmt, ...)
{
    va_list ap;

    if (isamem_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define isamem_log(fmt, ...)
#endif

/* Why this convoluted setup with the mem_dev stuff when it's much simpler
   to just pass the exec pointer as p as well, and then just use that. */
/* Read one byte from onboard RAM. */
static uint8_t
ram_readb(uint32_t addr, void *priv)
{
    ext_ram_t *dev = (ext_ram_t *) priv;
    uint8_t    ret = 0xff;

    /* Grab the data. */
    ret = *(uint8_t *) (dev->ptr + (addr - dev->base));

    return ret;
}

/* Read one word from onboard RAM. */
static uint16_t
ram_readw(uint32_t addr, void *priv)
{
    ext_ram_t *dev = (ext_ram_t *) priv;
    uint16_t   ret = 0xffff;

    /* Grab the data. */
    ret = *(uint16_t *) (dev->ptr + (addr - dev->base));

    return ret;
}

/* Write one byte to onboard RAM. */
static void
ram_writeb(uint32_t addr, uint8_t val, void *priv)
{
    ext_ram_t *dev = (ext_ram_t *) priv;

    /* Write the data. */
    *(uint8_t *) (dev->ptr + (addr - dev->base)) = val;
}

/* Write one word to onboard RAM. */
static void
ram_writew(uint32_t addr, uint16_t val, void *priv)
{
    ext_ram_t *dev = (ext_ram_t *) priv;

    /* Write the data. */
    *(uint16_t *) (dev->ptr + (addr - dev->base)) = val;
}

/* Read one byte from onboard paged RAM. */
static uint8_t
ems_readb(uint32_t addr, void *priv)
{
    emsreg_t *dev = (emsreg_t *) priv;
    uint8_t   ret = 0xff;

    /* Grab the data. */
    ret = *(uint8_t *) (dev->addr + (addr & 0x3fff));
#if ISAMEM_DEBUG
    if ((addr % 4096) == 0)
        isamem_log("EMS readb(%06x) = %02x\n", addr & 0x3fff, ret);
#endif

    return ret;
}

/* Read one word from onboard paged RAM. */
static uint16_t
ems_readw(uint32_t addr, void *priv)
{
    emsreg_t *dev = (emsreg_t *) priv;
    uint16_t  ret = 0xffff;

    /* Grab the data. */
    ret = *(uint16_t *) (dev->addr + (addr & 0x3fff));
#if ISAMEM_DEBUG
    if ((addr % 4096) == 0)
        isamem_log("EMS readw(%06x) = %04x\n", addr & 0x3fff, ret);
#endif

    return ret;
}

/* Write one byte to onboard paged RAM. */
static void
ems_writeb(uint32_t addr, uint8_t val, void *priv)
{
    emsreg_t *dev = (emsreg_t *) priv;

    /* Write the data. */
#if ISAMEM_DEBUG
    if ((addr % 4096) == 0)
        isamem_log("EMS writeb(%06x, %02x)\n", addr & 0x3fff, val);
#endif
    *(uint8_t *) (dev->addr + (addr & 0x3fff)) = val;
}

/* Write one word to onboard paged RAM. */
static void
ems_writew(uint32_t addr, uint16_t val, void *priv)
{
    emsreg_t *dev = (emsreg_t *) priv;

    /* Write the data. */
#if ISAMEM_DEBUG
    if ((addr % 4096) == 0)
        isamem_log("EMS writew(%06x, %04x)\n", addr & 0x3fff, val);
#endif
    *(uint16_t *) (dev->addr + (addr & 0x3fff)) = val;
}

/* Handle a READ operation from one of our registers. */
static uint8_t
ems_in(uint16_t port, void *priv)
{
    const emsreg_t *dev   = (emsreg_t *) priv;
    uint8_t         ret   = 0xff;
    /* Get the viewport page number. */
#ifdef ENABLE_ISAMEM_LOG
    int             vpage = (port / EMS_PGSIZE);
#endif

    port &= (EMS_PGSIZE - 1);

    switch (port & 0x0001) {
        case 0x0000: /* page number register */
            ret = dev->page;
            if (dev->enabled)
                ret |= 0x80;
            break;

        case 0x0001: /* W/O */
            break;

        default:
            break;
    }

    isamem_log("ISAMEM: read(%04x) = %02x) page=%d\n", port, ret, vpage);

    return ret;
}

/* Handle a READ operation from one of our registers. */
static uint8_t
consecutive_ems_in(uint16_t port, void *priv)
{
    const memdev_t *dev   = (memdev_t *) priv;
    uint8_t         ret   = 0xff;
    /* Get the viewport page number. */
    int             vpage = (port - dev->base_addr[0]);

    ret = dev->ems[vpage].page;
    if (dev->ems[vpage].enabled)
        ret |= 0x80;

    isamem_log("ISAMEM: read(%04x) = %02x) page=%d\n", port, ret, vpage);

    return ret;
}

/* Handle a WRITE operation to one of our registers. */
static void
ems_out(uint16_t port, uint8_t val, void *priv)
{
    emsreg_t *dev   = (emsreg_t *) priv;
    /* Get the viewport page number. */
    int       vpage = (port / EMS_PGSIZE);

    port &= (EMS_PGSIZE - 1);

    switch (port & 0x0001) {
        case 0x0000: /* page mapping registers */
            /* Set the page number. */
            dev->enabled = (val & 0x80);
            dev->page    = (val & 0x7f);

            if (dev->enabled && (dev->page < *dev->ems_pages)) {
                /* Pre-calculate the page address in EMS RAM. */
                dev->addr = dev->ram + ((val & 0x7f) * EMS_PGSIZE);

                isamem_log("ISAMEM: map port %04X, page %i, starting at %08X: %08X -> %08X\n", port,
                           vpage, *dev->frame_addr,
                           *dev->frame_addr + (EMS_PGSIZE * (vpage & 3)), dev->addr - dev->ram);
                mem_mapping_set_addr(&dev->mapping, *dev->frame_addr + (EMS_PGSIZE * vpage), EMS_PGSIZE);

                /* Update the EMS RAM address for this page. */
                mem_mapping_set_exec(&dev->mapping, dev->addr);

                /* Enable this page. */
                mem_mapping_enable(&dev->mapping);
            } else {
                isamem_log("ISAMEM: map port %04X, page %i, starting at %08X: %08X -> N/A\n",
                           port, vpage, *dev->frame_addr, *dev->frame_addr + (EMS_PGSIZE * vpage));

                /* Disable this page. */
                mem_mapping_disable(&dev->mapping);
            }
            break;

        case 0x0001: /* page frame registers */
            /*
             * The EV-159 EMM driver configures the frame address
             * by setting bits in these registers. The information
             * in their manual is unclear, but here is what was
             * found out by repeatedly changing EMM's config:
             *
             * 08 04 00  Address
             * -----------------
             * 00 00 00  C4000
             * 00 00 80  C8000
             * 00 80 00  CC000
             * 00 80 80  D0000
             * 80 00 00  D4000
             * 80 00 80  D8000
             * 80 80 00  DC000
             * 80 80 80  E0000
             */
            dev->frame = val;
            *dev->frame_val = (*dev->frame_val & ~(1 << vpage)) | ((val >> 7) << vpage);
            *dev->frame_addr = 0x000c4000 + (*dev->frame_val << 14);
            isamem_log("ISAMEM: map port %04X page %i: frame_addr = %08X\n", port, vpage, *dev->frame_addr);
            /* Destroy the page registers. */
            for (uint8_t i = 0; i < 4; i ++) {
                isamem_log("    ");
                outb((port & 0x3ffe) + (i << 14), 0x00);
            }
            break;

        default:
            break;
    }
}

/* Handle a WRITE operation to one of our registers. */
static void
consecutive_ems_out(uint16_t port, uint8_t val, void *priv)
{
    memdev_t *dev   = (memdev_t *) priv;
    /* Get the viewport page number. */
    int       vpage = (port - dev->base_addr[0]);

    isamem_log("ISAMEM: write(%04x, %02x) to page mapping registers! (page=%d)\n", port, val, vpage);

    /* Set the page number. */
    dev->ems[vpage].enabled = 1;
    dev->ems[vpage].page    = val;

    /* Make sure we can do that.. */
    if (dev->flags & FLAG_CONFIG) {
        if (dev->ems[vpage].page < dev->ems_pages[0]) {
            /* Pre-calculate the page address in EMS RAM. */
            dev->ems[vpage].addr = dev->ram + dev->ems_start[0] + (val * EMS_PGSIZE);
        } else {
            /* That page does not exist. */
            dev->ems[vpage].enabled = 0;
        }

        if (dev->ems[vpage].enabled) {
            /* Update the EMS RAM address for this page. */
            mem_mapping_set_exec(&dev->ems[vpage].mapping,
                                 dev->ems[vpage].addr);

            /* Enable this page. */
            mem_mapping_enable(&dev->ems[vpage].mapping);
        } else {
            /* Disable this page. */
            mem_mapping_disable(&dev->ems[vpage].mapping);
        }
    }
}

/* Initialize the device for use. */
static void *
isamem_init(const device_t *info)
{
    memdev_t *dev;
    uint32_t  k;
    uint32_t  t;
    uint32_t  addr;
    uint32_t  tot;
    /* EMS 3.2 cannot have more than 2048KB per board. */
    uint32_t  ems_max = EMS_MAXSIZE;
    uint8_t  *ptr;

    /* Find our device and create an instance. */
    dev = (memdev_t *) calloc(1, sizeof(memdev_t));
    dev->name  = info->name;
    dev->board = info->local;

    dev->base_addr[1]  = 0x0000;
    dev->frame_addr[1] = 0x00000000;

    /* Do per-board initialization. */
    tot = 0;
    switch (dev->board) {
        case ISAMEM_IBMXT_CARD:      /* IBM PC/XT Memory Expansion Card */
        case ISAMEM_GENXT_CARD:      /* Generic PC/XT Memory Expansion Card */
        case ISAMEM_RAMCARD_CARD:    /* Microsoft RAMCard for IBM PC */
        case ISAMEM_SYSTEMCARD_CARD: /* Microsoft SystemCard */
        case ISAMEM_P5PAK_CARD:      /* Paradise Systems 5-PAK */
        case ISAMEM_A6PAK_CARD:      /* AST SixPakPlus */
            dev->total_size = device_get_config_int("size");
            dev->start_addr = device_get_config_int("start");
            tot             = dev->total_size;
            break;

        case ISAMEM_IBMAT_128K_CARD: /* IBM PC/AT 128K Memory Expansion Option */
            dev->total_size = 128;
            dev->start_addr = 512;
            tot             = dev->total_size;
            dev->flags |= FLAG_WIDE;
            break;

        case ISAMEM_IBMAT_CARD: /* IBM PC/AT Memory Expansion Card */
        case ISAMEM_GENAT_CARD: /* Generic PC/AT Memory Expansion Card */
            dev->total_size = device_get_config_int("size");
            dev->start_addr = device_get_config_int("start");
            tot             = dev->total_size;
            dev->flags |= FLAG_WIDE;
            break;

        case ISAMEM_EMS5150_CARD: /* Micro Mainframe EMS-5150(T) */
            dev->base_addr[0]  = device_get_config_hex16("base");
            dev->total_size    = device_get_config_int("size");
            dev->start_addr    = 0;
            dev->frame_addr[0] = 0xd0000;
            dev->flags        |= (FLAG_EMS | FLAG_CONFIG);
            break;

        case ISAMEM_EV159_CARD: /* Everex EV-159 RAM 3000 */
            /* The EV-159 cannot have more than 3072KB per board. */
            ems_max = EMS_EV159_MAXSIZE;
            dev->base_addr[0]  = device_get_config_hex16("base");
            dev->base_addr[1]  = device_get_config_hex16("base2");
            dev->total_size    = device_get_config_int("size");
            dev->start_addr    = device_get_config_int("start");
            tot                = device_get_config_int("length");
            if (!!device_get_config_int("width"))
                dev->flags    |= FLAG_WIDE;
            if (!!device_get_config_int("speed"))
                dev->flags    |= FLAG_FAST;
            if (!!device_get_config_int("ems"))
                dev->flags    |= FLAG_EMS;
            dev->frame_addr[0] = 0xd0000;
            dev->frame_addr[1] = 0xe0000;
            break;

        case ISAMEM_EV165A_CARD: /* Everex Maxi Magic EV-165A */
            dev->base_addr[0]  = device_get_config_hex16("base");
            dev->total_size    = device_get_config_int("size");
            dev->start_addr    = device_get_config_int("start");
            tot                = device_get_config_int("length");
            if (!!device_get_config_int("ems"))
                dev->flags    |= FLAG_EMS;
            dev->frame_addr[0] = 0xe0000;
            break;

        case ISAMEM_RAMPAGEXT_CARD:  /* AST RAMpage/XT */
            dev->base_addr[0]  = device_get_config_hex16("base");
            dev->total_size    = device_get_config_int("size");
            dev->start_addr    = device_get_config_int("start");
            tot                = dev->total_size;
            dev->flags        |= FLAG_EMS;
            dev->frame_addr[0] = 0xe0000;
            break;

        case ISAMEM_ABOVEBOARD_CARD: /* Intel AboveBoard */
        case ISAMEM_BRAT_CARD:       /* BocaRAM/AT */
            dev->base_addr[0]   = device_get_config_hex16("base");
            dev->total_size     = device_get_config_int("size");
            if (!!device_get_config_int("start"))
                dev->start_addr = device_get_config_int("start");
            dev->frame_addr[0]  = device_get_config_hex20("frame");
            dev->flags         |= FLAG_EMS;
            if (!!device_get_config_int("width"))
                dev->flags     |= FLAG_WIDE;
            if (!!device_get_config_int("speed"))
                dev->flags     |= FLAG_FAST;
            break;

        case ISAMEM_LOTECH_EMS_CARD: /* Lotech EMS */
            /* The Lotech EMS cannot have more than 4096KB per board. */
            ems_max = EMS_LOTECH_MAXSIZE;
            fallthrough;
        case ISAMEM_BRXT_CARD:   /* BocaRAM/XT */
            dev->base_addr[0]   = device_get_config_hex16("base");
            dev->total_size     = device_get_config_int("size");
            dev->start_addr     = 0;
            dev->frame_addr[0]  = device_get_config_hex20("frame");
            dev->flags         |= (FLAG_EMS | FLAG_CONFIG);
            break;

        default:
            break;
    }

    /* Fix up the memory start address. */
    dev->start_addr <<= 10;

    /* Say hello! */
    isamem_log("ISAMEM: %s (%iKB", info->name, dev->total_size);
    if (tot && (dev->total_size != tot))
        isamem_log(", %iKB for RAM", tot);
    if (dev->flags & FLAG_FAST)
        isamem_log(", FAST");
    if (dev->flags & FLAG_WIDE)
        isamem_log(", 16BIT");

    isamem_log(")\n");

    /* Force (back to) 8-bit bus if needed. */
    if ((!is286) && (dev->flags & FLAG_WIDE)) {
        isamem_log("ISAMEM: not AT+ system, forcing 8-bit mode!\n");
        dev->flags &= ~FLAG_WIDE;
    }

    /* Allocate and initialize our RAM. */
    k        = dev->total_size << 10;
    dev->ram = (uint8_t *) malloc(k);
    memset(dev->ram, 0x00, k);
    ptr = dev->ram;

    /*
     * The 'Memory Start Address' switch indicates at which address
     * we should start adding memory. No memory is added if it is
     * set to 0.
     */
    tot <<= 10;
    addr = dev->start_addr;
    if (addr > 0 && tot > 0) {
        /* Adjust K for the RAM we will use. */
        k -= tot;

        /*
         * First, see if we have to expand the conventional
         * (low) memory area. This can extend up to 640KB,
         * so check this first.
         */
        t = (addr < RAM_TOPMEM) ? RAM_TOPMEM - addr : 0;
        if (t > 0) {
            /*
             * We need T bytes to extend that area.
             *
             * If the board doesn't have that much, grab
             * as much as we can.
             */
            if (t > tot)
                t = tot;
            isamem_log("ISAMEM: RAM at %05iKB (%iKB)\n", addr >> 10, t >> 10);

            dev->ext_ram[EXTRAM_CONVENTIONAL].ptr  = ptr;
            dev->ext_ram[EXTRAM_CONVENTIONAL].base = addr;

            /* Create, initialize and enable the low-memory mapping. */
            mem_mapping_add(&dev->low_mapping, addr, t,
                            ram_readb,
                            (dev->flags & FLAG_WIDE) ? ram_readw : NULL,
                            NULL,
                            ram_writeb,
                            (dev->flags & FLAG_WIDE) ? ram_writew : NULL,
                            NULL,
                            ptr, MEM_MAPPING_EXTERNAL, &dev->ext_ram[EXTRAM_CONVENTIONAL]);

            /* Tell the memory system this is external RAM. */
            mem_set_mem_state(addr, t,
                              MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);

            /* Update pointers. */
            ptr += t;
            tot -= t;
            addr += t;
        }

        /* Skip to high memory if needed. */
        if ((addr == RAM_TOPMEM) && (tot >= RAM_UMAMEM)) {
            /*
             * We have more RAM available, but we are at the
             * top of conventional RAM. So, the next 384K are
             * skipped, and placed into different mappings so
             * they can be re-mapped later.
             */
            t = RAM_UMAMEM; /* 384KB */

            isamem_log("ISAMEM: RAM at %05iKB (%iKB)\n", addr >> 10, t >> 10);

            dev->ext_ram[EXTRAM_HIGH].ptr  = ptr;
            dev->ext_ram[EXTRAM_HIGH].base = addr + tot;

            /* Update and enable the remap. */
            mem_mapping_set(&ram_remapped_mapping,
                            addr + tot, t,
                            ram_readb, ram_readw, NULL,
                            ram_writeb, ram_writew, NULL,
                            ptr, MEM_MAPPING_EXTERNAL,
                            &dev->ext_ram[EXTRAM_HIGH]);
            mem_mapping_disable(&ram_remapped_mapping);

            /* Tell the memory system this is external RAM. */
            mem_set_mem_state(addr + tot, t,
                              MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);

            /* Update pointers. */
            ptr += t;
            tot -= t;
            addr += t;
        }
    }

    /*
     * Next, on systems that support it (80286 and up), we can add
     * (some of) our RAM to the system as Extended Memory, that is,
     * memory located above 1MB. This memory cannot be addressed in
     * real mode (so, not by DOS, for example) but it can be used in
     * protected mode.
     */
    if (is286 && addr > 0 && tot > 0) {
        t = tot;
        isamem_log("ISAMEM: RAM at %05iKB (%iKB)\n", addr >> 10, t >> 10);

        dev->ext_ram[EXTRAM_XMS].ptr  = ptr;
        dev->ext_ram[EXTRAM_XMS].base = addr;

        /* Create, initialize and enable the high-memory mapping. */
        mem_mapping_add(&dev->high_mapping, addr, t,
                        ram_readb, ram_readw, NULL,
                        ram_writeb, ram_writew, NULL,
                        ptr, MEM_MAPPING_EXTERNAL, &dev->ext_ram[EXTRAM_XMS]);

        /* Tell the memory system this is external RAM. */
        mem_set_mem_state(addr, t, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);

        /* Update pointers. */
        ptr += t;
        tot -= t;
        addr += t;
    }

    isa_mem_size += dev->total_size - (k >> 10);

    /* If EMS is enabled, use the remainder for EMS. */
    if (dev->flags & FLAG_EMS) {
        t = k;

        if (t > ems_max)
            t = ems_max;

        /* Set up where EMS begins in local RAM, and how much we have. */
        dev->ems_start[0]     = ptr - dev->ram;
        if ((dev->board == ISAMEM_EV159_CARD) && (t > (2 << 20))) {
            dev->ems_size[0]  = 2 << 10;
            dev->ems_pages[0] = (2 << 20) / EMS_PGSIZE;
        } else {
            dev->ems_size[0]  = t >> 10;
            dev->ems_pages[0] = t / EMS_PGSIZE;
        }
        isamem_log("ISAMEM: EMS #1 enabled, I/O=%04XH, %iKB (%i pages)",
                   dev->base_addr[0], dev->ems_size[0], dev->ems_pages[0]);
        if (dev->frame_addr[0] > 0)
            isamem_log(", Frame[0]=%05XH", dev->frame_addr[0]);

        isamem_log("\n");

        if ((dev->board == ISAMEM_EV159_CARD) && (t > (2 << 20))) {
            dev->ems_start[1] = dev->ems_start[0] + (2 << 20);
            dev->ems_size[1]  = (t - (2 << 20)) >> 10;
            dev->ems_pages[1] = (t - (2 << 20)) / EMS_PGSIZE;
            isamem_log("ISAMEM: EMS #2 enabled, I/O=%04XH, %iKB (%i pages)",
                       dev->base_addr[1], dev->ems_size[1], dev->ems_pages[1]);
            if (dev->frame_addr[1] > 0)
                isamem_log(", Frame[1]=%05XH", dev->frame_addr[1]);

            isamem_log("\n");
        }

        /*
         * For each supported page (we can have a maximum of 4),
         * create, initialize and disable the mappings, and set
         * up the I/O control handler.
         */
        for (uint8_t i = 0; i < EMS_MAXPAGE; i++) {
            dev->ems[i].ram        = dev->ram + dev->ems_start[0];
            dev->ems[i].frame_val  = &dev->frame_val[0];
            dev->ems[i].ems_size   = &dev->ems_size[0];
            dev->ems[i].ems_pages  = &dev->ems_pages[0];
            dev->ems[i].frame_addr = &dev->frame_addr[0];

            /* Create and initialize a page mapping. */
            mem_mapping_add(&dev->ems[i].mapping,
                            dev->frame_addr[0] + (EMS_PGSIZE * i), EMS_PGSIZE,
                            ems_readb,
                            (dev->flags & FLAG_WIDE) ? ems_readw : NULL,
                            NULL,
                            ems_writeb,
                            (dev->flags & FLAG_WIDE) ? ems_writew : NULL,
                            NULL,
                            ptr, MEM_MAPPING_EXTERNAL,
                            &(dev->ems[i]));

            /* For now, disable it. */
            mem_mapping_disable(&dev->ems[i].mapping);

            /* Set up an I/O port handler. */
            if (dev->board != ISAMEM_LOTECH_EMS_CARD)
                io_sethandler(dev->base_addr[0] + (EMS_PGSIZE * i), 2,
                              ems_in, NULL, NULL, ems_out, NULL, NULL, &(dev->ems[i]));

            if ((dev->board == ISAMEM_EV159_CARD) && (t > (2 << 20))) {
                dev->ems[i | 4].ram        = dev->ram + dev->ems_start[1];
                dev->ems[i | 4].frame_val  = &dev->frame_val[1];
                dev->ems[i | 4].ems_size   = &dev->ems_size[1];
                dev->ems[i | 4].ems_pages  = &dev->ems_pages[1];
                dev->ems[i | 4].frame_addr = &dev->frame_addr[1];

                /* Create and initialize a page mapping. */
                mem_mapping_add(&dev->ems[i | 4].mapping,
                                dev->frame_addr[1] + (EMS_PGSIZE * i), EMS_PGSIZE,
                                ems_readb,
                                (dev->flags & FLAG_WIDE) ? ems_readw : NULL,
                                NULL,
                                ems_writeb,
                                (dev->flags & FLAG_WIDE) ? ems_writew : NULL,
                                NULL,
                                ptr + (2 << 20), MEM_MAPPING_EXTERNAL,
                                &(dev->ems[i | 4]));

                /* For now, disable it. */
                mem_mapping_disable(&dev->ems[i | 4].mapping);

                io_sethandler(dev->base_addr[1] + (EMS_PGSIZE * i), 2,
                              ems_in, NULL, NULL, ems_out, NULL, NULL, &(dev->ems[i | 4]));
            }
        }

        if (dev->board == ISAMEM_LOTECH_EMS_CARD)
            io_sethandler(dev->base_addr[0], 4,
                          consecutive_ems_in, NULL, NULL, consecutive_ems_out, NULL, NULL, dev);
    }

    /* Let them know our device instance. */
    return ((void *) dev);
}

/* Remove the device from the system. */
static void
isamem_close(void *priv)
{
    memdev_t *dev = (memdev_t *) priv;

    if (dev->ram != NULL)
        free(dev->ram);

    free(dev);
}

static const device_config_t ibmxt_32k_config[] = {
  // clang-format off
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 32,
        .file_filter    = NULL,
        .spinner        = {
            .min  =  32,
            .max  = 576,
            .step =  32
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "start",
        .description    = "Start Address",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 64,
        .file_filter    = NULL,
        .spinner        = {
            .min  =   0,
            .max  = 608,
            .step =  32
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t ibmxt_32k_device = {
    .name          = "IBM PC/XT 32K Memory Expansion Option",
    .internal_name = "ibmxt_32k",
    .flags         = DEVICE_ISA,
    .local         = ISAMEM_IBMXT_CARD,
    .init          = isamem_init,
    .close         = isamem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ibmxt_32k_config
};

static const device_config_t ibmxt_64k_config[] = {
  // clang-format off
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 64,
        .file_filter    = NULL,
        .spinner        = {
            .min  =  64,
            .max  = 576,
            .step =  64
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "start",
        .description    = "Start Address",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 64,
        .file_filter    = NULL,
        .spinner        = {
            .min  =   0,
            .max  = 576,
            .step =  64
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t ibmxt_64k_device = {
    .name          = "IBM PC/XT 64K Memory Expansion Option",
    .internal_name = "ibmxt_64k",
    .flags         = DEVICE_ISA,
    .local         = ISAMEM_IBMXT_CARD,
    .init          = isamem_init,
    .close         = isamem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ibmxt_64k_config
};

static const device_config_t ibmxt_config[] = {
  // clang-format off
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 128,
        .file_filter    = NULL,
        .spinner        = {
            .min  =  64,
            .max  = 576,
            .step =  64
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "start",
        .description    = "Start Address",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 256,
        .file_filter    = NULL,
        .spinner        = {
            .min  =   0,
            .max  = 576,
            .step =  64
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t ibmxt_device = {
    .name          = "IBM PC/XT 64/256K Memory Expansion Option",
    .internal_name = "ibmxt",
    .flags         = DEVICE_ISA,
    .local         = ISAMEM_IBMXT_CARD,
    .init          = isamem_init,
    .close         = isamem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ibmxt_config
};

static const device_config_t genericxt_config[] = {
  // clang-format off
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 16,
        .file_filter    = NULL,
        .spinner        = {
            .min  =   0,
            .max  = 640,
            .step =  16
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "start",
        .description    = "Start Address",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = {
            .min  =   0,
            .max  = 640,
            .step =  16
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

// This also nicely accounts for the Everex EV-138
static const device_t genericxt_device = {
    .name          = "Generic PC/XT Memory Expansion",
    .internal_name = "genericxt",
    .flags         = DEVICE_ISA,
    .local         = ISAMEM_GENXT_CARD,
    .init          = isamem_init,
    .close         = isamem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = genericxt_config
};

static const device_config_t msramcard_config[] = {
  // clang-format off
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 64,
        .file_filter    = NULL,
        .spinner        = {
            .min  =   0,
            .max  = 256,
            .step =  64
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "start",
        .description    = "Start Address",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = {
            .min  =   0,
            .max  = 624,
            .step =  64
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t msramcard_device = {
    .name          = "Microsoft RAMCard",
    .internal_name = "msramcard",
    .flags         = DEVICE_ISA,
    .local         = ISAMEM_RAMCARD_CARD,
    .init          = isamem_init,
    .close         = isamem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = msramcard_config
};

static const device_config_t mssystemcard_config[] = {
  // clang-format off
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 64,
        .file_filter    = NULL,
        .spinner        = {
            .min  =   0,
            .max  = 256,
            .step =  64
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "start",
        .description    = "Start Address",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = {
            .min  =   0,
            .max  = 624,
            .step =  64
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t mssystemcard_device = {
    .name          = "Microsoft SystemCard",
    .internal_name = "mssystemcard",
    .flags         = DEVICE_ISA,
    .local         = ISAMEM_SYSTEMCARD_CARD,
    .init          = isamem_init,
    .close         = isamem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = mssystemcard_config
};

static const device_t ibmat_128k_device = {
    .name          = "IBM PC/AT 128KB Memory Expansion Option",
    .internal_name = "ibmat_128k",
    .flags         = DEVICE_ISA,
    .local         = ISAMEM_IBMAT_128K_CARD,
    .init          = isamem_init,
    .close         = isamem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

static const device_config_t ibmat_config[] = {
  // clang-format off
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 512,
        .file_filter    = NULL,
        .spinner        = {
            .min  =     0,
            .max  = 12288,
            .step =   512
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "start",
        .description    = "Start Address",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 1024,
        .file_filter    = NULL,
        .spinner        = {
            .min  =     0,
            .max  = 15872,
            .step =   512
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t ibmat_device = {
    .name          = "IBM PC/AT Memory Expansion",
    .internal_name = "ibmat",
    .flags         = DEVICE_ISA,
    .local         = ISAMEM_IBMAT_CARD,
    .init          = isamem_init,
    .close         = isamem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ibmat_config
};

static const device_config_t genericat_config[] = {
  // clang-format off
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 512,
        .file_filter    = NULL,
        .spinner        = {
            .min  =     0,
            .max  = 16384,
            .step =   128
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "start",
        .description    = "Start Address",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 1024,
        .file_filter    = NULL,
        .spinner        = {
            .min  =     0,
            .max  = 15872,
            .step =   128
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

// This also nicely accounts for the Everex EV-135
static const device_t genericat_device = {
    .name          = "Generic PC/AT Memory Expansion",
    .internal_name = "genericat",
    .flags         = DEVICE_ISA,
    .local         = ISAMEM_GENAT_CARD,
    .init          = isamem_init,
    .close         = isamem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = genericat_config
};

static const device_config_t p5pak_config[] = {
  // clang-format off
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 128,
        .file_filter    = NULL,
        .spinner        = {
            .min  =   0,
            .max  = 384,
            .step =  64
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "start",
        .description    = "Start Address",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 512,
        .file_filter    = NULL,
        .spinner        = {
            .min  =  64,
            .max  = 576,
            .step =  64
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t p5pak_device = {
    .name          = "Paradise Systems 5-PAK",
    .internal_name = "p5pak",
    .flags         = DEVICE_ISA,
    .local         = ISAMEM_P5PAK_CARD,
    .init          = isamem_init,
    .close         = isamem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = p5pak_config
};

static const device_config_t a6pak_config[] = {
  // clang-format off
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 64,
        .file_filter    = NULL,
        .spinner        = {
            .min  =   0,
            .max  = 384,
            .step =  64
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "start",
        .description    = "Start Address",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 256,
        .file_filter    = NULL,
        .spinner        = {
            .min  =  64,
            .max  = 512,
            .step =  64
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t a6pak_device = {
    .name          = "AST SixPakPlus",
    .internal_name = "a6pak",
    .flags         = DEVICE_ISA,
    .local         = ISAMEM_A6PAK_CARD,
    .init          = isamem_init,
    .close         = isamem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = a6pak_config
};

static const device_config_t ems5150_config[] = {
  // clang-format off
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 256,
        .file_filter    = NULL,
        .spinner        = {
            .min  =    0,
            .max  = 2048,
            .step =   64
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = 0x0000 },
            { .description = "Board 1",  .value = 0x0208 },
            { .description = "Board 2",  .value = 0x020a },
            { .description = "Board 3",  .value = 0x020c },
            { .description = "Board 4",  .value = 0x020e },
            { .description = ""                          }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t ems5150_device = {
    .name          = "Micro Mainframe EMS-5150(T)",
    .internal_name = "ems5150",
    .flags         = DEVICE_ISA,
    .local         = ISAMEM_EMS5150_CARD,
    .init          = isamem_init,
    .close         = isamem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ems5150_config
};

static const device_config_t ev159_config[] = {
  // clang-format off
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 512,
        .file_filter    = NULL,
        .spinner        = {
            .min  =    0,
            .max  = 3072,
            .step =  512
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "start",
        .description    = "Start Address",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = {
            .min  =     0,
            .max  = 16128,
            .step =   128
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "length",
        .description    = "Contiguous Size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = {
            .min  =     0,
            .max  = 16384,
            .step =   128
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "width",
        .description    = "I/O Width",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "8-bit",  .value = 0 },
            { .description = "16-bit", .value = 1 },
            { .description = ""                   }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "speed",
        .description    = "Transfer Speed",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Standard (150ns)",   .value = 0 },
            { .description = "High-Speed (120ns)", .value = 1 },
            { .description = ""                               }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "ems",
        .description    = "EMS mode",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = 0 },
            { .description = "Enabled",  .value = 1 },
            { .description = ""                     }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x0258,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "208H", .value = 0x0208 },
            { .description = "218H", .value = 0x0218 },
            { .description = "258H", .value = 0x0258 },
            { .description = "268H", .value = 0x0268 },
            { .description = "2A8H", .value = 0x02A8 },
            { .description = "2B8H", .value = 0x02B8 },
            { .description = "2E8H", .value = 0x02E8 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "base2",
        .description    = "Address for > 2 MB",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x0268,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "208H", .value = 0x0208 },
            { .description = "218H", .value = 0x0218 },
            { .description = "258H", .value = 0x0258 },
            { .description = "268H", .value = 0x0268 },
            { .description = "2A8H", .value = 0x02A8 },
            { .description = "2B8H", .value = 0x02B8 },
            { .description = "2E8H", .value = 0x02E8 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t ev159_device = {
    .name          = "Everex EV-159 RAM 3000 Deluxe",
    .internal_name = "ev159",
    .flags         = DEVICE_ISA,
    .local         = ISAMEM_EV159_CARD,
    .init          = isamem_init,
    .close         = isamem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ev159_config
};

static const device_config_t ev165a_config[] = {
  // clang-format off
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 256,
        .file_filter    = NULL,
        .spinner        = {
            .min  =    0,
            .max  = 2048,
            .step =  256
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "start",
        .description    = "Start Address",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 64,
        .file_filter    = NULL,
        .spinner        = {
            .min  =  64,
            .max  = 640,
            .step =  64
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name = "length",
        .description = "Contiguous Size",
        .type = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = {
            .min  =    0,
            .max  = 2048,
            .step =  256
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "ems",
        .description    = "EMS mode",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = 0 },
            { .description = "Enabled",  .value = 1 },
            { .description = ""                     }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x0258,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "208H", .value = 0x0208 },
            { .description = "218H", .value = 0x0218 },
            { .description = "258H", .value = 0x0258 },
            { .description = "268H", .value = 0x0268 },
            { .description = "2A8H", .value = 0x02A8 },
            { .description = "2B8H", .value = 0x02B8 },
            { .description = "2E8H", .value = 0x02E8 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t ev165a_device = {
    .name          = "Everex Maxi Magic EV-165A",
    .internal_name = "ev165a",
    .flags         = DEVICE_ISA,
    .local         = ISAMEM_EV165A_CARD,
    .init          = isamem_init,
    .close         = isamem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ev165a_config
};

static const device_config_t brxt_config[] = {
  // clang-format off
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x0268,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "208H", .value = 0x0208 },
            { .description = "218H", .value = 0x0218 },
            { .description = "258H", .value = 0x0258 },
            { .description = "268H", .value = 0x0268 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "frame",
        .description    = "Frame Address",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0xD0000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "D000H", .value = 0xD0000 },
            { .description = "E000H", .value = 0xE0000 },
            { .description = ""                        }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 512,
        .file_filter    = NULL,
        .spinner        = {
            .min  =    0,
            .max  = 2048,
            .step =  512
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t brxt_device = {
    .name          = "BocaRAM/XT",
    .internal_name = "brxt",
    .flags         = DEVICE_ISA,
    .local         = ISAMEM_BRXT_CARD,
    .init          = isamem_init,
    .close         = isamem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = brxt_config
};

#ifdef USE_ISAMEM_BRAT
static const device_config_t brat_config[] = {
  // clang-format off
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x0268,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "208H", .value = 0x0208 },
            { .description = "218H", .value = 0x0218 },
            { .description = "258H", .value = 0x0258 },
            { .description = "268H", .value = 0x0268 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "frame",
        .description    = "Frame Address",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0xD0000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "D000H",    .value = 0xD0000 },
            { .description = "E000H",    .value = 0xE0000 },
            { .description = ""                           }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "width",
        .description    = "I/O Width",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 8,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "8-bit",  .value =  8 },
            { .description = "16-bit", .value = 16 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "speed",
        .description    = "Transfer Speed",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Standard",   .value = 0 },
            { .description = "High-Speed", .value = 1 },
            { .description = ""                       }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 512,
        .file_filter    = NULL,
        .spinner        = {
            .min  =    0,
            .max  = 4096,
            .step =  512
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "start",
        .description    = "Start Address",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = {
            .min  =     0,
            .max  = 14336,
            .step =   512
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t brat_device = {
    .name          = "BocaRAM/AT",
    .internal_name = "brat",
    .flags         = DEVICE_ISA,
    .local         = ISAMEM_BRAT_CARD,
    .init          = isamem_init,
    .close         = isamem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = brat_config
};
#endif /* USE_ISAMEM_BRAT */

static const device_config_t lotech_config[] = {
// clang-format off
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x0260,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "260H", .value = 0x0260 },
            { .description = "264H", .value = 0x0264 },
            { .description = "268H", .value = 0x0268 },
            { .description = "26CH", .value = 0x026C },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "frame",
        .description    = "Frame Address",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0xe0000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "C000H",    .value = 0xC0000 },
            { .description = "D000H",    .value = 0xD0000 },
            { .description = "E000H",    .value = 0xE0000 },
            { .description = ""                           }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 2048,
        .file_filter    = NULL,
        .spinner        = {
            .min  =  512,
            .max  = 4096,
            .step =  512
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
// clang-format on
};

static const device_t lotech_ems_device = {
    .name          = "Lo-tech EMS Board",
    .internal_name = "lotechems",
    .flags         = DEVICE_ISA,
    .local         = ISAMEM_LOTECH_EMS_CARD,
    .init          = isamem_init,
    .close         = isamem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = lotech_config
};

#ifdef USE_ISAMEM_RAMPAGE
// TODO: Dual Paging support
// TODO: Conventional memory suppport
static const device_config_t rampage_config[] = {
  // clang-format off
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x0218,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "208H", .value = 0x0208 },
            { .description = "218H", .value = 0x0218 },
            { .description = "258H", .value = 0x0258 },
            { .description = "268H", .value = 0x0268 },
            { .description = "2A8H", .value = 0x02A8 },
            { .description = "2B8H", .value = 0x02B8 },
            { .description = "2E8H", .value = 0x02E8 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 256, /* Technically 128k, but banks 2-7 must be 256, headaches elsewise */
        .file_filter    = NULL,
        .spinner        = {
            .min  =  256,
            .max  = 2048,
            .step =  256
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "start",
        .description    = "Start Address",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 640,
        .file_filter    = NULL,
        .spinner        = {
            .min  =   0,
            .max  = 640,
            .step =  64
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t rampage_device = {
    .name          = "AST RAMpage/XT",
    .internal_name = "rampage",
    .flags         = DEVICE_ISA,
    .local         = ISAMEM_RAMPAGEXT_CARD,
    .init          = isamem_init,
    .close         = isamem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = rampage_config
};
#endif /* USE_ISAMEM_RAMPAGE */

#ifdef USE_ISAMEM_IAB
static const device_config_t iab_config[] = {
  // clang-format off
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x0258,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "208H", .value = 0x0208 },
            { .description = "218H", .value = 0x0218 },
            { .description = "258H", .value = 0x0258 },
            { .description = "268H", .value = 0x0268 },
            { .description = "2A8H", .value = 0x02A8 },
            { .description = "2B8H", .value = 0x02B8 },
            { .description = "2E8H", .value = 0x02E8 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "frame",
        .description    = "Frame Address",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = 0x00000 },
            { .description = "C000H",    .value = 0xC0000 },
            { .description = "D000H",    .value = 0xD0000 },
            { .description = "E000H",    .value = 0xE0000 },
            { .description = ""                           }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "width",
        .description    = "I/O Width",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 8,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "8-bit",  .value =  8 },
            { .description = "16-bit", .value = 16 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "speed",
        .description    = "Transfer Speed",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Standard",   .value = 0 },
            { .description = "High-Speed", .value = 1 },
            { .description = ""                       }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "size",
        .description    = "Memory size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 128,
        .file_filter    = NULL,
        .spinner        = {
            .min  =    0,
            .max  = 8192,
            .step =  128
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_t iab_device = {
    .name          = "Intel AboveBoard",
    .internal_name = "iab",
    .flags         = DEVICE_ISA,
    .local         = ISAMEM_ABOVEBOARD_CARD,
    .init          = isamem_init,
    .close         = isamem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = iab_config
};
#endif /* USE_ISAMEM_IAB */

static const struct {
    const device_t *dev;
} boards[] = {
    // clang-format off
    { &device_none         },
    // XT Ram Expansion Cards
    { &ibmxt_32k_device    },
    { &ibmxt_64k_device    },
    { &ibmxt_device        },
    { &genericxt_device    },
    { &msramcard_device    },
    { &mssystemcard_device },
    // AT RAM Expansion Cards
    { &ibmat_128k_device   },
    { &ibmat_device        },
    { &genericat_device    },
    // EMS Cards
    { &p5pak_device        },
    { &a6pak_device        },
    { &ems5150_device      },
    { &ev159_device        },
    { &ev165a_device       },
    { &brxt_device         },
#ifdef USE_ISAMEM_BRAT
    { &brat_device         },
#endif /* USE_ISAMEM_BRAT */
#ifdef USE_ISAMEM_RAMPAGE
    { &rampage_device      },
#endif /* USE_ISAMEM_RAMPAGE */
#ifdef USE_ISAMEM_IAB
    { &iab_device          },
#endif /* USE_ISAMEM_IAB */
    { &lotech_ems_device   },
    { NULL                 }
    // clang-format on
};

void
isamem_reset(void)
{
    int k;

    /* We explicitly set to zero here or bad things happen */
    isa_mem_size = 0;

    for (uint8_t i = 0; i < ISAMEM_MAX; i++) {
        k = isamem_type[i];
        if (k == 0)
            continue;

        /* Add the instance to the system. */
        device_add_inst(boards[k].dev, i + 1);
    }
}

const char *
isamem_get_name(int board)
{
    if (boards[board].dev == NULL)
        return (NULL);

    return (boards[board].dev->name);
}

const char *
isamem_get_internal_name(int board)
{
    return device_get_internal_name(boards[board].dev);
}

int
isamem_get_from_internal_name(const char *s)
{
    int c = 0;

    while (boards[c].dev != NULL) {
        if (!strcmp(boards[c].dev->internal_name, s))
            return c;
        c++;
    }

    /* Not found. */
    return 0;
}

const device_t *
isamem_get_device(int board)
{
    /* Add the instance to the system. */
    return boards[board].dev;
}

int
isamem_has_config(int board)
{
    if (boards[board].dev == NULL)
        return 0;

    return (boards[board].dev->config ? 1 : 0);
}
