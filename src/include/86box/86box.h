/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Main include file for the application.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *f		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2017-2020 Fred N. van Kempen.
 *		Copyright 2021 Laci bá'
 */
#ifndef EMU_86BOX_H
# define EMU_86BOX_H


/* Configuration values. */
#define SERIAL_MAX	4
#define PARALLEL_MAX	4
#define SCREEN_RES_X	640
#define SCREEN_RES_Y	480

/* Filename and pathname info. */
#define CONFIG_FILE	"86box.cfg"
#define NVR_PATH        "nvr"
#define SCREENSHOT_PATH "screenshots"


/* Default language 0xFFFF = from system, 0x409 = en-US */
#define DEFAULT_LANGUAGE 0x0409

#ifdef MIN
#undef MIN
#endif
#ifdef MAX
#undef MAX
#endif
#ifdef ABS
#undef ABS
#endif

#define MIN(a, b)	((a) < (b) ? (a) : (b))
#define MAX(a, b)	((a) > (b) ? (a) : (b))
#define ABS(x)		((x) > 0 ? (x) : -(x))
#define BCD8(x)		((((x) / 10) << 4) | ((x) % 10))
#define BCD16(x)	((((x) / 1000) << 12) | (((x) / 100) << 8) | BCD8(x))
#define BCD32(x)	((((x) / 10000000) << 28) | (((x) / 1000000) << 24) | (((x) / 100000) << 20) | (((x) / 10000) << 16) | BCD16(x))

#ifdef __cplusplus
extern "C" {
#endif

/* Global variables. */
extern uint32_t	lang_sys;	/* (-) system language code */

extern int	dump_on_exit;			/* (O) dump regs on exit*/
extern int	do_dump_config;			/* (O) dump cfg after load */
extern int	start_in_fullscreen;		/* (O) start in fullscreen */
#ifdef _WIN32
extern int	force_debug;			/* (O) force debug output */
#endif
#ifdef USE_WX
extern int	video_fps;			/* (O) render speed in fps */
#endif
extern int	settings_only;			/* (O) show only the settings dialog */
extern int	confirm_exit_cmdl;		/* (O) do not ask for confirmation on quit if set to 0 */
#ifdef _WIN32
extern uint64_t	unique_id;
extern uint64_t	source_hwnd;
#endif
extern char	rom_path[1024];			/* (O) full path to ROMs */
extern char	log_path[1024];			/* (O) full path of logfile */
extern char	vm_name[1024];			/* (O) display name of the VM */


extern int	window_w, window_h,		/* (C) window size and */
		window_x, window_y,		/*     position info */
		window_remember,
		vid_resize,			/* (C) allow resizing */
		invert_display,			/* (C) invert the display */
		suppress_overscan;		/* (C) suppress overscans */
extern uint32_t	lang_id;	/* (C) language code identifier */
extern char  icon_set[256]; /* (C) iconset identifier */
extern int	scale;				/* (C) screen scale factor */
extern int  dpi_scale;      /* (C) DPI scaling of the emulated screen */
extern int	vid_api;			/* (C) video renderer */
extern int	vid_cga_contrast,		/* (C) video */
		video_fullscreen,		/* (C) video */
		video_fullscreen_first,		/* (C) video */
		video_fullscreen_scale,		/* (C) video */
		enable_overscan,		/* (C) video */
		force_43,			/* (C) video */
		video_filter_method,		/* (C) video */
		video_vsync,			/* (C) video */
		video_framerate,		/* (C) video */
		gfxcard;			/* (C) graphics/video card */
extern char	video_shader[512];		/* (C) video */
extern int	serial_enabled[],		/* (C) enable serial ports */
		bugger_enabled,			/* (C) enable ISAbugger */
		postcard_enabled,		/* (C) enable POST card */
		isamem_type[],			/* (C) enable ISA mem cards */
		isartc_type;			/* (C) enable ISA RTC card */
extern int	sound_is_float,			/* (C) sound uses FP values */
		GAMEBLASTER,			/* (C) sound option */
		GUS, GUSMAX,			/* (C) sound option */
		SSI2001,			/* (C) sound option */
		voodoo_enabled;			/* (C) video option */
extern uint32_t	mem_size;			/* (C) memory size (Installed on system board) */
extern uint32_t	isa_mem_size;		/* (C) memory size (ISA Memory Cards) */
extern int	cpu,				/* (C) cpu type */
		cpu_use_dynarec,		/* (C) cpu uses/needs Dyna */
		fpu_type;			/* (C) fpu type */
extern int	time_sync;			/* (C) enable time sync */
extern int	network_type;			/* (C) net provider type */
extern int	network_card;			/* (C) net interface num */
extern char	network_host[522];		/* (C) host network intf */
extern int	hdd_format_type;		/* (C) hard disk file format */
extern int	confirm_reset,			/* (C) enable reset confirmation */
		confirm_exit,			/* (C) enable exit confirmation */
		confirm_save;			/* (C) enable save confirmation */
extern int	enable_discord;			/* (C) enable Discord integration */

extern int	is_pentium;			/* TODO: Move back to cpu/cpu.h when it's figured out,
							 how to remove that hack from the ET4000/W32p. */
extern int	fixed_size_x, fixed_size_y;


extern char	exe_path[2048];			/* path (dir) of executable */
extern char	usr_path[1024];			/* path (dir) of user data */
extern char	cfg_path[1024];			/* full path of config file */
#ifndef USE_NEW_DYNAREC
extern FILE	*stdlog;			/* file to log output to */
#endif
extern int	scrnsz_x,			/* current screen size, X */
		scrnsz_y;			/* current screen size, Y */
extern int	efscrnsz_y;
extern int	config_changed;			/* config has changed */


/* Function prototypes. */
#ifdef HAVE_STDARG_H
extern void	pclog_ex(const char *fmt, va_list);
extern void	fatal_ex(const char *fmt, va_list);
#endif
extern void	pclog_toggle_suppr(void);
extern void	pclog(const char *fmt, ...);
extern void	fatal(const char *fmt, ...);
extern void	set_screen_size(int x, int y);
extern void	reset_screen_size(void);
extern void	set_screen_size_natural(void);
extern void update_mouse_msg();
#if 0
extern void	pc_reload(wchar_t *fn);
#endif
extern int	pc_init_modules(void);
extern int	pc_init(int argc, char *argv[]);
extern void	pc_close(void *threadid);
extern void	pc_reset_hard_close(void);
extern void	pc_reset_hard_init(void);
extern void	pc_reset_hard(void);
extern void	pc_full_speed(void);
extern void	pc_speed_changed(void);
extern void	pc_send_cad(void);
extern void	pc_send_cae(void);
extern void	pc_send_cab(void);
extern void	pc_run(void);
extern void	pc_start(void);
extern void	pc_onesec(void);

extern uint16_t	get_last_addr(void);

/* This is for external subtraction of cycles;
   should be in cpu.c but I put it here to avoid
   having to include cpu.c everywhere. */
extern void	sub_cycles(int c);
extern void	resub_cycles(int old_cycles);

extern double	isa_timing;
extern int	io_delay, framecountx;

extern volatile int	cpu_thread_run;

#ifdef __cplusplus
}
#endif


#endif	/*EMU_86BOX_H*/
