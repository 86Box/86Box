#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../device.h"
#include "../WIN/plat_midi.h"
#include "midi_system.h"
#include "midi.h"

void* system_midi_init()
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

int system_midi_available()
{
        return plat_midi_get_num_devs();
}

static device_config_t system_midi_config[] =
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

device_t system_midi_device =
{
        SYSTEM_MIDI_NAME,
        0,
        system_midi_init,
        system_midi_close,
        system_midi_available,
        NULL,
        NULL,
        NULL,
        system_midi_config
};
