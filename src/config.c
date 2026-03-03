/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Configuration file handler.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          Overdoze,
 *          David Hrdlička, <hrdlickadavid@outlook.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2017-2019 Fred N. van Kempen.
 *          Copyright 2018-2019 David Hrdlička.
 *          Copyright 2021      Andreas J. Reichel.
 *          Copyright 2021-2025 Jasmine Iwanek.
 *
 * NOTE:    Forcing config files to be in Unicode encoding breaks
 *          it on Windows XP, and possibly also Vista. Use the
 *          -DANSI_CFG for use on these systems.
 */

#ifdef _WIN32
#    include <ws2tcpip.h>
#else
#    include <arpa/inet.h>
#endif
#include <inttypes.h>
#ifdef ENABLE_CONFIG_LOG
#include <stdarg.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/cassette.h>
#include <86box/cartridge.h>
#include <86box/nvr.h>
#include <86box/ini.h>
#include <86box/config.h>
#include <86box/isamem.h>
#include <86box/isarom.h>
#include <86box/isartc.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/hdd.h>
#include <86box/hdd_audio.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdd_audio.h>
#include <86box/fdc_ext.h>
#include <86box/gameport.h>
#include <86box/keyboard.h>
#include <86box/serial_passthrough.h>
#include <86box/machine.h>
#include <86box/mouse.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/cdrom_interface.h>
#include <86box/rdisk.h>
#include <86box/mo.h>
#include <86box/sound.h>
#include <86box/midi.h>
#include <86box/snd_mpu401.h>
#include <86box/video.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/plat_dir.h>
#include <86box/ui.h>
#include <86box/snd_opl.h>
#include <86box/version.h>

#ifndef USE_SDL_UI
/* Deliberate to not make the 86box.h header kitchen-sink. */
#include <86box/qt-glsl.h>
extern char gl3_shader_file[MAX_USER_SHADERS][512];
#endif

static int   cx;
static int   cy;
static int   cw;
static int   ch;
static ini_t config;
static ini_t global;

#ifdef ENABLE_CONFIG_LOG
int config_do_log = ENABLE_CONFIG_LOG;

static void
config_log(const char *fmt, ...)
{
    va_list ap;

    if (config_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define config_log(fmt, ...)
#endif

/* Load global configuration */
static void
load_global(void)
{
    ini_section_t cat = ini_find_section(global, "");
    char         *p;

    p = ini_section_get_string(cat, "language", NULL);
    if (p != NULL)
        lang_id = plat_language_code(p);
    else
        lang_id = plat_language_code(DEFAULT_LANGUAGE);

    open_dir_usr_path = ini_section_get_int(cat, "open_dir_usr_path", 0);

    confirm_reset = ini_section_get_int(cat, "confirm_reset", 1);
    confirm_exit  = ini_section_get_int(cat, "confirm_exit", 1);
    confirm_save  = ini_section_get_int(cat, "confirm_save", 1);
    color_scheme  = ini_section_get_int(cat, "color_scheme", 0);

    inhibit_multimedia_keys = ini_section_get_int(cat, "inhibit_multimedia_keys", 0);

    mouse_sensitivity = ini_section_get_double(cat, "mouse_sensitivity", 1.0);
    if (mouse_sensitivity < 0.1)
        mouse_sensitivity = 0.1;
    else if (mouse_sensitivity > 2.0)
        mouse_sensitivity = 2.0;

    vmm_disabled = ini_section_get_int(cat, "vmm_disabled", 0);

    p = ini_section_get_string(cat, "vmm_path", NULL);
    if (p != NULL) {
        /* Convert relative paths to absolute in portable mode */
        if (portable_mode && !path_abs(p)) {
            path_append_filename(vmm_path_cfg, exe_path, p);
            path_normalize(vmm_path_cfg);
        } else {
            strncpy(vmm_path_cfg, p, sizeof(vmm_path_cfg) - 1);
        }
    } else {
        plat_get_vmm_dir(vmm_path_cfg, sizeof(vmm_path_cfg));
    }
}

/* Load scan code mappings. */
static void
load_scan_code_mappings(void)
{
    ini_section_t cat = ini_find_section(config, "Scan code mappings");
    char          temp[512];

    for (int c = 0; c < 768; c++) {
        sprintf(temp, "%03X", c);

        int mapping = ini_section_get_hex12(cat, temp, c);

        if (mapping == c)
            ini_section_delete_var(cat, temp);
        else
            scancode_config_map[c] = mapping;
    }
}

/* Load "General" section. */
static void
load_general(void)
{
    ini_section_t cat = ini_find_section(config, "General");
    char          temp[512];
    char         *p;

    vid_resize = ini_section_get_int(cat, "vid_resize", 0);
    if (vid_resize & ~3)
        vid_resize &= 3;

    memset(temp, '\0', sizeof(temp));
    p       = ini_section_get_string(cat, "vid_renderer", "default");
    vid_api = plat_vidapi(p);
    if (!strcmp(p, "default"))
        ini_section_delete_var(cat, "vid_api");

    video_fullscreen_scale = ini_section_get_int(cat, "video_fullscreen_scale", 1);

    video_filter_method = ini_section_get_int(cat, "video_filter_method", 1);

    force_43 = !!ini_section_get_int(cat, "force_43", 0);
    scale    = ini_section_get_int(cat, "scale", 1);
    if (scale > 9)
        scale = 9;
    dpi_scale = ini_section_get_int(cat, "dpi_scale", 1);

    enable_overscan  = !!ini_section_get_int(cat, "enable_overscan", 0);
    vid_cga_contrast = !!ini_section_get_int(cat, "vid_cga_contrast", 0);
    video_grayscale  = ini_section_get_int(cat, "video_grayscale", 0);
    video_graytype   = ini_section_get_int(cat, "video_graytype", 0);

    force_10ms = !!ini_section_get_int(cat, "force_10ms", 0);

    rctrl_is_lalt = ini_section_get_int(cat, "rctrl_is_lalt", 0);
    update_icons  = ini_section_get_int(cat, "update_icons", 1);

    start_in_fullscreen |= ini_section_get_int(cat, "start_in_fullscreen", 0);

    window_remember = ini_section_get_int(cat, "window_remember", 0);

    if (!window_remember && !(vid_resize & 2))
        window_w = window_h = window_x = window_y = 0;

    if (vid_resize & 2) {
        p = ini_section_get_string(cat, "window_fixed_res", NULL);
        if (p == NULL)
            p = "120x120";
        sscanf(p, "%ix%i", &fixed_size_x, &fixed_size_y);
        if (fixed_size_x < 120)
            fixed_size_x = 120;
        if (fixed_size_x > 2048)
            fixed_size_x = 2048;
        if (fixed_size_y < 120)
            fixed_size_y = 120;
        if (fixed_size_y > 2048)
            fixed_size_y = 2048;
    } else {
        ini_section_delete_var(cat, "window_fixed_res");

        fixed_size_x = fixed_size_y = 120;
    }

    sound_gain = ini_section_get_int(cat, "sound_gain", 0);

    kbd_req_capture = ini_section_get_int(cat, "kbd_req_capture", 0);
    hide_status_bar = ini_section_get_int(cat, "hide_status_bar", 0);
    hide_tool_bar   = ini_section_get_int(cat, "hide_tool_bar", 0);
    sound_muted     = ini_section_get_int(cat, "sound_muted", 0);

    enable_discord = !!ini_section_get_int(cat, "enable_discord", 0);

    video_framerate = ini_section_get_int(cat, "video_gl_framerate", -1);
    video_vsync     = ini_section_get_int(cat, "video_gl_vsync", 0);

    video_gl_input_scale      = ini_section_get_double(cat, "video_gl_input_scale", 1.0);
    video_gl_input_scale_mode = ini_section_get_int(cat, "video_gl_input_scale_mode", FULLSCR_SCALE_FULL);

    window_remember = ini_section_get_int(cat, "window_remember", 0);
    if (window_remember) {
        p = ini_section_get_string(cat, "window_coordinates", NULL);
        if (p == NULL)
            p = "640, 480, 0, 0";
        sscanf(p, "%i, %i, %i, %i", &cw, &ch, &cx, &cy);
    } else {
        cw = ch = cx = cy = 0;
        ini_section_delete_var(cat, "window_coordinates");
    }

    do_auto_pause = ini_section_get_int(cat, "do_auto_pause", 0);
    force_constant_mouse = ini_section_get_int(cat, "force_constant_mouse", 0);
    fdd_sounds_enabled = ini_section_get_int(cat, "fdd_sounds_enabled", 1);

    p = ini_section_get_string(cat, "uuid", NULL);
    if (p != NULL)
        strncpy(uuid, p, sizeof(uuid) - 1);
    else
        strncpy(uuid, "", sizeof(uuid) - 1);
}

/* Load monitor section. */
static void
load_monitor(int monitor_index)
{
    ini_section_t       cat;
    char                name[512];
    char                temp[512];
    const char   *      p         = NULL;
    monitor_settings_t *ms        = &monitor_settings[monitor_index];

    sprintf(name, "Monitor #%i", monitor_index + 1);
    sprintf(temp, "%i, %i, %i, %i", cx, cy, cw, ch);

    cat = ini_find_section(config, name);

    p = ini_section_get_string(cat, "window_coordinates", temp);

    if (window_remember) {
        sscanf(p, "%i, %i, %i, %i", &ms->mon_window_x, &ms->mon_window_y,
               &ms->mon_window_w, &ms->mon_window_h);
        ms->mon_window_maximized = !!ini_section_get_int(cat, "window_maximized", 0);
    } else
        ms->mon_window_maximized = 0;
}

/* Load "Machine" section. */
static void
load_machine(void)
{
    ini_section_t cat = ini_find_section(config, "Machine");
    ini_section_t migration_cat;
    const char   *p;
    const char   *migrate_from = NULL;
    int           c;
    int           i;
    int           j;
    int           speed;
    double        multi;

    static const struct {
        const char *old;
        const char *new;
        const char *new_bios;
    } machine_migrations[] = {
        { .old = "tandy", .new = "tandy1000sx", .new_bios = NULL },
        { .old = "mr1217", .new = "325ax", .new_bios = "mr1217" },
        { .old = "deskpro386_05_1988", .new = "deskpro386", .new_bios = "deskpro386_05_1988" },
        { .old = "mr495", .new = "ami495", .new_bios = "mr495" },
        { .old = "403tg_d", .new = "403tg", .new_bios = "403tg_d" },
        { .old = "403tg_d_mr", .new = "403tg", .new_bios = "403tg_d_mr" },
        { .old = "aptiva510", .new = "pc330_6573", .new_bios = "aptiva510" },
        { .old = "ambradp60", .new = "batman", .new_bios = "ambradp60" },
        { .old = "dellxp60", .new = "batman", .new_bios = "dellxp60" },
        { .old = "586mc1", .new = "586is", .new_bios = NULL },
        { .old = "ambradp90", .new = "plato", .new_bios = "ambradp90" },
        { .old = "dellplato", .new = "plato", .new_bios = "dellplato" },
        { .old = "430nx", .new = "586ip", .new_bios = NULL },
        { .old = "p54tp4xe_mr", .new = "p54tp4xe", .new_bios = "p54tp4xe_mr" },
        { .old = "gw2katx", .new = "thor", .new_bios = "gw2katx" },
        { .old = "mrthor", .new = "thor", .new_bios = "mrthor" },
        { .old = "equium5200", .new = "cu430hx", .new_bios = "equium5200" },
        { .old = "infinia7200", .new = "tc430hx", .new_bios = "infinia7200" },
        { .old = "dellvenus", .new = "vs440fx", .new_bios = "dellvenus" },
        { .old = "gw2kvenus", .new = "vs440fx", .new_bios = "gw2kvenus" },
        { .old = "lgibmx7g", .new = "ms6119", .new_bios = "lgibmx7g" },
        { 0 }
    };

    p = ini_section_get_string(cat, "machine", NULL);
    if (p != NULL) {
        /* Migrate renamed machines. */
        for (i = 0; machine_migrations[i].old; i++) {
            if (!strcmp(p, machine_migrations[i].old)) {
                machine      = machine_get_machine_from_internal_name(machine_migrations[i].new);
                if (machine != -1) {
                    migrate_from = p;
                    if (machine_migrations[i].new_bios) {
                        migration_cat = ini_find_or_create_section(config, machine_get_device(machine)->name);
                        ini_section_set_string(migration_cat, "bios", machine_migrations[i].new_bios);
                    }
                }
                break;
            }
        }
        if (!migrate_from) {
            machine = machine_get_machine_from_internal_name(p);
            if (machine == -1)
                machine = 0;
        }
    } else {
        machine = 0;
    }

    if (machine >= machine_count())
        machine = machine_count() - 1;

    /* Copy NVR files when migrating a machine to a new NVR name. */
    if (migrate_from && strcmp(migrate_from, machine_get_nvr_name())) {
        char old_fn[256];
        c = snprintf(old_fn, sizeof(old_fn), "%s.", migrate_from);
        char new_fn[256];
        i = snprintf(new_fn, sizeof(new_fn), "%s.", machine_get_nvr_name());

        /* Iterate through NVR files. */
        DIR *dirp = opendir(nvr_path("."));
        if (dirp) {
            struct dirent *entry;
            while ((entry = readdir(dirp))) {
                /* Check if this file corresponds to the old name. */
                if (strncmp(entry->d_name, old_fn, c))
                    continue;

                /* Add extension to the new name. */
                strcpy(&new_fn[i], &entry->d_name[c]);

                /* Only copy if a file with the new name doesn't already exist. */
                FILE *g = nvr_fopen(new_fn, "rb");
                if (g == NULL) {
                    FILE *f = nvr_fopen(entry->d_name, "rb");
                    g       = nvr_fopen(new_fn, "wb");

                    uint8_t buf[4096];
                    while ((j = fread(buf, 1, sizeof(buf), f)))
                        fwrite(buf, 1, j, g);

                    fclose(f);
                }
                fclose(g);
            }
        }
    }

    cpu_override             = ini_section_get_int(cat, "cpu_override", 0);
    cpu_override_interpreter = ini_section_get_int(cat, "cpu_override_interpreter", 0);
    cpu_f                    = NULL;
    p                        = ini_section_get_string(cat, "cpu_family", NULL);
    if (p) {
        /* Migrate CPU family changes. */
        if (machines[machine].init == machine_at_deskpro386_init)
            cpu_f = cpu_get_family("i386dx_deskpro386");
        else
            cpu_f = cpu_get_family(p);

        if (cpu_f && !cpu_family_is_eligible(cpu_f, machine)) /* only honor eligible families */
            cpu_f = NULL;
    }

    if (cpu_f) {
        speed = ini_section_get_int(cat, "cpu_speed", 0);
        multi = ini_section_get_double(cat, "cpu_multi", 0);

        /* Find the configured CPU. */
        cpu = 0;
        c   = 0;
        i   = 256;
        while (cpu_f->cpus[cpu].cpu_type) {
            if (cpu_is_eligible(cpu_f, cpu, machine)) {
                /* Skip ineligible CPUs. */
                if ((cpu_f->cpus[cpu].rspeed == speed) && (cpu_f->cpus[cpu].multi == multi))
                    /* Exact speed/multiplier match. */
                    break;
                else if ((cpu_f->cpus[cpu].rspeed >= speed) && (i == 256))
                    /* Closest speed match. */
                    i = cpu;
                c = cpu; /* store fastest eligible CPU */
            }
            cpu++;
        }
        if (!cpu_f->cpus[cpu].cpu_type)
            /* if no exact match was found, use closest matching faster CPU or fastest eligible CPU. */
            cpu = MIN(i, c);
    } else {
        /* Default, find first eligible family. */
        c = 0;
        while (!cpu_family_is_eligible(&cpu_families[c], machine)) {
            if (cpu_families[c++].package == 0) {
                /* End of list. */
                fatal("Configuration: No eligible CPU families for the selected machine\n");
                return;
            }
        }
        cpu_f = (cpu_family_t *) &cpu_families[c];

        /* Find first eligible CPU in that family. */
        cpu = 0;
        while (!cpu_is_eligible(cpu_f, cpu, machine)) {
            if (cpu_f->cpus[cpu++].cpu_type == 0) {
                /* End of list. */
                cpu = 0;
                break;
            }
        }
    }
    cpu_s = (CPU *) &cpu_f->cpus[cpu];

    cpu_waitstates = ini_section_get_int(cat, "cpu_waitstates", 0);

    p        = ini_section_get_string(cat, "fpu_type", "none");
    fpu_type = fpu_get_type(cpu_f, cpu, p);

    mem_size = ini_section_get_int(cat, "mem_size", 64);

    if (mem_size > machine_get_max_ram(machine))
        mem_size = machine_get_max_ram(machine);

    cpu_use_dynarec = !!ini_section_get_int(cat, "cpu_use_dynarec", 0);
    fpu_softfloat = !!ini_section_get_int(cat, "fpu_softfloat", 0);
    if ((fpu_type != FPU_NONE) && machine_has_flags(machine, MACHINE_SOFTFLOAT_ONLY))
        fpu_softfloat = 1;

    p = ini_section_get_string(cat, "time_sync", NULL);
    if (p != NULL) {
        if (!strcmp(p, "disabled"))
            time_sync = TIME_SYNC_DISABLED;
        else if (!strcmp(p, "local"))
            time_sync = TIME_SYNC_ENABLED;
        else if (!strcmp(p, "utc") || !strcmp(p, "gmt"))
            time_sync = TIME_SYNC_ENABLED | TIME_SYNC_UTC;
        else
            time_sync = TIME_SYNC_ENABLED;
    } else
        time_sync = TIME_SYNC_ENABLED;

    pit_mode = ini_section_get_int(cat, "pit_mode", -1);
}

/* Load "Video" section. */
static void
load_video(void)
{
    ini_section_t cat = ini_find_section(config, "Video");
    char         *p;
    int           free_p = 0;

    if (machine_has_flags(machine, MACHINE_VIDEO_ONLY)) {
        ini_section_delete_var(cat, "gfxcard");
        gfxcard[0] = VID_INTERNAL;
    } else {
        p = ini_section_get_string(cat, "gfxcard", NULL);
        if (p == NULL) {
            if (machine_has_flags(machine, MACHINE_VIDEO)) {
                p = (char *) malloc((strlen("internal") + 1) * sizeof(char));
                strcpy(p, "internal");
            } else {
                p = (char *) malloc((strlen("none") + 1) * sizeof(char));
                strcpy(p, "none");
            }
            free_p = 1;
        } else if (!strcmp(p, "c&t_69000")) {
            p = (char *) malloc((strlen("chips_69000") + 1) * sizeof(char));
            strcpy(p, "chips_69000");
            free_p = 1;
        }
        gfxcard[0] = video_get_video_from_internal_name(p);
        if (free_p) {
            free(p);
            p = NULL;
        }
    }

    if (((gfxcard[0] == VID_INTERNAL) && machine_has_flags(machine, MACHINE_VIDEO_8514A)) ||
        video_card_get_flags(gfxcard[0]) == VIDEO_FLAG_TYPE_8514)
        ini_section_delete_var(cat, "8514a");

    voodoo_enabled                   = !!ini_section_get_int(cat, "voodoo", 0);
    ibm8514_standalone_enabled       = !!ini_section_get_int(cat, "8514a", 0);
    ibm8514_active                   = ibm8514_standalone_enabled;
    xga_standalone_enabled           = !!ini_section_get_int(cat, "xga", 0);
    xga_active                       = xga_standalone_enabled;
    da2_standalone_enabled           = !!ini_section_get_int(cat, "da2", 0);
    show_second_monitors             = !!ini_section_get_int(cat, "show_second_monitors", 1);
    video_fullscreen_scale_maximized = !!ini_section_get_int(cat, "video_fullscreen_scale_maximized", 0);

    vid_cga_comp_brightness = ini_section_get_int(cat, "vid_cga_comp_brightness", 0);
    vid_cga_comp_sharpness  = ini_section_get_int(cat, "vid_cga_comp_sharpness", 0);
    vid_cga_comp_contrast   = ini_section_get_int(cat, "vid_cga_comp_contrast", 100);
    vid_cga_comp_hue        = ini_section_get_int(cat, "vid_cga_comp_hue", 0);
    vid_cga_comp_saturation = ini_section_get_int(cat, "vid_cga_comp_saturation", 100);

    // TODO
    for (uint8_t i = 1; i < GFXCARD_MAX; i ++) {
        p = ini_section_get_string(cat, "gfxcard_2", NULL);
        if (!p)
            p = "none";
        gfxcard[i] = video_get_video_from_internal_name(p);
    }

    monitor_edid = ini_section_get_int(cat, "monitor_edid", 0);

    monitor_edid_path[0] = 0;
    strncpy(monitor_edid_path, ini_section_get_string(cat, "monitor_edid_path", (char*)""), sizeof(monitor_edid_path) - 1);
}

/* Load "Input Devices" section. */
static void
load_input_devices(void)
{
    ini_section_t cat = ini_find_section(config, "Input devices");
    char          temp[512];
    char          tmp2[32];
    char         *p;

    p = ini_section_get_string(cat, "keyboard_type", NULL);
    if (p != NULL)
        keyboard_type = keyboard_get_from_internal_name(p);
    else if (machines[machine].init == machine_xt_pc5086_init)
        keyboard_type = KEYBOARD_TYPE_PC_XT;
    else if (machine_has_bus(machine, MACHINE_BUS_PS2_PORTS)) {
        if (machine_has_flags(machine, MACHINE_KEYBOARD_JIS))
            keyboard_type = KEYBOARD_TYPE_PS55;
        else
            keyboard_type = KEYBOARD_TYPE_PS2;
    } else if (machine_has_bus(machine, MACHINE_BUS_ISA16) ||
               machine_has_bus(machine, MACHINE_BUS_PCI)) {
        if (machine_has_flags(machine, MACHINE_KEYBOARD_JIS))
            keyboard_type = KEYBOARD_TYPE_AX;
        else
            keyboard_type = KEYBOARD_TYPE_AT;
    } else
        keyboard_type = KEYBOARD_TYPE_PC_XT;

    p = ini_section_get_string(cat, "mouse_type", NULL);
    if (p != NULL)
        mouse_type = mouse_get_from_internal_name(p);
    else
        mouse_type = 0;

    uint8_t joy_insn = 0;
    p = ini_section_get_string(cat, "joystick_type", NULL);
    if (p != NULL) {
        joystick_type[joy_insn] = joystick_get_from_internal_name(p);

        if (!joystick_type[joy_insn]) {
            /* Try to read an integer for backwards compatibility with old configs */
            if (!strcmp(p, "0"))
                /* Workaround for ini_section_get_int returning 0 on non-integer data */
                joystick_type[joy_insn] = joystick_get_from_internal_name("2axis_2button");
            else {
                int js = ini_section_get_int(cat, "joystick_type", 8);
                switch (js) {
                    case JS_TYPE_2AXIS_4BUTTON:
                        joystick_type[joy_insn] = joystick_get_from_internal_name("2axis_4button");
                        break;
                    case JS_TYPE_2AXIS_6BUTTON:
                        joystick_type[joy_insn] = joystick_get_from_internal_name("2axis_6button");
                        break;
                    case JS_TYPE_2AXIS_8BUTTON:
                        joystick_type[joy_insn] = joystick_get_from_internal_name("2axis_8button");
                        break;
                    case JS_TYPE_4AXIS_4BUTTON:
                        joystick_type[joy_insn] = joystick_get_from_internal_name("4axis_4button");
                        break;
                    case JS_TYPE_CH_FLIGHTSTICK_PRO:
                        joystick_type[joy_insn] = joystick_get_from_internal_name("ch_flightstick_pro");
                        break;
                    case JS_TYPE_SIDEWINDER_PAD:
                        joystick_type[joy_insn] = joystick_get_from_internal_name("sidewinder_pad");
                        break;
                    case JS_TYPE_THRUSTMASTER_FCS:
                        joystick_type[joy_insn] = joystick_get_from_internal_name("thrustmaster_fcs");
                        break;
                    default:
                        joystick_type[joy_insn] = JS_TYPE_NONE;
                        break;
                }
            }
        }
    } else
        joystick_type[joy_insn] = JS_TYPE_NONE;

    uint8_t gp = 0;

    for (int js = 0; js < joystick_get_max_joysticks(joystick_type[joy_insn]); js++) {
        sprintf(temp, "joystick_%i_nr", js);
        joystick_state[gp][js].plat_joystick_nr = ini_section_get_int(cat, temp, 0);

        if (joystick_state[gp][js].plat_joystick_nr) {
            // --- Load Axis Mappings ---
            for (int axis_nr = 0; axis_nr < joystick_get_axis_count(joystick_type[joy_insn]); axis_nr++) {
                sprintf(temp, "joystick_%i_axis_%i", js, axis_nr);
                joystick_state[gp][js].axis_mapping[axis_nr] = ini_section_get_int(cat, temp, axis_nr);
            }

            // --- Load Button Mappings ---
            for (int button_nr = 0; button_nr < joystick_get_button_count(joystick_type[joy_insn]); button_nr++) {
                sprintf(temp, "joystick_%i_button_%i", js, button_nr);
                joystick_state[gp][js].button_mapping[button_nr] = ini_section_get_int(cat, temp, button_nr);
            }

            // --- Load POV (Hat Switch) Mappings ---
            for (int pov_nr = 0; pov_nr < joystick_get_pov_count(joystick_type[joy_insn]); pov_nr++) {
                sprintf(temp, "joystick_%i_pov_%i", js, pov_nr);
                sprintf(tmp2, "%i, %i", 0, 0);
                p                                             = ini_section_get_string(cat, temp, tmp2);
                joystick_state[gp][js].pov_mapping[pov_nr][0] = 0;
				joystick_state[gp][js].pov_mapping[pov_nr][1] = 0;
                sscanf(p, "%i, %i", &joystick_state[gp][js].pov_mapping[pov_nr][0],
                       &joystick_state[gp][js].pov_mapping[pov_nr][1]);
            }
        }
    }

    tablet_tool_type = !!ini_section_get_int(cat, "tablet_tool_type", 1);
}

/* Load "Sound" section. */
static void
load_sound(void)
{
    ini_section_t cat = ini_find_section(config, "Sound");
    char          temp[512];
    char         *p;

    p = ini_section_get_string(cat, "sndcard", NULL);
    if (p != NULL)
        sound_card_current[0] = sound_card_get_from_internal_name(p);
    else
        sound_card_current[0] = 0;

    p = ini_section_get_string(cat, "sndcard2", NULL);
    if (p != NULL)
        sound_card_current[1] = sound_card_get_from_internal_name(p);
    else
        sound_card_current[1] = 0;

    p = ini_section_get_string(cat, "sndcard3", NULL);
    if (p != NULL)
        sound_card_current[2] = sound_card_get_from_internal_name(p);
    else
        sound_card_current[2] = 0;

    p = ini_section_get_string(cat, "sndcard4", NULL);
    if (p != NULL)
        sound_card_current[3] = sound_card_get_from_internal_name(p);
    else
        sound_card_current[3] = 0;

    p = ini_section_get_string(cat, "midi_device", NULL);
    if (p != NULL)
        midi_output_device_current = midi_out_device_get_from_internal_name(p);
    else
        midi_output_device_current = 0;

    p = ini_section_get_string(cat, "midi_in_device", NULL);
    if (p != NULL)
        midi_input_device_current = midi_in_device_get_from_internal_name(p);
    else
        midi_input_device_current = 0;

    mpu401_standalone_enable = !!ini_section_get_int(cat, "mpu401_standalone", 0);

    /* Backwards compatibility for standalone SSI-2001, CMS and GUS from v3.11 and older. */
    const char *legacy_cards[][2] = {
        {"ssi2001",      "ssi2001"},
        { "gameblaster", "cms"    },
        { "gus",         "gus"    }
    };
    for (int i = 0, j = 0; i < (sizeof(legacy_cards) / sizeof(legacy_cards[0])); i++) {
        if (ini_section_get_int(cat, legacy_cards[i][0], 0) == 1) {
            /* Migrate to the first available sound card slot. */
            for (; j < (sizeof(sound_card_current) / sizeof(sound_card_current[0])); j++) {
                if (!sound_card_current[j]) {
                    sound_card_current[j] = sound_card_get_from_internal_name(legacy_cards[i][1]);
                    break;
                }
            }
        }
    }

    /* Correct Aztech codec selection in old configs so the OPTi 930 AD1848 type isn't selected */
    for (int i = 0; i < (sizeof(sound_card_current) / sizeof(sound_card_current[0])); i++) {
        sprintf(temp, "Aztech Sound Galaxy Pro 16 AB (Washington) #%i", i + 1);
        ini_section_t c = ini_find_section(config, temp);
        if (c != NULL) {
            if (ini_section_get_int(c, "codec", 1) == 2)
                ini_section_set_int(c, "codec", 3);
        }
    }
    for (int i = 0; i < (sizeof(sound_card_current) / sizeof(sound_card_current[0])); i++) {
        sprintf(temp, "Aztech Sound Galaxy Nova 16 Extra (Clinton) #%i", i + 1);
        ini_section_t c = ini_find_section(config, temp);
        if (c != NULL) {
            if (ini_section_get_int(c, "codec", 1) == 2)
                ini_section_set_int(c, "codec", 3);
        }
    }


    memset(temp, '\0', sizeof(temp));
    p = ini_section_get_string(cat, "sound_type", "float");
    if (strlen(p) > 511)
        fatal("Configuration: Length of sound_type is more than 511\n");
    else
        strncpy(temp, p, 511);
    if (!strcmp(temp, "float") || !strcmp(temp, "1"))
        sound_is_float = 1;
    else
        sound_is_float = 0;

    p = ini_section_get_string(cat, "fm_driver", "nuked");
    if (!strcmp(p, "ymfm")) {
        fm_driver = FM_DRV_YMFM;
    } else {
        fm_driver = FM_DRV_NUKED;
    }
}

/* Load "Network" section. */
static void
load_network(void)
{
    ini_section_t   cat       = ini_find_section(config, "Network");
    char         *  p;
    char            temp[512];
    uint16_t        c         = 0;
    uint16_t        min       = 0;
    netcard_conf_t *nc        = &net_cards_conf[c];

    /* Handle legacy configuration which supported only one NIC */
    p = ini_section_get_string(cat, "net_card", NULL);
    if (p != NULL) {
        nc->device_num = network_card_get_from_internal_name(p);

        p = ini_section_get_string(cat, "net_type", NULL);
        if (p != NULL) {
            if (!strcmp(p, "pcap") || !strcmp(p, "1"))
                nc->net_type = NET_TYPE_PCAP;
            else if (!strcmp(p, "slirp") || !strcmp(p, "2"))
                nc->net_type = NET_TYPE_SLIRP;
            else if (!strcmp(p, "vde") || !strcmp(p, "3"))
                nc->net_type = NET_TYPE_VDE;
            else if (!strcmp(p, "tap") || !strcmp(p, "4"))
                nc->net_type = NET_TYPE_TAP;
            else if (!strcmp(p, "nlswitch") || !strcmp(p, "nmswitch") || !strcmp(p, "5"))
                nc->net_type = NET_TYPE_NLSWITCH;
            else if (!strcmp(p, "nrswitch") || !strcmp(p, "6"))
                nc->net_type = NET_TYPE_NRSWITCH;
            else
                nc->net_type = NET_TYPE_NONE;
        } else
            nc->net_type = NET_TYPE_NONE;

        p = ini_section_get_string(cat, "net_host_device", NULL);
        if (p != NULL) {
            if (nc->net_type == NET_TYPE_PCAP) {
                if ((network_dev_to_id(p) == -1) || (network_ndev == 1)) {
                    if (network_ndev == 1)
                        ui_msgbox_header(MBX_ERROR, plat_get_string(STRING_PCAP_ERROR_NO_DEVICES), plat_get_string(STRING_PCAP_ERROR_DESC));
                    else if (network_dev_to_id(p) == -1)
                        ui_msgbox_header(MBX_ERROR, plat_get_string(STRING_PCAP_ERROR_INVALID_DEVICE), plat_get_string(STRING_PCAP_ERROR_DESC));
                    strcpy(nc->host_dev_name, "none");
                } else
                    strncpy(nc->host_dev_name, p, sizeof(nc->host_dev_name) - 1);
            } else
                strncpy(nc->host_dev_name, p, sizeof(nc->host_dev_name) - 1);
        } else
            strcpy(nc->host_dev_name, "none");

        min++;
    }

    ini_section_delete_var(cat, "net_card");
    ini_section_delete_var(cat, "net_type");
    ini_section_delete_var(cat, "net_host_device");

    for (c = min; c < NET_CARD_MAX; c++) {
        nc = &net_cards_conf[c];
        sprintf(temp, "net_%02i_card", c + 1);
        p = ini_section_get_string(cat, temp, NULL);
        if (p != NULL)
            nc->device_num = network_card_get_from_internal_name(p);
        else
            nc->device_num = 0;

        sprintf(temp, "net_%02i_net_type", c + 1);
        p = ini_section_get_string(cat, temp, NULL);
        if (p != NULL) {
            if (!strcmp(p, "pcap") || !strcmp(p, "1"))
                nc->net_type = NET_TYPE_PCAP;
            else if (!strcmp(p, "slirp") || !strcmp(p, "2"))
                nc->net_type = NET_TYPE_SLIRP;
            else if (!strcmp(p, "vde") || !strcmp(p, "3"))
                nc->net_type = NET_TYPE_VDE;
            else if (!strcmp(p, "tap") || !strcmp(p, "4"))
                nc->net_type = NET_TYPE_TAP;
            else if (!strcmp(p, "nlswitch") || !strcmp(p, "nmswitch") || !strcmp(p, "5"))
                nc->net_type = NET_TYPE_NLSWITCH;
            else if (!strcmp(p, "nrswitch") || !strcmp(p, "6"))
                nc->net_type = NET_TYPE_NRSWITCH;
            else
                nc->net_type = NET_TYPE_NONE;
        } else
            nc->net_type = NET_TYPE_NONE;
        sprintf(temp, "net_%02i_host_device", c + 1);
        p = ini_section_get_string(cat, temp, NULL);
        if (p != NULL) {
            if (nc->net_type == NET_TYPE_PCAP) {
                if ((network_dev_to_id(p) == -1) || (network_ndev == 1)) {
                    if (network_ndev == 1)
                        ui_msgbox_header(MBX_ERROR, plat_get_string(STRING_PCAP_ERROR_NO_DEVICES), plat_get_string(STRING_PCAP_ERROR_DESC));
                    else if (network_dev_to_id(p) == -1)
                        ui_msgbox_header(MBX_ERROR, plat_get_string(STRING_PCAP_ERROR_INVALID_DEVICE), plat_get_string(STRING_PCAP_ERROR_DESC));
                    strcpy(nc->host_dev_name, "none");
                } else
                    strncpy(nc->host_dev_name, p, sizeof(nc->host_dev_name) - 1);
            } else
                strncpy(nc->host_dev_name, p, sizeof(nc->host_dev_name) - 1);
        } else
            strcpy(nc->host_dev_name, "none");

        if (nc->net_type == NET_TYPE_SLIRP) {
            sprintf(temp, "net_%02i_addr", c + 1);
            p = ini_section_get_string(cat, temp, "");
            if (p && *p) {
                struct in_addr addr;
                if (inet_pton(AF_INET, p, &addr)) {
                    uint8_t *bytes = (uint8_t *)&addr.s_addr;
                    bytes[3] = 0;
                    sprintf(nc->slirp_net, "%d.%d.%d.0", bytes[0], bytes[1], bytes[2]);
                } else {
                    nc->slirp_net[0] = '\0';
                }
            } else {
                nc->slirp_net[0] = '\0';
            }
        } else {
            nc->slirp_net[0] = '\0';
        }

        sprintf(temp, "net_%02i_secret", c + 1);
        p = ini_section_get_string(cat, temp, NULL);
        strncpy(nc->secret, p ? p : "", sizeof(nc->secret) - 1);
        nc->secret[sizeof(net_cards_conf[c].secret) - 1] = '\0';

        sprintf(temp, "net_%02i_promisc", c + 1);
        nc->promisc_mode = ini_section_get_int(cat, temp, 0);

        sprintf(temp, "net_%02i_nrs_host", c + 1);
        p = ini_section_get_string(cat, temp, NULL);
        strncpy(nc->nrs_hostname, p ? p : "", sizeof(nc->nrs_hostname) - 1);

        sprintf(temp, "net_%02i_link", c + 1);
        nc->link_state = ini_section_get_int(cat, temp,
                                             (NET_LINK_10_HD | NET_LINK_10_FD |
                                              NET_LINK_100_HD | NET_LINK_100_FD |
                                              NET_LINK_1000_HD | NET_LINK_1000_FD));
    }
}

/* Load "Ports" section. */
static void
load_ports(void)
{
    ini_section_t cat = ini_find_section(config, "Ports (COM & LPT)");
    char         *p;
    char          temp[512];
    memset(temp, 0, sizeof(temp));

    int           has_jumpers = machine_has_jumpered_ecp_dma(machine, DMA_ANY);
    int           def_jumper  = machine_get_default_jumpered_ecp_dma(machine);

    jumpered_internal_ecp_dma = ini_section_get_int(cat, "jumpered_internal_ecp_dma", def_jumper);

    if (!has_jumpers || (jumpered_internal_ecp_dma == def_jumper))
        ini_section_delete_var(cat, "jumpered_internal_ecp_dma");
    else if (has_jumpers && !(machine_has_jumpered_ecp_dma(machine, jumpered_internal_ecp_dma))) {
        jumpered_internal_ecp_dma = def_jumper;
        ini_section_delete_var(cat, "jumpered_internal_ecp_dma");
    }

    for (int c = 0; c < (SERIAL_MAX - 1); c++) {
        sprintf(temp, "serial%d_enabled", c + 1);
        com_ports[c].enabled = !!ini_section_get_int(cat, temp, (c >= 2) ? 0 : 1);

        sprintf(temp, "serial%d_passthrough_enabled", c + 1);
        serial_passthrough_enabled[c] = !!ini_section_get_int(cat, temp, 0);

        if (serial_passthrough_enabled[c])
            config_log("Serial Port %d: passthrough enabled.\n\n", c + 1);
    }

    for (int c = 0; c < PARALLEL_MAX; c++) {
        sprintf(temp, "lpt%d_enabled", c + 1);
        lpt_ports[c].enabled = !!ini_section_get_int(cat, temp, (c == 0) ? 1 : 0);

        sprintf(temp, "lpt%d_device", c + 1);
        p                    = ini_section_get_string(cat, temp, "none");
        lpt_ports[c].device  = lpt_device_get_from_internal_name(p);
    }

#if 0
// TODO: Load
    for (c = 0; c < GAMEPORT_MAX; c++) {
        sprintf(temp, "gameport%d_enabled", c + 1);
        game_ports[c].enabled = !!ini_section_get_int(cat, temp, (c == 0) ? 1 : 0);

        sprintf(temp, "gameport%d_device", c + 1);
        p                   = ini_section_get_string(cat, temp, "none");
        game_ports[c].device = gameport_get_from_internal_name(p);
    }

    for (uint8_t c = 0; c < GAMEPORT_MAX; c++) {
        sprintf(temp, "gameport%d_type", c);

        p              = ini_section_get_string(cat, temp, "none");
        gameport_type[c] = gameport_get_from_internal_name(p);

        if (!strcmp(p, "none"))
            ini_section_delete_var(cat, temp);
    }
#endif
}

static char *
memrmem(char *src, char *start, char *what)
{
    if ((src == NULL) || (what == NULL))
        return NULL;

    while (1) {
        if (memcmp(src, what, strlen(what)) == 0)
            return src;
        src--;
        if (src < start)
            return NULL;
    }
}

static int
load_image_file(char *dest, char *p, uint8_t *ui_wp)
{
    char *prefix = "";
    int   ret    = 0;
    char *slash  = NULL;
    char *above  = NULL;
    char *above2 = NULL;
    char *use    = NULL;

    if ((slash = memrmem(usr_path + strlen(usr_path) - 2, usr_path, "/")) != NULL) {
        slash++;
        above = (char *) calloc(1, slash - usr_path + 1);
        memcpy(above, usr_path, slash - usr_path);

        if ((slash = memrmem(above + strlen(above) - 2, above, "/")) != NULL) {
            slash++;
            above2 = (char *) calloc(1, slash - above + 1);
            memcpy(above2, above, slash - above);
        }
    }

    if (strstr(p, "wp://") == p) {
        p += 5;
        prefix = "wp://";
        if (ui_wp != NULL)
            *ui_wp = 1;
    } else if ((ui_wp != NULL) && *ui_wp)
        prefix = "wp://";

    if (strstr(p, "ioctl://") == p) {
        if (strlen(p) > (MAX_IMAGE_PATH_LEN - 11))
            ret = 1;
        else
            snprintf(dest, MAX_IMAGE_PATH_LEN, "%s", p);

        if (above2 != NULL)
            free(above2);

        if (above != NULL)
            free(above);

        return ret;
    }

    if (memcmp(p, "<exe_path>/", strlen("<exe_path>/")) == 0) {
        if ((strlen(prefix) + strlen(exe_path) + strlen(path_get_slash(exe_path)) + strlen(p + strlen("<exe_path>/"))) >
            (MAX_IMAGE_PATH_LEN - 11))
            ret = 1;
        else
            snprintf(dest, MAX_IMAGE_PATH_LEN, "%s%s%s%s", prefix, exe_path, path_get_slash(exe_path),
                     p + strlen("<exe_path>/"));
    } else if (memcmp(p, "../../", strlen("../../")) == 0) {
        use = (above2 == NULL) ? usr_path : above2;
        if ((strlen(prefix) + strlen(use) + strlen(path_get_slash(use)) + strlen(p + strlen("../../"))) >
            (MAX_IMAGE_PATH_LEN - 11))
            ret = 1;
        else
            snprintf(dest, MAX_IMAGE_PATH_LEN, "%s%s%s%s", prefix, use, path_get_slash(use), p + strlen("../../"));
    } else if (memcmp(p, "../", strlen("../")) == 0) {
        use = (above == NULL) ? usr_path : above;
        if ((strlen(prefix) + strlen(use) + strlen(path_get_slash(use)) + strlen(p + strlen("../"))) >
            (MAX_IMAGE_PATH_LEN - 11))
            ret = 1;
        else
            snprintf(dest, MAX_IMAGE_PATH_LEN, "%s%s%s%s", prefix, use, path_get_slash(use), p + strlen("../"));
    } else if (path_abs(p)) {
        if ((strlen(prefix) + strlen(p)) > (MAX_IMAGE_PATH_LEN - 11))
            ret = 1;
        else
            snprintf(dest, MAX_IMAGE_PATH_LEN, "%s%s", prefix, p);
    } else {
        if ((strlen(prefix) + strlen(usr_path) + strlen(path_get_slash(usr_path)) + strlen(p)) > (MAX_IMAGE_PATH_LEN - 11))
            ret = 1;
        else
            snprintf(dest, MAX_IMAGE_PATH_LEN, "%s%s%s%s", prefix, usr_path, path_get_slash(usr_path), p);
    }

    path_normalize(dest);

    if (above2 != NULL)
        free(above2);

    if (above != NULL)
        free(above);

    return ret;
}

/* Load "Storage Controllers" section. */
static void
load_storage_controllers(void)
{
    ini_section_t cat = ini_find_section(config, "Storage controllers");
    ini_section_t migration_cat;
    char         *p;
    char          temp[512];
    int           min = 0;

    for (int c = min; c < SCSI_CARD_MAX; c++) {
        sprintf(temp, "scsicard_%d", c + 1);

        p = ini_section_get_string(cat, temp, NULL);
        if (p != NULL)
            scsi_card_current[c] = scsi_card_get_from_internal_name(p);
        else
            scsi_card_current[c] = 0;
    }

    p = ini_section_get_string(cat, "fdc", NULL);
#if 1
    if (p != NULL)
        fdc_current[0] = fdc_card_get_from_internal_name(p);
    else
        fdc_current[0] = FDC_INTERNAL;
#else
    int free_p = 0;

    if (p == NULL) {
        if (machine_has_flags(machine, MACHINE_FDC)) {
            p = (char *) malloc((strlen("internal") + 1) * sizeof(char));
            strcpy(p, "internal");
        } else {
            p = (char *) malloc((strlen("none") + 1) * sizeof(char));
            strcpy(p, "none");
        }
        free_p = 1;
    }

    fdc_current[0] = fdc_card_get_from_internal_name(p);

    if (free_p) {
        free(p);
        p = NULL;
        free_p = 0;
    }
#endif

    for (int c = min; c < HDC_MAX; c++) {
        sprintf(temp, "hdc_%d", c + 1);

        p = ini_section_get_string(cat, temp, NULL);
        if (p != NULL)
            hdc_current[c] = hdc_get_from_internal_name(p);
        else
            hdc_current[c] = 0;
    }

    /* Backwards compatibility for single HDC and standalone tertiary/quaternary IDE from v4.2 and older. */
    const char *legacy_cards[] = { NULL, "ide_ter", "ide_qua" };
    p = ini_section_get_string(cat, "hdc", NULL);
    for (int i = !(p || machine_has_flags(machine, MACHINE_HDC)), j = 0; i < (sizeof(legacy_cards) / sizeof(legacy_cards[0])); i++) {
        if (!legacy_cards[i] || (ini_section_get_int(cat, legacy_cards[i], 0) == 1)) {
            /* Migrate to the first available HDC slot. */
            for (; j < (sizeof(hdc_current) / sizeof(hdc_current[0])); j++) {
                if (!hdc_current[j]) {
                    if (!legacy_cards[i]) {
                        if (!p) {
                            hdc_current[j] = hdc_get_from_internal_name((j == 0) ? "internal" : "none");
                        } else if (!strcmp(p, "xtide_plus")) {
                            hdc_current[j] = hdc_get_from_internal_name("xtide");
                            sprintf(temp, "PC/XT XTIDE #%i", j + 1);
                            migration_cat = ini_find_or_create_section(config, temp);
                            ini_section_set_string(migration_cat, "bios", "xt_plus");
                        } else if (!strcmp(p, "xtide_at_386")) {
                            hdc_current[j] = hdc_get_from_internal_name("xtide_at");
                            sprintf(temp, "PC/AT XTIDE #%i", j + 1);
                            migration_cat = ini_find_or_create_section(config, temp);
                            ini_section_set_string(migration_cat, "bios", "at_386");
                        } else {
                            hdc_current[j] = hdc_get_from_internal_name(p);
                        }
                    } else {
                        hdc_current[j] = hdc_get_from_internal_name(legacy_cards[i]);
                    }
                    break;
                }
            }
        }
    }
    ini_section_delete_var(cat, "hdc");

    p = ini_section_get_string(cat, "cdrom_interface", NULL);
    if (p != NULL)
        cdrom_interface_current = cdrom_interface_get_from_internal_name(p);

    if (machine_has_bus(machine, MACHINE_BUS_CASSETTE))
        cassette_enable = !!ini_section_get_int(cat, "cassette_enabled", 0);
    else
        cassette_enable = 0;

    cassette_ui_writeprot = !!ini_section_get_int(cat, "cassette_writeprot", 0);
    ini_section_delete_var(cat, "cassette_writeprot");

    p = ini_section_get_string(cat, "cassette_file", "");

    if (!strcmp(p, usr_path))
        p[0] = 0x00;

    if (p[0] != 0x00) {
        if (load_image_file(cassette_fname, p, (uint8_t *) &cassette_ui_writeprot))
            fatal("Configuration: Length of cassette_file is more than 511\n");
    }

    p = ini_section_get_string(cat, "cassette_mode", "load");
    if (strlen(p) > 511)
        fatal("Configuration: Length of cassette_mode is more than 511\n");
    else
        strncpy(cassette_mode, p, 511);

    for (int i = 0; i < MAX_PREV_IMAGES; i++) {
        cassette_image_history[i] = (char *) calloc((MAX_IMAGE_PATH_LEN + 1) << 1, sizeof(char));
        sprintf(temp, "cassette_image_history_%02i", i + 1);
        p = ini_section_get_string(cat, temp, NULL);
        if (p) {
            if (load_image_file(cassette_image_history[i], p, NULL))
                fatal("Configuration: Length of cassette_image_history_%02i is more "
                      "than %i\n", i + 1, MAX_IMAGE_PATH_LEN - 1);
        }
    }
    cassette_pos          = ini_section_get_int(cat, "cassette_position", 0);
    if (!cassette_pos)
        ini_section_delete_var(cat, "cassette_position");
    cassette_srate        = ini_section_get_int(cat, "cassette_srate", 44100);
    if (cassette_srate == 44100)
        ini_section_delete_var(cat, "cassette_srate");
    cassette_append       = !!ini_section_get_int(cat, "cassette_append", 0);
    if (!cassette_append)
        ini_section_delete_var(cat, "cassette_append");
    cassette_pcm          = ini_section_get_int(cat, "cassette_pcm", 0);
    if (!cassette_pcm)
        ini_section_delete_var(cat, "cassette_pcm");

    if (!cassette_enable) {
        ini_section_delete_var(cat, "cassette_file");
        ini_section_delete_var(cat, "cassette_mode");
        for (int i = 0; i < MAX_PREV_IMAGES; i++) {
            sprintf(temp, "cassette_image_history_%02i", i + 1);
            ini_section_delete_var(cat, temp);
        }
        ini_section_delete_var(cat, "cassette_position");
        ini_section_delete_var(cat, "cassette_srate");
        ini_section_delete_var(cat, "cassette_append");
        ini_section_delete_var(cat, "cassette_pcm");
        ini_section_delete_var(cat, "cassette_ui_writeprot");
    }

    for (int c = 0; c < 2; c++) {
        sprintf(temp, "cartridge_%02i_fn", c + 1);
        p = ini_section_get_string(cat, temp, "");

        if (!strcmp(p, usr_path))
            p[0] = 0x00;

        if (p[0] != 0x00) {
            if (load_image_file(cart_fns[c], p, NULL))
                fatal("Configuration: Length of cartridge_%02i_fn is more than 511\n", c + 1);
        }

        for (int i = 0; i < MAX_PREV_IMAGES; i++) {
            cart_image_history[c][i] = (char *) calloc((MAX_IMAGE_PATH_LEN + 1) << 1, sizeof(char));
            sprintf(temp, "cartridge_%02i_image_history_%02i", c + 1, i + 1);
            p = ini_section_get_string(cat, temp, NULL);
            if (p) {
                if (load_image_file(cart_image_history[c][i], p, NULL))
                    fatal("Configuration: Length of cartridge_%02i_image_history_%02i "
                          "is more than %i\n", c + 1, i + 1, MAX_IMAGE_PATH_LEN - 1);
            }
        }
    }
}

/* Load "Hard Disks" section. */
static void
load_hard_disks(void)
{
    ini_section_t cat = ini_find_section(config, "Hard disks");
    char          temp[512];
    char          tmp2[512];
    char          s[512];
    char         *p;
    uint32_t      max_spt;
    uint32_t      max_hpc;
    uint32_t      max_tracks;
    uint32_t      board = 0;
    uint32_t      dev = 0;

    hdd_audio_load_profiles();

    memset(temp, '\0', sizeof(temp));
    for (uint8_t c = 0; c < HDD_NUM; c++) {
        sprintf(temp, "hdd_%02i_parameters", c + 1);
        p = ini_section_get_string(cat, temp, "0, 0, 0, 0, none");
        sscanf(p, "%u, %u, %u, %i, %s",
               &hdd[c].spt, &hdd[c].hpc, &hdd[c].tracks, (int *) &hdd[c].wp, s);

        hdd[c].bus_type = hdd_string_to_bus(s, 0);
        switch (hdd[c].bus_type) {
            default:
            case HDD_BUS_DISABLED:
                max_spt = max_hpc = max_tracks = 0;
                break;

            case HDD_BUS_MFM:
                max_spt    = 26; /* 26 for RLL */
                max_hpc    = 15;
                max_tracks = 2047;
                break;

            case HDD_BUS_XTA:
                max_spt    = 63;
                max_hpc    = 16;
                max_tracks = 1023;
                break;

            case HDD_BUS_ESDI:
                max_spt    = 99;
                max_hpc    = 16;
                max_tracks = 266305;
                break;

            case HDD_BUS_IDE:
                max_spt    = 255;
                max_hpc    = 255;
                max_tracks = 266305;
                break;

            case HDD_BUS_SCSI:
            case HDD_BUS_ATAPI:
                max_spt    = 255;
                max_hpc    = 255;
                max_tracks = 266305;
                break;
        }

        if (hdd[c].spt > max_spt)
            hdd[c].spt = max_spt;
        if (hdd[c].hpc > max_hpc)
            hdd[c].hpc = max_hpc;
        if (hdd[c].tracks > max_tracks)
            hdd[c].tracks = max_tracks;

        sprintf(temp, "hdd_%02i_speed", c + 1);
        switch (hdd[c].bus_type) {
            case HDD_BUS_IDE:
            case HDD_BUS_ESDI:
            case HDD_BUS_ATAPI:
            case HDD_BUS_SCSI:
                sprintf(tmp2, "1997_5400rpm");
                break;
            default:
                sprintf(tmp2, "ramdisk");
                break;
        }
        p                   = ini_section_get_string(cat, temp, tmp2);
        hdd[c].speed_preset = hdd_preset_get_from_internal_name(p);

        /* Audio Profile */
        sprintf(temp, "hdd_%02i_audio", c + 1);
        p = ini_section_get_string(cat, temp, "none");
        hdd[c].audio_profile = hdd_audio_get_profile_by_internal_name(p);

        /* MFM/RLL */
        sprintf(temp, "hdd_%02i_mfm_channel", c + 1);
        if (hdd[c].bus_type == HDD_BUS_MFM)
            hdd[c].mfm_channel = !!ini_section_get_int(cat, temp, c & 1);
        else
            ini_section_delete_var(cat, temp);

        /* XTA */
        sprintf(temp, "hdd_%02i_xta_channel", c + 1);
        if (hdd[c].bus_type == HDD_BUS_XTA)
            hdd[c].xta_channel = !!ini_section_get_int(cat, temp, c & 1);
        else
            ini_section_delete_var(cat, temp);

        /* ESDI */
        sprintf(temp, "hdd_%02i_esdi_channel", c + 1);
        if (hdd[c].bus_type == HDD_BUS_ESDI)
            hdd[c].esdi_channel = !!ini_section_get_int(cat, temp, c & 1);
        else
            ini_section_delete_var(cat, temp);

        /* IDE */
        sprintf(temp, "hdd_%02i_ide_channel", c + 1);
        if ((hdd[c].bus_type == HDD_BUS_IDE) || (hdd[c].bus_type == HDD_BUS_ATAPI)) {
            sprintf(tmp2, "%01u:%01u", c >> 1, c & 1);
            p = ini_section_get_string(cat, temp, tmp2);
            sscanf(p, "%01u:%01u", &board, &dev);
            board &= 3;
            dev &= 1;
            hdd[c].ide_channel = (board << 1) + dev;

            if (hdd[c].ide_channel > 7)
                hdd[c].ide_channel = 7;
        } else
            ini_section_delete_var(cat, temp);

        /* SCSI */
        if (hdd[c].bus_type == HDD_BUS_SCSI) {
            sprintf(temp, "hdd_%02i_scsi_location", c + 1);
            sprintf(tmp2, "%01u:%02u", SCSI_BUS_MAX, c + 2);
            p = ini_section_get_string(cat, temp, tmp2);
            sscanf(p, "%01u:%02u", &board, &dev);
            if (board >= SCSI_BUS_MAX) {
                /* Invalid bus - check legacy ID */
                sprintf(temp, "hdd_%02i_scsi_id", c + 1);
                hdd[c].scsi_id = ini_section_get_int(cat, temp, c + 2);

                if (hdd[c].scsi_id > 15)
                    hdd[c].scsi_id = 15;
            } else {
                board %= SCSI_BUS_MAX;
                dev &= 15;
                hdd[c].scsi_id = (board << 4) + dev;
            }
        } else {
            sprintf(temp, "hdd_%02i_scsi_location", c + 1);
            ini_section_delete_var(cat, temp);
        }

        sprintf(temp, "hdd_%02i_scsi_id", c + 1);
        ini_section_delete_var(cat, temp);

        memset(hdd[c].fn, 0x00, sizeof(hdd[c].fn));
        sprintf(temp, "hdd_%02i_fn", c + 1);
        p = ini_section_get_string(cat, temp, "");

        /*
         * NOTE:
         * When loading differencing VHDs, the absolute path is required.
         * So we should not convert absolute paths to relative. -sards
         */
        if (!strcmp(p, usr_path))
            p[0] = 0x00;

        if (p[0] != 0x00) {
            if (load_image_file(hdd[c].fn, p, NULL))
                fatal("Configuration: Length of hdd_%02i_fn is more than 511\n", c + 1);
        }

#if defined(ENABLE_CONFIG_LOG) && (ENABLE_CONFIG_LOG == 2)
        if (*p != '\0')
            config_log("HDD%d: %ls\n", c, hdd[c].fn);
#endif

        sprintf(temp, "hdd_%02i_vhd_blocksize", c + 1);
        hdd[c].vhd_blocksize = ini_section_get_int(cat, temp, 0);

        sprintf(temp, "hdd_%02i_vhd_parent", c + 1);
        p = ini_section_get_string(cat, temp, "");
        strncpy(hdd[c].vhd_parent, p, sizeof(hdd[c].vhd_parent) - 1);

        /* If disk is empty or invalid, mark it for deletion. */
        if (!hdd_is_valid(c)) {
            sprintf(temp, "hdd_%02i_parameters", c + 1);
            ini_section_delete_var(cat, temp);

            sprintf(temp, "hdd_%02i_mfm_channel", c + 1);
            ini_section_delete_var(cat, temp);

            sprintf(temp, "hdd_%02i_xta_channel", c + 1);
            ini_section_delete_var(cat, temp);

            sprintf(temp, "hdd_%02i_esdi_channel", c + 1);
            ini_section_delete_var(cat, temp);

            sprintf(temp, "hdd_%02i_ide_channel", c + 1);
            ini_section_delete_var(cat, temp);

            sprintf(temp, "hdd_%02i_scsi_location", c + 1);
            ini_section_delete_var(cat, temp);

            sprintf(temp, "hdd_%02i_fn", c + 1);
            ini_section_delete_var(cat, temp);
        }
    }
}

/* Load "Floppy and CD-ROM Drives" section. */
static void
load_floppy_and_cdrom_drives(void)
{
    ini_section_t cat = ini_find_section(config, "Floppy and CD-ROM drives");
    char          temp[512];
    char          tmp2[512];
    char         *p;
    char          s[512];
    unsigned int  board = 0;
    unsigned int  dev = 0;
    int           c;
    int           d;
    int           count = cdrom_get_type_count();

#ifndef DISABLE_FDD_AUDIO
    fdd_audio_load_profiles();
#endif

    memset(temp, 0x00, sizeof(temp));
    for (c = 0; c < FDD_NUM; c++) {
        sprintf(temp, "fdd_%02i_type", c + 1);

        p = ini_section_get_string(cat, temp, (c < 2) ? "525_2dd" : "none");
        if (!strcmp(p, "525_2hd_ps2"))
            d = fdd_get_from_internal_name("525_2hd");
        else if (!strcmp(p, "35_2hd_ps2"))
            d = fdd_get_from_internal_name("35_2hd");
        else
            d = fdd_get_from_internal_name(p);
        fdd_set_type(c, d);
        if (fdd_get_type(c) > 13)
            fdd_set_type(c, 13);

        sprintf(temp, "fdd_%02i_writeprot", c + 1);
        ui_writeprot[c] = !!ini_section_get_int(cat, temp, 0);
        if (ui_writeprot[c] == 0)
            ini_section_delete_var(cat, temp);

        sprintf(temp, "fdd_%02i_fn", c + 1);
        p = ini_section_get_string(cat, temp, "");

        if (!strcmp(p, usr_path))
            p[0] = 0x00;

        if (p[0] != 0x00) {
            if (load_image_file(floppyfns[c], p, (uint8_t *) &(ui_writeprot[c])))
                fatal("Configuration: Length of fdd_%02i_fn is more than 511\n", c + 1);
        }

#if defined(ENABLE_CONFIG_LOG) && (ENABLE_CONFIG_LOG == 2)
        if (*p != '\0')
            config_log("Floppy%d: %ls\n", c, floppyfns[c]);
#endif

        sprintf(temp, "fdd_%02i_turbo", c + 1);
        fdd_set_turbo(c, !!ini_section_get_int(cat, temp, 0));
        sprintf(temp, "fdd_%02i_check_bpb", c + 1);
        fdd_set_check_bpb(c, !!ini_section_get_int(cat, temp, 1));

        /* Check whether each value is default, if yes, delete it so that only
           non-default values will later be saved. */
        if (fdd_get_type(c) == ((c < 2) ? 2 : 0)) {
            sprintf(temp, "fdd_%02i_type", c + 1);
            ini_section_delete_var(cat, temp);
        }
        if (strlen(floppyfns[c]) == 0) {
            sprintf(temp, "fdd_%02i_fn", c + 1);
            ini_section_delete_var(cat, temp);
        }
        if (fdd_get_turbo(c) == 0) {
            sprintf(temp, "fdd_%02i_turbo", c + 1);
            ini_section_delete_var(cat, temp);
        }
        if (fdd_get_check_bpb(c) == 1) {
            sprintf(temp, "fdd_%02i_check_bpb", c + 1);
            ini_section_delete_var(cat, temp);
        }
        sprintf(temp, "fdd_%02i_audio", c + 1);
#ifndef DISABLE_FDD_AUDIO
        p = ini_section_get_string(cat, temp, "none");
        if (!strcmp(p, "panasonic"))
            d = fdd_audio_get_profile_by_internal_name("panasonic_ju4755_40t");
        else
            d = fdd_audio_get_profile_by_internal_name(p);
        fdd_set_audio_profile(c, d);
#else
        fdd_set_audio_profile(c, 0);
#endif

        for (int i = 0; i < MAX_PREV_IMAGES; i++) {
            fdd_image_history[c][i] = (char *) calloc((MAX_IMAGE_PATH_LEN + 1) << 1, sizeof(char));
            sprintf(temp, "fdd_%02i_image_history_%02i", c + 1, i + 1);
            p = ini_section_get_string(cat, temp, NULL);
            if (p) {
                if (load_image_file(fdd_image_history[c][i], p, NULL))
                    fatal("Configuration: Length of fdd_%02i_image_history_%02i is more "
                          "than %i\n", c + 1, i + 1, MAX_IMAGE_PATH_LEN - 1);
            }
        }
    }

    memset(temp, 0x00, sizeof(temp));
    for (c = 0; c < CDROM_NUM; c++) {
        sprintf(temp, "cdrom_%02i_host_drive", c + 1);
        ini_section_delete_var(cat, temp);

        sprintf(temp, "cdrom_%02i_parameters", c + 1);
        d = 0;
        p = ini_section_get_string(cat, temp, NULL);
        if (p != NULL)
            sscanf(p, "%01u, %s", &d, s);
        else if (c == 0)
            /* If this is the first drive, unmute the audio. */
            sscanf("1, none", "%01u, %s", &d, s);
        else
            sscanf("0, none", "%01u, %s", &d, s);
        cdrom[c].sound_on = d;
        cdrom[c].bus_type = hdd_string_to_bus(s, 1);

        sprintf(temp, "cdrom_%02i_speed", c + 1);
        cdrom[c].speed = ini_section_get_int(cat, temp, 8);

        sprintf(temp, "cdrom_%02i_no_check", c + 1);
        cdrom[c].no_check = ini_section_get_int(cat, temp, 0);

        sprintf(temp, "cdrom_%02i_type", c + 1);
        p = ini_section_get_string(cat, temp, cdrom[c].bus_type == CDROM_BUS_MKE ? "cr563" : "86cd");
        /* TODO: Configuration migration, remove when no longer needed. */
        int cdrom_type = cdrom_get_from_internal_name(!strcmp(p, "goldstar") ? "goldstar_r560b" : p);
        if (cdrom_type == -1) {
            cdrom_type = cdrom_get_from_name(p);
            if (cdrom_type == -1)
                cdrom_set_type(c, cdrom_get_from_internal_name("86cd"));
            else
                cdrom_set_type(c, cdrom_type);
        } else
            cdrom_set_type(c, cdrom_type);
        if (cdrom_get_type(c) >= count)
            cdrom_set_type(c, count - 1);
        if (!strcmp(p, "86cd"))
            ini_section_delete_var(cat, temp);

        /* Default values, needed for proper operation of the Settings dialog. */
        cdrom[c].mke_channel = cdrom[c].ide_channel = cdrom[c].scsi_device_id = c & 3;

        if (cdrom[c].bus_type == CDROM_BUS_MKE) {
            char *type = cdrom_get_internal_name(cdrom_get_type(c));

            if (strstr(type, "cr56") == NULL)
                cdrom_set_type(c, cdrom_get_from_internal_name("cr563_075"));

            sprintf(temp, "cdrom_%02i_mke_channel", c + 1);
            cdrom[c].mke_channel = ini_section_get_int(cat, temp, c & 3);

            if (cdrom[c].mke_channel > 3)
                cdrom[c].mke_channel = 3;

        } else if (cdrom[c].bus_type == CDROM_BUS_ATAPI) {
            sprintf(temp, "cdrom_%02i_ide_channel", c + 1);
            sprintf(tmp2, "%01u:%01u", (c & 3) >> 1, (c & 3) & 1);
            p = ini_section_get_string(cat, temp, tmp2);
            sscanf(p, "%01u:%01u", &board, &dev);
            board &= 3;
            dev &= 1;
            cdrom[c].ide_channel = (board << 1) + dev;

            if (cdrom[c].ide_channel > 7)
                cdrom[c].ide_channel = 7;
        } else if (cdrom[c].bus_type == CDROM_BUS_SCSI) {
            sprintf(temp, "cdrom_%02i_scsi_location", c + 1);
            sprintf(tmp2, "%01u:%02u", SCSI_BUS_MAX, c & 3);
            p = ini_section_get_string(cat, temp, tmp2);
            sscanf(p, "%01u:%02u", &board, &dev);
            if (board >= SCSI_BUS_MAX) {
                /* Invalid bus - check legacy ID */
                sprintf(temp, "cdrom_%02i_scsi_id", c + 1);
                cdrom[c].scsi_device_id = ini_section_get_int(cat, temp, c & 3);

                if (cdrom[c].scsi_device_id > 15)
                    cdrom[c].scsi_device_id = 15;
            } else {
                board %= SCSI_BUS_MAX;
                dev &= 15;
                cdrom[c].scsi_device_id = (board << 4) + dev;
            }
        }

        if (cdrom[c].bus_type != CDROM_BUS_MKE) {
            sprintf(temp, "cdrom_%02i_mke_channel", c + 1);
            ini_section_delete_var(cat, temp);
        }

        if (cdrom[c].bus_type != CDROM_BUS_ATAPI) {
            sprintf(temp, "cdrom_%02i_ide_channel", c + 1);
            ini_section_delete_var(cat, temp);
        }

        if (cdrom[c].bus_type != CDROM_BUS_SCSI) {
            sprintf(temp, "cdrom_%02i_scsi_location", c + 1);
            ini_section_delete_var(cat, temp);
        }

        sprintf(temp, "cdrom_%02i_scsi_id", c + 1);
        ini_section_delete_var(cat, temp);

        sprintf(temp, "cdrom_%02i_image_path", c + 1);
        p = ini_section_get_string(cat, temp, "");

        if (!strcmp(p, usr_path))
            p[0] = 0x00;

        if (p[0] != 0x00) {
            if (load_image_file(cdrom[c].image_path, p, NULL))
                fatal("Configuration: Length of cdrom_%02i_image_path is more than 511\n", c + 1);
        }

#if defined(ENABLE_CONFIG_LOG) && (ENABLE_CONFIG_LOG == 2)
        if (*p != '\0')
            config_log("CD-ROM%d: %ls\n", c, cdrom[c].image_path);
#endif

        for (int i = 0; i < MAX_PREV_IMAGES; i++) {
            cdrom[c].image_history[i] = (char *) calloc((MAX_IMAGE_PATH_LEN + 1) << 1, sizeof(char));
            sprintf(temp, "cdrom_%02i_image_history_%02i", c + 1, i + 1);
            p = ini_section_get_string(cat, temp, NULL);
            if (p) {
                if (load_image_file(cdrom[c].image_history[i], p, NULL))
                    fatal("Configuration: Length of cdrom_%02i_image_history_%02i is more "
                          "than %i\n", c + 1, i + 1, MAX_IMAGE_PATH_LEN - 1);
            }
        }

        /* If the CD-ROM is disabled, delete all its variables. */
        if (cdrom[c].bus_type == CDROM_BUS_DISABLED) {
            sprintf(temp, "cdrom_%02i_parameters", c + 1);
            ini_section_delete_var(cat, temp);

            sprintf(temp, "cdrom_%02i_speed", c + 1);
            ini_section_delete_var(cat, temp);

            sprintf(temp, "cdrom_%02i_type", c + 1);
            ini_section_delete_var(cat, temp);

            sprintf(temp, "cdrom_%02i_ide_channel", c + 1);
            ini_section_delete_var(cat, temp);

            sprintf(temp, "cdrom_%02i_scsi_location", c + 1);
            ini_section_delete_var(cat, temp);

            sprintf(temp, "cdrom_%02i_image_path", c + 1);
            ini_section_delete_var(cat, temp);

            for (int i = 0; i < MAX_PREV_IMAGES; i++) {
                sprintf(temp, "cdrom_%02i_image_history_%02i", c + 1, i + 1);
                ini_section_delete_var(cat, temp);
            }
        }

        sprintf(temp, "cdrom_%02i_iso_path", c + 1);
        ini_section_delete_var(cat, temp);
    }
}

/* Load "Other Removable Devices" section. */
static void
load_other_removable_devices(void)
{
    ini_section_t cat = ini_find_section(config, "Other removable devices");
    char          temp[512];
    char          tmp2[512];
    char         *p;
    char          s[512];
    unsigned int  board = 0;
    unsigned int  dev = 0;
    int           c;
    int           legacy_zip_drives = 0;

    memset(temp, 0x00, sizeof(temp));
    for (c = 0; c < RDISK_NUM; c++) {
        sprintf(temp, "zip_%02i_parameters", c + 1);
        p = ini_section_get_string(cat, temp, NULL);
        if (p != NULL) {
            sscanf(p, "%01u, %s", &rdisk_drives[c].type, s);
            legacy_zip_drives++;
        } else
            sscanf("0, none", "%01u, %s", &rdisk_drives[c].type, s);
        rdisk_drives[c].type++;
        rdisk_drives[c].bus_type = hdd_string_to_bus(s, 1);

        /* Default values, needed for proper operation of the Settings dialog. */
        rdisk_drives[c].ide_channel = rdisk_drives[c].scsi_device_id = c + 2;

        if (rdisk_drives[c].bus_type == RDISK_BUS_ATAPI) {
            sprintf(temp, "zip_%02i_ide_channel", c + 1);
            sprintf(tmp2, "%01u:%01u", (c + 2) >> 1, (c + 2) & 1);
            p = ini_section_get_string(cat, temp, tmp2);
            sscanf(p, "%01u:%01u", &board, &dev);
            board &= 3;
            dev &= 1;
            rdisk_drives[c].ide_channel = (board << 1) + dev;

            if (rdisk_drives[c].ide_channel > 7)
                rdisk_drives[c].ide_channel = 7;
        } else if (rdisk_drives[c].bus_type == RDISK_BUS_SCSI) {
            sprintf(temp, "zip_%02i_scsi_location", c + 1);
            sprintf(tmp2, "%01u:%02u", SCSI_BUS_MAX, c + 2);
            p = ini_section_get_string(cat, temp, tmp2);
            sscanf(p, "%01u:%02u", &board, &dev);
            if (board >= SCSI_BUS_MAX) {
                /* Invalid bus - check legacy ID */
                sprintf(temp, "zip_%02i_scsi_id", c + 1);
                rdisk_drives[c].scsi_device_id = ini_section_get_int(cat, temp, c + 2);

                if (rdisk_drives[c].scsi_device_id > 15)
                    rdisk_drives[c].scsi_device_id = 15;
            } else {
                board %= SCSI_BUS_MAX;
                dev &= 15;
                rdisk_drives[c].scsi_device_id = (board << 4) + dev;
            }
        }

        if (rdisk_drives[c].bus_type != RDISK_BUS_ATAPI) {
            sprintf(temp, "zip_%02i_ide_channel", c + 1);
            ini_section_delete_var(cat, temp);
        }

        if (rdisk_drives[c].bus_type != RDISK_BUS_SCSI) {
            sprintf(temp, "zip_%02i_scsi_location", c + 1);
            ini_section_delete_var(cat, temp);
        }

        sprintf(temp, "zip_%02i_scsi_id", c + 1);
        ini_section_delete_var(cat, temp);

        sprintf(temp, "zip_%02i_image_path", c + 1);
        p = ini_section_get_string(cat, temp, "");

        sprintf(temp, "zip_%02i_writeprot", c + 1);
        rdisk_drives[c].read_only =  ini_section_get_int(cat, temp, 0);
        ini_section_delete_var(cat, temp);

        if (!strcmp(p, usr_path))
            p[0] = 0x00;

        if (p[0] != 0x00) {
            if (load_image_file(rdisk_drives[c].image_path, p, &(rdisk_drives[c].read_only)))
                fatal("Configuration: Length of zip_%02i_image_path is more than 511\n", c + 1);
        }

        for (int i = 0; i < MAX_PREV_IMAGES; i++) {
            rdisk_drives[c].image_history[i] = (char *) calloc((MAX_IMAGE_PATH_LEN + 1) << 1, sizeof(char));
            sprintf(temp, "zip_%02i_image_history_%02i", c + 1, i + 1);
            p = ini_section_get_string(cat, temp, NULL);
            if (p) {
                if (load_image_file(rdisk_drives[c].image_history[i], p, NULL))
                    fatal("Configuration: Length of zip_%02i_image_history_%02i is more than %i\n",
                          c + 1, i + 1, MAX_IMAGE_PATH_LEN - 1);
            }
        }

        sprintf(temp, "zip_%02i_parameters", c + 1);
        ini_section_delete_var(cat, temp);

        sprintf(temp, "zip_%02i_ide_channel", c + 1);
        ini_section_delete_var(cat, temp);

        sprintf(temp, "zip_%02i_scsi_location", c + 1);
        ini_section_delete_var(cat, temp);

        sprintf(temp, "zip_%02i_image_path", c + 1);
        ini_section_delete_var(cat, temp);

        for (int i = 0; i < MAX_PREV_IMAGES; i++) {
            sprintf(temp, "zip_%02i_image_history_%02i", c + 1, i + 1);
            ini_section_delete_var(cat, temp);
        }
    }

    if (legacy_zip_drives > 0)
        goto go_to_mo;

    memset(temp, 0x00, sizeof(temp));
    for (c = 0; c < RDISK_NUM; c++) {
        sprintf(temp, "rdisk_%02i_parameters", c + 1);
        p = ini_section_get_string(cat, temp, NULL);
        if (p != NULL) {
            sscanf(p, "%01u, %s", &rdisk_drives[c].type, s);
            legacy_zip_drives++;
        } else
            sscanf("0, none", "%01u, %s", &rdisk_drives[c].type, s);
        rdisk_drives[c].bus_type = hdd_string_to_bus(s, 1);

        /* Default values, needed for proper operation of the Settings dialog. */
        rdisk_drives[c].ide_channel = rdisk_drives[c].scsi_device_id = c + 2;

        if (rdisk_drives[c].bus_type == RDISK_BUS_ATAPI) {
            sprintf(temp, "rdisk_%02i_ide_channel", c + 1);
            sprintf(tmp2, "%01u:%01u", (c + 2) >> 1, (c + 2) & 1);
            p = ini_section_get_string(cat, temp, tmp2);
            sscanf(p, "%01u:%01u", &board, &dev);
            board &= 3;
            dev &= 1;
            rdisk_drives[c].ide_channel = (board << 1) + dev;

            if (rdisk_drives[c].ide_channel > 7)
                rdisk_drives[c].ide_channel = 7;
        } else if (rdisk_drives[c].bus_type == RDISK_BUS_SCSI) {
            sprintf(temp, "rdisk_%02i_scsi_location", c + 1);
            sprintf(tmp2, "%01u:%02u", SCSI_BUS_MAX, c + 2);
            p = ini_section_get_string(cat, temp, tmp2);
            sscanf(p, "%01u:%02u", &board, &dev);
            if (board >= SCSI_BUS_MAX) {
                /* Invalid bus - check legacy ID */
                sprintf(temp, "rdisk_%02i_scsi_id", c + 1);
                rdisk_drives[c].scsi_device_id = ini_section_get_int(cat, temp, c + 2);

                if (rdisk_drives[c].scsi_device_id > 15)
                    rdisk_drives[c].scsi_device_id = 15;
            } else {
                board %= SCSI_BUS_MAX;
                dev &= 15;
                rdisk_drives[c].scsi_device_id = (board << 4) + dev;
            }
        }

        if (rdisk_drives[c].bus_type != RDISK_BUS_ATAPI) {
            sprintf(temp, "rdisk_%02i_ide_channel", c + 1);
            ini_section_delete_var(cat, temp);
        }

        if (rdisk_drives[c].bus_type != RDISK_BUS_SCSI) {
            sprintf(temp, "rdisk_%02i_scsi_location", c + 1);
            ini_section_delete_var(cat, temp);
        }

        sprintf(temp, "rdisk_%02i_scsi_id", c + 1);
        ini_section_delete_var(cat, temp);

        sprintf(temp, "rdisk_%02i_image_path", c + 1);
        p = ini_section_get_string(cat, temp, "");

        sprintf(temp, "rdisk_%02i_writeprot", c + 1);
        rdisk_drives[c].read_only =  ini_section_get_int(cat, temp, 0);
        ini_section_delete_var(cat, temp);

        if (!strcmp(p, usr_path))
            p[0] = 0x00;

        if (p[0] != 0x00) {
            if (load_image_file(rdisk_drives[c].image_path, p, &(rdisk_drives[c].read_only)))
                fatal("Configuration: Length of rdisk_%02i_image_path is more than 511\n", c + 1);
        }

        for (int i = 0; i < MAX_PREV_IMAGES; i++) {
            rdisk_drives[c].image_history[i] = (char *) calloc((MAX_IMAGE_PATH_LEN + 1) << 1, sizeof(char));
            sprintf(temp, "rdisk_%02i_image_history_%02i", c + 1, i + 1);
            p = ini_section_get_string(cat, temp, NULL);
            if (p) {
                if (load_image_file(rdisk_drives[c].image_history[i], p, NULL))
                    fatal("Configuration: Length of rdisk_%02i_image_history_%02i is more than %i\n",
                          c + 1, i + 1, MAX_IMAGE_PATH_LEN - 1);
            }
        }

        /* If the removable disk drive is disabled, delete all its variables. */
        if (rdisk_drives[c].bus_type == RDISK_BUS_DISABLED) {
            sprintf(temp, "rdisk_%02i_parameters", c + 1);
            ini_section_delete_var(cat, temp);

            sprintf(temp, "rdisk_%02i_ide_channel", c + 1);
            ini_section_delete_var(cat, temp);

            sprintf(temp, "rdisk_%02i_scsi_location", c + 1);
            ini_section_delete_var(cat, temp);

            sprintf(temp, "rdisk_%02i_image_path", c + 1);
            ini_section_delete_var(cat, temp);

            for (int i = 0; i < MAX_PREV_IMAGES; i++) {
                sprintf(temp, "rdisk_%02i_image_history_%02i", c + 1, i + 1);
                ini_section_delete_var(cat, temp);
            }
        }
    }

go_to_mo:
    memset(temp, 0x00, sizeof(temp));
    for (c = 0; c < MO_NUM; c++) {
        sprintf(temp, "mo_%02i_parameters", c + 1);
        p = ini_section_get_string(cat, temp, NULL);
        if (p != NULL)
            sscanf(p, "%u, %s", &mo_drives[c].type, s);
        else
            sscanf("00, none", "%u, %s", &mo_drives[c].type, s);
        mo_drives[c].bus_type = hdd_string_to_bus(s, 1);

        /* Default values, needed for proper operation of the Settings dialog. */
        mo_drives[c].ide_channel = mo_drives[c].scsi_device_id = c + 2;

        if (mo_drives[c].bus_type == MO_BUS_ATAPI) {
            sprintf(temp, "mo_%02i_ide_channel", c + 1);
            sprintf(tmp2, "%01u:%01u", (c + 2) >> 1, (c + 2) & 1);
            p = ini_section_get_string(cat, temp, tmp2);
            sscanf(p, "%01u:%01u", &board, &dev);
            board &= 3;
            dev &= 1;
            mo_drives[c].ide_channel = (board << 1) + dev;

            if (mo_drives[c].ide_channel > 7)
                mo_drives[c].ide_channel = 7;
        } else if (mo_drives[c].bus_type == MO_BUS_SCSI) {
            sprintf(temp, "mo_%02i_scsi_location", c + 1);
            sprintf(tmp2, "%01u:%02u", SCSI_BUS_MAX, c + 2);
            p = ini_section_get_string(cat, temp, tmp2);
            sscanf(p, "%01u:%02u", &board, &dev);
            if (board >= SCSI_BUS_MAX) {
                /* Invalid bus - check legacy ID */
                sprintf(temp, "mo_%02i_scsi_id", c + 1);
                mo_drives[c].scsi_device_id = ini_section_get_int(cat, temp, c + 2);

                if (mo_drives[c].scsi_device_id > 15)
                    mo_drives[c].scsi_device_id = 15;
            } else {
                board %= SCSI_BUS_MAX;
                dev &= 15;
                mo_drives[c].scsi_device_id = (board << 4) + dev;
            }
        }

        if (mo_drives[c].bus_type != MO_BUS_ATAPI) {
            sprintf(temp, "mo_%02i_ide_channel", c + 1);
            ini_section_delete_var(cat, temp);
        }

        if (mo_drives[c].bus_type != MO_BUS_SCSI) {
            sprintf(temp, "mo_%02i_scsi_location", c + 1);
            ini_section_delete_var(cat, temp);
        }

        sprintf(temp, "mo_%02i_scsi_id", c + 1);
        ini_section_delete_var(cat, temp);

        sprintf(temp, "mo_%02i_image_path", c + 1);
        p = ini_section_get_string(cat, temp, "");

        sprintf(temp, "mo_%02i_writeprot", c + 1);
        mo_drives[c].read_only = ini_section_get_int(cat, temp, 0);
        ini_section_delete_var(cat, temp);

        if (!strcmp(p, usr_path))
            p[0] = 0x00;

        if (p[0] != 0x00) {
            if (load_image_file(mo_drives[c].image_path, p, &(mo_drives[c].read_only)))
                fatal("Configuration: Length of mo_%02i_image_path is more than 511\n", c + 1);
        }

        for (int i = 0; i < MAX_PREV_IMAGES; i++) {
            mo_drives[c].image_history[i] = (char *) calloc((MAX_IMAGE_PATH_LEN + 1) << 1, sizeof(char));
            sprintf(temp, "mo_%02i_image_history_%02i", c + 1, i + 1);
            p = ini_section_get_string(cat, temp, NULL);
            if (p) {
                if (load_image_file(mo_drives[c].image_history[i], p, NULL))
                    fatal("Configuration: Length of mo_%02i_image_history_%02i is more than %i\n",
                          c + 1, i + 1, MAX_IMAGE_PATH_LEN - 1);
            }
        }

        /* If the MO drive is disabled, delete all its variables. */
        if (mo_drives[c].bus_type == MO_BUS_DISABLED) {
            sprintf(temp, "mo_%02i_parameters", c + 1);
            ini_section_delete_var(cat, temp);

            sprintf(temp, "mo_%02i_ide_channel", c + 1);
            ini_section_delete_var(cat, temp);

            sprintf(temp, "mo_%02i_scsi_location", c + 1);
            ini_section_delete_var(cat, temp);

            sprintf(temp, "mo_%02i_image_path", c + 1);
            ini_section_delete_var(cat, temp);

            for (int i = 0; i < MAX_PREV_IMAGES; i++) {
                sprintf(temp, "mo_%02i_image_history_%02i", c + 1, i + 1);
                ini_section_delete_var(cat, temp);
            }
        }
    }
}

/* Load "Other Peripherals" section. */
static void
load_other_peripherals(void)
{
    ini_section_t cat = ini_find_section(config, "Other peripherals");
    char         *p;
    char          temp[512];

    bugger_enabled         = !!ini_section_get_int(cat, "bugger_enabled", 0);
    postcard_enabled       = !!ini_section_get_int(cat, "postcard_enabled", 0);
    unittester_enabled     = !!ini_section_get_int(cat, "unittester_enabled", 0);
    novell_keycard_enabled = !!ini_section_get_int(cat, "novell_keycard_enabled", 0);

    if (!bugger_enabled)
        ini_section_delete_var(cat, "bugger_enabled");

    if (!postcard_enabled)
        ini_section_delete_var(cat, "postcard_enabled");

    if (!unittester_enabled)
        ini_section_delete_var(cat, "unittester_enabled");

    if (!novell_keycard_enabled)
        ini_section_delete_var(cat, "novell_keycard_enabled");

    // ISA RAM Boards
    for (uint8_t c = 0; c < ISAMEM_MAX; c++) {
        sprintf(temp, "isamem%d_type", c);

        p              = ini_section_get_string(cat, temp, "none");
        isamem_type[c] = isamem_get_from_internal_name(p);

        if (!strcmp(p, "none"))
            ini_section_delete_var(cat, temp);
    }

    // ISA ROM Boards
    for (uint8_t c = 0; c < ISAROM_MAX; c++) {
        sprintf(temp, "isarom%d_type", c);

        p              = ini_section_get_string(cat, temp, "none");
        isarom_type[c] = isarom_get_from_internal_name(p);

        if (!strcmp(p, "none"))
            ini_section_delete_var(cat, temp);
    }

    /* Backwards compatibility for standalone LBA Enhancer from v4.2 and older. */
    if (ini_section_get_int(ini_find_section(config, "Storage controllers"), "lba_enhancer_enabled", 0) == 1) {
        /* Migrate to the first available ISA ROM slot. */
        for (uint8_t c = 0; c < ISAROM_MAX; c++) {
            if (!isarom_type[c]) {
                isarom_type[c] = isarom_get_from_internal_name("lba_enhancer");
                break;
            }
        }
    }

    p           = ini_section_get_string(cat, "isartc_type", "none");
    isartc_type = isartc_get_from_internal_name(p);

    if (!strcmp(p, "none"))
        ini_section_delete_var(cat, temp);
}

#ifndef USE_SDL_UI
/* Load OpenGL 3.0 renderer options. */
static void
load_gl3_shaders(void)
{
    ini_section_t cat = ini_find_section(config, "GL3 Shaders");
    char         *p;
    char          temp[512];
    int           i = 0, shaders = 0;
    memset(temp, 0, sizeof(temp));
    memset(gl3_shader_file, 0, sizeof(gl3_shader_file));

    shaders = ini_section_get_int(cat, "shaders", 0);
    if (shaders > MAX_USER_SHADERS)
        shaders = MAX_USER_SHADERS;

    if (shaders == 0) {
        ini_section_t general = ini_find_section(config, "General");
        if (general) {
            p = ini_section_get_string(general, "video_gl_shader", NULL);
            if (p) {
                if (strlen(p) > 511)
                    fatal("Configuration: Length of video_gl_shadr is more than 511\n");
                else
                    strncpy(gl3_shader_file[0], p, 511);
                ini_delete_var(config, general, "video_gl_shader");
                return;
            }
        }
    }

    for (i = 0; i < shaders; i++) {
        temp[0] = 0;
        snprintf(temp, 512, "shader%d", i);
        p = ini_section_get_string(cat, temp, "");
        if (p[0]) {
            strncpy(gl3_shader_file[i], p, 512);
        } else {
            gl3_shader_file[i][0] = 0;
            break;
        }
    }
}
#endif

/* Load "Keybinds" section. */
static void
load_keybinds(void)
{
    ini_section_t cat = ini_find_section(config, "Keybinds");
    char         *p;
    char          temp[512];
    memset(temp, 0, sizeof(temp));

    /* Now load values from config */
    for (int x = 0; x < NUM_ACCELS; x++) {
         p = ini_section_get_string(cat, acc_keys[x].name, "default");
         /* Check if the binding was marked as cleared */
         if (strcmp(p, "none") == 0)
             acc_keys[x].seq[0] = '\0';
         /* If there's no binding in the file, leave it alone. */
         else if (strcmp(p, "default") != 0) {
             /*
                It would be ideal to validate whether the user entered a
                valid combo at this point, but the Qt method for testing that is
                not available from C. Fortunately, if you feed Qt an invalid
                keysequence string it just assigns nothing, so this won't blow up.
                However, to improve the user experience, we should validate keys
                and erase any bad combos from config on mainwindow load.
              */
             strcpy(acc_keys[x].seq, p);
        }
    }
}

void
config_load_global(void)
{
    config_log("Loading global config file '%s'...\n", global_cfg_path);

    global = ini_read(global_cfg_path);

    if (global == NULL) {
        global = ini_new();

        config_log("Global config file not present or invalid!\n");
    }

    load_global();
}

/* Load the specified or a default configuration file. */
void
config_load(void)
{
    int           i;
    ini_section_t c;

    config_log("Loading VM config file '%s'...\n", cfg_path);

    memset(hdd, 0, sizeof(hard_disk_t));
    memset(cdrom, 0, sizeof(cdrom_t) * CDROM_NUM);
#ifdef USE_IOCTL
    memset(cdrom_ioctl, 0, sizeof(cdrom_ioctl_t) * CDROM_NUM);
#endif
    memset(rdisk_drives, 0, sizeof(rdisk_drive_t));

    for (int i = 0; i < 768; i++)
        scancode_config_map[i] = i;

    config = ini_read(cfg_path);

    if (config == NULL) {
        config = ini_new();

        cpu_f = (cpu_family_t *) &cpu_families[0];
        cpu   = 0;

        kbd_req_capture      = 0;
        hide_status_bar      = 0;
        hide_tool_bar        = 0;
        scale                = 1;
        machine              = machine_get_machine_from_internal_name("ibmpc");
        dpi_scale            = 1;
        do_auto_pause        = 0;
        force_constant_mouse = 0;

        cpu_override_interpreter = 0;

        fpu_type               = fpu_get_type(cpu_f, cpu, "none");
        gfxcard[0]             = video_get_video_from_internal_name("cga");
        vid_api                = plat_vidapi("default");
        vid_resize             = 0;
        video_fullscreen_scale = 1;
        time_sync              = TIME_SYNC_ENABLED;

        keyboard_type          = KEYBOARD_TYPE_PC_XT;

        for (int i = 0; i < HDC_MAX; i++)
            hdc_current[i]         = hdc_get_from_internal_name("none");

        jumpered_internal_ecp_dma = -1;

        com_ports[0].enabled = 1;
        com_ports[1].enabled = 1;
        for (i = 2; i < (SERIAL_MAX - 1); i++)
            com_ports[i].enabled = 0;

        lpt_ports[0].enabled = 1;

        for (i = 1; i < PARALLEL_MAX; i++)
            lpt_ports[i].enabled = 0;

        for (i = 0; i < FDD_NUM; i++) {
            if (i < 2)
                fdd_set_type(i, 2);
            else
                fdd_set_type(i, 0);

            fdd_set_turbo(i, 0);
            fdd_set_check_bpb(i, 1);
        }

        /* Unmute the CD audio on the first CD-ROM drive. */
        cdrom[0].sound_on = 1;
        mem_size          = 64;
        isartc_type       = 0;
        for (i = 0; i < ISAROM_MAX; i++)
            isarom_type[i] = 0;
        for (i = 0; i < ISAMEM_MAX; i++)
            isamem_type[i] = 0;

        cassette_enable = 1;
        memset(cassette_fname, 0x00, sizeof(cassette_fname));
        memcpy(cassette_mode, "load", strlen("load") + 1);
        cassette_pos          = 0;
        cassette_srate        = 44100;
        cassette_append       = 0;
        cassette_pcm          = 0;
        cassette_ui_writeprot = 0;

        config_log("VM config file not present or invalid!\n");
    } else {
        load_general();                 /* General */
        for (i = 0; i < MONITORS_NUM; i++)
            load_monitor(i);            /* Monitors */
        load_scan_code_mappings();      /* Scan code mappings */
        load_machine();                 /* Machine */
        load_video();                   /* Video */
        load_input_devices();           /* Input devices */
        load_sound();                   /* Sound */
        load_network();                 /* Network */
        load_ports();                   /* Ports (COM & LPT) */
        load_storage_controllers();     /* Storage controllers */
        load_hard_disks();              /* Hard disks */
        load_floppy_and_cdrom_drives(); /* Floppy and CD-ROM drives */
        load_other_removable_devices(); /* Other removable devices */
        load_other_peripherals();       /* Other peripherals */
#ifndef USE_SDL_UI
        load_gl3_shaders();             /* GL3 Shaders */
#endif
        load_keybinds();                /* Load shortcut keybinds */

        /* Migrate renamed device configurations. */
        c = ini_find_section(config, "MDA");
        if (c != NULL)
            ini_rename_section(c, "IBM MDA");
        c = ini_find_section(config, "CGA");
        if (c != NULL)
            ini_rename_section(c, "IBM CGA");
        c = ini_find_section(config, "EGA");
        if (c != NULL)
            ini_rename_section(c, "IBM EGA");
        c = ini_find_section(config, "3DFX Voodoo Graphics");
        if (c != NULL)
            ini_rename_section(c, "3Dfx Voodoo Graphics");
        c = ini_find_section(config, "3dfx Voodoo Banshee");
        if (c != NULL)
            ini_rename_section(c, "3Dfx Voodoo Banshee");

        config_log("VM config loaded.\n\n");
    }

    /* Mark the configuration as changed. */
    config_changed = 1;

    video_copy = (video_grayscale || invert_display) ? video_transform_copy : memcpy;
}

/* Save global configuration */
static void
save_global(void)
{
    ini_section_t cat = ini_find_or_create_section(global, "");
    char          buffer[512] = { 0 };

    if (lang_id == plat_language_code(DEFAULT_LANGUAGE))
        ini_section_delete_var(cat, "language");
    else {
        plat_language_code_r(lang_id, buffer, 511);
        ini_section_set_string(cat, "language", buffer);
    }

    if (color_scheme)
        ini_section_set_int(cat, "color_scheme", color_scheme);
    else
        ini_section_delete_var(cat, "color_scheme");

    if (open_dir_usr_path)
        ini_section_set_int(cat, "open_dir_usr_path", open_dir_usr_path);
    else
        ini_section_delete_var(cat, "open_dir_usr_path");

    if (confirm_reset != 1)
        ini_section_set_int(cat, "confirm_reset", confirm_reset);
    else
        ini_section_delete_var(cat, "confirm_reset");

    if (confirm_exit != 1)
        ini_section_set_int(cat, "confirm_exit", confirm_exit);
    else
        ini_section_delete_var(cat, "confirm_exit");

    if (confirm_save != 1)
        ini_section_set_int(cat, "confirm_save", confirm_save);
    else
        ini_section_delete_var(cat, "confirm_save");

    if (inhibit_multimedia_keys == 1)
        ini_section_set_int(cat, "inhibit_multimedia_keys", inhibit_multimedia_keys);
    else
        ini_section_delete_var(cat, "inhibit_multimedia_keys");

    if (mouse_sensitivity != 1.0)
        ini_section_set_double(cat, "mouse_sensitivity", mouse_sensitivity);
    else
        ini_section_delete_var(cat, "mouse_sensitivity");

    if (vmm_disabled != 0)
        ini_section_set_int(cat, "vmm_disabled", vmm_disabled);
    else
        ini_section_delete_var(cat, "vmm_disabled");

    if (vmm_path_cfg[0] != 0) {
        /* Save path as relative to the EXE path in portable mode */
        if (portable_mode && path_abs(vmm_path_cfg) && !strnicmp(vmm_path_cfg, exe_path, strlen(exe_path))) {
            ini_section_set_string(cat, "vmm_path", &vmm_path_cfg[strlen(exe_path)]);
        } else {
            ini_section_set_string(cat, "vmm_path", vmm_path_cfg);
        }
    } else {
        ini_section_delete_var(cat, "vmm_path");
    }
}

/* Save scan code mappings. */
static void
save_scan_code_mappings(void)
{
    ini_section_t cat = ini_find_section(config, "Scan code mappings");
    char          temp[512];

    for (int c = 0; c < 768; c++) {
        sprintf(temp, "%03X", c);

        if (scancode_config_map[c] == c)
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_hex12(cat, temp, scancode_config_map[c]);
    }

    ini_delete_section_if_empty(config, cat);
}

/* Save "General" section. */
static void
save_general(void)
{
    ini_section_t cat = ini_find_or_create_section(config, "General");
    char          temp[512];

    const char *va_name;

    ini_section_set_int(cat, "force_10ms", force_10ms);
    if (force_10ms == 0)
        ini_section_delete_var(cat, "force_10ms");

    ini_section_set_int(cat, "sound_muted", sound_muted);
    if (sound_muted == 0)
        ini_section_delete_var(cat, "sound_muted");

    ini_section_set_int(cat, "vid_resize", vid_resize);
    if (vid_resize == 0)
        ini_section_delete_var(cat, "vid_resize");

    va_name = plat_vidapi_name(vid_api);
    if (!strcmp(va_name, "default"))
        ini_section_delete_var(cat, "vid_renderer");
    else
        ini_section_set_string(cat, "vid_renderer", va_name);

    if (video_fullscreen_scale == 1)
        ini_section_delete_var(cat, "video_fullscreen_scale");
    else
        ini_section_set_int(cat, "video_fullscreen_scale", video_fullscreen_scale);

    if (video_filter_method == 1)
        ini_section_delete_var(cat, "video_filter_method");
    else
        ini_section_set_int(cat, "video_filter_method", video_filter_method);

    if (force_43 == 0)
        ini_section_delete_var(cat, "force_43");
    else
        ini_section_set_int(cat, "force_43", force_43);

    if (scale == 1)
        ini_section_delete_var(cat, "scale");
    else
        ini_section_set_int(cat, "scale", scale);

    if (dpi_scale == 1)
        ini_section_delete_var(cat, "dpi_scale");
    else
        ini_section_set_int(cat, "dpi_scale", dpi_scale);

    if (enable_overscan == 0)
        ini_section_delete_var(cat, "enable_overscan");
    else
        ini_section_set_int(cat, "enable_overscan", enable_overscan);

    if (vid_cga_contrast == 0)
        ini_section_delete_var(cat, "vid_cga_contrast");
    else
        ini_section_set_int(cat, "vid_cga_contrast", vid_cga_contrast);

    if (video_grayscale == 0)
        ini_section_delete_var(cat, "video_grayscale");
    else
        ini_section_set_int(cat, "video_grayscale", video_grayscale);

    if (video_graytype == 0)
        ini_section_delete_var(cat, "video_graytype");
    else
        ini_section_set_int(cat, "video_graytype", video_graytype);

    if (rctrl_is_lalt == 0)
        ini_section_delete_var(cat, "rctrl_is_lalt");
    else
        ini_section_set_int(cat, "rctrl_is_lalt", rctrl_is_lalt);

    if (update_icons == 1)
        ini_section_delete_var(cat, "update_icons");
    else
        ini_section_set_int(cat, "update_icons", update_icons);

    if (window_remember)
        ini_section_set_int(cat, "window_remember", window_remember);
    else
        ini_section_delete_var(cat, "window_remember");

    if (video_fullscreen)
        ini_section_set_int(cat, "start_in_fullscreen", video_fullscreen);
    else
        ini_section_delete_var(cat, "start_in_fullscreen");

    if (vid_resize & 2) {
        sprintf(temp, "%ix%i", fixed_size_x, fixed_size_y);
        ini_section_set_string(cat, "window_fixed_res", temp);
    } else
        ini_section_delete_var(cat, "window_fixed_res");

    if (sound_gain != 0)
        ini_section_set_int(cat, "sound_gain", sound_gain);
    else
        ini_section_delete_var(cat, "sound_gain");

    if (kbd_req_capture != 0)
        ini_section_set_int(cat, "kbd_req_capture", kbd_req_capture);
    else
        ini_section_delete_var(cat, "kbd_req_capture");

    if (hide_status_bar != 0)
        ini_section_set_int(cat, "hide_status_bar", hide_status_bar);
    else
        ini_section_delete_var(cat, "hide_status_bar");

    if (hide_tool_bar != 0)
        ini_section_set_int(cat, "hide_tool_bar", hide_tool_bar);
    else
        ini_section_delete_var(cat, "hide_tool_bar");

    if (enable_discord)
        ini_section_set_int(cat, "enable_discord", enable_discord);
    else
        ini_section_delete_var(cat, "enable_discord");

    if (video_framerate != -1)
        ini_section_set_int(cat, "video_gl_framerate", video_framerate);
    else
        ini_section_delete_var(cat, "video_gl_framerate");
    if (video_vsync != 0)
        ini_section_set_int(cat, "video_gl_vsync", video_vsync);
    else
        ini_section_delete_var(cat, "video_gl_vsync");

    if (do_auto_pause)
        ini_section_set_int(cat, "do_auto_pause", do_auto_pause);
    else
        ini_section_delete_var(cat, "do_auto_pause");

    if (video_gl_input_scale != 1.0) {
        ini_section_set_double(cat, "video_gl_input_scale", video_gl_input_scale);
    } else {
        ini_section_delete_var(cat, "video_gl_input_scale");
    }

    if (video_gl_input_scale_mode != FULLSCR_SCALE_FULL) {
        ini_section_set_int(cat, "video_gl_input_scale_mode", video_gl_input_scale_mode);
    } else {
        ini_section_delete_var(cat, "video_gl_input_scale_mode");
    }

    if (force_constant_mouse)
        ini_section_set_int(cat, "force_constant_mouse", force_constant_mouse);
    else
        ini_section_delete_var(cat, "force_constant_mouse");

    if (fdd_sounds_enabled == 1)
        ini_section_delete_var(cat, "fdd_sounds_enabled");
    else
        ini_section_set_int(cat, "fdd_sounds_enabled", fdd_sounds_enabled);

    char cpu_buf[128] = { 0 };
    plat_get_cpu_string(cpu_buf, 128);
    ini_section_set_string(cat, "host_cpu", cpu_buf);

    if (EMU_BUILD_NUM != 0)
        ini_section_set_int(cat, "emu_build_num", EMU_BUILD_NUM);
    else
        ini_section_delete_var(cat, "emu_build_num");

  if (strnlen(uuid, sizeof(uuid) - 1) > 0)
        ini_section_set_string(cat, "uuid", uuid);
    else
        ini_section_delete_var(cat, "uuid");

    ini_delete_section_if_empty(config, cat);
}

/* Save monitor section. */
static void
save_monitor(int monitor_index)
{
    ini_section_t       cat;
    char                name[sizeof("Monitor #") + 12] = { [0] = 0 };
    char                temp[512];
    monitor_settings_t *ms        = &monitor_settings[monitor_index];

    snprintf(name, sizeof(name), "Monitor #%i", monitor_index + 1);
    cat = ini_find_or_create_section(config, name);

    if (window_remember) {
        sprintf(temp, "%i, %i, %i, %i", ms->mon_window_x, ms->mon_window_y,
                ms->mon_window_w, ms->mon_window_h);

        ini_section_set_string(cat, "window_coordinates", temp);
        if (ms->mon_window_maximized != 0)
            ini_section_set_int(cat, "window_maximized", ms->mon_window_maximized);
        else
            ini_section_delete_var(cat, "window_maximized");
    } else {
        ini_section_delete_var(cat, "window_coordinates");
        ini_section_delete_var(cat, "window_maximized");
    }

    ini_delete_section_if_empty(config, cat);
}

/* Save "Machine" section. */
static void
save_machine(void)
{
    ini_section_t cat = ini_find_or_create_section(config, "Machine");
    const char   *p;

    p = machine_get_internal_name();
    ini_section_set_string(cat, "machine", p);

    ini_section_set_string(cat, "cpu_family", cpu_f->internal_name);
    ini_section_set_uint(cat, "cpu_speed", cpu_f->cpus[cpu].rspeed);
    ini_section_set_double(cat, "cpu_multi", cpu_f->cpus[cpu].multi);
    if (cpu_override)
        ini_section_set_int(cat, "cpu_override", cpu_override);
    else
        ini_section_delete_var(cat, "cpu_override");
    if (cpu_override_interpreter)
        ini_section_set_int(cat, "cpu_override_interpreter", cpu_override_interpreter);
    else
        ini_section_delete_var(cat, "cpu_override_interpreter");

    /* Downgrade compatibility with the previous CPU model system. */
    ini_section_delete_var(cat, "cpu_manufacturer");
    ini_section_delete_var(cat, "cpu");

    if (cpu_waitstates == 0)
        ini_section_delete_var(cat, "cpu_waitstates");
    else
        ini_section_set_int(cat, "cpu_waitstates", cpu_waitstates);

    if (fpu_type == 0)
        ini_section_delete_var(cat, "fpu_type");
    else
        ini_section_set_string(cat, "fpu_type", fpu_get_internal_name(cpu_f, cpu, fpu_type));

    /* Write the mem_size explicitly to the setttings in order to help managers
       to display it without having the actual machine table. */
    ini_section_set_int(cat, "mem_size", mem_size);

    ini_section_set_int(cat, "cpu_use_dynarec", cpu_use_dynarec);

    if (fpu_softfloat == 0)
        ini_section_delete_var(cat, "fpu_softfloat");
    else
        ini_section_set_int(cat, "fpu_softfloat", fpu_softfloat);

    if (time_sync & TIME_SYNC_ENABLED)
        if (time_sync & TIME_SYNC_UTC)
            ini_section_set_string(cat, "time_sync", "utc");
        else
            ini_section_delete_var(cat, "time_sync");
    else
        ini_section_set_string(cat, "time_sync", "disabled");

    if (pit_mode == -1)
        ini_section_delete_var(cat, "pit_mode");
    else
        ini_section_set_int(cat, "pit_mode", pit_mode);

    ini_delete_section_if_empty(config, cat);
}

/* Save "Video" section. */
static void
save_video(void)
{
    ini_section_t cat = ini_find_or_create_section(config, "Video");

    ini_section_set_string(cat, "gfxcard",
                           video_get_internal_name(gfxcard[0]));

    if (monitor_edid)
        ini_section_set_int(cat, "monitor_edid", monitor_edid);
    else
        ini_section_delete_var(cat, "monitor_edid");

    if (monitor_edid_path[0])
        ini_section_set_string(cat, "monitor_edid_path", monitor_edid_path);
    else
        ini_section_delete_var(cat, "monitor_edid_path");

    if (vid_cga_comp_brightness)
        ini_section_set_int(cat, "vid_cga_comp_brightness", vid_cga_comp_brightness);
    else
        ini_section_delete_var(cat, "vid_cga_comp_brightness");

    if (vid_cga_comp_sharpness)
        ini_section_set_int(cat, "vid_cga_comp_sharpness", vid_cga_comp_sharpness);
    else
        ini_section_delete_var(cat, "vid_cga_comp_sharpness");

    if (vid_cga_comp_contrast != 100)
        ini_section_set_int(cat, "vid_cga_comp_contrast", vid_cga_comp_contrast);
    else
        ini_section_delete_var(cat, "vid_cga_comp_contrast");

    if (vid_cga_comp_hue)
        ini_section_set_int(cat, "vid_cga_comp_hue", vid_cga_comp_hue);
    else
        ini_section_delete_var(cat, "vid_cga_comp_hue");

    if (vid_cga_comp_saturation != 100)
        ini_section_set_int(cat, "vid_cga_comp_saturation", vid_cga_comp_saturation);
    else
        ini_section_delete_var(cat, "vid_cga_comp_saturation");

    if (voodoo_enabled == 0)
        ini_section_delete_var(cat, "voodoo");
    else
        ini_section_set_int(cat, "voodoo", voodoo_enabled);

    if (ibm8514_standalone_enabled == 0)
        ini_section_delete_var(cat, "8514a");
    else
        ini_section_set_int(cat, "8514a", ibm8514_standalone_enabled);

    if (xga_standalone_enabled == 0)
        ini_section_delete_var(cat, "xga");
    else
        ini_section_set_int(cat, "xga", xga_standalone_enabled);

    if (da2_standalone_enabled == 0)
        ini_section_delete_var(cat, "da2");
    else
        ini_section_set_int(cat, "da2", da2_standalone_enabled);

    // TODO
    for (uint8_t i = 1; i < GFXCARD_MAX; i ++) {
        if (gfxcard[i] == 0)
            ini_section_delete_var(cat, "gfxcard_2");
        else
            ini_section_set_string(cat, "gfxcard_2", video_get_internal_name(gfxcard[i]));
    }

    if (show_second_monitors == 1)
        ini_section_delete_var(cat, "show_second_monitors");
    else
        ini_section_set_int(cat, "show_second_monitors", show_second_monitors);

    if (video_fullscreen_scale_maximized == 0)
        ini_section_delete_var(cat, "video_fullscreen_scale_maximized");
    else
        ini_section_set_int(cat, "video_fullscreen_scale_maximized", video_fullscreen_scale_maximized);

    ini_delete_section_if_empty(config, cat);
}

/* Save "Input Devices" section. */
static void
save_input_devices(void)
{
    ini_section_t cat = ini_find_or_create_section(config, "Input devices");
    char          temp[512];
    char          tmp2[32];

    ini_section_set_string(cat, "keyboard_type", keyboard_get_internal_name(keyboard_type));

    ini_section_set_string(cat, "mouse_type", mouse_get_internal_name(mouse_type));

    uint8_t joy_insn = 0;
    if (!joystick_type[joy_insn]) {
        ini_section_delete_var(cat, "joystick_type");

        for (int js = 0; js < MAX_PLAT_JOYSTICKS; js++) {
            sprintf(temp, "joystick_%i_nr", js);
            ini_section_delete_var(cat, temp);

            // --- Save Axis Mappings ---
            for (int axis_nr = 0; axis_nr < MAX_JOY_AXES; axis_nr++) {
                sprintf(temp, "joystick_%i_axis_%i", js, axis_nr);
                ini_section_delete_var(cat, temp);
            }

            // --- Save Button Mappings ---
            for (int button_nr = 0; button_nr < MAX_JOY_BUTTONS; button_nr++) {
                sprintf(temp, "joystick_%i_button_%i", js, button_nr);
                ini_section_delete_var(cat, temp);
            }

            // --- Save POV (Hat Switch) Mappings ---
            for (int pov_nr = 0; pov_nr < MAX_JOY_POVS; pov_nr++) {
                sprintf(temp, "joystick_%i_pov_%i", js, pov_nr);
                ini_section_delete_var(cat, temp);
            }
        }
    } else {
        uint8_t gp = 0;

        ini_section_set_string(cat, "joystick_type", joystick_get_internal_name(joystick_type[joy_insn]));

        for (int js = 0; js < joystick_get_max_joysticks(joystick_type[joy_insn]); js++) {
            sprintf(temp, "joystick_%i_nr", js);
            ini_section_set_int(cat, temp, joystick_state[gp][js].plat_joystick_nr);

            if (joystick_state[gp][js].plat_joystick_nr) {
                // --- Save Axis Mappings ---
                for (int axis_nr = 0; axis_nr < joystick_get_axis_count(joystick_type[joy_insn]); axis_nr++) {
                    sprintf(temp, "joystick_%i_axis_%i", js, axis_nr);
                    ini_section_set_int(cat, temp, joystick_state[gp][js].axis_mapping[axis_nr]);
                }

                // --- Save Button Mappings ---
                for (int button_nr = 0; button_nr < joystick_get_button_count(joystick_type[joy_insn]); button_nr++) {
                    sprintf(temp, "joystick_%i_button_%i", js, button_nr);
                    ini_section_set_int(cat, temp, joystick_state[gp][js].button_mapping[button_nr]);
                }

                // --- Save POV (Hat Switch) Mappings ---
                for (int pov_nr = 0; pov_nr < joystick_get_pov_count(joystick_type[joy_insn]); pov_nr++) {
                    sprintf(temp, "joystick_%i_pov_%i", js, pov_nr);
                    sprintf(tmp2, "%i, %i", joystick_state[gp][js].pov_mapping[pov_nr][0],
                                            joystick_state[gp][js].pov_mapping[pov_nr][1]);
                    ini_section_set_string(cat, temp, tmp2);
                }
            }
        }
    }

    if (tablet_tool_type != 1)
        ini_section_set_int(cat, "tablet_tool_type", tablet_tool_type);
    else
        ini_section_delete_var(cat, "tablet_tool_type");

    ini_delete_section_if_empty(config, cat);
}

/* Save "Sound" section. */
static void
save_sound(void)
{
    ini_section_t cat = ini_find_or_create_section(config, "Sound");

    if (sound_card_current[0] == 0)
        ini_section_delete_var(cat, "sndcard");
    else
        ini_section_set_string(cat, "sndcard", sound_card_get_internal_name(sound_card_current[0]));

    if (sound_card_current[1] == 0)
        ini_section_delete_var(cat, "sndcard2");
    else
        ini_section_set_string(cat, "sndcard2", sound_card_get_internal_name(sound_card_current[1]));

    if (sound_card_current[2] == 0)
        ini_section_delete_var(cat, "sndcard3");
    else
        ini_section_set_string(cat, "sndcard3", sound_card_get_internal_name(sound_card_current[2]));

    if (sound_card_current[3] == 0)
        ini_section_delete_var(cat, "sndcard4");
    else
        ini_section_set_string(cat, "sndcard4", sound_card_get_internal_name(sound_card_current[3]));

    if (!strcmp(midi_out_device_get_internal_name(midi_output_device_current), "none"))
        ini_section_delete_var(cat, "midi_device");
    else
        ini_section_set_string(cat, "midi_device", midi_out_device_get_internal_name(midi_output_device_current));

    if (!strcmp(midi_in_device_get_internal_name(midi_input_device_current), "none"))
        ini_section_delete_var(cat, "midi_in_device");
    else
        ini_section_set_string(cat, "midi_in_device", midi_in_device_get_internal_name(midi_input_device_current));

    if (mpu401_standalone_enable == 0)
        ini_section_delete_var(cat, "mpu401_standalone");
    else
        ini_section_set_int(cat, "mpu401_standalone", mpu401_standalone_enable);

    /* Downgrade compatibility for standalone SSI-2001, CMS and GUS from v3.11 and older. */
    const char *legacy_cards[][2] = {
        {"ssi2001",      "ssi2001"},
        { "gameblaster", "cms"    },
        { "gus",         "gus"    }
    };
    for (int i = 0; i < (sizeof(legacy_cards) / sizeof(legacy_cards[0])); i++) {
        int card_id = sound_card_get_from_internal_name(legacy_cards[i][1]);
        for (int j = 0; j < (sizeof(sound_card_current) / sizeof(sound_card_current[0])); j++) {
            if (sound_card_current[j] == card_id) {
                /* A special value of 2 still enables the cards on older versions,
                   but lets newer versions know that they've already been migrated. */
                ini_section_set_int(cat, legacy_cards[i][0], 2);
                card_id = 0; /* mark as found */
                break;
            }
        }
        if (card_id > 0) /* not found */
            ini_section_delete_var(cat, legacy_cards[i][0]);
    }

    if (sound_is_float == 1)
        ini_section_delete_var(cat, "sound_type");
    else
        ini_section_set_string(cat, "sound_type", (sound_is_float == 1) ? "float" : "int16");

    if (fm_driver == FM_DRV_NUKED)
        ini_section_delete_var(cat, "fm_driver");
    else
        ini_section_set_string(cat, "fm_driver", "ymfm");

    ini_delete_section_if_empty(config, cat);
}

/* Save "Network" section. */
static void
save_network(void)
{
    char            temp[512];
    ini_section_t   cat       = ini_find_or_create_section(config, "Network");
    netcard_conf_t *nc;

    ini_section_delete_var(cat, "net_type");
    ini_section_delete_var(cat, "net_host_device");
    ini_section_delete_var(cat, "net_card");

    for (uint8_t c = 0; c < NET_CARD_MAX; c++) {
        nc = &net_cards_conf[c];

        sprintf(temp, "net_%02i_card", c + 1);
        if (nc->device_num == 0)
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_string(cat, temp, network_card_get_internal_name(nc->device_num));

        sprintf(temp, "net_%02i_net_type", c + 1);
        switch(nc->net_type) {
            case NET_TYPE_NONE:
                ini_section_delete_var(cat, temp);
                break;
            case NET_TYPE_SLIRP:
                ini_section_set_string(cat, temp, "slirp");
                break;
            case NET_TYPE_PCAP:
                ini_section_set_string(cat, temp, "pcap");
                break;
            case NET_TYPE_VDE:
                ini_section_set_string(cat, temp, "vde");
                break;
            case NET_TYPE_TAP:
                ini_section_set_string(cat, temp, "tap");
                break;
            case NET_TYPE_NLSWITCH:
                ini_section_set_string(cat, temp, "nlswitch");
                break;
            case NET_TYPE_NRSWITCH:
                ini_section_set_string(cat, temp, "nrswitch");
                break;
            default:
                break;
        }

        sprintf(temp, "net_%02i_host_device", c + 1);
        if (nc->host_dev_name[0] != '\0') {
            if (!strcmp(nc->host_dev_name, "none"))
                ini_section_delete_var(cat, temp);
            else
                ini_section_set_string(cat, temp, nc->host_dev_name);
        } else
            ini_section_delete_var(cat, temp);

        sprintf(temp, "net_%02i_link", c + 1);
        if (nc->link_state == (NET_LINK_10_HD | NET_LINK_10_FD |
                               NET_LINK_100_HD | NET_LINK_100_FD |
                               NET_LINK_1000_HD | NET_LINK_1000_FD))
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_int(cat, temp, nc->link_state);

        if (nc->net_type == NET_TYPE_SLIRP && nc->slirp_net[0] != '\0') {
            sprintf(temp, "net_%02i_addr", c + 1);
            ini_section_set_string(cat, temp, nc->slirp_net);
        } else {
            sprintf(temp, "net_%02i_addr", c + 1);
            ini_section_delete_var(cat, temp);
        }

        sprintf(temp, "net_%02i_secret", c + 1);
        if (nc->secret[0] == '\0')
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_string(cat, temp, net_cards_conf[c].secret);

        sprintf(temp, "net_%02i_promisc", c + 1);
        if (nc->promisc_mode == 0)
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_int(cat, temp, nc->promisc_mode);

        sprintf(temp, "net_%02i_nrs_host", c + 1);
        if (nc->nrs_hostname[0] == '\0')
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_string(cat, temp, net_cards_conf[c].nrs_hostname);
    }

    ini_delete_section_if_empty(config, cat);
}

/* Save "Ports" section. */
static void
save_ports(void)
{
    ini_section_t cat = ini_find_or_create_section(config, "Ports (COM & LPT)");
    char          temp[512];

    int           has_jumpers = machine_has_jumpered_ecp_dma(machine, DMA_ANY);
    int           def_jumper  = machine_get_default_jumpered_ecp_dma(machine);

    if (!has_jumpers || (jumpered_internal_ecp_dma == def_jumper))
        ini_section_delete_var(cat, "jumpered_internal_ecp_dma");
    else if (has_jumpers && !(machine_has_jumpered_ecp_dma(machine, jumpered_internal_ecp_dma))) {
        jumpered_internal_ecp_dma = def_jumper;
        ini_section_set_int(cat, "jumpered_internal_ecp_dma", jumpered_internal_ecp_dma);
    } else
        ini_section_set_int(cat, "jumpered_internal_ecp_dma", jumpered_internal_ecp_dma);

    for (int c = 0; c < (SERIAL_MAX - 1); c++) {
        sprintf(temp, "serial%d_enabled", c + 1);
        if (((c < 2) && com_ports[c].enabled) || ((c >= 2) && !com_ports[c].enabled))
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_int(cat, temp, com_ports[c].enabled);

        sprintf(temp, "serial%d_passthrough_enabled", c + 1);
        if (serial_passthrough_enabled[c])
            ini_section_set_int(cat, temp, 1);
        else
            ini_section_delete_var(cat, temp);
    }

    for (int c = 0; c < PARALLEL_MAX; c++) {
        sprintf(temp, "lpt%d_enabled", c + 1);
        int d = (c == 0) ? 1 : 0;
        if (lpt_ports[c].enabled == d)
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_int(cat, temp, lpt_ports[c].enabled);

        sprintf(temp, "lpt%d_device", c + 1);
        if (lpt_ports[c].device == 0)
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_string(cat, temp, lpt_device_get_internal_name(lpt_ports[c].device));
    }

#if 0
// TODO: Save
    for (c = 0; c < GAMEPORT_MAX; c++) {
        sprintf(temp, "gameport%d_enabled", c + 1);
        d = (c == 0) ? 1 : 0;
        if (game_ports[c].enabled == d)
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_int(cat, temp, game_ports[c].enabled);

        sprintf(temp, "gameport%d_device", c + 1);
        if (game_ports[c].device == 0)
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_string(cat, temp,
                                   gameport_get_internal_name(game_ports[c].device));
    }

    for (uint8_t c = 0; c < GAMEPORT_MAX; c++) {
        sprintf(temp, "gameport%d_enabled", c);
        if (gameport_type[c] == 0)
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_string(cat, temp,
                                   gameport_get_internal_name(gameport_type[c]));
    }
#endif

    ini_delete_section_if_empty(config, cat);
}

/* Save "Keybinds" section. */
static void
save_keybinds(void)
{
    ini_section_t cat = ini_find_or_create_section(config, "Keybinds");

    for (int x = 0; x < NUM_ACCELS; x++) {
        /* Has accelerator been changed from default? */
        if (strcmp(def_acc_keys[x].seq, acc_keys[x].seq) == 0)
            ini_section_delete_var(cat, acc_keys[x].name);
        /* Check for a cleared binding to avoid saving it as an empty string */
        else if (acc_keys[x].seq[0] == '\0')
            ini_section_set_string(cat, acc_keys[x].name, "none");
        else
            ini_section_set_string(cat, acc_keys[x].name, acc_keys[x].seq);
    }

    ini_delete_section_if_empty(config, cat);
}

static void
save_image_file(char *cat, char *var, char *src)
{
    char  temp[2048] = { 0 };
    char *prefix     = "";
    char *slash      = NULL;
    char *above      = NULL;
    char *above2     = NULL;
    char *above3     = NULL;

    if ((slash = memrmem(usr_path + strlen(usr_path) - 2, usr_path, "/")) != NULL) {
        slash++;
        above = (char *) calloc(1, slash - usr_path + 1);
        memcpy(above, usr_path, slash - usr_path);

        if ((slash = memrmem(above + strlen(above) - 2, above, "/")) != NULL) {
            slash++;
            above2 = (char *) calloc(1, slash - above + 1);
            memcpy(above2, above, slash - above);

            if ((slash = memrmem(above2 + strlen(above2) - 2, above2, "/")) != NULL) {
                slash++;
                above3 = (char *) calloc(1, slash - above2 + 1);
                memcpy(above3, above2, slash - above2);
            }
        }
    }

    path_normalize(src);

    if (strstr(src, "wp://") == src) {
        src    += 5;
        prefix  = "wp://";
    }

    if (strstr(src, "ioctl://") == src)
        sprintf(temp, "%s", src);
    else if (!strnicmp(src, usr_path, strlen(usr_path)))
        sprintf(temp, "%s%s", prefix, &src[strlen(usr_path)]);
    /* Do not relativize to root. */
    else if ((above2 != NULL) && (above3 != NULL) && !strnicmp(src, above2, strlen(above2)))
        sprintf(temp, "../../%s%s", prefix, &src[strlen(above2)]);
    /* Do not relativize to root. */
    else if ((above != NULL) && (above2 != NULL) && !strnicmp(src, above, strlen(above)))
        sprintf(temp, "../%s%s", prefix, &src[strlen(above)]);
    else if (!strnicmp(src, exe_path, strlen(exe_path)))
        sprintf(temp, "<exe_path>/%s%s", prefix, &src[strlen(exe_path)]);
    else
        sprintf(temp, "%s%s", prefix, src);

    ini_section_set_string(cat, var, temp);

    if (above3 != NULL)
        free(above3);

    if (above2 != NULL)
        free(above2);

    if (above != NULL)
        free(above);
}

/* Save "Storage Controllers" section. */
static void
save_storage_controllers(void)
{
    ini_section_t cat = ini_find_or_create_section(config, "Storage controllers");
    char          temp[512];
    int           c;
    char          *def_hdc;

    ini_section_delete_var(cat, "scsicard");

    for (c = 0; c < SCSI_CARD_MAX; c++) {
        sprintf(temp, "scsicard_%d", c + 1);

        if (scsi_card_current[c] == 0)
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_string(cat, temp,
                                   scsi_card_get_internal_name(scsi_card_current[c]));
    }

    if (fdc_current[0] == FDC_INTERNAL)
        ini_section_delete_var(cat, "fdc");
    else
        ini_section_set_string(cat, "fdc",
                               fdc_card_get_internal_name(fdc_current[0]));

    ini_section_delete_var(cat, "hdc");

    for (c = 0; c < HDC_MAX; c++) {
        sprintf(temp, "hdc_%d", c + 1);

        if ((c == 0) && machine_has_flags(machine, MACHINE_HDC))
            def_hdc = "internal";
        else
            def_hdc = "none";

        if (!strcmp(hdc_get_internal_name(hdc_current[c]), def_hdc) || ((c > 0) && (hdc_current[c] == 1)))
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_string(cat, temp,
                                   hdc_get_internal_name(hdc_current[c]));
    }

    /* Downgrade compatibility for standalone tertiary/quaternary IDE from v4.2 and older. */
    const char *legacy_cards[] = { "ide_ter", "ide_qua" };
    for (int i = 0; i < (sizeof(legacy_cards) / sizeof(legacy_cards[0])); i++) {
        int card_id = hdc_get_from_internal_name(legacy_cards[i]);
        for (int j = 0; j < (sizeof(sound_card_current) / sizeof(sound_card_current[0])); j++) {
            if (hdc_current[j] == card_id) {
                /* A special value of 2 still enables the cards on older versions,
                   but lets newer versions know that they've already been migrated. */
                ini_section_set_int(cat, legacy_cards[i], 2);
                card_id = 0; /* mark as found */
                break;
            }
        }
        if (card_id > 0) /* not found */
            ini_section_delete_var(cat, legacy_cards[i]);
    }

    if (cdrom_interface_current == 0)
        ini_section_delete_var(cat, "cdrom_interface");
    else
        ini_section_set_string(cat, "cdrom_interface",
                               cdrom_interface_get_internal_name(cdrom_interface_current));

    if (cassette_enable == 0)
        ini_section_delete_var(cat, "cassette_enabled");
    else
        ini_section_set_int(cat, "cassette_enabled", cassette_enable);

    if (strlen(cassette_fname) == 0)
        ini_section_delete_var(cat, "cassette_file");
    else
        save_image_file(cat, "cassette_file", cassette_fname);

    if (!strcmp(cassette_mode, "load"))
        ini_section_delete_var(cat, "cassette_mode");
    else
        ini_section_set_string(cat, "cassette_mode", cassette_mode);

    for (int i = 0; i < MAX_PREV_IMAGES; i++) {
        sprintf(temp, "cassette_image_history_%02i", i + 1);
        if ((cassette_image_history[i] == 0) || strlen(cassette_image_history[i]) == 0)
            ini_section_delete_var(cat, temp);
        else
            save_image_file(cat, temp, cassette_image_history[i]);
    }

    if (cassette_pos == 0)
        ini_section_delete_var(cat, "cassette_position");
    else
        ini_section_set_int(cat, "cassette_position", cassette_pos);

    if (cassette_srate == 44100)
        ini_section_delete_var(cat, "cassette_srate");
    else
        ini_section_set_int(cat, "cassette_srate", cassette_srate);

    if (cassette_append == 0)
        ini_section_delete_var(cat, "cassette_append");
    else
        ini_section_set_int(cat, "cassette_append", cassette_append);

    if (cassette_pcm == 0)
        ini_section_delete_var(cat, "cassette_pcm");
    else
        ini_section_set_int(cat, "cassette_pcm", cassette_pcm);

    ini_section_delete_var(cat, "cassette_writeprot");

    for (c = 0; c < 2; c++) {
        sprintf(temp, "cartridge_%02i_fn", c + 1);

        if (strlen(cart_fns[c]) == 0)
            ini_section_delete_var(cat, temp);
        else
            save_image_file(cat, temp, cart_fns[c]);

        for (int i = 0; i < MAX_PREV_IMAGES; i++) {
            sprintf(temp, "cartridge_%02i_image_history_%02i", c + 1, i + 1);
            if ((cart_image_history[c][i] == 0) || strlen(cart_image_history[c][i]) == 0)
                ini_section_delete_var(cat, temp);
            else
                save_image_file(cat, temp, cart_image_history[c][i]);
        }
    }

    /* Downgrade compatibility for standalone LBA Enhancer from v4.2 and older. */
    int card_id = isarom_get_from_internal_name("lba_enhancer");
    for (c = 0; c < ISAROM_MAX; c++) {
        if (isarom_type[c] == card_id) {
            /* A special value of 2 still enables the cards on older versions,
               but lets newer versions know that they've already been migrated. */
            ini_section_set_int(cat, "lba_enhancer_enabled", 2);
            card_id = 0; /* mark as found */
            break;
        }
    }
    if (card_id > 0) /* not found */
        ini_section_delete_var(cat, "lba_enhancer_enabled");

    ini_delete_section_if_empty(config, cat);
}

/* Save "Other Peripherals" section. */
static void
save_other_peripherals(void)
{
    ini_section_t cat = ini_find_or_create_section(config, "Other peripherals");
    char          temp[512];

    if (bugger_enabled == 0)
        ini_section_delete_var(cat, "bugger_enabled");
    else
        ini_section_set_int(cat, "bugger_enabled", bugger_enabled);

    if (postcard_enabled == 0)
        ini_section_delete_var(cat, "postcard_enabled");
    else
        ini_section_set_int(cat, "postcard_enabled", postcard_enabled);

    if (unittester_enabled == 0)
        ini_section_delete_var(cat, "unittester_enabled");
    else
        ini_section_set_int(cat, "unittester_enabled", unittester_enabled);

    if (novell_keycard_enabled == 0)
        ini_section_delete_var(cat, "novell_keycard_enabled");
    else
        ini_section_set_int(cat, "novell_keycard_enabled", novell_keycard_enabled);

    // ISA RAM Boards
    for (uint8_t c = 0; c < ISAMEM_MAX; c++) {
        sprintf(temp, "isamem%d_type", c);
        if (isamem_type[c] == 0)
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_string(cat, temp,
                                   isamem_get_internal_name(isamem_type[c]));
    }

    // ISA ROM Boards
    for (uint8_t c = 0; c < ISAROM_MAX; c++) {
        sprintf(temp, "isarom%d_type", c);
        if (isarom_type[c] == 0)
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_string(cat, temp,
                                   isarom_get_internal_name(isarom_type[c]));
    }

    if (isartc_type == 0)
        ini_section_delete_var(cat, "isartc_type");
    else
        ini_section_set_string(cat, "isartc_type",
                               isartc_get_internal_name(isartc_type));

    ini_delete_section_if_empty(config, cat);
}

#ifndef USE_SDL_UI
/* Save "GL3 Shaders" section. */
static void
save_gl3_shaders(void)
{
    ini_section_t cat = ini_find_or_create_section(config, "GL3 Shaders");
    char          temp[512];
    int shaders = 0, i = 0;

    for (i = 0; i < MAX_USER_SHADERS; i++) {
        if (gl3_shader_file[i][0] == 0) {
            temp[0] = 0;
            snprintf(temp, 512, "shader%d", i);
            ini_section_delete_var(cat, temp);
            break;
        }
        shaders++;
    }

    ini_section_set_int(cat, "shaders", shaders);
    if (shaders == 0) {
        ini_section_delete_var(cat, "shaders");
    } else {
        for (i = 0; i < shaders; i++) {
            temp[0] = 0;
            snprintf(temp, 512, "shader%d", i);
            ini_section_set_string(cat, temp, gl3_shader_file[i]);
        }
    }

    ini_delete_section_if_empty(config, cat);
}
#endif

/* Save "Hard Disks" section. */
static void
save_hard_disks(void)
{
    ini_section_t cat = ini_find_or_create_section(config, "Hard disks");
    char          temp[32];
    char          tmp2[512];
    char         *p;

    memset(temp, 0x00, sizeof(temp));
    for (uint8_t c = 0; c < HDD_NUM; c++) {
        sprintf(temp, "hdd_%02i_parameters", c + 1);
        if (hdd_is_valid(c)) {
            p = hdd_bus_to_string(hdd[c].bus_type, 0);
            sprintf(tmp2, "%u, %u, %u, %i, %s",
                    hdd[c].spt, hdd[c].hpc, hdd[c].tracks, hdd[c].wp, p);
            ini_section_set_string(cat, temp, tmp2);
        } else
            ini_section_delete_var(cat, temp);

        sprintf(temp, "hdd_%02i_mfm_channel", c + 1);
        if (hdd_is_valid(c) && (hdd[c].bus_type == HDD_BUS_MFM))
            ini_section_set_int(cat, temp, hdd[c].mfm_channel);
        else
            ini_section_delete_var(cat, temp);

        sprintf(temp, "hdd_%02i_xta_channel", c + 1);
        if (hdd_is_valid(c) && (hdd[c].bus_type == HDD_BUS_XTA))
            ini_section_set_int(cat, temp, hdd[c].xta_channel);
        else
            ini_section_delete_var(cat, temp);

        sprintf(temp, "hdd_%02i_esdi_channel", c + 1);
        if (hdd_is_valid(c) && (hdd[c].bus_type == HDD_BUS_ESDI))
            ini_section_set_int(cat, temp, hdd[c].esdi_channel);
        else
            ini_section_delete_var(cat, temp);

        sprintf(temp, "hdd_%02i_ide_channel", c + 1);
        if (!hdd_is_valid(c) || ((hdd[c].bus_type != HDD_BUS_IDE) &&
            (hdd[c].bus_type != HDD_BUS_ATAPI)))
            ini_section_delete_var(cat, temp);
        else {
            sprintf(tmp2, "%01u:%01u", hdd[c].ide_channel >> 1, hdd[c].ide_channel & 1);
            ini_section_set_string(cat, temp, tmp2);
        }

        sprintf(temp, "hdd_%02i_scsi_id", c + 1);
        ini_section_delete_var(cat, temp);

        sprintf(temp, "hdd_%02i_scsi_location", c + 1);
        if (hdd[c].bus_type != HDD_BUS_SCSI)
            ini_section_delete_var(cat, temp);
        else {
            sprintf(tmp2, "%01u:%02u", hdd[c].scsi_id >> 4,
                    hdd[c].scsi_id & 15);
            ini_section_set_string(cat, temp, tmp2);
        }

        sprintf(temp, "hdd_%02i_fn", c + 1);
        if (hdd_is_valid(c) && (strlen(hdd[c].fn) != 0))
            save_image_file(cat, temp, hdd[c].fn);
        else
            ini_section_delete_var(cat, temp);

        sprintf(temp, "hdd_%02i_vhd_blocksize", c + 1);
        if (hdd_is_valid(c) && (hdd[c].vhd_blocksize > 0))
            ini_section_set_int(cat, temp, hdd[c].vhd_blocksize);
        else
            ini_section_delete_var(cat, temp);

        sprintf(temp, "hdd_%02i_vhd_parent", c + 1);
        if (hdd_is_valid(c) && hdd[c].vhd_parent[0]) {
            path_normalize(hdd[c].vhd_parent);
            ini_section_set_string(cat, temp, hdd[c].vhd_parent);
        } else
            ini_section_delete_var(cat, temp);

        sprintf(temp, "hdd_%02i_speed", c + 1);
        if (!hdd_is_valid(c) ||
            ((hdd[c].bus_type != HDD_BUS_ESDI) && (hdd[c].bus_type != HDD_BUS_IDE) &&
            (hdd[c].bus_type != HDD_BUS_SCSI) && (hdd[c].bus_type != HDD_BUS_ATAPI)))
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_string(cat, temp, hdd_preset_get_internal_name(hdd[c].speed_preset));

        sprintf(temp, "hdd_%02i_audio", c + 1);
        if (!hdd_is_valid(c) || hdd[c].audio_profile == 0) {
            ini_section_delete_var(cat, temp);
        } else {
            const char *internal_name = hdd_audio_get_profile_internal_name(hdd[c].audio_profile);
            if (internal_name && strcmp(internal_name, "none") != 0)
                ini_section_set_string(cat, temp, internal_name);
            else
                ini_section_delete_var(cat, temp);
        }
    }

    ini_delete_section_if_empty(config, cat);
}

/* Save "Floppy Drives" section. */
static void
save_floppy_and_cdrom_drives(void)
{
    ini_section_t cat = ini_find_or_create_section(config, "Floppy and CD-ROM drives");
    char          temp[512];
    char          tmp2[512];
    int           c;

    for (c = 0; c < FDD_NUM; c++) {
        sprintf(temp, "fdd_%02i_type", c + 1);
        if (fdd_get_type(c) == ((c < 2) ? 2 : 0))
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_string(cat, temp,
                                   fdd_get_internal_name(fdd_get_type(c)));

        sprintf(temp, "fdd_%02i_fn", c + 1);
        if (strlen(floppyfns[c]) == 0) {
            ini_section_delete_var(cat, temp);

            ui_writeprot[c] = 0;

            sprintf(temp, "fdd_%02i_writeprot", c + 1);
            ini_section_delete_var(cat, temp);
        } else
            save_image_file(cat, temp, floppyfns[c]);

        sprintf(temp, "fdd_%02i_writeprot", c + 1);
        ini_section_delete_var(cat, temp);

        sprintf(temp, "fdd_%02i_turbo", c + 1);
        if (fdd_get_turbo(c) == 0)
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_int(cat, temp, fdd_get_turbo(c));

        sprintf(temp, "fdd_%02i_check_bpb", c + 1);
        if (fdd_get_check_bpb(c) == 1)
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_int(cat, temp, fdd_get_check_bpb(c));

        for (int i = 0; i < MAX_PREV_IMAGES; i++) {
            sprintf(temp, "fdd_%02i_image_history_%02i", c + 1, i + 1);
            if ((fdd_image_history[c][i] == 0) || strlen(fdd_image_history[c][i]) == 0)
                ini_section_delete_var(cat, temp);
            else
                save_image_file(cat, temp, fdd_image_history[c][i]);
        }

        sprintf(temp, "fdd_%02i_audio", c + 1);
#ifndef DISABLE_FDD_AUDIO
        int         prof          = fdd_get_audio_profile(c);
        const char *internal_name = fdd_audio_get_profile_internal_name(prof);
        if (internal_name && strcmp(internal_name, "none") != 0) {
            ini_section_set_string(cat, temp, internal_name);
        } else {
            ini_section_delete_var(cat, temp);
        }
#else
        ini_section_delete_var(cat, temp);
#endif
    }

    for (c = 0; c < CDROM_NUM; c++) {
        sprintf(temp, "cdrom_%02i_host_drive", c + 1);
        ini_section_delete_var(cat, temp);

        sprintf(temp, "cdrom_%02i_no_check", c + 1);
        if (cdrom[c].no_check)
            ini_section_set_int(cat, temp, cdrom[c].no_check);
        else
            ini_section_delete_var(cat, temp);

        sprintf(temp, "cdrom_%02i_speed", c + 1);
        if ((cdrom[c].bus_type == 0) || (cdrom[c].speed == 8))
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_int(cat, temp, cdrom[c].speed);

        sprintf(temp, "cdrom_%02i_type", c + 1);
        char *tn = cdrom_get_internal_name(cdrom_get_type(c));
        if ((cdrom[c].bus_type == 0) || (cdrom[c].bus_type == CDROM_BUS_MITSUMI) || !strcmp(tn, "86cd"))
            ini_section_delete_var(cat, temp);
        else
            ini_section_set_string(cat, temp, tn);

        sprintf(temp, "cdrom_%02i_parameters", c + 1);
        if (cdrom[c].bus_type == 0)
            ini_section_delete_var(cat, temp);
        else {
            /* In case one wants an ATAPI drive on SCSI and vice-versa. */
            if ((cdrom_drive_types[cdrom_get_type(c)].bus_type != BUS_TYPE_BOTH) &&
                (cdrom_drive_types[cdrom_get_type(c)].bus_type != cdrom[c].bus_type))
                cdrom[c].bus_type = cdrom_drive_types[cdrom_get_type(c)].bus_type;

            sprintf(tmp2, "%u, %s", cdrom[c].sound_on,
                    hdd_bus_to_string(cdrom[c].bus_type, 1));
            ini_section_set_string(cat, temp, tmp2);
        }

        sprintf(temp, "cdrom_%02i_mke_channel", c + 1);
        if (cdrom[c].bus_type != CDROM_BUS_MKE)
            ini_section_delete_var(cat, temp);
        else {
            ini_section_set_int(cat, temp, cdrom[c].mke_channel);
        }

        sprintf(temp, "cdrom_%02i_ide_channel", c + 1);
        if (cdrom[c].bus_type != CDROM_BUS_ATAPI)
            ini_section_delete_var(cat, temp);
        else {
            sprintf(tmp2, "%01u:%01u", cdrom[c].ide_channel >> 1,
                    cdrom[c].ide_channel & 1);
            ini_section_set_string(cat, temp, tmp2);
        }

        sprintf(temp, "cdrom_%02i_scsi_id", c + 1);
        ini_section_delete_var(cat, temp);

        sprintf(temp, "cdrom_%02i_scsi_location", c + 1);
        if (cdrom[c].bus_type != CDROM_BUS_SCSI)
            ini_section_delete_var(cat, temp);
        else {
            sprintf(tmp2, "%01u:%02u", cdrom[c].scsi_device_id >> 4,
                    cdrom[c].scsi_device_id & 15);
            ini_section_set_string(cat, temp, tmp2);
        }

        sprintf(temp, "cdrom_%02i_image_path", c + 1);
        if ((cdrom[c].bus_type == 0) || (strlen(cdrom[c].image_path) == 0))
            ini_section_delete_var(cat, temp);
        else
            save_image_file(cat, temp, cdrom[c].image_path);

        for (int i = 0; i < MAX_PREV_IMAGES; i++) {
            sprintf(temp, "cdrom_%02i_image_history_%02i", c + 1, i + 1);
            if ((cdrom[c].image_history[i] == 0) || strlen(cdrom[c].image_history[i]) == 0)
                ini_section_delete_var(cat, temp);
            else
                save_image_file(cat, temp, cdrom[c].image_history[i]);
        }
    }

    ini_delete_section_if_empty(config, cat);
}

/* Save "Other Removable Devices" section. */
static void
save_other_removable_devices(void)
{
    ini_section_t cat = ini_find_or_create_section(config, "Other removable devices");
    char          temp[512];
    char          tmp2[512];
    int           c;

    for (c = 0; c < RDISK_NUM; c++) {
        sprintf(temp, "rdisk_%02i_parameters", c + 1);
        if (rdisk_drives[c].bus_type == 0) {
            ini_section_delete_var(cat, temp);
        } else {
            sprintf(tmp2, "%u, %s", rdisk_drives[c].type,
                    hdd_bus_to_string(rdisk_drives[c].bus_type, 1));
            ini_section_set_string(cat, temp, tmp2);
        }

        sprintf(temp, "rdisk_%02i_ide_channel", c + 1);
        if (rdisk_drives[c].bus_type != RDISK_BUS_ATAPI)
            ini_section_delete_var(cat, temp);
        else {
            sprintf(tmp2, "%01u:%01u", rdisk_drives[c].ide_channel >> 1,
                    rdisk_drives[c].ide_channel & 1);
            ini_section_set_string(cat, temp, tmp2);
        }

        sprintf(temp, "rdisk_%02i_scsi_id", c + 1);
        ini_section_delete_var(cat, temp);

        sprintf(temp, "rdisk_%02i_writeprot", c + 1);
        ini_section_delete_var(cat, temp);

        sprintf(temp, "rdisk_%02i_scsi_location", c + 1);
        if (rdisk_drives[c].bus_type != RDISK_BUS_SCSI)
            ini_section_delete_var(cat, temp);
        else {
            sprintf(tmp2, "%01u:%02u", rdisk_drives[c].scsi_device_id >> 4,
                    rdisk_drives[c].scsi_device_id & 15);
            ini_section_set_string(cat, temp, tmp2);
        }

        sprintf(temp, "rdisk_%02i_image_path", c + 1);
        if ((rdisk_drives[c].bus_type == 0) || (strlen(rdisk_drives[c].image_path) == 0))
            ini_section_delete_var(cat, temp);
        else
            save_image_file(cat, temp, rdisk_drives[c].image_path);

        for (int i = 0; i < MAX_PREV_IMAGES; i++) {
            sprintf(temp, "rdisk_%02i_image_history_%02i", c + 1, i + 1);
            if ((rdisk_drives[c].image_history[i] == 0) || strlen(rdisk_drives[c].image_history[i]) == 0)
                ini_section_delete_var(cat, temp);
            else
                save_image_file(cat, temp, rdisk_drives[c].image_history[i]);
        }
    }

    for (c = 0; c < MO_NUM; c++) {
        sprintf(temp, "mo_%02i_parameters", c + 1);
        if (mo_drives[c].bus_type == 0) {
            ini_section_delete_var(cat, temp);
        } else {
            sprintf(tmp2, "%u, %s", mo_drives[c].type,
                    hdd_bus_to_string(mo_drives[c].bus_type, 1));
            ini_section_set_string(cat, temp, tmp2);
        }

        sprintf(temp, "mo_%02i_ide_channel", c + 1);
        if (mo_drives[c].bus_type != MO_BUS_ATAPI)
            ini_section_delete_var(cat, temp);
        else {
            sprintf(tmp2, "%01u:%01u", mo_drives[c].ide_channel >> 1,
                    mo_drives[c].ide_channel & 1);
            ini_section_set_string(cat, temp, tmp2);
        }

        sprintf(temp, "mo_%02i_scsi_id", c + 1);
        ini_section_delete_var(cat, temp);

        sprintf(temp, "mo_%02i_writeprot", c + 1);
        ini_section_delete_var(cat, temp);

        sprintf(temp, "mo_%02i_scsi_location", c + 1);
        if (mo_drives[c].bus_type != MO_BUS_SCSI)
            ini_section_delete_var(cat, temp);
        else {
            sprintf(tmp2, "%01u:%02u", mo_drives[c].scsi_device_id >> 4,
                    mo_drives[c].scsi_device_id & 15);
            ini_section_set_string(cat, temp, tmp2);
        }

        sprintf(temp, "mo_%02i_image_path", c + 1);
        if ((mo_drives[c].bus_type == 0) || (strlen(mo_drives[c].image_path) == 0))
            ini_section_delete_var(cat, temp);
        else
            save_image_file(cat, temp, mo_drives[c].image_path);

        for (int i = 0; i < MAX_PREV_IMAGES; i++) {
            sprintf(temp, "mo_%02i_image_history_%02i", c + 1, i + 1);
            if ((mo_drives[c].image_history[i] == 0) || strlen(mo_drives[c].image_history[i]) == 0)
                ini_section_delete_var(cat, temp);
            else
                save_image_file(cat, temp, mo_drives[c].image_history[i]);
        }
    }

    ini_delete_section_if_empty(config, cat);
}

void
config_save_global(void)
{
    save_global();                  /* Global */

    ini_write(global, global_cfg_path);
}

void
config_save(void)
{
    save_general();                 /* General */
    for (uint8_t i = 0; i < MONITORS_NUM; i++)
        save_monitor(i);            /* Monitors */
    save_scan_code_mappings();      /* Scan code mappings */
    save_machine();                 /* Machine */
    save_video();                   /* Video */
    save_input_devices();           /* Input devices */
    save_sound();                   /* Sound */
    save_network();                 /* Network */
    save_ports();                   /* Ports (COM & LPT) */
    save_storage_controllers();     /* Storage controllers */
    save_hard_disks();              /* Hard disks */
    save_floppy_and_cdrom_drives(); /* Floppy and CD-ROM drives */
    save_other_removable_devices(); /* Other removable devices */
    save_other_peripherals();       /* Other peripherals */
#ifndef USE_SDL_UI
    save_gl3_shaders();             /* GL3 Shaders */
#endif
    save_keybinds();                /* Key bindings */

    ini_write(config, cfg_path);

    config_save_global();
}

ini_t
config_get_ini(void)
{
    return config;
}
