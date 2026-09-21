/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Interface to the OpenAL sound processing library.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#undef AL_API
#undef ALC_API
#define AL_LIBTYPE_STATIC
#define ALC_LIBTYPE_STATIC

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"
#include <86box/86box.h>
#include <86box/sound.h>
#include <86box/plat_unused.h>

typedef struct _openal_source_ {
    ALuint      source;
    int         sample_rate;

    ALuint      buffers[4];    /* Front and back buffers. */
} openal_source_t;

typedef struct _openal_ {
    ALCcontext *Context;
    ALCdevice  *Device;
} openal_t;

static void
openal_give_buffer(void *priv, void *src, const void *buf, const int size, UNUSED(int gain))
{
    const openal_t *       dev    = (openal_t *) priv;
    const openal_source_t *source = (openal_source_t *) src;

    if (!fast_forward && (dev != NULL) && (source != NULL)) {
        int    processed;
        int    state;
        ALuint buffer;

        alGetSourcei(source->source, AL_SOURCE_STATE, &state);

        if (state == 0x1014)
            alSourcePlay(source->source);

        alGetSourcei(source->source, AL_BUFFERS_PROCESSED, &processed);

        if (processed >= 1) {
            const double master_gain = (sound_muted) ? 0.0 : pow(10.0, (double) sound_gain / 20.0);
            alListenerf(AL_GAIN, (float) master_gain);

            alSourceUnqueueBuffers(source->source, 1, &buffer);

            if (sound_is_float)
                alBufferData(buffer, AL_FORMAT_STEREO_FLOAT32, buf, size * (int) sizeof(float), source->sample_rate);
            else
                alBufferData(buffer, AL_FORMAT_STEREO16, buf, size * (int) sizeof(int16_t), source->sample_rate);

            alSourceQueueBuffers(source->source, 1, &buffer);
        }
    }
}

static void *
openal_init_source(void *priv, int sample_rate, int buffer_size)
{
    float   *        buf    = { 0 };
    int16_t *        buf16  = { 0 };

    openal_source_t *source = (openal_source_t *) calloc(1, sizeof(openal_source_t));

    if (sound_is_float)
        buf   = (float *) calloc(buffer_size, sizeof(float));
    else
        buf16 = (int16_t *) calloc(buffer_size, sizeof(int16_t));

    alGenBuffers(4, source->buffers);

    /* Create source. */
    alGenSources(1, &source->source);

    alSource3f(source->source, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSource3f(source->source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    alSource3f(source->source, AL_DIRECTION, 0.0f, 0.0f, 0.0f);
    alSourcef(source->source, AL_ROLLOFF_FACTOR, 0.0f);
    alSourcei(source->source, AL_SOURCE_RELATIVE, AL_TRUE);

    source->sample_rate = sample_rate;

    if (sound_is_float)
        memset(buf, 0x00, buffer_size * sizeof(float));
    else
        memset(buf16, 0x00, buffer_size * sizeof(int16_t));

    for (uint8_t c = 0; c < 4; c++) {
        if (sound_is_float)
            alBufferData(source->buffers[c], AL_FORMAT_STEREO_FLOAT32,
                         buf, (int) buffer_size * (int) sizeof(float),
                         (int) sample_rate);
        else
            alBufferData(source->buffers[c], AL_FORMAT_STEREO16,
                         buf16, (int) buffer_size * (int) sizeof(int16_t),
                         (int) sample_rate);
    }

    alSourceQueueBuffers(source->source, 4, source->buffers);
    alSourcePlay(source->source);

    if (sound_is_float)
        free(buf);
    else
        free(buf16);

    return source;
}

static void
openal_close_source(UNUSED(void *priv), void *src)
{
    openal_source_t *source = (openal_source_t *) src;

    alSourceStopv(1, &source->source);

    alDeleteSources(1, &source->source);

    alDeleteBuffers(4, source->buffers);

    free(source);
}

static const char *
openal_get_output_devices(void)
{
    const char *output_devices = NULL;

    if (alcIsExtensionPresent(NULL, "ALC_ENUMERATE_ALL_EXT"))
        output_devices = alcGetString(NULL, ALC_ALL_DEVICES_SPECIFIER);
    else if (alcIsExtensionPresent(NULL, "ALC_ENUMERATION_EXT"))
        output_devices = alcGetString(NULL, ALC_DEVICE_SPECIFIER);

    return output_devices;
}

static void
openal_close(void *priv)
{
    const openal_t *dev = (openal_t *) priv;

    if (dev != NULL) {
        if (dev->Context != NULL) {
            /* Disable context */
            alcMakeContextCurrent(NULL);

            /* Release context(s) */
            alcDestroyContext(dev->Context);

            if (dev->Device != NULL) {
                /* Close device */
                alcCloseDevice(dev->Device);
            }
        }
    }
}

static void *
openal_init(void)
{
    openal_t *dev = (openal_t *) calloc(1, sizeof(openal_t));

    /* Open device: use the user-selected device, or NULL for system default */
    const ALCchar *dev_name = (sound_output_device[0] != '\0') ? sound_output_device : NULL;

    dev->Device = alcOpenDevice(dev_name);
    if (dev->Device != NULL) {
        /* Create context(s) */
        dev->Context = alcCreateContext(dev->Device, NULL);

        if (dev->Context != NULL) {
            /* Set active context */
            alcMakeContextCurrent(dev->Context);
        }
    }

    return dev;
}

const sound_backend_t sound_backend_openal = {
    .name               = "OpenAL",
    .internal_name      = "openal",
    .give_buffer        = openal_give_buffer,
    .init_source        = openal_init_source,
    .close_source       = openal_close_source,
    .get_output_devices = openal_get_output_devices,
    .init               = openal_init,
    .close              = openal_close
};
