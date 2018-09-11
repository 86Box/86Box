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
    int midi_id;

    HANDLE m_event;

    HMIDIOUT midi_out_device;

    MIDIHDR m_hdr;
} plat_midi_t;

plat_midi_t *pm = NULL;


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
