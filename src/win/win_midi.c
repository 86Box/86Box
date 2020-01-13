#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../config.h"
#include "../sound/midi.h"
#include "../plat.h"
#include "../plat_midi.h"


typedef struct
{
    int midi_id, midi_input_id;

    HANDLE m_event;

    HMIDIOUT midi_out_device;
	HMIDIIN midi_in_device;

    MIDIHDR m_hdr;
} plat_midi_t;

plat_midi_t *pm = NULL, *pm_in = NULL;


void
plat_midi_init(void)
{
    MMRESULT hr;

    pm = (plat_midi_t *) malloc(sizeof(plat_midi_t));
    memset(pm, 0, sizeof(plat_midi_t));

    pm->midi_id = config_get_int(SYSTEM_MIDI_NAME, "midi", 0);

    hr = MMSYSERR_NOERROR;

    pm->m_event = CreateEvent(NULL, TRUE, TRUE, NULL);

    hr = midiOutOpen(&pm->midi_out_device, pm->midi_id,
		     (uintptr_t) pm->m_event, 0, CALLBACK_EVENT);
    if (hr != MMSYSERR_NOERROR) {
	printf("midiOutOpen error - %08X\n", hr);
	pm->midi_id = 0;
	hr = midiOutOpen(&pm->midi_out_device, pm->midi_id,
			 (uintptr_t) pm->m_event, 0, CALLBACK_EVENT);
	if (hr != MMSYSERR_NOERROR) {
		printf("midiOutOpen error - %08X\n", hr);
		return;
	}
    }

    midiOutReset(pm->midi_out_device);
}


void
plat_midi_close(void)
{
    if (pm) {
	if (pm->midi_out_device != NULL) {
		midiOutReset(pm->midi_out_device);
		midiOutClose(pm->midi_out_device);
		CloseHandle(pm->m_event);
	}
	
	free(pm);
	pm = NULL;
    }
}


int
plat_midi_get_num_devs(void)
{
    return midiOutGetNumDevs();
}


void
plat_midi_get_dev_name(int num, char *s)
{
    MIDIOUTCAPS caps;

    midiOutGetDevCaps(num, &caps, sizeof(caps));
    strcpy(s, caps.szPname);
}


void
plat_midi_play_msg(uint8_t *msg)
{
    if (!pm)
	return;

    midiOutShortMsg(pm->midi_out_device, *(uint32_t *) msg);
}


void
plat_midi_play_sysex(uint8_t *sysex, unsigned int len)
{
    MMRESULT result;

    if (!pm)
	return;

    if (WaitForSingleObject(pm->m_event, 2000) == WAIT_TIMEOUT)
	return;

    midiOutUnprepareHeader(pm->midi_out_device, &pm->m_hdr, sizeof(pm->m_hdr));

    pm->m_hdr.lpData = (char *) sysex;
    pm->m_hdr.dwBufferLength = len;
    pm->m_hdr.dwBytesRecorded = len;
    pm->m_hdr.dwUser = 0;

    result = midiOutPrepareHeader(pm->midi_out_device, &pm->m_hdr, sizeof(pm->m_hdr));

    if (result != MMSYSERR_NOERROR)
	return;
    ResetEvent(pm->m_event);
    result = midiOutLongMsg(pm->midi_out_device, &pm->m_hdr, sizeof(pm->m_hdr));
    if (result != MMSYSERR_NOERROR) {
	SetEvent(pm->m_event);
	return;
    }
}


int
plat_midi_write(uint8_t val)
{
    return 0;
}

void CALLBACK
plat_midi_in_callback(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) 
{
	uint8_t msg[4] = {((dwParam1&0xff)),(((dwParam1&0xff00)>>8)),
					(((dwParam1&0xff0000)>>16)),MIDI_evt_len[((dwParam1&0xff))]};
	uint8_t *sysex;
	uint32_t len;
	int cnt;
	MIDIHDR *hdr;
	switch (wMsg) {
		case MM_MIM_DATA:  /* 0x3C3 - midi message */
			input_msg(midi_in_p, msg);
			break;
		case MM_MIM_OPEN:  /* 0x3C1 */
			break;
		case MM_MIM_CLOSE: /* 0x3C2 */
			break;
		case MM_MIM_LONGDATA: /* 0x3C4 - sysex */
			hdr = (MIDIHDR *)dwParam1;
			sysex = (uint8_t *)hdr->lpData; 
			len = (uint32_t)hdr->dwBytesRecorded;
			cnt = 5;
			while (cnt) { /*abort if timed out*/
				int ret = input_sysex(midi_in_p, sysex, len, 0);
				if (!ret) {
					len = 0;
					break;
				}
				if (len==ret) 
					cnt--; 
				else 
					cnt = 5;
				sysex += len-ret;
				len = ret;
				Sleep(5);/*msec*/
			}
			if (len) 
				input_sysex(midi_in_p, sysex, 0, 0);
			
			midiInUnprepareHeader(hMidiIn, hdr, sizeof(*hdr));
			hdr->dwBytesRecorded = 0;
			midiInPrepareHeader(hMidiIn, hdr, sizeof(*hdr));
			break;
		case MM_MIM_ERROR:
		case MM_MIM_LONGERROR:
			break;
		default:
			break;
	}
}

void
plat_midi_input_init(void)
{
    MMRESULT hr;

    pm_in = (plat_midi_t *) malloc(sizeof(plat_midi_t));
    memset(pm_in, 0, sizeof(plat_midi_t));

    pm_in->midi_input_id = config_get_int(MIDI_INPUT_NAME, "midi_input", 0);
	
	hr = MMSYSERR_NOERROR;	
	
    hr = midiInOpen(&pm_in->midi_in_device, pm_in->midi_input_id,
		     (uintptr_t) plat_midi_in_callback, 0, CALLBACK_FUNCTION);
    if (hr != MMSYSERR_NOERROR) {
	printf("midiInOpen error - %08X\n", hr);
	pm_in->midi_input_id = 0;
	hr = midiInOpen(&pm_in->midi_in_device, pm_in->midi_input_id,
			 (uintptr_t) plat_midi_in_callback, 0, CALLBACK_FUNCTION);
	if (hr != MMSYSERR_NOERROR) {
		printf("midiInOpen error - %08X\n", hr);
		return;
	}
    }
	
	pm_in->m_hdr.lpData = (char*)&MIDI_InSysexBuf[0];
	pm_in->m_hdr.dwBufferLength = SYSEX_SIZE;
	pm_in->m_hdr.dwBytesRecorded = 0;
	pm_in->m_hdr.dwUser = 0;
	midiInPrepareHeader(pm_in->midi_in_device,&pm_in->m_hdr,sizeof(pm_in->m_hdr));
	midiInStart(pm_in->midi_in_device);
}

void
plat_midi_input_close(void)
{
    if (pm_in) {
	if (pm_in->midi_in_device != NULL) {
		midiInStop(pm_in->midi_in_device);
		midiInClose(pm_in->midi_in_device);
	}
	
	free(pm_in);
	pm_in = NULL;
    }
}

int
plat_midi_in_get_num_devs(void)
{
    return midiInGetNumDevs();
}


void
plat_midi_in_get_dev_name(int num, char *s)
{
    MIDIINCAPS caps;

    midiInGetDevCaps(num, &caps, sizeof(caps));
    strcpy(s, caps.szPname);
}