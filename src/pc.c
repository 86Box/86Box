/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Main emulator module where most things are controlled.
 *
 * Version:	@(#)pc.c	1.0.71	2018/04/29
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "86box.h"
#include "config.h"
#include "cpu/cpu.h"
#ifdef USE_DYNAREC
# include "cpu/codegen.h"
#endif
#include "cpu/x86_ops.h"
#include "io.h"
#include "mem.h"
#include "rom.h"
#include "dma.h"
#include "pci.h"
#include "pic.h"
#include "pit.h"
#include "random.h"
#include "timer.h"
#include "device.h"
#include "nvr.h"
#include "machine/machine.h"
#include "bugger.h"
#include "lpt.h"
#include "serial.h"
#include "keyboard.h"
#include "mouse.h"
#include "game/gameport.h"
#include "floppy/fdd.h"
#include "floppy/fdc.h"
#include "disk/hdd.h"
#include "disk/hdc.h"
#include "disk/hdc_ide.h"
#include "disk/zip.h"
#include "scsi/scsi.h"
#include "cdrom/cdrom.h"
#include "cdrom/cdrom_image.h"
#include "cdrom/cdrom_null.h"
#include "network/network.h"
#include "sound/sound.h"
#include "sound/midi.h"
#include "sound/snd_speaker.h"
#include "video/video.h"
#include "ui.h"
#include "plat.h"
#include "plat_midi.h"


/* Commandline options. */
int	dump_on_exit = 0;			/* (O) dump regs on exit */
int	do_dump_config = 0;			/* (O) dump config on load */
int	start_in_fullscreen = 0;		/* (O) start in fullscreen */
#ifdef _WIN32
int	force_debug = 0;			/* (O) force debug output */
#endif
#ifdef USE_WX
int	video_fps = RENDER_FPS;			/* (O) render speed in fps */
#endif
int	settings_only = 0;			/* (O) show only the settings dialog */
#ifdef _WIN32
uint64_t	unique_id = 0;
uint64_t	source_hwnd = 0;
#endif
wchar_t log_path[1024] = { L'\0'};		/* (O) full path of logfile */

/* Configuration values. */
int	window_w, window_h,			/* (C) window size and */
	window_x, window_y,			/*     position info */
	window_remember,
	vid_resize,				/* (C) allow resizing */
	invert_display,				/* (C) invert the display */
	suppress_overscan = 0;			/* (C) suppress overscans */
int	scale = 0;				/* (C) screen scale factor */
int	vid_api = 0;				/* (C) video renderer */
int	vid_cga_contrast = 0,			/* (C) video */
	video_fullscreen = 0,			/* (C) video */
	video_fullscreen_scale = 0,		/* (C) video */
	video_fullscreen_first = 0,		/* (C) video */
	enable_overscan = 0,			/* (C) video */
	force_43 = 0,				/* (C) video */
	video_speed = 0;			/* (C) video */
int	serial_enabled[SERIAL_MAX] = {0,0},	/* (C) enable serial ports */
	lpt_enabled = 0,			/* (C) enable LPT ports */
	bugger_enabled = 0;			/* (C) enable ISAbugger */
int	gfxcard = 0;				/* (C) graphics/video card */
int	sound_is_float = 1,			/* (C) sound uses FP values */
	GAMEBLASTER = 0,			/* (C) sound option */
	GUS = 0,				/* (C) sound option */
	SSI2001 = 0,				/* (C) sound option */
	voodoo_enabled = 0;			/* (C) video option */
uint32_t mem_size = 0;				/* (C) memory size */
int	cpu_manufacturer = 0,			/* (C) cpu manufacturer */
	cpu_use_dynarec = 0,			/* (C) cpu uses/needs Dyna */
	cpu = 3,				/* (C) cpu type */
	enable_external_fpu = 0;		/* (C) enable external FPU */
int	enable_sync = 0;			/* (C) enable time sync */

/* Statistics. */
extern int
	mmuflush,
	readlnum,
	writelnum;

int	fps, framecount;			/* emulator % */

int	CPUID;
int	output;
int	atfullspeed;
int	cpuspeed2;
int	clockrate;

int	gfx_present[GFX_MAX];			/* should not be here */

wchar_t	exe_path[1024];				/* path (dir) of executable */
wchar_t	usr_path[1024];				/* path (dir) of user data */
wchar_t	cfg_path[1024];				/* full path of config file */
FILE	*stdlog = NULL;				/* file to log output to */
int	scrnsz_x = SCREEN_RES_X,		/* current screen size, X */
	scrnsz_y = SCREEN_RES_Y;		/* current screen size, Y */
int	config_changed;				/* config has changed */
int	romset;					/* current machine ID */
int	title_update;
int64_t	main_time;


int	unscaled_size_x = SCREEN_RES_X,	/* current unscaled size X */
	unscaled_size_y = SCREEN_RES_Y,	/* current unscaled size Y */
	efscrnsz_y = SCREEN_RES_Y;


#ifndef RELEASE_BUILD
static char buff[1024];
static int seen = 0;

static int suppr_seen = 1;
#endif

/*
 * Log something to the logfile or stdout.
 *
 * To avoid excessively-large logfiles because some
 * module repeatedly logs, we keep track of what is
 * being logged, and catch repeating entries.
 */
void
pclog_ex(const char *fmt, va_list ap)
{
#ifndef RELEASE_BUILD
    char temp[1024];

    if (stdlog == NULL) {
	if (log_path[0] != L'\0') {
		stdlog = plat_fopen(log_path, L"w");
		if (stdlog == NULL)
			stdlog = stdout;
	} else {
		stdlog = stdout;
	}
    }

    vsprintf(temp, fmt, ap);
    if (suppr_seen && ! strcmp(buff, temp)) {
	seen++;
    } else {
	if (suppr_seen && seen) {
		fprintf(stdlog, "*** %d repeats ***\n", seen);
	}
	seen = 0;
	strcpy(buff, temp);
	fprintf(stdlog, temp, ap);
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
pclog(const char *fmt, ...)
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
    char temp[1024];
    va_list ap;
    char *sp;

    va_start(ap, fmt);

    if (stdlog == NULL) {
	if (log_path[0] != L'\0') {
		stdlog = plat_fopen(log_path, L"w");
		if (stdlog == NULL)
			stdlog = stdout;
	} else {
		stdlog = stdout;
	}
    }

    vsprintf(temp, fmt, ap);
    fprintf(stdlog, "%s", temp);
    fflush(stdlog);
    va_end(ap);

    nvr_save();

    config_save();

    dumppic();
    dumpregs(1);

    /* Make sure the message does not have a trailing newline. */
    if ((sp = strchr(temp, '\n')) != NULL) *sp = '\0';

    ui_msgbox(MBX_ERROR|MBX_FATAL|MBX_ANSI, temp);

    fflush(stdlog);

    exit(-1);
}


#ifdef ENABLE_PC_LOG
int pc_do_log = ENABLE_PC_LOG;
#endif


static void
pc_log(const char *format, ...)
{
#ifdef ENABLE_PC_LOG
    va_list ap;

    if (pc_do_log) {
	va_start(ap, format);
	pclog_ex(format, ap);
	va_end(ap);
    }
#endif
}


/*
 * Perform initial startup of the PC.
 *
 * This is the platform-indepenent part of the startup,
 * where we check commandline arguments and load a
 * configuration file.
 */
int
pc_init(int argc, wchar_t *argv[])
{
    wchar_t path[2048];
    wchar_t *cfg = NULL, *p;
    char temp[128];
    struct tm *info;
    time_t now;
    int c;
    uint32_t *uid, *shwnd;

    /* Grab the executable's full path. */
    plat_get_exe_name(exe_path, sizeof(exe_path)-1);
    p = plat_get_filename(exe_path);
    *p = L'\0';

    /*
     * Get the current working directory.
     *
     * This is normally the directory from where the
     * program was run. If we have been started via
     * a shortcut (desktop icon), however, the CWD
     * could have been set to something else.
     */
    plat_getcwd(usr_path, sizeof_w(usr_path)-1);
    memset(path, 0x00, sizeof(path));

    for (c=1; c<argc; c++) {
	if (argv[c][0] != L'-') break;

	if (!wcscasecmp(argv[c], L"--help") || !wcscasecmp(argv[c], L"-?")) {
usage:
		printf("\nUsage: 86box [options] [cfg-file]\n\n");
		printf("Valid options are:\n\n");
		printf("-? or --help         - show this information\n");
		printf("-C or --dumpcfg      - dump config file after loading\n");
#ifdef _WIN32
		printf("-D or --debug        - force debug output logging\n");
#endif
		printf("-F or --fullscreen   - start in fullscreen mode\n");
		printf("-L or --logfile path - set 'path' to be the logfile\n");
		printf("-P or --vmpath path  - set 'path' to be root for vm\n");
		printf("-S or --settings     - show only the settings dialog\n");
#ifdef _WIN32
		printf("-H or --hwnd id,hwnd - sends back the main dialog's hwnd\n");
#endif
		printf("\nA config file can be specified. If none is, the default file will be used.\n");
		return(0);
	} else if (!wcscasecmp(argv[c], L"--dumpcfg") ||
		   !wcscasecmp(argv[c], L"-C")) {
		do_dump_config = 1;
#ifdef _WIN32
	} else if (!wcscasecmp(argv[c], L"--debug") ||
		   !wcscasecmp(argv[c], L"-D")) {
		force_debug = 1;
#endif
	} else if (!wcscasecmp(argv[c], L"--fullscreen") ||
		   !wcscasecmp(argv[c], L"-F")) {
		start_in_fullscreen = 1;
	} else if (!wcscasecmp(argv[c], L"--logfile") ||
		   !wcscasecmp(argv[c], L"-L")) {
		if ((c+1) == argc) goto usage;

		wcscpy(log_path, argv[++c]);
	} else if (!wcscasecmp(argv[c], L"--vmpath") ||
		   !wcscasecmp(argv[c], L"-P")) {
		if ((c+1) == argc) goto usage;

		wcscpy(path, argv[++c]);
	} else if (!wcscasecmp(argv[c], L"--settings") ||
		   !wcscasecmp(argv[c], L"-S")) {
		settings_only = 1;
#ifdef _WIN32
	} else if (!wcscasecmp(argv[c], L"--hwnd") ||
		   !wcscasecmp(argv[c], L"-H")) {

		if ((c+1) == argc) goto usage;

		wcstombs(temp, argv[++c], 128);
		uid = (uint32_t *) &unique_id;
		shwnd = (uint32_t *) &source_hwnd;
		sscanf(temp, "%08X%08X,%08X%08X", uid + 1, uid, shwnd + 1, shwnd);
#endif
	} else if (!wcscasecmp(argv[c], L"--test")) {
		/* some (undocumented) test function here.. */

		/* .. and then exit. */
		return(0);
	}

	/* Uhm... out of options here.. */
	else goto usage;
    }

    /* One argument (config file) allowed. */
    if (c < argc)
	cfg = argv[c++];
    if (c != argc) goto usage;

    /*
     * If the user provided a path for files, use that
     * instead of the current working directory. We do
     * make sure that if that was a relative path, we
     * make it absolute.
     */
    if (path[0] != L'\0') {
	if (! plat_path_abs(path)) {
		/*
		 * This looks like a relative path.
		 *
		 * Add it to the current working directory
		 * to convert it (back) to an absolute path.
		 */
		plat_path_slash(usr_path);
		wcscat(usr_path, path);
	} else {
		/*
		 * The user-provided path seems like an
		 * absolute path, so just use that.
		 */
		wcscpy(usr_path, path);
	}
    }

    /* Make sure we have a trailing backslash. */
    plat_path_slash(usr_path);

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
    p = plat_get_filename(cfg);
    if (cfg != p) {
	/*
	 * OK, the configuration file name has a
	 * path component. Separate the two, and
	 * add the path component to the cfg path.
	 */
	*(p-1) = L'\0';

	/*
	 * If this is an absolute path, keep it, as
	 * there is probably have a reason to do so.
	 * Otherwise, assume the pathname given is
	 * relative to whatever the usr_path is.
	 */
	if (plat_path_abs(cfg))
		wcscpy(usr_path, cfg);
	  else
		wcscat(usr_path, cfg);

	/* Make sure we have a trailing backslash. */
	plat_path_slash(usr_path);
    }

    /* At this point, we can safely create the full path name. */
    plat_append_filename(cfg_path, usr_path, p);

    /*
     * This is where we start outputting to the log file,
     * if there is one. Create a little info header first.
     */
    (void)time(&now);
    info = localtime(&now);
    strftime(temp, sizeof(temp), "%Y/%m/%d %H:%M:%S", info);
    pclog("#\n# %ls v%ls logfile, created %s\n#\n",
		EMU_NAME_W, EMU_VERSION_W, temp);
    pclog("# Emulator path: %ls\n", exe_path);
    pclog("# Userfiles path: %ls\n", usr_path);
    pclog("# Configuration file: %ls\n#\n\n", cfg_path);

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

    /* Load the configuration file. */
    config_load();

    /* All good! */
    return(1);
}


void
pc_full_speed(void)
{
    cpuspeed2 = cpuspeed;

    if (! atfullspeed) {
	pc_log("Set fullspeed - %i %i %i\n", is386, AT, cpuspeed2);
	if (AT)
		setpitclock(machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].rspeed);
	  else
		setpitclock(14318184.0);
    }
    atfullspeed = 1;

    nvr_period_recalc();
}


void
pc_speed_changed(void)
{
    if (AT)
	setpitclock(machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].rspeed);
      else
	setpitclock(14318184.0);

    nvr_period_recalc();
}


#if 0
/* Re-load system configuration and restart. */
/* FIXME: this has to be reviewed! */
void
pc_reload(wchar_t *fn)
{
    int i;

    config_write(cfg_path);

    for (i=0; i<FDD_NUM; i++)
	fdd_close(i);

    cdrom_close();

    pc_reset_hard_close();

    // FIXME: set up new config file path 'fn'... --FvK

    config_load();

    cdrom_hard_reset();

    zip_hard_reset();

    fdd_load(0, floppyfns[0]);
    fdd_load(1, floppyfns[1]);
    fdd_load(2, floppyfns[2]);
    fdd_load(3, floppyfns[3]);

    network_init();

    pc_reset_hard_init();
}
#endif


/* Initialize modules, ran once, after pc_init. */
int
pc_init_modules(void)
{
    int c, i;

    pc_log("Scanning for ROM images:\n");
    for (c=0,i=0; i<ROM_MAX; i++) {
	romspresent[i] = rom_load_bios(i);
	c += romspresent[i];
    }
    if (c == 0) {
	/* No usable ROMs found, aborting. */
	return(0);
    }
    pc_log("A total of %d ROM sets have been loaded.\n", c);

    /* Load the ROMs for the selected machine. */
again:
    if (! rom_load_bios(romset)) {
	/* Whoops, ROMs not found. */
	if (romset != -1)
		ui_msgbox(MBX_INFO, (wchar_t *)IDS_2063);

	/*
	 * Select another machine to use.
	 *
	 * FIXME:
	 * We should not do that here.  If something turns out
	 * to be wrong with the configuration (such as missing
	 * ROM images, we should just display a fatal message
	 * in the render window's center, let them click OK,
	 * and then exit so they can remedy the situation.
	 */
	for (c=0; c<ROM_MAX; c++) {
		if (romspresent[c]) {
			romset = c;
			machine = machine_getmachine(romset);
			config_save();

			/* This can loop if all ROMs are now bad.. */
			goto again;
		}
	}
    }

    /* Make sure we have a usable video card. */
    for (c=0; c<GFX_MAX; c++)
	gfx_present[c] = video_card_available(video_old_to_new(c));
again2:
    if (! video_card_available(video_old_to_new(gfxcard))) {
	if (romset != -1) {
		ui_msgbox(MBX_INFO, (wchar_t *)IDS_2064);
	}
	for (c=GFX_MAX-1; c>=0; c--) {
		if (gfx_present[c]) {
			gfxcard = c;
			config_save();

			/* This can loop if all cards now bad.. */
			goto again2;
		}
	}
    }

    cpuspeed2 = (AT) ? 2 : 1;
    atfullspeed = 0;

    random_init();

    mem_init();

#ifdef USE_DYNAREC
    codegen_init();
#endif

    keyboard_init();
    joystick_init();
    video_init();
    device_init();

    timer_reset();

    fdd_init();

    sound_init();

    hdc_init(hdc_name);

    cdrom_hard_reset();
    zip_hard_reset();

    scsi_card_init();

    pc_full_speed();
    shadowbios = 0;

    return(1);
}


/* Insert keystrokes into the machine's keyboard buffer. */
static void
pc_keyboard_send(uint8_t val)
{
    if (AT)
	keyboard_at_adddata_keyboard_raw(val);
    else
	keyboard_send(val);
}


void
pc_send_ca(uint8_t sc)
{
    pc_keyboard_send(29);	/* Ctrl key pressed */
    pc_keyboard_send(56);	/* Alt key pressed */
    pc_keyboard_send(sc);
    pc_keyboard_send(sc | 0x80);
    pc_keyboard_send(184);	/* Alt key released */
    pc_keyboard_send(157);	/* Ctrl key released */
}


/* Send the machine a Control-Alt-DEL sequence. */
void
pc_send_cad(void)
{
    pc_send_ca(83);
}


/* Send the machine a Control-Alt-ESC sequence. */
void
pc_send_cae(void)
{
    pc_send_ca(1);
}


void
pc_reset_hard_close(void)
{
    suppress_overscan = 0;

    nvr_save();

    mouse_close();

    lpt_devices_close();

    device_close_all();

    midi_close();

    cdrom_close();

    closeal();
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

    /* Reset the general machine support modules. */
    io_init();
    timer_reset();

    device_init();

    sound_reset();

    /* This is needed to initialize the serial timer. */
    serial_init();

    cdrom_hard_reset();
    zip_hard_reset();

    /* Initialize the actual machine and its basic modules. */
    machine_init();

    fdd_reset();

    /*
     * Once the machine has been initialized, all that remains
     * should be resetting all devices set up for it, to their
     * current configurations !
     *
     * For now, we will call their reset functions here, but
     * that will be a call to device_reset_all() later !
     */

    /* Reset some basic devices. */
    speaker_init();
    serial_reset();
    lpt_devices_init();
    shadowbios = 0;

    /*
     * This has to be after the serial initialization so that
     * serial_init() doesn't break the serial mouse by resetting
     * the RCR callback to NULL.
     */
    mouse_reset();

    /* Reset the video card. */
    video_reset(gfxcard);

    /* Reset the Hard Disk Controller module. */
    hdc_reset();

    /* Reset and reconfigure the SCSI layer. */
    scsi_card_init();

    /* Reset and reconfigure the Sound Card layer. */
    sound_card_reset();

    /* Reset and reconfigure the Network Card layer. */
    network_reset();

    if (joystick_type != 7)
	gameport_update_joystick_type();

    if (config_changed) {
	ui_sb_update_panes();

        config_save();

	config_changed = 0;
    }

    /* Needs the status bar... */
    if (bugger_enabled)
	device_add(&bugger_device);

    /* Reset the CPU module. */
    resetx86();
    dma_reset();
    pic_reset();
    cpu_cache_int_enabled = cpu_cache_ext_enabled = 0;

    if (AT)
	setpitclock(machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].rspeed);
    else
	setpitclock(14318184.0);
}


void
pc_reset_hard(void)
{
    pc_reset_hard_close();

    pc_reset_hard_init();
}


void
pc_reset(int hard)
{
    plat_pause(1);

    plat_delay_ms(100);

    nvr_save();

    config_save();

    if (hard)
        pc_reset_hard();
      else
        pc_send_cad();

    plat_pause(0);
}


void
pc_close(thread_t *ptr)
{
    int i;

    /* Wait a while so things can shut down. */
    plat_delay_ms(200);

    /* Claim the video blitter. */
    startblit();

    /* Terminate the main thread. */
    if (ptr != NULL) {
	thread_kill(ptr);

	/* Wait some more. */
	plat_delay_ms(200);
    }

    nvr_save();

    config_save();

    plat_mouse_capture(0);

    lpt_devices_close();

    for (i=0; i<ZIP_NUM; i++)
	zip_close(i);

    for (i=0; i<FDD_NUM; i++)
       fdd_close(i);

    if (dump_on_exit)
	dumppic();
    dumpregs(0);

    video_close();

    device_close_all();

    midi_close();

    network_close();

    sound_cd_thread_end();
    cdrom_close();

    zip_destroy_drives();
}


/*
 * The main thread runs the actual emulator code.
 *
 * We basically run until the upper layers terminate us, by
 * setting the variable 'quited' there to 1. We get a pointer
 * to that variable as our function argument.
 */
void
pc_thread(void *param)
{
    wchar_t temp[200], wcpu[2048];
    wchar_t wmachine[2048];
    uint64_t start_time, end_time;
    uint32_t old_time, new_time;
    int done, drawits, frames;
    int *quitp = (int *)param;
    int framecountx;

    pc_log("PC: starting main thread...\n");

    main_time = 0;
    framecountx = 0;
    title_update = 1;
    old_time = plat_get_ticks();
    done = drawits = frames = 0;
    while (! *quitp) {
	/* See if it is time to run a frame of code. */
	new_time = plat_get_ticks();
	drawits += (new_time - old_time);
	old_time = new_time;
	if (drawits > 0 && !dopause) {
		/* Yes, so do one frame now. */
		start_time = plat_timer_read();
		drawits -= 10;
		if (drawits > 50)
			drawits = 0;

		/* Run a block of code. */
		startblit();
		clockrate = machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].rspeed;

		if (is386) {
#ifdef USE_DYNAREC
			if (cpu_use_dynarec)
				exec386_dynarec(clockrate/100);
			  else
#endif
				exec386(clockrate/100);
		} else if (AT) {
			exec386(clockrate/100);
		} else {
			execx86(clockrate/100);
		}

		mouse_process();

		joystick_process();

		endblit();

		/* Done with this frame, update statistics. */
		framecount++;
		if (++framecountx >= 100) {
			framecountx = 0;

			readlnum = writelnum = 0;
			egareads = egawrites = 0;
			mmuflush = 0;
			frames = 0;
		}

		if (title_update) {
			mbstowcs(wmachine, machine_getname(), strlen(machine_getname())+1);
			mbstowcs(wcpu, machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].name,
				 strlen(machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].name)+1);
			swprintf(temp, sizeof_w(temp),
				 L"%ls v%ls - %i%% - %ls - %ls - %ls",
				 EMU_NAME_W,EMU_VERSION_W,fps,wmachine,wcpu,
				 (!mouse_capture) ? plat_get_string(IDS_2077)
				  : (mouse_get_buttons() > 2) ? plat_get_string(IDS_2078) : plat_get_string(IDS_2079));

			ui_window_title(temp);

			title_update = 0;
		}

		/* One more frame done! */
		done++;

		/* Every 200 frames we save the machine status. */
		if (++frames >= 200 && nvr_dosave) {
			nvr_save();
			nvr_dosave = 0;
			frames = 0;
		}

		end_time = plat_timer_read();
		main_time += (end_time - start_time);
	} else {
		/* Just so we dont overload the host OS. */
		plat_delay_ms(1);
	}

	/* If needed, handle a screen resize. */
	if (doresize && !video_fullscreen) {
		plat_resize(scrnsz_x, scrnsz_y);

		doresize = 0;
	}
    }

    pc_log("PC: main thread done.\n");
}


/* Handler for the 1-second timer to refresh the window title. */
void
pc_onesec(void)
{
    fps = framecount;
    framecount = 0;

    title_update = 1;
}


void
set_screen_size(int x, int y)
{
    int owsx = scrnsz_x;
    int owsy = scrnsz_y;
    int temp_overscan_x = overscan_x;
    int temp_overscan_y = overscan_y;
    double dx, dy, dtx, dty;

    /* Make sure we keep usable values. */
#if 0
    pc_log("SetScreenSize(%d, %d) resize=%d\n", x, y, vid_resize);
#endif
    if (x < 320) x = 320;
    if (y < 200) y = 200;
    if (x > 2048) x = 2048;
    if (y > 2048) y = 2048;

    /* Save the new values as "real" (unscaled) resolution. */
    unscaled_size_x = x;
    efscrnsz_y = y;

    if (suppress_overscan)
	temp_overscan_x = temp_overscan_y = 0;

    if (force_43) {
	dx = (double)x;
	dtx = (double)temp_overscan_x;

	dy = (double)y;
	dty = (double)temp_overscan_y;

	/* Account for possible overscan. */
	if (!(video_is_ega_vga()) && (temp_overscan_y == 16)) {
		/* CGA */
		dy = (((dx - dtx) / 4.0) * 3.0) + dty;
	} else if (!(video_is_ega_vga()) && (temp_overscan_y < 16)) {
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
	unscaled_size_y = (int)dy;
    } else {
	unscaled_size_y = efscrnsz_y;
    }

    switch(scale) {
	case 0:		/* 50% */
		scrnsz_x = (unscaled_size_x>>1);
		scrnsz_y = (unscaled_size_y>>1);
		break;

	case 1:		/* 100% */
		scrnsz_x = unscaled_size_x;
		scrnsz_y = unscaled_size_y;
		break;

	case 2:		/* 150% */
		scrnsz_x = ((unscaled_size_x*3)>>1);
		scrnsz_y = ((unscaled_size_y*3)>>1);
		break;

	case 3:		/* 200% */
		scrnsz_x = (unscaled_size_x<<1);
		scrnsz_y = (unscaled_size_y<<1);
		break;
    }

    /* If the resolution has changed, let the main thread handle it. */
    if ((owsx != scrnsz_x) || (owsy != scrnsz_y))
	doresize = 1;
      else
	doresize = 0;
}


void
set_screen_size_natural(void)
{
    set_screen_size(unscaled_size_x, unscaled_size_y);
}


int
get_actual_size_x(void)
{
    return(unscaled_size_x);
}


int
get_actual_size_y(void)
{
    return(efscrnsz_y);
}
