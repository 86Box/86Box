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

void system_midi_close(void* p)
{
        plat_midi_close();

        midi_close();
}

int system_midi_available(void)
{
        return plat_midi_get_num_devs();
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

const device_t system_midi_device =
{
        SYSTEM_MIDI_NAME,
        0, 0,
        system_midi_init,
        system_midi_close,
	NULL,
        system_midi_available,
        NULL,
        NULL,
        system_midi_config
};
