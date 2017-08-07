/* some code borrowed from scummvm */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fluidsynth.h>
#include "../config.h"
#include "../WIN/plat_thread.h"
#include "../device.h"
#include "midi_fluidsynth.h"
#include "midi.h"
#include "sound.h"

#define RENDER_RATE 30

extern void givealbuffer_midi(void *buf, uint32_t size);
extern void pclog(const char *format, ...);
extern void al_set_midi(int freq, int buf_size);
extern int soundon;

typedef struct fluidsynth_t
{
        fluid_settings_t* settings;
        fluid_synth_t* synth;
        int samplerate;
        int sound_font;

        thread_t* thread_h;
        event_t* event;
        int buf_size;
        float* buffer;
        int16_t* buffer_int16;
        int midi_pos;

} fluidsynth_t;

fluidsynth_t fsdev;

int fluidsynth_available()
{
        return 1;
}

void fluidsynth_poll()
{
        fluidsynth_t* data = &fsdev;
        data->midi_pos++;
        if (data->midi_pos == 48000/RENDER_RATE)
        {
                data->midi_pos = 0;
                thread_set_event(data->event);
        }
}

static void fluidsynth_thread(void *param)
{
        fluidsynth_t* data = (fluidsynth_t*)param;
        while (1)
        {
                thread_wait_event(data->event, -1);
		if (sound_is_float)
		{
	                memset(data->buffer, 0, data->buf_size * sizeof(float));
	                if (data->synth)
        	                fluid_synth_write_float(data->synth, data->buf_size/2, data->buffer, 0, 2, data->buffer, 1, 2);
	                if (soundon)
        	                givealbuffer_midi(data->buffer, data->buf_size);
		}
		else
		{
	                memset(data->buffer, 0, data->buf_size * sizeof(int16_t));
	                if (data->synth)
        	                fluid_synth_write_s16(data->synth, data->buf_size/2, data->buffer_int16, 0, 2, data->buffer_int16, 1, 2);
	                if (soundon)
        	                givealbuffer_midi(data->buffer_int16, data->buf_size);
		}
        }
}

void fluidsynth_msg(uint8_t *msg)
{
        fluidsynth_t* data = &fsdev;

	uint32_t val = *((uint32_t*)msg);

        uint32_t param2 = (uint8_t) ((val >> 16) & 0xFF);
        uint32_t param1 = (uint8_t) ((val >>  8) & 0xFF);
        uint8_t cmd    = (uint8_t) (val & 0xF0);
        uint8_t chan   = (uint8_t) (val & 0x0F);

        switch (cmd) {
        case 0x80:      /* Note Off */
                fluid_synth_noteoff(data->synth, chan, param1);
                break;
        case 0x90:      /* Note On */
                fluid_synth_noteon(data->synth, chan, param1, param2);
                break;
        case 0xA0:      /* Aftertouch */
                break;
        case 0xB0:      /* Control Change */
                fluid_synth_cc(data->synth, chan, param1, param2);
                break;
        case 0xC0:      /* Program Change */
                fluid_synth_program_change(data->synth, chan, param1);
                break;
        case 0xD0:      /* Channel Pressure */
                break;
        case 0xE0:      /* Pitch Bend */
                fluid_synth_pitch_bend(data->synth, chan, (param2 << 7) | param1);
                break;
        case 0xF0:      /* SysEx */
                break;
        default:
                pclog("fluidsynth: unknown send() command 0x%02X", cmd);
                break;
        }
}

void fluidsynth_sysex(uint8_t* data, unsigned int len)
{
        fluidsynth_t* d = &fsdev;

        fluid_synth_sysex(d->synth, (const char *) data, len, 0, 0, 0, 0);
}

void* fluidsynth_init()
{
        fluidsynth_t* data = &fsdev;
        memset(data, 0, sizeof(fluidsynth_t));

        data->settings = new_fluid_settings();

        fluid_settings_setnum(data->settings, "synth.sample-rate", 44100);
        fluid_settings_setnum(data->settings, "synth.gain", device_get_config_int("output_gain")/100.0f);

        data->synth = new_fluid_synth(data->settings);

        char* sound_font = device_get_config_string("sound_font");
        data->sound_font = fluid_synth_sfload(data->synth, sound_font, 1);

        if (device_get_config_int("chorus"))
        {
                fluid_synth_set_chorus_on(data->synth, 1);

                int chorus_voices = device_get_config_int("chorus_voices");
                double chorus_level = device_get_config_int("chorus_level") / 100.0;
                double chorus_speed = device_get_config_int("chorus_speed") / 100.0;
                double chorus_depth = device_get_config_int("chorus_depth") / 10.0;

                int chorus_waveform = FLUID_CHORUS_MOD_SINE;
                if (device_get_config_int("chorus_waveform") == 0)
                        chorus_waveform = FLUID_CHORUS_MOD_SINE;
                else
                        chorus_waveform = FLUID_CHORUS_MOD_TRIANGLE;

                fluid_synth_set_chorus(data->synth, chorus_voices, chorus_level, chorus_speed, chorus_depth, chorus_waveform);
        }
        else
                fluid_synth_set_chorus_on(data->synth, 0);

        if (device_get_config_int("reverb"))
        {
                fluid_synth_set_reverb_on(data->synth, 1);

                double reverb_room_size = device_get_config_int("reverb_room_size") / 100.0;
                double reverb_damping = device_get_config_int("reverb_damping") / 100.0;
                int reverb_width = device_get_config_int("reverb_width");
                double reverb_level = device_get_config_int("reverb_level") / 100.0;

                fluid_synth_set_reverb(data->synth, reverb_room_size, reverb_damping, reverb_width, reverb_level);
        }
        else
                fluid_synth_set_reverb_on(data->synth, 0);

        int interpolation = device_get_config_int("interpolation");
        int fs_interpolation = FLUID_INTERP_4THORDER;

        if (interpolation == 0)
                fs_interpolation = FLUID_INTERP_NONE;
        else if (interpolation == 1)
                fs_interpolation = FLUID_INTERP_LINEAR;
        else if (interpolation == 2)
                fs_interpolation = FLUID_INTERP_4THORDER;
        else if (interpolation == 3)
                fs_interpolation = FLUID_INTERP_7THORDER;

        fluid_synth_set_interp_method(data->synth, -1, fs_interpolation);

        double samplerate;
        fluid_settings_getnum(data->settings, "synth.sample-rate", &samplerate);
        data->samplerate = (int)samplerate;
        data->buf_size = data->samplerate/RENDER_RATE*2;
	if (sound_is_float)
	{
	        data->buffer = malloc(data->buf_size * sizeof(float));
		data->buffer_int16 = NULL;
	}
	else
	{
	        data->buffer = NULL;
		data->buffer_int16 = malloc(data->buf_size * sizeof(int16_t));
	}
        data->event = thread_create_event();
        data->thread_h = thread_create(fluidsynth_thread, data);

        al_set_midi(data->samplerate, data->buf_size);

        pclog("fluidsynth (%s) initialized, samplerate %d, buf_size %d\n", fluid_version_str(), data->samplerate, data->buf_size);

        midi_device_t* dev = malloc(sizeof(midi_device_t));
        memset(dev, 0, sizeof(midi_device_t));

        dev->play_msg = fluidsynth_msg;
        dev->play_sysex = fluidsynth_sysex;
        dev->poll = fluidsynth_poll;

        midi_init(dev);

        return dev;
}

void fluidsynth_close(void* p)
{
        if (!p) return;

        fluidsynth_t* data = &fsdev;

        if (data->sound_font != -1)
                fluid_synth_sfunload(data->synth, data->sound_font, 1);
        delete_fluid_synth(data->synth);
        delete_fluid_settings(data->settings);

        midi_close();

	if (data->buffer)
	{
	        free(data->buffer);
	}

	if (data->buffer_int16)
	{
	        free(data->buffer_int16);
	}

        pclog("fluidsynth closed\n");
}

static device_config_t fluidsynth_config[] =
{
        {
                .name = "sound_font",
                .description = "Sound Font",
                .type = CONFIG_FILE,
                .default_string = "",
                .file_filter =
                {
                        {
                                .description = "SF2 Sound Fonts",
                                .extensions =
                                {
                                        "sf2"
                                }
                        }
                }
        },
        {
                .name = "output_gain",
                .description = "Output Gain",
                .type = CONFIG_SPINNER,
                .spinner =
                {
                        .min = 0,
                        .max = 100
                },
                .default_int = 100
        },
        {
                .name = "chorus",
                .description = "Chorus",
                .type = CONFIG_BINARY,
                .default_int = 0
        },
        {
                .name = "chorus_voices",
                .description = "Chorus Voices",
                .type = CONFIG_SPINNER,
                .spinner =
                {
                        .min = 0,
                        .max = 99
                },
                .default_int = 3
        },
        {
                .name = "chorus_level",
                .description = "Chorus Level",
                .type = CONFIG_SPINNER,
                .spinner =
                {
                        .min = 0,
                        .max = 100
                },
                .default_int = 100
        },
        {
                .name = "chorus_speed",
                .description = "Chorus Speed",
                .type = CONFIG_SPINNER,
                .spinner =
                {
                        .min = 30,
                        .max = 500
                },
                .default_int = 30
        },
        {
                .name = "chorus_depth",
                .description = "Chorus Depth",
                .type = CONFIG_SPINNER,
                .spinner =
                {
                        .min = 0,
                        .max = 210
                },
                .default_int = 80
        },
        {
                .name = "chorus_waveform",
                .description = "Chorus Waveform",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                               .description = "Sine",
                                .value = 0
                        },
                        {
                                .description = "Triangle",
                                .value = 1
                        }
                },
                .default_int = 0
        },
        {
                .name = "reverb",
                .description = "Reverb",
                .type = CONFIG_BINARY,
                .default_int = 0
        },
        {
                .name = "reverb_room_size",
                .description = "Reverb Room Size",
                .type = CONFIG_SPINNER,
                .spinner =
                {
                        .min = 0,
                        .max = 120
                },
                .default_int = 20
        },
        {
                .name = "reverb_damping",
                .description = "Reverb Damping",
                .type = CONFIG_SPINNER,
                .spinner =
                {
                        .min = 0,
                        .max = 100
                },
                .default_int = 0
        },
        {
                .name = "reverb_width",
                .description = "Reverb Width",
                .type = CONFIG_SPINNER,
                .spinner =
                {
                        .min = 0,
                        .max = 100
                },
                .default_int = 1
        },
        {
                .name = "reverb_level",
                .description = "Reverb Level",
                .type = CONFIG_SPINNER,
                .spinner =
                {
                        .min = 0,
                        .max = 100
                },
                .default_int = 90
        },
        {
                .name = "interpolation",
                .description = "Interpolation Method",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "None",
                                .value = 0
                        },
                        {
                                .description = "Linear",
                                .value = 1
                        },
                        {
                                .description = "4th Order",
                                .value = 2
                        },
                        {
                                .description = "7th Order",
                                .value = 3
                        }
                },
                .default_int = 2
        },
        {
                .type = -1
        }
};

device_t fluidsynth_device =
{
        "FluidSynth",
        0,
        fluidsynth_init,
        fluidsynth_close,
        fluidsynth_available,
        NULL,
        NULL,
        NULL,
        fluidsynth_config
};
