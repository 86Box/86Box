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
 * Version:	@(#)win_settings.c	1.0.51	2018/05/25
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 */
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#undef BITMAP
#include <commctrl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
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
#include "../scsi/scsi.h"
#include "../cdrom/cdrom.h"
#include "../disk/hdd.h"
#include "../disk/hdc.h"
#include "../disk/hdc_ide.h"
#include "../disk/zip.h"
#include "../floppy/fdd.h"
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


#define SETTINGS_PAGE_MACHINE			0
#define SETTINGS_PAGE_VIDEO			1
#define SETTINGS_PAGE_INPUT			2
#define SETTINGS_PAGE_SOUND			3
#define SETTINGS_PAGE_NETWORK			4
#define SETTINGS_PAGE_PORTS			5
#define SETTINGS_PAGE_PERIPHERALS		6
#define SETTINGS_PAGE_HARD_DISKS		7
#define SETTINGS_PAGE_FLOPPY_DRIVES		8
#define SETTINGS_PAGE_OTHER_REMOVABLE_DEVICES	9

/* Icon, Bus, File, C, H, S, Size */
#define C_COLUMNS_HARD_DISKS			6


/* Machine category */
static int temp_machine, temp_cpu_m, temp_cpu, temp_wait_states, temp_fpu, temp_sync;
static uint32_t temp_mem_size;
#ifdef USE_DYNAREC
static int temp_dynarec;
#endif

/* Video category */
static int temp_gfxcard, temp_voodoo;

/* Input devices category */
static int temp_mouse, temp_joystick;

/* Sound category */
static int temp_sound_card, temp_midi_device, temp_mpu401, temp_SSI2001, temp_GAMEBLASTER, temp_GUS, temp_opl_type;
static int temp_float;

/* Network category */
static int temp_net_type, temp_net_card;
static char temp_pcap_dev[520];

/* Ports category */
static char temp_lpt_device_names[3][16];
static int temp_serial[2], temp_lpt;

/* Other peripherals category */
static int temp_scsi_card, temp_ide_ter, temp_ide_qua;
static char temp_hdc_name[32];
static char *hdc_names[32];
static int temp_bugger;

static uint8_t temp_deviceconfig;

/* Hard disks category */
static hard_disk_t temp_hdd[HDD_NUM];

/* Floppy drives category */
static int temp_fdd_types[FDD_NUM];
static int temp_fdd_turbo[FDD_NUM];
static int temp_fdd_check_bpb[FDD_NUM];

/* Other removable devices category */
static cdrom_drive_t temp_cdrom_drives[CDROM_NUM];
static zip_drive_t temp_zip_drives[ZIP_NUM];

static HWND hwndParentDialog, hwndChildDialog;

static uint32_t displayed_category = 0;

extern int is486;
static int romstolist[ROM_MAX], listtomachine[ROM_MAX], romstomachine[ROM_MAX], machinetolist[ROM_MAX];
static int settings_device_to_list[20], settings_list_to_device[20];
static int settings_midi_to_list[20], settings_list_to_midi[20];

static int max_spt = 63, max_hpc = 255, max_tracks = 266305;
static uint64_t mfm_tracking, esdi_tracking, xta_tracking, ide_tracking, scsi_tracking[2];
static uint64_t size;
static int hd_listview_items, hdc_id_to_listview_index[HDD_NUM];
static int no_update = 0, existing = 0, chs_enabled = 0;
static int lv1_current_sel, lv2_current_sel;
static int hard_disk_added = 0, next_free_id = 0, selection = 127;
static int spt, hpc, tracks, ignore_change = 0;

static hard_disk_t new_hdd, *hdd_ptr;

static wchar_t hd_file_name[512];


static BOOL
image_list_init(HWND hwndList, const uint8_t *icon_ids)
{
    HICON hiconItem;
    HIMAGELIST hSmall;

    int i = 0;

    hSmall = ImageList_Create(GetSystemMetrics(SM_CXSMICON),
			      GetSystemMetrics(SM_CYSMICON),
			      ILC_MASK | ILC_COLOR32, 1, 1);

    while(1) {
	if (icon_ids[i] == 0)
		break;

	hiconItem = LoadIcon(hinstance, (LPCWSTR) ((uint32_t) icon_ids[i]));
	ImageList_AddIcon(hSmall, hiconItem);
	DestroyIcon(hiconItem);

	i++;
    }

    ListView_SetImageList(hwndList, hSmall, LVSIL_SMALL);

    return TRUE;
}


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
static void
win_settings_init(void)
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
    temp_opl_type = opl_type;
    temp_float = sound_is_float;

    /* Network category */
    temp_net_type = network_type;
    memset(temp_pcap_dev, 0, sizeof(temp_pcap_dev));
    strcpy(temp_pcap_dev, network_host);
    temp_net_card = network_card;

    /* Ports category */
    for (i = 0; i < 3; i++)
	strncpy(temp_lpt_device_names[i], lpt_device_names[i], sizeof(temp_lpt_device_names[i]) - 1);
    temp_serial[0] = serial_enabled[0];
    temp_serial[1] = serial_enabled[1];
    temp_lpt = lpt_enabled;

    /* Other peripherals category */
    temp_scsi_card = scsi_card_current;
    strncpy(temp_hdc_name, hdc_name, sizeof(temp_hdc_name) - 1);
    temp_ide_ter = ide_ter_enabled;
    temp_ide_qua = ide_qua_enabled;
    temp_bugger = bugger_enabled;

    mfm_tracking = xta_tracking = esdi_tracking = ide_tracking = 0;
    for (i = 0; i < 2; i++)
	scsi_tracking[i] = 0;

    /* Hard disks category */
    memcpy(temp_hdd, hdd, HDD_NUM * sizeof(hard_disk_t));
    for (i = 0; i < HDD_NUM; i++) {
	if (hdd[i].bus == HDD_BUS_MFM)
		mfm_tracking |= (1 << (hdd[i].mfm_channel << 3));
	else if (hdd[i].bus == HDD_BUS_XTA)
		xta_tracking |= (1 << (hdd[i].xta_channel << 3));
	else if (hdd[i].bus == HDD_BUS_ESDI)
		esdi_tracking |= (1 << (hdd[i].esdi_channel << 3));
	else if (hdd[i].bus == HDD_BUS_IDE)
		ide_tracking |= (1 << (hdd[i].ide_channel << 3));
	else if (hdd[i].bus == HDD_BUS_SCSI)
		scsi_tracking[hdd[i].scsi_id >> 3] |= (1 << ((hdd[i].scsi_id & 0x07) << 3));
    }

    /* Floppy drives category */
    for (i = 0; i < FDD_NUM; i++) {
	temp_fdd_types[i] = fdd_get_type(i);
	temp_fdd_turbo[i] = fdd_get_turbo(i);
	temp_fdd_check_bpb[i] = fdd_get_check_bpb(i);
    }

    /* Other removable devices category */
    memcpy(temp_cdrom_drives, cdrom_drives, CDROM_NUM * sizeof(cdrom_drive_t));
    for (i = 0; i < CDROM_NUM; i++) {
	if (cdrom_drives[i].bus_type == CDROM_BUS_ATAPI)
		ide_tracking |= (2 << (cdrom_drives[i].ide_channel << 3));
	else if (cdrom_drives[i].bus_type == CDROM_BUS_SCSI)
		scsi_tracking[cdrom_drives[i].scsi_device_id >> 3] |= (1 << ((cdrom_drives[i].scsi_device_id & 0x07) << 3));
    }
    memcpy(temp_zip_drives, zip_drives, ZIP_NUM * sizeof(zip_drive_t));
    for (i = 0; i < ZIP_NUM; i++) {
	if (zip_drives[i].bus_type == ZIP_BUS_ATAPI)
		ide_tracking |= (4 << (zip_drives[i].ide_channel << 3));
	else if (zip_drives[i].bus_type == ZIP_BUS_SCSI)
		scsi_tracking[zip_drives[i].scsi_device_id >> 3] |= (1 << ((zip_drives[i].scsi_device_id & 0x07) << 3));
    }

    temp_deviceconfig = 0;
}


/* This returns 1 if any variable has changed, 0 if not. */
static int
win_settings_changed(void)
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
    i = i || (opl_type != temp_opl_type);
    i = i || (sound_is_float != temp_float);

    /* Network category */
    i = i || (network_type != temp_net_type);
    i = i || strcmp(temp_pcap_dev, network_host);
    i = i || (network_card != temp_net_card);

    /* Ports category */
    for (j = 0; j < 3; j++)
	i = i || strncmp(temp_lpt_device_names[j], lpt_device_names[j], sizeof(temp_lpt_device_names[j]) - 1);
    i = i || (temp_serial[0] != serial_enabled[0]);
    i = i || (temp_serial[1] != serial_enabled[1]);
    i = i || (temp_lpt != lpt_enabled);

    /* Peripherals category */
    i = i || (scsi_card_current != temp_scsi_card);
    i = i || strncmp(temp_hdc_name, hdc_name, sizeof(temp_hdc_name) - 1);
    i = i || (temp_ide_ter != ide_ter_enabled);
    i = i || (temp_ide_qua != ide_qua_enabled);
    i = i || (temp_bugger != bugger_enabled);

    /* Hard disks category */
    i = i || memcmp(hdd, temp_hdd, HDD_NUM * sizeof(hard_disk_t));

    /* Floppy drives category */
    for (j = 0; j < FDD_NUM; j++) {
	i = i || (temp_fdd_types[j] != fdd_get_type(j));
	i = i || (temp_fdd_turbo[j] != fdd_get_turbo(j));
	i = i || (temp_fdd_check_bpb[j] != fdd_get_check_bpb(j));
    }

    /* Other removable devices category */
    i = i || memcmp(cdrom_drives, temp_cdrom_drives, CDROM_NUM * sizeof(cdrom_drive_t));
    i = i || memcmp(zip_drives, temp_zip_drives, ZIP_NUM * sizeof(zip_drive_t));

    i = i || !!temp_deviceconfig;

    return i;
}


static int
settings_msgbox_reset(void)
{
    int changed, i = 0;

    changed = win_settings_changed();

    if (changed) {
	i = settings_msgbox(MBX_QUESTION, (wchar_t *)IDS_2051);

	if (i == 1) return(1);	/* no */

	if (i < 0) return(0);	/* cancel */

	return(2);		/* yes */
    } else
	return(1);
}


/* This saves the settings back to the global variables. */
static void
win_settings_save(void)
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
    opl_type = temp_opl_type;
    sound_is_float = temp_float;

    /* Network category */
    network_type = temp_net_type;
    memset(network_host, '\0', sizeof(network_host));
    strcpy(network_host, temp_pcap_dev);
    network_card = temp_net_card;

    /* Ports category */
    for (i = 0; i < 3; i++)
	strncpy(lpt_device_names[i], temp_lpt_device_names[i], sizeof(temp_lpt_device_names[i]) - 1);
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
    ide_ter_enabled = temp_ide_ter;
    ide_qua_enabled = temp_ide_qua;
    bugger_enabled = temp_bugger;

    /* Hard disks category */
    memcpy(hdd, temp_hdd, HDD_NUM * sizeof(hard_disk_t));

    /* Floppy drives category */
    for (i = 0; i < FDD_NUM; i++) {
	fdd_set_type(i, temp_fdd_types[i]);
	fdd_set_turbo(i, temp_fdd_turbo[i]);
	fdd_set_check_bpb(i, temp_fdd_check_bpb[i]);
    }

    /* Removable devices category */
    memcpy(cdrom_drives, temp_cdrom_drives, CDROM_NUM * sizeof(cdrom_drive_t));
    memcpy(zip_drives, temp_zip_drives, ZIP_NUM * sizeof(zip_drive_t));

    /* Mark configuration as changed. */
    config_changed = 1;

    pc_reset_hard_init();
}


static void
win_settings_machine_recalc_cpu(HWND hdlg)
{
    HWND h;
    int cpu_type, temp_romset;
#ifdef USE_DYNAREC
    int cpu_flags;
#endif

    temp_romset = machine_getromset_ex(temp_machine);

    h = GetDlgItem(hdlg, IDC_COMBO_WS);
    cpu_type = machines[romstomachine[temp_romset]].cpu[temp_cpu_m].cpus[temp_cpu].cpu_type;
    if ((cpu_type >= CPU_286) && (cpu_type <= CPU_386DX))
	EnableWindow(h, TRUE);
    else
	EnableWindow(h, FALSE);

#ifdef USE_DYNAREC
    h=GetDlgItem(hdlg, IDC_CHECK_DYNAREC);
    cpu_flags = machines[romstomachine[temp_romset]].cpu[temp_cpu_m].cpus[temp_cpu].cpu_flags;
    if (!(cpu_flags & CPU_SUPPORTS_DYNAREC) && (cpu_flags & CPU_REQUIRES_DYNAREC))
	fatal("Attempting to select a CPU that requires the recompiler and does not support it at the same time\n");
    if (!(cpu_flags & CPU_SUPPORTS_DYNAREC) || (cpu_flags & CPU_REQUIRES_DYNAREC)) {
	if (!(cpu_flags & CPU_SUPPORTS_DYNAREC))
		temp_dynarec = 0;
	if (cpu_flags & CPU_REQUIRES_DYNAREC)
		temp_dynarec = 1;
	SendMessage(h, BM_SETCHECK, temp_dynarec, 0);
	EnableWindow(h, FALSE);
    } else
	EnableWindow(h, TRUE);
#endif

    h = GetDlgItem(hdlg, IDC_CHECK_FPU);
    cpu_type = machines[romstomachine[temp_romset]].cpu[temp_cpu_m].cpus[temp_cpu].cpu_type;
    if ((cpu_type < CPU_i486DX) && (cpu_type >= CPU_286))
	EnableWindow(h, TRUE);
    else if (cpu_type < CPU_286) {
	temp_fpu = 0;
	EnableWindow(h, FALSE);
    } else {
	temp_fpu = 1;
	EnableWindow(h, FALSE);
    }
    SendMessage(h, BM_SETCHECK, temp_fpu, 0);
}


static void
win_settings_machine_recalc_cpu_m(HWND hdlg)
{
    HWND h;
    int c, temp_romset;
    LPTSTR lptsTemp;
    char *stransi;

    temp_romset = machine_getromset_ex(temp_machine);
    lptsTemp = (LPTSTR) malloc(512);

    h = GetDlgItem(hdlg, IDC_COMBO_CPU);
    SendMessage(h, CB_RESETCONTENT, 0, 0);
    c = 0;
    while (machines[romstomachine[temp_romset]].cpu[temp_cpu_m].cpus[c].cpu_type != -1) {
	stransi = (char *) machines[romstomachine[temp_romset]].cpu[temp_cpu_m].cpus[c].name;
	mbstowcs(lptsTemp, stransi, strlen(stransi) + 1);
	SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)lptsTemp);
	c++;
    }
    EnableWindow(h, TRUE);
    if (temp_cpu >= c)
	temp_cpu = (c - 1);
    SendMessage(h, CB_SETCURSEL, temp_cpu, 0);

    win_settings_machine_recalc_cpu(hdlg);

    free(lptsTemp);
}


static void
win_settings_machine_recalc_machine(HWND hdlg)
{
    HWND h;
    int c, temp_romset;
    LPTSTR lptsTemp;
    const char *stransi;
    UDACCEL accel;

    temp_romset = machine_getromset_ex(temp_machine);
    lptsTemp = (LPTSTR) malloc(512);

    h = GetDlgItem(hdlg, IDC_CONFIGURE_MACHINE);
    if (machine_getdevice(temp_machine))
	EnableWindow(h, TRUE);
    else
	EnableWindow(h, FALSE);

    h = GetDlgItem(hdlg, IDC_COMBO_CPU_TYPE);
    SendMessage(h, CB_RESETCONTENT, 0, 0);
    c = 0;
    while (machines[romstomachine[temp_romset]].cpu[c].cpus != NULL && c < 4) {
	stransi = machines[romstomachine[temp_romset]].cpu[c].name;
	mbstowcs(lptsTemp, stransi, strlen(stransi) + 1);
	SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)lptsTemp);
	c++;
    }
    EnableWindow(h, TRUE);
    if (temp_cpu_m >= c)
	temp_cpu_m = (c - 1);
    SendMessage(h, CB_SETCURSEL, temp_cpu_m, 0);
    EnableWindow(h, (c == 1) ? FALSE : TRUE);

    win_settings_machine_recalc_cpu_m(hdlg);

    h = GetDlgItem(hdlg, IDC_MEMSPIN);
    SendMessage(h, UDM_SETRANGE, 0, (machines[romstomachine[temp_romset]].min_ram << 16) | machines[romstomachine[temp_romset]].max_ram);
    accel.nSec = 0;
    accel.nInc = machines[romstomachine[temp_romset]].ram_granularity;
    SendMessage(h, UDM_SETACCEL, 1, (LPARAM)&accel);
    if (!(machines[romstomachine[temp_romset]].flags & MACHINE_AT) || (machines[romstomachine[temp_romset]].ram_granularity >= 128)) {
	SendMessage(h, UDM_SETPOS, 0, temp_mem_size);
	h = GetDlgItem(hdlg, IDC_TEXT_MB);
	SendMessage(h, WM_SETTEXT, 0, win_get_string(IDS_2094));
    } else {
	SendMessage(h, UDM_SETPOS, 0, temp_mem_size / 1024);
	h = GetDlgItem(hdlg, IDC_TEXT_MB);
	SendMessage(h, WM_SETTEXT, 0, win_get_string(IDS_2087));
    }

    free(lptsTemp);
}


#ifdef __amd64__
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_machine_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND h, h2;
    int c, d;
    LPTSTR lptsTemp;
    char *stransi;

    switch (message) {
	case WM_INITDIALOG:
		lptsTemp = (LPTSTR) malloc(512);

		h = GetDlgItem(hdlg, IDC_COMBO_MACHINE);
		for (c = 0; c < ROM_MAX; c++)
			romstolist[c] = 0;
		c = d = 0;
		while (machines[c].id != -1) {
			if (romspresent[machines[c].id]) {
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
                SendMessage(h, CB_ADDSTRING, 0, win_get_string(IDS_2099));

		for (c = 0; c < 8; c++) {
			wsprintf(lptsTemp, plat_get_string(2100), c);
        	        SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
		}

		SendMessage(h, CB_SETCURSEL, temp_wait_states, 0);

#ifdef USE_DYNAREC
       	        h=GetDlgItem(hdlg, IDC_CHECK_DYNAREC);
                SendMessage(h, BM_SETCHECK, temp_dynarec, 0);
#endif

		h = GetDlgItem(hdlg, IDC_MEMSPIN);
		h2 = GetDlgItem(hdlg, IDC_MEMTEXT);
		SendMessage(h, UDM_SETBUDDY, (WPARAM)h2, 0);

       	        h=GetDlgItem(hdlg, IDC_CHECK_SYNC);
                SendMessage(h, BM_SETCHECK, temp_sync, 0);

		win_settings_machine_recalc_machine(hdlg);

		free(lptsTemp);

		return TRUE;

	case WM_COMMAND:
               	switch (LOWORD(wParam)) {
               	        case IDC_COMBO_MACHINE:
        	                if (HIWORD(wParam) == CBN_SELCHANGE) {
       		                        h = GetDlgItem(hdlg, IDC_COMBO_MACHINE);
                	                temp_machine = listtomachine[SendMessage(h,CB_GETCURSEL,0,0)];

					win_settings_machine_recalc_machine(hdlg);
				}
				break;
			case IDC_COMBO_CPU_TYPE:
        	                if (HIWORD(wParam) == CBN_SELCHANGE) {
       		                        h = GetDlgItem(hdlg, IDC_COMBO_CPU_TYPE);
                	                temp_cpu_m = SendMessage(h, CB_GETCURSEL, 0, 0);

					temp_cpu = 0;
					win_settings_machine_recalc_cpu_m(hdlg);
				}
				break;
			case IDC_COMBO_CPU:
        	                if (HIWORD(wParam) == CBN_SELCHANGE) {
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
		sscanf(stransi, "%u", &temp_mem_size);
		temp_mem_size &= ~(machines[temp_machine].ram_granularity - 1);
		if (temp_mem_size < machines[temp_machine].min_ram)
			temp_mem_size = machines[temp_machine].min_ram;
		else if (temp_mem_size > machines[temp_machine].max_ram)
			temp_mem_size = machines[temp_machine].max_ram;
		if ((machines[temp_machine].flags & MACHINE_AT) && (machines[temp_machine].ram_granularity < 128))
			temp_mem_size *= 1024;
		free(stransi);
		free(lptsTemp);

	default:
		return FALSE;
    }

    return FALSE;
}


static void
recalc_vid_list(HWND hdlg)
{
    HWND h = GetDlgItem(hdlg, IDC_COMBO_VIDEO);
    int c = 0, d = 0;
    int found_card = 0;
    WCHAR szText[512];

    SendMessage(h, CB_RESETCONTENT, 0, 0);
    SendMessage(h, CB_SETCURSEL, 0, 0);

    while (1) {
	/* Skip "internal" if machine doesn't have it. */
	if ((c == 1) && !(machines[temp_machine].flags&MACHINE_VIDEO)) {
		c++;
		continue;
	}

	char *s = video_card_getname(c);

	if (!s[0])
		break;

	if (video_card_available(c) && gfx_present[video_new_to_old(c)] &&
	    device_is_valid(video_card_getdevice(c), machines[temp_machine].flags)) {
		mbstowcs(szText, s, strlen(s) + 1);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) szText);
		if (video_new_to_old(c) == temp_gfxcard) {
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


#ifdef __amd64__
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_video_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND h;
    LPTSTR lptsTemp;
    char *stransi;
    int gfx;

    switch (message) {
	case WM_INITDIALOG:
		lptsTemp = (LPTSTR) malloc(512);
		stransi = (char *) malloc(512);

		recalc_vid_list(hdlg);

                h=GetDlgItem(hdlg, IDC_CHECK_VOODOO);
       	        SendMessage(h, BM_SETCHECK, temp_voodoo, 0);

		h = GetDlgItem(hdlg, IDC_COMBO_VIDEO);
		SendMessage(h, CB_GETLBTEXT, SendMessage(h, CB_GETCURSEL, 0, 0), (LPARAM) lptsTemp);
		wcstombs(stransi, lptsTemp, 512);
		gfx = video_card_getid(stransi);

		h = GetDlgItem(hdlg, IDC_CONFIGURE_VID);
		if (video_card_has_config(gfx))
			EnableWindow(h, TRUE);
		else
			EnableWindow(h, FALSE);

		free(stransi);
		free(lptsTemp);

		return TRUE;

	case WM_COMMAND:
               	switch (LOWORD(wParam)) {
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
					EnableWindow(h, TRUE);
				else
					EnableWindow(h, FALSE);

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

		h = GetDlgItem(hdlg, IDC_CHECK_VOODOO);
		temp_voodoo = SendMessage(h, BM_GETCHECK, 0, 0);

		free(stransi);
		free(lptsTemp);

	default:
		return FALSE;
    }
    return FALSE;
}


static int
mouse_valid(int num, int m)
{
    const device_t *dev;

    if ((num == MOUSE_TYPE_INTERNAL) &&
	!(machines[m].flags & MACHINE_MOUSE)) return(0);

    dev = mouse_get_device(num);
    return(device_is_valid(dev, machines[m].flags));
}


#ifdef __amd64__
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_input_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    wchar_t str[128];
    HWND h;
    int c, d;

    switch (message) {
	case WM_INITDIALOG:
		h = GetDlgItem(hdlg, IDC_COMBO_MOUSE);
		c = d = 0;
		for (c = 0; c < mouse_get_ndev(); c++) {
			settings_device_to_list[c] = d;

			if (mouse_valid(c, temp_machine)) {
				mbstowcs(str, mouse_get_name(c), sizeof_w(str));
				SendMessage(h, CB_ADDSTRING, 0, (LPARAM)str);

				settings_list_to_device[d] = c;
				d++;
			}
		}

		SendMessage(h, CB_SETCURSEL, settings_device_to_list[temp_mouse], 0);

		h = GetDlgItem(hdlg, IDC_CONFIGURE_MOUSE);
		if (mouse_has_config(temp_mouse))
			EnableWindow(h, TRUE);
		else
			EnableWindow(h, FALSE);

		h = GetDlgItem(hdlg, IDC_COMBO_JOYSTICK);
		c = 0;
		while (joystick_get_name(c)) {
			SendMessage(h, CB_ADDSTRING, 0, win_get_string(2105 + c));
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
               	switch (LOWORD(wParam)) {
			case IDC_COMBO_MOUSE:
				h = GetDlgItem(hdlg, IDC_COMBO_MOUSE);
				temp_mouse = settings_list_to_device[SendMessage(h, CB_GETCURSEL, 0, 0)];

				h = GetDlgItem(hdlg, IDC_CONFIGURE_MOUSE);
				if (mouse_has_config(temp_mouse))
					EnableWindow(h, TRUE);
				else
					EnableWindow(h, FALSE);
				break;

			case IDC_CONFIGURE_MOUSE:
				h = GetDlgItem(hdlg, IDC_COMBO_MOUSE);
				temp_mouse = settings_list_to_device[SendMessage(h, CB_GETCURSEL, 0, 0)];
				temp_deviceconfig |= deviceconfig_open(hdlg, (void *)mouse_get_device(temp_mouse));
				break;

			case IDC_COMBO_JOYSTICK:
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
				break;

			case IDC_JOY1:
				h = GetDlgItem(hdlg, IDC_COMBO_JOYSTICK);
				temp_joystick = SendMessage(h, CB_GETCURSEL, 0, 0);
				temp_deviceconfig |= joystickconfig_open(hdlg, 0, temp_joystick);
				break;

			case IDC_JOY2:
				h = GetDlgItem(hdlg, IDC_COMBO_JOYSTICK);
				temp_joystick = SendMessage(h, CB_GETCURSEL, 0, 0);
				temp_deviceconfig |= joystickconfig_open(hdlg, 1, temp_joystick);
				break;

			case IDC_JOY3:
				h = GetDlgItem(hdlg, IDC_COMBO_JOYSTICK);
				temp_joystick = SendMessage(h, CB_GETCURSEL, 0, 0);
				temp_deviceconfig |= joystickconfig_open(hdlg, 2, temp_joystick);
				break;

			case IDC_JOY4:
				h = GetDlgItem(hdlg, IDC_COMBO_JOYSTICK);
				temp_joystick = SendMessage(h, CB_GETCURSEL, 0, 0);
				temp_deviceconfig |= joystickconfig_open(hdlg, 3, temp_joystick);
				break;
		}
		return FALSE;

	case WM_SAVESETTINGS:
		h = GetDlgItem(hdlg, IDC_COMBO_MOUSE);
		temp_mouse = settings_list_to_device[SendMessage(h, CB_GETCURSEL, 0, 0)];

		h = GetDlgItem(hdlg, IDC_COMBO_JOYSTICK);
		temp_joystick = SendMessage(h, CB_GETCURSEL, 0, 0);

	default:
		return FALSE;
    }
    return FALSE;
}


static int
mpu401_present(void)
{
    char *n;

    n = sound_card_get_internal_name(temp_sound_card);
    if (n != NULL) {
	if (!strcmp(n, "sb16") || !strcmp(n, "sbawe32"))
		return 1;
    }

    return temp_mpu401 ? 1 : 0;
}


int
mpu401_standalone_allow(void)
{
    char *n, *md;

    n = sound_card_get_internal_name(temp_sound_card);
    md = midi_device_get_internal_name(temp_midi_device);
    if (n != NULL) {
	if (!strcmp(n, "sb16") || !strcmp(n, "sbawe32"))
		return 0;
    }

    if (md != NULL) {
	if (!strcmp(md, "none"))
		return 0;
    }

    return 1;
}


#ifdef __amd64__
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_sound_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND h;
    int c, d;
    LPTSTR lptsTemp;
    const device_t *sound_dev;
    char *s;

    switch (message) {
	case WM_INITDIALOG:
		lptsTemp = (LPTSTR) malloc(512);

		h = GetDlgItem(hdlg, IDC_COMBO_SOUND);
		c = d = 0;
		while (1) {
			s = sound_card_getname(c);

			if (!s[0])
				break;

			settings_device_to_list[c] = d;

			if (sound_card_available(c)) {
				sound_dev = sound_card_getdevice(c);

				if (device_is_valid(sound_dev, machines[temp_machine].flags)) {
					if (c == 0)
						SendMessage(h, CB_ADDSTRING, 0, win_get_string(IDS_2112));
					else {
						mbstowcs(lptsTemp, s, strlen(s) + 1);
						SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
					}
					settings_list_to_device[d] = c;
					d++;
				}
			}

			c++;
		}
		SendMessage(h, CB_SETCURSEL, settings_device_to_list[temp_sound_card], 0);

		EnableWindow(h, d ? TRUE : FALSE);

		h = GetDlgItem(hdlg, IDC_CONFIGURE_SND);
		EnableWindow(h, sound_card_has_config(temp_sound_card) ? TRUE : FALSE);

		h = GetDlgItem(hdlg, IDC_COMBO_MIDI);
		c = d = 0;
		while (1) {
			s = midi_device_getname(c);

			if (!s[0])
				break;

			settings_midi_to_list[c] = d;

			if (midi_device_available(c)) {
				if (c == 0)
					SendMessage(h, CB_ADDSTRING, 0, win_get_string(IDS_2112));
				else {
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
			EnableWindow(h, TRUE);
		else
			EnableWindow(h, FALSE);

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
		SendMessage(h, BM_SETCHECK, temp_opl_type, 0);

		h=GetDlgItem(hdlg, IDC_CHECK_FLOAT);
		SendMessage(h, BM_SETCHECK, temp_float, 0);

		free(lptsTemp);

		return TRUE;

	case WM_COMMAND:
               	switch (LOWORD(wParam)) {
			case IDC_COMBO_SOUND:
				h = GetDlgItem(hdlg, IDC_COMBO_SOUND);
				temp_sound_card = settings_list_to_device[SendMessage(h, CB_GETCURSEL, 0, 0)];

				h = GetDlgItem(hdlg, IDC_CONFIGURE_SND);
				if (sound_card_has_config(temp_sound_card))
					EnableWindow(h, TRUE);
				else
					EnableWindow(h, FALSE);

			        h = GetDlgItem(hdlg, IDC_CHECK_MPU401);
       			        SendMessage(h, BM_SETCHECK, temp_mpu401, 0);
			        EnableWindow(h, mpu401_standalone_allow() ? TRUE : FALSE);

			        h = GetDlgItem(hdlg, IDC_CONFIGURE_MPU401);
			        EnableWindow(h, (mpu401_standalone_allow() && temp_mpu401) ? TRUE : FALSE);
				break;

			case IDC_CONFIGURE_SND:
				h = GetDlgItem(hdlg, IDC_COMBO_SOUND);
				temp_sound_card = settings_list_to_device[SendMessage(h, CB_GETCURSEL, 0, 0)];

				temp_deviceconfig |= deviceconfig_open(hdlg, (void *)sound_card_getdevice(temp_sound_card));
				break;

			case IDC_COMBO_MIDI:
				h = GetDlgItem(hdlg, IDC_COMBO_MIDI);
				temp_midi_device = settings_list_to_midi[SendMessage(h, CB_GETCURSEL, 0, 0)];

				h = GetDlgItem(hdlg, IDC_CONFIGURE_MIDI);
				if (midi_device_has_config(temp_midi_device))
					EnableWindow(h, TRUE);
				else
					EnableWindow(h, FALSE);

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
		temp_sound_card = settings_list_to_device[SendMessage(h, CB_GETCURSEL, 0, 0)];

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
		temp_opl_type = SendMessage(h, BM_GETCHECK, 0, 0);

		h = GetDlgItem(hdlg, IDC_CHECK_FLOAT);
		temp_float = SendMessage(h, BM_GETCHECK, 0, 0);

	default:
		return FALSE;
    }
    return FALSE;
}


#ifdef __amd64__
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_ports_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND h;
    int c, d, i;
    char *s;
    LPTSTR lptsTemp;

    switch (message) {
	case WM_INITDIALOG:
		lptsTemp = (LPTSTR) malloc(512);

		for (i = 0; i < 3; i++) {
			h = GetDlgItem(hdlg, IDC_COMBO_LPT1 + i);
			c = d = 0;
			while (1) {
				s = lpt_device_get_name(c);

				if (!s)
					break;

				if (c == 0)
					SendMessage(h, CB_ADDSTRING, 0, win_get_string(IDS_2112));
				else {
					mbstowcs(lptsTemp, s, strlen(s) + 1);
					SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
				}

				if (!strcmp(temp_lpt_device_names[i], lpt_device_get_internal_name(c)))
					d = c;

				c++;
			}
			SendMessage(h, CB_SETCURSEL, d, 0);
		}

		h=GetDlgItem(hdlg, IDC_CHECK_SERIAL1);
		SendMessage(h, BM_SETCHECK, temp_serial[0], 0);

		h=GetDlgItem(hdlg, IDC_CHECK_SERIAL2);
		SendMessage(h, BM_SETCHECK, temp_serial[1], 0);

		h=GetDlgItem(hdlg, IDC_CHECK_PARALLEL);
		SendMessage(h, BM_SETCHECK, temp_lpt, 0);

		free(lptsTemp);

		return TRUE;

	case WM_SAVESETTINGS:
		for (i = 0; i < 3; i++) {
			h = GetDlgItem(hdlg, IDC_COMBO_LPT1 + i);
			c = SendMessage(h, CB_GETCURSEL, 0, 0);
			strcpy(temp_lpt_device_names[i], lpt_device_get_internal_name(c));
		}

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


static void
recalc_hdc_list(HWND hdlg, int machine, int use_selected_hdc)
{
    HWND h;
    char *s, old_name[32];
    int valid, c, d;

    LPTSTR lptsTemp;

    lptsTemp = (LPTSTR) malloc(512);

    h = GetDlgItem(hdlg, IDC_COMBO_HDC);

    valid = 0;

    if (use_selected_hdc) {
	c = SendMessage(h, CB_GETCURSEL, 0, 0);

	if (c != -1 && hdc_names[c])
		strncpy(old_name, hdc_names[c], sizeof(old_name) - 1);
	else
		strcpy(old_name, "none");
    } else
	strncpy(old_name, temp_hdc_name, sizeof(old_name) - 1);

    SendMessage(h, CB_RESETCONTENT, 0, 0);
    c = d = 0;
    while (1) {
	s = hdc_get_name(c);
	if (s[0] == 0)
		break;
	if (c==1 && !(machines[temp_machine].flags&MACHINE_HDC)) {
		/* Skip "Internal" if machine doesn't have one. */
		c++;
		continue;
	}
	if (!hdc_available(c) || !device_is_valid(hdc_get_device(c), machines[temp_machine].flags)) {
		c++;
		continue;
	}
	mbstowcs(lptsTemp, s, strlen(s) + 1);
	SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);

	hdc_names[d] = hdc_get_internal_name(c);
	if (!strcmp(old_name, hdc_names[d])) {
		SendMessage(h, CB_SETCURSEL, d, 0);
		valid = 1;
	}
	c++;
	d++;
    }

    if (!valid)
	SendMessage(h, CB_SETCURSEL, 0, 0);

    EnableWindow(h, d ? TRUE : FALSE);

    free(lptsTemp);
}


#ifdef __amd64__
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_peripherals_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND h;
    int c, d, temp_hdc_type;
    LPTSTR lptsTemp;
    const device_t *scsi_dev;

    switch (message) {
	case WM_INITDIALOG:
		lptsTemp = (LPTSTR) malloc(512);

		/*SCSI config*/
		h = GetDlgItem(hdlg, IDC_COMBO_SCSI);
		c = d = 0;
		while (1) {
			char *s = scsi_card_getname(c);

			if (!s[0])
				break;

			settings_device_to_list[c] = d;			

			if (scsi_card_available(c)) {
				scsi_dev = scsi_card_getdevice(c);

				if (device_is_valid(scsi_dev, machines[temp_machine].flags)) {
					if (c == 0)
						SendMessage(h, CB_ADDSTRING, 0, win_get_string(IDS_2112));
					else {
						mbstowcs(lptsTemp, s, strlen(s) + 1);
						SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
					}
					settings_list_to_device[d] = c;
					d++;
				}
			}

			c++;
		}
		SendMessage(h, CB_SETCURSEL, settings_device_to_list[temp_scsi_card], 0);

		EnableWindow(h, d ? TRUE : FALSE);

		h = GetDlgItem(hdlg, IDC_CONFIGURE_SCSI);
		EnableWindow(h, scsi_card_has_config(temp_scsi_card) ? TRUE : FALSE);

		recalc_hdc_list(hdlg, temp_machine, 0);

		h = GetDlgItem(hdlg, IDC_CONFIGURE_HDC);
		EnableWindow(h, hdc_has_config(hdc_get_from_internal_name(temp_hdc_name)) ? TRUE : FALSE);

		h = GetDlgItem(hdlg, IDC_CHECK_IDE_TER);
	        EnableWindow(h, (machines[temp_machine].flags & MACHINE_AT) ? TRUE : FALSE);

		h = GetDlgItem(hdlg, IDC_BUTTON_IDE_TER);
		EnableWindow(h, ((machines[temp_machine].flags & MACHINE_AT) && temp_ide_ter) ? TRUE : FALSE);

       		h = GetDlgItem(hdlg, IDC_CHECK_IDE_QUA);
	        EnableWindow(h, (machines[temp_machine].flags & MACHINE_AT) ? TRUE : FALSE);

		h = GetDlgItem(hdlg, IDC_BUTTON_IDE_QUA);
		EnableWindow(h, ((machines[temp_machine].flags & MACHINE_AT) && temp_ide_qua) ? TRUE : FALSE);

                h=GetDlgItem(hdlg, IDC_CHECK_IDE_TER);
       	        SendMessage(h, BM_SETCHECK, temp_ide_ter, 0);

                h=GetDlgItem(hdlg, IDC_CHECK_IDE_QUA);
       	        SendMessage(h, BM_SETCHECK, temp_ide_qua, 0);

		h=GetDlgItem(hdlg, IDC_CHECK_BUGGER);
		SendMessage(h, BM_SETCHECK, temp_bugger, 0);

		free(lptsTemp);

		return TRUE;

	case WM_COMMAND:
               	switch (LOWORD(wParam)) {
			case IDC_COMBO_HDC:
				h = GetDlgItem(hdlg, IDC_COMBO_HDC);
				temp_hdc_type = hdc_get_from_internal_name(hdc_names[SendMessage(h, CB_GETCURSEL, 0, 0)]);

				h = GetDlgItem(hdlg, IDC_CONFIGURE_HDC);
				EnableWindow(h, hdc_has_config(temp_hdc_type) ? TRUE : FALSE);
				break;

			case IDC_CONFIGURE_HDC:
				h = GetDlgItem(hdlg, IDC_COMBO_HDC);
				temp_hdc_type = hdc_get_from_internal_name(hdc_names[SendMessage(h, CB_GETCURSEL, 0, 0)]);

				temp_deviceconfig |= deviceconfig_open(hdlg, (void *)hdc_get_device(temp_hdc_type));
				break;

			case IDC_CONFIGURE_SCSI:
				h = GetDlgItem(hdlg, IDC_COMBO_SCSI);
				temp_scsi_card = settings_list_to_device[SendMessage(h, CB_GETCURSEL, 0, 0)];

				temp_deviceconfig |= deviceconfig_open(hdlg, (void *)scsi_card_getdevice(temp_scsi_card));
				break;

			case IDC_COMBO_SCSI:
				h = GetDlgItem(hdlg, IDC_COMBO_SCSI);
				temp_scsi_card = settings_list_to_device[SendMessage(h, CB_GETCURSEL, 0, 0)];

				h = GetDlgItem(hdlg, IDC_CONFIGURE_SCSI);
				if (scsi_card_has_config(temp_scsi_card))
					EnableWindow(h, TRUE);
				else
					EnableWindow(h, FALSE);
				break;

			case IDC_CHECK_IDE_TER:
        		        h = GetDlgItem(hdlg, IDC_CHECK_IDE_TER);
				temp_ide_ter = SendMessage(h, BM_GETCHECK, 0, 0);

        		        h = GetDlgItem(hdlg, IDC_BUTTON_IDE_TER);
				EnableWindow(h, temp_ide_ter ? TRUE : FALSE);
				break;

			case IDC_BUTTON_IDE_TER:
				temp_deviceconfig |= deviceconfig_open(hdlg, (void *)&ide_ter_device);
				break;

			case IDC_CHECK_IDE_QUA:
        		        h = GetDlgItem(hdlg, IDC_CHECK_IDE_QUA);
				temp_ide_qua = SendMessage(h, BM_GETCHECK, 0, 0);

        		        h = GetDlgItem(hdlg, IDC_BUTTON_IDE_QUA);
				EnableWindow(h, temp_ide_qua ? TRUE : FALSE);
				break;

			case IDC_BUTTON_IDE_QUA:
				temp_deviceconfig |= deviceconfig_open(hdlg, (void *)&ide_qua_device);
				break;
		}
		return FALSE;

	case WM_SAVESETTINGS:
		h = GetDlgItem(hdlg, IDC_COMBO_HDC);
		c = SendMessage(h, CB_GETCURSEL, 0, 0);
		if (hdc_names[c])
			strncpy(temp_hdc_name, hdc_names[c], sizeof(temp_hdc_name) - 1);
		else
			strcpy(temp_hdc_name, "none");

		h = GetDlgItem(hdlg, IDC_COMBO_SCSI);
		temp_scsi_card = settings_list_to_device[SendMessage(h, CB_GETCURSEL, 0, 0)];

                h = GetDlgItem(hdlg, IDC_CHECK_IDE_TER);
		temp_ide_ter = SendMessage(h, BM_GETCHECK, 0, 0);

                h = GetDlgItem(hdlg, IDC_CHECK_IDE_QUA);
		temp_ide_qua = SendMessage(h, BM_GETCHECK, 0, 0);

		h = GetDlgItem(hdlg, IDC_CHECK_BUGGER);
		temp_bugger = SendMessage(h, BM_GETCHECK, 0, 0);

	default:
		return FALSE;
    }
    return FALSE;
}


static void network_recalc_combos(HWND hdlg)
{
    HWND h;

    ignore_change = 1;

    h = GetDlgItem(hdlg, IDC_COMBO_PCAP);
    EnableWindow(h, (temp_net_type == NET_TYPE_PCAP) ? TRUE : FALSE);

    h = GetDlgItem(hdlg, IDC_COMBO_NET);
    if (temp_net_type == NET_TYPE_SLIRP)
	EnableWindow(h, TRUE);
    else if ((temp_net_type == NET_TYPE_PCAP) &&
	     (network_dev_to_id(temp_pcap_dev) > 0))
	EnableWindow(h, TRUE);
    else
	EnableWindow(h, FALSE);

    h = GetDlgItem(hdlg, IDC_CONFIGURE_NET);
    if (network_card_has_config(temp_net_card) &&
	(temp_net_type == NET_TYPE_SLIRP))
	EnableWindow(h, TRUE);
    else if (network_card_has_config(temp_net_card) &&
	     (temp_net_type == NET_TYPE_PCAP) &&
	     (network_dev_to_id(temp_pcap_dev) > 0))
	EnableWindow(h, TRUE);
    else
	EnableWindow(h, FALSE);

    ignore_change = 0;
}


#ifdef __amd64__
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_network_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND h;
    int c, d;
    LPTSTR lptsTemp;
    char *s;

    switch (message) {
	case WM_INITDIALOG:
		lptsTemp = (LPTSTR) malloc(512);

		h = GetDlgItem(hdlg, IDC_COMBO_NET_TYPE);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) L"None");
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) L"PCap");
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM) L"SLiRP");
		SendMessage(h, CB_SETCURSEL, temp_net_type, 0);

		h = GetDlgItem(hdlg, IDC_COMBO_PCAP);
		if (temp_net_type == NET_TYPE_PCAP)
			EnableWindow(h, TRUE);
		else
			EnableWindow(h, FALSE);

		h = GetDlgItem(hdlg, IDC_COMBO_PCAP);
		for (c = 0; c < network_ndev; c++) {
			mbstowcs(lptsTemp, network_devs[c].description, strlen(network_devs[c].description) + 1);
			SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
		}
		SendMessage(h, CB_SETCURSEL, network_dev_to_id(temp_pcap_dev), 0);

		/*NIC config*/
		h = GetDlgItem(hdlg, IDC_COMBO_NET);
		c = d = 0;
		while (1) {
			s = network_card_getname(c);

			if (s[0] == '\0')
				break;

			settings_device_to_list[c] = d;

			if (network_card_available(c) && device_is_valid(network_card_getdevice(c), machines[temp_machine].flags)) {
				if (c == 0)
					SendMessage(h, CB_ADDSTRING, 0, win_get_string(2112));
				else {
					mbstowcs(lptsTemp, s, strlen(s) + 1);
					SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
				}
				settings_list_to_device[d] = c;
				d++;
			}

			c++;
		}

		SendMessage(h, CB_SETCURSEL, settings_device_to_list[temp_net_card], 0);
		EnableWindow(h, d ? TRUE : FALSE);
		network_recalc_combos(hdlg);
		free(lptsTemp);

		return TRUE;

	case WM_COMMAND:
               	switch (LOWORD(wParam)) {
			case IDC_COMBO_NET_TYPE:
				if (ignore_change)
					return FALSE;

				h = GetDlgItem(hdlg, IDC_COMBO_NET_TYPE);
				temp_net_type = SendMessage(h, CB_GETCURSEL, 0, 0);

				network_recalc_combos(hdlg);
				break;

			case IDC_COMBO_PCAP:
				if (ignore_change)
					return FALSE;

				h = GetDlgItem(hdlg, IDC_COMBO_PCAP);
				memset(temp_pcap_dev, '\0', sizeof(temp_pcap_dev));
				strcpy(temp_pcap_dev, network_devs[SendMessage(h, CB_GETCURSEL, 0, 0)].device);

				network_recalc_combos(hdlg);
				break;

			case IDC_COMBO_NET:
				if (ignore_change)
					return FALSE;

				h = GetDlgItem(hdlg, IDC_COMBO_NET);
				temp_net_card = settings_list_to_device[SendMessage(h, CB_GETCURSEL, 0, 0)];

				network_recalc_combos(hdlg);
				break;

			case IDC_CONFIGURE_NET:
				if (ignore_change)
					return FALSE;

				h = GetDlgItem(hdlg, IDC_COMBO_NET);
				temp_net_card = settings_list_to_device[SendMessage(h, CB_GETCURSEL, 0, 0)];

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
		temp_net_card = settings_list_to_device[SendMessage(h, CB_GETCURSEL, 0, 0)];

	default:
		return FALSE;
    }

    return FALSE;
}


static void
normalize_hd_list()
{
    hard_disk_t ihdd[HDD_NUM];
    int i, j;

    j = 0;
    memset(ihdd, 0x00, HDD_NUM * sizeof(hard_disk_t));

    for (i = 0; i < HDD_NUM; i++) {
	if (temp_hdd[i].bus != HDD_BUS_DISABLED) {
		memcpy(&(ihdd[j]), &(temp_hdd[i]), sizeof(hard_disk_t));
		j++;
	}
    }

    memcpy(temp_hdd, ihdd, HDD_NUM * sizeof(hard_disk_t));
}


static int
get_selected_hard_disk(HWND hdlg)
{
    int hard_disk = -1;
    int i, j = 0;
    HWND h;

    if (hd_listview_items == 0)
	return 0;

    for (i = 0; i < hd_listview_items; i++) {
	h = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
	j = ListView_GetItemState(h, i, LVIS_SELECTED);
	if (j)
		hard_disk = i;
    }

    return hard_disk;
}


static void
add_locations(HWND hdlg)
{
    LPTSTR lptsTemp;
    HWND h;
    int i = 0;

    lptsTemp = (LPTSTR) malloc(512);

    h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
    for (i = 0; i < 5; i++)
	SendMessage(h, CB_ADDSTRING, 0, win_get_string(IDS_4352 + i));

    h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
    for (i = 0; i < 2; i++) {
	wsprintf(lptsTemp, plat_get_string(IDS_4097), i >> 1, i & 1);
	SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
    }

    h = GetDlgItem(hdlg, IDC_COMBO_HD_ID);
    for (i = 0; i < 16; i++) {
	wsprintf(lptsTemp, plat_get_string(IDS_4098), i);
	SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
    }

    h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL_IDE);
    for (i = 0; i < 8; i++) {
	wsprintf(lptsTemp, plat_get_string(IDS_4097), i >> 1, i & 1);
	SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
    }

    free(lptsTemp);
}


static uint8_t
next_free_binary_channel(uint64_t *tracking)
{
    int64_t i;

    for (i = 0; i < 2; i++) {
	if (!(*tracking & (0xffLL << (i << 3LL))))
		return i;
    }

    return 2;
}


static uint8_t
next_free_ide_channel(void)
{
    int64_t i;

    for (i = 0; i < 8; i++) {
	if (!(ide_tracking & (0xffLL << (i << 3LL))))
		return i;
    }

    return 7;
}


static void
next_free_scsi_id(uint8_t *id)
{
    int64_t i;

    for (i = 0; i < 16; i++) {
	if (!(scsi_tracking[i >> 3] & (0xffLL << ((i & 0x07) << 3LL)))) {
		*id = i;
		return;
	}
    }

    *id = 6;
}


static void
recalc_location_controls(HWND hdlg, int is_add_dlg, int assign_id)
{
    int i = 0;
    HWND h;

    int bus = 0;

    for (i = IDT_1722; i <= IDT_1723; i++) {
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

    h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL_IDE);
    EnableWindow(h, FALSE);
    ShowWindow(h, SW_HIDE);

    if ((hd_listview_items > 0) || is_add_dlg) {
	h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
	bus = SendMessage(h, CB_GETCURSEL, 0, 0);
	bus++;

	switch(bus) {
		case HDD_BUS_MFM:		/* MFM */
			h = GetDlgItem(hdlg, IDT_1722);
			ShowWindow(h, SW_SHOW);
			EnableWindow(h, TRUE);

			h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
			ShowWindow(h, SW_SHOW);
			EnableWindow(h, TRUE);
			if (assign_id)
				temp_hdd[lv1_current_sel].mfm_channel = next_free_binary_channel(&mfm_tracking);
			SendMessage(h, CB_SETCURSEL, is_add_dlg ? new_hdd.mfm_channel : temp_hdd[lv1_current_sel].mfm_channel, 0);
			break;
		case HDD_BUS_XTA:		/* XTA */
			h = GetDlgItem(hdlg, IDT_1722);
			ShowWindow(h, SW_SHOW);
			EnableWindow(h, TRUE);

			h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
			ShowWindow(h, SW_SHOW);
			EnableWindow(h, TRUE);
			if (assign_id)
				temp_hdd[lv1_current_sel].xta_channel = next_free_binary_channel(&xta_tracking);
			SendMessage(h, CB_SETCURSEL, is_add_dlg ? new_hdd.xta_channel : temp_hdd[lv1_current_sel].xta_channel, 0);
			break;
		case HDD_BUS_ESDI:		/* ESDI */
			h = GetDlgItem(hdlg, IDT_1722);
			ShowWindow(h, SW_SHOW);
			EnableWindow(h, TRUE);

			h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
			ShowWindow(h, SW_SHOW);
			EnableWindow(h, TRUE);
			if (assign_id)
				temp_hdd[lv1_current_sel].esdi_channel = next_free_binary_channel(&esdi_tracking);
			SendMessage(h, CB_SETCURSEL, is_add_dlg ? new_hdd.esdi_channel : temp_hdd[lv1_current_sel].esdi_channel, 0);
			break;
		case HDD_BUS_IDE:		/* IDE */
			h = GetDlgItem(hdlg, IDT_1722);
			ShowWindow(h, SW_SHOW);
			EnableWindow(h, TRUE);

			h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL_IDE);
			ShowWindow(h, SW_SHOW);
			EnableWindow(h, TRUE);
			if (assign_id)
				temp_hdd[lv1_current_sel].ide_channel = next_free_ide_channel();
			SendMessage(h, CB_SETCURSEL, is_add_dlg ? new_hdd.ide_channel : temp_hdd[lv1_current_sel].ide_channel, 0);
			break;
		case HDD_BUS_SCSI:		/* SCSI */
			h = GetDlgItem(hdlg, IDT_1723);
			ShowWindow(h, SW_SHOW);
			EnableWindow(h, TRUE);
			h = GetDlgItem(hdlg, IDT_1724);
			ShowWindow(h, SW_SHOW);
			EnableWindow(h, TRUE);

			if (assign_id)
				next_free_scsi_id((uint8_t *) is_add_dlg ? &new_hdd.scsi_id : &temp_hdd[lv1_current_sel].scsi_id);

			h = GetDlgItem(hdlg, IDC_COMBO_HD_ID);
			ShowWindow(h, SW_SHOW);
			EnableWindow(h, TRUE);
			SendMessage(h, CB_SETCURSEL, is_add_dlg ? new_hdd.scsi_id : temp_hdd[lv1_current_sel].scsi_id, 0);
	}
    }

    if ((hd_listview_items == 0) && !is_add_dlg) {
	h = GetDlgItem(hdlg, IDT_1721);
	EnableWindow(h, FALSE);
	ShowWindow(h, SW_HIDE);

	h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
	EnableWindow(h, FALSE);		ShowWindow(h, SW_HIDE);
    } else {
	h = GetDlgItem(hdlg, IDT_1721);
	ShowWindow(h, SW_SHOW);
	EnableWindow(h, TRUE);

	h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
	ShowWindow(h, SW_SHOW);
	EnableWindow(h, TRUE);
    }
}


static int
bus_full(uint64_t *tracking, int count)
{
    int full = 0;
    switch(count) {
	case 2:
	default:
		full = (*tracking & 0xFF00LL);
		full = full && (*tracking & 0x00FFLL);
		return full;
	case 8:
		full = (*tracking & 0xFF00000000000000LL);
		full = full && (*tracking & 0x00FF000000000000LL);
		full = full && (*tracking & 0x0000FF0000000000LL);
		full = full && (*tracking & 0x000000FF00000000LL);
		full = full && (*tracking & 0x00000000FF000000LL);
		full = full && (*tracking & 0x0000000000FF0000LL);
		full = full && (*tracking & 0x000000000000FF00LL);
		full = full && (*tracking & 0x00000000000000FFLL);
		return full;
    }
}


static void
recalc_next_free_id(HWND hdlg)
{
    HWND h;
    int i, enable_add = 0;
    int c_mfm = 0, c_esdi = 0;
    int c_xta = 0, c_ide = 0;
    int c_scsi = 0;

    next_free_id = -1;

    for (i = 0; i < HDD_NUM; i++) {
	if (temp_hdd[i].bus == HDD_BUS_MFM)
		c_mfm++;
	else if (temp_hdd[i].bus == HDD_BUS_ESDI)
		c_esdi++;
	else if (temp_hdd[i].bus == HDD_BUS_XTA)
		c_xta++;
	else if (temp_hdd[i].bus == HDD_BUS_IDE)
		c_ide++;
	else if (temp_hdd[i].bus == HDD_BUS_SCSI)
		c_scsi++;
    }

    for (i = 0; i < HDD_NUM; i++) {
	if (temp_hdd[i].bus == HDD_BUS_DISABLED) {
		next_free_id = i;
		break;
	}
    }

    enable_add = enable_add || (next_free_id >= 0);
    enable_add = enable_add && ((c_mfm < MFM_NUM) || (c_esdi < ESDI_NUM) || (c_xta < XTA_NUM) ||
				(c_ide < IDE_NUM) || (c_scsi < SCSI_NUM));
    enable_add = enable_add && !bus_full(&mfm_tracking, 2);
    enable_add = enable_add && !bus_full(&esdi_tracking, 2);
    enable_add = enable_add && !bus_full(&xta_tracking, 2);
    enable_add = enable_add && !bus_full(&ide_tracking, 8);
    for (i = 0; i < 2; i++)
	enable_add = enable_add && !bus_full(&(scsi_tracking[i]), 8);

    h = GetDlgItem(hdlg, IDC_BUTTON_HDD_ADD_NEW);
    EnableWindow(h, enable_add ? TRUE : FALSE);

    h = GetDlgItem(hdlg, IDC_BUTTON_HDD_ADD);
    EnableWindow(h, enable_add ? TRUE : FALSE);

    h = GetDlgItem(hdlg, IDC_BUTTON_HDD_REMOVE);
    EnableWindow(h, ((c_mfm == 0) && (c_esdi == 0) && (c_xta == 0) && (c_ide == 0) && (c_scsi == 0)) ?
		 FALSE : TRUE);
}


static void
win_settings_hard_disks_update_item(HWND hwndList, int i, int column)
{
    LVITEM lvI;
    WCHAR szText[256];

    lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
    lvI.stateMask = lvI.iSubItem = lvI.state = 0;

    lvI.iSubItem = column;
    lvI.iItem = i;

    if (column == 0) {
	switch(temp_hdd[i].bus) {
		case HDD_BUS_MFM:
			wsprintf(szText, plat_get_string(IDS_4608), temp_hdd[i].mfm_channel >> 1, temp_hdd[i].mfm_channel & 1);
			break;
		case HDD_BUS_XTA:
			wsprintf(szText, plat_get_string(IDS_4609), temp_hdd[i].xta_channel >> 1, temp_hdd[i].xta_channel & 1);
			break;
		case HDD_BUS_ESDI:
			wsprintf(szText, plat_get_string(IDS_4610), temp_hdd[i].esdi_channel >> 1, temp_hdd[i].esdi_channel & 1);
			break;
		case HDD_BUS_IDE:
			wsprintf(szText, plat_get_string(IDS_4611), temp_hdd[i].ide_channel >> 1, temp_hdd[i].ide_channel & 1);
			break;
		case HDD_BUS_SCSI:
			wsprintf(szText, plat_get_string(IDS_4612), temp_hdd[i].scsi_id);
			break;
	}
	lvI.pszText = szText;
	lvI.iImage = 0;
    } else if (column == 1) {
	lvI.pszText = temp_hdd[i].fn;
	lvI.iImage = 0;
    } else if (column == 2) {
	wsprintf(szText, plat_get_string(IDS_4098), temp_hdd[i].tracks);
	lvI.pszText = szText;
	lvI.iImage = 0;
    } else if (column == 3) {
	wsprintf(szText, plat_get_string(IDS_4098), temp_hdd[i].hpc);
	lvI.pszText = szText;
	lvI.iImage = 0;
    } else if (column == 4) {
	wsprintf(szText, plat_get_string(IDS_4098), temp_hdd[i].spt);
	lvI.pszText = szText;
	lvI.iImage = 0;
    } else if (column == 5) {
	wsprintf(szText, plat_get_string(IDS_4098), (temp_hdd[i].tracks * temp_hdd[i].hpc * temp_hdd[i].spt) >> 11);
	lvI.pszText = szText;
	lvI.iImage = 0;
    }

    if (ListView_SetItem(hwndList, &lvI) == -1)
	return;
}


static BOOL
win_settings_hard_disks_recalc_list(HWND hwndList)
{
    LVITEM lvI;
    int i, j = 0;
    WCHAR szText[256];

    hd_listview_items = 0;
    lv1_current_sel = -1;

    ListView_DeleteAllItems(hwndList);

    lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
    lvI.stateMask = lvI.iSubItem = lvI.state = 0;

    for (i = 0; i < HDD_NUM; i++) {
	if (temp_hdd[i].bus > 0) {
		hdc_id_to_listview_index[i] = j;
		lvI.iSubItem = 0;
		switch(temp_hdd[i].bus) {
			case HDD_BUS_MFM:
				wsprintf(szText, plat_get_string(IDS_4608), temp_hdd[i].mfm_channel >> 1, temp_hdd[i].mfm_channel & 1);
				break;
			case HDD_BUS_XTA:
				wsprintf(szText, plat_get_string(IDS_4609), temp_hdd[i].xta_channel >> 1, temp_hdd[i].xta_channel & 1);
				break;
			case HDD_BUS_ESDI:
				wsprintf(szText, plat_get_string(IDS_4610), temp_hdd[i].esdi_channel >> 1, temp_hdd[i].esdi_channel & 1);
				break;
			case HDD_BUS_IDE:
				wsprintf(szText, plat_get_string(IDS_4611), temp_hdd[i].ide_channel >> 1, temp_hdd[i].ide_channel & 1);
				break;
			case HDD_BUS_SCSI:
				wsprintf(szText, plat_get_string(IDS_4612), temp_hdd[i].scsi_id);
				break;
		}
		lvI.pszText = szText;
		lvI.iItem = j;
		lvI.iImage = 0;

		if (ListView_InsertItem(hwndList, &lvI) == -1)
			return FALSE;

		lvI.iSubItem = 1;
		lvI.pszText = temp_hdd[i].fn;
		lvI.iItem = j;
		lvI.iImage = 0;

		if (ListView_SetItem(hwndList, &lvI) == -1)
			return FALSE;

		lvI.iSubItem = 2;
		wsprintf(szText, plat_get_string(IDS_4098), temp_hdd[i].tracks);
		lvI.pszText = szText;
		lvI.iItem = j;
		lvI.iImage = 0;

		if (ListView_SetItem(hwndList, &lvI) == -1)
			return FALSE;

		lvI.iSubItem = 3;
		wsprintf(szText, plat_get_string(IDS_4098), temp_hdd[i].hpc);
		lvI.pszText = szText;
		lvI.iItem = j;
		lvI.iImage = 0;

		if (ListView_SetItem(hwndList, &lvI) == -1)
			return FALSE;

		lvI.iSubItem = 4;
		wsprintf(szText, plat_get_string(IDS_4098), temp_hdd[i].spt);
		lvI.pszText = szText;
		lvI.iItem = j;
		lvI.iImage = 0;

		if (ListView_SetItem(hwndList, &lvI) == -1)
			return FALSE;

		lvI.iSubItem = 5;
		wsprintf(szText, plat_get_string(IDS_4098), (temp_hdd[i].tracks * temp_hdd[i].hpc * temp_hdd[i].spt) >> 11);
		lvI.pszText = szText;
		lvI.iItem = j;
		lvI.iImage = 0;

		if (ListView_SetItem(hwndList, &lvI) == -1)
			return FALSE;

		j++;
	} else
		hdc_id_to_listview_index[i] = -1;
    }

    hd_listview_items = j;

    return TRUE;
}


static BOOL
win_settings_hard_disks_init_columns(HWND hwndList)
{
    LVCOLUMN lvc;
    int iCol;

    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;

    for (iCol = 0; iCol < C_COLUMNS_HARD_DISKS; iCol++) {
	lvc.iSubItem = iCol;
	lvc.pszText = plat_get_string(IDS_2082 + iCol);

	switch(iCol) {
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
		return FALSE;
    }

    return TRUE;
}


static void
get_edit_box_contents(HWND hdlg, int id, uint32_t *val)
{
    HWND h;
    WCHAR szText[256];
    char stransi[256];

    h = GetDlgItem(hdlg, id);
    SendMessage(h, WM_GETTEXT, 255, (LPARAM) szText);
    wcstombs(stransi, szText, 256);
    sscanf(stransi, "%u", val);
}


static void
get_combo_box_selection(HWND hdlg, int id, uint32_t *val)
{
    HWND h;

    h = GetDlgItem(hdlg, id);
    *val = SendMessage(h, CB_GETCURSEL, 0, 0);
}


static void
set_edit_box_contents(HWND hdlg, int id, uint32_t val)
{
    HWND h;
    WCHAR szText[256];

    h = GetDlgItem(hdlg, id);
    wsprintf(szText, plat_get_string(IDS_2115), val);
    SendMessage(h, WM_SETTEXT, (WPARAM) wcslen(szText), (LPARAM) szText);
}


static int hdconf_initialize_hdt_combo(HWND hdlg)
{
    HWND h;
    int i = 0;
    uint64_t temp_size = 0;
    uint32_t size_mb = 0;
    WCHAR szText[256];

    selection = 127;

    h = GetDlgItem(hdlg, IDC_COMBO_HD_TYPE);
    for (i = 0; i < 127; i++) {	
	temp_size = hdd_table[i][0] * hdd_table[i][1] * hdd_table[i][2];
	size_mb = (uint32_t) (temp_size >> 11LL);
	wsprintf(szText, plat_get_string(IDS_2116), size_mb, hdd_table[i][0], hdd_table[i][1], hdd_table[i][2]);
	SendMessage(h, CB_ADDSTRING, 0, (LPARAM) szText);
	if ((tracks == (int) hdd_table[i][0]) && (hpc == (int) hdd_table[i][1]) &&
	    (spt == (int) hdd_table[i][2]))
		selection = i;
    }
    SendMessage(h, CB_ADDSTRING, 0, win_get_string(IDS_4100));
    SendMessage(h, CB_ADDSTRING, 0, win_get_string(IDS_4101));
    SendMessage(h, CB_SETCURSEL, selection, 0);
    return selection;
}


static void
recalc_selection(HWND hdlg)
{
    HWND h;
    int i = 0;

    selection = 127;
    h = GetDlgItem(hdlg, IDC_COMBO_HD_TYPE);
    for (i = 0; i < 127; i++) {	
	if ((tracks == (int) hdd_table[i][0]) &&
	    (hpc == (int) hdd_table[i][1]) &&
	    (spt == (int) hdd_table[i][2]))
		selection = i;
    }
    if ((selection == 127) && (hpc == 16) && (spt == 63))
	selection = 128;
    SendMessage(h, CB_SETCURSEL, selection, 0);
}


#ifdef __amd64__
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_hard_disks_add_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND h;
    FILE *f;
    uint32_t temp, i = 0, sector_size = 512;
    uint32_t zero = 0, base = 0x1000;
    uint64_t signature = 0xD778A82044445459ll;
    uint64_t temp_size, r = 0;
    char buf[512], *big_buf;
    int b = 0;
    uint8_t channel = 0;
    uint8_t id = 0;
    wchar_t *twcs;
    vhd_footer_t *vft = NULL;

    switch (message) {
	case WM_INITDIALOG:
		memset(hd_file_name, 0, sizeof(hd_file_name));

		hdd_ptr = &(temp_hdd[next_free_id]);

		SetWindowText(hdlg, plat_get_string((existing & 1) ? IDS_4103 : IDS_4102));

		no_update = 1;
		spt = (existing & 1) ? 0 : 17;
		set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, spt);
		hpc = (existing & 1) ? 0 : 15;
		set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, hpc);
		tracks = (existing & 1) ? 0 : 1023;
		set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, tracks);
		size = (tracks * hpc * spt) << 9;
		set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20LL));
		hdconf_initialize_hdt_combo(hdlg);
		if (existing & 1) {
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
		} else
			chs_enabled = 1;
		add_locations(hdlg);
		h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
		hdd_ptr->bus = HDD_BUS_IDE;
		max_spt = 63;
		max_hpc = 255;
		SendMessage(h, CB_SETCURSEL, hdd_ptr->bus, 0);
		max_tracks = 266305;
		recalc_location_controls(hdlg, 1, 0);

		channel = next_free_ide_channel();
		next_free_scsi_id(&id);
		h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
		SendMessage(h, CB_SETCURSEL, 0, 0);
		h = GetDlgItem(hdlg, IDC_COMBO_HD_ID);
		SendMessage(h, CB_SETCURSEL, id, 0);
		h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL_IDE);
		SendMessage(h, CB_SETCURSEL, channel, 0);

		new_hdd.mfm_channel = next_free_binary_channel(&mfm_tracking);
		new_hdd.esdi_channel = next_free_binary_channel(&esdi_tracking);
		new_hdd.xta_channel = next_free_binary_channel(&xta_tracking);
		new_hdd.ide_channel = channel;
		new_hdd.scsi_id = id;

		h = GetDlgItem(hdlg, IDC_EDIT_HD_FILE_NAME);
		EnableWindow(h, FALSE);

		h = GetDlgItem(hdlg, IDT_1752);
		EnableWindow(h, FALSE);
		ShowWindow(h, SW_HIDE);

		h = GetDlgItem(hdlg, IDC_PBAR_IMG_CREATE);
		EnableWindow(h, FALSE);
		ShowWindow(h, SW_HIDE);

		no_update = 0;
		return TRUE;

	case WM_COMMAND:
               	switch (LOWORD(wParam)) {
			case IDOK:
				h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
				hdd_ptr->bus = SendMessage(h, CB_GETCURSEL, 0, 0) + 1;

				/* Make sure no file name is allowed with removable SCSI hard disks. */
				if (wcslen(hd_file_name) == 0) {
					hdd_ptr->bus = HDD_BUS_DISABLED;
					settings_msgbox(MBX_ERROR, (wchar_t *)IDS_4112);
					return TRUE;
				}

				get_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, &(hdd_ptr->spt));
				get_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, &(hdd_ptr->hpc));
				get_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, &(hdd_ptr->tracks));
				spt = hdd_ptr->spt;
				hpc = hdd_ptr->hpc;
				tracks = hdd_ptr->tracks;

				switch(hdd_ptr->bus) {
					case HDD_BUS_MFM:
						h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
						hdd_ptr->mfm_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
						break;
					case HDD_BUS_ESDI:
						h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
						hdd_ptr->esdi_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
						break;
					case HDD_BUS_XTA:
						h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
						hdd_ptr->xta_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
						break;
					case HDD_BUS_IDE:
						h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL_IDE);
						hdd_ptr->ide_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
						break;
					case HDD_BUS_SCSI:
						h = GetDlgItem(hdlg, IDC_COMBO_HD_ID);
						hdd_ptr->scsi_id = SendMessage(h, CB_GETCURSEL, 0, 0);
						break;
				}

				memset(hdd_ptr->fn, 0, sizeof(hdd_ptr->fn));
				wcscpy(hdd_ptr->fn, hd_file_name);

				sector_size = 512;

				if (!(existing & 1) && (wcslen(hd_file_name) > 0)) {
					f = _wfopen(hd_file_name, L"wb");

					if (size > 0x1FFFFFFE00ll) {
						fclose(f);
						settings_msgbox(MBX_ERROR, (wchar_t *)IDS_4105);
						return TRUE;							
					}

					if (image_is_hdi(hd_file_name)) {
						if (size >= 0x100000000ll) {
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
							fwrite(&zero, 1, 4, f);
					} else if (image_is_hdx(hd_file_name, 0)) {
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

					temp_size = size;

					r = size >> 20;
					size &= 0xfffff;

					if (size || r) {
						h = GetDlgItem(hdlg, IDT_1731);
						EnableWindow(h, FALSE);
						ShowWindow(h, SW_HIDE);

						h = GetDlgItem(hdlg, IDC_EDIT_HD_FILE_NAME);
						EnableWindow(h, FALSE);
						ShowWindow(h, SW_HIDE);

						h = GetDlgItem(hdlg, IDC_CFILE);
						EnableWindow(h, FALSE);
						ShowWindow(h, SW_HIDE);

						h = GetDlgItem(hdlg, IDC_PBAR_IMG_CREATE);
						EnableWindow(h, TRUE);
						ShowWindow(h, SW_SHOW);
						SendMessage(h, PBM_SETRANGE32, (WPARAM) 0, (LPARAM) r);
						SendMessage(h, PBM_SETPOS, (WPARAM) 0, (LPARAM) 0);

						h = GetDlgItem(hdlg, IDT_1752);
						EnableWindow(h, TRUE);
						ShowWindow(h, SW_SHOW);
					}

					if (size) {
						fwrite(buf, 1, size, f);
						SendMessage(h, PBM_SETPOS, (WPARAM) 1, (LPARAM) 0);
					}

					if (r) {
						big_buf = (char *) malloc(1048576);
						memset(big_buf, 0, 1048576);
						for (i = 0; i < r; i++) {
							fwrite(big_buf, 1, 1048576, f);
							SendMessage(h, PBM_SETPOS, (WPARAM) (size + 1), (LPARAM) 0);
						}
						free(big_buf);
					}

					if (image_is_vhd(hd_file_name, 0)) {
						/* VHD image. */
						/* Generate new footer. */
						new_vhd_footer(&vft);
						vft->orig_size = vft->curr_size = temp_size;
						vft->geom.cyl = tracks;
						vft->geom.heads = hpc;
						vft->geom.spt = spt;
						generate_vhd_checksum(vft);
						memset(buf, 0, 512);
						vhd_footer_to_bytes((uint8_t *) buf, vft);
						fwrite(buf, 1, 512, f);
						memset(buf, 0, 512);
						free(vft);
						vft = NULL;
					}

					fclose(f);
					settings_msgbox(MBX_INFO, (wchar_t *)IDS_4113);	                        
				}

				hard_disk_added = 1;
				EndDialog(hdlg, 0);
				return TRUE;

			case IDCANCEL:
				hard_disk_added = 0;
				hdd_ptr->bus = HDD_BUS_DISABLED;
				EndDialog(hdlg, 0);
				return TRUE;

			case IDC_CFILE:
	                        if (!file_dlg_w(hdlg, plat_get_string(IDS_4106), L"", !(existing & 1))) {
					if (!wcschr(wopenfilestring, L'.')) {
						if (wcslen(wopenfilestring) && (wcslen(wopenfilestring) <= 256)) {
							twcs = &wopenfilestring[wcslen(wopenfilestring)];
							twcs[0] = L'.';
							twcs[1] = L'i';
							twcs[2] = L'm';
							twcs[3] = L'g';
						}
					}

					if (!(existing & 1)) {
						f = _wfopen(wopenfilestring, L"rb");
						if (f != NULL) {
							fclose(f);
							if (settings_msgbox(MBX_QUESTION, (wchar_t *)IDS_4111) != 0)	/* yes */
								return FALSE;
						}
					}

					f = _wfopen(wopenfilestring, (existing & 1) ? L"rb" : L"wb");
					if (f == NULL) {
hdd_add_file_open_error:
						settings_msgbox(MBX_ERROR, (existing & 1) ? (wchar_t *)IDS_4107 : (wchar_t *)IDS_4108);
						return TRUE;
					}
					if (existing & 1) {
						if (image_is_hdi(wopenfilestring) || image_is_hdx(wopenfilestring, 1)) {
							fseeko64(f, 0x10, SEEK_SET);
							fread(&sector_size, 1, 4, f);
							if (sector_size != 512) {
								settings_msgbox(MBX_ERROR, (wchar_t *)IDS_4109);
								fclose(f);
								return TRUE;
							}
							spt = hpc = tracks = 0;
							fread(&spt, 1, 4, f);
							fread(&hpc, 1, 4, f);
							fread(&tracks, 1, 4, f);
						} else if (image_is_vhd(wopenfilestring, 1)) {
							fseeko64(f, -512, SEEK_END);
							fread(buf, 1, 512, f);
							new_vhd_footer(&vft);
							vhd_footer_from_bytes(vft, (uint8_t *) buf);
							size = vft->orig_size;
							tracks = vft->geom.cyl;
							hpc = vft->geom.heads;
							spt = vft->geom.spt;
							free(vft);
							vft = NULL;
						} else {
							fseeko64(f, 0, SEEK_END);
							size = ftello64(f);
							fclose(f);
							if (((size % 17) == 0) && (size <= 142606336)) {
								spt = 17;
								if (size <= 26738688)
									hpc = 4;
								else if (((size % 3072) == 0) && (size <= 53477376))
									hpc = 6;
								else {
									for (i = 5; i < 16; i++) {
										if (((size % (i << 9)) == 0) && (size <= ((i * 17) << 19)))
											break;
										if (i == 5)
											i++;
									}
									hpc = i;
								}
							} else {
								spt = 63;
								hpc = 16;
							}

							tracks = ((size >> 9) / hpc) / spt;
						}

						if ((spt > max_spt) || (hpc > max_hpc) || (tracks > max_tracks))
								goto hdd_add_file_open_error;
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
					} else
						fclose(f);
				}

				h = GetDlgItem(hdlg, IDC_EDIT_HD_FILE_NAME);
				SendMessage(h, WM_SETTEXT, 0, (LPARAM) wopenfilestring);
				memset(hd_file_name, 0, sizeof(hd_file_name));
				wcscpy(hd_file_name, wopenfilestring);

				return TRUE;

			case IDC_EDIT_HD_CYL:
				if (no_update)
					return FALSE;

				no_update = 1;
				get_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, &temp);
				if (tracks != (int64_t) temp) {
					tracks = temp;
					size = (tracks * hpc * spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				if (tracks > max_tracks) {
					tracks = max_tracks;
					size = (tracks * hpc * spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, tracks);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				no_update = 0;
				break;

			case IDC_EDIT_HD_HPC:
				if (no_update)
					return FALSE;

				no_update = 1;
				get_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, &temp);
				if (hpc != (int64_t) temp) {
					hpc = temp;
					size = (tracks * hpc * spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				if (hpc > max_hpc) {
					hpc = max_hpc;
					size = (tracks * hpc * spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, hpc);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				no_update = 0;
				break;

			case IDC_EDIT_HD_SPT:
				if (no_update)
					return FALSE;

				no_update = 1;
				get_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, &temp);
				if (spt != (int64_t) temp) {
					spt = temp;
					size = (tracks * hpc * spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				if (spt > max_spt) {
					spt = max_spt;
					size = (tracks * hpc * spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, spt);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				no_update = 0;
				break;

			case IDC_EDIT_HD_SIZE:
				if (no_update)
					return FALSE;

				no_update = 1;
				get_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, &temp);
				if (temp != (uint32_t) (size >> 20)) {
					size = ((uint64_t) temp) << 20LL;
					/* This is needed to ensure VHD standard compliance. */
					hdd_image_calc_chs((uint32_t *) &tracks, (uint32_t *) &hpc, (uint32_t *) &spt, temp);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, tracks);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, hpc);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, spt);
					recalc_selection(hdlg);
				}

				if (tracks > max_tracks) {
					tracks = max_tracks;
					size = (tracks * hpc * spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, tracks);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				if (hpc > max_hpc) {
					hpc = max_hpc;
					size = (tracks * hpc * spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, hpc);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				if (spt > max_spt) {
					spt = max_spt;
					size = (tracks * hpc * spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, spt);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				no_update = 0;
				break;

			case IDC_COMBO_HD_TYPE:
				if (no_update)
					return FALSE;

				no_update = 1;
				get_combo_box_selection(hdlg, IDC_COMBO_HD_TYPE, &temp);
				if ((temp != selection) && (temp != 127) && (temp != 128)) {
					selection = temp;
					tracks = hdd_table[selection][0];
					hpc = hdd_table[selection][1];
					spt = hdd_table[selection][2];
					size = (tracks * hpc * spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, tracks);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, hpc);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, spt);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
				} else if ((temp != selection) && (temp == 127))
					selection = temp;
				else if ((temp != selection) && (temp == 128)) {
					selection = temp;
					hpc = 16;
					spt = 63;
					size = (tracks * hpc * spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, hpc);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, spt);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
				}

				if (spt > max_spt) {
					spt = max_spt;
					size = (tracks * hpc * spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, spt);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				if (hpc > max_hpc) {
					hpc = max_hpc;
					size = (tracks * hpc * spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, hpc);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				if (tracks > max_tracks) {
					tracks = max_tracks;
					size = (tracks * hpc * spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, tracks);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				no_update = 0;
				break;

			case IDC_COMBO_HD_BUS:
				if (no_update)
					return FALSE;

				no_update = 1;
				recalc_location_controls(hdlg, 1, 0);
				h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
				b = SendMessage(h,CB_GETCURSEL,0,0) + 1;
				if (b == hdd_ptr->bus)
					goto hd_add_bus_skip;

				hdd_ptr->bus = b;

				switch(hdd_ptr->bus) {
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
					case HDD_BUS_XTA:
						max_spt = 63;
						max_hpc = 16;
						max_tracks = 1023;
						break;
					case HDD_BUS_IDE:
						max_spt = 63;
						max_hpc = 255;
						max_tracks = 266305;
						break;
					case HDD_BUS_SCSI:
						max_spt = 99;
						max_hpc = 255;
						max_tracks = 266305;
						break;
				}

				if (!chs_enabled) {
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

				if (spt > max_spt) {
					spt = max_spt;
					size = (tracks * hpc * spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, spt);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				if (hpc > max_hpc) {
					hpc = max_hpc;
					size = (tracks * hpc * spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, hpc);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				if (tracks > max_tracks) {
					tracks = max_tracks;
					size = (tracks * hpc * spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, tracks);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
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


int
hard_disk_was_added(void)
{
    return hard_disk_added;
}


void
hard_disk_add_open(HWND hwnd, int is_existing)
{
    existing = is_existing;
    hard_disk_added = 0;
    DialogBox(hinstance, (LPCWSTR)DLG_CFG_HARD_DISKS_ADD, hwnd, win_settings_hard_disks_add_proc);
}


static void
hard_disk_track(uint8_t id)
{
    switch(temp_hdd[id].bus) {
	case HDD_BUS_MFM:
		mfm_tracking |= (1 << (temp_hdd[id].mfm_channel << 3));
		break;
	case HDD_BUS_ESDI:
		esdi_tracking |= (1 << (temp_hdd[id].esdi_channel << 3));
		break;
	case HDD_BUS_XTA:
		xta_tracking |= (1 << (temp_hdd[id].xta_channel << 3));
		break;
	case HDD_BUS_IDE:
		ide_tracking |= (1 << (temp_hdd[id].ide_channel << 3));
		break;
	case HDD_BUS_SCSI:
		scsi_tracking[temp_hdd[id].scsi_id >> 3] |= (1 << ((temp_hdd[id].scsi_id & 0x07) << 3));
		break;
    }
}


static void
hard_disk_untrack(uint8_t id)
{
    switch(temp_hdd[id].bus) {
	case HDD_BUS_MFM:
		mfm_tracking &= ~(1 << (temp_hdd[id].mfm_channel << 3));
		break;
	case HDD_BUS_ESDI:
		esdi_tracking &= ~(1 << (temp_hdd[id].esdi_channel << 3));
		break;
	case HDD_BUS_XTA:
		xta_tracking &= ~(1 << (temp_hdd[id].xta_channel << 3));
		break;
	case HDD_BUS_IDE:
		ide_tracking &= ~(1 << (temp_hdd[id].ide_channel << 3));
		break;
	case HDD_BUS_SCSI:
		scsi_tracking[temp_hdd[id].scsi_id >> 3] &= ~(1 << ((temp_hdd[id].scsi_id & 0x07) << 3));
		break;
    }
}


static void
hard_disk_track_all(void)
{
    int i;

    for (i = 0; i < HDD_NUM; i++)
	hard_disk_track(i);
}


#ifdef __amd64__
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_hard_disks_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND h;
    int old_sel = 0, b = 0, assign = 0;
    const uint8_t hd_icons[2] = { 64, 0 };

    switch (message) {
	case WM_INITDIALOG:
		ignore_change = 1;

		normalize_hd_list();	/* Normalize the hard disks so that non-disabled hard disks start from index 0, and so they are contiguous.
					   This will cause an emulator reset prompt on the first opening of this category with a messy hard disk list
					   (which can only happen by manually editing the configuration file). */
		h = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
		win_settings_hard_disks_init_columns(h);
		image_list_init(h, (const uint8_t *) hd_icons);
		win_settings_hard_disks_recalc_list(h);
		recalc_next_free_id(hdlg);
		add_locations(hdlg);
		if (hd_listview_items > 0) {
			ListView_SetItemState(h, 0, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
			lv1_current_sel = 0;
			h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
			SendMessage(h, CB_SETCURSEL, temp_hdd[0].bus - 1, 0);
		} else
			lv1_current_sel = -1;
		recalc_location_controls(hdlg, 0, 0);

		ignore_change = 0;
		return TRUE;

	case WM_NOTIFY:
		if ((hd_listview_items == 0) || ignore_change)
			return FALSE;

		if ((((LPNMHDR)lParam)->code == LVN_ITEMCHANGED) && (((LPNMHDR)lParam)->idFrom == IDC_LIST_HARD_DISKS)) {
			old_sel = lv1_current_sel;
			lv1_current_sel = get_selected_hard_disk(hdlg);
			if (lv1_current_sel == old_sel)
				return FALSE;
			else if (lv1_current_sel == -1) {
				ignore_change = 1;
				lv1_current_sel = old_sel;
				ListView_SetItemState(h, lv1_current_sel, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
				ignore_change = 0;
				return FALSE;
			}
			ignore_change = 1;
			h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
			SendMessage(h, CB_SETCURSEL, temp_hdd[lv1_current_sel].bus - 1, 0);
			recalc_location_controls(hdlg, 0, 0);
			ignore_change = 0;
		}
		break;

	case WM_COMMAND:
		if (ignore_change && (LOWORD(wParam) != IDC_BUTTON_HDD_ADD) &&
		    (LOWORD(wParam) != IDC_BUTTON_HDD_ADD_NEW) && (LOWORD(wParam) != IDC_BUTTON_HDD_REMOVE))
			return FALSE;
               	switch (LOWORD(wParam)) {
			case IDC_COMBO_HD_BUS:
				ignore_change = 1;
				h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
				b = SendMessage(h, CB_GETCURSEL, 0, 0) + 1;
				if (b == temp_hdd[lv1_current_sel].bus)
					goto hd_bus_skip;
				hard_disk_untrack(lv1_current_sel);
				assign = (temp_hdd[lv1_current_sel].bus == b) ? 0 : 1;
				temp_hdd[lv1_current_sel].bus = b;
				recalc_location_controls(hdlg, 0, assign);
				hard_disk_track(lv1_current_sel);
				h = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
				win_settings_hard_disks_update_item(h, lv1_current_sel, 0);
hd_bus_skip:
				ignore_change = 0;
				return FALSE;

			case IDC_COMBO_HD_CHANNEL:
				ignore_change = 1;
				h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL);
				hard_disk_untrack(lv1_current_sel);
				if (temp_hdd[lv1_current_sel].bus == HDD_BUS_MFM)
					temp_hdd[lv1_current_sel].mfm_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
				else if (temp_hdd[lv1_current_sel].bus == HDD_BUS_ESDI)
					temp_hdd[lv1_current_sel].esdi_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
				else if (temp_hdd[lv1_current_sel].bus == HDD_BUS_XTA)
					temp_hdd[lv1_current_sel].xta_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
				hard_disk_track(lv1_current_sel);
				h = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
				win_settings_hard_disks_update_item(h, lv1_current_sel, 0);
				ignore_change = 0;
				return FALSE;

			case IDC_COMBO_HD_CHANNEL_IDE:
				ignore_change = 1;
				h = GetDlgItem(hdlg, IDC_COMBO_HD_CHANNEL_IDE);
				hard_disk_untrack(lv1_current_sel);
				temp_hdd[lv1_current_sel].ide_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
				hard_disk_track(lv1_current_sel);
				h = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
				win_settings_hard_disks_update_item(h, lv1_current_sel, 0);
				ignore_change = 0;
				return FALSE;

			case IDC_COMBO_HD_ID:
				ignore_change = 1;
				h = GetDlgItem(hdlg, IDC_COMBO_HD_ID);
				hard_disk_untrack(lv1_current_sel);
				temp_hdd[lv1_current_sel].scsi_id = SendMessage(h, CB_GETCURSEL, 0, 0);
				hard_disk_track(lv1_current_sel);
				h = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
				win_settings_hard_disks_update_item(h, lv1_current_sel, 0);
				ignore_change = 0;
				return FALSE;

			case IDC_BUTTON_HDD_ADD:
				hard_disk_add_open(hdlg, 1);
				if (hard_disk_added) {
					ignore_change = 1;
					h = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
					win_settings_hard_disks_recalc_list(h);
					recalc_next_free_id(hdlg);
					hard_disk_track_all();
					ignore_change = 0;
				}
				return FALSE;

			case IDC_BUTTON_HDD_ADD_NEW:
				hard_disk_add_open(hdlg, 0);
				if (hard_disk_added) {
					ignore_change = 1;
					h = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
					win_settings_hard_disks_recalc_list(h);
					recalc_next_free_id(hdlg);
					hard_disk_track_all();
					ignore_change = 0;
				}
				return FALSE;

			case IDC_BUTTON_HDD_REMOVE:
				memcpy(temp_hdd[lv1_current_sel].fn, L"", 4);
				hard_disk_untrack(lv1_current_sel);
				temp_hdd[lv1_current_sel].bus = HDD_BUS_DISABLED;	/* Only set the bus to zero, the list normalize code below will take care of turning this entire entry to a complete zero. */
				normalize_hd_list();			/* Normalize the hard disks so that non-disabled hard disks start from index 0, and so they are contiguous. */
				ignore_change = 1;
				h = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
				win_settings_hard_disks_recalc_list(h);
				recalc_next_free_id(hdlg);
				if (hd_listview_items > 0) {
					ListView_SetItemState(h, 0, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
					lv1_current_sel = 0;
					h = GetDlgItem(hdlg, IDC_COMBO_HD_BUS);
					SendMessage(h, CB_SETCURSEL, temp_hdd[0].bus - 1, 0);
				} else
					lv1_current_sel = -1;
				recalc_location_controls(hdlg, 0, 0);
				ignore_change = 0;
				return FALSE;
		}

	default:
		return FALSE;
    }

    return FALSE;
}


static int
combo_id_to_string_id(int combo_id)
{
    return IDS_5376 + combo_id;
}


static int
combo_id_to_format_string_id(int combo_id)
{
    return IDS_5632 + combo_id;
}


static BOOL
win_settings_floppy_drives_recalc_list(HWND hwndList)
{
    LVITEM lvI;
    int i = 0;
    char s[256];
    WCHAR szText[256];

    lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
    lvI.stateMask = lvI.state = 0;

    for (i = 0; i < 4; i++) {
	lvI.iSubItem = 0;
	if (temp_fdd_types[i] > 0) {
		strcpy(s, fdd_getname(temp_fdd_types[i]));
		mbstowcs(szText, s, strlen(s) + 1);
		lvI.pszText = szText;
	} else
		lvI.pszText = plat_get_string(IDS_5376);
	lvI.iItem = i;
	lvI.iImage = temp_fdd_types[i];

	if (ListView_InsertItem(hwndList, &lvI) == -1)
		return FALSE;

	lvI.iSubItem = 1;
	lvI.pszText = plat_get_string(temp_fdd_turbo[i] ? IDS_2060 : IDS_2061);
	lvI.iItem = i;
	lvI.iImage = 0;

	if (ListView_SetItem(hwndList, &lvI) == -1)
		return FALSE;

	lvI.iSubItem = 2;
	lvI.pszText = plat_get_string(temp_fdd_check_bpb[i] ? IDS_2060 : IDS_2061);
	lvI.iItem = i;
	lvI.iImage = 0;

	if (ListView_SetItem(hwndList, &lvI) == -1)
		return FALSE;
    }

    return TRUE;
}


static BOOL
win_settings_cdrom_drives_recalc_list(HWND hwndList)
{
    LVITEM lvI;
    int i = 0, fsid = 0;
    WCHAR szText[256];

    lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
   lvI.stateMask = lvI.iSubItem = lvI.state = 0;

    for (i = 0; i < 4; i++) {
	fsid = combo_id_to_format_string_id(temp_cdrom_drives[i].bus_type);

	lvI.iSubItem = 0;
	switch (temp_cdrom_drives[i].bus_type) {
		case CDROM_BUS_DISABLED:
		default:
			lvI.pszText = plat_get_string(fsid);
			lvI.iImage = 0;
			break;
		case CDROM_BUS_ATAPI:
			wsprintf(szText, plat_get_string(fsid), temp_cdrom_drives[i].ide_channel >> 1, temp_cdrom_drives[i].ide_channel & 1);
			lvI.pszText = szText;
			lvI.iImage = 1;
			break;
		case CDROM_BUS_SCSI:
			wsprintf(szText, plat_get_string(fsid), temp_cdrom_drives[i].scsi_device_id);
			lvI.pszText = szText;
			lvI.iImage = 1;
			break;
	}

	lvI.iItem = i;

	if (ListView_InsertItem(hwndList, &lvI) == -1)
		return FALSE;

	lvI.iSubItem = 1;
	if (temp_cdrom_drives[i].bus_type == CDROM_BUS_DISABLED)
		lvI.pszText = plat_get_string(IDS_2112);
	else {
		wsprintf(szText, L"%ix", temp_cdrom_drives[i].speed);
		lvI.pszText = szText;
	}
	lvI.iItem = i;
	lvI.iImage = 0;

	if (ListView_SetItem(hwndList, &lvI) == -1)
		return FALSE;
    }

    return TRUE;
}


static BOOL
win_settings_zip_drives_recalc_list(HWND hwndList)
{
    LVITEM lvI;
    int i = 0, fsid = 0;
    WCHAR szText[256];

    lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
    lvI.stateMask = lvI.iSubItem = lvI.state = 0;

    for (i = 0; i < 4; i++) {
	fsid = combo_id_to_format_string_id(temp_zip_drives[i].bus_type);

	lvI.iSubItem = 0;
	switch (temp_zip_drives[i].bus_type) {
		case ZIP_BUS_DISABLED:
		default:
			lvI.pszText = plat_get_string(fsid);
			lvI.iImage = 0;
			break;
		case ZIP_BUS_ATAPI:
			wsprintf(szText, plat_get_string(fsid), temp_zip_drives[i].ide_channel >> 1, temp_zip_drives[i].ide_channel & 1);
			lvI.pszText = szText;
			lvI.iImage = 1;
			break;
		case ZIP_BUS_SCSI:
			wsprintf(szText, plat_get_string(fsid), temp_zip_drives[i].scsi_device_id);
			lvI.pszText = szText;
			lvI.iImage = 1;
			break;
	}

	lvI.iItem = i;

	if (ListView_InsertItem(hwndList, &lvI) == -1)
		return FALSE;

	lvI.iSubItem = 1;
	lvI.pszText = plat_get_string(temp_zip_drives[i].is_250 ? IDS_5901 : IDS_5900);
	lvI.iItem = i;
	lvI.iImage = 0;

	if (ListView_SetItem(hwndList, &lvI) == -1)
		return FALSE;
    }

    return TRUE;
}


static BOOL
win_settings_floppy_drives_init_columns(HWND hwndList)
{
    LVCOLUMN lvc;

    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;

    lvc.iSubItem = 0;
    lvc.pszText = plat_get_string(IDS_2101);

    lvc.cx = 292;
    lvc.fmt = LVCFMT_LEFT;

    if (ListView_InsertColumn(hwndList, 0, &lvc) == -1)
	return FALSE;

    lvc.iSubItem = 1;
    lvc.pszText = plat_get_string(IDS_2059);

    lvc.cx = 50;
    lvc.fmt = LVCFMT_LEFT;

    if (ListView_InsertColumn(hwndList, 1, &lvc) == -1)
	return FALSE;

    lvc.iSubItem = 2;
    lvc.pszText = plat_get_string(IDS_2088);

    lvc.cx = 75;
    lvc.fmt = LVCFMT_LEFT;

    if (ListView_InsertColumn(hwndList, 2, &lvc) == -1)
	return FALSE;

    return TRUE;
}


static BOOL
win_settings_cdrom_drives_init_columns(HWND hwndList)
{
    LVCOLUMN lvc;

    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;

    lvc.iSubItem = 0;
    lvc.pszText = plat_get_string(IDS_2082);

    lvc.cx = 342;
    lvc.fmt = LVCFMT_LEFT;

    if (ListView_InsertColumn(hwndList, 0, &lvc) == -1)
	return FALSE;

    lvc.iSubItem = 1;
    lvc.pszText = plat_get_string(IDS_2053);

    lvc.cx = 50;
    lvc.fmt = LVCFMT_LEFT;

    if (ListView_InsertColumn(hwndList, 1, &lvc) == -1)
	return FALSE;

    return TRUE;
}


static BOOL
win_settings_zip_drives_init_columns(HWND hwndList)
{
    LVCOLUMN lvc;

    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;

    lvc.iSubItem = 0;
    lvc.pszText = plat_get_string(IDS_2082);

    lvc.cx = 342;
    lvc.fmt = LVCFMT_LEFT;

    if (ListView_InsertColumn(hwndList, 0, &lvc) == -1)
	return FALSE;

    lvc.iSubItem = 1;
    lvc.pszText = plat_get_string(IDS_2101);

    lvc.cx = 50;
    lvc.fmt = LVCFMT_LEFT;

    if (ListView_InsertColumn(hwndList, 1, &lvc) == -1)
	return FALSE;

    return TRUE;
}


static int
get_selected_drive(HWND hdlg, int id)
{
    int drive = -1;
    int i, j = 0;
    HWND h;

    for (i = 0; i < 4; i++) {
	h = GetDlgItem(hdlg, id);
	j = ListView_GetItemState(h, i, LVIS_SELECTED);
	if (j)
		drive = i;
    }

    return drive;
}


static void
win_settings_floppy_drives_update_item(HWND hwndList, int i)
{
    LVITEM lvI;
    char s[256];
    WCHAR szText[256];

    lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
    lvI.stateMask = lvI.iSubItem = lvI.state = 0;

    lvI.iSubItem = 0;
    lvI.iItem = i;

    if (temp_fdd_types[i] > 0) {
	strcpy(s, fdd_getname(temp_fdd_types[i]));
	mbstowcs(szText, s, strlen(s) + 1);
	lvI.pszText = szText;
    } else
	lvI.pszText = plat_get_string(IDS_5376);
    lvI.iImage = temp_fdd_types[i];

    if (ListView_SetItem(hwndList, &lvI) == -1)
	return;

    lvI.iSubItem = 1;
    lvI.pszText = plat_get_string(temp_fdd_turbo[i] ? IDS_2060 : IDS_2061);
    lvI.iItem = i;
    lvI.iImage = 0;

    if (ListView_SetItem(hwndList, &lvI) == -1)
	return;

    lvI.iSubItem = 2;
    lvI.pszText = plat_get_string(temp_fdd_check_bpb[i] ? IDS_2060 : IDS_2061);
    lvI.iItem = i;
    lvI.iImage = 0;

    if (ListView_SetItem(hwndList, &lvI) == -1)
	return;
}


static void
win_settings_cdrom_drives_update_item(HWND hwndList, int i)
{
    LVITEM lvI;
    WCHAR szText[256];
    int fsid;

    lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
    lvI.stateMask = lvI.iSubItem = lvI.state = 0;

    lvI.iSubItem = 0;
    lvI.iItem = i;

    fsid = combo_id_to_format_string_id(temp_cdrom_drives[i].bus_type);

    switch (temp_cdrom_drives[i].bus_type) {
	case CDROM_BUS_DISABLED:
	default:
		lvI.pszText = plat_get_string(fsid);
		lvI.iImage = 0;
		break;
	case CDROM_BUS_ATAPI:
		wsprintf(szText, plat_get_string(fsid), temp_cdrom_drives[i].ide_channel >> 1, temp_cdrom_drives[i].ide_channel & 1);
		lvI.pszText = szText;
		lvI.iImage = 1;
		break;
	case CDROM_BUS_SCSI:
		wsprintf(szText, plat_get_string(fsid), temp_cdrom_drives[i].scsi_device_id);
		lvI.pszText = szText;
		lvI.iImage = 1;
		break;
    }

    if (ListView_SetItem(hwndList, &lvI) == -1)
	return;

    lvI.iSubItem = 1;
    if (temp_cdrom_drives[i].bus_type == CDROM_BUS_DISABLED)
	lvI.pszText = plat_get_string(IDS_2112);
    else {
	wsprintf(szText, L"%ix", temp_cdrom_drives[i].speed);
	lvI.pszText = szText;
    }
    lvI.iItem = i;
    lvI.iImage = 0;

    if (ListView_SetItem(hwndList, &lvI) == -1)
	return;
}


static void
win_settings_zip_drives_update_item(HWND hwndList, int i)
{
    LVITEM lvI;
    WCHAR szText[256];
    int fsid;

    lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
    lvI.stateMask = lvI.iSubItem = lvI.state = 0;

    lvI.iSubItem = 0;
    lvI.iItem = i;

    fsid = combo_id_to_format_string_id(temp_zip_drives[i].bus_type);

    switch (temp_zip_drives[i].bus_type) {
	case ZIP_BUS_DISABLED:
	default:
		lvI.pszText = plat_get_string(fsid);
		lvI.iImage = 0;
		break;
	case ZIP_BUS_ATAPI:
		wsprintf(szText, plat_get_string(fsid), temp_zip_drives[i].ide_channel >> 1, temp_zip_drives[i].ide_channel & 1);
		lvI.pszText = szText;
		lvI.iImage = 1;
		break;
	case ZIP_BUS_SCSI:
		wsprintf(szText, plat_get_string(fsid), temp_zip_drives[i].scsi_device_id);
		lvI.pszText = szText;
		lvI.iImage = 1;
		break;
    }

    if (ListView_SetItem(hwndList, &lvI) == -1)
	return;

    lvI.iSubItem = 1;
    lvI.pszText = plat_get_string(temp_zip_drives[i].is_250 ? IDS_5901 : IDS_5900);
    lvI.iItem = i;
    lvI.iImage = 0;

    if (ListView_SetItem(hwndList, &lvI) == -1)
	return;
}


static void
cdrom_add_locations(HWND hdlg)
{
    LPTSTR lptsTemp;
    HWND h;
    int i = 0;

    lptsTemp = (LPTSTR) malloc(512);

    h = GetDlgItem(hdlg, IDC_COMBO_CD_BUS);
    for (i = CDROM_BUS_DISABLED; i <= CDROM_BUS_SCSI; i++) {
	if ((i == CDROM_BUS_DISABLED) || (i >= CDROM_BUS_ATAPI))
		SendMessage(h, CB_ADDSTRING, 0, win_get_string(combo_id_to_string_id(i)));
    }

    h = GetDlgItem(hdlg, IDC_COMBO_CD_SPEED);
    for (i = 1; i <= 72; i++) {
	wsprintf(lptsTemp, L"%ix", i);
	SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
    }

    h = GetDlgItem(hdlg, IDC_COMBO_CD_ID);
    for (i = 0; i < 16; i++) {
	wsprintf(lptsTemp, plat_get_string(IDS_4098), i);
	SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
    }

    h = GetDlgItem(hdlg, IDC_COMBO_CD_CHANNEL_IDE);
    for (i = 0; i < 8; i++) {
	wsprintf(lptsTemp, plat_get_string(IDS_4097), i >> 1, i & 1);
	SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
    }

    free(lptsTemp);
}


static void cdrom_recalc_location_controls(HWND hdlg, int assign_id)
{
    int i = 0;
    HWND h;

    int bus = temp_cdrom_drives[lv1_current_sel].bus_type;

    for (i = IDT_1741; i < (IDT_1742 + 1); i++) {
	h = GetDlgItem(hdlg, i);
	EnableWindow(h, FALSE);
	ShowWindow(h, SW_HIDE);
    }

    h = GetDlgItem(hdlg, IDC_COMBO_CD_ID);
    EnableWindow(h, FALSE);
    ShowWindow(h, SW_HIDE);

    h = GetDlgItem(hdlg, IDC_COMBO_CD_CHANNEL_IDE);
    EnableWindow(h, FALSE);
    ShowWindow(h, SW_HIDE);

    h = GetDlgItem(hdlg, IDC_COMBO_CD_SPEED);
    if (bus == CDROM_BUS_DISABLED) {
	EnableWindow(h, FALSE);
	ShowWindow(h, SW_HIDE);
    } else {
	ShowWindow(h, SW_SHOW);
	EnableWindow(h, TRUE);
	SendMessage(h, CB_SETCURSEL, temp_cdrom_drives[lv1_current_sel].speed - 1, 0);
    }

    h = GetDlgItem(hdlg, IDT_1758);
    if (bus == CDROM_BUS_DISABLED) {
	EnableWindow(h, FALSE);
	ShowWindow(h, SW_HIDE);
    } else {
	ShowWindow(h, SW_SHOW);
	EnableWindow(h, TRUE);
    }

    switch(bus) {
	case CDROM_BUS_ATAPI:		/* ATAPI */
		h = GetDlgItem(hdlg, IDT_1742);
		ShowWindow(h, SW_SHOW);
		EnableWindow(h, TRUE);

		if (assign_id)
			temp_cdrom_drives[lv1_current_sel].ide_channel = next_free_ide_channel();

		h = GetDlgItem(hdlg, IDC_COMBO_CD_CHANNEL_IDE);
		ShowWindow(h, SW_SHOW);
		EnableWindow(h, TRUE);
		SendMessage(h, CB_SETCURSEL, temp_cdrom_drives[lv1_current_sel].ide_channel, 0);
		break;
	case CDROM_BUS_SCSI:		/* SCSI */
		h = GetDlgItem(hdlg, IDT_1741);
		ShowWindow(h, SW_SHOW);
		EnableWindow(h, TRUE);

		if (assign_id)
			next_free_scsi_id((uint8_t *) &temp_cdrom_drives[lv1_current_sel].scsi_device_id);

		h = GetDlgItem(hdlg, IDC_COMBO_CD_ID);
		ShowWindow(h, SW_SHOW);
		EnableWindow(h, TRUE);
		SendMessage(h, CB_SETCURSEL, temp_cdrom_drives[lv1_current_sel].scsi_device_id, 0);
		break;
    }
}


static void
zip_add_locations(HWND hdlg)
{
    LPTSTR lptsTemp;
    HWND h;
    int i = 0;

    lptsTemp = (LPTSTR) malloc(512);

    h = GetDlgItem(hdlg, IDC_COMBO_ZIP_BUS);
    for (i = ZIP_BUS_DISABLED; i <= ZIP_BUS_SCSI; i++) {
	if ((i == ZIP_BUS_DISABLED) || (i >= ZIP_BUS_ATAPI))
		SendMessage(h, CB_ADDSTRING, 0, win_get_string(combo_id_to_string_id(i)));
    }

    h = GetDlgItem(hdlg, IDC_COMBO_ZIP_ID);
    for (i = 0; i < 16; i++) {
	wsprintf(lptsTemp, plat_get_string(IDS_4098), i);
	SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
    }

    h = GetDlgItem(hdlg, IDC_COMBO_ZIP_CHANNEL_IDE);
    for (i = 0; i < 8; i++) {
	wsprintf(lptsTemp, plat_get_string(IDS_4097), i >> 1, i & 1);
	SendMessage(h, CB_ADDSTRING, 0, (LPARAM) lptsTemp);
    }

    free(lptsTemp);
}


static void
zip_recalc_location_controls(HWND hdlg, int assign_id)
{
    int i = 0;
    HWND h;

    int bus = temp_zip_drives[lv2_current_sel].bus_type;

    for (i = IDT_1754; i < (IDT_1755 + 1); i++) {
	h = GetDlgItem(hdlg, i);
	EnableWindow(h, FALSE);
	ShowWindow(h, SW_HIDE);
    }

    h = GetDlgItem(hdlg, IDC_COMBO_ZIP_ID);
    EnableWindow(h, FALSE);
    ShowWindow(h, SW_HIDE);

    h = GetDlgItem(hdlg, IDC_COMBO_ZIP_CHANNEL_IDE);
    EnableWindow(h, FALSE);
    ShowWindow(h, SW_HIDE);

    h = GetDlgItem(hdlg, IDC_CHECK250);
    if (bus == ZIP_BUS_DISABLED) {
	EnableWindow(h, FALSE);
	ShowWindow(h, SW_HIDE);
    } else {
	ShowWindow(h, SW_SHOW);
	EnableWindow(h, TRUE);
	SendMessage(h, BM_SETCHECK, temp_zip_drives[lv2_current_sel].is_250, 0);
    }

    switch(bus) {
	case ZIP_BUS_ATAPI:		/* ATAPI */
		h = GetDlgItem(hdlg, IDT_1755);
		ShowWindow(h, SW_SHOW);
		EnableWindow(h, TRUE);

		if (assign_id)
			temp_zip_drives[lv2_current_sel].ide_channel = next_free_ide_channel();

		h = GetDlgItem(hdlg, IDC_COMBO_ZIP_CHANNEL_IDE);
		ShowWindow(h, SW_SHOW);
		EnableWindow(h, TRUE);
		SendMessage(h, CB_SETCURSEL, temp_zip_drives[lv2_current_sel].ide_channel, 0);
		break;
	case ZIP_BUS_SCSI:		/* SCSI */
		h = GetDlgItem(hdlg, IDT_1754);
		ShowWindow(h, SW_SHOW);
		EnableWindow(h, TRUE);

		if (assign_id)
			next_free_scsi_id((uint8_t *) &temp_zip_drives[lv2_current_sel].scsi_device_id);

		h = GetDlgItem(hdlg, IDC_COMBO_ZIP_ID);
		ShowWindow(h, SW_SHOW);
		EnableWindow(h, TRUE);
		SendMessage(h, CB_SETCURSEL, temp_zip_drives[lv2_current_sel].scsi_device_id, 0);
		break;
    }
}


static void
cdrom_track(uint8_t id)
{
    if (temp_cdrom_drives[id].bus_type == CDROM_BUS_ATAPI)
	ide_tracking |= (2 << (temp_cdrom_drives[id].ide_channel << 3));
    else if (temp_cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
	scsi_tracking[temp_cdrom_drives[id].scsi_device_id >> 3] |= (1 << (temp_cdrom_drives[id].scsi_device_id & 0x07));
}


static void
cdrom_untrack(uint8_t id)
{
    if (temp_cdrom_drives[id].bus_type == CDROM_BUS_ATAPI)
	ide_tracking &= ~(2 << (temp_cdrom_drives[id].ide_channel << 3));
    else if (temp_cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
	scsi_tracking[temp_cdrom_drives[id].scsi_device_id >> 3] &= ~(1 << (temp_cdrom_drives[id].scsi_device_id & 0x07));
}


static void
zip_track(uint8_t id)
{
    if (temp_zip_drives[id].bus_type == ZIP_BUS_ATAPI)
	ide_tracking |= (1 << temp_zip_drives[id].ide_channel);
    else if (temp_zip_drives[id].bus_type == ZIP_BUS_SCSI)
	scsi_tracking[temp_zip_drives[id].scsi_device_id >> 3] |= (1 << (temp_zip_drives[id].scsi_device_id & 0x07));
}


static void
zip_untrack(uint8_t id)
{
    if (temp_zip_drives[id].bus_type == ZIP_BUS_ATAPI)
	ide_tracking &= ~(1 << temp_zip_drives[id].ide_channel);
    else if (temp_zip_drives[id].bus_type == ZIP_BUS_SCSI)
	scsi_tracking[temp_zip_drives[id].scsi_device_id >> 3] &= ~(1 << (temp_zip_drives[id].scsi_device_id & 0x07));
}


#ifdef __amd64__
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_floppy_drives_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND h;
    int i = 0, old_sel = 0;
    WCHAR szText[256];
    const uint8_t fd_icons[15] = { 248, 16, 16, 16, 16, 16, 16, 24, 24, 24, 24, 24, 24, 24, 0 };

    switch (message) {
	case WM_INITDIALOG:
		ignore_change = 1;

		lv1_current_sel = 0;
		h = GetDlgItem(hdlg, IDC_LIST_FLOPPY_DRIVES);
		win_settings_floppy_drives_init_columns(h);
		image_list_init(h, (const uint8_t *) fd_icons);
		win_settings_floppy_drives_recalc_list(h);
		ListView_SetItemState(h, 0, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
		h = GetDlgItem(hdlg, IDC_COMBO_FD_TYPE);
		for (i = 0; i < 14; i++) {
			if (i == 0)
				SendMessage(h, CB_ADDSTRING, 0, win_get_string(IDS_5376));
			else {
				mbstowcs(szText, fdd_getname(i), strlen(fdd_getname(i)) + 1);
				SendMessage(h, CB_ADDSTRING, 0, (LPARAM) szText);
			}
		}
		SendMessage(h, CB_SETCURSEL, temp_fdd_types[lv1_current_sel], 0);

		h = GetDlgItem(hdlg, IDC_CHECKTURBO);
		SendMessage(h, BM_SETCHECK, temp_fdd_turbo[lv1_current_sel], 0);

		h = GetDlgItem(hdlg, IDC_CHECKBPB);
		SendMessage(h, BM_SETCHECK, temp_fdd_check_bpb[lv1_current_sel], 0);

		ignore_change = 0;
		return TRUE;

	case WM_NOTIFY:
		if (ignore_change)
			return FALSE;

		if ((((LPNMHDR)lParam)->code == LVN_ITEMCHANGED) && (((LPNMHDR)lParam)->idFrom == IDC_LIST_FLOPPY_DRIVES)) {
			old_sel = lv1_current_sel;
			lv1_current_sel = get_selected_drive(hdlg, IDC_LIST_FLOPPY_DRIVES);
			if (lv1_current_sel == old_sel)
				return FALSE;
			else if (lv1_current_sel == -1) {
				ignore_change = 1;
				lv1_current_sel = old_sel;
				ListView_SetItemState(h, lv1_current_sel, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
				ignore_change = 0;
				return FALSE;
			}
			ignore_change = 1;
			h = GetDlgItem(hdlg, IDC_COMBO_FD_TYPE);
			SendMessage(h, CB_SETCURSEL, temp_fdd_types[lv1_current_sel], 0);
			h = GetDlgItem(hdlg, IDC_CHECKTURBO);
			SendMessage(h, BM_SETCHECK, temp_fdd_turbo[lv1_current_sel], 0);
			h = GetDlgItem(hdlg, IDC_CHECKBPB);
			SendMessage(h, BM_SETCHECK, temp_fdd_check_bpb[lv1_current_sel], 0);
			ignore_change = 0;
		}
		break;

	case WM_COMMAND:
		if (ignore_change)
			return FALSE;

		ignore_change = 1;
               	switch (LOWORD(wParam)) {
			case IDC_COMBO_FD_TYPE:
				h = GetDlgItem(hdlg, IDC_COMBO_FD_TYPE);
				temp_fdd_types[lv1_current_sel] = SendMessage(h, CB_GETCURSEL, 0, 0);
				h = GetDlgItem(hdlg, IDC_LIST_FLOPPY_DRIVES);
				win_settings_floppy_drives_update_item(h, lv1_current_sel);
				break;

			case IDC_CHECKTURBO:
				h = GetDlgItem(hdlg, IDC_CHECKTURBO);
				temp_fdd_turbo[lv1_current_sel] = SendMessage(h, BM_GETCHECK, 0, 0);
				h = GetDlgItem(hdlg, IDC_LIST_FLOPPY_DRIVES);
				win_settings_floppy_drives_update_item(h, lv1_current_sel);
				break;

			case IDC_CHECKBPB:
				h = GetDlgItem(hdlg, IDC_CHECKBPB);
				temp_fdd_check_bpb[lv1_current_sel] = SendMessage(h, BM_GETCHECK, 0, 0);
				h = GetDlgItem(hdlg, IDC_LIST_FLOPPY_DRIVES);
				win_settings_floppy_drives_update_item(h, lv1_current_sel);
				break;
		}
		ignore_change = 0;

	default:
		return FALSE;
    }

    return FALSE;
}


#ifdef __amd64__
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_other_removable_devices_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND h;
    int old_sel = 0, b = 0, assign = 0;
    uint32_t b2 = 0;
    const uint8_t cd_icons[3] = { 249, 32, 0 };
    const uint8_t zip_icons[3] = { 250, 48, 0 };

    switch (message) {
	case WM_INITDIALOG:
		ignore_change = 1;

		lv1_current_sel = 0;
		h = GetDlgItem(hdlg, IDC_LIST_CDROM_DRIVES);
		win_settings_cdrom_drives_init_columns(h);
		image_list_init(h, (const uint8_t *) cd_icons);
		win_settings_cdrom_drives_recalc_list(h);
		ListView_SetItemState(h, 0, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
		cdrom_add_locations(hdlg);

		h = GetDlgItem(hdlg, IDC_COMBO_CD_BUS);

		switch (temp_cdrom_drives[lv1_current_sel].bus_type) {
			case CDROM_BUS_DISABLED:
			default:
				b = 0;
				break;
			case CDROM_BUS_ATAPI:
				b = 1;
				break;
			case CDROM_BUS_SCSI:
				b = 2;
				break;
		}

		SendMessage(h, CB_SETCURSEL, b, 0);

		cdrom_recalc_location_controls(hdlg, 0);

		lv2_current_sel = 0;
		h = GetDlgItem(hdlg, IDC_LIST_ZIP_DRIVES);
		win_settings_zip_drives_init_columns(h);
		image_list_init(h, (const uint8_t *) zip_icons);
		win_settings_zip_drives_recalc_list(h);
		ListView_SetItemState(h, 0, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
		zip_add_locations(hdlg);

		h = GetDlgItem(hdlg, IDC_COMBO_ZIP_BUS);

		switch (temp_zip_drives[lv2_current_sel].bus_type) {
			case ZIP_BUS_DISABLED:
			default:
				b = 0;
				break;
			case ZIP_BUS_ATAPI:
				b = 1;
				break;
			case ZIP_BUS_SCSI:
				b = 2;
				break;
		}

		SendMessage(h, CB_SETCURSEL, b, 0);

		zip_recalc_location_controls(hdlg, 0);

		ignore_change = 0;
		return TRUE;

	case WM_NOTIFY:
		if (ignore_change)
			return FALSE;

		if ((((LPNMHDR)lParam)->code == LVN_ITEMCHANGED) && (((LPNMHDR)lParam)->idFrom == IDC_LIST_CDROM_DRIVES)) {
			old_sel = lv1_current_sel;
			lv1_current_sel = get_selected_drive(hdlg, IDC_LIST_CDROM_DRIVES);
			if (lv1_current_sel == old_sel)
				return FALSE;
			else if (lv1_current_sel == -1) {
				ignore_change = 1;
				lv1_current_sel = old_sel;
				ListView_SetItemState(h, lv1_current_sel, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
				ignore_change = 0;
				return FALSE;
			}
			ignore_change = 1;

			h = GetDlgItem(hdlg, IDC_COMBO_CD_BUS);

			switch (temp_cdrom_drives[lv1_current_sel].bus_type) {
				case CDROM_BUS_DISABLED:
				default:
					b = 0;
					break;
				case CDROM_BUS_ATAPI:
					b = 1;
					break;
				case CDROM_BUS_SCSI:
					b = 2;
					break;
			}

			SendMessage(h, CB_SETCURSEL, b, 0);

			cdrom_recalc_location_controls(hdlg, 0);
			ignore_change = 0;
		} else if ((((LPNMHDR)lParam)->code == LVN_ITEMCHANGED) && (((LPNMHDR)lParam)->idFrom == IDC_LIST_ZIP_DRIVES)) {
			old_sel = lv2_current_sel;
			lv2_current_sel = get_selected_drive(hdlg, IDC_LIST_ZIP_DRIVES);
			if (lv2_current_sel == old_sel)
				return FALSE;
			else if (lv2_current_sel == -1) {
				ignore_change = 1;
				lv2_current_sel = old_sel;
				ListView_SetItemState(h, lv2_current_sel, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
				ignore_change = 0;
				return FALSE;
			}
			ignore_change = 1;

			h = GetDlgItem(hdlg, IDC_COMBO_ZIP_BUS);

			switch (temp_zip_drives[lv2_current_sel].bus_type) {
				case ZIP_BUS_DISABLED:
				default:
					b = 0;
					break;
				case ZIP_BUS_ATAPI:
					b = 1;
					break;
				case ZIP_BUS_SCSI:
					b = 2;
					break;
			}

			SendMessage(h, CB_SETCURSEL, b, 0);

			zip_recalc_location_controls(hdlg, 0);

			ignore_change = 0;
		}
		break;

	case WM_COMMAND:
		if (ignore_change)
			return FALSE;

		ignore_change = 1;
               	switch (LOWORD(wParam)) {
			case IDC_COMBO_CD_BUS:
				h = GetDlgItem(hdlg, IDC_COMBO_CD_BUS);
				b = SendMessage(h, CB_GETCURSEL, 0, 0);
				switch (b) {
					case 0:
						b2 = CDROM_BUS_DISABLED;
						break;
					case 1:
						b2 = CDROM_BUS_ATAPI;
						break;
					case 2:
						b2 = CDROM_BUS_SCSI;
						break;
				}
				if (b2 == temp_cdrom_drives[lv1_current_sel].bus_type)
					break;
				cdrom_untrack(lv1_current_sel);
				assign = (temp_cdrom_drives[lv1_current_sel].bus_type == b2) ? 0 : 1;
				if (temp_cdrom_drives[lv1_current_sel].bus_type == CDROM_BUS_DISABLED)
					temp_cdrom_drives[lv1_current_sel].speed = 8;
				temp_cdrom_drives[lv1_current_sel].bus_type = b2;
				cdrom_recalc_location_controls(hdlg, assign);
				cdrom_track(lv1_current_sel);
				h = GetDlgItem(hdlg, IDC_LIST_CDROM_DRIVES);
				win_settings_cdrom_drives_update_item(h, lv1_current_sel);
				break;

			case IDC_COMBO_CD_ID:
				h = GetDlgItem(hdlg, IDC_COMBO_CD_ID);
				cdrom_untrack(lv1_current_sel);
				temp_cdrom_drives[lv1_current_sel].scsi_device_id = SendMessage(h, CB_GETCURSEL, 0, 0);
				cdrom_track(lv1_current_sel);
				h = GetDlgItem(hdlg, IDC_LIST_CDROM_DRIVES);
				win_settings_cdrom_drives_update_item(h, lv1_current_sel);
				break;

			case IDC_COMBO_CD_CHANNEL_IDE:
				h = GetDlgItem(hdlg, IDC_COMBO_CD_CHANNEL_IDE);
				cdrom_untrack(lv1_current_sel);
				temp_cdrom_drives[lv1_current_sel].ide_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
				cdrom_track(lv1_current_sel);
				h = GetDlgItem(hdlg, IDC_LIST_CDROM_DRIVES);
				win_settings_cdrom_drives_update_item(h, lv1_current_sel);
				break;

			case IDC_COMBO_CD_SPEED:
				h = GetDlgItem(hdlg, IDC_COMBO_CD_SPEED);
				temp_cdrom_drives[lv1_current_sel].speed = SendMessage(h, CB_GETCURSEL, 0, 0) + 1;
				h = GetDlgItem(hdlg, IDC_LIST_CDROM_DRIVES);
				win_settings_cdrom_drives_update_item(h, lv1_current_sel);
				break;

			case IDC_COMBO_ZIP_BUS:
				h = GetDlgItem(hdlg, IDC_COMBO_ZIP_BUS);
				b = SendMessage(h, CB_GETCURSEL, 0, 0);
				switch (b) {
					case 0:
						b2 = ZIP_BUS_DISABLED;
						break;
					case 1:
						b2 = ZIP_BUS_ATAPI;
						break;
					case 2:
						b2 = ZIP_BUS_SCSI;
						break;
				}
				if (b2 == temp_zip_drives[lv2_current_sel].bus_type)
					break;
				zip_untrack(lv2_current_sel);
				assign = (temp_zip_drives[lv2_current_sel].bus_type == b2) ? 0 : 1;
				temp_zip_drives[lv2_current_sel].bus_type = b2;
				zip_recalc_location_controls(hdlg, assign);
				zip_track(lv2_current_sel);
				h = GetDlgItem(hdlg, IDC_LIST_ZIP_DRIVES);
				win_settings_zip_drives_update_item(h, lv2_current_sel);
				break;

			case IDC_COMBO_ZIP_ID:
				h = GetDlgItem(hdlg, IDC_COMBO_ZIP_ID);
				zip_untrack(lv2_current_sel);
				temp_zip_drives[lv2_current_sel].scsi_device_id = SendMessage(h, CB_GETCURSEL, 0, 0);
				zip_track(lv2_current_sel);
				h = GetDlgItem(hdlg, IDC_LIST_ZIP_DRIVES);
				win_settings_zip_drives_update_item(h, lv2_current_sel);
				break;

			case IDC_COMBO_ZIP_CHANNEL_IDE:
				h = GetDlgItem(hdlg, IDC_COMBO_ZIP_CHANNEL_IDE);
				zip_untrack(lv2_current_sel);
				temp_zip_drives[lv2_current_sel].ide_channel = SendMessage(h, CB_GETCURSEL, 0, 0);
				zip_track(lv2_current_sel);
				h = GetDlgItem(hdlg, IDC_LIST_ZIP_DRIVES);
				win_settings_zip_drives_update_item(h, lv2_current_sel);
				break;

			case IDC_CHECK250:
				h = GetDlgItem(hdlg, IDC_CHECK250);
				temp_zip_drives[lv2_current_sel].is_250 = SendMessage(h, BM_GETCHECK, 0, 0);
				h = GetDlgItem(hdlg, IDC_LIST_ZIP_DRIVES);
				win_settings_zip_drives_update_item(h, lv2_current_sel);
				break;
		}
		ignore_change = 0;

	default:
		return FALSE;
    }

    return FALSE;
}


void win_settings_show_child(HWND hwndParent, DWORD child_id)
{
    if (child_id == displayed_category)
	return;
    else
	displayed_category = child_id;

    SendMessage(hwndChildDialog, WM_SAVESETTINGS, 0, 0);

    DestroyWindow(hwndChildDialog);

    switch(child_id) {
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
	case SETTINGS_PAGE_FLOPPY_DRIVES:
		hwndChildDialog = CreateDialog(hinstance, (LPCWSTR)DLG_CFG_FLOPPY_DRIVES, hwndParent, win_settings_floppy_drives_proc);
		break;
	case SETTINGS_PAGE_OTHER_REMOVABLE_DEVICES:
		hwndChildDialog = CreateDialog(hinstance, (LPCWSTR)DLG_CFG_OTHER_REMOVABLE_DEVICES, hwndParent, win_settings_other_removable_devices_proc);
		break;
	default:
		fatal("Invalid child dialog ID\n");
		return;
    }

    ShowWindow(hwndChildDialog, SW_SHOWNORMAL);
}


static BOOL
win_settings_main_insert_categories(HWND hwndList)
{
    LVITEM lvI;
    int i = 0;

    lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
    lvI.stateMask = lvI.iSubItem = lvI.state = 0;

    for (i = 0; i < 10; i++) {
	lvI.pszText = plat_get_string(IDS_2065+i);
	lvI.iItem = i;
	lvI.iImage = i;

	if (ListView_InsertItem(hwndList, &lvI) == -1)
		return FALSE;
    }

    return TRUE;
}


#ifdef __amd64__
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_main_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND h;
    int category, i = 0, j = 0;
    const uint8_t cat_icons[11] = { 240, 241, 242, 243, 80, 244, 245, 64, 246, 247, 0 };

    hwndParentDialog = hdlg;

    switch (message) {
	case WM_INITDIALOG:
		plat_pause(1);
		win_settings_init();
		displayed_category = -1;
		h = GetDlgItem(hdlg, IDC_SETTINGSCATLIST);
		image_list_init(h, (const uint8_t *) cat_icons);
		win_settings_main_insert_categories(h);
		ListView_SetItemState(h, 0, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
		return TRUE;
	case WM_NOTIFY:
		if ((((LPNMHDR)lParam)->code == LVN_ITEMCHANGED) && (((LPNMHDR)lParam)->idFrom == IDC_SETTINGSCATLIST)) {
			category = -1;
			for (i = 0; i < 10; i++) {
				h = GetDlgItem(hdlg, IDC_SETTINGSCATLIST);
				j = ListView_GetItemState(h, i, LVIS_SELECTED);
				if (j)
					category = i;
			}
			if (category != -1)
				win_settings_show_child(hdlg, category);
		}
		break;
	case WM_COMMAND:
               	switch (LOWORD(wParam)) {
			case IDOK:
				SendMessage(hwndChildDialog, WM_SAVESETTINGS, 0, 0);
				i = settings_msgbox_reset();
				if (i > 0) {
					if (i == 2)
						win_settings_save();

					DestroyWindow(hwndChildDialog);
                                        EndDialog(hdlg, 0);
       	                                plat_pause(0);
                                        return TRUE;
				} else
					return FALSE;
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


void
win_settings_open(HWND hwnd)
{
    DialogBox(hinstance, (LPCWSTR)DLG_CONFIG, hwnd, win_settings_main_proc);
}
