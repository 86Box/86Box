/*
 * 86Box     A hypervisor and IBM PC system emulator that specializes in
 *           running old operating systems and software designed for IBM
 *           PC systems and compatibles from 1981 through fairly recent
 *           system designs based on the PCI bus.
 *
 *           This file is part of the 86Box distribution.
 *
 *           Interface to the OpenAL sound processing library.
 *
 *
 *
 * Authors:  Sarah Walker, <https://pcem-emulator.co.uk/>
 *           Miran Grca, <mgrca8@gmail.com>
 *
 *           Copyright 2008-2019 Sarah Walker.
 *           Copyright 2016-2019 Miran Grca.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
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

#define FREQ   SOUND_FREQ
#define BUFLEN SOUNDBUFLEN

ALuint        buffers[4];       /* front and back buffers */
ALuint        buffers_music[4]; /* front and back buffers */
ALuint        buffers_cd[4];    /* front and back buffers */
ALuint        buffers_midi[4];  /* front and back buffers */
static ALuint source[4];        /* audio source */

static int         midi_freq     = 44100;
static int         midi_buf_size = 4410;
static int         initialized   = 0;
static int         sources       = 2;
static ALCcontext *Context;
static ALCdevice  *Device;

void
al_set_midi(int freq, int buf_size)
{
    midi_freq     = freq;
    midi_buf_size = buf_size;
}

void closeal(void);
ALvoid
alutInit(UNUSED(ALint *argc), UNUSED(ALbyte **argv))
{
    /* Open device */
    Device = alcOpenDevice((ALCchar *) "");
    if (Device != NULL) {
        /* Create context(s) */
        Context = alcCreateContext(Device, NULL);
        if (Context != NULL) {
            /* Set active context */
            alcMakeContextCurrent(Context);
        }
    }
}

ALvoid
alutExit(ALvoid)
{
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

    if (sources == 4)
        alDeleteBuffers(4, buffers_midi);
    alDeleteBuffers(4, buffers_cd);
    alDeleteBuffers(4, buffers_music);
    alDeleteBuffers(4, buffers);

    alutExit();

    initialized = 0;
}

void
inital(void)
{
    float   *buf             = NULL;
    float   *music_buf       = NULL;
    float   *cd_buf          = NULL;
    float   *midi_buf        = NULL;
    int16_t *buf_int16       = NULL;
    int16_t *music_buf_int16 = NULL;
    int16_t *cd_buf_int16    = NULL;
    int16_t *midi_buf_int16  = NULL;

    const char *mdn;
    int         init_midi = 0;

    if (initialized)
        return;

    alutInit(0, 0);
    atexit(closeal);

    mdn = midi_out_device_get_internal_name(midi_output_device_current);
    if (strcmp(mdn, "none") && strcmp(mdn, SYSTEM_MIDI_INTERNAL_NAME))
        init_midi = 1; /* If the device is neither none, nor system MIDI, initialize the
                MIDI buffer and source, otherwise, do not. */
    sources = 3 + !!init_midi;

    if (sound_is_float) {
        buf       = (float *) calloc((BUFLEN << 1), sizeof(float));
        music_buf = (float *) calloc((MUSICBUFLEN << 1), sizeof(float));
        cd_buf    = (float *) calloc((CD_BUFLEN << 1), sizeof(float));
        if (init_midi)
            midi_buf = (float *) calloc(midi_buf_size, sizeof(float));
    } else {
        buf_int16       = (int16_t *) calloc((BUFLEN << 1), sizeof(int16_t));
        music_buf_int16 = (int16_t *) calloc((MUSICBUFLEN << 1), sizeof(int16_t));
        cd_buf_int16    = (int16_t *) calloc((CD_BUFLEN << 1), sizeof(int16_t));
        if (init_midi)
            midi_buf_int16 = (int16_t *) calloc(midi_buf_size, sizeof(int16_t));
    }

    alGenBuffers(4, buffers);
    alGenBuffers(4, buffers_cd);
    alGenBuffers(4, buffers_music);
    if (init_midi)
        alGenBuffers(4, buffers_midi);

    if (init_midi)
        alGenSources(4, source);
    else
        alGenSources(3, source);

    alSource3f(source[0], AL_POSITION, 0.0, 0.0, 0.0);
    alSource3f(source[0], AL_VELOCITY, 0.0, 0.0, 0.0);
    alSource3f(source[0], AL_DIRECTION, 0.0, 0.0, 0.0);
    alSourcef(source[0], AL_ROLLOFF_FACTOR, 0.0);
    alSourcei(source[0], AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(source[1], AL_POSITION, 0.0, 0.0, 0.0);
    alSource3f(source[1], AL_VELOCITY, 0.0, 0.0, 0.0);
    alSource3f(source[1], AL_DIRECTION, 0.0, 0.0, 0.0);
    alSourcef(source[1], AL_ROLLOFF_FACTOR, 0.0);
    alSourcei(source[1], AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(source[2], AL_POSITION, 0.0, 0.0, 0.0);
    alSource3f(source[2], AL_VELOCITY, 0.0, 0.0, 0.0);
    alSource3f(source[2], AL_DIRECTION, 0.0, 0.0, 0.0);
    alSourcef(source[2], AL_ROLLOFF_FACTOR, 0.0);
    alSourcei(source[2], AL_SOURCE_RELATIVE, AL_TRUE);
    if (init_midi) {
        alSource3f(source[3], AL_POSITION, 0.0, 0.0, 0.0);
        alSource3f(source[3], AL_VELOCITY, 0.0, 0.0, 0.0);
        alSource3f(source[3], AL_DIRECTION, 0.0, 0.0, 0.0);
        alSourcef(source[3], AL_ROLLOFF_FACTOR, 0.0);
        alSourcei(source[3], AL_SOURCE_RELATIVE, AL_TRUE);
    }

    if (sound_is_float) {
        memset(buf, 0, BUFLEN * 2 * sizeof(float));
        memset(cd_buf, 0, CD_BUFLEN * 2 * sizeof(float));
        memset(music_buf, 0, MUSICBUFLEN * 2 * sizeof(float));
        if (init_midi)
            memset(midi_buf, 0, midi_buf_size * sizeof(float));
    } else {
        memset(buf_int16, 0, BUFLEN * 2 * sizeof(int16_t));
        memset(cd_buf_int16, 0, CD_BUFLEN * 2 * sizeof(int16_t));
        memset(music_buf_int16, 0, MUSICBUFLEN * 2 * sizeof(int16_t));
        if (init_midi)
            memset(midi_buf_int16, 0, midi_buf_size * sizeof(int16_t));
    }

    for (uint8_t c = 0; c < 4; c++) {
        if (sound_is_float) {
            alBufferData(buffers[c], AL_FORMAT_STEREO_FLOAT32, buf, BUFLEN * 2 * sizeof(float), FREQ);
            alBufferData(buffers_music[c], AL_FORMAT_STEREO_FLOAT32, music_buf, MUSICBUFLEN * 2 * sizeof(float), MUSIC_FREQ);
            alBufferData(buffers_cd[c], AL_FORMAT_STEREO_FLOAT32, cd_buf, CD_BUFLEN * 2 * sizeof(float), CD_FREQ);
            if (init_midi)
                alBufferData(buffers_midi[c], AL_FORMAT_STEREO_FLOAT32, midi_buf, midi_buf_size * sizeof(float), midi_freq);
        } else {
            alBufferData(buffers[c], AL_FORMAT_STEREO16, buf_int16, BUFLEN * 2 * sizeof(int16_t), FREQ);
            alBufferData(buffers_music[c], AL_FORMAT_STEREO16, music_buf_int16, MUSICBUFLEN * 2 * sizeof(int16_t), MUSIC_FREQ);
            alBufferData(buffers_cd[c], AL_FORMAT_STEREO16, cd_buf_int16, CD_BUFLEN * 2 * sizeof(int16_t), CD_FREQ);
            if (init_midi)
                alBufferData(buffers_midi[c], AL_FORMAT_STEREO16, midi_buf_int16, midi_buf_size * sizeof(int16_t), midi_freq);
        }
    }

    alSourceQueueBuffers(source[0], 4, buffers);
    alSourceQueueBuffers(source[1], 4, buffers_music);
    alSourceQueueBuffers(source[2], 4, buffers_cd);
    if (init_midi)
        alSourceQueueBuffers(source[3], 4, buffers_midi);
    alSourcePlay(source[0]);
    alSourcePlay(source[1]);
    alSourcePlay(source[2]);
    if (init_midi)
        alSourcePlay(source[3]);

    if (sound_is_float) {
        if (init_midi)
            free(midi_buf);
        free(cd_buf);
        free(music_buf);
        free(buf);
    } else {
        if (init_midi)
            free(midi_buf_int16);
        free(cd_buf_int16);
        free(music_buf_int16);
        free(buf_int16);
    }

    initialized = 1;
}

void
givealbuffer_common(void *buf, uint8_t src, int size, int freq)
{
    int    processed;
    int    state;
    ALuint buffer;
    double gain;

    if (!initialized)
        return;

    alGetSourcei(source[src], AL_SOURCE_STATE, &state);

    if (state == 0x1014) {
        alSourcePlay(source[src]);
    }

    alGetSourcei(source[src], AL_BUFFERS_PROCESSED, &processed);
    if (processed >= 1) {
        gain = pow(10.0, (double) sound_gain / 20.0);
        alListenerf(AL_GAIN, gain);

        alSourceUnqueueBuffers(source[src], 1, &buffer);

        if (sound_is_float)
            alBufferData(buffer, AL_FORMAT_STEREO_FLOAT32, buf, size * sizeof(float), freq);
        else
            alBufferData(buffer, AL_FORMAT_STEREO16, buf, size * sizeof(int16_t), freq);

        alSourceQueueBuffers(source[src], 1, &buffer);
    }
}

void
givealbuffer(void *buf)
{
    givealbuffer_common(buf, 0, BUFLEN << 1, FREQ);
}

void
givealbuffer_music(void *buf)
{
    givealbuffer_common(buf, 1, MUSICBUFLEN << 1, MUSIC_FREQ);
}

void
givealbuffer_cd(void *buf)
{
    givealbuffer_common(buf, 2, CD_BUFLEN << 1, CD_FREQ);
}

void
givealbuffer_midi(void *buf, uint32_t size)
{
    givealbuffer_common(buf, 3, size, midi_freq);
}
