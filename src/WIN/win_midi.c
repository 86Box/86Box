#include <windows.h>
#include <mmsystem.h>
#include "../ibm.h"
#include "../config.h"
#include "../SOUND/midi.h"
#include "plat_midi.h"

typedef struct plat_midi_t
{
        int id;
        char name[512];
        HMIDIOUT midi_out_device;
        HANDLE event;
        MIDIHDR hdr;
} plat_midi_t;

void* plat_midi_init()
{
        plat_midi_t* data = malloc(sizeof(plat_midi_t));
        memset(data, 0, sizeof(plat_midi_t));

	/* This is for compatibility with old configuration files. */
	data->id = config_get_int("Sound", "midi_host_device", -1);
	if (data->id == -1)
	{
		data->id = config_get_int(SYSTEM_MIDI_NAME, "midi", 0);
	}
	else
	{
		config_delete_var("Sound", "midi_host_device");
		config_set_int(SYSTEM_MIDI_NAME, "midi", data->id);
	}

        MMRESULT hr = MMSYSERR_NOERROR;

	data->event = CreateEvent(NULL, TRUE, TRUE, NULL);

        hr = midiOutOpen(&data->midi_out_device, data->id, (DWORD) data->event,
		   0, CALLBACK_EVENT);
        if (hr != MMSYSERR_NOERROR) {
                printf("midiOutOpen error - %08X\n",hr);
                data->id = 0;
                hr = midiOutOpen(&data->midi_out_device, data->id, (DWORD) data->event,
        		   0, CALLBACK_EVENT);
                if (hr != MMSYSERR_NOERROR) {
                        printf("midiOutOpen error - %08X\n",hr);
                        free(data);
                        return 0;
                }
        }

        plat_midi_get_dev_name(data->id, data->name);

        midiOutReset(data->midi_out_device);

        return data;
}

void plat_midi_close(void* p)
{
        plat_midi_t* data = (plat_midi_t*)p;
        if (data->midi_out_device != NULL)
        {
                midiOutReset(data->midi_out_device);
                midiOutClose(data->midi_out_device);
                /* midi_out_device = NULL; */
		CloseHandle(data->event);
        }
        free(data);
}

int plat_midi_get_num_devs()
{
        return midiOutGetNumDevs();
}
void plat_midi_get_dev_name(int num, char *s)
{
        MIDIOUTCAPS caps;

        midiOutGetDevCaps(num, &caps, sizeof(caps));
        strcpy(s, caps.szPname);
}

void plat_midi_play_msg(midi_device_t* device, uint8_t *msg)
{
        plat_midi_t* data = (plat_midi_t*)device->data;
	midiOutShortMsg(data->midi_out_device, *(uint32_t *) msg);
}

void plat_midi_play_sysex(midi_device_t* device, uint8_t *sysex, unsigned int len)
{
        plat_midi_t* data = (plat_midi_t*)device->data;
	MMRESULT result;

	if (WaitForSingleObject(data->event, 2000) == WAIT_TIMEOUT)
	{
		pclog("Can't send MIDI message\n");
		return;
	}

	midiOutUnprepareHeader(data->midi_out_device, &data->hdr, sizeof(data->hdr));

	data->hdr.lpData = (char *) sysex;
	data->hdr.dwBufferLength = len;
	data->hdr.dwBytesRecorded = len;
	data->hdr.dwUser = 0;

	result = midiOutPrepareHeader(data->midi_out_device, &data->hdr, sizeof(data->hdr));

	if (result != MMSYSERR_NOERROR)  return;
	ResetEvent(data->event);
	result = midiOutLongMsg(data->midi_out_device, &data->hdr, sizeof(data->hdr));
	if (result != MMSYSERR_NOERROR)
	{
		SetEvent(data->event);
		return;
	}
}

int plat_midi_write(midi_device_t* device, uint8_t val)
{
	return 0;
}

void plat_midi_add_status_info(char *s, int max_len, struct midi_device_t* device)
{
        char temps[512];
        sprintf(temps, "MIDI out device: %s\n\n", ((plat_midi_t*)device->data)->name);
        strncat(s, temps, max_len);
}
