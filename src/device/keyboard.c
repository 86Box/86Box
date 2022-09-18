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
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2015-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/machine.h>
#include <86box/keyboard.h>

#include "cpu.h"

int keyboard_scan;
void (*keyboard_send)(uint16_t val);

static int recv_key[512]; /* keyboard input buffer */
static int oldkey[512];
#if 0
static int	keydelay[512];
#endif
static scancode *scan_table; /* scancode table for keyboard */

static uint8_t caps_lock   = 0;
static uint8_t num_lock    = 0;
static uint8_t scroll_lock = 0;
static uint8_t shift       = 0;

void
keyboard_init(void)
{
    memset(recv_key, 0x00, sizeof(recv_key));

    keyboard_scan = 1;
    scan_table    = NULL;

    memset(keyboard_set3_flags, 0x00, sizeof(keyboard_set3_flags));
    keyboard_set3_all_repeat = 0;
    keyboard_set3_all_break  = 0;
}

void
keyboard_set_table(const scancode *ptr)
{
    scan_table = (scancode *) ptr;
}

static uint8_t
fake_shift_needed(uint16_t scan)
{
    switch (scan) {
        case 0x147:
        case 0x148:
        case 0x149:
        case 0x14a:
        case 0x14b:
        case 0x14d:
        case 0x14f:
        case 0x150:
        case 0x151:
        case 0x152:
        case 0x153:
            return 1;
        default:
            return 0;
    }
}

void
key_process(uint16_t scan, int down)
{
    scancode *codes = scan_table;
    int       c;

    if (!keyboard_scan || (keyboard_send == NULL))
        return;

    oldkey[scan] = down;
    if (down && codes[scan].mk[0] == 0)
        return;

    if (!down && codes[scan].brk[0] == 0)
        return;

    /* TODO: The keyboard controller needs to report the AT flag to us here. */
    if (is286 && ((keyboard_mode & 3) == 3)) {
        if (!keyboard_set3_all_break && !down && !(keyboard_set3_flags[codes[scan].mk[0]] & 2))
            return;
    }

    c = 0;
    if (down) {
        /* Send the special code indicating an opening fake shift might be needed. */
        if (fake_shift_needed(scan))
            keyboard_send(0x100);
        while (codes[scan].mk[c] != 0)
            keyboard_send(codes[scan].mk[c++]);
    } else {
        while (codes[scan].brk[c] != 0)
            keyboard_send(codes[scan].brk[c++]);
        /* Send the special code indicating a closing fake shift might be needed. */
        if (fake_shift_needed(scan))
            keyboard_send(0x101);
    }
}

/* Handle a keystroke event from the UI layer. */
void
keyboard_input(int down, uint16_t scan)
{
    /* Translate E0 xx scan codes to 01xx because we use 512-byte arrays for states
       and scan code sets. */
    if ((scan >> 8) == 0xe0) {
        scan &= 0x00ff;
        scan |= 0x0100; /* extended key code */
    } else if ((scan >> 8) != 0x01)
        scan &= 0x00ff; /* we can receive a scan code whose upper byte is 0x01,
                           this means we're the Win32 version running on windows
                           that already sends us preprocessed scan codes, which
                           means we then use the scan code as is, and need to
                           make sure we do not accidentally strip that upper byte */

    if (recv_key[scan & 0x1ff] ^ down) {
        if (down) {
            switch (scan & 0x1ff) {
                case 0x01c: /* Left Ctrl */
                    shift |= 0x01;
                    break;
                case 0x11c: /* Right Ctrl */
                    shift |= 0x10;
                    break;
                case 0x02a: /* Left Shift */
                    shift |= 0x02;
                    break;
                case 0x036: /* Right Shift */
                    shift |= 0x20;
                    break;
                case 0x038: /* Left Alt */
                    shift |= 0x04;
                    break;
                case 0x138: /* Right Alt */
                    shift |= 0x40;
                    break;
            }
        } else {
            switch (scan & 0x1ff) {
                case 0x01c: /* Left Ctrl */
                    shift &= ~0x01;
                    break;
                case 0x11c: /* Right Ctrl */
                    shift &= ~0x10;
                    break;
                case 0x02a: /* Left Shift */
                    shift &= ~0x02;
                    break;
                case 0x036: /* Right Shift */
                    shift &= ~0x20;
                    break;
                case 0x038: /* Left Alt */
                    shift &= ~0x04;
                    break;
                case 0x138: /* Right Alt */
                    shift &= ~0x40;
                    break;
                case 0x03a: /* Caps Lock */
                    caps_lock ^= 1;
                    break;
                case 0x045:
                    num_lock ^= 1;
                    break;
                case 0x046:
                    scroll_lock ^= 1;
                    break;
            }
        }
    }

    /* NOTE: Shouldn't this be some sort of bit shift? An array of 8 unsigned 64-bit integers
             should be enough. */
    /* recv_key[scan >> 6] |= ((uint64_t) down << ((uint64_t) scan & 0x3fLL)); */

    /* pclog("Received scan code: %03X (%s)\n", scan & 0x1ff, down ? "down" : "up"); */
    recv_key[scan & 0x1ff] = down;

    key_process(scan & 0x1ff, down);
}

static uint8_t
keyboard_do_break(uint16_t scan)
{
    scancode *codes = scan_table;

    /* TODO: The keyboard controller needs to report the AT flag to us here. */
    if (is286 && ((keyboard_mode & 3) == 3)) {
        if (!keyboard_set3_all_break && !recv_key[scan] && !(keyboard_set3_flags[codes[scan].mk[0]] & 2))
            return 0;
        else
            return 1;
    } else
        return 1;
}

/* Also called by the emulated keyboard controller to update the states of
   Caps Lock, Num Lock, and Scroll Lock when receving the "Set keyboard LEDs"
   command. */
void
keyboard_update_states(uint8_t cl, uint8_t nl, uint8_t sl)
{
    caps_lock   = cl;
    num_lock    = nl;
    scroll_lock = sl;
}

uint8_t
keyboard_get_shift(void)
{
    return shift;
}

void
keyboard_get_states(uint8_t *cl, uint8_t *nl, uint8_t *sl)
{
    if (cl)
        *cl = caps_lock;
    if (nl)
        *nl = num_lock;
    if (sl)
        *sl = scroll_lock;
}

/* Called by the UI to update the states of Caps Lock, Num Lock, and Scroll Lock. */
void
keyboard_set_states(uint8_t cl, uint8_t nl, uint8_t sl)
{
    scancode *codes = scan_table;

    int i;

    if (caps_lock != cl) {
        i = 0;
        while (codes[0x03a].mk[i] != 0)
            keyboard_send(codes[0x03a].mk[i++]);
        if (keyboard_do_break(0x03a)) {
            i = 0;
            while (codes[0x03a].brk[i] != 0)
                keyboard_send(codes[0x03a].brk[i++]);
        }
    }

    if (num_lock != nl) {
        i = 0;
        while (codes[0x045].mk[i] != 0)
            keyboard_send(codes[0x045].mk[i++]);
        if (keyboard_do_break(0x045)) {
            i = 0;
            while (codes[0x045].brk[i] != 0)
                keyboard_send(codes[0x045].brk[i++]);
        }
    }

    if (scroll_lock != sl) {
        i = 0;
        while (codes[0x046].mk[i] != 0)
            keyboard_send(codes[0x046].mk[i++]);
        if (keyboard_do_break(0x046)) {
            i = 0;
            while (codes[0x046].brk[i] != 0)
                keyboard_send(codes[0x046].brk[i++]);
        }
    }

    keyboard_update_states(cl, nl, sl);
}

int
keyboard_recv(uint16_t key)
{
    return recv_key[key];
}

/* Do we have Control-Alt-PgDn in the keyboard buffer? */
int
keyboard_isfsexit(void)
{
    return ((recv_key[0x01D] || recv_key[0x11D]) && (recv_key[0x038] || recv_key[0x138]) && (recv_key[0x051] || recv_key[0x151]));
}

/* Do we have F8-F12 in the keyboard buffer? */
int
keyboard_ismsexit(void)
{
#ifdef _WIN32
    /* Windows: F8+F12 */
    return (recv_key[0x042] && recv_key[0x058]);
#else
    /* WxWidgets cannot do two regular keys.. CTRL+END */
    return ((recv_key[0x01D] || recv_key[0x11D]) && (recv_key[0x04F] || recv_key[0x14F]));
#endif
}
