/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Sound emulation core.
 *
 * Version:	@(#)sound.c	1.0.25	2018/10/28
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../device.h"
#include "../timer.h"
#include "../cdrom/cdrom.h"
#include "../plat.h"
#include "sound.h"
#include "midi.h"
#include "snd_opl.h"
#include "snd_cms.h"
#include "snd_adlib.h"
#include "snd_adlibgold.h"
#include "snd_audiopci.h"
#include "snd_gus.h"
#include "snd_mpu401.h"
#if defined(DEV_BRANCH) && defined(USE_PAS16)
# include "snd_pas16.h"
#endif
#include "snd_sb.h"
#include "snd_sb_dsp.h"
#include "snd_ssi2001.h"
#include "snd_wss.h"
#include "filters.h"


typedef struct {
        const char *name;
        const char *internal_name;
        const device_t *device;
} SOUND_CARD;

typedef struct {
        void (*get_buffer)(int32_t *buffer, int len, void *p);
        void *priv;
} sound_handler_t;


int sound_card_current = 0;
int sound_pos_global = 0;
int sound_gain = 0;


static sound_handler_t sound_handlers[8];

static thread_t *sound_cd_thread_h;
static event_t *sound_cd_event;
static event_t *sound_cd_start_event;
static int32_t *outbuffer;
static float *outbuffer_ex;
static int16_t *outbuffer_ex_int16;
static int sound_handlers_num;
static int64_t sound_poll_time = 0LL, sound_poll_latch;

static int16_t cd_buffer[CDROM_NUM][CD_BUFLEN * 2];
static float cd_out_buffer[CD_BUFLEN * 2];
static int16_t cd_out_buffer_int16[CD_BUFLEN * 2];
static unsigned int cd_vol_l, cd_vol_r;
static int cd_buf_update = CD_BUFLEN / SOUNDBUFLEN;
static volatile int cdaudioon = 0;
static int cd_thread_enable = 0;


static const SOUND_CARD sound_cards[] =
{
    { "None",					"none",		NULL				},
    { "[ISA] Adlib",				"adlib",	&adlib_device			},
    { "[ISA] Adlib Gold",			"adlibgold",	&adgold_device			},
    { "[ISA] Sound Blaster 1.0",		"sb",		&sb_1_device			},
    { "[ISA] Sound Blaster 1.5",		"sb1.5",	&sb_15_device			},
    { "[ISA] Sound Blaster 2.0",		"sb2.0",	&sb_2_device			},
    { "[ISA] Sound Blaster Pro v1",		"sbprov1",	&sb_pro_v1_device		},
    { "[ISA] Sound Blaster Pro v2",		"sbprov2",	&sb_pro_v2_device		},
    { "[ISA] Sound Blaster 16",			"sb16",		&sb_16_device			},
    { "[ISA] Sound Blaster AWE32",		"sbawe32",	&sb_awe32_device		},
#if defined(DEV_BRANCH) && defined(USE_PAS16)
    { "[ISA] Pro Audio Spectrum 16",		"pas16",	&pas16_device			},
#endif
    { "[ISA] Windows Sound System",		"wss",		&wss_device			},
    { "[MCA] Adlib",                		"adlib_mca",	&adlib_mca_device		},
    { "[MCA] NCR Business Audio",    		"ncraudio",	&ncr_business_audio_device	},
    { "[MCA] Sound Blaster MCV",    		"sbmcv",	&sb_mcv_device			},
    { "[MCA] Sound Blaster Pro MCV",		"sbpromcv",	&sb_pro_mcv_device		},
    { "[PCI] Ensoniq AudioPCI (ES1371)",	"es1371",	&es1371_device			},
    { "[PCI] Sound Blaster PCI 128",		"sbpci128",	&es1371_device			},
    { "",					"",		NULL				}
};


#ifdef ENABLE_SOUND_LOG
int sound_do_log = ENABLE_SOUND_LOG;


static void
sound_log(const char *fmt, ...)
{
    va_list ap;

    if (sound_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define sound_log(fmt, ...)
#endif


int
sound_card_available(int card)
{
    if (sound_cards[card].device)
	return device_available(sound_cards[card].device);

    return 1;
}


char *
sound_card_getname(int card)
{
    return (char *) sound_cards[card].name;
}


const device_t *
sound_card_getdevice(int card)
{
    return sound_cards[card].device;
}


int
sound_card_has_config(int card)
{
    if (!sound_cards[card].device)
	return 0;
    return sound_cards[card].device->config ? 1 : 0;
}


char *
sound_card_get_internal_name(int card)
{
    return (char *) sound_cards[card].internal_name;
}


int
sound_card_get_from_internal_name(char *s)
{
    int c = 0;

    while (strlen((char *) sound_cards[c].internal_name)) {
	if (!strcmp((char *) sound_cards[c].internal_name, s))
		return c;
	c++;
    }

    return 0;
}


void
sound_card_init(void)
{
    if (sound_cards[sound_card_current].device)
	device_add(sound_cards[sound_card_current].device);
}


void
sound_set_cd_volume(unsigned int vol_l, unsigned int vol_r)
{
    cd_vol_l = vol_l;
    cd_vol_r = vol_r;
}


static void
sound_cd_clean_buffers(void)
{
    if (sound_is_float)
	memset(cd_out_buffer, 0, (CD_BUFLEN * 2) * sizeof(float));
    else
	memset(cd_out_buffer_int16, 0, (CD_BUFLEN * 2) * sizeof(int16_t));
}


static void
sound_cd_thread(void *param)
{
    int c, r, i, channel_select[2];
    float audio_vol_l, audio_vol_r;
    float cd_buffer_temp[2] = {0.0, 0.0};

    thread_set_event(sound_cd_start_event);

    while (cdaudioon) {
	thread_wait_event(sound_cd_event, -1);
	thread_reset_event(sound_cd_event);

	if (!cdaudioon)
		return;

	sound_cd_clean_buffers();

	for (i = 0; i < CDROM_NUM; i++) {
		if ((cdrom[i].bus_type == CDROM_BUS_DISABLED) ||
		    (cdrom[i].cd_status == CD_STATUS_EMPTY))
			continue;
		r = cdrom_audio_callback(&(cdrom[i]), cd_buffer[i], CD_BUFLEN * 2);
		if (!cdrom[i].bus_type || !cdrom[i].sound_on || !r)
				continue;

		if (cdrom[i].get_volume) {
			audio_vol_l = (float) (cdrom[i].get_volume(cdrom[i].priv, 0));
			audio_vol_r = (float) (cdrom[i].get_volume(cdrom[i].priv, 1));
		} else {
			audio_vol_l = 255.0;
			audio_vol_r = 255.0;
		}

		audio_vol_l /= 511.0;
		audio_vol_r /= 511.0;

		if (cdrom[i].get_channel) {
			channel_select[0] = cdrom[i].get_channel(cdrom[i].priv, 0);
			channel_select[1] = cdrom[i].get_channel(cdrom[i].priv, 1);
		} else {
			channel_select[0] = 1;
			channel_select[1] = 2;
		}

		for (c = 0; c < CD_BUFLEN*2; c += 2) {
			/*Apply ATAPI channel select*/
			cd_buffer_temp[0] = cd_buffer_temp[1] = 0.0;

			if (channel_select[0] & 1)
				cd_buffer_temp[0] += ((float) cd_buffer[i][c]) * audio_vol_l;
			if (channel_select[0] & 2)
				cd_buffer_temp[1] += ((float) cd_buffer[i][c]) * audio_vol_l;
			if (channel_select[1] & 1)
				cd_buffer_temp[0] += ((float) cd_buffer[i][c + 1]) * audio_vol_r;
			if (channel_select[1] & 2)
				cd_buffer_temp[1] += ((float) cd_buffer[i][c + 1]) * audio_vol_r;

			/*Apply sound card CD volume*/
			cd_buffer_temp[0] *= ((float) cd_vol_l) / 65535.0;
			cd_buffer_temp[1] *= ((float) cd_vol_r) / 65535.0;

			if (sound_is_float) {
				cd_out_buffer[c] += (cd_buffer_temp[0] / 32768.0);
				cd_out_buffer[c+1] += (cd_buffer_temp[1] / 32768.0);
			} else {
				if (cd_buffer_temp[0] > 32767)
					cd_buffer_temp[0] = 32767;
				if (cd_buffer_temp[0] < -32768)
					cd_buffer_temp[0] = -32768;
				if (cd_buffer_temp[1] > 32767)
					cd_buffer_temp[1] = 32767;
				if (cd_buffer_temp[1] < -32768)
					cd_buffer_temp[1] = -32768;

				cd_out_buffer_int16[c] += cd_buffer_temp[0];
				cd_out_buffer_int16[c+1] += cd_buffer_temp[1];
			}
		}
	}

	if (sound_is_float)
		givealbuffer_cd(cd_out_buffer);
	else
		givealbuffer_cd(cd_out_buffer_int16);
    }
}


static void
sound_realloc_buffers(void)
{
    if (outbuffer_ex != NULL)
	free(outbuffer_ex);

    if (outbuffer_ex_int16 != NULL)
	free(outbuffer_ex_int16);

    if (sound_is_float)
        outbuffer_ex = malloc(SOUNDBUFLEN * 2 * sizeof(float));
    else
        outbuffer_ex_int16 = malloc(SOUNDBUFLEN * 2 * sizeof(int16_t));
}


void
sound_init(void)
{
    int i = 0;
    int available_cdrom_drives = 0;

    outbuffer_ex = NULL;
    outbuffer_ex_int16 = NULL;

    outbuffer = malloc(SOUNDBUFLEN * 2 * sizeof(int32_t));

    for (i = 0; i < CDROM_NUM; i++) {
	if (cdrom[i].bus_type != CDROM_BUS_DISABLED)
		available_cdrom_drives++;
    }

    if (available_cdrom_drives) {
	cdaudioon = 1;

	sound_cd_start_event = thread_create_event();

	sound_cd_event = thread_create_event();
	sound_cd_thread_h = thread_create(sound_cd_thread, NULL);

	sound_log("Waiting for CD start event...\n");
	thread_wait_event(sound_cd_start_event, -1);
	thread_reset_event(sound_cd_start_event);
	sound_log("Done!\n");
    } else
	cdaudioon = 0;

    cd_thread_enable = available_cdrom_drives ? 1 : 0;
}


void
sound_add_handler(void (*get_buffer)(int32_t *buffer, int len, void *p), void *p)
{
    sound_handlers[sound_handlers_num].get_buffer = get_buffer;
    sound_handlers[sound_handlers_num].priv = p;
    sound_handlers_num++;
}


void
sound_poll(void *priv)
{
    sound_poll_time += sound_poll_latch;

    midi_poll();

    sound_pos_global++;
    if (sound_pos_global == SOUNDBUFLEN) {
	int c;

	memset(outbuffer, 0, SOUNDBUFLEN * 2 * sizeof(int32_t));

	for (c = 0; c < sound_handlers_num; c++)
		sound_handlers[c].get_buffer(outbuffer, SOUNDBUFLEN, sound_handlers[c].priv);

	for (c = 0; c < SOUNDBUFLEN * 2; c++) {
		if (sound_is_float)
			outbuffer_ex[c] = ((float) outbuffer[c]) / 32768.0;
		else {
			if (outbuffer[c] > 32767)
				outbuffer[c] = 32767;
			if (outbuffer[c] < -32768)
				outbuffer[c] = -32768;

			outbuffer_ex_int16[c] = outbuffer[c];
		}
	}

	if (sound_is_float)
		givealbuffer(outbuffer_ex);
	else
		givealbuffer(outbuffer_ex_int16);

	if (cd_thread_enable) {
                cd_buf_update--;
       	        if (!cd_buf_update) {
       	                cd_buf_update = (48000 / SOUNDBUFLEN) / (CD_FREQ / CD_BUFLEN);
               	        thread_set_event(sound_cd_event);
                }
	}

	sound_pos_global = 0;
    }
}


void
sound_speed_changed(void)
{
    sound_poll_latch = (int64_t)((double)TIMER_USEC * (1000000.0 / 48000.0));
}


void
sound_reset(void)
{
    sound_realloc_buffers();

    midi_device_init();
    inital();

    timer_add(sound_poll, &sound_poll_time, TIMER_ALWAYS_ENABLED, NULL);

    sound_handlers_num = 0;

    sound_set_cd_volume(65535, 65535);
}


void
sound_card_reset(void)
{
    sound_card_init();
    if (mpu401_standalone_enable)
	mpu401_device_add();
    if (GUS)
	device_add(&gus_device);
    if (GAMEBLASTER)
	device_add(&cms_device);
    if (SSI2001)
	device_add(&ssi2001_device);
}


void
sound_cd_thread_end(void)
{
    if (cdaudioon) {
	cdaudioon = 0;

	sound_log("Waiting for CD Audio thread to terminate...\n");
	thread_set_event(sound_cd_event);
	thread_wait(sound_cd_thread_h, -1);
	sound_log("CD Audio thread terminated...\n");

	if (sound_cd_event) {
		thread_destroy_event(sound_cd_event);
		sound_cd_event = NULL;
	}

	sound_cd_thread_h = NULL;

	if (sound_cd_start_event) {
		thread_destroy_event(sound_cd_start_event);
		sound_cd_event = NULL;
	}
    }
}


void
sound_cd_thread_reset(void)
{
    int i = 0;
    int available_cdrom_drives = 0;

    for (i = 0; i < CDROM_NUM; i++) {
	cdrom_stop(&(cdrom[i]));

	if (cdrom[i].bus_type != CDROM_BUS_DISABLED)
		available_cdrom_drives++;
    }

    if (available_cdrom_drives && !cd_thread_enable) {
	cdaudioon = 1;

	sound_cd_start_event = thread_create_event();

	sound_cd_event = thread_create_event();
	sound_cd_thread_h = thread_create(sound_cd_thread, NULL);

	thread_wait_event(sound_cd_start_event, -1);
	thread_reset_event(sound_cd_start_event);
    } else if (!available_cdrom_drives && cd_thread_enable)
	sound_cd_thread_end();

    cd_thread_enable = available_cdrom_drives ? 1 : 0;
}
