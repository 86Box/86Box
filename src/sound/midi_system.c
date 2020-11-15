#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/plat_midi.h>
#include <86box/midi.h>
#include <86box/midi_input.h>


void* system_midi_init(const device_t *info)
{
        midi_device_t* dev = malloc(sizeof(midi_device_t));
        memset(dev, 0, sizeof(midi_device_t));

        dev->play_msg = plat_midi_play_msg;
        dev->play_sysex = plat_midi_play_sysex;
        dev->write = plat_midi_write;

        plat_midi_init();

        midi_init(dev);

        return dev;
}

void* midi_input_init(const device_t *info)
{
        midi_device_t* dev = malloc(sizeof(midi_device_t));
        memset(dev, 0, sizeof(midi_device_t));

        plat_midi_input_init();

        midi_in_init(dev, &midi_in);

		midi_in->midi_realtime = device_get_config_int("realtime");
		midi_in->thruchan = device_get_config_int("thruchan");
		midi_in->midi_clockout = device_get_config_int("clockout");

        return dev;
}

void system_midi_close(void* p)
{
        plat_midi_close();

        midi_close();
}

void midi_input_close(void* p)
{
        plat_midi_input_close();

        midi_close();
}

int system_midi_available(void)
{
        return plat_midi_get_num_devs();
}

int midi_input_available(void)
{
        return plat_midi_in_get_num_devs();
}

static const device_config_t system_midi_config[] =
{
        {
                .name = "midi",
                .description = "MIDI out device",
                .type = CONFIG_MIDI,
                .default_int = 0
        },
        {
                .type = -1
        }
};

static const device_config_t midi_input_config[] =
{
        {
                .name = "midi_input", 
				.description = "MIDI in device", 
				.type = CONFIG_MIDI_IN, 
				.default_int = 0
        },
        {
                .name = "realtime", 
				.description = "MIDI Real time", 
				.type = CONFIG_BINARY, 
				.default_int = 0
        },
        {
                .name = "thruchan", 
				.description = "MIDI Thru", 
				.type = CONFIG_BINARY, 
				.default_int = 1
        },
        {
                .name = "clockout", 
				.description = "MIDI Clockout", 
				.type = CONFIG_BINARY, 
				.default_int = 1
        },
        {
                .type = -1
        }
};

const device_t system_midi_device =
{
        SYSTEM_MIDI_NAME,
        0, 0,
        system_midi_init,
        system_midi_close,
	NULL,
        { system_midi_available },
        NULL,
        NULL,
        system_midi_config
};


const device_t midi_input_device =
{
        MIDI_INPUT_NAME,
        0, 0,
        midi_input_init,
        midi_input_close,
	NULL,
        { midi_input_available },
        NULL,
        NULL,
        midi_input_config
};