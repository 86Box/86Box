/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation core dispatcher.
 *
 * Version:	@(#)pc.c	1.0.22	2017/10/10
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
#include <wchar.h>
#include "86box.h"
#include "config.h"
#include "ibm.h"
#include "cpu/cpu.h"
#include "cpu/codegen.h"
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
#ifdef WALTJE
# define UNICODE
# include <direct.h>
# include "plat_dir.h"
# undef UNICODE
#endif
#include "ui.h"
#include "plat.h"
#include "plat_joystick.h"
#include "plat_keyboard.h"
#include "plat_midi.h"
#include "plat_mouse.h"


int	window_w, window_h, window_x, window_y, window_remember;
int	dump_on_exit = 0;
int	start_in_fullscreen = 0;
int	CPUID;
int	vid_resize, vid_api;
int	output;
int	atfullspeed;
int	cycles_lost = 0;
int	clockrate;
int	insc = 0;
float	mips, flops;
int	framecount, fps;
int	win_title_update = 0;
int	updatestatus = 0;
int	pollmouse_delay = 2;
int	mousecapture;
int	suppress_overscan = 0;
int	cpuspeed2;
wchar_t	exe_path[1024];
wchar_t	cfg_path[1024];


extern int mmuflush;
extern int readlnum,writelnum;


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


/*
 * This function returns the absolute pathname to a file (str)
 * that is to be found in the user (formerly 'nvr_path' area.
 */
wchar_t *
pc_concat(wchar_t *str)
{
    static wchar_t temp[1024];

    /* Get the full prefix in place. */
    memset(temp, 0x00, sizeof(temp));
    wcscpy(temp, cfg_path);

    /* Create the directory if needed. */
    if (! dir_check_exist(temp))
	dir_create(temp);

    /* Now append the actual filename. */
#ifdef WIN32
    wcscat(temp, L"\\");
#else
    wcscat(temp, L"/");
#endif
    wcscat(temp, str);

    return(temp);
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
    wchar_t *cfg = NULL;
    wchar_t *p;
#ifdef WALTJE
    struct direct *dp;
    DIR *dir;
#endif
    int c;

    /* Grab the executable's full path. */
    get_executable_name(exe_path, sizeof(exe_path)-1);
    p = get_filename_w(exe_path);
    *p = L'\0';

    /*
     * Get the current working directory.
     * This is normally the directory from where the
     * program was run. If we have been started via
     * a shortcut (desktop icon), however, the CWD
     * could have been set to something else.
     */
    _wgetcwd(cfg_path, sizeof(cfg_path)-1);

    for (c=1; c<argc; c++) {
	if (! _wcsicmp(argv[c], L"--help")) {
usage:
		printf("\nCommand line options:\n\n");
		printf("--config file.cfg - use given file as configuration\n");
		printf("--dump            - always dump memory on exit\n");
		printf("--fullscreen      - start in fullscreen mode\n");
		printf("--vmpath pathname - set 'path' to be root for vm\n");
		return(0);
	} else if (!_wcsicmp(argv[c], L"--config") ||
		   !_wcsicmp(argv[c], L"-C")) {
		if ((c+1) == argc) break;

		cfg = argv[++c];
	} else if (!_wcsicmp(argv[c], L"--dump") ||
		   !_wcsicmp(argv[c], L"-D")) {
		dump_on_exit = 1;
	} else if (!_wcsicmp(argv[c], L"--fullscreen") ||
		   !_wcsicmp(argv[c], L"-F")) {
		start_in_fullscreen = 1;
	} else if (!_wcsicmp(argv[c], L"--test")) {
		/* some (undocumented) test function here.. */
#ifdef WALTJE
		dir = opendirw(exe_path);
		if (dir != NULL) {
			printf("Directory '%ws':\n", exe_path);
			for (;;) {
				dp = readdir(dir);
				if (dp == NULL) break;
				printf(">> '%ws'\n", dp->d_name);
			}
			closedir(dir);
		} else {
			printf("Could not open '%ws'..\n", exe_path);
		}
#endif

		/* .. and then exit. */
		return(0);
	} else if (!_wcsicmp(argv[c], L"--vmpath") ||
		   !_wcsicmp(argv[c], L"-P")) {
		if ((c+1) == argc) break;

		wcscpy(cfg_path, argv[++c]);
	}

	/* Uhm... out of options here.. */
	else goto usage;
    }

    /*
     * This is where we start outputting to the log file,
     * if there is one. Maybe we should log a header with
     * application build info and such?  --FvK
     */

    /* Make sure cfg_path has a trailing backslash. */
    pclog("exe_path=%ws\n", exe_path);
    if ((cfg_path[wcslen(cfg_path)-1] != L'\\') &&
	(cfg_path[wcslen(cfg_path)-1] != L'/')) {
	wcscat(cfg_path, L"\\");
    }
    pclog("cfg_path=%ws\n", cfg_path);

    if (cfg != NULL) {
	/*
	 * The user specified a configuration file.
	 *
	 * If this is an absolute path, keep it, as
	 * they probably have a reason to do that.
	 * Otherwise, assume the pathname given is
	 * relative to whatever the cfg_path is.
	 */
	if ((cfg[1] == L':') ||	/* drive letter present */
	    (cfg[0] == L'\\'))	/* backslash, root dir */
		append_filename_w(config_file_default,
				  NULL,	cfg, 511);
	  else
		append_filename_w(config_file_default,
				  cfg_path, cfg, 511);
	cfg = NULL;
    } else {
        append_filename_w(config_file_default, cfg_path, CONFIG_FILE_W, 511);
    }

    /*
     * We are about to read the configuration file, which MAY
     * put data into global variables (the hard- and floppy
     * disks are an example) so we have to initialize those
     * modules before we load the config..
     */
    hdd_init();
    network_init();
    cdrom_init_host_drives();

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
    c = 0;
    for (i=0; i<ROM_MAX; i++) {
	romspresent[i] = rom_load_bios(i);
	c += romspresent[i];
    }
    if (c == 0) {
	/* No usable ROMs found, aborting. */
	return(0);
    }
    pclog("A total of %d ROM sets have been loaded.\n", c);

    /*
     * Load the ROMs for the selected machine.
     *
     * FIXME:
     * We should not do that here.  If something turns out
     * to be wrong with the configuration (such as missing
     * ROM images, we should just display a fatal message
     * in the render window's center, let them click OK,
     * and then exit so they can remedy the situation.
     */
again:
    if (! rom_load_bios(romset)) {
	/* Whoops, ROMs not found. */
	if (romset != -1)
		ui_msgbox(MBX_INFO, (wchar_t *)IDS_2063);

	/* Select another machine to use. */
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

    codegen_init();

    mouse_init();
#ifdef WALTJE
    serial_init();
#endif
    joystick_init();
    video_init();

    ide_init_first();

    cdrom_general_init();

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
pc_close(void)
{
    int i;

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
 * Run the actual configured PC.
 */
int framecountx=0;
int sndcount=0;
int sreadlnum,swritelnum,segareads,segawrites, scycles_lost;
int serial_fifo_read, serial_fifo_write;
int emu_fps = 0;

static wchar_t wmachine[2048];
static wchar_t wcpu[2048];


static void
pollmouse(void)
{
    int x, y, z;

    if (--pollmouse_delay) return;

    pollmouse_delay = 2;

    mouse_poll_host();

    mouse_get_mickeys(&x, &y, &z);

    mouse_poll(x, y, z, mouse_buttons);
}


void
pc_run(void)
{
    wchar_t s[200];
    int done = 0;

    startblit();
    clockrate = machines[machine].cpu[cpu_manufacturer].cpus[cpu].rspeed;
        
    if (is386)   {
	if (cpu_use_dynarec)
		exec386_dynarec(machines[machine].cpu[cpu_manufacturer].cpus[cpu].rspeed / 100);
	  else
		exec386(machines[machine].cpu[cpu_manufacturer].cpus[cpu].rspeed / 100);
    } else if (AT) {
	exec386(machines[machine].cpu[cpu_manufacturer].cpus[cpu].rspeed / 100);
    } else {
	execx86(machines[machine].cpu[cpu_manufacturer].cpus[cpu].rspeed / 100);
    }

    keyboard_process();

    pollmouse();

    if (joystick_type != 7)
	joystick_poll();

    endblit();

    framecountx++;
    framecount++;
    if (framecountx >= 100) {
	framecountx = 0;
	mips = (float)insc/1000000.0f;
	insc = 0;
	flops = (float)fpucount/1000000.0f;
	fpucount = 0;
	sreadlnum = readlnum;
	swritelnum = writelnum;
	segareads = egareads;
	segawrites = egawrites;
	scycles_lost = cycles_lost;

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

	updatestatus = 1;
	readlnum = writelnum = 0;
	egareads = egawrites = 0;
	cycles_lost = 0;
	mmuflush = 0;
	emu_fps = frames;
	frames = 0;
    }

    if (win_title_update) {
	mbstowcs(wmachine, machine_getname(), strlen(machine_getname())+1);
	mbstowcs(wcpu, machines[machine].cpu[cpu_manufacturer].cpus[cpu].name,
		 strlen(machines[machine].cpu[cpu_manufacturer].cpus[cpu].name)+1);
	_swprintf(s, L"%s v%s - %i%% - %s - %s - %s",
		  EMU_NAME_W, EMU_VERSION_W,
		  fps, wmachine, wcpu,
		  (!mousecapture) ? plat_get_string(IDS_2077)
				  : ((mouse_get_type(mouse_type) & MOUSE_TYPE_3BUTTON) ? plat_get_string(IDS_2078) : plat_get_string(IDS_2079)));
	set_window_title(s);

	win_title_update = 0;
    }

    done++;
}


void
onesec(void)
{
    fps = framecount;
    framecount = 0;
    win_title_update = 1;
}
