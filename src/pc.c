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
 * Version:	@(#)pc.c	1.0.7	2017/08/24
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "86box.h"
#include "ibm.h"
#include "mem.h"
#include "cpu/cpu.h"
#include "cpu/x86_ops.h"
#include "cpu/codegen.h"
#include "dma.h"
#include "nvr.h"
#include "pic.h"
#include "pit.h"
#include "timer.h"
#include "device.h"
#include "machine/machine.h"

#include "disc.h"
#include "disc_86f.h"
#include "disc_fdi.h"
#include "disc_imd.h"
#include "disc_img.h"
#include "disc_td0.h"
#include "disc_random.h"
#include "config.h"
#include "fdc.h"
#include "fdd.h"
#include "gameport.h"
#include "hdd/hdd.h"
#include "hdd/hdd_ide_at.h"
#include "cdrom.h"
#include "cdrom_ioctl.h"
#include "cdrom_image.h"
#include "cdrom_null.h"
#include "keyboard.h"
#include "keyboard_at.h"
#include "sound/midi.h"
#include "mouse.h"
#ifdef USE_NETWORK
#include "network/network.h"
#endif
#ifdef WALTJE
# define UNICODE
# include "plat_dir.h"
# undef UNICODE
#endif
#include "plat_joystick.h"
#include "plat_keyboard.h"
#include "plat_midi.h"
#include "plat_mouse.h"
#include "plat_ui.h"
#include "scsi/scsi.h"
#include "serial.h"
#include "sound/sound.h"
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


wchar_t pcempath[512];

wchar_t nvr_path[1024];
int path_len;

int window_w, window_h, window_x, window_y, window_remember;

int dump_on_exit = 0;
int start_in_fullscreen = 0;

int CPUID;
int vid_resize, vid_api;

int cycles_lost = 0;

int clockrate;
int insc=0;
float mips,flops;
extern int mmuflush;
extern int readlnum,writelnum;
void fullspeed();

int framecount,fps;

int output;
int atfullspeed;

void saveconfig();
int infocus;
int mousecapture;


void pclog(const char *format, ...)
{
#ifndef RELEASE_BUILD
   va_list ap;
   va_start(ap, format);
   vprintf(format, ap);
   va_end(ap);
   fflush(stdout);
#endif
}

void pclog_w(const wchar_t *format, ...)
{
#ifndef RELEASE_BUILD
   va_list ap;
   va_start(ap, format);
   vwprintf(format, ap);
   va_end(ap);
   fflush(stdout);
#endif
}

#ifndef __unix
#ifndef _LIBC
# define __builtin_expect(expr, val)   (expr)
#endif

#undef memmem


/* Return the first occurrence of NEEDLE in HAYSTACK.  */
void *memmem (const void *haystack, size_t haystack_len, const void *needle, size_t needle_len)
{
	const char *begin;
	const char *const last_possible = (const char *) haystack + haystack_len - needle_len;

	if (needle_len == 0)
		/* The first occurrence of the empty string is deemed to occur at
		   the beginning of the string.  */
		return (void *) haystack;

	/* Sanity check, otherwise the loop might search through the whole
	   memory.  */
	if (__builtin_expect (haystack_len < needle_len, 0))
		return NULL;

	for (begin = (const char *) haystack; begin <= last_possible; ++begin)
		if (begin[0] == ((const char *) needle)[0] && !memcmp ((const void *) &begin[1], (const void *) ((const char *) needle + 1), needle_len - 1))
			return (void *) begin;

	return NULL;
}
#endif


void fatal(const char *format, ...)
{
   char msg[1024];
#ifndef __unix
   char *newline;
#endif
   va_list ap;
   va_start(ap, format);
   vsprintf(msg, format, ap);
   printf(msg);
   va_end(ap);
   fflush(stdout);
   savenvr();
   saveconfig();
#ifndef __unix
   newline = memmem(msg, strlen(msg), "\n", strlen("\n"));
   if (newline != NULL)
   {
      *newline = 0;
   }
   plat_msgbox_fatal(msg);
#endif
   dumppic();
   dumpregs(1);
   fflush(stdout);
   exit(-1);
}

uint8_t cgastat;


int pollmouse_delay = 2;
void pollmouse(void)
{
        int x, y, z;
        pollmouse_delay--;
        if (pollmouse_delay) return;
        pollmouse_delay = 2;
        mouse_poll_host();
        mouse_get_mickeys(&x, &y, &z);
	mouse_poll(x, y, z, mouse_buttons);
}

/*PC1512 languages -
  7=English
  6=German
  5=French
  4=Spanish
  3=Danish
  2=Swedish
  1=Italian
        3,2,1 all cause the self test to fail for some reason
  */

int cpuspeed2;

int clocks[3][12][4]=
{
        {
                {4772728,13920,59660,5965},  /*4.77MHz*/
                {8000000,23333,110000,0}, /*8MHz*/
                {10000000,29166,137500,0}, /*10MHz*/
                {12000000,35000,165000,0}, /*12MHz*/
                {16000000,46666,220000,0}, /*16MHz*/
        },
        {
                {8000000,23333,110000,0}, /*8MHz*/
                {12000000,35000,165000,0}, /*12MHz*/
                {16000000,46666,220000,0}, /*16MHz*/
                {20000000,58333,275000,0}, /*20MHz*/
                {25000000,72916,343751,0}, /*25MHz*/
        },
        {
                {16000000, 46666,220000,0}, /*16MHz*/
                {20000000, 58333,275000,0}, /*20MHz*/
                {25000000, 72916,343751,0}, /*25MHz*/
                {33000000, 96000,454000,0}, /*33MHz*/
                {40000000,116666,550000,0}, /*40MHz*/
                {50000000, 72916*2,343751*2,0}, /*50MHz*/
                {33000000*2, 96000*2,454000*2,0}, /*66MHz*/
                {75000000, 72916*3,343751*3,0}, /*75MHz*/
                {80000000,116666*2,550000*2,0}, /*80MHz*/
                {100000000, 72916*4,343751*4,0}, /*100MHz*/
                {120000000,116666*3,550000*3,0}, /*120MHz*/
                {133000000, 96000*4,454000*4,0}, /*133MHz*/
        }
};

int updatestatus;
int win_title_update=0;


void onesec(void)
{
        fps=framecount;
        framecount=0;
        win_title_update=1;
}

void pc_reset(void)
{
        cpu_set();
        resetx86();
        dma_reset();
        fdc_reset();
        pic_reset();
        serial_reset();

        if (AT)
                setpitclock(machines[machine].cpu[cpu_manufacturer].cpus[cpu].rspeed);
        else
                setpitclock(14318184.0);
}


#undef printf
void initpc(int argc, wchar_t *argv[])
{
        wchar_t *p;
        wchar_t *config_file = NULL;
        int c;
        get_executable_name(pcempath, 511);
        pclog("executable_name = %S\n", pcempath);
        p=get_filename_w(pcempath);
        *p=L'\0';
        pclog("path = %S\n", pcempath);        
#ifdef WALTJE
	DIR *dir;
	struct direct *dp;
#endif

        for (c = 1; c < argc; c++)
        {
                if (!_wcsicmp(argv[c], L"--help"))
                {
                        printf("Command line options :\n\n");
                        printf("--config file.cfg - use given config file as initial configuration\n");
                        printf("--dump            - always dump memory on exit\n");
                        printf("--fullscreen      - start in fullscreen mode\n");
                        exit(-1);
                }
                else if (!_wcsicmp(argv[c], L"--config"))
                {
                        if ((c+1) == argc)
                                break;
                        config_file = argv[c+1];
                        c++;
                }
                else if (!_wcsicmp(argv[c], L"--dump"))
                {
                        dump_on_exit = 1;
                }
                else if (!_wcsicmp(argv[c], L"--fullscreen"))
                {
                        start_in_fullscreen = 1;
                }
                else if (!_wcsicmp(argv[c], L"--test"))
                {
			/* some (undocumented) test function here.. */
#ifdef WALTJE
			dir = opendirw(pcempath);
			if (dir != NULL) {
				printf("Directory '%S':\n", pcempath);
				for (;;) {
					dp = readdir(dir);
					if (dp == NULL) break;
					printf(">> '%S'\n", dp->d_name);
				}
				closedir(dir);
			} else {
				printf("Could not open '%S'..\n", pcempath);
			}
#endif

			/* .. and then exit. */
			exit(0);
		}
        }

	if (config_file == NULL)
	{
	        append_filename_w(config_file_default, pcempath, CONFIG_FILE_W, 511);
	}
	else
	{
	        append_filename_w(config_file_default, pcempath, config_file, 511);
	}

        loadconfig(config_file);
        pclog("Config loaded\n");
}

void initmodules(void)
{
	int i;

	/* Initialize modules. */
        mouse_init();
#ifdef WALTJE
	serial_init();
#endif
	disc_random_init();

        joystick_init();

        cpuspeed2=(AT)?2:1;
        atfullspeed=0;

        initvideo();
        mem_init();
        loadbios();
        mem_add_bios();

        codegen_init();

        device_init();        
                       
        timer_reset();

	for (i = 0; i < CDROM_NUM; i++)
	{
		if (cdrom_drives[i].bus_type)
		{
			SCSIReset(cdrom_drives[i].scsi_device_id, cdrom_drives[i].scsi_device_lun);
		}

		if (cdrom_drives[i].host_drive == 200)
		{
			image_open(i, cdrom_image[i].image_path);
		}
		else if ((cdrom_drives[i].host_drive >= 'A') && (cdrom_drives[i].host_drive <= 'Z'))
		{
			ioctl_open(i, cdrom_drives[i].host_drive);
		}
		else
		{
		        cdrom_null_open(i, cdrom_drives[i].host_drive);
		}
	}

        sound_reset();
	fdc_init();
	disc_init();
	fdi_init();
        img_init();
        d86f_init();
	td0_init();
	imd_init();

        disc_load(0, discfns[0]);
        disc_load(1, discfns[1]);
        disc_load(2, discfns[2]);
        disc_load(3, discfns[3]);
                
        loadnvr();
        sound_init();

        resetide();
	scsi_card_init();

	fullspeed();
        shadowbios=0;
        
	for (i = 0; i < CDROM_NUM; i++)
	{
		if (cdrom_drives[i].host_drive == 200)
		{
			image_reset(i);
		}
		else if ((cdrom_drives[i].host_drive >= 'A') && (cdrom_drives[i].host_drive <= 'Z'))
		{
			ioctl_reset(i);
		}
	}
}

void resetpc(void)
{
        pc_reset();
        shadowbios=0;
}

void pc_keyboard_send(uint8_t val)
{
	if (AT)
	{
		keyboard_at_adddata_keyboard_raw(val);
	}
	else
	{
		keyboard_send(val);
	}
}

void resetpc_cad(void)
{
	pc_keyboard_send(29);	/* Ctrl key pressed */
	pc_keyboard_send(56);	/* Alt key pressed */
	pc_keyboard_send(83);	/* Delete key pressed */
	pc_keyboard_send(157);	/* Ctrl key released */
	pc_keyboard_send(184);	/* Alt key released */
	pc_keyboard_send(211);	/* Delete key released */
}


void ctrl_alt_esc(void)
{
	pc_keyboard_send(29);	/* Ctrl key pressed */
	pc_keyboard_send(56);	/* Alt key pressed */
	pc_keyboard_send(1);	/* Esc key pressed */
	pc_keyboard_send(157);	/* Ctrl key released */
	pc_keyboard_send(184);	/* Alt key released */
	pc_keyboard_send(129);	/* Esc key released */
}

int suppress_overscan = 0;

void resetpchard_close(void)
{
	suppress_overscan = 0;

	savenvr();

        device_close_all();
	mouse_emu_close();
        closeal();
}

void resetpchard_init(void)
{
	int i = 0;

	sound_realloc_buffers();

        initalmain(0,NULL);

        device_init();
        midi_device_init();
        inital();
    
        timer_reset();
        sound_reset();
        mem_resize();
        fdc_init();
	disc_reset();

#ifndef WALTJE
	serial_init();
#endif
        machine_init();
        video_init();
        speaker_init();        

	ide_ter_disable();
	ide_qua_disable();

	if (ide_enable[2])
	{
		ide_ter_init();
	}

	if (ide_enable[3])
	{
		ide_qua_init();
	}

        resetide();
	scsi_card_init();
#ifdef USE_NETWORK
	network_reset();
#endif

        sound_card_init();
        if (mpu401_standalone_enable)
                mpu401_device_add();
        if (GUS)
                device_add(&gus_device);
        if (GAMEBLASTER)
                device_add(&cms_device);
        if (SSI2001)
                device_add(&ssi2001_device);
        if (voodoo_enabled)
                device_add(&voodoo_device);
	hdd_controller_init(hdd_controller_name);
        pc_reset();
	mouse_emu_init();
 
        loadnvr();

        shadowbios = 0;
        
        keyboard_at_reset();
        
	cpu_cache_int_enabled = cpu_cache_ext_enabled = 0;

	for (i = 0; i < CDROM_NUM; i++)
	{
		if (cdrom_drives[i].host_drive == 200)
		{
			image_reset(i);
		}
		else if ((cdrom_drives[i].host_drive >= 'A') && (cdrom_drives[i].host_drive <= 'Z'))
		{
			ioctl_reset(i);
		}
	}

	sound_cd_thread_reset();
}

void resetpchard(void)
{
	resetpchard_close();
	resetpchard_init();
}

int framecountx=0;
int sndcount=0;

int sreadlnum,swritelnum,segareads,segawrites, scycles_lost;

int serial_fifo_read, serial_fifo_write;

int emu_fps = 0;

static wchar_t wmachine[2048];
static wchar_t wcpu[2048];

void runpc(void)
{
        wchar_t s[200];
        int done=0;

        startblit();
        clockrate = machines[machine].cpu[cpu_manufacturer].cpus[cpu].rspeed;
        
        if (is386)   
        {
                if (cpu_use_dynarec)
                        exec386_dynarec(machines[machine].cpu[cpu_manufacturer].cpus[cpu].rspeed / 100);
                else
                        exec386(machines[machine].cpu[cpu_manufacturer].cpus[cpu].rspeed / 100);
        }
        else if (AT)
	{
                exec386(machines[machine].cpu[cpu_manufacturer].cpus[cpu].rspeed / 100);
	}
        else
	{
                execx86(machines[machine].cpu[cpu_manufacturer].cpus[cpu].rspeed / 100);
	}
        
                keyboard_process();
                pollmouse();
                if (joystick_type != 7)  joystick_poll();
		endblit();

                framecountx++;
                framecount++;
                if (framecountx>=100)
                {
                        framecountx=0;
                        mips=(float)insc/1000000.0f;
                        insc=0;
                        flops=(float)fpucount/1000000.0f;
                        fpucount=0;
                        sreadlnum=readlnum;
                        swritelnum=writelnum;
                        segareads=egareads;
                        segawrites=egawrites;
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

                        updatestatus=1;
                        readlnum=writelnum=0;
                        egareads=egawrites=0;
                        cycles_lost = 0;
                        mmuflush=0;
                        emu_fps = frames;
                        frames = 0;
                }
                if (win_title_update)
                {
                        win_title_update=0;
			mbstowcs(wmachine, machine_getname(), strlen(machine_getname()) + 1);
			mbstowcs(wcpu, machines[machine].cpu[cpu_manufacturer].cpus[cpu].name, strlen(machines[machine].cpu[cpu_manufacturer].cpus[cpu].name) + 1);
                        _swprintf(s, L"%s v%s - %i%% - %s - %s - %s", EMU_NAME_W, EMU_VERSION_W, fps, wmachine, wcpu, (!mousecapture) ? plat_get_string_from_id(IDS_2077) : ((mouse_get_type(mouse_type) & MOUSE_TYPE_3BUTTON) ? plat_get_string_from_id(IDS_2078) : plat_get_string_from_id(IDS_2079)));
                        set_window_title(s);
                }
                done++;
}

void fullspeed(void)
{
        cpuspeed2=cpuspeed;
        if (!atfullspeed)
        {
                printf("Set fullspeed - %i %i %i\n",is386,AT,cpuspeed2);
                if (AT)
                        setpitclock(machines[machine].cpu[cpu_manufacturer].cpus[cpu].rspeed);
                else
                        setpitclock(14318184.0);
        }
        atfullspeed=1;
        nvr_recalc();
}

void speedchanged(void)
{
        if (AT)
                setpitclock(machines[machine].cpu[cpu_manufacturer].cpus[cpu].rspeed);
        else
                setpitclock(14318184.0);
        nvr_recalc();
}

void closepc(void)
{
	int i = 0;
	for (i = 0; i < CDROM_NUM; i++)
	{
        	cdrom_drives[i].handler->exit(i);
	}
        dumppic();
	for (i = 0; i < FDD_NUM; i++)
	{
	        disc_close(i);
	}
        dumpregs(0);
        closevideo();
        device_close_all();
        midi_close();
#ifdef USE_NETWORK
	network_close();
#endif
}
