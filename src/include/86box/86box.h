/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Main include file for the application.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2017-2020 Fred N. van Kempen.
 *          Copyright 2021 Laci bá'
 *          Copyright 2021-2025 Jasmine Iwanek.
 */
#ifndef EMU_86BOX_H
#define EMU_86BOX_H

/* Configuration values. */
#define GFXCARD_MAX  2
#define SERIAL_MAX   7
#define PARALLEL_MAX 4
#define SCREEN_RES_X 640
#define SCREEN_RES_Y 480

/* Filename and pathname info. */
#define CONFIG_FILE     "86box.cfg"
#define NVR_PATH        "nvr"
#define SCREENSHOT_PATH "screenshots"

/* Recently used images */
#define MAX_PREV_IMAGES    10
#define MAX_IMAGE_PATH_LEN 2048

/* Max UUID Length */
#define MAX_UUID_LEN 64

/* Default language 0xFFFF = from system, 0x409 = en-US */
#define DEFAULT_LANGUAGE 0x0409

#define POSTCARDS_NUM 4
#define POSTCARD_MASK (POSTCARDS_NUM - 1)

#ifdef MIN
#    undef MIN
#endif
#ifdef MAX
#    undef MAX
#endif
#ifdef ABS
#    undef ABS
#endif
#ifdef ABSD
#    undef ABSD
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ABS(x)    ((x) > 0 ? (x) : -(x))
#define ABSD(x)   ((x) > 0.0 ? (x) : -(x))
#define BCD8(x)   ((((x) / 10) << 4) | ((x) % 10))
#define BCD16(x)  ((((x) / 1000) << 12) | (((x) / 100) << 8) | BCD8(x))
#define BCD32(x)  ((((x) / 10000000) << 28) | (((x) / 1000000) << 24) | (((x) / 100000) << 20) | (((x) / 10000) << 16) | BCD16(x))

#if defined(__GNUC__) || defined(__clang__)
#    define UNLIKELY(x) __builtin_expect((x), 0)
#    define LIKELY(x)   __builtin_expect((x), 1)
#else
#    define UNLIKELY(x) (x)
#    define LIKELY(x)   (x)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Global variables. */
extern uint32_t lang_sys; /* (-) system language code */

extern int dump_on_exit;        /* (O) dump regs on exit*/
extern int start_in_fullscreen; /* (O) start in fullscreen */
#ifdef _WIN32
extern int force_debug; /* (O) force debug output */
#endif
#ifdef USE_WX
extern int video_fps; /* (O) render speed in fps */
#endif
extern int settings_only;     /* (O) show only the settings dialog */
extern int confirm_exit_cmdl; /* (O) do not ask for confirmation on quit if set to 0 */
#ifdef _WIN32
extern uint64_t unique_id;
extern uint64_t source_hwnd;
#endif
extern char rom_path[1024]; /* (O) full path to ROMs */
extern char log_path[1024]; /* (O) full path of logfile */
extern char vm_name[1024];  /* (O) display name of the VM */
#ifdef USE_INSTRUMENT
extern uint8_t  instru_enabled;
extern uint64_t instru_run_ms;
#endif

#define window_x monitor_settings[0].mon_window_x
#define window_y monitor_settings[0].mon_window_y
#define window_w monitor_settings[0].mon_window_w
#define window_h monitor_settings[0].mon_window_h
extern int      window_remember;
extern int      vid_resize;                 /* (C) allow resizing */
extern int      invert_display;             /* (C) invert the display */
extern int      suppress_overscan;          /* (C) suppress overscans */
extern uint32_t lang_id;                    /* (C) language code identifier */
extern char     icon_set[256];              /* (C) iconset identifier */
extern int      scale;                      /* (C) screen scale factor */
extern int      dpi_scale;                  /* (C) DPI scaling of the emulated screen */
extern int      vid_api;                    /* (C) video renderer */
extern int      vid_cga_contrast;           /* (C) video */
extern int      video_fullscreen;           /* (C) video */
extern int      video_fullscreen_first;     /* (C) video */
extern int      video_fullscreen_scale;     /* (C) video */
extern int      enable_overscan;            /* (C) video */
extern int      force_43;                   /* (C) video */
extern int      video_filter_method;        /* (C) video */
extern int      video_vsync;                /* (C) video */
extern int      video_framerate;            /* (C) video */
extern int      gfxcard[GFXCARD_MAX];       /* (C) graphics/video card */
extern char     video_shader[512];          /* (C) video */
extern int      bugger_enabled;             /* (C) enable ISAbugger */
extern int      novell_keycard_enabled;     /* (C) enable Novell NetWare 2.x key card emulation. */
extern int      postcard_enabled;           /* (C) enable POST card */
extern int      unittester_enabled;         /* (C) enable unit tester device */
extern int      gameport_type[];            /* (C) enable gameports */
extern int      isamem_type[];              /* (C) enable ISA mem cards */
extern int      isartc_type;                /* (C) enable ISA RTC card */
extern int      sound_is_float;             /* (C) sound uses FP values */
extern int      voodoo_enabled;             /* (C) video option */
extern int      ibm8514_standalone_enabled; /* (C) video option */
extern int      xga_standalone_enabled;     /* (C) video option */
extern uint32_t mem_size;                   /* (C) memory size (Installed on system board) */
extern uint32_t isa_mem_size;               /* (C) memory size (ISA Memory Cards) */
extern int      cpu;                        /* (C) cpu type */
extern int      cpu_use_dynarec;            /* (C) cpu uses/needs Dyna */
extern int      fpu_type;                   /* (C) fpu type */
extern int      fpu_softfloat;              /* (C) fpu uses softfloat */
extern int      time_sync;                  /* (C) enable time sync */
extern int      hdd_format_type;            /* (C) hard disk file format */
extern int      lba_enhancer_enabled;       /* (C) enable Vision Systems LBA Enhancer */
extern int      confirm_reset;              /* (C) enable reset confirmation */
extern int      confirm_exit;               /* (C) enable exit confirmation */
extern int      confirm_save;               /* (C) enable save confirmation */
extern int      enable_discord;             /* (C) enable Discord integration */
extern int      other_ide_present;          /* IDE controllers from non-IDE cards are present */
extern int      other_scsi_present;         /* SCSI controllers from non-SCSI cards are present */

extern int    hard_reset_pending;
extern int    fixed_size_x;
extern int    fixed_size_y;
extern int    do_auto_pause;                /* (C) Auto-pause the emulator on focus loss */
extern int    auto_paused;
extern double mouse_sensitivity;            /* (C) Mouse sensitivity scale */
#ifdef _Atomic
extern _Atomic double mouse_x_error;        /* Mouse error accumulator - Y */
extern _Atomic double mouse_y_error;        /* Mouse error accumulator - Y */
#endif
extern int    pit_mode;                     /* (C) force setting PIT mode */
extern int    fm_driver;                    /* (C) select FM sound driver */
extern int    hook_enabled;                 /* (C) Keyboard hook is enabled */

/* Keyboard variables for future key combination redefinition. */
extern uint16_t key_prefix_1_1;
extern uint16_t key_prefix_1_2;
extern uint16_t key_prefix_2_1;
extern uint16_t key_prefix_2_2;
extern uint16_t key_uncapture_1;
extern uint16_t key_uncapture_2;

extern char exe_path[2048];     /* path (dir) of executable */
extern char usr_path[1024];     /* path (dir) of user data */
extern char cfg_path[1024];     /* full path of config file */
extern int  open_dir_usr_path;  /* default file open dialog directory of usr_path */
extern char uuid[MAX_UUID_LEN]; /* UUID or machine identifier */
#ifndef USE_NEW_DYNAREC
extern FILE *stdlog; /* file to log output to */
#endif
extern int config_changed; /* config has changed */

/* Function prototypes. */
#ifdef HAVE_STDARG_H
extern void pclog_ex(const char *fmt, va_list ap);
extern void fatal_ex(const char *fmt, va_list ap);
#endif
extern void pclog_toggle_suppr(void);
#ifdef _MSC_VER
extern void pclog(const char *fmt, ...);
extern void fatal(const char *fmt, ...);
#else
extern void pclog(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
extern void fatal(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
#endif
extern void set_screen_size(int x, int y);
extern void set_screen_size_monitor(int x, int y, int monitor_index);
extern void reset_screen_size(void);
extern void reset_screen_size_monitor(int monitor_index);
extern void set_screen_size_natural(void);
extern void update_mouse_msg(void);
#if 0
extern void pc_reload(wchar_t *fn);
#endif
extern int  pc_init_modules(void);
extern int  pc_init(int argc, char *argv[]);
extern void pc_close(void *threadid);
extern void pc_reset_hard_close(void);
extern void pc_reset_hard_init(void);
extern void pc_reset_hard(void);
extern void pc_full_speed(void);
extern void pc_speed_changed(void);
extern void pc_send_cad(void);
extern void pc_send_cae(void);
extern void pc_send_cab(void);
extern void pc_run(void);
extern void pc_start(void);
extern void pc_onesec(void);

extern uint16_t get_last_addr(void);

/* This is for external subtraction of cycles;
   should be in cpu.c but I put it here to avoid
   having to include cpu.h everywhere. */
extern void sub_cycles(int c);
extern void resub_cycles(int old_cycles);

extern void ack_pause(void);
extern void do_pause(int p);

extern double isa_timing;
extern int    io_delay;
extern int    framecountx;

extern volatile int     cpu_thread_run;
extern          uint8_t postcard_codes[POSTCARDS_NUM];

#ifdef __cplusplus
}
#endif

#endif /*EMU_86BOX_H*/
