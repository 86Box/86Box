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
ALuint buffers_midi[4];		/* front and back buffers */
static ALuint source[3];	/* audio source */
#endif
#define FREQ 48000
#define BUFLEN SOUNDBUFLEN


static int midi_freq = 44100;
static int midi_buf_size = 4410;
static int initialized = 0;

void al_set_midi(int freq, int buf_size)
{
        midi_freq = freq;
        midi_buf_size = buf_size;
}

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
        if (!initialized) return;
#ifdef USE_OPENAL
        alutExit();
#endif
	initialized = 0;
}

void inital(ALvoid)
{
        if (initialized) return;

#ifdef USE_OPENAL
        int c;

	float *buf = NULL, *cd_buf = NULL, *midi_buf = NULL;
	int16_t *buf_int16 = NULL, *cd_buf_int16 = NULL, *midi_buf_int16 = NULL;

	if (sound_is_float)
	{
		buf = (float *) malloc((BUFLEN << 1) * sizeof(float));
		cd_buf = (float *) malloc((CD_BUFLEN << 1) * sizeof(float));
		midi_buf = (float *) malloc(midi_buf_size * sizeof(float));
	}
	else
	{
		buf_int16 = (int16_t *) malloc((BUFLEN << 1) * sizeof(int16_t));
		cd_buf_int16 = (int16_t *) malloc((CD_BUFLEN << 1) * sizeof(int16_t));
		midi_buf_int16 = (int16_t *) malloc(midi_buf_size * sizeof(int16_t));
	}

        alGenBuffers(4, buffers);
        alGenBuffers(4, buffers_cd);
        alGenBuffers(4, buffers_midi);
        
        alGenSources(3, source);

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
        alSource3f(source[2], AL_POSITION,        0.0, 0.0, 0.0);
        alSource3f(source[2], AL_VELOCITY,        0.0, 0.0, 0.0);
        alSource3f(source[2], AL_DIRECTION,       0.0, 0.0, 0.0);
        alSourcef (source[2], AL_ROLLOFF_FACTOR,  0.0          );
        alSourcei (source[2], AL_SOURCE_RELATIVE, AL_TRUE      );

	if (sound_is_float)
	{
	        memset(buf,0,BUFLEN*2*sizeof(float));
        	memset(cd_buf,0,BUFLEN*2*sizeof(float));
        	memset(midi_buf,0,midi_buf_size*sizeof(float));
	}
	else
	{
	        memset(buf_int16,0,BUFLEN*2*sizeof(int16_t));
        	memset(cd_buf_int16,0,BUFLEN*2*sizeof(int16_t));
        	memset(midi_buf_int16,0,midi_buf_size*sizeof(int16_t));
	}

        for (c = 0; c < 4; c++)
        {
		if (sound_is_float)
		{
	                alBufferData(buffers[c], AL_FORMAT_STEREO_FLOAT32, buf, BUFLEN*2*sizeof(float), FREQ);
        	        alBufferData(buffers_cd[c], AL_FORMAT_STEREO_FLOAT32, cd_buf, CD_BUFLEN*2*sizeof(float), CD_FREQ);
        	        alBufferData(buffers_midi[c], AL_FORMAT_STEREO_FLOAT32, midi_buf, midi_buf_size*sizeof(float), midi_freq);
		}
		else
		{
	                alBufferData(buffers[c], AL_FORMAT_STEREO16, buf_int16, BUFLEN*2*sizeof(int16_t), FREQ);
        	        alBufferData(buffers_cd[c], AL_FORMAT_STEREO16, cd_buf_int16, CD_BUFLEN*2*sizeof(int16_t), CD_FREQ);
        	        alBufferData(buffers_midi[c], AL_FORMAT_STEREO16, midi_buf_int16, midi_buf_size*sizeof(int16_t), midi_freq);
		}
        }

        alSourceQueueBuffers(source[0], 4, buffers);
        alSourceQueueBuffers(source[1], 4, buffers_cd);
        alSourceQueueBuffers(source[2], 4, buffers_midi);
        alSourcePlay(source[0]);
        alSourcePlay(source[1]);
        alSourcePlay(source[2]);

	if (sound_is_float)
	{
		free(midi_buf);
		free(cd_buf);
		free(buf);
	}
	else
	{
		free(midi_buf_int16);
		free(cd_buf_int16);
		free(buf_int16);
	}

        initialized = 1;
#endif
}

void givealbuffer_common(void *buf, uint8_t src, int size, int freq)
{
#ifdef USE_OPENAL
        int processed;
        int state;
	ALuint buffer;

        alGetSourcei(source[src], AL_SOURCE_STATE, &state);

        if (state==0x1014)
        {
                alSourcePlay(source[src]);
        }
        alGetSourcei(source[src], AL_BUFFERS_PROCESSED, &processed);

        if (processed>=1)
        {
                alSourceUnqueueBuffers(source[src], 1, &buffer);

		if (sound_is_float)
		{
	                alBufferData(buffer, AL_FORMAT_STEREO_FLOAT32, buf, size * sizeof(float), freq);
		}
		else
		{
	                alBufferData(buffer, AL_FORMAT_STEREO16, buf, size * sizeof(int16_t), freq);
		}

                alSourceQueueBuffers(source[src], 1, &buffer);
        }
#endif
}

void givealbuffer(void *buf)
{
	givealbuffer_common(buf, 0, BUFLEN << 1, FREQ);
}

void givealbuffer_cd(void *buf)
{
	givealbuffer_common(buf, 1, CD_BUFLEN << 1, CD_FREQ);
}

void givealbuffer_midi(void *buf, uint32_t size)
{
	givealbuffer_common(buf, 2, size, midi_freq);
}
