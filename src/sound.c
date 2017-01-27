#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "cdrom.h"
#include "ibm.h"

#include "device.h"

#include "filters.h"

#include "sound_opl.h"

#include "sound.h"
#include "sound_adlib.h"
#include "sound_adlibgold.h"
#include "sound_pas16.h"
#include "sound_sb.h"
#include "sound_sb_dsp.h"
#include "sound_wss.h"

#include "timer.h"
#include "thread.h"

int sound_card_current = 0;
static int sound_card_last = 0;

typedef struct
{
        char name[32];
        device_t *device;
} SOUND_CARD;

static SOUND_CARD sound_cards[] =
{
        {"None",                  NULL},
        {"Adlib",                 &adlib_device},
        {"Sound Blaster 1.0",     &sb_1_device},
        {"Sound Blaster 1.5",     &sb_15_device},
        {"Sound Blaster 2.0",     &sb_2_device},
        {"Sound Blaster Pro v1",  &sb_pro_v1_device},
        {"Sound Blaster Pro v2",  &sb_pro_v2_device},
        {"Sound Blaster 16",      &sb_16_device},
        {"Sound Blaster AWE32",   &sb_awe32_device},
        {"Adlib Gold",            &adgold_device},
        {"Windows Sound System",  &wss_device},        
        {"Pro Audio Spectrum 16", &pas16_device},
        {"", NULL}
};

int sound_card_available(int card)
{
        if (sound_cards[card].device)
                return device_available(sound_cards[card].device);

        return 1;
}

char *sound_card_getname(int card)
{
        return sound_cards[card].name;
}

device_t *sound_card_getdevice(int card)
{
        return sound_cards[card].device;
}

int sound_card_has_config(int card)
{
        if (!sound_cards[card].device)
                return 0;
        return sound_cards[card].device->config ? 1 : 0;
}

void sound_card_init()
{
        if (sound_cards[sound_card_current].device)
                device_add(sound_cards[sound_card_current].device);
        sound_card_last = sound_card_current;
}

static struct
{
        void (*get_buffer)(int32_t *buffer, int len, void *p);
        void *priv;
} sound_handlers[8];

static int sound_handlers_num;

static int sound_poll_time = 0, sound_get_buffer_time = 0, sound_poll_latch;
int sound_pos_global = 0;

int soundon = 1;

static int16_t cd_buffer[CDROM_NUM][CD_BUFLEN * 2];
static float cd_out_buffer[CD_BUFLEN * 2];
static thread_t *sound_cd_thread_h;
static event_t *sound_cd_event;
static unsigned int cd_vol_l, cd_vol_r;

void sound_set_cd_volume(unsigned int vol_l, unsigned int vol_r)
{
        cd_vol_l = vol_l;
        cd_vol_r = vol_r;
}

static void sound_cd_thread(void *param)
{
	int i = 0;

	while (1)
	{
		int c, has_audio;

		thread_wait_event(sound_cd_event, -1);
		for (c = 0; c < CD_BUFLEN*2; c += 2)
		{
			cd_out_buffer[c] = 0.0;
			cd_out_buffer[c+1] = 0.0;
		}
		for (i = 0; i < CDROM_NUM; i++)
		{
			has_audio = 0;
			if (cdrom_drives[i].handler->audio_callback)
			{
				cdrom_drives[i].handler->audio_callback(i, cd_buffer[i], CD_BUFLEN*2);
				has_audio = cdrom_drives[i].sound_on;
			}
			if (soundon && has_audio)
			{
				int32_t audio_vol_l = cdrom_mode_sense_get_volume(i, 0);
				int32_t audio_vol_r = cdrom_mode_sense_get_volume(i, 1);
				int channel_select[2];

				channel_select[0] = cdrom_mode_sense_get_channel(i, 0);
				channel_select[1] = cdrom_mode_sense_get_channel(i, 1);

				for (c = 0; c < CD_BUFLEN*2; c += 2)
				{
					float cd_buffer_temp[2] = {0.0, 0.0};
					float cd_buffer_temp2[2] = {0.0, 0.0};

					/* First, transfer the CD audio data to the temporary buffer. */
					cd_buffer_temp[0] = (float) cd_buffer[i][c];
					cd_buffer_temp[1] = (float) cd_buffer[i][c+1];
					
					/* Then, adjust input from drive according to ATAPI/SCSI volume. */
					cd_buffer_temp[0] *= (float) audio_vol_l;
					cd_buffer_temp[0] /= 255.0;
					cd_buffer_temp[1] *= (float) audio_vol_r;
					cd_buffer_temp[1] /= 255.0;

					/*Apply ATAPI channel select*/
					cd_buffer_temp2[0] = cd_buffer_temp2[1] = 0.0;
					if (channel_select[0] & 1)
					{
						cd_buffer_temp2[0] += cd_buffer_temp[0];
					}
					if (channel_select[0] & 2)
					{
						cd_buffer_temp2[1] += cd_buffer_temp[0];
					}
					if (channel_select[1] & 1)
					{
						cd_buffer_temp2[0] += cd_buffer_temp[1];
					}
					if (channel_select[1] & 2)
					{
						cd_buffer_temp2[1] += cd_buffer_temp[1];
					}

					/*Apply sound card CD volume*/
					cd_buffer_temp2[0] *= (float) cd_vol_l;
					cd_buffer_temp2[0] /= 65535.0;

					cd_buffer_temp2[1] *= (float) cd_vol_r;
					cd_buffer_temp2[1] /= 65535.0;

					cd_out_buffer[c] += (cd_buffer_temp2[0] / 32768.0);
					cd_out_buffer[c+1] += (cd_buffer_temp2[1] / 32768.0);
				}
			}
		}
		givealbuffer_cd(cd_out_buffer);
	}
}

static int32_t *outbuffer;
static float *outbuffer_ex;

void sound_init()
{
        initalmain(0,NULL);
        inital();

        outbuffer = malloc(SOUNDBUFLEN * 2 * sizeof(int32_t));
        outbuffer_ex = malloc(SOUNDBUFLEN * 2 * sizeof(float));
        
        sound_cd_event = thread_create_event();
        sound_cd_thread_h = thread_create(sound_cd_thread, NULL);
}

void sound_add_handler(void (*get_buffer)(int32_t *buffer, int len, void *p), void *p)
{
        sound_handlers[sound_handlers_num].get_buffer = get_buffer;
        sound_handlers[sound_handlers_num].priv = p;
        sound_handlers_num++;
}

void sound_poll(void *priv)
{
        sound_poll_time += sound_poll_latch;
        
        sound_pos_global++;
        if (sound_pos_global == SOUNDBUFLEN)
        {
                int c;
/*                int16_t buf16[SOUNDBUFLEN * 2 ];*/

                memset(outbuffer, 0, SOUNDBUFLEN * 2 * sizeof(int32_t));

                for (c = 0; c < sound_handlers_num; c++)
                        sound_handlers[c].get_buffer(outbuffer, SOUNDBUFLEN, sound_handlers[c].priv);


/*                for (c=0;c<SOUNDBUFLEN*2;c++)
                {
                        if (outbuffer[c] < -32768)
                                buf16[c] = -32768;
                        else if (outbuffer[c] > 32767)
                                buf16[c] = 32767;
                        else
                                buf16[c] = outbuffer[c];
                }

        if (!soundf) soundf=fopen("sound.pcm","wb");
        fwrite(buf16,(SOUNDBUFLEN)*2*2,1,soundf);*/

		for (c = 0; c < SOUNDBUFLEN * 2; c++)
		{
			outbuffer_ex[c] = ((float) outbuffer[c]) / 32768.0;
		}
        
                if (soundon) givealbuffer(outbuffer_ex);
        
                thread_set_event(sound_cd_event);

                sound_pos_global = 0;
        }
}

void sound_speed_changed()
{
        sound_poll_latch = (int)((double)TIMER_USEC * (1000000.0 / 48000.0));
}

void sound_reset()
{
	int i = 0;

        timer_add(sound_poll, &sound_poll_time, TIMER_ALWAYS_ENABLED, NULL);

        sound_handlers_num = 0;
        
        sound_set_cd_volume(65535, 65535);

	for (i = 0; i < CDROM_NUM; i++)
	{
		pclog("Resetting audio for CD-ROM %i...\n", i);
		if (cdrom_drives[i].handler->audio_stop)
		{
	        	cdrom_drives[i].handler->audio_stop(i);
		}
	}
}
