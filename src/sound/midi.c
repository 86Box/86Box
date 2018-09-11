/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		MIDI device core module.
 *
 * Version:	@(#)midi.c	1.0.0	2018/09/06
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Bit,
 *		DOSBox Team,
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2016-2018 Bit.
 *		Copyright 2008-2018 DOSBox Team.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../device.h"
#include "../plat.h"
#include "../plat_midi.h"
#include "midi.h"
#include "midi_system.h"
#ifdef USE_FLUIDSYNTH
# include "midi_fluidsynth.h"
#endif
#ifdef USE_MUNT
# include "midi_mt32.h"
#endif


#define SYSEX_SIZE 1024
#define RAWBUF 1024

int midi_device_current = 0;
static int midi_device_last = 0;


typedef struct
{
    uint8_t midi_rt_buf[1024], midi_cmd_buf[1024],
	    midi_status, midi_sysex_data[1026];
    int midi_cmd_pos, midi_cmd_len;
    unsigned int midi_sysex_start, midi_sysex_delay,
		 midi_pos;
    midi_device_t* m_device;
} midi_t;

static midi_t *midi = NULL;

static uint8_t MIDI_evt_len[256] = {
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,		/* 0x00 */
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,		/* 0x10 */
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,		/* 0x20 */
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,		/* 0x30 */
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,		/* 0x40 */
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,		/* 0x50 */
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,		/* 0x60 */
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,		/* 0x70 */

    3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,		/* 0x80 */
    3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,		/* 0x90 */
    3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,		/* 0xa0 */
    3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,		/* 0xb0 */

    2,2,2,2, 2,2,2,2, 2,2,2,2, 2,2,2,2,		/* 0xc0 */
    2,2,2,2, 2,2,2,2, 2,2,2,2, 2,2,2,2,		/* 0xd0 */

    3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,		/* 0xe0 */

    0,2,3,2, 0,0,1,0, 1,0,1,1, 1,0,1,0		/* 0xf0 */
};

typedef struct
{
    const char *name, *internal_name;
    const device_t *device;
} MIDI_DEVICE;

static const MIDI_DEVICE devices[] =
{
    {"None",                        "none",                         NULL},
#ifdef USE_FLUIDSYNTH
    {"FluidSynth",                  "fluidsynth",                   &fluidsynth_device},
#endif
#ifdef USE_MUNT
    {"Roland MT-32 Emulation",      "mt32",                         &mt32_device},
    {"Roland CM-32L Emulation",     "cm32l",                        &cm32l_device},
#endif
    {SYSTEM_MIDI_NAME,              SYSTEM_MIDI_INTERNAL_NAME,      &system_midi_device},
    {"", "", NULL}
};


int
midi_device_available(int card)
{
    if (devices[card].device)
	return device_available(devices[card].device);

    return 1;
}


char *
midi_device_getname(int card)
{
    return (char *) devices[card].name;
}


const device_t *
midi_device_getdevice(int card)
{
    return devices[card].device;
}


int
midi_device_has_config(int card)
{
    if (!devices[card].device)
	return 0;
    return devices[card].device->config ? 1 : 0;
}


char *
midi_device_get_internal_name(int card)
{
    return (char *) devices[card].internal_name;
}


int
midi_device_get_from_internal_name(char *s)
{
    int c = 0;

    while (strlen(devices[c].internal_name)) {
	if (!strcmp(devices[c].internal_name, s))
		return c;
	c++;
    }

    return 0;
}


void
midi_device_init()
{
    if (devices[midi_device_current].device)
	device_add(devices[midi_device_current].device);
    midi_device_last = midi_device_current;
}


void
midi_init(midi_device_t* device)
{
    midi = (midi_t *) malloc(sizeof(midi_t));
    memset(midi, 0, sizeof(midi_t));

    midi->m_device = device;
}


void
midi_close(void)
{
    if (midi) {
	free(midi);
	midi = NULL;
    }
}


void
midi_poll(void)
{
    if (midi && midi->m_device && midi->m_device->poll)
	midi->m_device->poll();
}


void
play_msg(uint8_t *msg)
{
    if (midi->m_device->play_msg)
	midi->m_device->play_msg(msg);
}


void
play_sysex(uint8_t *sysex, unsigned int len)
{
    if (midi->m_device->play_sysex)
	midi->m_device->play_sysex(sysex, len);
}


void
midi_write(uint8_t val)
{
    uint32_t passed_ticks;

    if (!midi || !midi->m_device)
	return;

    if (midi->m_device->write && midi->m_device->write(val))
	return;

    if (midi->midi_sysex_start) {
	passed_ticks = plat_get_ticks() - midi->midi_sysex_start;
	if (passed_ticks < midi->midi_sysex_delay)
		plat_delay_ms(midi->midi_sysex_delay - passed_ticks);
    }

    /* Test for a realtime MIDI message */
    if (val >= 0xf8) {
	midi->midi_rt_buf[0] = val;
	play_msg(midi->midi_rt_buf);
	return;
    }

    /* Test for a active sysex transfer */
    if (midi->midi_status == 0xf0) {
	if (!(val & 0x80)) {
		if (midi->midi_pos  < (SYSEX_SIZE-1))
			midi->midi_sysex_data[midi->midi_pos++] = val;
		return;
	} else {
		midi->midi_sysex_data[midi->midi_pos++] = 0xf7;

		if ((midi->midi_sysex_start) && (midi->midi_pos >= 4) && (midi->midi_pos <= 9) &&
		    (midi->midi_sysex_data[1] == 0x41) && (midi->midi_sysex_data[3] == 0x16)) {
			/* pclog("MIDI: Skipping invalid MT-32 SysEx MIDI message\n"); */
		} else {
			play_sysex(midi->midi_sysex_data, midi->midi_pos);
			if (midi->midi_sysex_start) {
				if (midi-> midi_sysex_data[5] == 0x7f)
					midi->midi_sysex_delay = 290;	/* All parameters reset */
				else if ((midi->midi_sysex_data[5] == 0x10) && (midi->midi_sysex_data[6] == 0x00) &&
					 (midi->midi_sysex_data[7] == 0x04))
					midi->midi_sysex_delay = 145;	/* Viking Child */
				else if ((midi->midi_sysex_data[5] == 0x10) && (midi->midi_sysex_data[6] == 0x00) &&
					 (midi->midi_sysex_data[7] == 0x01))
					midi->midi_sysex_delay = 30;	/* Dark Sun 1 */
				else
					midi->midi_sysex_delay = (unsigned int) (((float) (midi->midi_pos) * 1.25f) * 1000.0f / 3125.0f) + 2;

				midi->midi_sysex_start = plat_get_ticks();
			}
		}
	}
    }

    if (val & 0x80) {
	midi->midi_status = val;
	midi->midi_cmd_pos = 0;
	midi->midi_cmd_len = MIDI_evt_len[val];
	if (midi->midi_status == 0xf0) {
		midi->midi_sysex_data[0] = 0xf0;
		midi->midi_pos = 1;
	}
    }

    if (midi->midi_cmd_len) {
	midi->midi_cmd_buf[midi->midi_cmd_pos++] = val;
	if (midi->midi_cmd_pos >= midi->midi_cmd_len) {
		play_msg(midi->midi_cmd_buf);
		midi->midi_cmd_pos = 1;
	}
    }
}
