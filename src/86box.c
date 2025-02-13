/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Main emulator module where most things are controlled.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2017-2020 Fred N. van Kempen.
 *          Copyright 2021      Laci bá'
 *          Copyright 2021      dob205
 *          Copyright 2021      Andreas J. Reichel.
 *          Copyright 2021-2025 Jasmine Iwanek.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <stdatomic.h>
#include <unistd.h>

#ifndef _WIN32
#    include <pwd.h>
#endif
#ifdef __APPLE__
#    include <string.h>
#    include <dispatch/dispatch.h>
#    ifdef __aarch64__
#        include <pthread.h>
#    endif
#endif

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/mem.h>
#include "cpu.h"
#ifdef USE_DYNAREC
#    include "codegen_public.h"
#endif
#include "x86_ops.h"
#include <86box/io.h>
#include <86box/rom.h>
#include <86box/dma.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/pit.h>
#include <86box/random.h>
#include <86box/nvr.h>
#include <86box/machine.h>
#include <86box/bugger.h>
#include <86box/postcard.h>
#include <86box/unittester.h>
#include <86box/novell_cardkey.h>
#include <86box/isamem.h>
#include <86box/isartc.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/serial_passthrough.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/gameport.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/hdd.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/cdrom_interface.h>
#include <86box/zip.h>
#include <86box/mo.h>
#include <86box/scsi_disk.h>
#include <86box/cdrom_image.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/sound.h>
#include <86box/midi.h>
#include <86box/snd_speaker.h>
#include <86box/video.h>
#include <86box/ui.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/version.h>
#include <86box/gdbstub.h>
#include <86box/machine_status.h>
#include <86box/apm.h>
#include <86box/acpi.h>
#include <86box/nv/vid_nv_rivatimer.h>

// Disable c99-designator to avoid the warnings about int ng
#ifdef __clang__
#    if __has_warning("-Wunused-but-set-variable")
#        pragma clang diagnostic ignored "-Wunused-but-set-variable"
#    endif
#endif

/* Stuff that used to be globally declared in plat.h but is now extern there
   and declared here instead. */
int          dopause = 1;  /* system is paused */
atomic_flag  doresize; /* screen resize requested */
volatile int is_quit;  /* system exit requested */
uint64_t     timer_freq;
char         emu_version[200]; /* version ID string */

#ifdef MTR_ENABLED
int tracing_on = 0;
#endif

/* Commandline options. */
int dump_on_exit        = 0; /* (O) dump regs on exit */
int start_in_fullscreen = 0; /* (O) start in fullscreen */
#ifdef _WIN32
int force_debug = 0; /* (O) force debug output */
#endif
#ifdef USE_WX
int video_fps = RENDER_FPS; /* (O) render speed in fps */
#endif
int settings_only     = 0; /* (O) show only the settings dialog */
int confirm_exit_cmdl = 1; /* (O) do not ask for confirmation on quit if set to 0 */
#ifdef _WIN32
uint64_t unique_id   = 0;
uint64_t source_hwnd = 0;
#endif
char       rom_path[1024] = { '\0' };     /* (O) full path to ROMs */
rom_path_t rom_paths      = { "", NULL }; /* (O) full paths to ROMs */
char       log_path[1024] = { '\0' };     /* (O) full path of logfile */
char       vm_name[1024]  = { '\0' };     /* (O) display name of the VM */
int      do_nothing                             = 0;
int      dump_missing                           = 0;
int      clear_cmos                             = 0;
#ifdef USE_INSTRUMENT
uint8_t  instru_enabled                         = 0;
uint64_t instru_run_ms                          = 0;
#endif
int      clear_flash                            = 0;
int      auto_paused                            = 0;

/* Configuration values. */
int      window_remember;
int      vid_resize;                                              /* (C) allow resizing */
int      invert_display                         = 0;              /* (C) invert the display */
int      suppress_overscan                      = 0;              /* (C) suppress overscans */
int      scale                                  = 0;              /* (C) screen scale factor */
int      dpi_scale                              = 0;              /* (C) DPI scaling of the emulated
                                                                         screen */
int      vid_api                                = 0;              /* (C) video renderer */
int      vid_cga_contrast                       = 0;              /* (C) video */
int      video_fullscreen                       = 0;              /* (C) video */
int      video_fullscreen_scale                 = 0;              /* (C) video */
int      video_fullscreen_first                 = 0;              /* (C) video */
int      enable_overscan                        = 0;              /* (C) video */
int      force_43                               = 0;              /* (C) video */
int      video_filter_method                    = 1;              /* (C) video */
int      video_vsync                            = 0;              /* (C) video */
int      video_framerate                        = -1;             /* (C) video */
char     video_shader[512]                      = { '\0' };       /* (C) video */
bool     serial_passthrough_enabled[SERIAL_MAX] = { 0, 0, 0, 0, 0, 0, 0 }; /* (C) activation and kind of
                                                                                  pass-through for serial ports */
int      bugger_enabled                         = 0;              /* (C) enable ISAbugger */
int      novell_keycard_enabled                 = 0;              /* (C) enable Novell NetWare 2.x key card emulation. */
int      postcard_enabled                       = 0;              /* (C) enable POST card */
int      unittester_enabled                     = 0;              /* (C) enable unit tester device */
int      gameport_type[GAMEPORT_MAX]            = { 0, 0 };       /* (C) enable gameports */
int      isamem_type[ISAMEM_MAX]                = { 0, 0, 0, 0 }; /* (C) enable ISA mem cards */
int      isartc_type                            = 0;              /* (C) enable ISA RTC card */
int      gfxcard[GFXCARD_MAX]                   = { 0, 0 };       /* (C) graphics/video card */
int      show_second_monitors                   = 1;              /* (C) show non-primary monitors */
int      sound_is_float                         = 1;              /* (C) sound uses FP values */
int      voodoo_enabled                         = 0;              /* (C) video option */
int      lba_enhancer_enabled                   = 0;              /* (C) enable Vision Systems LBA Enhancer */
int      ibm8514_standalone_enabled             = 0;              /* (C) video option */
int      xga_standalone_enabled                 = 0;              /* (C) video option */
uint32_t mem_size                               = 0;              /* (C) memory size (Installed on
                                                                         system board)*/
uint32_t isa_mem_size                           = 0;              /* (C) memory size (ISA Memory Cards) */
int      cpu_use_dynarec                        = 0;              /* (C) cpu uses/needs Dyna */
int      cpu                                    = 0;              /* (C) cpu type */
int      fpu_type                               = 0;              /* (C) fpu type */
int      fpu_softfloat                          = 0;              /* (C) fpu uses softfloat */
int      time_sync                              = 0;              /* (C) enable time sync */
int      confirm_reset                          = 1;              /* (C) enable reset confirmation */
int      confirm_exit                           = 1;              /* (C) enable exit confirmation */
int      confirm_save                           = 1;              /* (C) enable save confirmation */
int      enable_discord                         = 0;              /* (C) enable Discord integration */
int      pit_mode                               = -1;             /* (C) force setting PIT mode */
int      fm_driver                              = 0;              /* (C) select FM sound driver */
int      open_dir_usr_path                      = 0;              /* (C) default file open dialog directory
                                                                         of usr_path */
int      video_fullscreen_scale_maximized       = 0;              /* (C) Whether fullscreen scaling settings
                                                                         also apply when maximized. */
int      do_auto_pause                          = 0;              /* (C) Auto-pause the emulator on focus
                                                                         loss */
int      hook_enabled                           = 1;              /* (C) Keyboard hook is enabled */
int      test_mode                              = 0;              /* (C) Test mode */
char     uuid[MAX_UUID_LEN]                     = { '\0' };       /* (C) UUID or machine identifier */

int      other_ide_present = 0;                                   /* IDE controllers from non-IDE cards are
                                                                     present */
int      other_scsi_present = 0;                                  /* SCSI controllers from non-SCSI cards are
                                                                     present */

/* Statistics. */
extern int mmuflush;
extern int readlnum;
extern int writelnum;

/* emulator % */
int fps;
int framecount;

extern int CPUID;
extern int output;
int        atfullspeed;

char  exe_path[2048]; /* path (dir) of executable */
char  usr_path[1024]; /* path (dir) of user data */
char  cfg_path[1024]; /* full path of config file */
FILE *stdlog = NULL;  /* file to log output to */
#if 0
int   scrnsz_x = SCREEN_RES_X; /* current screen size, X */
int   scrnsz_y = SCREEN_RES_Y; /* current screen size, Y */
#endif
int config_changed; /* config has changed */
int title_update;
int framecountx        = 0;
int hard_reset_pending = 0;

#if 0
int unscaled_size_x = SCREEN_RES_X; /* current unscaled size X */
int unscaled_size_y = SCREEN_RES_Y; /* current unscaled size Y */
int efscrnsz_y = SCREEN_RES_Y;
#endif

static wchar_t mouse_msg[3][200];

static volatile atomic_int do_pause_ack = 0;
static volatile atomic_int pause_ack = 0;

#ifndef RELEASE_BUILD

#define LOG_SIZE_BUFFER 1024            /* Log size buffer */

static char buff[LOG_SIZE_BUFFER];

static int seen = 0;

static int suppr_seen = 1;

// Functions only used in this translation unit
void pclog_ensure_stdlog_open(void);
#endif

/* 
    Ensures STDLOG is open for pclog_ex and pclog_ex_cyclic
*/
void pclog_ensure_stdlog_open(void)
{
#ifndef RELEASE_BUILD
    if (stdlog == NULL) {
        if (log_path[0] != '\0') {
            stdlog = plat_fopen(log_path, "w");
            if (stdlog == NULL)
                stdlog = stdout;
        } else
            stdlog = stdout;
    }
#endif
}

/*
 * Log something to the logfile or stdout.
 *
 * To avoid excessively-large logfiles because some
 * module repeatedly logs, we keep track of what is
 * being logged, and catch repeating entries.
 */
void
pclog_ex(UNUSED(const char *fmt), UNUSED(va_list ap))
{
#ifndef RELEASE_BUILD
    char temp[LOG_SIZE_BUFFER];

    if (strcmp(fmt, "") == 0)
        return;

    pclog_ensure_stdlog_open();

    vsprintf(temp, fmt, ap);
    if (suppr_seen && !strcmp(buff, temp))
        seen++;
    else {
        if (suppr_seen && seen)
            fprintf(stdlog, "*** %d repeats ***\n", seen);
        seen = 0;
        strcpy(buff, temp);
        fprintf(stdlog, "%s", temp);
    }

    fflush(stdlog);
#endif
}



void
pclog_toggle_suppr(void)
{
#ifndef RELEASE_BUILD
    suppr_seen ^= 1;
#endif
}

/* Log something. We only do this in non-release builds. */
void
pclog(UNUSED(const char *fmt), ...)
{
#ifndef RELEASE_BUILD
    va_list ap;

    va_start(ap, fmt);
    pclog_ex(fmt, ap);
    va_end(ap);
#endif
}

/* Log a fatal error, and display a UI message before exiting. */
void
fatal(const char *fmt, ...)
{
    char    temp[1024];
    va_list ap;
    char   *sp;

    va_start(ap, fmt);

    if (stdlog == NULL) {
        if (log_path[0] != '\0') {
            stdlog = plat_fopen(log_path, "w");
            if (stdlog == NULL)
                stdlog = stdout;
        } else
            stdlog = stdout;
    }

    vsprintf(temp, fmt, ap);
    fprintf(stdlog, "%s", temp);
    fflush(stdlog);
    va_end(ap);

    nvr_save();

    config_save();

#ifdef ENABLE_808X_LOG
    dumpregs(1);
#endif

    /* Make sure the message does not have a trailing newline. */
    if ((sp = strchr(temp, '\n')) != NULL)
        *sp = '\0';

    do_pause(2);

    ui_msgbox(MBX_ERROR | MBX_FATAL | MBX_ANSI, temp);

    /* Cleanly terminate all of the emulator's components so as
       to avoid things like threads getting stuck. */
    do_stop();

    fflush(stdlog);

    exit(-1);
}

void
fatal_ex(const char *fmt, va_list ap)
{
    char  temp[1024];
    char *sp;

    if (stdlog == NULL) {
        if (log_path[0] != '\0') {
            stdlog = plat_fopen(log_path, "w");
            if (stdlog == NULL)
                stdlog = stdout;
        } else
            stdlog = stdout;
    }

    vsprintf(temp, fmt, ap);
    fprintf(stdlog, "%s", temp);
    fflush(stdlog);

    nvr_save();

    config_save();

#ifdef ENABLE_808X_LOG
    dumpregs(1);
#endif

    /* Make sure the message does not have a trailing newline. */
    if ((sp = strchr(temp, '\n')) != NULL)
        *sp = '\0';

    do_pause(2);

    ui_msgbox(MBX_ERROR | MBX_FATAL | MBX_ANSI, temp);

    /* Cleanly terminate all of the emulator's components so as
       to avoid things like threads getting stuck. */
    do_stop();

    fflush(stdlog);
}

#ifdef ENABLE_PC_LOG
int pc_do_log = ENABLE_PC_LOG;

static void
pc_log(const char *fmt, ...)
{
    va_list ap;

    if (pc_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define pc_log(fmt, ...)
#endif

static void
delete_nvr_file(uint8_t flash)
{
    char *fn = NULL;
    int c;

    /* Set up the NVR file's name. */
    c       = strlen(machine_get_nvr_name()) + 5;
    fn      = (char *) malloc(c + 1);

    if (fn == NULL)
        fatal("Error allocating memory for the removal of the %s file\n",
              flash ? "BIOS flash" : "CMOS");

    if (flash)
        sprintf(fn, "%s.bin", machine_get_nvr_name());
    else
        sprintf(fn, "%s.nvr", machine_get_nvr_name());

    remove(nvr_path(fn));

    free(fn);
    fn = NULL;
}

extern void  device_find_all_descs(void);

/*
 * Perform initial startup of the PC.
 *
 * This is the platform-indepenent part of the startup,
 * where we check commandline arguments and load a
 * configuration file.
 */
int
pc_init(int argc, char *argv[])
{
    char            *ppath = NULL;
    char            *rpath = NULL;
    char            *cfg = NULL;
    char            *p;
    char             temp[2048];
    char            *fn[FDD_NUM] = { NULL };
    char             drive = 0;
    char            *temp2 = NULL;
    char            *what;
    const struct tm *info;
    time_t           now;
    int              c;
    int              lvmp = 0;
#ifdef ENABLE_NG
    int ng = 0;
#endif
#ifdef _WIN32
    uint32_t *uid;
    uint32_t *shwnd;
#endif
    uint32_t lang_init = 0;

    /* Grab the executable's full path. */
    plat_get_exe_name(exe_path, sizeof(exe_path) - 1);
    p  = path_get_filename(exe_path);
    *p = '\0';
#if defined(__APPLE__)
    c = strlen(exe_path);
    if ((c >= 16) && !strcmp(&exe_path[c - 16], "/Contents/MacOS/")) {
        exe_path[c - 16] = '\0';
        p                = path_get_filename(exe_path);
        *p               = '\0';
    }
    if (!strncmp(exe_path, "/private/var/folders/", 21)) {
        ui_msgbox_header(MBX_FATAL, L"App Translocation", EMU_NAME_W L" cannot determine the emulated machine's location due to a macOS security feature. Please move the " EMU_NAME_W L" app to another folder (not /Applications), or make a copy of it and open that copy instead.");
        return 0;
    }
#elif !defined(_WIN32)
    /* Grab the actual path if we are an AppImage. */
    p = getenv("APPIMAGE");
    if (p && (p[0] != '\0'))
        path_get_dirname(exe_path, p);
#endif

    path_slash(exe_path);

    /*
     * Get the current working directory.
     *
     * This is normally the directory from where the
     * program was run. If we have been started via
     * a shortcut (desktop icon), however, the CWD
     * could have been set to something else.
     */
    plat_getcwd(usr_path, sizeof(usr_path) - 1);
    plat_getcwd(rom_path, sizeof(rom_path) - 1);

    for (c = 1; c < argc; c++) {
        if (argv[c][0] != '-')
            break;

        if (!strcasecmp(argv[c], "--help") || !strcasecmp(argv[c], "-?")) {
usage:
            for (uint8_t i = 0; i < FDD_NUM; i++) {
                if (fn[i] != NULL) {
                    free(fn[i]);
                    fn[i] = NULL;
                }
            }

            printf("\nUsage: 86box [options] [cfg-file]\n\n");
            printf("Valid options are:\n\n");
            printf("-? or --help            - show this information\n");
            printf("-C or --config path     - set 'path' to be config file\n");
#ifdef _WIN32
            printf("-D or --debug           - force debug output logging\n");
#endif
#if 0
            printf("-E or --nographic       - forces the old behavior\n");
#endif
            printf("-F or --fullscreen      - start in fullscreen mode\n");
            printf("-G or --lang langid     - start with specified language (e.g. en-US, or system)\n");
#ifdef _WIN32
            printf("-H or --hwnd id,hwnd    - sends back the main dialog's hwnd\n");
#endif
            printf("-I or --image d:path    - load 'path' as floppy image on drive d\n");
#ifdef USE_INSTRUMENT
            printf("-J or --instrument name - set 'name' to be the profiling instrument\n");
#endif
            printf("-K or --keycodes codes  - set 'codes' to be the uncapture combination\n");
            printf("-L or --logfile path    - set 'path' to be the logfile\n");
            printf("-M or --missing         - dump missing machines and video cards\n");
            printf("-N or --noconfirm       - do not ask for confirmation on quit\n");
            printf("-P or --vmpath path     - set 'path' to be root for vm\n");
            printf("-R or --rompath path    - set 'path' to be ROM path\n");
#ifndef USE_SDL_UI
            printf("-S or --settings        - show only the settings dialog\n");
#endif
            printf("-T or --testmode        - test mode: execute the test mode entry point on init/hard reset\n");
            printf("-V or --vmname name     - overrides the name of the running VM\n");
            printf("-W or --nohook          - disables keyboard hook (compatibility-only outside Windows)\n");
            printf("-X or --clear what      - clears the 'what' (cmos/flash/both)\n");
            printf("-Y or --donothing       - do not show any UI or run the emulation\n");
            printf("-Z or --lastvmpath      - the last parameter is VM path rather than config\n");
            printf("\nA config file can be specified. If none is, the default file will be used.\n");
            return 0;
        } else if (!strcasecmp(argv[c], "--lastvmpath") || !strcasecmp(argv[c], "-Z")) {
            lvmp = 1;
#ifdef _WIN32
        } else if (!strcasecmp(argv[c], "--debug") || !strcasecmp(argv[c], "-D")) {
            force_debug = 1;
#endif
#ifdef ENABLE_NG
        } else if (!strcasecmp(argv[c], "--nographic") || !strcasecmp(argv[c], "-E")) {
            /* Currently does nothing, but if/when we implement a built-in manager,
               it's going to force the manager not to run, allowing the old usage
               without parameter. */
            ng = 1;
#endif
        } else if (!strcasecmp(argv[c], "--fullscreen") || !strcasecmp(argv[c], "-F")) {
            start_in_fullscreen = 1;
        } else if (!strcasecmp(argv[c], "--logfile") || !strcasecmp(argv[c], "-L")) {
            if ((c + 1) == argc)
                goto usage;

            strcpy(log_path, argv[++c]);
        } else if (!strcasecmp(argv[c], "--vmpath") || !strcasecmp(argv[c], "-P")) {
            if ((c + 1) == argc)
                goto usage;

            ppath = argv[++c];
        } else if (!strcasecmp(argv[c], "--rompath") || !strcasecmp(argv[c], "-R")) {
            if ((c + 1) == argc)
                goto usage;

            rpath = argv[++c];
            rom_add_path(rpath);
        } else if (!strcasecmp(argv[c], "--config") || !strcasecmp(argv[c], "-C")) {
            if ((c + 1) == argc || plat_dir_check(argv[c + 1]))
                goto usage;

            cfg = argv[++c];
        } else if (!strcasecmp(argv[c], "--image") || !strcasecmp(argv[c], "-I")) {
            if ((c + 1) == argc)
                goto usage;

            temp2 = (char *) calloc(2048, 1);
            sscanf(argv[++c], "%c:%s", &drive, temp2);
            if (drive > 0x40)
                drive = (drive & 0x1f) - 1;
            else
                drive = drive & 0x1f;
            if (drive < 0)
                drive = 0;
            if (drive >= FDD_NUM)
                drive = FDD_NUM - 1;
            fn[(int) drive] = (char *) calloc(2048, 1);
            strcpy(fn[(int) drive], temp2);
            pclog("Drive %c: %s\n", drive + 0x41, fn[(int) drive]);
            free(temp2);
            temp2 = NULL;
        } else if (!strcasecmp(argv[c], "--vmname") || !strcasecmp(argv[c], "-V")) {
            if ((c + 1) == argc)
                goto usage;

            strcpy(vm_name, argv[++c]);
#ifndef USE_SDL_UI
        } else if (!strcasecmp(argv[c], "--settings") || !strcasecmp(argv[c], "-S")) {
            settings_only = 1;
#endif
        } else if (!strcasecmp(argv[c], "--testmode") || !strcasecmp(argv[c], "-T")) {
            test_mode = 1;
        } else if (!strcasecmp(argv[c], "--noconfirm") || !strcasecmp(argv[c], "-N")) {
            confirm_exit_cmdl = 0;
        } else if (!strcasecmp(argv[c], "--missing") || !strcasecmp(argv[c], "-M")) {
            dump_missing = 1;
        } else if (!strcasecmp(argv[c], "--donothing") || !strcasecmp(argv[c], "-Y")) {
            do_nothing = 1;
        } else if (!strcasecmp(argv[c], "--nohook") || !strcasecmp(argv[c], "-W")) {
            hook_enabled = 0;
        } else if (!strcasecmp(argv[c], "--keycodes") || !strcasecmp(argv[c], "-K")) {
            if ((c + 1) == argc)
                goto usage;

            sscanf(argv[++c], "%03hX,%03hX,%03hX,%03hX,%03hX,%03hX",
                   &key_prefix_1_1, &key_prefix_1_2, &key_prefix_2_1, &key_prefix_2_2,
                   &key_uncapture_1, &key_uncapture_2);
        } else if (!strcasecmp(argv[c], "--clearboth") || !strcasecmp(argv[c], "-X")) {
            if ((c + 1) == argc)
                goto usage;

            what = argv[++c];

            if (!strcasecmp(what, "cmos"))
                clear_cmos = 1;
            else if (!strcasecmp(what, "flash"))
                clear_flash = 1;
            else if (!strcasecmp(what, "both")) {
                clear_cmos = 1;
                clear_flash = 1;
            } else
                goto usage;
#ifdef _WIN32
        } else if (!strcasecmp(argv[c], "--hwnd") || !strcasecmp(argv[c], "-H")) {

            if ((c + 1) == argc)
                goto usage;

            uid   = (uint32_t *) &unique_id;
            shwnd = (uint32_t *) &source_hwnd;
            sscanf(argv[++c], "%08X%08X,%08X%08X", uid + 1, uid, shwnd + 1, shwnd);
#endif
        } else if (!strcasecmp(argv[c], "--lang") || !strcasecmp(argv[c], "-G")) {
            // This function is currently unimplemented for *nix but has placeholders.

            lang_init = plat_language_code(argv[++c]);
            if (!lang_init)
                printf("\nWarning: Invalid language code, ignoring --lang parameter.\n\n");

            // The return value of 0 only means that the code is invalid,
            //   not related to that translation is exists or not for the
            //  selected language.
        } else if (!strcasecmp(argv[c], "--test") || !strcasecmp(argv[c], "-T")) {
            /* some (undocumented) test function here.. */

            /* .. and then exit. */
            return 0;
#ifdef USE_INSTRUMENT
        } else if (!strcasecmp(argv[c], "--instrument") || !strcasecmp(argv[c], "-J")) {
            if ((c + 1) == argc)
                goto usage;
            instru_enabled = 1;
            sscanf(argv[++c], "%llu", &instru_run_ms);
#endif
        }

        /* Uhm... out of options here.. */
        else
            goto usage;
    }

    /* One argument (config file) allowed. */
    if (c < argc) {
        if (lvmp)
            ppath = argv[c++];
        else
            cfg = argv[c++];
    }

    if (c != argc)
        goto usage;

    path_slash(usr_path);
    path_slash(rom_path);

    /*
     * If the user provided a path for files, use that
     * instead of the current working directory. We do
     * make sure that if that was a relative path, we
     * make it absolute.
     */
    if (ppath != NULL) {
        if (!path_abs(ppath)) {
            /*
             * This looks like a relative path.
             *
             * Add it to the current working directory
             * to convert it (back) to an absolute path.
             */
            strcat(usr_path, ppath);
        } else {
            /*
             * The user-provided path seems like an
             * absolute path, so just use that.
             */
            strcpy(usr_path, ppath);
        }

        /* If the specified path does not yet exist,
           create it. */
        if (!plat_dir_check(usr_path))
            plat_dir_create(usr_path);
    }

    // Add the VM-local ROM path.
    path_append_filename(temp, usr_path, "roms");
    rom_add_path(temp);

    // Add the standard ROM path in the same directory as the executable.
    path_append_filename(temp, exe_path, "roms");
    rom_add_path(temp);

    plat_init_rom_paths();

    /*
     * If the user provided a path for ROMs, use that
     * instead of the current working directory. We do
     * make sure that if that was a relative path, we
     * make it absolute.
     */
    if (rpath != NULL) {
        if (!path_abs(rpath)) {
            /*
             * This looks like a relative path.
             *
             * Add it to the current working directory
             * to convert it (back) to an absolute path.
             */
            strcat(rom_path, rpath);
        } else {
            /*
             * The user-provided path seems like an
             * absolute path, so just use that.
             */
            strcpy(rom_path, rpath);
        }

        /* If the specified path does not yet exist,
           create it. */
        if (!plat_dir_check(rom_path))
            plat_dir_create(rom_path);
    } else
        rom_path[0] = '\0';

    /* Grab the name of the configuration file. */
    if (cfg == NULL)
        cfg = CONFIG_FILE;

    /*
     * If the configuration file name has (part of)
     * a pathname, consider that to be part of the
     * actual working directory.
     *
     * This can happen when people load a config
     * file using the UI, for example.
     */
    p = path_get_filename(cfg);
    if (cfg != p) {
        /*
         * OK, the configuration file name has a
         * path component. Separate the two, and
         * add the path component to the cfg path.
         */
        *(p - 1) = '\0';

        /*
         * If this is an absolute path, keep it, as
         * there is probably have a reason to do so.
         * Otherwise, assume the pathname given is
         * relative to whatever the usr_path is.
         */
        if (path_abs(cfg))
            strcpy(usr_path, cfg);
        else
            strcat(usr_path, cfg);
    }

    /* Make sure we have a trailing backslash. */
    path_slash(usr_path);
    if (rom_path[0] != '\0')
        path_slash(rom_path);

    /* At this point, we can safely create the full path name. */
    path_append_filename(cfg_path, usr_path, p);

    /*
     * Get the current directory's name
     *
     * At this point usr_path is perfectly initialized.
     * If no --vmname parameter specified we'll use the
     *   working directory name as the VM's name.
     */
    if (strlen(vm_name) == 0) {
        char ltemp[1024] = { '\0' };
        path_get_dirname(ltemp, usr_path);
        strcpy(vm_name, path_get_filename(ltemp));
    }

    /*
     * This is where we start outputting to the log file,
     * if there is one. Create a little info header first.
     */
    (void) time(&now);
    info = localtime(&now);
    strftime(temp, sizeof(temp), "%Y/%m/%d %H:%M:%S", info);
    pclog("#\n# %ls v%ls logfile, created %s\n#\n",
          EMU_NAME_W, EMU_VERSION_FULL_W, temp);
    pclog("# VM: %s\n#\n", vm_name);
    pclog("# Emulator path: %s\n", exe_path);
    pclog("# Userfiles path: %s\n", usr_path);
    for (rom_path_t *rom_path = &rom_paths; rom_path != NULL; rom_path = rom_path->next) {
        pclog("# ROM path: %s\n", rom_path->path);
    }

    pclog("# Configuration file: %s\n#\n\n", cfg_path);
    /*
     * We are about to read the configuration file, which MAY
     * put data into global variables (the hard- and floppy
     * disks are an example) so we have to initialize those
     * modules before we load the config..
     */
    hdd_init();
    network_init();
    mouse_init();
    cdrom_global_init();
    zip_global_init();
    mo_global_init();

    /* Load the configuration file. */
    config_load();

    /* Clear the CMOS and/or BIOS flash file, if we were started with
       the relevant parameter(s). */
    if (clear_cmos) {
        delete_nvr_file(0);
        clear_cmos = 0;
    }

    if (clear_flash) {
        delete_nvr_file(1);
        clear_flash = 0;
    }

    for (uint8_t i = 0; i < FDD_NUM; i++) {
        if (fn[i] != NULL) {
            if (strlen(fn[i]) <= 511)
                strncpy(floppyfns[i], fn[i], 511);
            free(fn[i]);
            fn[i] = NULL;
        }
    }

    /* Load the desired language */
    if (lang_init)
        lang_id = lang_init;

    gdbstub_init();

    /* All good! */
    return 1;
}

void
pc_speed_changed(void)
{
    if (cpu_s->cpu_type >= CPU_286)
        pit_set_clock(cpu_s->rspeed);
    else
        pit_set_clock((uint32_t) 14318184.0);
}

void
pc_full_speed(void)
{
    if (!atfullspeed) {
        pc_log("Set fullspeed - %i %i\n", is386, is486);
        pc_speed_changed();
    }
    atfullspeed = 1;
}

/* Initialize modules, ran once, after pc_init. */
int
pc_init_modules(void)
{
    int     c;
    int     m;
    wchar_t temp[512];
    char    tempc[512];

    if (dump_missing) {
        dump_missing = 0;

        c = m = 0;
        while (machine_get_internal_name_ex(c) != NULL) {
            m = machine_available(c);
            if (!m)
                pclog("Missing machine: %s\n", machine_getname_ex(c));
            c++;
        }

        c = m = 0;
        while (video_get_internal_name(c) != NULL) {
            memset(tempc, 0, sizeof(tempc));
            device_get_name(video_card_getdevice(c), 0, tempc);
            if ((c > 1) && !(tempc[0]))
                break;
            m = video_card_available(c);
            if (!m)
                pclog("Missing video card: %s\n", tempc);
            c++;
        }
    }

    pc_log("Scanning for ROM images:\n");
    c = m = 0;
    while (machine_get_internal_name_ex(m) != NULL) {
        c += machine_available(m);
        m++;
    }
    if (c == 0) {
        /* No usable ROMs found, aborting. */
        return 0;
    }
    pc_log("A total of %d ROM sets have been loaded.\n", c);

    /* Load the ROMs for the selected machine. */
    if (!machine_available(machine)) {
        swprintf(temp, sizeof_w(temp), plat_get_string(STRING_HW_NOT_AVAILABLE_MACHINE), machine_getname());
        c       = 0;
        machine = -1;
        while (machine_get_internal_name_ex(c) != NULL) {
            if (machine_available(c)) {
                ui_msgbox_header(MBX_INFO, plat_get_string(STRING_HW_NOT_AVAILABLE_TITLE), temp);
                machine = c;
                config_save();
                break;
            }
            c++;
        }
        if (machine == -1) {
            fatal("No available machines\n");
            exit(-1);
        }
    }

    /* Make sure we have a usable video card. */
    if (!video_card_available(gfxcard[0])) {
        memset(tempc, 0, sizeof(tempc));
        device_get_name(video_card_getdevice(gfxcard[0]), 0, tempc);
        swprintf(temp, sizeof_w(temp), plat_get_string(STRING_HW_NOT_AVAILABLE_VIDEO), tempc);
        c = 0;
        while (video_get_internal_name(c) != NULL) {
            gfxcard[0] = -1;
            if (video_card_available(c)) {
                ui_msgbox_header(MBX_INFO, plat_get_string(STRING_HW_NOT_AVAILABLE_TITLE), temp);
                gfxcard[0] = c;
                config_save();
                break;
            }
            c++;
        }
        if (gfxcard[0] == -1) {
            fatal("No available video cards\n");
            exit(-1);
        }
    }

    // TODO
    for (uint8_t i = 1; i < GFXCARD_MAX; i ++) {
        if (!video_card_available(gfxcard[i])) {
            char tempc[512] = { 0 };
            device_get_name(video_card_getdevice(gfxcard[i]), 0, tempc);
            swprintf(temp, sizeof_w(temp), plat_get_string(STRING_HW_NOT_AVAILABLE_VIDEO2), tempc);
            ui_msgbox_header(MBX_INFO, plat_get_string(STRING_HW_NOT_AVAILABLE_TITLE), temp);
            gfxcard[i] = 0;
        }
    }

    atfullspeed = 0;

    random_init();

    mem_init();

#ifdef USE_DYNAREC
#    if defined(__APPLE__) && defined(__aarch64__)
    if (__builtin_available(macOS 11.0, *)) {
        pthread_jit_write_protect_np(0);
    }
#    endif
    codegen_init();
#    if defined(__APPLE__) && defined(__aarch64__)
    if (__builtin_available(macOS 11.0, *)) {
        pthread_jit_write_protect_np(1);
    }
#    endif
#endif

    keyboard_init();
    joystick_init();

    video_init();

    fdd_init();

    sound_init();

    hdc_init();

    video_reset_close();

    machine_status_init();

    if (do_nothing) {
        do_nothing = 0;
        exit(-1);
    }

    return 1;
}

void
pc_send_ca(uint16_t sc)
{
    keyboard_input(1, 0x1D); /* Ctrl key pressed */
    keyboard_input(1, 0x38); /* Alt key pressed */
    keyboard_input(1, sc);
    usleep(50000);
    keyboard_input(0, sc);
    keyboard_input(0, 0x38); /* Alt key released */
    keyboard_input(0, 0x1D); /* Ctrl key released */
}

/* Send the machine a Control-Alt-DEL sequence. */
void
pc_send_cad(void)
{
    pc_send_ca(0x153);
}

/* Send the machine a Control-Alt-ESC sequence. */
void
pc_send_cae(void)
{
    pc_send_ca(1);
}

/*
   Currently available API:

   extern void     resetx86(void);
   extern void     softresetx86(void);
   extern void     hardresetx86(void);

   extern void     prefetch_queue_set_pos(int pos);
   extern void     prefetch_queue_set_ip(uint16_t ip);
   extern void     prefetch_queue_set_prefetching(int p);
   extern int      prefetch_queue_get_pos(void);
   extern uint16_t prefetch_queue_get_ip(void);
   extern int      prefetch_queue_get_prefetching(void);
   extern int      prefetch_queue_get_size(void);
 */
static void
pc_test_mode_entry_point(void)
{
    pclog("Test mode entry point\n=====================\n");
}

void
pc_reset_hard_close(void)
{
    ui_sb_set_ready(0);

    /* Close all the memory mappings. */
    mem_close();

    suppress_overscan = 0;

    /* Turn off timer processing to avoid potential segmentation faults. */
    timer_close();

    lpt_devices_close();

#ifdef UNCOMMENT_LATER
    lpt_close();
#endif

    nvr_save();
    nvr_close();

    mouse_close();

    device_close_all();

    scsi_device_close_all();

    midi_out_close();

    midi_in_close();

    cdrom_close();

    zip_close();

    mo_close();

    scsi_disk_close();

    closeal();

    video_reset_close();

    cpu_close();

    serial_set_next_inst(0);
}

/*
 * This is basically the spot where we start up the actual machine,
 * by issuing a 'hard reset' to the entire configuration. Order is
 * somewhat important here. Functions here should be named _reset
 * really, as that is what they do.
 */
void
pc_reset_hard_init(void)
{
    /*
     * First, we reset the modules that are not part of
     * the actual machine, but which support some of the
     * modules that are.
     */

    /* Reset the IDE and SCSI presences */
    other_ide_present = other_scsi_present = 0;

    /* Mark ACPI as unavailable */
    acpi_enabled = 0;

    /* Reset the general machine support modules. */
    io_init();

    /* Turn on and (re)initialize timer processing. */
    timer_init();

    device_init();

    sound_reset();

    scsi_reset();
    scsi_device_init();

    /* Initialize the actual machine and its basic modules. */
    machine_init();

    /* Reset some basic devices. */
    speaker_init();
    shadowbios = 0;

    /*
     * Once the machine has been initialized, all that remains
     * should be resetting all devices set up for it, to their
     * current configurations !
     *
     * For now, we will call their reset functions here, but
     * that will be a call to device_reset_all() later !
     */

    /* Reset and reconfigure the Sound Card layer. */
    sound_card_reset();

    /* Initialize parallel devices. */
    /* note: PLIP LPT side has to be initialized before the network side */
    lpt_devices_init();

    /* Reset and reconfigure the serial ports. */
    /* note: SLIP COM side has to be initialized before the network side */
    serial_standalone_init();
    serial_passthrough_init();

    /* Reset and reconfigure the Network Card layer. */
    network_reset();

    /*
     * Reset the mouse, this will attach it to any port needed.
     */
    mouse_reset();

    /* Reset the Hard Disk Controller module. */
    hdc_reset();

    fdc_card_init();

    fdd_reset();

    /* Reset the CD-ROM Controller module. */
    cdrom_interface_reset();

    /* Reset and reconfigure the SCSI layer. */
    scsi_card_init();

    scsi_disk_hard_reset();

    cdrom_hard_reset();

    mo_hard_reset();

    zip_hard_reset();

    /* Reset any ISA RTC cards. */
    isartc_reset();

    /* Initialize the Voodoo cards here inorder to minimize
       the chances of the SCSI controller ending up on the bridge. */
    video_voodoo_init();

    if (joystick_type)
        gameport_update_joystick_type(); /* installs game port if no device provides one, must be late */

    ui_sb_update_panes();

    if (config_changed) {
        config_save();

        config_changed = 0;
    } else
        ui_sb_set_ready(1);

    /* Needs the status bar... */
    if (bugger_enabled)
        device_add(&bugger_device);
    if (postcard_enabled)
        device_add(&postcard_device);
    if (unittester_enabled)
        device_add(&unittester_device);

    if (lba_enhancer_enabled)
        device_add(&lba_enhancer_device);

    if (novell_keycard_enabled)
        device_add(&novell_keycard_device);

    if (IS_ARCH(machine, MACHINE_BUS_PCI)) {
        pci_register_cards();
        device_reset_all(DEVICE_PCI);
    }

    /* Mark IDE shadow drives (slaves with a present master) as such in case
       the IDE controllers present are not some form of PCI. */
    ide_drives_set_shadow();

    /* Reset the CPU module. */
    resetx86();
    dma_reset();
    pci_pic_reset();
    cpu_cache_int_enabled = cpu_cache_ext_enabled = 0;

    atfullspeed = 0;
    pc_full_speed();

    cycles = 0;
#ifdef FPU_CYCLES
    fpu_cycles = 0;
#endif
#ifdef USE_DYNAREC
    cycles_main = 0;
#endif

    update_mouse_msg();

    if (test_mode)
        pc_test_mode_entry_point();

    ui_hard_reset_completed();
}

void
update_mouse_msg(void)
{
    wchar_t  wcpufamily[2048];
    wchar_t  wcpu[2048];
    wchar_t  wmachine[2048];
    wchar_t *wcp;

    mbstowcs(wmachine, machine_getname(), strlen(machine_getname()) + 1);

    if (!cpu_override)
        mbstowcs(wcpufamily, cpu_f->name, strlen(cpu_f->name) + 1);
    else
        swprintf(wcpufamily, sizeof_w(wcpufamily), L"[U] %hs", cpu_f->name);

    wcp = wcschr(wcpufamily, L'(');
    if (wcp) /* remove parentheses */
        *(wcp - 1) = L'\0';
    mbstowcs(wcpu, cpu_s->name, strlen(cpu_s->name) + 1);
#ifdef _WIN32
    swprintf(mouse_msg[0], sizeof_w(mouse_msg[0]), L"%%i%%%% - %ls",
             plat_get_string(STRING_MOUSE_CAPTURE));
    swprintf(mouse_msg[1], sizeof_w(mouse_msg[1]), L"%%i%%%% - %ls",
             (mouse_get_buttons() > 2) ? plat_get_string(STRING_MOUSE_RELEASE) : plat_get_string(STRING_MOUSE_RELEASE_MMB));
    wcsncpy(mouse_msg[2], L"%i%%", sizeof_w(mouse_msg[2]));
#else
    swprintf(mouse_msg[0], sizeof_w(mouse_msg[0]), L"%ls v%ls - %%i%%%% - %ls - %ls/%ls - %ls",
             EMU_NAME_W, EMU_VERSION_FULL_W, wmachine, wcpufamily, wcpu,
             plat_get_string(STRING_MOUSE_CAPTURE));
    swprintf(mouse_msg[1], sizeof_w(mouse_msg[1]), L"%ls v%ls - %%i%%%% - %ls - %ls/%ls - %ls",
             EMU_NAME_W, EMU_VERSION_FULL_W, wmachine, wcpufamily, wcpu,
             (mouse_get_buttons() > 2) ? plat_get_string(STRING_MOUSE_RELEASE) : plat_get_string(STRING_MOUSE_RELEASE_MMB));
    swprintf(mouse_msg[2], sizeof_w(mouse_msg[2]), L"%ls v%ls - %%i%%%% - %ls - %ls/%ls",
             EMU_NAME_W, EMU_VERSION_FULL_W, wmachine, wcpufamily, wcpu);
#endif
}

void
pc_reset_hard(void)
{
    hard_reset_pending = 1;
}

void
pc_close(UNUSED(thread_t *ptr))
{
    /* Wait a while so things can shut down. */
    plat_delay_ms(200);

    /* Claim the video blitter. */
    startblit();

    /* Terminate the UI thread. */
    is_quit = 1;

    nvr_save();

    config_save();

    plat_mouse_capture(0);

    /* Close all the memory mappings. */
    mem_close();

    /* Turn off timer processing to avoid potential segmentation faults. */
    timer_close();

    lpt_devices_close();

    for (uint8_t i = 0; i < FDD_NUM; i++)
        fdd_close(i);

#ifdef ENABLE_808X_LOG
    if (dump_on_exit)
        dumpregs(0);
#endif

    video_close();

    device_close_all();

    scsi_device_close_all();

    midi_out_close();

    midi_in_close();

    network_close();

    sound_cd_thread_end();

    cdrom_close();

    zip_close();

    mo_close();

    scsi_disk_close();

    gdbstub_close();
}

#ifdef __APPLE__
static void
_ui_window_title(void *s)
{
    ui_window_title((wchar_t *) s);
    free(s);
}
#endif

void
ack_pause(void)
{
    if (atomic_load(&do_pause_ack)) {
        atomic_store(&do_pause_ack, 0);
        atomic_store(&pause_ack, 1);
    }
}

void
pc_run(void)
{
    int     mouse_msg_idx;
    wchar_t temp[200];

    /* Trigger a hard reset if one is pending. */
    if (hard_reset_pending) {
        hard_reset_pending = 0;
        pc_reset_hard_close();
        pc_reset_hard_init();
    }

    /* Update the guest-CPU independent timer for devices with independent clock speed */
    rivatimer_update_all();

    /* Run a block of code. */
    startblit();
    cpu_exec((int32_t) cpu_s->rspeed / 100);
    ack_pause();
#ifdef USE_GDBSTUB /* avoid a KBC FIFO overflow when CPU emulation is stalled */
    if (gdbstub_step == GDBSTUB_EXEC) {
#endif
        if (!mouse_timed)
            mouse_process();
#ifdef USE_GDBSTUB /* avoid a KBC FIFO overflow when CPU emulation is stalled */
    }
#endif
    joystick_process();
    endblit();

    /* Done with this frame, update statistics. */
    framecount++;
    if (++framecountx >= 100) {
        framecountx = 0;
        frames      = 0;
    }

    if (title_update) {
        mouse_msg_idx = ((mouse_type == MOUSE_TYPE_NONE) || (mouse_input_mode >= 1)) ? 2 : !!mouse_capture;
        swprintf(temp, sizeof_w(temp), mouse_msg[mouse_msg_idx], fps);
#ifdef __APPLE__
        /* Needed due to modifying the UI on the non-main thread is a big no-no. */
        dispatch_async_f(dispatch_get_main_queue(), wcsdup((const wchar_t *) temp), _ui_window_title);
#else
        ui_window_title(temp);
#endif
        title_update = 0;
    }
}

/* Handler for the 1-second timer to refresh the window title. */
void
pc_onesec(void)
{
    fps        = framecount;
    framecount = 0;

    title_update = 1;
}

void
set_screen_size_monitor(int x, int y, int monitor_index)
{
    int    temp_overscan_x = monitors[monitor_index].mon_overscan_x;
    int    temp_overscan_y = monitors[monitor_index].mon_overscan_y;
    double dx;
    double dy;
    double dtx;
    double dty;

    /* Make sure we keep usable values. */
#if 0
    pc_log("SetScreenSize(%d, %d) resize=%d\n", x, y, vid_resize);
#endif
    if (x < 320)
        x = 320;
    if (y < 200)
        y = 200;
    if (x > 2048)
        x = 2048;
    if (y > 2048)
        y = 2048;

    /* Save the new values as "real" (unscaled) resolution. */
    monitors[monitor_index].mon_unscaled_size_x = x;
    monitors[monitor_index].mon_efscrnsz_y      = y;

    if (suppress_overscan)
        temp_overscan_x = temp_overscan_y = 0;

    if (force_43) {
        dx  = (double) x;
        dtx = (double) temp_overscan_x;

        dy  = (double) y;
        dty = (double) temp_overscan_y;

        /* Account for possible overscan. */
        if (video_get_type_monitor(monitor_index) != VIDEO_FLAG_TYPE_SPECIAL && (temp_overscan_y == 16)) {
            /* CGA */
            dy = (((dx - dtx) / 4.0) * 3.0) + dty;
        } else if (video_get_type_monitor(monitor_index) != VIDEO_FLAG_TYPE_SPECIAL && (temp_overscan_y < 16)) {
            /* MDA/Hercules */
            dy = (x / 4.0) * 3.0;
        } else {
            if (enable_overscan) {
                /* EGA/(S)VGA with overscan */
                dy = (((dx - dtx) / 4.0) * 3.0) + dty;
            } else {
                /* EGA/(S)VGA without overscan */
                dy = (x / 4.0) * 3.0;
            }
        }
        monitors[monitor_index].mon_unscaled_size_y = (int) dy;
    } else
        monitors[monitor_index].mon_unscaled_size_y = monitors[monitor_index].mon_efscrnsz_y;

    switch (scale) {
        case 0: /* 50% */
            monitors[monitor_index].mon_scrnsz_x = (monitors[monitor_index].mon_unscaled_size_x >> 1);
            monitors[monitor_index].mon_scrnsz_y = (monitors[monitor_index].mon_unscaled_size_y >> 1);
            break;

        case 1: /* 100% */
            monitors[monitor_index].mon_scrnsz_x = monitors[monitor_index].mon_unscaled_size_x;
            monitors[monitor_index].mon_scrnsz_y = monitors[monitor_index].mon_unscaled_size_y;
            break;

        case 2: /* 150% */
            monitors[monitor_index].mon_scrnsz_x = ((monitors[monitor_index].mon_unscaled_size_x * 3) >> 1);
            monitors[monitor_index].mon_scrnsz_y = ((monitors[monitor_index].mon_unscaled_size_y * 3) >> 1);
            break;

        case 3: /* 200% */
            monitors[monitor_index].mon_scrnsz_x = (monitors[monitor_index].mon_unscaled_size_x << 1);
            monitors[monitor_index].mon_scrnsz_y = (monitors[monitor_index].mon_unscaled_size_y << 1);
            break;

        case 4: /* 300% */
            monitors[monitor_index].mon_scrnsz_x = (monitors[monitor_index].mon_unscaled_size_x * 3);
            monitors[monitor_index].mon_scrnsz_y = (monitors[monitor_index].mon_unscaled_size_y * 3);
            break;

        case 5: /* 400% */
            monitors[monitor_index].mon_scrnsz_x = (monitors[monitor_index].mon_unscaled_size_x << 2);
            monitors[monitor_index].mon_scrnsz_y = (monitors[monitor_index].mon_unscaled_size_y << 2);
            break;

        case 6: /* 500% */
            monitors[monitor_index].mon_scrnsz_x = (monitors[monitor_index].mon_unscaled_size_x * 5);
            monitors[monitor_index].mon_scrnsz_y = (monitors[monitor_index].mon_unscaled_size_y * 5);
            break;

        case 7: /* 600% */
            monitors[monitor_index].mon_scrnsz_x = (monitors[monitor_index].mon_unscaled_size_x * 6);
            monitors[monitor_index].mon_scrnsz_y = (monitors[monitor_index].mon_unscaled_size_y * 6);
            break;

        case 8: /* 700% */
            monitors[monitor_index].mon_scrnsz_x = (monitors[monitor_index].mon_unscaled_size_x * 7);
            monitors[monitor_index].mon_scrnsz_y = (monitors[monitor_index].mon_unscaled_size_y * 7);
            break;

        case 9: /* 800% */
            monitors[monitor_index].mon_scrnsz_x = (monitors[monitor_index].mon_unscaled_size_x << 3);
            monitors[monitor_index].mon_scrnsz_y = (monitors[monitor_index].mon_unscaled_size_y << 3);
            break;

        default:
            break;
    }

    plat_resize_request(monitors[monitor_index].mon_scrnsz_x, monitors[monitor_index].mon_scrnsz_y, monitor_index);
}

void
set_screen_size(int x, int y)
{
    set_screen_size_monitor(x, y, monitor_index_global);
}

void
reset_screen_size_monitor(int monitor_index)
{
    set_screen_size(monitors[monitor_index].mon_unscaled_size_x, monitors[monitor_index].mon_efscrnsz_y);
}

void
reset_screen_size(void)
{
    for (uint8_t i = 0; i < MONITORS_NUM; i++)
        set_screen_size(monitors[i].mon_unscaled_size_x, monitors[i].mon_efscrnsz_y);
}

void
set_screen_size_natural(void)
{
    for (uint8_t i = 0; i < MONITORS_NUM; i++)
        set_screen_size(monitors[i].mon_unscaled_size_x, monitors[i].mon_unscaled_size_y);
}

int
get_actual_size_x(void)
{
    return (unscaled_size_x);
}

int
get_actual_size_y(void)
{
    return (efscrnsz_y);
}

void
do_pause(int p)
{
    int old_p = dopause;

    if ((p == 1) && !old_p)
        do_pause_ack = p;
    dopause = !!p;
    if ((p == 1) && !old_p) {
        while (!atomic_load(&pause_ack))
            ;
    }
    atomic_store(&pause_ack, 0);
}
