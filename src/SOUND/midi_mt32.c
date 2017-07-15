#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "munt/c_interface/c_interface.h"
#include "../WIN/plat_thread.h"
#include "../WIN/plat_ticks.h"
#include "../ibm.h"
#include "../device.h"
#include "../mem.h"
#include "../rom.h"
#include "midi_mt32.h"
#include "midi.h"
#include "sound.h"

#define RENDER_RATE 30
#define MESSAGE_HIDE 10000

extern void givealbuffer_midi(void *buf, uint32_t size);
extern void pclog(const char *format, ...);
extern void al_set_midi(int freq, int buf_size);
extern int soundon;

typedef struct mt32_t
{
        mt32emu_context context;
        char message[MT32EMU_SYSEX_BUFFER_SIZE];
        unsigned int message_shown;
        thread_t* thread_h;
        event_t* event;

        uint32_t samplerate;
        int buf_size;
        float* buffer;
        int16_t* buffer_int16;
        int midi_pos;

        int status_show_instruments;

        char model_name[50];
} mt32_t;

void showLCDMessage(void *instance_data, const char *message)
{
        mt32_t* data = (mt32_t*)instance_data;
        strncpy(data->message, message, 999);
        data->message[999] = 0;
        data->message_shown = get_ticks();
}

static const mt32emu_report_handler_i_v0 handler_v0 = {
        /** Returns the actual interface version ID */
        NULL, //mt32emu_report_handler_version (*getVersionID)(mt32emu_report_handler_i i);

        /** Callback for debug messages, in vprintf() format */
        NULL, //void (*printDebug)(void *instance_data, const char *fmt, va_list list);
        /** Callbacks for reporting errors */
        NULL, //void (*onErrorControlROM)(void *instance_data);
        NULL, //void (*onErrorPCMROM)(void *instance_data);
        /** Callback for reporting about displaying a new custom message on LCD */
        showLCDMessage, //void (*showLCDMessage)(void *instance_data, const char *message);
        /** Callback for reporting actual processing of a MIDI message */
        NULL, //void (*onMIDIMessagePlayed)(void *instance_data);
        /**
         * Callback for reporting an overflow of the input MIDI queue.
         * Returns MT32EMU_BOOL_TRUE if a recovery action was taken
         * and yet another attempt to enqueue the MIDI event is desired.
         */
        NULL, //mt32emu_boolean (*onMIDIQueueOverflow)(void *instance_data);
        /**
         * Callback invoked when a System Realtime MIDI message is detected in functions
         * mt32emu_parse_stream and mt32emu_play_short_message and the likes.
         */
        NULL, //void (*onMIDISystemRealtime)(void *instance_data, mt32emu_bit8u system_realtime);
        /** Callbacks for reporting system events */
        NULL, //void (*onDeviceReset)(void *instance_data);
        NULL, //void (*onDeviceReconfig)(void *instance_data);
        /** Callbacks for reporting changes of reverb settings */
        NULL, //void (*onNewReverbMode)(void *instance_data, mt32emu_bit8u mode);
        NULL, //void (*onNewReverbTime)(void *instance_data, mt32emu_bit8u time);
        NULL, //void (*onNewReverbLevel)(void *instance_data, mt32emu_bit8u level);
        /** Callbacks for reporting various information */
        NULL, //void (*onPolyStateChanged)(void *instance_data, mt32emu_bit8u part_num);
        NULL, //void (*onProgramChanged)(void *instance_data, mt32emu_bit8u part_num, const char *sound_group_name, const char *patch_name);
};

static const mt32emu_report_handler_i handler = { &handler_v0 };

static int roms_present[] = { -1, -1 };

mt32emu_return_code mt32_check(const char* func, mt32emu_return_code ret, mt32emu_return_code expected)
{
        if (ret != expected)
        {
                pclog("%s() failed, expected %d but returned %d\n", func, expected, ret);
                return 0;
        }
        return 1;
}

int mt32_available()
{
        if (roms_present[0] < 0)
                roms_present[0] = (rom_present(L"roms/mt32/mt32_control.rom") && rom_present(L"roms/mt32/mt32_pcm.rom"));
        return roms_present[0];
}

int cm32l_available()
{
        if (roms_present[1] < 0)
                roms_present[1] = (rom_present(L"roms/cm32l/cm32l_control.rom") && rom_present(L"roms/cm32l/cm32l_pcm.rom"));
        return roms_present[1];
}

void mt32_stream(mt32emu_context context, float* stream, int len)
{
        if (context) mt32emu_render_float(context, stream, len);
}

void mt32_stream_int16(mt32emu_context context, int16_t* stream, int len)
{
        if (context) mt32emu_render_bit16s(context, stream, len);
}

void mt32_poll(midi_device_t *device)
{
	mt32_t *data = (mt32_t *) device->data;
        data->midi_pos++;
        if (data->midi_pos == 48000/RENDER_RATE)
        {
                data->midi_pos = 0;
                thread_set_event(data->event);
        }
	if (get_ticks() > data->message_shown+MESSAGE_HIDE)
		data->message[0] = 0;
}

static void mt32_thread(void *param)
{
	mt32_t *data = (mt32_t *) param;
        while (1)
        {
                thread_wait_event(data->event, -1);
		if (sound_is_float)
		{
	                memset(data->buffer, 0, data->buf_size);
	                mt32_stream(data->context, data->buffer, data->buf_size / (sizeof(float) << 1));
	                if (soundon)
				givealbuffer_midi(data->buffer, data->buf_size);
		}
		else
		{
	                memset(data->buffer_int16, 0, data->buf_size);
	                mt32_stream_int16(data->context, data->buffer_int16, data->buf_size / (sizeof(int16_t) << 1));
	                if (soundon)
				givealbuffer_midi(data->buffer_int16, data->buf_size);
		}
        }
}

void mt32_msg(midi_device_t *device, uint8_t* val)
{
	mt32emu_context context = ((mt32_t *)device->data)->context;
        if (context) mt32_check("mt32emu_play_msg", mt32emu_play_msg(context, *(uint32_t*)val), MT32EMU_RC_OK);
}

void mt32_sysex(midi_device_t *device, uint8_t* data, unsigned int len)
{
	mt32emu_context context = ((mt32_t *)device->data)->context;
        if (context) mt32_check("mt32emu_play_sysex", mt32emu_play_sysex(context, data, len), MT32EMU_RC_OK);
}

static mt32emu_return_code mt32emu_add_rom_file_ex(mt32emu_context context, wchar_t *s)
{
	char fn[512];
	wcstombs(fn, s, (wcslen(s) << 1) + 2);
	return mt32emu_add_rom_file(context, fn);
}

static void* mt32emu_init(wchar_t* control_rom, wchar_t* pcm_rom)
{
        wchar_t s[512];

        mt32_t* data = malloc(sizeof(mt32_t));
        memset(data, 0, sizeof(mt32_t));
        mt32emu_context context = mt32emu_create_context(handler, data);

	if (
	        !rom_getfile(control_rom, s, 512) ||
	        !mt32_check("mt32emu_add_rom_file", mt32emu_add_rom_file_ex(context, s), MT32EMU_RC_ADDED_CONTROL_ROM) ||
	        !rom_getfile(pcm_rom, s, 512) ||
		!mt32_check("mt32emu_add_rom_file", mt32emu_add_rom_file_ex(context, s), MT32EMU_RC_ADDED_PCM_ROM) ||
		!mt32_check("mt32emu_open_synth", mt32emu_open_synth(context), MT32EMU_RC_OK))
	{
		free(data);
		return 0;
	}

        data->samplerate = mt32emu_get_actual_stereo_output_samplerate(context);
	if (sound_is_float)
	{
	        data->buf_size = data->samplerate/RENDER_RATE*(sizeof(float) << 1);
	        data->buffer = malloc(data->buf_size);
		data->buffer_int16 = NULL;
	}
	else
	{
	        data->buf_size = data->samplerate/RENDER_RATE*(sizeof(int16_t) << 1);
	        data->buffer = NULL;
		data->buffer_int16 = malloc(data->buf_size);
	}
        data->event = thread_create_event();
        data->thread_h = thread_create(mt32_thread, data);
        data->status_show_instruments = device_get_config_int("status_show_instruments");

        mt32emu_set_output_gain(context, device_get_config_int("output_gain")/100.0f);
        mt32emu_set_reverb_enabled(context, device_get_config_int("reverb"));
        mt32emu_set_reverb_output_gain(context, device_get_config_int("reverb_output_gain")/100.0f);
        mt32emu_set_reversed_stereo_enabled(context, device_get_config_int("reversed_stereo"));

        pclog("mt32 output gain: %f\n", mt32emu_get_output_gain(context));
        pclog("mt32 reverb output gain: %f\n", mt32emu_get_reverb_output_gain(context));
        pclog("mt32 reverb: %d\n", mt32emu_is_reverb_enabled(context));
        pclog("mt32 reversed stereo: %d\n", mt32emu_is_reversed_stereo_enabled(context));

        al_set_midi(data->samplerate, data->buf_size);

        pclog("mt32 (Munt %s) initialized, samplerate %d, buf_size %d\n", mt32emu_get_library_version_string(), data->samplerate, data->buf_size);

	data->context = context;
	data->message[0] = 0;

        midi_device_t* dev = malloc(sizeof(midi_device_t));
        memset(dev, 0, sizeof(midi_device_t));

        dev->play_msg = mt32_msg;
        dev->play_sysex = mt32_sysex;
        dev->poll = mt32_poll;
        dev->data = data;

        midi_init(dev);

        return dev;
}

void* mt32_init()
{
        midi_device_t* dev = mt32emu_init(L"roms/mt32/mt32_control.rom", L"roms/mt32/mt32_pcm.rom");
        if (dev)
                strcpy(((mt32_t*)dev->data)->model_name, "MT-32");
        return dev;
}

void* cm32l_init()
{
        midi_device_t* dev = mt32emu_init(L"roms/cm32l/cm32l_control.rom", L"roms/cm32l/cm32l_pcm.rom");
        if (dev)
                strcpy(((mt32_t*)dev->data)->model_name, "CM-32L");
        return dev;
}

void mt32_close(void* p)
{
        if (!p) return;

        midi_device_t* device = (midi_device_t*)p;

        mt32_t* data = (mt32_t*)device->data;

        if (data->thread_h)
                thread_kill(data->thread_h);
        if (data->event)
                thread_destroy_event(data->event);

        if (data->context)
        {
                mt32emu_close_synth(data->context);
                mt32emu_free_context(data->context);
        }

        if (data->buffer)
                free(data->buffer);

        if (data->buffer_int16)
                free(data->buffer_int16);

        midi_close();

	free(data);
        free((midi_device_t*)p);

        pclog("mt32 closed\n");
}

void mt32_add_status_info(char *s, int max_len, void *p)
{
        int i;
        char temps[MT32EMU_SYSEX_BUFFER_SIZE];
        midi_device_t* dev = (midi_device_t*)p;
        mt32_t* data = (mt32_t*)dev->data;
        mt32emu_context context = data->context;
        if (strlen(data->message))
        {
                sprintf(temps, "%s message: %s\n", data->model_name, data->message);
                strncat(s, temps, max_len);
        }
        if (mt32emu_is_active(context))
        {
                sprintf(temps, "%s playback frequency: %iHz\n", data->model_name, data->samplerate);
                strncat(s, temps, max_len);
                if (data->status_show_instruments)
                {
                        for (i = 0; i < 8; ++i)
                        {
                                const char* patch_name = mt32emu_get_patch_name(context, i);
                                sprintf(temps, "%s inst. %d: %s\n", data->model_name, i+1, patch_name);
                                strncat(s, temps, max_len);
                        }
                }
                strncat(s, "\n", max_len);
        }
        else
        {
                strncat(s, data->model_name, max_len);
                strncat(s, " playback stopped\n\n", max_len);
        }
}

static device_config_t mt32_config[] =
{
        {
                .name = "output_gain",
                .description = "Output Gain",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "100%",
                                .value = 100
                        },
                        {
                                .description = "75%",
                                .value = 75
                        },
                        {
                                .description = "50%",
                                .value = 50
                        },
                        {
                                .description = "25%",
                                .value = 25
                        },
                        {
                                .description = "0%",
                                .value = 0
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 100
        },
        {
                .name = "reverb",
                .description = "Reverb",
                .type = CONFIG_BINARY,
                .default_int = 1
        },
        {
                .name = "reverb_output_gain",
                .description = "Reverb Output Gain",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "100%",
                                .value = 100
                        },
                        {
                                .description = "75%",
                                .value = 75
                        },
                        {
                                .description = "50%",
                                .value = 50
                        },
                        {
                                .description = "25%",
                                .value = 25
                        },
                        {
                                .description = "0%",
                                .value = 0
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 100
        },
        {
                .name = "reversed_stereo",
                .description = "Reversed stereo",
                .type = CONFIG_BINARY,
                .default_int = 0
        },
        {
                .name = "status_show_instruments",
                .description = "(Status) Show instruments",
                .type = CONFIG_BINARY,
                .default_int = 0
        },
        {
                .type = -1
        }
};

device_t mt32_device =
{
        "Roland MT-32 Emulation",
        0,
        mt32_init,
        mt32_close,
        mt32_available,
        NULL,
        NULL,
        mt32_add_status_info,
        mt32_config
};

device_t cm32l_device =
{
        "Roland CM-32L Emulation",
        0,
        cm32l_init,
        mt32_close,
        cm32l_available,
        NULL,
        NULL,
        mt32_add_status_info,
        mt32_config
};
