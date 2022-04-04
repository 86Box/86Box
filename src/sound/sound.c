/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Sound emulation core.
 *
 *
 *
 * Authors: Sarah Walker, <http://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2020 Miran Grca.
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/cdrom.h>
#include <86box/device.h>
#include <86box/filters.h>
#include <86box/hdc_ide.h>
#include <86box/machine.h>
#include <86box/midi.h>
#include <86box/plat.h>
#include <86box/snd_ac97.h>
#include <86box/snd_azt2316a.h>
#include <86box/timer.h>
#include <86box/snd_mpu401.h>
#include <86box/sound.h>
#include <86box/snd_opl.h>
#include <86box/snd_sb_dsp.h>

typedef struct {
    const device_t *device;
} SOUND_CARD;

typedef struct {
    void (*get_buffer)(int32_t *buffer, int len, void *p);
    void *priv;
} sound_handler_t;

int sound_card_current = 0;
int sound_pos_global   = 0;
int sound_gain         = 0;

static sound_handler_t sound_handlers[8];

static thread_t  *sound_cd_thread_h;
static event_t   *sound_cd_event;
static event_t   *sound_cd_start_event;
static int32_t   *outbuffer;
static float     *outbuffer_ex;
static int16_t   *outbuffer_ex_int16;
static int        sound_handlers_num;
static pc_timer_t sound_poll_timer;
static uint64_t   sound_poll_latch;

static int16_t      cd_buffer[CDROM_NUM][CD_BUFLEN * 2];
static float        cd_out_buffer[CD_BUFLEN * 2];
static int16_t      cd_out_buffer_int16[CD_BUFLEN * 2];
static unsigned int cd_vol_l, cd_vol_r;
static int          cd_buf_update    = CD_BUFLEN / SOUNDBUFLEN;
static volatile int cdaudioon        = 0;
static int          cd_thread_enable = 0;

static void (*filter_cd_audio)(int channel, double *buffer, void *p) = NULL;
static void *filter_cd_audio_p                                       = NULL;

static const device_t sound_none_device = {
    "None",
    "none",
    0,
    0,
    NULL,
    NULL,
    NULL,
    { NULL },
    NULL,
    NULL,
    NULL
};
static const device_t sound_internal_device = {
    "Internal",
    "internal",
    0,
    0,
    NULL,
    NULL,
    NULL,
    { NULL },
    NULL,
    NULL,
    NULL
};

static const SOUND_CARD sound_cards[] = {
    // clang-format off
    { &sound_none_device         },
    { &sound_internal_device     },
    { &adlib_device              },
    { &adgold_device             },
    { &azt2316a_device           },
    { &azt1605_device            },
    { &cs4235_device             },
    { &cs4236b_device            },
    { &sb_1_device               },
    { &sb_15_device              },
    { &sb_2_device               },
    { &sb_pro_v1_device          },
    { &sb_pro_v2_device          },
    { &sb_16_device              },
    { &sb_16_pnp_device          },
    { &sb_32_pnp_device          },
    { &sb_awe32_device           },
    { &sb_awe32_pnp_device       },
    { &sb_awe64_value_device     },
    { &sb_awe64_device           },
    { &sb_awe64_gold_device      },
#if defined(DEV_BRANCH) && defined(USE_PAS16)
    { &pas16_device              },
#endif
#if defined(DEV_BRANCH) && defined(USE_TANDY_ISA)
    { &pssj_isa_device           },
    { &tndy_device               },
#endif
    { &wss_device                },
    { &adlib_mca_device          },
    { &ncr_business_audio_device },
    { &sb_mcv_device             },
    { &sb_pro_mcv_device         },
    { &cmi8338_device            },
    { &cmi8738_device            },
    { &es1371_device             },
    { &ad1881_device             },
    { &cs4297a_device            },
    { NULL                       }
    // clang-format on
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
#    define sound_log(fmt, ...)
#endif

int
sound_card_available(int card)
{
    if (sound_cards[card].device)
        return device_available(sound_cards[card].device);

    return 1;
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
    return device_has_config(sound_cards[card].device) ? 1 : 0;
}

char *
sound_card_get_internal_name(int card)
{
    return device_get_internal_name(sound_cards[card].device);
}

int
sound_card_get_from_internal_name(char *s)
{
    int c = 0;

    while (sound_cards[c].device != NULL) {
        if (!strcmp((char *) sound_cards[c].device->internal_name, s))
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
    uint32_t lba;
    int      c, r, i, pre, channel_select[2];
    double   audio_vol_l, audio_vol_r;
    double   cd_buffer_temp[2] = { 0.0, 0.0 };

    thread_set_event(sound_cd_start_event);

    while (cdaudioon) {
        thread_wait_event(sound_cd_event, -1);
        thread_reset_event(sound_cd_event);

        if (!cdaudioon)
            return;

        sound_cd_clean_buffers();

        for (i = 0; i < CDROM_NUM; i++) {
            if ((cdrom[i].bus_type == CDROM_BUS_DISABLED) || (cdrom[i].cd_status == CD_STATUS_EMPTY))
                continue;
            lba = cdrom[i].seek_pos;
            r   = cdrom_audio_callback(&(cdrom[i]), cd_buffer[i], CD_BUFLEN * 2);
            if (!cdrom[i].bus_type || !cdrom[i].sound_on || !r)
                continue;
            pre = cdrom_is_pre(&(cdrom[i]), lba);

            if (cdrom[i].get_volume) {
                audio_vol_l = (float) (cdrom[i].get_volume(cdrom[i].priv, 0));
                audio_vol_r = (float) (cdrom[i].get_volume(cdrom[i].priv, 1));
            } else {
                audio_vol_l = 255.0;
                audio_vol_r = 255.0;
            }

            /* Calculate attenuation per the specification. */
            if (audio_vol_l >= 255.0)
                audio_vol_l = 1.0;
            else if (audio_vol_l > 0.0)
                audio_vol_l = (48.0 + (20.0 * log(audio_vol_l / 256.0))) / 48.0;
            else
                audio_vol_l = 0.0;

            if (audio_vol_r >= 255.0)
                audio_vol_r = 1.0;
            else if (audio_vol_r > 0.0)
                audio_vol_r = (48.0 + (20.0 * log(audio_vol_r / 256.0))) / 48.0;
            else
                audio_vol_r = 0.0;

            if (cdrom[i].get_channel) {
                channel_select[0] = cdrom[i].get_channel(cdrom[i].priv, 0);
                channel_select[1] = cdrom[i].get_channel(cdrom[i].priv, 1);
            } else {
                channel_select[0] = 1;
                channel_select[1] = 2;
            }

            for (c = 0; c < CD_BUFLEN * 2; c += 2) {
                /*Apply ATAPI channel select*/
                cd_buffer_temp[0] = cd_buffer_temp[1] = 0.0;

                if ((audio_vol_l != 0.0) && (channel_select[0] != 0)) {
                    if (channel_select[0] & 1)
                        cd_buffer_temp[0] += ((double) cd_buffer[i][c]); /* Channel 0 => Port 0 */
                    if (channel_select[0] & 2)
                        cd_buffer_temp[0] += ((double) cd_buffer[i][c + 1]); /* Channel 1 => Port 0 */

                    cd_buffer_temp[0] *= audio_vol_l; /* Multiply Port 0 by Port 0 volume */

                    if (pre)
                        cd_buffer_temp[0] = deemph_iir(0, cd_buffer_temp[0]); /* De-emphasize if necessary */
                }

                if ((audio_vol_r != 0.0) && (channel_select[1] != 0)) {
                    if (channel_select[1] & 1)
                        cd_buffer_temp[1] += ((double) cd_buffer[i][c]); /* Channel 0 => Port 1 */
                    if (channel_select[1] & 2)
                        cd_buffer_temp[1] += ((double) cd_buffer[i][c + 1]); /* Channel 1 => Port 1 */

                    cd_buffer_temp[1] *= audio_vol_r; /* Multiply Port 1 by Port 1 volume */

                    if (pre)
                        cd_buffer_temp[1] = deemph_iir(1, cd_buffer_temp[1]); /* De-emphasize if necessary */
                }

                /* Apply sound card CD volume and filters */
                if (filter_cd_audio != NULL) {
                    filter_cd_audio(0, &(cd_buffer_temp[0]), filter_cd_audio_p);
                    filter_cd_audio(1, &(cd_buffer_temp[1]), filter_cd_audio_p);
                }

                if (sound_is_float) {
                    cd_out_buffer[c] += (float) (cd_buffer_temp[0] / 32768.0);
                    cd_out_buffer[c + 1] += (float) (cd_buffer_temp[1] / 32768.0);
                } else {
                    if (cd_buffer_temp[0] > 32767)
                        cd_buffer_temp[0] = 32767;
                    if (cd_buffer_temp[0] < -32768)
                        cd_buffer_temp[0] = -32768;
                    if (cd_buffer_temp[1] > 32767)
                        cd_buffer_temp[1] = 32767;
                    if (cd_buffer_temp[1] < -32768)
                        cd_buffer_temp[1] = -32768;

                    cd_out_buffer_int16[c] += (int16_t) cd_buffer_temp[0];
                    cd_out_buffer_int16[c + 1] += (int16_t) cd_buffer_temp[1];
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
    if (outbuffer_ex != NULL) {
        free(outbuffer_ex);
        outbuffer_ex = NULL;
    }

    if (outbuffer_ex_int16 != NULL) {
        free(outbuffer_ex_int16);
        outbuffer_ex_int16 = NULL;
    }

    if (sound_is_float) {
        outbuffer_ex = calloc(SOUNDBUFLEN * 2, sizeof(float));
        memset(outbuffer_ex, 0x00, SOUNDBUFLEN * 2 * sizeof(float));
    } else {
        outbuffer_ex_int16 = calloc(SOUNDBUFLEN * 2, sizeof(int16_t));
        memset(outbuffer_ex_int16, 0x00, SOUNDBUFLEN * 2 * sizeof(int16_t));
    }
}

void
sound_init(void)
{
    int i                      = 0;
    int available_cdrom_drives = 0;

    outbuffer_ex       = NULL;
    outbuffer_ex_int16 = NULL;

    outbuffer = NULL;
    outbuffer = calloc(SOUNDBUFLEN * 2, sizeof(int32_t));
    memset(outbuffer, 0x00, SOUNDBUFLEN * 2 * sizeof(int32_t));

    for (i = 0; i < CDROM_NUM; i++) {
        if (cdrom[i].bus_type != CDROM_BUS_DISABLED)
            available_cdrom_drives++;
    }

    if (available_cdrom_drives) {
        cdaudioon = 1;

        sound_cd_start_event = thread_create_event();

        sound_cd_event    = thread_create_event();
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
    sound_handlers[sound_handlers_num].priv       = p;
    sound_handlers_num++;
}

void
sound_set_cd_audio_filter(void (*filter)(int channel, double *buffer, void *p), void *p)
{
    if ((filter_cd_audio == NULL) || (filter == NULL)) {
        filter_cd_audio   = filter;
        filter_cd_audio_p = p;
    }
}

void
sound_poll(void *priv)
{
    timer_advance_u64(&sound_poll_timer, sound_poll_latch);

    midi_poll();

    sound_pos_global++;
    if (sound_pos_global == SOUNDBUFLEN) {
        int c;

        memset(outbuffer, 0x00, SOUNDBUFLEN * 2 * sizeof(int32_t));

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
    sound_poll_latch = (uint64_t) ((double) TIMER_USEC * (1000000.0 / 48000.0));
}

void
sound_reset(void)
{
    sound_realloc_buffers();

    midi_out_device_init();
    midi_in_device_init();

    inital();

    timer_add(&sound_poll_timer, sound_poll, NULL, 1);

    sound_handlers_num = 0;
    memset(sound_handlers, 0x00, 8 * sizeof(sound_handler_t));

    filter_cd_audio   = NULL;
    filter_cd_audio_p = NULL;

    sound_set_cd_volume(65535, 65535);
}

void
sound_card_reset(void)
{
    /* Reset the MPU-401 already loaded flag and the chain of input/output handlers. */
    midi_in_handlers_clear();

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
        thread_wait(sound_cd_thread_h);
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
    int i                      = 0;
    int available_cdrom_drives = 0;

    for (i = 0; i < CDROM_NUM; i++) {
        cdrom_stop(&(cdrom[i]));

        if (cdrom[i].bus_type != CDROM_BUS_DISABLED)
            available_cdrom_drives++;
    }

    if (available_cdrom_drives && !cd_thread_enable) {
        cdaudioon = 1;

        sound_cd_start_event = thread_create_event();

        sound_cd_event    = thread_create_event();
        sound_cd_thread_h = thread_create(sound_cd_thread, NULL);

        thread_wait_event(sound_cd_start_event, -1);
        thread_reset_event(sound_cd_start_event);
    } else if (!available_cdrom_drives && cd_thread_enable)
        sound_cd_thread_end();

    cd_thread_enable = available_cdrom_drives ? 1 : 0;
}
