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
 *		f000:e1be	DI bits set mean output text!!!	(801a)
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
 *		  f000:ef66	RTC read reg cl
 *				polls until jim 0xa is zero,
 *				output cl at jim 0xa
 *				read low 4 nibble at jim 0xa
 *				read low 4 nibble at jim 0xa
 *				return first nibble<<4|second nibble in ah
 *		  f000:f046	seldom compares ret 
 *		  f000:fe87	0 -> ds
 *
 *		Memory:
 *		  0000:0469	bit 0: b0000 memory available
 *				bit 1: b8000 memory available
 *		  0000:046a:	00 jim 250 01 jim 350
 *
 * WARNING	THIS IS A WORK-IN-PROGRESS MODULE. USE AT OWN RISK.
 *		
 * Version:	@(#)europc.c	1.0.12	2019/11/15
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Inspired by the "jim.c" file originally present, but a
 *		fully re-written module, based on the information from
 *		Schneider's schematics and technical manuals, and the
 *		input from people with real EuroPC hardware.
 *
 *		Copyright 2017-2019 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <time.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../io.h"
#include "../timer.h"
#include "../nmi.h"
#include "../mem.h"
#include "../pit.h"
#include "../rom.h"
#include "../device.h"
#include "../nvr.h"
#include "../keyboard.h"
#include "../mouse.h"
#include "../game/gameport.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../disk/hdc.h"
#include "../video/video.h"
#include "machine.h"


#define EUROPC_DEBUG	0			/* current debugging level */


/* M3002 RTC chip registers. */
#define MRTC_SECONDS	0x00			/* BCD, 00-59 */
#define MRTC_MINUTES	0x01			/* BCD, 00-59 */
#define MRTC_HOURS	0x02			/* BCD, 00-23 */
#define MRTC_DAYS	0x03			/* BCD, 01-31 */
#define MRTC_MONTHS	0x04			/* BCD, 01-12 */
#define MRTC_YEARS	0x05			/* BCD, 00-99 (year only) */
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

    nvr_t	nvr;				/* NVR */
    uint8_t	nvr_stat;
    uint8_t	nvr_addr;

    void *	mouse;
} europc_t;


static europc_t europc;


#ifdef ENABLE_EUROPC_LOG
int europc_do_log = ENABLE_EUROPC_LOG;


static void
europc_log(const char *fmt, ...)
{
   va_list ap;

   if (europc_do_log)
   {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
   }
}
#else
#define europc_log(fmt, ...)
#endif


/*
 * This is called every second through the NVR/RTC hook.
 *
 * We fake a 'running' RTC by updating its registers on
 * each passing second. Not exactly accurate, but good
 * enough.
 *
 * Note that this code looks nasty because of all the
 * BCD to decimal vv going on.
 *
 * FIXME: should we mark NVR as dirty?
 */
static void
europc_rtc_tick(nvr_t *nvr)
{
    uint8_t *regs;
    int mon, yr;

    /* Only if RTC is running.. */
    regs = nvr->regs;
    if (! (regs[MRTC_CTRLSTAT] & 0x01)) return;

    regs[MRTC_SECONDS] = RTC_BCDINC(nvr->regs[MRTC_SECONDS], 1);
    if (regs[MRTC_SECONDS] >= RTC_BCD(60)) {
	regs[MRTC_SECONDS] = RTC_BCD(0);
	regs[MRTC_MINUTES] = RTC_BCDINC(regs[MRTC_MINUTES], 1);
	if (regs[MRTC_MINUTES] >= RTC_BCD(60)) {
		regs[MRTC_MINUTES] = RTC_BCD(0);
		regs[MRTC_HOURS] = RTC_BCDINC(regs[MRTC_HOURS], 1);
		if (regs[MRTC_HOURS] >= RTC_BCD(24)) {
			regs[MRTC_HOURS] = RTC_BCD(0);
			regs[MRTC_DAYS] = RTC_BCDINC(regs[MRTC_DAYS], 1);
			mon = RTC_DCB(regs[MRTC_MONTHS]);
			yr = RTC_DCB(regs[MRTC_YEARS]) + 1900;
			if (RTC_DCB(regs[MRTC_DAYS]) > nvr_get_days(mon, yr)) {
				regs[MRTC_DAYS] = RTC_BCD(1);
				regs[MRTC_MONTHS] = RTC_BCDINC(regs[MRTC_MONTHS], 1);
				if (regs[MRTC_MONTHS] > RTC_BCD(12)) {
					regs[MRTC_MONTHS] = RTC_BCD(1);
					regs[MRTC_YEARS] = RTC_BCDINC(regs[MRTC_YEARS], 1) & 0xff;
				}
			}
		}
	}
    }
}


/* Get the current NVR time. */
static void
rtc_time_get(uint8_t *regs, struct tm *tm)
{
    /* NVR is in BCD data mode. */
    tm->tm_sec = RTC_DCB(regs[MRTC_SECONDS]);
    tm->tm_min = RTC_DCB(regs[MRTC_MINUTES]);
    tm->tm_hour = RTC_DCB(regs[MRTC_HOURS]);
    tm->tm_wday = (RTC_DCB(regs[MRTC_WEEKDAY]) - 1);
    tm->tm_mday = RTC_DCB(regs[MRTC_DAYS]);
    tm->tm_mon = (RTC_DCB(regs[MRTC_MONTHS]) - 1);
    tm->tm_year = RTC_DCB(regs[MRTC_YEARS]);
#if USE_Y2K
    tm->tm_year += (RTC_DCB(regs[MRTC_CENTURY]) * 100) - 1900;
#endif
}


/* Set the current NVR time. */
static void
rtc_time_set(uint8_t *regs, struct tm *tm)
{
    /* NVR is in BCD data mode. */
    regs[MRTC_SECONDS] = RTC_BCD(tm->tm_sec);
    regs[MRTC_MINUTES] = RTC_BCD(tm->tm_min);
    regs[MRTC_HOURS] = RTC_BCD(tm->tm_hour);
    regs[MRTC_WEEKDAY] = RTC_BCD(tm->tm_wday + 1);
    regs[MRTC_DAYS] = RTC_BCD(tm->tm_mday);
    regs[MRTC_MONTHS] = RTC_BCD(tm->tm_mon + 1);
    regs[MRTC_YEARS] = RTC_BCD(tm->tm_year % 100);
#if USE_Y2K
    regs[MRTC_CENTURY] = RTC_BCD((tm->tm_year+1900) / 100);
#endif
}


static void
rtc_start(nvr_t *nvr)
{
    struct tm tm;

    /* Initialize the internal and chip times. */
    if (time_sync & TIME_SYNC_ENABLED) {
	/* Use the internal clock's time. */
	nvr_time_get(&tm);
	rtc_time_set(nvr->regs, &tm);
    } else {
	/* Set the internal clock from the chip time. */
	rtc_time_get(nvr->regs, &tm);
	nvr_time_set(&tm);
    }

#if 0
    /* Start the RTC - BIOS will do this. */
    nvr->regs[MRTC_CTRLSTAT] = 0x01;
#endif
}


/* Create a valid checksum for the current NVR data. */
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


/* Reset the machine's NVR to a sane state. */
static void
rtc_reset(nvr_t *nvr)
{
    /* Initialize the RTC to a known state. */
    nvr->regs[MRTC_SECONDS] = RTC_BCD(0);	/* seconds */
    nvr->regs[MRTC_MINUTES] = RTC_BCD(0);	/* minutes */
    nvr->regs[MRTC_HOURS] = RTC_BCD(0);		/* hours */
    nvr->regs[MRTC_DAYS] = RTC_BCD(1);		/* days */
    nvr->regs[MRTC_MONTHS] = RTC_BCD(1);	/* months */
    nvr->regs[MRTC_YEARS] = RTC_BCD(80);	/* years */
    nvr->regs[MRTC_WEEKDAY] = RTC_BCD(1);	/* weekday */
    nvr->regs[MRTC_WEEKNO] = RTC_BCD(1);	/* weekno */

    /*
     * EuroPC System Configuration:
     *
     * [A]	unknown
     *
     * [B]	7	1  bootdrive extern
     *			0  bootdribe intern
     *		6:5	11 invalid hard disk type
     *			10 hard disk installed, type 2
     *			01 hard disk installed, type 1
     *			00 hard disk not installed
     *		4:3	11 invalid external drive type
     *			10 external drive 720K
     *			01 external drive 360K
     *			00 external drive disabled
     *		2	unknown
     *		1:0	11 invalid internal drive type
     *			10 internal drive 360K
     *			01 internal drive 720K
     *			00 internal drive disabled
     *
     *	 [C]	7:6	unknown
     *		5	monitor detection OFF
     *		4	unknown
     *		3:2	11 illegal memory size
     *			10 512K
     *			01 256K
     *			00 640K
     *		1:0	11 illegal game port
     *			10 gameport as mouse port
     *			01 gameport as joysticks
     *			00 gameport disabled
     *
     * [D]	7:6	10 9MHz CPU speed
     *			01 7MHz CPU speed
     *			00 4.77 MHz CPU
     *		5	unknown
     *		4	external: color, internal: mono
     *		3	unknown
     *		2	internal video ON
     *		1:0	11 mono
     *			10 color80
     *			01 color40
     *			00 special (EGA,VGA etc)
     *
     * [E]	7:4	unknown
     *		3:0	country	(00=Deutschland, 0A=ASCII)
     */
    nvr->regs[MRTC_CONF_A] = 0x00;	/* CONFIG A */
    nvr->regs[MRTC_CONF_B] = 0x0A;	/* CONFIG B */
    nvr->regs[MRTC_CONF_C] = 0x28;	/* CONFIG C */
    nvr->regs[MRTC_CONF_D] = 0x12;	/* CONFIG D */
    nvr->regs[MRTC_CONF_E] = 0x0A;	/* CONFIG E */

    nvr->regs[MRTC_CHECK_LO] = 0x00;	/* checksum (LO) */
    nvr->regs[MRTC_CHECK_HI] = 0x00;	/* checksum (HI) */

    nvr->regs[MRTC_CTRLSTAT] = 0x01;	/* status/control */

    /* Generate a valid checksum. */
    nvr->regs[MRTC_CHECK_LO] = rtc_checksum(nvr->regs);
}


/* Execute a JIM control command. */
static void
jim_set(europc_t *sys, uint8_t reg, uint8_t val)
{
    switch(reg) {
	case 0:		/* MISC control (WO) */
		// bit0: enable MOUSE
		// bit1: enable joystick
		break;

	case 2:		/* AGA control */
		if (! (val & 0x80)) {
			/* Reset AGA. */
			break;
		}

		switch (val) {
			case 0x1f:	/* 0001 1111 */
			case 0x0b:	/* 0000 1011 */
				//europc_jim.mode=AGA_MONO;
				europc_log("EuroPC: AGA Monochrome mode!\n");
				break;

			case 0x18:	/* 0001 1000 */
			case 0x1a:	/* 0001 1010 */
				//europc_jim.mode=AGA_COLOR;
				break;

			case 0x0e:	/* 0000 1100 */
				/*80 columns? */
				europc_log("EuroPC: AGA 80-column mode!\n");
				break;

			case 0x0d:	/* 0000 1011 */
				/*40 columns? */
				europc_log("EuroPC: AGA 40-column mode!\n");
				break;

			default:
				//europc_jim.mode=AGA_OFF;
				break;
		}
		break;

	case 4:		/* CPU Speed control */
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
    europc_t *sys = (europc_t *)priv;
    uint8_t b;

#if EUROPC_DEBUG > 1
    europc_log("EuroPC: jim_wr(%04x, %02x)\n", addr, val);
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
		jim_set(sys, (addr & 0x07), val);
		break;

	case 0x0a:		/* M3002 RTC INDEX/DATA register */
		switch(sys->nvr_stat) {
			case 0:		/* save index */
				sys->nvr_addr = val & 0x0f;
				sys->nvr_stat++;
				break;

			case 1:		/* save data HI nibble */
				b = sys->nvr.regs[sys->nvr_addr] & 0x0f;
				b |= (val << 4);
				sys->nvr.regs[sys->nvr_addr] = b;
				sys->nvr_stat++;
				nvr_dosave++;
				break;

			case 2:		/* save data LO nibble */
				b = sys->nvr.regs[sys->nvr_addr] & 0xf0;
				b |= (val & 0x0f);
				sys->nvr.regs[sys->nvr_addr] = b;
				sys->nvr_stat = 0;
				nvr_dosave++;
				break;
		}
		break;

	default:
		europc_log("EuroPC: invalid JIM write %02x, val %02x\n", addr, val);
		break;
    }
}


/* Read from one of the JIM registers. */
static uint8_t
jim_read(uint16_t addr, void *priv)
{
    europc_t *sys = (europc_t *)priv;
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

	case 0x0a:		/* M3002 RTC INDEX/DATA register */
		switch(sys->nvr_stat) {
			case 0:
				r = 0x00;
				break;

			case 1:		/* read data HI nibble */
				r = (sys->nvr.regs[sys->nvr_addr] >> 4);
				sys->nvr_stat++;
				break;

			case 2:		/* read data LO nibble */
				r = (sys->nvr.regs[sys->nvr_addr] & 0x0f);
				sys->nvr_stat = 0;
				break;
		}
		break;

	default:
		europc_log("EuroPC: invalid JIM read %02x\n", addr);
		break;
    }

#if EUROPC_DEBUG > 1
    europc_log("EuroPC: jim_rd(%04x): %02x\n", addr, r);
#endif

    return(r);
}


/* Initialize the mainboard 'device' of the machine. */
static void *
europc_boot(const device_t *info)
{
    europc_t *sys = &europc;
    uint8_t b;

#if EUROPC_DEBUG
    europc_log("EuroPC: booting mainboard..\n");
#endif

    europc_log("EuroPC: NVR=[ %02x %02x %02x %02x %02x ] %sVALID\n",
	sys->nvr.regs[MRTC_CONF_A], sys->nvr.regs[MRTC_CONF_B],
	sys->nvr.regs[MRTC_CONF_C], sys->nvr.regs[MRTC_CONF_D],
	sys->nvr.regs[MRTC_CONF_E],
	(sys->nvr.regs[MRTC_CHECK_LO]!=rtc_checksum(sys->nvr.regs))?"IN":"");

    /*
     * Now that we have initialized the NVR (either from file,
     * or by setting it to defaults) we can start overriding it
     * with values set by the user.
     */
    b = (sys->nvr.regs[MRTC_CONF_D] & ~0x17);
    video_reset(gfxcard);
    if (video_is_cga())
	b |= 0x12;	/* external video, CGA80 */
    else if (video_is_mda())
	b |= 0x03;	/* external video, mono */
    else
	b |= 0x10;	/* external video, special */

    sys->nvr.regs[MRTC_CONF_D] = b;

    /* Update the memory size. */
    b = (sys->nvr.regs[MRTC_CONF_C] & 0xf3);
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
    sys->nvr.regs[MRTC_CONF_C] = b;

    /* Update CPU speed. */
    b = (sys->nvr.regs[MRTC_CONF_D] & 0x3f);
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
    sys->nvr.regs[MRTC_CONF_D] = b;

    /* Set up game port. */
    b = (sys->nvr.regs[MRTC_CONF_C] & 0xfc);
    if (mouse_type == MOUSE_TYPE_INTERNAL) {
	sys->mouse = device_add(&mouse_logibus_onboard_device);
	mouse_bus_set_irq(sys->mouse, 2);
	/* Configure the port for (Bus Mouse Compatible) Mouse. */
	b |= 0x01;
    } else if (joystick_type != JOYSTICK_TYPE_NONE)
	b |= 0x02;	/* enable port as joysticks */
    sys->nvr.regs[MRTC_CONF_C] = b;

#if 0
    /* Set up floppy types. */
    sys->nvr.regs[MRTC_CONF_B] = 0x2a;
#endif

    /* Validate the NVR checksum and save. */
    sys->nvr.regs[MRTC_CHECK_LO] = rtc_checksum(sys->nvr.regs);
    nvr_dosave++;

    /*
     * Allocate the system's I/O handlers.
     *
     * The first one is for the JIM. Note that although JIM usually
     * resides at 0x0250, a special solder jumper on the mainboard
     * (JS9) can be used to "move" it to 0x0350, to get it out of
     * the way of other cards that need this range.
     */
    io_sethandler(sys->jim, 16,
		  jim_read,NULL,NULL, jim_write,NULL,NULL, sys);

    /* Only after JIM has been initialized. */
    (void)device_add(&keyboard_xt_device);

    /* Enable and set up the FDC. */
    (void)device_add(&fdc_xt_device);

     /*
     * Set up and enable the HD20 disk controller.
     *
     * We only do this if we have not configured another one.
     */
    if (hdc_current == 1)
	(void)device_add(&xta_hd20_device);

    return(sys);
}


static void
europc_close(void *priv)
{
    nvr_t *nvr = &europc.nvr;

    if (nvr->fn != NULL)
	free(nvr->fn);
}


static const device_config_t europc_config[] = {
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


const device_t europc_device = {
    "EuroPC System Board",
    0, 0,
    europc_boot, europc_close, NULL,
    NULL, NULL, NULL,
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
int
machine_europc_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/europc/50145",
			   0x000f8000, 32768, 0);

    if (bios_only || !ret)
	return ret;

    machine_common_init(model);
    pit_ctr_set_out_func(&pit->counters[1], pit_refresh_timer_xt);

    nmi_init();

    /* Clear the machine state. */
    memset(&europc, 0x00, sizeof(europc_t));
    europc.jim = 0x0250;

    /* This is machine specific. */
    europc.nvr.size = model->nvrmask + 1;
    europc.nvr.irq = -1;

    /* Set up any local handlers here. */
    europc.nvr.reset = rtc_reset;
    europc.nvr.start = rtc_start;
    europc.nvr.tick = europc_rtc_tick;

    /* Initialize the actual NVR. */
    nvr_init(&europc.nvr);

    /* Enable and set up the mainboard device. */
    device_add(&europc_device);

    return ret;
}
