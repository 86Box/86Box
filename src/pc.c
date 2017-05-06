/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "86box.h"
#include "ibm.h"
#include "device.h"

#ifndef __unix
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#undef BITMAP
#include "win.h"
#include "win-language.h"
#endif

#include "ali1429.h"
#include "cdrom.h"
#include "cdrom-ioctl.h"
#include "disc.h"
#include "disc_86f.h"
#include "disc_fdi.h"
#include "disc_imd.h"
#include "disc_img.h"
#include "disc_random.h"
#include "disc_td0.h"
#include "mem.h"
#include "x86_ops.h"
#include "codegen.h"
#include "cdrom-iso.h"
#include "cdrom-null.h"
#include "config.h"
#include "cpu.h"
#include "dma.h"
#include "fdc.h"
#include "fdd.h"
#include "gameport.h"
#include "sound_gus.h"
#include "ide.h"
#include "cdrom.h"
#include "scsi.h"
#include "keyboard.h"
#include "keyboard_at.h"
#include "mem.h"
#include "model.h"
#include "mouse.h"
#include "ne2000.h"
#include "nethandler.h"
#include "nvr.h"
#include "pic.h"
#include "pit.h"
#include "plat-joystick.h"
#include "plat-midi.h"
#include "plat-mouse.h"
#include "plat-keyboard.h"
#include "serial.h"
#include "sound.h"
#include "sound_cms.h"
#include "sound_dbopl.h"
#include "sound_opl.h"
#include "sound_sb.h"
#include "sound_speaker.h"
#include "sound_ssi2001.h"
#include "timer.h"
#include "vid_voodoo.h"
#include "video.h"
#include "amstrad.h"
#include "hdd.h"
#include "nethandler.h"
#define NE2000      1
#define RTL8029AS   2
uint8_t ethif;
int inum;

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
   msgbox_fatal(ghwnd, msg);
#endif
   dumppic();
   dumpregs(1);
   fflush(stdout);
   exit(-1);
}

uint8_t cgastat;

int pollmouse_delay = 2;
void pollmouse()
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


void onesec()
{
        fps=framecount;
        framecount=0;
        win_title_update=1;
}

void pc_reset()
{
        cpu_set();
        resetx86();
        dma_reset();
        fdc_reset();
        pic_reset();
        serial_reset();

        if (AT)
                setpitclock(models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed);
        else
                setpitclock(14318184.0);

        ali1429_reset();
}
#undef printf
void initpc(int argc, wchar_t *argv[])
{
        wchar_t *p;
        wchar_t *config_file = NULL;
        int c, i;
	FILE *ff;
        get_executable_name(pcempath, 511);
        pclog("executable_name = %ws\n", pcempath);
        p=get_filename_w(pcempath);
        *p=L'\0';
        pclog("path = %ws\n", pcempath);        

        for (c = 1; c < argc; c++)
        {
                if (!_wcsicmp(argv[c], L"--help"))
                {
                        printf("PCem command line options :\n\n");
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

			/* .. and then exit. */
			exit(0);
		}
        }

        keyboard_init();
        mouse_init();
        midi_init();

	if (config_file == NULL)
	{
	        append_filename_w(config_file_default, pcempath, L"86box.cfg", 511);
	}
	else
	{
	        append_filename_w(config_file_default, pcempath, config_file, 511);
	}

	disc_random_init();
        
        loadconfig(config_file);
        pclog("Config loaded\n");
        if (config_file)
                saveconfig();

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

		if (cdrom_drives[i].host_drive == 0)
		{
		        cdrom_null_open(i, cdrom_drives[i].host_drive);
		}
		else
		{
			if (cdrom_drives[i].host_drive == 200)
			{
				ff = _wfopen(cdrom_iso[i].iso_path, L"rb");
				if (ff)
				{
					fclose(ff);
					iso_open(i, cdrom_iso[i].iso_path);
				}
				else
				{
					cdrom_drives[i].host_drive = 0;
					cdrom_null_open(i, cdrom_drives[i].host_drive);
				}
			}
			else
			{
				ioctl_open(i, cdrom_drives[i].host_drive);
			}
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
        ali1429_reset();
        shadowbios=0;
        
	for (i = 0; i < CDROM_NUM; i++)
	{
		if (cdrom_drives[i].host_drive != 0)
		{
			if (cdrom_drives[i].host_drive == 200)
			{
				iso_reset(i);
			}
			else
			{
				ioctl_reset(i);
			}
		}
	}
}

void resetpc()
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

void resetpc_cad()
{
	pc_keyboard_send(29);	/* Ctrl key pressed */
	pc_keyboard_send(56);	/* Alt key pressed */
	pc_keyboard_send(83);	/* Delete key pressed */
	pc_keyboard_send(157);	/* Ctrl key released */
	pc_keyboard_send(184);	/* Alt key released */
	pc_keyboard_send(211);	/* Delete key released */
}

int suppress_overscan = 0;

void resetpchard()
{
	int i = 0;

	suppress_overscan = 0;

	savenvr();
	saveconfig();

        device_close_all();
	mouse_emu_close();
        device_init();
        
        midi_close();
        midi_init();
        
        timer_reset();
        sound_reset();
        mem_resize();
        fdc_init();
	disc_reset();
        
        model_init();
	mouse_emu_init();
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

	network_card_init();

	for (i = 0; i < CDROM_NUM; i++)
	{
		if (cdrom_drives[i].bus_type)
		{
			SCSIReset(cdrom_drives[i].scsi_device_id, cdrom_drives[i].scsi_device_lun);
		}
	}		

        resetide();
	scsi_card_init();

        sound_card_init();
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
 
        loadnvr();

        shadowbios = 0;
        ali1429_reset();
        
        keyboard_at_reset();
        
	cpu_cache_int_enabled = cpu_cache_ext_enabled = 0;

	for (i = 0; i < CDROM_NUM; i++)
	{
		if (cdrom_drives[i].host_drive != 0)
		{
			if (cdrom_drives[i].host_drive == 200)
			{
				iso_reset(i);
			}
			else
			{
				ioctl_reset(i);
			}
		}
	}

	sound_cd_thread_reset();
}

char romsets[17][40]={"IBM PC","IBM XT","Generic Turbo XT","Euro PC","Tandy 1000","Amstrad PC1512","Sinclair PC200","Amstrad PC1640","IBM AT","AMI 286 clone","Dell System 200","Misc 286","IBM AT 386","Misc 386","386 clone","486 clone","486 clone 2"};
char clockspeeds[3][12][16]=
{
        {"4.77MHz","8MHz","10MHz","12MHz","16MHz"},
        {"8MHz","12MHz","16MHz","20MHz","25MHz"},
        {"16MHz","20MHz","25MHz","33MHz","40MHz","50MHz","66MHz","75MHz","80MHz","100MHz","120MHz","133MHz"},
};
int framecountx=0;
int sndcount=0;
int oldat70hz;

int sreadlnum,swritelnum,segareads,segawrites, scycles_lost;

int serial_fifo_read, serial_fifo_write;

int emu_fps = 0;

static WCHAR wmodel[2048];
static WCHAR wcpu[2048];

void runpc()
{
        wchar_t s[200];
        int done=0;

        startblit();
        clockrate = models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed;
        
        if (is386)   
        {
                if (cpu_use_dynarec)
                        exec386_dynarec(models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed / 100);
                else
                        exec386(models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed / 100);
        }
        else if (AT)
	{
                exec386(models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed / 100);
	}
        else
	{
                execx86(models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed / 100);
	}
        
                keyboard_poll_host();
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
			mbstowcs(wmodel, model_getname(), strlen(model_getname()) + 1);
			mbstowcs(wcpu, models[model].cpu[cpu_manufacturer].cpus[cpu].name, strlen(models[model].cpu[cpu_manufacturer].cpus[cpu].name) + 1);
                        _swprintf(s, L"86Box v%s - %i%% - %s - %s - %s", emulator_version_w, fps, wmodel, wcpu, (!mousecapture) ? win_language_get_string_from_id(2077) : ((mouse_get_type(mouse_type) & MOUSE_TYPE_3BUTTON) ? win_language_get_string_from_id(2078) : win_language_get_string_from_id(2079)));
                        set_window_title(s);
                }
                done++;
}

void fullspeed()
{
        cpuspeed2=cpuspeed;
        if (!atfullspeed)
        {
                printf("Set fullspeed - %i %i %i\n",is386,AT,cpuspeed2);
                if (AT)
                        setpitclock(models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed);
                else
                        setpitclock(14318184.0);
        }
        atfullspeed=1;
        nvr_recalc();
}

void speedchanged()
{
        if (AT)
                setpitclock(models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed);
        else
                setpitclock(14318184.0);
        nvr_recalc();
}

void closepc()
{
	int i = 0;
	for (i = 0; i < CDROM_NUM; i++)
	{
        	cdrom_drives[i].handler->exit(i);
	}
        dumppic();
        disc_close(0);
        disc_close(1);
        disc_close(2);
        disc_close(3);
        dumpregs(0);
        closevideo();
        device_close_all();
        midi_close();
}

/*int main()
{
        initpc();
        while (!key[KEY_F11])
        {
                runpc();
        }
        closepc();
        return 0;
}

END_OF_MAIN();*/

void loadconfig(wchar_t *fn)
{
	int c, d;
	char s[512];
        char *p;
        WCHAR *wp, *wq;
	char temps[512];
        
        if (!fn)
                config_load(config_file_default);
        else
                config_load(fn);
        
        GAMEBLASTER = config_get_int(NULL, "gameblaster", 0);
        GUS = config_get_int(NULL, "gus", 0);
        SSI2001 = config_get_int(NULL, "ssi2001", 0);
        voodoo_enabled = config_get_int(NULL, "voodoo", 0);

	/* SCSI */
        p = (char *)config_get_string(NULL, "scsicard", "");
        if (p)
                scsi_card_current = scsi_card_get_from_internal_name(p);
        else
                scsi_card_current = 0;

	/* network */
	ethif = config_get_int(NULL, "netinterface", 1);
        if (ethif >= inum)
            inum = ethif + 1;
        p = (char *)config_get_string(NULL, "netcard", "");
        if (p)
                network_card_current = network_card_get_from_internal_name(p);
        else
                network_card_current = 0;
	ne2000_generate_maclocal(config_get_int(NULL, "maclocal", -1));
	ne2000_generate_maclocal_pci(config_get_int(NULL, "maclocal_pci", -1));

        p = (char *)config_get_string(NULL, "model", "");
        if (p)
                model = model_get_model_from_internal_name(p);
        else
                model = 0;

        if (model >= model_count())
                model = model_count() - 1;

        romset = model_getromset();
        cpu_manufacturer = config_get_int(NULL, "cpu_manufacturer", 0);
        cpu = config_get_int(NULL, "cpu", 0);
        cpu_use_dynarec = config_get_int(NULL, "cpu_use_dynarec", 0);
        
	cpu_waitstates = config_get_int(NULL, "cpu_waitstates", 0);
                
        p = (char *)config_get_string(NULL, "gfxcard", "");
        if (p)
                gfxcard = video_get_video_from_internal_name(p);
        else
                gfxcard = 0;
        video_speed = config_get_int(NULL, "video_speed", 3);
        p = (char *)config_get_string(NULL, "sndcard", "");
        if (p)
                sound_card_current = sound_card_get_from_internal_name(p);
        else
                sound_card_current = 0;

        mem_size = config_get_int(NULL, "mem_size", 4096);
        if (mem_size < ((models[model].flags & MODEL_AT) ? models[model].min_ram*1024 : models[model].min_ram))
                mem_size = ((models[model].flags & MODEL_AT) ? models[model].min_ram*1024 : models[model].min_ram);
 
	for (c = 0; c < FDD_NUM; c++)
	{
		sprintf(temps, "fdd_%02i_type", c + 1);
	        p = (char *)config_get_string(NULL, temps, (c < 2) ? "525_2dd" : "none");
        	if (p)
                	fdd_set_type(c, fdd_get_from_internal_name(p));
	        else
        	        fdd_set_type(c, (c < 2) ? 2 : 0);

		sprintf(temps, "fdd_%02i_fn", c + 1);
	        wp = (WCHAR *)config_get_wstring(NULL, temps, L"");
        	if (wp) memcpy(discfns[c], wp, 512);
	        else    {
			memcpy(discfns[c], L"", 2);
			discfns[c][0] = L'\0';
		}
		printf("Floppy: %ws\n", discfns[c]);
		sprintf(temps, "fdd_%02i_writeprot", c + 1);
	        ui_writeprot[c] = config_get_int(NULL, temps, 0);
	}

        p = (char *)config_get_string(NULL, "hdd_controller", "");
        if (p)
                strncpy(hdd_controller_name, p, sizeof(hdd_controller_name)-1);
        else
                strncpy(hdd_controller_name, "none", sizeof(hdd_controller_name)-1);        

	memset(temps, 0, 512);
	for (c = 2; c < 4; c++)
	{
		sprintf(temps, "ide_%02i_enable", c + 1);
		ide_enable[c] = config_get_int(NULL, temps, 0);
		sprintf(temps, "ide_%02i_irq", c + 1);
		ide_irq[c] = config_get_int(NULL, temps, 8 + c);
	}

	memset(temps, 0, 512);
	for (c = 0; c < HDC_NUM; c++)
	{
		sprintf(temps, "hdd_%02i_sectors", c + 1);
		hdc[c].spt = config_get_int(NULL, temps, 0);
		sprintf(temps, "hdd_%02i_heads", c + 1);
		hdc[c].hpc = config_get_int(NULL, temps, 0);
		sprintf(temps, "hdd_%02i_cylinders", c + 1);
		hdc[c].tracks = config_get_int(NULL, temps, 0);
		sprintf(temps, "hdd_%02i_bus_type", c + 1);
		hdc[c].bus = config_get_int(NULL, temps, 0);
		sprintf(temps, "hdd_%02i_mfm_channel", c + 1);
		hdc[c].mfm_channel = config_get_int(NULL, temps, 0);
		sprintf(temps, "hdd_%02i_ide_channel", c + 1);
		hdc[c].ide_channel = config_get_int(NULL, temps, 0);
		sprintf(temps, "hdd_%02i_scsi_device_id", c + 1);
		hdc[c].scsi_id = config_get_int(NULL, temps, (c < 7) ? c : ((c < 15) ? (c + 1) : 15));
		sprintf(temps, "hdd_%02i_scsi_device_lun", c + 1);
		hdc[c].scsi_lun = config_get_int(NULL, temps, 0);
		sprintf(temps, "hdd_%02i_fn", c + 1);
	        wp = (WCHAR *)config_get_wstring(NULL, temps, L"");
        	if (wp) memcpy(hdd_fn[c], wp, 512);
	        else    {
			memcpy(hdd_fn[c], L"", 2);
			hdd_fn[c][0] = L'\0';
		}
	}

	memset(temps, 0, 512);
	for (c = 0; c < CDROM_NUM; c++)
	{
		sprintf(temps, "cdrom_%02i_host_drive", c + 1);
		cdrom_drives[c].host_drive = config_get_int(NULL, temps, 0);
		cdrom_drives[c].prev_host_drive = cdrom_drives[c].host_drive;
		sprintf(temps, "cdrom_%02i_enabled", c + 1);
		cdrom_drives[c].enabled = config_get_int(NULL, temps, 0);
		sprintf(temps, "cdrom_%02i_sound_on", c + 1);
		cdrom_drives[c].sound_on = config_get_int(NULL, temps, 1);
		sprintf(temps, "cdrom_%02i_bus_type", c + 1);
		cdrom_drives[c].bus_type = config_get_int(NULL, temps, 0);
		sprintf(temps, "cdrom_%02i_atapi_dma", c + 1);
		cdrom_drives[c].atapi_dma = config_get_int(NULL, temps, 0);
		sprintf(temps, "cdrom_%02i_ide_channel", c + 1);
		cdrom_drives[c].ide_channel = config_get_int(NULL, temps, 2);
		sprintf(temps, "cdrom_%02i_scsi_device_id", c + 1);
		cdrom_drives[c].scsi_device_id = config_get_int(NULL, temps, c + 2);
		sprintf(temps, "cdrom_%02i_scsi_device_lun", c + 1);
		cdrom_drives[c].scsi_device_lun = config_get_int(NULL, temps, 0);

		sprintf(temps, "cdrom_%02i_iso_path", c + 1);
	        wp = (WCHAR *)config_get_wstring(NULL, temps, L"");
        	if (wp) memcpy(cdrom_iso[c].iso_path, wp, 512);
	        else    {
			memcpy(cdrom_iso[c].iso_path, L"", 2);
			cdrom_iso[c].iso_path[0] = L'\0';
		}
	}

        vid_resize = config_get_int(NULL, "vid_resize", 0);
        vid_api = config_get_int(NULL, "vid_api", 0);
        video_fullscreen_scale = config_get_int(NULL, "video_fullscreen_scale", 0);
        video_fullscreen_first = config_get_int(NULL, "video_fullscreen_first", 1);

	force_43 = config_get_int(NULL, "force_43", 0);
	scale = config_get_int(NULL, "scale", 1);
	enable_overscan = config_get_int(NULL, "enable_overscan", 0);
        enable_flash = config_get_int(NULL, "enable_flash", 1);

        enable_sync = config_get_int(NULL, "enable_sync", 1);
        opl3_type = config_get_int(NULL, "opl3_type", 1);

        window_w = config_get_int(NULL, "window_w", 0);
        window_h = config_get_int(NULL, "window_h", 0);
        window_x = config_get_int(NULL, "window_x", 0);
        window_y = config_get_int(NULL, "window_y", 0);
        window_remember = config_get_int(NULL, "window_remember", 0);

        joystick_type = config_get_int(NULL, "joystick_type", 0);
        p = (char *)config_get_string(NULL, "mouse_type", "");
        if (p)
                mouse_type = mouse_get_from_internal_name(p);
        else
                mouse_type = 0;

	enable_xtide = config_get_int(NULL, "enable_xtide", 1);
	enable_external_fpu = config_get_int(NULL, "enable_external_fpu", 0);

        for (c = 0; c < joystick_get_max_joysticks(joystick_type); c++)
        {
                sprintf(s, "joystick_%i_nr", c);
                joystick_state[c].plat_joystick_nr = config_get_int("Joysticks", s, 0);

                if (joystick_state[c].plat_joystick_nr)
                {
                        for (d = 0; d < joystick_get_axis_count(joystick_type); d++)
                        {                        
                                sprintf(s, "joystick_%i_axis_%i", c, d);
                                joystick_state[c].axis_mapping[d] = config_get_int("Joysticks", s, d);
                        }
                        for (d = 0; d < joystick_get_button_count(joystick_type); d++)
                        {                        
                                sprintf(s, "joystick_%i_button_%i", c, d);
                                joystick_state[c].button_mapping[d] = config_get_int("Joysticks", s, d);
                        }
                        for (d = 0; d < joystick_get_pov_count(joystick_type); d++)
                        {                        
                                sprintf(s, "joystick_%i_pov_%i_x", c, d);
                                joystick_state[c].pov_mapping[d][0] = config_get_int("Joysticks", s, d);
                                sprintf(s, "joystick_%i_pov_%i_y", c, d);
                                joystick_state[c].pov_mapping[d][1] = config_get_int("Joysticks", s, d);
                        }
                }
        }

	memset(nvr_path, 0, 2048);
        wp = (char *)config_get_wstring(NULL, "nvr_path", "nvr");
        if (wp) {
		if (strlen(wp) <= 992)  wcscpy(nvr_path, wp);
		else
		{
			append_filename_w(nvr_path, pcempath, L"nvr", 511);
		}
	}
        else   append_filename_w(nvr_path, pcempath, L"nvr", 511);

	if (nvr_path[wcslen(nvr_path) - 1] != L'/')
	{
		if (nvr_path[wcslen(nvr_path) - 1] != L'\\')
		{
			nvr_path[wcslen(nvr_path)] = L'/';
			nvr_path[wcslen(nvr_path) + 1] = L'\0';
		}
	}

	path_len = wcslen(nvr_path);

        serial_enabled[0] = config_get_int(NULL, "serial1_enabled", 1);
        serial_enabled[1] = config_get_int(NULL, "serial2_enabled", 1);
        lpt_enabled = config_get_int(NULL, "lpt_enabled", 1);
        bugger_enabled = config_get_int(NULL, "bugger_enabled", 0);
}

wchar_t *nvr_concat(wchar_t *to_concat)
{
	char *p = (char *) nvr_path;
	p += (path_len * 2);
	wchar_t *wp = (wchar_t *) p;

	memset(wp, 0, (1024 - path_len) * 2);
	wcscpy(wp, to_concat);
	return nvr_path;
}

void saveconfig()
{
        int c, d;

	char temps[512];

        config_set_int(NULL, "gameblaster", GAMEBLASTER);
        config_set_int(NULL, "gus", GUS);
        config_set_int(NULL, "ssi2001", SSI2001);
        config_set_int(NULL, "voodoo", voodoo_enabled);

	config_set_string(NULL, "scsicard", scsi_card_get_internal_name(scsi_card_current));

	config_set_int(NULL, "netinterface", ethif);
	config_set_string(NULL, "netcard", network_card_get_internal_name(network_card_current));
	config_set_int(NULL, "maclocal", net2000_get_maclocal());
	config_set_int(NULL, "maclocal_pci", net2000_get_maclocal_pci());

        config_set_string(NULL, "model", model_get_internal_name());
        config_set_int(NULL, "cpu_manufacturer", cpu_manufacturer);
        config_set_int(NULL, "cpu", cpu);
        config_set_int(NULL, "cpu_use_dynarec", cpu_use_dynarec);
	config_set_int(NULL, "cpu_waitstates", cpu_waitstates);
        
        config_set_string(NULL, "gfxcard", video_get_internal_name(video_old_to_new(gfxcard)));
        config_set_int(NULL, "video_speed", video_speed);
	config_set_string(NULL, "sndcard", sound_card_get_internal_name(sound_card_current));
        config_set_int(NULL, "cpu_speed", cpuspeed);
        config_set_int(NULL, "has_fpu", hasfpu);

        config_set_int(NULL, "mem_size", mem_size);

	memset(temps, 0, 512);
	for (c = 0; c < FDD_NUM; c++)
	{
		sprintf(temps, "fdd_%02i_type", c + 1);
	        config_set_string(NULL, temps, fdd_get_internal_name(fdd_get_type(c)));
		sprintf(temps, "fdd_%02i_fn", c + 1);
	        config_set_wstring(NULL, temps, discfns[c]);
		sprintf(temps, "fdd_%02i_writeprot", c + 1);
	        config_set_int(NULL, temps, ui_writeprot[c]);
	}

        config_set_string(NULL, "hdd_controller", hdd_controller_name);

	memset(temps, 0, 512);
	for (c = 2; c < 4; c++)
	{
		sprintf(temps, "ide_%02i_enable", c + 1);
	        config_set_int(NULL, temps, ide_enable[c]);
		sprintf(temps, "ide_%02i_irq", c + 1);
	        config_set_int(NULL, temps, ide_irq[c]);
	}

	memset(temps, 0, 512);
	for (c = 0; c < HDC_NUM; c++)
	{
		sprintf(temps, "hdd_%02i_sectors", c + 1);
		config_set_int(NULL, temps, hdc[c].spt);
		sprintf(temps, "hdd_%02i_heads", c + 1);
		config_set_int(NULL, temps, hdc[c].hpc);
		sprintf(temps, "hdd_%02i_cylinders", c + 1);
		config_set_int(NULL, temps, hdc[c].tracks);
		sprintf(temps, "hdd_%02i_bus_type", c + 1);
		config_set_int(NULL, temps, hdc[c].bus);
		sprintf(temps, "hdd_%02i_mfm_channel", c + 1);
		config_set_int(NULL, temps, hdc[c].mfm_channel);
		sprintf(temps, "hdd_%02i_ide_channel", c + 1);
		config_set_int(NULL, temps, hdc[c].ide_channel);
		sprintf(temps, "hdd_%02i_scsi_device_id", c + 1);
		config_set_int(NULL, temps, hdc[c].scsi_id);
		sprintf(temps, "hdd_%02i_scsi_device_lun", c + 1);
		config_set_int(NULL, temps, hdc[c].scsi_lun);
		sprintf(temps, "hdd_%02i_fn", c + 1);
	        config_set_wstring(NULL, temps, hdd_fn[c]);
	}

	memset(temps, 0, 512);
	for (c = 0; c < CDROM_NUM; c++)
	{
		sprintf(temps, "cdrom_%02i_host_drive", c + 1);
		config_set_int(NULL, temps, cdrom_drives[c].host_drive);
		sprintf(temps, "cdrom_%02i_enabled", c + 1);
		config_set_int(NULL, temps, cdrom_drives[c].enabled);
		sprintf(temps, "cdrom_%02i_sound_on", c + 1);
		config_set_int(NULL, temps, cdrom_drives[c].sound_on);
		sprintf(temps, "cdrom_%02i_bus_type", c + 1);
		config_set_int(NULL, temps, cdrom_drives[c].bus_type);
		sprintf(temps, "cdrom_%02i_atapi_dma", c + 1);
		config_set_int(NULL, temps, cdrom_drives[c].atapi_dma);
		sprintf(temps, "cdrom_%02i_ide_channel", c + 1);
		config_set_int(NULL, temps, cdrom_drives[c].ide_channel);
		sprintf(temps, "cdrom_%02i_scsi_device_id", c + 1);
		config_set_int(NULL, temps, cdrom_drives[c].scsi_device_id);
		sprintf(temps, "cdrom_%02i_scsi_device_lun", c + 1);
		config_set_int(NULL, temps, cdrom_drives[c].scsi_device_lun);

		sprintf(temps, "cdrom_%02i_iso_path", c + 1);
		config_set_wstring(NULL, temps, cdrom_iso[c].iso_path);
	}

        config_set_int(NULL, "vid_resize", vid_resize);
        config_set_int(NULL, "vid_api", vid_api);
        config_set_int(NULL, "video_fullscreen_scale", video_fullscreen_scale);
        config_set_int(NULL, "video_fullscreen_first", video_fullscreen_first);

        config_set_int(NULL, "force_43", force_43);
        config_set_int(NULL, "scale", scale);
        config_set_int(NULL, "enable_overscan", enable_overscan);
        config_set_int(NULL, "enable_flash", enable_flash);
        
        config_set_int(NULL, "enable_sync", enable_sync);
        config_set_int(NULL, "opl3_type", opl3_type);

        config_set_int(NULL, "joystick_type", joystick_type);
	config_set_string(NULL, "mouse_type", mouse_get_internal_name(mouse_type));

        config_set_int(NULL, "enable_xtide", enable_xtide);
        config_set_int(NULL, "enable_external_fpu", enable_external_fpu);

        for (c = 0; c < joystick_get_max_joysticks(joystick_type); c++)
        {
                char s[80];

                sprintf(s, "joystick_%i_nr", c);
                config_set_int("Joysticks", s, joystick_state[c].plat_joystick_nr);

                if (joystick_state[c].plat_joystick_nr)
                {
                        for (d = 0; d < joystick_get_axis_count(joystick_type); d++)
                        {                        
                                sprintf(s, "joystick_%i_axis_%i", c, d);
                                config_set_int("Joysticks", s, joystick_state[c].axis_mapping[d]);
                        }
                        for (d = 0; d < joystick_get_button_count(joystick_type); d++)
                        {                        
                                sprintf(s, "joystick_%i_button_%i", c, d);
                                config_set_int("Joysticks", s, joystick_state[c].button_mapping[d]);
                        }
                        for (d = 0; d < joystick_get_pov_count(joystick_type); d++)
                        {                        
                                sprintf(s, "joystick_%i_pov_%i_x", c, d);
                                config_set_int("Joysticks", s, joystick_state[c].pov_mapping[d][0]);
                                sprintf(s, "joystick_%i_pov_%i_y", c, d);
                                config_set_int("Joysticks", s, joystick_state[c].pov_mapping[d][1]);
                        }
                }
        }

        config_set_int(NULL, "window_w", window_w);
        config_set_int(NULL, "window_h", window_h);
        config_set_int(NULL, "window_x", window_x);
        config_set_int(NULL, "window_y", window_y);
        config_set_int(NULL, "window_remember", window_remember);

        config_set_int(NULL, "serial1_enabled", serial_enabled[0]);
        config_set_int(NULL, "serial2_enabled", serial_enabled[1]);
        config_set_int(NULL, "lpt_enabled", lpt_enabled);
        config_set_int(NULL, "bugger_enabled", bugger_enabled);
        
        config_save(config_file_default);
}
