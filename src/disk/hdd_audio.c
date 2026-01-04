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

/* Maximum number of simultaneous seek sounds */
#define HDD_MAX_SEEK_VOICES 8

typedef struct {
    int   active;
    int   position;
    float volume;
} hdd_seek_voice_t;

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

/* Active profile for audio playback (first HDD with valid profile) */
static int active_audio_profile = 0;

static hdd_seek_voice_t hdd_seek_voices[HDD_MAX_SEEK_VOICES];
static mutex_t         *hdd_audio_mutex = NULL;

/* Spindle motor state */
static hdd_spindle_state_t spindle_state = HDD_SPINDLE_STOPPED;
static int                 spindle_pos = 0;
static int                 spindle_transition_pos = 0;  /* Position in start/stop sample */

/* Load audio profiles from configuration file */
void
hdd_audio_load_profiles(void)
{
    ini_t profiles_ini;
    char  cfg_fn[1024] = { 0 };

    int ret = asset_getfile("assets/sounds/hdd/hdd_audio_profiles.cfg", cfg_fn, 1024);
    if (!ret) {
        pclog("HDD Audio: Could not find hdd_audio_profiles.cfg\n");
        return;
    }

    profiles_ini = ini_read_ex(cfg_fn, 1);
    if (profiles_ini == NULL) {
        pclog("HDD Audio: Failed to load hdd_audio_profiles.cfg\n");
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

        pclog("HDD Audio: Loaded profile %d: %s (%s)\n",
              audio_profile_count, config->name, config->internal_name);

        audio_profile_count++;
    }

    ini_close(profiles_ini);

    pclog("HDD Audio: Loaded %d audio profiles\n", audio_profile_count);
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
    
    pclog("HDD Audio: Loading samples for profile %d (%s)\n", profile_id, config->name);
    
    /* Load spindle loop (main running sound) */
    if (config->spindlemotor_loop.filename[0]) {
        samples->spindle_loop_buffer = sound_load_wav(
            config->spindlemotor_loop.filename,
            &samples->spindle_loop_samples);
        if (samples->spindle_loop_buffer) {
            samples->spindle_loop_volume = config->spindlemotor_loop.volume;
            pclog("HDD Audio: Loaded spindle loop, %d frames\n", samples->spindle_loop_samples);
        } else {
            pclog("HDD Audio: Failed to load spindle loop: %s\n", config->spindlemotor_loop.filename);
        }
    }
    
    /* Load spindle start */
    if (config->spindlemotor_start.filename[0]) {
        samples->spindle_start_buffer = sound_load_wav(
            config->spindlemotor_start.filename,
            &samples->spindle_start_samples);
        if (samples->spindle_start_buffer) {
            samples->spindle_start_volume = config->spindlemotor_start.volume;
            pclog("HDD Audio: Loaded spindle start, %d frames\n", samples->spindle_start_samples);
        }
    }
    
    /* Load spindle stop */
    if (config->spindlemotor_stop.filename[0]) {
        samples->spindle_stop_buffer = sound_load_wav(
            config->spindlemotor_stop.filename,
            &samples->spindle_stop_samples);
        if (samples->spindle_stop_buffer) {
            samples->spindle_stop_volume = config->spindlemotor_stop.volume;
            pclog("HDD Audio: Loaded spindle stop, %d frames\n", samples->spindle_stop_samples);
        }
    }
    
    /* Load seek sound */
    if (config->seek_track.filename[0]) {
        samples->seek_buffer = sound_load_wav(
            config->seek_track.filename,
            &samples->seek_samples);
        if (samples->seek_buffer) {
            samples->seek_volume = config->seek_track.volume;
            pclog("HDD Audio: Loaded seek sound, %d frames (%.1f ms)\n", 
                  samples->seek_samples, (float)samples->seek_samples / 48.0f);
        } else {
            pclog("HDD Audio: Failed to load seek sound: %s\n", config->seek_track.filename);
        }
    }
    
    samples->loaded = 1;
}

void
hdd_audio_init(void)
{
    /* Initialize profile samples */
    memset(profile_samples, 0, sizeof(profile_samples));
    
    pclog("HDD Audio Init: audio_profile_count=%d\n", audio_profile_count);
    
    /* Find first HDD with a valid audio profile and load its samples */
    active_audio_profile = 0;
    for (int i = 0; i < HDD_NUM; i++) {
        if (hdd[i].bus_type != HDD_BUS_DISABLED) {
            pclog("HDD Audio Init: HDD %d bus_type=%d audio_profile=%d\n", 
                  i, hdd[i].bus_type, hdd[i].audio_profile);
            if (hdd[i].audio_profile > 0) {
                active_audio_profile = hdd[i].audio_profile;
                pclog("HDD Audio: Using profile %d from HDD %d\n", active_audio_profile, i);
                break;
            }
        }
    }
    
    pclog("HDD Audio Init: active_audio_profile=%d\n", active_audio_profile);
    
    for (int i = 0; i < HDD_MAX_SEEK_VOICES; i++) {
        hdd_seek_voices[i].active = 0;
        hdd_seek_voices[i].position = 0;
        hdd_seek_voices[i].volume = 1.0f;
    }

    /* Create mutex BEFORE loading samples or calling spinup */
    if (!hdd_audio_mutex)
        hdd_audio_mutex = thread_create_mutex();

    /* Load samples for the active profile */
    if (active_audio_profile > 0 && active_audio_profile < audio_profile_count) {
        hdd_audio_load_profile_samples(active_audio_profile);
        /* Start spindle motor */
        hdd_audio_spinup();
    }

    sound_hdd_thread_init();
}

void
hdd_audio_reset(void)
{
    pclog("HDD Audio: Reset\n");
    
    /* Lock mutex to prevent audio callback from accessing buffers during reset */
    if (hdd_audio_mutex)
        thread_wait_mutex(hdd_audio_mutex);
    
    /* Reset spindle state first to stop audio playback */
    spindle_state = HDD_SPINDLE_STOPPED;
    spindle_pos = 0;
    spindle_transition_pos = 0;
    
    /* Reset seek voices */
    for (int i = 0; i < HDD_MAX_SEEK_VOICES; i++) {
        hdd_seek_voices[i].active = 0;
        hdd_seek_voices[i].position = 0;
        hdd_seek_voices[i].volume = 1.0f;
    }
    
    /* Reset active profile before freeing buffers */
    active_audio_profile = 0;
    
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
    
    /* Find new active profile from current HDD configuration */
    for (int i = 0; i < HDD_NUM; i++) {
        if (hdd[i].bus_type != HDD_BUS_DISABLED) {
            pclog("HDD Audio Reset: HDD %d audio_profile=%d\n", i, hdd[i].audio_profile);
            if (hdd[i].audio_profile > 0) {
                active_audio_profile = hdd[i].audio_profile;
                pclog("HDD Audio: Reset with profile %d from HDD %d\n", active_audio_profile, i);
                break;
            }
        }
    }
    
    /* Load samples for the active profile */
    if (active_audio_profile > 0 && active_audio_profile < audio_profile_count) {
        hdd_audio_load_profile_samples(active_audio_profile);
        /* Start spindle motor */
        hdd_audio_spinup();
    }
}

void
hdd_audio_seek(hard_disk_t *hdd_drive, uint32_t new_cylinder)
{
    uint32_t cylinder_diff = abs((int) hdd_drive->cur_cylinder - (int) new_cylinder);

    if (cylinder_diff == 0)
        return;

    /* Use the drive's audio profile, fallback to active profile */
    int profile_id = hdd_drive->audio_profile;
    if (profile_id == 0)
        profile_id = active_audio_profile;
    
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

    for (int i = 0; i < HDD_MAX_SEEK_VOICES; i++) {
        if (hdd_seek_voices[i].active) {
            int pos = hdd_seek_voices[i].position;
            if (pos >= 0 && pos < min_seek_spacing) {
                thread_release_mutex(hdd_audio_mutex);
                return;
            }
        }
    }

    for (int i = 0; i < HDD_MAX_SEEK_VOICES; i++) {
        if (!hdd_seek_voices[i].active) {
            hdd_seek_voices[i].active = 1;
            hdd_seek_voices[i].position = 0;
            hdd_seek_voices[i].volume = samples->seek_volume;
            thread_release_mutex(hdd_audio_mutex);
            return;
        }
    }

    thread_release_mutex(hdd_audio_mutex);
}

void
hdd_audio_spinup(void)
{
    if (spindle_state == HDD_SPINDLE_RUNNING || spindle_state == HDD_SPINDLE_STARTING)
        return;
    
    pclog("HDD Audio: Spinup requested (current state: %d)\n", spindle_state);
    
    if (hdd_audio_mutex)
        thread_wait_mutex(hdd_audio_mutex);
    spindle_state = HDD_SPINDLE_STARTING;
    spindle_transition_pos = 0;
    if (hdd_audio_mutex)
        thread_release_mutex(hdd_audio_mutex);
}

void
hdd_audio_spindown(void)
{
    if (spindle_state == HDD_SPINDLE_STOPPED || spindle_state == HDD_SPINDLE_STOPPING)
        return;
    
    pclog("HDD Audio: Spindown requested (current state: %d)\n", spindle_state);
    
    if (hdd_audio_mutex)
        thread_wait_mutex(hdd_audio_mutex);
    spindle_state = HDD_SPINDLE_STOPPING;
    spindle_transition_pos = 0;
    if (hdd_audio_mutex)
        thread_release_mutex(hdd_audio_mutex);
}

hdd_spindle_state_t
hdd_audio_get_spindle_state(void)
{
    return spindle_state;
}

void
hdd_audio_callback(int16_t *buffer, int length)
{
    int frames_in_buffer = length / 2;

    /* Get active profile samples */
    hdd_audio_samples_t *samples = NULL;
    if (active_audio_profile > 0 && active_audio_profile < HDD_AUDIO_PROFILE_MAX) {
        samples = &profile_samples[active_audio_profile];
    }

    if (sound_is_float) {
        float *float_buffer = (float *) buffer;

        /* Initialize buffer to silence */
        for (int i = 0; i < length; i++) {
            float_buffer[i] = 0.0f;
        }

        /* Handle spindle states */
        if (samples) {
            switch (spindle_state) {
                case HDD_SPINDLE_STARTING:
                    /* Play spinup sound */
                    if (samples->spindle_start_buffer && samples->spindle_start_samples > 0) {
                        float start_volume = samples->spindle_start_volume;
                        for (int i = 0; i < frames_in_buffer && spindle_transition_pos < samples->spindle_start_samples; i++) {
                            float left_sample = (float) samples->spindle_start_buffer[spindle_transition_pos * 2] / 131072.0f * start_volume;
                            float right_sample = (float) samples->spindle_start_buffer[spindle_transition_pos * 2 + 1] / 131072.0f * start_volume;
                            float_buffer[i * 2]     = left_sample;
                            float_buffer[i * 2 + 1] = right_sample;
                            spindle_transition_pos++;
                        }
                        if (spindle_transition_pos >= samples->spindle_start_samples) {
                            spindle_state = HDD_SPINDLE_RUNNING;
                            spindle_pos = 0;
                            pclog("HDD Audio: Spinup complete, now running\n");
                        }
                    } else {
                        /* No start sample, go directly to running */
                        spindle_state = HDD_SPINDLE_RUNNING;
                        spindle_pos = 0;
                    }
                    break;

                case HDD_SPINDLE_RUNNING:
                    /* Play spindle loop */
                    if (samples->spindle_loop_buffer && samples->spindle_loop_samples > 0) {
                        float spindle_volume = samples->spindle_loop_volume;
                        for (int i = 0; i < frames_in_buffer; i++) {
                            float left_sample = (float) samples->spindle_loop_buffer[spindle_pos * 2] / 131072.0f * spindle_volume;
                            float right_sample = (float) samples->spindle_loop_buffer[spindle_pos * 2 + 1] / 131072.0f * spindle_volume;
                            float_buffer[i * 2]     = left_sample;
                            float_buffer[i * 2 + 1] = right_sample;

                            spindle_pos++;
                            if (spindle_pos >= samples->spindle_loop_samples) {
                                spindle_pos = 0;
                            }
                        }
                    }
                    break;

                case HDD_SPINDLE_STOPPING:
                    /* Play spindown sound */
                    if (samples->spindle_stop_buffer && samples->spindle_stop_samples > 0) {
                        float stop_volume = samples->spindle_stop_volume;
                        for (int i = 0; i < frames_in_buffer && spindle_transition_pos < samples->spindle_stop_samples; i++) {
                            float left_sample = (float) samples->spindle_stop_buffer[spindle_transition_pos * 2] / 131072.0f * stop_volume;
                            float right_sample = (float) samples->spindle_stop_buffer[spindle_transition_pos * 2 + 1] / 131072.0f * stop_volume;
                            float_buffer[i * 2]     = left_sample;
                            float_buffer[i * 2 + 1] = right_sample;
                            spindle_transition_pos++;
                        }
                        if (spindle_transition_pos >= samples->spindle_stop_samples) {
                            spindle_state = HDD_SPINDLE_STOPPED;
                            pclog("HDD Audio: Spindown complete, now stopped\n");
                        }
                    } else {
                        /* No stop sample, go directly to stopped */
                        spindle_state = HDD_SPINDLE_STOPPED;
                    }
                    break;

                case HDD_SPINDLE_STOPPED:
                default:
                    /* Silence - buffer already zeroed */
                    break;
            }
        }

        /* Seek sounds from profile - only play when spindle is running */
        if (samples && samples->seek_buffer && samples->seek_samples > 0 && 
            hdd_audio_mutex && spindle_state == HDD_SPINDLE_RUNNING) {
            thread_wait_mutex(hdd_audio_mutex);

            for (int v = 0; v < HDD_MAX_SEEK_VOICES; v++) {
                if (!hdd_seek_voices[v].active)
                    continue;

                float voice_vol = hdd_seek_voices[v].volume;
                int pos = hdd_seek_voices[v].position;
                if (pos < 0) pos = 0;

                for (int i = 0; i < frames_in_buffer && pos < samples->seek_samples; i++, pos++) {
                    float seek_left = (float) samples->seek_buffer[pos * 2] / 131072.0f * voice_vol;
                    float seek_right = (float) samples->seek_buffer[pos * 2 + 1] / 131072.0f * voice_vol;

                    float_buffer[i * 2]     += seek_left;
                    float_buffer[i * 2 + 1] += seek_right;
                }

                if (pos >= samples->seek_samples) {
                    hdd_seek_voices[v].active = 0;
                    hdd_seek_voices[v].position = 0;
                } else {
                    hdd_seek_voices[v].position = pos;
                }
            }

            thread_release_mutex(hdd_audio_mutex);
        }
    } else {
        /* Initialize buffer to silence */
        for (int i = 0; i < length; i++) {
            buffer[i] = 0;
        }

        /* Handle spindle states */
        if (samples) {
            switch (spindle_state) {
                case HDD_SPINDLE_STARTING:
                    /* Play spinup sound */
                    if (samples->spindle_start_buffer && samples->spindle_start_samples > 0) {
                        float start_volume = samples->spindle_start_volume;
                        for (int i = 0; i < frames_in_buffer && spindle_transition_pos < samples->spindle_start_samples; i++) {
                            buffer[i * 2]     = (int16_t)(samples->spindle_start_buffer[spindle_transition_pos * 2] * start_volume);
                            buffer[i * 2 + 1] = (int16_t)(samples->spindle_start_buffer[spindle_transition_pos * 2 + 1] * start_volume);
                            spindle_transition_pos++;
                        }
                        if (spindle_transition_pos >= samples->spindle_start_samples) {
                            spindle_state = HDD_SPINDLE_RUNNING;
                            spindle_pos = 0;
                            pclog("HDD Audio: Spinup complete, now running\n");
                        }
                    } else {
                        spindle_state = HDD_SPINDLE_RUNNING;
                        spindle_pos = 0;
                    }
                    break;

                case HDD_SPINDLE_RUNNING:
                    /* Play spindle loop */
                    if (samples->spindle_loop_buffer && samples->spindle_loop_samples > 0) {
                        float spindle_volume = samples->spindle_loop_volume;
                        for (int i = 0; i < frames_in_buffer; i++) {
                            buffer[i * 2]     = (int16_t)(samples->spindle_loop_buffer[spindle_pos * 2] * spindle_volume);
                            buffer[i * 2 + 1] = (int16_t)(samples->spindle_loop_buffer[spindle_pos * 2 + 1] * spindle_volume);

                            spindle_pos++;
                            if (spindle_pos >= samples->spindle_loop_samples) {
                                spindle_pos = 0;
                            }
                        }
                    }
                    break;

                case HDD_SPINDLE_STOPPING:
                    /* Play spindown sound */
                    if (samples->spindle_stop_buffer && samples->spindle_stop_samples > 0) {
                        float stop_volume = samples->spindle_stop_volume;
                        for (int i = 0; i < frames_in_buffer && spindle_transition_pos < samples->spindle_stop_samples; i++) {
                            buffer[i * 2]     = (int16_t)(samples->spindle_stop_buffer[spindle_transition_pos * 2] * stop_volume);
                            buffer[i * 2 + 1] = (int16_t)(samples->spindle_stop_buffer[spindle_transition_pos * 2 + 1] * stop_volume);
                            spindle_transition_pos++;
                        }
                        if (spindle_transition_pos >= samples->spindle_stop_samples) {
                            spindle_state = HDD_SPINDLE_STOPPED;
                            pclog("HDD Audio: Spindown complete, now stopped\n");
                        }
                    } else {
                        spindle_state = HDD_SPINDLE_STOPPED;
                    }
                    break;

                case HDD_SPINDLE_STOPPED:
                default:
                    /* Silence - buffer already zeroed */
                    break;
            }
        }

        /* Seek sounds from profile - only play when spindle is running */
        if (samples && samples->seek_buffer && samples->seek_samples > 0 && 
            hdd_audio_mutex && spindle_state == HDD_SPINDLE_RUNNING) {
            thread_wait_mutex(hdd_audio_mutex);

            for (int v = 0; v < HDD_MAX_SEEK_VOICES; v++) {
                if (!hdd_seek_voices[v].active)
                    continue;

                float voice_vol = hdd_seek_voices[v].volume;
                int pos = hdd_seek_voices[v].position;
                if (pos < 0) pos = 0;

                for (int i = 0; i < frames_in_buffer && pos < samples->seek_samples; i++, pos++) {
                    int32_t left = buffer[i * 2] + (int32_t)(samples->seek_buffer[pos * 2] * voice_vol);
                    int32_t right = buffer[i * 2 + 1] + (int32_t)(samples->seek_buffer[pos * 2 + 1] * voice_vol);

                    if (left > 32767) left = 32767;
                    if (left < -32768) left = -32768;
                    if (right > 32767) right = 32767;
                    if (right < -32768) right = -32768;

                    buffer[i * 2]     = (int16_t) left;
                    buffer[i * 2 + 1] = (int16_t) right;
                }

                if (pos >= samples->seek_samples) {
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