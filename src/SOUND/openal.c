#define USE_OPENAL
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef USE_OPENAL
# include <AL/al.h>
# include <AL/alc.h>
# include <AL/alext.h>
#endif
#include "../ibm.h"
#include "sound.h"


FILE *allog;
#ifdef USE_OPENAL
ALuint buffers[4];		/* front and back buffers */
ALuint buffers_cd[4];		/* front and back buffers */
static ALuint source[2];	/* audio source */
#endif
#define FREQ 48000
#define BUFLEN SOUNDBUFLEN


void closeal(void);
ALvoid  alutInit(ALint *argc,ALbyte **argv) 
{
	ALCcontext *Context;
	ALCdevice *Device;
	
	/* Open device */
 	Device=alcOpenDevice((ALCchar *)"");
	/* Create context(s) */
	Context=alcCreateContext(Device,NULL);
	/* Set active context */
	alcMakeContextCurrent(Context);
	/* Register extensions */
}

ALvoid  alutExit(ALvoid) 
{
	ALCcontext *Context;
	ALCdevice *Device;

	/* Unregister extensions */

	/* Get active context */
	Context=alcGetCurrentContext();
	/* Get device for active context */
	Device=alcGetContextsDevice(Context);
	/* Disable context */
	alcMakeContextCurrent(NULL);
	/* Release context(s) */
	alcDestroyContext(Context);
	/* Close device */
	alcCloseDevice(Device);
}
void initalmain(int argc, char *argv[])
{
#ifdef USE_OPENAL
        alutInit(0,0);
        atexit(closeal);
#endif
}

void closeal(void)
{
#ifdef USE_OPENAL
        alutExit();
#endif
}

void inital(ALvoid)
{
#ifdef USE_OPENAL
        int c;

        float buf[BUFLEN*2];

        float cd_buf[CD_BUFLEN*2];

        int16_t buf_int16[BUFLEN*2];

        int16_t cd_buf_int16[CD_BUFLEN*2];

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

        for (c = 0; c < 4; c++)
        {
		if (sound_is_float)
		{
	                alBufferData(buffers[c], AL_FORMAT_STEREO_FLOAT32, buf, BUFLEN*2*sizeof(float), FREQ);
        	        alBufferData(buffers_cd[c], AL_FORMAT_STEREO_FLOAT32, cd_buf, CD_BUFLEN*2*sizeof(float), CD_FREQ);
		}
		else
		{
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

        if (state==0x1014)
        {
                alSourcePlay(source[0]);
        }
        alGetSourcei(source[0], AL_BUFFERS_PROCESSED, &processed);

        if (processed>=1)
        {
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

        if (state==0x1014)
        {
                alSourcePlay(source[0]);
        }
        alGetSourcei(source[0], AL_BUFFERS_PROCESSED, &processed);

        if (processed>=1)
        {
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

        if (state==0x1014)
        {
                alSourcePlay(source[1]);
        }
        alGetSourcei(source[1], AL_BUFFERS_PROCESSED, &processed);

        if (processed>=1)
        {
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

        if (state==0x1014)
        {
                alSourcePlay(source[1]);
        }
        alGetSourcei(source[1], AL_BUFFERS_PROCESSED, &processed);

        if (processed>=1)
        {
                ALuint buffer;

                alSourceUnqueueBuffers(source[1], 1, &buffer);

                alBufferData(buffer, AL_FORMAT_STEREO16, buf, CD_BUFLEN*2*sizeof(int16_t), CD_FREQ);

                alSourceQueueBuffers(source[1], 1, &buffer);
        }
#endif
}
