/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the hard disk audio emulation.
 *
 * Authors: Toni Riikonen, <riikonen.toni@gmail.com>
 *
 *          Copyright 2026 Toni Riikonen.
 */
#ifndef EMU_HDD_AUDIO_H
#define EMU_HDD_AUDIO_H

#include <stdint.h>
#include <86box/hdd.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HDD_AUDIO_PROFILE_MAX 64

/* Spindle motor states */
typedef enum {
    HDD_SPINDLE_STOPPED = 0,
    HDD_SPINDLE_STARTING,
    HDD_SPINDLE_RUNNING,
    HDD_SPINDLE_STOPPING
} hdd_spindle_state_t;

/* Audio sample configuration structure */
typedef struct {
    char  filename[512];
    float volume;
} hdd_audio_sample_config_t;

/* HDD audio profile configuration */
typedef struct {
    int                       id;
    char                      name[128];
    char                      internal_name[64];
    uint32_t                  rpm;
    hdd_audio_sample_config_t spindlemotor_start;
    hdd_audio_sample_config_t spindlemotor_loop;
    hdd_audio_sample_config_t spindlemotor_stop;
    hdd_audio_sample_config_t seek_track;
} hdd_audio_profile_config_t;

/* Functions for profile management */
extern void                              hdd_audio_load_profiles(void);
extern int                               hdd_audio_get_profile_count(void);
extern const hdd_audio_profile_config_t *hdd_audio_get_profile(int id);
extern const char                       *hdd_audio_get_profile_name(int id);
extern const char                       *hdd_audio_get_profile_internal_name(int id);
extern uint32_t                          hdd_audio_get_profile_rpm(int id);
extern int                               hdd_audio_get_profile_by_internal_name(const char *internal_name);

/* HDD audio initialization and cleanup */
extern void hdd_audio_init(void);
extern void hdd_audio_reset(void);
extern void hdd_audio_close(void);
extern void hdd_audio_callback(int16_t *buffer, int length);
extern void hdd_audio_seek(hard_disk_t *hdd, uint32_t new_cylinder);

/* Per-drive spindle control */
extern void hdd_audio_spinup_drive(int hdd_index);
extern void hdd_audio_spindown_drive(int hdd_index);
extern hdd_spindle_state_t hdd_audio_get_drive_spindle_state(int hdd_index);

/* Legacy functions for backward compatibility - operate on all drives */
extern void hdd_audio_spinup(void);
extern void hdd_audio_spindown(void);
extern hdd_spindle_state_t hdd_audio_get_spindle_state(void);

#ifdef __cplusplus
}
#endif

#endif /* EMU_HDD_AUDIO_H */