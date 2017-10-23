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
 * Version:	@(#)pc.c	1.0.32	2017/10/22
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <wchar.h>
#include "86box.h"
#include "config.h"
#include "ibm.h"
#include "cpu/cpu.h"
#ifdef USE_DYNAREC
# include "cpu/codegen.h"
#endif
#include "cpu/x86_ops.h"
#include "io.h"
#include "mem.h"
#include "rom.h"
#include "dma.h"
#include "pic.h"
#include "pit.h"
#include "random.h"
#include "timer.h"
#include "mouse.h"
#include "device.h"
#include "nvr.h"
#include "machine/machine.h"
#include "game/gameport.h"
#include "keyboard.h"
#include "keyboard_at.h"
#include "lpt.h"
#include "serial.h"
#include "cdrom/cdrom.h"
#include "disk/hdd.h"
#include "disk/hdc.h"
#include "disk/hdc_ide.h"
#include "floppy/floppy.h"
#include "floppy/fdc.h"
#include "scsi/scsi.h"
#include "network/network.h"
#include "sound/sound.h"
#include "sound/midi.h"
#include "sound/snd_cms.h"
#include "sound/snd_dbopl.h"
#include "sound/snd_mpu401.h"
#include "sound/snd_opl.h"
#include "sound/snd_gus.h"
#include "sound/snd_sb.h"
#include "sound/snd_speaker.h"
#include "sound/snd_ssi2001.h"
#include "video/video.h"
#include "video/vid_voodoo.h"
#include "ui.h"
#include "plat.h"
#include "plat_joystick.h"
#include "plat_keyboard.h"
#include "plat_midi.h"
#include "plat_mouse.h"


/* Commandline options. */
int	dump_on_exit = 0;			/* (O) dump regs on exit */
int	do_dump_config = 0;			/* (O) dump config on load */
int	start_in_fullscreen = 0;		/* (O) start in fullscreen */

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


/* Statistics. */
extern int
	mmuflush,
	readlnum,
	writelnum;

int	sndcount = 0;
int	sreadlnum,
	swritelnum,
	segareads,
	segawrites,
	scycles_lost;
float	mips, flops;
int	cycles_lost = 0;			// video
int	insc = 0;				// cpu
int	emu_fps = 0, fps;			// video
int	framecount;

int	CPUID;
int	output;
int	atfullspeed;
int	cpuspeed2;
int	clockrate;

int	gfx_present[GFX_MAX];			// should not be here

wchar_t	exe_path[1024];				/* path (dir) of executable */
wchar_t	cfg_path[1024];				/* path (dir) of user data */
int	scrnsz_x = SCREEN_RES_X,		/* current screen size, X */
	scrnsz_y = SCREEN_RES_Y;		/* current screen size, Y */
int	title_update;
int64_t	main_time;


static int	unscaled_size_x = SCREEN_RES_X,	/* current unscaled size X */
		unscaled_size_y = SCREEN_RES_Y;	/* current unscaled size Y */


/* Log something to the logfile or stdout. */
void
pclog(const char *format, ...)
{
#ifndef RELEASE_BUILD
   va_list ap;
   va_start(ap, format);
   vprintf(format, ap);
   va_end(ap);
   fflush(stdout);
#endif
}


/* Log a fatal error, and display a UI message before exiting. */
void
fatal(const char *format, ...)
{
   char msg[1024];
   va_list ap;
   char *sp;

   va_start(ap, format);
   vsprintf(msg, format, ap);
   printf(msg);
   va_end(ap);
   fflush(stdout);

   nvr_save();

   config_save();

   dumppic();
   dumpregs(1);

   /* Make sure the message does not have a trailing newline. */
   if ((sp = strchr(msg, '\n')) != NULL) *sp = '\0';

   ui_msgbox(MBX_ERROR|MBX_FATAL|MBX_ANSI, msg);

   fflush(stdout);

   exit(-1);
}


void
set_screen_size(int x, int y)
{
    int owsx = scrnsz_x;
    int owsy = scrnsz_y;
    int temp_overscan_x = overscan_x;
    int temp_overscan_y = overscan_y;
    double dx, dy, dtx, dty;
    int efscrnsz_y;

    /* Make sure we keep usable values. */
#if 0
    pclog("SetScreenSize(%d, %d) resize=%d\n", x, y, vid_resize);
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
	if (!(VGA) && (temp_overscan_y == 16)) {
		/* CGA */
		dy = (((dx - dtx) / 4.0) * 3.0) + dty;
	} else if (!(VGA) && (temp_overscan_y < 16)) {
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


/*
 * Perform initial startup of the PC.
 *
 * This is the platform-indepenent part of the startup,
 * where we check commandline arguments and loading a
 * configuration file.
 */
int
pc_init(int argc, wchar_t *argv[])
{
    wchar_t *cfg = NULL, *p;
    char temp[128];
    struct tm *info;
    time_t now;
    int c;

    /* Grab the executable's full path. */
    plat_get_exe_name(exe_path, sizeof(exe_path)-1);
    p = plat_get_filename(exe_path);
    *p = L'\0';

    /*
     * Get the current working directory.
     * This is normally the directory from where the
     * program was run. If we have been started via
     * a shortcut (desktop icon), however, the CWD
     * could have been set to something else.
     */
    plat_getcwd(cfg_path, sizeof(cfg_path)-1);

    for (c=1; c<argc; c++) {
	if (argv[c][0] != L'-') break;

	if (!wcscasecmp(argv[c], L"--help") || !wcscasecmp(argv[c], L"-?")) {
usage:
		printf("\nUsage: 86box [options] [cfg-file]\n\n");
		printf("Valid options are:\n\n");
		printf("-? or --help        - show this information\n");
		printf("-C or --dumpcfg     - dump config file after loading\n");
		printf("-D or --dump        - dump memory on exit\n");
		printf("-F or --fullscreen  - start in fullscreen mode\n");
		printf("-P or --vmpath path - set 'path' to be root for vm\n");
		printf("\nA config file can be specified. If none is, the default file will be used.\n");
		return(0);
	} else if (!wcscasecmp(argv[c], L"--dumpcfg") ||
		   !wcscasecmp(argv[c], L"-C")) {
		do_dump_config = 1;
	} else if (!wcscasecmp(argv[c], L"--dump") ||
		   !wcscasecmp(argv[c], L"-D")) {
		dump_on_exit = 1;
	} else if (!wcscasecmp(argv[c], L"--fullscreen") ||
		   !wcscasecmp(argv[c], L"-F")) {
		start_in_fullscreen = 1;
	} else if (!wcscasecmp(argv[c], L"--vmpath") ||
		   !wcscasecmp(argv[c], L"-P")) {
		if ((c+1) == argc) break;

		wcscpy(cfg_path, argv[++c]);
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

    /* Make sure cfg_path has a trailing backslash. */
    if ((cfg_path[wcslen(cfg_path)-1] != L'\\') &&
	(cfg_path[wcslen(cfg_path)-1] != L'/')) {
#ifdef WIN32
	wcscat(cfg_path, L"\\");
#else
	wcscat(cfg_path, L"/");
#endif
    }

    if (cfg != NULL) {
	/*
	 * The user specified a configuration file.
	 *
	 * If this is an absolute path, keep it, as
	 * they probably have a reason to do that.
	 * Otherwise, assume the pathname given is
	 * relative to whatever the cfg_path is.
	 */
#ifdef WIN32
	if ((cfg[1] == L':') ||	/* drive letter present */
	    (cfg[0] == L'\\'))	/* backslash, root dir */
#else
	if (cfg[0] == L'/')	/* slash, root dir */
#endif
		wcscpy(config_file_default, cfg);
	  else
		plat_append_filename(config_file_default, cfg_path, cfg, 511);
	cfg = NULL;
    } else {
        plat_append_filename(config_file_default, cfg_path, CONFIG_FILE_W, 511);
    }

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
    pclog("# Userfiles path: %ls\n", cfg_path);
    pclog("# Configuration file: %ls\n#\n\n", config_file_default);

    /*
     * We are about to read the configuration file, which MAY
     * put data into global variables (the hard- and floppy
     * disks are an example) so we have to initialize those
     * modules before we load the config..
     */
    hdd_init();
    network_init();
    cdrom_global_init();

    /* Load the configuration file. */
    config_load(cfg);

    /* All good! */
    return(1);
}


void
pc_full_speed(void)
{
    cpuspeed2 = cpuspeed;

    if (! atfullspeed) {
	pclog("Set fullspeed - %i %i %i\n", is386, AT, cpuspeed2);
	if (AT)
		setpitclock(machines[machine].cpu[cpu_manufacturer].cpus[cpu].rspeed);
	  else
		setpitclock(14318184.0);
    }
    atfullspeed = 1;

    nvr_recalc();
}


void
pc_speed_changed(void)
{
    if (AT)
	setpitclock(machines[machine].cpu[cpu_manufacturer].cpus[cpu].rspeed);
      else
	setpitclock(14318184.0);

    nvr_recalc();
}


/* Initialize modules, ran once, after pc_init. */
int
pc_init_modules(void)
{
    int c, i;

    pclog("Scanning for ROM images:\n");
    for (c=0,i=0; i<ROM_MAX; i++) {
	romspresent[i] = rom_load_bios(i);
	c += romspresent[i];
    }
    if (c == 0) {
	/* No usable ROMs found, aborting. */
	return(0);
    }
    pclog("A total of %d ROM sets have been loaded.\n", c);

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

    mouse_init();
#ifdef WALTJE
    serial_init();
#endif
    joystick_init();
    video_init();

    ide_init_first();

    cdrom_global_reset();

    device_init();        
                       
    timer_reset();

    sound_reset();

    fdc_init();

    floppy_general_init();

    sound_init();

    hdc_init(hdc_name);

    ide_reset();

    cdrom_hard_reset();

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


/* Send the machine a Control-Alt-DEL sequence. */
void
pc_send_cad(void)
{
    pc_keyboard_send(29);	/* Ctrl key pressed */
    pc_keyboard_send(56);	/* Alt key pressed */
    pc_keyboard_send(83);	/* Delete key pressed */
    pc_keyboard_send(157);	/* Ctrl key released */
    pc_keyboard_send(184);	/* Alt key released */
    pc_keyboard_send(211);	/* Delete key released */
}


/* Send the machine a Control-Alt-ESC sequence. */
void
pc_send_cae(void)
{
    pc_keyboard_send(29);	/* Ctrl key pressed */
    pc_keyboard_send(56);	/* Alt key pressed */
    pc_keyboard_send(1);	/* Esc key pressed */
    pc_keyboard_send(157);	/* Ctrl key released */
    pc_keyboard_send(184);	/* Alt key released */
    pc_keyboard_send(129);	/* Esc key released */
}


void
pc_reset_hard_close(void)
{
    suppress_overscan = 0;

    nvr_save();

    device_close_all();
    midi_close();
    mouse_emu_close();
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
    /* First, we reset the modules that are not part of the
     * actual machine, but which support some of the modules
     * that are.
     */
    sound_realloc_buffers();
    sound_cd_thread_reset();
    initalmain(0, NULL);

    /* Reset the general machine support modules. */
    mem_resize();
    io_init();
    device_init();
    timer_reset();

    midi_device_init();
    inital();
    sound_reset();

    fdc_init();
    fdc_update_is_nsc(0);
    floppy_reset();

#ifndef WALTJE
    /* This is needed to initialize the serial timer. */
    serial_init();
#endif

    /* Initialize the actual machine and its basic modules. */
    machine_init();

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
    lpt1_device_init();

    /* Reset keyboard and/or mouse. */
    keyboard_at_reset();
    mouse_emu_init();
        
    /* Reset the video card. */
    video_reset();
    if (voodoo_enabled)
	device_add(&voodoo_device);

    /* Reset the Floppy Disk controller. */
    fdc_reset();

    /* Reset the Hard Disk Controller module. */
    hdc_reset();

    /* Reconfire and reset the IDE layer. */
    ide_ter_disable();
    ide_qua_disable();
    if (ide_enable[2])
	ide_ter_init();
    if (ide_enable[3])
	ide_qua_init();
    ide_reset();

    /* Reset and reconfigure the SCSI layer. */
    scsi_card_init();

    cdrom_hard_reset();

    /* Reset and reconfigure the Network Card layer. */
    network_reset();

    /* Reset and reconfigure the Sound Card layer. */
    sound_card_init();
    if (mpu401_standalone_enable)
	mpu401_device_add();
    if (GUS)
	device_add(&gus_device);
    if (GAMEBLASTER)
	device_add(&cms_device);
    if (SSI2001)
	device_add(&ssi2001_device);

    /* Reset the CPU module. */
    cpu_set();
    cpu_cache_int_enabled = cpu_cache_ext_enabled = 0;
    resetx86();
    dma_reset();
    pic_reset();

    shadowbios = 0;

    if (AT)
	setpitclock(machines[machine].cpu[cpu_manufacturer].cpus[cpu].rspeed);
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

#if 0
    if (mouse_capture) {
        ClipCursor(&oldclip);
        ShowCursor(TRUE);
    }
#endif

    for (i=0; i<CDROM_NUM; i++)
	cdrom_drives[i].handler->exit(i);

    dumppic();

    for (i=0; i<FDD_NUM; i++)
       floppy_close(i);

    dumpregs(0);

    video_close();

    lpt1_device_close();

    device_close_all();

    midi_close();

    network_close();
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
    int status_update_needed;
    int done, drawits, frames;
    int *quitp = (int *)param;
    int framecountx;

    pclog("PC: starting main thread...\n");

    main_time = 0;
    framecountx = 0;
    status_update_needed = title_update = 1;
    old_time = plat_get_ticks();
    done = drawits = frames = 0;
    while (! *quitp) {
	/* Update the Stat(u)s window with the current info. */
	if (status_update_needed) {
#if 0
		pclog("Updating STATS window..\n");
//		ui_status_update();
#endif
		status_update_needed = 0;
	}

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
		clockrate = machines[machine].cpu[cpu_manufacturer].cpus[cpu].rspeed;

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

		keyboard_process();

		mouse_process();

		joystick_process();

		endblit();

		/* Done with this frame, update statistics. */
		framecount++;
		if (++framecountx >= 100) {
			framecountx = 0;

			/* FIXME: all this should go into a "stats" struct! */
			mips = (float)insc/1000000.0f;
			insc = 0;
			flops = (float)fpucount/1000000.0f;
			fpucount = 0;
			sreadlnum = readlnum;
			swritelnum = writelnum;
			segareads = egareads;
			segawrites = egawrites;
			scycles_lost = cycles_lost;

#ifdef USE_DYNAREC
			cpu_recomp_blocks_latched = cpu_recomp_blocks;
			cpu_recomp_ins_latched = cpu_state.cpu_recomp_ins;
			cpu_recomp_full_ins_latched = cpu_recomp_full_ins;
			cpu_new_blocks_latched = cpu_new_blocks;
			cpu_recomp_flushes_latched = cpu_recomp_flushes;
			cpu_recomp_evicted_latched = cpu_recomp_evicted;
			cpu_recomp_reuse_latched = cpu_recomp_reuse;
			cpu_recomp_removed_latched = cpu_recomp_removed;
			cpu_reps_latched = cpu_reps;
			cpu_notreps_latched = cpu_notreps;

			cpu_recomp_blocks = 0;
			cpu_state.cpu_recomp_ins = 0;
			cpu_recomp_full_ins = 0;
			cpu_new_blocks = 0;
			cpu_recomp_flushes = 0;
			cpu_recomp_evicted = 0;
			cpu_recomp_reuse = 0;
			cpu_recomp_removed = 0;
			cpu_reps = 0;
			cpu_notreps = 0;
#endif

			readlnum = writelnum = 0;
			egareads = egawrites = 0;
			cycles_lost = 0;
			mmuflush = 0;
			emu_fps = frames;
			frames = 0;

			/* We need a Status window update now. */
			status_update_needed = 1;
		}

		if (title_update) {
			mbstowcs(wmachine, machine_getname(), strlen(machine_getname())+1);
			mbstowcs(wcpu, machines[machine].cpu[cpu_manufacturer].cpus[cpu].name,
				 strlen(machines[machine].cpu[cpu_manufacturer].cpus[cpu].name)+1);
			swprintf(temp, sizeof_w(temp),
				 L"%ls v%ls - %i%% - %ls - %ls - %ls",
				 EMU_NAME_W,EMU_VERSION_W,fps,wmachine,wcpu,
				 (!mouse_capture) ? plat_get_string(IDS_2077)
				  : ((mouse_get_type(mouse_type) & MOUSE_TYPE_3BUTTON) ? plat_get_string(IDS_2078) : plat_get_string(IDS_2079)));

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

	/* If needed, hand a screen resize. */
	if (!video_fullscreen && doresize && (scrnsz_x>0) && (scrnsz_y>0)) {
		plat_resize(scrnsz_x, scrnsz_y);

		doresize = 0;
	}

	/* If requested, leave full-screen mode. */
	if (leave_fullscreen_flag) {
#if 1
		pclog("Leaving full-screen mode..\n");
//		plat_fullscreen(0);
#else
		SendMessage(hwndMain, WM_LEAVEFULLSCREEN, 0, 0);
#endif
		leave_fullscreen_flag = 0;
	}

#if 0
	/* Do we really need this all the time? */
	if (video_fullscreen && infocus)
		SetCursorPos(9999, 9999);
#endif
    }

    pclog("PC: main thread done.\n");
}


/* Handler for the 1-second timer to refresh the window title. */
void
pc_onesec(void)
{
    fps = framecount;
    framecount = 0;

    title_update = 1;
}
