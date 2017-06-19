#define USE_OPENAL
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef USE_OPENAL
# undef AL_API
# undef ALC_API
# define AL_LIBTYPE_STATIC
# define ALC_LIBTYPE_STATIC
# include <AL/al.h>
# include <AL/alc.h>
# include <AL/alext.h>
#endif
#include "../ibm.h"
#include "sound.h"


#define FREQ	48000
#define BUFLEN	SOUNDBUFLEN


#ifdef USE_OPENAL
ALuint buffers[4];		/* front and back buffers */
ALuint buffers_cd[4];		/* front and back buffers */
static ALuint source[2];	/* audio source */
#endif


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


ALvoid alutExit(ALvoid) 
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


void closeal(void)
{
#ifdef USE_OPENAL
    alutExit();
#endif
}


void initalmain(int argc, char *argv[])
{
#ifdef USE_OPENAL
    alutInit(0,0);
    atexit(closeal);
#endif
}


void inital(ALvoid)
{
#ifdef USE_OPENAL
    float buf[BUFLEN*2];
    float cd_buf[CD_BUFLEN*2];
    int16_t buf_int16[BUFLEN*2];
    int16_t cd_buf_int16[CD_BUFLEN*2];
    int c;

    alGenBuffers(4, buffers);
    alGenBuffers(4, buffers_cd);
        
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

    memset(buf,0,BUFLEN*2*sizeof(float));
    memset(cd_buf,0,BUFLEN*2*sizeof(float));

    for (c = 0; c < 4; c++) {
	if (sound_is_float) {
		alBufferData(buffers[c], AL_FORMAT_STEREO_FLOAT32, buf, BUFLEN*2*sizeof(float), FREQ);
		alBufferData(buffers_cd[c], AL_FORMAT_STEREO_FLOAT32, cd_buf, CD_BUFLEN*2*sizeof(float), CD_FREQ);
	} else {
		alBufferData(buffers[c], AL_FORMAT_STEREO16, buf_int16, BUFLEN*2*sizeof(int16_t), FREQ);
		alBufferData(buffers_cd[c], AL_FORMAT_STEREO16, cd_buf_int16, CD_BUFLEN*2*sizeof(int16_t), CD_FREQ);
	}
    }

    alSourceQueueBuffers(source[0], 4, buffers);
    alSourceQueueBuffers(source[1], 4, buffers_cd);
    alSourcePlay(source[0]);
    alSourcePlay(source[1]);
#endif
}


void givealbuffer(float *buf)
{
#ifdef USE_OPENAL
    int processed;
    int state;
    ALuint buffer;

    alGetSourcei(source[0], AL_SOURCE_STATE, &state);

    if (state==0x1014) {
	alSourcePlay(source[0]);
    }
    alGetSourcei(source[0], AL_BUFFERS_PROCESSED, &processed);

    if (processed>=1) {
	alSourceUnqueueBuffers(source[0], 1, &buffer);

	alBufferData(buffer, AL_FORMAT_STEREO_FLOAT32, buf, BUFLEN*2*sizeof(float), FREQ);

	alSourceQueueBuffers(source[0], 1, &buffer);
    }
#endif
}


void givealbuffer_int16(int16_t *buf)
{
#ifdef USE_OPENAL
    int processed;
    int state;
    ALuint buffer;

    alGetSourcei(source[0], AL_SOURCE_STATE, &state);

    if (state==0x1014) {
	alSourcePlay(source[0]);
    }
    alGetSourcei(source[0], AL_BUFFERS_PROCESSED, &processed);

    if (processed>=1) {
	alSourceUnqueueBuffers(source[0], 1, &buffer);

	alBufferData(buffer, AL_FORMAT_STEREO16, buf, BUFLEN*2*sizeof(int16_t), FREQ);

	alSourceQueueBuffers(source[0], 1, &buffer);
    }
#endif
}


void givealbuffer_cd(float *buf)
{
#ifdef USE_OPENAL
    int processed;
    int state;

    alGetSourcei(source[1], AL_SOURCE_STATE, &state);

    if (state==0x1014) {
	alSourcePlay(source[1]);
    }
    alGetSourcei(source[1], AL_BUFFERS_PROCESSED, &processed);

    if (processed>=1) {
	ALuint buffer;

	alSourceUnqueueBuffers(source[1], 1, &buffer);

	alBufferData(buffer, AL_FORMAT_STEREO_FLOAT32, buf, CD_BUFLEN*2*sizeof(float), CD_FREQ);

	alSourceQueueBuffers(source[1], 1, &buffer);
    }
#endif
}


void givealbuffer_cd_int16(int16_t *buf)
{
#ifdef USE_OPENAL
    int processed;
    int state;

    alGetSourcei(source[1], AL_SOURCE_STATE, &state);

    if (state==0x1014) {
	alSourcePlay(source[1]);
    }
    alGetSourcei(source[1], AL_BUFFERS_PROCESSED, &processed);

    if (processed>=1) {
	ALuint buffer;

	alSourceUnqueueBuffers(source[1], 1, &buffer);

	alBufferData(buffer, AL_FORMAT_STEREO16, buf, CD_BUFLEN*2*sizeof(int16_t), CD_FREQ);

	alSourceQueueBuffers(source[1], 1, &buffer);
    }
#endif
}
