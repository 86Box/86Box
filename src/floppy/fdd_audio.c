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
#include <ctype.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdd_audio.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/sound.h>
#include <86box/plat.h>
#include <86box/path.h>

#ifndef DISABLE_FDD_AUDIO

/* Audio sample structure */
typedef struct {
    const char *filename;
    int16_t    *buffer;
    int         samples;
    float       volume;
} audio_sample_t;

/* Single step audio state */
typedef struct {
    int position;
    int active;
} single_step_state_t;

/* Multi-track seek audio state */
typedef struct {
    int position;
    int active;
    int duration_samples;
    int from_track;
    int to_track;
} multi_seek_state_t;

/* Drive type specific audio samples */
typedef struct {
    audio_sample_t spindlemotor_start;
    audio_sample_t spindlemotor_loop;
    audio_sample_t spindlemotor_stop;
    audio_sample_t single_track_step;
    audio_sample_t multi_track_seek;
} drive_audio_samples_t;

/* 5.25" Teac FD-55GFR sample set */
static drive_audio_samples_t samples_teac = {
    .spindlemotor_start = {
        .filename = "roms/floppy/samples/TeacFD-55GFR_5.25_1.2MB_motor_start_48000_16_1_PCM.wav",
        .buffer   = NULL, .samples = 0, .volume = 3.0f
    },
    .spindlemotor_loop = {
        .filename = "roms/floppy/samples/TeacFD-55GFR_5.25_1.2MB_motor_loop_48000_16_1_PCM.wav",
        .buffer   = NULL, .samples = 0, .volume = 3.0f
    },
    .spindlemotor_stop = {
        .filename = "roms/floppy/samples/TeacFD-55GFR_5.25_1.2MB_motor_stop_48000_16_1_PCM.wav",
        .buffer   = NULL, .samples = 0, .volume = 3.0f
    },
    .single_track_step = {
        .filename = "roms/floppy/samples/TeacFD-55GFR_5.25_1.2MB_track_step_48000_16_1_PCM.wav",
        .buffer   = NULL, .samples = 0, .volume = 2.0f
    },
    .multi_track_seek = {
        .filename = "roms/floppy/samples/TeacFD_55GFR_5.25_1.2MB_seekupdown_80_tracks1100ms_48000_16_1_PCM.wav",
        .buffer   = NULL, .samples = 0, .volume = 2.0f
    }
};

/* 3.5" drive audio samples (Mitsumi) */
static drive_audio_samples_t samples_35 = {
    .spindlemotor_start = {
        .filename = "roms/floppy/samples/mitsumi_spindle_motor_start_48000_16_1_PCM.wav",
        .buffer   = NULL, .samples  = 0, .volume   = 0.2f
    },
    .spindlemotor_loop = {
        .filename = "roms/floppy/samples/mitsumi_spindle_motor_loop_48000_16_1_PCM.wav",
        .buffer   = NULL, .samples  = 0, .volume   = 0.2f
    },
    .spindlemotor_stop = {
        .filename = "roms/floppy/samples/mitsumi_spindle_motor_stop_48000_16_1_PCM.wav",
        .buffer   = NULL, .samples  = 0, .volume   = 0.2f
    },
    .single_track_step = {
        .filename = "roms/floppy/samples/mitsumi_track_step_48000_16_1_PCM.wav",
        .buffer   = NULL, .samples  = 0, .volume   = 1.0f
    },
    .multi_track_seek = {
        .filename = "roms/floppy/samples/mitsumi_seek_80_tracks_495ms_48000_16_1_PCM.wav",
        .buffer   = NULL, .samples  = 0, .volume   = 1.0f
    }
};

/* 5.25" drive audio samples (Panasonic) */
static drive_audio_samples_t samples_525 = {
    .spindlemotor_start = {
        .filename = "roms/floppy/samples/Panasonic_JU-475-5_5.25_1.2MB_motor_start_48000_16_1_PCM.wav",
        .buffer   = NULL, .samples  = 0, .volume   = 1.0f
    },
    .spindlemotor_loop = {
        .filename = "roms/floppy/samples/Panasonic_JU-475-5_5.25_1.2MB_motor_loop_48000_16_1_PCM.wav",
        .buffer   = NULL, .samples  = 0, .volume   = 1.0f
    },
    .spindlemotor_stop = {
        .filename = "roms/floppy/samples/Panasonic_JU-475-5_5.25_1.2MB_motor_stop_48000_16_1_PCM.wav",
        .buffer   = NULL, .samples  = 0, .volume   = 1.0f
    },
    .single_track_step = {
        .filename = "roms/floppy/samples/Panasonic_JU-475-5_5.25_1.2MB_track_step_48000_16_1_PCM.wav",
        .buffer   = NULL, .samples  = 0, .volume   = 2.0f
    },
    .multi_track_seek = {
        .filename = "roms/floppy/samples/Panasonic_JU-475-5_5.25_1.2MB_seekup_40_tracks_285ms_5ms_per_track_48000_16_1_PCM.wav",
        .buffer   = NULL, .samples  = 0, .volume   = 2.0f
    }
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

extern uint64_t motoron[FDD_NUM];
extern char     exe_path[2048]; /* path (dir) of executable */

extern int fdd_get_audio_profile(int drive); /* from fdd.h */

/* Forward declaration */
static int16_t *load_wav(const char *filename, int *sample_count);


static drive_audio_samples_t *
get_drive_samples(int drive)
{
    switch (fdd_get_audio_profile(drive)) {
        case FDD_AUDIO_PROFILE_PANASONIC:
            return &samples_525;
        case FDD_AUDIO_PROFILE_TEAC:
            return &samples_teac;
        case FDD_AUDIO_PROFILE_MITSUMI:
            return &samples_35;
        default:
            return NULL;
    }
}

static int16_t *
load_wav(const char *filename, int *sample_count)
{
    FILE *f = NULL;

    if ((filename == NULL) || (strlen(filename) == 0))
        return NULL;

    if (strstr(filename, "..") != NULL)
        return NULL;

    f = rom_fopen(filename, "rb");
    if (f == NULL)
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

    /* Load audio samples for both drive types */
    samples_35.spindlemotor_start.buffer = load_wav(samples_35.spindlemotor_start.filename, &samples_35.spindlemotor_start.samples);
    samples_35.spindlemotor_loop.buffer  = load_wav(samples_35.spindlemotor_loop.filename, &samples_35.spindlemotor_loop.samples);
    samples_35.spindlemotor_stop.buffer  = load_wav(samples_35.spindlemotor_stop.filename, &samples_35.spindlemotor_stop.samples);
    samples_35.single_track_step.buffer  = load_wav(samples_35.single_track_step.filename, &samples_35.single_track_step.samples);
    samples_35.multi_track_seek.buffer   = load_wav(samples_35.multi_track_seek.filename, &samples_35.multi_track_seek.samples);

    samples_525.spindlemotor_start.buffer = load_wav(samples_525.spindlemotor_start.filename, &samples_525.spindlemotor_start.samples);
    samples_525.spindlemotor_loop.buffer  = load_wav(samples_525.spindlemotor_loop.filename, &samples_525.spindlemotor_loop.samples);
    samples_525.spindlemotor_stop.buffer  = load_wav(samples_525.spindlemotor_stop.filename, &samples_525.spindlemotor_stop.samples);
    samples_525.single_track_step.buffer  = load_wav(samples_525.single_track_step.filename, &samples_525.single_track_step.samples);
    samples_525.multi_track_seek.buffer   = load_wav(samples_525.multi_track_seek.filename, &samples_525.multi_track_seek.samples);

    samples_teac.spindlemotor_start.buffer = load_wav(samples_teac.spindlemotor_start.filename, &samples_teac.spindlemotor_start.samples);
    samples_teac.spindlemotor_loop.buffer  = load_wav(samples_teac.spindlemotor_loop.filename, &samples_teac.spindlemotor_loop.samples);
    samples_teac.spindlemotor_stop.buffer  = load_wav(samples_teac.spindlemotor_stop.filename, &samples_teac.spindlemotor_stop.samples);
    samples_teac.single_track_step.buffer  = load_wav(samples_teac.single_track_step.filename, &samples_teac.single_track_step.samples);
    samples_teac.multi_track_seek.buffer   = load_wav(samples_teac.multi_track_seek.filename, &samples_teac.multi_track_seek.samples);

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
        multi_seek_state[i].position         = 0;
        multi_seek_state[i].active           = 0;
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
    /* Free 3.5" samples */
    if (samples_35.spindlemotor_start.buffer) {
        free(samples_35.spindlemotor_start.buffer);
        samples_35.spindlemotor_start.buffer  = NULL;
        samples_35.spindlemotor_start.samples = 0;
    }
    if (samples_35.spindlemotor_loop.buffer) {
        free(samples_35.spindlemotor_loop.buffer);
        samples_35.spindlemotor_loop.buffer  = NULL;
        samples_35.spindlemotor_loop.samples = 0;
    }
    if (samples_35.spindlemotor_stop.buffer) {
        free(samples_35.spindlemotor_stop.buffer);
        samples_35.spindlemotor_stop.buffer  = NULL;
        samples_35.spindlemotor_stop.samples = 0;
    }
    if (samples_35.single_track_step.buffer) {
        free(samples_35.single_track_step.buffer);
        samples_35.single_track_step.buffer  = NULL;
        samples_35.single_track_step.samples = 0;
    }
    if (samples_35.multi_track_seek.buffer) {
        free(samples_35.multi_track_seek.buffer);
        samples_35.multi_track_seek.buffer  = NULL;
        samples_35.multi_track_seek.samples = 0;
    }

    /* Free 5.25" samples */
    if (samples_525.spindlemotor_start.buffer) {
        free(samples_525.spindlemotor_start.buffer);
        samples_525.spindlemotor_start.buffer  = NULL;
        samples_525.spindlemotor_start.samples = 0;
    }
    if (samples_525.spindlemotor_loop.buffer) {
        free(samples_525.spindlemotor_loop.buffer);
        samples_525.spindlemotor_loop.buffer  = NULL;
        samples_525.spindlemotor_loop.samples = 0;
    }
    if (samples_525.spindlemotor_stop.buffer) {
        free(samples_525.spindlemotor_stop.buffer);
        samples_525.spindlemotor_stop.buffer  = NULL;
        samples_525.spindlemotor_stop.samples = 0;
    }
    if (samples_525.single_track_step.buffer) {
        free(samples_525.single_track_step.buffer);
        samples_525.single_track_step.buffer  = NULL;
        samples_525.single_track_step.samples = 0;
    }
    if (samples_525.multi_track_seek.buffer) {
        free(samples_525.multi_track_seek.buffer);
        samples_525.multi_track_seek.buffer  = NULL;
        samples_525.multi_track_seek.samples = 0;
    }

    if (samples_teac.spindlemotor_start.buffer) {
        free(samples_teac.spindlemotor_start.buffer);
        samples_teac.spindlemotor_start.buffer  = NULL;
        samples_teac.spindlemotor_start.samples = 0;
    }
    if (samples_teac.spindlemotor_loop.buffer) {
        free(samples_teac.spindlemotor_loop.buffer);
        samples_teac.spindlemotor_loop.buffer  = NULL;
        samples_teac.spindlemotor_loop.samples = 0;
    }
    if (samples_teac.spindlemotor_stop.buffer) {
        free(samples_teac.spindlemotor_stop.buffer);
        samples_teac.spindlemotor_stop.buffer  = NULL;
        samples_teac.spindlemotor_stop.samples = 0;
    }
    if (samples_teac.single_track_step.buffer) {
        free(samples_teac.single_track_step.buffer);
        samples_teac.single_track_step.buffer  = NULL;
        samples_teac.single_track_step.samples = 0;
    }
    if (samples_teac.multi_track_seek.buffer) {
        free(samples_teac.multi_track_seek.buffer);
        samples_teac.multi_track_seek.buffer  = NULL;
        samples_teac.multi_track_seek.samples = 0;
    }

    /* End sound thread */
    sound_fdd_thread_end();
}

void
fdd_audio_set_motor_enable(int drive, int motor_enable)
{
    if (!fdd_sounds_enabled || fdd_get_turbo(drive))
        return;

    drive_audio_samples_t *samples = get_drive_samples(drive);
    if (!samples)
        return;

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
    if (!fdd_sounds_enabled || fdd_get_turbo(drive))
        return;

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
    if (!fdd_sounds_enabled || fdd_get_turbo(drive))
        return;

    if (drive < 0 || drive >= FDD_NUM)
        return;

    int track_diff = abs(from_track - to_track);
    if (track_diff <= 1)
        return; /* Use single step for 1 track movements */

    drive_audio_samples_t *samples = get_drive_samples(drive);
    if (!samples || !samples->multi_track_seek.buffer || samples->multi_track_seek.samples == 0)
        return; /* No multi-track seek sample loaded */

    /* Check if a seek is already active */
    if (multi_seek_state[drive].active && 
        multi_seek_state[drive].from_track == from_track && 
        multi_seek_state[drive].to_track == to_track) {
        return;
    }

    /* Calculate duration based on drive type */
    int duration_samples;
    if (fdd_is_525(drive)) {
        /* 5.25": 285ms for 40 tracks = 7.125ms per track at 48kHz sample rate */
        /* 7.125ms = 0.007125s, at 48000 Hz = 342 samples per track */
        duration_samples = track_diff * 342;
    } else {
        /* 3.5": 495ms for 80 tracks = 6.1875ms per track at 48kHz sample rate */
        /* 6.1875ms = 0.0061875s, at 48000 Hz = 297 samples per track */
        duration_samples = track_diff * 297;
    }

    /* Clamp to maximum available sample length */
    if (duration_samples > samples->multi_track_seek.samples)
        duration_samples = samples->multi_track_seek.samples;

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

    float   *float_buffer      = (float *) buffer;
    int16_t *int16_buffer      = (int16_t *) buffer;
    int      samples_in_buffer = length / 2;

    /* Process audio for all drives */
    if (sound_is_float)  for (int drive = 0; drive < FDD_NUM; drive++) {
        drive_audio_samples_t *samples = get_drive_samples(drive);
        if (!samples)
            continue;
        
        for (int i = 0; i < samples_in_buffer; i++) {
            float left_sample  = 0.0f;
            float right_sample = 0.0f;

            /* Process motor audio */
            if (spindlemotor_state[drive] != MOTOR_STATE_STOPPED) {
                switch (spindlemotor_state[drive]) {
                    case MOTOR_STATE_STARTING:
                        if (samples->spindlemotor_start.buffer && spindlemotor_pos[drive] < samples->spindlemotor_start.samples) {
                            /* Play start sound with volume control */
                            left_sample  = (float) samples->spindlemotor_start.buffer[spindlemotor_pos[drive] * 2] / 131072.0f * samples->spindlemotor_start.volume;
                            right_sample = (float) samples->spindlemotor_start.buffer[spindlemotor_pos[drive] * 2 + 1] / 131072.0f * samples->spindlemotor_start.volume;
                            spindlemotor_pos[drive]++;
                        } else {
                            /* Start sound finished, transition to loop */
                            spindlemotor_state[drive] = MOTOR_STATE_RUNNING;
                            spindlemotor_pos[drive]   = 0;
                        }
                        break;

                    case MOTOR_STATE_RUNNING:
                        if (samples->spindlemotor_loop.buffer && samples->spindlemotor_loop.samples > 0) {
                            /* Play loop sound with volume control */
                            left_sample  = (float) samples->spindlemotor_loop.buffer[spindlemotor_pos[drive] * 2] / 131072.0f * samples->spindlemotor_loop.volume;
                            right_sample = (float) samples->spindlemotor_loop.buffer[spindlemotor_pos[drive] * 2 + 1] / 131072.0f * samples->spindlemotor_loop.volume;
                            spindlemotor_pos[drive]++;

                            /* Loop back to beginning */
                            if (spindlemotor_pos[drive] >= samples->spindlemotor_loop.samples) {
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
                            if (samples->spindlemotor_loop.buffer && samples->spindlemotor_loop.samples > 0) {
                                int loop_pos = spindlemotor_pos[drive] % samples->spindlemotor_loop.samples;
                                loop_left    = (float) samples->spindlemotor_loop.buffer[loop_pos * 2] / 131072.0f * samples->spindlemotor_loop.volume;
                                loop_right   = (float) samples->spindlemotor_loop.buffer[loop_pos * 2 + 1] / 131072.0f * samples->spindlemotor_loop.volume;
                            }

                            /* Get stop sample with volume control */
                            if (samples->spindlemotor_stop.buffer && spindlemotor_pos[drive] < samples->spindlemotor_stop.samples) {
                                stop_left  = (float) samples->spindlemotor_stop.buffer[spindlemotor_pos[drive] * 2] / 131072.0f * samples->spindlemotor_stop.volume;
                                stop_right = (float) samples->spindlemotor_stop.buffer[spindlemotor_pos[drive] * 2 + 1] / 131072.0f * samples->spindlemotor_stop.volume;
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
                            if (samples->spindlemotor_stop.buffer && spindlemotor_pos[drive] < samples->spindlemotor_stop.samples) {
                                left_sample  = (float) samples->spindlemotor_stop.buffer[spindlemotor_pos[drive] * 2] / 131072.0f * samples->spindlemotor_stop.volume;
                                right_sample = (float) samples->spindlemotor_stop.buffer[spindlemotor_pos[drive] * 2 + 1] / 131072.0f * samples->spindlemotor_stop.volume;
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
                if (samples->single_track_step.buffer && single_step_state[drive].position < samples->single_track_step.samples) {
                    /* Mix step sound with motor sound with volume control */
                    float step_left  = (float) samples->single_track_step.buffer[single_step_state[drive].position * 2] / 131072.0f * samples->single_track_step.volume;
                    float step_right = (float) samples->single_track_step.buffer[single_step_state[drive].position * 2 + 1] / 131072.0f * samples->single_track_step.volume;

                    left_sample += step_left;
                    right_sample += step_right;

                    single_step_state[drive].position++;
                } else {
                    /* Step sound finished */
                    single_step_state[drive].active   = 0;
                    single_step_state[drive].position = 0;
                }
            }

            /* Process multi-track seek audio */
            if (multi_seek_state[drive].active) {
                if (samples->multi_track_seek.buffer && 
                    multi_seek_state[drive].position < multi_seek_state[drive].duration_samples &&
                    multi_seek_state[drive].position < samples->multi_track_seek.samples) {
                    /* Mix seek sound with motor sound with volume control */
                    float seek_left  = (float) samples->multi_track_seek.buffer[multi_seek_state[drive].position * 2] / 131072.0f * samples->multi_track_seek.volume;
                    float seek_right = (float) samples->multi_track_seek.buffer[multi_seek_state[drive].position * 2 + 1] / 131072.0f * samples->multi_track_seek.volume;

                    left_sample += seek_left;
                    right_sample += seek_right;

                    multi_seek_state[drive].position++;
                } else {
                    /* Seek sound finished */
                    multi_seek_state[drive].active           = 0;
                    multi_seek_state[drive].position         = 0;
                    multi_seek_state[drive].duration_samples = 0;
                    multi_seek_state[drive].from_track       = -1;
                    multi_seek_state[drive].to_track         = -1;
                }
            }

            /* Mix this drive's audio into the buffer */
            float_buffer[i * 2] += left_sample;
            float_buffer[i * 2 + 1] += right_sample;
        }
    } else  for (int drive = 0; drive < FDD_NUM; drive++) {
        drive_audio_samples_t *samples = get_drive_samples(drive);
        if (!samples)
            continue;
        
        for (int i = 0; i < samples_in_buffer; i++) {
            int16_t left_sample  = 0.0f;
            int16_t right_sample = 0.0f;

            /* Process motor audio */
            if (spindlemotor_state[drive] != MOTOR_STATE_STOPPED) {
                switch (spindlemotor_state[drive]) {
                    case MOTOR_STATE_STARTING:
                        if (samples->spindlemotor_start.buffer && spindlemotor_pos[drive] < samples->spindlemotor_start.samples) {
                            /* Play start sound with volume control */
                            left_sample  = (int16_t) (float) samples->spindlemotor_start.buffer[spindlemotor_pos[drive] * 2] / 4.0f * samples->spindlemotor_start.volume;
                            right_sample = (int16_t) (float) samples->spindlemotor_start.buffer[spindlemotor_pos[drive] * 2 + 1] / 4.0f * samples->spindlemotor_start.volume;
                            spindlemotor_pos[drive]++;
                        } else {
                            /* Start sound finished, transition to loop */
                            spindlemotor_state[drive] = MOTOR_STATE_RUNNING;
                            spindlemotor_pos[drive]   = 0;
                        }
                        break;

                    case MOTOR_STATE_RUNNING:
                        if (samples->spindlemotor_loop.buffer && samples->spindlemotor_loop.samples > 0) {
                            /* Play loop sound with volume control */
                            left_sample  = (int16_t) (float) samples->spindlemotor_loop.buffer[spindlemotor_pos[drive] * 2] / 4.0f * samples->spindlemotor_loop.volume;
                            right_sample = (int16_t) (float) samples->spindlemotor_loop.buffer[spindlemotor_pos[drive] * 2 + 1] / 4.0f * samples->spindlemotor_loop.volume;
                            spindlemotor_pos[drive]++;

                            /* Loop back to beginning */
                            if (spindlemotor_pos[drive] >= samples->spindlemotor_loop.samples) {
                                spindlemotor_pos[drive] = 0;
                            }
                        }
                        break;

                    case MOTOR_STATE_STOPPING:
                        if (spindlemotor_fade_samples_remaining[drive] > 0) {
                            /* Mix fading loop sound with rising stop sound */
                            float loop_volume = spindlemotor_fade_volume[drive];
                            float stop_volume = 1.0f - loop_volume;

                            int16_t loop_left = 0x0000, loop_right = 0x0000;
                            int16_t stop_left = 0x0000, stop_right = 0x0000;

                            /* Get loop sample (continue from current position) with volume control */
                            if (samples->spindlemotor_loop.buffer && samples->spindlemotor_loop.samples > 0) {
                                int loop_pos = spindlemotor_pos[drive] % samples->spindlemotor_loop.samples;
                                loop_left    = (int16_t) (float) samples->spindlemotor_loop.buffer[loop_pos * 2] / 4.0f * samples->spindlemotor_loop.volume;
                                loop_right   = (int16_t) (float) samples->spindlemotor_loop.buffer[loop_pos * 2 + 1] / 4.0f * samples->spindlemotor_loop.volume;
                            }

                            /* Get stop sample with volume control */
                            if (samples->spindlemotor_stop.buffer && spindlemotor_pos[drive] < samples->spindlemotor_stop.samples) {
                                stop_left  = (int16_t) (float) samples->spindlemotor_stop.buffer[spindlemotor_pos[drive] * 2] / 4.0f * samples->spindlemotor_stop.volume;
                                stop_right = (int16_t) samples->spindlemotor_stop.buffer[spindlemotor_pos[drive] * 2 + 1] / 4.0f * samples->spindlemotor_stop.volume;
                            }

                            /* Mix the sounds */
                            left_sample  = loop_left * loop_volume + stop_left * stop_volume;
                            right_sample = loop_right * loop_volume + stop_right * stop_volume;

                            spindlemotor_pos[drive]++;
                            spindlemotor_fade_samples_remaining[drive]--;

                            /* Update fade volume */
                            spindlemotor_fade_volume[drive] = (int16_t) (float) spindlemotor_fade_samples_remaining[drive] / FADE_SAMPLES;
                        } else {
                            /* Fade completed, play remaining stop sound with volume control */
                            if (samples->spindlemotor_stop.buffer && spindlemotor_pos[drive] < samples->spindlemotor_stop.samples) {
                                left_sample  = (int16_t) (float) samples->spindlemotor_stop.buffer[spindlemotor_pos[drive] * 2] / 4.0f * samples->spindlemotor_stop.volume;
                                right_sample = (int16_t) (float) samples->spindlemotor_stop.buffer[spindlemotor_pos[drive] * 2 + 1] / 4.0f * samples->spindlemotor_stop.volume;
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
                if (samples->single_track_step.buffer && single_step_state[drive].position < samples->single_track_step.samples) {
                    /* Mix step sound with motor sound with volume control */
                    int16_t step_left  = (int16_t) (float) samples->single_track_step.buffer[single_step_state[drive].position * 2] / 4.0f * samples->single_track_step.volume;
                    int16_t step_right = (int16_t) (float) samples->single_track_step.buffer[single_step_state[drive].position * 2 + 1] / 4.0f * samples->single_track_step.volume;

                    left_sample += step_left;
                    right_sample += step_right;

                    single_step_state[drive].position++;
                } else {
                    /* Step sound finished */
                    single_step_state[drive].active   = 0;
                    single_step_state[drive].position = 0;
                }
            }

            /* Process multi-track seek audio */
            if (multi_seek_state[drive].active) {
                if (samples->multi_track_seek.buffer && 
                    multi_seek_state[drive].position < multi_seek_state[drive].duration_samples &&
                    multi_seek_state[drive].position < samples->multi_track_seek.samples) {
                    /* Mix seek sound with motor sound with volume control */
                    int16_t seek_left  = (int16_t) (float) samples->multi_track_seek.buffer[multi_seek_state[drive].position * 2] / 4.0f * samples->multi_track_seek.volume;
                    int16_t seek_right = (int16_t) (float) samples->multi_track_seek.buffer[multi_seek_state[drive].position * 2 + 1] / 4.0f * samples->multi_track_seek.volume;

                    left_sample += seek_left;
                    right_sample += seek_right;

                    multi_seek_state[drive].position++;
                } else {
                    /* Seek sound finished */
                    multi_seek_state[drive].active           = 0;
                    multi_seek_state[drive].position         = 0;
                    multi_seek_state[drive].duration_samples = 0;
                    multi_seek_state[drive].from_track       = -1;
                    multi_seek_state[drive].to_track         = -1;
                }
            }

            /* Mix this drive's audio into the buffer */
            int16_buffer[i * 2] += left_sample;
            int16_buffer[i * 2 + 1] += right_sample;
        }
    }
}
#else

void fdd_audio_init(void) {}
void fdd_audio_close(void) {}
void fdd_audio_set_motor_enable(int drive, int motor_enable) { (void) drive; (void) motor_enable; }
void fdd_audio_play_single_track_step(int drive, int from_track, int to_track) { (void) drive; (void) from_track; (void) to_track; }
void fdd_audio_play_multi_track_seek(int drive, int from_track, int to_track) { (void) drive; (void) from_track; (void) to_track; }
void fdd_audio_callback(int16_t *buffer, int length) { memset(buffer, 0, length * sizeof(int16_t)); }

#endif /* DISABLE_FDD_AUDIO */