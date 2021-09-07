#include <alsa/asoundlib.h>
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/midi.h>
#include <86box/plat.h>
#include <86box/plat_midi.h>

#define MAX_MIDI_DEVICES 128

static struct
{
	int card;
	int device;
	int sub;
	char name[50];
} midi_devices[MAX_MIDI_DEVICES], midi_in_devices[MAX_MIDI_DEVICES];

static int midi_device_count = 0, midi_in_device_count = 0;

static int midi_queried = 0, midi_in_queried = 0;

static snd_rawmidi_t *midiout = NULL, *midiin = NULL;

static void plat_midi_query()
{
	int status;
	int card = -1;

	midi_queried = 1;

	if ((status = snd_card_next(&card)) < 0)
		return;

	if (card < 0)
		return; /*No cards*/

	while (card >= 0)
	{
		char *shortname;

		if ((status = snd_card_get_name(card, &shortname)) >= 0)
		{
			snd_ctl_t *ctl;
			char name[32];

			sprintf(name, "hw:%i", card);

			if ((status = snd_ctl_open(&ctl, name, 0)) >= 0)
			{
				int device = -1;

				do
				{
					status = snd_ctl_rawmidi_next_device(ctl, &device);
					if (status >= 0 && device != -1)
					{
						snd_rawmidi_info_t *info;
						int sub_nr, sub;

						snd_rawmidi_info_alloca(&info);
						snd_rawmidi_info_set_device(info, device);
						snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_OUTPUT);
						snd_ctl_rawmidi_info(ctl, info);
						sub_nr = snd_rawmidi_info_get_subdevices_count(info);
						//pclog("sub_nr=%i\n",sub_nr);

						for (sub = 0; sub < sub_nr; sub++)
						{
							snd_rawmidi_info_set_subdevice(info, sub);

							if (snd_ctl_rawmidi_info(ctl, info) == 0)
							{
								//pclog("%s: MIDI device=%i:%i:%i\n", shortname, card, device,sub);

								midi_devices[midi_device_count].card = card;
								midi_devices[midi_device_count].device = device;
								midi_devices[midi_device_count].sub = sub;
								snprintf(midi_devices[midi_device_count].name, 50, "%s (%i:%i:%i)", shortname, card, device, sub);
								midi_device_count++;
								if (midi_device_count >= MAX_MIDI_DEVICES)
									return;
							}
						}
					}
				} while (device >= 0);
			}
		}

		if (snd_card_next(&card) < 0)
			break;
	}
}

void plat_midi_init()
{
	char portname[32];
	int midi_id;

	if (!midi_queried)
		plat_midi_query();

	midi_id = config_get_int(SYSTEM_MIDI_NAME, "midi", 0);

	sprintf(portname, "hw:%i,%i,%i", midi_devices[midi_id].card,
					 midi_devices[midi_id].device,
					 midi_devices[midi_id].sub);
	//pclog("Opening MIDI port %s\n", portname);

	if (snd_rawmidi_open(NULL, &midiout, portname, SND_RAWMIDI_SYNC) < 0)
	{
		//pclog("Failed to open MIDI\n");
		return;
	}
}

void plat_midi_close()
{
	if (midiout != NULL)
	{
		snd_rawmidi_close(midiout);
		midiout = NULL;
	}
}

static int midi_pos, midi_len;
static uint8_t midi_command[4];
static int midi_lengths[8] = {3, 3, 3, 3, 2, 2, 3, 1};
static int midi_insysex;
static uint8_t midi_sysex_data[65536];

int plat_midi_write(uint8_t val)
{
	return 0;
}

void plat_midi_play_sysex(uint8_t *sysex, unsigned int len)
{
	if (midiout)
	{
		snd_rawmidi_write(midiout, (const void*)sysex, (size_t)len);
	}
}

void plat_midi_play_msg(uint8_t *msg)
{
	plat_midi_play_sysex(msg, midi_lengths[(msg[0] >> 4) & 7]);
}

int plat_midi_get_num_devs()
{
	if (!midi_queried)
		plat_midi_query();

	return midi_device_count;
}

void plat_midi_get_dev_name(int num, char *s)
{
	strcpy(s, midi_devices[num].name);
}

static void plat_midi_query_in()
{
	int status;
	int card = -1;

	midi_in_queried = 1;

	if ((status = snd_card_next(&card)) < 0)
		return;

	if (card < 0)
		return; /*No cards*/

	while (card >= 0)
	{
		char *shortname;

		if ((status = snd_card_get_name(card, &shortname)) >= 0)
		{
			snd_ctl_t *ctl;
			char name[32];

			sprintf(name, "hw:%i", card);

			if ((status = snd_ctl_open(&ctl, name, 0)) >= 0)
			{
				int device = -1;

				do
				{
					status = snd_ctl_rawmidi_next_device(ctl, &device);
					if (status >= 0 && device != -1)
					{
						snd_rawmidi_info_t *info;
						int sub_nr, sub;

						snd_rawmidi_info_alloca(&info);
						snd_rawmidi_info_set_device(info, device);
						snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);
						snd_ctl_rawmidi_info(ctl, info);
						sub_nr = snd_rawmidi_info_get_subdevices_count(info);
						//pclog("sub_nr=%i\n",sub_nr);

						for (sub = 0; sub < sub_nr; sub++)
						{
							snd_rawmidi_info_set_subdevice(info, sub);

							if (snd_ctl_rawmidi_info(ctl, info) == 0)
							{
								//pclog("%s: MIDI device=%i:%i:%i\n", shortname, card, device,sub);

								midi_in_devices[midi_device_count].card = card;
								midi_in_devices[midi_device_count].device = device;
								midi_in_devices[midi_device_count].sub = sub;
								snprintf(midi_in_devices[midi_device_count].name, 50, "%s (%i:%i:%i)", shortname, card, device, sub);
								midi_in_device_count++;
								if (midi_in_device_count >= MAX_MIDI_DEVICES)
									return;
							}
						}
					}
				} while (device >= 0);
			}
		}

		if (snd_card_next(&card) < 0)
			break;
	}
}
mutex_t* midiinmtx = NULL;

static void plat_midi_in_thread(void* param)
{
	int sysexpos = 0;
	uint8_t midimsg[3];
	while(1)
	{
		thread_wait_mutex(midiinmtx);
		if (midiin == NULL)
		{
			thread_release_mutex(midiinmtx);
			break;
		}
		if (snd_rawmidi_read(midiin, midimsg, 1) > 0)
		{
			if (midimsg[0] == 0xF0)
			{
				MIDI_InSysexBuf[sysexpos++] = 0xF0;
				while(1)
				{
					snd_rawmidi_read(midiin, &MIDI_InSysexBuf[sysexpos++], 1);
					if (MIDI_InSysexBuf[sysexpos - 1] == 0xF7)
					{
						midi_in_sysex(MIDI_InSysexBuf, sysexpos);
					}
				}
			}
			else if (midimsg[0] & 0x80)
			{
				int lengthofmsg = midi_lengths[(midimsg[0] >> 4) & 7] - 1;
				snd_rawmidi_read(midiin, midimsg + 1, lengthofmsg);
				midi_in_msg(midimsg);
			}
		}
		thread_release_mutex(midiinmtx);
	}
}

void plat_midi_input_init(void)
{
	char portname[32];
	int midi_id;
	snd_rawmidi_params_t* params;
	int err;

	snd_rawmidi_params_malloc(&params);
	if (!params) return;
	if (!midi_in_queried)
		plat_midi_query_in();

	
	midi_id = config_get_int(MIDI_INPUT_NAME, "midi_input", 0);

	sprintf(portname, "hw:%i,%i,%i", midi_in_devices[midi_id].card,
					 midi_in_devices[midi_id].device,
					 midi_in_devices[midi_id].sub);
	//pclog("Opening MIDI port %s\n", portname);

	if (snd_rawmidi_open(NULL, &midiin, portname, SND_RAWMIDI_NONBLOCK) < 0)
	{
		//pclog("Failed to open MIDI\n");
		return;
	}
	midiin = thread_create_mutex();
	thread_create(plat_midi_in_thread, NULL);
}

void plat_midi_input_close(void)
{
	if (midiinmtx) thread_wait_mutex(midiinmtx);
	if (midiin != NULL)
	{
		snd_rawmidi_close(midiin);
		midiin = NULL;
	}
	if (midiinmtx)
	{
		thread_release_mutex(midiinmtx);
		thread_close_mutex(midiinmtx);
	}
	midiinmtx = NULL;
}

int plat_midi_in_get_num_devs(void)
{
    if (!midi_queried)
		plat_midi_query_in();
	
	return midi_in_device_count;
}

void plat_midi_in_get_dev_name(int num, char *s)
{
    strcpy(s, midi_in_devices[num].name);
}
