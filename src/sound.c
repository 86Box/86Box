/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <stdio.h>
#include <stdlib.h>
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
        // {"Sound Blaster AWE64 PCI",&sb_awe64pci_device},
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

static int64_t sound_poll_time = 0, sound_get_buffer_time = 0, sound_poll_latch;
int sound_pos_global = 0;

int soundon = 1;

static int16_t cd_buffer[CD_BUFLEN * 2];
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
        while (1)
        {
                int c;
                
                thread_wait_event(sound_cd_event, -1);
                ioctl_audio_callback(cd_buffer, CD_BUFLEN*2);
                if (soundon)
                {
                        int32_t atapi_vol_l = atapi_get_cd_volume(0);
                        int32_t atapi_vol_r = atapi_get_cd_volume(1);
                        int channel_select[2];
                        
                        channel_select[0] = atapi_get_cd_channel(0);
                        channel_select[1] = atapi_get_cd_channel(1);
                        
                        for (c = 0; c < CD_BUFLEN*2; c += 2)
                        {
                                int32_t cd_buffer_temp[2] = {0, 0};
                                
        			/*First, adjust input from drive according to ATAPI volume.*/
        			cd_buffer[c]   = ((int32_t)cd_buffer[c]   * atapi_vol_l) / 255;
                                cd_buffer[c+1] = ((int32_t)cd_buffer[c+1] * atapi_vol_r) / 255;

                                /*Apply ATAPI channel select*/
                                if (channel_select[0] & 1)
                                        cd_buffer_temp[0] += cd_buffer[c];
                                if (channel_select[0] & 2)
                                        cd_buffer_temp[1] += cd_buffer[c];
                                if (channel_select[1] & 1)
                                        cd_buffer_temp[0] += cd_buffer[c+1];
                                if (channel_select[1] & 2)
                                        cd_buffer_temp[1] += cd_buffer[c+1];
                                
                                /*Apply sound card CD volume*/
                                cd_buffer_temp[0] = (cd_buffer_temp[0] * (int)cd_vol_l) / 65535;
                                cd_buffer_temp[1] = (cd_buffer_temp[1] * (int)cd_vol_r) / 65535;

                                if (cd_buffer_temp[0] > 32767)
                                        cd_buffer_temp[0] = 32767;
                                if (cd_buffer_temp[0] < -32768)
                                        cd_buffer_temp[0] = -32768;
                                if (cd_buffer_temp[1] > 32767)
                                        cd_buffer_temp[1] = 32767;
                                if (cd_buffer_temp[1] < -32768)
                                        cd_buffer_temp[1] = -32768;

                                cd_buffer[c]   = cd_buffer_temp[0];
                                cd_buffer[c+1] = cd_buffer_temp[1];
                        }

                        givealbuffer_cd(cd_buffer);
                }
        }
}

static int32_t *outbuffer;

void sound_init()
{
        initalmain(0,NULL);
        inital();

        outbuffer = malloc(SOUNDBUFLEN * 2 * sizeof(int32_t));
        
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
        
                if (soundon) givealbuffer(outbuffer);
        
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
        timer_add(sound_poll, &sound_poll_time, TIMER_ALWAYS_ENABLED, NULL);

        sound_handlers_num = 0;
        
        sound_set_cd_volume(65535, 65535);
        ioctl_audio_stop();
}
