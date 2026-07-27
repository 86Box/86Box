/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CLAP host wrapper implementation.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clap_host.h"

#ifdef _WIN32
#    include <windows.h>
#    include <shlobj.h>
#else
#    include <dlfcn.h>
#    include <dirent.h>
#    include <sys/stat.h>
#endif

#include <86box/86box.h>

/* ------------------------------------------------------------------ */
/*  Dynamic library helpers                                           */
/* ------------------------------------------------------------------ */
typedef void *dynlib_handle_t;

static dynlib_handle_t
dynlib_open(const char *path)
{
#ifdef _WIN32
    return (dynlib_handle_t)LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void *
dynlib_symbol(dynlib_handle_t h, const char *name)
{
#ifdef _WIN32
    return (void *)GetProcAddress((HMODULE)h, name);
#else
    return dlsym(h, name);
#endif
}

static void
dynlib_close(dynlib_handle_t h)
{
#ifdef _WIN32
    FreeLibrary((HMODULE)h);
#else
    dlclose(h);
#endif
}

/* ------------------------------------------------------------------ */
/*  CLAP host descriptor (presented to the plugin)                    */
/* ------------------------------------------------------------------ */
static const void *
host_get_extension(const clap_host_t *host, const char *ext)
{
    (void) host;
    (void) ext;
    return NULL;
}

static void host_noop(const clap_host_t *host) { (void) host; }

static const clap_host_t boxclap_host = {
    .clap_version    = CLAP_VERSION_INIT,
    .host_data       = NULL,
    .name            = "86Box",
    .vendor          = "The 86Box Team",
    .url             = "https://86box.net",
    .version         = "1.0",
    .get_extension   = host_get_extension,
    .request_restart = host_noop,
    .request_process = host_noop,
    .request_callback = host_noop
};

/* ------------------------------------------------------------------ */
/*  Instance structure                                                */
/* ------------------------------------------------------------------ */
struct clap_host_instance_t {
    dynlib_handle_t         lib;
    const clap_plugin_entry_t *entry;
    const clap_plugin_t       *plugin;

    /* process()-related state */
    clap_audio_buffer_t  audio_in;
    clap_audio_buffer_t  audio_out;
    clap_process_t       proc;
};

/* ------------------------------------------------------------------ */
/*  Validation helpers (match DOSBox's validate_note_ports etc.)      */
/* ------------------------------------------------------------------ */
static int
validate_note_ports(const clap_plugin_t *plugin)
{
    const clap_plugin_note_ports_t *np =
        (const clap_plugin_note_ports_t *)plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS);
    if (!np)
        return 0;

    if (np->count(plugin, 1 /* input */) != 1)
        return 0;
    if (np->count(plugin, 0 /* output */) != 0)
        return 0;

    clap_note_port_info_t info;
    memset(&info, 0, sizeof(info));
    np->get(plugin, 0, 1, &info);

    if (!(info.supported_dialects & CLAP_NOTE_DIALECT_MIDI))
        return 0;

    return 1;
}

static int
validate_audio_ports(const clap_plugin_t *plugin)
{
    const clap_plugin_audio_ports_t *ap =
        (const clap_plugin_audio_ports_t *)plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS);
    if (!ap)
        return 0;

    if (ap->count(plugin, 1 /* input */) != 0)
        return 0;
    if (ap->count(plugin, 0 /* output */) != 1)
        return 0;

    clap_audio_port_info_t info;
    memset(&info, 0, sizeof(info));
    ap->get(plugin, 0, 0, &info);

    if (info.channel_count != 2 || strcmp(info.port_type, CLAP_PORT_STEREO) != 0)
        return 0;

    return 1;
}

/* ------------------------------------------------------------------ */
/*  Directory scanning                                                */
/* ------------------------------------------------------------------ */
#ifdef _WIN32
static int
scan_dir_for_claps(const char *dir, clap_plugin_info_t **infos, int *count)
{
    WIN32_FIND_DATAA fd;
    char pattern[1024];
    HANDLE hf;
    int total = *count;

    snprintf(pattern, sizeof(pattern), "%s\\*.clap", dir);
    hf = FindFirstFileA(pattern, &fd);
    if (hf == INVALID_HANDLE_VALUE)
        return total;

    do {
        char path[1024];
        dynlib_handle_t lib;
        const clap_plugin_entry_t *entry;
        const clap_plugin_factory_t *factory;
        uint32_t i, n;

        snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);

        lib = dynlib_open(path);
        if (!lib)
            continue;

        entry = (const clap_plugin_entry_t *)dynlib_symbol(lib, "clap_entry");
        if (!entry) {
            dynlib_close(lib);
            continue;
        }

        entry->init(path);
        factory = (const clap_plugin_factory_t *)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
        if (!factory) {
            entry->deinit();
            dynlib_close(lib);
            continue;
        }

        n = factory->get_plugin_count(factory);
        for (i = 0; i < n; i++) {
            const clap_plugin_descriptor_t *desc = factory->get_plugin_descriptor(factory, i);
            if (!desc)
                continue;

            *infos = (clap_plugin_info_t *)realloc(*infos,
                        (size_t)(total + 1) * sizeof(clap_plugin_info_t));

            clap_plugin_info_t *pi = &(*infos)[total];
            memset(pi, 0, sizeof(*pi));
            memcpy(pi->library_path, path, sizeof(pi->library_path) - 1);
            pi->library_path[sizeof(pi->library_path) - 1] = '\0';
            if (desc->id)          strncpy(pi->id,          desc->id,          sizeof(pi->id) - 1);
            if (desc->name)        strncpy(pi->name,        desc->name,        sizeof(pi->name) - 1);
            if (desc->description) strncpy(pi->description, desc->description, sizeof(pi->description) - 1);
            if (desc->version)     strncpy(pi->version,     desc->version,     sizeof(pi->version) - 1);
            total++;
        }

        entry->deinit();
        dynlib_close(lib);
    } while (FindNextFileA(hf, &fd));

    FindClose(hf);
    *count = total;
    return total;
}
#else /* POSIX */
static int
has_clap_extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && strcmp(dot, ".clap") == 0;
}

static int
scan_dir_for_claps(const char *dir, clap_plugin_info_t **infos, int *count)
{
    DIR *d;
    struct dirent *ent;
    int total = *count;

    d = opendir(dir);
    if (!d)
        return total;

    while ((ent = readdir(d)) != NULL) {
        char path[1024];
        dynlib_handle_t lib;
        const clap_plugin_entry_t *entry;
        const clap_plugin_factory_t *factory;
        uint32_t i, n;

#if defined(__APPLE__)
        char path2[1024];

        snprintf(path2, sizeof(path2), "%s/%s/Contents/MacOS/Nuked-SC55", dir, ent->d_name);
        lib = dynlib_open(path2);

        if (!lib) {
            if (!has_clap_extension(ent->d_name))
                continue;

            snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
            lib = dynlib_open(path);
            if (!lib)
                continue;
        } else
            snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
#else
        if (!has_clap_extension(ent->d_name))
            continue;

        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        lib = dynlib_open(path);
        if (!lib)
            continue;
#endif

        entry = (const clap_plugin_entry_t *)dynlib_symbol(lib, "clap_entry");
        if (!entry) {
            dynlib_close(lib);
            continue;
        }

        entry->init(path);
        factory = (const clap_plugin_factory_t *)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
        if (!factory) {
            entry->deinit();
            dynlib_close(lib);
            continue;
        }

        n = factory->get_plugin_count(factory);
        for (i = 0; i < n; i++) {
            const clap_plugin_descriptor_t *desc = factory->get_plugin_descriptor(factory, i);
            if (!desc)
                continue;

            *infos = (clap_plugin_info_t *)realloc(*infos,
                        (size_t)(total + 1) * sizeof(clap_plugin_info_t));

            clap_plugin_info_t *pi = &(*infos)[total];
            memset(pi, 0, sizeof(*pi));
            memcpy(pi->library_path, path, sizeof(pi->library_path) - 1);
            pi->library_path[sizeof(pi->library_path) - 1] = '\0';
            if (desc->id)          strncpy(pi->id,          desc->id,          sizeof(pi->id) - 1);
            if (desc->name)        strncpy(pi->name,        desc->name,        sizeof(pi->name) - 1);
            if (desc->description) strncpy(pi->description, desc->description, sizeof(pi->description) - 1);
            if (desc->version)     strncpy(pi->version,     desc->version,     sizeof(pi->version) - 1);
            total++;
        }

        entry->deinit();
        dynlib_close(lib);
    }

    closedir(d);
    *count = total;
    return total;
}
#endif

/* ------------------------------------------------------------------ */
/*  Public API — enumerate                                            */
/* ------------------------------------------------------------------ */
int
clap_host_enumerate_plugins(const char *search_dir,
                            clap_plugin_info_t **out_infos,
                            int *out_count)
{
    *out_infos = NULL;
    *out_count = 0;

    if (search_dir && search_dir[0])
        scan_dir_for_claps(search_dir, out_infos, out_count);

    /* Also scan the CLAP standard paths */
#ifdef _WIN32
    {
        char common[MAX_PATH];
        char local[MAX_PATH];

        if (SHGetFolderPathA(NULL, CSIDL_PROGRAM_FILES_COMMON, NULL, 0, common) == S_OK) {
            char buf[1024];
            snprintf(buf, sizeof(buf), "%s\\CLAP", common);
            scan_dir_for_claps(buf, out_infos, out_count);
        }
        if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local) == S_OK) {
            char buf[1024];
            snprintf(buf, sizeof(buf), "%s\\Programs\\Common\\CLAP", local);
            scan_dir_for_claps(buf, out_infos, out_count);
        }
    }
#elif defined(__APPLE__)
    scan_dir_for_claps("/Library/Audio/Plug-Ins/CLAP", out_infos, out_count);
    {
        const char *home = getenv("HOME");
        if (home) {
            char buf[1024];
            snprintf(buf, sizeof(buf), "%s/Library/Audio/Plug-Ins/CLAP", home);
            scan_dir_for_claps(buf, out_infos, out_count);
        }
    }
#else /* Linux */
    {
        const char *home = getenv("HOME");
        if (home) {
            char buf[1024];
            snprintf(buf, sizeof(buf), "%s/.clap", home);
            scan_dir_for_claps(buf, out_infos, out_count);
        }
    }
    scan_dir_for_claps("/usr/lib/clap", out_infos, out_count);
#endif

    /* Scan CLAP_PATH environment variable */
    {
        const char *clap_path = getenv("CLAP_PATH");
        if (clap_path && clap_path[0]) {
            char *copy = strdup(clap_path);
            char *tok;
#ifdef _WIN32
            const char *delim = ";";
#else
            const char *delim = ":";
#endif
            for (tok = strtok(copy, delim); tok; tok = strtok(NULL, delim)) {
                scan_dir_for_claps(tok, out_infos, out_count);
            }
            free(copy);
        }
    }

    return *out_count;
}

/* ------------------------------------------------------------------ */
/*  Public API — load                                                 */
/* ------------------------------------------------------------------ */
clap_host_instance_t *
clap_host_load_plugin(const char *library_path, const char *plugin_id)
{
    clap_host_instance_t *inst;
    dynlib_handle_t lib;
    const clap_plugin_entry_t *entry;
    const clap_plugin_factory_t *factory;
    const clap_plugin_t *plugin;

#if defined(__APPLE__)
    char path[2048] = { 0 };
    strncpy(path, library_path, strlen(library_path));
    strcat(path, "/Contents/MacOS/Nuked-SC55");

    lib = dynlib_open(path);
    if (!lib) {
        pclog("CLAP: Failed to load '%s', falling back to '%s'\n", path, library_path);
        lib = dynlib_open(library_path);
        if (!lib) {
            pclog("CLAP: Failed to load '%s'\n", library_path);
            return NULL;
        }
    }
#else
    lib = dynlib_open(library_path);
    if (!lib) {
        pclog("CLAP: Failed to load '%s'\n", library_path);
        return NULL;
    }
#endif

    entry = (const clap_plugin_entry_t *)dynlib_symbol(lib, "clap_entry");
    if (!entry) {
        pclog("CLAP: No clap_entry in '%s'\n", library_path);
        dynlib_close(lib);
        return NULL;
    }

    entry->init(library_path);

    factory = (const clap_plugin_factory_t *)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    if (!factory) {
        pclog("CLAP: No plugin factory in '%s'\n", library_path);
        entry->deinit();
        dynlib_close(lib);
        return NULL;
    }

    plugin = factory->create_plugin(factory, &boxclap_host, plugin_id);
    if (!plugin) {
        pclog("CLAP: Failed to create plugin '%s'\n", plugin_id);
        entry->deinit();
        dynlib_close(lib);
        return NULL;
    }

    if (!plugin->init(plugin)) {
        pclog("CLAP: Plugin '%s' init failed\n", plugin_id);
        plugin->destroy(plugin);
        entry->deinit();
        dynlib_close(lib);
        return NULL;
    }

    if (!validate_note_ports(plugin)) {
        pclog("CLAP: Plugin '%s' has unsupported note port configuration\n", plugin_id);
        plugin->destroy(plugin);
        entry->deinit();
        dynlib_close(lib);
        return NULL;
    }

    if (!validate_audio_ports(plugin)) {
        pclog("CLAP: Plugin '%s' has unsupported audio port configuration\n", plugin_id);
        plugin->destroy(plugin);
        entry->deinit();
        dynlib_close(lib);
        return NULL;
    }

    inst = (clap_host_instance_t *)calloc(1, sizeof(*inst));
    inst->lib    = lib;
    inst->entry  = entry;
    inst->plugin = plugin;

    /* Set up audio buffer descriptors */
    inst->audio_in.channel_count  = 0;
    inst->audio_in.data32         = NULL;
    inst->audio_in.data64         = NULL;
    inst->audio_out.channel_count = 2;
    inst->audio_out.data32        = NULL;
    inst->audio_out.data64        = NULL;

    inst->proc.transport            = NULL;
    inst->proc.audio_inputs         = &inst->audio_in;
    inst->proc.audio_inputs_count   = 0;
    inst->proc.audio_outputs        = &inst->audio_out;
    inst->proc.audio_outputs_count  = 1;

    pclog("CLAP: Loaded plugin '%s' (version %s)\n",
          plugin->desc->name ? plugin->desc->name : plugin_id,
          plugin->desc->version ? plugin->desc->version : "?");

    return inst;
}

/* ------------------------------------------------------------------ */
/*  Public API — activate                                             */
/* ------------------------------------------------------------------ */
void
clap_host_activate(clap_host_instance_t *inst, int sample_rate_hz)
{
    if (!inst || !inst->plugin)
        return;
    inst->plugin->activate(inst->plugin, (double)sample_rate_hz, 1, 8192);
}

/* ------------------------------------------------------------------ */
/*  Public API — process                                              */
/* ------------------------------------------------------------------ */
void
clap_host_process(clap_host_instance_t *inst,
                  float *audio_left, float *audio_right,
                  int num_frames,
                  clap_event_list_t *event_list)
{
    float *out_ptrs[2];

    if (!inst || !inst->plugin)
        return;

    out_ptrs[0] = audio_left;
    out_ptrs[1] = audio_right;

    inst->audio_out.data32 = out_ptrs;

    inst->proc.frames_count = (uint32_t)num_frames;
    inst->proc.steady_time  = -1;
    inst->proc.in_events    = clap_event_list_get_input(event_list);
    inst->proc.out_events   = clap_event_list_get_output(event_list);

    inst->plugin->process(inst->plugin, &inst->proc);

    clap_event_list_clear(event_list);
}

/* ------------------------------------------------------------------ */
/*  Public API — destroy                                              */
/* ------------------------------------------------------------------ */
void
clap_host_destroy(clap_host_instance_t *inst)
{
    if (!inst)
        return;

    if (inst->plugin) {
        inst->plugin->reset(inst->plugin);
        inst->plugin->deactivate(inst->plugin);
        inst->plugin->destroy(inst->plugin);
    }

    if (inst->entry)
        inst->entry->deinit();

    if (inst->lib)
        dynlib_close(inst->lib);

    free(inst);
}
