/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		General keyboard driver interface.
 *
 * Version:	@(#)keyboard.c	1.0.9	2017/11/03
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "machine/machine.h"
#include "keyboard.h"


int64_t	keyboard_delay;
int	keyboard_scan;
void	(*keyboard_send)(uint8_t val);


static int	recv_key[272];		/* keyboard input buffer */
static int	oldkey[272];
static int	keydelay[272];
static scancode	*scan_table;		/* scancode table for keyboard */


/*
 * This array acts an intermediary so scan codes are processed in
 * the correct order (ALT-CTRL-SHIFT-RSHIFT first, then all others).
 */
static int scorder[272] =  {
     0x38,  0xB8,  0x1D,  0x9D,  0xFF,  0x2A,  0x36, 0x103,
     0x00,  0x01,  0x02,  0x03,  0x04,  0x05,  0x06,  0x07,
     0x08,  0x09,  0x0A,  0x0B,  0x0C,  0x0D,  0x0E,  0x0F,
     0x10,  0x11,  0x12,  0x13,  0x14,  0x15,  0x16,  0x17,
     0x18,  0x19,  0x1A,  0x1B,  0x1C,  0x1E,  0x1F,  0x20,
     0x21,  0x22,  0x23,  0x24,  0x25,  0x26,  0x27,  0x28,
     0x29,  0x2B,  0x2C,  0x2D,  0x2E,  0x2F,  0x30,  0x31,
     0x32,  0x33,  0x34,  0x35,  0x37,  0x39,  0x3A,  0x3B,
     0x3C,  0x3D,  0x3E,  0x3F,  0x40,  0x41,  0x42,  0x43,
     0x44,  0x45,  0x46,  0x47,  0x48,  0x49,  0x4A,  0x4B,
     0x4C,  0x4D,  0x4E,  0x4F,  0x50,  0x51,  0x52,  0x53,
     0x54,  0x55,  0x56,  0x57,  0x58,  0x59,  0x5A,  0x5B,
     0x5C,  0x5D,  0x5E,  0x5F,  0x60,  0x61,  0x62,  0x63,
     0x64,  0x65,  0x66,  0x67,  0x68,  0x69,  0x6A,  0x6B,
     0x6C,  0x6D,  0x6E,  0x6F,  0x70,  0x71,  0x72,  0x73,
     0x74,  0x75,  0x76,  0x77,  0x78,  0x79,  0x7A,  0x7B,
     0x7C,  0x7D,  0x7E,  0x7F,  0x80,  0x81,  0x82,  0x83,
     0x84,  0x85,  0x86,  0x87,  0x88,  0x89,  0x8A,  0x8B,
     0x8C,  0x8D,  0x8E,  0x8F,  0x90,  0x91,  0x92,  0x93,
     0x94,  0x95,  0x96,  0x97,  0x98,  0x99,  0x9A,  0x9B,
     0x9C,  0x9E,  0x9F,  0xA0,  0xA1,  0xA2,  0xA3,  0xA4,
     0xA5,  0xA6,  0xA7,  0xA8,  0xA9,  0xAA,  0xAB,  0xAC,
     0xAD,  0xAE,  0xAF,  0xB0,  0xB1,  0xB2,  0xB3,  0xB4,
     0xB5,  0xB6,  0xB7,  0xB9,  0xBA,  0xBB,  0xBC,  0xBD,
     0xBE,  0xBF,  0xC0,  0xC1,  0xC2,  0xC3,  0xC4,  0xC5,
     0xC6,  0xC7,  0xC8,  0xC9,  0xCA,  0xCB,  0xCC,  0xCD,
     0xCE,  0xCF,  0xD0,  0xD1,  0xD2,  0xD3,  0xD4,  0xD5,
     0xD6,  0xD7,  0xD8,  0xD9,  0xDA,  0xDB,  0xDC,  0xDD,
     0xDE,  0xDF,  0xE0,  0xE1,  0xE2,  0xE3,  0xE4,  0xE5,
     0xE6,  0xE7,  0xE8,  0xE9,  0xEA,  0xEB,  0xEC,  0xED,
     0xEE,  0xEF,  0xF0,  0xF1,  0xF2,  0xF3,  0xF4,  0xF5,
     0xF6,  0xF7,  0xF8,  0xF9,  0xFA,  0xFB,  0xFC,  0xFD,
     0xFE, 0x100, 0x101, 0x102, 0x104, 0x105, 0x106, 0x107,
    0x108, 0x109, 0x10A, 0x10B, 0x10C, 0x10D, 0x10E, 0x10F
};


void
keyboard_init(void)
{
    memset(recv_key, 0x00, sizeof(recv_key));

    keyboard_scan = 1;
    keyboard_delay = 0;
    scan_table = NULL;

    memset(keyboard_set3_flags, 0x00, sizeof(keyboard_set3_flags));
    keyboard_set3_all_repeat = 0;
    keyboard_set3_all_break = 0;
}


void
keyboard_set_table(scancode *ptr)
{
    scan_table = ptr;
}


void
keyboard_process(void)
{
    scancode *codes = scan_table;
    int c, d;

    if (! keyboard_scan) return;

    for (c = 0; c < 272; c++) {
	if (recv_key[scorder[c]])
		keydelay[scorder[c]]++;
	  else
		keydelay[scorder[c]] = 0;
    }

    for (c = 0; c < 272; c++) {
	if (recv_key[scorder[c]] != oldkey[scorder[c]]) {
		oldkey[scorder[c]] = recv_key[scorder[c]];
		if (recv_key[scorder[c]] &&
		    codes[scorder[c]].mk[0]  == -1) continue;

		if (!recv_key[scorder[c]] &&
		    codes[scorder[c]].brk[0] == -1) continue;

		if (AT && ((keyboard_mode & 3) == 3)) {
			if (!keyboard_set3_all_break &&
			    !recv_key[scorder[c]] &&
			    !(keyboard_set3_flags[codes[scorder[c]].mk[0]] & 2)) continue;
		}

		d = 0;
		if (recv_key[scorder[c]]) {
			while (codes[scorder[c]].mk[d] != -1)
				keyboard_send(codes[scorder[c]].mk[d++]);
		} else {
			while (codes[scorder[c]].brk[d] != -1)
				keyboard_send(codes[scorder[c]].brk[d++]);
		}
	}
    }

    for (c = 0; c < 272; c++) {
	if (AT && ((keyboard_mode & 3) == 3)) {
		if (codes[scorder[c]].mk[0] == -1) continue;

		if (!keyboard_set3_all_repeat &&
		    !recv_key[scorder[c]] &&
		    !(keyboard_set3_flags[codes[scorder[c]].mk[0]] & 1)) continue;
	}

	if (keydelay[scorder[c]] >= 30) {
		keydelay[scorder[c]] -= 10;
		if (codes[scorder[c]].mk[0] == -1) continue;

		d = 0;
		while (codes[scorder[c]].mk[d] != -1)
			keyboard_send(codes[scorder[c]].mk[d++]);
	}
    }
}


/* Handle a keystroke event from the UI layer. */
void
keyboard_input(int down, uint16_t scan)
{
    int key;

#if 0
    pclog("KBD: kbinput %d %04x\n", down, scan);
#endif

    if ((scan >> 8) == 0xf0) {
	scan &= 0x00ff;
	scan |= 0x0100;		/* ext key code in disambiguated format */
    } else if ((scan >> 8) == 0xe0) {
	scan &= 0x00ff;
	scan |= 0x0080;		/* normal extended key code */
    }

    /* First key. */
    key = (scan >> 8) & 0xff;
    if (key > 0)
	recv_key[key] = down;

    /* Second key. */
    key = scan & 0xff;
    if (key > 0)
	recv_key[key] = down;
}


/* Do we have Control-Alt-PgDn in the keyboard buffer? */
int
keyboard_isfsexit(void)
{
    return( (recv_key[0x1D] || recv_key[0x9D]) &&
	    (recv_key[0x38] || recv_key[0xB8]) &&
	    (recv_key[0x51] || recv_key[0xD1]) );
}


/* Do we have F8-F12 in the keyboard buffer? */
int
keyboard_ismsexit(void)
{
#ifdef _WIN32
    /* Windows: F8+F12 */
    return( recv_key[0x42] && recv_key[0x58] );
#else
    /* WxWidgets cannot do two regular keys.. CTRL+END */
    return( (recv_key[0x1D] || recv_key[0x9D]) && recv_key[0xCF] );
#endif
}
