/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Interface to sndio
 *
 * Authors: Nishi
 *
 *          Copyright 2025 Nishi.
 */
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sndio.h>

#include <86box/86box.h>
#include <86box/sound.h>
#include <86box/plat_unused.h>

#define I_NORMAL 0
#define I_MUSIC 1
#define I_WT 2
#define I_CD 3
#define I_FDD 4
#define I_HDD 5
#define I_YM2151 6
#define I_CQM 7
#define I_MIDI 8

extern bool fast_forward;
static struct sio_hdl* audio[9] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static struct sio_par  info[9];
static int             freqs[9] = { 0, MUSIC_FREQ, WT_FREQ, CD_FREQ, 0, 0, YM2151_FREQ, CQM_FREQ, 0 };
const char *
sound_get_output_devices(void)
{
    static char dev_list[1024];
    char       *p   = dev_list;
    size_t      rem = sizeof(dev_list);

    memset(dev_list, 0, sizeof(dev_list));

    for (int i = 0; i < 16; i++) {
        char           devname[32];
        struct sio_hdl *hdl;
        size_t          len;

        snprintf(devname, sizeof(devname), "snd/%d", i);
        hdl = sio_open(devname, SIO_PLAY, 0);
        if (hdl == NULL)
            break; /* devices are numbered consecutively */
        sio_close(hdl);

        len = strlen(devname) + 1;
        if (len < rem) {
            memcpy(p, devname, len);
            p   += len;
            rem -= len;
        }
    }

    if (p > dev_list) {
        if (rem > 0)
            *p = '\0'; /* double-null terminator */
        return dev_list;
    }
    return NULL; /* sndiod not running or no devices */
}

int
sound_get_device_sample_rate(const char *device_name)
{
    const char     *devname = (device_name && device_name[0]) ? device_name : SIO_DEVANY;
    struct sio_hdl *hdl     = sio_open(devname, SIO_PLAY, 0);
    int             rate    = 0;

    if (hdl != NULL) {
        struct sio_par par;
        sio_getpar(hdl, &par);
        rate = (int) par.rate;
        sio_close(hdl);
    }
    return rate;
}

int
sound_get_device_supported_rates(const char *device_name, int *rates_out, int max_rates)
{
    static const int candidates[] = { FREQ_44100, FREQ_48000 };
    const int        num_cands    = (int) (sizeof(candidates) / sizeof(candidates[0]));
    const char      *devname      = (device_name && device_name[0]) ? device_name : SIO_DEVANY;
    int              count        = 0;

    for (int i = 0; i < num_cands && count < max_rates; i++) {
        struct sio_hdl *hdl = sio_open(devname, SIO_PLAY, 0);
        if (hdl == NULL)
            continue;

        struct sio_par par;
        sio_initpar(&par);
        par.rate = (unsigned int) candidates[i];
        sio_setpar(hdl, &par);
        sio_getpar(hdl, &par);
        sio_close(hdl);

        if ((int) par.rate == candidates[i])
            rates_out[count++] = candidates[i];
    }

    if (count == 0) {
        for (int i = 0; i < num_cands && i < max_rates; i++)
            rates_out[i] = candidates[i];
        count = num_cands;
    }

    return count;
}

void
closeal(void)
{
    for (int i = 0; i < sizeof(audio) / sizeof(audio[0]); i++) {
        if (audio[i] != NULL)
            sio_close(audio[i]);

        audio[i] = NULL;
    }
}

void
inital(void)
{
    freqs[I_NORMAL] = freqs[I_FDD] = freqs[I_HDD] = sound_sample_rate;

    const char *devname = (sound_output_device[0] != '\0') ? sound_output_device : SIO_DEVANY;

    for (int i = 0; i < sizeof(audio) / sizeof(audio[0]); i++) {
        audio[i] = sio_open(devname, SIO_PLAY, 0);
        if (audio[i] != NULL) {
            int rate;
            int max_frames;
            sio_getpar(audio[i], &info[i]);
            rate = info[i].rate;
            max_frames = info[i].bufsz;
            sio_initpar(&info[i]);
            info[i].sig = 1;
            info[i].bits = 16;
            info[i].pchan = 2;
            info[i].rate = rate;
            info[i].appbufsz = max_frames;
            sio_setpar(audio[i], &info[i]);
            sio_getpar(audio[i], &info[i]);
            if (!sio_start(audio[i])) {
                sio_close(audio[i]);
                audio[i] = NULL;
            }
        }
    }
}

void
givealbuffer_common(const void *buf, const uint8_t src, const int size)
{
    const int freq = freqs[src];
    int16_t* output;
    int output_size;
    int16_t* conv;
    int conv_size;
    double gain;
    int target_rate;
    if (audio[src] == NULL || fast_forward)
        return;

    gain = sound_muted ? 0.0 : pow(10.0, (double) sound_gain / 20.0);

    if (sound_is_float) {
        float* input = (float*) buf;
        conv_size = sizeof(int16_t) * size;
        conv = calloc(1, conv_size);
        for (int i = 0; i < conv_size / sizeof(int16_t); i++)
            conv[i] = 32767 * input[i];
    } else {
        conv_size = size * sizeof(int16_t);
        conv = calloc(1, conv_size);
        memcpy(conv, buf, conv_size);
    }

    target_rate = info[src].rate;

    output_size = (double) conv_size * target_rate / freq;
    output_size -= output_size % 4;
    output = calloc(1, output_size);
    
    for (int i = 0; i < output_size / sizeof(int16_t) / 2; i++) {
        int ind = i * freq / target_rate * 2;
        output[i * 2 + 0] = conv[ind + 0] * gain;
        output[i * 2 + 1] = conv[ind + 1] * gain;
    }

    sio_write(audio[src], output, output_size);

    free(conv);
    free(output);
}

void
givealbuffer(const void *buf)
{
    givealbuffer_common(buf, I_NORMAL, (sound_sample_rate / 50) << 1);
}

void
givealbuffer_music(const void *buf)
{
    givealbuffer_common(buf, I_MUSIC, MUSICBUFLEN << 1);
}

void
givealbuffer_wt(const void *buf)
{
    givealbuffer_common(buf, I_WT, WTBUFLEN << 1);
}

void
givealbuffer_cd(const void *buf)
{
    givealbuffer_common(buf, I_CD, CD_BUFLEN << 1);
}

void
givealbuffer_midi(const void *buf, const uint32_t size)
{
    givealbuffer_common(buf, I_MIDI, (int) size);
}

void
givealbuffer_fdd(const void *buf, const uint32_t size)
{
    givealbuffer_common(buf, I_FDD, (int) size);
}

void
givealbuffer_hdd(const void *buf, const uint32_t size)
{
    givealbuffer_common(buf, I_HDD, (int) size);
}	

void
givealbuffer_ym2151(const void *buf)
{
    givealbuffer_common(buf, I_YM2151, YM2151BUFLEN << 1);
}

void
givealbuffer_cqm(const void *buf)
{
    givealbuffer_common(buf, I_CQM, CQMBUFLEN << 1);
}

void
al_set_midi(const int freq, UNUSED(const int buf_size))
{
    freqs[I_MIDI] = freq;
}
