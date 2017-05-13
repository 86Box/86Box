#include "../ibm.h"
#include "sound.h"
#include "snd_speaker.h"


int speaker_mute = 0;
int speaker_gated = 0;
int speaker_enable = 0, was_speaker_enable = 0;


static int16_t speaker_buffer[SOUNDBUFLEN];
static int speaker_pos = 0;


void speaker_update(void)
{
        int16_t val;
        
        for (; speaker_pos < sound_pos_global; speaker_pos++)
        {
                if (speaker_gated && was_speaker_enable)
                {
                        if (!pit.m[2] || pit.m[2]==4)
                                val = speakval;
                        else if (pit.l[2] < 0x40)
                                val = 0xa00;
                        else 
                                val = speakon ? 0x1400 : 0;
                }
                else
                        val = was_speaker_enable ? 0x1400 : 0;

                if (!speaker_enable)
                        was_speaker_enable = 0;

                speaker_buffer[speaker_pos] = val;
        }
}

static void speaker_get_buffer(int32_t *buffer, int len, void *p)
{
        int c;

        speaker_update();
        
        if (!speaker_mute)
        {
                for (c = 0; c < len * 2; c++)
                        buffer[c] += speaker_buffer[c >> 1];
        }

        speaker_pos = 0;
}


void speaker_init(void)
{
        sound_add_handler(speaker_get_buffer, NULL);
        speaker_mute = 0;
}
