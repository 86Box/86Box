/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Interface to sndio.
 *
 * Authors: Nishi,
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2025 Nishi.
 *          Copyright 2026 Miran Grca.
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

typedef struct _sndio_source_ {
    struct sio_hdl *hdl;          /* Handle of the sndio device. */
    int             sample_rate;  /* Sample rate of the source. */

    struct sio_par  info;         /* Parameters of the device. */
} sndio_source_t;

typedef struct _sndio_ {
    /* sndio has no device-wide handle: every source opens its own
       handle, so this only marks the backend as initialized. */
    int        initialized;
} sndio_t;

static void
sndio_give_buffer(void *priv, void *src, const void *buf, const int size, UNUSED(int gain))
{
    const sndio_t *       dev    = (sndio_t *) priv;
    const sndio_source_t *source = (sndio_source_t *) src;

    if (!fast_forward && (dev != NULL) && (source != NULL) && (source->hdl != NULL)) {
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

        target_rate = source->info.rate;

        output_size = (double) conv_size * target_rate / freq;
        output_size -= output_size % 4;
        output = calloc(1, output_size);

        for (int i = 0; i < output_size / (int) sizeof(int16_t) / 2; i++) {
            int ind = i * freq / target_rate * 2;

            output[i * 2 + 0] = conv[ind + 0] * gain;
            output[i * 2 + 1] = conv[ind + 1] * gain;
        }

        sio_write(source->hdl, output, output_size);

        free(conv);
        free(output);
    }
}

static void *
sndio_init_source(void *priv, int sample_rate, int buffer_size)
{
    const sndio_t *dev = (sndio_t *) priv;

    sndio_source_t *source = (sndio_source_t *) calloc(1, sizeof(sndio_source_t));

    if ((dev == NULL) || (source == NULL)) {
        free(source);
        return NULL;
    }

    const char *devname = (sound_output_device[0] != '\0') ? sound_output_device : SIO_DEVANY;

    source->hdl = sio_open(devname, SIO_PLAY, 0);
    if (source->hdl == NULL) {
        free(source);
        return NULL;
    }

    int rate       = 0;
    int max_frames = 0;

    sio_getpar(source->hdl, &source->info);
    rate       = source->info.rate;
    max_frames = source->info.bufsz;
    sio_initpar(&source->info);
    source->info.sig      = 1;
    source->info.bits     = 16;
    source->info.pchan    = 2;
    source->info.rate     = rate;
    source->info.appbufsz = max_frames;
    sio_setpar(source->hdl, &source->info);
    sio_getpar(source->hdl, &source->info);
    if (!sio_start(source->hdl)) {
        sio_close(source->hdl);
        source->hdl = NULL;

        free(source);
        return NULL;
    }

    source->sample_rate = sample_rate;

    return source;
}

static void
sndio_close_source(UNUSED(void *priv), void *src)
{
    sndio_source_t *source = (sndio_source_t *) src;

    if (source != NULL) {
        if (source->hdl != NULL) {
            sio_close(source->hdl);
            source->hdl = NULL;
        }

        free(source);
    }
}

static const char *
sndio_get_output_devices(void)
{
    static char dev_list[1024];
    char       *p   = dev_list;
    size_t      rem = sizeof(dev_list);

    memset(dev_list, 0x00, sizeof(dev_list));

    for (int i = 0; i < 16; i++) {
        char            devname[32];
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

static void
sndio_close(void *priv)
{
    sndio_t *dev = (sndio_t *) priv;

    if (dev != NULL)
        free(dev);
}

static void *
sndio_init(void)
{
    sndio_t *dev = (sndio_t *) calloc(1, sizeof(sndio_t));

    if (dev != NULL)
        dev->initialized = 1;

    return dev;
}

const sound_backend_t sound_backend_sndio = {
    .name               = "sndio",
    .internal_name      = "sndio",
    .give_buffer        = sndio_give_buffer,
    .init_source        = sndio_init_source,
    .close_source       = sndio_close_source,
    .get_output_devices = sndio_get_output_devices,
    .init               = sndio_init,
    .close              = sndio_close
};
