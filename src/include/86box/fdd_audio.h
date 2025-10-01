/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the floppy drive audio emulation.
 *
 * Authors: Toni Riikonen, <riikonen.toni@gmail.com>
 *
 *          Copyright 2025 Toni Riikonen.
 */
#ifndef EMU_FDD_AUDIO_H
#define EMU_FDD_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DISABLE_FDD_AUDIO

/* Audio sample configuration structure */
typedef struct {
    char filename[512];
    float volume;
} audio_sample_config_t;

/* Drive type specific audio configuration */
typedef struct {
    int id;
    char name[128];
    char internal_name[64];
    audio_sample_config_t spindlemotor_start;
    audio_sample_config_t spindlemotor_loop;
    audio_sample_config_t spindlemotor_stop;
    audio_sample_config_t single_track_step;
    audio_sample_config_t multi_track_seek;
    int total_tracks;
    int samples_per_track;
    double initial_seek_time;
    double initial_seek_time_pcjr;
    double track_seek_time;
    double track_seek_time_pcjr;
} fdd_audio_profile_config_t;

#define FDD_AUDIO_PROFILE_MAX 64

/* Motor sound states */
typedef enum {
    MOTOR_STATE_STOPPED = 0,
    MOTOR_STATE_STARTING,
    MOTOR_STATE_RUNNING,
    MOTOR_STATE_STOPPING
} motor_state_t;

/* WAV header structure */
typedef struct {
    char     riff[4];
    uint32_t size;
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

/* Fade duration: 75ms at 48kHz = 3600 samples */
#define FADE_DURATION_MS 75
#define FADE_SAMPLES     (48000 * FADE_DURATION_MS / 1000)

/* Functions for configuration management */
extern void fdd_audio_load_profiles(void);
extern int fdd_audio_get_profile_count(void);
extern const fdd_audio_profile_config_t* fdd_audio_get_profile(int id);
extern const char* fdd_audio_get_profile_name(int id);
extern const char* fdd_audio_get_profile_internal_name(int id);
extern int fdd_audio_get_profile_by_internal_name(const char* internal_name);
extern double fdd_audio_get_seek_time(int drive, int is_initial, int track_count);

#else

typedef enum {
    MOTOR_STATE_STOPPED = 0
} motor_state_t;

typedef struct {
    int id;
    char name[128];
    char internal_name[64];
} fdd_audio_profile_config_t;

#define FDD_AUDIO_PROFILE_MAX 1

#endif /* DISABLE_FDD_AUDIO */

/* FDD audio initialization and cleanup */
extern void fdd_audio_init(void);
extern void fdd_audio_close(void);

/* Motor control for audio */
extern void fdd_audio_set_motor_enable(int drive, int motor_enable);

/* Single sector movement audio */
extern void fdd_audio_play_single_track_step(int drive, int from_track, int to_track);

/* Multi-track seek audio */
extern void fdd_audio_play_multi_track_seek(int drive, int from_track, int to_track);

/* Audio callback function */
extern void fdd_audio_callback(int16_t *buffer, int length);

#ifdef __cplusplus
}
#endif

#endif /*EMU_FDD_AUDIO_H*/