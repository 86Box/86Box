/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Minimal CLAP (CLever Audio Plugin) API type definitions
 *          required by the Sound Canvas backend.  Based on the MIT-licensed
 *          CLAP specification by Alexandre BIQUE et al.
 *
 *          Only the subset used by the 86Box CLAP host wrapper is
 *          reproduced here to avoid pulling in the full 60+ header tree.
 *
 * Authors: Alexandre BIQUE (original CLAP specification)
 *          The DOSBox Staging Team (C++ wrapper, ported to C here)
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Original CLAP spec: Copyright 2021 Alexandre BIQUE, MIT License.
 *          86Box adaptation: 2026.
 *          Copyright 2026 Jasmine Iwanek.
 */
#ifndef EMU_CLAP_DEFS_H
#define EMU_CLAP_DEFS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Version                                                           */
/* ------------------------------------------------------------------ */
typedef struct clap_version {
    uint32_t major;
    uint32_t minor;
    uint32_t revision;
} clap_version_t;

#define CLAP_VERSION_MAJOR 1
#define CLAP_VERSION_MINOR 2
#define CLAP_VERSION_REVISION 2
#define CLAP_VERSION_INIT { CLAP_VERSION_MAJOR, CLAP_VERSION_MINOR, CLAP_VERSION_REVISION }

/* ------------------------------------------------------------------ */
/*  Plugin entry (from the dynamic library)                           */
/* ------------------------------------------------------------------ */
typedef struct clap_plugin_entry {
    clap_version_t clap_version;
    int (*init)(const char *plugin_path);
    void (*deinit)(void);
    const void *(*get_factory)(const char *factory_id);
} clap_plugin_entry_t;

/* ------------------------------------------------------------------ */
/*  Plugin descriptor                                                 */
/* ------------------------------------------------------------------ */
typedef struct clap_plugin_descriptor {
    clap_version_t clap_version;
    const char    *id;
    const char    *name;
    const char    *vendor;
    const char    *url;
    const char    *manual_url;
    const char    *support_url;
    const char    *version;
    const char    *description;
    const char   **features;
} clap_plugin_descriptor_t;

/* ------------------------------------------------------------------ */
/*  Host descriptor                                                   */
/* ------------------------------------------------------------------ */
typedef struct clap_host {
    clap_version_t clap_version;
    void       *host_data;
    const char *name;
    const char *vendor;
    const char *url;
    const char *version;
    const void *(*get_extension)(const struct clap_host *host, const char *extension_id);
    void (*request_restart)(const struct clap_host *host);
    void (*request_process)(const struct clap_host *host);
    void (*request_callback)(const struct clap_host *host);
} clap_host_t;

/* ------------------------------------------------------------------ */
/*  Plugin factory                                                    */
/* ------------------------------------------------------------------ */
#define CLAP_PLUGIN_FACTORY_ID "clap.plugin-factory"

typedef struct clap_plugin_factory {
    uint32_t (*get_plugin_count)(const struct clap_plugin_factory *factory);
    const clap_plugin_descriptor_t *(*get_plugin_descriptor)(
        const struct clap_plugin_factory *factory, uint32_t index);
    const struct clap_plugin *(*create_plugin)(
        const struct clap_plugin_factory *factory,
        const clap_host_t *host,
        const char *plugin_id);
} clap_plugin_factory_t;

/* ------------------------------------------------------------------ */
/*  Events                                                            */
/* ------------------------------------------------------------------ */
typedef struct clap_event_header {
    uint32_t size;
    uint32_t time;
    uint16_t space_id;
    uint16_t type;
    uint32_t flags;
} clap_event_header_t;

#define CLAP_CORE_EVENT_SPACE_ID 0

enum {
    CLAP_EVENT_MIDI       = 10,
    CLAP_EVENT_MIDI_SYSEX = 11,
    CLAP_EVENT_MIDI2      = 12
};

typedef struct clap_event_midi {
    clap_event_header_t header;
    uint16_t port_index;
    uint8_t  data[3];
} clap_event_midi_t;

typedef struct clap_event_midi_sysex {
    clap_event_header_t header;
    uint16_t        port_index;
    const uint8_t  *buffer;
    uint32_t        size;
} clap_event_midi_sysex_t;

/* ------------------------------------------------------------------ */
/*  Event input / output lists                                        */
/* ------------------------------------------------------------------ */
typedef struct clap_input_events {
    void     *ctx;
    uint32_t (*size)(const struct clap_input_events *list);
    const clap_event_header_t *(*get)(const struct clap_input_events *list, uint32_t index);
} clap_input_events_t;

typedef struct clap_output_events {
    void *ctx;
    int  (*try_push)(const struct clap_output_events *list, const clap_event_header_t *event);
} clap_output_events_t;

/* ------------------------------------------------------------------ */
/*  Audio buffer                                                      */
/* ------------------------------------------------------------------ */
typedef struct clap_audio_buffer {
    float   **data32;
    double  **data64;
    uint32_t  channel_count;
    uint32_t  latency;
    uint64_t  constant_mask;
} clap_audio_buffer_t;

/* ------------------------------------------------------------------ */
/*  Process                                                           */
/* ------------------------------------------------------------------ */
typedef struct clap_event_transport clap_event_transport_t; /* opaque, unused */

typedef struct clap_process {
    int64_t  steady_time;
    uint32_t frames_count;
    const clap_event_transport_t *transport;
    const clap_audio_buffer_t    *audio_inputs;
    clap_audio_buffer_t          *audio_outputs;
    uint32_t audio_inputs_count;
    uint32_t audio_outputs_count;
    const clap_input_events_t    *in_events;
    const clap_output_events_t   *out_events;
} clap_process_t;

enum {
    CLAP_PROCESS_ERROR                 = 0,
    CLAP_PROCESS_CONTINUE              = 1,
    CLAP_PROCESS_CONTINUE_IF_NOT_QUIET = 2,
    CLAP_PROCESS_TAIL                  = 3,
    CLAP_PROCESS_SLEEP                 = 4
};

/* ------------------------------------------------------------------ */
/*  Plugin                                                            */
/* ------------------------------------------------------------------ */
typedef struct clap_plugin {
    const clap_plugin_descriptor_t *desc;
    void *plugin_data;
    int  (*init)(const struct clap_plugin *plugin);
    void (*destroy)(const struct clap_plugin *plugin);
    int  (*activate)(const struct clap_plugin *plugin,
                     double sample_rate,
                     uint32_t min_frames_count,
                     uint32_t max_frames_count);
    void (*deactivate)(const struct clap_plugin *plugin);
    int  (*start_processing)(const struct clap_plugin *plugin);
    void (*stop_processing)(const struct clap_plugin *plugin);
    void (*reset)(const struct clap_plugin *plugin);
    int32_t (*process)(const struct clap_plugin *plugin, const clap_process_t *process);
    const void *(*get_extension)(const struct clap_plugin *plugin, const char *id);
    void (*on_main_thread)(const struct clap_plugin *plugin);
} clap_plugin_t;

/* ------------------------------------------------------------------ */
/*  Note ports extension (for validation)                             */
/* ------------------------------------------------------------------ */
#define CLAP_EXT_NOTE_PORTS "clap.note-ports"
#define CLAP_NOTE_DIALECT_MIDI (1 << 1)

typedef struct clap_note_port_info {
    uint32_t    id;
    uint32_t    supported_dialects;
    uint32_t    preferred_dialect;
    char        name[256];
} clap_note_port_info_t;

typedef struct clap_plugin_note_ports {
    uint32_t (*count)(const clap_plugin_t *plugin, int is_input);
    int (*get)(const clap_plugin_t *plugin, uint32_t index,
               int is_input, clap_note_port_info_t *info);
} clap_plugin_note_ports_t;

/* ------------------------------------------------------------------ */
/*  Audio ports extension (for validation)                            */
/* ------------------------------------------------------------------ */
#define CLAP_EXT_AUDIO_PORTS "clap.audio-ports"
#define CLAP_PORT_STEREO     "stereo"

typedef struct clap_audio_port_info {
    uint32_t    id;
    char        name[256];
    uint32_t    flags;
    uint32_t    channel_count;
    const char *port_type;
    uint32_t    in_place_pair;
} clap_audio_port_info_t;

typedef struct clap_plugin_audio_ports {
    uint32_t (*count)(const clap_plugin_t *plugin, int is_input);
    int (*get)(const clap_plugin_t *plugin, uint32_t index,
               int is_input, clap_audio_port_info_t *info);
} clap_plugin_audio_ports_t;

#ifdef __cplusplus
}
#endif

#endif /* EMU_CLAP_DEFS_H */
