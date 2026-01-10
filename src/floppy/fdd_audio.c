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
#include <86box/sound_util.h>

#ifndef DISABLE_FDD_AUDIO

/* Global audio profile configurations */
static fdd_audio_profile_config_t audio_profiles[FDD_AUDIO_PROFILE_MAX];
static int                        audio_profile_count = 0;

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

extern uint8_t *rom;
extern uint32_t biosmask;
extern uint32_t biosaddr;
typedef enum {
    BIOS_VENDOR_UNKNOWN = 0,
    BIOS_VENDOR_AMI,
    BIOS_VENDOR_AWARD,
    BIOS_VENDOR_PHOENIX,
    BIOS_VENDOR_IBM,
    BIOS_VENDOR_COMPAQ,
    BIOS_VENDOR_OTHER
} bios_vendor_t;

#ifdef ENABLE_FDD_LOG
int fdd_audio_do_log = ENABLE_FDD_LOG;

static void
fdd_log(const char *fmt, ...)
{
    va_list ap;

    if (fdd_audio_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#        define fdd_log(fmt, ...)
#endif

/* Detect BIOS vendor by scanning ROM for signature strings */
static bios_vendor_t
fdd_audio_detect_bios_vendor(void)
{
    if (!rom || biosmask == 0)
        return BIOS_VENDOR_UNKNOWN;

    /* Search for BIOS vendor strings in ROM */
    for (uint32_t i = 0; i < (biosmask + 1); i++) {
        /* AMI BIOS signatures */
        if ((i + 7) < (biosmask + 1)) {
            if (memcmp(&rom[i], "AMIBIOS", 7) == 0) {
                fdd_log("FDD Audio: Detected AMI BIOS\n");
                return BIOS_VENDOR_AMI;
            }
            if (memcmp(&rom[i], "American Megatrends", 19) == 0) {
                fdd_log("FDD Audio: Detected AMI BIOS (American Megatrends)\n");
                return BIOS_VENDOR_AMI;
            }
        }

        /* Award BIOS signatures */
        if ((i + 5) < (biosmask + 1)) {
            if (memcmp(&rom[i], "Award", 5) == 0) {
                fdd_log("FDD Audio: Detected Award BIOS\n");
                return BIOS_VENDOR_AWARD;
            }
        }

        /* Phoenix BIOS signatures */
        if ((i + 7) < (biosmask + 1)) {
            if (memcmp(&rom[i], "Phoenix", 7) == 0) {
                fdd_log("FDD Audio: Detected Phoenix BIOS\n");
                return BIOS_VENDOR_PHOENIX;
            }
        }

        /* IBM BIOS signatures */
        if ((i + 3) < (biosmask + 1)) {
            if (memcmp(&rom[i], "IBM", 3) == 0 && (i + 10) < (biosmask + 1)) {
                if (memcmp(&rom[i], "IBM CORP", 8) == 0) {
                    fdd_log("FDD Audio: Detected IBM BIOS\n");
                    return BIOS_VENDOR_IBM;
                }
            }
        }

        /* Compaq BIOS signatures */
        if ((i + 6) < (biosmask + 1)) {
            if (memcmp(&rom[i], "COMPAQ", 6) == 0) {
                fdd_log("FDD Audio: Detected Compaq BIOS\n");
                return BIOS_VENDOR_COMPAQ;
            }
        }
    }

    fdd_log("FDD Audio: BIOS vendor unknown\n");
    return BIOS_VENDOR_UNKNOWN;
}

/* Determine if this BIOS uses POST-mode FDC seeks */
static int
fdd_audio_get_bios_vendor(void)
{
    static bios_vendor_t detected_vendor = BIOS_VENDOR_UNKNOWN;
    static int           detection_done  = 0;

    /* Only detect once */
    if (!detection_done) {
        detected_vendor = fdd_audio_detect_bios_vendor();
        detection_done  = 1;
    }

    return detected_vendor;
}

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

                /* POST mode seek down samples */
                snprintf(key, sizeof(key), "post_seek_down_%dtrack_file", track_count);
                filename = ini_section_get_string(section, key, "");
                strncpy(profile->post_seek_down[track_count - 1].filename, filename,
                        sizeof(profile->post_seek_down[track_count - 1].filename) - 1);
                profile->post_seek_down[track_count - 1].filename[sizeof(profile->post_seek_down[track_count - 1].filename) - 1] = '\0';

                snprintf(key, sizeof(key), "post_seek_down_%dtrack_volume", track_count);
                profile->post_seek_down[track_count - 1].volume = ini_section_get_double(section, key, 1.0);                

                /* BIOS vendor-specific POST mode seek samples */
                static const char *bios_prefixes[] = {
                    NULL,           /* BIOS_VENDOR_UNKNOWN */
                    "amibios",      /* BIOS_VENDOR_AMI */
                    "award",        /* BIOS_VENDOR_AWARD */
                    "phoenix",      /* BIOS_VENDOR_PHOENIX */
                    "ibm",          /* BIOS_VENDOR_IBM */
                    "compaq",       /* BIOS_VENDOR_COMPAQ */
                    NULL            /* BIOS_VENDOR_OTHER */
                };

                for (int vendor = 1; vendor < BIOS_VENDOR_COUNT; vendor++) {
                    if (!bios_prefixes[vendor])
                        continue;

                    /* BIOS-specific POST mode seek up samples */
                    snprintf(key, sizeof(key), "%s_post_seek_up_%dtrack_file", bios_prefixes[vendor], track_count);
                    filename = ini_section_get_string(section, key, "");
                    strncpy(profile->bios_post_seek_up[vendor][track_count - 1].filename, filename,
                            sizeof(profile->bios_post_seek_up[vendor][track_count - 1].filename) - 1);
                    profile->bios_post_seek_up[vendor][track_count - 1].filename[sizeof(profile->bios_post_seek_up[vendor][track_count - 1].filename) - 1] = '\0';

                    snprintf(key, sizeof(key), "%s_post_seek_up_%dtrack_volume", bios_prefixes[vendor], track_count);
                    profile->bios_post_seek_up[vendor][track_count - 1].volume = ini_section_get_double(section, key, 1.0);

                    /* BIOS-specific POST mode seek down samples */
                    snprintf(key, sizeof(key), "%s_post_seek_down_%dtrack_file", bios_prefixes[vendor], track_count);
                    filename = ini_section_get_string(section, key, "");
                    strncpy(profile->bios_post_seek_down[vendor][track_count - 1].filename, filename,
                            sizeof(profile->bios_post_seek_down[vendor][track_count - 1].filename) - 1);
                    profile->bios_post_seek_down[vendor][track_count - 1].filename[sizeof(profile->bios_post_seek_down[vendor][track_count - 1].filename) - 1] = '\0';

                    snprintf(key, sizeof(key), "%s_post_seek_down_%dtrack_volume", bios_prefixes[vendor], track_count);
                    profile->bios_post_seek_down[vendor][track_count - 1].volume = ini_section_get_double(section, key, 1.0);

                    /* BIOS-specific POST mode seek time in milliseconds */
                    snprintf(key, sizeof(key), "%s_post_seek_%dtrack_time_ms", bios_prefixes[vendor], track_count);
                    profile->bios_post_seek_time_ms[vendor][track_count - 1] = ini_section_get_double(section, key, 0.0);
                }

                /* Seek time in milliseconds - used for FDC timing, not sample playback */
                snprintf(key, sizeof(key), "seek_%dtrack_time_ms", track_count);
                profile->seek_time_ms[track_count - 1] = ini_section_get_double(section, key, 6.0 * track_count);

                /* POST mode seek time in milliseconds */
                snprintf(key, sizeof(key), "post_seek_%dtrack_time_ms", track_count);
                profile->post_seek_time_ms[track_count - 1] = ini_section_get_double(section, key, 0.0);
            }

            /* Load timing configurations */
            profile->total_tracks = ini_section_get_int(section, "total_tracks", 0);

            audio_profile_count++;
        }
    }

    ini_close(profiles_ini);

    fdd_log("FDD Audio: Loaded %d audio profiles\n", audio_profile_count);
}

void
load_profile_samples(int profile_id)
{
    if (profile_id < 0 || profile_id >= audio_profile_count)
        return;

    fdd_audio_profile_config_t *config  = &audio_profiles[profile_id];
    drive_audio_samples_t      *samples = &profile_samples[profile_id];

    fdd_log("FDD Audio: Loading samples for profile %d (%s)\n",
            profile_id, config->name);

    /* Load samples if not already loaded */
    if (samples->spindlemotor_start.buffer == NULL && config->spindlemotor_start.filename[0]) {
        strcpy(samples->spindlemotor_start.filename, config->spindlemotor_start.filename);
        samples->spindlemotor_start.volume = config->spindlemotor_start.volume;
        samples->spindlemotor_start.buffer = sound_load_wav(config->spindlemotor_start.filename,
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
        samples->spindlemotor_loop.buffer = sound_load_wav(config->spindlemotor_loop.filename,
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
        samples->spindlemotor_stop.buffer = sound_load_wav(config->spindlemotor_stop.filename,
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
            samples->seek_up[idx].buffer = sound_load_wav(config->seek_up[idx].filename,
                                                    &samples->seek_up[idx].samples);
            if (samples->seek_up[idx].buffer) {
                fdd_log("  Loaded seek_up[%d]: %s (%d samples, volume %.2f)\n",
                        idx, config->seek_up[idx].filename,
                        samples->seek_up[idx].samples, config->seek_up[idx].volume);
            }
        }

        /* Load seek down sample */
        if (samples->seek_down[idx].buffer == NULL && config->seek_down[idx].filename[0]) {
            strcpy(samples->seek_down[idx].filename, config->seek_down[idx].filename);
            samples->seek_down[idx].volume = config->seek_down[idx].volume;
            samples->seek_down[idx].buffer = sound_load_wav(config->seek_down[idx].filename,
                                                      &samples->seek_down[idx].samples);
            if (samples->seek_down[idx].buffer) {
                fdd_log("  Loaded seek_down[%d]: %s (%d samples, volume %.2f)\n",
                        idx, config->seek_down[idx].filename,
                        samples->seek_down[idx].samples, config->seek_down[idx].volume);
            }
        }

        /* Load POST mode seek samples if configured */
        if (config->post_seek_up[idx].filename[0]) {
            if (samples->post_seek_up[idx].buffer == NULL) {
                strcpy(samples->post_seek_up[idx].filename, config->post_seek_up[idx].filename);
                samples->post_seek_up[idx].volume = config->post_seek_up[idx].volume;
                samples->post_seek_up[idx].buffer = sound_load_wav(config->post_seek_up[idx].filename,
                                                             &samples->post_seek_up[idx].samples);
                if (samples->post_seek_up[idx].buffer) {
                    fdd_log("  Loaded POST seek_up[%d] (%d-track): %s (%d samples, volume %.2f)\n",
                            idx, track_count, config->post_seek_up[idx].filename,
                            samples->post_seek_up[idx].samples, config->post_seek_up[idx].volume);
                }
            }
        }

        if (config->post_seek_down[idx].filename[0]) {
            if (samples->post_seek_down[idx].buffer == NULL) {
                strcpy(samples->post_seek_down[idx].filename, config->post_seek_down[idx].filename);
                samples->post_seek_down[idx].volume = config->post_seek_down[idx].volume;
                samples->post_seek_down[idx].buffer = sound_load_wav(config->post_seek_down[idx].filename,
                                                               &samples->post_seek_down[idx].samples);
                if (samples->post_seek_down[idx].buffer) {
                    fdd_log("  Loaded POST seek_down[%d] (%d-track): %s (%d samples, volume %.2f)\n",
                            idx, track_count, config->post_seek_down[idx].filename,
                            samples->post_seek_down[idx].samples, config->post_seek_down[idx].volume);
                }
            }
        }

#ifdef ENABLE_FDD_LOG
        /* Load BIOS vendor-specific POST mode seek samples if configured */
        static const char *bios_names[] = {
            "UNKNOWN", "AMI", "AWARD", "PHOENIX", "IBM", "COMPAQ", "OTHER"
        };
#endif

        for (int vendor = 1; vendor < BIOS_VENDOR_COUNT; vendor++) {
            if (config->bios_post_seek_up[vendor][idx].filename[0]) {
                if (samples->bios_post_seek_up[vendor][idx].buffer == NULL) {
                    strcpy(samples->bios_post_seek_up[vendor][idx].filename, config->bios_post_seek_up[vendor][idx].filename);
                    samples->bios_post_seek_up[vendor][idx].volume = config->bios_post_seek_up[vendor][idx].volume;
                    samples->bios_post_seek_up[vendor][idx].buffer = sound_load_wav(config->bios_post_seek_up[vendor][idx].filename,
                                                                              &samples->bios_post_seek_up[vendor][idx].samples);
                    if (samples->bios_post_seek_up[vendor][idx].buffer) {
                        fdd_log("  Loaded %s POST seek_up[%d] (%d-track): %s (%d samples, volume %.2f)\n",
                                bios_names[vendor], idx, track_count, config->bios_post_seek_up[vendor][idx].filename,
                                samples->bios_post_seek_up[vendor][idx].samples, config->bios_post_seek_up[vendor][idx].volume);
                    }
                }
            }

            if (config->bios_post_seek_down[vendor][idx].filename[0]) {
                if (samples->bios_post_seek_down[vendor][idx].buffer == NULL) {
                    strcpy(samples->bios_post_seek_down[vendor][idx].filename, config->bios_post_seek_down[vendor][idx].filename);
                    samples->bios_post_seek_down[vendor][idx].volume = config->bios_post_seek_down[vendor][idx].volume;
                    samples->bios_post_seek_down[vendor][idx].buffer = sound_load_wav(config->bios_post_seek_down[vendor][idx].filename,
                                                                                &samples->bios_post_seek_down[vendor][idx].samples);
                    if (samples->bios_post_seek_down[vendor][idx].buffer) {
                        fdd_log("  Loaded %s POST seek_down[%d] (%d-track): %s (%d samples, volume %.2f)\n",
                                bios_names[vendor], idx, track_count, config->bios_post_seek_down[vendor][idx].filename,
                                samples->bios_post_seek_down[vendor][idx].samples, config->bios_post_seek_down[vendor][idx].volume);
                    }
                }
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
    if (profile_id < 0 || profile_id >= audio_profile_count) {
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
        /* In POST mode, check for BIOS-specific timing first */
        if (fdd_get_boot_status() == BIOS_BOOT_POST) {
            int bios_vendor = fdd_audio_get_bios_vendor();
            
            /* Check BIOS vendor-specific timing first */
            if (bios_vendor > 0 && bios_vendor < BIOS_VENDOR_COUNT && 
                profile->bios_post_seek_time_ms[bios_vendor][track_count - 1] > 0.0) {
                return profile->bios_post_seek_time_ms[bios_vendor][track_count - 1] * 1000.0;
            }
            
            /* Fall back to generic POST timing */
            if (profile->post_seek_time_ms[track_count - 1] > 0.0) {
                return profile->post_seek_time_ms[track_count - 1] * 1000.0;
            }
        }        
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
            seek_state[i][j].sample_to_play   = NULL;
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
            if (samples->post_seek_up[track_count].buffer) {
                free(samples->post_seek_up[track_count].buffer);
                samples->post_seek_up[track_count].buffer  = NULL;
                samples->post_seek_up[track_count].samples = 0;
            }
            if (samples->post_seek_down[track_count].buffer) {
                free(samples->post_seek_down[track_count].buffer);
                samples->post_seek_down[track_count].buffer  = NULL;
                samples->post_seek_down[track_count].samples = 0;
            }

            /* Free BIOS vendor-specific POST seek samples */
            for (int vendor = 0; vendor < BIOS_VENDOR_COUNT; vendor++) {
                if (samples->bios_post_seek_up[vendor][track_count].buffer) {
                    free(samples->bios_post_seek_up[vendor][track_count].buffer);
                    samples->bios_post_seek_up[vendor][track_count].buffer  = NULL;
                    samples->bios_post_seek_up[vendor][track_count].samples = 0;
                }
                if (samples->bios_post_seek_down[vendor][track_count].buffer) {
                    free(samples->bios_post_seek_down[vendor][track_count].buffer);
                    samples->bios_post_seek_down[vendor][track_count].buffer  = NULL;
                    samples->bios_post_seek_down[vendor][track_count].samples = 0;
                }
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

    int boot_status = fdd_get_boot_status();
    int bios_vendor = fdd_audio_get_bios_vendor();
    int idx = track_diff - 1;
    int real_track_diff = to_track - from_track;
    audio_sample_t *sample_to_use = NULL;

    if (boot_status == BIOS_BOOT_POST) {
        if (bios_vendor == BIOS_VENDOR_AMI) {
            /* AMI BIOS POST mode: use AMI-specific samples if available */
            
            /* AMI BIOS quirk: for single-track seeks down (except 10->9), do not play audio */
            if (real_track_diff == -1 && (from_track != 10 || to_track != 9)) {
                fdd_log("FDD Audio Drive %d: AMI BIOS quirk: for single-track seeks down (except 10->9), do not play audio\n", drive);
                return;
            }

            /* For 10->9 seek, use the 1-track sample (which should be the 10-0 sound) */
            sample_to_use = is_seek_down ? &samples->bios_post_seek_down[bios_vendor][idx] : &samples->bios_post_seek_up[bios_vendor][idx];
            
            if (sample_to_use->buffer && sample_to_use->samples > 0) {
                fdd_log("FDD Audio Drive %d: Using AMI BIOS POST mode seek sample (idx=%d, %s)\n", 
                    drive, idx, is_seek_down ? "DOWN" : "UP");
            } else {
                /* Fall back to generic POST sample */
                sample_to_use = is_seek_down ? &samples->post_seek_down[idx] : &samples->post_seek_up[idx];
                if (sample_to_use->buffer && sample_to_use->samples > 0) {
                    fdd_log("FDD Audio Drive %d: AMI BIOS sample not available, using generic POST sample (idx=%d, %s)\n", 
                        drive, idx, is_seek_down ? "DOWN" : "UP");
                } else {
                    /* Fall back to normal sample */
                    fdd_log("FDD Audio Drive %d: POST sample not available, using normal sample\n", drive);
                    sample_to_use = is_seek_down ? &samples->seek_down[idx] : &samples->seek_up[idx];
                }
            }
        } else if (bios_vendor > 0 && bios_vendor < BIOS_VENDOR_COUNT) {
            /* Other known BIOS vendors: try vendor-specific samples first */
            sample_to_use = is_seek_down ? &samples->bios_post_seek_down[bios_vendor][idx] : &samples->bios_post_seek_up[bios_vendor][idx];
            
            if (sample_to_use->buffer && sample_to_use->samples > 0) {
                fdd_log("FDD Audio Drive %d: Using BIOS vendor %d POST mode seek sample (idx=%d, %s)\n", 
                    drive, bios_vendor, idx, is_seek_down ? "DOWN" : "UP");
            } else {
                /* Fall back to generic POST sample */
                sample_to_use = is_seek_down ? &samples->post_seek_down[idx] : &samples->post_seek_up[idx];
                if (sample_to_use->buffer && sample_to_use->samples > 0) {
                    fdd_log("FDD Audio Drive %d: BIOS-specific sample not available, using generic POST sample (idx=%d, %s)\n", 
                        drive, idx, is_seek_down ? "DOWN" : "UP");
                } else {
                    /* Fall back to normal sample */
                    fdd_log("FDD Audio Drive %d: POST sample not available, using normal sample\n", drive);
                    sample_to_use = is_seek_down ? &samples->seek_down[idx] : &samples->seek_up[idx];
                }
            }
        } else {
            /* Unknown BIOS vendor POST mode */
            sample_to_use = is_seek_down ? &samples->post_seek_down[idx] : &samples->post_seek_up[idx];
            if (!sample_to_use->buffer || sample_to_use->samples == 0) {
                fdd_log("FDD Audio Drive %d: POST sample not available, using normal sample\n", drive);
                sample_to_use = is_seek_down ? &samples->seek_down[idx] : &samples->seek_up[idx];
            } else {
                fdd_log("FDD Audio Drive %d: Using POST mode seek sample (idx=%d, %s)\n", 
                    drive, idx, is_seek_down ? "DOWN" : "UP");
            }
        }
    } else {
        /* Use normal samples */
        sample_to_use = is_seek_down ? &samples->seek_down[idx] : &samples->seek_up[idx];
    }

    /* Only proceed if we have the appropriate sample */
    if (!sample_to_use || !sample_to_use->buffer || sample_to_use->samples == 0)
        return;

    fdd_log("FDD Audio Drive %d: Multi-track seek %d -> %d (%d tracks, %s, POST=%d)\n",
            drive, from_track, to_track, track_diff, is_seek_down ? "DOWN" : "UP", boot_status);

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
    seek_state[drive][slot].sample_to_play   = sample_to_use;

    fdd_log("FDD Audio Drive %d: Started seek in slot %d, duration %d samples\n",
            drive, slot, sample_to_use->samples);
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

                    audio_sample_t *seek_sample = seek_state[drive][slot].sample_to_play;

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
                        seek_state[drive][slot].sample_to_play   = NULL;
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

                    audio_sample_t *seek_sample = seek_state[drive][slot].sample_to_play;

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
                        seek_state[drive][slot].sample_to_play   = NULL;
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
