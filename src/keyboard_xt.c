/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the XT-style keyboard.
 *
 * Version:	@(#)keyboard_xt.c	1.0.2	2017/11/04
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "machine/machine.h"
#include "io.h"
#include "pic.h"
#include "pit.h"
#include "ppi.h"
#include "mem.h"
#include "rom.h"
#include "timer.h"
#include "device.h"
#include "tandy_eeprom.h"
#include "sound/sound.h"
#include "sound/snd_speaker.h"
#include "video/video.h"
#include "keyboard.h"


#define STAT_PARITY     0x80
#define STAT_RTIMEOUT   0x40
#define STAT_TTIMEOUT   0x20
#define STAT_LOCK       0x10
#define STAT_CD         0x08
#define STAT_SYSFLAG    0x04
#define STAT_IFULL      0x02
#define STAT_OFULL      0x01


typedef struct {
    int blocked;

    uint8_t pa;
    uint8_t pb;

    int tandy;
} xtkbd_t;


/*XT keyboard has no escape scancodes, and no scancodes beyond 53*/
static scancode scancode_xt[272] = {
    { {-1},       {-1}       }, { {0x01, -1}, {0x81, -1} },
    { {0x02, -1}, {0x82, -1} }, { {0x03, -1}, {0x83, -1} },
    { {0x04, -1}, {0x84, -1} }, { {0x05, -1}, {0x85, -1} },
    { {0x06, -1}, {0x86, -1} }, { {0x07, -1}, {0x87, -1} },
    { {0x08, -1}, {0x88, -1} }, { {0x09, -1}, {0x89, -1} },
    { {0x0a, -1}, {0x8a, -1} }, { {0x0b, -1}, {0x8b, -1} },
    { {0x0c, -1}, {0x8c, -1} }, { {0x0d, -1}, {0x8d, -1} },
    { {0x0e, -1}, {0x8e, -1} }, { {0x0f, -1}, {0x8f, -1} },
    { {0x10, -1}, {0x90, -1} }, { {0x11, -1}, {0x91, -1} },
    { {0x12, -1}, {0x92, -1} }, { {0x13, -1}, {0x93, -1} },
    { {0x14, -1}, {0x94, -1} }, { {0x15, -1}, {0x95, -1} },
    { {0x16, -1}, {0x96, -1} }, { {0x17, -1}, {0x97, -1} },
    { {0x18, -1}, {0x98, -1} }, { {0x19, -1}, {0x99, -1} },
    { {0x1a, -1}, {0x9a, -1} }, { {0x1b, -1}, {0x9b, -1} },
    { {0x1c, -1}, {0x9c, -1} }, { {0x1d, -1}, {0x9d, -1} },
    { {0x1e, -1}, {0x9e, -1} }, { {0x1f, -1}, {0x9f, -1} },
    { {0x20, -1}, {0xa0, -1} }, { {0x21, -1}, {0xa1, -1} },
    { {0x22, -1}, {0xa2, -1} }, { {0x23, -1}, {0xa3, -1} },
    { {0x24, -1}, {0xa4, -1} }, { {0x25, -1}, {0xa5, -1} },
    { {0x26, -1}, {0xa6, -1} }, { {0x27, -1}, {0xa7, -1} },
    { {0x28, -1}, {0xa8, -1} }, { {0x29, -1}, {0xa9, -1} },
    { {0x2a, -1}, {0xaa, -1} }, { {0x2b, -1}, {0xab, -1} },
    { {0x2c, -1}, {0xac, -1} }, { {0x2d, -1}, {0xad, -1} },
    { {0x2e, -1}, {0xae, -1} }, { {0x2f, -1}, {0xaf, -1} },
    { {0x30, -1}, {0xb0, -1} }, { {0x31, -1}, {0xb1, -1} },
    { {0x32, -1}, {0xb2, -1} }, { {0x33, -1}, {0xb3, -1} },
    { {0x34, -1}, {0xb4, -1} }, { {0x35, -1}, {0xb5, -1} },
    { {0x36, -1}, {0xb6, -1} }, { {0x37, -1}, {0xb7, -1} },
    { {0x38, -1}, {0xb8, -1} }, { {0x39, -1}, {0xb9, -1} },
    { {0x3a, -1}, {0xba, -1} }, { {0x3b, -1}, {0xbb, -1} },
    { {0x3c, -1}, {0xbc, -1} }, { {0x3d, -1}, {0xbd, -1} },
    { {0x3e, -1}, {0xbe, -1} }, { {0x3f, -1}, {0xbf, -1} },
    { {0x40, -1}, {0xc0, -1} }, { {0x41, -1}, {0xc1, -1} },
    { {0x42, -1}, {0xc2, -1} }, { {0x43, -1}, {0xc3, -1} },
    { {0x44, -1}, {0xc4, -1} }, { {0x45, -1}, {0xc5, -1} },
    { {0x46, -1}, {0xc6, -1} }, { {0x47, -1}, {0xc7, -1} },
    { {0x48, -1}, {0xc8, -1} }, { {0x49, -1}, {0xc9, -1} },
    { {0x4a, -1}, {0xca, -1} }, { {0x4b, -1}, {0xcb, -1} },
    { {0x4c, -1}, {0xcc, -1} }, { {0x4d, -1}, {0xcd, -1} },
    { {0x4e, -1}, {0xce, -1} }, { {0x4f, -1}, {0xcf, -1} },
    { {0x50, -1}, {0xd0, -1} }, { {0x51, -1}, {0xd1, -1} },
    { {0x52, -1}, {0xd2, -1} }, { {0x53, -1}, {0xd3, -1} },
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*54*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*58*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*5c*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*60*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*64*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*68*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*6c*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*70*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*74*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*78*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*7c*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*80*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*84*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*88*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*8c*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*90*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*94*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*98*/
    { {0x1c, -1}, {0x9c, -1} }, { {0x1d, -1}, {0x9d, -1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*9c*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*a0*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*a4*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {0xaa, -1}, {0x2a, -1} }, { {-1},             {-1} },	/*a8*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*ac*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*b0*/
    { {-1},             {-1} }, { {0x35, -1}, {0xb5, -1} },
    { {0xb6, -1}, {0x36, -1} }, { {0x37, -1}, {0xb7, -1} },	/*b4*/
    { {0x38, -1}, {0xb8, -1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*b8*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*bc*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*c0*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {0x46, -1}, {0xc6, -1} }, { {0x47, -1}, {0xc7, -1} },	/*c4*/
    { {0x48, -1}, {0xc8, -1} }, { {0x49, -1}, {0xc9, -1} },
    { {-1},             {-1} }, { {0x4b, -1}, {0xcb, -1} },	/*c8*/
    { {-1},             {-1} }, { {0x4d, -1}, {0xcd, -1} },
    { {-1},             {-1} }, { {0x4f, -1}, {0xcf, -1} },	/*cc*/
    { {0x50, -1}, {0xd0, -1} }, { {0x51, -1}, {0xd1, -1} },
    { {0x52, -1}, {0xd2, -1} }, { {0x53, -1}, {0xd3, -1} },	/*d0*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*d4*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*d8*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*dc*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*e0*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*e4*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*e8*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*ec*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*f0*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*f4*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*f8*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*fc*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*100*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*104*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*108*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*10c*/
};


static uint8_t	key_queue[16];
static int	key_queue_start,
		key_queue_end;


static void
kbd_poll(void *priv)
{
    xtkbd_t *kbd = (xtkbd_t *)priv;

    keyboard_delay += (1000LL * TIMER_USEC);

    if (key_queue_start != key_queue_end && !kbd->blocked) {
	kbd->pa = key_queue[key_queue_start];
	picint(2);
#if ENABLE_KEYBOARD_LOG
	pclog("Reading %02X from the key queue at %i\n",
				kbd->pa, key_queue_start);
#endif
	key_queue_start = (key_queue_start + 1) & 0xf;
	kbd->blocked = 1;
    }
}


static void
kbd_adddata(uint8_t val)
{
    key_queue[key_queue_end] = val;
#if ENABLE_KEYBOARD_LOG
    pclog("XTkbd: %02X added to key queue at %i\n",
				val, key_queue_end);
#endif
    key_queue_end = (key_queue_end + 1) & 0xf;
}


static void
kbd_write(uint16_t port, uint8_t val, void *priv)
{
    xtkbd_t *kbd = (xtkbd_t *)priv;

    if (port != 0x61) return;

    if (!(kbd->pb & 0x40) && (val & 0x40)) { /*Reset keyboard*/
#if ENABLE_KEYBOARD_LOG
	pclog("XTkbd: reset keyboard\n");
#endif
	key_queue_end = key_queue_start;
	kbd_adddata(0xaa);
    }

    if ((kbd->pb & 0x80)==0 && (val & 0x80)!=0) {
	kbd->pa = 0;
	kbd->blocked = 0;
	picintc(2);
    }
    kbd->pb = val;
    ppi.pb = val;

    timer_process();
    timer_update_outstanding();

    speaker_update();
    speaker_gated = val & 1;
    speaker_enable = val & 2;
    if (speaker_enable) 
	was_speaker_enable = 1;
    pit_set_gate(&pit, 2, val & 1);
}


static uint8_t
kbd_read(uint16_t port, void *priv)
{
    xtkbd_t *kbd = (xtkbd_t *)priv;
    uint8_t ret = 0xff;

    switch (port) {
	case 0x60:
		if ((romset == ROM_IBMPC) && (kbd->pb & 0x80)) {
			if (VGA || gfxcard == GFX_EGA)
				ret = 0x4D;
			  else if (MDA)
				ret = 0x7D;
			  else
				ret = 0x6D;
		} else
			ret = kbd->pa;
		break;

	case 0x61:
		ret = kbd->pb;
		break;

	case 0x62:
		if (romset == ROM_IBMPC) {
			if (kbd->pb & 0x04)
				ret = ((mem_size-64) / 32) & 0xf;
			else
				ret = ((mem_size-64) / 32) >> 4;
		} else {
			if (kbd->pb & 0x08) {
				if (VGA || gfxcard == GFX_EGA)
					ret = 4;
				  else if (MDA)
					ret = 7;
				  else
					ret = 6;
			} else
				ret = 0x0D;
		}
		ret |= (ppispeakon ? 0x20 : 0);

		if (kbd->tandy)
			ret |= (tandy_eeprom_read() ? 0x10 : 0);
		break;

	default:
		pclog("\nXTkbd: bad read %04X\n", port);
		ret = 0xff;
    }

    return(ret);
}


static void
kbd_reset(void *priv)
{
    xtkbd_t *kbd = (xtkbd_t *)priv;

    kbd->blocked = 0;
    kbd->pa = 0x00;
    kbd->pb = 0x00;

    key_queue_start = 0,
    key_queue_end = 0;
}


static void *
kbd_init(device_t *info)
{
    xtkbd_t *kbd;

    kbd = (xtkbd_t *)malloc(sizeof(xtkbd_t));
    memset(kbd, 0x00, sizeof(xtkbd_t));

    keyboard_set_table(scancode_xt);

    if (info->local == 1) {
	kbd->tandy = 1;
    }

    keyboard_scan = 1;

    io_sethandler(0x0060, 4,
		  kbd_read, NULL, NULL, kbd_write, NULL, NULL, kbd);
    keyboard_send = kbd_adddata;
    timer_add(kbd_poll, &keyboard_delay, TIMER_ALWAYS_ENABLED, kbd);

    return(kbd);
}


static void
kbd_close(void *priv)
{
    xtkbd_t *kbd = (xtkbd_t *)priv;

    /* Stop the timer. */
    keyboard_delay = 0;

    /* Disable scanning. */
    keyboard_scan = 0;

    keyboard_send = NULL;

    io_removehandler(0x0060, 4,
		     kbd_read, NULL, NULL, kbd_write, NULL, NULL, kbd);

    free(kbd);
}


device_t keyboard_xt_device = {
    "PC/XT Keyboard",
    0,
    0,
    kbd_init,
    kbd_close,
    kbd_reset,
    NULL, NULL, NULL, NULL
};

device_t keyboard_tandy_device = {
    "Tandy 1000 Keyboard",
    0,
    1,
    kbd_init,
    kbd_close,
    kbd_reset,
    NULL, NULL, NULL, NULL
};
