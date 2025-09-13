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
// OK 3. Implement sound support for all drives (not only for drive 0)
// OK 4. Single sector read/write sound emulation
// OK 5. Multi-track seek sound emulation
// OK 6. Volume control for drive sounds
// 7. Limit sound emulation only for 3,5" 300 rpm drives, until we have sound samples for other rpm drives
// 8. Configuration option to enable/disable drive sounds

/* Audio sample structure */
typedef struct {
    const char *filename;
    int16_t    *buffer;
    int         samples;
    float       volume;  /* Volume control: 0.0 = mute, 1.0 = full volume */
} audio_sample_t;

/* Single step audio state */
typedef struct {
    int             position;
    int             active;
} single_step_state_t;

/* Multi-track seek audio state */
typedef struct {
    int             position;
    int             active;
    int             duration_samples;
    int             from_track;
    int             to_track;
} multi_seek_state_t;

/* Static audio sample definitions */
static audio_sample_t spindlemotor_start = {
    .filename = "mitsumi_spindle_motor_start_48000_16_1_PCM.wav",
    .buffer   = NULL,
    .samples  = 0,
    .volume   = 0.2f
};

static audio_sample_t spindlemotor_loop = {
    .filename = "mitsumi_spindle_motor_loop_48000_16_1_PCM.wav",
    .buffer   = NULL,
    .samples  = 0,
    .volume   = 0.2f
};

static audio_sample_t spindlemotor_stop = {
    .filename = "mitsumi_spindle_motor_stop_48000_16_1_PCM.wav",
    .buffer   = NULL,
    .samples  = 0,
    .volume   = 0.2f
};

static audio_sample_t single_track_step = {
    .filename = "mitsumi_track_step_48000_16_1_PCM.wav",
    .buffer   = NULL,
    .samples  = 0,
    .volume   = 1.0f
};

static audio_sample_t multi_track_seek = {
    .filename = "mitsumi_seek_80_tracks_495ms_48000_16_1_PCM.wav",
    .buffer   = NULL,
    .samples  = 0,
    .volume   = 1.0f
};

/* Audio state for each drive */
static int           spindlemotor_pos[FDD_NUM]                    = {};
static motor_state_t spindlemotor_state[FDD_NUM]                  = {};
static float         spindlemotor_fade_volume[FDD_NUM]            = {};
static int           spindlemotor_fade_samples_remaining[FDD_NUM] = {};

/* Single step audio state for each drive */
static single_step_state_t single_step_state[FDD_NUM] = {};

/* Multi-track seek audio state for each drive */
static multi_seek_state_t multi_seek_state[FDD_NUM] = {};

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

    /* Load audio samples using the new structure */
    spindlemotor_start.buffer = load_wav(spindlemotor_start.filename, &spindlemotor_start.samples);
    spindlemotor_loop.buffer  = load_wav(spindlemotor_loop.filename, &spindlemotor_loop.samples);
    spindlemotor_stop.buffer  = load_wav(spindlemotor_stop.filename, &spindlemotor_stop.samples);
    single_track_step.buffer  = load_wav(single_track_step.filename, &single_track_step.samples);
    multi_track_seek.buffer   = load_wav(multi_track_seek.filename, &multi_track_seek.samples);

    /* Initialize audio state for all drives */
    for (i = 0; i < FDD_NUM; i++) {
        spindlemotor_pos[i]                    = 0;
        spindlemotor_state[i]                  = MOTOR_STATE_STOPPED;
        spindlemotor_fade_volume[i]            = 1.0f;
        spindlemotor_fade_samples_remaining[i] = 0;
        
        /* Initialize single step state */
        single_step_state[i].position = 0;
        single_step_state[i].active   = 0;
        
        /* Initialize multi-track seek state */
        multi_seek_state[i].position        = 0;
        multi_seek_state[i].active          = 0;
        multi_seek_state[i].duration_samples = 0;
        multi_seek_state[i].from_track       = -1;
        multi_seek_state[i].to_track         = -1;
    }

    /* Initialize sound thread */
    sound_fdd_thread_init();
}

void
fdd_audio_close(void)
{
    if (spindlemotor_start.buffer) {
        free(spindlemotor_start.buffer);
        spindlemotor_start.buffer = NULL;
        spindlemotor_start.samples = 0;
    }
    if (spindlemotor_loop.buffer) {
        free(spindlemotor_loop.buffer);
        spindlemotor_loop.buffer = NULL;
        spindlemotor_loop.samples = 0;
    }
    if (spindlemotor_stop.buffer) {
        free(spindlemotor_stop.buffer);
        spindlemotor_stop.buffer = NULL;
        spindlemotor_stop.samples = 0;
    }
    if (single_track_step.buffer) {
        free(single_track_step.buffer);
        single_track_step.buffer = NULL;
        single_track_step.samples = 0;
    }
    if (multi_track_seek.buffer) {
        free(multi_track_seek.buffer);
        multi_track_seek.buffer = NULL;
        multi_track_seek.samples = 0;
    }

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
    }
}

void
fdd_audio_play_single_track_step(int drive, int from_track, int to_track)
{
    if (drive < 0 || drive >= FDD_NUM)
        return;
    if (abs(from_track - to_track) != 1)
        return; /* Only single track movements */
    
    single_step_state[drive].position = 0;
    single_step_state[drive].active   = 1;
}

void
fdd_audio_play_multi_track_seek(int drive, int from_track, int to_track)
{
    if (drive < 0 || drive >= FDD_NUM)
        return;

    int track_diff = abs(from_track - to_track);
    if (track_diff <= 1)
        return; /* Use single step for 1 track movements */

    if (!multi_track_seek.buffer || multi_track_seek.samples == 0)
        return; /* No multi-track seek sample loaded */

    /* Check if a seek is already active */
    if (multi_seek_state[drive].active && 
        multi_seek_state[drive].from_track == from_track && 
        multi_seek_state[drive].to_track == to_track) {
        return;
    }

    /* Calculate duration: 495ms for 80 tracks = 6.1875ms per track at 48kHz sample rate */
    /* 6.1875ms = 0.0061875s, at 48000 Hz = 297 samples per track */
    int duration_samples = track_diff * 297; /* 6.1875ms * track_diff * 48kHz */

    /* Clamp to maximum available sample length */
    if (duration_samples > multi_track_seek.samples)
        duration_samples = multi_track_seek.samples;

    /* Start new seek (or restart interrupted seek) */
    multi_seek_state[drive].position         = 0;
    multi_seek_state[drive].active           = 1;
    multi_seek_state[drive].duration_samples = duration_samples;
    multi_seek_state[drive].from_track       = from_track;
    multi_seek_state[drive].to_track         = to_track;
}

void
fdd_audio_callback(int16_t *buffer, int length)
{
    /* Clear buffer */
    memset(buffer, 0, length * sizeof(int16_t));

    /* Check if any motor is running or transitioning, or any audio is active */
    int any_audio_active = 0;
    for (int drive = 0; drive < FDD_NUM; drive++) {
        if (spindlemotor_state[drive] != MOTOR_STATE_STOPPED || 
            single_step_state[drive].active || 
            multi_seek_state[drive].active) {
            any_audio_active = 1;
            break;
        }
    }

    if (!any_audio_active)
        return;

    float *float_buffer      = (float *) buffer;
    int    samples_in_buffer = length / 2;

    /* Process audio for all drives */
    for (int drive = 0; drive < FDD_NUM; drive++) {
        for (int i = 0; i < samples_in_buffer; i++) {
            float left_sample  = 0.0f;
            float right_sample = 0.0f;

            /* Process motor audio */
            if (spindlemotor_state[drive] != MOTOR_STATE_STOPPED) {
                switch (spindlemotor_state[drive]) {
                    case MOTOR_STATE_STARTING:
                        if (spindlemotor_start.buffer && spindlemotor_pos[drive] < spindlemotor_start.samples) {
                            /* Play start sound with volume control */
                            left_sample  = (float) spindlemotor_start.buffer[spindlemotor_pos[drive] * 2] / 32768.0f * spindlemotor_start.volume;
                            right_sample = (float) spindlemotor_start.buffer[spindlemotor_pos[drive] * 2 + 1] / 32768.0f * spindlemotor_start.volume;
                            spindlemotor_pos[drive]++;
                        } else {
                            /* Start sound finished, transition to loop */
                            spindlemotor_state[drive] = MOTOR_STATE_RUNNING;
                            spindlemotor_pos[drive]   = 0;
                        }
                        break;

                    case MOTOR_STATE_RUNNING:
                        if (spindlemotor_loop.buffer && spindlemotor_loop.samples > 0) {
                            /* Play loop sound with volume control */
                            left_sample  = (float) spindlemotor_loop.buffer[spindlemotor_pos[drive] * 2] / 32768.0f * spindlemotor_loop.volume;
                            right_sample = (float) spindlemotor_loop.buffer[spindlemotor_pos[drive] * 2 + 1] / 32768.0f * spindlemotor_loop.volume;
                            spindlemotor_pos[drive]++;

                            /* Loop back to beginning */
                            if (spindlemotor_pos[drive] >= spindlemotor_loop.samples) {
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

                            /* Get loop sample (continue from current position) with volume control */
                            if (spindlemotor_loop.buffer && spindlemotor_loop.samples > 0) {
                                int loop_pos = spindlemotor_pos[drive] % spindlemotor_loop.samples;
                                loop_left    = (float) spindlemotor_loop.buffer[loop_pos * 2] / 32768.0f * spindlemotor_loop.volume;
                                loop_right   = (float) spindlemotor_loop.buffer[loop_pos * 2 + 1] / 32768.0f * spindlemotor_loop.volume;
                            }

                            /* Get stop sample with volume control */
                            if (spindlemotor_stop.buffer && spindlemotor_pos[drive] < spindlemotor_stop.samples) {
                                stop_left  = (float) spindlemotor_stop.buffer[spindlemotor_pos[drive] * 2] / 32768.0f * spindlemotor_stop.volume;
                                stop_right = (float) spindlemotor_stop.buffer[spindlemotor_pos[drive] * 2 + 1] / 32768.0f * spindlemotor_stop.volume;
                            }

                            /* Mix the sounds */
                            left_sample  = loop_left * loop_volume + stop_left * stop_volume;
                            right_sample = loop_right * loop_volume + stop_right * stop_volume;

                            spindlemotor_pos[drive]++;
                            spindlemotor_fade_samples_remaining[drive]--;

                            /* Update fade volume */
                            spindlemotor_fade_volume[drive] = (float) spindlemotor_fade_samples_remaining[drive] / FADE_SAMPLES;
                        } else {
                            /* Fade completed, play remaining stop sound with volume control */
                            if (spindlemotor_stop.buffer && spindlemotor_pos[drive] < spindlemotor_stop.samples) {
                                left_sample  = (float) spindlemotor_stop.buffer[spindlemotor_pos[drive] * 2] / 32768.0f * spindlemotor_stop.volume;
                                right_sample = (float) spindlemotor_stop.buffer[spindlemotor_pos[drive] * 2 + 1] / 32768.0f * spindlemotor_stop.volume;
                                spindlemotor_pos[drive]++;
                            } else {
                                /* Stop sound finished */
                                spindlemotor_state[drive] = MOTOR_STATE_STOPPED;
                                /* Note: Timer disabling is handled by fdd.c, not here */
                            }
                        }
                        break;

                    default:
                        break;
                }
            }

            /* Process single step audio */
            if (single_step_state[drive].active) {                
                if (single_track_step.buffer && single_step_state[drive].position < single_track_step.samples) {
                    /* Mix step sound with motor sound with volume control */
                    float step_left  = (float) single_track_step.buffer[single_step_state[drive].position * 2] / 32768.0f * single_track_step.volume;
                    float step_right = (float) single_track_step.buffer[single_step_state[drive].position * 2 + 1] / 32768.0f * single_track_step.volume;
                    
                    left_sample  += step_left;
                    right_sample += step_right;
                    
                    single_step_state[drive].position++;
                } else {
                    /* Step sound finished */
                    single_step_state[drive].active = 0;
                    single_step_state[drive].position = 0;
                }
            }

            /* Process multi-track seek audio */
            if (multi_seek_state[drive].active) {
                if (multi_track_seek.buffer && 
                    multi_seek_state[drive].position < multi_seek_state[drive].duration_samples &&
                    multi_seek_state[drive].position < multi_track_seek.samples) {
                    /* Mix seek sound with motor sound with volume control */
                    float seek_left  = (float) multi_track_seek.buffer[multi_seek_state[drive].position * 2] / 32768.0f * multi_track_seek.volume;
                    float seek_right = (float) multi_track_seek.buffer[multi_seek_state[drive].position * 2 + 1] / 32768.0f * multi_track_seek.volume;
                    
                    left_sample  += seek_left;
                    right_sample += seek_right;
                    
                    multi_seek_state[drive].position++;
                } else {
                    /* Seek sound finished */
                    multi_seek_state[drive].active = 0;
                    multi_seek_state[drive].position = 0;
                    multi_seek_state[drive].duration_samples = 0;
                    multi_seek_state[drive].from_track       = -1;
                    multi_seek_state[drive].to_track         = -1;
                }
            }

            /* Mix this drive's audio into the buffer */
            float_buffer[i * 2]     += left_sample;
            float_buffer[i * 2 + 1] += right_sample;
        }
    }
}