/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of a memory expansion board for the ISA Bus.
 *
 *		Although modern systems use direct-connect local buses to
 *		connect the CPU with its memory, originally the main system
 *		bus(es) were used for that. Memory expension cards could add
 *		memory to the system through the ISA bus, using a variety of
 *		techniques.
 *
 *		The majority of these boards could provide some (additional)
 *		conventional (low) memory, extended (high) memory on 80286
 *		and higher systems, as well as EMS bank-switched memory.
 *
 *		This implementation uses the LIM 3.2 specifications for EMS.
 *
 *		With the EMS method, the system's standard memory is expanded
 *		by means of bank-switching. One or more 'frames' in the upper
 *		memory area (640K-1024K) are used as viewports into an array
 *		of RAM pages numbered 0 to N. Each page is defined to be 16KB
 *		in size, so, for a 1024KB board, 64 such pages are available.
 *		I/O control registers are used to set up the mappings. More
 *		modern boards even have multiple 'copies' of those registers,
 *		which can be switched very fast, to allow for multitasking.
 *
 * TODO:	The EV159 is supposed to support 16b EMS transfers, but the
 *		EMM.sys driver for it doesn't seem to want to do that..
 *
 *
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2018 Fred N. van Kempen.
 *
 *		Redistribution and  use  in source  and binary forms, with
 *		or  without modification, are permitted  provided that the
 *		following conditions are met:
 *
 *		1. Redistributions of  source  code must retain the entire
 *		   above notice, this list of conditions and the following
 *		   disclaimer.
 *
 *		2. Redistributions in binary form must reproduce the above
 *		   copyright  notice,  this list  of  conditions  and  the
 *		   following disclaimer in  the documentation and/or other
 *		   materials provided with the distribution.
 *
 *		3. Neither the  name of the copyright holder nor the names
 *		   of  its  contributors may be used to endorse or promote
 *		   products  derived from  this  software without specific
 *		   prior written permission.
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

#define ISAMEM_IBMXT_CARD 0
#define ISAMEM_GENXT_CARD 1
#define ISAMEM_RAMCARD_CARD 2
#define ISAMEM_SYSTEMCARD_CARD 3
#define ISAMEM_IBMAT_CARD 4
#define ISAMEM_GENAT_CARD 5
#define ISAMEM_P5PAK_CARD 6
#define ISAMEM_A6PAK_CARD 7
#define ISAMEM_EMS5150_CARD 8
#define ISAMEM_EV159_CARD 10
#define ISAMEM_RAMPAGEXT_CARD 11
#define ISAMEM_ABOVEBOARD_CARD 12
#define ISAMEM_BRAT_CARD 13

#define ISAMEM_DEBUG	0

#define RAM_TOPMEM	(640 << 10)		/* end of low memory */
#define RAM_UMAMEM	(384 << 10)		/* upper memory block */
#define RAM_EXTMEM	(1024 << 10)		/* start of high memory */

#define EMS_MAXSIZE	(2048 << 10)		/* max EMS memory size */
#define EMS_PGSIZE	(16 << 10)		/* one page is this big */
#define EMS_MAXPAGE	4			/* number of viewport pages */

#define EXTRAM_CONVENTIONAL 0
#define EXTRAM_HIGH 1
#define EXTRAM_XMS 2

typedef struct {
    int8_t	enabled;			/* 1=ENABLED */
    uint8_t	page;				/* page# in EMS RAM */
    uint8_t	frame;				/* (varies with board) */
    char	pad;
    uint8_t	*addr;				/* start addr in EMS RAM */
    mem_mapping_t mapping;			/* mapping entry for page */
} emsreg_t;

typedef struct {
    uint32_t	base;
    uint8_t	*ptr;
} ext_ram_t;

typedef struct {
    const char	*name;
    uint8_t	board    : 6,			/* board type */
		reserved : 2;

    uint8_t	flags;
#define FLAG_CONFIG	0x01			/* card is configured */
#define FLAG_WIDE	0x10			/* card uses 16b mode */
#define FLAG_FAST	0x20			/* fast (<= 120ns) chips */
#define FLAG_EMS	0x40			/* card has EMS mode enabled */

    uint16_t	total_size;			/* configured size in KB */
    uint32_t	base_addr,			/* configured I/O address */
		start_addr,			/* configured memory start */
		frame_addr;			/* configured frame address */

    uint16_t	ems_size,			/* EMS size in KB */
		ems_pages;			/* EMS size in pages */
    uint32_t	ems_start;			/* start of EMS in RAM */

    uint8_t	*ram;				/* allocated RAM buffer */

    ext_ram_t	  ext_ram[3];			/* structures for the mappings */

    mem_mapping_t low_mapping;			/* mapping for low mem */
    mem_mapping_t high_mapping;			/* mapping for high mem */

    emsreg_t	ems[EMS_MAXPAGE];		/* EMS controller registers */
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
#define isamem_log(fmt, ...)
#endif


/* Why this convoluted setup with the mem_dev stuff when it's much simpler
   to just pass the exec pointer as p as well, and then just use that. */
/* Read one byte from onboard RAM. */
static uint8_t
ram_readb(uint32_t addr, void *priv)
{
    ext_ram_t *dev = (ext_ram_t *)priv;
    uint8_t ret = 0xff;

    /* Grab the data. */
    ret = *(uint8_t *)(dev->ptr + (addr - dev->base));

    return(ret);
}


/* Read one word from onboard RAM. */
static uint16_t
ram_readw(uint32_t addr, void *priv)
{
    ext_ram_t *dev = (ext_ram_t *)priv;
    uint16_t ret = 0xffff;

    /* Grab the data. */
    ret = *(uint16_t *)(dev->ptr + (addr - dev->base));

    return(ret);
}


/* Write one byte to onboard RAM. */
static void
ram_writeb(uint32_t addr, uint8_t val, void *priv)
{
    ext_ram_t *dev = (ext_ram_t *)priv;

    /* Write the data. */
    *(uint8_t *)(dev->ptr + (addr - dev->base)) = val;
}


/* Write one word to onboard RAM. */
static void
ram_writew(uint32_t addr, uint16_t val, void *priv)
{
    ext_ram_t *dev = (ext_ram_t *)priv;

    /* Write the data. */
    *(uint16_t *)(dev->ptr + (addr - dev->base)) = val;
}


/* Read one byte from onboard paged RAM. */
static uint8_t
ems_readb(uint32_t addr, void *priv)
{
    memdev_t *dev = (memdev_t *)priv;
    uint8_t ret = 0xff;

    /* Grab the data. */
    ret = *(uint8_t *)(dev->ems[((addr & 0xffff) >> 14)].addr + (addr & 0x3fff));
#if ISAMEM_DEBUG
    if ((addr % 4096)==0) isamem_log("EMS readb(%06x) = %02x\n",addr-dev&0x3fff,ret);
#endif

    return(ret);
}


/* Read one word from onboard paged RAM. */
static uint16_t
ems_readw(uint32_t addr, void *priv)
{
    memdev_t *dev = (memdev_t *)priv;
    uint16_t ret = 0xffff;

    /* Grab the data. */
    ret = *(uint16_t *)(dev->ems[((addr & 0xffff) >> 14)].addr + (addr & 0x3fff));
#if ISAMEM_DEBUG
    if ((addr % 4096)==0) isamem_log("EMS readw(%06x) = %04x\n",addr-dev&0x3fff,ret);
#endif

    return(ret);
}


/* Write one byte to onboard paged RAM. */
static void
ems_writeb(uint32_t addr, uint8_t val, void *priv)
{
    memdev_t *dev = (memdev_t *)priv;

    /* Write the data. */
#if ISAMEM_DEBUG
    if ((addr % 4096)==0) isamem_log("EMS writeb(%06x, %02x)\n",addr-dev&0x3fff,val);
#endif
    *(uint8_t *)(dev->ems[((addr & 0xffff) >> 14)].addr + (addr & 0x3fff)) = val;
}


/* Write one word to onboard paged RAM. */
static void
ems_writew(uint32_t addr, uint16_t val, void *priv)
{
    memdev_t *dev = (memdev_t *)priv;

    /* Write the data. */
#if ISAMEM_DEBUG
    if ((addr % 4096)==0) isamem_log("EMS writew(%06x, %04x)\n",addr&0x3fff,val);
#endif
    *(uint16_t *)(dev->ems[((addr & 0xffff) >> 14)].addr + (addr & 0x3fff)) = val;
}


/* Handle a READ operation from one of our registers. */
static uint8_t
ems_read(uint16_t port, void *priv)
{
    memdev_t *dev = (memdev_t *)priv;
    uint8_t ret = 0xff;
    int vpage;

    /* Get the viewport page number. */
    vpage = (port / EMS_PGSIZE);
    port &= (EMS_PGSIZE - 1);

    switch(port - dev->base_addr) {
	case 0x0000:		/* page number register */
		ret = dev->ems[vpage].page;
		if (dev->ems[vpage].enabled)
			ret |= 0x80;
		break;

	case 0x0001:		/* W/O */
		break;
    }

#if ISAMEM_DEBUG
    isamem_log("ISAMEM: read(%04x) = %02x)\n", port, ret);
#endif

    return(ret);
}


/* Handle a WRITE operation to one of our registers. */
static void
ems_write(uint16_t port, uint8_t val, void *priv)
{
    memdev_t *dev = (memdev_t *)priv;
    int vpage;

    /* Get the viewport page number. */
    vpage = (port / EMS_PGSIZE);
    port &= (EMS_PGSIZE - 1);

#if ISAMEM_DEBUG
    isamem_log("ISAMEM: write(%04x, %02x) page=%d\n", port, val, vpage);
#endif

    switch(port - dev->base_addr) {
	case 0x0000:		/* page mapping registers */
		/* Set the page number. */
		dev->ems[vpage].enabled = (val & 0x80);
		dev->ems[vpage].page = (val & 0x7f);

		/* Make sure we can do that.. */
		if (dev->flags & FLAG_CONFIG) {
			if (dev->ems[vpage].page < dev->ems_pages) {
				/* Pre-calculate the page address in EMS RAM. */
				dev->ems[vpage].addr = dev->ram + dev->ems_start + ((val & 0x7f) * EMS_PGSIZE);
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
		break;

	case 0x0001:		/* page frame registers */
		/*
		 * The EV-159 EMM driver configures the frame address
		 * by setting bits in these registers. The information
		 * in their manual is unclear, but here is what was
		 * found out by repeatedly changing EMM's config:
		 *
		 * 00 04 08  Address
		 * -----------------
		 * 80 c0 e0  C0000
		 * 80 c0 e0  C4000
		 * 80 c0 e0  C8000
		 * 80 c0 e0  CC000
		 * 80 c0 e0  D0000
		 * 80 c0 e0  D4000
		 * 80 c0 e0  D8000
		 * 80 c0 e0  DC000
		 * 80 c0 e0  E0000
                 */
isamem_log("EMS: write(%02x) to register 1 !\n");
		dev->ems[vpage].frame = val;
		if (val)
			dev->flags |= FLAG_CONFIG;
		break;
    }
}


/* Initialize the device for use. */
static void *
isamem_init(const device_t *info)
{
    memdev_t *dev;
    uint32_t k, t;
    uint32_t addr;
    uint32_t tot;
    uint8_t *ptr;
    int i;

    /* Find our device and create an instance. */
    dev = (memdev_t *)malloc(sizeof(memdev_t));
    memset(dev, 0x00, sizeof(memdev_t));
    dev->name = info->name;
    dev->board = info->local;

    /* Do per-board initialization. */
    tot = 0;
    switch(dev->board) {
	case ISAMEM_IBMXT_CARD:		/* IBM PC/XT Memory Expansion Card */
	case ISAMEM_GENXT_CARD:		/* Generic PC/XT Memory Expansion Card */
	case ISAMEM_RAMCARD_CARD:	/* Microsoft RAMCard for IBM PC */
	case ISAMEM_SYSTEMCARD_CARD:	/* Microsoft SystemCard */
	case ISAMEM_P5PAK_CARD:		/* Paradise Systems 5-PAK */
	case ISAMEM_A6PAK_CARD:		/* AST SixPakPlus */
		dev->total_size = device_get_config_int("size");
		dev->start_addr = device_get_config_int("start");
		tot = dev->total_size;
		break;

	case ISAMEM_IBMAT_CARD:		/* IBM PC/AT Memory Expansion Card */
	case ISAMEM_GENAT_CARD:		/* Generic PC/AT Memory Expansion Card */
		dev->total_size = device_get_config_int("size");
		dev->start_addr = device_get_config_int("start");
		tot = dev->total_size;
		dev->flags |= FLAG_WIDE;
		break;

	case ISAMEM_EMS5150_CARD:		/* Micro Mainframe EMS-5150(T) */
		dev->base_addr = device_get_config_hex16("base");
		dev->total_size = device_get_config_int("size");
		dev->frame_addr = 0xD0000;
		dev->flags |= (FLAG_EMS | FLAG_CONFIG);
		break;

	case ISAMEM_EV159_CARD:	/* Everex EV-159 RAM 3000 */
		dev->base_addr = device_get_config_hex16("base");
		dev->total_size = device_get_config_int("size");
		dev->start_addr = device_get_config_int("start");
		tot = device_get_config_int("length");
		if (!!device_get_config_int("width"))
			dev->flags |= FLAG_WIDE;
		if (!!device_get_config_int("speed"))
			dev->flags |= FLAG_FAST;
		if (!!device_get_config_int("ems"))
			dev->flags |= FLAG_EMS;
dev->frame_addr = 0xE0000;
		break;

	case ISAMEM_RAMPAGEXT_CARD: /* AST RAMpage/XT */
	case ISAMEM_ABOVEBOARD_CARD: /* Intel AboveBoard */
	case ISAMEM_BRAT_CARD: /* BocaRAM/AT */
		dev->base_addr = device_get_config_hex16("base");
		dev->total_size = device_get_config_int("size");
		dev->start_addr = device_get_config_int("start");
		dev->frame_addr = device_get_config_hex20("frame");
		if (!!device_get_config_int("width"))
			dev->flags |= FLAG_WIDE;
		if (!!device_get_config_int("speed"))
			dev->flags |= FLAG_FAST;
		break;
    }

    /* Fix up the memory start address. */
    dev->start_addr <<= 10;

    /* Say hello! */
    isamem_log("ISAMEM: %s (%iKB", info->name, dev->total_size);
    if (tot && (dev->total_size != tot))
	isamem_log(", %iKB for RAM", tot);
    if (dev->flags & FLAG_FAST) isamem_log(", FAST");
    if (dev->flags & FLAG_WIDE) isamem_log(", 16BIT");
    isamem_log(")\n");

    /* Force (back to) 8-bit bus if needed. */
    if ((!is286) && (dev->flags & FLAG_WIDE)) {
	isamem_log("ISAMEM: not AT+ system, forcing 8-bit mode!\n");
	dev->flags &= ~FLAG_WIDE;
    }

    /* Allocate and initialize our RAM. */
    k = dev->total_size << 10;
    dev->ram = (uint8_t *)malloc(k);
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
		isamem_log("ISAMEM: RAM at %05iKB (%iKB)\n", addr>>10, t>>10);

		dev->ext_ram[EXTRAM_CONVENTIONAL].ptr = ptr;
		dev->ext_ram[EXTRAM_CONVENTIONAL].base = addr;

		/* Create, initialize and enable the low-memory mapping. */
		mem_mapping_add(&dev->low_mapping, addr, t,
				ram_readb,
				(dev->flags&FLAG_WIDE) ? ram_readw : NULL,
				NULL,
				ram_writeb,
				(dev->flags&FLAG_WIDE) ? ram_writew : NULL,
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
		t = RAM_UMAMEM;		/* 384KB */

		isamem_log("ISAMEM: RAM at %05iKB (%iKB)\n", addr>>10, t>>10);

		dev->ext_ram[EXTRAM_HIGH].ptr = ptr;
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
	isamem_log("ISAMEM: RAM at %05iKB (%iKB)\n", addr>>10, t>>10);

	dev->ext_ram[EXTRAM_XMS].ptr = ptr;
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
	/* EMS 3.2 cannot have more than 2048KB per board. */
	t = k;
	if (t > EMS_MAXSIZE)
		t = EMS_MAXSIZE;

	/* Set up where EMS begins in local RAM, and how much we have. */
	dev->ems_start = ptr - dev->ram;
	dev->ems_size = t >> 10;
	dev->ems_pages = t / EMS_PGSIZE;
	isamem_log("ISAMEM: EMS enabled, I/O=%04XH, %iKB (%i pages)",
		dev->base_addr, dev->ems_size, dev->ems_pages);
	if (dev->frame_addr > 0)
		isamem_log(", Frame=%05XH", dev->frame_addr);
	isamem_log("\n");

	/*
	 * For each supported page (we can have a maximum of 4),
	 * create, initialize and disable the mappings, and set
	 * up the I/O control handler.
	 */
	for (i = 0; i < EMS_MAXPAGE; i++) {
		/* Create and initialize a page mapping. */
		mem_mapping_add(&dev->ems[i].mapping,
				dev->frame_addr + (EMS_PGSIZE*i), EMS_PGSIZE,
				ems_readb,
				(dev->flags&FLAG_WIDE) ? ems_readw : NULL,
				NULL,
				ems_writeb,
				(dev->flags&FLAG_WIDE) ? ems_writew : NULL,
				NULL,
				ptr, MEM_MAPPING_EXTERNAL,
				dev);

		/* For now, disable it. */
		mem_mapping_disable(&dev->ems[i].mapping);

		/* Set up an I/O port handler. */
		io_sethandler(dev->base_addr + (EMS_PGSIZE*i), 2,
			      ems_read,NULL,NULL, ems_write,NULL,NULL, dev);
	}
    }

    /* Let them know our device instance. */
    return((void *) dev);
}


/* Remove the device from the system. */
static void
isamem_close(void *priv)
{
    memdev_t *dev = (memdev_t *)priv;
    int i;

    if (dev->flags & FLAG_EMS) {
	for (i = 0; i < EMS_MAXPAGE; i++) {
		io_removehandler(dev->base_addr + (EMS_PGSIZE*i), 2,
				 ems_read,NULL,NULL, ems_write,NULL,NULL, dev);

	}
    }

    if (dev->ram != NULL)
	free(dev->ram);

    free(dev);
}

static const device_config_t ibmxt_config[] = {
// clang-format off
    {
        "size", "Memory Size", CONFIG_SPINNER, "", 128, "",
        { 0, 512, 16 },
        { { 0 } }
    },
    {
        "start", "Start Address", CONFIG_SPINNER, "", 256, "",
        { 0, 576, 64 },
        { { 0 } }
    },
    { "", "", -1 }
// clang-format on
};

static const device_t ibmxt_device = {
    .name = "IBM PC/XT Memory Expansion",
    .internal_name = "ibmxt",
    .flags = DEVICE_ISA,
    .local = ISAMEM_IBMXT_CARD,
    .init = isamem_init,
    .close = isamem_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = ibmxt_config
};

static const device_config_t genericxt_config[] = {
// clang-format off
    {
        "size", "Memory Size", CONFIG_SPINNER, "", 16, "",
        { 0, 640, 16 },
        { { 0 } }
    },
    {
        "start", "Start Address", CONFIG_SPINNER, "", 0, "",
        { 0, 624, 16 },
        { { 0 } }
    },
    { "", "", -1 }
// clang-format on
};

static const device_t genericxt_device = {
    .name = "Generic PC/XT Memory Expansion",
    .internal_name = "genericxt",
    .flags = DEVICE_ISA,
    .local = ISAMEM_GENXT_CARD,
    .init = isamem_init,
    .close = isamem_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = genericxt_config
};

static const device_config_t msramcard_config[] = {
// clang-format off
    {
        "size", "Memory Size", CONFIG_SPINNER, "", 64, "",
        { 0, 256, 64 },
        { { 0 } }
    },
    {
        "start", "Start Address", CONFIG_SPINNER, "", 0, "",
        { 0, 624, 64 },
        { { 0 } }
    },
    { "", "", -1 }
// clang-format on
};

static const device_t msramcard_device = {
    .name = "Microsoft RAMCard for IBM PC",
    .internal_name = "msramcard",
    .flags = DEVICE_ISA,
    .local = ISAMEM_RAMCARD_CARD,
    .init = isamem_init,
    .close = isamem_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = msramcard_config
};

static const device_config_t mssystemcard_config[] = {
// clang-format off
    {
        "size", "Memory Size", CONFIG_SPINNER, "", 64, "",
        { 0, 256, 64 },
        { { 0 } }
    },
    {
        "start", "Start Address", CONFIG_SPINNER, "", 0, "",
        { 0, 624, 64 },
        { { 0 } }
    },
    { "", "", -1 }
// clang-format on
};

static const device_t mssystemcard_device = {
    .name = "Microsoft SystemCard",
    .internal_name = "mssystemcard",
    .flags = DEVICE_ISA,
    .local = ISAMEM_SYSTEMCARD_CARD,
    .init = isamem_init,
    .close = isamem_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = mssystemcard_config
};

static const device_config_t ibmat_config[] = {
// clang-format off
    {
        "size", "Memory Size", CONFIG_SPINNER, "", 512, "",
        { 0, 12288, 512 },
        { { 0 } }
    },
    {
        "start", "Start Address", CONFIG_SPINNER, "", 512, "",
        { 0, 15872, 512 },
        { { 0 } }
    },
    { "", "", -1 }
// clang-format on
};

static const device_t ibmat_device = {
    .name = "IBM PC/AT Memory Expansion",
    .internal_name = "ibmat",
    .flags = DEVICE_ISA,
    .local = ISAMEM_IBMAT_CARD,
    .init = isamem_init,
    .close = isamem_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = ibmat_config
};

static const device_config_t genericat_config[] = {
// clang-format off
    {
        "size", "Memory Size", CONFIG_SPINNER, "", 512, "",
        { 0, 16384, 512 },
        { { 0 } }
    },
    {
        "start", "Start Address", CONFIG_SPINNER, "", 512, "",
        { 0, 15872, 128 },
        { { 0 } }
    },
    { "", "", -1 }
// clang-format on
};

static const device_t genericat_device = {
    .name = "Generic PC/AT Memory Expansion",
    .internal_name = "genericat",
    .flags = DEVICE_ISA,
    .local = ISAMEM_GENAT_CARD,
    .init = isamem_init,
    .close = isamem_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = genericat_config
};

static const device_config_t p5pak_config[] = {
// clang-format off
    {
        "size", "Memory Size", CONFIG_SPINNER, "", 128, "",
        { 0, 384, 64 },
        { { 0 } }
    },
    {
        "start", "Start Address", CONFIG_SPINNER, "", 512, "",
        { 64, 576, 64 },
        { { 0 } }
    },
    { "", "", -1 }
// clang-format on
};

static const device_t p5pak_device = {
    .name = "Paradise Systems 5-PAK",
    .internal_name = "p5pak",
    .flags = DEVICE_ISA,
    .local = ISAMEM_P5PAK_CARD,
    .init = isamem_init,
    .close = isamem_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = p5pak_config
};


static const device_config_t a6pak_config[] = {
// clang-format off
    {
        "size", "Memory Size", CONFIG_SPINNER, "", 64, "",
        { 0, 576, 64 },
        { { 0 } }
    },
    {
        "start", "Start Address", CONFIG_SPINNER, "", 256, "",
        { 64, 512, 64 },
        { { 0 } }
    },
    { "", "", -1 }
// clang-format on
};

static const device_t a6pak_device = {
    .name = "AST SixPakPlus",
    .internal_name = "a6pak",
    .flags = DEVICE_ISA,
    .local = ISAMEM_A6PAK_CARD,
    .init = isamem_init,
    .close = isamem_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = a6pak_config
};

static const device_config_t ems5150_config[] = {
// clang-format off
    {
        "size", "Memory Size", CONFIG_SPINNER, "", 256, "",
        { 0, 2048, 64 },
        { { 0 } }
    },
    {
        "base", "Address", CONFIG_HEX16, "", 0, "", { 0 },
        {
            { "Disabled", 0x0000 },
            { "Board 1",  0x0208 },
            { "Board 2",  0x020a },
            { "Board 3",  0x020c },
            { "Board 4",  0x020e },
            { "" }
        },
    },
    { "", "", -1 }
// clang-format on
};

static const device_t ems5150_device = {
    .name = "Micro Mainframe EMS-5150(T)",
    .internal_name = "ems5150",
    .flags = DEVICE_ISA,
    .local = ISAMEM_EMS5150_CARD,
    .init = isamem_init,
    .close = isamem_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = ems5150_config
};

static const device_config_t ev159_config[] = {
// clang-format off
    {
        "size", "Memory Size", CONFIG_SPINNER, "", 512, "",
        { 0, 3072, 512 },
        { { 0 } }
    },
    {
        "start", "Start Address", CONFIG_SPINNER, "", 0, "",
        { 0, 16128, 128 },
        { { 0 } }
    },
    {
        "length", "Contiguous Size", CONFIG_SPINNER, "", 0, "",
        { 0, 16384, 128 },
        { { 0 } }
    },
    {
        "width", "I/O Width", CONFIG_SELECTION, "", 0, "", { 0 },
        {
            { "8-bit",  0 },
            { "16-bit", 1 },
            { ""          }
        },
    },
    {
        "speed", "Transfer Speed", CONFIG_SELECTION, "", 0, "", { 0 },
        {
            { "Standard (150ns)",   0 },
            { "High-Speed (120ns)", 1 },
            { ""                      }
        }
    },
    {
        "ems", "EMS mode", CONFIG_SELECTION, "", 0, "", { 0 },
        {
            { "Disabled", 0 },
            { "Enabled",  1 },
            { "" }
        },
    },
    {
        "base", "Address", CONFIG_HEX16, "", 0x0258, "", { 0 },
        {
            { "208H", 0x0208 },
            { "218H", 0x0218 },
            { "258H", 0x0258 },
            { "268H", 0x0268 },
            { "2A8H", 0x02A8 },
            { "2B8H", 0x02B8 },
            { "2E8H", 0x02E8 },
            { ""             }
        },
    },
    { "", "", -1 }
// clang-format on
};

static const device_t ev159_device = {
    .name = "Everex EV-159 RAM 3000 Deluxe",
    .internal_name = "ev159",
    .flags = DEVICE_ISA,
    .local = ISAMEM_EV159_CARD,
    .init = isamem_init,
    .close = isamem_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = ev159_config
};

#if defined(DEV_BRANCH) && defined(USE_ISAMEM_BRAT)
static const device_config_t brat_config[] = {
// clang-format off
    {
        "base", "Address", CONFIG_HEX16, "", 0x0258, "", { 0 },
        {
            { "208H", 0x0208 },
            { "218H", 0x0218 },
            { "258H", 0x0258 },
            { "268H", 0x0268 },
            { ""             }
        },
    },
    {
        "frame", "Frame Address", CONFIG_HEX20, "", 0, "", { 0 },
        {
            { "Disabled", 0x00000 },
            { "D000H",    0xD0000 },
            { "E000H",    0xE0000 },
            { ""                  }
        },
    },
    {
        "width", "I/O Width", CONFIG_SELECTION, "", 8, "", { 0 },
        {
            { "8-bit",   8 },
            { "16-bit", 16 },
            { ""           }
        },
    },
    {
        "speed", "Transfer Speed", CONFIG_SELECTION, "", 0, "", { 0 },
        {
            { "Standard",   0 },
            { "High-Speed", 1 },
            { ""              }
        }
    },
    {
        "size", "Memory Size", CONFIG_SPINNER, "", 128,
        "",
        { 0, 8192, 512 },
        { { 0 } }
    },
    { "", "", -1 }
// clang-format on
};

static const device_t brat_device = {
    .name = "BocaRAM/AT",
    .internal_name = "brat",
    .flags = DEVICE_ISA,
    .local = ISAMEM_BRAT_CARD,
    .init = isamem_init,
    .close = isamem_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = brat_config
};
#endif

#if defined(DEV_BRANCH) && defined(USE_ISAMEM_RAMPAGE)
static const device_config_t rampage_config[] = {
// clang-format off
    {
        "base", "Address", CONFIG_HEX16, "", 0x0258, "", { 0 },
        {
            { "208H", 0x0208 },
            { "218H", 0x0218 },
            { "258H", 0x0258 },
            { "268H", 0x0268 },
            { "2A8H", 0x02A8 },
            { "2B8H", 0x02B8 },
            { "2E8H", 0x02E8 },
            { ""             }
        },
    },
    {
        "frame", "Frame Address", CONFIG_HEX20, "", 0, "", { 0 },
        {
            { "Disabled", 0x00000 },
            { "C000H",    0xC0000 },
            { "D000H",    0xD0000 },
            { "E000H",    0xE0000 },
            { ""                  }
        },
    },
    {
        "width", "I/O Width", CONFIG_SELECTION, "", 8, "", { 0 },
        {
            { "8-bit",   8 },
            { "16-bit", 16 },
            { ""           }
        },
    },
    {
        "speed", "Transfer Speed", CONFIG_SELECTION, "", 0, "", { 0 },
        {
            { "Standard",   0 },
            { "High-Speed", 1 },
            { ""              }
        }
    },
    {
        "size", "Memory Size", CONFIG_SPINNER, "", 128, "",
        { 0, 8192, 128 },
        { { 0 } }
    },
    { "", "", -1 }
// clang-format on
};

static const device_t rampage_device = {
    .name = "AST RAMpage/XT",
    .internal_name = "rampage",
    .flags = DEVICE_ISA,
    .local = ISAMEM_RAMPAGEXT_CARD,
    .init = isamem_init,
    .close = isamem_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = rampage_config
};
#endif

#if defined(DEV_BRANCH) && defined(USE_ISAMEM_IAB)
static const device_config_t iab_config[] = {
// clang-format off
    {
        "base", "Address", CONFIG_HEX16, "", 0x0258, "", { 0 },
        {
            { "208H", 0x0208 },
            { "218H", 0x0218 },
            { "258H", 0x0258 },
            { "268H", 0x0268 },
            { "2A8H", 0x02A8 },
            { "2B8H", 0x02B8 },
            { "2E8H", 0x02E8 },
            { ""             }
        },
    },
    {
        "frame", "Frame Address", CONFIG_HEX20, "", 0, "", { 0 },
        {
            { "Disabled", 0x00000 },
            { "C000H",    0xC0000 },
            { "D000H",    0xD0000 },
            { "E000H",    0xE0000 },
            { ""                  }
        },
    },
    {
        "width", "I/O Width", CONFIG_SELECTION, "", 8, "", { 0 },
        {
            { "8-bit",   8 },
            { "16-bit", 16 },
            { ""           }
        },
    },
    {
        "speed", "Transfer Speed", CONFIG_SELECTION, "", 0, "", { 0 },
        {
            { "Standard",   0 },
            { "High-Speed", 1 },
            { ""              }
        }
    },
    {
        "size", "Memory Size", CONFIG_SPINNER, "", 128, "",
        { 0, 8192, 128 },
        { { 0 } }
    },
    { "", "", -1 }
// clang-format on
};

static const device_t iab_device = {
    .name = "Intel AboveBoard",
    .internal_name = "iab",
    .flags = DEVICE_ISA,
    .local = ISAMEM_ABOVEBOARD_CARD,
    .init = isamem_init,
    .close = isamem_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = iab_config
};
#endif

static const device_t isa_none_device = {
    .name = "None",
    .internal_name = "none",
    .flags = 0,
    .local = 0,
    .init = NULL,
    .close = NULL,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

static const struct {
    const device_t	*dev;
} boards[] = {
// clang-format off
    { &isa_none_device     },
    { &ibmxt_device        },
    { &genericxt_device    },
    { &msramcard_device    },
    { &mssystemcard_device },
    { &ibmat_device        },
    { &genericat_device    },
    { &p5pak_device        },
    { &a6pak_device        },
    { &ems5150_device      },
    { &ev159_device        },
#if defined(DEV_BRANCH) && defined(USE_ISAMEM_BRAT)
    { &brat_device         },
#endif
#if defined(DEV_BRANCH) && defined(USE_ISAMEM_RAMPAGE)
    { &rampage_device      },
#endif
#if defined(DEV_BRANCH) && defined(USE_ISAMEM_IAB)
    { &iab_device          },
#endif
    { NULL                 }
// clang-format on
};

void
isamem_reset(void)
{
    int k, i;

    /* We explicitly set to zero here or bad things happen */
    isa_mem_size = 0;

    for (i = 0; i < ISAMEM_MAX; i++) {
	k = isamem_type[i];
	if (k == 0) continue;

	/* Add the instance to the system. */
	device_add_inst(boards[k].dev, i + 1);
    }
}

const char *
isamem_get_name(int board)
{
    if (boards[board].dev == NULL) return(NULL);

    return(boards[board].dev->name);
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
	if (! strcmp(boards[c].dev->internal_name, s))
		return(c);
	c++;
    }

    /* Not found. */
    return(0);
}

const device_t *
isamem_get_device(int board)
{
    /* Add the instance to the system. */
    return boards[board].dev;
}
