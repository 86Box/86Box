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
#define I_MIDI 4
#define I_FDD 5
#define I_HDD 6

extern bool fast_forward;
static struct sio_hdl* audio[7] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static struct sio_par  info[7];
static int             freqs[7] = { SOUND_FREQ, MUSIC_FREQ, WT_FREQ, CD_FREQ, SOUND_FREQ, SOUND_FREQ, 0 };
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
    for (int i = 0; i < sizeof(audio) / sizeof(audio[0]); i++) {
        audio[i] = sio_open(SIO_DEVANY, SIO_PLAY, 0);
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
        conv = malloc(conv_size);
        for (int i = 0; i < conv_size / sizeof(int16_t); i++)
            conv[i] = 32767 * input[i];
    } else {
        conv_size = size * sizeof(int16_t);
        conv = malloc(conv_size);
        memcpy(conv, buf, conv_size);
    }

    target_rate = info[src].rate;

    output_size = (double) conv_size * target_rate / freq;
    output_size -= output_size % 4;
    output = malloc(output_size);
    
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
    givealbuffer_common(buf, I_NORMAL, SOUNDBUFLEN << 1);
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
al_set_midi(const int freq, UNUSED(const int buf_size))
{
    freqs[I_MIDI] = freq;
}
