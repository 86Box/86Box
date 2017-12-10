/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Windows 86Box Settings dialog handler.
 *
 * Version:	@(#)win_settings.c	1.0.26	2017/12/09
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016,2017 Miran Grca.
 */
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#undef BITMAP
#include <commctrl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../config.h"
#include "../cpu/cpu.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "../nvr.h"
#include "../machine/machine.h"
#include "../game/gameport.h"
#include "../lpt.h"
#include "../mouse.h"
#include "../cdrom/cdrom.h"
#include "../disk/hdd.h"
#include "../disk/hdc.h"
#include "../disk/hdc_ide.h"
#include "../floppy/floppy.h"
#include "../floppy/fdd.h"
#include "../scsi/scsi.h"
#include "../network/network.h"
#include "../sound/sound.h"
#include "../sound/midi.h"
#include "../sound/snd_dbopl.h"
#include "../sound/snd_mpu401.h"
#include "../video/video.h"
#include "../video/vid_voodoo.h"
#include "../plat.h"
#include "../plat_midi.h"
#include "../ui.h"
#include "win.h"


/* Machine category */
static int temp_machine, temp_cpu_m, temp_cpu, temp_wait_states, temp_mem_size, temp_fpu, temp_sync;
#ifdef USE_DYNAREC
static int temp_dynarec;
#endif

/* Video category */
static int temp_gfxcard, temp_video_speed, temp_voodoo;

/* Input devices category */
static int temp_mouse, temp_joystick;

/* Sound category */
static int temp_sound_card, temp_midi_device, temp_mpu401, temp_SSI2001, temp_GAMEBLASTER, temp_GUS, temp_opl3_type;
static int temp_float;

/* Network category */
static int temp_net_type, temp_net_card;
static char temp_pcap_dev[520];

/* Ports category */
static char temp_lpt1_device_name[16];
static int temp_serial[2], temp_lpt;

/* Peripherals category */
static int temp_scsi_card, temp_ide_ter, temp_ide_ter_irq, temp_ide_qua, temp_ide_qua_irq;
static char temp_hdc_name[16];
static char *hdc_names[16];
static int temp_bugger;

static uint8_t temp_deviceconfig;

/* Hard disks category */
static hard_disk_t temp_hdd[HDD_NUM];

/* Removable devices category */
static int temp_fdd_types[FDD_NUM];
static int temp_fdd_turbo[FDD_NUM];
static int temp_fdd_check_bpb[FDD_NUM];
static cdrom_drive_t temp_cdrom_drives[CDROM_NUM];

static HWND hwndParentDialog, hwndChildDialog;

static int displayed_category = 0;

extern int is486;
static int romstolist[ROM_MAX], listtomachine[ROM_MAX], romstomachine[ROM_MAX], machinetolist[ROM_MAX];
static int settings_sound_to_list[20], settings_list_to_sound[20];
static int settings_midi_to_list[20], settings_list_to_midi[20];
static int settings_mouse_to_list[20], settings_list_to_mouse[20];
static int settings_scsi_to_list[20], settings_list_to_scsi[20];
static int settings_network_to_list[20], settings_list_to_network[20];



/* Show a MessageBox dialog.  This is nasty, I know.  --FvK */
static int
settings_msgbox(int type, void *arg)
{
    HWND h;
    int i;

    h = hwndMain;
    hwndMain = hwndParentDialog;

    i = ui_msgbox(type, arg);

    hwndMain = h;

    return(i);
}


/* This does the initial read of global variables into the temporary ones. */
static void win_settings_init(void)
{
	int i = 0;

	/* Machine category */
	temp_machine = machine;
	temp_cpu_m = cpu_manufacturer;
	temp_wait_states = cpu_waitstates;
	temp_cpu = cpu;
	temp_mem_size = mem_size;
#ifdef USE_DYNAREC
	temp_dynarec = cpu_use_dynarec;
#endif
	temp_fpu = enable_external_fpu;
	temp_sync = enable_sync;

	/* Video category */
	temp_gfxcard = gfxcard;
	temp_video_speed = video_speed;
	temp_voodoo = voodoo_enabled;

	/* Input devices category */
	temp_mouse = mouse_type;
	temp_joystick = joystick_type;

	/* Sound category */
	temp_sound_card = sound_card_current;
	temp_midi_device = midi_device_current;
	temp_mpu401 = mpu401_standalone_enable;
	temp_SSI2001 = SSI2001;
	temp_GAMEBLASTER = GAMEBLASTER;
	temp_GUS = GUS;
	temp_opl3_type = opl3_type;
	temp_float = sound_is_float;

	/* Network category */
	temp_net_type = network_type;
	memset(temp_pcap_dev, 0, sizeof(temp_pcap_dev));
	strcpy(temp_pcap_dev, network_pcap);
	temp_net_card = network_card;

	/* Ports category */
	strncpy(temp_lpt1_device_name, lpt1_device_name, sizeof(temp_lpt1_device_name) - 1);
	temp_serial[0] = serial_enabled[0];
	temp_serial[1] = serial_enabled[1];
	temp_lpt = lpt_enabled;

	/* Peripherals category */
	temp_scsi_card = scsi_card_current;
	strncpy(temp_hdc_name, hdc_name, sizeof(temp_hdc_name) - 1);
	temp_ide_ter = ide_enable[2];
	temp_ide_ter_irq = ide_irq[2];
	temp_ide_qua = ide_enable[3];
	temp_ide_qua_irq = ide_irq[3];
	temp_bugger = bugger_enabled;

	/* Hard disks category */
	memcpy(temp_hdd, hdd, HDD_NUM * sizeof(hard_disk_t));

	/* Removable devices category */
	for (i = 0; i < FDD_NUM; i++)
	{
		temp_fdd_types[i] = fdd_get_type(i);
		temp_fdd_turbo[i] = fdd_get_turbo(i);
		temp_fdd_check_bpb[i] = fdd_get_check_bpb(i);
	}
	memcpy(temp_cdrom_drives, cdrom_drives, CDROM_NUM * sizeof(cdrom_drive_t));

	temp_deviceconfig = 0;
}


/* This returns 1 if any variable has changed, 0 if not. */
static int win_settings_changed(void)
{
	int i = 0;
	int j = 0;

	/* Machine category */
	i = i || (machine != temp_machine);
	i = i || (cpu_manufacturer != temp_cpu_m);
	i = i || (cpu_waitstates != temp_wait_states);
	i = i || (cpu != temp_cpu);
	i = i || (mem_size != temp_mem_size);
#ifdef USE_DYNAREC
	i = i || (temp_dynarec != cpu_use_dynarec);
#endif
	i = i || (temp_fpu != enable_external_fpu);
	i = i || (temp_sync != enable_sync);

	/* Video category */
	i = i || (gfxcard != temp_gfxcard);
	i = i || (video_speed != temp_video_speed);
	i = i || (voodoo_enabled != temp_voodoo);

	/* Input devices category */
	i = i || (mouse_type != temp_mouse);
	i = i || (joystick_type != temp_joystick);

	/* Sound category */
	i = i || (sound_card_current != temp_sound_card);
	i = i || (midi_device_current != temp_midi_device);
	i = i || (mpu401_standalone_enable != temp_mpu401);
	i = i || (SSI2001 != temp_SSI2001);
	i = i || (GAMEBLASTER != temp_GAMEBLASTER);
	i = i || (GUS != temp_GUS);
	i = i || (opl3_type != temp_opl3_type);
	i = i || (sound_is_float != temp_float);

	/* Network category */
	i = i || (network_type != temp_net_type);
	i = i || strcmp(temp_pcap_dev, network_pcap);
	i = i || (network_card != temp_net_card);

	/* Ports category */
	i = i || strncmp(temp_lpt1_device_name, lpt1_device_name, sizeof(temp_lpt1_device_name) - 1);
	i = i || (temp_serial[0] != serial_enabled[0]);
	i = i || (temp_serial[1] != serial_enabled[1]);
	i = i || (temp_lpt != lpt_enabled);

	/* Peripherals category */
	i = i || (scsi_card_current != temp_scsi_card);
	i = i || strncmp(temp_hdc_name, hdc_name, sizeof(temp_hdc_name) - 1);
	i = i || (temp_ide_ter != ide_enable[2]);
	i = i || (temp_ide_ter_irq != ide_irq[2]);
	i = i || (temp_ide_qua != ide_enable[3]);
	i = i || (temp_ide_qua_irq != ide_irq[3]);
	i = i || (temp_bugger != bugger_enabled);

	/* Hard disks category */
	i = i || memcmp(hdd, temp_hdd, HDD_NUM * sizeof(hard_disk_t));

	/* Removable devices category */
	for (j = 0; j < FDD_NUM; j++)
	{
		i = i || (temp_fdd_types[j] != fdd_get_type(j));
		i = i || (temp_fdd_turbo[j] != fdd_get_turbo(j));
		i = i || (temp_fdd_check_bpb[j] != fdd_get_check_bpb(j));
	}
	i = i || memcmp(cdrom_drives, temp_cdrom_drives, CDROM_NUM * sizeof(cdrom_drive_t));

	i = i || !!temp_deviceconfig;

	return i;
}


static int settings_msgbox_reset(void)
{
	int i = 0;
	int changed = 0;

	changed = win_settings_changed();

	if (changed)
	{
		i = settings_msgbox(MBX_QUESTION, (wchar_t *)IDS_2051);

		if (i == 1) return(1);	/* no */

		if (i < 0) return(0);	/* cancel */

		return(2);		/* yes */
	} else {
		return(1);
	}
}


/* This saves the settings back to the global variables. */
static void win_settings_save(void)
{
	int i = 0;

	pc_reset_hard_close();

	/* Machine category */
	machine = temp_machine;
	romset = machine_getromset();
	cpu_manufacturer = temp_cpu_m;
	cpu_waitstates = temp_wait_states;
	cpu = temp_cpu;
	mem_size = temp_mem_size;
#ifdef USE_DYNAREC
	cpu_use_dynarec = temp_dynarec;
#endif
	enable_external_fpu = temp_fpu;
	enable_sync = temp_sync;

	/* Video category */
	gfxcard = temp_gfxcard;
	video_speed = temp_video_speed;
	voodoo_enabled = temp_voodoo;

	/* Input devices category */
	mouse_type = temp_mouse;
	joystick_type = temp_joystick;

	/* Sound category */
	sound_card_current = temp_sound_card;
	midi_device_current = temp_midi_device;
	mpu401_standalone_enable = temp_mpu401;
	SSI2001 = temp_SSI2001;
	GAMEBLASTER = temp_GAMEBLASTER;
	GUS = temp_GUS;
	opl3_type = temp_opl3_type;
	sound_is_float = temp_float;

	/* Network category */
	network_type = temp_net_type;
	memset(network_pcap, '\0', sizeof(network_pcap));
	strcpy(network_pcap, temp_pcap_dev);
	network_card = temp_net_card;

	/* Ports category */
	strncpy(lpt1_device_name, temp_lpt1_device_name, sizeof(temp_lpt1_device_name) - 1);
	serial_enabled[0] = temp_serial[0];
	serial_enabled[1] = temp_serial[1];
	lpt_enabled = temp_lpt;

	/* Peripherals category */
	scsi_card_current = temp_scsi_card;
	if (hdc_name) {
		free(hdc_name);
		hdc_name = NULL;
	}
	hdc_name = (char *) malloc(sizeof(temp_hdc_name));
	strncpy(hdc_name, temp_hdc_name, sizeof(temp_hdc_name) - 1);
	hdc_init(hdc_name);
	ide_enable[2] = temp_ide_ter;
	ide_irq[2] = temp_ide_ter_irq;
	ide_enable[3] = temp_ide_qua;
	ide_irq[3] = temp_ide_qua_irq;
	bugger_enabled = temp_bugger;

	/* Hard disks category */
	memcpy(hdd, temp_hdd, HDD_NUM * sizeof(hard_disk_t));

	/* Removable devices category */
	for (i = 0; i < FDD_NUM; i++)
	{
		fdd_set_type(i, temp_fdd_types[i]);
		fdd_set_turbo(i, temp_fdd_turbo[i]);
		fdd_set_check_bpb(i, temp_fdd_check_bpb[i]);
	}
	memcpy(cdrom_drives, temp_cdrom_drives, CDROM_NUM * sizeof(cdrom_drive_t));

	/* Mark configuration as changed. */
	config_changed = 1;

#if 1
	pc_reset_hard_init();
#else
	mem_resize();
	rom_load_bios(romset);

	ui_sb_update_panes();

	sound_realloc_buffers();

	pc_reset_hard_init();

	cpu_set();

	cpu_update_waitstates();

	config_save();

	pc_speed_changed();

	if (joystick_type != 7)  gameport_update_joystick_type();
#endif
}


static void win_settings_machine_recalc_cpu(HWND hdlg)
{
	HWND h;
	int temp_romset = 0;
#ifdef USE_DYNAREC
        int cpu_flags;
#endif
        int cpu_type;

	temp_romset = machine_getromset_ex(temp_machine);

	h = GetDlgItem(hdlg, IDC_COMBO_WS);
	cpu_type = machines[romstomachine[temp_romset]].cpu[temp_cpu_m].cpus[temp_cpu].cpu_type;
	if ((cpu_type >= CPU_286) && (cpu_type <= CPU_386DX))
	{
		EnableWindow(h, TRUE);
	}
	else
	{
		EnableWindow(h, FALSE);
	}

#ifdef USE_DYNAREC
	h=GetDlgItem(hdlg, IDC_CHECK_DYNAREC);
	cpu_flags = machines[romstomachine[temp_romset]].cpu[temp_cpu_m].cpus[temp_cpu].cpu_flags;
	if (!(cpu_flags & CPU_SUPPORTS_DYNAREC) && (cpu_flags & CPU_REQUIRES_DYNAREC))
	{
		fatal("Attempting to select a CPU that requires the recompiler and does not support it at the same time\n");
	}
	if (!(cpu_flags & CPU_SUPPORTS_DYNAREC) || (cpu_flags & CPU_REQUIRES_DYNAREC))
	{
		if (!(cpu_flags & CPU_SUPPORTS_DYNAREC))
		{
			temp_dynarec = 0;
		}
		if (cpu_flags & CPU_REQUIRES_DYNAREC)
		{
			temp_dynarec = 1;
		}
                SendMessage(h, BM_SETCHECK, temp_dynarec, 0);
		EnableWindow(h, FALSE);
	}
	else
	{
		EnableWindow(h, TRUE);
	}
#endif

	h = GetDlgItem(hdlg, IDC_CHECK_FPU);
	cpu_type = machines[romstomachine[temp_romset]].cpu[temp_cpu_m].cpus[temp_cpu].cpu_type;
	if ((cpu_type < CPU_i486DX) && (cpu_type >= CPU_286))
	{
		EnableWindow(h, TRUE);
	}
	else if (cpu_type < CPU_286)
	{
                temp_fpu = 0;
		EnableWindow(h, FALSE);
	}
	else
	{
                temp_fpu = 1;
		EnableWindow(h, FALSE);
	}
	SendMessage(h, BM_SETCHECK, temp_fpu, 0);
}


static void win_settings_machine_recalc_cpu_m(HWND hdlg)
{
	HWND h;
	int c = 0;
	int temp_romset = 0;
	LPTSTR lptsTemp;
	char *stransi;

	temp_romset = machine_getromset_ex(temp_machine);
	lptsTemp = (LPTSTR) malloc(512);

	h = GetDlgItem(hdlg, IDC_COMBO_CPU);
	SendMessage(h, CB_RESETCONTENT, 0, 0);
	c = 0;
	while (machines[romstomachine[temp_romset]].cpu[temp_cpu_m].cpus[c].cpu_type != -1)
	{
		stransi = machines[romstomachine[temp_romset]].cpu[temp_cpu_m].cpus[c].name;
		mbstowcs(lptsTemp, stransi, strlen(stransi) + 1);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)lptsTemp);
		c++;
	}
	EnableWindow(h, TRUE);
	if (temp_cpu >= c)
	{
		temp_cpu = (c - 1);
	}
	SendMessage(h, CB_SETCURSEL, temp_cpu, 0);

	win_settings_machine_recalc_cpu(hdlg);

	free(lptsTemp);
}


static void win_settings_machine_recalc_machine(HWND hdlg)
{
	HWND h;
	int c = 0;
	int temp_romset = 0;
	LPTSTR lptsTemp;
	const char *stransi;
        UDACCEL accel;

	temp_romset = machine_getromset_ex(temp_machine);
	lptsTemp = (LPTSTR) malloc(512);

	h = GetDlgItem(hdlg, IDC_CONFIGURE_MACHINE);
	if (machine_getdevice(temp_machine))
	{
		EnableWindow(h, TRUE);
	}
	else
	{
		EnableWindow(h, FALSE);
	}

	h = GetDlgItem(hdlg, IDC_COMBO_CPU_TYPE);
	SendMessage(h, CB_RESETCONTENT, 0, 0);
	c = 0;
	while (machines[romstomachine[temp_romset]].cpu[c].cpus != NULL && c < 4)
	{
		stransi = machines[romstomachine[temp_romset]].cpu[c].name;
		mbstowcs(lptsTemp, stransi, strlen(stransi) + 1);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)lptsTemp);
		c++;
	}
	EnableWindow(h, TRUE);
	if (temp_cpu_m >= c)
	{
		temp_cpu_m = (c - 1);
	}
	SendMessage(h, CB_SETCURSEL, temp_cpu_m, 0);
	if (c == 1)
	{
		EnableWindow(h, FALSE);
	}
	else
	{
		EnableWindow(h, TRUE);
	}

	win_settings_machine_recalc_cpu_m(hdlg);

	h = GetDlgItem(hdlg, IDC_MEMSPIN);
	SendMessage(h, UDM_SETRANGE, 0, (machines[romstomachine[temp_romset]].min_ram << 16) | machines[romstomachine[temp_romset]].max_ram);
	accel.nSec = 0;
	accel.nInc = machines[romstomachine[temp_romset]].ram_granularity;
	SendMessage(h, UDM_SETACCEL, 1, (LPARAM)&accel);
	if (!(machines[romstomachine[temp_romset]].flags & MACHINE_AT) || (machines[romstomachine[temp_romset]].ram_granularity >= 128))
	{
		SendMessage(h, UDM_SETPOS, 0, temp_mem_size);
		h = GetDlgItem(hdlg, IDC_TEXT_MB);
		SendMessage(h, WM_SETTEXT, 0, (LPARAM) plat_get_string(IDS_2094));
	}
	else
	{
		SendMessage(h, UDM_SETPOS, 0, temp_mem_size / 1024);
		h = GetDlgItem(hdlg, IDC_TEXT_MB);
		SendMessage(h, WM_SETTEXT, 0, (LPARAM) plat_get_string(IDS_2087));
	}

	free(lptsTemp);
}


static BOOL CALLBACK win_settings_machine_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND h;
	int c = 0;
	int d = 0;
	LPTSTR lptsTemp;
	char *stransi;

        switch (message)
        {
		case WM_INITDIALOG:
			lptsTemp = (LPTSTR) malloc(512);

			h = GetDlgItem(hdlg, IDC_COMBO_MACHINE);
			for (c = 0; c < ROM_MAX; c++)
			{
				romstolist[c] = 0;
			}
			c = d = 0;
			while (machines[c].id != -1)
			{
				if (romspresent[machines[c].id])
				{
					stransi = (char *)machines[c].name;
					mbstowcs(lptsTemp, stransi, strlen(stransi) + 1);
					SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
					machinetolist[c] = d;
					listtomachine[d] = c;
					romstolist[machines[c].id] = d;
					romstomachine[machines[c].id] = c;
					d++;
				}
				c++;
			}
			SendMessage(h, CB_SETCURSEL, machinetolist[temp_machine], 0);

			h = GetDlgItem(hdlg, IDC_COMBO_WS);
	                SendMessage(h, CB_ADDSTRING, 0, (LPARAM) plat_get_string(IDS_2131));

			for (c = 0; c < 8; c++)
			{
				wsprintf(lptsTemp, plat_get_string(2132), c);
	        	        SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
			}

			SendMessage(h, CB_SETCURSEL, temp_wait_states, 0);

#ifdef USE_DYNAREC
        	        h=GetDlgItem(hdlg, IDC_CHECK_DYNAREC);
	                SendMessage(h, BM_SETCHECK, temp_dynarec, 0);
#endif

			h = GetDlgItem(hdlg, IDC_MEMSPIN);
			SendMessage(h, UDM_SETBUDDY, (WPARAM)GetDlgItem(hdlg, IDC_MEMTEXT), 0);

        	        h=GetDlgItem(hdlg, IDC_CHECK_SYNC);
	                SendMessage(h, BM_SETCHECK, temp_sync, 0);

			win_settings_machine_recalc_machine(hdlg);

			free(lptsTemp);

			return TRUE;

		case WM_COMMAND:
                	switch (LOWORD(wParam))
	                {
                	        case IDC_COMBO_MACHINE:
	        	                if (HIWORD(wParam) == CBN_SELCHANGE)
		                        {
        		                        h = GetDlgItem(hdlg, IDC_COMBO_MACHINE);
	                	                temp_machine = listtomachine[SendMessage(h,CB_GETCURSEL,0,0)];

						win_settings_machine_recalc_machine(hdlg);
					}
					break;
				case IDC_COMBO_CPU_TYPE:
	        	                if (HIWORD(wParam) == CBN_SELCHANGE)
		                        {
        		                        h = GetDlgItem(hdlg, IDC_COMBO_CPU_TYPE);
	                	                temp_cpu_m = SendMessage(h, CB_GETCURSEL, 0, 0);

						temp_cpu = 0;
						win_settings_machine_recalc_cpu_m(hdlg);
					}
					break;
				case IDC_COMBO_CPU:
	        	                if (HIWORD(wParam) == CBN_SELCHANGE)
		                        {
        		                        h = GetDlgItem(hdlg, IDC_COMBO_CPU);
	                	                temp_cpu = SendMessage(h, CB_GETCURSEL, 0, 0);

						win_settings_machine_recalc_cpu(hdlg);
					}
					break;
				case IDC_CONFIGURE_MACHINE:
					h = GetDlgItem(hdlg, IDC_COMBO_MACHINE);
					temp_machine = listtomachine[SendMessage(h, CB_GETCURSEL, 0, 0)];
                        
                        		temp_deviceconfig |= deviceconfig_open(hdlg, (void *)machine_getdevice(temp_machine));
                        		break;
			}

			return FALSE;

		case WM_SAVESETTINGS:
			lptsTemp = (LPTSTR) malloc(512);
			stransi = (char *)malloc(512);

#ifdef USE_DYNAREC
        	        h=GetDlgItem(hdlg, IDC_CHECK_DYNAREC);
			temp_dynarec = SendMessage(h, BM_GETCHECK, 0, 0);
#endif

        	        h=GetDlgItem(hdlg, IDC_CHECK_SYNC);
			temp_sync = SendMessage(h, BM_GETCHECK, 0, 0);

        	        h=GetDlgItem(hdlg, IDC_CHECK_FPU);
			temp_fpu = SendMessage(h, BM_GETCHECK, 0, 0);

                        h = GetDlgItem(hdlg, IDC_COMBO_WS);
                        temp_wait_states = SendMessage(h, CB_GETCURSEL, 0, 0);

			h = GetDlgItem(hdlg, IDC_MEMTEXT);
			SendMessage(h, WM_GETTEXT, 255, (LPARAM) lptsTemp);
			wcstombs(stransi, lptsTemp, 512);
			sscanf(stransi, "%i", &temp_mem_size);
			temp_mem_size &= ~(machines[temp_machine].ram_granularity - 1);
			if (temp_mem_size < machines[temp_machine].min_ram)
			{
				temp_mem_size = machines[temp_machine].min_ram;
			}
			else if (temp_mem_size > machines[temp_machine].max_ram)
			{
				temp_mem_size = machines[temp_machine].max_ram;
			}
			if ((machines[temp_machine].flags & MACHINE_AT) && (machines[temp_machine].ram_granularity < 128))
			{
				temp_mem_size *= 1024;
			}
			if (machines[temp_machine].flags & MACHINE_VIDEO)
			{
				gfxcard = GFX_INTERNAL;
			}
			free(stransi);
			free(lptsTemp);

		default:
			return FALSE;
	}

	return FALSE;
}


static void recalc_vid_list(HWND hdlg)
{
        HWND h = GetDlgItem(hdlg, IDC_COMBO_VIDEO);
        int c = 0, d = 0;
        int found_card = 0;
	WCHAR szText[512];
        
        SendMessage(h, CB_RESETCONTENT, 0, 0);
        SendMessage(h, CB_SETCURSEL, 0, 0);
        
        while (1)
        {
		/* Skip "internal" if machine doesn't have it. */
		if (c==1 && !(machines[temp_machine].flags&MACHINE_VIDEO)) {
			c++;
			continue;
		}

                char *s = video_card_getname(c);

                if (!s[0])
                        break;

                if (video_card_available(c) && gfx_present[video_new_to_old(c)] &&
                    device_is_valid(video_card_getdevice(c), machines[temp_machine].flags))
                {
			mbstowcs(szText, s, strlen(s) + 1);
                        SendMessage(h, CB_ADDSTRING, 0, (LPARAM) szText);
                        if (video_new_to_old(c) == temp_gfxcard)
                        {

                                SendMessage(h, CB_SETCURSEL, d, 0);
                                found_card = 1;
                        }

                        d++;
                }

                c++;
        }
        if (!found_card)
                SendMessage(h, CB_SETCURSEL, 0, 0);
        EnableWindow(h, machines[temp_machine].fixed_gfxcard ? FALSE : TRUE);

        h = GetDlgItem(hdlg, IDC_CHECK_VOODOO);
        EnableWindow(h, (machines[temp_machine].flags & MACHINE_PCI) ? TRUE : FALSE);

        h = GetDlgItem(hdlg, IDC_BUTTON_VOODOO);
        EnableWindow(h, ((machines[temp_machine].flags & MACHINE_PCI) && temp_voodoo) ? TRUE : FALSE);
}


static BOOL CALLBACK win_settings_video_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND h;
	LPTSTR lptsTemp;
	char *stransi;
	int gfx = 0;

        switch (message)
        {
		case WM_INITDIALOG:
			lptsTemp = (LPTSTR) malloc(512);
			stransi = (char *) malloc(512);

			recalc_vid_list(hdlg);

			h = GetDlgItem(hdlg, IDC_COMBO_VIDEO_SPEED);
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM)plat_get_string(IDS_2133));
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM)plat_get_string(IDS_2134));
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM)plat_get_string(IDS_2135));
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM)plat_get_string(IDS_2136));
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM)plat_get_string(IDS_2137));
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM)plat_get_string(IDS_2138));
			SendMessage(h, CB_SETCURSEL, temp_video_speed, 0);

	                h=GetDlgItem(hdlg, IDC_CHECK_VOODOO);
        	        SendMessage(h, BM_SETCHECK, temp_voodoo, 0);

                        h = GetDlgItem(hdlg, IDC_COMBO_VIDEO);
                        SendMessage(h, CB_GETLBTEXT, SendMessage(h, CB_GETCURSEL, 0, 0), (LPARAM) lptsTemp);
			wcstombs(stransi, lptsTemp, 512);
			gfx = video_card_getid(stransi);

			h = GetDlgItem(hdlg, IDC_CONFIGURE_VID);
			if (video_card_has_config(gfx))
			{
				EnableWindow(h, TRUE);
			}
			else
			{
				EnableWindow(h, FALSE);
			}

			free(stransi);
			free(lptsTemp);

			return TRUE;

		case WM_COMMAND:
                	switch (LOWORD(wParam))
	                {
				case IDC_COMBO_VIDEO:
					lptsTemp = (LPTSTR) malloc(512);
					stransi = (char *) malloc(512);

		                        h = GetDlgItem(hdlg, IDC_COMBO_VIDEO);
		                        SendMessage(h, CB_GETLBTEXT, SendMessage(h, CB_GETCURSEL, 0, 0), (LPARAM) lptsTemp);
					wcstombs(stransi, lptsTemp, 512);
					gfx = video_card_getid(stransi);
		                        temp_gfxcard = video_new_to_old(gfx);

					h = GetDlgItem(hdlg, IDC_CONFIGURE_VID);
					if (video_card_has_config(gfx))
					{
						EnableWindow(h, TRUE);
					}
					else
					{
						EnableWindow(h, FALSE);
					}

					free(stransi);
					free(lptsTemp);
					break;

				case IDC_CHECK_VOODOO:
	        		        h = GetDlgItem(hdlg, IDC_CHECK_VOODOO);
					temp_voodoo = SendMessage(h, BM_GETCHECK, 0, 0);

	        		        h = GetDlgItem(hdlg, IDC_BUTTON_VOODOO);
					EnableWindow(h, temp_voodoo ? TRUE : FALSE);
					break;

				case IDC_BUTTON_VOODOO:
					temp_deviceconfig |= deviceconfig_open(hdlg, (void *)&voodoo_device);
					break;

				case IDC_CONFIGURE_VID:
					lptsTemp = (LPTSTR) malloc(512);
					stransi = (char *) malloc(512);

					h = GetDlgItem(hdlg, IDC_COMBO_VIDEO);
		                        SendMessage(h, CB_GETLBTEXT, SendMessage(h, CB_GETCURSEL, 0, 0), (LPARAM) lptsTemp);
					wcstombs(stransi, lptsTemp, 512);
					temp_deviceconfig |= deviceconfig_open(hdlg, (void *)video_card_getdevice(video_card_getid(stransi)));

					free(stransi);
					free(lptsTemp);
					break;
			}
			return FALSE;

		case WM_SAVESETTINGS:
			lptsTemp = (LPTSTR) malloc(512);
			stransi = (char *) malloc(512);

                        h = GetDlgItem(hdlg, IDC_COMBO_VIDEO);
                        SendMessage(h, CB_GETLBTEXT, SendMessage(h, CB_GETCURSEL, 0, 0), (LPARAM) lptsTemp);
			wcstombs(stransi, lptsTemp, 512);
                        temp_gfxcard = video_new_to_old(video_card_getid(stransi));

                        h = GetDlgItem(hdlg, IDC_COMBO_VIDEO_SPEED);
			temp_video_speed = SendMessage(h, CB_GETCURSEL, 0, 0);

	                h = GetDlgItem(hdlg, IDC_CHECK_VOODOO);
			temp_voodoo = SendMessage(h, BM_GETCHECK, 0, 0);

			free(stransi);
			free(lptsTemp);

		default:
			return FALSE;
	}
	return FALSE;
}


static int mouse_valid(int num, int m)
{
	device_t *dev;

	if ((num == MOUSE_TYPE_INTERNAL) &&
	    !(machines[m].flags & MACHINE_MOUSE)) return(0);

	dev = mouse_get_device(num);
	return(device_is_valid(dev, machines[m].flags));
}


static BOOL CALLBACK win_settings_input_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	wchar_t str[128];
	HWND h;
	int c = 0;
	int d = 0;

        switch (message)
        {
		case WM_INITDIALOG:
			h = GetDlgItem(hdlg, IDC_COMBO_MOUSE);
			c = d = 0;
			for (c = 0; c < mouse_get_ndev(); c++)
			{
				settings_mouse_to_list[c] = d;

				if (mouse_valid(c, temp_machine))
				{
					mbstowcs(str, mouse_get_name(c), sizeof_w(str));
					SendMessage(h, CB_ADDSTRING, 0, (LPARAM)str);

					settings_list_to_mouse[d] = c;
					d++;
				}
			}

			SendMessage(h, CB_SETCURSEL, settings_mouse_to_list[temp_mouse], 0);
			h = GetDlgItem(hdlg, IDC_CONFIGURE_MOUSE);
			EnableWindow(h, TRUE);

			h = GetDlgItem(hdlg, IDC_COMBO_JOYSTICK);
			c = 0;
			while (joystick_get_name(c))
			{
				SendMessage(h, CB_ADDSTRING, 0, (LPARAM)plat_get_string(2144 + c));
				c++;
			}
			EnableWindow(h, TRUE);
			SendMessage(h, CB_SETCURSEL, temp_joystick, 0);

			h = GetDlgItem(hdlg, IDC_JOY1);
			EnableWindow(h, (joystick_get_max_joysticks(temp_joystick) >= 1) ? TRUE : FALSE);
			h = GetDlgItem(hdlg, IDC_JOY2);
			EnableWindow(h, (joystick_get_max_joysticks(temp_joystick) >= 2) ? TRUE : FALSE);
			h = GetDlgItem(hdlg, IDC_JOY3);
			EnableWindow(h, (joystick_get_max_joysticks(temp_joystick) >= 3) ? TRUE : FALSE);
			h = GetDlgItem(hdlg, IDC_JOY4);
			EnableWindow(h, (joystick_get_max_joysticks(temp_joystick) >= 4) ? TRUE : FALSE);

			return TRUE;

		case WM_COMMAND:
                	switch (LOWORD(wParam))
	                {
				case IDC_CONFIGURE_MOUSE:
					h = GetDlgItem(hdlg, IDC_COMBO_MOUSE);
					temp_mouse = settings_list_to_mouse[SendMessage(h, CB_GETCURSEL, 0, 0)];
					temp_deviceconfig |= deviceconfig_open(hdlg, (void *)mouse_get_device(temp_mouse));
					break;

				case IDC_COMBO_JOYSTICK:
					if (HIWORD(wParam) == CBN_SELCHANGE)
					{
						h = GetDlgItem(hdlg, IDC_COMBO_JOYSTICK);
						temp_joystick = SendMessage(h, CB_GETCURSEL, 0, 0);

						h = GetDlgItem(hdlg, IDC_JOY1);
						EnableWindow(h, (joystick_get_max_joysticks(temp_joystick) >= 1) ? TRUE : FALSE);
						h = GetDlgItem(hdlg, IDC_JOY2);
						EnableWindow(h, (joystick_get_max_joysticks(temp_joystick) >= 2) ? TRUE : FALSE);
						h = GetDlgItem(hdlg, IDC_JOY3);
						EnableWindow(h, (joystick_get_max_joysticks(temp_joystick) >= 3) ? TRUE : FALSE);
						h = GetDlgItem(hdlg, IDC_JOY4);
						EnableWindow(h, (joystick_get_max_joysticks(temp_joystick) >= 4) ? TRUE : FALSE);
					}
					break;

				case IDC_JOY1:
					h = GetDlgItem(hdlg, IDC_COMBO_JOY);
					temp_joystick = SendMessage(h, CB_GETCURSEL, 0, 0);
					joystickconfig_open(hdlg, 0, temp_joystick);
					break;

				case IDC_JOY2:
					h = GetDlgItem(hdlg, IDC_COMBO_JOY);
					temp_joystick = SendMessage(h, CB_GETCURSEL, 0, 0);
					joystickconfig_open(hdlg, 1, temp_joystick);
					break;

				case IDC_JOY3:
					h = GetDlgItem(hdlg, IDC_COMBO_JOY);
					temp_joystick = SendMessage(h, CB_GETCURSEL, 0, 0);
					joystickconfig_open(hdlg, 2, temp_joystick);
					break;

				case IDC_JOY4:
					h = GetDlgItem(hdlg, IDC_COMBO_JOY);
					temp_joystick = SendMessage(h, CB_GETCURSEL, 0, 0);
					joystickconfig_open(hdlg, 3, temp_joystick);
					break;
			}
			return FALSE;

		case WM_SAVESETTINGS:
			h = GetDlgItem(hdlg, IDC_COMBO_MOUSE);
			temp_mouse = settings_list_to_mouse[SendMessage(h, CB_GETCURSEL, 0, 0)];

			h = GetDlgItem(hdlg, IDC_COMBO_JOYSTICK);
			temp_joystick = SendMessage(h, CB_GETCURSEL, 0, 0);

		default:
			return FALSE;
	}
	return FALSE;
}


int mpu401_present(void)
{
	char *n;

	n = sound_card_get_internal_name(temp_sound_card);
	if (n != NULL)
	{
		if (!strcmp(n, "sb16") || !strcmp(n, "sbawe32"))
		{
			return 1;
		}
	}

	return temp_mpu401 ? 1 : 0;
}

int mpu401_standalone_allow(void)
{
	char *n, *md;

	n = sound_card_get_internal_name(temp_sound_card);
	md = midi_device_get_internal_name(temp_midi_device);
	if (n != NULL)
	{
		if (!strcmp(n, "sb16") || !strcmp(n, "sbawe32"))
		{
			return 0;
		}
	}

	if (md != NULL)
	{
		if (!strcmp(md, "none"))
		{
			return 0;
		}
	}

	return 1;
}

static BOOL CALLBACK win_settings_sound_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND h;
	int c = 0;
	int d = 0;
	LPTSTR lptsTemp;
	device_t *sound_dev/*, *midi_dev*/;
	char *s;

        switch (message)
        {
		case WM_INITDIALOG:
			lptsTemp = (LPTSTR) malloc(512);

			h = GetDlgItem(hdlg, IDC_COMBO_SOUND);
			c = d = 0;
			while (1)
			{
				s = sound_card_getname(c);

				if (!s[0])
				{
					break;
				}

				settings_sound_to_list[c] = d;

				if (sound_card_available(c))
				{
					sound_dev = sound_card_getdevice(c);

					if (device_is_valid(sound_dev, machines[temp_machine].flags))
					{
						if (c == 0)
						{
							SendMessage(h, CB_ADDSTRING, 0, (LPARAM) plat_get_string(IDS_2152));
						}
						else
						{
							mbstowcs(lptsTemp, s, strlen(s) + 1);
							SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
						}
						settings_list_to_sound[d] = c;
						d++;
					}
				}

				c++;
			}
			SendMessage(h, CB_SETCURSEL, settings_sound_to_list[temp_sound_card], 0);

			EnableWindow(h, d ? TRUE : FALSE);

			h = GetDlgItem(hdlg, IDC_CONFIGURE_SND);
			if (sound_card_has_config(temp_sound_card))
			{
				EnableWindow(h, TRUE);
			}
			else
			{
				EnableWindow(h, FALSE);
			}

			h = GetDlgItem(hdlg, IDC_COMBO_MIDI);
			c = d = 0;
			while (1)
			{
				s = midi_device_getname(c);

				if (!s[0])
				{
					break;
				}

				settings_midi_to_list[c] = d;

				if (midi_device_available(c))
				{
					/* midi_dev = midi_device_getdevice(c); */

					if (c == 0)
					{
						SendMessage(h, CB_ADDSTRING, 0, (LPARAM)plat_get_string(IDS_2152));
					}
					else
					{
						mbstowcs(lptsTemp, s, strlen(s) + 1);
						SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
					}
					settings_list_to_midi[d] = c;
					d++;
				}

				c++;
			}
			SendMessage(h, CB_SETCURSEL, settings_midi_to_list[temp_midi_device], 0);

			h = GetDlgItem(hdlg, IDC_CONFIGURE_MIDI);
			if (midi_device_has_config(temp_midi_device))
			{
				EnableWindow(h, TRUE);
			}
			else
			{
				EnableWindow(h, FALSE);
			}

		        h = GetDlgItem(hdlg, IDC_CHECK_MPU401);
        	        SendMessage(h, BM_SETCHECK, temp_mpu401, 0);
		        EnableWindow(h, mpu401_standalone_allow() ? TRUE : FALSE);

		        h = GetDlgItem(hdlg, IDC_CONFIGURE_MPU401);
		        EnableWindow(h, (mpu401_standalone_allow() && temp_mpu401) ? TRUE : FALSE);

			h=GetDlgItem(hdlg, IDC_CHECK_CMS);
			SendMessage(h, BM_SETCHECK, temp_GAMEBLASTER, 0);

			h=GetDlgItem(hdlg, IDC_CHECK_GUS);
			SendMessage(h, BM_SETCHECK, temp_GUS, 0);

			h=GetDlgItem(hdlg, IDC_CHECK_SSI);
			SendMessage(h, BM_SETCHECK, temp_SSI2001, 0);

			h=GetDlgItem(hdlg, IDC_CHECK_NUKEDOPL);
			SendMessage(h, BM_SETCHECK, temp_opl3_type, 0);

			h=GetDlgItem(hdlg, IDC_CHECK_FLOAT);
			SendMessage(h, BM_SETCHECK, temp_float, 0);

			free(lptsTemp);

			return TRUE;

		case WM_COMMAND:
                	switch (LOWORD(wParam))
	                {
				case IDC_COMBO_SOUND:
					h = GetDlgItem(hdlg, IDC_COMBO_SOUND);
					temp_sound_card = settings_list_to_sound[SendMessage(h, CB_GETCURSEL, 0, 0)];

					h = GetDlgItem(hdlg, IDC_CONFIGURE_SND);
					if (sound_card_has_config(temp_sound_card))
					{
						EnableWindow(h, TRUE);
					}
					else
					{
						EnableWindow(h, FALSE);
					}

				        h = GetDlgItem(hdlg, IDC_CHECK_MPU401);
        			        SendMessage(h, BM_SETCHECK, temp_mpu401, 0);
				        EnableWindow(h, mpu401_standalone_allow() ? TRUE : FALSE);

				        h = GetDlgItem(hdlg, IDC_CONFIGURE_MPU401);
				        EnableWindow(h, (mpu401_standalone_allow() && temp_mpu401) ? TRUE : FALSE);
					break;

				case IDC_CONFIGURE_SND:
					h = GetDlgItem(hdlg, IDC_COMBO_SOUND);
					temp_sound_card = settings_list_to_sound[SendMessage(h, CB_GETCURSEL, 0, 0)];

					temp_deviceconfig |= deviceconfig_open(hdlg, (void *)sound_card_getdevice(temp_sound_card));
					break;

				case IDC_COMBO_MIDI:
					h = GetDlgItem(hdlg, IDC_COMBO_MIDI);
					temp_midi_device = settings_list_to_midi[SendMessage(h, CB_GETCURSEL, 0, 0)];

					h = GetDlgItem(hdlg, IDC_CONFIGURE_MIDI);
					if (midi_device_has_config(temp_midi_device))
					{
						EnableWindow(h, TRUE);
					}
					else
					{
						EnableWindow(h, FALSE);
					}

				        h = GetDlgItem(hdlg, IDC_CHECK_MPU401);
        			        SendMessage(h, BM_SETCHECK, temp_mpu401, 0);
				        EnableWindow(h, mpu401_standalone_allow() ? TRUE : FALSE);

				        h = GetDlgItem(hdlg, IDC_CONFIGURE_MPU401);
				        EnableWindow(h, (mpu401_standalone_allow() && temp_mpu401) ? TRUE : FALSE);
					break;

				case IDC_CONFIGURE_MIDI:
					h = GetDlgItem(hdlg, IDC_COMBO_MIDI);
					temp_midi_device = settings_list_to_midi[SendMessage(h, CB_GETCURSEL, 0, 0)];

					temp_deviceconfig |= deviceconfig_open(hdlg, (void *)midi_device_getdevice(temp_midi_device));
					break;

				case IDC_CHECK_MPU401:
	        		        h = GetDlgItem(hdlg, IDC_CHECK_MPU401);
					temp_mpu401 = SendMessage(h, BM_GETCHECK, 0, 0);

	        		        h = GetDlgItem(hdlg, IDC_CONFIGURE_MPU401);
					EnableWindow(h, mpu401_present() ? TRUE : FALSE);
					break;

				case IDC_CONFIGURE_MPU401:
					temp_deviceconfig |= deviceconfig_open(hdlg, (void *)&mpu401_device);
					break;
			}
			return FALSE;

		case WM_SAVESETTINGS:
			h = GetDlgItem(hdlg, IDC_COMBO_SOUND);
			temp_sound_card = settings_list_to_sound[SendMessage(h, CB_GETCURSEL, 0, 0)];

			h = GetDlgItem(hdlg, IDC_COMBO_MIDI);
			temp_midi_device = settings_list_to_midi[SendMessage(h, CB_GETCURSEL, 0, 0)];

			h = GetDlgItem(hdlg, IDC_CHECK_MPU401);
			temp_mpu401 = SendMessage(h, BM_GETCHECK, 0, 0);

			h = GetDlgItem(hdlg, IDC_CHECK_CMS);
			temp_GAMEBLASTER = SendMessage(h, BM_GETCHECK, 0, 0);

			h = GetDlgItem(hdlg, IDC_CHECK_GUS);
			temp_GUS = SendMessage(h, BM_GETCHECK, 0, 0);

			h = GetDlgItem(hdlg, IDC_CHECK_SSI);
			temp_SSI2001 = SendMessage(h, BM_GETCHECK, 0, 0);

			h = GetDlgItem(hdlg, IDC_CHECK_NUKEDOPL);
			temp_opl3_type = SendMessage(h, BM_GETCHECK, 0, 0);

			h = GetDlgItem(hdlg, IDC_CHECK_FLOAT);
			temp_float = SendMessage(h, BM_GETCHECK, 0, 0);

		default:
			return FALSE;
	}
	return FALSE;
}


static BOOL CALLBACK win_settings_ports_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND h;
	int c = 0;
	int d = 0;
	char *s;
	LPTSTR lptsTemp;

        switch (message)
        {
		case WM_INITDIALOG:
			lptsTemp = (LPTSTR) malloc(512);

			h = GetDlgItem(hdlg, IDC_COMBO_LPT1);
			c = d = 0;
			while (1)
			{
				s = lpt_device_get_name(c);

				if (!s)
				{
					break;
				}

				if (c == 0)
				{
					SendMessage(h, CB_ADDSTRING, 0, (LPARAM) plat_get_string(IDS_2152));
				}
				else
				{
					mbstowcs(lptsTemp, s, strlen(s) + 1);
					SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
				}

				if (!strcmp(temp_lpt1_device_name, lpt_device_get_internal_name(c)))
				{
					d = c;
				}

				c++;
			}
			SendMessage(h, CB_SETCURSEL, d, 0);

			h=GetDlgItem(hdlg, IDC_CHECK_SERIAL1);
			SendMessage(h, BM_SETCHECK, temp_serial[0], 0);

			h=GetDlgItem(hdlg, IDC_CHECK_SERIAL2);
			SendMessage(h, BM_SETCHECK, temp_serial[1], 0);

			h=GetDlgItem(hdlg, IDC_CHECK_PARALLEL);
			SendMessage(h, BM_SETCHECK, temp_lpt, 0);

			free(lptsTemp);

			return TRUE;

		case WM_SAVESETTINGS:
			h = GetDlgItem(hdlg, IDC_COMBO_LPT1);
			c = SendMessage(h, CB_GETCURSEL, 0, 0);
			strcpy(temp_lpt1_device_name, lpt_device_get_internal_name(c));

			h = GetDlgItem(hdlg, IDC_CHECK_SERIAL1);
			temp_serial[0] = SendMessage(h, BM_GETCHECK, 0, 0);

			h = GetDlgItem(hdlg, IDC_CHECK_SERIAL2);
			temp_serial[1] = SendMessage(h, BM_GETCHECK, 0, 0);

			h = GetDlgItem(hdlg, IDC_CHECK_PARALLEL);
			temp_lpt = SendMessage(h, BM_GETCHECK, 0, 0);

		default:
			return FALSE;
	}
	return FALSE;
}


static void recalc_hdc_list(HWND hdlg, int machine, int use_selected_hdc)
{
	HWND h;

	char *s;
	int valid = 0;
	char old_name[16];
	int c, d;

	LPTSTR lptsTemp;

	lptsTemp = (LPTSTR) malloc(512);

	h = GetDlgItem(hdlg, IDC_COMBO_HDC);

	valid = 0;

	if (use_selected_hdc)
	{
		c = SendMessage(h, CB_GETCURSEL, 0, 0);

		if (c != -1 && hdc_names[c])
		{
			strncpy(old_name, hdc_names[c], sizeof(old_name) - 1);
		}
		else
		{
			strcpy(old_name, "none");
		}
	}
	else
	{
		strncpy(old_name, temp_hdc_name, sizeof(old_name) - 1);
	}

	SendMessage(h, CB_RESETCONTENT, 0, 0);
	c = d = 0;
	while (1)
	{
		s = hdc_get_name(c);
		if (s[0] == 0)
		{
			break;
		}
		if (c==1 && !(machines[temp_machine].flags&MACHINE_HDC))
		{
			/* Skip "Internal" if machine doesn't have one. */
			c++;
			continue;
		}
		if (!hdc_available(c) || !device_is_valid(hdc_get_device(c), machines[temp_machine].flags))
		{
			c++;
			continue;
		}
		mbstowcs(lptsTemp, s, strlen(s) + 1);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);

		hdc_names[d] = hdc_get_internal_name(c);
		if (!strcmp(old_name, hdc_names[d]))
		{
			SendMessage(h, CB_SETCURSEL, d, 0);
			valid = 1;
		}
		c++;
		d++;
	}

	if (!valid)
	{
		SendMessage(h, CB_SETCURSEL, 0, 0);
	}

	EnableWindow(h, d ? TRUE : FALSE);

	free(lptsTemp);
}


int valid_ide_irqs[11] = { 2, 3, 4, 5, 7, 9, 10, 11, 12, 14, 15 };


int find_irq_in_array(int irq, int def)
{
	int i = 0;

	for (i = 0; i < 11; i++)
	{
		if (valid_ide_irqs[i] == irq)
		{
			return i + 1;
		}
	}

	return 7 + def;
}


static BOOL CALLBACK win_settings_peripherals_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND h;
	int c = 0;
	int d = 0;
	LPTSTR lptsTemp;
	device_t *scsi_dev;

        switch (message)
        {
		case WM_INITDIALOG:
			lptsTemp = (LPTSTR) malloc(512);

			/*SCSI config*/
			h = GetDlgItem(hdlg, IDC_COMBO_SCSI);
			c = d = 0;
			while (1)
			{
				char *s = scsi_card_getname(c);

				if (!s[0])
				{
					break;
				}

				settings_scsi_to_list[c] = d;			
				
				if (scsi_card_available(c))
				{
					scsi_dev = scsi_card_getdevice(c);


					if (device_is_valid(scsi_dev, machines[temp_machine].flags))
					{
						if (c == 0)
						{
							SendMessage(h, CB_ADDSTRING, 0, (LPARAM) plat_get_string(IDS_2152));
						}
						else
						{
							mbstowcs(lptsTemp, s, strlen(s) + 1);
							SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
						}
						settings_list_to_scsi[d] = c;
						d++;
					}
				}

				c++;
			}
			SendMessage(h, CB_SETCURSEL, settings_scsi_to_list[temp_scsi_card], 0);

			EnableWindow(h, d ? TRUE : FALSE);

			h = GetDlgItem(hdlg, IDC_CONFIGURE_SCSI);
			if (scsi_card_has_config(temp_scsi_card))
			{
				EnableWindow(h, TRUE);
			}
			else
			{
				EnableWindow(h, FALSE);
			}

			recalc_hdc_list(hdlg, temp_machine, 0);

			h=GetDlgItem(hdlg, IDC_COMBO_IDE_TER);
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM)plat_get_string(IDS_5376));

			for (c = 0; c < 11; c++)
			{
				wsprintf(lptsTemp, plat_get_string(IDS_2155), valid_ide_irqs[c]);
	        	        SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
			}

			if (temp_ide_ter)
			{
				SendMessage(h, CB_SETCURSEL, find_irq_in_array(temp_ide_ter_irq, 0), 0);
			}
			else
			{
				SendMessage(h, CB_SETCURSEL, 0, 0);
			}

			h=GetDlgItem(hdlg, IDC_COMBO_IDE_QUA);
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM)plat_get_string(IDS_5376));

			for (c = 0; c < 11; c++)
			{
				wsprintf(lptsTemp, plat_get_string(IDS_2155), valid_ide_irqs[c]);
	        	        SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
			}

			if (temp_ide_qua)
			{
				SendMessage(h, CB_SETCURSEL, find_irq_in_array(temp_ide_qua_irq, 1), 0);
			}
			else
			{
				SendMessage(h, CB_SETCURSEL, 0, 0);
			}

			h=GetDlgItem(hdlg, IDC_CHECK_BUGGER);
			SendMessage(h, BM_SETCHECK, temp_bugger, 0);

			free(lptsTemp);

			return TRUE;

		case WM_COMMAND:
                	switch (LOWORD(wParam))
	                {
				case IDC_CONFIGURE_SCSI:
					h = GetDlgItem(hdlg, IDC_COMBO_SCSI);
					temp_scsi_card = settings_list_to_scsi[SendMessage(h, CB_GETCURSEL, 0, 0)];

					temp_deviceconfig |= deviceconfig_open(hdlg, (void *)scsi_card_getdevice(temp_scsi_card));
					break;

				case IDC_COMBO_SCSI:
					h = GetDlgItem(hdlg, IDC_COMBO_SCSI);
					temp_scsi_card = settings_list_to_scsi[SendMessage(h, CB_GETCURSEL, 0, 0)];

					h = GetDlgItem(hdlg, IDC_CONFIGURE_SCSI);
					if (scsi_card_has_config(temp_scsi_card))
					{
						EnableWindow(h, TRUE);
					}
					else
					{
						EnableWindow(h, FALSE);
					}
					break;
			}
			return FALSE;

		case WM_SAVESETTINGS:
			h = GetDlgItem(hdlg, IDC_COMBO_HDC);
			c = SendMessage(h, CB_GETCURSEL, 0, 0);
			if (hdc_names[c])
			{
				strncpy(temp_hdc_name, hdc_names[c], sizeof(temp_hdc_name) - 1);
			}
			else
			{
				strcpy(temp_hdc_name, "none");
			}

			h = GetDlgItem(hdlg, IDC_COMBO_SCSI);
			temp_scsi_card = settings_list_to_scsi[SendMessage(h, CB_GETCURSEL, 0, 0)];

			h = GetDlgItem(hdlg, IDC_COMBO_IDE_TER);
			temp_ide_ter = SendMessage(h, CB_GETCURSEL, 0, 0);
			if (temp_ide_ter > 1)
			{
				temp_ide_ter_irq = valid_ide_irqs[temp_ide_ter - 1];
				temp_ide_ter = 1;
			}

			h = GetDlgItem(hdlg, IDC_COMBO_IDE_QUA);
			temp_ide_qua = SendMessage(h, CB_GETCURSEL, 0, 0);
			if (temp_ide_qua > 1)
			{
				temp_ide_qua_irq = valid_ide_irqs[temp_ide_qua - 1];
				temp_ide_qua = 1;
			}

			h = GetDlgItem(hdlg, IDC_CHECK_BUGGER);
			temp_bugger = SendMessage(h, BM_GETCHECK, 0, 0);

		default:
			return FALSE;
	}
	return FALSE;
}


int net_ignore_message = 0;

static void network_recalc_combos(HWND hdlg)
{
	HWND h;

	net_ignore_message = 1;

	h = GetDlgItem(hdlg, IDC_COMBO_PCAP);
	if (temp_net_type == NET_TYPE_PCAP)
	{
		EnableWindow(h, TRUE);
	}
	else
	{
		EnableWindow(h, FALSE);
	}

	h = GetDlgItem(hdlg, IDC_COMBO_NET);
	if (temp_net_type == NET_TYPE_SLIRP)
	{
		EnableWindow(h, TRUE);
	}
	else if ((temp_net_type == NET_TYPE_PCAP) &&
		 (network_dev_to_id(temp_pcap_dev) > 0))
	{
		EnableWindow(h, TRUE);
	}
	else
	{
		EnableWindow(h, FALSE);
	}

	h = GetDlgItem(hdlg, IDC_CONFIGURE_NET);
	if (network_card_has_config(temp_net_card) &&
	    (temp_net_type == NET_TYPE_SLIRP))
	{
		EnableWindow(h, TRUE);
	}
	else if (network_card_has_config(temp_net_card) &&
	         (temp_net_type == NET_TYPE_PCAP) &&
		  (network_dev_to_id(temp_pcap_dev) > 0))
	{
		EnableWindow(h, TRUE);
	}
	else
	{
		EnableWindow(h, FALSE);
	}

	net_ignore_message = 0;
}

static BOOL CALLBACK win_settings_network_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND h;
	int c = 0;
	int d = 0;
	LPTSTR lptsTemp;

        switch (message)
        {
		case WM_INITDIALOG:
			lptsTemp = (LPTSTR) malloc(512);

			h = GetDlgItem(hdlg, IDC_COMBO_NET_TYPE);
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM) L"None");
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM) L"PCap");
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM) L"SLiRP");
			SendMessage(h, CB_SETCURSEL, temp_net_type, 0);

			h = GetDlgItem(hdlg, IDC_COMBO_PCAP);
			if (temp_net_type == NET_TYPE_PCAP)
			{
				EnableWindow(h, TRUE);
			}
			else
			{
				EnableWindow(h, FALSE);
			}

			h = GetDlgItem(hdlg, IDC_COMBO_PCAP);
			for (c = 0; c < network_ndev; c++)
			{
				mbstowcs(lptsTemp, network_devs[c].description, strlen(network_devs[c].description) + 1);
				SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
			}
			SendMessage(h, CB_SETCURSEL, network_dev_to_id(temp_pcap_dev), 0);

			/*NIC config*/
			h = GetDlgItem(hdlg, IDC_COMBO_NET);
			c = d = 0;
			while (1)
			{
				char *s = network_card_getname(c);

				if (s[0] == '\0')
				{
					break;
				}

				settings_network_to_list[c] = d;

				if (network_card_available(c) && device_is_valid(network_card_getdevice(c), machines[temp_machine].flags))
				{
					if (c == 0)
					{
						SendMessage(h, CB_ADDSTRING, 0, (LPARAM) plat_get_string(2152));
					}
					else
					{
						mbstowcs(lptsTemp, s, strlen(s) + 1);
						SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
					}
					settings_list_to_network[d] = c;
					d++;
				}

				c++;
			}
			SendMessage(h, CB_SETCURSEL, settings_network_to_list[temp_net_card], 0);

			EnableWindow(h, d ? TRUE : FALSE);

			network_recalc_combos(hdlg);

			free(lptsTemp);

			return TRUE;

		case WM_COMMAND:
                	switch (LOWORD(wParam))
	                {
				case IDC_COMBO_NET_TYPE:
					if (net_ignore_message)
					{
						return FALSE;
					}

					h = GetDlgItem(hdlg, IDC_COMBO_NET_TYPE);
					temp_net_type = SendMessage(h, CB_GETCURSEL, 0, 0);

					network_recalc_combos(hdlg);
					break;

				case IDC_COMBO_PCAP:
					if (net_ignore_message)
					{
						return FALSE;
					}

					h = GetDlgItem(hdlg, IDC_COMBO_PCAP);
					memset(temp_pcap_dev, '\0', sizeof(temp_pcap_dev));
					strcpy(temp_pcap_dev, network_devs[SendMessage(h, CB_GETCURSEL, 0, 0)].device);

					network_recalc_combos(hdlg);
					break;

				case IDC_COMBO_NET:
					if (net_ignore_message)
					{
						return FALSE;
					}

					h = GetDlgItem(hdlg, IDC_COMBO_NET);
					temp_net_card = settings_list_to_network[SendMessage(h, CB_GETCURSEL, 0, 0)];

					network_recalc_combos(hdlg);
					break;

				case IDC_CONFIGURE_NET:
					if (net_ignore_message)
					{
						return FALSE;
					}

					h = GetDlgItem(hdlg, IDC_COMBO_NET);
					temp_net_card = settings_list_to_network[SendMessage(h, CB_GETCURSEL, 0, 0)];

					temp_deviceconfig |= deviceconfig_open(hdlg, (void *)network_card_getdevice(temp_net_card));
					break;
			}
			return FALSE;

		case WM_SAVESETTINGS:
			h = GetDlgItem(hdlg, IDC_COMBO_NET_TYPE);
			temp_net_type = SendMessage(h, CB_GETCURSEL, 0, 0);

			h = GetDlgItem(hdlg, IDC_COMBO_PCAP);
			memset(temp_pcap_dev, '\0', sizeof(temp_pcap_dev));
			strcpy(temp_pcap_dev, network_devs[SendMessage(h, CB_GETCURSEL, 0, 0)].device);

			h = GetDlgItem(hdlg, IDC_COMBO_NET);
			temp_net_card = settings_list_to_network[SendMessage(h, CB_GETCURSEL, 0, 0)];

		default:
			return FALSE;
	}

	return FALSE;
}

static BOOL win_settings_hard_disks_image_list_init(HWND hwndList)
{
	HICON hiconItem;
	HIMAGELIST hSmall;

	hSmall = ImageList_Create(GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON),
                                  ILC_MASK | ILC_COLOR32, 1, 1);

	hiconItem = LoadIcon(hinstance, (LPCWSTR) 192);
	ImageList_AddIcon(hSmall, hiconItem);
	DestroyIcon(hiconItem);

	hiconItem = LoadIcon(hinstance, (LPCWSTR) 176);
	ImageList_AddIcon(hSmall, hiconItem);
	DestroyIcon(hiconItem);

	ListView_SetImageList(hwndList, hSmall, LVSIL_SMALL);

	return TRUE;
}

int next_free_id = 0;

static void normalize_hd_list()
{
	hard_disk_t ihdd[HDD_NUM];
	int i, j;

	j = 0;
	memset(ihdd, 0x00, HDD_NUM * sizeof(hard_disk_t));

	for (i = 0; i < HDD_NUM; i++)
	{
		if (temp_hdd[i].bus != HDD_BUS_DISABLED)
		{
			memcpy(&(ihdd[j]), &(temp_hdd[i]), sizeof(hard_disk_t));
			j++;
		}
	}

	memcpy(temp_hdd, ihdd, HDD_NUM * sizeof(hard_disk_t));
}

int hdc_id_to_listview_index[HDD_NUM];
int hd_listview_items;

hard_disk_t new_hdd;
int hdlv_current_sel;

static int get_selected_hard_disk(HWND hdlg)
{
	int hard_disk = -1;
	int i, j = 0;
	HWND h;

	if (hd_listview_items == 0)
	{
		return 0;
	}

	for (i = 0; i < hd_listview_items; i++)
	{
		h = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
		j = ListView_GetItemState(h, i, LVIS_SELECTED);
		if (j)
		{
			hard_disk = i;
		}
	}

	return hard_disk;
}

static void add_locations(HWND hdlg)
{
	LPTSTR lptsTemp;
	HWND h;
	int i = 0;

	lptsTemp = (LPTSTR) malloc(512);

	h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
	for (i = 0; i < 7; i++)
	{
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM)plat_get_string(IDS_4352 + i));
	}

	h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
	for (i = 0; i < 8; i++)
	{
		wsprintf(lptsTemp, plat_get_string(IDS_4097), i >> 1, i & 1);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
	}

	h = GetDlgItem(hdlg, IDC_COMBO_HD_ID);
	for (i = 0; i < 16; i++)
	{
		wsprintf(lptsTemp, plat_get_string(IDS_4098), i);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
	}

	h = GetDlgItem(hdlg, IDC_COMBO_HD_LUN);
	for (i = 0; i < 8; i++)
	{
		wsprintf(lptsTemp, plat_get_string(IDS_4098), i);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
	}

	h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL_IDE);
	for (i = 0; i < 8; i++)
	{
		wsprintf(lptsTemp, plat_get_string(IDS_4097), i >> 1, i & 1);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
	}

	free(lptsTemp);
}

static void recalc_location_controls(HWND hdlg, int is_add_dlg)
{
	int i = 0;
	HWND h;

	int bus = 0;

	for (i = IDT_1722; i <= IDT_1724; i++)
	{
		h = GetDlgItem(hdlg, i);
		EnableWindow(h, FALSE);
		ShowWindow(h, SW_HIDE);
	}

	h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
	EnableWindow(h, FALSE);
	ShowWindow(h, SW_HIDE);

	h = GetDlgItem(hdlg, IDC_COMBO_HD_ID);
	EnableWindow(h, FALSE);
	ShowWindow(h, SW_HIDE);

	h = GetDlgItem(hdlg, IDC_COMBO_HD_LUN);
	EnableWindow(h, FALSE);
	ShowWindow(h, SW_HIDE);

	h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL_IDE);
	EnableWindow(h, FALSE);
	ShowWindow(h, SW_HIDE);

	if ((hd_listview_items > 0) || is_add_dlg)
	{
		h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
		bus = SendMessage(h, CB_GETCURSEL, 0, 0);
		bus++;

		switch(bus)
		{
			case HDD_BUS_MFM:		/* MFM */
				h = GetDlgItem(hdlg, IDT_1722);
				ShowWindow(h, SW_SHOW);
				EnableWindow(h, TRUE);

				h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
				ShowWindow(h, SW_SHOW);
				EnableWindow(h, TRUE);
				SendMessage(h, CB_SETCURSEL, is_add_dlg ? new_hdd.mfm_channel : temp_hdd[hdlv_current_sel].mfm_channel, 0);
				break;
			case HDD_BUS_XTIDE:		/* XT IDE */
				h = GetDlgItem(hdlg, IDT_1722);
				ShowWindow(h, SW_SHOW);
				EnableWindow(h, TRUE);

				h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
				ShowWindow(h, SW_SHOW);
				EnableWindow(h, TRUE);
				SendMessage(h, CB_SETCURSEL, is_add_dlg ? new_hdd.xtide_channel : temp_hdd[hdlv_current_sel].xtide_channel, 0);
				break;
			case HDD_BUS_ESDI:		/* ESDI */
				h = GetDlgItem(hdlg, IDT_1722);
				ShowWindow(h, SW_SHOW);
				EnableWindow(h, TRUE);

				h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
				ShowWindow(h, SW_SHOW);
				EnableWindow(h, TRUE);
				SendMessage(h, CB_SETCURSEL, is_add_dlg ? new_hdd.esdi_channel : temp_hdd[hdlv_current_sel].esdi_channel, 0);
				break;
			case HDD_BUS_IDE_PIO_ONLY:		/* IDE (PIO-only) */
			case HDD_BUS_IDE_PIO_AND_DMA:		/* IDE (PIO and DMA) */
				h = GetDlgItem(hdlg, IDT_1722);
				ShowWindow(h, SW_SHOW);
				EnableWindow(h, TRUE);

				h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL_IDE);
				ShowWindow(h, SW_SHOW);
				EnableWindow(h, TRUE);
				SendMessage(h, CB_SETCURSEL, is_add_dlg ? new_hdd.ide_channel : temp_hdd[hdlv_current_sel].ide_channel, 0);
				break;
			case HDD_BUS_SCSI:		/* SCSI */
			case HDD_BUS_SCSI_REMOVABLE:		/* SCSI (removable) */
				h = GetDlgItem(hdlg, IDT_1723);
				ShowWindow(h, SW_SHOW);
				EnableWindow(h, TRUE);
				h = GetDlgItem(hdlg, IDT_1724);
				ShowWindow(h, SW_SHOW);
				EnableWindow(h, TRUE);

				h = GetDlgItem(hdlg, IDC_COMBO_HD_ID);
				ShowWindow(h, SW_SHOW);
				EnableWindow(h, TRUE);
				SendMessage(h, CB_SETCURSEL, is_add_dlg ? new_hdd.scsi_id : temp_hdd[hdlv_current_sel].scsi_id, 0);

				h = GetDlgItem(hdlg, IDC_COMBO_HD_LUN);
				ShowWindow(h, SW_SHOW);
				EnableWindow(h, TRUE);
				SendMessage(h, CB_SETCURSEL, is_add_dlg ? new_hdd.scsi_lun : temp_hdd[hdlv_current_sel].scsi_lun, 0);
				break;
		}
	}

	if ((hd_listview_items == 0) && !is_add_dlg)
	{
		h = GetDlgItem(hdlg, IDT_1721);
		EnableWindow(h, FALSE);
		ShowWindow(h, SW_HIDE);

		h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
		EnableWindow(h, FALSE);		ShowWindow(h, SW_HIDE);
	}
	else
	{
		h = GetDlgItem(hdlg, IDT_1721);
		ShowWindow(h, SW_SHOW);
		EnableWindow(h, TRUE);

		h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
		ShowWindow(h, SW_SHOW);
		EnableWindow(h, TRUE);
	}
}

static void recalc_next_free_id(HWND hdlg)
{
	HWND h;
	int i;

	int c_mfm = 0;
	int c_esdi = 0;
	int c_xtide = 0;
	int c_ide_pio = 0;
	int c_ide_dma = 0;
	int c_scsi = 0;
	int enable_add = 0;

	next_free_id = -1;

	for (i = 0; i < HDD_NUM; i++)
	{
		if (temp_hdd[i].bus == HDD_BUS_MFM)
		{
			c_mfm++;
		}
		else if (temp_hdd[i].bus == HDD_BUS_ESDI)
		{
			c_esdi++;
		}
		else if (temp_hdd[i].bus == HDD_BUS_XTIDE)
		{
			c_xtide++;
		}
		else if (temp_hdd[i].bus == HDD_BUS_IDE_PIO_ONLY)
		{
			c_ide_pio++;
		}
		else if (temp_hdd[i].bus == HDD_BUS_IDE_PIO_AND_DMA)
		{
			c_ide_dma++;
		}
		else if (temp_hdd[i].bus == HDD_BUS_SCSI)
		{
			c_scsi++;
		}
		else if (temp_hdd[i].bus == HDD_BUS_SCSI_REMOVABLE)
		{
			c_scsi++;
		}
	}

	for (i = 0; i < HDD_NUM; i++)
	{
		if (temp_hdd[i].bus == HDD_BUS_DISABLED)
		{
			next_free_id = i;
			break;
		}
	}

	/* pclog("Next free ID: %i\n", next_free_id); */

	enable_add = enable_add || (next_free_id >= 0);
	/* pclog("Enable add: %i\n", enable_add); */
	enable_add = enable_add && ((c_mfm < MFM_NUM) || (c_esdi < ESDI_NUM) || (c_xtide < XTIDE_NUM) || (c_ide_pio < IDE_NUM) || (c_ide_dma < IDE_NUM) || (c_scsi < SCSI_NUM));
	/* pclog("Enable add: %i\n", enable_add); */

	h = GetDlgItem(hdlg, IDC_BUTTON_HDD_ADD_NEW);

	if (enable_add)
	{
		EnableWindow(h, TRUE);
	}
	else
	{
		EnableWindow(h, FALSE);
	}

	h = GetDlgItem(hdlg, IDC_BUTTON_HDD_ADD);

	if (enable_add)
	{
		EnableWindow(h, TRUE);
	}
	else
	{
		EnableWindow(h, FALSE);
	}

	h = GetDlgItem(hdlg, IDC_BUTTON_HDD_REMOVE);

	if ((c_mfm == 0) && (c_esdi == 0) && (c_xtide == 0) && (c_ide_pio == 0) && (c_ide_dma == 0) && (c_scsi == 0))
	{
		EnableWindow(h, FALSE);
	}
	else
	{
		EnableWindow(h, TRUE);
	}
}

static void win_settings_hard_disks_update_item(HWND hwndList, int i, int column)
{
	LVITEM lvI;
	WCHAR szText[256];

	lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
	lvI.stateMask = lvI.iSubItem = lvI.state = 0;

	lvI.iSubItem = column;
	lvI.iItem = i;

	if (column == 0)
	{
		switch(temp_hdd[i].bus)
		{
			case HDD_BUS_MFM:
				wsprintf(szText, plat_get_string(IDS_4608), temp_hdd[i].mfm_channel >> 1, temp_hdd[i].mfm_channel & 1);
				break;
			case HDD_BUS_XTIDE:
				wsprintf(szText, plat_get_string(IDS_4609), temp_hdd[i].xtide_channel >> 1, temp_hdd[i].xtide_channel & 1);
				break;
			case HDD_BUS_ESDI:
				wsprintf(szText, plat_get_string(IDS_4610), temp_hdd[i].esdi_channel >> 1, temp_hdd[i].esdi_channel & 1);
				break;
			case HDD_BUS_IDE_PIO_ONLY:
				wsprintf(szText, plat_get_string(IDS_4611), temp_hdd[i].ide_channel >> 1, temp_hdd[i].ide_channel & 1);
				break;
			case HDD_BUS_IDE_PIO_AND_DMA:
				wsprintf(szText, plat_get_string(IDS_4612), temp_hdd[i].ide_channel >> 1, temp_hdd[i].ide_channel & 1);
				break;
			case HDD_BUS_SCSI:
				wsprintf(szText, plat_get_string(IDS_4613), temp_hdd[i].scsi_id, temp_hdd[i].scsi_lun);
				break;
			case HDD_BUS_SCSI_REMOVABLE:
				wsprintf(szText, plat_get_string(IDS_4614), temp_hdd[i].scsi_id, temp_hdd[i].scsi_lun);
				break;
		}
		lvI.pszText = szText;
		lvI.iImage = (temp_hdd[i].bus == HDD_BUS_SCSI_REMOVABLE) ? 1 : 0;
	}
	else if (column == 1)
	{
		lvI.pszText = temp_hdd[i].fn;
		lvI.iImage = 0;
	}
	else if (column == 2)
	{
		wsprintf(szText, plat_get_string(IDS_4098), temp_hdd[i].tracks);
		lvI.pszText = szText;
		lvI.iImage = 0;
	}
	else if (column == 3)
	{
		wsprintf(szText, plat_get_string(IDS_4098), temp_hdd[i].hpc);
		lvI.pszText = szText;
		lvI.iImage = 0;
	}
	else if (column == 4)
	{
		wsprintf(szText, plat_get_string(IDS_4098), temp_hdd[i].spt);
		lvI.pszText = szText;
		lvI.iImage = 0;
	}
	else if (column == 5)
	{
		wsprintf(szText, plat_get_string(IDS_4098), (temp_hdd[i].tracks * temp_hdd[i].hpc * temp_hdd[i].spt) >> 11);
		lvI.pszText = szText;
		lvI.iImage = 0;
	}

	if (ListView_SetItem(hwndList, &lvI) == -1)
	{
		return;
	}
}

static BOOL win_settings_hard_disks_recalc_list(HWND hwndList)
{
	LVITEM lvI;
	int i = 0;
	int j = 0;
	WCHAR szText[256];

	hd_listview_items = 0;
	hdlv_current_sel = -1;

	ListView_DeleteAllItems(hwndList);

	lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
	lvI.stateMask = lvI.iSubItem = lvI.state = 0;

	for (i = 0; i < HDD_NUM; i++)
	{
		if (temp_hdd[i].bus > 0)
		{
			hdc_id_to_listview_index[i] = j;
			lvI.iSubItem = 0;
			switch(temp_hdd[i].bus)
			{
				case HDD_BUS_MFM:
					wsprintf(szText, plat_get_string(IDS_4608), temp_hdd[i].mfm_channel >> 1, temp_hdd[i].mfm_channel & 1);
					break;
				case HDD_BUS_XTIDE:
					wsprintf(szText, plat_get_string(IDS_4609), temp_hdd[i].xtide_channel >> 1, temp_hdd[i].xtide_channel & 1);
					break;
				case HDD_BUS_ESDI:
					wsprintf(szText, plat_get_string(IDS_4610), temp_hdd[i].esdi_channel >> 1, temp_hdd[i].esdi_channel & 1);
					break;
				case HDD_BUS_IDE_PIO_ONLY:
					wsprintf(szText, plat_get_string(IDS_4611), temp_hdd[i].ide_channel >> 1, temp_hdd[i].ide_channel & 1);
					break;
				case HDD_BUS_IDE_PIO_AND_DMA:
					wsprintf(szText, plat_get_string(IDS_4612), temp_hdd[i].ide_channel >> 1, temp_hdd[i].ide_channel & 1);
					break;
				case HDD_BUS_SCSI:
					wsprintf(szText, plat_get_string(IDS_4613), temp_hdd[i].scsi_id, temp_hdd[i].scsi_lun);
					break;
				case HDD_BUS_SCSI_REMOVABLE:
					wsprintf(szText, plat_get_string(IDS_4614), temp_hdd[i].scsi_id, temp_hdd[i].scsi_lun);
					break;
			}
			lvI.pszText = szText;
			lvI.iItem = j;
			lvI.iImage = (temp_hdd[i].bus == HDD_BUS_SCSI_REMOVABLE) ? 1 : 0;

			if (ListView_InsertItem(hwndList, &lvI) == -1)
			{
				return FALSE;
			}

			lvI.iSubItem = 1;
			lvI.pszText = temp_hdd[i].fn;
			lvI.iItem = j;
			lvI.iImage = 0;

			if (ListView_SetItem(hwndList, &lvI) == -1)
			{
				return FALSE;
			}

			lvI.iSubItem = 2;
			wsprintf(szText, plat_get_string(IDS_4098), temp_hdd[i].tracks);
			lvI.pszText = szText;
			lvI.iItem = j;
			lvI.iImage = 0;

			if (ListView_SetItem(hwndList, &lvI) == -1)
			{
				return FALSE;
			}

			lvI.iSubItem = 3;
			wsprintf(szText, plat_get_string(IDS_4098), temp_hdd[i].hpc);
			lvI.pszText = szText;
			lvI.iItem = j;
			lvI.iImage = 0;

			if (ListView_SetItem(hwndList, &lvI) == -1)
			{
				return FALSE;
			}

			lvI.iSubItem = 4;
			wsprintf(szText, plat_get_string(IDS_4098), temp_hdd[i].spt);
			lvI.pszText = szText;
			lvI.iItem = j;
			lvI.iImage = 0;

			if (ListView_SetItem(hwndList, &lvI) == -1)
			{
				return FALSE;
			}

			lvI.iSubItem = 5;
			wsprintf(szText, plat_get_string(IDS_4098), (temp_hdd[i].tracks * temp_hdd[i].hpc * temp_hdd[i].spt) >> 11);
			lvI.pszText = szText;
			lvI.iItem = j;
			lvI.iImage = 0;

			if (ListView_SetItem(hwndList, &lvI) == -1)
			{
				return FALSE;
			}

			j++;
		}
		else
		{
			hdc_id_to_listview_index[i] = -1;
		}
	}

	hd_listview_items = j;

	return TRUE;
}

/* Icon, Bus, File, C, H, S, Size */
#define C_COLUMNS_HARD_DISKS 6

static BOOL win_settings_hard_disks_init_columns(HWND hwndList)
{
	LVCOLUMN lvc;
	int iCol;

	lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;

	for (iCol = 0; iCol < C_COLUMNS_HARD_DISKS; iCol++)
	{
		lvc.iSubItem = iCol;
		lvc.pszText = plat_get_string(2082 + iCol);

		switch(iCol)
		{

			case 0: /* Bus */
				lvc.cx = 135;
				lvc.fmt = LVCFMT_LEFT;
				break;
			case 2: /* Cylinders */
				lvc.cx = 41;
				lvc.fmt = LVCFMT_RIGHT;
				break;
			case 3: /* Heads */
			case 4: /* Sectors */
				lvc.cx = 25;
				lvc.fmt = LVCFMT_RIGHT;
				break;
			case 1: /* File */
				lvc.cx = 150;
				lvc.fmt = LVCFMT_LEFT;
				break;
			case 5: /* Size (MB) 8 */
				lvc.cx = 41;
				lvc.fmt = LVCFMT_RIGHT;
				break;
		}

		if (ListView_InsertColumn(hwndList, iCol, &lvc) == -1)
		{
			return FALSE;
		}
	}

	return TRUE;
}

static void get_edit_box_contents(HWND hdlg, int id, uint64_t *val)
{
	HWND h;
	WCHAR szText[256];
	char stransi[256];

	h = GetDlgItem(hdlg, id);
	SendMessage(h, WM_GETTEXT, 255, (LPARAM) szText);
	wcstombs(stransi, szText, 256);
	sscanf(stransi, "%" PRIu64, val);
}

static void get_combo_box_selection(HWND hdlg, int id, uint64_t *val)
{
	HWND h;

	h = GetDlgItem(hdlg, id);
	*val = SendMessage(h, CB_GETCURSEL, 0, 0);
}

static void set_edit_box_contents(HWND hdlg, int id, uint64_t val)
{
	HWND h;
	WCHAR szText[256];

	h = GetDlgItem(hdlg, id);
	wsprintf(szText, plat_get_string(IDS_2156), val);
	SendMessage(h, WM_SETTEXT, (WPARAM) wcslen(szText), (LPARAM) szText);
}

int hard_disk_added = 0;
int max_spt = 63;
int max_hpc = 255;
int max_tracks = 266305;

int no_update = 0;

int existing = 0;
uint64_t selection = 127;

uint64_t spt, hpc, tracks, size;
wchar_t hd_file_name[512];

static hard_disk_t *hdd_ptr;

static int hdconf_initialize_hdt_combo(HWND hdlg)
{
        HWND h;
	int i = 0;
	uint64_t temp_size = 0;
	uint64_t size_mb = 0;
	WCHAR szText[256];

	selection = 127;

	h = GetDlgItem(hdlg, IDC_COMBO_HD_TYPE);
	for (i = 0; i < 127; i++)
	{	
		temp_size = hdd_table[i][0] * hdd_table[i][1] * hdd_table[i][2];
		size_mb = temp_size >> 11;
                wsprintf(szText, plat_get_string(IDS_2157), size_mb, hdd_table[i][0], hdd_table[i][1], hdd_table[i][2]);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) szText);
		if ((tracks == hdd_table[i][0]) && (hpc == hdd_table[i][1]) && (spt == hdd_table[i][2]))
		{
			selection = i;
		}
	}
	SendMessage(h, CB_ADDSTRING, 0, (LPARAM)plat_get_string(IDS_4100));
	SendMessage(h, CB_ADDSTRING, 0, (LPARAM)plat_get_string(IDS_4101));
	SendMessage(h, CB_SETCURSEL, selection, 0);
	return selection;
}

static void recalc_selection(HWND hdlg)
{
	HWND h;
	int i = 0;

	selection = 127;
	h = GetDlgItem(hdlg, IDC_COMBO_HD_TYPE);
	for (i = 0; i < 127; i++)
	{	
		if ((tracks == hdd_table[i][0]) && (hpc == hdd_table[i][1]) && (spt == hdd_table[i][2]))
		{
			selection = i;
		}
	}
	if ((selection == 127) && (hpc == 16) && (spt == 63))
	{
		selection = 128;
	}
	SendMessage(h, CB_SETCURSEL, selection, 0);
}

static int chs_enabled = 0;

static BOOL CALLBACK win_settings_hard_disks_add_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND h;
	int64_t i = 0;
	uint64_t temp;
	FILE *f;
	uint32_t sector_size = 512;
	uint32_t zero = 0;
	uint32_t base = 0x1000;
	uint64_t signature = 0xD778A82044445459ll;
	char buf[512];
	char *big_buf;
	int b = 0;
	uint64_t r = 0;

        switch (message)
        {
		case WM_INITDIALOG:
			memset(hd_file_name, 0, sizeof(hd_file_name));

			if (existing & 2)
			{
				next_free_id = (existing >> 3) & 0x1f;
				hdd_ptr = &(hdd[next_free_id]);
			}
			else
			{
				hdd_ptr = &(temp_hdd[next_free_id]);
			}

			SetWindowText(hdlg, plat_get_string((existing & 1) ? IDS_4103 : IDS_4102));

			no_update = 1;
			spt = (existing & 1) ? 0 : 17;
			set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, spt);
			hpc = (existing & 1) ? 0 : 15;
			set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, hpc);
			tracks = (existing & 1) ? 0 : 1023;
			set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, tracks);
			size = (tracks * hpc * spt) << 9;
			set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, size >> 20);
			hdconf_initialize_hdt_combo(hdlg);
			if (existing & 1)
			{
				h = GetDlgItem(hdlg, IDC_EDIT_HD_SPT);
				EnableWindow(h, FALSE);
				h = GetDlgItem(hdlg, IDC_EDIT_HD_HPC);
				EnableWindow(h, FALSE);
				h = GetDlgItem(hdlg, IDC_EDIT_HD_CYL);
				EnableWindow(h, FALSE);
				h = GetDlgItem(hdlg, IDC_EDIT_HD_SIZE);
				EnableWindow(h, FALSE);
				h = GetDlgItem(hdlg, IDC_COMBO_HD_TYPE);
				EnableWindow(h, FALSE);
				chs_enabled = 0;
			}
			else
			{
				chs_enabled = 1;
			}
			add_locations(hdlg);
			h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
			if (existing & 2)
			{
				hdd_ptr->bus = HDD_BUS_SCSI_REMOVABLE;
				max_spt = 99;
				max_hpc = 255;
			}
			else
			{
				hdd_ptr->bus = HDD_BUS_IDE_PIO_ONLY;
				max_spt = 63;
				max_hpc = 255;
			}
			SendMessage(h, CB_SETCURSEL, hdd_ptr->bus, 0);
			max_tracks = 266305;
			recalc_location_controls(hdlg, 1);
			if (existing & 2)
			{
				/* We're functioning as a load image dialog for a removable SCSI hard disk,
				   called from win.c, so let's hide the bus selection as we should not
				   allow it at this point. */
				EnableWindow(h, FALSE);
				ShowWindow(h, SW_HIDE);

				h = GetDlgItem(hdlg, 1798);
				ShowWindow(h, SW_HIDE);

				/* Disable and hide the SCSI ID and LUN combo boxes. */
				h = GetDlgItem(hdlg, IDC_COMBO_HD_ID);
				EnableWindow(h, FALSE);
				ShowWindow(h, SW_HIDE);

				h = GetDlgItem(hdlg, IDC_COMBO_HD_LUN);
				EnableWindow(h, FALSE);
				ShowWindow(h, SW_HIDE);

				/* Set the file name edit box contents to our existing parameters. */
				h = GetDlgItem(hdlg, IDC_EDIT_HD_FILE_NAME);
				SendMessage(h, WM_SETTEXT, 0, (LPARAM) hdd[next_free_id].fn);
			}
			else
			{
				h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
				SendMessage(h, CB_SETCURSEL, 0, 0);
				h = GetDlgItem(hdlg, IDC_COMBO_HD_ID);
				SendMessage(h, CB_SETCURSEL, 0, 0);
				h = GetDlgItem(hdlg, IDC_COMBO_HD_LUN);
				SendMessage(h, CB_SETCURSEL, 0, 0);
				h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL_IDE);
				SendMessage(h, CB_SETCURSEL, 0, 0);
			}
			h = GetDlgItem(hdlg, IDC_EDIT_HD_FILE_NAME);
			EnableWindow(h, FALSE);
			no_update = 0;
			return TRUE;

		case WM_COMMAND:
	               	switch (LOWORD(wParam))
        	        {
				case IDOK:
					if (!(existing & 2))
					{
						h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
						hdd_ptr->bus = SendMessage(h, CB_GETCURSEL, 0, 0) + 1;
					}

					/* Make sure no file name is allowed with removable SCSI hard disks. */
					if ((wcslen(hd_file_name) == 0) && (hdd_ptr->bus != HDD_BUS_SCSI_REMOVABLE))
					{
						hdd_ptr->bus = HDD_BUS_DISABLED;
						settings_msgbox(MBX_ERROR, (wchar_t *)IDS_4112);
						return TRUE;
					}
					else if ((wcslen(hd_file_name) == 0) && (hdd_ptr->bus == HDD_BUS_SCSI_REMOVABLE))
					{
						/* Mark hard disk added but return empty - it will signify the disk was ejected. */
						hdd_ptr->spt = hdd_ptr->hpc = hdd_ptr->tracks = 0;
						memset(hdd_ptr->fn, 0, sizeof(hdd_ptr->fn));

						goto hd_add_ok_common;
					}

					get_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, &(hdd_ptr->spt));
					get_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, &(hdd_ptr->hpc));
					get_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, &(hdd_ptr->tracks));
					spt = hdd_ptr->spt;
					hpc = hdd_ptr->hpc;
					tracks = hdd_ptr->tracks;

					if (existing & 2)
					{
						if (hdd_ptr->bus == HDD_BUS_SCSI_REMOVABLE)
						{
							memset(hdd_ptr->prev_fn, 0, sizeof(hdd_ptr->prev_fn));
							wcscpy(hdd_ptr->prev_fn, hdd_ptr->fn);
						}
					}
					else
					{
						switch(hdd_ptr->bus)
						{
							case HDD_BUS_MFM:
								h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
								hdd_ptr->mfm_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
								break;
							case HDD_BUS_ESDI:
								h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
								hdd_ptr->esdi_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
								break;
							case HDD_BUS_XTIDE:
								h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
								hdd_ptr->xtide_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
								break;
							case HDD_BUS_IDE_PIO_ONLY:
							case HDD_BUS_IDE_PIO_AND_DMA:
								h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL_IDE);
								hdd_ptr->ide_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
								break;
							case HDD_BUS_SCSI:
							case HDD_BUS_SCSI_REMOVABLE:
								h = GetDlgItem(hdlg, IDC_COMBO_HD_ID);
								hdd_ptr->scsi_id = SendMessage(h, CB_GETCURSEL, 0, 0);
								h = GetDlgItem(hdlg, IDC_COMBO_HD_LUN);
								hdd_ptr->scsi_lun = SendMessage(h, CB_GETCURSEL, 0, 0);
								break;
						}
					}

					memset(hdd_ptr->fn, 0, sizeof(hdd_ptr->fn));
					wcscpy(hdd_ptr->fn, hd_file_name);

					sector_size = 512;

					if (!(existing & 1) && (wcslen(hd_file_name) > 0))
					{
						f = _wfopen(hd_file_name, L"wb");

						if (image_is_hdi(hd_file_name))
						{
							if (size >= 0x100000000ll)
							{
								fclose(f);
								settings_msgbox(MBX_ERROR, (wchar_t *)IDS_4104);
								return TRUE;
							}

							fwrite(&zero, 1, 4, f);			/* 00000000: Zero/unknown */
							fwrite(&zero, 1, 4, f);			/* 00000004: Zero/unknown */
							fwrite(&base, 1, 4, f);			/* 00000008: Offset at which data starts */
							fwrite(&size, 1, 4, f);			/* 0000000C: Full size of the data (32-bit) */
							fwrite(&sector_size, 1, 4, f);		/* 00000010: Sector size in bytes */
							fwrite(&spt, 1, 4, f);			/* 00000014: Sectors per cylinder */
							fwrite(&hpc, 1, 4, f);			/* 00000018: Heads per cylinder */
							fwrite(&tracks, 1, 4, f);		/* 0000001C: Cylinders */

							for (i = 0; i < 0x3f8; i++)
							{
								fwrite(&zero, 1, 4, f);
							}
						}
						else if (image_is_hdx(hd_file_name, 0))
						{
							if (size > 0xffffffffffffffffll)
							{
								fclose(f);
								settings_msgbox(MBX_ERROR, (wchar_t *)IDS_4105);
								return TRUE;							
							}

							fwrite(&signature, 1, 8, f);		/* 00000000: Signature */
							fwrite(&size, 1, 8, f);			/* 00000008: Full size of the data (64-bit) */
							fwrite(&sector_size, 1, 4, f);		/* 00000010: Sector size in bytes */
							fwrite(&spt, 1, 4, f);			/* 00000014: Sectors per cylinder */
							fwrite(&hpc, 1, 4, f);			/* 00000018: Heads per cylinder */
							fwrite(&tracks, 1, 4, f);		/* 0000001C: Cylinders */
							fwrite(&zero, 1, 4, f);			/* 00000020: [Translation] Sectors per cylinder */
							fwrite(&zero, 1, 4, f);			/* 00000004: [Translation] Heads per cylinder */
						}

						memset(buf, 0, 512);
						size >>= 9;
						r = (size >> 11) << 11;
						size -= r;
						r >>= 11;

						if (size)
						{
							for (i = 0; i < size; i++)
							{
								fwrite(buf, 1, 512, f);
							}
						}

						if (r)
						{
							big_buf = (char *) malloc(1048576);
							memset(big_buf, 0, 1048576);
							for (i = 0; i < r; i++)
							{
								fwrite(big_buf, 1, 1048576, f);
							}
							free(big_buf);
						}

						fclose(f);
						settings_msgbox(MBX_INFO, (wchar_t *)IDS_4113);	                        
					}

hd_add_ok_common:
					hard_disk_added = 1;
					EndDialog(hdlg, 0);
					return TRUE;

				case IDCANCEL:
					hard_disk_added = 0;
					if (!(existing & 2))
					{
						hdd_ptr->bus = HDD_BUS_DISABLED;
					}
					EndDialog(hdlg, 0);
					return TRUE;

				case IDC_CFILE:
		                        if (!file_dlg_w(hdlg, plat_get_string(IDS_4106), L"", !(existing & 1)))
       			                {
						if (!(existing & 1))
						{
							f = _wfopen(wopenfilestring, L"rb");
							if (f != NULL)
							{
								fclose(f);
								if (settings_msgbox(MBX_QUESTION, (wchar_t *)IDS_4111) != 0)	/* yes */
								{
									return FALSE;
								}
							}
						}

						f = _wfopen(wopenfilestring, (existing & 1) ? L"rb" : L"wb");
						if (f == NULL)
						{
hdd_add_file_open_error:
							settings_msgbox(MBX_ERROR, (existing & 1) ? (wchar_t *)IDS_4107 : (wchar_t *)IDS_4108);
							return TRUE;
						}
						if (existing & 1)
						{
							if (image_is_hdi(wopenfilestring) || image_is_hdx(wopenfilestring, 1))
							{
								fseeko64(f, 0x10, SEEK_SET);
								fread(&sector_size, 1, 4, f);
								if (sector_size != 512)
								{
									settings_msgbox(MBX_ERROR, (wchar_t *)IDS_4109);
									fclose(f);
									return TRUE;
								}
								spt = hpc = tracks = 0;
								fread(&spt, 1, 4, f);
								fread(&hpc, 1, 4, f);
								fread(&tracks, 1, 4, f);
							}
							else
							{
								fseeko64(f, 0, SEEK_END);
								size = ftello64(f);
								fclose(f);
								if (((size % 17) == 0) && (size <= 142606336))
								{
									spt = 17;
									if (size <= 26738688)
									{
										hpc = 4;
									}
									else if (((size % 3072) == 0) && (size <= 53477376))
									{
										hpc = 6;
									}
									else
									{
										for (i = 5; i < 16; i++)
										{
											if (((size % (i << 9)) == 0) && (size <= ((i * 17) << 19)))
											{
												break;
											}
											if (i == 5)
											{
												i++;
											}
										}
										hpc = i;
									}
								}
								else
								{
									spt = 63;
									hpc = 16;
								}

								tracks = ((size >> 9) / hpc) / spt;
							}

							if ((spt > max_spt) || (hpc > max_hpc) || (tracks > max_tracks))
							{
								goto hdd_add_file_open_error;
							}
							no_update = 1;

							set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, spt);
							set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, hpc);
							set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, tracks);
							set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, size >> 20);
							recalc_selection(hdlg);

							h = GetDlgItem(hdlg, IDC_EDIT_HD_SPT);
							EnableWindow(h, TRUE);
							h = GetDlgItem(hdlg, IDC_EDIT_HD_HPC);
							EnableWindow(h, TRUE);
							h = GetDlgItem(hdlg, IDC_EDIT_HD_CYL);
							EnableWindow(h, TRUE);
							h = GetDlgItem(hdlg, IDC_EDIT_HD_SIZE);
							EnableWindow(h, TRUE);
							h = GetDlgItem(hdlg, IDC_COMBO_HD_TYPE);
							EnableWindow(h, TRUE);

							chs_enabled = 1;

							no_update = 0;
					}
						else
						{
							fclose(f);
						}
					}

					h = GetDlgItem(hdlg, IDC_EDIT_HD_FILE_NAME);
					SendMessage(h, WM_SETTEXT, 0, (LPARAM) wopenfilestring);
					memset(hd_file_name, 0, sizeof(hd_file_name));
					wcscpy(hd_file_name, wopenfilestring);

					return TRUE;

				case IDC_EDIT_HD_CYL:
					if (no_update)
					{
						return FALSE;
					}

					no_update = 1;
					get_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, &temp);
					if (temp != tracks)
					{
						tracks = temp;
						size = (tracks * hpc * spt) << 9;
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, size >> 20);
						recalc_selection(hdlg);
					}

					if (tracks > max_tracks)
					{
						tracks = max_tracks;
						size = (tracks * hpc * spt) << 9;
						set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, tracks);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (size >> 20));
						recalc_selection(hdlg);
					}

					no_update = 0;
					break;

				case IDC_EDIT_HD_HPC:
					if (no_update)
					{
						return FALSE;
					}

					no_update = 1;
					get_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, &temp);
					if (temp != hpc)
					{
						hpc = temp;
						size = (tracks * hpc * spt) << 9;
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, size >> 20);
						recalc_selection(hdlg);
					}

					if (hpc > max_hpc)
					{
						hpc = max_hpc;
						size = (tracks * hpc * spt) << 9;
						set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, hpc);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (size >> 20));
						recalc_selection(hdlg);
					}

					no_update = 0;
					break;

				case IDC_EDIT_HD_SPT:
					if (no_update)
					{
						return FALSE;
					}

					no_update = 1;
					get_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, &temp);
					if (temp != spt)
					{
						spt = temp;
						size = (tracks * hpc * spt) << 9;
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, size >> 20);
						recalc_selection(hdlg);
					}

					if (spt > max_spt)
					{
						spt = max_spt;
						size = (tracks * hpc * spt) << 9;
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, spt);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (size >> 20));
						recalc_selection(hdlg);
					}

					no_update = 0;
					break;

				case IDC_EDIT_HD_SIZE:
					if (no_update)
					{
						return FALSE;
					}

					no_update = 1;
					get_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, &temp);
					if (temp != (size >> 20))
					{
						size = temp << 20;
						tracks = ((size >> 9) / hpc) / spt;
						set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, tracks);
						recalc_selection(hdlg);
					}

					if (tracks > max_tracks)
					{
						tracks = max_tracks;
						size = (tracks * hpc * spt) << 9;
						set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, tracks);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (size >> 20));
						recalc_selection(hdlg);
					}

					no_update = 0;
					break;

				case IDC_COMBO_HD_TYPE:
					if (no_update)
					{
						return FALSE;
					}

					no_update = 1;
					get_combo_box_selection(hdlg, IDC_COMBO_HD_TYPE, &temp);
					if ((temp != selection) && (temp != 127) && (temp != 128))
					{
						selection = temp;
						tracks = hdd_table[selection][0];
						hpc = hdd_table[selection][1];
						spt = hdd_table[selection][2];
						size = (tracks * hpc * spt) << 9;
						set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, tracks);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, hpc);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, spt);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, size >> 20);
					}
					else if ((temp != selection) && (temp == 127))
					{
						selection = temp;
					}
					else if ((temp != selection) && (temp == 128))
					{
						selection = temp;
						hpc = 16;
						spt = 63;
						size = (tracks * hpc * spt) << 9;
						set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, hpc);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, spt);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, size >> 20);
					}

					if (spt > max_spt)
					{
						spt = max_spt;
						size = (tracks * hpc * spt) << 9;
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, spt);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (size >> 20));
						recalc_selection(hdlg);
					}

					if (hpc > max_hpc)
					{
						hpc = max_hpc;
						size = (tracks * hpc * spt) << 9;
						set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, hpc);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (size >> 20));
						recalc_selection(hdlg);
					}

					if (tracks > max_tracks)
					{
						tracks = max_tracks;
						size = (tracks * hpc * spt) << 9;
						set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, tracks);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (size >> 20));
						recalc_selection(hdlg);
					}

					no_update = 0;
					break;

				case IDC_COMBO_HD_BUS:
					if (no_update)
					{
						return FALSE;
					}

					no_update = 1;
					recalc_location_controls(hdlg, 1);
					h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
					b = SendMessage(h,CB_GETCURSEL,0,0) + 1;
					if (b == hdd_ptr->bus)
					{
						goto hd_add_bus_skip;
					}

					hdd_ptr->bus = b;

					switch(hdd_ptr->bus)
					{
						case HDD_BUS_DISABLED:
						default:
							max_spt = max_hpc = max_tracks = 0;
							break;
						case HDD_BUS_MFM:
							max_spt = 17;
							max_hpc = 15;
							max_tracks = 1023;
							break;
						case HDD_BUS_ESDI:
						case HDD_BUS_XTIDE:
							max_spt = 63;
							max_hpc = 16;
							max_tracks = 1023;
							break;
						case HDD_BUS_IDE_PIO_ONLY:
						case HDD_BUS_IDE_PIO_AND_DMA:
							max_spt = 63;
							max_hpc = 255;
							max_tracks = 266305;
							break;
						case HDD_BUS_SCSI_REMOVABLE:
							if (spt == 0)
							{
								spt = 63;
								size = (tracks * hpc * spt) << 9;
								set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, spt);
								set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (size >> 20));
							}
							if (hpc == 0)
							{
								hpc = 16;
								size = (tracks * hpc * spt) << 9;
								set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, hpc);
								set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (size >> 20));
							}
							if (tracks == 0)
							{
								tracks = 1023;
								size = (tracks * hpc * spt) << 9;
								set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, tracks);
								set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (size >> 20));
							}
						case HDD_BUS_SCSI:
							max_spt = 99;
							max_hpc = 255;
							max_tracks = 266305;
							break;
					}

					if ((hdd_ptr->bus == HDD_BUS_SCSI_REMOVABLE) && !chs_enabled)
					{
						h = GetDlgItem(hdlg, IDC_EDIT_HD_SPT);
						EnableWindow(h, TRUE);
						h = GetDlgItem(hdlg, IDC_EDIT_HD_HPC);
						EnableWindow(h, TRUE);
						h = GetDlgItem(hdlg, IDC_EDIT_HD_CYL);
						EnableWindow(h, TRUE);
						h = GetDlgItem(hdlg, IDC_EDIT_HD_SIZE);
						EnableWindow(h, TRUE);
						h = GetDlgItem(hdlg, IDC_COMBO_HD_TYPE);
						EnableWindow(h, TRUE);
					}
					else if ((hdd_ptr->bus != HDD_BUS_SCSI_REMOVABLE) && !chs_enabled)
					{
						h = GetDlgItem(hdlg, IDC_EDIT_HD_SPT);
						EnableWindow(h, FALSE);
						h = GetDlgItem(hdlg, IDC_EDIT_HD_HPC);
						EnableWindow(h, FALSE);
						h = GetDlgItem(hdlg, IDC_EDIT_HD_CYL);
						EnableWindow(h, FALSE);
						h = GetDlgItem(hdlg, IDC_EDIT_HD_SIZE);
						EnableWindow(h, FALSE);
						h = GetDlgItem(hdlg, IDC_COMBO_HD_TYPE);
						EnableWindow(h, FALSE);
					}

					if (spt > max_spt)
					{
						spt = max_spt;
						size = (tracks * hpc * spt) << 9;
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, spt);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (size >> 20));
						recalc_selection(hdlg);
					}

					if (hpc > max_hpc)
					{
						hpc = max_hpc;
						size = (tracks * hpc * spt) << 9;
						set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, hpc);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (size >> 20));
						recalc_selection(hdlg);
					}

					if (tracks > max_tracks)
					{
						tracks = max_tracks;
						size = (tracks * hpc * spt) << 9;
						set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, tracks);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (size >> 20));
						recalc_selection(hdlg);
					}

hd_add_bus_skip:
					no_update = 0;
					break;
			}

			return FALSE;
	}

	return FALSE;
}

int hard_disk_was_added(void)
{
	return hard_disk_added;
}

void hard_disk_add_open(HWND hwnd, int is_existing)
{
	existing = is_existing;
	hard_disk_added = 0;
        DialogBox(hinstance, (LPCWSTR)DLG_CFG_HARD_DISKS_ADD, hwnd, win_settings_hard_disks_add_proc);
}

int ignore_change = 0;

static BOOL CALLBACK win_settings_hard_disks_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND h;
	int old_sel = 0;
	int b = 0;

        switch (message)
        {
		case WM_INITDIALOG:
			ignore_change = 1;

			normalize_hd_list();	/* Normalize the hard disks so that non-disabled hard disks start from index 0, and so they are contiguous.
						   This will cause an emulator reset prompt on the first opening of this category with a messy hard disk list
						   (which can only happen by manually editing the configuration file). */
			h = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
			win_settings_hard_disks_init_columns(h);
			win_settings_hard_disks_image_list_init(h);
			win_settings_hard_disks_recalc_list(h);
			recalc_next_free_id(hdlg);
			add_locations(hdlg);
			if (hd_listview_items > 0)
			{
				ListView_SetItemState(h, 0, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
				hdlv_current_sel = 0;
				h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
				SendMessage(h, CB_SETCURSEL, temp_hdd[0].bus - 1, 0);
			}
			else
			{
				hdlv_current_sel = -1;
			}
			recalc_location_controls(hdlg, 0);
			
			ignore_change = 0;
			return TRUE;

		case WM_NOTIFY:
			if ((hd_listview_items == 0) || ignore_change)
			{
				return FALSE;
			}

			if ((((LPNMHDR)lParam)->code == LVN_ITEMCHANGED) && (((LPNMHDR)lParam)->idFrom == IDC_LIST_HARD_DISKS))
			{
				old_sel = hdlv_current_sel;
				hdlv_current_sel = get_selected_hard_disk(hdlg);
				if (hdlv_current_sel == old_sel)
				{
					return FALSE;
				}
				else if (hdlv_current_sel == -1)
				{
					ignore_change = 1;
					hdlv_current_sel = old_sel;
					ListView_SetItemState(h, hdlv_current_sel, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
					ignore_change = 0;
					return FALSE;
				}
				ignore_change = 1;
				h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
				SendMessage(h, CB_SETCURSEL, temp_hdd[hdlv_current_sel].bus - 1, 0);
				recalc_location_controls(hdlg, 0);
				ignore_change = 0;
			}
			break;

		case WM_COMMAND:
                	switch (LOWORD(wParam))
	                {
				case IDC_COMBO_HD_BUS:
					if (ignore_change)
					{
						return FALSE;
					}

					ignore_change = 1;
					h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
					b = SendMessage(h, CB_GETCURSEL, 0, 0) + 1;
					if (b == temp_hdd[hdlv_current_sel].bus)
					{
						goto hd_bus_skip;
					}
					temp_hdd[hdlv_current_sel].bus = b;
					recalc_location_controls(hdlg, 0);
					h = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
					win_settings_hard_disks_update_item(h, hdlv_current_sel, 0);
hd_bus_skip:
					ignore_change = 0;
					return FALSE;

				case IDC_COMBO_HD_CHANNEL:
					if (ignore_change)
					{
						return FALSE;
					}

					ignore_change = 1;
					h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
					if (temp_hdd[hdlv_current_sel].bus == HDD_BUS_MFM)
					{
						temp_hdd[hdlv_current_sel].mfm_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
					}
					else if (temp_hdd[hdlv_current_sel].bus == HDD_BUS_ESDI)
					{
						temp_hdd[hdlv_current_sel].esdi_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
					}
					else if (temp_hdd[hdlv_current_sel].bus == HDD_BUS_XTIDE)
					{
						temp_hdd[hdlv_current_sel].xtide_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
					}
					h = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
					win_settings_hard_disks_update_item(h, hdlv_current_sel, 0);
					ignore_change = 0;
					return FALSE;

				case IDC_COMBO_HD_CHANNEL_IDE:
					if (ignore_change)
					{
						return FALSE;
					}

					ignore_change = 1;
					h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL_IDE);
					temp_hdd[hdlv_current_sel].ide_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
					h = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
					win_settings_hard_disks_update_item(h, hdlv_current_sel, 0);
					ignore_change = 0;
					return FALSE;

				case IDC_COMBO_HD_ID:
					if (ignore_change)
					{
						return FALSE;
					}

					ignore_change = 1;
					h = GetDlgItem(hdlg, IDC_COMBO_HD_ID);
					temp_hdd[hdlv_current_sel].scsi_id = SendMessage(h, CB_GETCURSEL, 0, 0);
					h = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
					win_settings_hard_disks_update_item(h, hdlv_current_sel, 0);
					ignore_change = 0;
					return FALSE;

				case IDC_COMBO_HD_LUN:
					if (ignore_change)
					{
						return FALSE;
					}

					ignore_change = 1;
					h = GetDlgItem(hdlg, IDC_COMBO_HD_LUN);
					temp_hdd[hdlv_current_sel].scsi_lun = SendMessage(h, CB_GETCURSEL, 0, 0);
					h = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
					win_settings_hard_disks_update_item(h, hdlv_current_sel, 0);
					ignore_change = 0;
					return FALSE;

				case IDC_BUTTON_HDD_ADD:
					hard_disk_add_open(hdlg, 1);
					if (hard_disk_added)
					{
						ignore_change = 1;
						h = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
						win_settings_hard_disks_recalc_list(h);
						recalc_next_free_id(hdlg);
						ignore_change = 0;
					}
					return FALSE;

				case IDC_BUTTON_HDD_ADD_NEW:
					hard_disk_add_open(hdlg, 0);
					if (hard_disk_added)
					{
						ignore_change = 1;
						h = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
						win_settings_hard_disks_recalc_list(h);
						recalc_next_free_id(hdlg);
						ignore_change = 0;
					}
					return FALSE;

				case IDC_BUTTON_HDD_REMOVE:
					memcpy(temp_hdd[hdlv_current_sel].fn, L"", 4);
					temp_hdd[hdlv_current_sel].bus = HDD_BUS_DISABLED;	/* Only set the bus to zero, the list normalize code below will take care of turning this entire entry to a complete zero. */
					normalize_hd_list();			/* Normalize the hard disks so that non-disabled hard disks start from index 0, and so they are contiguous. */
					ignore_change = 1;
					h = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
					win_settings_hard_disks_recalc_list(h);
					recalc_next_free_id(hdlg);
					if (hd_listview_items > 0)
					{
						ListView_SetItemState(h, 0, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
						hdlv_current_sel = 0;
						h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
						SendMessage(h, CB_SETCURSEL, temp_hdd[0].bus - 1, 0);
					}
					else
					{
						hdlv_current_sel = -1;
					}
					recalc_location_controls(hdlg, 0);
					ignore_change = 0;
					return FALSE;
			}

		default:
			return FALSE;
	}

	return FALSE;
}

int fdlv_current_sel;
int cdlv_current_sel;

static int combo_id_to_string_id(int combo_id)
{
	return IDS_5376 + combo_id;
}

static int combo_id_to_format_string_id(int combo_id)
{
	return IDS_5632 + combo_id;
}

static BOOL win_settings_floppy_drives_image_list_init(HWND hwndList)
{
	HICON hiconItem;
	HIMAGELIST hSmall;

	int i = 0;

	hSmall = ImageList_Create(GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON),
                                  ILC_MASK | ILC_COLOR32, 1, 1);

	for (i = 0; i < 14; i++)
	{
		hiconItem = LoadIcon(hinstance, (LPCWSTR) fdd_type_to_icon(i));
		ImageList_AddIcon(hSmall, hiconItem);
		DestroyIcon(hiconItem);
	}

	ListView_SetImageList(hwndList, hSmall, LVSIL_SMALL);

	return TRUE;
}

static BOOL win_settings_cdrom_drives_image_list_init(HWND hwndList)
{
	HICON hiconItem;
	HIMAGELIST hSmall;

	hSmall = ImageList_Create(GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON),
                                  ILC_MASK | ILC_COLOR32, 1, 1);

	hiconItem = LoadIcon(hinstance, (LPCWSTR) 514);
	ImageList_AddIcon(hSmall, hiconItem);
	DestroyIcon(hiconItem);

	hiconItem = LoadIcon(hinstance, (LPCWSTR) 160);
	ImageList_AddIcon(hSmall, hiconItem);
	DestroyIcon(hiconItem);

	ListView_SetImageList(hwndList, hSmall, LVSIL_SMALL);

	return TRUE;
}

static BOOL win_settings_floppy_drives_recalc_list(HWND hwndList)
{
	LVITEM lvI;
	int i = 0;
	char s[256];
	WCHAR szText[256];

	lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
	lvI.stateMask = lvI.state = 0;

	for (i = 0; i < 4; i++)
	{
		lvI.iSubItem = 0;
		if (temp_fdd_types[i] > 0)
		{
			strcpy(s, fdd_getname(temp_fdd_types[i]));
			mbstowcs(szText, s, strlen(s) + 1);
			lvI.pszText = szText;
		}
		else
		{
			lvI.pszText = plat_get_string(IDS_5376);
		}
		lvI.iItem = i;
		lvI.iImage = temp_fdd_types[i];

		if (ListView_InsertItem(hwndList, &lvI) == -1)
			return FALSE;

		lvI.iSubItem = 1;
		lvI.pszText = plat_get_string(temp_fdd_turbo[i] ? IDS_2060 : IDS_2061);
		lvI.iItem = i;
		lvI.iImage = 0;

		if (ListView_SetItem(hwndList, &lvI) == -1)
		{
			return FALSE;
		}

		lvI.iSubItem = 2;
		lvI.pszText = plat_get_string(temp_fdd_check_bpb[i] ? IDS_2060 : IDS_2061);
		lvI.iItem = i;
		lvI.iImage = 0;

		if (ListView_SetItem(hwndList, &lvI) == -1)
		{
			return FALSE;
		}
	}

	return TRUE;
}

static BOOL win_settings_cdrom_drives_recalc_list(HWND hwndList)
{
	LVITEM lvI;
	int i = 0;
	WCHAR szText[256];
	int fsid = 0;

	lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
	lvI.stateMask = lvI.iSubItem = lvI.state = 0;

	for (i = 0; i < 4; i++)
	{
		fsid = combo_id_to_format_string_id(temp_cdrom_drives[i].bus_type);

		switch (temp_cdrom_drives[i].bus_type)
		{
			case CDROM_BUS_DISABLED:
			default:
				lvI.pszText = plat_get_string(fsid);
				lvI.iImage = 0;
				break;
			case CDROM_BUS_ATAPI_PIO_ONLY:
				wsprintf(szText, plat_get_string(fsid), temp_cdrom_drives[i].ide_channel >> 1, temp_cdrom_drives[i].ide_channel & 1);
				lvI.pszText = szText;
				lvI.iImage = 1;
				break;
			case CDROM_BUS_ATAPI_PIO_AND_DMA:
				wsprintf(szText, plat_get_string(fsid), temp_cdrom_drives[i].ide_channel >> 1, temp_cdrom_drives[i].ide_channel & 1);
				lvI.pszText = szText;
				lvI.iImage = 1;
				break;
			case CDROM_BUS_SCSI:
				wsprintf(szText, plat_get_string(fsid), temp_cdrom_drives[i].scsi_device_id, temp_cdrom_drives[i].scsi_device_lun);
				lvI.pszText = szText;
				lvI.iImage = 1;
				break;
		}

		lvI.iItem = i;

		if (ListView_InsertItem(hwndList, &lvI) == -1)
			return FALSE;
	}

	return TRUE;
}

static BOOL win_settings_floppy_drives_init_columns(HWND hwndList)
{
	LVCOLUMN lvc;

	lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;

	lvc.iSubItem = 0;
	lvc.pszText = plat_get_string(IDS_2143);

	lvc.cx = 292;
	lvc.fmt = LVCFMT_LEFT;

	if (ListView_InsertColumn(hwndList, 0, &lvc) == -1)
	{
		return FALSE;
	}

	lvc.iSubItem = 1;
	lvc.pszText = plat_get_string(IDS_2059);

	lvc.cx = 50;
	lvc.fmt = LVCFMT_LEFT;

	if (ListView_InsertColumn(hwndList, 1, &lvc) == -1)
	{
		return FALSE;
	}

	lvc.iSubItem = 2;
	lvc.pszText = plat_get_string(IDS_2170);

	lvc.cx = 75;
	lvc.fmt = LVCFMT_LEFT;

	if (ListView_InsertColumn(hwndList, 2, &lvc) == -1)
	{
		return FALSE;
	}
	return TRUE;
}

static BOOL win_settings_cdrom_drives_init_columns(HWND hwndList)
{
	LVCOLUMN lvc;

	lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;

	lvc.iSubItem = 0;
	lvc.pszText = plat_get_string(IDS_2082);

	lvc.cx = 392;
	lvc.fmt = LVCFMT_LEFT;

	if (ListView_InsertColumn(hwndList, 0, &lvc) == -1)
	{
		return FALSE;
	}

	return TRUE;
}

static int get_selected_floppy_drive(HWND hdlg)
{
	int floppy_drive = -1;
	int i, j = 0;
	HWND h;

	for (i = 0; i < 6; i++)
	{
		h = GetDlgItem(hdlg, IDC_LIST_FLOPPY_DRIVES);
		j = ListView_GetItemState(h, i, LVIS_SELECTED);
		if (j)
		{
			floppy_drive = i;
		}
	}

	return floppy_drive;
}

static int get_selected_cdrom_drive(HWND hdlg)
{
	int cd_drive = -1;
	int i, j = 0;
	HWND h;

	for (i = 0; i < 6; i++)
	{
		h = GetDlgItem(hdlg, IDC_LIST_CDROM_DRIVES);
		j = ListView_GetItemState(h, i, LVIS_SELECTED);
		if (j)
		{
			cd_drive = i;
		}
	}

	return cd_drive;
}

static void win_settings_floppy_drives_update_item(HWND hwndList, int i)
{
	LVITEM lvI;
	char s[256];
	WCHAR szText[256];

	lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
	lvI.stateMask = lvI.iSubItem = lvI.state = 0;

	lvI.iSubItem = 0;
	lvI.iItem = i;

	if (temp_fdd_types[i] > 0)
	{
		strcpy(s, fdd_getname(temp_fdd_types[i]));
		mbstowcs(szText, s, strlen(s) + 1);
		lvI.pszText = szText;
	}
	else
	{
		lvI.pszText = plat_get_string(IDS_5376);
	}
	lvI.iImage = temp_fdd_types[i];

	if (ListView_SetItem(hwndList, &lvI) == -1)
	{
		return;
	}

	lvI.iSubItem = 1;
	lvI.pszText = plat_get_string(temp_fdd_turbo[i] ? IDS_2060 : IDS_2061);
	lvI.iItem = i;
	lvI.iImage = 0;

	if (ListView_SetItem(hwndList, &lvI) == -1)
	{
		return;
	}

	lvI.iSubItem = 2;
	lvI.pszText = plat_get_string(temp_fdd_check_bpb[i] ? IDS_2060 : IDS_2061);
	lvI.iItem = i;
	lvI.iImage = 0;

	if (ListView_SetItem(hwndList, &lvI) == -1)
	{
		return;
	}
}

static void win_settings_cdrom_drives_update_item(HWND hwndList, int i)
{
	LVITEM lvI;
	WCHAR szText[256];
	int fsid;

	lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
	lvI.stateMask = lvI.iSubItem = lvI.state = 0;

	lvI.iSubItem = 0;
	lvI.iItem = i;

	fsid = combo_id_to_format_string_id(temp_cdrom_drives[i].bus_type);

	switch (temp_cdrom_drives[i].bus_type)
	{
		case CDROM_BUS_DISABLED:
		default:
			lvI.pszText = plat_get_string(fsid);
			lvI.iImage = 0;
			break;
		case CDROM_BUS_ATAPI_PIO_ONLY:
			wsprintf(szText, plat_get_string(fsid), temp_cdrom_drives[i].ide_channel >> 1, temp_cdrom_drives[i].ide_channel & 1);
			lvI.pszText = szText;
			lvI.iImage = 1;
			break;
		case CDROM_BUS_ATAPI_PIO_AND_DMA:
			wsprintf(szText, plat_get_string(fsid), temp_cdrom_drives[i].ide_channel >> 1, temp_cdrom_drives[i].ide_channel & 1);
			lvI.pszText = szText;
			lvI.iImage = 1;
			break;
		case CDROM_BUS_SCSI:
			wsprintf(szText, plat_get_string(fsid), temp_cdrom_drives[i].scsi_device_id, temp_cdrom_drives[i].scsi_device_lun);
			lvI.pszText = szText;
			lvI.iImage = 1;
			break;
	}

	if (ListView_SetItem(hwndList, &lvI) == -1)
	{
		return;
	}
}

static void cdrom_add_locations(HWND hdlg)
{
	LPTSTR lptsTemp;
	HWND h;
	int i = 0;

	lptsTemp = (LPTSTR) malloc(512);

	h = GetDlgItem(hdlg, IDC_COMBO_CD_BUS);
	for (i = CDROM_BUS_DISABLED; i <= CDROM_BUS_SCSI; i++)
	{
		if ((i == CDROM_BUS_DISABLED) || (i >= CDROM_BUS_ATAPI_PIO_ONLY))
		{
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM)plat_get_string(combo_id_to_string_id(i)));
		}
	}

	h = GetDlgItem(hdlg, IDC_COMBO_CD_ID);
	for (i = 0; i < 16; i++)
	{
		wsprintf(lptsTemp, plat_get_string(IDS_4098), i);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
	}

	h = GetDlgItem(hdlg, IDC_COMBO_CD_LUN);
	for (i = 0; i < 8; i++)
	{
		wsprintf(lptsTemp, plat_get_string(IDS_4098), i);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
	}

	h = GetDlgItem(hdlg, IDC_COMBO_CD_CHANNEL_IDE);
	for (i = 0; i < 8; i++)
	{
		wsprintf(lptsTemp, plat_get_string(IDS_4097), i >> 1, i & 1);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
	}

	free(lptsTemp);
}
static void cdrom_recalc_location_controls(HWND hdlg)
{
	int i = 0;
	HWND h;

	int bus = temp_cdrom_drives[cdlv_current_sel].bus_type;

	for (i = IDT_1741; i < (IDT_1743 + 1); i++)
	{
		h = GetDlgItem(hdlg, i);
		EnableWindow(h, FALSE);
		ShowWindow(h, SW_HIDE);
	}

	h = GetDlgItem(hdlg, IDC_COMBO_CD_ID);
	EnableWindow(h, FALSE);
	ShowWindow(h, SW_HIDE);

	h = GetDlgItem(hdlg, IDC_COMBO_CD_LUN);
	EnableWindow(h, FALSE);
	ShowWindow(h, SW_HIDE);

	h = GetDlgItem(hdlg, IDC_COMBO_CD_CHANNEL_IDE);
	EnableWindow(h, FALSE);
	ShowWindow(h, SW_HIDE);

	switch(bus)
	{
		case CDROM_BUS_ATAPI_PIO_ONLY:			/* ATAPI (PIO-only) */
		case CDROM_BUS_ATAPI_PIO_AND_DMA:		/* ATAPI (PIO and DMA) */
			h = GetDlgItem(hdlg, IDT_1743);
			ShowWindow(h, SW_SHOW);
			EnableWindow(h, TRUE);

			h = GetDlgItem(hdlg, IDC_COMBO_CD_CHANNEL_IDE);
			ShowWindow(h, SW_SHOW);
			EnableWindow(h, TRUE);
			SendMessage(h, CB_SETCURSEL, temp_cdrom_drives[cdlv_current_sel].ide_channel, 0);
			break;
		case CDROM_BUS_SCSI:		/* SCSI */
			h = GetDlgItem(hdlg, IDT_1741);
			ShowWindow(h, SW_SHOW);
			EnableWindow(h, TRUE);
			h = GetDlgItem(hdlg, IDT_1742);
			ShowWindow(h, SW_SHOW);
			EnableWindow(h, TRUE);

			h = GetDlgItem(hdlg, IDC_COMBO_CD_ID);
			ShowWindow(h, SW_SHOW);
			EnableWindow(h, TRUE);
			SendMessage(h, CB_SETCURSEL, temp_cdrom_drives[cdlv_current_sel].scsi_device_id, 0);

			h = GetDlgItem(hdlg, IDC_COMBO_CD_LUN);
			ShowWindow(h, SW_SHOW);
			EnableWindow(h, TRUE);
			SendMessage(h, CB_SETCURSEL, temp_cdrom_drives[cdlv_current_sel].scsi_device_lun, 0);
			break;
	}
}


int rd_ignore_change = 0;

static BOOL CALLBACK win_settings_removable_devices_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND h;
	int i = 0;
	int old_sel = 0;
	int b = 0;
	int b2 = 0;
	WCHAR szText[256];

        switch (message)
        {
		case WM_INITDIALOG:
			rd_ignore_change = 1;

			fdlv_current_sel = 0;
			h = GetDlgItem(hdlg, IDC_LIST_FLOPPY_DRIVES);
			win_settings_floppy_drives_init_columns(h);
			win_settings_floppy_drives_image_list_init(h);
			win_settings_floppy_drives_recalc_list(h);
			ListView_SetItemState(h, 0, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
			h = GetDlgItem(hdlg, IDC_COMBO_FD_TYPE);
			for (i = 0; i < 14; i++)
			{
				if (i == 0)
				{
					SendMessage(h, CB_ADDSTRING, 0, (LPARAM)plat_get_string(IDS_5376));
				}
				else
				{
					mbstowcs(szText, fdd_getname(i), strlen(fdd_getname(i)) + 1);
					SendMessage(h, CB_ADDSTRING, 0, (LPARAM) szText);
				}
			}
			SendMessage(h, CB_SETCURSEL, temp_fdd_types[fdlv_current_sel], 0);

			h = GetDlgItem(hdlg, IDC_CHECKTURBO);
			SendMessage(h, BM_SETCHECK, temp_fdd_turbo[fdlv_current_sel], 0);

			h = GetDlgItem(hdlg, IDC_CHECKBPB);
			SendMessage(h, BM_SETCHECK, temp_fdd_check_bpb[fdlv_current_sel], 0);

			cdlv_current_sel = 0;
			h = GetDlgItem(hdlg, IDC_LIST_CDROM_DRIVES);
			win_settings_cdrom_drives_init_columns(h);
			win_settings_cdrom_drives_image_list_init(h);
			win_settings_cdrom_drives_recalc_list(h);
			ListView_SetItemState(h, 0, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
			cdrom_add_locations(hdlg);

			h = GetDlgItem(hdlg, IDC_COMBO_CD_BUS);

			switch (temp_cdrom_drives[cdlv_current_sel].bus_type)
			{
				case CDROM_BUS_DISABLED:
				default:
					b = 0;
					break;
				case CDROM_BUS_ATAPI_PIO_ONLY:
					b = 1;
					break;
				case CDROM_BUS_ATAPI_PIO_AND_DMA:
					b = 2;
					break;
				case CDROM_BUS_SCSI:
					b = 3;
					break;
			}

			SendMessage(h, CB_SETCURSEL, b, 0);

			cdrom_recalc_location_controls(hdlg);

			rd_ignore_change = 0;
			return TRUE;

		case WM_NOTIFY:
			if (rd_ignore_change)
			{
				return FALSE;
			}

			if ((((LPNMHDR)lParam)->code == LVN_ITEMCHANGED) && (((LPNMHDR)lParam)->idFrom == IDC_LIST_FLOPPY_DRIVES))
			{
				old_sel = fdlv_current_sel;
				fdlv_current_sel = get_selected_floppy_drive(hdlg);
				if (fdlv_current_sel == old_sel)
				{
					return FALSE;
				}
				else if (fdlv_current_sel == -1)
				{
					rd_ignore_change = 1;
					fdlv_current_sel = old_sel;
					ListView_SetItemState(h, fdlv_current_sel, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
					rd_ignore_change = 0;
					return FALSE;
				}
				rd_ignore_change = 1;
				h = GetDlgItem(hdlg, IDC_COMBO_FD_TYPE);
				SendMessage(h, CB_SETCURSEL, temp_fdd_types[fdlv_current_sel], 0);
				h = GetDlgItem(hdlg, IDC_CHECKTURBO);
				SendMessage(h, BM_SETCHECK, temp_fdd_turbo[fdlv_current_sel], 0);
				h = GetDlgItem(hdlg, IDC_CHECKBPB);
				SendMessage(h, BM_SETCHECK, temp_fdd_check_bpb[fdlv_current_sel], 0);
				rd_ignore_change = 0;
			}
			else if ((((LPNMHDR)lParam)->code == LVN_ITEMCHANGED) && (((LPNMHDR)lParam)->idFrom == IDC_LIST_CDROM_DRIVES))
			{
				old_sel = cdlv_current_sel;
				cdlv_current_sel = get_selected_cdrom_drive(hdlg);
				if (cdlv_current_sel == old_sel)
				{
					return FALSE;
				}
				else if (cdlv_current_sel == -1)
				{
					rd_ignore_change = 1;
					cdlv_current_sel = old_sel;
					ListView_SetItemState(h, cdlv_current_sel, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
					rd_ignore_change = 0;
					return FALSE;
				}
				rd_ignore_change = 1;

				h = GetDlgItem(hdlg, IDC_COMBO_CD_BUS);

				switch (temp_cdrom_drives[cdlv_current_sel].bus_type)
				{
					case CDROM_BUS_DISABLED:
					default:
						b = 0;
						break;
					case CDROM_BUS_ATAPI_PIO_ONLY:
						b = 1;
						break;
					case CDROM_BUS_ATAPI_PIO_AND_DMA:
						b = 2;
						break;
					case CDROM_BUS_SCSI:
						b = 3;
						break;
				}

				SendMessage(h, CB_SETCURSEL, b, 0);

				cdrom_recalc_location_controls(hdlg);
				rd_ignore_change = 0;
			}
			break;

		case WM_COMMAND:
                	switch (LOWORD(wParam))
	                {
				case IDC_COMBO_FD_TYPE:
					if (rd_ignore_change)
					{
						return FALSE;
					}

					rd_ignore_change = 1;
					h = GetDlgItem(hdlg, IDC_COMBO_FD_TYPE);
					temp_fdd_types[fdlv_current_sel] = SendMessage(h, CB_GETCURSEL, 0, 0);
					h = GetDlgItem(hdlg, IDC_LIST_FLOPPY_DRIVES);
					win_settings_floppy_drives_update_item(h, fdlv_current_sel);
					rd_ignore_change = 0;
					return FALSE;

				case IDC_CHECKTURBO:
					if (rd_ignore_change)
					{
						return FALSE;
					}

					rd_ignore_change = 1;
					h = GetDlgItem(hdlg, IDC_CHECKTURBO);
					temp_fdd_turbo[fdlv_current_sel] = SendMessage(h, BM_GETCHECK, 0, 0);
					h = GetDlgItem(hdlg, IDC_LIST_FLOPPY_DRIVES);
					win_settings_floppy_drives_update_item(h, fdlv_current_sel);
					rd_ignore_change = 0;
					return FALSE;

				case IDC_CHECKBPB:
					if (rd_ignore_change)
					{
						return FALSE;
					}

					rd_ignore_change = 1;
					h = GetDlgItem(hdlg, IDC_CHECKBPB);
					temp_fdd_check_bpb[fdlv_current_sel] = SendMessage(h, BM_GETCHECK, 0, 0);
					h = GetDlgItem(hdlg, IDC_LIST_FLOPPY_DRIVES);
					win_settings_floppy_drives_update_item(h, fdlv_current_sel);
					rd_ignore_change = 0;
					return FALSE;

				case IDC_COMBO_CD_BUS:
					if (rd_ignore_change)
					{
						return FALSE;
					}

					rd_ignore_change = 1;
					h = GetDlgItem(hdlg, IDC_COMBO_CD_BUS);
					b = SendMessage(h, CB_GETCURSEL, 0, 0);
					switch (b)
					{
						case 0:
							b2 = CDROM_BUS_DISABLED;
							break;
						case 1:
							b2 = CDROM_BUS_ATAPI_PIO_ONLY;
							break;
						case 2:
							b2 = CDROM_BUS_ATAPI_PIO_AND_DMA;
							break;
						case 3:
							b2 = CDROM_BUS_SCSI;
							break;
					}
					if (b2 == temp_cdrom_drives[cdlv_current_sel].bus_type)
					{
						goto cdrom_bus_skip;
					}
					temp_cdrom_drives[cdlv_current_sel].bus_type = b2;
					cdrom_recalc_location_controls(hdlg);
					h = GetDlgItem(hdlg, IDC_LIST_CDROM_DRIVES);
					win_settings_cdrom_drives_update_item(h, cdlv_current_sel);
cdrom_bus_skip:
					rd_ignore_change = 0;
					return FALSE;

				case IDC_COMBO_CD_ID:
					if (rd_ignore_change)
					{
						return FALSE;
					}

					rd_ignore_change = 1;
					h = GetDlgItem(hdlg, IDC_COMBO_CD_ID);
					temp_cdrom_drives[cdlv_current_sel].scsi_device_id = SendMessage(h, CB_GETCURSEL, 0, 0);
					h = GetDlgItem(hdlg, IDC_LIST_CDROM_DRIVES);
					win_settings_cdrom_drives_update_item(h, cdlv_current_sel);
					rd_ignore_change = 0;
					return FALSE;

				case IDC_COMBO_CD_LUN:
					if (rd_ignore_change)
					{
						return FALSE;
					}

					rd_ignore_change = 1;
					h = GetDlgItem(hdlg, IDC_COMBO_CD_LUN);
					temp_cdrom_drives[cdlv_current_sel].scsi_device_lun = SendMessage(h, CB_GETCURSEL, 0, 0);
					h = GetDlgItem(hdlg, IDC_LIST_CDROM_DRIVES);
					win_settings_cdrom_drives_update_item(h, cdlv_current_sel);
					rd_ignore_change = 0;
					return FALSE;

				case IDC_COMBO_CD_CHANNEL_IDE:
					if (rd_ignore_change)
					{
						return FALSE;
					}

					rd_ignore_change = 1;
					h = GetDlgItem(hdlg, IDC_COMBO_CD_CHANNEL_IDE);
					temp_cdrom_drives[cdlv_current_sel].ide_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
					h = GetDlgItem(hdlg, IDC_LIST_CDROM_DRIVES);
					win_settings_cdrom_drives_update_item(h, cdlv_current_sel);
					rd_ignore_change = 0;
					return FALSE;
			}

		default:
			return FALSE;
	}

	return FALSE;
}

#define SETTINGS_PAGE_MACHINE	0
#define SETTINGS_PAGE_VIDEO	1
#define SETTINGS_PAGE_INPUT	2
#define SETTINGS_PAGE_SOUND	3
#define SETTINGS_PAGE_NETWORK	4
#define SETTINGS_PAGE_PORTS		5
#define SETTINGS_PAGE_PERIPHERALS	6
#define SETTINGS_PAGE_HARD_DISKS	7
#define SETTINGS_PAGE_REMOVABLE_DEVICES	8

void win_settings_show_child(HWND hwndParent, DWORD child_id)
{
	if (child_id == displayed_category)
	{
		return;
	}
	else
	{
		displayed_category = child_id;
	}

	SendMessage(hwndChildDialog, WM_SAVESETTINGS, 0, 0);

	DestroyWindow(hwndChildDialog);

	switch(child_id)
	{
		case SETTINGS_PAGE_MACHINE:
			hwndChildDialog = CreateDialog(hinstance, (LPCWSTR)DLG_CFG_MACHINE, hwndParent, win_settings_machine_proc);
			break;
		case SETTINGS_PAGE_VIDEO:
			hwndChildDialog = CreateDialog(hinstance, (LPCWSTR)DLG_CFG_VIDEO, hwndParent, win_settings_video_proc);
			break;
		case SETTINGS_PAGE_INPUT:
			hwndChildDialog = CreateDialog(hinstance, (LPCWSTR)DLG_CFG_INPUT, hwndParent, win_settings_input_proc);
			break;
		case SETTINGS_PAGE_SOUND:
			hwndChildDialog = CreateDialog(hinstance, (LPCWSTR)DLG_CFG_SOUND, hwndParent, win_settings_sound_proc);
			break;
		case SETTINGS_PAGE_NETWORK:
			hwndChildDialog = CreateDialog(hinstance, (LPCWSTR)DLG_CFG_NETWORK, hwndParent, win_settings_network_proc);
			break;
		case SETTINGS_PAGE_PORTS:
			hwndChildDialog = CreateDialog(hinstance, (LPCWSTR)DLG_CFG_PORTS, hwndParent, win_settings_ports_proc);
			break;
		case SETTINGS_PAGE_PERIPHERALS:
			hwndChildDialog = CreateDialog(hinstance, (LPCWSTR)DLG_CFG_PERIPHERALS, hwndParent, win_settings_peripherals_proc);
			break;
		case SETTINGS_PAGE_HARD_DISKS:
			hwndChildDialog = CreateDialog(hinstance, (LPCWSTR)DLG_CFG_HARD_DISKS, hwndParent, win_settings_hard_disks_proc);
			break;
		case SETTINGS_PAGE_REMOVABLE_DEVICES:
			hwndChildDialog = CreateDialog(hinstance, (LPCWSTR)DLG_CFG_REMOVABLE_DEVICES, hwndParent, win_settings_removable_devices_proc);
			break;
		default:
			fatal("Invalid child dialog ID\n");
			return;
	}

	ShowWindow(hwndChildDialog, SW_SHOWNORMAL);
}

static BOOL win_settings_main_image_list_init(HWND hwndList)
{
	HICON hiconItem;
	HIMAGELIST hSmall;

	int i = 0;

	hSmall = ImageList_Create(GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON),
                                  ILC_MASK | ILC_COLOR32, 1, 1);

	for (i = 0; i < 9; i++)
	{
		hiconItem = LoadIcon(hinstance, (LPCWSTR) (256 + i));
		ImageList_AddIcon(hSmall, hiconItem);
		DestroyIcon(hiconItem);
	}

	ListView_SetImageList(hwndList, hSmall, LVSIL_SMALL);

	return TRUE;
}

static BOOL win_settings_main_insert_categories(HWND hwndList)
{
	LVITEM lvI;
	int i = 0;

	lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
	lvI.stateMask = lvI.iSubItem = lvI.state = 0;

	for (i = 0; i < 9; i++)
	{
		lvI.pszText = plat_get_string(IDS_2065+i);
		lvI.iItem = i;
		lvI.iImage = i;

		if (ListView_InsertItem(hwndList, &lvI) == -1)
			return FALSE;
	}

	return TRUE;
}

static BOOL CALLBACK win_settings_main_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND h;
	int category;
	int i = 0;
	int j = 0;

	hwndParentDialog = hdlg;

        switch (message)
        {
		case WM_INITDIALOG:
			plat_pause(1);
			win_settings_init();
			displayed_category = -1;
			h = GetDlgItem(hdlg, IDC_SETTINGSCATLIST);
			win_settings_main_image_list_init(h);
			win_settings_main_insert_categories(h);
			ListView_SetItemState(h, 0, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
/* Leave this commented out until we do localization. */
#if 0
			h = GetDlgItem(hdlg, IDC_COMBO_LANG);	/* This is currently disabled, I am going to add localization options in the future. */
			EnableWindow(h, FALSE);
			ShowWindow(h, SW_HIDE);
			h = GetDlgItem(hdlg, IDS_LANG_ENUS);	/*was:2047 !*/
			EnableWindow(h, FALSE);
			ShowWindow(h, SW_HIDE);
#endif
			return TRUE;
		case WM_NOTIFY:
			if ((((LPNMHDR)lParam)->code == LVN_ITEMCHANGED) && (((LPNMHDR)lParam)->idFrom == IDC_SETTINGSCATLIST))
			{
				category = -1;
				for (i = 0; i < 9; i++)
				{
					h = GetDlgItem(hdlg, IDC_SETTINGSCATLIST);
					j = ListView_GetItemState(h, i, LVIS_SELECTED);
					if (j)
					{
						category = i;
						/* pclog("Category %i selected\n", i); */
					}
				}
				if (category != -1)
				{
					/* pclog("Showing child: %i\n", category); */
					win_settings_show_child(hdlg, category);
				}
			}
			break;
		case WM_COMMAND:
                	switch (LOWORD(wParam))
	                {
				case IDOK:
					/* pclog("Saving settings...\n"); */
					SendMessage(hwndChildDialog, WM_SAVESETTINGS, 0, 0);
					i = settings_msgbox_reset();
					if (i > 0)
					{
						if (i == 2)
						{
							win_settings_save();
						}

						/* pclog("Destroying window...\n"); */
						DestroyWindow(hwndChildDialog);
	                                        EndDialog(hdlg, 0);
        	                                plat_pause(0);
	                                        return TRUE;
					}
					else
					{
						return FALSE;
					}
				case IDCANCEL:
					DestroyWindow(hwndChildDialog);
                		        EndDialog(hdlg, 0);
        	                	plat_pause(0);
		                        return TRUE;
			}
			break;
		default:
			return FALSE;
	}

	return FALSE;
}

void win_settings_open(HWND hwnd)
{
        DialogBox(hinstance, (LPCWSTR)DLG_CONFIG, hwnd, win_settings_main_proc);
}
