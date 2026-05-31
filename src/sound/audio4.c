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
static int freqs[7] = {0, MUSIC_FREQ, WT_FREQ, CD_FREQ, 0, 0, 0};
const char *
sound_get_output_devices(void)
{
    static char dev_list[1024];
    char       *p   = dev_list;
    size_t      rem = sizeof(dev_list);

    memset(dev_list, 0, sizeof(dev_list));

    for (int i = 0; i < 8; i++) {
        char   devname[32];
        size_t len;

        snprintf(devname, sizeof(devname), "/dev/audio%d", i);
        if (access(devname, F_OK) != 0)
            break; /* devices are numbered consecutively */

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
    return NULL; /* no audio devices found */
}

int
sound_get_device_sample_rate(const char *device_name)
{
    const char *devpath = (device_name && device_name[0]) ? device_name : "/dev/audio";
    int         fd      = open(devpath, O_WRONLY | O_NONBLOCK);
    int         rate    = 0;

    if (fd == -1 && (!device_name || !device_name[0]))
        fd = open("/dev/audio0", O_WRONLY | O_NONBLOCK);
    if (fd == -1)
        return 0;

#ifdef USE_NEW_API
    struct audio_swpar par;
    AUDIO_INITPAR(&par);
    if (ioctl(fd, AUDIO_GETPAR, &par) == 0)
        rate = (int) par.rate;
#else
    audio_info_t ai;
    AUDIO_INITINFO(&ai);
#    if defined(__NetBSD__) && (__NetBSD_Version__ >= 900000000)
    if (ioctl(fd, AUDIO_GETFORMAT, &ai) == 0)
#    else
    if (ioctl(fd, AUDIO_GETINFO, &ai) == 0)
#    endif
        rate = (int) ai.play.sample_rate;
#endif

    close(fd);
    return rate;
}

int
sound_get_device_supported_rates(const char *device_name, int *rates_out, int max_rates)
{
    static const int candidates[] = { FREQ_44100, FREQ_48000 };
    const int        num_cands    = (int) (sizeof(candidates) / sizeof(candidates[0]));
    const char      *devpath      = (device_name && device_name[0]) ? device_name : "/dev/audio";
    int              count        = 0;

    for (int i = 0; i < num_cands && count < max_rates; i++) {
        int fd = open(devpath, O_WRONLY | O_NONBLOCK);
        if (fd == -1 && (!device_name || !device_name[0]))
            fd = open("/dev/audio0", O_WRONLY | O_NONBLOCK);
        if (fd == -1)
            continue;

        int got = 0;
#ifdef USE_NEW_API
        struct audio_swpar par;
        AUDIO_INITPAR(&par);
        ioctl(fd, AUDIO_GETPAR, &par);
        par.rate = (unsigned int) candidates[i];
        if (ioctl(fd, AUDIO_SETPAR, &par) == 0) {
            AUDIO_INITPAR(&par);
            ioctl(fd, AUDIO_GETPAR, &par);
            got = (int) par.rate;
        }
#else
        audio_info_t ai;
        AUDIO_INITINFO(&ai);
        ai.play.sample_rate = (unsigned int) candidates[i];
        if (ioctl(fd, AUDIO_SETINFO, &ai) == 0) {
            AUDIO_INITINFO(&ai);
#    if defined(__NetBSD__) && (__NetBSD_Version__ >= 900000000)
            ioctl(fd, AUDIO_GETFORMAT, &ai);
#    else
            ioctl(fd, AUDIO_GETINFO, &ai);
#    endif
            got = (int) ai.play.sample_rate;
        }
#endif
        close(fd);

        if (got == candidates[i])
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
        if (audio[i] != -1)
            close(audio[i]);

        audio[i] = -1;
    }
}

void
inital(void)
{
    freqs[I_NORMAL] = freqs[I_FDD] = freqs[I_HDD] = sound_sample_rate;

    for (int i = 0; i < sizeof(audio) / sizeof(audio[0]); i++) {
        if (sound_output_device[0] != '\0') {
            audio[i] = open(sound_output_device, O_WRONLY);
        } else {
            audio[i] = open("/dev/audio", O_WRONLY);
            if (audio[i] == -1)
                audio[i] = open("/dev/audio0", O_WRONLY);
        }
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
        conv = calloc(1, conv_size);
        for (int i = 0; i < conv_size / sizeof(int16_t); i++)
            conv[i] = 32767 * input[i];
    } else {
        conv_size = size * sizeof(int16_t);
        conv = calloc(1, conv_size);
        memcpy(conv, buf, conv_size);
    }

#ifdef USE_NEW_API
    target_rate = info[src].rate;
#else
    target_rate = info[src].play.sample_rate;
#endif

    output_size = (double) conv_size * target_rate / freq;
    output_size -= output_size % 4;
    output = calloc(1, output_size);
    
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
