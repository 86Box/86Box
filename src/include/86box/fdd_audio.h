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
#include <86box/fdd.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DISABLE_FDD_AUDIO

/* Maximum number of seek samples (for 80-track drives: 1-79 tracks) */
#define MAX_SEEK_SAMPLES 79

/* Maximum number of simultaneous seek sounds per drive */
#define MAX_CONCURRENT_SEEKS 8

/* Number of BIOS vendors (for BIOS-specific samples) */
#define BIOS_VENDOR_COUNT 7

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
    audio_sample_config_t seek_up[MAX_SEEK_SAMPLES];
    audio_sample_config_t seek_down[MAX_SEEK_SAMPLES];
    audio_sample_config_t post_seek_up[MAX_SEEK_SAMPLES];
    audio_sample_config_t post_seek_down[MAX_SEEK_SAMPLES];
    /* BIOS vendor-specific POST seek samples [vendor][track] */
    audio_sample_config_t bios_post_seek_up[BIOS_VENDOR_COUNT][MAX_SEEK_SAMPLES];
    audio_sample_config_t bios_post_seek_down[BIOS_VENDOR_COUNT][MAX_SEEK_SAMPLES];
    double seek_time_ms[MAX_SEEK_SAMPLES];
    double post_seek_time_ms[MAX_SEEK_SAMPLES];
    /* BIOS vendor-specific POST seek times [vendor][track] */
    double bios_post_seek_time_ms[BIOS_VENDOR_COUNT][MAX_SEEK_SAMPLES];
    int total_tracks; /* 40 or 80 */
} fdd_audio_profile_config_t;

#define FDD_AUDIO_PROFILE_MAX 64

/* Motor sound states */
typedef enum {
    MOTOR_STATE_STOPPED = 0,
    MOTOR_STATE_STARTING,
    MOTOR_STATE_RUNNING,
    MOTOR_STATE_STOPPING
} motor_state_t;

/* Audio sample structure */
typedef struct {
    char     filename[512];
    int16_t *buffer;
    int      samples;
    float    volume;
} audio_sample_t;

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
    int track_diff;
    audio_sample_t *sample_to_play;
} multi_seek_state_t;

/* Drive type specific audio samples */
typedef struct {
    audio_sample_t spindlemotor_start;
    audio_sample_t spindlemotor_loop;
    audio_sample_t spindlemotor_stop;
    /* Individual seek samples for each track count (indexed 0-78 for 1-79 tracks) */
    audio_sample_t seek_up[MAX_SEEK_SAMPLES];
    audio_sample_t seek_down[MAX_SEEK_SAMPLES];
    audio_sample_t post_seek_up[MAX_SEEK_SAMPLES];
    audio_sample_t post_seek_down[MAX_SEEK_SAMPLES];
    /* BIOS vendor-specific POST seek samples [vendor][track] */
    audio_sample_t bios_post_seek_up[BIOS_VENDOR_COUNT][MAX_SEEK_SAMPLES];
    audio_sample_t bios_post_seek_down[BIOS_VENDOR_COUNT][MAX_SEEK_SAMPLES];
} drive_audio_samples_t;

/* Fade duration: 75ms at 48kHz = 3600 samples */
#define FADE_DURATION_MS 75
#define FADE_SAMPLES     (48000 * FADE_DURATION_MS / 1000)

/* Functions for configuration management */
extern void fdd_audio_load_profiles(void);
extern int fdd_audio_get_profile_count(void);
extern const fdd_audio_profile_config_t* fdd_audio_get_profile(int id);
extern const char* fdd_audio_get_profile_name(int id);
extern const char* fdd_audio_get_profile_internal_name(int id);
extern int fdd_audio_get_profile_by_internal_name(const char *internal_name);
extern double fdd_audio_get_seek_time(int drive, int track_count, int is_seek_down);
extern void load_profile_samples(int profile_id);
extern int fdd_get_audio_profile(int drive);
extern bios_boot_status_t fdd_get_boot_status(void);

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