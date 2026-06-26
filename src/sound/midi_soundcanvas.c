/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Roland Sound Canvas (SC-55) MIDI backend using CLAP plugins.
 *
 *          Ported from DOSBox Staging's Sound Canvas backend.
 *
 * Authors: The DOSBox Staging Team (original implementation)
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *          win2kgamer
 *
 *          Original Copyright 2024-2025 The DOSBox Staging Team.
 *          86Box adaptation 2026.
 *          Copyright 2026 Jasmine Iwanek.
 *          Copyright 2026 win2kgamer
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/midi.h>
#include <86box/thread.h>
#include <86box/sound.h>
#include <86box/plat_unused.h>
#include <86box/path.h>
#include <86box/rom.h>
#include <86box/log.h>

#include "clap/clap_host.h"
#include "clap/clap_event_list.h"

#define RENDER_RATE      100
#define BUFFER_SEGMENTS  10

extern void al_set_midi(int freq, int buf_size);

/* ------------------------------------------------------------------ */
/*  Model definitions (matching DOSBox Staging)                       */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *label;
    const char *version;
    int         sample_rate;
} sc_model_t;

static const sc_model_t sc_models[] = {
    { "SC-55 v1.00",      "1.00", 32000 },
    { "SC-55 v1.10",      "1.10", 32000 },
    { "SC-55 v1.20",      "1.20", 32000 },
    { "SC-55 v1.21",      "1.21", 32000 },
    { "SC-55 v2.00",      "2.00", 32000 },
    { "SC-55mk2 v1.01",   "1.01", 33103 },
};

#define NUM_MODELS (sizeof(sc_models) / sizeof(sc_models[0]))

/* ------------------------------------------------------------------ */
/*  State                                                             */
/* ------------------------------------------------------------------ */
typedef struct soundcanvas {
    clap_host_instance_t *clap_inst;
    clap_event_list_t     events;

    int  sample_rate;
    int  model_index;

    /* audio buffers */
    float    *buf_left;
    float    *buf_right;
    float    *out_buffer;        /* interleaved float stereo */
    int16_t  *out_buffer_int16;  /* interleaved int16 stereo */
    int       buf_size;          /* total buffer size in bytes */
    float     vol_ctrl;          /* Output gain */

    /* threading */
    thread_t *thread_h;
    event_t  *event;
    event_t  *start_event;
    mutex_t  *midi_mutex;
    int       on;

    /* logging */
    void *    log;
} soundcanvas_t;

static soundcanvas_t scdev;

/* ------------------------------------------------------------------ */
/*  Logging                                                           */
/* ------------------------------------------------------------------ */

#ifdef ENABLE_SCANVAS_LOG
int scanvas_do_log = ENABLE_SCANVAS_LOG;

static void
scanvas_log(void *priv, const char *fmt, ...)
{
    if (scanvas_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define scanvas_log(fmt, ...)
#endif

/* ------------------------------------------------------------------ */
/*  Case-insensitive substring search                                 */
/* ------------------------------------------------------------------ */
static int
str_icontains(const char *haystack, const char *needle)
{
    if (!haystack || !needle)
        return 0;

    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    if (nlen > hlen)
        return 0;

    for (size_t i = 0; i <= hlen - nlen; i++) {
        size_t j;
        for (j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j]))
                break;
        }
        if (j == nlen)
            return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Find best matching CLAP plugin for a model                        */
/* ------------------------------------------------------------------ */
static int
find_sc_plugin(UNUSED(const char *rom_path), int model_index,
               char *lib_path_out, size_t lib_sz,
               char *plugin_id_out, size_t id_sz)
{
    clap_plugin_info_t *infos = NULL;
    int  count = 0;
    int  found = -1;
    char temp[1024] = { 0 };
    #ifdef ENABLE_SCANVAS_LOG
    soundcanvas_t *data = &scdev;
    #endif

    clap_host_enumerate_plugins(NULL, &infos, &count); /* Check system CLAP directories */

    if (count == 0) { /* Check roms/plugins */
        for (rom_path_t *rom_path = &rom_paths; rom_path != NULL; rom_path = rom_path->next) {
            path_append_filename(temp, rom_path->path, "plugins/");
            scanvas_log(data->log, "Checking ROM path %s for CLAP plugins\n", temp);
            clap_host_enumerate_plugins(temp, &infos, &count);
            scanvas_log(data->log, "ROM path %s contains %i CLAP plugins\n", temp, count);
            if (count > 0)
                break;

        }
    }

    scanvas_log(data->log, "CLAP plugin count: %i\n", count);

    if (count == 0)
        return 0;

    for (int i = 0; i < count; i++) {
        /* Must contain "sc-55" or "SC-55" in the name */
        scanvas_log(data->log, "CLAP plugin %i name: %s, version: %s\n", i, infos[i].name, infos[i].version);
        if (!str_icontains(infos[i].name, "sc-55") &&
            !str_icontains(infos[i].name, "sc55"))
            continue;

        if (model_index >= 0 && model_index < (int)NUM_MODELS) {
            /* Match specific model by label substring and version */
            const sc_model_t *m = &sc_models[model_index];

            scanvas_log(data->log, "CLAP plugin check: label: %s, version: %s\n", m->label, m->version);

            int name_match = str_icontains(infos[i].name, "mk2")
                ? str_icontains(m->label, "mk2")
                : !str_icontains(m->label, "mk2");

            if (name_match && str_icontains(infos[i].name, m->version)) {
                found = i;
                break;
            }
        } else {
            /* Auto mode: pick first SC-55 plugin found */
            found = i;
            break;
        }
    }

    if (found >= 0) {
        memcpy(lib_path_out, infos[found].library_path, lib_sz - 1);
        lib_path_out[lib_sz - 1] = '\0';
        memcpy(plugin_id_out, infos[found].id, id_sz - 1);
        plugin_id_out[id_sz - 1] = '\0';
    }

    free(infos);
    return found >= 0;
}

/* ------------------------------------------------------------------ */
/*  Poll callback (called from sound system at SOUND_FREQ)            */
/* ------------------------------------------------------------------ */
void
soundcanvas_poll(void)
{
    soundcanvas_t *data = &scdev;

    thread_set_event(data->event);
}

/* ------------------------------------------------------------------ */
/*  Render thread                                                     */
/* ------------------------------------------------------------------ */
static void
soundcanvas_thread(void *param)
{
    soundcanvas_t *data      = (soundcanvas_t *)param;
    int            buf_pos   = 0;
    int            frames_per_seg;
    int            seg_bytes;

    if (sound_is_float) {
        seg_bytes      = data->buf_size / BUFFER_SEGMENTS;
        frames_per_seg = seg_bytes / (2 * (int)sizeof(float));
    } else {
        seg_bytes      = data->buf_size / BUFFER_SEGMENTS;
        frames_per_seg = seg_bytes / (2 * (int)sizeof(int16_t));
    }

    thread_set_event(data->start_event);

    while (data->on) {
        thread_wait_event(data->event, -1);
        thread_reset_event(data->event);

        /* Grab pending MIDI events under lock */
        thread_wait_mutex(data->midi_mutex);

        /* Render through CLAP plugin */
        memset(data->buf_left,  0, (size_t)frames_per_seg * sizeof(float));
        memset(data->buf_right, 0, (size_t)frames_per_seg * sizeof(float));

        clap_host_process(data->clap_inst,
                          data->buf_left, data->buf_right,
                          frames_per_seg, &data->events);

        thread_release_mutex(data->midi_mutex);

        /* Interleave into output buffer */
        if (sound_is_float) {
            float *buf = (float *)((uint8_t *)data->out_buffer + buf_pos);

            for (int i = 0; i < frames_per_seg; i++) {
                /* Apply sound card MIDI volume and filters */
                if (filter_midi != NULL) {
                    double dl = (double) (data->buf_left[i] * data->vol_ctrl);
                    double dr = (double) (data->buf_right[i] * data->vol_ctrl);
                    filter_midi(0, &dl, filter_midi_p);
                    filter_midi(1, &dr, filter_midi_p);
                    buf[i * 2 + 0] = (float) dl;
                    buf[i * 2 + 1] = (float) dr;
                } else {
                    buf[i * 2 + 0] = data->buf_left[i] * data->vol_ctrl;
                    buf[i * 2 + 1] = data->buf_right[i] * data->vol_ctrl;
                }
            }
            buf_pos += seg_bytes;
            if (buf_pos >= data->buf_size) {
                givealbuffer_midi(data->out_buffer,
                                  data->buf_size / (int)sizeof(float));
                buf_pos = 0;
            }
        } else {
            int16_t *buf = (int16_t *)((uint8_t *)data->out_buffer_int16 + buf_pos);

            for (int i = 0; i < frames_per_seg; i++) {
                float l = data->buf_left[i]  * 32767.0f * data->vol_ctrl;
                float r = data->buf_right[i] * 32767.0f * data->vol_ctrl;

                /* Apply sound card MIDI volume and filters */
                if (filter_midi != NULL) {
                    double dl = (double) l;
                    double dr = (double) r;
                    filter_midi(0, &dl, filter_midi_p);
                    filter_midi(1, &dr, filter_midi_p);
                    l = (float) l;
                    r = (float) r;
                }

                if (l >  32767.0f) l =  32767.0f;
                if (l < -32768.0f) l = -32768.0f;
                if (r >  32767.0f) r =  32767.0f;
                if (r < -32768.0f) r = -32768.0f;

                buf[i * 2 + 0] = (int16_t)l;
                buf[i * 2 + 1] = (int16_t)r;
            }
            buf_pos += seg_bytes;
            if (buf_pos >= data->buf_size) {
                givealbuffer_midi(data->out_buffer_int16,
                                  data->buf_size / (int)sizeof(int16_t));
                buf_pos = 0;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  MIDI message callbacks                                            */
/* ------------------------------------------------------------------ */
void
soundcanvas_msg(uint8_t *msg)
{
    soundcanvas_t *data = &scdev;
    uint32_t val  = *((uint32_t *)msg);
    uint8_t  midi[3];
    int      len;

    midi[0] = (uint8_t)(val & 0xFF);
    midi[1] = (uint8_t)((val >> 8) & 0xFF);
    midi[2] = (uint8_t)((val >> 16) & 0xFF);

    /* Determine message length from status byte */
    uint8_t status = midi[0] & 0xF0;
    switch (status) {
        case 0xC0: case 0xD0:
            len = 2; break;
        default:
            len = 3; break;
    }

    thread_wait_mutex(data->midi_mutex);
    clap_event_list_add_midi(&data->events, midi, len, 0);
    thread_release_mutex(data->midi_mutex);
}

void
soundcanvas_sysex(uint8_t *buf, unsigned int len)
{
    soundcanvas_t *data = &scdev;

    thread_wait_mutex(data->midi_mutex);
    clap_event_list_add_sysex(&data->events, buf, len, 0);
    thread_release_mutex(data->midi_mutex);
}

/* ------------------------------------------------------------------ */
/*  Init / Close                                                      */
/* ------------------------------------------------------------------ */
int
soundcanvas_available(void)
{
    /* Check if any SC-55 CLAP plugin is discoverable */
    clap_plugin_info_t *infos = NULL;
    int count = 0;
    char temp[1024] = { 0 };

    clap_host_enumerate_plugins(NULL, &infos, &count);

    int found = 0;
    for (int i = 0; i < count; i++) {
        if (str_icontains(infos[i].name, "sc-55") ||
            str_icontains(infos[i].name, "sc55")) {
            found = 1;
            break;
        }
    }

    if (found == 0) {
        for (rom_path_t *rom_path = &rom_paths; rom_path != NULL; rom_path = rom_path->next) {
            path_append_filename(temp, rom_path->path, "plugins/");

            clap_host_enumerate_plugins(temp, &infos, &count);

            for (int i = 0; i < count; i++) {
                if (str_icontains(infos[i].name, "sc-55") ||
                    str_icontains(infos[i].name, "sc55")) {
                    found = 1;
                    break;
                }
            }
        }
    }

    free(infos);
    return found;
}

static void *
soundcanvas_init(UNUSED(const device_t *info))
{
    soundcanvas_t *data = &scdev;
    midi_device_t *dev;
    char           lib_path[1024]  = { 0 };
    char           plugin_id[256]  = { 0 };

    memset(data, 0, sizeof(soundcanvas_t));

    data->log = log_open("SC55");

    /* Determine model from config */
    int model_cfg = device_get_config_int("model");
    int model_idx = (model_cfg > 0 && model_cfg <= (int)NUM_MODELS)
                  ? model_cfg - 1 : -1;

    /* Find matching CLAP plugin */
    if (!find_sc_plugin(NULL, model_idx, lib_path, sizeof(lib_path),
                        plugin_id, sizeof(plugin_id))) {
        scanvas_log(data->log, "Sound Canvas: No SC-55 CLAP plugin found.\n");
        return NULL;
    }

    /* Load plugin */
    data->clap_inst = clap_host_load_plugin(lib_path, plugin_id);
    if (!data->clap_inst) {
        scanvas_log(data->log, "Sound Canvas: Failed to load CLAP plugin.\n");
        return NULL;
    }

    /* Determine sample rate from model */
    if (model_idx >= 0)
        data->sample_rate = sc_models[model_idx].sample_rate;
    else
        data->sample_rate = 32000;  /* default SC-55 rate */

    data->model_index = model_idx;

    /* Set up volume control */
    data->vol_ctrl = (device_get_config_int("output_gain") / 100.0f);

    /* Activate plugin */
    clap_host_activate(data->clap_inst, data->sample_rate);

    /* Initialize event list */
    clap_event_list_init(&data->events);

    /* Allocate render buffers (separate L/R for CLAP) */
    int frames_per_seg = data->sample_rate / RENDER_RATE;
    data->buf_left  = (float *)calloc((size_t)frames_per_seg, sizeof(float));
    data->buf_right = (float *)calloc((size_t)frames_per_seg, sizeof(float));

    /* Allocate interleaved output buffer */
    if (sound_is_float) {
        data->buf_size         = frames_per_seg * 2 * (int)sizeof(float) * BUFFER_SEGMENTS;
        data->out_buffer       = (float *)calloc(1, (size_t)data->buf_size);
        data->out_buffer_int16 = NULL;
    } else {
        data->buf_size         = frames_per_seg * 2 * (int)sizeof(int16_t) * BUFFER_SEGMENTS;
        data->out_buffer       = NULL;
        data->out_buffer_int16 = (int16_t *)calloc(1, (size_t)data->buf_size);
    }

    al_set_midi(data->sample_rate, data->buf_size);

    /* Set up MIDI device */
    dev = (midi_device_t *)calloc(1, sizeof(midi_device_t));
    dev->play_msg   = soundcanvas_msg;
    dev->play_sysex = soundcanvas_sysex;
    dev->poll       = soundcanvas_poll;

    midi_out_init(dev);

    /* Start render thread */
    data->on          = 1;
    data->midi_mutex  = thread_create_mutex();
    data->start_event = thread_create_event();
    data->event       = thread_create_event();
    data->thread_h    = thread_create(soundcanvas_thread, data);

    thread_wait_event(data->start_event, -1);
    thread_reset_event(data->start_event);

    scanvas_log(data->log, "Sound Canvas: Initialized (model=%s, rate=%d Hz)\n",
          model_idx >= 0 ? sc_models[model_idx].label : "auto",
          data->sample_rate);

    return dev;
}

static void
soundcanvas_close(void *priv)
{
    if (!priv)
        return;

    soundcanvas_t *data = &scdev;

    /* Stop render thread */
    data->on = 0;
    thread_set_event(data->event);
    thread_wait(data->thread_h);

    /* Destroy CLAP instance */
    if (data->clap_inst) {
        clap_host_destroy(data->clap_inst);
        data->clap_inst = NULL;
    }

    clap_event_list_free(&data->events);

    thread_close_mutex(data->midi_mutex);

    free(data->buf_left);
    free(data->buf_right);
    free(data->out_buffer);
    free(data->out_buffer_int16);

    data->buf_left         = NULL;
    data->buf_right        = NULL;
    data->out_buffer       = NULL;
    data->out_buffer_int16 = NULL;

    if (data->log != NULL) {
        log_close(data->log);
        data->log = NULL;
    }
}

/* ------------------------------------------------------------------ */
/*  Device config                                                     */
/* ------------------------------------------------------------------ */
static const device_config_t soundcanvas_config[] = {
    // clang-format off
    {
        .name           = "model",
        .description    = "Model",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Auto-detect",     .value = 0 },
            { .description = "SC-55 v1.00",     .value = 1 },
            { .description = "SC-55 v1.10",     .value = 2 },
            { .description = "SC-55 v1.20",     .value = 3 },
            { .description = "SC-55 v1.21",     .value = 4 },
            { .description = "SC-55 v2.00",     .value = 5 },
            { .description = "SC-55mk2 v1.01",  .value = 6 },
            { .description = ""                            }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "output_gain",
        .description    = "Output Gain",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 100,
        .file_filter    = NULL,
        .spinner        = {
            .min =   0,
            .max = 100
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t soundcanvas_device = {
    .name          = "Roland Sound Canvas (CLAP)",
    .internal_name = "soundcanvas",
    .flags         = 0,
    .local         = 0,
    .init          = soundcanvas_init,
    .close         = soundcanvas_close,
    .reset         = NULL,
    .available     = soundcanvas_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = soundcanvas_config
};
