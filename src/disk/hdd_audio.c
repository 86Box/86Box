#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <86box/86box.h>
#include <86box/hdd.h>
#include <86box/sound.h>
#include <86box/sound_util.h>
#include <86box/thread.h>

/* Maximum number of simultaneous seek sounds */
#define HDD_MAX_SEEK_VOICES 8

typedef struct {
    int   active;
    int   position;
    float volume;
} hdd_seek_voice_t;

static int16_t *hdd_spindle_sound_buffer = NULL;
static int16_t *hdd_seek_sound_buffer    = NULL;
static int      hdd_samples;
static int      hdd_seek_samples;

static hdd_seek_voice_t hdd_seek_voices[HDD_MAX_SEEK_VOICES];
static mutex_t         *hdd_audio_mutex = NULL;

void
hdd_audio_init(void)
{
    hdd_spindle_sound_buffer = sound_load_wav(
        "assets/sounds/hdd/1993 IBM H3171/1993_IBM_H3171_3.5_SPINDLE_RUNNING.wav",
        &hdd_samples);

    if (hdd_spindle_sound_buffer) {
        pclog("HDD Audio: Loaded spindle sound, %d frames\n", hdd_samples);
    } else {
        pclog("HDD Audio: Failed to load spindle sound!\n");
    }

    hdd_seek_sound_buffer = sound_load_wav(
        "assets/sounds/hdd/1993 IBM H3171/1993_IBM_H3171_3.5_SEEK_1TRACK.wav",
        &hdd_seek_samples);

    if (hdd_seek_sound_buffer) {
        pclog("HDD Audio: Loaded seek sound, %d frames (%.1f ms)\n", 
              hdd_seek_samples, (float)hdd_seek_samples / 48.0f);
    } else {
        pclog("HDD Audio: Failed to load seek sound!\n");
    }

    for (int i = 0; i < HDD_MAX_SEEK_VOICES; i++) {
        hdd_seek_voices[i].active = 0;
        hdd_seek_voices[i].position = 0;
        hdd_seek_voices[i].volume = 1.0f;
    }

    hdd_audio_mutex = thread_create_mutex();

    sound_hdd_thread_init();
}

void
hdd_audio_seek(hard_disk_t *hdd, uint32_t new_cylinder)
{
    uint32_t cylinder_diff = abs((int) hdd->cur_cylinder - (int) new_cylinder);

    if (cylinder_diff == 0)
        return;

    pclog("HDD Audio Seek: cur_cyl=%u -> new_cyl=%u, diff=%u, speed_preset=%d, cyl_switch_usec=%.1f\n",
          hdd->cur_cylinder, new_cylinder, cylinder_diff, hdd->speed_preset, 
          hdd->cyl_switch_usec);

    if (hdd->speed_preset == 0)
        return;

    if (!hdd_seek_sound_buffer || hdd_seek_samples == 0) {
        pclog("HDD Audio Seek: No seek sound buffer loaded!\n");
        return;
    }

    int min_seek_spacing = 0;
    if (hdd->cyl_switch_usec > 0)
        min_seek_spacing = (int)(hdd->cyl_switch_usec * 48000.0 / 1000000.0);

    thread_wait_mutex(hdd_audio_mutex);

    for (int i = 0; i < HDD_MAX_SEEK_VOICES; i++) {
        if (hdd_seek_voices[i].active) {
            int pos = hdd_seek_voices[i].position;
            if (pos >= 0 && pos < min_seek_spacing) {
                thread_release_mutex(hdd_audio_mutex);
                return;
            }
        }
    }

    int active_count = 0;

    for (int i = 0; i < HDD_MAX_SEEK_VOICES; i++) {
        if (!hdd_seek_voices[i].active) {
            hdd_seek_voices[i].active = 1;
            hdd_seek_voices[i].position = 0;
            hdd_seek_voices[i].volume = 0.5f;
            pclog("HDD Audio Seek: Using free voice %d\n", i);
            thread_release_mutex(hdd_audio_mutex);
            return;
        }
        active_count++;
    }

    pclog("HDD Audio Seek: All %d voices active, skipping seek\n", active_count);

    thread_release_mutex(hdd_audio_mutex);
}

void
hdd_audio_callback(int16_t *buffer, int length)
{
    static int spindle_pos = 0;
    static int debug_counter = 0;
    const float spindle_volume = 0.2f;
    const float seek_volume = 0.5f;
    int frames_in_buffer = length / 2;

    if (sound_is_float) {
        float *float_buffer = (float *) buffer;

        if (hdd_spindle_sound_buffer && hdd_samples > 0) {
            for (int i = 0; i < frames_in_buffer; i++) {
                float left_sample = (float) hdd_spindle_sound_buffer[spindle_pos * 2] / 131072.0f * spindle_volume;
                float right_sample = (float) hdd_spindle_sound_buffer[spindle_pos * 2 + 1] / 131072.0f * spindle_volume;
                float_buffer[i * 2]     = left_sample;
                float_buffer[i * 2 + 1] = right_sample;

                spindle_pos++;
                if (spindle_pos >= hdd_samples) {
                    spindle_pos = 0;
                }
            }
        } else {
            for (int i = 0; i < length; i++) {
                float_buffer[i] = 0.0f;
            }
        }

        if (hdd_seek_sound_buffer && hdd_seek_samples > 0 && hdd_audio_mutex) {
            thread_wait_mutex(hdd_audio_mutex);

            for (int v = 0; v < HDD_MAX_SEEK_VOICES; v++) {
                if (!hdd_seek_voices[v].active)
                    continue;

                float voice_vol = hdd_seek_voices[v].volume;
                int pos = hdd_seek_voices[v].position;
                if (pos < 0) pos = 0;

                for (int i = 0; i < frames_in_buffer && pos < hdd_seek_samples; i++, pos++) {
                    float seek_left = (float) hdd_seek_sound_buffer[pos * 2] / 131072.0f * seek_volume * voice_vol;
                    float seek_right = (float) hdd_seek_sound_buffer[pos * 2 + 1] / 131072.0f * seek_volume * voice_vol;

                    float_buffer[i * 2]     += seek_left;
                    float_buffer[i * 2 + 1] += seek_right;
                }

                if (pos >= hdd_seek_samples) {
                    hdd_seek_voices[v].active = 0;
                    hdd_seek_voices[v].position = 0;
                } else {
                    hdd_seek_voices[v].position = pos;
                }
            }

            thread_release_mutex(hdd_audio_mutex);
        }

        if (debug_counter++ % 100 == 0) {
            pclog("HDD Audio: float_buffer[0]=%.6f, float_buffer[1]=%.6f, spindle_pos=%d, frames=%d\n", 
                  float_buffer[0], float_buffer[1], spindle_pos, frames_in_buffer);
        }
    } else {
        if (hdd_spindle_sound_buffer && hdd_samples > 0) {
            for (int i = 0; i < frames_in_buffer; i++) {
                buffer[i * 2]     = hdd_spindle_sound_buffer[spindle_pos * 2];
                buffer[i * 2 + 1] = hdd_spindle_sound_buffer[spindle_pos * 2 + 1];

                spindle_pos++;
                if (spindle_pos >= hdd_samples) {
                    spindle_pos = 0;
                }
            }
        } else {
            for (int i = 0; i < length; i++) {
                buffer[i] = 0;
            }
        }

        if (hdd_seek_sound_buffer && hdd_seek_samples > 0 && hdd_audio_mutex) {
            thread_wait_mutex(hdd_audio_mutex);

            for (int v = 0; v < HDD_MAX_SEEK_VOICES; v++) {
                if (!hdd_seek_voices[v].active)
                    continue;

                int pos = hdd_seek_voices[v].position;
                if (pos < 0) pos = 0;

                for (int i = 0; i < frames_in_buffer && pos < hdd_seek_samples; i++, pos++) {
                    int32_t left = buffer[i * 2] + hdd_seek_sound_buffer[pos * 2] / 2;
                    int32_t right = buffer[i * 2 + 1] + hdd_seek_sound_buffer[pos * 2 + 1] / 2;

                    if (left > 32767) left = 32767;
                    if (left < -32768) left = -32768;
                    if (right > 32767) right = 32767;
                    if (right < -32768) right = -32768;

                    buffer[i * 2]     = (int16_t) left;
                    buffer[i * 2 + 1] = (int16_t) right;
                }

                if (pos >= hdd_seek_samples) {
                    hdd_seek_voices[v].active = 0;
                    hdd_seek_voices[v].position = 0;
                } else {
                    hdd_seek_voices[v].position = pos;
                }
            }

            thread_release_mutex(hdd_audio_mutex);
        }
    }
}