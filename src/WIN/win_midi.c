/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		MIDI interface to host device.
 *
 * Version:	@(#)win_midi.c	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

#include <windows.h>
#include <mmsystem.h>
#include "../ibm.h"
#include "../config.h"
#include "plat_midi.h"

int midi_id = 0;
static HMIDIOUT midi_out_device = NULL;

HANDLE m_event;

void midi_close();

static uint8_t midi_rt_buf[1024];
static uint8_t midi_cmd_buf[1024];
static int midi_cmd_pos = 0;
static int midi_cmd_len = 0;
static uint8_t midi_status = 0;
static unsigned int midi_sysex_start = 0;
static unsigned int midi_sysex_delay = 0;

uint8_t MIDI_evt_len[256] = {
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x00
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x10
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x20
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x30
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x40
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x50
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x60
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x70

  3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,  // 0x80
  3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,  // 0x90
  3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,  // 0xa0
  3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,  // 0xb0

  2,2,2,2, 2,2,2,2, 2,2,2,2, 2,2,2,2,  // 0xc0
  2,2,2,2, 2,2,2,2, 2,2,2,2, 2,2,2,2,  // 0xd0

  3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,  // 0xe0

  0,2,3,2, 0,0,1,0, 1,0,1,1, 1,0,1,0   // 0xf0
};

void midi_init()
{
        MMRESULT hr = MMSYSERR_NOERROR;

	memset(midi_rt_buf, 0, sizeof(midi_rt_buf));
	memset(midi_cmd_buf, 0, sizeof(midi_cmd_buf));

	midi_cmd_pos = midi_cmd_len = 0;
	midi_status = 0;

	midi_sysex_start = midi_sysex_delay = 0;

	m_event = CreateEvent(NULL, TRUE, TRUE, NULL);

        hr = midiOutOpen(&midi_out_device, midi_id, (DWORD) m_event,
		   0, CALLBACK_EVENT);
        if (hr != MMSYSERR_NOERROR) {
                printf("midiOutOpen error - %08X\n",hr);
                midi_id = 0;
                hr = midiOutOpen(&midi_out_device, midi_id, (DWORD) m_event,
        		   0, CALLBACK_EVENT);
                if (hr != MMSYSERR_NOERROR) {
                        printf("midiOutOpen error - %08X\n",hr);
                        return;
                }
        }
        
        midiOutReset(midi_out_device);
}

void midi_close()
{
        if (midi_out_device != NULL)
        {
                midiOutReset(midi_out_device);
                midiOutClose(midi_out_device);
                /* midi_out_device = NULL; */
		CloseHandle(m_event);
        }
}

int midi_get_num_devs()
{
        return midiOutGetNumDevs();
}
void midi_get_dev_name(int num, char *s)
{
        MIDIOUTCAPS caps;

        midiOutGetDevCaps(num, &caps, sizeof(caps));
        strcpy(s, caps.szPname);
}

static int midi_pos, midi_len;
static uint32_t midi_command;
static int midi_lengths[8] = {3, 3, 3, 3, 2, 2, 3, 1};
static int midi_insysex;
static char midi_sysex_data[1024+2];

static void midi_send_sysex()
{
        MIDIHDR hdr;
        
        hdr.lpData = midi_sysex_data;
        hdr.dwBufferLength = midi_pos;
        hdr.dwFlags = 0;

        midiOutPrepareHeader(midi_out_device, &hdr, sizeof(MIDIHDR));
        midiOutLongMsg(midi_out_device, &hdr, sizeof(MIDIHDR));

        midi_insysex = 0;
}

void PlayMsg(uint8_t *msg)
{
	midiOutShortMsg(midi_out_device, *(uint32_t *) msg);
}

MIDIHDR m_hdr;

void PlaySysex(uint8_t *sysex, unsigned int len)
{
	MMRESULT result;

	if (WaitForSingleObject(m_event, 2000) == WAIT_TIMEOUT)
	{
		pclog("Can't send MIDI message\n");
		return;
	}

	midiOutUnprepareHeader(midi_out_device, &m_hdr, sizeof(m_hdr));

	m_hdr.lpData = (char *) sysex;
	m_hdr.dwBufferLength = len;
	m_hdr.dwBytesRecorded = len;
	m_hdr.dwUser = 0;

	result = midiOutPrepareHeader(midi_out_device, &m_hdr, sizeof(m_hdr));

	if (result != MMSYSERR_NOERROR)  return;
	ResetEvent(m_event);
	result = midiOutLongMsg(midi_out_device, &m_hdr, sizeof(m_hdr));
	if (result != MMSYSERR_NOERROR)
	{
		SetEvent(m_event);
		return;
	}
}

#define SYSEX_SIZE 1024
#define RAWBUF 1024

void midi_write(uint8_t val)
{
	uint32_t passed_ticks;

	if (midi_sysex_start)
	{
		passed_ticks = GetTickCount() - midi_sysex_start;
		if (passed_ticks < midi_sysex_delay)
		{
			Sleep(midi_sysex_delay - passed_ticks);
		}
	}

	/* Test for a realtime MIDI message */
	if (val >= 0xf8)
	{
		midi_rt_buf[0] = val;
		PlayMsg(midi_rt_buf);
		return;
	}

	/* Test for a active sysex transfer */

	if (midi_status == 0xf0)
	{
		if (!(val & 0x80))
		{
			if (midi_pos  < (SYSEX_SIZE-1))  midi_sysex_data[midi_pos++] = val;
			return;
		}
		else
		{
			midi_sysex_data[midi_pos++] = 0xf7;

			if ((midi_sysex_start) && (midi_pos >= 4) && (midi_pos <= 9) && (midi_sysex_data[1] == 0x411) && (midi_sysex_data[3] == 0x16))
			{
				/* pclog("MIDI: Skipping invalid MT-32 SysEx MIDI message\n"); */
			}
			else
			{
				PlaySysex(midi_sysex_data, midi_pos);
				if (midi_sysex_start)
				{
					if (midi_sysex_data[5] == 0x7f)
					{
						midi_sysex_delay = 290;		/* All parameters reset */
					}
					else if ((midi_sysex_data[5] == 0x10) && (midi_sysex_data[6] == 0x00) && (midi_sysex_data[7] == 0x04))
					{
						midi_sysex_delay = 145;		/* Viking Child */
					}
					else if ((midi_sysex_data[5] == 0x10) && (midi_sysex_data[6] == 0x00) && (midi_sysex_data[7] == 0x01))
					{
						midi_sysex_delay = 30;		/* Dark Sun 1 */
					}
					else
						midi_sysex_delay = (unsigned int) (((float) (midi_pos) * 1.25f) * 1000.0f / 3125.0f) + 2;

					midi_sysex_start = GetTickCount();
				}
			}
		}
	}


	if (val & 0x80)
	{
		midi_status = val;
		midi_cmd_pos = 0;
		midi_cmd_len = MIDI_evt_len[val];
		if (midi_status == 0xf0)
		{
			midi_sysex_data[0] = 0xf0;
			midi_pos = 1;
		}
	}

	if (midi_cmd_len)
	{
		midi_cmd_buf[midi_cmd_pos++] = val;
		if (midi_cmd_pos >= midi_cmd_len)
		{
			PlayMsg(midi_cmd_buf);
			midi_cmd_pos = 1;
		}
	}
}

void midi_reset()
{
	uint8_t buf[64], used;

	/* Flush buffers */
	midiOutReset(midi_out_device);

	/* GM1 reset */
	buf[0] = 0xf0;
	buf[1] = 0x7e;
	buf[2] = 0x7f;
	buf[3] = 0x09;
	buf[4] = 0x01;
	buf[5] = 0xf7;
	PlaySysex((uint8_t *) buf, 6);

	/* GS1 reset */
	buf[0] = 0xf0;
	buf[1] = 0x41;
	buf[2] = 0x10;
	buf[3] = 0x42;
	buf[4] = 0x12;
	buf[5] = 0x40;
	buf[6] = 0x00;
	buf[7] = 0x7f;
	buf[8] = 0x00;
	buf[9] = 0x41;
	buf[10] = 0xf7;
	PlaySysex((uint8_t *) buf, 11);
}
