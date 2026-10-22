/* SPDX-License-Identifier: GPL-2.0-or-later
 * Linux LD_PRELOAD helper: capture CD mixer buffers as submitted to OpenAL.
 * Each record is three native uint32_t values (format, size, frequency), then
 * the sample bytes. The Python harness requires a little-endian host.
 * With no emulated sound card, these 100 ms / 44100 Hz buffers are CD audio.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <AL/al.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void
alBufferData(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq)
{
    void (*next)(ALuint, ALenum, const ALvoid *, ALsizei, ALsizei) = dlsym(RTLD_NEXT, "alBufferData");
    if (!next)
        abort();
    if (freq == 44100 && (size == 17640 || size == 35280)) {
        pthread_mutex_lock(&lock);
        const char *path = getenv("CDROM_AUDIO_CAPTURE");
        if (path) {
            FILE *f = fopen(path, "ab");
            if (f) {
                uint32_t header[3] = { format, size, freq };
                fwrite(header, sizeof(header), 1, f);
                fwrite(data, size, 1, f);
                fclose(f);
            }
        }
        pthread_mutex_unlock(&lock);
    }
    next(buffer, format, data, size, freq);
}
