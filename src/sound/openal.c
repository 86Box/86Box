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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#undef AL_API
#undef ALC_API
#define AL_LIBTYPE_STATIC
#define ALC_LIBTYPE_STATIC

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"
#include <86box/86box.h>
#include <86box/midi.h>
#include <86box/sound.h>
#include <86box/plat_unused.h>
#include <86box/thread.h>

ALuint                    buffers[I_MAX][4]; /* front and back buffers */
static ALuint             source[I_MAX];     /* audio sources */

static int                initialized       = 0;
static int                atexit_registered = 0;
static int                sources           = 2;
static ALCcontext *       Context;
static ALCdevice  *       Device;
static ALCdevice  *       CaptureDevice;
static int         CaptureChannels = 2;
static int         CaptureRate     = 0;
static mutex_t *capture_mutex = NULL;

#define SNDIN_CAPTURE_BUFLEN (SOUNDBUFLEN * 16)

static unsigned long long buf_sizes[I_MAX] = {
    0, (MUSICBUFLEN << 1), (WTBUFLEN << 1),     (CD_BUFLEN << 1),
    0, 0,                  (YM2151BUFLEN << 1), 0
};

void
al_set_midi(const int freq, const int buf_size)
{
    midi_freq     = freq;
    midi_buf_size = buf_size;
}

const char *
sound_get_output_devices(void)
{
    if (alcIsExtensionPresent(NULL, "ALC_ENUMERATE_ALL_EXT"))
        return alcGetString(NULL, ALC_ALL_DEVICES_SPECIFIER);
    if (alcIsExtensionPresent(NULL, "ALC_ENUMERATION_EXT"))
        return alcGetString(NULL, ALC_DEVICE_SPECIFIER);
    return NULL;
}

const char *
sound_get_input_devices(void)
{
    if (alcIsExtensionPresent(NULL, "ALC_EXT_CAPTURE"))
        return alcGetString(NULL, ALC_CAPTURE_DEVICE_SPECIFIER);
    return NULL;
}

int
sound_get_device_sample_rate(const char *device_name)
{
    const ALCchar *dev_name = (device_name && device_name[0]) ? device_name : NULL;
    ALCdevice     *dev      = alcOpenDevice(dev_name);
    ALCint         freq     = 0;

    if (dev != NULL) {
        ALCcontext *prev_ctx = alcGetCurrentContext();
        ALCcontext *ctx      = alcCreateContext(dev, NULL);
        if (ctx != NULL) {
            alcMakeContextCurrent(ctx);
            alcGetIntegerv(dev, ALC_FREQUENCY, 1, &freq);
            alcMakeContextCurrent(prev_ctx);
            alcDestroyContext(ctx);
        }
        alcCloseDevice(dev);
    }
    return (int) freq;
}

int
al_capture_get_rate(void)
{
    int rate;

    if (capture_mutex != NULL)
        thread_wait_mutex(capture_mutex);
    rate = CaptureDevice ? CaptureRate : 0;
    if (capture_mutex != NULL)
        thread_release_mutex(capture_mutex);

    return rate;
}

int
sound_get_device_supported_rates(const char *device_name, int *rates_out, int max_rates)
{
    /* Candidate rates: only those where rate/50 <= SOUNDBUFLEN to avoid overflowing
       static device buffers. */
    static const int candidates[] = { FREQ_44100, FREQ_48000 };
    const int        num_cands    = (int) (sizeof(candidates) / sizeof(candidates[0]));

    const ALCchar *dev_name = (device_name && device_name[0]) ? device_name : NULL;
    ALCdevice     *dev      = alcOpenDevice(dev_name);
    int            count    = 0;

    if (dev != NULL) {
        ALCcontext *prev_ctx = alcGetCurrentContext();

        for (int i = 0; i < num_cands && count < max_rates; i++) {
            ALCint      attrs[] = { ALC_FREQUENCY, candidates[i], 0 };
            ALCcontext *ctx     = alcCreateContext(dev, attrs);
            ALCint      got     = 0;

            if (ctx != NULL) {
                alcMakeContextCurrent(ctx);
                alcGetIntegerv(dev, ALC_FREQUENCY, 1, &got);
                alcMakeContextCurrent(prev_ctx);
                alcDestroyContext(ctx);
            }

            if (got == candidates[i])
                rates_out[count++] = candidates[i];
        }

        alcCloseDevice(dev);
    }

    /* Fallback: if detection failed entirely, return all candidates. */
    if (count == 0) {
        for (int i = 0; i < num_cands && i < max_rates; i++)
            rates_out[i] = candidates[i];
        count = num_cands;
    }

    return count;
}

ALvoid
alutInit(UNUSED(ALint *argc), UNUSED(ALbyte **argv))
{
    if (capture_mutex == NULL)
        capture_mutex = thread_create_mutex();

    /* Open device: use the user-selected device, or NULL for system default */
    const ALCchar *dev_name = (sound_output_device[0] != '\0') ? sound_output_device : NULL;
    Device = alcOpenDevice(dev_name);
    if (Device != NULL) {
        /* Create context(s) */
        Context = alcCreateContext(Device, NULL);
        if (Context != NULL) {
            /* Set active context */
            alcMakeContextCurrent(Context);
        }
    }

    CaptureDevice   = NULL;
    CaptureChannels = 2;
    CaptureRate     = 0;

}

void
al_capture_open(void)
{
    if (capture_mutex != NULL)
        thread_wait_mutex(capture_mutex);

    if (CaptureDevice != NULL) {
        if (capture_mutex != NULL)
            thread_release_mutex(capture_mutex);
        return;
    }

    CaptureChannels = 2;
    CaptureRate     = 0;
    if (sound_input_enabled) {
        const ALCchar *cap_name = (sound_input_dev_name[0] != '\0') ? sound_input_dev_name : NULL;

        int            want     = (sb_input_rate == FREQ_48000) ? FREQ_48000
                                                                   : FREQ_44100;

        CaptureRate   = want;
        CaptureDevice = alcCaptureOpenDevice(cap_name, want, AL_FORMAT_STEREO16, SNDIN_CAPTURE_BUFLEN);
        if (CaptureDevice == NULL) {
            CaptureDevice = alcCaptureOpenDevice(cap_name, want, AL_FORMAT_MONO16, SNDIN_CAPTURE_BUFLEN);
            if (CaptureDevice != NULL)
                CaptureChannels = 1;
        }

        if ((CaptureDevice == NULL) && (want != SOUND_FREQ)) {
            CaptureDevice = alcCaptureOpenDevice(cap_name, SOUND_FREQ, AL_FORMAT_STEREO16, SNDIN_CAPTURE_BUFLEN);
            if (CaptureDevice == NULL) {
                CaptureDevice = alcCaptureOpenDevice(cap_name, SOUND_FREQ, AL_FORMAT_MONO16, SNDIN_CAPTURE_BUFLEN);
                if (CaptureDevice != NULL)
                    CaptureChannels = 1;
            }
            if (CaptureDevice != NULL)
                CaptureRate = SOUND_FREQ;
        }

        if (CaptureDevice == NULL)
            CaptureRate = 0;
    }

    if (capture_mutex != NULL)
        thread_release_mutex(capture_mutex);
}

void
al_capture_close(void)
{
    if (capture_mutex != NULL)
        thread_wait_mutex(capture_mutex);

    if (CaptureDevice != NULL) {
        alcCaptureCloseDevice(CaptureDevice);
        CaptureDevice   = NULL;
        CaptureChannels = 2;
        CaptureRate     = 0;
    }

    if (capture_mutex != NULL)
        thread_release_mutex(capture_mutex);
}

ALvoid
alutExit(ALvoid)
{
    al_capture_close();

    if (Context != NULL) {
        /* Disable context */
        alcMakeContextCurrent(NULL);

        /* Release context(s) */
        alcDestroyContext(Context);

        if (Device != NULL) {
            /* Close device */
            alcCloseDevice(Device);
        }
    }
}

void
closeal(void)
{
    if (!initialized)
        return;

    alSourceStopv(sources, source);
    alDeleteSources(sources, source);

    for (int i = (sources - 1); i >= 0; i--)
        alDeleteBuffers(4, buffers[i]);

    alutExit();

    initialized = 0;
}

int
al_capture_available(void)
{
    int available;

    if (capture_mutex != NULL)
        thread_wait_mutex(capture_mutex);
    available = !!CaptureDevice;
    if (capture_mutex != NULL)
        thread_release_mutex(capture_mutex);

    return available;
}

void
al_capture_start(void)
{
    if (capture_mutex != NULL)
        thread_wait_mutex(capture_mutex);
    if (CaptureDevice != NULL)
        alcCaptureStart(CaptureDevice);
    if (capture_mutex != NULL)
        thread_release_mutex(capture_mutex);
}

void
al_capture_stop(void)
{
    if (capture_mutex != NULL)
        thread_wait_mutex(capture_mutex);
    if (CaptureDevice != NULL)
        alcCaptureStop(CaptureDevice);
    if (capture_mutex != NULL)
        thread_release_mutex(capture_mutex);
}

void
al_capture_get_data(int16_t *buf, size_t *len)
{
    ALint availableSamples = 0;

    if (!buf || !len)
        return;

    if (capture_mutex != NULL)
        thread_wait_mutex(capture_mutex);

    if (CaptureDevice == NULL) {
        *len = 0;
        goto unlock;
    }

    alcGetIntegerv(CaptureDevice, ALC_CAPTURE_SAMPLES, 1, &availableSamples);
    if (availableSamples <= 0) {
        *len = 0;
        goto unlock;
    }

    if ((size_t) availableSamples > *len)
        availableSamples = (ALint) *len;

    alcCaptureSamples(CaptureDevice, buf, availableSamples);

    if (CaptureChannels == 1) {
        int i;
        for (i = availableSamples - 1; i >= 0; i--) {
            buf[i * 2]     = buf[i];
            buf[i * 2 + 1] = buf[i];
        }
    }

    *len = (size_t) availableSamples;

unlock:
    if (capture_mutex != NULL)
        thread_release_mutex(capture_mutex);
}

void
inital(void)
{
    float   *buf[I_MAX]   = { 0 };
    int16_t *buf16[I_MAX] = { 0 };

    int init_midi         = 0;

    if (initialized)
        return;

    alutInit(0, 0);

    if (!atexit_registered) {
        atexit(closeal);
        atexit_registered = 1;
    }

    const char *mdn = midi_out_device_get_internal_name(midi_output_device_current);
    if ((strcmp(mdn, "none") != 0) && (strcmp(mdn, SYSTEM_MIDI_INTERNAL_NAME) != 0))
        init_midi = 1; /* If the device is neither none, nor system MIDI, initialize the
                          MIDI buffer and source, otherwise, do not. */

    const int pcm_buf_len = sound_sample_rate / 50;

    buf_sizes[I_NORMAL] = buf_sizes[I_FDD] = buf_sizes[I_HDD] = (pcm_buf_len << 1);
    src_freqs[I_NORMAL] = src_freqs[I_FDD] = src_freqs[I_HDD] = sound_sample_rate;

    if (init_midi) {
        buf_sizes[I_MIDI] = midi_buf_size;
        src_freqs[I_MIDI]     = midi_freq;
    }

    sources = I_MIDI + !!init_midi;
    if (sound_is_float) {
        for (int i = 0; i < sources; i++)
            buf[i]   = (float *) calloc(buf_sizes[i], sizeof(float));
    } else {
        for (int i = 0; i < sources; i++)
            buf16[i] = (int16_t *) calloc(buf_sizes[i], sizeof(int16_t));
    }

    for (int i = 0; i < sources; i++)
       alGenBuffers(4, buffers[i]);

    // Create sources: 0=main, 1=music, 2=wt, 3=cd, 4=fdd, 5=hdd, 6=midi(optional)
    if (init_midi)
        alGenSources(8, source);
    else
        alGenSources(7, source);

    for (int i = 0; i < sources; i++) {
        alSource3f(source[i], AL_POSITION, 0.0f, 0.0f, 0.0f);
        alSource3f(source[i], AL_VELOCITY, 0.0f, 0.0f, 0.0f);
        alSource3f(source[i], AL_DIRECTION, 0.0f, 0.0f, 0.0f);
        alSourcef(source[i], AL_ROLLOFF_FACTOR, 0.0f);
        alSourcei(source[i], AL_SOURCE_RELATIVE, AL_TRUE);
    }

    if (sound_is_float) {
        for (int i = 0; i < sources; i++)
            memset(buf[i], 0x00, buf_sizes[i] * sizeof(float));
    } else {
        for (int i = 0; i < sources; i++)
            memset(buf16[i], 0x00, buf_sizes[i] * sizeof(int16_t));
    }

    for (uint8_t c = 0; c < 4; c++) {
        if (sound_is_float) {
            for (int i = 0; i < sources; i++)
                alBufferData(buffers[i][c], AL_FORMAT_STEREO_FLOAT32, buf[i], (int) buf_sizes[i] * (int) sizeof(float), (int) src_freqs[i]);
        } else {
            for (int i = 0; i < sources; i++)
                alBufferData(buffers[i][c], AL_FORMAT_STEREO16, buf16[i], (int) buf_sizes[i] * (int) sizeof(int16_t), (int) src_freqs[i]);
        }
    }

    for (int i = 0; i < sources; i++) {
        alSourceQueueBuffers(source[i], 4, buffers[i]);
        alSourcePlay(source[i]);
    }

    if (sound_is_float) {
        for (int i = (sources - 1); i >= 0; i--)
            free(buf[i]);
    } else {
        for (int i = (sources - 1); i >= 0; i--)
            free(buf16[i]);
    }

    initialized = 1;
}

void
givealbuffer_common(const void *buf, const uint8_t src, const int size)
{
    int    processed;
    int    state;
    ALuint buffer;

    if (!initialized || fast_forward)
        return;

    alGetSourcei(source[src], AL_SOURCE_STATE, &state);

    if (state == 0x1014) {
        alSourcePlay(source[src]);
    }

    alGetSourcei(source[src], AL_BUFFERS_PROCESSED, &processed);
    if (processed >= 1) {
        const double gain = (sound_muted) ? 0.0 : pow(10.0, (double) sound_gain / 20.0);
        alListenerf(AL_GAIN, (float) gain);

        alSourceUnqueueBuffers(source[src], 1, &buffer);

        if (sound_is_float)
            alBufferData(buffer, AL_FORMAT_STEREO_FLOAT32, buf, size * (int) sizeof(float), (int) src_freqs[src]);
        else
            alBufferData(buffer, AL_FORMAT_STEREO16, buf, size * (int) sizeof(int16_t), (int) src_freqs[src]);

        alSourceQueueBuffers(source[src], 1, &buffer);
    }
}
