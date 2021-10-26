/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Intel 8042 (AT keyboard controller) emulation.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		EngiNerd <webmaster.crrc@yahoo.it>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2017-2020 Fred N. van Kempen.
 *		Copyright 2020 EngiNerd.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/ppi.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/m_xt_xi8088.h>
#include <86box/m_at_t3100e.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sound.h>
#include <86box/snd_speaker.h>
#include <86box/video.h>
#include <86box/keyboard.h>


#define STAT_PARITY		0x80
#define STAT_RTIMEOUT		0x40
#define STAT_TTIMEOUT		0x20
#define STAT_MFULL		0x20
#define STAT_UNLOCKED		0x10
#define STAT_CD			0x08
#define STAT_SYSFLAG		0x04
#define STAT_IFULL		0x02
#define STAT_OFULL		0x01

#define RESET_DELAY_TIME	1000		/* 100 ms */

#define CCB_UNUSED		0x80
#define CCB_TRANSLATE		0x40
#define CCB_PCMODE		0x20
#define CCB_ENABLEKBD		0x10
#define CCB_IGNORELOCK		0x08
#define CCB_SYSTEM		0x04
#define CCB_ENABLEMINT		0x02
#define CCB_ENABLEKINT		0x01

#define CCB_MASK		0x68
#define MODE_MASK		0x6c

#define KBC_TYPE_ISA		0x00		/* AT ISA-based chips */
#define KBC_TYPE_PS2_1		0x04		/* PS2 type, no refresh */
/* This only differs in that translation is forced off. */
#define KBC_TYPE_PS2_2		0x05		/* PS2 on PS/2, type 2 */
#define KBC_TYPE_MASK		0x07

#define KBC_FLAG_PS2		0x04

/* We need to redefine this:
	Currently, we use bits 3-7 for vendor, we should instead use bits 4-7
	for vendor, 0-3 for revision/variant, and have a dev->ps2 flag controlling
	controller mode, normally set according to the flags, but togglable on
	AMIKey:
		0000 0000	0x00	IBM, AT
		0000 0001	0x01	MR
		0000 0010	0x02	Xi8088, clone of IBM PS/2 type 1
		0001 0000	0x10	Olivetti
		0010 0000	0x20	Toshiba
		0011 0000	0x30	Quadtel
		0100 0000	0x40	Phoenix MultiKey/42
		0101 0000	0x50	AMI KF
		0101 0001	0x51	AMI KH
		0101 0010	0x52	AMIKey
		0101 0011	0x53	AMIKey-2
		0101 0100	0x54	JetKey (clone of AMI KF/AMIKey)
		0110 0000	0x60	Award
		0110 0001	0x61	Award 286 (has some AMI commands apparently)
		0111 0000	0x70	Siemens
*/		

/* Standard IBM controller */
#define KBC_VEN_GENERIC		0x00
/* All commands are standard PS/2 */
#define KBC_VEN_IBM_MCA		0x08
/* Standard IBM commands, differs in input port bits */
#define KBC_VEN_IBM_PS1		0x10
/* Olivetti - proprietary commands and port 62h with switches
   readout */
#define KBC_VEN_OLIVETTI	0x20
/* Toshiba T3100e - has a bunch of proprietary commands, also sets
   IFULL on command AA */
#define KBC_VEN_TOSHIBA		0x28
/* Standard IBM commands, uses input port as a switches readout */
#define KBC_VEN_NCR		0x30
/* Xi8088 - standard IBM commands, has a turbo bit on port 61h, and the
   polarity of the video type bit in the input port is inverted */
#define KBC_VEN_XI8088		0x38
/* QuadtelKey - currently guesswork */
#define KBC_VEN_QUADTEL		0x40
/* Phoenix MultiKey/42 - not yet implemented */
#define KBC_VEN_PHOENIX		0x48
/* Generic commands, XI8088-like input port handling of video type,
   maybe we just need a flag for that? */
#define KBC_VEN_ACER		0x50
/* AMI KF/KH/AMIKey/AMIKey-2 */
#define KBC_VEN_AMI		0xf0
/* Standard AMI commands, differs in input port bits */
#define KBC_VEN_INTEL_AMI	0xf8
#define KBC_VEN_MASK		0xf8


/* Flags should be fully 32-bit:
	Bits  7- 0: Vendor and revision/variant;
	Bits 15- 8: Input port mask;
	Bits 23-16: Input port bits that are always on;
	Bits 31-24: Flags:
		Bit 0: Invert P1 video type bit polarity;
		Bit 1: Is PS/2;
		Bit 2: Translation forced always off.

	So for example, the IBM PS/2 type 1 controller flags would be: 00000010 00000000 11111111 00000000 = 0200ff00 . */


typedef struct {
    uint8_t	*c_in, *c_data,		/* Data to controller */
		*d_in, *d_data,		/* Data to device */
		*inhibit;
    
    void	(*process)(void *priv);
    void	*priv;
} kbc_dev_t;

typedef struct {
    uint8_t	status, ib, ob, p1, p2, old_p2, p2_locked, fast_a20_phase,
		secr_phase, mem_index, ami_stat, ami_mode,
		kbc_in, kbc_cmd, kbc_in_cmd, kbc_poll_phase, kbc_to_send,
		kbc_send_pending, kbc_channel, kbc_stat_hi, kbc_wait_for_response, inhibit;

    uint8_t	mem_int[0x40], mem[0x240];

    uint16_t	last_irq, kbc_phase;

    uint32_t	flags;

    kbc_dev_t *	kbc_devs[2];

    pc_timer_t	pulse_cb, send_delay_timer;

    uint8_t	(*write60_ven)(void *p, uint8_t val);
    uint8_t	(*write64_ven)(void *p, uint8_t val);

    void *	log;
} atkbc_t;


enum
{
    CHANNEL_KBC = 0,
    CHANNEL_KBD,
    CHANNEL_MOUSE
};

enum
{
    KBD_MAIN_LOOP = 0,
    KBD_CMD_PROCESS
};

enum
{
    MOUSE_MAIN_LOOP_1 = 0,
    MOUSE_CMD_PROCESS,
    MOUSE_CMD_END,
    MOUSE_MAIN_LOOP_2
};

enum {
    KBC_MAIN_LOOP = 0,
    KBC_RESET = 1,
    KBC_WAIT = 4,
    KBC_WAIT_FOR_KBD,
    KBC_WAIT_FOR_MOUSE,
    KBC_WAIT_FOR_BOTH
};


static void	kbc_wait(atkbc_t *dev, uint8_t flags);


/* Bits 0 - 1 = scan code set, bit 6 = translate or not. */
uint8_t		keyboard_mode = 0x42;

uint8_t *	ami_copr = (uint8_t *) "(C)1994 AMI";


uint8_t		mouse_queue[16];
int		mouse_queue_start = 0, mouse_queue_end = 0;
static void	(*mouse_write)(uint8_t val, void *priv) = NULL;
static void	*mouse_p = NULL;
static uint8_t	sc_or = 0;
static atkbc_t	*saved_kbc = NULL;


/* Non-translated to translated scan codes. */
static const uint8_t nont_to_t[256] = {
  0xff, 0x43, 0x41, 0x3f, 0x3d, 0x3b, 0x3c, 0x58,
  0x64, 0x44, 0x42, 0x40, 0x3e, 0x0f, 0x29, 0x59,
  0x65, 0x38, 0x2a, 0x70, 0x1d, 0x10, 0x02, 0x5a,
  0x66, 0x71, 0x2c, 0x1f, 0x1e, 0x11, 0x03, 0x5b,
  0x67, 0x2e, 0x2d, 0x20, 0x12, 0x05, 0x04, 0x5c,
  0x68, 0x39, 0x2f, 0x21, 0x14, 0x13, 0x06, 0x5d,
  0x69, 0x31, 0x30, 0x23, 0x22, 0x15, 0x07, 0x5e,
  0x6a, 0x72, 0x32, 0x24, 0x16, 0x08, 0x09, 0x5f,
  0x6b, 0x33, 0x25, 0x17, 0x18, 0x0b, 0x0a, 0x60,
  0x6c, 0x34, 0x35, 0x26, 0x27, 0x19, 0x0c, 0x61,
  0x6d, 0x73, 0x28, 0x74, 0x1a, 0x0d, 0x62, 0x6e,
  0x3a, 0x36, 0x1c, 0x1b, 0x75, 0x2b, 0x63, 0x76,
  0x55, 0x56, 0x77, 0x78, 0x79, 0x7a, 0x0e, 0x7b,
  0x7c, 0x4f, 0x7d, 0x4b, 0x47, 0x7e, 0x7f, 0x6f,
  0x52, 0x53, 0x50, 0x4c, 0x4d, 0x48, 0x01, 0x45,
  0x57, 0x4e, 0x51, 0x4a, 0x37, 0x49, 0x46, 0x54,
  0x80, 0x81, 0x82, 0x41, 0x54, 0x85, 0x86, 0x87,
  0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
  0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
  0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
  0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
  0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
  0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
  0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
  0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
  0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
  0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
  0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
  0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
  0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
  0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
  0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};


#define UISTR_LEN	256
static char	kbc_str[UISTR_LEN];	/* UI output string */


extern void	ui_sb_bugui(char *__str);


static void
kbc_status(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsprintf(kbc_str, fmt, ap);
    ui_sb_bugui(kbc_str);
    va_end(ap);
}


#define ENABLE_KBC_AT_LOG 1
#if (!defined(RELEASE_BUILD) && defined(ENABLE_KBC_AT_LOG))
int kbc_at_do_log = ENABLE_KBC_AT_LOG;


static void
kbc_log(atkbc_t *dev, const char *fmt, ...)
{
    va_list ap;

    if ((dev == NULL) || (dev->log == NULL))
	return;

    if (kbc_at_do_log) {
	va_start(ap, fmt);
	log_out(dev->log, fmt, ap);
	va_end(ap);
    }
}
#else
#define kbc_log(dev, fmt, ...)
#endif


static void
kbc_send_to_ob(atkbc_t *dev, uint8_t val, uint8_t channel, uint8_t stat_hi)
{
    uint8_t ch = (channel > 0) ? channel : 1;
    uint8_t do_irq = (dev->mem[0x20] & ch);
    int translate = (channel == 1) && (keyboard_mode & 0x60);

    if ((channel == 2) && !(dev->flags & KBC_FLAG_PS2))
	return;

    stat_hi |= dev->inhibit;

    if (!dev->kbc_send_pending) {
	dev->kbc_send_pending = 1;
	dev->kbc_to_send = val;
	dev->kbc_channel = channel;
	dev->kbc_stat_hi = stat_hi;
	return;
    }

    if (translate) {
	/* Allow for scan code translation. */
	if (val == 0xf0) {
		kbc_log(dev, "Translate is on, F0 prefix detected\n");
		sc_or = 0x80;
		return;
	}

	/* Skip break code if translated make code has bit 7 set. */
	if ((sc_or == 0x80) && (val & 0x80)) {
		kbc_log(dev, "Translate is on, skipping scan code: %02X (original: F0 %02X)\n", nont_to_t[val], val);
		sc_or = 0;
		return;
	}
    }

    dev->last_irq = (ch == 2) ? 0x1000 : 0x0002;
    if (do_irq) {
	kbc_log(dev, "[%04X:%08X] IRQ %i\n", CS, cpu_state.pc, (ch == 2) ? 12 : 1);
	picint(dev->last_irq);
    }
    kbc_log(dev, "%02X coming from channel %i (%i)\n", val, channel, do_irq);
    dev->ob = translate ? (nont_to_t[val] | sc_or) : val;

    dev->status = (dev->status & 0x0f) | (stat_hi | (dev->mem[0x20] & STAT_SYSFLAG) | STAT_OFULL);
    if (ch == 2)
	dev->status |= STAT_MFULL;

    if (translate && (sc_or == 0x80))
	sc_or = 0;
}


static void
write_output(atkbc_t *dev, uint8_t val)
{
    uint8_t kbc_ven = dev->flags & KBC_VEN_MASK;
    kbc_log(dev, "Write output port: %02X (old: %02X)\n", val, dev->p2);

    if ((kbc_ven == KBC_VEN_AMI) || (dev->flags & KBC_FLAG_PS2))
	val |= ((dev->mem[0x20] << 4) & 0x30);

    dev->kbc_devs[0]->inhibit = (val & 0x40);
    dev->kbc_devs[1]->inhibit = (val & 0x08);

    if ((dev->p2 ^ val) & 0x20) { /*IRQ 12*/
	if (val & 0x20) {
		kbc_log(dev, "write_output(): IRQ 12\n");
		picint(1 << 12);
	} else
		picintc(1 << 12);
    }
    if ((dev->p2 ^ val) & 0x10) { /*IRQ 1*/
	if (val & 0x10) {
		kbc_log(dev, "write_output(): IRQ  1\n");
		picint(1 << 1);
	} else
		picintc(1 << 1);
    }
    if ((dev->p2 ^ val) & 0x02) { /*A20 enable change*/
	mem_a20_key = val & 0x02;
	mem_a20_recalc();
	flushmmucache();
    }
    if ((dev->p2 ^ val) & 0x01) { /*Reset*/
	if (! (val & 0x01)) {
		/* Pin 0 selected. */
		softresetx86(); /*Pulse reset!*/
		cpu_set_edx();
		smbase = is_am486dxl ? 0x00060000 : 0x00030000;
	}
    }
    /* Mask off the A20 stuff because we use mem_a20_key directly for that. */
    dev->p2 = val;
}


static void
write_cmd(atkbc_t *dev, uint8_t val)
{
    uint8_t kbc_ven = dev->flags & KBC_VEN_MASK;
    kbc_log(dev, "Write command byte: %02X (old: %02X)\n", val, dev->mem[0x20]);

    /* PS/2 type 2 keyboard controllers always force the XLAT bit to 0. */
    if ((dev->flags & KBC_TYPE_MASK) == KBC_TYPE_PS2_2)
	val &= ~CCB_TRANSLATE;

    dev->mem[0x20] = val;

    /* Scan code translate ON/OFF. */
    keyboard_mode &= 0x93;
    keyboard_mode |= (val & MODE_MASK);

    kbc_log(dev, "Keyboard interrupt is now %s\n",  (val & 0x01) ? "enabled" : "disabled");

    /* ISA AT keyboard controllers use bit 5 for keyboard mode (1 = PC/XT, 2 = AT);
       PS/2 (and EISA/PCI) keyboard controllers use it as the PS/2 mouse enable switch.
       The AMIKEY firmware apparently uses this bit for something else. */
    if ((kbc_ven == KBC_VEN_AMI) || (dev->flags & KBC_FLAG_PS2)) {
	keyboard_mode &= ~CCB_PCMODE;
	/* Update the output port to mirror the KBD DIS and AUX DIS bits, if active. */
	write_output(dev, dev->p2);

	kbc_log(dev, "Mouse interrupt is now %s\n",  (val & 0x02) ? "enabled" : "disabled");
    }

    kbc_log(dev, "Command byte now: %02X (%02X)\n", dev->mem[0x20], val);

    dev->status = (dev->status & ~STAT_SYSFLAG) | (val & STAT_SYSFLAG);
}


static void
pulse_output(atkbc_t *dev, uint8_t mask)
{
    if (mask != 0x0f) {
    	dev->old_p2 = dev->p2 & ~(0xf0 | mask);
	kbc_log(dev, "pulse_output(): Output port now: %02X\n", dev->p2 & (0xf0 | mask | (dev->mem[0x20] & 0x30)));
    	write_output(dev, dev->p2 & (0xf0 | mask | (dev->mem[0x20] & 0x30)));
    	timer_set_delay_u64(&dev->pulse_cb, 6ULL * TIMER_USEC);
    }
}


static void
set_enable_kbd(atkbc_t *dev, uint8_t enable)
{
    dev->mem[0x20] &= 0xef;
    dev->mem[0x20] |= (enable ? 0x00 : 0x10);
}


static void
set_enable_mouse(atkbc_t *dev, uint8_t enable)
{
    dev->mem[0x20] &= 0xdf;
    dev->mem[0x20] |= (enable ? 0x00 : 0x20);
}


static void
kbc_transmit(atkbc_t *dev, uint8_t val)
{
    kbc_send_to_ob(dev, val, 0, 0x00);
}


static void
kbc_command(atkbc_t *dev)
{
    uint8_t mask, val = dev->ib;
    uint8_t kbc_ven = dev->flags & KBC_VEN_MASK;
    int bad = 1;

    if ((dev->kbc_phase > 0) && (dev->kbc_cmd == 0xac)) {
	if (dev-> kbc_phase < 16)
		kbc_transmit(dev, dev->mem[dev->kbc_phase]);
	else if (dev-> kbc_phase == 16)
		kbc_transmit(dev, (dev->p1 & 0xf0) | 0x80);
	else if (dev-> kbc_phase == 17)
		kbc_transmit(dev, dev->p2);
	else if (dev-> kbc_phase == 18)
		kbc_transmit(dev, dev->status);

	dev->kbc_phase++;
	if (dev->kbc_phase == 19) {
		dev->kbc_phase = 0;
		dev->kbc_cmd = 0x00;
	}
	return;
    } else if ((dev->kbc_phase > 0) && (dev->kbc_cmd == 0xa0) && (kbc_ven >= KBC_VEN_AMI)) {
	val = ami_copr[dev->kbc_phase];
	kbc_transmit(dev, val);
	if (val == 0x00) {
		dev->kbc_phase = 0;
		dev->kbc_cmd = 0x00;
	} else
		dev->kbc_phase++;
	return;
    } else if ((dev->kbc_in > 0) && (dev->kbc_cmd == 0xa5) && (dev->flags & KBC_FLAG_PS2)) {
	/* load security */
	kbc_log(dev, "Load security\n");
	dev->mem[0x50 + dev->kbc_in - 0x01] = val;
	if ((dev->kbc_in == 0x80) && (val != 0x00)) {
		/* Security string too long, set it to 0x00. */
		dev->mem[0x50] = 0x00;
		dev->kbc_in = 0;
		dev->kbc_cmd = 0;
	} else if (val == 0x00) {
		/* Security string finished. */
		dev->kbc_in = 0;
		dev->kbc_cmd = 0;
	} else	/* Increase pointer and request another byte. */
		dev->kbc_in++;
	return;
    }

    /* If the written port is 64, go straight to the beginning of the command. */
    if (!(dev->status & STAT_CD) && dev->kbc_in) {
	/* Write data to controller. */
	dev->kbc_in = 0;
	dev->kbc_phase = 0;

	switch (dev->kbc_cmd) {
		case 0x60 ... 0x7f:
			if (dev->kbc_cmd == 0x60)
				write_cmd(dev, val);
			else
				dev->mem[(dev->kbc_cmd & 0x1f) + 0x20] = val;
			break;

		case 0xc7:	/* or input port with system data */
			dev->p1 |= val;
			break;

		case 0xcb:	/* set keyboard mode */
			kbc_log(dev, "New AMIKey mode: %02X\n", val);
			dev->ami_mode = val;
			dev->flags &= ~KBC_FLAG_PS2;
			if (val & 1)
				dev->flags |= KBC_FLAG_PS2;
#if (!defined(RELEASE_BUILD) && defined(ENABLE_KBD_AT_LOG))
			log_set_dev_name(dev->kbc_log, (dev->flags & KBC_FLAG_PS2) ? "AT KBC" : "PS/2 KBC");
#endif
			break;

		case 0xd1: /* write output port */
			if (dev->p2_locked) {
				/*If keyboard controller lines P22-P23 are blocked,
				  we force them to remain unchanged.*/
				val &= ~0x0c;
				val |= (dev->p2 & 0x0c);
			}
			kbc_log(dev, "Write %02X to output port\n", val);
			write_output(dev, val);
			break;

		case 0xd2: /* write to keyboard output buffer */
			kbc_log(dev, "Write %02X to keyboard output buffer\n", val);
			/* Should be channel 1, but we send to 0 to avoid translation,
			   since bytes output using this command do *NOT* get translated. */
			kbc_send_to_ob(dev, val, 0, 0x00);
			break;

		case 0xd3: /* write to mouse output buffer */
			kbc_log(dev, "Write %02X to mouse output buffer\n", val);
			if (dev->flags & KBC_FLAG_PS2)
				kbc_send_to_ob(dev, val, 2, 0x00);
			break;

		case 0xd4: /* write to mouse */
			kbc_log(dev, "Write %02X to mouse\n", val);

			if (dev->flags & KBC_FLAG_PS2) {
				set_enable_mouse(dev, 1);
				dev->mem[0x20] &= ~0x20;
				if (dev->kbc_devs[1] && !dev->kbc_devs[1]->c_in) {
					kbc_log(dev, "Transmitting %02X to mouse...\n", dev->ib);
					dev->kbc_devs[1]->d_data = val;
					dev->kbc_devs[1]->d_in = 1;
					dev->kbc_wait_for_response = 2;
				} else
					kbc_send_to_ob(dev, 0xfe, 2, 0x40);
			}
			break;

		default:
			/*
			 * Run the vendor-specific handler
			 * if we have one. Otherwise, or if
			 * it returns an error, log a bad
			 * controller command.
			 */
			if (dev->write60_ven)
				bad = dev->write60_ven(dev, val);

			if (bad)
				kbc_log(dev, "Bad controller command %02x data %02x\n", dev->kbc_cmd, val);
	}
    } else {
	/* Controller command. */
	kbc_log(dev, "Controller command: %02X\n", val);
	dev->kbc_in = 0;
	dev->kbc_phase = 0;

	switch (val) {
		/* Read data from KBC memory. */
		case 0x20 ... 0x3f:
			kbc_transmit(dev, dev->mem[(val & 0x1f) + 0x20]);
			break;

		/* Write data to KBC memory. */
		case 0x60 ... 0x7f:
			dev->kbc_in = 1;
			break;

		case 0xaa:	/* self-test */
			kbc_log(dev, "Self-test\n");
			write_output(dev, (dev->flags & KBC_FLAG_PS2) ? 0x4b : 0xcf);

			/* Always reinitialize all queues - the real hardware pulls keyboard and mouse
			   clocks high, which stops keyboard scanning. */
			dev->in_cmd = dev->mouse_in_cmd = 0;
			dev->status &= ~STAT_OFULL;
			dev->last_irq = 0;
			dev->kbc_phase = 0;

			/* Phoenix MultiKey should have 0x60 | STAT_SYSFLAG. */
			if (dev->flags & KBC_FLAG_PS2)
				write_cmd(dev, 0x30 | STAT_SYSFLAG);
			else
				write_cmd(dev, 0x10 | STAT_SYSFLAG);
			kbc_transmit(dev, 0x55);
			break;

		case 0xab:	/* interface test */
			kbc_log(dev, "Interface test\n");
			/* No error. */
			kbc_transmit(dev, 0x00);
			break;

		case 0xac:	/* diagnostic dump */
			kbc_log(dev, "Diagnostic dump\n");
			kbc_transmit(dev, dev->mem[0x20]);
			dev->kbc_phase = 1;
			break;

		case 0xad:	/* disable keyboard */
			kbc_log(dev, "Disable keyboard\n");
			set_enable_kbd(dev, 0);
			break;

		case 0xae:	/* enable keyboard */
			kbc_log(dev, "Enable keyboard\n");
			set_enable_kbd(dev, 1);
			break;

		case 0xc7:	/* or input port with system data */
			kbc_log(dev, "Phoenix - or input port with system data\n");
			dev->kbc_in = 1;
			break;

		case 0xca:	/* read keyboard mode */
			kbc_log(dev, "AMI - Read keyboard mode\n");
			kbc_transmit(dev, dev->ami_mode);
			break;

		case 0xcb:	/* set keyboard mode */
			kbc_log(dev, "ATkbc: AMI - Set keyboard mode\n");
			dev->kbc_in = 1;
			break;

		case 0xd0:	/* read output port */
			kbc_log(dev, "Read output port\n");
			mask = 0xff;
			if (dev->mem[0x20] & 0x10)
				mask &= 0xbf;
			if ((dev->flags & KBC_FLAG_PS2) && (dev->mem[0x20] & 0x20))
				mask &= 0xf7;
			kbc_transmit(dev, dev->p2 & mask);
			break;

		case 0xd1:	/* write output port */
			kbc_log(dev, "Write output port\n");
			dev->kbc_in = 1;
			break;

		case 0xd2:	/* write keyboard output buffer */
			kbc_log(dev, "Write keyboard output buffer\n");
			if (dev->flags & KBC_FLAG_PS2)
				dev->kbc_in = 1;
			else
				kbc_transmit(dev, 0x00);	/* NCR */
			break;

		case 0xdd:	/* disable A20 address line */
		case 0xdf:	/* enable A20 address line */
			kbc_log(dev, "%sable A20\n", (val == 0xdd) ? "Dis": "En");
			write_output(dev, (dev->p2 & 0xfd) | (val & 0x02));
			break;

		case 0xe0:	/* read test inputs */
			kbc_log(dev, "Read test inputs\n");
			kbc_transmit(dev, 0x00);
			break;

		default:
			/*
			 * Unrecognized controller command.
			 *
			 * If we have a vendor-specific handler, run
			 * that. Otherwise, or if that handler fails,
			 * log a bad command.
			 */
			if (dev->write64_ven)
				bad = dev->write64_ven(dev, val);

			if (bad)
				kbc_log(dev, "Bad controller command %02X\n", val);
	}

	/* If the command needs data, remember the command. */
	if (dev->kbc_in || (dev->kbc_phase > 0))
		dev->kbc_cmd = val;
    }
}


static void
kbc_dev_data_to_ob(atkbc_t *dev, uint8_t channel)
{
    if (channel == 0)
	return;

    dev->kbc_devs[channel - 1]->c_in = 0;
    kbc_log(dev, "Forwarding %02X from channel %i...\n", dev->kbc_devs[channel - 1]->c_data, channel);
    kbc_send_to_ob(dev, dev->kbc_devs[channel - 1]->c_data, channel, 0x00);
}


static void
kbc_main_loop_scan(atkbc_t *dev)
{
    uint8_t port_dis = dev->mem[0x20] & 0x30;
    uint8_t ps2 = (dev->flags & KBC_FLAG_PS2);

    if (!ps2)
	port_dis |= 0x20;

    if (!(dev->status & STAT_OFULL)) {
	if (port_dis & 0x20) {
		if (!(port_dis & 0x10)) {
			kbc_log(dev, "kbc_process(): Main loop, Scan: AUX DIS, KBD EN\n");
			/* Enable communication with keyboard. */
			dev->p2 &= 0xbf;
			dev->kbc_devs[0]->inhibit = 0;
			kbc_wait(dev, 1);
		} else
			kbc_log(dev, "kbc_process(): Main loop, Scan: AUX DIS, KBD DIS\n");
	} else {
		/* Enable communication with mouse. */
		dev->p2 &= 0xf7;
		dev->kbc_devs[1]->inhibit = 0;
		if (dev->mem[0x20] & 0x10) {
			kbc_log(dev, "kbc_process(): Main loop, Scan: AUX EN , KBD DIS\n");
			kbc_wait(dev, 2);
		} else {
			/* Enable communication with keyboard. */
			kbc_log(dev, "kbc_process(): Main loop, Scan: AUX EN , KBD EN\n");
			dev->p2 &= 0xbf;
			dev->kbc_devs[0]->inhibit = 0;
			kbc_wait(dev, 3);
		}
	}
    } else
	kbc_log(dev, "kbc_process(): Main loop, Scan: IBF not full and OBF full, do nothing\n");
}


static uint8_t
kbc_reset_cmd(atkbc_t *dev)
{
    uint8_t ret = 0;

    if ((dev->status & STAT_CD) || (dev->kbc_poll_phase == KBC_WAIT_FOR_NOBF)) {
	kbc_log(dev, "    Resetting command\n");
    	dev->kbc_phase = 0;
	dev->kbc_in = 0;
	dev->kbc_in_cmd = 0;
	dev->kbc_poll_phase = KBC_MAIN_LOOP;
	ret = 1;
    }

    return ret;
}


static uint8_t
kbc_process_cmd(atkbdt_t *dev, uint8_t restart)
{
    uint8_t ret = 0;

    if (restart)
	dev->kbc_in_cmd = 1;
    kbc_command(dev);

    if ((dev->kbc_phase == 0) && !dev->kbc_in)
	dev->kbc_in_cmd = 0;
    else
	ret = 1;

    dev->kbc_poll_phase = KBC_MAIN_LOOP;
    if (!dev->kbc_wait_for_response && !(dev->status & STAT_OFULL))
	kbc_main_loop_scan(dev);

    return ret;
}


static void
kbc_process_ib(atkbc_t *dev)
{
    if ((dev->status & STAT_CD) || (kbc->flags & KBC_FLAG_PS2) || !(dev->status & STAT_OFULL))
	dev->status &= ~STAT_IFULL;

    if (dev->status & STAT_CD)
        (void) kbc_process_cmd(dev, 1);
    else if ((kbc->flags & KBC_FLAG_PS2) || !(dev->status & STAT_OFULL))
	/* The AT KBC does *NOT* send data to the keyboard if OBF. */
	set_enable_mouse(dev, 1);
	dev->mem[0x20] &= ~0x10;
	if (dev->kbc_devs[0] && !dev->kbc_devs[0]->c_in) {
		dev->kbc_devs[0]->d_data = val;
		dev->kbc_devs[0]->d_in = 1;
		dev->kbc_wait_for_response = 1;
	} else
		kbc_send_to_ob(dev, 0xfe, 1, 0x40);

	dev->kbc_poll_phase = KBC_MAIN_LOOP;
	if (!dev->kbc_wait_for_response && !(dev->status & STAT_OFULL))
		kbc_main_loop_scan(dev);
    }
}


static void
kbc_wait(atkbc_t *dev, uint8_t flags)
{
    if ((flags & 1) && dev->kbc_devs[0]->c_in) {
	/* Disable communication with mouse. */
	dev->p2 |= 0x08;
	dev->kbc_devs[1]->inhibit = 1;
	/* Send keyboard byte to host. */
	kbc_dev_data_to_ob(dev, CHANNEL_KBD);
	dev->kbc_poll_phase = KBC_MAIN_LOOP;
    } else if ((flags & 2) && dev->kbc_devs[1]->c_in) {
	/* Disable communication with keyboard. */
	dev->p2 |= 0x40;
	dev->kbc_devs[0]->inhibit = 1;
	/* Send mouse byte to host. */
	kbc_dev_data_to_ob(dev, CHANNEL_MOUSE);
	dev->kbc_poll_phase = KBC_MAIN_LOOP;
    } else if (dev->status & STAT_IFULL) {
	/* Disable communication with keyboard and mouse. */
	dev->p2 |= 0x48;
	dev->kbc_devs[0]->inhibit = dev->kbc_devs[1]->inhibit = 1;
	kbc_process_ib(dev);
    } else
	dev->kbc_poll_phase = KBC_WAIT | flags;
}


/* Controller processing */
static void
kbc_process(atkbc_t *dev)
{
    /* If we're waiting for the response from the keyboard or mouse, do nothing
       until the device has repsonded back. */
    if (dev->kbc_wait_for_response > 0) {
	if (dev->kbc_devs[dev->kbc_wait_for_response - 1]->c_in)
		dev->kbc_wait_for_response = 0;
	else
		return;
    }

    if (dev->kbc_send_pending) {
	kbc_log(dev, "Sending delayed %02X on channel %i with high status %02X\n",
		dev->kbc_to_send, dev->kbc_channel, dev->kbc_stat_hi);
	kbc_send_to_ob(dev, dev->kbc_to_send, dev->kbc_channel, dev->kbc_stat_hi);
	dev->kbc_send_pending = 0;
    }

    /* Make absolutely sure to do nothing if OBF is full and IBF is empty. */
    if ((dev->kbc_poll_phase == KBC_RESET) || (dev->kbc_poll_phase >= KBC_WAIT_FOR_NIBF) ||
	!(dev->status & STAT_OFULL) || (dev->status & STAT_IFULL))  switch (dev->kbc_poll_phase) {
	case KBC_RESET:
		kbc_log(dev, "kbc_process(): Reset loop()\n");

		if (dev->status & STAT_IFULL)  {
			dev->status &= ~STAT_IFULL;

			if ((dev->status & STAT_CD) && (dev->ib == 0xaa)) {
				(void) kbc_process_cmd(dev, 1);
				dev->kbc_poll_phase = KBC_MAIN_LOOP;
			}
		}
		break;
	case KBC_MAIN_LOOP:
		if (dev->status & STAT_IFULL) {
			kbc_log(dev, "kbc_process(): Main loop, IBF full, process\n");
			kbc_process_ib(dev);
		} else
			kbc_main_loop_scan(dev);
		break;
	case KBC_SCAN_KBD:
	case KBC_SCAN_MOUSE:
	case KBC_SCAN_BOTH:
		kbc_log(dev, "kbc_process(): Scan: Phase %i\n", dev->kbc_poll_phase);
		kbc_wait(dev, dev->kbc_poll_phase & 3);
		break;
	case KBC_WAIT_FOR_NOBF:
		kbc_log(dev, "kbc_process(): Waiting for !OBF\n");

		if (dev->status & STAT_IFULL) {
			/* Host writing a command aborts the current command. */
			(void) !kbc_reset_cmd(dev);

			/* Process the input buffer. */
			kbc_process_ib(dev);
		} else if (!dev->status & STAT_OFULL) {
			/* Not aborted and OBF cleared - process command. */
			kbc_log(dev, "    Continuing commmand\n");

			if (kbc_process_cmd(dev, 0))
				return;
		}
		break;
	case KBC_WAIT_FOR_IBF:
		kbc_log(dev, "kbc_process(): Waiting for IBF\n");

		if (dev->status & STAT_IFULL) {
			/* IBF, process if port 60h, otherwise abort the current command. */
			dev->status &= ~STAT_IFULL;

			if (!kbc_reset_cmd(dev))
				kbc_log(dev, "    Continuing commmand\n");

			/* Process command. */
			if (kbc_process_cmd(dev, 0))
				return;
		}
		break;
	default:
		kbc_log(dev, "kbc_process(): Invalid phase %i\n", dev->kbc_poll_phase);
		break;
    }
}


static void
kbd_poll(void *priv)
{
    atkbc_t *dev = (atkbc_t *) priv;
    uint8_t i;

    if (dev == NULL)
	return;

    timer_advance_u64(&dev->send_delay_timer, (100ULL * TIMER_USEC));

    /* Device processing */
    for (i = 0; i < 2; i++) {
	if (dev->kbc_devs[i] && dev->kbd_devs[i]->priv && dev->kbd_devs[i]->process)
		dev->kbc_devs[i]->process(dev->kbc_devs[i]->priv);
    }

    /* Controller processing */
    kbc_process(dev);
}


static void
pulse_poll(void *priv)
{
    atkbc_t *dev = (atkbc_t *)priv;

    kbc_log(dev, "pulse_poll(): Output port now: %02X\n", dev->p2 | dev->old_p2);
    write_output(dev, dev->p2 | dev->old_p2);
}


static uint8_t
write64_generic(void *priv, uint8_t val)
{
    atkbc_t *dev = (atkbc_t *)priv;
    uint8_t current_drive, fixed_bits;
    uint8_t kbc_ven = 0x0;
    kbc_ven = dev->flags & KBC_VEN_MASK;

    switch (val) {
	case 0xa4:	/* check if password installed */
		if (dev->flags & KBC_FLAG_PS2) {
			kbc_log(dev, "Check if password installed\n");
			kbc_transmit(dev, (dev->mem[0x50] == 0x00) ? 0xf1 : 0xfa);
			return 0;
		}
		break;

	case 0xa5:	/* load security */
		if (dev->flags & KBC_FLAG_PS2) {
			kbc_log(dev, "Load security\n");
			dev->kbc_in = 1;
			return 0;
		}
		break;

	case 0xa7:	/* disable mouse port */
		if (dev->flags & KBC_FLAG_PS2) {
			kbc_log(dev, "Disable mouse port\n");
			return 0;
		}
		break;

	case 0xa8:	/*Enable mouse port*/
		if (dev->flags & KBC_FLAG_PS2) {
			kbc_log(dev, "Enable mouse port\n");
			return 0;
		}
		break;

	case 0xa9:	/*Test mouse port*/
		kbc_log(dev, "Test mouse port\n");
		if (dev->flags & KBC_FLAG_PS2) {
			/* No error, this is testing the channel 2 interface. */
			kbc_transmit(dev, 0x00);
			return 0;
		}
		break;

	case 0xaf:	/* read keyboard version */
		kbc_log(dev, "Read keyboard version\n");
		kbc_transmit(dev, 0x00);
		return 0;

	case 0xc0:	/* read input port */
		/* IBM PS/1:
			Bit 2 and 4 ignored (we return always 0),
			Bit 6 must 1 for 5.25" floppy drive, 0 for 3.5".
		   Intel AMI:
			Bit 2 ignored (we return always 1),
			Bit 4 must be 1,
			Bit 6 must be 1 or else error in SMM.
		   Acer:
			Bit 2 must be 0,
			Bit 4 must be 0,
			Bit 6 ignored.
		   P6RP4:
			Bit 2 must be 1 or CMOS setup is disabled. */
		kbc_log(dev, "Read input port\n");
		fixed_bits = 4;
		/* The SMM handlers of Intel AMI Pentium BIOS'es expect bit 6 to be set. */
		if (kbc_ven == KBC_VEN_INTEL_AMI)
			fixed_bits |= 0x40;
		if (kbc_ven == KBC_VEN_IBM_PS1) {
			current_drive = fdc_get_current_drive();
			kbc_transmit(dev, dev->p1 | fixed_bits | (fdd_is_525(current_drive) ? 0x40 : 0x00));
			dev->p1 = ((dev->p1 + 1) & 3) | (dev->p1 & 0xfc) | (fdd_is_525(current_drive) ? 0x40 : 0x00);
		} else if (kbc_ven == KBC_VEN_NCR) {
			/* switch settings
			 * bit 7: keyboard disable
			 * bit 6: display type (0 color, 1 mono)
			 * bit 5: power-on default speed (0 high, 1 low)
			 * bit 4: sense RAM size (0 unsupported, 1 512k on system board)
			 * bit 3: coprocessor detect
			 * bit 2: unused
			 * bit 1: high/auto speed
			 * bit 0: dma mode
			 */
			kbc_transmit(dev, (dev->p1 | fixed_bits | (video_is_mda() ? 0x40 : 0x00) | (hasfpu ? 0x08 : 0x00)) & 0xdf);
			dev->p1 = ((dev->p1 + 1) & 3) | (dev->p1 & 0xfc);
		} else {
			if ((dev->flags & KBC_FLAG_PS2) && ((dev->flags & KBC_VEN_MASK) != KBC_VEN_INTEL_AMI))
				kbc_transmit(dev, (dev->p1 | fixed_bits) & (((dev->flags & KBC_VEN_MASK) == KBC_VEN_ACER) ? 0xeb : 0xef));
			else
				kbc_transmit(dev, dev->p1 | fixed_bits);
			dev->p1 = ((dev->p1 + 1) & 3) | (dev->p1 & 0xfc);
		}
		return 0;

	case 0xd3:	/* write mouse output buffer */
		if (dev->flags & KBC_FLAG_PS2) {
			kbc_log(dev, "Write mouse output buffer\n");
			dev->kbc_in = 1;
			return 0;
		}
		break;

	case 0xd4:	/* write to mouse */
		kbc_log(dev, "Write to mouse\n");
		dev->kbc_in = 1;
		return 0;

	case 0xf0 ... 0xff:
		kbc_log(dev, "Pulse %01X\n", val & 0x0f);
		pulse_output(dev, val & 0x0f);
		return 0;
    }

    kbc_log(dev, "Bad command %02X\n", val);
    return 1;
}


static uint8_t
write60_ami(void *priv, uint8_t val)
{
    atkbc_t *dev = (atkbc_t *)priv;
    uint16_t index = 0x00c0;

    switch(dev->kbc_cmd) {
	/* 0x40 - 0x5F are aliases for 0x60 - 0x7F */
	case 0x40 ... 0x5f:
		kbc_log(dev, "AMI - Alias write to %08X\n", dev->kbc_cmd);
		if (dev->kbc_cmd == 0x40)
			write_cmd(dev, val);
		else
			dev->mem[(dev->kbc_cmd & 0x1f) + 0x20] = val;
		return 0;

	case 0xaf:	/* set extended controller RAM */
		kbc_log(dev, "AMI - Set extended controller RAM, input phase %i\n", dev->secr_phase);
		if (dev->secr_phase == 0) {
			dev->mem_index = val;
			dev->kbc_in = 1;
			dev->secr_phase++;
		} else if (dev->secr_phase == 1) {
			if (dev->mem_index == 0x20)
				write_cmd(dev, val);
			else
				dev->mem[dev->mem_index] = val;
			dev->secr_phase = 0;
		}
		return 0;

	case 0xb8:
		kbc_log(dev, "AMIKey-3 - Memory index %02X\n", val);
		dev->mem_index = val;
		return 0;

	case 0xbb:
		kbc_log(dev, "AMIKey-3 - write %02X to memory index %02X\n", val, dev->mem_index);
		if (dev->mem_index >= 0x80) {
			switch (dev->mem[0x9b] & 0xc0) {
				case 0x00:
					index = 0x0080;
					break;
				case 0x40: case 0x80:
					index = 0x0000;
					break;
				case 0xc0:
					index = 0x0100;
					break;
			}
			dev->mem[index + dev->mem_index] = val;
		} else if (dev->mem_index == 0x60)
			write_cmd(dev, val);
		else if (dev->mem_index == 0x42)
			dev->status = val;
		else if (dev->mem_index >= 0x40)
			dev->mem[dev->mem_index - 0x40] = val;
		else
			dev->mem_int[dev->mem_index] = val;
		return 0;

	case 0xbd:
		kbc_log(dev, "AMIKey-3 - write %02X to config index %02X\n", val, dev->mem_index);
		switch (dev->mem_index) {
			case 0x00:	/* STAT8042 */
				dev->status = val;
				break;
			case 0x01:	/* Password_ptr */
				dev->mem[0x1c] = val;
				break;
			case 0x02:	/* Wakeup_Tsk_Reg */
				dev->mem[0x1e] = val;
				break;
			case 0x03:	/* CCB */
				write_cmd(dev, val);
				break;
			case 0x04:	/* Debounce_time */
				dev->mem[0x4d] = val;
				break;
			case 0x05:	/* Pulse_Width */
				dev->mem[0x4e] = val;
				break;
			case 0x06:	/* Pk_sel_byte */
				dev->mem[0x4c] = val;
				break;
			case 0x07:	/* Func_Tsk_Reg */
				dev->mem[0x7e] = val;
				break;
			case 0x08:	/* TypematicRate */
				dev->mem[0x80] = val;
				break;
			case 0x09:	/* Led_Flag_Byte */
				dev->mem[0x81] = val;
				break;
			case 0x0a:	/* Kbms_Command_St */
				dev->mem[0x87] = val;
				break;
			case 0x0b:	/* Delay_Count_Byte */
				dev->mem[0x86] = val;
				break;
			case 0x0c:	/* KBC_Flags */
				dev->mem[0x9b] = val;
				break;
			case 0x0d:	/* SCODE_HK1 */
				dev->mem[0x50] = val;
				break;
			case 0x0e:	/* SCODE_HK2 */
				dev->mem[0x51] = val;
				break;
			case 0x0f:	/* SCODE_HK3 */
				dev->mem[0x52] = val;
				break;
			case 0x10:	/* SCODE_HK4 */
				dev->mem[0x53] = val;
				break;
			case 0x11:	/* SCODE_HK5 */
				dev->mem[0x54] = val;
				break;
			case 0x12:	/* SCODE_HK6 */
				dev->mem[0x55] = val;
				break;
			case 0x13:	/* TASK_HK1 */
				dev->mem[0x56] = val;
				break;
			case 0x14:	/* TASK_HK2 */
				dev->mem[0x57] = val;
				break;
			case 0x15:	/* TASK_HK3 */
				dev->mem[0x58] = val;
				break;
			case 0x16:	/* TASK_HK4 */
				dev->mem[0x59] = val;
				break;
			case 0x17:	/* TASK_HK5 */
				dev->mem[0x5a] = val;
				break;
			/* The next 4 bytes have uncertain correspondences. */
			case 0x18:	/* Batt_Poll_delay_Time */
				dev->mem[0x5b] = val;
				break;
			case 0x19:	/* Batt_Alarm_Reg1 */
				dev->mem[0x5c] = val;
				break;
			case 0x1a:	/* Batt_Alarm_Reg2 */
				dev->mem[0x5d] = val;
				break;
			case 0x1b:	/* Batt_Alarm_Tsk_Reg */
				dev->mem[0x5e] = val;
				break;
			case 0x1c:	/* Kbc_State1 */
				dev->mem[0x9d] = val;
				break;
			case 0x1d:	/* Aux_Config */
				dev->mem[0x75] = val;
				break;
			case 0x1e:	/* Kbc_State3 */
				dev->mem[0x73] = val;
				break;
		}
		return 0;

	case 0xc1:	/* write input port */
		kbc_log(dev, "AMI MegaKey - write %02X to input port\n", val);
		dev->p1 = val;
		return 0;

	case 0xcb:	/* set keyboard mode */
		kbc_log(dev, "AMI - Set keyboard mode\n");
		return 0;
    }

    return 1;
}


static uint8_t
write64_ami(void *priv, uint8_t val)
{
    atkbc_t *dev = (atkbc_t *)priv;
    uint16_t index = 0x00c0;

    switch (val) {
	case 0x00 ... 0x1f:
		kbc_log(dev, "AMI - Alias read from %08X\n", val);
		kbc_transmit(dev, dev->mem[val + 0x20]);
		return 0;

	case 0x40 ... 0x5f:
		kbc_log(dev, "AMI - Alias write to %08X\n", dev->kbc_cmd);
		dev->kbc_in = 1;
		return 0;

	case 0xa0:	/* copyright message */
		kbc_log(dev, "AMI - Get copyright message\n");
		kbc_transmit(dev, ami_copr[0]);
		dev->kbc_phase = 1;
		return 0;
		
	case 0xa1:	/* get controller version */
		kbc_log(dev, "AMI - Get controller version\n");
		// kbc_transmit(dev, 'H');
		kbc_transmit(dev, '5');
		return 0;

	case 0xa2:	/* clear keyboard controller lines P22/P23 */
		if (!(dev->flags & KBC_FLAG_PS2)) {
			kbc_log(dev, "AMI - Clear KBC lines P22 and P23\n");
			write_output(dev, dev->p2 & 0xf3);
			kbc_transmit(dev, 0x00);
			return 0;
		}
		break;

	case 0xa3:	/* set keyboard controller lines P22/P23 */
		if (!(dev->flags & KBC_FLAG_PS2)) {
			kbc_log(dev, "AMI - Set KBC lines P22 and P23\n");
			write_output(dev, dev->p2 | 0x0c);
			kbc_transmit(dev, 0x00);
			return 0;
		}
		break;

	case 0xa4:	/* write clock = low */
		if (!(dev->flags & KBC_FLAG_PS2)) {
			kbc_log(dev, "AMI - Write clock = low\n");
			dev->ami_stat &= 0xfe;
			return 0;
		}
		break;

	case 0xa5:	/* write clock = high */
		if (!(dev->flags & KBC_FLAG_PS2)) {
			kbc_log(dev, "AMI - Write clock = high\n");
			dev->ami_stat |= 0x01;
			return 0;
		}
		break;

	case 0xa6:	/* read clock */
		if (!(dev->flags & KBC_FLAG_PS2)) {
			kbc_log(dev, "AMI - Read clock\n");
			kbc_transmit(dev, !!(dev->ami_stat & 1));
			return 0;
		}
		break;

	case 0xa7:	/* write cache bad */
		if (!(dev->flags & KBC_FLAG_PS2)) {
			kbc_log(dev, "AMI - Write cache bad\n");
			dev->ami_stat &= 0xfd;
			return 0;
		}
		break;

	case 0xa8:	/* write cache good */
		if (!(dev->flags & KBC_FLAG_PS2)) {
			kbc_log(dev, "AMI - Write cache good\n");
			dev->ami_stat |= 0x02;
			return 0;
		}
		break;

	case 0xa9:	/* read cache */
		if (!(dev->flags & KBC_FLAG_PS2)) {
			kbc_log(dev, "AMI - Read cache\n");
			kbc_transmit(dev, !!(dev->ami_stat & 2));
			return 0;
		}
		break;

	case 0xaf:	/* set extended controller RAM */
		kbc_log(dev, "AMI - Set extended controller RAM\n");
		dev->kbc_in = 1;
		return 0;

	case 0xb0 ... 0xb3:
		/* set KBC lines P10-P13 (input port bits 0-3) low */
		kbc_log(dev, "AMI - Set KBC lines P10-P13 (input port bits 0-3) low\n");
		if (!(dev->flags & KBC_FLAG_PS2) || (val > 0xb1)) {
			dev->p1 &= ~(1 << (val & 0x03));
		}
		kbc_transmit(dev, 0x00);
		return 0;

	case 0xb4: case 0xb5:
		/* set KBC lines P22-P23 (output port bits 2-3) low */
		kbc_log(dev, "AMI - Set KBC lines P22-P23 (output port bits 2-3) low\n");
		if (!(dev->flags & KBC_FLAG_PS2))
			write_output(dev, dev->p2 & ~(4 << (val & 0x01)));
		kbc_transmit(dev, 0x00);
		return 0;

#if 0
	case 0xb8 ... 0xbb:
#else
	case 0xb9:
#endif
		/* set KBC lines P10-P13 (input port bits 0-3) high */
		kbc_log(dev, "AMI - Set KBC lines P10-P13 (input port bits 0-3) high\n");
		if (!(dev->flags & KBC_FLAG_PS2) || (val > 0xb9)) {
			dev->p1 |= (1 << (val & 0x03));
			kbc_transmit(dev, 0x00);
		}
		return 0;

	case 0xb8:
		kbc_log(dev, "AMIKey-3 - memory index\n");
		dev->kbc_in = 1;
		return 0;

	case 0xba:
		kbc_log(dev, "AMIKey-3 - read %02X memory from index %02X\n", dev->mem[dev->mem_index], dev->mem_index);
		if (dev->mem_index >= 0x80) {
			switch (dev->mem[0x9b] & 0xc0) {
				case 0x00:
					index = 0x0080;
					break;
				case 0x40: case 0x80:
					index = 0x0000;
					break;
				case 0xc0:
					index = 0x0100;
					break;
			}
			kbc_transmit(dev, dev->mem[index + dev->mem_index]);
		} else if (dev->mem_index == 0x42)
			kbc_transmit(dev, dev->status);
		else if (dev->mem_index >= 0x40)
			kbc_transmit(dev, dev->mem[dev->mem_index - 0x40]);
		else
			kbc_transmit(dev, dev->mem_int[dev->mem_index]);
		return 0;

	case 0xbb:
		kbc_log(dev, "AMIKey-3 - write to memory index %02X\n", dev->mem_index);
		dev->kbc_in = 1;
		return 0;

#if 0
	case 0xbc: case 0xbd:
		/* set KBC lines P22-P23 (output port bits 2-3) high */
		kbc_log(dev, "AMI - Set KBC lines P22-P23 (output port bits 2-3) high\n");
		if (!(dev->flags & KBC_FLAG_PS2))
			write_output(dev, dev->p2 | (4 << (val & 0x01)));
		kbc_transmit(dev, 0x00);
		return 0;
#endif

	case 0xbc:
		switch (dev->mem_index) {
			case 0x00:	/* STAT8042 */
				kbc_transmit(dev, dev->status);
				break;
			case 0x01:	/* Password_ptr */
				kbc_transmit(dev, dev->mem[0x1c]);
				break;
			case 0x02:	/* Wakeup_Tsk_Reg */
				kbc_transmit(dev, dev->mem[0x1e]);
				break;
			case 0x03:	/* CCB */
				kbc_transmit(dev, dev->mem[0x20]);
				break;
			case 0x04:	/* Debounce_time */
				kbc_transmit(dev, dev->mem[0x4d]);
				break;
			case 0x05:	/* Pulse_Width */
				kbc_transmit(dev, dev->mem[0x4e]);
				break;
			case 0x06:	/* Pk_sel_byte */
				kbc_transmit(dev, dev->mem[0x4c]);
				break;
			case 0x07:	/* Func_Tsk_Reg */
				kbc_transmit(dev, dev->mem[0x7e]);
				break;
			case 0x08:	/* TypematicRate */
				kbc_transmit(dev, dev->mem[0x80]);
				break;
			case 0x09:	/* Led_Flag_Byte */
				kbc_transmit(dev, dev->mem[0x81]);
				break;
			case 0x0a:	/* Kbms_Command_St */
				kbc_transmit(dev, dev->mem[0x87]);
				break;
			case 0x0b:	/* Delay_Count_Byte */
				kbc_transmit(dev, dev->mem[0x86]);
				break;
			case 0x0c:	/* KBC_Flags */
				kbc_transmit(dev, dev->mem[0x9b]);
				break;
			case 0x0d:	/* SCODE_HK1 */
				kbc_transmit(dev, dev->mem[0x50]);
				break;
			case 0x0e:	/* SCODE_HK2 */
				kbc_transmit(dev, dev->mem[0x51]);
				break;
			case 0x0f:	/* SCODE_HK3 */
				kbc_transmit(dev, dev->mem[0x52]);
				break;
			case 0x10:	/* SCODE_HK4 */
				kbc_transmit(dev, dev->mem[0x53]);
				break;
			case 0x11:	/* SCODE_HK5 */
				kbc_transmit(dev, dev->mem[0x54]);
				break;
			case 0x12:	/* SCODE_HK6 */
				kbc_transmit(dev, dev->mem[0x55]);
				break;
			case 0x13:	/* TASK_HK1 */
				kbc_transmit(dev, dev->mem[0x56]);
				break;
			case 0x14:	/* TASK_HK2 */
				kbc_transmit(dev, dev->mem[0x57]);
				break;
			case 0x15:	/* TASK_HK3 */
				kbc_transmit(dev, dev->mem[0x58]);
				break;
			case 0x16:	/* TASK_HK4 */
				kbc_transmit(dev, dev->mem[0x59]);
				break;
			case 0x17:	/* TASK_HK5 */
				kbc_transmit(dev, dev->mem[0x5a]);
				break;
			/* The next 4 bytes have uncertain correspondences. */
			case 0x18:	/* Batt_Poll_delay_Time */
				kbc_transmit(dev, dev->mem[0x5b]);
				break;
			case 0x19:	/* Batt_Alarm_Reg1 */
				kbc_transmit(dev, dev->mem[0x5c]);
				break;
			case 0x1a:	/* Batt_Alarm_Reg2 */
				kbc_transmit(dev, dev->mem[0x5d]);
				break;
			case 0x1b:	/* Batt_Alarm_Tsk_Reg */
				kbc_transmit(dev, dev->mem[0x5e]);
				break;
			case 0x1c:	/* Kbc_State1 */
				kbc_transmit(dev, dev->mem[0x9d]);
				break;
			case 0x1d:	/* Aux_Config */
				kbc_transmit(dev, dev->mem[0x75]);
				break;
			case 0x1e:	/* Kbc_State3 */
				kbc_transmit(dev, dev->mem[0x73]);
				break;
			default:
				kbc_transmit(dev, 0x00);
				break;
		}
		kbc_log(dev, "AMIKey-3 - read from config index %02X\n", dev->mem_index);
		return 0;

	case 0xbd:
		kbc_log(dev, "AMIKey-3 - write to config index %02X\n", dev->mem_index);
		dev->kbc_in = 1;
		return 0;

	case 0xc1:	/* write input port */
		kbc_log(dev, "AMIKey-3 - write input port\n");
		dev->kbc_in = 1;
		return 0;

	case 0xc8: case 0xc9:
		/*
		 * (un)block KBC lines P22/P23
		 * (allow command D1 to change bits 2/3 of the output port)
		 */
		kbc_log(dev, "AMI - %slock KBC lines P22 and P23\n", (val & 1) ? "B" : "Unb");
		dev->p2_locked = (val & 1);
		return 0;

	case 0xef:	/* ??? - sent by AMI486 */
		kbc_log(dev, "??? - sent by AMI486\n");
		return 0;
    }

    return write64_generic(dev, val);
}


static uint8_t
write64_ibm_mca(void *priv, uint8_t val)
{
    atkbc_t *dev = (atkbc_t *)priv;

    switch (val) {
	case 0xc1: /*Copy bits 0 to 3 of input port to status bits 4 to 7*/
		kbc_log(dev, "Copy bits 0 to 3 of input port to status bits 4 to 7\n");
		dev->status &= 0x0f;
		dev->status |= ((((dev->p1 & 0xfc) | 0x84) & 0x0f) << 4);
		return 0;

	case 0xc2: /*Copy bits 4 to 7 of input port to status bits 4 to 7*/
		kbc_log(dev, "Copy bits 4 to 7 of input port to status bits 4 to 7\n");
		dev->status &= 0x0f;
		dev->status |= (((dev->p1 & 0xfc) | 0x84) & 0xf0);
		return 0;

	case 0xaf:
		kbc_log(dev, "Bad KBC command AF\n");
		return 1;

	case 0xf0 ... 0xff:
		kbc_log(dev, "Pulse: %01X\n", (val & 0x03) | 0x0c);
		pulse_output(dev, (val & 0x03) | 0x0c);
		return 0;
    }

    return write64_generic(dev, val);
}


static uint8_t
write60_quadtel(void *priv, uint8_t val)
{
    atkbc_t *dev = (atkbc_t *)priv;

    switch(dev->kbc_cmd) {
	case 0xcf:	/*??? - sent by MegaPC BIOS*/
		kbc_log(dev, "??? - sent by MegaPC BIOS\n");
		return 0;
    }

    return 1;
}


static uint8_t
write64_olivetti(void *priv, uint8_t val)
{
    atkbc_t *dev = (atkbc_t *)priv;

    switch (val) {
	/* This appears to be a clone of "Read input port", in which case, the bis would be:
		7: M290 (AT KBC):
			Keyboard lock (1 = unlocked, 0 = locked);
		   M300 (PS/2 KBC):
			Bus expansion board present (1 = present, 0 = not present);
		6: Usually:
			Display (1 = MDA, 0 = CGA, but can have its polarity inverted);
		5: Manufacturing jumper (1 = not installed, 0 = installed (infinite loop));
		4: RAM on motherboard (1 = 256 kB, 0 = 512 kB - which machine actually uses this?);
		3: Fast Ram check (if inactive keyboard works erratically);
		2: Keyboard fuse present
		   This appears to be in-line with PS/2: 1 = no power, 0 = keyboard power normal;
		1: M290 (AT KBC):
			Unused;
		   M300 (PS/2 KBC):
			Mouse data in;
		0: M290 (AT KBC):
			Unused;
		   M300 (PS/2 KBC):
			Key data in.
	*/
	case 0x80:	/* Olivetti-specific command */
		/*
		* bit 7: bus expansion board present (M300) / keyboard unlocked (M290)
		* bits 4-6: ???
		* bit 3: fast ram check (if inactive keyboard works erratically)
		* bit 2: keyboard fuse present
		* bits 0-1: ???
		*/
		kbc_transmit(dev, 0x0c | (is386 ? 0x00 : 0x80));
		return 0;
    }

    return write64_generic(dev, val);
}


static uint8_t
write64_quadtel(void *priv, uint8_t val)
{
    atkbc_t *dev = (atkbc_t *)priv;

    switch (val) {
	case 0xaf:
		kbc_log(dev, "Bad KBC command AF\n");
		return 1;

	case 0xcf:	/*??? - sent by MegaPC BIOS*/
		kbc_log(dev, "??? - sent by MegaPC BIOS\n");
		dev->kbc_in = 1;
		return 0;
    }

    return write64_generic(dev, val);
}


static uint8_t
write60_toshiba(void *priv, uint8_t val)
{
    atkbc_t *dev = (atkbc_t *)priv;

    switch(dev->kbc_cmd) {
	case 0xb6:	/* T3100e - set color/mono switch */
		kbc_log(dev, "T3100e - Set color/mono switch\n");
		t3100e_mono_set(val);
		return 0;
    }

    return 1;
}


static uint8_t
write64_toshiba(void *priv, uint8_t val)
{
    atkbc_t *dev = (atkbc_t *)priv;

    switch (val) {
	case 0xaf:
		kbc_log(dev, "Bad KBC command AF\n");
		return 1;

	case 0xb0:	/* T3100e: Turbo on */
		kbc_log(dev, "T3100e: Turbo on\n");
		t3100e_turbo_set(1);
		return 0;

	case 0xb1:	/* T3100e: Turbo off */
		kbc_log(dev, "T3100e: Turbo off\n");
		t3100e_turbo_set(0);
		return 0;

	case 0xb2:	/* T3100e: Select external display */
		kbc_log(dev, "T3100e: Select external display\n");
		t3100e_display_set(0x00);
		return 0;

	case 0xb3:	/* T3100e: Select internal display */
		kcd_log("T3100e: Select internal display\n");
		t3100e_display_set(0x01);
		return 0;

	case 0xb4:	/* T3100e: Get configuration / status */
		kbc_log(dev, "T3100e: Get configuration / status\n");
		kbc_transmit(dev, t3100e_config_get());
		return 0;

	case 0xb5:	/* T3100e: Get colour / mono byte */
		kbc_log(dev, "T3100e: Get colour / mono byte\n");
		kbc_transmit(dev, t3100e_mono_get());
		return 0;

	case 0xb6:	/* T3100e: Set colour / mono byte */
		kbc_log(dev, "T3100e: Set colour / mono byte\n");
		dev->kbc_in = 1;
		return 0;

	case 0xb7:	/* T3100e: Emulate PS/2 keyboard */
	case 0xb8:	/* T3100e: Emulate AT keyboard */
		dev->flags &= ~KBC_FLAG_PS2;
		if (val == 0xb7) {
			kbc_log(dev, "T3100e: Emulate PS/2 keyboard\n");
			dev->flags |= KBC_FLAG_PS2;
		} else
			kbc_log(dev, "T3100e: Emulate AT keyboard\n");
#if (!defined(RELEASE_BUILD) && defined(ENABLE_KBD_AT_LOG))
		log_set_dev_name(dev->kbc_log, (dev->flags & KBC_FLAG_PS2) ? "AT KBC" : "PS/2 KBC");
#endif
		return 0;

	case 0xbb:	/* T3100e: Read 'Fn' key.
			   Return it for right Ctrl and right Alt; on the real
			   T3100e, these keystrokes could only be generated
			   using 'Fn'. */
		kbc_log(dev, "T3100e: Read 'Fn' key\n");
		if (keyboard_recv(0xb8) ||	/* Right Alt */
		    keyboard_recv(0x9d))	/* Right Ctrl */
			kbc_transmit(dev, 0x04);
		else
			kbc_transmit(dev, 0x00);
		return 0;

	case 0xbc:	/* T3100e: Reset Fn+Key notification */
		kbc_log(dev, "T3100e: Reset Fn+Key notification\n");
		t3100e_notify_set(0x00);
		return 0;

	case 0xc0:	/*Read input port*/
		kbc_log(dev, "Read input port\n");

		/* The T3100e returns all bits set except bit 6 which
		 * is set by t3100e_mono_set() */
		dev->p1 = (t3100e_mono_get() & 1) ? 0xff : 0xbf;
		kbc_transmit(dev, dev->p1);
		return 0;

    }

    return write64_generic(dev, val);
}


static void
kbc_write(uint16_t port, uint8_t val, void *priv)
{
    atkbc_t *dev = (atkbc_t *)priv;

    kbc_log(dev, "[%04X:%08X] write(%04X, %02X)\n", CS, cpu_state.pc, port, val);

    switch (port) {
	case 0x60:
		dev->status = (dev->status & ~STAT_CD) | STAT_IFULL;
		dev->ib = val;
		// kbd_status("Write %02X: %02X, Status = %02X\n", port, val, dev->status);

#if 0
		if ((dev->fast_a20_phase == 1)/* && ((val == 0xdd) || (val == 0xdf))*/) {
			dev->status &= ~STAT_IFULL;
			write_output(dev, val);
			dev->fast_a20_phase = 0;
		}
#endif
		break;
	case 0x64:
		dev->status |= (STAT_CD | STAT_IFULL);
		dev->ib = val;
		// kbd_status("Write %02X: %02X, Status = %02X\n", port, val, dev->status);

#if 0
		if (val == 0xd1) {
			dev->status &= ~STAT_IFULL;
			dev->fast_a20_phase = 1;
		} else if (val == 0xfe) {
			dev->status &= ~STAT_IFULL;
			pulse_output(dev, 0x0e);
		} else if ((val == 0xad) || (val == 0xae)) {
			dev->status &= ~STAT_IFULL;
			if (val & 0x01)
				dev->mem[0x20] |= 0x10;
			else
				dev->mem[0x20] &= ~0x10;
		} else if (val == 0xa1) {
			dev->status &= ~STAT_IFULL;
			kbc_send_to_ob(dev, 'H', 0, 0x00);
		}
#endif
		break;
    }
}


static uint8_t
kbc_read(uint16_t port, void *priv)
{
    atkbc_t *dev = (atkbc_t *)priv;
    uint8_t ret = 0xff;

    // if (dev->flags & KBC_FLAG_PS2)
	// cycles -= ISA_CYCLES(8);

    switch (port) {
	case 0x60:
                ret = dev->ob;
                dev->status &= ~STAT_OFULL;
                picintc(dev->last_irq);
                dev->last_irq = 0;
		break;

	case 0x64:
		ret = dev->status;
		break;

	default:
		kbc_log(dev, "Reading unknown port %02X\n", port);
		break;
    }

    kbc_log(dev, "[%04X:%08X] read(%04X) = %02X\n",CS, cpu_state.pc, port, ret);

    return(ret);
}


static void
kbc_reset(void *priv)
{
    atkbc_t *dev = (atkbc_t *)priv;
    int i;
    uint8_t kbc_ven = 0x0;
    kbc_ven = dev->flags & KBC_VEN_MASK;

    dev->status = STAT_UNLOCKED;
    dev->mem[0x20] = 0x01;
    dev->mem[0x20] |= CCB_TRANSLATE;
    write_output(dev, 0xcf);
    dev->last_irq = 0;
    dev->secr_phase = 0;
    dev->in = 0;
    dev->ami_mode = !!(dev->flags & KBC_FLAG_PS2);

    /* Set up the correct Video Type bits. */
    dev->p1 = video_is_mda() ? 0xf0 : 0xb0;
    if ((kbc_ven == KBC_VEN_XI8088) || (kbc_ven == KBC_VEN_ACER))
	dev->p1 ^= 0x40;
    if ((kbc_ven == KBC_VEN_AMI) || (dev->flags & KBC_FLAG_PS2))
	dev->inhibit = ((dev->p1 & 0x80) >> 3);
    else
	dev->inhibit = 0x10;
    kbc_log(dev, "Input port = %02x\n", dev->p1);

    keyboard_mode = 0x02 | (dev->mem[0x20] & CCB_TRANSLATE);

    /* Enable keyboard, disable mouse. */
    set_enable_kbd(dev, 1);
    keyboard_scan = 1;
    set_enable_mouse(dev, 0);
    mouse_scan = 0;

    dev->ob = 0xff;

    sc_or = 0;

    dev->mem[0x31] = 0xfe;
}


/* Reset the AT keyboard - this is needed for the PCI TRC and is done
   until a better solution is found. */
void
keyboard_at_reset(void)
{
    kbc_reset(SavedKbd);
}


void
kbc_dev_attach(kbc_dev_t *kbc_dev, int channel)
{
    if ((channel < 1) || (channel > 2))
	log_fatal(saved_kbc->log, "Attaching device to invalid channel %i\n", channel);
    else {
	kbc_log(saved_kbc, "Attaching device to channel %i\n", channel);
	saved_kbc->kbc_devs[channel - 1] = kbc_dev;
    }
}


static void
kbc_close(void *priv)
{
    atkbc_t *dev = (atkbc_t *)priv;

    kbc_reset(dev);

    /* Stop timers. */
    timer_disable(&dev->send_delay_timer);

#if (!defined(RELEASE_BUILD) && defined(ENABLE_KBC_AT_LOG))
    log_close(dev->log);
#endif

    free(dev);
}


static void *
kbc_init(const device_t *info)
{
    atkbc_t *dev;

    dev = (atkbc_t *)malloc(sizeof(atkbc_t));
    memset(dev, 0x00, sizeof(atkbc_t));

    dev->flags = info->local;

    video_reset(gfxcard);
    dev->kbc_poll_phase = KBC_RESET;

    io_sethandler(0x0060, 1, kbc_read, NULL, NULL, kbc_write, NULL, NULL, dev);
    io_sethandler(0x0064, 1, kbc_read, NULL, NULL, kbc_write, NULL, NULL, dev);

    timer_add(&dev->send_delay_timer, kbd_poll, dev, 1); 
    timer_add(&dev->pulse_cb, pulse_poll, dev, 0);

#if (!defined(RELEASE_BUILD) && defined(ENABLE_KBC_AT_LOG))
    dev->kbc_log = log_open((dev->flags & KBC_FLAG_PS2) ? "AT KBC" : "PS/2 KBC");
#endif

    dev->write60_ven = NULL;
    dev->write64_ven = NULL;

    switch(dev->flags & KBC_VEN_MASK) {
	case KBC_VEN_ACER:
	case KBC_VEN_GENERIC:
	case KBC_VEN_NCR:
	case KBC_VEN_IBM_PS1:
	case KBC_VEN_XI8088:
		dev->write64_ven = write64_generic;
		break;

	case KBC_VEN_OLIVETTI:
		/* The Olivetti controller is a special case - starts directly in the
		   main loop instead of the reset loop. */
		dev->kbc_poll_phase = KBC_MAIN_LOOP;
		dev->write64_ven = write64_olivetti;
		break;

	case KBC_VEN_AMI:
	case KBC_VEN_INTEL_AMI:
		dev->write60_ven = write60_ami;
		dev->write64_ven = write64_ami;
		break;

	case KBC_VEN_IBM_MCA:
		dev->write64_ven = write64_ibm_mca;
		break;

	case KBC_VEN_QUADTEL:
		dev->write60_ven = write60_quadtel;
		dev->write64_ven = write64_quadtel;
		break;

	case KBC_VEN_TOSHIBA:
		dev->write60_ven = write60_toshiba;
		dev->write64_ven = write64_toshiba;
		break;
    }

    kbc_reset(dev);

    /* Local variable, needed for device attaching. */
    saved_kbc = dev;

    /* Add the actual keyboard. */
    device_add(&keyboard_at_kbd_device);

    return(dev);
}


const device_t keyboard_at_device = {
    "PC/AT Keyboard",
    0,
    KBC_TYPE_ISA | KBC_VEN_GENERIC,
    kbc_init,
    kbc_close,
    kbc_reset,
    { NULL }, NULL, NULL, NULL
};

const device_t keyboard_at_ami_device = {
    "PC/AT Keyboard (AMI)",
    0,
    KBC_TYPE_ISA | KBC_VEN_AMI,
    kbc_init,
    kbc_close,
    kbc_reset,
    { NULL }, NULL, NULL, NULL
};

const device_t keyboard_at_toshiba_device = {
    "PC/AT Keyboard (Toshiba)",
    0,
    KBC_TYPE_ISA | KBC_VEN_TOSHIBA,
    kbc_init,
    kbc_close,
    kbc_reset,
    { NULL }, NULL, NULL, NULL
};

const device_t keyboard_at_olivetti_device = {
    "PC/AT Keyboard (Olivetti)",
    0,
    KBC_TYPE_ISA | KBC_VEN_OLIVETTI,
    kbc_init,
    kbc_close,
    kbc_reset,
    { NULL }, NULL, NULL, NULL
};

const device_t keyboard_at_ncr_device = {
    "PC/AT Keyboard (NCR)",
    0,
    KBC_TYPE_ISA | KBC_VEN_NCR,
    kbc_init,
    kbc_close,
    kbc_reset,
    { NULL }, NULL, NULL, NULL
};

const device_t keyboard_ps2_device = {
    "PS/2 Keyboard",
    0,
    KBC_TYPE_PS2_1 | KBC_VEN_GENERIC,
    kbc_init,
    kbc_close,
    kbc_reset,
    { NULL }, NULL, NULL, NULL
};

const device_t keyboard_ps2_ps1_device = {
    "PS/2 Keyboard (IBM PS/1)",
    0,
    KBC_TYPE_PS2_1 | KBC_VEN_IBM_PS1,
    kbc_init,
    kbc_close,
    kbc_reset,
    { NULL }, NULL, NULL, NULL
};

const device_t keyboard_ps2_ps1_pci_device = {
    "PS/2 Keyboard (IBM PS/1)",
    DEVICE_PCI,
    KBC_TYPE_PS2_1 | KBC_VEN_IBM_PS1,
    kbc_init,
    kbc_close,
    kbc_reset,
    { NULL }, NULL, NULL, NULL
};

const device_t keyboard_ps2_xi8088_device = {
    "PS/2 Keyboard (Xi8088)",
    0,
    KBC_TYPE_PS2_1 | KBC_VEN_XI8088,
    kbc_init,
    kbc_close,
    kbc_reset,
    { NULL }, NULL, NULL, NULL
};

const device_t keyboard_ps2_ami_device = {
    "PS/2 Keyboard (AMI)",
    0,
    KBC_TYPE_PS2_1 | KBC_VEN_AMI,
    kbc_init,
    kbc_close,
    kbc_reset,
    { NULL }, NULL, NULL, NULL
};

const device_t keyboard_ps2_olivetti_device = {
    "PS/2 Keyboard (Olivetti)",
    0,
    KBC_TYPE_PS2_1 | KBC_VEN_OLIVETTI,
    kbc_init,
    kbc_close,
    kbc_reset,
    { NULL }, NULL, NULL, NULL
};

const device_t keyboard_ps2_mca_device = {
    "PS/2 Keyboard",
    0,
    KBC_TYPE_PS2_1 | KBC_VEN_IBM_MCA,
    kbc_init,
    kbc_close,
    kbc_reset,
    { NULL }, NULL, NULL, NULL
};

const device_t keyboard_ps2_mca_2_device = {
    "PS/2 Keyboard",
    0,
    KBC_TYPE_PS2_2 | KBC_VEN_IBM_MCA,
    kbc_init,
    kbc_close,
    kbc_reset,
    { NULL }, NULL, NULL, NULL
};

const device_t keyboard_ps2_quadtel_device = {
    "PS/2 Keyboard (Quadtel/MegaPC)",
    0,
    KBC_TYPE_PS2_1 | KBC_VEN_QUADTEL,
    kbc_init,
    kbc_close,
    kbc_reset,
    { NULL }, NULL, NULL, NULL
};

const device_t keyboard_ps2_pci_device = {
    "PS/2 Keyboard",
    DEVICE_PCI,
    KBC_TYPE_PS2_1 | KBC_VEN_GENERIC,
    kbc_init,
    kbc_close,
    kbc_reset,
    { NULL }, NULL, NULL, NULL
};

const device_t keyboard_ps2_ami_pci_device = {
    "PS/2 Keyboard (AMI)",
    DEVICE_PCI,
    KBC_TYPE_PS2_1 | KBC_VEN_AMI,
    kbc_init,
    kbc_close,
    kbc_reset,
    { NULL }, NULL, NULL, NULL
};

const device_t keyboard_ps2_intel_ami_pci_device = {
    "PS/2 Keyboard (AMI)",
    DEVICE_PCI,
    KBC_TYPE_PS2_1 | KBC_VEN_INTEL_AMI,
    kbc_init,
    kbc_close,
    kbc_reset,
    { NULL }, NULL, NULL, NULL
};

const device_t keyboard_ps2_acer_pci_device = {
    "PS/2 Keyboard (Acer 90M002A)",
    DEVICE_PCI,
    KBC_TYPE_PS2_1 | KBC_VEN_ACER,
    kbc_init,
    kbc_close,
    kbc_reset,
    { NULL }, NULL, NULL, NULL
};


void
keyboard_at_set_mouse(void (*func)(uint8_t val, void *priv), void *priv)
{
}


void
keyboard_at_adddata_mouse(uint8_t val)
{
    return;
}


void
keyboard_at_adddata_mouse_direct(uint8_t val)
{
    return;
}


void
keyboard_at_adddata_mouse_cmd(uint8_t val)
{
    return;
}


void
keyboard_at_mouse_reset(void)
{
    return;
}


uint8_t
keyboard_at_mouse_pos(void)
{
    return ((mouse_queue_end - mouse_queue_start) & 0xf);
}


int
keyboard_at_fixed_channel(void)
{
    return 0x000;
}


void
keyboard_at_set_mouse_scan(uint8_t val)
{
    atkbc_t *dev = SavedKbd;
    uint8_t temp_mouse_scan = val ? 1 : 0;

    if (temp_mouse_scan == !(dev->mem[0x20] & 0x20))
	return;

    set_enable_mouse(dev, val ? 1 : 0);

    kbc_log(dev, "Mouse scan %sabled via PCI\n", mouse_scan ? "en" : "dis");
}


uint8_t
keyboard_at_get_mouse_scan(void)
{
    atkbc_t *dev = SavedKbd;

    return((dev->mem[0x20] & 0x20) ? 0x00 : 0x10);
}


void
keyboard_at_set_a20_key(int state)
{
    atkbc_t *dev = SavedKbd;

    write_output(dev, (dev->p2 & 0xfd) | ((!!state) << 1));
}


void
keyboard_at_set_mode(int ps2)
{
    atkbc_t *dev = SavedKbd;

    if (ps2)
	dev->flags |= KBC_FLAG_PS2;
    else
	dev->flags &= ~KBC_FLAG_PS2;
}
