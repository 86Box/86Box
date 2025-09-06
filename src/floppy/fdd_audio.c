/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the floppy drive audio emulation.
 *
 * Authors: Toni Riikonen, <riikonen.toni@gmail.com>
 *
 *          Copyright 2025 Toni Riikonen.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdd_audio.h>
#include <86box/sound.h>

// TODO:
// OK 1. Implement spindle motor spin-up and spin-down
// OK 2. Move audio emulation to separate code file
// 3. Implement sound support for all drives (not only for drive 0)
// 4. Single sector read/write sound emulation
// 5. Multi-track seek sound emulation
// 6. Limit sound emulation only for 3,5" 300 rpm drives, until we have sound samples for other rpm drives

/* Static audio data */
static int16_t *spindlemotor_start_wav          = NULL;
static int      spindlemotor_start_wav_samples  = 0;
static char    *spindlemotor_start_wav_filename = "mitsumi_spindle_motor_start_48000_16_1_PCM.wav";

static int16_t *spindlemotor_loop_wav          = NULL;
static int      spindlemotor_loop_wav_samples  = 0;
static char    *spindlemotor_loop_wav_filename = "mitsumi_spindle_motor_loop_48000_16_1_PCM.wav";

static int16_t *spindlemotor_stop_wav          = NULL;
static int      spindlemotor_stop_wav_samples  = 0;
static char    *spindlemotor_stop_wav_filename = "mitsumi_spindle_motor_stop_48000_16_1_PCM.wav";

static int16_t *steptrackup_wav[80];
static int      steptrackup_wav_samples[80];
static int16_t *steptrackdown_wav[80];
static int      steptrackdown_wav_samples[80];
static int16_t *seekmultipletracks_wav[79]; /* Seek 2, 3, 4 ... 80 tracks = 79 sounds */
static int      seekmultipletracks_wav_samples[79];

/* Audio state for each drive */
static int           spindlemotor_pos[FDD_NUM]                    = {};
static motor_state_t spindlemotor_state[FDD_NUM]                  = {};
static float         spindlemotor_fade_volume[FDD_NUM]            = {};
static int           spindlemotor_fade_samples_remaining[FDD_NUM] = {};

/* External references to FDD timer and motoron state */
extern pc_timer_t fdd_poll_time[FDD_NUM];
extern uint64_t   motoron[FDD_NUM];

/* Forward declaration */
static int16_t *load_wav(const char *filename, int *sample_count);

const char *
fdd_audio_motor_state_name(motor_state_t state)
{
    switch (state) {
        case MOTOR_STATE_STOPPED:
            return "STOPPED";
        case MOTOR_STATE_STARTING:
            return "STARTING";
        case MOTOR_STATE_RUNNING:
            return "RUNNING";
        case MOTOR_STATE_STOPPING:
            return "STOPPING";
        default:
            return "UNKNOWN";
    }
}

static int16_t *
load_wav(const char *filename, int *sample_count)
{
    FILE *f = fopen(filename, "rb");
    if (!f)
        return NULL;

    wav_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
        fclose(f);
        return NULL;
    }

    if (memcmp(hdr.riff, "RIFF", 4) || memcmp(hdr.wave, "WAVE", 4) || memcmp(hdr.fmt, "fmt ", 4) || memcmp(hdr.data, "data", 4)) {
        fclose(f);
        return NULL;
    }

    /* Accept both mono and stereo, 16-bit PCM */
    if (hdr.audio_format != 1 || hdr.bits_per_sample != 16 || (hdr.num_channels != 1 && hdr.num_channels != 2)) {
        fclose(f);
        return NULL;
    }

    int      input_samples = hdr.data_size / 2; /* 2 bytes per sample */
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
        output_samples = input_samples;                               /* Number of stereo sample pairs */
        output_data    = malloc(input_samples * 2 * sizeof(int16_t)); /* Allocate for stereo */
        if (!output_data) {
            free(input_data);
            return NULL;
        }

        /* Convert mono to stereo by duplicating each sample */
        for (int i = 0; i < input_samples; i++) {
            output_data[i * 2]     = input_data[i]; /* Left channel */
            output_data[i * 2 + 1] = input_data[i]; /* Right channel */
        }

        free(input_data);
    } else {
        /* Already stereo */
        output_data    = input_data;
        output_samples = input_samples / 2; /* Number of stereo sample pairs */
    }

    if (sample_count)
        *sample_count = output_samples;

    return output_data;
}

void
fdd_audio_init(void)
{
    int i;

    /* Load audio samples */
    spindlemotor_start_wav = load_wav(spindlemotor_start_wav_filename, &spindlemotor_start_wav_samples);
    spindlemotor_loop_wav  = load_wav(spindlemotor_loop_wav_filename, &spindlemotor_loop_wav_samples);
    spindlemotor_stop_wav  = load_wav(spindlemotor_stop_wav_filename, &spindlemotor_stop_wav_samples);

    /* Initialize audio state for all drives */
    for (i = 0; i < FDD_NUM; i++) {
        spindlemotor_pos[i]                    = 0;
        spindlemotor_state[i]                  = MOTOR_STATE_STOPPED;
        spindlemotor_fade_volume[i]            = 1.0f;
        spindlemotor_fade_samples_remaining[i] = 0;
    }

    /* Initialize sound thread */
    sound_fdd_thread_init();
}

void
fdd_audio_close(void)
{
    /* Free loaded audio samples */
    if (spindlemotor_start_wav) {
        free(spindlemotor_start_wav);
        spindlemotor_start_wav = NULL;
    }
    if (spindlemotor_loop_wav) {
        free(spindlemotor_loop_wav);
        spindlemotor_loop_wav = NULL;
    }
    if (spindlemotor_stop_wav) {
        free(spindlemotor_stop_wav);
        spindlemotor_stop_wav = NULL;
    }

    /* TODO: Free seek sound arrays when they are implemented */

    /* End sound thread */
    sound_fdd_thread_end();
}

void
fdd_audio_set_motor_enable(int drive, int motor_enable)
{
    if (motor_enable && !motoron[drive]) {
        /* Motor starting up */
        if (spindlemotor_state[drive] == MOTOR_STATE_STOPPING) {
            /* Interrupt stop sequence and transition back to loop */
            spindlemotor_state[drive]                  = MOTOR_STATE_RUNNING;
            spindlemotor_pos[drive]                    = 0;
            spindlemotor_fade_volume[drive]            = 1.0f;
            spindlemotor_fade_samples_remaining[drive] = 0;
        } else {
            /* Normal startup */
            spindlemotor_state[drive]                  = MOTOR_STATE_STARTING;
            spindlemotor_pos[drive]                    = 0;
            spindlemotor_fade_volume[drive]            = 1.0f;
            spindlemotor_fade_samples_remaining[drive] = 0;
        }
    } else if (!motor_enable && motoron[drive]) {
        /* Motor stopping */
        spindlemotor_state[drive]                  = MOTOR_STATE_STOPPING;
        spindlemotor_pos[drive]                    = 0;
        spindlemotor_fade_volume[drive]            = 1.0f;
        spindlemotor_fade_samples_remaining[drive] = FADE_SAMPLES;
        /* Don't disable timer yet - let the stop sound finish */
    }
}

void
fdd_audio_callback(int16_t *buffer, int length)
{
    /* Clear buffer */
    memset(buffer, 0, length * sizeof(int16_t));

    /* Check if any motor is running or transitioning */
    int any_motor_active = 0;
    for (int drive = 0; drive < FDD_NUM; drive++) {
        if (spindlemotor_state[drive] != MOTOR_STATE_STOPPED) {
            any_motor_active = 1;
            break;
        }
    }

    if (!any_motor_active)
        return;

    float *float_buffer      = (float *) buffer;
    int    samples_in_buffer = length / 2;

    /* Process audio for drive 0 only for now */
    int drive = 0;

    if (spindlemotor_state[drive] == MOTOR_STATE_STOPPED)
        return;

    for (int i = 0; i < samples_in_buffer; i++) {
        float left_sample  = 0.0f;
        float right_sample = 0.0f;

        switch (spindlemotor_state[drive]) {
            case MOTOR_STATE_STARTING:
                if (spindlemotor_start_wav && spindlemotor_pos[drive] < spindlemotor_start_wav_samples) {
                    /* Play start sound */
                    left_sample  = (float) spindlemotor_start_wav[spindlemotor_pos[drive] * 2] / 32768.0f;
                    right_sample = (float) spindlemotor_start_wav[spindlemotor_pos[drive] * 2 + 1] / 32768.0f;
                    spindlemotor_pos[drive]++;
                } else {
                    /* Start sound finished, transition to loop */
                    spindlemotor_state[drive] = MOTOR_STATE_RUNNING;
                    spindlemotor_pos[drive]   = 0;
                }
                break;

            case MOTOR_STATE_RUNNING:
                if (spindlemotor_loop_wav && spindlemotor_loop_wav_samples > 0) {
                    /* Play loop sound */
                    left_sample  = (float) spindlemotor_loop_wav[spindlemotor_pos[drive] * 2] / 32768.0f;
                    right_sample = (float) spindlemotor_loop_wav[spindlemotor_pos[drive] * 2 + 1] / 32768.0f;
                    spindlemotor_pos[drive]++;

                    /* Loop back to beginning */
                    if (spindlemotor_pos[drive] >= spindlemotor_loop_wav_samples) {
                        spindlemotor_pos[drive] = 0;
                    }
                }
                break;

            case MOTOR_STATE_STOPPING:
                if (spindlemotor_fade_samples_remaining[drive] > 0) {
                    /* Mix fading loop sound with rising stop sound */
                    float loop_volume = spindlemotor_fade_volume[drive];
                    float stop_volume = 1.0f - loop_volume;

                    float loop_left = 0.0f, loop_right = 0.0f;
                    float stop_left = 0.0f, stop_right = 0.0f;

                    /* Get loop sample (continue from current position) */
                    if (spindlemotor_loop_wav && spindlemotor_loop_wav_samples > 0) {
                        int loop_pos = spindlemotor_pos[drive] % spindlemotor_loop_wav_samples;
                        loop_left    = (float) spindlemotor_loop_wav[loop_pos * 2] / 32768.0f;
                        loop_right   = (float) spindlemotor_loop_wav[loop_pos * 2 + 1] / 32768.0f;
                    }

                    /* Get stop sample */
                    if (spindlemotor_stop_wav && spindlemotor_pos[drive] < spindlemotor_stop_wav_samples) {
                        stop_left  = (float) spindlemotor_stop_wav[spindlemotor_pos[drive] * 2] / 32768.0f;
                        stop_right = (float) spindlemotor_stop_wav[spindlemotor_pos[drive] * 2 + 1] / 32768.0f;
                    }

                    /* Mix the sounds */
                    left_sample  = loop_left * loop_volume + stop_left * stop_volume;
                    right_sample = loop_right * loop_volume + stop_right * stop_volume;

                    spindlemotor_pos[drive]++;
                    spindlemotor_fade_samples_remaining[drive]--;

                    /* Update fade volume */
                    spindlemotor_fade_volume[drive] = (float) spindlemotor_fade_samples_remaining[drive] / FADE_SAMPLES;
                } else {
                    /* Fade completed, play remaining stop sound */
                    if (spindlemotor_stop_wav && spindlemotor_pos[drive] < spindlemotor_stop_wav_samples) {
                        left_sample  = (float) spindlemotor_stop_wav[spindlemotor_pos[drive] * 2] / 32768.0f;
                        right_sample = (float) spindlemotor_stop_wav[spindlemotor_pos[drive] * 2 + 1] / 32768.0f;
                        spindlemotor_pos[drive]++;
                    } else {
                        /* Stop sound finished */
                        spindlemotor_state[drive] = MOTOR_STATE_STOPPED;
                        timer_disable(&fdd_poll_time[drive]);
                    }
                }
                break;

            default:
                break;
        }

        float_buffer[i * 2]     = left_sample;
        float_buffer[i * 2 + 1] = right_sample;
    }
}