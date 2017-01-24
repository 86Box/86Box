/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "86box.h"
#include "ibm.h"
#include "device.h"

#include "ali1429.h"
#include "cdrom.h"
#include "cdrom-ioctl.h"
#include "disc.h"
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
#include "buslogic.h"
#include "cdrom.h"
#include "scsi.h"
#include "ide.h"
#include "keyboard.h"
#include "keyboard_at.h"
#include "model.h"
#include "mouse.h"
#include "nvr.h"
#include "pic.h"
#include "pit.h"
#include "plat-joystick.h"
#include "plat-mouse.h"
#include "serial.h"
#include "sound.h"
#include "sound_cms.h"
#include "sound_opl.h"
#include "sound_sb.h"
#include "sound_ssi2001.h"
#include "timer.h"
#include "vid_voodoo.h"
#include "video.h"
#include "amstrad.h"
#include "nethandler.h"
#define NE2000      1
#define RTL8029AS   2
uint8_t ethif;
int inum;

char nvr_path[1024];
int path_len;

int window_w, window_h, window_x, window_y, window_remember;

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
// FILE *pclogf;
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

void fatal(const char *format, ...)
{
   va_list ap;
   va_start(ap, format);
   vprintf(format, ap);
   va_end(ap);
   fflush(stdout);
   savenvr();
   saveconfig();
   dumppic();
   dumpregs();
   fflush(stdout);
   exit(-1);
}

uint8_t cgastat;

int pollmouse_delay = 2;
void pollmouse()
{
        int x, y, z;
//        return;
        pollmouse_delay--;
        if (pollmouse_delay) return;
        pollmouse_delay = 2;
        mouse_poll_host();
        mouse_get_mickeys(&x, &y, &z);
        if (mouse_poll)
           mouse_poll(x, y, z, mouse_buttons);
//        if (mousecapture) position_mouse(64,64);
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
        //timer_reset();
        dma_reset();
        fdc_reset();
        pic_reset();
        pit_reset();
        serial_reset();

        if (AT)
                setpitclock(models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed);
        else
                setpitclock(14318184.0);
        
//        sb_reset();

        ali1429_reset();
//        video_init();
}
#undef printf
void initpc(int argc, char *argv[])
{
        char *p;
        char *config_file = NULL;
        int c, i;
	FILE *ff;
//        allegro_init();
        get_executable_name(pcempath,511);
        pclog("executable_name = %s\n", pcempath);
        p=get_filename(pcempath);
        *p=0;
        pclog("path = %s\n", pcempath);        

        for (c = 1; c < argc; c++)
        {
                if (!strcasecmp(argv[c], "--help"))
                {
                        printf("PCem command line options :\n\n");
                        printf("--config file.cfg - use given config file as initial configuration\n");
                        printf("--fullscreen      - start in fullscreen mode\n");
                        exit(-1);
                }
                else if (!strcasecmp(argv[c], "--fullscreen"))
                {
                        start_in_fullscreen = 1;
                }
                else if (!strcasecmp(argv[c], "--config"))
                {
                        if ((c+1) == argc)
                                break;
                        config_file = argv[c+1];
                        c++;
                }
        }

        keyboard_init();
        mouse_init();
        joystick_init();
        midi_init();

	if (config_file == NULL)
	{
	        append_filename(config_file_default, pcempath, "86box.cfg", 511);
	}
	else
	{
	        append_filename(config_file_default, pcempath, config_file, 511);
	}
        
        loadconfig(config_file);
        pclog("Config loaded\n");
        if (config_file)
                saveconfig();

        cpuspeed2=(AT)?2:1;
//        cpuspeed2=cpuspeed;
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
				ff = fopen(cdrom_iso[i].iso_path, "rb");
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

	if (network_card_current != 0)
	{
		vlan_reset();	//NETWORK
	}
	network_card_init(network_card_current);

        disc_load(0, discfns[0]);
        disc_load(1, discfns[1]);
        disc_load(2, discfns[2]);
        disc_load(3, discfns[3]);
                
        //loadfont();
        loadnvr();
        sound_init();

        resetide();
	if (buslogic_enabled)
	{
		device_add(&BuslogicDevice);
	}		

        pit_reset();        
/*        if (romset==ROM_AMI386 || romset==ROM_AMI486) */fullspeed();
        ali1429_reset();
//        CPUID=(is486 && (cpuspeed==7 || cpuspeed>=9));
//        pclog("Init - CPUID %i %i\n",CPUID,cpuspeed);
        shadowbios=0;
        
	for (i = 0; i < CDROM_NUM; i++)
	{
		if (cdrom_drives[i].host_drive == 0)
		{
		        cdrom_null_reset(i);
		}
		else
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
//        cpuspeed2=(AT)?2:1;
//        atfullspeed=0;
///*        if (romset==ROM_AMI386 || romset==ROM_AMI486) */fullspeed();
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
	// mem_add_bios();
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

	if (network_card_current != 0)
	{
		vlan_reset();	//NETWORK
	}
	network_card_init(network_card_current);      

        sound_card_init(sound_card_current);
        if (GUS)
                device_add(&gus_device);
        if (GAMEBLASTER)
                device_add(&cms_device);
        if (SSI2001)
                device_add(&ssi2001_device);
        if (voodoo_enabled)
                device_add(&voodoo_device);        
        pc_reset();
        
	for (i = 0; i < CDROM_NUM; i++)
	{
		if (cdrom_drives[i].bus_type)
		{
			SCSIReset(cdrom_drives[i].scsi_device_id, cdrom_drives[i].scsi_device_lun);
		}
	}		

        resetide();
	if (buslogic_enabled)
	{
		device_add(&BuslogicDevice);
	}
 
        loadnvr();

//        cpuspeed2 = (AT)?2:1;
//        atfullspeed = 0;
//        setpitclock(models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed);

        shadowbios = 0;
        ali1429_reset();
        
        keyboard_at_reset();
        
	cpu_cache_int_enabled = cpu_cache_ext_enabled = 0;

//        output=3;

	for (i = 0; i < CDROM_NUM; i++)
	{
		if (cdrom_drives[i].host_drive == 0)
		{
		        cdrom_null_reset(i);
		}
		else
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

void runpc()
{
        char s[200];
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
                exec386(models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed / 100);
        else
                execx86(models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed / 100);
        
                keyboard_poll_host();
                keyboard_process();
//                checkkeys();
                pollmouse();
                if (joystick_type != 7)  joystick_poll();
        endblit();

                framecountx++;
                framecount++;
                if (framecountx>=100)
                {
                        // pclog("onesec\n");
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
                        sprintf(s, "86Box v%s - %i%% - %s - %s - %s", emulator_version, fps, model_getname(), models[model].cpu[cpu_manufacturer].cpus[cpu].name, (!mousecapture) ? "Click to capture mouse" : ((mouse_get_type(mouse_type) & MOUSE_TYPE_3BUTTON) ? "Press F12-F8 to release mouse" : "Press F12-F8 or middle button to release mouse"));
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
//                if (is386) setpitclock(clocks[2][cpuspeed2][0]);
//                else       setpitclock(clocks[AT?1:0][cpuspeed2][0]);
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
//        ioctl_close();
        dumppic();
//        output=7;
//        setpitclock(clocks[0][0][0]);
//        while (1) runpc();
        disc_close(0);
        disc_close(1);
        disc_close(2);
        disc_close(3);
        dumpregs();
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

void loadconfig(char *fn)
{
	int c, d;
	char s[512];
        char *p;
        
        if (!fn)
                config_load(config_file_default);
        else
                config_load(fn);
        
        GAMEBLASTER = config_get_int(NULL, "gameblaster", 0);
        GUS = config_get_int(NULL, "gus", 0);
        SSI2001 = config_get_int(NULL, "ssi2001", 0);
        voodoo_enabled = config_get_int(NULL, "voodoo", 0);
		buslogic_enabled = config_get_int(NULL, "buslogic", 0);

	scsi_model = config_get_int(NULL, "scsi_model", 1);
	scsi_base = config_get_int(NULL, "scsi_base", 0x330);
	scsi_irq = config_get_int(NULL, "scsi_irq", 11);
	scsi_dma = config_get_int(NULL, "scsi_dma", 6);

	//network
	ethif = config_get_int(NULL, "netinterface", 1);
        if (ethif >= inum)
            inum = ethif + 1;
        network_card_current = config_get_int(NULL, "netcard", NE2000);

        model = config_get_int(NULL, "model", 14);

        if (model >= model_count())
                model = model_count() - 1;

        romset = model_getromset();
        cpu_manufacturer = config_get_int(NULL, "cpu_manufacturer", 0);
        cpu = config_get_int(NULL, "cpu", 0);
        cpu_use_dynarec = config_get_int(NULL, "cpu_use_dynarec", 0);
        
	cpu_waitstates = config_get_int(NULL, "cpu_waitstates", 0);
                
        gfxcard = config_get_int(NULL, "gfxcard", 0);
        video_speed = config_get_int(NULL, "video_speed", 3);
        sound_card_current = config_get_int(NULL, "sndcard", SB2);

	// d86f_unregister(0);
	// d86f_unregister(1);

        p = (char *)config_get_string(NULL, "disc_a", "");
        if (p) strcpy(discfns[0], p);
        else   strcpy(discfns[0], "");
        ui_writeprot[0] = config_get_int(NULL, "disc_a_writeprot", 0);

        p = (char *)config_get_string(NULL, "disc_b", "");
        if (p) strcpy(discfns[1], p);
        else   strcpy(discfns[1], "");
        ui_writeprot[1] = config_get_int(NULL, "disc_b_writeprot", 0);

        p = (char *)config_get_string(NULL, "disc_3", "");
        if (p) strcpy(discfns[2], p);
        else   strcpy(discfns[2], "");
        ui_writeprot[2] = config_get_int(NULL, "disc_3_writeprot", 0);

        p = (char *)config_get_string(NULL, "disc_4", "");
        if (p) strcpy(discfns[3], p);
        else   strcpy(discfns[3], "");
        ui_writeprot[3] = config_get_int(NULL, "disc_4_writeprot", 0);

        mem_size = config_get_int(NULL, "mem_size", 4096);
        if (mem_size < ((models[model].flags & MODEL_AT) ? models[model].min_ram*1024 : models[model].min_ram))
                mem_size = ((models[model].flags & MODEL_AT) ? models[model].min_ram*1024 : models[model].min_ram);

        cdrom_drives[0].host_drive = config_get_int(NULL, "cdrom_1_host_drive", 0);
	cdrom_drives[0].prev_host_drive = cdrom_drives[0].host_drive;
        cdrom_drives[0].enabled = config_get_int(NULL, "cdrom_1_enabled", 0);
        cdrom_drives[0].sound_on = config_get_int(NULL, "cdrom_1_sound_on", 1);
        cdrom_drives[0].bus_type = config_get_int(NULL, "cdrom_1_bus_type", 0);
        cdrom_drives[0].ide_channel = config_get_int(NULL, "cdrom_1_ide_channel", 2);
        cdrom_drives[0].scsi_device_id = config_get_int(NULL, "cdrom_1_scsi_device_id", 2);
        cdrom_drives[0].scsi_device_lun = config_get_int(NULL, "cdrom_1_scsi_device_lun", 0);

        p = (char *)config_get_string(NULL, "cdrom_1_iso_path", "");
        if (p) strcpy(cdrom_iso[0].iso_path, p);
        else   strcpy(cdrom_iso[0].iso_path, "");

        cdrom_drives[1].host_drive = config_get_int(NULL, "cdrom_2_host_drive", 0);
	cdrom_drives[1].prev_host_drive = cdrom_drives[1].host_drive;
        cdrom_drives[1].enabled = config_get_int(NULL, "cdrom_2_enabled", 0);
        cdrom_drives[1].sound_on = config_get_int(NULL, "cdrom_2_sound_on", 1);
        cdrom_drives[1].bus_type = config_get_int(NULL, "cdrom_2_bus_type", 0);
        cdrom_drives[1].ide_channel = config_get_int(NULL, "cdrom_2_ide_channel", 3);
        cdrom_drives[1].scsi_device_id = config_get_int(NULL, "cdrom_2_scsi_device_id", 3);
        cdrom_drives[1].scsi_device_lun = config_get_int(NULL, "cdrom_2_scsi_device_lun", 0);

        p = (char *)config_get_string(NULL, "cdrom_2_iso_path", "");
        if (p) strcpy(cdrom_iso[1].iso_path, p);
        else   strcpy(cdrom_iso[1].iso_path, "");

        cdrom_drives[2].host_drive = config_get_int(NULL, "cdrom_3_host_drive", 0);
	cdrom_drives[2].prev_host_drive = cdrom_drives[2].host_drive;
        cdrom_drives[2].enabled = config_get_int(NULL, "cdrom_3_enabled", 0);
        cdrom_drives[2].sound_on = config_get_int(NULL, "cdrom_3_sound_on", 1);
        cdrom_drives[2].bus_type = config_get_int(NULL, "cdrom_3_bus_type", 0);
        cdrom_drives[2].ide_channel = config_get_int(NULL, "cdrom_3_ide_channel", 4);
        cdrom_drives[2].scsi_device_id = config_get_int(NULL, "cdrom_3_scsi_device_id", 4);
        cdrom_drives[2].scsi_device_lun = config_get_int(NULL, "cdrom_3_scsi_device_lun", 0);

        p = (char *)config_get_string(NULL, "cdrom_3_iso_path", "");
        if (p) strcpy(cdrom_iso[2].iso_path, p);
        else   strcpy(cdrom_iso[2].iso_path, "");

        cdrom_drives[3].host_drive = config_get_int(NULL, "cdrom_4_host_drive", 0);
	cdrom_drives[3].prev_host_drive = cdrom_drives[3].host_drive;
        cdrom_drives[3].enabled = config_get_int(NULL, "cdrom_4_enabled", 0);
        cdrom_drives[3].sound_on = config_get_int(NULL, "cdrom_4_sound_on", 1);
        cdrom_drives[3].bus_type = config_get_int(NULL, "cdrom_4_bus_type", 0);
        cdrom_drives[3].ide_channel = config_get_int(NULL, "cdrom_4_ide_channel", 5);
        cdrom_drives[3].scsi_device_id = config_get_int(NULL, "cdrom_4_scsi_device_id", 5);
        cdrom_drives[3].scsi_device_lun = config_get_int(NULL, "cdrom_4_scsi_device_lun", 0);

        p = (char *)config_get_string(NULL, "cdrom_4_iso_path", "");
        if (p) strcpy(cdrom_iso[3].iso_path, p);
        else   strcpy(cdrom_iso[3].iso_path, "");

        vid_resize = config_get_int(NULL, "vid_resize", 0);
        vid_api = config_get_int(NULL, "vid_api", 0);
        video_fullscreen_scale = config_get_int(NULL, "video_fullscreen_scale", 0);
        video_fullscreen_first = config_get_int(NULL, "video_fullscreen_first", 1);

        hdc[0].spt = config_get_int(NULL, "hdc_sectors", 0);
        hdc[0].hpc = config_get_int(NULL, "hdc_heads", 0);
        hdc[0].tracks = config_get_int(NULL, "hdc_cylinders", 0);
        p = (char *)config_get_string(NULL, "hdc_fn", "");
        if (p) strcpy(ide_fn[0], p);
        else   strcpy(ide_fn[0], "");
        hdc[1].spt = config_get_int(NULL, "hdd_sectors", 0);
        hdc[1].hpc = config_get_int(NULL, "hdd_heads", 0);
        hdc[1].tracks = config_get_int(NULL, "hdd_cylinders", 0);
        p = (char *)config_get_string(NULL, "hdd_fn", "");
        if (p) strcpy(ide_fn[1], p);
        else   strcpy(ide_fn[1], "");
        hdc[2].spt = config_get_int(NULL, "hde_sectors", 0);
        hdc[2].hpc = config_get_int(NULL, "hde_heads", 0);
        hdc[2].tracks = config_get_int(NULL, "hde_cylinders", 0);
        p = (char *)config_get_string(NULL, "hde_fn", "");
        if (p) strcpy(ide_fn[2], p);
        else   strcpy(ide_fn[2], "");
        hdc[3].spt = config_get_int(NULL, "hdf_sectors", 0);
        hdc[3].hpc = config_get_int(NULL, "hdf_heads", 0);
        hdc[3].tracks = config_get_int(NULL, "hdf_cylinders", 0);
        p = (char *)config_get_string(NULL, "hdf_fn", "");
        if (p) strcpy(ide_fn[3], p);
        else   strcpy(ide_fn[3], "");
        hdc[4].spt = config_get_int(NULL, "hdg_sectors", 0);
        hdc[4].hpc = config_get_int(NULL, "hdg_heads", 0);
        hdc[4].tracks = config_get_int(NULL, "hdg_cylinders", 0);
        p = (char *)config_get_string(NULL, "hdg_fn", "");
        if (p) strcpy(ide_fn[4], p);
        else   strcpy(ide_fn[4], "");
        hdc[5].spt = config_get_int(NULL, "hdh_sectors", 0);
        hdc[5].hpc = config_get_int(NULL, "hdh_heads", 0);
        hdc[5].tracks = config_get_int(NULL, "hdh_cylinders", 0);
        p = (char *)config_get_string(NULL, "hdh_fn", "");
        if (p) strcpy(ide_fn[5], p);
        else   strcpy(ide_fn[5], "");
        hdc[6].spt = config_get_int(NULL, "hdi_sectors", 0);
        hdc[6].hpc = config_get_int(NULL, "hdi_heads", 0);
        hdc[6].tracks = config_get_int(NULL, "hdi_cylinders", 0);
        p = (char *)config_get_string(NULL, "hdi_fn", "");
        if (p) strcpy(ide_fn[6], p);
        else   strcpy(ide_fn[6], "");
        hdc[7].spt = config_get_int(NULL, "hdj_sectors", 0);
        hdc[7].hpc = config_get_int(NULL, "hdj_heads", 0);
        hdc[7].tracks = config_get_int(NULL, "hdj_cylinders", 0);
        p = (char *)config_get_string(NULL, "hdj_fn", "");
        if (p) strcpy(ide_fn[7], p);
        else   strcpy(ide_fn[7], "");

	ide_enable[2] = config_get_int(NULL, "ide_ter_enable", 0);
	ide_irq[2] = config_get_int(NULL, "ide_ter_irq", 10);
	ide_enable[3] = config_get_int(NULL, "ide_qua_enable", 0);
	ide_irq[3] = config_get_int(NULL, "ide_qua_irq", 11);

	fdd_set_type(0, config_get_int(NULL, "drive_a_type", 1));
        fdd_set_type(1, config_get_int(NULL, "drive_b_type", 1));
        fdd_set_type(2, config_get_int(NULL, "drive_3_type", 1));
        fdd_set_type(3, config_get_int(NULL, "drive_4_type", 1));

	force_43 = config_get_int(NULL, "force_43", 0);
	enable_overscan = config_get_int(NULL, "enable_overscan", 0);
        enable_flash = config_get_int(NULL, "enable_flash", 1);

        enable_sync = config_get_int(NULL, "enable_sync", 1);

        window_w = config_get_int(NULL, "window_w", 0);
        window_h = config_get_int(NULL, "window_h", 0);
        window_x = config_get_int(NULL, "window_x", 0);
        window_y = config_get_int(NULL, "window_y", 0);
        window_remember = config_get_int(NULL, "window_remember", 0);

        joystick_type = config_get_int(NULL, "joystick_type", 0);
	mouse_type = config_get_int(NULL, "mouse_type", 0);

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

	memset(nvr_path, 0, 1024);
        p = (char *)config_get_string(NULL, "nvr_path", "nvr");
        if (p) {
		if (strlen(p) <= 992)  strcpy(nvr_path, p);
		else   strcpy(nvr_path, "nvr");
	}
        else   strcpy(nvr_path, "nvr");

	if (nvr_path[strlen(nvr_path)] != '/')
	{
		nvr_path[strlen(nvr_path)] = '/';
	}

	path_len = strlen(nvr_path);
}

char *nvr_concat(char *to_concat)
{
	memset(nvr_path + path_len, 0, 1024 - path_len);
	strcpy(nvr_path + path_len, to_concat);
	return nvr_path;
}

void saveconfig()
{
        int c, d;

        config_set_int(NULL, "gameblaster", GAMEBLASTER);
        config_set_int(NULL, "gus", GUS);
        config_set_int(NULL, "ssi2001", SSI2001);
        config_set_int(NULL, "voodoo", voodoo_enabled);
		config_set_int(NULL, "buslogic", buslogic_enabled);

	config_set_int(NULL, "scsi_model", scsi_model);
	config_set_int(NULL, "scsi_base", scsi_base);
	config_set_int(NULL, "scsi_irq", scsi_irq);
	config_set_int(NULL, "scsi_dma", scsi_dma);

	config_set_int(NULL, "netinterface", ethif);
	config_set_int(NULL, "netcard", network_card_current);

        config_set_int(NULL, "model", model);
        config_set_int(NULL, "cpu_manufacturer", cpu_manufacturer);
        config_set_int(NULL, "cpu", cpu);
        config_set_int(NULL, "cpu_use_dynarec", cpu_use_dynarec);
	config_set_int(NULL, "cpu_waitstates", cpu_waitstates);
        
        config_set_int(NULL, "gfxcard", gfxcard);
        config_set_int(NULL, "video_speed", video_speed);
        config_set_int(NULL, "sndcard", sound_card_current);
        config_set_int(NULL, "cpu_speed", cpuspeed);
        config_set_int(NULL, "has_fpu", hasfpu);
        config_set_string(NULL, "disc_a", discfns[0]);
        config_set_int(NULL, "disc_a_writeprot", ui_writeprot[0]);
        config_set_string(NULL, "disc_b", discfns[1]);
        config_set_int(NULL, "disc_b_writeprot", ui_writeprot[1]);
        config_set_string(NULL, "disc_3", discfns[2]);
        config_set_int(NULL, "disc_3_writeprot", ui_writeprot[2]);
        config_set_string(NULL, "disc_4", discfns[3]);
        config_set_int(NULL, "disc_4_writeprot", ui_writeprot[3]);
        config_set_int(NULL, "mem_size", mem_size);

        config_set_int(NULL, "cdrom_1_host_drive", cdrom_drives[0].host_drive);
        config_set_int(NULL, "cdrom_1_enabled", cdrom_drives[0].enabled);
        config_set_int(NULL, "cdrom_1_sound_on", cdrom_drives[0].sound_on);
        config_set_int(NULL, "cdrom_1_bus_type", cdrom_drives[0].bus_type);
        config_set_int(NULL, "cdrom_1_ide_channel", cdrom_drives[0].ide_channel);
        config_set_int(NULL, "cdrom_1_scsi_device_id", cdrom_drives[0].scsi_device_id);		
        config_set_int(NULL, "cdrom_1_scsi_device_lun", cdrom_drives[0].scsi_device_lun);

        config_set_string(NULL, "cdrom_1_iso_path", cdrom_iso[0].iso_path);

        config_set_int(NULL, "cdrom_2_host_drive", cdrom_drives[1].host_drive);
        config_set_int(NULL, "cdrom_2_enabled", cdrom_drives[1].enabled);
        config_set_int(NULL, "cdrom_2_sound_on", cdrom_drives[1].sound_on);
        config_set_int(NULL, "cdrom_2_bus_type", cdrom_drives[1].bus_type);
        config_set_int(NULL, "cdrom_2_ide_channel", cdrom_drives[1].ide_channel);
        config_set_int(NULL, "cdrom_2_scsi_device_id", cdrom_drives[1].scsi_device_id);
        config_set_int(NULL, "cdrom_2_scsi_device_lun", cdrom_drives[1].scsi_device_lun);

        config_set_string(NULL, "cdrom_2_iso_path", cdrom_iso[1].iso_path);

        config_set_int(NULL, "cdrom_3_host_drive", cdrom_drives[2].host_drive);
        config_set_int(NULL, "cdrom_3_enabled", cdrom_drives[2].enabled);
        config_set_int(NULL, "cdrom_3_sound_on", cdrom_drives[2].sound_on);
        config_set_int(NULL, "cdrom_3_bus_type", cdrom_drives[2].bus_type);
        config_set_int(NULL, "cdrom_3_ide_channel", cdrom_drives[2].ide_channel);
        config_set_int(NULL, "cdrom_3_scsi_device_id", cdrom_drives[2].scsi_device_id);
        config_set_int(NULL, "cdrom_3_scsi_device_lun", cdrom_drives[2].scsi_device_lun);

        config_set_string(NULL, "cdrom_3_iso_path", cdrom_iso[2].iso_path);

        config_set_int(NULL, "cdrom_4_host_drive", cdrom_drives[3].host_drive);
        config_set_int(NULL, "cdrom_4_enabled", cdrom_drives[3].enabled);
        config_set_int(NULL, "cdrom_4_sound_on", cdrom_drives[3].sound_on);
        config_set_int(NULL, "cdrom_4_bus_type", cdrom_drives[3].bus_type);
        config_set_int(NULL, "cdrom_4_ide_channel", cdrom_drives[3].ide_channel);
        config_set_int(NULL, "cdrom_4_scsi_device_id", cdrom_drives[3].scsi_device_id);
        config_set_int(NULL, "cdrom_4_scsi_device_lun", cdrom_drives[3].scsi_device_lun);

        config_set_string(NULL, "cdrom_4_iso_path", cdrom_iso[3].iso_path);

        config_set_int(NULL, "vid_resize", vid_resize);
        config_set_int(NULL, "vid_api", vid_api);
        config_set_int(NULL, "video_fullscreen_scale", video_fullscreen_scale);
        config_set_int(NULL, "video_fullscreen_first", video_fullscreen_first);
        
        config_set_int(NULL, "hdc_sectors", hdc[0].spt);
        config_set_int(NULL, "hdc_heads", hdc[0].hpc);
        config_set_int(NULL, "hdc_cylinders", hdc[0].tracks);
        config_set_string(NULL, "hdc_fn", ide_fn[0]);
        config_set_int(NULL, "hdd_sectors", hdc[1].spt);
        config_set_int(NULL, "hdd_heads", hdc[1].hpc);
        config_set_int(NULL, "hdd_cylinders", hdc[1].tracks);
        config_set_string(NULL, "hdd_fn", ide_fn[1]);
        config_set_int(NULL, "hde_sectors", hdc[2].spt);
        config_set_int(NULL, "hde_heads", hdc[2].hpc);
        config_set_int(NULL, "hde_cylinders", hdc[2].tracks);
        config_set_string(NULL, "hde_fn", ide_fn[2]);
        config_set_int(NULL, "hdf_sectors", hdc[3].spt);
        config_set_int(NULL, "hdf_heads", hdc[3].hpc);
        config_set_int(NULL, "hdf_cylinders", hdc[3].tracks);
        config_set_string(NULL, "hdf_fn", ide_fn[3]);
        config_set_int(NULL, "hdg_sectors", hdc[4].spt);
        config_set_int(NULL, "hdg_heads", hdc[4].hpc);
        config_set_int(NULL, "hdg_cylinders", hdc[4].tracks);
        config_set_string(NULL, "hdg_fn", ide_fn[4]);
        config_set_int(NULL, "hdh_sectors", hdc[5].spt);
        config_set_int(NULL, "hdh_heads", hdc[5].hpc);
        config_set_int(NULL, "hdh_cylinders", hdc[5].tracks);
        config_set_string(NULL, "hdh_fn", ide_fn[5]);
        config_set_int(NULL, "hdi_sectors", hdc[6].spt);
        config_set_int(NULL, "hdi_heads", hdc[6].hpc);
        config_set_int(NULL, "hdi_cylinders", hdc[6].tracks);
        config_set_string(NULL, "hdi_fn", ide_fn[6]);
        config_set_int(NULL, "hdj_sectors", hdc[7].spt);
        config_set_int(NULL, "hdj_heads", hdc[7].hpc);
        config_set_int(NULL, "hdj_cylinders", hdc[7].tracks);
        config_set_string(NULL, "hdj_fn", ide_fn[7]);

        config_set_int(NULL, "ide_ter_enable", ide_enable[2]);
        config_set_int(NULL, "ide_ter_irq", ide_irq[2]);
        config_set_int(NULL, "ide_qua_enable", ide_enable[3]);
        config_set_int(NULL, "ide_qua_irq", ide_irq[3]);

        config_set_int(NULL, "drive_a_type", fdd_get_type(0));
        config_set_int(NULL, "drive_b_type", fdd_get_type(1));
        config_set_int(NULL, "drive_3_type", fdd_get_type(2));
        config_set_int(NULL, "drive_4_type", fdd_get_type(3));

        config_set_int(NULL, "force_43", force_43);
        config_set_int(NULL, "enable_overscan", enable_overscan);
        config_set_int(NULL, "enable_flash", enable_flash);
        
        config_set_int(NULL, "enable_sync", enable_sync);

        config_set_int(NULL, "joystick_type", joystick_type);
        config_set_int(NULL, "mouse_type", mouse_type);

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
        
        config_save(config_file_default);
}
