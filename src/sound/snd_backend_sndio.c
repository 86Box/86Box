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
 * Authors: Nishi
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2025 Nishi.
 *          Copyright 2026 Miran Grca.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sndio.h>

#include <86box/86box.h>
#include <86box/sound.h>
#include <86box/plat_unused.h>
#include <86box/runctl.h>

typedef struct _sndio_source_ {
    struct sio_hdl *hdl;
    struct sio_par  par;
    int             sample_rate;
} sndio_source_t;

typedef struct _sndio_input_ {
    struct sio_hdl *hdl;
    int             rate;
} sndio_input_t;

static const char *
sndio_get_devices(const unsigned int mode)
{
    static char dev_list[1024];
    char       *p   = dev_list;
    size_t      rem = sizeof(dev_list);

    memset(dev_list, 0, sizeof(dev_list));

    for (int i = 0; i < 16; i++) {
        char            devname[32];
        struct sio_hdl *hdl;
        size_t          len;

        snprintf(devname, sizeof(devname), "snd/%d", i);
        hdl = sio_open(devname, mode, 0);
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

static const char *
sndio_get_output_devices(void)
{
    return sndio_get_devices(SIO_PLAY);
}

static const char *
sndio_get_input_devices(void)
{
    return sndio_get_devices(SIO_REC);
}

static int
sndio_get_supported_rates(const char *device_name, int *rates_out, const int max_rates)
{
    static const int candidates[] = { FREQ_44100, FREQ_48000 };
    const int        num_cands    = (int) (sizeof(candidates) / sizeof(candidates[0]));
    const char      *devname      = (device_name && device_name[0]) ? device_name : SIO_DEVANY;
    int              count        = 0;

    for (int i = 0; (i < num_cands) && (count < max_rates); i++) {
        struct sio_hdl *hdl = sio_open(devname, SIO_PLAY, 0);
        struct sio_par  par;

        if (hdl == NULL)
            continue;

        sio_initpar(&par);
        par.rate = (unsigned int) candidates[i];
        sio_setpar(hdl, &par);
        sio_getpar(hdl, &par);
        sio_close(hdl);

        if ((int) par.rate == candidates[i])
            rates_out[count++] = candidates[i];
    }

    /* Fallback: if detection failed entirely, return all candidates. */
    if (count == 0) {
        for (int i = 0; (i < num_cands) && (i < max_rates); i++)
            rates_out[i] = candidates[i];
        count = (num_cands < max_rates) ? num_cands : max_rates;
    }

    return count;
}

static void
sndio_give_buffer(UNUSED(void *priv), void *src, const void *buf, const int size, UNUSED(int gain))
{
    const sndio_source_t *source = (sndio_source_t *) src;

    if ((source != NULL) && (source->hdl != NULL) && (runctl_mode() != RUN_MODE_FAST)) {
        const int     freq        = source->sample_rate;
        const int     target_rate = (int) source->par.rate;
        const double  vol         = sound_muted ? 0.0 : pow(10.0, (double) sound_gain / 20.0);
        const int     in_frames   = size / 2;
        int           out_frames  = (int) (((int64_t) in_frames * target_rate) / freq);
        int16_t      *output;

        if ((out_frames <= 0) || (target_rate <= 0))
            return;

        output = (int16_t *) calloc((size_t) out_frames * 2, sizeof(int16_t));
        if (output == NULL)
            return;

        for (int i = 0; i < out_frames; i++) {
            int ind = (int) (((int64_t) i * freq) / target_rate);

            if (ind >= in_frames)
                ind = in_frames - 1;

            for (int ch = 0; ch < 2; ch++) {
                double v;

                if (sound_is_float)
                    v = 32767.0 * (double) ((const float *) buf)[ind * 2 + ch];
                else
                    v = (double) ((const int16_t *) buf)[ind * 2 + ch];

                v *= vol;
                if (v > 32767.0)
                    v = 32767.0;
                if (v < -32768.0)
                    v = -32768.0;

                output[i * 2 + ch] = (int16_t) v;
            }
        }

        sio_write(source->hdl, output, (size_t) out_frames * 2 * sizeof(int16_t));

        free(output);
    }
}

/* sndio needs no global state, but the backend layer wants a handle. */
static void *
sndio_init(void)
{
    return calloc(1, sizeof(int));
}

static void
sndio_close(void *priv)
{
    free(priv);
}

static void *
sndio_init_source(UNUSED(void *priv), const int sample_rate, UNUSED(int buffer_size))
{
    const char     *devname = (sound_output_device[0] != '\0') ? sound_output_device : SIO_DEVANY;
    sndio_source_t *source  = (sndio_source_t *) calloc(1, sizeof(sndio_source_t));
    unsigned int    rate;
    unsigned int    max_frames;

    source->sample_rate = sample_rate;
    source->hdl         = sio_open(devname, SIO_PLAY, 0);

    /* The configured device may belong to another backend or no longer exist. */
    if ((source->hdl == NULL) && (devname != SIO_DEVANY))
        source->hdl = sio_open(SIO_DEVANY, SIO_PLAY, 0);

    if (source->hdl == NULL) {
        free(source);
        return NULL;
    }

    sio_getpar(source->hdl, &source->par);
    rate       = source->par.rate;
    max_frames = source->par.bufsz;

    sio_initpar(&source->par);
    source->par.sig      = 1;
    source->par.bits     = 16;
    source->par.pchan    = 2;
    source->par.rate     = rate;
    source->par.appbufsz = max_frames;
    sio_setpar(source->hdl, &source->par);
    sio_getpar(source->hdl, &source->par);

    if (!sio_start(source->hdl)) {
        sio_close(source->hdl);
        free(source);
        return NULL;
    }

    return source;
}

static void
sndio_close_source(UNUSED(void *priv), void *src)
{
    sndio_source_t *source = (sndio_source_t *) src;

    if (source != NULL) {
        if (source->hdl != NULL)
            sio_close(source->hdl);

        free(source);
    }
}

static void *
sndio_init_input(UNUSED(void *priv), const int sample_rate)
{
    const char     *devname = (sound_input_dev_name[0] != '\0') ? sound_input_dev_name : SIO_DEVANY;
    sndio_input_t  *in      = (sndio_input_t *) calloc(1, sizeof(sndio_input_t));
    struct sio_par  par;

    in->hdl = sio_open(devname, SIO_REC, 1);
    if ((in->hdl == NULL) && (devname != SIO_DEVANY))
        in->hdl = sio_open(SIO_DEVANY, SIO_REC, 1);
    if (in->hdl == NULL) {
        free(in);
        return NULL;
    }

    sio_initpar(&par);
    par.sig   = 1;
    par.bits  = 16;
    par.bps   = 2;
    par.le    = SIO_LE_NATIVE;
    par.rchan = 2;
    par.rate  = (unsigned int) sample_rate;

    if (!sio_setpar(in->hdl, &par) || !sio_getpar(in->hdl, &par) ||
        (par.bits != 16) || (par.bps != 2) || !par.sig || (par.rchan != 2) ||
        (par.le != SIO_LE_NATIVE)) {
        sio_close(in->hdl);
        free(in);
        return NULL;
    }

    in->rate = (int) par.rate;

    return in;
}

static void
sndio_close_input(UNUSED(void *priv), void *input)
{
    sndio_input_t *in = (sndio_input_t *) input;

    if (in != NULL) {
        sio_close(in->hdl);
        free(in);
    }
}

static void
sndio_start_input(UNUSED(void *priv), void *input)
{
    const sndio_input_t *in = (sndio_input_t *) input;

    if (in != NULL)
        sio_start(in->hdl);
}

static void
sndio_stop_input(UNUSED(void *priv), void *input)
{
    const sndio_input_t *in = (sndio_input_t *) input;

    if (in != NULL)
        sio_stop(in->hdl);
}

static int
sndio_get_input_rate(UNUSED(void *priv), void *input)
{
    const sndio_input_t *in = (sndio_input_t *) input;

    return (in != NULL) ? in->rate : 0;
}

static void
sndio_get_input_data(UNUSED(void *priv), void *input, int16_t *buf, size_t *len)
{
    const sndio_input_t *in = (sndio_input_t *) input;
    size_t               got;

    if ((in == NULL) || sio_eof(in->hdl)) {
        *len = 0;
        return;
    }

    /* The handle is non-blocking: this returns 0 when nothing is available. */
    got  = sio_read(in->hdl, buf, *len * 2 * sizeof(int16_t));
    *len = got / (2 * sizeof(int16_t));
}

const sound_backend_t sound_backend_sndio = {
    .name                       = "sndio",
    .internal_name              = "sndio",
    .give_buffer                = sndio_give_buffer,
    .init_source                = sndio_init_source,
    .close_source               = sndio_close_source,
    .get_output_devices         = sndio_get_output_devices,
    .init                       = sndio_init,
    .close                      = sndio_close,
    .get_input_devices          = sndio_get_input_devices,
    .init_input                 = sndio_init_input,
    .close_input                = sndio_close_input,
    .start_input                = sndio_start_input,
    .stop_input                 = sndio_stop_input,
    .get_input_rate             = sndio_get_input_rate,
    .get_input_data             = sndio_get_input_data,
    .get_device_supported_rates = sndio_get_supported_rates
};
