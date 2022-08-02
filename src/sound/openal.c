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
 * Authors:  Sarah Walker, <http://pcem-emulator.co.uk/>
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

#define FREQ   48000
#define BUFLEN SOUNDBUFLEN

static int         midi_freq     = 44100;
static int         midi_buf_size = 4410;
static int         initialized   = 0;
static int         sources       = 2;
static ALCcontext *Context;
static ALCdevice  *Device;

struct al_source
{
    ALuint buffers[4];
    ALuint source;
    uint32_t buffersize, freq;
    al_source* next;
} *midi_source = NULL;

static al_source* first_al_source;

void
al_set_midi(int freq, int buf_size)
{
    midi_freq     = freq;
    midi_buf_size = buf_size;
}

void closeal(void);
ALvoid
alutInit(ALint *argc, ALbyte **argv)
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
    al_source* source = first_al_source;

    if (!initialized)
        return;

    while (source) {
        al_source* orig_source = source;
        alSourceStop(source->source);
        alDeleteSources(1, &source->source);
        alDeleteBuffers(4, source->buffers);
        source = source->next;
        free(orig_source);
    }

    midi_source = first_al_source = NULL;
    alutExit();

    initialized = 0;
}

al_source*
al_create_sound_source(uint32_t freq, uint32_t buffer_size_mono)
{
    void* soundbuf = NULL;
    int c = 0;
    al_source* new_source = calloc(sizeof(al_source), 1);
    if (!new_source) return NULL;

    if (sound_is_float)
        soundbuf = calloc(buffer_size_mono, 2 * sizeof(float));
    else
        soundbuf = calloc(buffer_size_mono, 2 * sizeof(int16_t));

    if (soundbuf == NULL) {
        free(new_source);
        return NULL;
    }

    alGenBuffers(4, new_source->buffers);
    alGenSources(1, &new_source->source);
    alSource3f(new_source->source, AL_POSITION, 0.0, 0.0, 0.0);
    alSource3f(new_source->source, AL_VELOCITY, 0.0, 0.0, 0.0);
    alSource3f(new_source->source, AL_DIRECTION, 0.0, 0.0, 0.0);
    alSourcef(new_source->source, AL_ROLLOFF_FACTOR, 0.0);
    alSourcei(new_source->source, AL_SOURCE_RELATIVE, AL_TRUE);
    for (c = 0; c < 4; c++)
        alBufferData(new_source->buffers[c], sound_is_float ? AL_FORMAT_STEREO_FLOAT32 : AL_FORMAT_STEREO16, soundbuf, BUFLEN * 2 * (sound_is_float ? sizeof(float) : sizeof(int16_t)), freq);

    alSourceQueueBuffers(new_source->source, 4, new_source->buffers);
    alSourcePlay(new_source->source);
    if (!first_al_source) first_al_source = new_source;
    else {
        al_source* current_source = first_al_source;
        while (current_source) { 
            if (current_source->next == NULL) break;
            current_source = current_source->next;
        }
        current_source->next = new_source;
    }
    free(soundbuf);
    return new_source;
}

void
inital(void)
{
    int      c;

    char *mdn;
    int   init_midi = 0;

    if (initialized)
        return;

    alutInit(0, 0);
    atexit(closeal);

    mdn = midi_out_device_get_internal_name(midi_output_device_current);
    if (strcmp(mdn, "none") && strcmp(mdn, SYSTEM_MIDI_INTERNAL_NAME))
        init_midi = 1; /* If the device is neither none, nor system MIDI, initialize the
                MIDI buffer and source, otherwise, do not. */
    sources = 2 + !!init_midi;

    al_create_sound_source(SOUNDFREQ, SOUNDBUFLEN);
    al_create_sound_source(CD_FREQ, CD_BUFLEN);
    if (init_midi) midi_source = al_create_sound_source(midi_freq, midi_buf_size);

    initialized = 1;
}

void
givealbuffer_common(void *buf, al_source* src, int size, int freq)
{
    int    processed;
    int    state;
    ALuint buffer;
    double gain;

    if (!initialized)
        return;

    alGetSourcei(src->source, AL_SOURCE_STATE, &state);

    if (state == 0x1014) {
        alSourcePlay(src->source);
    }

    alGetSourcei(src->source, AL_BUFFERS_PROCESSED, &processed);
    if (processed >= 1) {
        gain = pow(10.0, (double) sound_gain / 20.0);
        alListenerf(AL_GAIN, gain);

        alSourceUnqueueBuffers(src->source, 1, &buffer);

        if (sound_is_float)
            alBufferData(buffer, AL_FORMAT_STEREO_FLOAT32, buf, size * sizeof(float), freq);
        else
            alBufferData(buffer, AL_FORMAT_STEREO16, buf, size * sizeof(int16_t), freq);

        alSourceQueueBuffers(src->source, 1, &buffer);
    }
}

void
givealbuffer(void *buf)
{
    givealbuffer_common(buf, first_al_source, BUFLEN << 1, FREQ);
}

void
givealbuffer_cd(void *buf)
{
    givealbuffer_common(buf, first_al_source->next, CD_BUFLEN << 1, CD_FREQ);
}

void
givealbuffer_midi(void *buf, uint32_t size)
{
    givealbuffer_common(buf, midi_source, size, midi_freq);
}

void
givealbuffer_source(void *buf, al_source* src, int size, int freq)
{
    return givealbuffer_common(buf, src, size, freq);
}
