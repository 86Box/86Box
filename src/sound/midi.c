#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../device.h"
#include "../ibm.h"
#include "../plat.h"
#include "../plat_midi.h"
#include "midi.h"
#include "midi_system.h"
#ifdef USE_FLUIDSYNTH
# include "midi_fluidsynth.h"
#endif
#ifdef USE_MUNT
# include "midi_mt32.h"
#endif


int midi_device_current = 0;
static int midi_device_last = 0;


typedef struct
{
        const char *name;
        const char *internal_name;
        device_t *device;
} MIDI_DEVICE;

static MIDI_DEVICE devices[] =
{
        {"None",                        "none",                         NULL},
#ifdef USE_FLUIDSYNTH
        {"FluidSynth",                  "fluidsynth",                   &fluidsynth_device},
#endif
#ifdef USE_MUNT
        {"Roland MT-32 Emulation",      "mt32",                         &mt32_device},
        {"Roland CM-32L Emulation",     "cm32l",                        &cm32l_device},
#endif
        {SYSTEM_MIDI_NAME,              SYSTEM_MIDI_INTERNAL_NAME,      &system_midi_device},
        {"", "", NULL}
};

static midi_device_t* m_device = NULL;

int midi_device_available(int card)
{
        if (devices[card].device)
                return device_available(devices[card].device);

        return 1;
}

char *midi_device_getname(int card)
{
        return (char *) devices[card].name;
}

device_t *midi_device_getdevice(int card)
{
        return devices[card].device;
}

int midi_device_has_config(int card)
{
        if (!devices[card].device)
                return 0;
        return devices[card].device->config ? 1 : 0;
}

char *midi_device_get_internal_name(int card)
{
        return (char *) devices[card].internal_name;
}

int midi_device_get_from_internal_name(char *s)
{
	int c = 0;
	
	while (strlen(devices[c].internal_name))
	{
		if (!strcmp(devices[c].internal_name, s))
			return c;
		c++;
	}
	
	return 0;
}

void midi_device_init()
{
        if (devices[midi_device_current].device)
                device_add(devices[midi_device_current].device);
        midi_device_last = midi_device_current;
}

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

static unsigned int midi_pos;
static uint8_t midi_sysex_data[1024+2];

void midi_init(midi_device_t* device)
{
        memset(midi_rt_buf, 0, sizeof(midi_rt_buf));
        memset(midi_cmd_buf, 0, sizeof(midi_cmd_buf));

        midi_cmd_pos = midi_cmd_len = 0;
        midi_status = 0;

        midi_sysex_start = midi_sysex_delay = 0;

        m_device = device;

}

void midi_close()
{
        m_device = NULL;
}

void midi_poll()
{
        if (m_device && m_device->poll) m_device->poll();
}

void play_msg(uint8_t *msg)
{
        if (m_device->play_msg) m_device->play_msg(msg);
}


void play_sysex(uint8_t *sysex, unsigned int len)
{
        if (m_device->play_sysex) m_device->play_sysex(sysex, len);
}

#define SYSEX_SIZE 1024
#define RAWBUF 1024

void midi_write(uint8_t val)
{
        if (!m_device) return;

        if (m_device->write && m_device->write(val)) return;

        uint32_t passed_ticks;

        if (midi_sysex_start)
        {
                passed_ticks = get_ticks() - midi_sysex_start;
                if (passed_ticks < midi_sysex_delay)
                {
                        delay_ms(midi_sysex_delay - passed_ticks);
                }
        }

        /* Test for a realtime MIDI message */
        if (val >= 0xf8)
        {
                midi_rt_buf[0] = val;
                play_msg(midi_rt_buf);
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

                        if ((midi_sysex_start) && (midi_pos >= 4) && (midi_pos <= 9) && (midi_sysex_data[1] == 0x41) && (midi_sysex_data[3] == 0x16))
                        {
                                /* pclog("MIDI: Skipping invalid MT-32 SysEx MIDI message\n"); */
                        }
                        else
                        {
                                play_sysex(midi_sysex_data, midi_pos);
                                if (midi_sysex_start)
                                {
                                        if (midi_sysex_data[5] == 0x7f)
                                        {
                                                midi_sysex_delay = 290;         /* All parameters reset */
                                        }
                                        else if ((midi_sysex_data[5] == 0x10) && (midi_sysex_data[6] == 0x00) && (midi_sysex_data[7] == 0x04))
                                        {
                                                midi_sysex_delay = 145;         /* Viking Child */
                                        }
                                        else if ((midi_sysex_data[5] == 0x10) && (midi_sysex_data[6] == 0x00) && (midi_sysex_data[7] == 0x01))
                                        {
                                                midi_sysex_delay = 30;          /* Dark Sun 1 */
                                        }
                                        else
                                                midi_sysex_delay = (unsigned int) (((float) (midi_pos) * 1.25f) * 1000.0f / 3125.0f) + 2;

                                        midi_sysex_start = get_ticks();
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
                        play_msg(midi_cmd_buf);
                        midi_cmd_pos = 1;
                }
        }
}
