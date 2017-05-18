/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#undef BITMAP

#include <commctrl.h>
#include <inttypes.h>
#include "ibm.h"
#include "mem.h"
#include "cpu/cpu.h"
#include "nvr.h"
#include "model.h"
#include "device.h"
#include "cdrom.h"
#include "disc.h"
#include "fdd.h"
#include "hdd.h"
#include "ide.h"
#include "scsi.h"
#include "scsi_buslogic.h"
#include "network.h"
#include "plat-midi.h"
#include "sound/sound.h"
#include "sound/snd_dbopl.h"
#include "video/video.h"
#include "video/vid_voodoo.h"
#include "gameport.h"
#include "mouse.h"
#include "win.h"
#include "win-language.h"
#include "resource.h"


#define WM_SAVESETTINGS 0x8888			/* 86Box-specific message, used to tell the child dialog to save the currently specified settings. */


/* Machine category */
int temp_model, temp_cpu_m, temp_cpu, temp_wait_states, temp_mem_size, temp_dynarec, temp_fpu, temp_sync;

/* Video category */
int temp_gfxcard, temp_video_speed, temp_voodoo;

/* Input devices category */
int temp_mouse, temp_joystick;

/* Sound category */
int temp_sound_card, temp_midi_id, temp_SSI2001, temp_GAMEBLASTER, temp_GUS, temp_opl3_type;

/* Network category */
int temp_net_type, temp_net_card;
char temp_pcap_dev[520];

/* Peripherals category */
int temp_scsi_card, hdc_ignore, temp_ide_ter, temp_ide_ter_irq, temp_ide_qua, temp_ide_qua_irq;
int temp_serial[2], temp_lpt, temp_bugger;

char temp_hdc_name[16];

/* Hard disks category */
hard_disk_t temp_hdc[HDC_NUM];
wchar_t temp_hdd_fn[HDC_NUM][512];

/* Removable devices category */
int temp_fdd_types[FDD_NUM];
cdrom_drive_t temp_cdrom_drives[CDROM_NUM];

static HWND hwndParentDialog, hwndChildDialog;

int hdd_controller_current;

int displayed_category = 0;

extern int is486;
static int romstolist[ROM_MAX], listtomodel[ROM_MAX], romstomodel[ROM_MAX], modeltolist[ROM_MAX];
static int settings_sound_to_list[20], settings_list_to_sound[20];
static int settings_mouse_to_list[20], settings_list_to_mouse[20];
static int settings_scsi_to_list[20], settings_list_to_scsi[20];
static int settings_network_to_list[20], settings_list_to_network[20];
static char *hdd_names[16];

/* This does the initial read of global variables into the temporary ones. */
static void win_settings_init()
{
	int i = 0;

	/* Machine category */
	temp_model = model;
	temp_cpu_m = cpu_manufacturer;
	temp_cpu = cpu;
	temp_mem_size = mem_size;
	temp_dynarec = cpu_use_dynarec;
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
	temp_midi_id = midi_id;
	temp_SSI2001 = SSI2001;
	temp_GAMEBLASTER = GAMEBLASTER;
	temp_GUS = GUS;
	temp_opl3_type = opl3_type;

	/* Network category */
	temp_net_type = network_type;
	memset(temp_pcap_dev, 0, 520);
	strcpy(temp_pcap_dev, pcap_dev);
	temp_net_card = network_card;

	/* Peripherals category */
	temp_scsi_card = scsi_card_current;
	strncpy(temp_hdc_name, hdd_controller_name, sizeof(temp_hdc_name) - 1);
	temp_ide_ter = ide_enable[2];
	temp_ide_ter_irq = ide_irq[2];
	temp_ide_qua = ide_enable[3];
	temp_ide_qua_irq = ide_irq[3];
	temp_serial[0] = serial_enabled[0];
	temp_serial[1] = serial_enabled[1];
	temp_lpt = lpt_enabled;
	temp_bugger = bugger_enabled;

	/* Hard disks category */
	memcpy(temp_hdc, hdc, HDC_NUM * sizeof(hard_disk_t));
	for (i = 0; i < HDC_NUM; i++)
	{
		memcpy(temp_hdd_fn[i], hdd_fn[i], 1024);
	}

	/* Removable devices category */
	for (i = 0; i < FDD_NUM; i++)
	{
		temp_fdd_types[i] = fdd_get_type(i);
	}
	memcpy(temp_cdrom_drives, cdrom_drives, CDROM_NUM * sizeof(cdrom_drive_t));
}

/* This returns 1 if any variable has changed, 0 if not. */
static int win_settings_changed()
{
	int i = 0;
	int j = 0;

	/* Machine category */
	i = i || (model != temp_model);
	i = i || (cpu_manufacturer != temp_cpu_m);
	i = i || (cpu != temp_cpu);
	i = i || (mem_size != temp_mem_size);
	i = i || (temp_dynarec != cpu_use_dynarec);
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
	i = i || (midi_id != temp_midi_id);
	i = i || (SSI2001 != temp_SSI2001);
	i = i || (GAMEBLASTER != temp_GAMEBLASTER);
	i = i || (GUS != temp_GUS);
	i = i || (opl3_type != temp_opl3_type);

	/* Network category */
	i = i || (network_type != temp_net_type);
	i = i || strcmp(temp_pcap_dev, pcap_dev);
	i = i || (network_card != temp_net_card);

	/* Peripherals category */
	i = i || (scsi_card_current != temp_scsi_card);
	i = i || strncmp(temp_hdc_name, hdd_controller_name, sizeof(temp_hdc_name) - 1);
	i = i || (temp_ide_ter != ide_enable[2]);
	i = i || (temp_ide_ter_irq != ide_irq[2]);
	i = i || (temp_ide_qua != ide_enable[3]);
	i = i || (temp_ide_qua_irq != ide_irq[3]);
	i = i || (temp_serial[0] != serial_enabled[0]);
	i = i || (temp_serial[1] != serial_enabled[1]);
	i = i || (temp_lpt != lpt_enabled);
	i = i || (temp_bugger != bugger_enabled);

	/* Hard disks category */
	i = i || memcmp(hdc, temp_hdc, HDC_NUM * sizeof(hard_disk_t));
	for (j = 0; j < HDC_NUM; j++)
	{
		i = i || memcmp(hdd_fn[j], temp_hdd_fn[j], 1024);
	}

	/* Removable devices category */
	for (j = 0; j < FDD_NUM; j++)
	{
		i = i || (temp_fdd_types[j] != fdd_get_type(j));
	}
	i = i || memcmp(cdrom_drives, temp_cdrom_drives, CDROM_NUM * sizeof(cdrom_drive_t));

	return i;
}

static int settings_msgbox_reset()
{
	int i = 0;
	int changed = 0;

	changed = win_settings_changed();

	if (changed)
	{
		i = msgbox_reset(hwndParentDialog);

		if (i == IDNO)
		{
			return 1;
		}
		else if (i == IDCANCEL)
		{
			return 0;
		}
		else
		{
			return 2;
		}
	}
	else
{
		return 1;
	}
}

/* This saves the settings back to the global variables. */
static void win_settings_save()
{
	int i = 0;

	/* Machine category */
	model = temp_model;
	romset = model_getromset();
	cpu_manufacturer = temp_cpu_m;
	cpu = temp_cpu;
	mem_size = temp_mem_size;
	cpu_use_dynarec = temp_dynarec;
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
	midi_id = temp_midi_id;
	SSI2001 = temp_SSI2001;
	GAMEBLASTER = temp_GAMEBLASTER;
	GUS = temp_GUS;
	opl3_type = temp_opl3_type;

	/* Network category */
	network_type = temp_net_type;
	memset(pcap_dev, 0, 512);
	strcpy(pcap_dev, temp_pcap_dev);
	network_card = temp_net_card;

	/* Peripherals category */
	scsi_card_current = temp_scsi_card;
	strncpy(hdd_controller_name, temp_hdc_name, sizeof(temp_hdc_name) - 1);
	ide_enable[2] = temp_ide_ter;
	ide_irq[2] = temp_ide_ter_irq;
	ide_enable[3] = temp_ide_qua;
	ide_irq[3] = temp_ide_qua_irq;
	serial_enabled[0] = temp_serial[0];
	serial_enabled[1] = temp_serial[1];
	lpt_enabled = temp_lpt;
	bugger_enabled = temp_bugger;

	/* Hard disks category */
	memcpy(hdc, temp_hdc, HDC_NUM * sizeof(hard_disk_t));
	for (i = 0; i < HDC_NUM; i++)
	{
		memcpy(hdd_fn[i], temp_hdd_fn[i], 1024);
	}

	/* Removable devices category */
	for (i = 0; i < FDD_NUM; i++)
	{
		fdd_set_type(i, temp_fdd_types[i]);
	}
	memcpy(cdrom_drives, temp_cdrom_drives, CDROM_NUM * sizeof(cdrom_drive_t));

	mem_resize();
	loadbios();

	resetpchard();

	cpu_set();

	cpu_update_waitstates();

	saveconfig();

	speedchanged();

	if (joystick_type != 7)  gameport_update_joystick_type();

	update_status_bar_panes(hwndStatus);
}

static void win_settings_machine_recalc_cpu(HWND hdlg)
{
	HWND h;
	int temp_romset = 0;
        int cpu_flags;
        int cpu_type;

	temp_romset = model_getromset_ex(temp_model);

	h = GetDlgItem(hdlg, IDC_COMBO_WS);
	cpu_type = models[romstomodel[temp_romset]].cpu[temp_cpu_m].cpus[temp_cpu].cpu_type;
	if ((cpu_type >= CPU_286) && (cpu_type <= CPU_386DX))
	{
		EnableWindow(h, TRUE);
	}
	else
	{
		EnableWindow(h, FALSE);
	}

	h=GetDlgItem(hdlg, IDC_CHECK_DYNAREC);
	cpu_flags = models[romstomodel[temp_romset]].cpu[temp_cpu_m].cpus[temp_cpu].cpu_flags;
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

	h = GetDlgItem(hdlg, IDC_CHECK_FPU);
	cpu_type = models[romstomodel[temp_romset]].cpu[temp_cpu_m].cpus[temp_cpu].cpu_type;
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

	temp_romset = model_getromset_ex(temp_model);
	lptsTemp = (LPTSTR) malloc(512);

	h = GetDlgItem(hdlg, IDC_COMBO_CPU);
	SendMessage(h, CB_RESETCONTENT, 0, 0);
	c = 0;
	while (models[romstomodel[temp_romset]].cpu[temp_cpu_m].cpus[c].cpu_type != -1)
	{
		stransi = models[romstomodel[temp_romset]].cpu[temp_cpu_m].cpus[c].name;
		mbstowcs(lptsTemp, stransi, strlen(stransi) + 1);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)lptsTemp);
		c++;
	}
	EnableWindow(h, TRUE);
	SendMessage(h, CB_SETCURSEL, temp_cpu, 0);

	win_settings_machine_recalc_cpu(hdlg);

	free(lptsTemp);
}

static void win_settings_machine_recalc_model(HWND hdlg)
{
	HWND h;
	int c = 0;
	int temp_romset = 0;
	LPTSTR lptsTemp;
	char *stransi;
        UDACCEL accel;

	temp_romset = model_getromset_ex(temp_model);
	lptsTemp = (LPTSTR) malloc(512);

	h = GetDlgItem(hdlg, IDC_CONFIGURE_MACHINE);
	if (model_getdevice(temp_model))
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
	while (models[romstomodel[temp_romset]].cpu[c].cpus != NULL && c < 4)
	{
		stransi = models[romstomodel[temp_romset]].cpu[c].name;
		mbstowcs(lptsTemp, stransi, strlen(stransi) + 1);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)lptsTemp);
		c++;
	}
	EnableWindow(h, TRUE);
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
	SendMessage(h, UDM_SETRANGE, 0, (models[romstomodel[temp_romset]].min_ram << 16) | models[romstomodel[temp_romset]].max_ram);
	accel.nSec = 0;
	accel.nInc = models[romstomodel[temp_romset]].ram_granularity;
	SendMessage(h, UDM_SETACCEL, 1, (LPARAM)&accel);
	if (!(models[romstomodel[temp_romset]].flags & MODEL_AT))
	{
		SendMessage(h, UDM_SETPOS, 0, temp_mem_size);
		h = GetDlgItem(hdlg, IDC_TEXT_MB);
		SendMessage(h, WM_SETTEXT, 0, (LPARAM) win_language_get_string_from_id(2094));
	}
	else
	{
		SendMessage(h, UDM_SETPOS, 0, temp_mem_size / 1024);
		h = GetDlgItem(hdlg, IDC_TEXT_MB);
		SendMessage(h, WM_SETTEXT, 0, (LPARAM) win_language_get_string_from_id(2087));
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
			while (models[c].id != -1)
			{
				if (romspresent[models[c].id])
				{
					stransi = models[c].name;
					mbstowcs(lptsTemp, stransi, strlen(stransi) + 1);
					SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
					modeltolist[c] = d;
					listtomodel[d] = c;
					romstolist[models[c].id] = d;
					romstomodel[models[c].id] = c;
					d++;
				}
				c++;
			}
			SendMessage(h, CB_SETCURSEL, modeltolist[temp_model], 0);

			h = GetDlgItem(hdlg, IDC_COMBO_WS);
	                SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(2131));

			for (c = 0; c < 8; c++)
			{
				wsprintf(lptsTemp, win_language_get_string_from_id(2132), c);
	        	        SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
			}

			SendMessage(h, CB_SETCURSEL, cpu_waitstates, 0);

        	        h=GetDlgItem(hdlg, IDC_CHECK_DYNAREC);
	                SendMessage(h, BM_SETCHECK, temp_dynarec, 0);

			h = GetDlgItem(hdlg, IDC_MEMSPIN);
			SendMessage(h, UDM_SETBUDDY, (WPARAM)GetDlgItem(hdlg, IDC_MEMTEXT), 0);

        	        h=GetDlgItem(hdlg, IDC_CHECK_SYNC);
	                SendMessage(h, BM_SETCHECK, temp_sync, 0);

			win_settings_machine_recalc_model(hdlg);

			free(lptsTemp);

			return TRUE;

		case WM_COMMAND:
                	switch (LOWORD(wParam))
	                {
                	        case IDC_COMBO_MACHINE:
	        	                if (HIWORD(wParam) == CBN_SELCHANGE)
		                        {
        		                        h = GetDlgItem(hdlg, IDC_COMBO_MACHINE);
	                	                temp_model = listtomodel[SendMessage(h,CB_GETCURSEL,0,0)];

						win_settings_machine_recalc_model(hdlg);
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
					temp_model = listtomodel[SendMessage(h, CB_GETCURSEL, 0, 0)];
                        
                        		deviceconfig_open(hdlg, (void *)model_getdevice(temp_model));
                        		break;
			}

			return FALSE;

		case WM_SAVESETTINGS:
			lptsTemp = (LPTSTR) malloc(512);
			stransi = (char *) malloc(512);

        	        h=GetDlgItem(hdlg, IDC_CHECK_DYNAREC);
			temp_dynarec = SendMessage(h, BM_GETCHECK, 0, 0);

        	        h=GetDlgItem(hdlg, IDC_CHECK_SYNC);
			temp_sync = SendMessage(h, BM_GETCHECK, 0, 0);

        	        h=GetDlgItem(hdlg, IDC_CHECK_FPU);
			temp_fpu = SendMessage(h, BM_GETCHECK, 0, 0);

                        h = GetDlgItem(hdlg, IDC_COMBO_WS);
                        temp_wait_states = SendMessage(h, CB_GETCURSEL, 0, 0);

			h = GetDlgItem(hdlg, IDC_MEMTEXT);
			SendMessage(h, WM_GETTEXT, 255, (LPARAM) lptsTemp);
			wcstombs(stransi, lptsTemp, (wcslen(lptsTemp) * 2) + 2);
			sscanf(stransi, "%i", &temp_mem_size);
			temp_mem_size &= ~(models[temp_model].ram_granularity - 1);
			if (temp_mem_size < models[temp_model].min_ram)
			{
				temp_mem_size = models[temp_model].min_ram;
			}
			else if (temp_mem_size > models[temp_model].max_ram)
			{
				temp_mem_size = models[temp_model].max_ram;
			}
			if (models[temp_model].flags & MODEL_AT)
			{
				temp_mem_size *= 1024;
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
                char *s = video_card_getname(c);

                if (!s[0])
                        break;

                if (video_card_available(c) && gfx_present[video_new_to_old(c)] &&
                    ((models[temp_model].flags & MODEL_PCI) || !(video_card_getdevice(c)->flags & DEVICE_PCI)))
                {
			mbstowcs(szText, s, strlen(s) + 1);
                        SendMessage(h, CB_ADDSTRING, 0, (LPARAM) szText);
                        if (video_new_to_old(c) == gfxcard)
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
        EnableWindow(h, models[temp_model].fixed_gfxcard ? FALSE : TRUE);

        h = GetDlgItem(hdlg, IDC_CHECK_VOODOO);
        EnableWindow(h, (models[model].flags & MODEL_PCI) ? TRUE : FALSE);

        h = GetDlgItem(hdlg, IDC_CONFIGURE_VOODOO);
        EnableWindow(h, ((models[model].flags & MODEL_PCI) && temp_voodoo) ? TRUE : FALSE);
}

static BOOL CALLBACK win_settings_video_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND h;
	int c = 0;
	int d = 0;
	LPTSTR lptsTemp;
	char *stransi;
	char *s;
	int gfx = 0;

        switch (message)
        {
		case WM_INITDIALOG:
			lptsTemp = (LPTSTR) malloc(512);
			stransi = (char *) malloc(512);

			recalc_vid_list(hdlg);

			h = GetDlgItem(hdlg, IDC_COMBO_VIDEO_SPEED);
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(2133));
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(2134));
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(2135));
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(2136));
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(2137));
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(2138));
			SendMessage(h, CB_SETCURSEL, temp_video_speed, 0);

	                h=GetDlgItem(hdlg, IDC_CHECK_VOODOO);
        	        SendMessage(h, BM_SETCHECK, temp_voodoo, 0);

                        h = GetDlgItem(hdlg, IDC_COMBO_VIDEO);
                        SendMessage(h, CB_GETLBTEXT, SendMessage(h, CB_GETCURSEL, 0, 0), (LPARAM) lptsTemp);
			wcstombs(stransi, lptsTemp, (wcslen(lptsTemp) * 2) + 2);
			gfx = video_card_getid(stransi);

			h = GetDlgItem(hdlg, IDC_CONFIGUREVID);
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
					wcstombs(stransi, lptsTemp, (wcslen(lptsTemp) * 2) + 2);
					gfx = video_card_getid(stransi);
		                        temp_gfxcard = video_new_to_old(gfx);

					h = GetDlgItem(hdlg, IDC_CONFIGUREVID);
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

	        		        h = GetDlgItem(hdlg, IDC_CONFIGURE_VOODOO);
					EnableWindow(h, temp_voodoo ? TRUE : FALSE);
					break;

				case IDC_CONFIGURE_VOODOO:
					deviceconfig_open(hdlg, (void *)&voodoo_device);
					break;

				case IDC_CONFIGUREVID:
					lptsTemp = (LPTSTR) malloc(512);
					stransi = (char *) malloc(512);

					h = GetDlgItem(hdlg, IDC_COMBO_VIDEO);
		                        SendMessage(h, CB_GETLBTEXT, SendMessage(h, CB_GETCURSEL, 0, 0), (LPARAM) lptsTemp);
					wcstombs(stransi, lptsTemp, (wcslen(lptsTemp) * 2) + 2);
					deviceconfig_open(hdlg, (void *)video_card_getdevice(video_card_getid(stransi)));

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
			wcstombs(stransi, lptsTemp, (wcslen(lptsTemp) * 2) + 2);
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


static int mouse_valid(int type, int model)
{
	type &= MOUSE_TYPE_MASK;

	if ((type == MOUSE_TYPE_PS2) &&
	    !(models[model].flags & MODEL_PS2)) return(0);

	if ((type == MOUSE_TYPE_AMSTRAD) &&
	    !(models[model].flags & MODEL_AMSTRAD)) return(0);

	if ((type == MOUSE_TYPE_OLIM24) &&
	    !(models[model].flags & MODEL_OLIM24)) return(0);

	return(1);
}


static BOOL CALLBACK win_settings_input_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND h;
	int c = 0;
	int d = 0;
	int type;
	int str_id = 0;

        switch (message)
        {
		case WM_INITDIALOG:
			h = GetDlgItem(hdlg, IDC_COMBO_MOUSE);
			c = d = 0;
			for (c = 0; c < mouse_get_ndev(); c++)
			{
				type = mouse_get_type(c);

				settings_mouse_to_list[c] = d;

				if (mouse_valid(type, temp_model))
				{
					switch(c)
					{
						case 0:	/* MS Serial */
						default:
							str_id = 2139;
							break;
						case 1:	/* PS2 2b */
							str_id = 2141;
							break;
						case 2:	/* PS2 intelli 3b */
							str_id = 2142;
							break;
						case 3: /* MS/logi bus 2b */
							str_id = 2143;
							break;
						case 4:	/* Amstrad */
							str_id = 2162;
							break;
						case 5:	/* Olivetti M24 */
							str_id = 2177;
							break;
						case 6:	/* MouseSystems */
							str_id = 2140;
							break;
						case 7:	/* Genius Bus */
							str_id = 2161;
							break;
					}

					SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(str_id));

					settings_list_to_mouse[d] = c;
					d++;
				}
			}

			SendMessage(h, CB_SETCURSEL, settings_mouse_to_list[temp_mouse], 0);

			h = GetDlgItem(hdlg, IDC_COMBO_JOYSTICK);
			c = 0;
			while (joystick_get_name(c))
			{
				SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(2144 + c));
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
					h = GetDlgItem(hdlg, IDC_COMBOJOY);
					temp_joystick = SendMessage(h, CB_GETCURSEL, 0, 0);
					joystickconfig_open(hdlg, 0, temp_joystick);
					break;

				case IDC_JOY2:
					h = GetDlgItem(hdlg, IDC_COMBOJOY);
					temp_joystick = SendMessage(h, CB_GETCURSEL, 0, 0);
					joystickconfig_open(hdlg, 1, temp_joystick);
					break;

				case IDC_JOY3:
					h = GetDlgItem(hdlg, IDC_COMBOJOY);
					temp_joystick = SendMessage(h, CB_GETCURSEL, 0, 0);
					joystickconfig_open(hdlg, 2, temp_joystick);
					break;

				case IDC_JOY4:
					h = GetDlgItem(hdlg, IDC_COMBOJOY);
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

static void recalc_hdd_list(HWND hdlg, int model, int use_selected_hdd)
{
	HWND h;

	char *s;
	int valid = 0;
	char old_name[16];
	int c, d;

	LPTSTR lptsTemp;

	lptsTemp = (LPTSTR) malloc(512);

	h = GetDlgItem(hdlg, IDC_COMBO_HDC);

	if (models[model].flags & MODEL_HAS_IDE)
	{
		hdc_ignore = 1;

		SendMessage(h, CB_RESETCONTENT, 0, 0);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(2154));
		EnableWindow(h, FALSE);
		SendMessage(h, CB_SETCURSEL, 0, 0);
	}
	else
	{
		hdc_ignore = 0;

		valid = 0;

		if (use_selected_hdd)
		{
			c = SendMessage(h, CB_GETCURSEL, 0, 0);

			if (c != -1 && hdd_names[c])
			{
				strncpy(old_name, hdd_names[c], sizeof(old_name) - 1);
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
			s = hdd_controller_get_name(c);
			if (s[0] == 0)
			{
				break;
			}
			if ((hdd_controller_get_flags(c) & DEVICE_AT) && !(models[model].flags & MODEL_AT))
			{
				c++;
				continue;
			}
			if ((hdd_controller_get_flags(c) & DEVICE_PS2) && !(models[model].flags & MODEL_PS2_HDD))
			{
				c++;
				continue;
			}
			if ((hdd_controller_get_flags(c) & DEVICE_MCA) && !(models[model].flags & MODEL_MCA))
			{
				c++;
				continue;
			}
			if (!hdd_controller_available(c))
			{
				c++;
				continue;
			}
			if (c < 2)
			{
				SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(2152 + c));
			}
			else
			{
				mbstowcs(lptsTemp, s, strlen(s) + 1);
				SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
			}
			hdd_names[d] = hdd_controller_get_internal_name(c);
			if (!strcmp(old_name, hdd_names[d]))
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

		EnableWindow(h, TRUE);
	}

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

static char midi_dev_name_buf[512];

static BOOL CALLBACK win_settings_sound_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND h;
	int c = 0;
	int d = 0;
	LPTSTR lptsTemp;
	device_t *sound_dev;
	int num = 0;

        switch (message)
        {
		case WM_INITDIALOG:
			lptsTemp = (LPTSTR) malloc(512);

			h = GetDlgItem(hdlg, IDC_COMBOSND);
			c = d = 0;
			while (1)
			{
				char *s = sound_card_getname(c);

				if (!s[0])
				{
					break;
				}

				settings_sound_to_list[c] = d;

				if (sound_card_available(c))
				{
					sound_dev = sound_card_getdevice(c);

					if (!sound_dev || (sound_dev->flags & DEVICE_MCA) == (models[temp_model].flags & MODEL_MCA))
					{
						if (c == 0)
						{
							SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(2152));
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

			h = GetDlgItem(hdlg, IDC_CONFIGURESND);
			if (sound_card_has_config(temp_sound_card))
			{
				EnableWindow(h, TRUE);
			}
			else
			{
				EnableWindow(h, FALSE);
			}

			h = GetDlgItem(hdlg, IDC_COMBO_MIDI);
			num  = midi_get_num_devs();
			for (c = 0; c < num; c++)
			{
				memset(midi_dev_name_buf, 0, 512);
				midi_get_dev_name(c, midi_dev_name_buf);
				mbstowcs(lptsTemp, midi_dev_name_buf, strlen(midi_dev_name_buf) + 1);
				SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
				if (c == temp_midi_id)
					SendMessage(h, CB_SETCURSEL, c, 0);
			}

			h=GetDlgItem(hdlg, IDC_CHECKCMS);
			SendMessage(h, BM_SETCHECK, temp_GAMEBLASTER, 0);

			h=GetDlgItem(hdlg, IDC_CHECKGUS);
			SendMessage(h, BM_SETCHECK, temp_GUS, 0);

			h=GetDlgItem(hdlg, IDC_CHECKSSI);
			SendMessage(h, BM_SETCHECK, temp_SSI2001, 0);

			h=GetDlgItem(hdlg, IDC_CHECKNUKEDOPL);
			SendMessage(h, BM_SETCHECK, temp_opl3_type, 0);

			free(lptsTemp);

			return TRUE;

		case WM_COMMAND:
                	switch (LOWORD(wParam))
	                {
				case IDC_CONFIGURESND:
					h = GetDlgItem(hdlg, IDC_COMBOSND);
					temp_sound_card = settings_list_to_sound[SendMessage(h, CB_GETCURSEL, 0, 0)];

					deviceconfig_open(hdlg, (void *)sound_card_getdevice(temp_sound_card));
					break;

				case IDC_COMBOSND:
					h = GetDlgItem(hdlg, IDC_COMBOSND);
					temp_sound_card = settings_list_to_sound[SendMessage(h, CB_GETCURSEL, 0, 0)];

					h = GetDlgItem(hdlg, IDC_CONFIGURESND);
					if (sound_card_has_config(temp_sound_card))
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
			h = GetDlgItem(hdlg, IDC_COMBOSND);
			temp_sound_card = settings_list_to_sound[SendMessage(h, CB_GETCURSEL, 0, 0)];

			h = GetDlgItem(hdlg, IDC_COMBO_MIDI);
			temp_midi_id = SendMessage(h, CB_GETCURSEL, 0, 0);

			h = GetDlgItem(hdlg, IDC_CHECKCMS);
			temp_GAMEBLASTER = SendMessage(h, BM_GETCHECK, 0, 0);

			h = GetDlgItem(hdlg, IDC_CHECKGUS);
			temp_GUS = SendMessage(h, BM_GETCHECK, 0, 0);

			h = GetDlgItem(hdlg, IDC_CHECKSSI);
			temp_SSI2001 = SendMessage(h, BM_GETCHECK, 0, 0);

			h = GetDlgItem(hdlg, IDC_CHECKNUKEDOPL);
			temp_opl3_type = SendMessage(h, BM_GETCHECK, 0, 0);

		default:
			return FALSE;
	}
	return FALSE;
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
					
					if (!scsi_dev || (scsi_dev->flags & DEVICE_MCA) == (models[temp_model].flags & MODEL_MCA))
					{
						if (c == 0)
						{
							SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(2152));
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

			h = GetDlgItem(hdlg, IDC_CONFIGURE_SCSI);
			if (scsi_card_has_config(temp_scsi_card))
			{
				EnableWindow(h, TRUE);
			}
			else
			{
				EnableWindow(h, FALSE);
			}

			recalc_hdd_list(hdlg, temp_model, 0);

			h=GetDlgItem(hdlg, IDC_COMBO_IDE_TER);
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(2151));

			for (c = 0; c < 11; c++)
			{
				wsprintf(lptsTemp, win_language_get_string_from_id(2155), valid_ide_irqs[c]);
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
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(2151));

			for (c = 0; c < 11; c++)
			{
				wsprintf(lptsTemp, win_language_get_string_from_id(2155), valid_ide_irqs[c]);
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

			h=GetDlgItem(hdlg, IDC_CHECKSERIAL1);
			SendMessage(h, BM_SETCHECK, temp_serial[0], 0);

			h=GetDlgItem(hdlg, IDC_CHECKSERIAL2);
			SendMessage(h, BM_SETCHECK, temp_serial[1], 0);

			h=GetDlgItem(hdlg, IDC_CHECKPARALLEL);
			SendMessage(h, BM_SETCHECK, temp_lpt, 0);

			h=GetDlgItem(hdlg, IDC_CHECKBUGGER);
			SendMessage(h, BM_SETCHECK, temp_bugger, 0);

			free(lptsTemp);

			return TRUE;

		case WM_COMMAND:
                	switch (LOWORD(wParam))
	                {
				case IDC_CONFIGURE_SCSI:
					h = GetDlgItem(hdlg, IDC_COMBO_SCSI);
					temp_scsi_card = settings_list_to_scsi[SendMessage(h, CB_GETCURSEL, 0, 0)];

					deviceconfig_open(hdlg, (void *)scsi_card_getdevice(temp_scsi_card));
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
			if (hdc_ignore == 0)
			{
				h = GetDlgItem(hdlg, IDC_COMBO_HDC);
				c = SendMessage(h, CB_GETCURSEL, 0, 0);
				if (hdd_names[c])
				{
					strncpy(temp_hdc_name, hdd_names[c], sizeof(temp_hdc_name) - 1);
				}
				else
				{
					strcpy(temp_hdc_name, "none");
				}
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

			h = GetDlgItem(hdlg, IDC_CHECKSERIAL1);
			temp_serial[0] = SendMessage(h, BM_GETCHECK, 0, 0);

			h = GetDlgItem(hdlg, IDC_CHECKSERIAL2);
			temp_serial[1] = SendMessage(h, BM_GETCHECK, 0, 0);

			h = GetDlgItem(hdlg, IDC_CHECKPARALLEL);
			temp_lpt = SendMessage(h, BM_GETCHECK, 0, 0);

			h = GetDlgItem(hdlg, IDC_CHECKBUGGER);
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

	h = GetDlgItem(hdlg, IDC_COMBOPCAP);
	if (temp_net_type == 1)
	{
		EnableWindow(h, TRUE);
	}
	else
	{
		EnableWindow(h, FALSE);
	}

	h = GetDlgItem(hdlg, IDC_COMBONET);
	if (temp_net_type == 0)
	{
		EnableWindow(h, TRUE);
	}
	else if ((temp_net_type == 1) && (network_dev_to_id(temp_pcap_dev) > 0))
	{
		EnableWindow(h, TRUE);
	}
	else
	{
		EnableWindow(h, FALSE);
	}

	h = GetDlgItem(hdlg, IDC_CONFIGURENET);
	if (network_card_has_config(temp_net_card) && (temp_net_type == 0))
	{
		EnableWindow(h, TRUE);
	}
	else if (network_card_has_config(temp_net_card) && (temp_net_type == 1) && (network_dev_to_id(temp_pcap_dev) > 0))
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
	device_t *scsi_dev;

        switch (message)
        {
		case WM_INITDIALOG:
			lptsTemp = (LPTSTR) malloc(512);

			h = GetDlgItem(hdlg, IDC_COMBONETTYPE);
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM) L"None");
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM) L"PCap");
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM) L"SLiRP");
			SendMessage(h, CB_SETCURSEL, temp_net_type + 1, 0);

			h = GetDlgItem(hdlg, IDC_COMBOPCAP);
			if (temp_net_type == 0)
			{
				EnableWindow(h, TRUE);
			}
			else
			{
				EnableWindow(h, FALSE);
			}

			h = GetDlgItem(hdlg, IDC_COMBOPCAP);
			for (c = 0; c < netdev_num; c++)
			{
				mbstowcs(lptsTemp, netdev_list[c].description, strlen(netdev_list[c].description) + 1);
				SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
			}
			SendMessage(h, CB_SETCURSEL, network_dev_to_id(temp_pcap_dev), 0);

			/*NIC config*/
			h = GetDlgItem(hdlg, IDC_COMBONET);
			c = d = 0;
			while (1)
			{
				char *s = network_card_getname(c);

				if (!s[0])
				{
					break;
				}

				settings_network_to_list[c] = d;

				if (network_card_available(c))
				{
					if (c == 0)
					{
						SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(2152));
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

			network_recalc_combos(hdlg);

			free(lptsTemp);

			return TRUE;

		case WM_COMMAND:
                	switch (LOWORD(wParam))
	                {
				case IDC_COMBONETTYPE:
					if (net_ignore_message)
					{
						return FALSE;
					}

					h = GetDlgItem(hdlg, IDC_COMBONETTYPE);
					temp_net_type = SendMessage(h, CB_GETCURSEL, 0, 0) - 1;

					network_recalc_combos(hdlg);
					break;

				case IDC_COMBOPCAP:
					if (net_ignore_message)
					{
						return FALSE;
					}

					h = GetDlgItem(hdlg, IDC_COMBOPCAP);
					memset(temp_pcap_dev, 0, 520);
					strcpy(temp_pcap_dev, netdev_list[SendMessage(h, CB_GETCURSEL, 0, 0)].device);

					network_recalc_combos(hdlg);
					break;

				case IDC_COMBONET:
					if (net_ignore_message)
					{
						return FALSE;
					}

					h = GetDlgItem(hdlg, IDC_COMBONET);
					temp_net_card = settings_list_to_network[SendMessage(h, CB_GETCURSEL, 0, 0)];

					network_recalc_combos(hdlg);
					break;

				case IDC_CONFIGURENET:
					if (net_ignore_message)
					{
						return FALSE;
					}

					h = GetDlgItem(hdlg, IDC_COMBONET);
					temp_net_card = settings_list_to_network[SendMessage(h, CB_GETCURSEL, 0, 0)];

					deviceconfig_open(hdlg, (void *)network_card_getdevice(temp_net_card));
					break;
			}
			return FALSE;

		case WM_SAVESETTINGS:
			h = GetDlgItem(hdlg, IDC_COMBONETTYPE);
			temp_net_type = SendMessage(h, CB_GETCURSEL, 0, 0) - 1;

			h = GetDlgItem(hdlg, IDC_COMBOPCAP);
			memset(temp_pcap_dev, 0, 520);
			strcpy(temp_pcap_dev, netdev_list[SendMessage(h, CB_GETCURSEL, 0, 0)].device);

			h = GetDlgItem(hdlg, IDC_COMBONET);
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

	int i = 0;

	hSmall = ImageList_Create(GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON),
                                  ILC_MASK | ILC_COLOR32, 1, 1);

	for (i = 0; i < 8; i += 2)
	{
		hiconItem = LoadIcon(hinstance, (LPCWSTR) (176 + i));
		ImageList_AddIcon(hSmall, hiconItem);
		DestroyIcon(hiconItem);
	}

	ListView_SetImageList(hwndList, hSmall, LVSIL_SMALL);

	return TRUE;
}

int next_free_id = 0;

wchar_t ifn[HDC_NUM][512];

static void normalize_hd_list()
{
	hard_disk_t ihdc[HDC_NUM];
	int i, j;

	j = 0;
	memset(ihdc, 0, HDC_NUM * sizeof(hard_disk_t));
	for (i = 0; i < HDC_NUM; i++)
	{
		memset(ifn[i], 0, 1024);
	}
	for (i = 0; i < HDC_NUM; i++)
	{
		if (temp_hdc[i].bus > 0)
		{
			memcpy(&(ihdc[j]), &(temp_hdc[i]), sizeof(hard_disk_t));
			memcpy(ifn[j], temp_hdd_fn[i], 1024);
			j++;
		}
	}

	memcpy(temp_hdc, ihdc, HDC_NUM * sizeof(hard_disk_t));
	for (i = 0; i < HDC_NUM; i++)
	{
		memcpy(temp_hdd_fn[i], ifn[i], 1024);
	}
}

int hdc_id_to_listview_index[HDC_NUM];
int hd_listview_items;

hard_disk_t new_hdc;
int hdlv_current_sel;

static int get_selected_hard_disk(HWND hdlg)
{
	int hard_disk = -1;
	int i, j = 0;
	HWND h;

	for (i = 0; i < 6; i++)
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
	for (i = 0; i < 4; i++)
	{
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(2165 + i));
	}

	h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
	for (i = 0; i < 8; i++)
	{
		wsprintf(lptsTemp, win_language_get_string_from_id(2169), i >> 1, i & 1);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
	}

	h = GetDlgItem(hdlg, IDC_COMBO_HD_ID);
	for (i = 0; i < 16; i++)
	{
		wsprintf(lptsTemp, win_language_get_string_from_id(2088), i);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
	}

	h = GetDlgItem(hdlg, IDC_COMBO_HD_LUN);
	for (i = 0; i < 8; i++)
	{
		wsprintf(lptsTemp, win_language_get_string_from_id(2088), i);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
	}

	h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL_IDE);
	for (i = 0; i < 8; i++)
	{
		wsprintf(lptsTemp, win_language_get_string_from_id(2169), i >> 1, i & 1);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
	}

	free(lptsTemp);
}

static void recalc_location_controls(HWND hdlg, int is_add_dlg)
{
	int i = 0;
	HWND h;

	int bus = 0;

	for (i = 1799; i < 1803; i++)
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

		switch(bus)
		{
			case 0:		/* MFM/RLL */
				h = GetDlgItem(hdlg, 1799);
				ShowWindow(h, SW_SHOW);
				EnableWindow(h, TRUE);

				h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
				ShowWindow(h, SW_SHOW);
				EnableWindow(h, TRUE);
				SendMessage(h, CB_SETCURSEL, is_add_dlg ? new_hdc.mfm_channel : temp_hdc[hdlv_current_sel].mfm_channel, 0);
				break;
			case 1:		/* IDE (PIO-only) */
			case 2:		/* IDE (PIO and DMA) */
				h = GetDlgItem(hdlg, 1802);
				ShowWindow(h, SW_SHOW);
				EnableWindow(h, TRUE);

				h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL_IDE);
				ShowWindow(h, SW_SHOW);
				EnableWindow(h, TRUE);
				SendMessage(h, CB_SETCURSEL, is_add_dlg ? new_hdc.ide_channel : temp_hdc[hdlv_current_sel].ide_channel, 0);
				break;
			case 3:		/* SCSI */
				h = GetDlgItem(hdlg, 1800);
				ShowWindow(h, SW_SHOW);
				EnableWindow(h, TRUE);
				h = GetDlgItem(hdlg, 1801);
				ShowWindow(h, SW_SHOW);
				EnableWindow(h, TRUE);

				h = GetDlgItem(hdlg, IDC_COMBO_HD_ID);
				ShowWindow(h, SW_SHOW);
				EnableWindow(h, TRUE);
				SendMessage(h, CB_SETCURSEL, is_add_dlg ? new_hdc.scsi_id : temp_hdc[hdlv_current_sel].scsi_id, 0);

				h = GetDlgItem(hdlg, IDC_COMBO_HD_LUN);
				ShowWindow(h, SW_SHOW);
				EnableWindow(h, TRUE);
				SendMessage(h, CB_SETCURSEL, is_add_dlg ? new_hdc.scsi_lun : temp_hdc[hdlv_current_sel].scsi_lun, 0);
				break;
		}
	}

	if ((hd_listview_items == 0) && !is_add_dlg)
	{
		h = GetDlgItem(hdlg, 1798);
		EnableWindow(h, FALSE);
		ShowWindow(h, SW_HIDE);

		h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
		EnableWindow(h, FALSE);		ShowWindow(h, SW_HIDE);
	}
	else
	{
		h = GetDlgItem(hdlg, 1798);
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
	int c_ide_pio = 0;
	int c_ide_dma = 0;
	int c_scsi = 0;
	int enable_add = 0;

	next_free_id = -1;

	for (i = 0; i < HDC_NUM; i++)
	{
		if (temp_hdc[i].bus == 1)
		{
			c_mfm++;
		}
		else if (temp_hdc[i].bus == 2)
		{
			c_ide_pio++;
		}
		else if (temp_hdc[i].bus == 3)
		{
			c_ide_dma++;
		}
		else if (temp_hdc[i].bus == 4)
		{
			c_scsi++;
		}
	}

	for (i = 0; i < HDC_NUM; i++)
	{
		if (temp_hdc[i].bus == 0)
		{
			next_free_id = i;
			break;
		}
	}

	/* pclog("Next free ID: %i\n", next_free_id); */

	enable_add = enable_add || (next_free_id >= 0);
	/* pclog("Enable add: %i\n", enable_add); */
	enable_add = enable_add && ((c_mfm < MFM_NUM) || (c_ide_pio < IDE_NUM) || (c_ide_dma < IDE_NUM) || (c_scsi < SCSI_NUM));
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

	if ((c_mfm == 0) && (c_ide_pio == 0) && (c_ide_dma == 0) && (c_scsi == 0))
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
		switch(temp_hdc[i].bus)
		{
			case 1:
				wsprintf(szText, win_language_get_string_from_id(2156), temp_hdc[i].mfm_channel >> 1, temp_hdc[i].mfm_channel & 1);
				break;
			case 2:
				wsprintf(szText, win_language_get_string_from_id(2195), temp_hdc[i].ide_channel >> 1, temp_hdc[i].ide_channel & 1);
				break;
			case 3:
				wsprintf(szText, win_language_get_string_from_id(2157), temp_hdc[i].ide_channel >> 1, temp_hdc[i].ide_channel & 1);
				break;
			case 4:
				wsprintf(szText, win_language_get_string_from_id(2158), temp_hdc[i].scsi_id, temp_hdc[i].scsi_lun);
				break;
		}
		lvI.pszText = szText;
		lvI.iImage = temp_hdc[i].bus - 1;
	}
	else if (column == 1)
	{
		lvI.pszText = temp_hdd_fn[i];
		lvI.iImage = 0;
	}
	else if (column == 2)
	{
		wsprintf(szText, win_language_get_string_from_id(2088), temp_hdc[i].tracks);
		lvI.pszText = szText;
		lvI.iImage = 0;
	}
	else if (column == 3)
	{
		wsprintf(szText, win_language_get_string_from_id(2088), temp_hdc[i].hpc);
		lvI.pszText = szText;
		lvI.iImage = 0;
	}
	else if (column == 4)
	{
		wsprintf(szText, win_language_get_string_from_id(2088), temp_hdc[i].spt);
		lvI.pszText = szText;
		lvI.iImage = 0;
	}
	else if (column == 5)
	{
		wsprintf(szText, win_language_get_string_from_id(2088), (temp_hdc[i].tracks * temp_hdc[i].hpc * temp_hdc[i].spt) >> 11);
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

	for (i = 0; i < HDC_NUM; i++)
	{
		if (temp_hdc[i].bus > 0)
		{
			hdc_id_to_listview_index[i] = j;
			lvI.iSubItem = 0;
			switch(temp_hdc[i].bus)
			{
				case 1:
					wsprintf(szText, win_language_get_string_from_id(2156), temp_hdc[i].mfm_channel >> 1, temp_hdc[i].mfm_channel & 1);
					break;
				case 2:
					wsprintf(szText, win_language_get_string_from_id(2195), temp_hdc[i].ide_channel >> 1, temp_hdc[i].ide_channel & 1);
					break;
				case 3:
					wsprintf(szText, win_language_get_string_from_id(2157), temp_hdc[i].ide_channel >> 1, temp_hdc[i].ide_channel & 1);
					break;
				case 4:
					wsprintf(szText, win_language_get_string_from_id(2158), temp_hdc[i].scsi_id, temp_hdc[i].scsi_lun);
					break;
			}
			lvI.pszText = szText;
			lvI.iItem = j;
			lvI.iImage = temp_hdc[i].bus - 1;

			if (ListView_InsertItem(hwndList, &lvI) == -1)
			{
				return FALSE;
			}

			lvI.iSubItem = 1;
			lvI.pszText = temp_hdd_fn[i];
			lvI.iItem = j;
			lvI.iImage = 0;

			if (ListView_SetItem(hwndList, &lvI) == -1)
			{
				return FALSE;
			}

			lvI.iSubItem = 2;
			wsprintf(szText, win_language_get_string_from_id(2088), temp_hdc[i].tracks);
			lvI.pszText = szText;
			lvI.iItem = j;
			lvI.iImage = 0;

			if (ListView_SetItem(hwndList, &lvI) == -1)
			{
				return FALSE;
			}

			lvI.iSubItem = 3;
			wsprintf(szText, win_language_get_string_from_id(2088), temp_hdc[i].hpc);
			lvI.pszText = szText;
			lvI.iItem = j;
			lvI.iImage = 0;

			if (ListView_SetItem(hwndList, &lvI) == -1)
			{
				return FALSE;
			}

			lvI.iSubItem = 4;
			wsprintf(szText, win_language_get_string_from_id(2088), temp_hdc[i].spt);
			lvI.pszText = szText;
			lvI.iItem = j;
			lvI.iImage = 0;

			if (ListView_SetItem(hwndList, &lvI) == -1)
			{
				return FALSE;
			}

			lvI.iSubItem = 5;
			wsprintf(szText, win_language_get_string_from_id(2088), (temp_hdc[i].tracks * temp_hdc[i].hpc * temp_hdc[i].spt) >> 11);
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
		lvc.pszText = win_language_get_string_from_id(2082 + iCol);

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
	wcstombs(stransi, szText, (wcslen(szText) * 2) + 2);
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
	wsprintf(szText, win_language_get_string_from_id(2160), val);
	SendMessage(h, WM_SETTEXT, (WPARAM) wcslen(szText), (LPARAM) szText);
}

int hard_disk_added = 0;
int max_spt = 63;

int no_update = 0;

int existing = 0;
uint64_t selection = 127;

uint64_t spt, hpc, tracks, size;
wchar_t hd_file_name[512];

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
		temp_size = hdt[i][0] * hdt[i][1] * hdt[i][2];
		size_mb = temp_size >> 11;
                wsprintf(szText, win_language_get_string_from_id(2171), size_mb, hdt[i][0], hdt[i][1], hdt[i][2]);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) szText);
		if ((tracks == hdt[i][0]) && (hpc == hdt[i][1]) && (spt == hdt[i][2]))
		{
			selection = i;
		}
	}
	SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(2170));
	SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(2187));
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
		if ((tracks == hdt[i][0]) && (hpc == hdt[i][1]) && (spt == hdt[i][2]))
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

static BOOL CALLBACK win_settings_hard_disks_add_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND h;
	int64_t i = 0;
	int bus;
	uint64_t temp;
	WCHAR szText[256];
	FILE *f;
	uint32_t sector_size = 512;
	uint32_t zero = 0;
	uint32_t base = 0x1000;
	uint64_t signature = 0xD778A82044445459ll;
	char buf[512];

        switch (message)
        {
		case WM_INITDIALOG:
			memset(hd_file_name, 0, 512);

			SetWindowText(hdlg, win_language_get_string_from_id(existing ? 2197 : 2196));

			no_update = 1;
			spt = existing ? 0 : 17;
			set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, spt);
			hpc = existing ? 0 : 15;
			set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, hpc);
			tracks = existing ? 0 : 1023;
			set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, tracks);
			size = (tracks * hpc * spt) << 9;
			set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, size >> 20);
			hdconf_initialize_hdt_combo(hdlg);
			if (existing)
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
			add_locations(hdlg);
			h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
			SendMessage(h, CB_SETCURSEL, 1, 0);
			recalc_location_controls(hdlg, 1);
			h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
			SendMessage(h, CB_SETCURSEL, 0, 0);
			h = GetDlgItem(hdlg, IDC_COMBO_HD_ID);
			SendMessage(h, CB_SETCURSEL, 0, 0);
			h = GetDlgItem(hdlg, IDC_COMBO_HD_LUN);
			SendMessage(h, CB_SETCURSEL, 0, 0);
			h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL_IDE);
			SendMessage(h, CB_SETCURSEL, 0, 0);
			h = GetDlgItem(hdlg, IDC_EDIT_HD_FILE_NAME);
			EnableWindow(h, FALSE);
			no_update = 0;
			return TRUE;

		case WM_COMMAND:
	               	switch (LOWORD(wParam))
        	        {
				case IDOK:
					if (wcslen(hd_file_name) == 0)
					{
						msgbox_error(hwndParentDialog, 2056);
						return TRUE;
					}

					get_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, &(temp_hdc[next_free_id].spt));
					get_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, &(temp_hdc[next_free_id].hpc));
					get_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, &(temp_hdc[next_free_id].tracks));
					h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
					temp_hdc[next_free_id].bus = SendMessage(h, CB_GETCURSEL, 0, 0) + 1;
					h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
					temp_hdc[next_free_id].mfm_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
					h = GetDlgItem(hdlg, IDC_COMBO_HD_ID);
					temp_hdc[next_free_id].scsi_id = SendMessage(h, CB_GETCURSEL, 0, 0);
					h = GetDlgItem(hdlg, IDC_COMBO_HD_LUN);
					temp_hdc[next_free_id].scsi_lun = SendMessage(h, CB_GETCURSEL, 0, 0);
					h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL_IDE);
					temp_hdc[next_free_id].ide_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
					memset(temp_hdd_fn[next_free_id], 0, 1024);
					memcpy(temp_hdd_fn[next_free_id], hd_file_name, (wcslen(hd_file_name) << 1) + 2);

					sector_size = 512;

					if (!existing && (wcslen(hd_file_name) > 0))
					{
						f = _wfopen(hd_file_name, L"wb");

						if (image_is_hdi(hd_file_name))
						{
							if (size >= 0x100000000ll)
							{
								fclose(f);
								msgbox_error(hwndParentDialog, 2058);
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
								msgbox_error(hwndParentDialog, 2163);
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
						for (i = 0; i < size; i++)
						{
							fwrite(buf, 512, 1, f);
						}

						fclose(f);
						msgbox_info(hwndParentDialog, 2059);	                        
					}

					hard_disk_added = 1;
					EndDialog(hdlg, 0);
					return TRUE;

				case IDCANCEL:
					hard_disk_added = 0;
					EndDialog(hdlg, 0);
					return TRUE;

				case IDC_CFILE:
		                        if (!file_dlg_w(hdlg, win_language_get_string_from_id(2172), L"", !existing))
       			                {
						if (!existing)
						{
							f = _wfopen(wopenfilestring, L"rb");
							if (f != NULL)
							{
								fclose(f);
								if (msgbox_question(ghwnd, 2178) != IDYES)
								{
									return FALSE;
								}
							}
						}

						f = _wfopen(wopenfilestring, existing ? L"rb" : L"wb");
						if (f == NULL)
						{
							msgbox_error(hwndParentDialog, existing ? 2060 : 2057);
							return TRUE;
						}
						if (existing)
						{
							if (image_is_hdi(wopenfilestring) || image_is_hdx(wopenfilestring, 1))
							{
								fseeko64(f, 0x10, SEEK_SET);
								fread(&sector_size, 1, 4, f);
								if (sector_size != 512)
								{
									msgbox_error(hwndParentDialog, 2061);
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
								if (((size % 17) == 0) && (size <= 133693440))
								{
									spt = 17;
									if (size <= 26738688)
									{
										hpc = 4;
									}
									else if (size <= 53477376)
									{
										hpc = 6;
									}
									else if (size <= 71303168)
									{
										hpc = 8;
									}
									else
									{
										hpc = 15;
									}
								}
								else
								{
									spt = 63;
									hpc = 16;
								}

								tracks = ((size >> 9) / hpc) / spt;
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

							no_update = 0;
						}
						else
						{
							fclose(f);
						}
					}

					h = GetDlgItem(hdlg, IDC_EDIT_HD_FILE_NAME);
					SendMessage(h, WM_SETTEXT, 0, (LPARAM) wopenfilestring);
					memcpy(hd_file_name, wopenfilestring, (wcslen(wopenfilestring) << 1) + 2);

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
						tracks = hdt[selection][0];
						hpc = hdt[selection][1];
						spt = hdt[selection][2];
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
					bus = SendMessage(h, CB_GETCURSEL, 0, 0);
					get_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, &spt);
					max_spt = (bus == 2) ? 99 : 63;
					if (spt > max_spt)
					{
						spt = max_spt;
						size = (tracks * hpc * spt) << 9;
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, 17);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (size >> 20));
						recalc_selection(hdlg);
					}
					no_update = 0;
					break;
			}

			return FALSE;
	}

	return FALSE;
}

void hard_disk_add_open(HWND hwnd, int is_existing)
{
	BOOL ret;

	existing = !!is_existing;
	hard_disk_added = 0;
        ret = DialogBox(hinstance, (LPCWSTR) CONFIGUREDLG_HARD_DISKS_ADD, hwnd, win_settings_hard_disks_add_proc);
}

int ignore_change = 0;

static BOOL CALLBACK win_settings_hard_disks_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND h;
	int old_sel = 0;

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
				SendMessage(h, CB_SETCURSEL, temp_hdc[0].bus - 1, 0);
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
				SendMessage(h, CB_SETCURSEL, temp_hdc[hdlv_current_sel].bus - 1, 0);
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
					recalc_location_controls(hdlg, 0);
					h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
					temp_hdc[hdlv_current_sel].bus = SendMessage(h, CB_GETCURSEL, 0, 0) + 1;
					h = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
					win_settings_hard_disks_update_item(h, hdlv_current_sel, 0);
					ignore_change = 0;
					return FALSE;

				case IDC_COMBO_HD_CHANNEL:
					if (ignore_change)
					{
						return FALSE;
					}

					ignore_change = 1;
					h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
					temp_hdc[hdlv_current_sel].mfm_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
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
					temp_hdc[hdlv_current_sel].ide_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
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
					temp_hdc[hdlv_current_sel].scsi_id = SendMessage(h, CB_GETCURSEL, 0, 0);
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
					temp_hdc[hdlv_current_sel].scsi_lun = SendMessage(h, CB_GETCURSEL, 0, 0);
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
					memcpy(temp_hdd_fn[hdlv_current_sel], L"", 4);
					temp_hdc[hdlv_current_sel].bus = 0;	/* Only set the bus to zero, the list normalize code below will take care of turning this entire entry to a complete zero. */
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
						SendMessage(h, CB_SETCURSEL, temp_hdc[0].bus - 1, 0);
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
	switch (combo_id)
	{
		case 0:	/* Disabled */
		default:
			return 2151;
			break;
		case 2:	/* Atapi (PIO-only) */
			return 2189;
			break;
		case 3:	/* Atapi (PIA and DMA) */
			return 2190;
			break;
		case 4: /* SCSI */
			return 2168;
			break;
	}
}

static int combo_id_to_format_string_id(int combo_id)
{
	switch (combo_id)
	{
		case 0:	/* Disabled */
		default:
			return 2151;
			break;
		case 2:	/* Atapi (PIO-only) */
			return 2191;
			break;
		case 3:	/* Atapi (PIA and DMA) */
			return 2192;
			break;
		case 4: /* SCSI */
			return 2158;
			break;
	}
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

	int i = 0;
	int j = 0;

	hSmall = ImageList_Create(GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON),
                                  ILC_MASK | ILC_COLOR32, 1, 1);

	hiconItem = LoadIcon(hinstance, (LPCWSTR) 514);
	ImageList_AddIcon(hSmall, hiconItem);
	DestroyIcon(hiconItem);

	hiconItem = LoadIcon(hinstance, (LPCWSTR) 160);
	ImageList_AddIcon(hSmall, hiconItem);
	DestroyIcon(hiconItem);

	hiconItem = LoadIcon(hinstance, (LPCWSTR) 162);
	ImageList_AddIcon(hSmall, hiconItem);
	DestroyIcon(hiconItem);

	hiconItem = LoadIcon(hinstance, (LPCWSTR) 164);
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
	lvI.stateMask = lvI.iSubItem = lvI.state = 0;

	for (i = 0; i < 4; i++)
	{
		if (temp_fdd_types[i] > 0)
		{
			strcpy(s, fdd_getname(temp_fdd_types[i]));
			mbstowcs(szText, s, strlen(s) + 1);
			lvI.pszText = szText;
		}
		else
		{
			lvI.pszText = win_language_get_string_from_id(2151);
		}
		lvI.iItem = i;
		lvI.iImage = temp_fdd_types[i];

		if (ListView_InsertItem(hwndList, &lvI) == -1)
			return FALSE;
	}

	return TRUE;
}

static BOOL win_settings_cdrom_drives_recalc_list(HWND hwndList)
{
	LVITEM lvI;
	int i = 0;
	char s[256];
	WCHAR szText[256];
	int bid = 0;
	int fsid = 0;

	lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
	lvI.stateMask = lvI.iSubItem = lvI.state = 0;

	for (i = 0; i < 4; i++)
	{
		fsid = combo_id_to_format_string_id(temp_cdrom_drives[i].bus_type);

		switch (temp_cdrom_drives[i].bus_type)
		{
			case 0:
			default:
				lvI.pszText = win_language_get_string_from_id(fsid);
				break;
			case 2:
			case 3:
				wsprintf(szText, win_language_get_string_from_id(fsid), temp_cdrom_drives[i].ide_channel >> 1, temp_cdrom_drives[i].ide_channel & 1);
				lvI.pszText = szText;
				break;
			case 4:
				wsprintf(szText, win_language_get_string_from_id(fsid), temp_cdrom_drives[i].scsi_device_id, temp_cdrom_drives[i].scsi_device_lun);
				lvI.pszText = szText;
				break;
		}

		lvI.iItem = i;

		if (temp_cdrom_drives[i].bus_type)
		{
			lvI.iImage = temp_cdrom_drives[i].bus_type - 1;
		}
		else
		{
			lvI.iImage = 0;
		}

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
	lvc.pszText = win_language_get_string_from_id(2188);

	lvc.cx = 392;
	lvc.fmt = LVCFMT_LEFT;

	if (ListView_InsertColumn(hwndList, 0, &lvc) == -1)
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
	lvc.pszText = win_language_get_string_from_id(2082);

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
		lvI.pszText = win_language_get_string_from_id(2151);
	}
	lvI.iImage = temp_fdd_types[i];

	if (ListView_SetItem(hwndList, &lvI) == -1)
	{
		return;
	}
}

static void win_settings_cdrom_drives_update_item(HWND hwndList, int i)
{
	LVITEM lvI;
	char s[256];
	WCHAR szText[256];
	int bid;
	int fsid;

	lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
	lvI.stateMask = lvI.iSubItem = lvI.state = 0;

	lvI.iSubItem = 0;
	lvI.iItem = i;

	fsid = combo_id_to_format_string_id(temp_cdrom_drives[i].bus_type);

	switch (temp_cdrom_drives[i].bus_type)
	{
		case 0:
		default:
			lvI.pszText = win_language_get_string_from_id(fsid);
			break;
		case 2:
		case 3:
			wsprintf(szText, win_language_get_string_from_id(fsid), temp_cdrom_drives[i].ide_channel >> 1, temp_cdrom_drives[i].ide_channel & 1);
			lvI.pszText = szText;
			break;
		case 4:
			wsprintf(szText, win_language_get_string_from_id(fsid), temp_cdrom_drives[i].scsi_device_id, temp_cdrom_drives[i].scsi_device_lun);
			lvI.pszText = szText;
			break;
	}

	if (temp_cdrom_drives[i].bus_type)
	{
		lvI.iImage = temp_cdrom_drives[i].bus_type - 1;
	}
	else
	{
		lvI.iImage = 0;
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
	for (i = 1; i < 5; i++)
	{
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(combo_id_to_string_id(i)));
	}

	h = GetDlgItem(hdlg, IDC_COMBO_CD_ID);
	for (i = 0; i < 16; i++)
	{
		wsprintf(lptsTemp, win_language_get_string_from_id(2088), i);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
	}

	h = GetDlgItem(hdlg, IDC_COMBO_CD_LUN);
	for (i = 0; i < 8; i++)
	{
		wsprintf(lptsTemp, win_language_get_string_from_id(2088), i);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
	}

	h = GetDlgItem(hdlg, IDC_COMBO_CD_CHANNEL_IDE);
	for (i = 0; i < 8; i++)
	{
		wsprintf(lptsTemp, win_language_get_string_from_id(2169), i >> 1, i & 1);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
	}

	free(lptsTemp);
}
static void cdrom_recalc_location_controls(HWND hdlg)
{
	int i = 0;
	HWND h;

	int bus = temp_cdrom_drives[cdlv_current_sel].bus_type;

	for (i = 1800; i < 1803; i++)
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
		case 2:		/* ATAPI (PIO-only) */
		case 3:		/* ATAPI (PIO and DMA) */
			h = GetDlgItem(hdlg, 1802);
			ShowWindow(h, SW_SHOW);
			EnableWindow(h, TRUE);

			h = GetDlgItem(hdlg, IDC_COMBO_CD_CHANNEL_IDE);
			ShowWindow(h, SW_SHOW);
			EnableWindow(h, TRUE);
			SendMessage(h, CB_SETCURSEL, temp_cdrom_drives[cdlv_current_sel].ide_channel, 0);
			break;
		case 4:		/* SCSI */
			h = GetDlgItem(hdlg, 1800);
			ShowWindow(h, SW_SHOW);
			EnableWindow(h, TRUE);
			h = GetDlgItem(hdlg, 1801);
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
	int cid = 0;
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
					SendMessage(h, CB_ADDSTRING, 0, (LPARAM) win_language_get_string_from_id(2151));
				}
				else
				{
					mbstowcs(szText, fdd_getname(i), strlen(fdd_getname(i)) + 1);
					SendMessage(h, CB_ADDSTRING, 0, (LPARAM) szText);
				}
			}
			SendMessage(h, CB_SETCURSEL, temp_fdd_types[fdlv_current_sel], 0);

			cdlv_current_sel = 0;
			h = GetDlgItem(hdlg, IDC_LIST_CDROM_DRIVES);
			win_settings_cdrom_drives_init_columns(h);
			win_settings_cdrom_drives_image_list_init(h);
			win_settings_cdrom_drives_recalc_list(h);
			ListView_SetItemState(h, 0, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
			cdrom_add_locations(hdlg);
			h = GetDlgItem(hdlg, IDC_COMBO_CD_BUS);
			if (temp_cdrom_drives[cdlv_current_sel].bus_type > 1)
			{
				SendMessage(h, CB_SETCURSEL, temp_cdrom_drives[cdlv_current_sel].bus_type, 0);
			}
			else
			{
				SendMessage(h, CB_SETCURSEL, 0, 0);
			}
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
				if (temp_cdrom_drives[cdlv_current_sel].bus_type > 1)
				{
					SendMessage(h, CB_SETCURSEL, temp_cdrom_drives[cdlv_current_sel].bus_type, 0);
				}
				else
				{
					SendMessage(h, CB_SETCURSEL, 0, 0);
				}
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

				case IDC_COMBO_CD_BUS:
					if (rd_ignore_change)
					{
						return FALSE;
					}

					rd_ignore_change = 1;
					h = GetDlgItem(hdlg, IDC_COMBO_CD_BUS);
					temp_cdrom_drives[cdlv_current_sel].bus_type = SendMessage(h, CB_GETCURSEL, 0, 0) + 1;
					if (temp_cdrom_drives[cdlv_current_sel].bus_type == 1)
					{
						temp_cdrom_drives[cdlv_current_sel].bus_type = 0;
					}
					cdrom_recalc_location_controls(hdlg);
					h = GetDlgItem(hdlg, IDC_LIST_CDROM_DRIVES);
					win_settings_cdrom_drives_update_item(h, cdlv_current_sel);
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
		case 0:
			hwndChildDialog = CreateDialog(hinstance, (LPCWSTR) CONFIGUREDLG_MACHINE, hwndParent, win_settings_machine_proc);
			break;
		case 1:
			hwndChildDialog = CreateDialog(hinstance, (LPCWSTR) CONFIGUREDLG_VIDEO, hwndParent, win_settings_video_proc);
			break;
		case 2:
			hwndChildDialog = CreateDialog(hinstance, (LPCWSTR) CONFIGUREDLG_INPUT, hwndParent, win_settings_input_proc);
			break;
		case 3:
			hwndChildDialog = CreateDialog(hinstance, (LPCWSTR) CONFIGUREDLG_SOUND, hwndParent, win_settings_sound_proc);
			break;
		case 4:
			hwndChildDialog = CreateDialog(hinstance, (LPCWSTR) CONFIGUREDLG_NETWORK, hwndParent, win_settings_network_proc);
			break;
		case 5:
			hwndChildDialog = CreateDialog(hinstance, (LPCWSTR) CONFIGUREDLG_PERIPHERALS, hwndParent, win_settings_peripherals_proc);
			break;
		case 6:
			hwndChildDialog = CreateDialog(hinstance, (LPCWSTR) CONFIGUREDLG_HARD_DISKS, hwndParent, win_settings_hard_disks_proc);
			break;
		case 7:
			hwndChildDialog = CreateDialog(hinstance, (LPCWSTR) CONFIGUREDLG_REMOVABLE_DEVICES, hwndParent, win_settings_removable_devices_proc);
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

	for (i = 0; i < 8; i++)
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

	for (i = 0; i < 8; i++)
	{
		lvI.pszText = win_language_get_settings_category(i);
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
			pause = 1;
			win_settings_init();
			displayed_category = -1;
			h = GetDlgItem(hdlg, IDC_SETTINGSCATLIST);
			win_settings_main_image_list_init(h);
			win_settings_main_insert_categories(h);
			ListView_SetItemState(h, 0, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
			h = GetDlgItem(hdlg, IDC_COMBO_LANG);	/* This is currently disabled, I am going to add localization options in the future. */
			EnableWindow(h, FALSE);
			ShowWindow(h, SW_HIDE);
			h = GetDlgItem(hdlg, 2047);
			EnableWindow(h, FALSE);
			ShowWindow(h, SW_HIDE);
			return TRUE;
		case WM_NOTIFY:
			if ((((LPNMHDR)lParam)->code == LVN_ITEMCHANGED) && (((LPNMHDR)lParam)->idFrom == IDC_SETTINGSCATLIST))
			{
				category = -1;
				for (i = 0; i < 8; i++)
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
        	                                pause = 0;
	                                        return TRUE;
					}
					else
					{
						return FALSE;
					}
				case IDCANCEL:
					DestroyWindow(hwndChildDialog);
                		        EndDialog(hdlg, 0);
        	                	pause=0;
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
        DialogBox(hinstance, (LPCWSTR) CONFIGUREDLG_MAIN, hwnd, win_settings_main_proc);
}
