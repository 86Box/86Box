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
 * Authors: Nishi,
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2025 Nishi.
 *          Copyright 2026 Miran Grca.
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

typedef struct _audio4_source_ {
    int        fd;            /* File descriptor of the audio device. */
    int        sample_rate;   /* Sample rate of the source. */

#ifdef USE_NEW_API
    struct audio_swpar info; /* Parameters of the device. */
#else
    audio_info_t       info; /* Parameters of the device. */
#endif
} audio4_source_t;

typedef struct _audio4_ {
    /* audio(4) has no device-wide handle: every source opens its own
       descriptor, so this only marks the backend as initialized. */
    int        initialized;
} audio4_t;

static void
audio4_give_buffer(void *priv, void *src, const void *buf, const int size, UNUSED(int gain))
{
    const audio4_t *       dev    = (audio4_t *) priv;
    const audio4_source_t *source = (audio4_source_t *) src;

    if (!fast_forward && (dev != NULL) && (source != NULL) && (source->fd != -1)) {
        int16_t *conv;
        int16_t *output;
        int      conv_size;
        int      output_size;
        int      target_rate;
        double   gain;
        const int freq = source->sample_rate;

        gain = (sound_muted) ? 0.0 : pow(10.0, (double) sound_gain / 20.0);

        if (sound_is_float) {
            float *input = (float *) buf;
            conv_size    = sizeof(int16_t) * size;
            conv         = calloc(1, conv_size);
            for (int i = 0; i < conv_size / (int) sizeof(int16_t); i++)
                conv[i] = 32767 * input[i];
        } else {
            conv_size = size * (int) sizeof(int16_t);
            conv      = calloc(1, conv_size);
            memcpy(conv, buf, conv_size);
        }

#ifdef USE_NEW_API
        target_rate = source->info.rate;
#else
        target_rate = source->info.play.sample_rate;
#endif

        output_size = (double) conv_size * target_rate / freq;
        output_size -= output_size % 4;
        output = calloc(1, output_size);

        for (int i = 0; i < output_size / (int) sizeof(int16_t) / 2; i++) {
            int ind = i * freq / target_rate * 2;

            output[i * 2 + 0] = conv[ind + 0] * gain;
            output[i * 2 + 1] = conv[ind + 1] * gain;
        }

        write(source->fd, output, output_size);

        free(conv);
        free(output);
    }
}

static void *
audio4_init_source(void *priv, int sample_rate, int buffer_size)
{
    const audio4_t *dev = (audio4_t *) priv;

    audio4_source_t *source = (audio4_source_t *) calloc(1, sizeof(audio4_source_t));

    if ((dev == NULL) || (source == NULL)) {
        free(source);
        return NULL;
    }

    if (sound_output_device[0] != '\0')
        source->fd = open(sound_output_device, O_WRONLY);
    else {
        source->fd = open("/dev/audio", O_WRONLY);
        if (source->fd == -1)
            source->fd = open("/dev/audio0", O_WRONLY);
    }

    if (source->fd == -1) {
        free(source);
        return NULL;
    }

#ifdef USE_NEW_API
    AUDIO_INITPAR(&source->info);
    ioctl(source->fd, AUDIO_GETPAR, &source->info);
    source->info.sig   = 1;
    source->info.bits  = 16;
    source->info.pchan = 2;
    source->info.bps   = 2;
    ioctl(source->fd, AUDIO_SETPAR, &source->info);
#else
    AUDIO_INITINFO(&source->info);
#    if defined(__NetBSD__) && (__NetBSD_Version__ >= 900000000)
    ioctl(source->fd, AUDIO_GETFORMAT, &source->info);
#    else
    ioctl(source->fd, AUDIO_GETINFO, &source->info);
#    endif
    source->info.play.channels  = 2;
    source->info.play.precision = 16;
    source->info.play.encoding  = AUDIO_ENCODING_SLINEAR;
    source->info.hiwat          = 5;
    source->info.lowat          = 3;
    ioctl(source->fd, AUDIO_SETINFO, &source->info);
#endif

    source->sample_rate = sample_rate;

    return source;
}

static void
audio4_close_source(UNUSED(void *priv), void *src)
{
    audio4_source_t *source = (audio4_source_t *) src;

    if (source != NULL) {
        if (source->fd != -1)
            close(source->fd);

        free(source);
    }
}

static const char *
audio4_get_output_devices(void)
{
    static char dev_list[1024];
    char       *p   = dev_list;
    size_t      rem = sizeof(dev_list);

    memset(dev_list, 0x00, sizeof(dev_list));

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

static void
audio4_close(void *priv)
{
    audio4_t *dev = (audio4_t *) priv;

    if (dev != NULL)
        free(dev);
}

static void *
audio4_init(void)
{
    audio4_t *dev = (audio4_t *) calloc(1, sizeof(audio4_t));

    if (dev != NULL)
        dev->initialized = 1;

    return dev;
}

const sound_backend_t sound_backend_audio4 = {
    .name               = "audio(4)",
    .internal_name      = "audio4",
    .give_buffer        = audio4_give_buffer,
    .init_source        = audio4_init_source,
    .close_source       = audio4_close_source,
    .get_output_devices = audio4_get_output_devices,
    .init               = audio4_init,
    .close              = audio4_close
};
