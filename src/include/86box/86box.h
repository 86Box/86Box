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

#if defined(__NetBSD__) || defined(__OpenBSD__)
/* Doesn't compile on NetBSD/OpenBSD without this include */
#include <stdarg.h>
#endif

#if defined(__HAIKU__)
/* Doesn't compile on Haiku without this include */
#include <stdlib.h>
#endif

/* Configuration values. */
#define GFXCARD_MAX  2
#define SERIAL_MAX   8
#define PARALLEL_MAX 4
#define SCREEN_RES_X 640
#define SCREEN_RES_Y 480

/* Filename and pathname info. */
#define CONFIG_FILE        "86box.cfg"
#define GLOBAL_CONFIG_FILE "86box_global.cfg"
#define NVR_PATH           "nvr"
#define SCREENSHOT_PATH    "screenshots"
#define VMM_PATH		   "Virtual Machines"
#define VMM_PATH_WINDOWS   "86Box VMs"

/* Recently used images */
#define MAX_PREV_IMAGES    10
#define MAX_IMAGE_PATH_LEN 2048

/* Max UUID Length */
#define MAX_UUID_LEN 64

/* Default language code */
#define DEFAULT_LANGUAGE "system"

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

#define AS_U8(x)     (*((uint8_t *) &(x)))
#define AS_U16(x)    (*((uint16_t *) &(x)))
#define AS_U32(x)    (*((uint32_t *) &(x)))
#define AS_U64(x)    (*((uint64_t *) &(x)))
#define AS_I8(x)     (*((int8_t *) &(x)))
#define AS_I16(x)    (*((int16_t *) &(x)))
#define AS_I32(x)    (*((int32_t *) &(x)))
#define AS_I64(x)    (*((int64_t *) &(x)))
#define AS_FLOAT(x)  (*((float *) &(x)))
#define AS_DOUBLE(x) (*((double *) &(x)))

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
extern int      inhibit_multimedia_keys;    /* (G) Inhibit multimedia keys on Windows. */
extern int      window_remember;
extern int      vid_resize;                 /* (C) allow resizing */
extern int      invert_display;             /* (C) invert the display */
extern int      suppress_overscan;          /* (C) suppress overscans */
extern int      lang_id;                    /* (G) language id */
extern int      scale;                      /* (C) screen scale factor */
extern int      dpi_scale;                  /* (C) DPI scaling of the emulated screen */
extern int      vid_api;                    /* (C) video renderer */
extern int      vid_cga_contrast;           /* (C) video */
extern int      vid_cga_comp_brightness;    /* (C) CGA composite brightness */
extern int      vid_cga_comp_sharpness;     /* (C) CGA composite sharpness */
extern int      vid_cga_comp_hue;           /* (C) CGA composite hue */
extern int      vid_cga_comp_saturation;    /* (C) CGA composite saturation */
extern int      vid_cga_comp_contrast;      /* (C) CGA composite saturation */
extern int      video_fullscreen;           /* (C) video */
extern int      video_fullscreen_scale;     /* (C) video */
extern int      enable_overscan;            /* (C) video */
extern int      force_43;                   /* (C) video */
extern int      video_filter_method;        /* (C) video */
extern int      video_vsync;                /* (C) video */
extern int      video_framerate;            /* (C) video */
extern double   video_gl_input_scale;       /* (C) OpenGL 3.x input scale */
extern int      video_gl_input_scale_mode;  /* (C) OpenGL 3.x input stretch mode */
extern int      gfxcard[GFXCARD_MAX];       /* (C) graphics/video card */
extern int      bugger_enabled;             /* (C) enable ISAbugger */
extern int      novell_keycard_enabled;     /* (C) enable Novell NetWare 2.x key card emulation. */
extern int      postcard_enabled;           /* (C) enable POST card */
extern int      unittester_enabled;         /* (C) enable unit tester device */
extern int      gameport_type[];            /* (C) enable gameports */
extern int      isamem_type[];              /* (C) enable ISA mem cards */
extern int      isarom_type[];              /* (C) enable ISA ROM cards */
extern int      isartc_type;                /* (C) enable ISA RTC card */
extern int      sound_is_float;             /* (C) sound uses FP values */
extern int      voodoo_enabled;             /* (C) video option */
extern int      ibm8514_standalone_enabled; /* (C) video option */
extern int      xga_standalone_enabled;     /* (C) video option */
extern int      da2_standalone_enabled;     /* (C) video option */
extern uint32_t mem_size;                   /* (C) memory size (Installed on system board) */
extern uint32_t isa_mem_size;               /* (C) memory size (ISA Memory Cards) */
extern int      cpu;                        /* (C) cpu type */
extern int      cpu_use_dynarec;            /* (C) cpu uses/needs Dyna */
extern int      fpu_type;                   /* (C) fpu type */
extern int      fpu_softfloat;              /* (C) fpu uses softfloat */
extern int      time_sync;                  /* (C) enable time sync */
extern int      hdd_format_type;            /* (C) hard disk file format */
extern int      confirm_reset;              /* (G) enable reset confirmation */
extern int      confirm_exit;               /* (G) enable exit confirmation */
extern int      confirm_save;               /* (G) enable save confirmation */
extern int      enable_discord;             /* (C) enable Discord integration */
extern int      force_10ms;                 /* (C) force 10ms CPU frame interval */
extern int      jumpered_internal_ecp_dma;  /* (C) Jumpered internal EPC DMA */
extern int      other_ide_present;          /* IDE controllers from non-IDE cards are present */
extern int      other_scsi_present;         /* SCSI controllers from non-SCSI cards are present */
extern int      is_pcjr;                    /* The current machine is PCjr. */

extern int    hard_reset_pending;
extern int    fixed_size_x;
extern int    fixed_size_y;
extern int    sound_muted;                  /* (C) Is sound muted? */
extern int    do_auto_pause;                /* (C) Auto-pause the emulator on focus loss */
extern int    auto_paused;
extern int    force_constant_mouse;         /* (C) Force constant updating of the mouse */
extern double mouse_sensitivity;            /* (G) Mouse sensitivity scale */
#ifdef _Atomic
extern _Atomic double mouse_x_error;        /* Mouse error accumulator - Y */
extern _Atomic double mouse_y_error;        /* Mouse error accumulator - Y */
#endif
extern int    pit_mode;                     /* (C) force setting PIT mode */
extern int    fm_driver;                    /* (C) select FM sound driver */
extern int    hook_enabled;                 /* (C) Keyboard hook is enabled */
extern int    vmm_disabled;                 /* (G) disable built-in manager */
extern char   vmm_path_cfg[1024];           /* (G) VMs path (unless -E is used) */

extern char exe_path[2048];        /* path (dir) of executable */
extern char usr_path[1024];        /* path (dir) of user data */
extern char cfg_path[1024];        /* full path of config file */
extern char global_cfg_path[1024]; /* full path of global config file */
extern int  open_dir_usr_path;     /* default file open dialog directory of usr_path */
extern char uuid[MAX_UUID_LEN];    /* UUID or machine identifier */
extern char vmm_path[1024];        /* VM Manager path to scan */
extern int  start_vmm;             /* the current execution will start the manager */
extern int  portable_mode;         /* we are running in portable mode 
                                      (global dirs = exe path) */
extern int global_cfg_overridden;  /* global config file was overriden on command line */

extern int  monitor_edid;                   /* (C) Which EDID to use. 0=default, 1=custom. */
extern char monitor_edid_path[1024];        /* (C) Path to custom EDID */

extern int color_scheme;                    /* (C) Color scheme of UI (Windows-only) */
extern int fdd_sounds_enabled;              /* (C) Enable floppy drive sounds */

#ifndef USE_NEW_DYNAREC
extern FILE *stdlog; /* file to log output to */
#endif
extern int config_changed; /* config has changed */

extern __thread int is_cpu_thread; /* Is this the CPU thread? */

/* Function prototypes. */
#ifdef HAVE_STDARG_H
extern void pclog_ex(const char *fmt, va_list ap);
extern void fatal_ex(const char *fmt, va_list ap);
extern void warning_ex(const char *fmt, va_list ap);
#endif
extern void pclog_toggle_suppr(void);
extern void pclog(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
extern void always_log(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
extern void fatal(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
extern void warning(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
extern void set_screen_size(int x, int y);
extern void set_screen_size_monitor(int x, int y, int monitor_index);
extern void reset_screen_size(void);
extern void reset_screen_size_monitor(int monitor_index);
extern void set_screen_size_natural(void);
extern void update_mouse_msg(void);
#if 0
extern void pc_reload(wchar_t *fn);
#endif
extern int  pc_init_roms(void);
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

// Accelerator key structure, defines, helper functions
struct accelKey {
	char name[64];
	char desc[64];
	char seq[64];
};
#define NUM_ACCELS 9
extern struct accelKey acc_keys[NUM_ACCELS];
extern struct accelKey def_acc_keys[NUM_ACCELS];
extern int FindAccelerator(const char *name);

#ifdef __cplusplus
}
#endif

#endif /*EMU_86BOX_H*/
