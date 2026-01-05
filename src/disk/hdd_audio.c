/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Hard disk audio emulation.
 *
 * Authors: Toni Riikonen, <riikonen.toni@gmail.com>
 *
 *          Copyright 2026 Toni Riikonen.
 */

 #include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/hdd.h>
#include <86box/hdd_audio.h>
#include <86box/sound.h>
#include <86box/sound_util.h>
#include <86box/thread.h>
#include <86box/plat.h>
#include <86box/path.h>
#include <86box/ini.h>
#include <86box/mem.h>
#include <86box/rom.h>

/* Maximum number of simultaneous seek sounds per HDD */
#define HDD_MAX_SEEK_VOICES_PER_HDD 8

/* Maximum number of HDDs with audio emulation */
#define HDD_AUDIO_MAX_DRIVES 8

typedef struct {
    int   active;
    int   position;
    float volume;
    int   profile_id;  /* Which profile's seek sound to use */
} hdd_seek_voice_t;

/* Per-HDD audio state */
typedef struct {
    int                  hdd_index;       /* Index into hdd[] array */
    int                  profile_id;      /* Audio profile ID */
    hdd_spindle_state_t  spindle_state;
    int                  spindle_pos;
    int                  spindle_transition_pos;
    hdd_seek_voice_t     seek_voices[HDD_MAX_SEEK_VOICES_PER_HDD];
} hdd_audio_drive_state_t;

/* Audio samples structure for a profile */
typedef struct {
    int16_t *spindle_start_buffer;
    int      spindle_start_samples;
    float    spindle_start_volume;
    int16_t *spindle_loop_buffer;
    int      spindle_loop_samples;
    float    spindle_loop_volume;
    int16_t *spindle_stop_buffer;
    int      spindle_stop_samples;
    float    spindle_stop_volume;
    int16_t *seek_buffer;
    int      seek_samples;
    float    seek_volume;
    int      loaded;
} hdd_audio_samples_t;

/* Global audio profile configurations */
static hdd_audio_profile_config_t audio_profiles[HDD_AUDIO_PROFILE_MAX];
static int                        audio_profile_count = 0;

/* Per-profile loaded samples */
static hdd_audio_samples_t profile_samples[HDD_AUDIO_PROFILE_MAX];

/* Per-HDD audio states */
static hdd_audio_drive_state_t drive_states[HDD_AUDIO_MAX_DRIVES];
static int                     active_drive_count = 0;

static mutex_t *hdd_audio_mutex = NULL;

#ifdef ENABLE_HDD_AUDIO_LOG
int hdd_audio_do_log = ENABLE_HDD_AUDIO_LOG;

static void
hdd_audio_log(const char *fmt, ...)
{
    va_list ap;

    if (hdd_audio_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define hdd_audio_log(fmt, ...)
#endif

/* Load audio profiles from configuration file */
void
hdd_audio_load_profiles(void)
{
    ini_t profiles_ini;
    char  cfg_fn[1024] = { 0 };

    /*
     * asset_getfile returns a path from the trusted asset search paths.
     * The filename is hardcoded and validated against existing files.
     */
    int ret = asset_getfile("assets/sounds/hdd/hdd_audio_profiles.cfg", cfg_fn, 1024);
    if (!ret) {
        hdd_audio_log("HDD Audio: Could not find hdd_audio_profiles.cfg\n");
        return;
    }

    /* Validate that the path does not contain path traversal sequences */
    if (strstr(cfg_fn, "..") != NULL) {
        hdd_audio_log("HDD Audio: Invalid path detected\n");
        return;
    }

    /* Validate the path ends with our expected filename */
    const char *expected_suffix = "hdd_audio_profiles.cfg";
    size_t cfg_len = strlen(cfg_fn);
    size_t suffix_len = strlen(expected_suffix);
    if (cfg_len < suffix_len || strcmp(cfg_fn + cfg_len - suffix_len, expected_suffix) != 0) {
        pclog("HDD Audio: Unexpected config path\n");
        return;
    }

    profiles_ini = ini_read_ex(cfg_fn, 1);  /* lgtm[cpp/path-injection] */
    if (profiles_ini == NULL) {
        hdd_audio_log("HDD Audio: Failed to load hdd_audio_profiles.cfg\n");
        return;
    }

    audio_profile_count = 0;

    /* Load profiles by trying known profile section names */
    for (int i = 0; i < HDD_AUDIO_PROFILE_MAX && audio_profile_count < HDD_AUDIO_PROFILE_MAX; i++) {
        char section_name[64];
        sprintf(section_name, "Profile \"%d\"", i);

        ini_section_t cat = ini_find_section(profiles_ini, section_name);
        if (cat == NULL)
            continue;

        hdd_audio_profile_config_t *config = &audio_profiles[audio_profile_count];
        memset(config, 0, sizeof(hdd_audio_profile_config_t));

        config->id = ini_section_get_int(cat, "id", i);

        const char *name = ini_section_get_string(cat, "name", "Unknown");
        strncpy(config->name, name, sizeof(config->name) - 1);

        const char *internal_name = ini_section_get_string(cat, "internal_name", "unknown");
        strncpy(config->internal_name, internal_name, sizeof(config->internal_name) - 1);

        config->rpm = ini_section_get_int(cat, "rpm", 0);

        /* Load spindle motor sample files */
        const char *file = ini_section_get_string(cat, "spindlemotor_start_file", "");
        strncpy(config->spindlemotor_start.filename, file, sizeof(config->spindlemotor_start.filename) - 1);
        config->spindlemotor_start.volume = (float) ini_section_get_double(cat, "spindlemotor_start_volume", 1.0);

        file = ini_section_get_string(cat, "spindlemotor_loop_file", "");
        strncpy(config->spindlemotor_loop.filename, file, sizeof(config->spindlemotor_loop.filename) - 1);
        config->spindlemotor_loop.volume = (float) ini_section_get_double(cat, "spindlemotor_loop_volume", 1.0);

        file = ini_section_get_string(cat, "spindlemotor_stop_file", "");
        strncpy(config->spindlemotor_stop.filename, file, sizeof(config->spindlemotor_stop.filename) - 1);
        config->spindlemotor_stop.volume = (float) ini_section_get_double(cat, "spindlemotor_stop_volume", 1.0);

        /* Load seek sample file */
        file = ini_section_get_string(cat, "seek_track_file", "");
        strncpy(config->seek_track.filename, file, sizeof(config->seek_track.filename) - 1);
        config->seek_track.volume = (float) ini_section_get_double(cat, "seek_track_volume", 1.0);

        hdd_audio_log("HDD Audio: Loaded profile %d: %s (%s)\n",
                      audio_profile_count, config->name, config->internal_name);

        audio_profile_count++;
    }

    ini_close(profiles_ini);

    hdd_audio_log("HDD Audio: Loaded %d audio profiles\n", audio_profile_count);
}

/* Public API functions */
int
hdd_audio_get_profile_count(void)
{
    return audio_profile_count;
}

const hdd_audio_profile_config_t *
hdd_audio_get_profile(int id)
{
    if (id < 0 || id >= audio_profile_count)
        return NULL;
    return &audio_profiles[id];
}

const char *
hdd_audio_get_profile_name(int id)
{
    if (id < 0 || id >= audio_profile_count)
        return NULL;
    return audio_profiles[id].name;
}

const char *
hdd_audio_get_profile_internal_name(int id)
{
    if (id < 0 || id >= audio_profile_count)
        return NULL;
    return audio_profiles[id].internal_name;
}

uint32_t
hdd_audio_get_profile_rpm(int id)
{
    if (id < 0 || id >= audio_profile_count)
        return 0;
    return audio_profiles[id].rpm;
}

int
hdd_audio_get_profile_by_internal_name(const char *internal_name)
{
    if (!internal_name)
        return 0;

    for (int i = 0; i < audio_profile_count; i++) {
        if (strcmp(audio_profiles[i].internal_name, internal_name) == 0)
            return i;
    }
    return 0;
}

void
hdd_audio_close(void)
{
    /* Free all loaded profile samples */
    for (int i = 0; i < HDD_AUDIO_PROFILE_MAX; i++) {
        if (profile_samples[i].spindle_start_buffer) {
            free(profile_samples[i].spindle_start_buffer);
            profile_samples[i].spindle_start_buffer = NULL;
        }
        if (profile_samples[i].spindle_loop_buffer) {
            free(profile_samples[i].spindle_loop_buffer);
            profile_samples[i].spindle_loop_buffer = NULL;
        }
        if (profile_samples[i].spindle_stop_buffer) {
            free(profile_samples[i].spindle_stop_buffer);
            profile_samples[i].spindle_stop_buffer = NULL;
        }
        if (profile_samples[i].seek_buffer) {
            free(profile_samples[i].seek_buffer);
            profile_samples[i].seek_buffer = NULL;
        }
        profile_samples[i].loaded = 0;
    }
    
    if (hdd_audio_mutex) {
        thread_close_mutex(hdd_audio_mutex);
        hdd_audio_mutex = NULL;
    }
}

/* Load samples for a specific profile */
static void
hdd_audio_load_profile_samples(int profile_id)
{
    if (profile_id < 0 || profile_id >= audio_profile_count)
        return;
    
    hdd_audio_profile_config_t *config  = &audio_profiles[profile_id];
    hdd_audio_samples_t        *samples = &profile_samples[profile_id];
    
    /* Already loaded? */
    if (samples->loaded)
        return;
    
    /* Profile 0 is "None" - no audio */
    if (profile_id == 0 || strcmp(config->internal_name, "none") == 0) {
        samples->loaded = 1;
        return;
    }

    hdd_audio_log("HDD Audio: Loading samples for profile %d (%s)\n", profile_id, config->name);
    
    /* Load spindle loop (main running sound) */
    if (config->spindlemotor_loop.filename[0]) {
        samples->spindle_loop_buffer = sound_load_wav(
            config->spindlemotor_loop.filename,
            &samples->spindle_loop_samples);
        if (samples->spindle_loop_buffer) {
            samples->spindle_loop_volume = config->spindlemotor_loop.volume;
            hdd_audio_log("HDD Audio: Loaded spindle loop, %d frames\n", samples->spindle_loop_samples);
        } else {
            hdd_audio_log("HDD Audio: Failed to load spindle loop: %s\n", config->spindlemotor_loop.filename);
        }
    }
    
    /* Load spindle start */
    if (config->spindlemotor_start.filename[0]) {
        samples->spindle_start_buffer = sound_load_wav(
            config->spindlemotor_start.filename,
            &samples->spindle_start_samples);
        if (samples->spindle_start_buffer) {
            samples->spindle_start_volume = config->spindlemotor_start.volume;
            hdd_audio_log("HDD Audio: Loaded spindle start, %d frames\n", samples->spindle_start_samples);
        }
    }
    
    /* Load spindle stop */
    if (config->spindlemotor_stop.filename[0]) {
        samples->spindle_stop_buffer = sound_load_wav(
            config->spindlemotor_stop.filename,
            &samples->spindle_stop_samples);
        if (samples->spindle_stop_buffer) {
            samples->spindle_stop_volume = config->spindlemotor_stop.volume;
            hdd_audio_log("HDD Audio: Loaded spindle stop, %d frames\n", samples->spindle_stop_samples);
        }
    }
    
    /* Load seek sound */
    if (config->seek_track.filename[0]) {
        samples->seek_buffer = sound_load_wav(
            config->seek_track.filename,
            &samples->seek_samples);
        if (samples->seek_buffer) {
            samples->seek_volume = config->seek_track.volume;
            hdd_audio_log("HDD Audio: Loaded seek sound, %d frames (%.1f ms)\n", 
                          samples->seek_samples, (float)samples->seek_samples / 48.0f);
        } else {
            hdd_audio_log("HDD Audio: Failed to load seek sound: %s\n", config->seek_track.filename);
        }
    }
    
    samples->loaded = 1;
}

/* Find drive state for a given HDD index, or NULL if not tracked */
static hdd_audio_drive_state_t *
hdd_audio_find_drive_state(int hdd_index)
{
    for (int i = 0; i < active_drive_count; i++) {
        if (drive_states[i].hdd_index == hdd_index)
            return &drive_states[i];
    }
    return NULL;
}

void
hdd_audio_init(void)
{
    /* Initialize profile samples */
    memset(profile_samples, 0, sizeof(profile_samples));
    memset(drive_states, 0, sizeof(drive_states));
    active_drive_count = 0;
    
    hdd_audio_log("HDD Audio Init: audio_profile_count=%d\n", audio_profile_count);
    
    /* Create mutex BEFORE loading samples or calling spinup */
    if (!hdd_audio_mutex)
        hdd_audio_mutex = thread_create_mutex();

    /* Find all HDDs with valid audio profiles and initialize their states */
    for (int i = 0; i < HDD_NUM && active_drive_count < HDD_AUDIO_MAX_DRIVES; i++) {
        if (hdd[i].bus_type != HDD_BUS_DISABLED && hdd[i].audio_profile > 0) {
            hdd_audio_log("HDD Audio Init: HDD %d bus_type=%d audio_profile=%d\n", 
                          i, hdd[i].bus_type, hdd[i].audio_profile);
            
            hdd_audio_drive_state_t *state = &drive_states[active_drive_count];
            state->hdd_index = i;
            state->profile_id = hdd[i].audio_profile;
            state->spindle_state = HDD_SPINDLE_STOPPED;
            state->spindle_pos = 0;
            state->spindle_transition_pos = 0;
            
            /* Initialize seek voices for this drive */
            for (int v = 0; v < HDD_MAX_SEEK_VOICES_PER_HDD; v++) {
                state->seek_voices[v].active = 0;
                state->seek_voices[v].position = 0;
                state->seek_voices[v].volume = 1.0f;
                state->seek_voices[v].profile_id = state->profile_id;
            }
            
            /* Load samples for this profile if not already loaded */
            hdd_audio_load_profile_samples(state->profile_id);
            
            hdd_audio_log("HDD Audio: Initialized drive %d with profile %d (%s)\n", 
                          i, state->profile_id, 
                          hdd_audio_get_profile_name(state->profile_id));
            
            active_drive_count++;
        }
    }
    
    hdd_audio_log("HDD Audio Init: %d active drives with audio\n", active_drive_count);
    
    /* Start spindle motors for all active drives */
    for (int i = 0; i < active_drive_count; i++) {
        hdd_audio_spinup_drive(drive_states[i].hdd_index);
    }

    sound_hdd_thread_init();
}

void
hdd_audio_reset(void)
{
    hdd_audio_log("HDD Audio: Reset\n");
    
    /* Lock mutex to prevent audio callback from accessing buffers during reset */
    if (hdd_audio_mutex)
        thread_wait_mutex(hdd_audio_mutex);
    
    /* Reset all drive states */
    for (int i = 0; i < active_drive_count; i++) {
        drive_states[i].spindle_state = HDD_SPINDLE_STOPPED;
        drive_states[i].spindle_pos = 0;
        drive_states[i].spindle_transition_pos = 0;
        for (int v = 0; v < HDD_MAX_SEEK_VOICES_PER_HDD; v++) {
            drive_states[i].seek_voices[v].active = 0;
            drive_states[i].seek_voices[v].position = 0;
            drive_states[i].seek_voices[v].volume = 1.0f;
        }
    }
    active_drive_count = 0;
    
    /* Free previously loaded samples (but keep profiles) */
    for (int i = 0; i < HDD_AUDIO_PROFILE_MAX; i++) {
        if (profile_samples[i].spindle_start_buffer) {
            free(profile_samples[i].spindle_start_buffer);
            profile_samples[i].spindle_start_buffer = NULL;
        }
        if (profile_samples[i].spindle_loop_buffer) {
            free(profile_samples[i].spindle_loop_buffer);
            profile_samples[i].spindle_loop_buffer = NULL;
        }
        if (profile_samples[i].spindle_stop_buffer) {
            free(profile_samples[i].spindle_stop_buffer);
            profile_samples[i].spindle_stop_buffer = NULL;
        }
        if (profile_samples[i].seek_buffer) {
            free(profile_samples[i].seek_buffer);
            profile_samples[i].seek_buffer = NULL;
        }
        profile_samples[i].loaded = 0;
    }
    
    if (hdd_audio_mutex)
        thread_release_mutex(hdd_audio_mutex);
    
    /* Find all HDDs with valid audio profiles and initialize their states */
    for (int i = 0; i < HDD_NUM && active_drive_count < HDD_AUDIO_MAX_DRIVES; i++) {
        if (hdd[i].bus_type != HDD_BUS_DISABLED && hdd[i].audio_profile > 0) {
            hdd_audio_log("HDD Audio Reset: HDD %d audio_profile=%d\n", i, hdd[i].audio_profile);
            
            hdd_audio_drive_state_t *state = &drive_states[active_drive_count];
            state->hdd_index = i;
            state->profile_id = hdd[i].audio_profile;
            state->spindle_state = HDD_SPINDLE_STOPPED;
            state->spindle_pos = 0;
            state->spindle_transition_pos = 0;
            
            /* Initialize seek voices for this drive */
            for (int v = 0; v < HDD_MAX_SEEK_VOICES_PER_HDD; v++) {
                state->seek_voices[v].active = 0;
                state->seek_voices[v].position = 0;
                state->seek_voices[v].volume = 1.0f;
                state->seek_voices[v].profile_id = state->profile_id;
            }
            
            /* Load samples for this profile if not already loaded */
            hdd_audio_load_profile_samples(state->profile_id);
            
            hdd_audio_log("HDD Audio: Reset drive %d with profile %d (%s)\n", 
                          i, state->profile_id, 
                          hdd_audio_get_profile_name(state->profile_id));
            
            active_drive_count++;
        }
    }
    
    hdd_audio_log("HDD Audio Reset: %d active drives with audio\n", active_drive_count);
    
    /* Start spindle motors for all active drives */
    for (int i = 0; i < active_drive_count; i++) {
        hdd_audio_spinup_drive(drive_states[i].hdd_index);
    }
}

void
hdd_audio_seek(hard_disk_t *hdd_drive, uint32_t new_cylinder)
{
    uint32_t cylinder_diff = abs((int) hdd_drive->cur_cylinder - (int) new_cylinder);

    if (cylinder_diff == 0)
        return;

    /* Find the drive state for this HDD */
    hdd_audio_drive_state_t *drive_state = NULL;
    for (int i = 0; i < active_drive_count; i++) {
        if (&hdd[drive_states[i].hdd_index] == hdd_drive) {
            drive_state = &drive_states[i];
            break;
        }
    }
    
    /* If no drive state found, drive has no audio profile */
    if (!drive_state)
        return;
    
    int profile_id = drive_state->profile_id;
    
    /* No audio profile selected */
    if (profile_id == 0 || profile_id >= audio_profile_count)
        return;
    
    /* Load samples if not already loaded */
    if (!profile_samples[profile_id].loaded)
        hdd_audio_load_profile_samples(profile_id);
    
    hdd_audio_samples_t *samples = &profile_samples[profile_id];
    
    if (!samples->seek_buffer || samples->seek_samples == 0) {
        return;
    }

    /* Mutex must exist */
    if (!hdd_audio_mutex)
        return;

    int min_seek_spacing = 0;
    if (hdd_drive->cyl_switch_usec > 0)
        min_seek_spacing = (int)(hdd_drive->cyl_switch_usec * 48000.0 / 1000000.0);

    thread_wait_mutex(hdd_audio_mutex);

    /* Check if we should skip due to minimum spacing (per-drive) */
    for (int v = 0; v < HDD_MAX_SEEK_VOICES_PER_HDD; v++) {
        if (drive_state->seek_voices[v].active) {
            int pos = drive_state->seek_voices[v].position;
            if (pos >= 0 && pos < min_seek_spacing) {
                thread_release_mutex(hdd_audio_mutex);
                return;
            }
        }
    }

    /* Find a free seek voice for this drive */
    for (int v = 0; v < HDD_MAX_SEEK_VOICES_PER_HDD; v++) {
        if (!drive_state->seek_voices[v].active) {
            drive_state->seek_voices[v].active = 1;
            drive_state->seek_voices[v].position = 0;
            drive_state->seek_voices[v].volume = samples->seek_volume;
            drive_state->seek_voices[v].profile_id = profile_id;
            thread_release_mutex(hdd_audio_mutex);
            return;
        }
    }

    thread_release_mutex(hdd_audio_mutex);
}

/* Spinup a specific drive by HDD index */
void
hdd_audio_spinup_drive(int hdd_index)
{
    hdd_audio_drive_state_t *state = hdd_audio_find_drive_state(hdd_index);
    if (!state)
        return;
    
    if (state->spindle_state == HDD_SPINDLE_RUNNING || state->spindle_state == HDD_SPINDLE_STARTING)
        return;

    hdd_audio_log("HDD Audio: Spinup requested for drive %d (current state: %d)\n", hdd_index, state->spindle_state);

    if (hdd_audio_mutex)
        thread_wait_mutex(hdd_audio_mutex);
    state->spindle_state = HDD_SPINDLE_STARTING;
    state->spindle_transition_pos = 0;
    if (hdd_audio_mutex)
        thread_release_mutex(hdd_audio_mutex);
}

/* Spindown a specific drive by HDD index */
void
hdd_audio_spindown_drive(int hdd_index)
{
    hdd_audio_drive_state_t *state = hdd_audio_find_drive_state(hdd_index);
    if (!state)
        return;
    
    if (state->spindle_state == HDD_SPINDLE_STOPPED || state->spindle_state == HDD_SPINDLE_STOPPING)
        return;

    hdd_audio_log("HDD Audio: Spindown requested for drive %d (current state: %d)\n", hdd_index, state->spindle_state);

    if (hdd_audio_mutex)
        thread_wait_mutex(hdd_audio_mutex);
    state->spindle_state = HDD_SPINDLE_STOPPING;
    state->spindle_transition_pos = 0;
    if (hdd_audio_mutex)
        thread_release_mutex(hdd_audio_mutex);
}

/* Legacy functions for backward compatibility - operate on all drives */
void
hdd_audio_spinup(void)
{
    for (int i = 0; i < active_drive_count; i++) {
        hdd_audio_spinup_drive(drive_states[i].hdd_index);
    }
}

void
hdd_audio_spindown(void)
{
    for (int i = 0; i < active_drive_count; i++) {
        hdd_audio_spindown_drive(drive_states[i].hdd_index);
    }
}

hdd_spindle_state_t
hdd_audio_get_spindle_state(void)
{
    /* Return running if any drive is running */
    for (int i = 0; i < active_drive_count; i++) {
        if (drive_states[i].spindle_state == HDD_SPINDLE_RUNNING)
            return HDD_SPINDLE_RUNNING;
    }
    for (int i = 0; i < active_drive_count; i++) {
        if (drive_states[i].spindle_state == HDD_SPINDLE_STARTING)
            return HDD_SPINDLE_STARTING;
    }
    for (int i = 0; i < active_drive_count; i++) {
        if (drive_states[i].spindle_state == HDD_SPINDLE_STOPPING)
            return HDD_SPINDLE_STOPPING;
    }
    return HDD_SPINDLE_STOPPED;
}

hdd_spindle_state_t
hdd_audio_get_drive_spindle_state(int hdd_index)
{
    hdd_audio_drive_state_t *state = hdd_audio_find_drive_state(hdd_index);
    if (!state)
        return HDD_SPINDLE_STOPPED;
    return state->spindle_state;
}

/* Helper: Mix spindle start sound into float buffer */
static void
hdd_audio_mix_spindle_start_float(hdd_audio_drive_state_t *state, hdd_audio_samples_t *samples,
                                   float *float_buffer, int frames_in_buffer)
{
    if (!samples->spindle_start_buffer || samples->spindle_start_samples <= 0) {
        state->spindle_state = HDD_SPINDLE_RUNNING;
        state->spindle_pos = 0;
        return;
    }
    
    float start_volume = samples->spindle_start_volume;
    for (int i = 0; i < frames_in_buffer && state->spindle_transition_pos < samples->spindle_start_samples; i++) {
        float left_sample = (float) samples->spindle_start_buffer[state->spindle_transition_pos * 2] / 131072.0f * start_volume;
        float right_sample = (float) samples->spindle_start_buffer[state->spindle_transition_pos * 2 + 1] / 131072.0f * start_volume;
        float_buffer[i * 2]     += left_sample;
        float_buffer[i * 2 + 1] += right_sample;
        state->spindle_transition_pos++;
    }
    
    if (state->spindle_transition_pos >= samples->spindle_start_samples) {
        state->spindle_state = HDD_SPINDLE_RUNNING;
        state->spindle_pos = 0;
        hdd_audio_log("HDD Audio: Drive %d spinup complete, now running\n", state->hdd_index);
    }
}

/* Helper: Mix spindle loop sound into float buffer */
static void
hdd_audio_mix_spindle_loop_float(hdd_audio_drive_state_t *state, hdd_audio_samples_t *samples,
                                  float *float_buffer, int frames_in_buffer)
{
    if (!samples->spindle_loop_buffer || samples->spindle_loop_samples <= 0)
        return;
    
    float spindle_volume = samples->spindle_loop_volume;
    for (int i = 0; i < frames_in_buffer; i++) {
        float left_sample = (float) samples->spindle_loop_buffer[state->spindle_pos * 2] / 131072.0f * spindle_volume;
        float right_sample = (float) samples->spindle_loop_buffer[state->spindle_pos * 2 + 1] / 131072.0f * spindle_volume;
        float_buffer[i * 2]     += left_sample;
        float_buffer[i * 2 + 1] += right_sample;

        state->spindle_pos++;
        if (state->spindle_pos >= samples->spindle_loop_samples) {
            state->spindle_pos = 0;
        }
    }
}

/* Helper: Mix spindle stop sound into float buffer */
static void
hdd_audio_mix_spindle_stop_float(hdd_audio_drive_state_t *state, hdd_audio_samples_t *samples,
                                  float *float_buffer, int frames_in_buffer)
{
    if (!samples->spindle_stop_buffer || samples->spindle_stop_samples <= 0) {
        state->spindle_state = HDD_SPINDLE_STOPPED;
        return;
    }
    
    float stop_volume = samples->spindle_stop_volume;
    for (int i = 0; i < frames_in_buffer && state->spindle_transition_pos < samples->spindle_stop_samples; i++) {
        float left_sample = (float) samples->spindle_stop_buffer[state->spindle_transition_pos * 2] / 131072.0f * stop_volume;
        float right_sample = (float) samples->spindle_stop_buffer[state->spindle_transition_pos * 2 + 1] / 131072.0f * stop_volume;
        float_buffer[i * 2]     += left_sample;
        float_buffer[i * 2 + 1] += right_sample;
        state->spindle_transition_pos++;
    }
    
    if (state->spindle_transition_pos >= samples->spindle_stop_samples) {
        state->spindle_state = HDD_SPINDLE_STOPPED;
        hdd_audio_log("HDD Audio: Drive %d spindown complete, now stopped\n", state->hdd_index);
    }
}

/* Helper: Mix seek sounds into float buffer */
static void
hdd_audio_mix_seek_float(hdd_audio_drive_state_t *state, float *float_buffer, int frames_in_buffer)
{
    for (int v = 0; v < HDD_MAX_SEEK_VOICES_PER_HDD; v++) {
        if (!state->seek_voices[v].active)
            continue;

        int seek_profile_id = state->seek_voices[v].profile_id;
        hdd_audio_samples_t *seek_samples = &profile_samples[seek_profile_id];
        if (!seek_samples->seek_buffer || seek_samples->seek_samples == 0)
            continue;

        float voice_vol = state->seek_voices[v].volume;
        int pos = state->seek_voices[v].position;
        if (pos < 0) pos = 0;

        for (int i = 0; i < frames_in_buffer && pos < seek_samples->seek_samples; i++, pos++) {
            float seek_left = (float) seek_samples->seek_buffer[pos * 2] / 131072.0f * voice_vol;
            float seek_right = (float) seek_samples->seek_buffer[pos * 2 + 1] / 131072.0f * voice_vol;

            float_buffer[i * 2]     += seek_left;
            float_buffer[i * 2 + 1] += seek_right;
        }

        if (pos >= seek_samples->seek_samples) {
            state->seek_voices[v].active = 0;
            state->seek_voices[v].position = 0;
        } else {
            state->seek_voices[v].position = pos;
        }
    }
}

/* Helper: Mix spindle start sound into int16 buffer */
static void
hdd_audio_mix_spindle_start_int16(hdd_audio_drive_state_t *state, hdd_audio_samples_t *samples,
                                   int16_t *buffer, int frames_in_buffer)
{
    if (!samples->spindle_start_buffer || samples->spindle_start_samples <= 0) {
        state->spindle_state = HDD_SPINDLE_RUNNING;
        state->spindle_pos = 0;
        return;
    }
    
    float start_volume = samples->spindle_start_volume;
    for (int i = 0; i < frames_in_buffer && state->spindle_transition_pos < samples->spindle_start_samples; i++) {
        int32_t left = buffer[i * 2] + (int32_t)(samples->spindle_start_buffer[state->spindle_transition_pos * 2] * start_volume);
        int32_t right = buffer[i * 2 + 1] + (int32_t)(samples->spindle_start_buffer[state->spindle_transition_pos * 2 + 1] * start_volume);
        if (left > 32767) left = 32767;
        if (left < -32768) left = -32768;
        if (right > 32767) right = 32767;
        if (right < -32768) right = -32768;
        buffer[i * 2]     = (int16_t) left;
        buffer[i * 2 + 1] = (int16_t) right;
        state->spindle_transition_pos++;
    }
    
    if (state->spindle_transition_pos >= samples->spindle_start_samples) {
        state->spindle_state = HDD_SPINDLE_RUNNING;
        state->spindle_pos = 0;
        hdd_audio_log("HDD Audio: Drive %d spinup complete, now running\n", state->hdd_index);
    }
}

/* Helper: Mix spindle loop sound into int16 buffer */
static void
hdd_audio_mix_spindle_loop_int16(hdd_audio_drive_state_t *state, hdd_audio_samples_t *samples,
                                  int16_t *buffer, int frames_in_buffer)
{
    if (!samples->spindle_loop_buffer || samples->spindle_loop_samples <= 0)
        return;
    
    float spindle_volume = samples->spindle_loop_volume;
    for (int i = 0; i < frames_in_buffer; i++) {
        int32_t left = buffer[i * 2] + (int32_t)(samples->spindle_loop_buffer[state->spindle_pos * 2] * spindle_volume);
        int32_t right = buffer[i * 2 + 1] + (int32_t)(samples->spindle_loop_buffer[state->spindle_pos * 2 + 1] * spindle_volume);
        if (left > 32767) left = 32767;
        if (left < -32768) left = -32768;
        if (right > 32767) right = 32767;
        if (right < -32768) right = -32768;
        buffer[i * 2]     = (int16_t) left;
        buffer[i * 2 + 1] = (int16_t) right;

        state->spindle_pos++;
        if (state->spindle_pos >= samples->spindle_loop_samples) {
            state->spindle_pos = 0;
        }
    }
}

/* Helper: Mix spindle stop sound into int16 buffer */
static void
hdd_audio_mix_spindle_stop_int16(hdd_audio_drive_state_t *state, hdd_audio_samples_t *samples,
                                  int16_t *buffer, int frames_in_buffer)
{
    if (!samples->spindle_stop_buffer || samples->spindle_stop_samples <= 0) {
        state->spindle_state = HDD_SPINDLE_STOPPED;
        return;
    }
    
    float stop_volume = samples->spindle_stop_volume;
    for (int i = 0; i < frames_in_buffer && state->spindle_transition_pos < samples->spindle_stop_samples; i++) {
        int32_t left = buffer[i * 2] + (int32_t)(samples->spindle_stop_buffer[state->spindle_transition_pos * 2] * stop_volume);
        int32_t right = buffer[i * 2 + 1] + (int32_t)(samples->spindle_stop_buffer[state->spindle_transition_pos * 2 + 1] * stop_volume);
        if (left > 32767) left = 32767;
        if (left < -32768) left = -32768;
        if (right > 32767) right = 32767;
        if (right < -32768) right = -32768;
        buffer[i * 2]     = (int16_t) left;
        buffer[i * 2 + 1] = (int16_t) right;
        state->spindle_transition_pos++;
    }
    
    if (state->spindle_transition_pos >= samples->spindle_stop_samples) {
        state->spindle_state = HDD_SPINDLE_STOPPED;
        hdd_audio_log("HDD Audio: Drive %d spindown complete, now stopped\n", state->hdd_index);
    }
}

/* Helper: Mix seek sounds into int16 buffer */
static void
hdd_audio_mix_seek_int16(hdd_audio_drive_state_t *state, int16_t *buffer, int frames_in_buffer)
{
    for (int v = 0; v < HDD_MAX_SEEK_VOICES_PER_HDD; v++) {
        if (!state->seek_voices[v].active)
            continue;

        int seek_profile_id = state->seek_voices[v].profile_id;
        hdd_audio_samples_t *seek_samples = &profile_samples[seek_profile_id];
        if (!seek_samples->seek_buffer || seek_samples->seek_samples == 0)
            continue;

        float voice_vol = state->seek_voices[v].volume;
        int pos = state->seek_voices[v].position;
        if (pos < 0) pos = 0;

        for (int i = 0; i < frames_in_buffer && pos < seek_samples->seek_samples; i++, pos++) {
            int32_t left = buffer[i * 2] + (int32_t)(seek_samples->seek_buffer[pos * 2] * voice_vol);
            int32_t right = buffer[i * 2 + 1] + (int32_t)(seek_samples->seek_buffer[pos * 2 + 1] * voice_vol);

            if (left > 32767) left = 32767;
            if (left < -32768) left = -32768;
            if (right > 32767) right = 32767;
            if (right < -32768) right = -32768;

            buffer[i * 2]     = (int16_t) left;
            buffer[i * 2 + 1] = (int16_t) right;
        }

        if (pos >= seek_samples->seek_samples) {
            state->seek_voices[v].active = 0;
            state->seek_voices[v].position = 0;
        } else {
            state->seek_voices[v].position = pos;
        }
    }
}

/* Process a single drive's audio in float mode */
static void
hdd_audio_process_drive_float(hdd_audio_drive_state_t *state, float *float_buffer, int frames_in_buffer)
{
    int profile_id = state->profile_id;
    
    if (profile_id <= 0 || profile_id >= HDD_AUDIO_PROFILE_MAX)
        return;
    
    hdd_audio_samples_t *samples = &profile_samples[profile_id];
    if (!samples->loaded)
        return;

    /* Handle spindle states for this drive */
    switch (state->spindle_state) {
        case HDD_SPINDLE_STARTING:
            hdd_audio_mix_spindle_start_float(state, samples, float_buffer, frames_in_buffer);
            break;
        case HDD_SPINDLE_RUNNING:
            hdd_audio_mix_spindle_loop_float(state, samples, float_buffer, frames_in_buffer);
            break;
        case HDD_SPINDLE_STOPPING:
            hdd_audio_mix_spindle_stop_float(state, samples, float_buffer, frames_in_buffer);
            break;
        case HDD_SPINDLE_STOPPED:
        default:
            break;
    }

    /* Seek sounds - only play when spindle is running */
    if (samples->seek_buffer && samples->seek_samples > 0 && 
        hdd_audio_mutex && state->spindle_state == HDD_SPINDLE_RUNNING) {
        thread_wait_mutex(hdd_audio_mutex);
        hdd_audio_mix_seek_float(state, float_buffer, frames_in_buffer);
        thread_release_mutex(hdd_audio_mutex);
    }
}

/* Process a single drive's audio in int16 mode */
static void
hdd_audio_process_drive_int16(hdd_audio_drive_state_t *state, int16_t *buffer, int frames_in_buffer)
{
    int profile_id = state->profile_id;
    
    if (profile_id <= 0 || profile_id >= HDD_AUDIO_PROFILE_MAX)
        return;
    
    hdd_audio_samples_t *samples = &profile_samples[profile_id];
    if (!samples->loaded)
        return;

    /* Handle spindle states for this drive */
    switch (state->spindle_state) {
        case HDD_SPINDLE_STARTING:
            hdd_audio_mix_spindle_start_int16(state, samples, buffer, frames_in_buffer);
            break;
        case HDD_SPINDLE_RUNNING:
            hdd_audio_mix_spindle_loop_int16(state, samples, buffer, frames_in_buffer);
            break;
        case HDD_SPINDLE_STOPPING:
            hdd_audio_mix_spindle_stop_int16(state, samples, buffer, frames_in_buffer);
            break;
        case HDD_SPINDLE_STOPPED:
        default:
            break;
    }

    /* Seek sounds - only play when spindle is running */
    if (samples->seek_buffer && samples->seek_samples > 0 && 
        hdd_audio_mutex && state->spindle_state == HDD_SPINDLE_RUNNING) {
        thread_wait_mutex(hdd_audio_mutex);
        hdd_audio_mix_seek_int16(state, buffer, frames_in_buffer);
        thread_release_mutex(hdd_audio_mutex);
    }
}

void
hdd_audio_callback(int16_t *buffer, int length)
{
    int frames_in_buffer = length / 2;

    if (sound_is_float) {
        float *float_buffer = (float *) buffer;

        /* Initialize buffer to silence */
        for (int i = 0; i < length; i++) {
            float_buffer[i] = 0.0f;
        }

        /* Process each active drive */
        for (int d = 0; d < active_drive_count; d++) {
            hdd_audio_process_drive_float(&drive_states[d], float_buffer, frames_in_buffer);
        }
    } else {
        /* Initialize buffer to silence */
        for (int i = 0; i < length; i++) {
            buffer[i] = 0;
        }

        /* Process each active drive */
        for (int d = 0; d < active_drive_count; d++) {
            hdd_audio_process_drive_int16(&drive_states[d], buffer, frames_in_buffer);
        }
    }
}