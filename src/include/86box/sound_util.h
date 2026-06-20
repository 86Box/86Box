#ifndef SOUND_UTIL_H
#define SOUND_UTIL_H

#include <stdint.h>

/* WAV file header structure */
typedef struct wav_header_t {
    char     riff[4];
    uint32_t file_size;
    char     wave[4];
    char     fmt[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char     data[4];
    uint32_t data_size;
} wav_header_t;

/* Load a WAV file and return stereo 16-bit samples
 * Returns allocated buffer (caller must free) or NULL on error
 * sample_count receives the number of stereo sample pairs */
int16_t *sound_load_wav(const char *filename, int *sample_count);

#endif /* SOUND_UTIL_H */