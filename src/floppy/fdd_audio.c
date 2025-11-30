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
#include <86box/fdc.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/sound.h>
#include <86box/plat.h>
#include <86box/path.h>
#include <86box/ini.h>

#ifndef DISABLE_FDD_AUDIO

/* Global audio profile configurations */
static fdd_audio_profile_config_t audio_profiles[FDD_AUDIO_PROFILE_MAX];
static int                        audio_profile_count = 0;

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
} multi_seek_state_t;

/* Drive type specific audio samples */
typedef struct {
    audio_sample_t spindlemotor_start;
    audio_sample_t spindlemotor_loop;
    audio_sample_t spindlemotor_stop;
    /* Individual seek samples for each track count (indexed 0-78 for 1-79 tracks) */
    audio_sample_t seek_up[MAX_SEEK_SAMPLES];
    audio_sample_t seek_down[MAX_SEEK_SAMPLES];
} drive_audio_samples_t;

/* Dynamic sample storage for each profile */
static drive_audio_samples_t profile_samples[FDD_AUDIO_PROFILE_MAX];

/* Audio state for each drive */
static int           spindlemotor_pos[FDD_NUM]                    = {};
static motor_state_t spindlemotor_state[FDD_NUM]                  = {};
static float         spindlemotor_fade_volume[FDD_NUM]            = {};
static int           spindlemotor_fade_samples_remaining[FDD_NUM] = {};

/* Multi-track seek audio state for each drive */
static multi_seek_state_t seek_state[FDD_NUM][MAX_CONCURRENT_SEEKS] = {};

extern uint64_t motoron[FDD_NUM];
extern char     exe_path[2048];

extern int fdd_get_audio_profile(int drive);

/* Forward declaration */
static int16_t *load_wav(const char *filename, int *sample_count);

#    ifdef ENABLE_FDD_LOG
int fdc_do_log = ENABLE_FDD_LOG;

static void
fdd_log(const char *fmt, ...)
{
    va_list ap;

    if (fdc_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#    else
#        define fdd_log(fmt, ...)
#    endif

/* Logging function for audio profile parameters */
static void
fdd_audio_log_profile_params(int drive, const fdd_audio_profile_config_t *profile)
{
    if (!profile) {
        fdd_log("FDD Audio Drive %d: No profile assigned\n", drive);
        return;
    }

    fdd_log("FDD Audio Drive %d Profile Parameters:\n", drive);
    fdd_log("  Profile ID: %d\n", profile->id);
    fdd_log("  Profile Name: %s\n", profile->name);
    fdd_log("  Internal Name: %s\n", profile->internal_name);

    fdd_log("  Sample Files:\n");
    fdd_log("    Spindle Start: %s (volume: %.2f)\n",
            profile->spindlemotor_start.filename, profile->spindlemotor_start.volume);
    fdd_log("    Spindle Loop: %s (volume: %.2f)\n",
            profile->spindlemotor_loop.filename, profile->spindlemotor_loop.volume);
    fdd_log("    Spindle Stop: %s (volume: %.2f)\n",
            profile->spindlemotor_stop.filename, profile->spindlemotor_stop.volume);

    /* Log a few sample seek files as examples */
    int max_tracks = (profile->total_tracks == 40) ? 39 : 79;
    fdd_log("    Individual seek samples (up to %d tracks):\n", max_tracks);
    for (int i = 0; i < max_tracks && i < 5; i++) {
        if (profile->seek_up[i].filename[0]) {
            fdd_log("      Seek up %d track(s): %s (volume: %.2f)\n",
                    i + 1, profile->seek_up[i].filename, profile->seek_up[i].volume);
        }
        if (profile->seek_down[i].filename[0]) {
            fdd_log("      Seek down %d track(s): %s (volume: %.2f)\n",
                    i + 1, profile->seek_down[i].filename, profile->seek_down[i].volume);
        }
    }
    if (max_tracks > 5)
        fdd_log("      ... and %d more seek samples\n", (max_tracks - 5) * 2);
}

/* Log audio profile parameters for a specific drive */
void
fdd_audio_log_drive_profile(int drive)
{
    if (drive < 0 || drive >= FDD_NUM) {
        fdd_log("FDD Audio: Invalid drive number %d\n", drive);
        return;
    }

    int                               profile_id = fdd_get_audio_profile(drive);
    const fdd_audio_profile_config_t *profile    = fdd_audio_get_profile(profile_id);

    fdd_log("FDD Audio Drive %d: Using profile %d\n", drive, profile_id);
    fdd_audio_log_profile_params(drive, profile);
}

/* Log only the audio profiles that are actually used by configured drives */
static void
fdd_audio_log_active_profiles(void)
{
    fdd_log("FDD Audio: Checking active drive configurations...\n");
    int active_drive_count = 0;

    for (int drive = 0; drive < FDD_NUM; drive++) {
        if (fdd_get_type(drive) == 0)
            continue;

        active_drive_count++;
        int profile_id = fdd_get_audio_profile(drive);
        if (profile_id >= 0 && profile_id < audio_profile_count) {
            fdd_log("FDD Audio: Drive %d (configured) uses profile %d\n", drive, profile_id);
            fdd_audio_log_profile_params(drive, &audio_profiles[profile_id]);
        }
    }

    if (active_drive_count == 0) {
        fdd_log("FDD Audio: No drives configured - no audio profiles to log\n");
        return;
    }

    fdd_log("FDD Audio: Active audio profiles for %d configured drive(s):\n", active_drive_count);
}

void
fdd_audio_load_profiles(void)
{
    ini_t profiles_ini;
    char  cfg_fn[1024] = { 0 };

    int   ret = asset_getfile("assets/sounds/fdd/fdd_audio_profiles.cfg", cfg_fn, 1024);
    if (!ret) {
        fdd_log("FDD Audio: Could not find profiles\n");
        return;
    }

    profiles_ini = ini_read_ex(cfg_fn, 1);
    if (profiles_ini == NULL) {
        fdd_log("FDD Audio: Could not load profiles\n");
        return;
    }

    audio_profile_count = 0;
    
    /* Load profiles by trying known profile section names */
    for (int i = 0; i < FDD_AUDIO_PROFILE_MAX && audio_profile_count < FDD_AUDIO_PROFILE_MAX; i++) {
        char section_name[64];
        snprintf(section_name, sizeof(section_name), "Profile \"%d\"", i);
        
        ini_section_t section = ini_find_section(profiles_ini, section_name);
        if (section) {
            fdd_audio_profile_config_t *profile = &audio_profiles[audio_profile_count];
            
            /* Load profile configuration */
            profile->id = ini_section_get_int(section, "id", audio_profile_count);

            const char *name = ini_section_get_string(section, "name", "Unknown");
            strncpy(profile->name, name, sizeof(profile->name) - 1);
            profile->name[sizeof(profile->name) - 1] = '\0';

            const char *internal_name = ini_section_get_string(section, "internal_name", "unknown");
            strncpy(profile->internal_name, internal_name, sizeof(profile->internal_name) - 1);
            profile->internal_name[sizeof(profile->internal_name) - 1] = '\0';

            /* Load sample configurations */
            const char *filename = ini_section_get_string(section, "spindlemotor_start_file", "");
            strncpy(profile->spindlemotor_start.filename, filename, sizeof(profile->spindlemotor_start.filename) - 1);
            profile->spindlemotor_start.filename[sizeof(profile->spindlemotor_start.filename) - 1] = '\0';
            profile->spindlemotor_start.volume                                                     = ini_section_get_double(section, "spindlemotor_start_volume", 1.0);

            filename = ini_section_get_string(section, "spindlemotor_loop_file", "");
            strncpy(profile->spindlemotor_loop.filename, filename, sizeof(profile->spindlemotor_loop.filename) - 1);
            profile->spindlemotor_loop.filename[sizeof(profile->spindlemotor_loop.filename) - 1] = '\0';
            profile->spindlemotor_loop.volume                                                    = ini_section_get_double(section, "spindlemotor_loop_volume", 1.0);

            filename = ini_section_get_string(section, "spindlemotor_stop_file", "");
            strncpy(profile->spindlemotor_stop.filename, filename, sizeof(profile->spindlemotor_stop.filename) - 1);
            profile->spindlemotor_stop.filename[sizeof(profile->spindlemotor_stop.filename) - 1] = '\0';
            profile->spindlemotor_stop.volume                                                    = ini_section_get_double(section, "spindlemotor_stop_volume", 1.0);

            /* Load seek samples and seek times for each track count */
            for (int track_count = 1; track_count <= MAX_SEEK_SAMPLES; track_count++) {
                char key[128];

                /* Seek up samples */
                snprintf(key, sizeof(key), "seek_up_%dtrack_file", track_count);
                filename = ini_section_get_string(section, key, "");
                strncpy(profile->seek_up[track_count - 1].filename, filename,
                        sizeof(profile->seek_up[track_count - 1].filename) - 1);
                profile->seek_up[track_count - 1].filename[sizeof(profile->seek_up[track_count - 1].filename) - 1] = '\0';

                snprintf(key, sizeof(key), "seek_up_%dtrack_volume", track_count);
                profile->seek_up[track_count - 1].volume = ini_section_get_double(section, key, 1.0);

                /* Seek down samples */
                snprintf(key, sizeof(key), "seek_down_%dtrack_file", track_count);
                filename = ini_section_get_string(section, key, "");
                strncpy(profile->seek_down[track_count - 1].filename, filename,
                        sizeof(profile->seek_down[track_count - 1].filename) - 1);
                profile->seek_down[track_count - 1].filename[sizeof(profile->seek_down[track_count - 1].filename) - 1] = '\0';

                snprintf(key, sizeof(key), "seek_down_%dtrack_volume", track_count);
                profile->seek_down[track_count - 1].volume = ini_section_get_double(section, key, 1.0);

                /* Seek time in milliseconds - used for FDC timing, not sample playback */
                snprintf(key, sizeof(key), "seek_%dtrack_time_ms", track_count);
                profile->seek_time_ms[track_count - 1] = ini_section_get_double(section, key, 6.0 * track_count);
            }

            /* Load timing configurations */
            profile->total_tracks = ini_section_get_int(section, "total_tracks", 80);

            audio_profile_count++;
        }
    }

    ini_close(profiles_ini);

    fdd_log("FDD Audio: Loaded %d audio profiles\n", audio_profile_count);
}

void
load_profile_samples(int profile_id)
{
    if (profile_id <= 0 || profile_id >= audio_profile_count)
        return;

    fdd_audio_profile_config_t *config  = &audio_profiles[profile_id];
    drive_audio_samples_t      *samples = &profile_samples[profile_id];

    fdd_log("FDD Audio: Loading samples for profile %d (%s)\n",
            profile_id, config->name);

    /* Load samples if not already loaded */
    if (samples->spindlemotor_start.buffer == NULL && config->spindlemotor_start.filename[0]) {
        strcpy(samples->spindlemotor_start.filename, config->spindlemotor_start.filename);
        samples->spindlemotor_start.volume = config->spindlemotor_start.volume;
        samples->spindlemotor_start.buffer = load_wav(config->spindlemotor_start.filename,
                                                      &samples->spindlemotor_start.samples);
        if (samples->spindlemotor_start.buffer) {
            fdd_log("  Loaded spindlemotor_start: %s (%d samples, volume %.2f)\n",
                    config->spindlemotor_start.filename,
                    samples->spindlemotor_start.samples,
                    config->spindlemotor_start.volume);
        } else {
            fdd_log("  Failed to load spindlemotor_start: %s\n",
                    config->spindlemotor_start.filename);
        }
    }

    if (samples->spindlemotor_loop.buffer == NULL && config->spindlemotor_loop.filename[0]) {
        strcpy(samples->spindlemotor_loop.filename, config->spindlemotor_loop.filename);
        samples->spindlemotor_loop.volume = config->spindlemotor_loop.volume;
        samples->spindlemotor_loop.buffer = load_wav(config->spindlemotor_loop.filename,
                                                     &samples->spindlemotor_loop.samples);
        if (samples->spindlemotor_loop.buffer) {
            fdd_log("  Loaded spindlemotor_loop: %s (%d samples, volume %.2f)\n",
                    config->spindlemotor_loop.filename,
                    samples->spindlemotor_loop.samples,
                    config->spindlemotor_loop.volume);
        } else {
            fdd_log("  Failed to load spindlemotor_loop: %s\n",
                    config->spindlemotor_loop.filename);
        }
    }

    if (samples->spindlemotor_stop.buffer == NULL && config->spindlemotor_stop.filename[0]) {
        strcpy(samples->spindlemotor_stop.filename, config->spindlemotor_stop.filename);
        samples->spindlemotor_stop.volume = config->spindlemotor_stop.volume;
        samples->spindlemotor_stop.buffer = load_wav(config->spindlemotor_stop.filename,
                                                     &samples->spindlemotor_stop.samples);
        if (samples->spindlemotor_stop.buffer) {
            fdd_log("  Loaded spindlemotor_stop: %s (%d samples, volume %.2f)\n",
                    config->spindlemotor_stop.filename,
                    samples->spindlemotor_stop.samples,
                    config->spindlemotor_stop.volume);
        } else {
            fdd_log("  Failed to load spindlemotor_stop: %s\n",
                    config->spindlemotor_stop.filename);
        }
    }

    /* Load individual seek samples for each track count */
    int max_tracks = (config->total_tracks == 40) ? 39 : 79;
    for (int track_count = 1; track_count <= max_tracks; track_count++) {
        int idx = track_count - 1;

        /* Load seek up sample */
        if (samples->seek_up[idx].buffer == NULL && config->seek_up[idx].filename[0]) {
            strcpy(samples->seek_up[idx].filename, config->seek_up[idx].filename);
            samples->seek_up[idx].volume = config->seek_up[idx].volume;
            samples->seek_up[idx].buffer = load_wav(config->seek_up[idx].filename,
                                                    &samples->seek_up[idx].samples);
            if (samples->seek_up[idx].buffer) {
                fdd_log("  Loaded seek_up[%d]: %s (%d samples, volume %.2f)\n",
                        track_count, config->seek_up[idx].filename,
                        samples->seek_up[idx].samples, config->seek_up[idx].volume);
            }
        }

        /* Load seek down sample */
        if (samples->seek_down[idx].buffer == NULL && config->seek_down[idx].filename[0]) {
            strcpy(samples->seek_down[idx].filename, config->seek_down[idx].filename);
            samples->seek_down[idx].volume = config->seek_down[idx].volume;
            samples->seek_down[idx].buffer = load_wav(config->seek_down[idx].filename,
                                                      &samples->seek_down[idx].samples);
            if (samples->seek_down[idx].buffer) {
                fdd_log("  Loaded seek_down[%d]: %s (%d samples, volume %.2f)\n",
                        track_count, config->seek_down[idx].filename,
                        samples->seek_down[idx].samples, config->seek_down[idx].volume);
            }
        }
    }
}

static drive_audio_samples_t *
get_drive_samples(int drive)
{
    int profile_id = fdd_get_audio_profile(drive);
    if (profile_id <= 0 || profile_id >= audio_profile_count)
        return NULL;

    /* Samples are preloaded during fdd_audio_init */
    return &profile_samples[profile_id];
}

/* Public API functions */
int
fdd_audio_get_profile_count(void)
{
    return audio_profile_count;
}

const fdd_audio_profile_config_t *
fdd_audio_get_profile(int id)
{
    if (id < 0 || id >= audio_profile_count)
        return NULL;
    return &audio_profiles[id];
}

const char *
fdd_audio_get_profile_name(int id)
{
    if (id < 0 || id >= audio_profile_count)
        return NULL;
    return audio_profiles[id].name;
}

const char *
fdd_audio_get_profile_internal_name(int id)
{
    if (id < 0 || id >= audio_profile_count)
        return NULL;
    return audio_profiles[id].internal_name;
}

int
fdd_audio_get_profile_by_internal_name(const char *internal_name)
{
    if (!internal_name || !*internal_name)
        return 0;

    if (audio_profile_count == 0)
        fdd_audio_load_profiles();

    for (int i = 0; i < audio_profile_count; i++) {
        if (!strcmp(audio_profiles[i].internal_name, internal_name))
            return i;
    }
    return 0;
}

double
fdd_audio_get_seek_time(int drive, int track_count, int is_seek_down)
{
    int profile_id = fdd_get_audio_profile(drive);
    if (profile_id <= 0 || profile_id >= audio_profile_count) {
        return 0;
    }

    fdd_audio_profile_config_t *profile = &audio_profiles[profile_id];
    if (!profile)
        return 0;

    /* Get the maximum available seek sample for this profile */
    int max_seek_tracks = (profile->total_tracks == 40) ? 39 : 79;

    /* Clamp track_count to maximum */
    if (track_count > max_seek_tracks)
        track_count = max_seek_tracks;

    /* Return configured seek time in microseconds */
    if (track_count > 0 && track_count <= MAX_SEEK_SAMPLES) {
        return profile->seek_time_ms[track_count - 1] * 1000.0;
    }

    /* Fallback */
    return 0;
}

void
fdd_audio_init(void)
{
    /* Load audio profiles configuration */
    fdd_audio_load_profiles();

    /* Initialize audio state for all drives */
    for (int i = 0; i < FDD_NUM; i++) {
        spindlemotor_pos[i]                    = 0;
        spindlemotor_state[i]                  = MOTOR_STATE_STOPPED;
        spindlemotor_fade_volume[i]            = 1.0f;
        spindlemotor_fade_samples_remaining[i] = 0;

        /* Initialize all concurrent seek slots */
        for (int j = 0; j < MAX_CONCURRENT_SEEKS; j++) {
            seek_state[i][j].position         = 0;
            seek_state[i][j].active           = 0;
            seek_state[i][j].duration_samples = 0;
            seek_state[i][j].from_track       = -1;
            seek_state[i][j].to_track         = -1;
            seek_state[i][j].track_diff       = 0;
        }
    }

    /* Preload audio samples for each drive's selected profile */
    for (int drive = 0; drive < FDD_NUM; drive++) {
        int profile_id = fdd_get_audio_profile(drive);
        if (profile_id > 0 && profile_id < audio_profile_count) {
            load_profile_samples(profile_id);
        }
    }

    /* Log only the active profiles used by configured drives */
    fdd_audio_log_active_profiles();

    /* Initialize sound thread */
    sound_fdd_thread_init();

    fdd_log("FDD Audio: Initialization complete\n");
}

void
fdd_audio_close(void)
{
    fdd_log("FDD Audio: Shutting down audio system\n");

    /* Free loaded profile samples */
    for (int profile_id = 0; profile_id < audio_profile_count; profile_id++) {
        drive_audio_samples_t *samples = &profile_samples[profile_id];

        if (samples->spindlemotor_start.buffer) {
            free(samples->spindlemotor_start.buffer);
            samples->spindlemotor_start.buffer  = NULL;
            samples->spindlemotor_start.samples = 0;
        }
        if (samples->spindlemotor_loop.buffer) {
            free(samples->spindlemotor_loop.buffer);
            samples->spindlemotor_loop.buffer  = NULL;
            samples->spindlemotor_loop.samples = 0;
        }
        if (samples->spindlemotor_stop.buffer) {
            free(samples->spindlemotor_stop.buffer);
            samples->spindlemotor_stop.buffer  = NULL;
            samples->spindlemotor_stop.samples = 0;
        }

        /* Free individual seek samples */
        for (int track_count = 0; track_count < MAX_SEEK_SAMPLES; track_count++) {
            if (samples->seek_up[track_count].buffer) {
                free(samples->seek_up[track_count].buffer);
                samples->seek_up[track_count].buffer  = NULL;
                samples->seek_up[track_count].samples = 0;
            }
            if (samples->seek_down[track_count].buffer) {
                free(samples->seek_down[track_count].buffer);
                samples->seek_down[track_count].buffer  = NULL;
                samples->seek_down[track_count].samples = 0;
            }
        }
    }

    sound_fdd_thread_end();

    fdd_log("FDD Audio: Shutdown complete\n");
}

void
fdd_audio_set_motor_enable(int drive, int motor_enable)
{
    if (!fdd_sounds_enabled || fdd_get_turbo(drive))
        return;

    drive_audio_samples_t *samples = get_drive_samples(drive);
    if (!samples)
        return;

    fdd_log("FDD Audio Drive %d: Motor %s\n", drive, motor_enable ? "ON" : "OFF");

    if (motor_enable && !motoron[drive]) {
        /* Motor starting up */
        if (spindlemotor_state[drive] == MOTOR_STATE_STOPPING) {
            /* Interrupt stop sequence and transition back to loop */
            fdd_log("FDD Audio Drive %d: Interrupting stop sequence, returning to loop\n", drive);
            spindlemotor_state[drive]                  = MOTOR_STATE_RUNNING;
            spindlemotor_pos[drive]                    = 0;
            spindlemotor_fade_volume[drive]            = 1.0f;
            spindlemotor_fade_samples_remaining[drive] = 0;
        } else {
            /* Normal startup */
            fdd_log("FDD Audio Drive %d: Starting motor (normal startup)\n", drive);
            spindlemotor_state[drive]                  = MOTOR_STATE_STARTING;
            spindlemotor_pos[drive]                    = 0;
            spindlemotor_fade_volume[drive]            = 1.0f;
            spindlemotor_fade_samples_remaining[drive] = 0;
        }
    } else if (!motor_enable && motoron[drive]) {
        /* Motor stopping */
        fdd_log("FDD Audio Drive %d: Stopping motor\n", drive);
        spindlemotor_state[drive]                  = MOTOR_STATE_STOPPING;
        spindlemotor_pos[drive]                    = 0;
        spindlemotor_fade_volume[drive]            = 1.0f;
        spindlemotor_fade_samples_remaining[drive] = FADE_SAMPLES;
    }
}

void
fdd_audio_play_multi_track_seek(int drive, int from_track, int to_track)
{
    if (!fdd_sounds_enabled || fdd_get_turbo(drive))
        return;

    if (drive < 0 || drive >= FDD_NUM)
        return;

    int track_diff = abs(from_track - to_track);
    if (track_diff < 1)
        return;

    drive_audio_samples_t *samples = get_drive_samples(drive);
    if (!samples)
        return;

    int is_seek_down = (to_track < from_track);

    /* Get the profile to check total_tracks */
    int profile_id = fdd_get_audio_profile(drive);
    if (profile_id < 1 || profile_id >= audio_profile_count)
        return;

    fdd_audio_profile_config_t *profile = &audio_profiles[profile_id];

    /* Determine the maximum available seek sample for this profile */
    int max_seek_tracks = (profile->total_tracks == 40) ? 39 : 79;

    /* Clamp track_diff to the maximum available sample */
    if (track_diff > max_seek_tracks) {
        fdd_log("FDD Audio Drive %d: Seek request for %d tracks exceeds maximum %d, clamping to %d\n",
                drive, track_diff, max_seek_tracks, max_seek_tracks);
        track_diff = max_seek_tracks;
    }

    /* Get the appropriate seek sample */
    int             idx           = track_diff - 1;
    audio_sample_t *sample_to_use = is_seek_down ? &samples->seek_down[idx] : &samples->seek_up[idx];

    /* Only proceed if we have the appropriate sample */
    if (!sample_to_use || !sample_to_use->buffer || sample_to_use->samples == 0)
        return;

    fdd_log("FDD Audio Drive %d: Multi-track seek %d -> %d (%d tracks, %s)\n",
            drive, from_track, to_track, track_diff, is_seek_down ? "DOWN" : "UP");

    /* Find an available seek slot */
    int slot = -1;
    for (int i = 0; i < MAX_CONCURRENT_SEEKS; i++) {
        if (!seek_state[drive][i].active) {
            slot = i;
            break;
        }
    }

    /* If no slot available, reuse the oldest (first) slot */
    if (slot == -1) {
        fdd_log("FDD Audio Drive %d: All seek slots in use, reusing slot 0\n", drive);
        slot = 0;
    }

    /* Start new seek in the available slot */
    seek_state[drive][slot].position         = 0;
    seek_state[drive][slot].active           = 1;
    seek_state[drive][slot].duration_samples = sample_to_use->samples;
    seek_state[drive][slot].from_track       = from_track;
    seek_state[drive][slot].to_track         = to_track;
    seek_state[drive][slot].track_diff       = track_diff;

    fdd_log("FDD Audio Drive %d: Started seek in slot %d, duration %d samples\n",
            drive, slot, sample_to_use->samples);
}

static int16_t *
load_wav(const char *filename, int *sample_count)
{
    if ((filename == NULL) || (strlen(filename) == 0))
        return NULL;

    if (strstr(filename, "..") != NULL)
        return NULL;

    FILE *f = asset_fopen(filename, "rb");
    if (f == NULL) {
        fdd_log("FDD Audio: Failed to open WAV file: %s\n", filename);
        return NULL;
    }

    wav_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
        fdd_log("FDD Audio: Failed to read WAV header from: %s\n", filename);
        fclose(f);
        return NULL;
    }

    if (memcmp(hdr.riff, "RIFF", 4) || memcmp(hdr.wave, "WAVE", 4) || memcmp(hdr.fmt, "fmt ", 4) || memcmp(hdr.data, "data", 4)) {
        fdd_log("FDD Audio: Invalid WAV format in file: %s\n", filename);
        fclose(f);
        return NULL;
    }

    /* Accept both mono and stereo, 16-bit PCM */
    if (hdr.audio_format != 1 || hdr.bits_per_sample != 16 || (hdr.num_channels != 1 && hdr.num_channels != 2)) {
        fdd_log("FDD Audio: Unsupported WAV format in %s (format: %d, bits: %d, channels: %d)\n",
                filename, hdr.audio_format, hdr.bits_per_sample, hdr.num_channels);
        fclose(f);
        return NULL;
    }

    int      input_samples = hdr.data_size / 2; /* 2 bytes per sample */
    int16_t *input_data    = malloc(hdr.data_size);
    if (!input_data) {
        fdd_log("FDD Audio: Failed to allocate memory for WAV data: %s\n", filename);
        fclose(f);
        return NULL;
    }

    if (fread(input_data, 1, hdr.data_size, f) != hdr.data_size) {
        fdd_log("FDD Audio: Failed to read WAV data from: %s\n", filename);
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
            fdd_log("FDD Audio: Failed to allocate stereo conversion buffer for: %s\n", filename);
            free(input_data);
            return NULL;
        }

        /* Convert mono to stereo by duplicating each sample */
        for (int i = 0; i < input_samples; i++) {
            output_data[i * 2]     = input_data[i]; /* Left channel */
            output_data[i * 2 + 1] = input_data[i]; /* Right channel */
        }

        free(input_data);
        fdd_log("FDD Audio: Loaded %s (mono->stereo, %d samples)\n", filename, output_samples);
    } else {
        /* Already stereo */
        output_data    = input_data;
        output_samples = input_samples / 2; /* Number of stereo sample pairs */
        fdd_log("FDD Audio: Loaded %s (stereo, %d samples)\n", filename, output_samples);
    }

    if (sample_count)
        *sample_count = output_samples;

    return output_data;
}

void
fdd_audio_callback(int16_t *buffer, int length)
{
    /* Clear buffer */
    memset(buffer, 0, length * sizeof(int16_t));

    /* Check if any motor is running or transitioning, or any audio is active */
    int any_audio_active = 0;
    for (int drive = 0; drive < FDD_NUM; drive++) {
        if (spindlemotor_state[drive] != MOTOR_STATE_STOPPED) {
            any_audio_active = 1;
            break;
        }
        for (int j = 0; j < MAX_CONCURRENT_SEEKS; j++) {
            if (seek_state[drive][j].active) {
                any_audio_active = 1;
                break;
            }
        }
        if (any_audio_active)
            break;
    }

    if (!any_audio_active)
        return;

    float   *float_buffer      = (float *) buffer;
    int16_t *int16_buffer      = (int16_t *) buffer;
    int      samples_in_buffer = length / 2;

    /* Process audio for all drives */
    if (sound_is_float) {
        for (int drive = 0; drive < FDD_NUM; drive++) {
            drive_audio_samples_t *samples = get_drive_samples(drive);
            if (!samples)
                continue;

            for (int i = 0; i < samples_in_buffer; i++) {
                float left_sample  = 0.0f;
                float right_sample = 0.0f;

                /* Process motor audio (unchanged) */
                if (spindlemotor_state[drive] != MOTOR_STATE_STOPPED) {
                    switch (spindlemotor_state[drive]) {
                        case MOTOR_STATE_STARTING:
                            if (samples->spindlemotor_start.buffer && spindlemotor_pos[drive] < samples->spindlemotor_start.samples) {
                                left_sample  = (float) samples->spindlemotor_start.buffer[spindlemotor_pos[drive] * 2] / 131072.0f * samples->spindlemotor_start.volume;
                                right_sample = (float) samples->spindlemotor_start.buffer[spindlemotor_pos[drive] * 2 + 1] / 131072.0f * samples->spindlemotor_start.volume;
                                spindlemotor_pos[drive]++;
                            } else {
                                spindlemotor_state[drive] = MOTOR_STATE_RUNNING;
                                spindlemotor_pos[drive]   = 0;
                            }
                            break;

                        case MOTOR_STATE_RUNNING:
                            if (samples->spindlemotor_loop.buffer && samples->spindlemotor_loop.samples > 0) {
                                left_sample  = (float) samples->spindlemotor_loop.buffer[spindlemotor_pos[drive] * 2] / 131072.0f * samples->spindlemotor_loop.volume;
                                right_sample = (float) samples->spindlemotor_loop.buffer[spindlemotor_pos[drive] * 2 + 1] / 131072.0f * samples->spindlemotor_loop.volume;
                                spindlemotor_pos[drive]++;

                                if (spindlemotor_pos[drive] >= samples->spindlemotor_loop.samples) {
                                    spindlemotor_pos[drive] = 0;
                                }
                            }
                            break;

                        case MOTOR_STATE_STOPPING:
                            if (spindlemotor_fade_samples_remaining[drive] > 0) {
                                float loop_volume = spindlemotor_fade_volume[drive];
                                float stop_volume = 1.0f - loop_volume;

                                float loop_left = 0.0f, loop_right = 0.0f;
                                float stop_left = 0.0f, stop_right = 0.0f;

                                if (samples->spindlemotor_loop.buffer && samples->spindlemotor_loop.samples > 0) {
                                    int loop_pos = spindlemotor_pos[drive] % samples->spindlemotor_loop.samples;
                                    loop_left    = (float) samples->spindlemotor_loop.buffer[loop_pos * 2] / 131072.0f * samples->spindlemotor_loop.volume;
                                    loop_right   = (float) samples->spindlemotor_loop.buffer[loop_pos * 2 + 1] / 131072.0f * samples->spindlemotor_loop.volume;
                                }

                                if (samples->spindlemotor_stop.buffer && spindlemotor_pos[drive] < samples->spindlemotor_stop.samples) {
                                    stop_left  = (float) samples->spindlemotor_stop.buffer[spindlemotor_pos[drive] * 2] / 131072.0f * samples->spindlemotor_stop.volume;
                                    stop_right = (float) samples->spindlemotor_stop.buffer[spindlemotor_pos[drive] * 2 + 1] / 131072.0f * samples->spindlemotor_stop.volume;
                                }

                                left_sample  = loop_left * loop_volume + stop_left * stop_volume;
                                right_sample = loop_right * loop_volume + stop_right * stop_volume;

                                spindlemotor_pos[drive]++;
                                spindlemotor_fade_samples_remaining[drive]--;

                                spindlemotor_fade_volume[drive] = (float) spindlemotor_fade_samples_remaining[drive] / FADE_SAMPLES;
                            } else {
                                if (samples->spindlemotor_stop.buffer && spindlemotor_pos[drive] < samples->spindlemotor_stop.samples) {
                                    left_sample  = (float) samples->spindlemotor_stop.buffer[spindlemotor_pos[drive] * 2] / 131072.0f * samples->spindlemotor_stop.volume;
                                    right_sample = (float) samples->spindlemotor_stop.buffer[spindlemotor_pos[drive] * 2 + 1] / 131072.0f * samples->spindlemotor_stop.volume;
                                    spindlemotor_pos[drive]++;
                                } else {
                                    spindlemotor_state[drive] = MOTOR_STATE_STOPPED;
                                }
                            }
                            break;

                        default:
                            break;
                    }
                }

                /* Process all concurrent seek audio slots */
                for (int slot = 0; slot < MAX_CONCURRENT_SEEKS; slot++) {
                    if (!seek_state[drive][slot].active)
                        continue;

                    int track_diff = seek_state[drive][slot].track_diff;
                    if (track_diff > 0 && track_diff <= MAX_SEEK_SAMPLES) {
                        int             idx          = track_diff - 1;
                        int             is_seek_down = (seek_state[drive][slot].to_track < seek_state[drive][slot].from_track);
                        audio_sample_t *seek_sample  = is_seek_down ? &samples->seek_down[idx] : &samples->seek_up[idx];

                        if (seek_sample && seek_sample->buffer && seek_state[drive][slot].position < seek_sample->samples) {
                            /* Mix seek sound with existing audio */
                            float seek_left  = (float) seek_sample->buffer[seek_state[drive][slot].position * 2] / 131072.0f * seek_sample->volume;
                            float seek_right = (float) seek_sample->buffer[seek_state[drive][slot].position * 2 + 1] / 131072.0f * seek_sample->volume;

                            left_sample += seek_left;
                            right_sample += seek_right;

                            seek_state[drive][slot].position++;
                        } else {
                            /* Seek sound finished */
                            seek_state[drive][slot].active           = 0;
                            seek_state[drive][slot].position         = 0;
                            seek_state[drive][slot].duration_samples = 0;
                            seek_state[drive][slot].from_track       = -1;
                            seek_state[drive][slot].to_track         = -1;
                            seek_state[drive][slot].track_diff       = 0;
                        }
                    }
                }

                /* Mix this drive's audio into the buffer */
                float_buffer[i * 2] += left_sample;
                float_buffer[i * 2 + 1] += right_sample;
            }
        }
    } else {
        /* int16 version - similar changes */
        for (int drive = 0; drive < FDD_NUM; drive++) {
            drive_audio_samples_t *samples = get_drive_samples(drive);
            if (!samples)
                continue;

            for (int i = 0; i < samples_in_buffer; i++) {
                int16_t left_sample  = 0;
                int16_t right_sample = 0;

                /* Process motor audio (same as float version but with int16) */
                if (spindlemotor_state[drive] != MOTOR_STATE_STOPPED) {
                    switch (spindlemotor_state[drive]) {
                        case MOTOR_STATE_STARTING:
                            if (samples->spindlemotor_start.buffer && spindlemotor_pos[drive] < samples->spindlemotor_start.samples) {
                                left_sample  = (int16_t) ((float) samples->spindlemotor_start.buffer[spindlemotor_pos[drive] * 2] / 4.0f * samples->spindlemotor_start.volume);
                                right_sample = (int16_t) ((float) samples->spindlemotor_start.buffer[spindlemotor_pos[drive] * 2 + 1] / 4.0f * samples->spindlemotor_start.volume);
                                spindlemotor_pos[drive]++;
                            } else {
                                spindlemotor_state[drive] = MOTOR_STATE_RUNNING;
                                spindlemotor_pos[drive]   = 0;
                            }
                            break;

                        case MOTOR_STATE_RUNNING:
                            if (samples->spindlemotor_loop.buffer && samples->spindlemotor_loop.samples > 0) {
                                left_sample  = (int16_t) ((float) samples->spindlemotor_loop.buffer[spindlemotor_pos[drive] * 2] / 4.0f * samples->spindlemotor_loop.volume);
                                right_sample = (int16_t) ((float) samples->spindlemotor_loop.buffer[spindlemotor_pos[drive] * 2 + 1] / 4.0f * samples->spindlemotor_loop.volume);
                                spindlemotor_pos[drive]++;

                                if (spindlemotor_pos[drive] >= samples->spindlemotor_loop.samples) {
                                    spindlemotor_pos[drive] = 0;
                                }
                            }
                            break;

                        case MOTOR_STATE_STOPPING:
                            if (spindlemotor_fade_samples_remaining[drive] > 0) {
                                float loop_volume = spindlemotor_fade_volume[drive];
                                float stop_volume = 1.0f - loop_volume;

                                int16_t loop_left = 0, loop_right = 0;
                                int16_t stop_left = 0, stop_right = 0;

                                if (samples->spindlemotor_loop.buffer && samples->spindlemotor_loop.samples > 0) {
                                    int loop_pos = spindlemotor_pos[drive] % samples->spindlemotor_loop.samples;
                                    loop_left    = (int16_t) ((float) samples->spindlemotor_loop.buffer[loop_pos * 2] / 4.0f * samples->spindlemotor_loop.volume);
                                    loop_right   = (int16_t) ((float) samples->spindlemotor_loop.buffer[loop_pos * 2 + 1] / 4.0f * samples->spindlemotor_loop.volume);
                                }

                                if (samples->spindlemotor_stop.buffer && spindlemotor_pos[drive] < samples->spindlemotor_stop.samples) {
                                    stop_left  = (int16_t) ((float) samples->spindlemotor_stop.buffer[spindlemotor_pos[drive] * 2] / 4.0f * samples->spindlemotor_stop.volume);
                                    stop_right = (int16_t) ((float) samples->spindlemotor_stop.buffer[spindlemotor_pos[drive] * 2 + 1] / 4.0f * samples->spindlemotor_stop.volume);
                                }

                                left_sample  = (int16_t) (loop_left * loop_volume + stop_left * stop_volume);
                                right_sample = (int16_t) (loop_right * loop_volume + stop_right * stop_volume);

                                spindlemotor_pos[drive]++;
                                spindlemotor_fade_samples_remaining[drive]--;

                                spindlemotor_fade_volume[drive] = (float) spindlemotor_fade_samples_remaining[drive] / FADE_SAMPLES;
                            } else {
                                if (samples->spindlemotor_stop.buffer && spindlemotor_pos[drive] < samples->spindlemotor_stop.samples) {
                                    left_sample  = (int16_t) ((float) samples->spindlemotor_stop.buffer[spindlemotor_pos[drive] * 2] / 4.0f * samples->spindlemotor_stop.volume);
                                    right_sample = (int16_t) ((float) samples->spindlemotor_stop.buffer[spindlemotor_pos[drive] * 2 + 1] / 4.0f * samples->spindlemotor_stop.volume);
                                    spindlemotor_pos[drive]++;
                                } else {
                                    spindlemotor_state[drive] = MOTOR_STATE_STOPPED;
                                }
                            }
                            break;

                        default:
                            break;
                    }
                }

                /* Process all concurrent seek audio slots */
                for (int slot = 0; slot < MAX_CONCURRENT_SEEKS; slot++) {
                    if (!seek_state[drive][slot].active)
                        continue;

                    int track_diff = seek_state[drive][slot].track_diff;
                    if (track_diff > 0 && track_diff <= MAX_SEEK_SAMPLES) {
                        int             idx          = track_diff - 1;
                        int             is_seek_down = (seek_state[drive][slot].to_track < seek_state[drive][slot].from_track);
                        audio_sample_t *seek_sample  = is_seek_down ? &samples->seek_down[idx] : &samples->seek_up[idx];

                        if (seek_sample && seek_sample->buffer && seek_state[drive][slot].position < seek_sample->samples) {
                            /* Mix seek sound with existing audio */
                            int16_t seek_left  = (int16_t) ((float) seek_sample->buffer[seek_state[drive][slot].position * 2] / 4.0f * seek_sample->volume);
                            int16_t seek_right = (int16_t) ((float) seek_sample->buffer[seek_state[drive][slot].position * 2 + 1] / 4.0f * seek_sample->volume);

                            left_sample += seek_left;
                            right_sample += seek_right;

                            seek_state[drive][slot].position++;
                        } else {
                            /* Seek sound finished */
                            seek_state[drive][slot].active           = 0;
                            seek_state[drive][slot].position         = 0;
                            seek_state[drive][slot].duration_samples = 0;
                            seek_state[drive][slot].from_track       = -1;
                            seek_state[drive][slot].to_track         = -1;
                            seek_state[drive][slot].track_diff       = 0;
                        }
                    }
                }

                /* Mix this drive's audio into the buffer */
                int16_buffer[i * 2] += left_sample;
                int16_buffer[i * 2 + 1] += right_sample;
            }
        }
    }
}
#else

/* Stub implementations when audio is disabled */
void
fdd_audio_load_profiles(void)
{
}
int
fdd_audio_get_profile_count(void)
{
    return 1;
}
const fdd_audio_profile_config_t *
fdd_audio_get_profile(int id)
{
    static fdd_audio_profile_config_t none_profile = { 0, "None", "none" };
    return (id == 0) ? &none_profile : NULL;
}
const char *
fdd_audio_get_profile_name(int id)
{
    return (id == 0) ? "None" : NULL;
}
const char *
fdd_audio_get_profile_internal_name(int id)
{
    return (id == 0) ? "none" : NULL;
}
int
fdd_audio_get_profile_by_internal_name(const char *internal_name)
{
    return 0;
}
double
fdd_audio_get_seek_time(int drive, int track_count, int is_seek_down)
{
    return 0;
}
void
fdd_audio_init(void)
{
}
void
fdd_audio_close(void)
{
}
void
fdd_audio_set_motor_enable(int drive, int motor_enable)
{
}
void
fdd_audio_play_multi_track_seek(int drive, int from_track, int to_track)
{
}
void
fdd_audio_callback(int16_t *buffer, int length)
{
    memset(buffer, 0, length * sizeof(int16_t));
}

#endif /* DISABLE_FDD_AUDIO */
