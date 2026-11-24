/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Interface to audio(4).
 *
 * Authors: Nishi
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2025 Nishi.
 *          Copyright 2026 Miran Grca.
 */
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
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
#include <86box/runctl.h>

#if defined(OpenBSD) && OpenBSD >= 201709
#    define USE_NEW_API
#endif

#if defined(__NetBSD__) && (__NetBSD_Version__ >= 900000000)
#    define AUDIO4_GETINFO AUDIO_GETFORMAT
#else
#    define AUDIO4_GETINFO AUDIO_GETINFO
#endif

typedef struct _audio4_source_ {
    int                fd;
    int                sample_rate;
    int                target_rate;
} audio4_source_t;

typedef struct _audio4_input_ {
    int                fd;
    int                rate;
} audio4_input_t;

static const char *
audio4_get_devices(void)
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

/* Opens the selected device, or the default one if none is selected. */
static int
audio4_open(const char *device_name, const int flags)
{
    int fd;

    if (device_name[0] != '\0')
        return open(device_name, flags);

    fd = open("/dev/audio", flags);
    if (fd == -1)
        fd = open("/dev/audio0", flags);

    return fd;
}

static int
audio4_get_supported_rates(const char *device_name, int *rates_out, const int max_rates)
{
    static const int candidates[] = { FREQ_44100, FREQ_48000 };
    const int        num_cands    = (int) (sizeof(candidates) / sizeof(candidates[0]));
    int              count        = 0;

    for (int i = 0; (i < num_cands) && (count < max_rates); i++) {
        const int fd = audio4_open((device_name != NULL) ? device_name : "", O_WRONLY | O_NONBLOCK);
        int       got = 0;

        if (fd == -1)
            continue;

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
            ioctl(fd, AUDIO4_GETINFO, &ai);
            got = (int) ai.play.sample_rate;
        }
#endif
        close(fd);

        if (got == candidates[i])
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
audio4_give_buffer(UNUSED(void *priv), void *src, const void *buf, const int size, UNUSED(int gain))
{
    const audio4_source_t *source = (audio4_source_t *) src;

    if ((source != NULL) && (source->fd != -1) && (runctl_mode() != RUN_MODE_FAST)) {
        const int    freq        = source->sample_rate;
        const int    target_rate = source->target_rate;
        const double vol         = sound_muted ? 0.0 : pow(10.0, (double) sound_gain / 20.0);
        const int    in_frames   = size / 2;
        const int    out_frames  = (target_rate > 0) ? (int) (((int64_t) in_frames * target_rate) / freq) : 0;
        int16_t     *output;

        if (out_frames <= 0)
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

        (void) write(source->fd, output, (size_t) out_frames * 2 * sizeof(int16_t));

        free(output);
    }
}

static void *
audio4_init_source(UNUSED(void *priv), const int sample_rate, UNUSED(int buffer_size))
{
    audio4_source_t *source = (audio4_source_t *) calloc(1, sizeof(audio4_source_t));

    source->sample_rate = sample_rate;
    source->fd          = audio4_open(sound_output_device, O_WRONLY);

    if (source->fd == -1) {
        free(source);
        return NULL;
    }

#ifdef USE_NEW_API
    struct audio_swpar info;

    AUDIO_INITPAR(&info);
    ioctl(source->fd, AUDIO_GETPAR, &info);
    info.sig   = 1;
    info.bits  = 16;
    info.pchan = 2;
    info.bps   = 2;
    ioctl(source->fd, AUDIO_SETPAR, &info);
    source->target_rate = (int) info.rate;
#else
    audio_info_t info;

    AUDIO_INITINFO(&info);
    ioctl(source->fd, AUDIO4_GETINFO, &info);
    info.play.channels  = 2;
    info.play.precision = 16;
    info.play.encoding  = AUDIO_ENCODING_SLINEAR;
    info.hiwat          = 5;
    info.lowat          = 3;
    ioctl(source->fd, AUDIO_SETINFO, &info);
    source->target_rate = (int) info.play.sample_rate;
#endif

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

static void *
audio4_init_input(UNUSED(void *priv), const int sample_rate)
{
    audio4_input_t *in = (audio4_input_t *) calloc(1, sizeof(audio4_input_t));

    in->fd = audio4_open(sound_input_dev_name, O_RDONLY | O_NONBLOCK);

    if (in->fd == -1) {
        free(in);
        return NULL;
    }

#ifdef USE_NEW_API
    struct audio_swpar info;

    AUDIO_INITPAR(&info);
    ioctl(in->fd, AUDIO_GETPAR, &info);
    info.sig   = 1;
    info.bits  = 16;
    info.bps   = 2;
    info.rchan = 2;
    info.rate  = (unsigned int) sample_rate;
    if ((ioctl(in->fd, AUDIO_SETPAR, &info) == -1) ||
        (ioctl(in->fd, AUDIO_GETPAR, &info) == -1) ||
        (info.bits != 16) || (info.rchan != 2)) {
        close(in->fd);
        free(in);
        return NULL;
    }
    in->rate = (int) info.rate;
#else
    audio_info_t info;

    AUDIO_INITINFO(&info);
    info.record.sample_rate = (unsigned int) sample_rate;
    info.record.channels    = 2;
    info.record.precision   = 16;
    info.record.encoding    = AUDIO_ENCODING_SLINEAR;
    if (ioctl(in->fd, AUDIO_SETINFO, &info) == -1) {
        close(in->fd);
        free(in);
        return NULL;
    }
    AUDIO_INITINFO(&info);
    ioctl(in->fd, AUDIO4_GETINFO, &info);
    if ((info.record.channels != 2) || (info.record.precision != 16)) {
        close(in->fd);
        free(in);
        return NULL;
    }
    in->rate = (int) info.record.sample_rate;
#endif

    return in;
}

static void
audio4_close_input(UNUSED(void *priv), void *input)
{
    audio4_input_t *in = (audio4_input_t *) input;

    if (in != NULL) {
        close(in->fd);
        free(in);
    }
}

static int
audio4_get_input_rate(UNUSED(void *priv), void *input)
{
    const audio4_input_t *in = (audio4_input_t *) input;

    return (in != NULL) ? in->rate : 0;
}

static void
audio4_get_input_data(UNUSED(void *priv), void *input, int16_t *buf, size_t *len)
{
    const audio4_input_t *in = (audio4_input_t *) input;
    ssize_t               got;

    if (in == NULL) {
        *len = 0;
        return;
    }

    /* The descriptor is non-blocking: nothing available is not an error. */
    got  = read(in->fd, buf, *len * 2 * sizeof(int16_t));
    *len = (got > 0) ? ((size_t) got / (2 * sizeof(int16_t))) : 0;
}

/* audio(4) needs no global state, but the backend layer wants a handle. */
static void *
audio4_init(void)
{
    return calloc(1, sizeof(int));
}

static void
audio4_close(void *priv)
{
    free(priv);
}

const sound_backend_t sound_backend_audio4 = {
    .name                       = "audio(4)",
    .internal_name              = "audio4",
    .give_buffer                = audio4_give_buffer,
    .init_source                = audio4_init_source,
    .close_source               = audio4_close_source,
    .get_output_devices         = audio4_get_devices,
    .init                       = audio4_init,
    .close                      = audio4_close,
    .get_input_devices          = audio4_get_devices,
    .init_input                 = audio4_init_input,
    .close_input                = audio4_close_input,
    .start_input                = NULL,
    .stop_input                 = NULL,
    .get_input_rate             = audio4_get_input_rate,
    .get_input_data             = audio4_get_input_data,
    .get_device_supported_rates = audio4_get_supported_rates
};
