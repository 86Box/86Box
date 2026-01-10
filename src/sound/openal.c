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

#define FREQ   SOUND_FREQ
#define BUFLEN SOUNDBUFLEN

#define I_NORMAL 0
#define I_MUSIC  1
#define I_WT     2
#define I_CD     3
#define I_FDD    4
#define I_HDD    5
#define I_MIDI   6

ALuint        buffers[4];       /* front and back buffers */
ALuint        buffers_music[4]; /* front and back buffers */
ALuint        buffers_wt[4];    /* front and back buffers */
ALuint        buffers_cd[4];    /* front and back buffers */
ALuint        buffers_fdd[4];   /* front and back buffers */
ALuint        buffers_hdd[4];   /* front and back buffers */
ALuint        buffers_midi[4];  /* front and back buffers */
static ALuint source[7];        /* audio sources */

static int         midi_freq     = 44100;
static int         midi_buf_size = 4410;
static int         initialized   = 0;
static int         sources       = 2;
static ALCcontext *Context;
static ALCdevice  *Device;

void
al_set_midi(const int freq, const int buf_size)
{
    midi_freq     = freq;
    midi_buf_size = buf_size;
}

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

    if (sources >= 7)
        alDeleteBuffers(4, buffers_midi);
    alDeleteBuffers(4, buffers_fdd);
    alDeleteBuffers(4, buffers_hdd);
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
    float   *wt_buf          = NULL;
    float   *cd_buf          = NULL;
    float   *midi_buf        = NULL;
    float   *fdd_buf         = NULL;
    float   *hdd_buf         = NULL;
    int16_t *buf_int16       = NULL;
    int16_t *music_buf_int16 = NULL;
    int16_t *wt_buf_int16    = NULL;
    int16_t *cd_buf_int16    = NULL;
    int16_t *midi_buf_int16  = NULL;
    int16_t *fdd_buf_int16   = NULL;
    int16_t *hdd_buf_int16   = NULL;

    int init_midi = 0;

    if (initialized)
        return;

    alutInit(0, 0);
    atexit(closeal);

    const char *mdn = midi_out_device_get_internal_name(midi_output_device_current);
    if ((strcmp(mdn, "none") != 0) && (strcmp(mdn, SYSTEM_MIDI_INTERNAL_NAME) != 0))
        init_midi = 1; /* If the device is neither none, nor system MIDI, initialize the
                          MIDI buffer and source, otherwise, do not. */

    sources = 6 + !!init_midi;
    if (sound_is_float) {
        buf       = (float *) calloc((BUFLEN << 1), sizeof(float));
        music_buf = (float *) calloc((MUSICBUFLEN << 1), sizeof(float));
        wt_buf    = (float *) calloc((WTBUFLEN << 1), sizeof(float));
        cd_buf    = (float *) calloc((CD_BUFLEN << 1), sizeof(float));
        fdd_buf   = (float *) calloc((BUFLEN << 1), sizeof(float));
        hdd_buf   = (float *) calloc((BUFLEN << 1), sizeof(float));
        if (init_midi)
            midi_buf = (float *) calloc(midi_buf_size, sizeof(float));
    } else {
        buf_int16       = (int16_t *) calloc((BUFLEN << 1), sizeof(int16_t));
        music_buf_int16 = (int16_t *) calloc((MUSICBUFLEN << 1), sizeof(int16_t));
        wt_buf_int16    = (int16_t *) calloc((WTBUFLEN << 1), sizeof(int16_t));
        cd_buf_int16    = (int16_t *) calloc((CD_BUFLEN << 1), sizeof(int16_t));
        fdd_buf_int16   = (int16_t *) calloc((BUFLEN << 1), sizeof(int16_t));
        hdd_buf_int16   = (int16_t *) calloc((BUFLEN << 1), sizeof(int16_t));
        if (init_midi)
            midi_buf_int16 = (int16_t *) calloc(midi_buf_size, sizeof(int16_t));
    }

    alGenBuffers(4, buffers);
    alGenBuffers(4, buffers_cd);
    alGenBuffers(4, buffers_fdd);
    alGenBuffers(4, buffers_hdd);
    alGenBuffers(4, buffers_music);
    alGenBuffers(4, buffers_wt);
    if (init_midi)
        alGenBuffers(4, buffers_midi);

    // Create sources: 0=main, 1=music, 2=wt, 3=cd, 4=fdd, 5=hdd, 6=midi(optional)
    if (init_midi)
        alGenSources(7, source);
    else
        alGenSources(6, source);

    alSource3f(source[I_NORMAL], AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSource3f(source[I_NORMAL], AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    alSource3f(source[I_NORMAL], AL_DIRECTION, 0.0f, 0.0f, 0.0f);
    alSourcef(source[I_NORMAL], AL_ROLLOFF_FACTOR, 0.0f);
    alSourcei(source[I_NORMAL], AL_SOURCE_RELATIVE, AL_TRUE);

    alSource3f(source[I_MUSIC], AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSource3f(source[I_MUSIC], AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    alSource3f(source[I_MUSIC], AL_DIRECTION, 0.0f, 0.0f, 0.0f);
    alSourcef(source[I_MUSIC], AL_ROLLOFF_FACTOR, 0.0f);
    alSourcei(source[I_MUSIC], AL_SOURCE_RELATIVE, AL_TRUE);

    alSource3f(source[I_WT], AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSource3f(source[I_WT], AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    alSource3f(source[I_WT], AL_DIRECTION, 0.0f, 0.0f, 0.0f);
    alSourcef(source[I_WT], AL_ROLLOFF_FACTOR, 0.0f);
    alSourcei(source[I_WT], AL_SOURCE_RELATIVE, AL_TRUE);

    alSource3f(source[I_CD], AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSource3f(source[I_CD], AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    alSource3f(source[I_CD], AL_DIRECTION, 0.0f, 0.0f, 0.0f);
    alSourcef(source[I_CD], AL_ROLLOFF_FACTOR, 0.0f);
    alSourcei(source[I_CD], AL_SOURCE_RELATIVE, AL_TRUE);

    alSource3f(source[I_FDD], AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSource3f(source[I_FDD], AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    alSource3f(source[I_FDD], AL_DIRECTION, 0.0f, 0.0f, 0.0f);
    alSourcef(source[I_FDD], AL_ROLLOFF_FACTOR, 0.0f);
    alSourcei(source[I_FDD], AL_SOURCE_RELATIVE, AL_TRUE);

    alSource3f(source[I_HDD], AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSource3f(source[I_HDD], AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    alSource3f(source[I_HDD], AL_DIRECTION, 0.0f, 0.0f, 0.0f);
    alSourcef(source[I_HDD], AL_ROLLOFF_FACTOR, 0.0f);
    alSourcei(source[I_HDD], AL_SOURCE_RELATIVE, AL_TRUE);

    if (init_midi) {
        alSource3f(source[I_MIDI], AL_POSITION, 0.0f, 0.0f, 0.0f);
        alSource3f(source[I_MIDI], AL_VELOCITY, 0.0f, 0.0f, 0.0f);
        alSource3f(source[I_MIDI], AL_DIRECTION, 0.0f, 0.0f, 0.0f);
        alSourcef(source[I_MIDI], AL_ROLLOFF_FACTOR, 0.0f);
        alSourcei(source[I_MIDI], AL_SOURCE_RELATIVE, AL_TRUE);
    }

    if (sound_is_float) {
        memset(buf, 0, BUFLEN * 2 * sizeof(float));
        memset(cd_buf, 0, CD_BUFLEN * 2 * sizeof(float));
        memset(music_buf, 0, MUSICBUFLEN * 2 * sizeof(float));
        memset(wt_buf, 0, WTBUFLEN * 2 * sizeof(float));
        memset(fdd_buf, 0, BUFLEN * 2 * sizeof(float));
        memset(hdd_buf, 0, BUFLEN * 2 * sizeof(float));
        if (init_midi)
            memset(midi_buf, 0, midi_buf_size * sizeof(float));
    } else {
        memset(buf_int16, 0, BUFLEN * 2 * sizeof(int16_t));
        memset(cd_buf_int16, 0, CD_BUFLEN * 2 * sizeof(int16_t));
        memset(music_buf_int16, 0, MUSICBUFLEN * 2 * sizeof(int16_t));
        memset(wt_buf_int16, 0, WTBUFLEN * 2 * sizeof(int16_t));
        memset(fdd_buf_int16, 0, BUFLEN * 2 * sizeof(int16_t));
        memset(hdd_buf_int16, 0, BUFLEN * 2 * sizeof(int16_t));
        if (init_midi)
            memset(midi_buf_int16, 0, midi_buf_size * sizeof(int16_t));
    }

    for (uint8_t c = 0; c < 4; c++) {
        if (sound_is_float) {
            alBufferData(buffers[c], AL_FORMAT_STEREO_FLOAT32, buf, BUFLEN * 2 * sizeof(float), FREQ);
            alBufferData(buffers_music[c], AL_FORMAT_STEREO_FLOAT32, music_buf, MUSICBUFLEN * 2 * sizeof(float), MUSIC_FREQ);
            alBufferData(buffers_wt[c], AL_FORMAT_STEREO_FLOAT32, wt_buf, WTBUFLEN * 2 * sizeof(float), WT_FREQ);
            alBufferData(buffers_cd[c], AL_FORMAT_STEREO_FLOAT32, cd_buf, CD_BUFLEN * 2 * sizeof(float), CD_FREQ);
            alBufferData(buffers_fdd[c], AL_FORMAT_STEREO_FLOAT32, fdd_buf, BUFLEN * 2 * sizeof(float), FREQ);
            alBufferData(buffers_hdd[c], AL_FORMAT_STEREO_FLOAT32, hdd_buf, BUFLEN * 2 * sizeof(float), FREQ);
            if (init_midi)
                alBufferData(buffers_midi[c], AL_FORMAT_STEREO_FLOAT32, midi_buf, midi_buf_size * (int) sizeof(float), midi_freq);
        } else {
            alBufferData(buffers[c], AL_FORMAT_STEREO16, buf_int16, BUFLEN * 2 * sizeof(int16_t), FREQ);
            alBufferData(buffers_music[c], AL_FORMAT_STEREO16, music_buf_int16, MUSICBUFLEN * 2 * sizeof(int16_t), MUSIC_FREQ);
            alBufferData(buffers_wt[c], AL_FORMAT_STEREO16, wt_buf_int16, WTBUFLEN * 2 * sizeof(int16_t), WT_FREQ);
            alBufferData(buffers_cd[c], AL_FORMAT_STEREO16, cd_buf_int16, CD_BUFLEN * 2 * sizeof(int16_t), CD_FREQ);
            alBufferData(buffers_fdd[c], AL_FORMAT_STEREO16, fdd_buf_int16, BUFLEN * 2 * sizeof(int16_t), FREQ);
            alBufferData(buffers_hdd[c], AL_FORMAT_STEREO16, hdd_buf_int16, BUFLEN * 2 * sizeof(int16_t), FREQ);
            if (init_midi)
                alBufferData(buffers_midi[c], AL_FORMAT_STEREO16, midi_buf_int16, midi_buf_size * (int) sizeof(int16_t), midi_freq);
        }
    }

    alSourceQueueBuffers(source[I_NORMAL], 4, buffers);
    alSourceQueueBuffers(source[I_MUSIC], 4, buffers_music);
    alSourceQueueBuffers(source[I_WT], 4, buffers_wt);
    alSourceQueueBuffers(source[I_CD], 4, buffers_cd);
    alSourceQueueBuffers(source[I_FDD], 4, buffers_fdd);
    alSourceQueueBuffers(source[I_HDD], 4, buffers_hdd);
    if (init_midi)
        alSourceQueueBuffers(source[I_MIDI], 4, buffers_midi);
    alSourcePlay(source[I_NORMAL]);
    alSourcePlay(source[I_MUSIC]);
    alSourcePlay(source[I_WT]);
    alSourcePlay(source[I_CD]);
    alSourcePlay(source[I_FDD]);
    alSourcePlay(source[I_HDD]);
    if (init_midi)
        alSourcePlay(source[I_MIDI]);

    if (sound_is_float) {
        if (init_midi)
            free(midi_buf);
        free(cd_buf);
        free(wt_buf);
        free(music_buf);
        free(buf);
        free(fdd_buf);
        free(hdd_buf);
    } else {
        if (init_midi)
            free(midi_buf_int16);
        free(cd_buf_int16);
        free(wt_buf_int16);
        free(music_buf_int16);
        free(buf_int16);
        free(fdd_buf_int16);
        free(hdd_buf_int16);
    }

    initialized = 1;
}

extern bool fast_forward;
void
givealbuffer_common(const void *buf, const uint8_t src, const int size, const int freq)
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
            alBufferData(buffer, AL_FORMAT_STEREO_FLOAT32, buf, size * (int) sizeof(float), freq);
        else
            alBufferData(buffer, AL_FORMAT_STEREO16, buf, size * (int) sizeof(int16_t), freq);

        alSourceQueueBuffers(source[src], 1, &buffer);
    }
}

void
givealbuffer(const void *buf)
{
    givealbuffer_common(buf, I_NORMAL, BUFLEN << 1, FREQ);
}

void
givealbuffer_music(const void *buf)
{
    givealbuffer_common(buf, I_MUSIC, MUSICBUFLEN << 1, MUSIC_FREQ);
}

void
givealbuffer_wt(const void *buf)
{
    givealbuffer_common(buf, I_WT, WTBUFLEN << 1, WT_FREQ);
}

void
givealbuffer_cd(const void *buf)
{
    givealbuffer_common(buf, I_CD, CD_BUFLEN << 1, CD_FREQ);
}

void
givealbuffer_midi(const void *buf, const uint32_t size)
{
    givealbuffer_common(buf, I_MIDI, (int) size, midi_freq);
}

void
givealbuffer_fdd(const void *buf, const uint32_t size)
{
    givealbuffer_common(buf, I_FDD, (int) size, FREQ);
}

void
givealbuffer_hdd(const void *buf, const uint32_t size)
{
    givealbuffer_common(buf, I_HDD, (int) size, FREQ);
}