/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the Amstrad series PC's.
 *
 * Version:	@(#)m_amstrad.c	1.0.2	2017/11/03
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
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../io.h"
#include "../nmi.h"
#include "../pic.h"
#include "../pit.h"
#include "../ppi.h"
#include "../mem.h"
#include "../rom.h"
#include "../timer.h"
#include "../device.h"
#include "../nvr.h"
#include "../keyboard.h"
#include "../mouse.h"
#include "../game/gameport.h"
#include "../lpt.h"
#include "../sound/sound.h"
#include "../sound/snd_speaker.h"
#include "../floppy/floppy.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "machine.h"


#define STAT_PARITY     0x80
#define STAT_RTIMEOUT   0x40
#define STAT_TTIMEOUT   0x20
#define STAT_LOCK       0x10
#define STAT_CD         0x08
#define STAT_SYSFLAG    0x04
#define STAT_IFULL      0x02
#define STAT_OFULL      0x01


typedef struct {
    /* Machine stuff. */
    uint8_t	dead;
    uint8_t	systemstat_1,
		systemstat_2;

    /* Keyboard stuff. */
    int8_t	wantirq;
    uint8_t	key_waiting;
    uint8_t	pa;
    uint8_t	pb;

    /* Mouse stuff. */
    uint8_t	mousex,
		mousey;
    int		oldb;
} amstrad_t;


static uint8_t	key_queue[16];
static int	key_queue_start = 0,
		key_queue_end = 0;


static void
ms_write(uint16_t addr, uint8_t val, void *priv)
{
    amstrad_t *ams = (amstrad_t *)priv;

    if (addr == 0x78)
	ams->mousex = 0;
      else
	ams->mousey = 0;
}


static uint8_t
ms_read(uint16_t addr, void *priv)
{
    amstrad_t *ams = (amstrad_t *)priv;

    if (addr == 0x78)
	return(ams->mousex);

    return(ams->mousey);
}


static uint8_t
ms_poll(int x, int y, int z, int b, void *priv)
{
    amstrad_t *ams = (amstrad_t *)priv;

    ams->mousex += x;
    ams->mousey -= y;

    if ((b & 1) && !(ams->oldb & 1))
	keyboard_send(0x7e);
    if ((b & 2) && !(ams->oldb & 2))
	keyboard_send(0x7d);
    if (!(b & 1) && (ams->oldb & 1))
	keyboard_send(0xfe);
    if (!(b & 2) && (ams->oldb & 2))
	keyboard_send(0xfd);

    ams->oldb = b;

    return(0);
}


static void
kbd_adddata(uint8_t val)
{
    key_queue[key_queue_end] = val;
#if ENABLE_KEYBOARD_LOG
    pclog("keyboard_amstrad : %02X added to key queue at %i\n",
					val, key_queue_end);
#endif
    key_queue_end = (key_queue_end + 1) & 0xf;
}


static void
kbd_write(uint16_t port, uint8_t val, void *priv)
{
    amstrad_t *ams = (amstrad_t *)priv;

#if ENABLE_KEYBOARD_LOG
    pclog("keyboard_amstrad : write %04X %02X %02X\n", port, val, ams->pb);
#endif

    switch (port) {
	case 0x61:
#if ENABLE_KEYBOARD_LOG
		pclog("keyboard_amstrad : pb write %02X %02X  %i %02X %i\n",
		    val, ams->pb, !(ams->pb&0x40), ams->pb&0x40, (val&0x40));
#endif
		if (!(ams->pb & 0x40) && (val & 0x40)) { /*Reset keyboard*/
#if ENABLE_KEYBOARD_LOG
			pclog("keyboard_amstrad : reset keyboard\n");
#endif
			kbd_adddata(0xaa);
		}
		ams->pb = val;
		ppi.pb = val;

		timer_process();
		timer_update_outstanding();

		speaker_update();
		speaker_gated = val & 1;
		speaker_enable = val & 2;
		if (speaker_enable) 
			was_speaker_enable = 1;
		pit_set_gate(&pit, 2, val & 1);

		if (val & 0x80)
			ams->pa = 0;
		break;

	case 0x63:
		break;

	case 0x64:
		ams->systemstat_1 = val;
		break;

	case 0x65:
		ams->systemstat_2 = val;
		break;

	default:
		pclog("\nBad Amstrad keyboard write %04X %02X\n", port, val);
    }
}


static uint8_t
kbd_read(uint16_t port, void *priv)
{
    amstrad_t *ams = (amstrad_t *)priv;
    uint8_t ret = 0xff;

    switch (port) {
	case 0x60:
		if (ams->pb & 0x80) {
			ret = (ams->systemstat_1 | 0xd) & 0x7f;
		} else {
			ret = ams->pa;
			if (key_queue_start == key_queue_end) {
				ams->wantirq = 0;
			} else {
				ams->key_waiting = key_queue[key_queue_start];
				key_queue_start = (key_queue_start + 1) & 0xf;
				ams->wantirq = 1;	
			}
		}	
		break;

	case 0x61:
		ret = ams->pb;
		break;
		
	case 0x62:
		if (ams->pb & 0x04)
		   ret = ams->systemstat_2 & 0xf;
		else
		   ret = ams->systemstat_2 >> 4;
		ret |= (ppispeakon ? 0x20 : 0);
		if (nmi)
			ret |= 0x40;
		break;

	default:
		pclog("\nBad Amstrad keyboard read %04X\n", port);
    }

    return(ret);
}


static void
kbd_poll(void *priv)
{
    amstrad_t *ams = (amstrad_t *)priv;

    keyboard_delay += (1000 * TIMER_USEC);

    if (ams->wantirq) {
	ams->wantirq = 0;
	ams->pa = ams->key_waiting;
	picint(2);
#if ENABLE_KEYBOARD_LOG
	pclog("keyboard_amstrad : take IRQ\n");
#endif
    }

    if (key_queue_start != key_queue_end && !ams->pa) {
	ams->key_waiting = key_queue[key_queue_start];
#if ENABLE_KEYBOARD_LOG
	pclog("Reading %02X from the key queue at %i\n",
			ams->key_waiting, key_queue_start);
#endif
	key_queue_start = (key_queue_start + 1) & 0xf;
	ams->wantirq = 1;
    }
}


static uint8_t
amstrad_read(uint16_t port, void *priv)
{
    amstrad_t *ams = (amstrad_t *)priv;

    pclog("amstrad_read: %04X\n", port);

    switch (port) {
	case 0x379:
		return(7);

	case 0x37a:
		if (romset == ROM_PC1512) return(0x20);
		if (romset == ROM_PC200)  return(0x80);
		return(0);

	case 0xdead:
		return(ams->dead);
    }

    return(0xff);
}


static void
amstrad_write(uint16_t port, uint8_t val, void *priv)
{
    amstrad_t *ams = (amstrad_t *)priv;

    switch (port) {
	case 0xdead:
		ams->dead = val;
		break;
    }
}


void
machine_amstrad_init(machine_t *model)
{
    amstrad_t *ams;

    ams = (amstrad_t *)malloc(sizeof(amstrad_t));
    memset(ams, 0x00, sizeof(amstrad_t));

    machine_common_init(model);

    lpt2_remove_ams();

    io_sethandler(0x0379, 2,
		  amstrad_read, NULL, NULL, NULL, NULL, NULL, ams);

    io_sethandler(0xdead, 1,
		  amstrad_read, NULL, NULL, amstrad_write, NULL, NULL, ams);

    io_sethandler(0x0078, 1,
		  ms_read, NULL, NULL, ms_write, NULL, NULL, ams);

    io_sethandler(0x007a, 1,
		  ms_read, NULL, NULL, ms_write, NULL, NULL, ams);

    ams->wantirq = 0;
    io_sethandler(0x0060, 6,
		  kbd_read, NULL, NULL, kbd_write, NULL, NULL, ams);
    timer_add(kbd_poll, &keyboard_delay, TIMER_ALWAYS_ENABLED, ams);
    keyboard_send = kbd_adddata;
    keyboard_scan = 1;

    /* Tell mouse driver about our internal mouse. */
    mouse_setpoll(ms_poll, ams);

    if (joystick_type != 7)
	device_add(&gameport_device);

    /* FIXME: make sure this is correct? */
    nvr_at_init(1);

    nmi_init();

    fdc_set_dskchg_activelow();
}
