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
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Bit,
 *		DOSBox Team,
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2016-2020 Bit.
 *		Copyright 2008-2020 DOSBox Team.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/midi.h>


int midi_device_current = 0;
static int midi_device_last = 0;
int midi_input_device_current = 0;
static int midi_input_device_last = 0;

midi_t *midi = NULL, *midi_in = NULL;

midi_in_handler_t *mih_first = NULL, *mih_last = NULL,
		  *mih_cur = NULL;

uint8_t MIDI_InSysexBuf[SYSEX_SIZE];

uint8_t MIDI_evt_len[256] = {
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
    const char *internal_name;
    const device_t *device;
} MIDI_DEVICE, MIDI_IN_DEVICE;

static const MIDI_DEVICE devices[] =
{
    { "none",				NULL			},
#ifdef USE_FLUIDSYNTH
    { "fluidsynth",			&fluidsynth_device	},
#endif
#ifdef USE_MUNT
    { "mt32",				&mt32_device		},
    { "cm32l",				&cm32l_device		},
#endif
    { SYSTEM_MIDI_INTERNAL_NAME,	&rtmidi_device	},
    { "",				NULL			}
};

static const MIDI_IN_DEVICE midi_in_devices[] =
{
    { "none",				NULL			},
    { MIDI_INPUT_INTERNAL_NAME,		&rtmidi_input_device	},
    { "",				NULL			}
};


int
midi_device_available(int card)
{
    if (devices[card].device)
	return device_available(devices[card].device);

    return 1;
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

    midi->m_out_device = device;
}

void
midi_in_init(midi_device_t* device, midi_t **mididev)
{
    *mididev = (midi_t *)malloc(sizeof(midi_t));
    memset(*mididev, 0, sizeof(midi_t));

    (*mididev)->m_in_device = device;
}


void
midi_close(void)
{
    if (midi && midi->m_out_device) {
	free(midi->m_out_device);
	midi->m_out_device = NULL;
    }

    if (midi) {
	free(midi);
	midi = NULL;
    }
}

void
midi_in_close(void)
{
    if (midi_in && midi_in->m_in_device) {
	free(midi_in->m_in_device);
	midi_in->m_in_device = NULL;
    }

    if (midi_in) {
	free(midi_in);
	midi_in = NULL;
    }
}


void
midi_poll(void)
{
    if (midi && midi->m_out_device && midi->m_out_device->poll)
	midi->m_out_device->poll();
}


void
play_msg(uint8_t *msg)
{
    if (midi->m_out_device->play_msg)
	midi->m_out_device->play_msg(msg);
}


void
play_sysex(uint8_t *sysex, unsigned int len)
{
    if (midi->m_out_device->play_sysex)
	midi->m_out_device->play_sysex(sysex, len);
}


int
midi_in_device_available(int card)
{
    if (midi_in_devices[card].device)
	return device_available(midi_in_devices[card].device);

    return 1;
}


const device_t *
midi_in_device_getdevice(int card)
{
    return midi_in_devices[card].device;
}


int
midi_in_device_has_config(int card)
{
    if (!midi_in_devices[card].device)
	return 0;
    return midi_in_devices[card].device->config ? 1 : 0;
}


char *
midi_in_device_get_internal_name(int card)
{
    return (char *) midi_in_devices[card].internal_name;
}


int
midi_in_device_get_from_internal_name(char *s)
{
    int c = 0;

    while (strlen(midi_in_devices[c].internal_name)) {
	if (!strcmp(midi_in_devices[c].internal_name, s))
		return c;
	c++;
    }

    return 0;
}


void
midi_in_device_init()
{
    if (midi_in_devices[midi_input_device_current].device)
	device_add(midi_in_devices[midi_input_device_current].device);
    midi_input_device_last = midi_input_device_current;
}


void
midi_raw_out_rt_byte(uint8_t val)
{
    if (!midi_in)
	return;

    if (!midi_in->midi_realtime)
	return;

    if ((!midi_in->midi_clockout && (val == 0xf8)))
	return;

    midi_in->midi_cmd_r = val << 24;
    /* pclog("Play RT Byte msg\n"); */
    play_msg((uint8_t *)&midi_in->midi_cmd_r);
}


void
midi_raw_out_thru_rt_byte(uint8_t val)
{
    if (midi_in && midi_in->thruchan)
	midi_raw_out_rt_byte(val);
}


void
midi_raw_out_byte(uint8_t val)
{
    uint32_t passed_ticks;

    if (!midi || !midi->m_out_device)
	return;

    if ((midi->m_out_device->write && midi->m_out_device->write(val)))
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


void 
midi_clear_buffer(void)
{
    if (!midi)
	return;

    midi->midi_pos = 0;
    midi->midi_status = 0x00;
    midi->midi_cmd_pos = 0;
    midi->midi_cmd_len = 0;
}


void
midi_in_handler(int set, void (*msg)(void *p, uint8_t *msg), int (*sysex)(void *p, uint8_t *buffer, uint32_t len, int abort), void *p)
{
    midi_in_handler_t *temp = NULL, *next;

    if (set) {
	/* Add MIDI IN handler. */
	if ((mih_first == NULL) && (mih_last != NULL))
		fatal("Last MIDI IN handler present with no first MIDI IN handler\n");

	if ((mih_first != NULL) && (mih_last == NULL))
		fatal("First MIDI IN handler present with no last MIDI IN handler\n");

	temp = (midi_in_handler_t *) malloc(sizeof(midi_in_handler_t));
	memset(temp, 0, sizeof(midi_in_handler_t));
	temp->msg = msg;
	temp->sysex = sysex;
	temp->p = p;

	if (mih_last == NULL)
		mih_first = mih_last = temp;
	else {
		temp->prev = mih_last;
		mih_last = temp;
	}
    } else if ((mih_first != NULL) && (mih_last != NULL)) {
	temp = mih_first;

	while(1) {
		if (temp == NULL)
			break;

		if ((temp->msg == msg) && (temp->sysex == sysex) && (temp->p == p)) {
			if (temp->prev != NULL)
				temp->prev->next = temp->next;

			if (temp->next != NULL)
				temp->next->prev = temp->prev;

			next = temp->next;

			if (temp == mih_first) {
				mih_first = NULL;
				if (next == NULL)
					mih_last = NULL;
			}

			if (temp == mih_last)
				mih_last = NULL;

			free(temp);
			temp = next;

			if (next == NULL)
				break;
		}
	}
    }
}


void
midi_in_handlers_clear(void)
{
    midi_in_handler_t *temp = mih_first, *next;

    while(1) {
	if (temp == NULL)
		break;

	next = temp->next;
	free(temp);

	temp = next;

	if (next == NULL)
		break;
    }

    mih_first = mih_last = NULL;
}


void
midi_in_msg(uint8_t *msg)
{
    midi_in_handler_t *temp = mih_first;

    while(1) {
	if (temp == NULL)
		break;

	if (temp->msg)
		temp->msg(temp->p, msg);

	temp = temp->next;

	if (temp == NULL)
		break;
    }
}


static void
midi_start_sysex(uint8_t *buffer, uint32_t len)
{
    midi_in_handler_t *temp = mih_first;

    while(1) {
	if (temp == NULL)
		break;

	temp->cnt = 5;
	temp->buf = buffer;
	temp->len = len;

	temp = temp->next;

	if (temp == NULL)
		break;
    }
}


/* Returns:
	0 = All handlers have returnd 0;
	1 = There are still handlers to go. */
static int
midi_do_sysex(void)
{
    midi_in_handler_t *temp = mih_first;
    int ret, cnt_acc = 0;

    while(1) {
	if (temp == NULL)
		break;

	/* Do nothing if the handler has a zero count. */
	if ((temp->cnt > 0) || (temp->len > 0)) {
		ret = 0;
		if (temp->sysex) {
			if (temp->cnt == 0)
				ret = temp->sysex(temp->p, temp->buf, 0, 0);
			else
				ret = temp->sysex(temp->p, temp->buf, temp->len, 0);
		}

		/* If count is 0 and length is 0, then this is just a finishing
		   call to temp->sysex(), so skip this entire block. */
		if (temp->cnt > 0) {
			if (ret) {
				/* Decrease or reset the counter. */
				if (temp->len == ret)
					temp->cnt--;
				else
					temp->cnt = 5;

				/* Advance the buffer pointer and remember the
				   remaining length. */
				temp->buf += (temp->len - ret);
				temp->len = ret;
			} else {
				/* Set count to 0 so that this handler will be
				   ignored on the next interation. */
				temp->cnt = 0;

				/* Reset the buffer pointer and length. */
				temp->buf = NULL;
				temp->len = 0;
			}

			/* If the remaining count is above zero, add it to the
			   accumulator. */
			if (temp->cnt > 0)
				cnt_acc |= temp->cnt;
		}
	}

	temp = temp->next;

	if (temp == NULL)
		break;
    }

    /* Return 0 if all handlers have returned 0 or all the counts are otherwise 0. */
    if (cnt_acc == 0)
	return 0;
    else
	return 1;
}


void
midi_in_sysex(uint8_t *buffer, uint32_t len)
{
    midi_start_sysex(buffer, len);

    while (1) {
	/* This will return 0 if all theh handlers have either
	   timed out or otherwise indicated it is time to stop. */
	if (midi_do_sysex())
		plat_delay_ms(5);	/* msec */
	else
		break;
    }
}
