/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Interface to the OpenAL sound processing library.
 *
 * Version:	@(#)openal.c	1.0.5	2018/02/19
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#ifdef USE_OPENAL
# undef AL_API
# undef ALC_API
# define AL_LIBTYPE_STATIC
# define ALC_LIBTYPE_STATIC
# include <AL/al.h>
# include <AL/alc.h>
# include <AL/alext.h>
#endif
#include "../86box.h"
#include "sound.h"
#include "midi.h"


#define FREQ	48000
#define BUFLEN	SOUNDBUFLEN


#ifdef USE_OPENAL
ALuint buffers[4];		/* front and back buffers */
ALuint buffers_cd[4];		/* front and back buffers */
ALuint buffers_midi[4];		/* front and back buffers */
static ALuint source[3];	/* audio source */
#endif


static int midi_freq = 44100;
static int midi_buf_size = 4410;
static int initialized = 0;


void
al_set_midi(int freq, int buf_size)
{
    midi_freq = freq;
    midi_buf_size = buf_size;
}


#ifdef USE_OPENAL
void closeal(void);
ALvoid alutInit(ALint *argc,ALbyte **argv) 
{
    ALCcontext *Context;
    ALCdevice *Device;
	
    /* Open device */
    Device = alcOpenDevice((ALCchar *)"");
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
    ALCcontext *Context;
    ALCdevice *Device;

    /* Get active context */
    Context = alcGetCurrentContext();
    if (Context != NULL) {
	/* Get device for active context */
	Device = alcGetContextsDevice(Context);
	if (Device != NULL) {
		/* Disable context */
		alcMakeContextCurrent(NULL);

		/* Close device */
		alcCloseDevice(Device);
	}

	/* Release context(s) */
	alcDestroyContext(Context);
    }
}
#endif


void
closeal(void)
{
#ifdef USE_OPENAL
    alutExit();
#endif
}


void
initalmain(int argc, char *argv[])
{
    if (! initialized) return;

#ifdef USE_OPENAL
    alutInit(0,0);
    atexit(closeal);
#endif
    initialized = 0;
}


void
inital(void)
{
    float *buf = NULL, *cd_buf = NULL, *midi_buf = NULL;
    int16_t *buf_int16 = NULL, *cd_buf_int16 = NULL, *midi_buf_int16 = NULL;
    int c;

    char *mdn;
    int init_midi = 0;

    if (initialized) return;

    mdn = midi_device_get_internal_name(midi_device_current);
    if (strcmp(mdn, "none") && strcmp(mdn, SYSTEM_MIDI_INTERNAL_NAME))
	init_midi = 1;	/* If the device is neither none, nor system MIDI, initialize the
			   MIDI buffer and source, otherwise, do not. */

#ifdef USE_OPENAL

    if (sound_is_float) {
	buf = (float *) malloc((BUFLEN << 1) * sizeof(float));
	cd_buf = (float *) malloc((CD_BUFLEN << 1) * sizeof(float));
	if (init_midi)
		midi_buf = (float *) malloc(midi_buf_size * sizeof(float));
    } else {
	buf_int16 = (int16_t *) malloc((BUFLEN << 1) * sizeof(int16_t));
	cd_buf_int16 = (int16_t *) malloc((CD_BUFLEN << 1) * sizeof(int16_t));
	if (init_midi)
		midi_buf_int16 = (int16_t *) malloc(midi_buf_size * sizeof(int16_t));
    }

    alGenBuffers(4, buffers);
    alGenBuffers(4, buffers_cd);
    if (init_midi)
	alGenBuffers(4, buffers_midi);

    if (init_midi)
	alGenSources(3, source);
    else
	alGenSources(2, source);

    alSource3f(source[0], AL_POSITION,        0.0, 0.0, 0.0);
    alSource3f(source[0], AL_VELOCITY,        0.0, 0.0, 0.0);
    alSource3f(source[0], AL_DIRECTION,       0.0, 0.0, 0.0);
    alSourcef (source[0], AL_ROLLOFF_FACTOR,  0.0          );
    alSourcei (source[0], AL_SOURCE_RELATIVE, AL_TRUE      );
    alSource3f(source[1], AL_POSITION,        0.0, 0.0, 0.0);
    alSource3f(source[1], AL_VELOCITY,        0.0, 0.0, 0.0);
    alSource3f(source[1], AL_DIRECTION,       0.0, 0.0, 0.0);
    alSourcef (source[1], AL_ROLLOFF_FACTOR,  0.0          );
    alSourcei (source[1], AL_SOURCE_RELATIVE, AL_TRUE      );
    if (init_midi) {
	alSource3f(source[2], AL_POSITION,        0.0, 0.0, 0.0);
	alSource3f(source[2], AL_VELOCITY,        0.0, 0.0, 0.0);
	alSource3f(source[2], AL_DIRECTION,       0.0, 0.0, 0.0);
	alSourcef (source[2], AL_ROLLOFF_FACTOR,  0.0          );
	alSourcei (source[2], AL_SOURCE_RELATIVE, AL_TRUE      );
    }

    if (sound_is_float) {
	memset(buf,0,BUFLEN*2*sizeof(float));
	memset(cd_buf,0,BUFLEN*2*sizeof(float));
	if (init_midi)
		memset(midi_buf,0,midi_buf_size*sizeof(float));
    } else {
	memset(buf_int16,0,BUFLEN*2*sizeof(int16_t));
	memset(cd_buf_int16,0,BUFLEN*2*sizeof(int16_t));
	if (init_midi)
		memset(midi_buf_int16,0,midi_buf_size*sizeof(int16_t));
    }

    for (c=0; c<4; c++) {
	if (sound_is_float) {
		alBufferData(buffers[c], AL_FORMAT_STEREO_FLOAT32, buf, BUFLEN*2*sizeof(float), FREQ);
		alBufferData(buffers_cd[c], AL_FORMAT_STEREO_FLOAT32, cd_buf, CD_BUFLEN*2*sizeof(float), CD_FREQ);
		if (init_midi)
			alBufferData(buffers_midi[c], AL_FORMAT_STEREO_FLOAT32, midi_buf, midi_buf_size*sizeof(float), midi_freq);
	} else {
		alBufferData(buffers[c], AL_FORMAT_STEREO16, buf_int16, BUFLEN*2*sizeof(int16_t), FREQ);
		alBufferData(buffers_cd[c], AL_FORMAT_STEREO16, cd_buf_int16, CD_BUFLEN*2*sizeof(int16_t), CD_FREQ);
		if (init_midi)
			alBufferData(buffers_midi[c], AL_FORMAT_STEREO16, midi_buf_int16, midi_buf_size*sizeof(int16_t), midi_freq);
	}
    }

    alSourceQueueBuffers(source[0], 4, buffers);
    alSourceQueueBuffers(source[1], 4, buffers_cd);
    if (init_midi)
	alSourceQueueBuffers(source[2], 4, buffers_midi);
    alSourcePlay(source[0]);
    alSourcePlay(source[1]);
    if (init_midi)
	alSourcePlay(source[2]);

    if (sound_is_float) {
	if (init_midi)
		free(midi_buf);
	free(cd_buf);
	free(buf);
    } else {
	if (init_midi)
		free(midi_buf_int16);
	free(cd_buf_int16);
	free(buf_int16);
    }

    initialized = 1;
#endif
}


void
givealbuffer_common(void *buf, uint8_t src, int size, int freq)
{
#ifdef USE_OPENAL
    int processed;
    int state;
    ALuint buffer;
    double gain;

    alGetSourcei(source[src], AL_SOURCE_STATE, &state);

    if (state == 0x1014) {
	alSourcePlay(source[src]);
    }

    alGetSourcei(source[src], AL_BUFFERS_PROCESSED, &processed);
    if (processed >= 1) {
	gain = pow(10.0, (double)sound_gain / 20.0);
	alListenerf(AL_GAIN, gain);

	alSourceUnqueueBuffers(source[src], 1, &buffer);

	if (sound_is_float) {
		alBufferData(buffer, AL_FORMAT_STEREO_FLOAT32, buf, size * sizeof(float), freq);
	} else {
		alBufferData(buffer, AL_FORMAT_STEREO16, buf, size * sizeof(int16_t), freq);
	}

	alSourceQueueBuffers(source[src], 1, &buffer);
    }
#endif
}


void
givealbuffer(void *buf)
{
    givealbuffer_common(buf, 0, BUFLEN << 1, FREQ);
}


void
givealbuffer_cd(void *buf)
{
    givealbuffer_common(buf, 1, CD_BUFLEN << 1, CD_FREQ);
}


void
givealbuffer_midi(void *buf, uint32_t size)
{
    givealbuffer_common(buf, 2, size, midi_freq);
}
