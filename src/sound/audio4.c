/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Interface to audio(4) for NetBSD/OpenBSD.
 *
 * Authors: Nishi
 *
 *          Copyright 2025 Nishi.
 */
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>

#include <sys/audioio.h>
#include <sys/param.h>

#include <86box/86box.h>
#include <86box/sound.h>
#include <86box/plat_unused.h>

#if defined(OpenBSD) && OpenBSD >= 201709
#define USE_NEW_API
#endif

#define I_NORMAL 0
#define I_MUSIC 1
#define I_WT 2
#define I_CD 3
#define I_FDD 4
#define I_HDD 5
#define I_MIDI 6

static int audio[7] = {-1, -1, -1, -1, -1, -1, -1};
extern bool fast_forward;

#ifdef USE_NEW_API
static struct audio_swpar info[7];
#else
static audio_info_t info[7];
#endif
static int freqs[7] = {SOUND_FREQ, MUSIC_FREQ, WT_FREQ, CD_FREQ, SOUND_FREQ, SOUND_FREQ, 0};
void
closeal(void)
{
    for (int i = 0; i < sizeof(audio) / sizeof(audio[0]); i++) {
        if (audio[i] != -1)
            close(audio[i]);

        audio[i] = -1;
    }
}

void
inital(void)
{
    for (int i = 0; i < sizeof(audio) / sizeof(audio[0]); i++) {
        audio[i] = open("/dev/audio", O_WRONLY);
        if (audio[i] == -1)
            audio[i] = open("/dev/audio0", O_WRONLY);
        if (audio[i] != -1) {
#ifdef USE_NEW_API
            AUDIO_INITPAR(&info[i]);
            ioctl(audio[i], AUDIO_GETPAR, &info[i]);
            info[i].sig = 1;
            info[i].bits = 16;
            info[i].pchan = 2;
            info[i].bps = 2;
            ioctl(audio[i], AUDIO_SETPAR, &info[i]);
#else
            AUDIO_INITINFO(&info[i]);
#if defined(__NetBSD__) && (__NetBSD_Version__ >= 900000000)
            ioctl(audio[i], AUDIO_GETFORMAT, &info[i]);
#else
            ioctl(audio[i], AUDIO_GETINFO, &info[i]);
#endif
            info[i].play.channels = 2;
            info[i].play.precision = 16;
            info[i].play.encoding = AUDIO_ENCODING_SLINEAR;
            info[i].hiwat = 5;
            info[i].lowat = 3;
            ioctl(audio[i], AUDIO_SETINFO, &info[i]);
#endif
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

    if(audio[src] == -1 || fast_forward)
        return;

    gain = sound_muted ? 0.0 : pow(10.0, (double) sound_gain / 20.0);

    if (sound_is_float) {
        float* input = (float*)buf;
        conv_size = sizeof(int16_t) * size;
        conv = malloc(conv_size);
        for (int i = 0; i < conv_size / sizeof(int16_t); i++)
            conv[i] = 32767 * input[i];
    } else {
        conv_size = size * sizeof(int16_t);
        conv = malloc(conv_size);
        memcpy(conv, buf, conv_size);
    }

#ifdef USE_NEW_API
    target_rate = info[src].rate;
#else
    target_rate = info[src].play.sample_rate;
#endif

    output_size = (double) conv_size * target_rate / freq;
    output_size -= output_size % 4;
    output = malloc(output_size);
    
    for (int i = 0; i < output_size / sizeof(int16_t) / 2; i++) {
        int ind = i * freq / target_rate * 2;
        output[i * 2 + 0] = conv[ind + 0] * gain;
        output[i * 2 + 1] = conv[ind + 1] * gain;
    }

    write(audio[src], output, output_size);

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
givealbuffer_midi(const void *buf, const uint32_t size)
{
    givealbuffer_common(buf, I_MIDI, (int) size);
}

void
al_set_midi(const int freq, UNUSED(const int buf_size))
{
    freqs[I_MIDI] = freq;
}
