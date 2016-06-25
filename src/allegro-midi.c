#include "allegro-main.h"
#include "ibm.h"
#include "plat-midi.h"

//#define USE_ALLEGRO_MIDI

void midi_init()
{
#ifdef USE_ALLEGRO_MIDI
	install_sound(DIGI_NONE, MIDI_AUTODETECT, NULL);
#endif
}

void midi_close()
{
#ifdef USE_ALLEGRO_MIDI
	remove_sound();
#endif
}

static int midi_cmd_pos, midi_len;
static uint8_t midi_command[3];
static int midi_lengths[8] = {3, 3, 3, 3, 2, 2, 3, 0};

void midi_write(uint8_t val)
{
        if (val & 0x80)
        {
                midi_cmd_pos = 0;
                midi_len = midi_lengths[(val >> 4) & 7];
                midi_command[0] = midi_command[1] = midi_command[2] = 0;
        }

        if (midi_len && midi_cmd_pos < 3)
        {                
                midi_command[midi_cmd_pos] = val;
                
                midi_cmd_pos++;
                
#ifdef USE_ALLEGRO_MIDI
                if (midi_cmd_pos == midi_len)
                        midi_out(midi_command, midi_len);
#endif
        }
}
