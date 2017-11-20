/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Schneider EuroPC system.
 *
 * NOTES:	BIOS info (taken from MAME, thanks guys!!)
 *
 *		f000:e107	bios checksum test
 *				memory test
 *		f000:e145	irq vector init
 *		f000:e156
 *		f000:e169-d774	test of special registers 254/354
 *		f000:e16c-e817
 *		f000:e16f
 *		f000:ec08	test of special registers 800a rtc time
 *				or date error, rtc corrected
 *		f000:ef66 0xf
 *		f000:db3e 0x8..0xc
 *		f000:d7f8
 *		f000:db5f
 *		f000:e172 
 *		f000:ecc5	801a video setup error
 *		f000:d6c9	copyright output
 *		f000:e1b7
 *		f000:e1be	DI bits set mean output text!!!,
 *		   (801a)
 *		f000:		0x8000 output
 *				  1 rtc error
 *				  2 rtc time or date error
 *				  4 checksum error in setup
 *				  8 rtc status corrected
 *			   	 10 video setup error
 *			  	 20 video ram bad
 *			 	 40 monitor type not recogniced
 *				 80 mouse port enabled
 *			 	100 joystick port enabled
 *		f000:e1e2-dc0c	CPU speed is 4.77 mhz
 *		f000:e1e5-f9c0	keyboard processor error
 *		f000:e1eb-c617	external lpt1 at 0x3bc
 *		f000:e1ee-e8ee	external coms at
 *
 *		Routines:
 *		  f000:c92d	output text at bp
 *		  f000:db3e	RTC read reg cl
 *	  	  f000:e8ee	piep
 *		  f000:e95e	RTC write reg cl
 *				polls until JIM 0xa is zero,
 *				output cl at jim 0xa
 *				write ah hinibble as lownibble into jim 0xa
 *				write ah lownibble into jim 0xa
 *		f000:ef66	RTC read reg cl
 *				polls until jim 0xa is zero,
 *				output cl at jim 0xa
 *				read low 4 nibble at jim 0xa
 *				read low 4 nibble at jim 0xa
 *				return first nibble<<4|second nibble in ah
 *		f000:f046	seldom compares ret 
 *		f000:fe87	0 -> ds
 *		0000:0469	bit 0: b0000 memory available
 *				bit 1: b8000 memory available
 *		0000:046a:	00 jim 250 01 jim 350
 *
 * WARNING	THIS IS A WORK-IN-PROGRESS MODULE. USE AT OWN RISK.
 *		
 * Version:	@(#)europc.c	1.0.5	2017/11/18
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Inspired by the "jim.c" file originally present, but a
 *		fully re-written module, based on the information from
 *		Schneider's schematics and technical manuals, and the
 *		input from people with real EuroPC hardware.
 *
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../io.h"
#include "../nmi.h"
#include "../mem.h"
#include "../rom.h"
#include "../nvr.h"
#include "../device.h"
#include "../disk/hdc.h"
#include "../keyboard.h"
#include "../mouse.h"
#include "../game/gameport.h"
#include "../video/video.h"
#include "machine.h"


#define EUROPC_DEBUG	1			/* current debugging level */


/* M3002 RTC chip registers. */
#define MRTC_SECONDS	0x00			/* BCD, 00-59 */
#define MRTC_MINUTES	0x01			/* BCD, 00-59 */
#define MRTC_HOURS	0x02			/* BCD, 00-23 */
#define MRTC_DAYS	0x03			/* BCD, 01-31 */
#define MRTC_MONTHS	0x04			/* BCD, 01-12 */
#define MRTC_YEARS	0x05			/* BCD, 00-99 (2017 is 0x17) */
#define MRTC_WEEKDAY	0x06			/* BCD, 01-07 */
#define MRTC_WEEKNO	0x07			/* BCD, 01-52 */
#define MRTC_CONF_A	0x08			/* EuroPC config, binary */
#define MRTC_CONF_B	0x09			/* EuroPC config, binary */
#define MRTC_CONF_C	0x0a			/* EuroPC config, binary */
#define MRTC_CONF_D	0x0b			/* EuroPC config, binary */
#define MRTC_CONF_E	0x0c			/* EuroPC config, binary */
#define MRTC_CHECK_LO	0x0d			/* Checksum, low byte */
#define MRTC_CHECK_HI	0x0e			/* Checksum, high byte */
#define MRTC_CTRLSTAT	0x0f			/* RTC control/status, binary */


typedef struct {
    uint16_t	jim;				/* JIM base address */

    uint8_t	regs[16];			/* JIM internal regs (8) */

    struct {
        uint8_t dat[16];
        uint8_t stat;
        uint8_t addr;
    }		rtc;

    nvr_t	nvr;				/* 86Box NVR */
} vm_t;


static vm_t	*vm = NULL;


/* Load the relevant portion of the NVR to disk. */
static int8_t
load_nvr(wchar_t *fn)
{
    FILE *f;

    if (vm == NULL) return(0);

    f = nvr_fopen(fn, L"rb");
    if (f != NULL) {
	(void)fread(vm->rtc.dat, 1, 16, f);
	(void)fclose(f);
	pclog("EuroPC: CMOS data loaded from file %ls !\n", fn);
	return(1);
    }

    pclog("EuroPC: unable to load NVR !\n");
    return(0);
}


/* Save the relevant portion of the NVR to disk. */
static int8_t
save_nvr(wchar_t *fn)
{
    FILE *f;

    if (vm == NULL) return(0);

    f = nvr_fopen(fn, L"wb");
    if (f != NULL) {
	(void)fwrite(vm->rtc.dat, 1, 16, f);
	(void)fclose(f);
	return(1);
    }

    pclog("EuroPC: unable to save NVR !\n");
    return(0);
}


/* This is called every second through the NVR/RTC hook. */
static void
rtc_hook(nvr_t *nvr)
{
#if 0
    int month, year;

    sys->rtc.dat[0] = bcd_adjust(sys->rtc.dat[0]+1);
    if (sys->rtc.dat[0] >= 0x60) {
	sys->rtc.dat[0] = 0;
	sys->rtc.dat[1] = bcd_adjust(sys->rtc.dat[1]+1);
	if (sys->rtc.dat[1] >= 0x60) {
			sys->rtc.dat[1] = 0;
			sys->rtc.dat[2] = bcd_adjust(sys->rtc.dat[2]+1);
			if (sys->rtc.dat[2] >= 0x24) {
				sys->rtc.dat[2] = 0;
				sys->uropc_rtc.data[3]=bcd_adjust(sys->uropc_rtc.data[3]+1);
				month = bcd_2_dec(sys->rtc.dat[4]);

				/* Save for julian_days_in_month_calculation. */
				year = bcd_2_dec(sys->rtc.dat[5])+2000;
				if (sys->rtc.dat[3] > gregorian_days_in_month(month, year)) {
					sys->rtc.dat[3] = 1;
					sys->rtc.dat[4] = bcd_adjust(sys->rtc.dat[4]+1);
					if (sys->rtc.dat[4]>0x12) {
						sys->rtc.dat[4] = 1;
						sys->rtc.dat[5] = bcd_adjust(sys->rtc.dat[5]+1)&0xff;
					}
				}
			}
		}
	}
    }
#endif
}


#if 0
void europc_rtc_init(void)
{
    europc_rtc.data[0xf]=1;

    europc_rtc.timer = timer_alloc(europc_rtc_timer);
    timer_adjust(europc_rtc.timer, 0, 0, 1.0);
}
#endif


/* Execute a JIM control command. */
static void
jim_action(vm_t *sys, uint8_t reg, uint8_t val)
{
    switch(reg) {
	case 0:		/* MISC control (WO) */
//pclog("EuroPC: write MISC = %02x\n", val);
		// bit0: enable MOUSE
		// bit1: enable joystick
		break;

	case 2:		/* AGA control */
//pclog("EuroPC: write AGA = %02x\n", val);
		if (! (val & 0x80)) {
			/* Reset AGA. */
			break;
		}

		switch (val) {
			case 0x1f:	/* 0001 1111 */
			case 0x0b:	/* 0000 1011 */
				//europc_jim.mode=AGA_MONO;
				pclog("EuroPC: AGA Monochrome mode!\n");
				break;

			case 0x18:	/* 0001 1000 */
			case 0x1a:	/* 0001 1010 */
				//europc_jim.mode=AGA_COLOR;
				break;

			case 0x0e:	/* 0000 1100 */
				/*80 columns? */
				pclog("EuroPC: AGA 80-column mode!\n");
				break;

			case 0x0d:	/* 0000 1011 */
				/*40 columns? */
				pclog("EuroPC: AGA 40-column mode!\n");
				break;

			default:
				//europc_jim.mode=AGA_OFF;
				break;
		}
		break;

	case 4:		/* CPU Speed control */
//pclog("EuroPC: write CPUCLK = %02x\n", val);
		switch(val & 0xc0) {
			case 0x00:	/* 4.77 MHz */
//				cpu_set_clockscale(0, 1.0/2);
				break;

			case 0x40:	/* 7.16 MHz */
//				cpu_set_clockscale(0, 3.0/4);
				break;

			default:	/* 9.54 MHz */
//				cpu_set_clockscale(0, 1);break;
				break;
		}
		break;

	default:
		break;
    }

    sys->regs[reg] = val;
}


/* Write to one of the JIM registers. */
static void
jim_write(uint16_t addr, uint8_t val, void *priv)
{
    vm_t *sys = (vm_t *)priv;
    uint8_t b;

#if EUROPC_DEBUG > 1
    pclog("EuroPC: jim_wr(%04x, %02x)\n", addr, val);
#endif

    switch (addr & 0x000f) {
	case 0x00:		/* JIM internal registers (WRONLY) */
	case 0x01:
	case 0x02:
	case 0x03:
	case 0x04:		/* JIM internal registers (R/W) */
	case 0x05:
	case 0x06:
	case 0x07:
		jim_action(sys, (addr & 0x07), val);
		break;

	case 0x0A:		/* M3002 RTC INDEX/DATA register */
		switch(sys->rtc.stat) {
			case 0:		/* save index */
				sys->rtc.addr = val & 0x0f;
				sys->rtc.stat++;
				break;

			case 1:		/* save data HI nibble */
                        	b = sys->rtc.dat[sys->rtc.addr] & 0x0f;
				b |= (val << 4);
                        	sys->rtc.dat[sys->rtc.addr] = b;
				sys->rtc.stat++;
				break;

			case 2:		/* save data LO nibble */
                        	b = sys->rtc.dat[sys->rtc.addr] & 0xf0;
				b |= (val & 0x0f);
                        	sys->rtc.dat[sys->rtc.addr] = b;
				sys->rtc.stat = 0;
				break;
		}
		break;

	default:
		pclog("EuroPC: invalid JIM write %02x, val %02x\n", addr, val);
		break;
    }
}


/* Read from one of the JIM registers. */
static uint8_t
jim_read(uint16_t addr, void *priv)
{
    vm_t *sys = (vm_t *)priv;
    uint8_t r = 0xff;

    switch (addr & 0x000f) {
	case 0x00:		/* JIM internal registers (WRONLY) */
	case 0x01:
	case 0x02:
	case 0x03:
		r = 0x00;
		break;

	case 0x04:		/* JIM internal registers (R/W) */
	case 0x05:
	case 0x06:
	case 0x07:
		r = sys->regs[addr & 0x07];
		break;

	case 0x0A:		/* M3002 RTC INDEX/DATA register */
		switch(sys->rtc.stat) {
			case 0:
				r = 0x00;
				break;

			case 1:		/* read data HI nibble */
                        	r = (sys->rtc.dat[sys->rtc.addr] >> 4);
				sys->rtc.stat++;
				break;

			case 2:		/* read data LO nibble */
                        	r = (sys->rtc.dat[sys->rtc.addr] & 0x0f);
				sys->rtc.stat = 0;
				break;
		}
		break;

	default:
		pclog("EuroPC: invalid JIM read %02x\n", addr);
		break;
    }

#if EUROPC_DEBUG > 1
    pclog("EuroPC: jim_rd(%04x): %02x\n", addr, r);
#endif

    return(r);
}


static uint8_t
rtc_checksum(uint8_t *ptr)
{
    uint8_t sum;
    int i;

    /* Calculate all bytes with XOR. */
    sum = 0x00;
    for (i=MRTC_CONF_A; i<=MRTC_CONF_E; i++)
	sum += ptr[i];

    return(sum);
}


/*
 * Initialize the mainboard 'device' of the machine.
 */
static void *
europc_boot(device_t *info)
{
    vm_t *sys = vm;
    uint8_t b;

#if EUROPC_DEBUG
    pclog("EuroPC: booting mainboard..\n");
#endif

    /* Try to load the NVR from file. */
    if (! nvr_load()) {
	/* Load failed, reset to defaults. */
	sys->rtc.dat[0x00] = 0x00;		/* RTC seconds */
	sys->rtc.dat[0x01] = 0x00;		/* RTC minutes */
	sys->rtc.dat[0x02] = 0x00;		/* RTC hours */
	sys->rtc.dat[0x03] = 0x01;		/* RTC days */
	sys->rtc.dat[0x04] = 0x01;		/* RTC months */
	sys->rtc.dat[0x05] = 0x17;		/* RTC years */
	sys->rtc.dat[0x06] = 0x01;		/* RTC weekday */
	sys->rtc.dat[0x07] = 0x01;		/* RTC weekno */

	/*
	 * EuroPC System Configuration:
	 *
 	 * [A]	unknown
	 *
	 * [B]	7	1  bootdrive extern
	 *		0  bootdribe intern
	 *	6:5	11 invalid hard disk type
	 *		10 hard disk installed, type 2
	 *		01 hard disk installed, type 1
	 *		00 hard disk not installed
	 *	4:3	11 invalid external drive type
	 *		10 external drive 720K
	 *		01 external drive 360K
	 *		00 external drive disabled
	 *	2	unknown
	 *	1:0	11 invalid internal drive type
	 *		10 internal drive 360K
	 *		01 internal drive 720K
	 *		00 internal drive disabled
	 *
	 * [C]	7:6	unknown
	 *	5	monitor detection OFF
	 *	4	unknown
	 *	3:2	11 illegal memory size
	 *		10 512K
	 *		01 256K
	 *		00 640K
	 *	1:0	11 illegal game port
	 *		10 gameport as mouse port
	 *		01 gameport as joysticks
	 *		00 gameport disabled
	 *
	 * [D]	7:6	10 9MHz CPU speed
	 *		01 7MHz CPU speed
	 *		00 4.77 MHz CPU
	 *	5	unknown
	 *	4	external: color, internal: mono
	 *	3	unknown
	 *	2	internal video ON
	 *	1:0	11 mono
	 *		10 color80
	 *		01 color40
	 *		00 special (EGA,VGA etc)
	 *
	 * [E]	7:4	unknown
	 *	3:0	country	(00=Deutschland, 0A=ASCII)
	 */
	sys->rtc.dat[MRTC_CONF_A] = 0x00;	/* CONFIG A */
	sys->rtc.dat[MRTC_CONF_B] = 0x0A;	/* CONFIG B */
	sys->rtc.dat[MRTC_CONF_C] = 0x28;	/* CONFIG C */
	sys->rtc.dat[MRTC_CONF_D] = 0x12;	/* CONFIG D */
	sys->rtc.dat[MRTC_CONF_E] = 0x0A;	/* CONFIG E */

	sys->rtc.dat[MRTC_CHECK_LO] = 0x44;	/* checksum (LO) */
	sys->rtc.dat[MRTC_CHECK_HI] = 0x00;	/* checksum (HI) */

	sys->rtc.dat[MRTC_CTRLSTAT] = 0x01;	/* status/control */

	/* Provide correct checksum. */
	sys->rtc.dat[MRTC_CHECK_LO] = rtc_checksum(sys->rtc.dat);
    }
    pclog("EuroPC: NVR=[ %02x %02x %02x %02x %02x ] %sVALID\n",
	sys->rtc.dat[MRTC_CONF_A], sys->rtc.dat[MRTC_CONF_B],
	sys->rtc.dat[MRTC_CONF_C], sys->rtc.dat[MRTC_CONF_D],
	sys->rtc.dat[MRTC_CONF_E],
	(sys->rtc.dat[MRTC_CHECK_LO]!=rtc_checksum(sys->rtc.dat))?"IN":"");

    /*
     * Now that we have initialized the NVR (either from file,
     * or by setting it to defaults) we can start overriding it
     * with values set by the 86Box user.
     */
    b = (sys->rtc.dat[MRTC_CONF_D] & ~0x17);
    switch(gfxcard) {
	case GFX_CGA:		/* Color, CGA */
	case GFX_COLORPLUS:	/* Color, Hercules ColorPlus */
		b |= 0x12;	/* external video, CGA80 */
		break;

	case GFX_MDA:		/* Monochrome, MDA */
	case GFX_HERCULES:	/* Monochrome, Hercules */
	case GFX_INCOLOR:	/* Color, ? */
		b |= 0x03;	/* external video, mono */
		break;

	default:		/* EGA, VGA etc */
		b |= 0x10;	/* external video, special */

    }
    sys->rtc.dat[MRTC_CONF_D] = b;

    /* Update the memory size. */
    b = (sys->rtc.dat[MRTC_CONF_C] & 0xf3);
    switch(mem_size) {
	case 256:
		b |= 0x04;
		break;

	case 512:
		b |= 0x08;
		break;

	case 640:
		b |= 0x00;
		break;
    }
    sys->rtc.dat[MRTC_CONF_C] = b;

    /* Update CPU speed. */
    b = (sys->rtc.dat[MRTC_CONF_D] & 0x3f);
    switch(cpu) {
	case 0:		/* 8088, 4.77 MHz */
		b |= 0x00;
		break;

	case 1:		/* 8088, 7.15 MHz */
		b |= 0x40;
		break;

	case 2:		/* 8088, 9.56 MHz */
		b |= 0x80;
		break;
    }
    sys->rtc.dat[MRTC_CONF_D] = b;

    /* Set up game port. */
    b = (sys->rtc.dat[MRTC_CONF_C] & 0xfc);
    if (mouse_type == MOUSE_TYPE_LOGIBUS) {
	b |= 0x01;	/* enable port as MOUSE */
    } else if (joystick_type != 7) {
	b |= 0x02;	/* enable port as joysticks */
	device_add(&gameport_device);
    }
    sys->rtc.dat[MRTC_CONF_C] = b;

#if 0
    /* Set up floppy types. */
    sys->rtc.dat[MRTC_CONF_B] = 0x2A;
#endif

    /* Validate the NVR checksum. */
    sys->rtc.dat[MRTC_CHECK_LO] = rtc_checksum(sys->rtc.dat);
    nvr_save();

    /*
     * Allocate the system's I/O handlers.
     *
     * The first one is for the JIM. Note that although JIM usually
     * resides at 0x0250, a special solder jumper on the mainboard
     * (JS9) can be used to "move" it to 0x0350, to get it out of
     * the way of other cards that need this range.
     */
    io_sethandler(sys->jim, 16,
		  jim_read, NULL, NULL,
		  jim_write, NULL, NULL, sys);

    /* Only after JIM has been initialized. */
    device_add(&keyboard_xt_device);

    /*
     * Set up and enable the HD20 disk controller.
     *
     * We only do this if we have not configured another one.
     */
    if (hdc_current == 1)
	device_add(&europc_hdc_device);

    return(sys);
}


static void
europc_close(void *priv)
{
    nvr_t *nvr = &vm->nvr;

    if (nvr->fname != NULL)
	free(nvr->fname);

    free(vm);
    vm = NULL;
}


static device_config_t europc_config[] = {
    {
	"js9", "JS9 Jumper (JIM)", CONFIG_INT, "", 0,
	{
		{
			"Disabled (250h)", 0
		},
		{
			"Enabled (350h)", 1
		},
		{
			""
		}
	},
    },
    {
	"", "", -1
    }
};


device_t europc_device = {
    "EuroPC System Board",
    0, 0,
    europc_boot,	/* init */
    europc_close,	/* close */
    NULL,
    NULL, NULL, NULL, NULL,
    europc_config
};


/*
 * This function sets up the Scheider EuroPC machine.
 *
 * Its task is to allocate a clean machine data block,
 * and then simply enable the mainboard "device" which
 * allows it to reset (dev init) and configured by the
 * user.
 */
void
machine_europc_init(machine_t *model)
{
    vm_t *sys;

    /* Allocate machine data. */
    sys = (vm_t *)malloc(sizeof(vm_t));
    if (sys == NULL) {
	pclog("EuroPC: unable to allocate machine data!\n");
	return;
    }
    memset(sys, 0x00, sizeof(vm_t));
    sys->jim = 0x0250;
    vm = sys;

    machine_common_init(model);
    nmi_init();
    mem_add_bios();

    /* This is machine specific. */
    vm->nvr.mask = model->nvrmask;
    vm->nvr.irq = -1;

    /* Set up any local handlers here. */
    vm->nvr.load = load_nvr;
    vm->nvr.save = save_nvr;
    vm->nvr.hook = rtc_hook;

    /* Initialize the actual NVR. */
    nvr_init(&vm->nvr);

    /* Enable and set up the mainboard device. */
    device_add(&europc_device);
}
