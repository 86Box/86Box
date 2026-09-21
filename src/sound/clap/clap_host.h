/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Minimal CLAP host wrapper — loads a CLAP plugin from a
 *          shared library, creates a plugin instance, and provides
 *          activate / process / destroy lifecycle.
 *
 *          Ported from DOSBox Staging (C++ → C).
 *
 * Authors: The DOSBox Staging Team (original C++ implementation)
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Original Copyright 2024-2025 The DOSBox Staging Team.
 *          86Box adaptation 2026.
 *          Copyright 2026 Jasmine Iwanek.
 */
#ifndef EMU_CLAP_HOST_H
#define EMU_CLAP_HOST_H

#include "clap_defs.h"
#include "clap_event_list.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle to a loaded CLAP library + plugin instance */
typedef struct clap_host_instance_t clap_host_instance_t;

/*
 * Plugin info returned during enumeration.
 */
typedef struct clap_plugin_info_t {
    char library_path[1024];
    char id[256];
    char name[256];
    char description[512];
    char version[64];
} clap_plugin_info_t;

/*
 * Scan a directory for .clap files, enumerate all plugins in each,
 * and return the list.
 *
 * The caller must free the returned array with free().
 * *out_count receives the number of entries.
 */
int clap_host_enumerate_plugins(const char *search_dir,
                                clap_plugin_info_t **out_infos,
                                int *out_count);

/*
 * Load a specific plugin by its path and ID.
 * Returns NULL on failure.
 */
clap_host_instance_t *clap_host_load_plugin(const char *library_path,
                                            const char *plugin_id);

/*
 * Activate the plugin at the given sample rate.
 * Must be called before the first process() call.
 */
void clap_host_activate(clap_host_instance_t *inst, int sample_rate_hz);

/*
 * Process audio: render num_frames of stereo float audio.
 * audio_left and audio_right must each be at least num_frames floats.
 * event_list contains accumulated MIDI events.
 * event_list is cleared after this call.
 */
void clap_host_process(clap_host_instance_t *inst,
                       float *audio_left, float *audio_right,
                       int num_frames,
                       clap_event_list_t *event_list);

/*
 * Destroy the plugin instance and unload the library.
 */
void clap_host_destroy(clap_host_instance_t *inst);

#ifdef __cplusplus
}
#endif

#endif /* EMU_CLAP_HOST_H */
