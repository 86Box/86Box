#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/plat.h>
#include <86box/sound_util.h>

int16_t *
sound_load_wav(const char *filename, int *sample_count)
{
    if ((filename == NULL) || (strlen(filename) == 0))
        return NULL;

    if (strstr(filename, "..") != NULL)
        return NULL;

    FILE *f = asset_fopen(filename, "rb");
    if (f == NULL)
        return NULL;

    wav_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
        fclose(f);
        return NULL;
    }

    if (memcmp(hdr.riff, "RIFF", 4) || memcmp(hdr.wave, "WAVE", 4) || 
        memcmp(hdr.fmt, "fmt ", 4) || memcmp(hdr.data, "data", 4)) {
        fclose(f);
        return NULL;
    }

    /* Accept both mono and stereo, 16-bit PCM */
    if (hdr.audio_format != 1 || hdr.bits_per_sample != 16 || 
        (hdr.num_channels != 1 && hdr.num_channels != 2)) {
        fclose(f);
        return NULL;
    }

    int      input_samples = hdr.data_size / 2;
    int16_t *input_data    = malloc(hdr.data_size);
    if (!input_data) {
        fclose(f);
        return NULL;
    }

    if (fread(input_data, 1, hdr.data_size, f) != hdr.data_size) {
        free(input_data);
        fclose(f);
        return NULL;
    }
    fclose(f);

    int16_t *output_data;
    int      output_samples;

    if (hdr.num_channels == 1) {
        /* Convert mono to stereo */
        output_samples = input_samples;
        output_data    = malloc(input_samples * 2 * sizeof(int16_t));
        if (!output_data) {
            free(input_data);
            return NULL;
        }

        for (int i = 0; i < input_samples; i++) {
            output_data[i * 2]     = input_data[i];
            output_data[i * 2 + 1] = input_data[i];
        }

        free(input_data);
    } else {
        output_data    = input_data;
        output_samples = input_samples / 2;
    }

    if (sample_count)
        *sample_count = output_samples;

    return output_data;
}