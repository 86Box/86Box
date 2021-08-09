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
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 * 		David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2018,2019 David Hrdlička.
 */
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#include <uxtheme.h>
#undef BITMAP
#ifdef ENABLE_SETTINGS_LOG
#include <assert.h>
#endif
#include <commctrl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/config.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/cassette.h>
#include <86box/nvr.h>
#include <86box/machine.h>
#include <86box/gameport.h>
#include <86box/isamem.h>
#include <86box/isartc.h>
#include <86box/lpt.h>
#include <86box/mouse.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/hdd.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/zip.h>
#include <86box/mo.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/network.h>
#include <86box/sound.h>
#include <86box/midi.h>
#include <86box/snd_mpu401.h>
#include <86box/video.h>
#include <86box/plat.h>
#include <86box/plat_midi.h>
#include <86box/ui.h>
#include <86box/win.h>
#include "../disk/minivhd/minivhd.h"
#include "../disk/minivhd/minivhd_util.h"


/* Icon, Bus, File, C, H, S, Size */
#define C_COLUMNS_HARD_DISKS			6


static int first_cat = 0;
static int dpi = 96;

/* Machine category */
static int temp_machine_type, temp_machine, temp_cpu, temp_wait_states, temp_fpu, temp_sync;
static cpu_family_t *temp_cpu_f;
static uint32_t temp_mem_size;
#ifdef USE_DYNAREC
static int temp_dynarec;
#endif

/* Video category */
static int temp_gfxcard, temp_voodoo;

/* Input devices category */
static int temp_mouse, temp_joystick;

/* Sound category */
static int temp_sound_card, temp_midi_device, temp_midi_input_device, temp_mpu401, temp_SSI2001, temp_GAMEBLASTER, temp_GUS;
static int temp_float;

/* Network category */
static int temp_net_type, temp_net_card;
static char temp_pcap_dev[522];

/* Ports category */
static int temp_lpt_devices[3];
static int temp_serial[4], temp_lpt[3];

/* Other peripherals category */
static int temp_fdc_card, temp_hdc, temp_ide_ter, temp_ide_qua, temp_cassette;
static int temp_scsi_card[SCSI_BUS_MAX];
static int temp_bugger;
static int temp_postcard;
static int temp_isartc;
static int temp_isamem[ISAMEM_MAX];

static uint8_t temp_deviceconfig;

/* Hard disks category */
static hard_disk_t temp_hdd[HDD_NUM];

/* Floppy drives category */
static int temp_fdd_types[FDD_NUM];
static int temp_fdd_turbo[FDD_NUM];
static int temp_fdd_check_bpb[FDD_NUM];

/* Other removable devices category */
static cdrom_t temp_cdrom[CDROM_NUM];
static zip_drive_t temp_zip_drives[ZIP_NUM];
static mo_drive_t temp_mo_drives[MO_NUM];

static HWND hwndParentDialog, hwndChildDialog;

static uint32_t displayed_category = 0;

extern int is486;
static int listtomachinetype[256], listtomachine[256];
static int listtocpufamily[256], listtocpu[256];
static int settings_list_to_device[2][256], settings_list_to_fdc[20];
static int settings_list_to_midi[20], settings_list_to_midi_in[20];
static int settings_list_to_hdc[20];

static int max_spt = 63, max_hpc = 255, max_tracks = 266305;
static uint64_t mfm_tracking, esdi_tracking, xta_tracking, ide_tracking, scsi_tracking[8];
static uint64_t size;
static int hd_listview_items, hdc_id_to_listview_index[HDD_NUM];
static int no_update = 0, existing = 0, chs_enabled = 0;
static int lv1_current_sel, lv2_current_sel;
static int hard_disk_added = 0, next_free_id = 0, selection = 127;
static int spt, hpc, tracks, ignore_change = 0;

static hard_disk_t new_hdd, *hdd_ptr;

static wchar_t hd_file_name[512];
static WCHAR device_name[512];


static int
settings_get_check(HWND hdlg, int id)
{
    return SendMessage(GetDlgItem(hdlg, id), BM_GETCHECK, 0, 0);
}


static int
settings_get_cur_sel(HWND hdlg, int id)
{
    return SendMessage(GetDlgItem(hdlg, id), CB_GETCURSEL, 0, 0);
}


static void
settings_set_check(HWND hdlg, int id, int val)
{
    SendMessage(GetDlgItem(hdlg, id), BM_SETCHECK, val, 0);
}


static void
settings_set_cur_sel(HWND hdlg, int id, int val)
{
    SendMessage(GetDlgItem(hdlg, id), CB_SETCURSEL, val, 0);
}


static void
settings_reset_content(HWND hdlg, int id)
{
    SendMessage(GetDlgItem(hdlg, id), CB_RESETCONTENT, 0, 0);
}


static void
settings_add_string(HWND hdlg, int id, LPARAM string)
{
    SendMessage(GetDlgItem(hdlg, id), CB_ADDSTRING, 0, string);
}


static void
settings_enable_window(HWND hdlg, int id, int condition)
{
    EnableWindow(GetDlgItem(hdlg, id), condition ? TRUE : FALSE);
}


static void
settings_show_window(HWND hdlg, int id, int condition)
{
    HWND h;

    h = GetDlgItem(hdlg, id);
    EnableWindow(h, condition ? TRUE : FALSE);
    ShowWindow(h, condition ? SW_SHOW : SW_HIDE);
}


static void
settings_listview_enable_styles(HWND hdlg, int id)
{
    HWND h;

    h = GetDlgItem(hdlg, id);
    SetWindowTheme(h, L"Explorer", NULL);
    ListView_SetExtendedListViewStyle(h, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
}


static void
settings_listview_select(HWND hdlg, int id, int selection)
{
    HWND h;

    h = GetDlgItem(hdlg, id);
    ListView_SetItemState(h, selection, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
}


static void
settings_process_messages()
{
    MSG msg;
    while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE | PM_NOYIELD)) {
	TranslateMessage(&msg); 
	DispatchMessage(&msg);
    }
}


static BOOL
image_list_init(HWND hdlg, int id, const uint8_t *icon_ids)
{
    HICON hiconItem;
    HIMAGELIST hSmall;
    HWND hwndList = GetDlgItem(hdlg, id);

    int i = 0;

    hSmall = ListView_GetImageList(hwndList, LVSIL_SMALL);
    if (hSmall != 0) ImageList_Destroy(hSmall);

    hSmall = ImageList_Create(win_get_system_metrics(SM_CXSMICON, dpi),
			      win_get_system_metrics(SM_CYSMICON, dpi),
			      ILC_MASK | ILC_COLOR32, 1, 1);

    while(1) {
	if (icon_ids[i] == 0)
		break;

#if defined(__amd64__) || defined(__aarch64__)
	hiconItem = LoadIcon(hinstance, (LPCWSTR) ((uint64_t) icon_ids[i]));
#else
	hiconItem = LoadIcon(hinstance, (LPCWSTR) ((uint32_t) icon_ids[i]));
#endif
	ImageList_AddIcon(hSmall, hiconItem);
	DestroyIcon(hiconItem);

	i++;
    }

    ListView_SetImageList(hwndList, hSmall, LVSIL_SMALL);

    return TRUE;
}


/* Show a MessageBox dialog.  This is nasty, I know.  --FvK */
static int
settings_msgbox_header(int flags, void *header, void *message)
{
    HWND h;
    int i;

    h = hwndMain;
    hwndMain = hwndParentDialog;

    i = ui_msgbox_header(flags, header, message);

    hwndMain = h;

    return(i);
}


static int
settings_msgbox_ex(int flags, void *header, void *message, void *btn1, void *btn2, void *btn3)
{
    HWND h;
    int i;

    h = hwndMain;
    hwndMain = hwndParentDialog;

    i = ui_msgbox_ex(flags, header, message, btn1, btn2, btn3);

    hwndMain = h;

    return(i);
}


/* This does the initial read of global variables into the temporary ones. */
static void
win_settings_init(void)
{
    int i = 0;

    /* Machine category */
    temp_machine_type = machines[machine].type;
    temp_machine = machine;
    temp_cpu_f = cpu_f;
    temp_wait_states = cpu_waitstates;
    temp_cpu = cpu;
    temp_mem_size = mem_size;
#ifdef USE_DYNAREC
    temp_dynarec = cpu_use_dynarec;
#endif
    temp_fpu = fpu_type;
    temp_sync = time_sync;

    /* Video category */
    temp_gfxcard = gfxcard;
    temp_voodoo = voodoo_enabled;

    /* Input devices category */
    temp_mouse = mouse_type;
    temp_joystick = joystick_type;

    /* Sound category */
    temp_sound_card = sound_card_current;
    temp_midi_device = midi_device_current;
    temp_midi_input_device = midi_input_device_current;
    temp_mpu401 = mpu401_standalone_enable;
    temp_SSI2001 = SSI2001;
    temp_GAMEBLASTER = GAMEBLASTER;
    temp_GUS = GUS;
    temp_float = sound_is_float;

    /* Network category */
    temp_net_type = network_type;
    memset(temp_pcap_dev, 0, sizeof(temp_pcap_dev));
#ifdef ENABLE_SETTINGS_LOG
    assert(sizeof(temp_pcap_dev) == sizeof(network_host));
#endif
    memcpy(temp_pcap_dev, network_host, sizeof(network_host));
    temp_net_card = network_card;

    /* Ports category */
    for (i = 0; i < 3; i++) {
	temp_lpt_devices[i] = lpt_ports[i].device;
	temp_lpt[i] = lpt_ports[i].enabled;
    }
    for (i = 0; i < 4; i++)
	temp_serial[i] = serial_enabled[i];

    /* Storage devices category */
    for (i = 0; i < SCSI_BUS_MAX; i++)
	temp_scsi_card[i] = scsi_card_current[i];
    temp_fdc_card = fdc_type;
    temp_hdc = hdc_current;
    temp_ide_ter = ide_ter_enabled;
    temp_ide_qua = ide_qua_enabled;
    temp_cassette = cassette_enable;

    mfm_tracking = xta_tracking = esdi_tracking = ide_tracking = 0;
    for (i = 0; i < 8; i++)
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
	else if ((hdd[i].bus == HDD_BUS_IDE) || (hdd[i].bus == HDD_BUS_ATAPI))
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
    memcpy(temp_cdrom, cdrom, CDROM_NUM * sizeof(cdrom_t));
    for (i = 0; i < CDROM_NUM; i++) {
	if (cdrom[i].bus_type == CDROM_BUS_ATAPI)
		ide_tracking |= (2 << (cdrom[i].ide_channel << 3));
	else if (cdrom[i].bus_type == CDROM_BUS_SCSI)
		scsi_tracking[cdrom[i].scsi_device_id >> 3] |= (1 << ((cdrom[i].scsi_device_id & 0x07) << 3));
    }
    memcpy(temp_zip_drives, zip_drives, ZIP_NUM * sizeof(zip_drive_t));
    for (i = 0; i < ZIP_NUM; i++) {
	if (zip_drives[i].bus_type == ZIP_BUS_ATAPI)
		ide_tracking |= (4 << (zip_drives[i].ide_channel << 3));
	else if (zip_drives[i].bus_type == ZIP_BUS_SCSI)
		scsi_tracking[zip_drives[i].scsi_device_id >> 3] |= (1 << ((zip_drives[i].scsi_device_id & 0x07) << 3));
    }
    memcpy(temp_mo_drives, mo_drives, MO_NUM * sizeof(mo_drive_t));
    for (i = 0; i < MO_NUM; i++) {
    if (mo_drives[i].bus_type == MO_BUS_ATAPI)
	ide_tracking |= (1 << (mo_drives[i].ide_channel << 3));
    else if (mo_drives[i].bus_type == MO_BUS_SCSI)
	scsi_tracking[mo_drives[i].scsi_device_id >> 3] |= (1 << ((mo_drives[i].scsi_device_id & 0x07) << 3));
    }

    /* Other peripherals category */
    temp_bugger = bugger_enabled;
    temp_postcard = postcard_enabled;
    temp_isartc = isartc_type;

    /* ISA memory boards. */
    for (i = 0; i < ISAMEM_MAX; i++)
	temp_isamem[i] = isamem_type[i];	

    temp_deviceconfig = 0;
}


/* This returns 1 if any variable has changed, 0 if not. */
static int
win_settings_changed(void)
{
    int i = 0, j = 0;

    /* Machine category */
    i = i || (machine != temp_machine);
    i = i || (cpu_f != temp_cpu_f);
    i = i || (cpu_waitstates != temp_wait_states);
    i = i || (cpu != temp_cpu);
    i = i || (mem_size != temp_mem_size);
#ifdef USE_DYNAREC
    i = i || (temp_dynarec != cpu_use_dynarec);
#endif
    i = i || (temp_fpu != fpu_type);
    i = i || (temp_sync != time_sync);

    /* Video category */
    i = i || (gfxcard != temp_gfxcard);
    i = i || (voodoo_enabled != temp_voodoo);

    /* Input devices category */
    i = i || (mouse_type != temp_mouse);
    i = i || (joystick_type != temp_joystick);

    /* Sound category */
    i = i || (sound_card_current != temp_sound_card);
    i = i || (midi_device_current != temp_midi_device);
    i = i || (midi_input_device_current != temp_midi_input_device);
    i = i || (mpu401_standalone_enable != temp_mpu401);
    i = i || (SSI2001 != temp_SSI2001);
    i = i || (GAMEBLASTER != temp_GAMEBLASTER);
    i = i || (GUS != temp_GUS);
    i = i || (sound_is_float != temp_float);

    /* Network category */
    i = i || (network_type != temp_net_type);
    i = i || strcmp(temp_pcap_dev, network_host);
    i = i || (network_card != temp_net_card);

    /* Ports category */
    for (j = 0; j < 3; j++) {
	i = i || (temp_lpt_devices[j] != lpt_ports[j].device);
	i = i || (temp_lpt[j] != lpt_ports[j].enabled);
    }
    for (j = 0; j < 4; j++)
	i = i || (temp_serial[j] != serial_enabled[j]);

    /* Storage devices category */
    for (j = 0; j < SCSI_BUS_MAX; j++)
	i = i || (temp_scsi_card[j] != scsi_card_current[j]);
    i = i || (fdc_type != temp_fdc_card);
    i = i || (hdc_current != temp_hdc);
    i = i || (temp_ide_ter != ide_ter_enabled);
    i = i || (temp_ide_qua != ide_qua_enabled);
    i = i || (temp_cassette != cassette_enable);

    /* Hard disks category */
    i = i || memcmp(hdd, temp_hdd, HDD_NUM * sizeof(hard_disk_t));

    /* Floppy drives category */
    for (j = 0; j < FDD_NUM; j++) {
	i = i || (temp_fdd_types[j] != fdd_get_type(j));
	i = i || (temp_fdd_turbo[j] != fdd_get_turbo(j));
	i = i || (temp_fdd_check_bpb[j] != fdd_get_check_bpb(j));
    }

    /* Other removable devices category */
    i = i || memcmp(cdrom, temp_cdrom, CDROM_NUM * sizeof(cdrom_t));
    i = i || memcmp(zip_drives, temp_zip_drives, ZIP_NUM * sizeof(zip_drive_t));
    i = i || memcmp(mo_drives, temp_mo_drives, MO_NUM * sizeof(mo_drive_t));

    /* Other peripherals category */
    i = i || (temp_bugger != bugger_enabled);
    i = i || (temp_postcard != postcard_enabled);
    i = i || (temp_isartc != isartc_type);

    /* ISA memory boards. */
    for (j = 0; j < ISAMEM_MAX; j++)
	i = i || (temp_isamem[j] != isamem_type[j]);

    i = i || !!temp_deviceconfig;

    return i;
}


/* This saves the settings back to the global variables. */
static void
win_settings_save(void)
{
    int i = 0;

    pc_reset_hard_close();

    /* Machine category */
    machine = temp_machine;
    cpu_f = temp_cpu_f;
    cpu_waitstates = temp_wait_states;
    cpu = temp_cpu;
    mem_size = temp_mem_size;
#ifdef USE_DYNAREC
    cpu_use_dynarec = temp_dynarec;
#endif
    fpu_type = temp_fpu;
    time_sync = temp_sync;

    /* Video category */
    gfxcard = temp_gfxcard;
    voodoo_enabled = temp_voodoo;

    /* Input devices category */
    mouse_type = temp_mouse;
    joystick_type = temp_joystick;

    /* Sound category */
    sound_card_current = temp_sound_card;
    midi_device_current = temp_midi_device;
    midi_input_device_current = temp_midi_input_device;
    mpu401_standalone_enable = temp_mpu401;
    SSI2001 = temp_SSI2001;
    GAMEBLASTER = temp_GAMEBLASTER;
    GUS = temp_GUS;
    sound_is_float = temp_float;

    /* Network category */
    network_type = temp_net_type;
    memset(network_host, '\0', sizeof(network_host));
    strcpy(network_host, temp_pcap_dev);
    network_card = temp_net_card;

    /* Ports category */
    for (i = 0; i < 3; i++) {
	lpt_ports[i].device = temp_lpt_devices[i];
	lpt_ports[i].enabled = temp_lpt[i];
    }
    for (i = 0; i < 4; i++)
	serial_enabled[i] = temp_serial[i];

    /* Storage devices category */
    for (i = 0; i < SCSI_BUS_MAX; i++)
	scsi_card_current[i] = temp_scsi_card[i];
    hdc_current = temp_hdc;
    fdc_type = temp_fdc_card;
    ide_ter_enabled = temp_ide_ter;
    ide_qua_enabled = temp_ide_qua;
    cassette_enable = temp_cassette;

    /* Hard disks category */
    memcpy(hdd, temp_hdd, HDD_NUM * sizeof(hard_disk_t));
    for (i = 0; i < HDD_NUM; i++)
	hdd[i].priv = NULL;

    /* Floppy drives category */
    for (i = 0; i < FDD_NUM; i++) {
	fdd_set_type(i, temp_fdd_types[i]);
	fdd_set_turbo(i, temp_fdd_turbo[i]);
	fdd_set_check_bpb(i, temp_fdd_check_bpb[i]);
    }

    /* Removable devices category */
    memcpy(cdrom, temp_cdrom, CDROM_NUM * sizeof(cdrom_t));
    for (i = 0; i < CDROM_NUM; i++) {
	cdrom[i].img_fp = NULL;
	cdrom[i].priv = NULL;
	cdrom[i].ops = NULL;
	cdrom[i].image = NULL;
	cdrom[i].insert = NULL;
	cdrom[i].close = NULL;
	cdrom[i].get_volume = NULL;
	cdrom[i].get_channel = NULL;
    }
    memcpy(zip_drives, temp_zip_drives, ZIP_NUM * sizeof(zip_drive_t));
    for (i = 0; i < ZIP_NUM; i++) {
	zip_drives[i].f = NULL;
	zip_drives[i].priv = NULL;
    }
    memcpy(mo_drives, temp_mo_drives, MO_NUM * sizeof(mo_drive_t));
    for (i = 0; i < MO_NUM; i++) {
	mo_drives[i].f = NULL;
	mo_drives[i].priv = NULL;
    }

    /* Other peripherals category */
    bugger_enabled = temp_bugger;
    postcard_enabled = temp_postcard;
    isartc_type = temp_isartc;

    /* ISA memory boards. */
    for (i = 0; i < ISAMEM_MAX; i++)
	isamem_type[i] = temp_isamem[i];

    /* Mark configuration as changed. */
    config_changed = 2;

    pc_reset_hard_init();
}


static void
win_settings_machine_recalc_fpu(HWND hdlg)
{
    int c, type;
    LPTSTR lptsTemp;
    const char *stransi;

    lptsTemp = (LPTSTR) malloc(512 * sizeof(WCHAR));

    settings_reset_content(hdlg, IDC_COMBO_FPU);
    c = 0;
    while (1) {
	stransi = (char *) fpu_get_name_from_index(temp_cpu_f, temp_cpu, c);
	type = fpu_get_type_from_index(temp_cpu_f, temp_cpu, c);
	if (!stransi)
		break;

	mbstowcs(lptsTemp, stransi, strlen(stransi) + 1);
	settings_add_string(hdlg, IDC_COMBO_FPU, (LPARAM)(LPCSTR)lptsTemp);
	if (!c || (type == temp_fpu))
		settings_set_cur_sel(hdlg, IDC_COMBO_FPU, c);

	c++;
    }

    settings_enable_window(hdlg, IDC_COMBO_FPU, c > 1);

    temp_fpu = fpu_get_type_from_index(temp_cpu_f, temp_cpu, settings_get_cur_sel(hdlg, IDC_COMBO_FPU));
}


static void
win_settings_machine_recalc_cpu(HWND hdlg)
{
    int cpu_type;
#ifdef USE_DYNAREC
    int cpu_flags;
#endif

    cpu_type = temp_cpu_f->cpus[temp_cpu].cpu_type;
    settings_enable_window(hdlg, IDC_COMBO_WS, (cpu_type >= CPU_286) && (cpu_type <= CPU_386DX));

#ifdef USE_DYNAREC
    cpu_flags = temp_cpu_f->cpus[temp_cpu].cpu_flags;
    if (!(cpu_flags & CPU_SUPPORTS_DYNAREC) && (cpu_flags & CPU_REQUIRES_DYNAREC))
	fatal("Attempting to select a CPU that requires the recompiler and does not support it at the same time\n");
    if (!(cpu_flags & CPU_SUPPORTS_DYNAREC) || (cpu_flags & CPU_REQUIRES_DYNAREC)) {
	if (!(cpu_flags & CPU_SUPPORTS_DYNAREC))
		temp_dynarec = 0;
	if (cpu_flags & CPU_REQUIRES_DYNAREC)
		temp_dynarec = 1;
	settings_set_check(hdlg, IDC_CHECK_DYNAREC, temp_dynarec);
	settings_enable_window(hdlg, IDC_CHECK_DYNAREC, FALSE);
    } else {
	settings_set_check(hdlg, IDC_CHECK_DYNAREC, temp_dynarec);
	settings_enable_window(hdlg, IDC_CHECK_DYNAREC, TRUE);
    }
#endif

    win_settings_machine_recalc_fpu(hdlg);
}


static void
win_settings_machine_recalc_cpu_m(HWND hdlg)
{
    int c, i, first_eligible = -1, current_eligible = 0, last_eligible = 0;
    LPTSTR lptsTemp;
    char *stransi;

    lptsTemp = (LPTSTR) malloc(512 * sizeof(WCHAR));

    settings_reset_content(hdlg, IDC_COMBO_CPU);
    c = i = 0;
    while (temp_cpu_f->cpus[c].cpu_type != 0) {
	if (cpu_is_eligible(temp_cpu_f, c, temp_machine)) {
		stransi = (char *) temp_cpu_f->cpus[c].name;
		mbstowcs(lptsTemp, stransi, strlen(stransi) + 1);
		settings_add_string(hdlg, IDC_COMBO_CPU, (LPARAM)(LPCSTR)lptsTemp);

		if (first_eligible == -1)
			first_eligible = i;
		if (temp_cpu == c)
			current_eligible = i;
		last_eligible = i;

		listtocpu[i++] = c;
	}
	c++;
    }
    if (i == 0)
    	fatal("No eligible CPUs for the selected family\n");
    settings_enable_window(hdlg, IDC_COMBO_CPU, i != 1);
    if (current_eligible < first_eligible)
    	current_eligible = first_eligible;
    else if (current_eligible > last_eligible)
	current_eligible = last_eligible;
    temp_cpu = listtocpu[current_eligible];
    settings_set_cur_sel(hdlg, IDC_COMBO_CPU, current_eligible);

    win_settings_machine_recalc_cpu(hdlg);

    free(lptsTemp);
}


static void
win_settings_machine_recalc_machine(HWND hdlg)
{
    HWND h;
    int c, i, current_eligible;
    LPTSTR lptsTemp;
    char *stransi;
    UDACCEL accel;
    device_t *d;

    lptsTemp = (LPTSTR) malloc(512 * sizeof(WCHAR));

    d = (device_t *) machine_getdevice(temp_machine);
    settings_enable_window(hdlg, IDC_CONFIGURE_MACHINE, d && d->config);

    settings_reset_content(hdlg, IDC_COMBO_CPU_TYPE);
    c = i = 0;
    current_eligible = -1;
    while (cpu_families[c].package != 0) {
	if (cpu_family_is_eligible(&cpu_families[c], temp_machine)) {
		stransi = malloc(strlen((char *) cpu_families[c].manufacturer) + strlen((char *) cpu_families[c].name) + 2);
		sprintf(stransi, "%s %s", (char *) cpu_families[c].manufacturer, (char *) cpu_families[c].name);
		mbstowcs(lptsTemp, stransi, strlen(stransi) + 1);
		free(stransi);
		settings_add_string(hdlg, IDC_COMBO_CPU_TYPE, (LPARAM)(LPCSTR)lptsTemp);
		if (&cpu_families[c] == temp_cpu_f)
			current_eligible = i;
		listtocpufamily[i++] = c;
	}
	c++;
    }
    if (i == 0)
	fatal("No eligible CPU families for the selected machine\n");
    settings_enable_window(hdlg, IDC_COMBO_CPU_TYPE, TRUE);
    if (current_eligible == -1) {
	temp_cpu_f = (cpu_family_t *) &cpu_families[listtocpufamily[0]];
	settings_set_cur_sel(hdlg, IDC_COMBO_CPU_TYPE, 0);
    } else {
	settings_set_cur_sel(hdlg, IDC_COMBO_CPU_TYPE, current_eligible);
    }
    settings_enable_window(hdlg, IDC_COMBO_CPU_TYPE, i != 1);

    win_settings_machine_recalc_cpu_m(hdlg);

    if ((machines[temp_machine].ram_granularity & 1023)) {
	/* KB granularity */
	h = GetDlgItem(hdlg, IDC_MEMSPIN);
	SendMessage(h, UDM_SETRANGE, 0, (machines[temp_machine].min_ram << 16) | machines[temp_machine].max_ram);

	accel.nSec = 0;
	accel.nInc = machines[temp_machine].ram_granularity;
	SendMessage(h, UDM_SETACCEL, 1, (LPARAM)&accel);

	SendMessage(h, UDM_SETPOS, 0, temp_mem_size);

	h = GetDlgItem(hdlg, IDC_TEXT_MB);
	SendMessage(h, WM_SETTEXT, 0, win_get_string(IDS_2088));
    } else {
	/* MB granularity */
	h = GetDlgItem(hdlg, IDC_MEMSPIN);
	SendMessage(h, UDM_SETRANGE, 0, (machines[temp_machine].min_ram << 6) | machines[temp_machine].max_ram >> 10);

	accel.nSec = 0;
	accel.nInc = machines[temp_machine].ram_granularity >> 10;

	SendMessage(h, UDM_SETACCEL, 1, (LPARAM)&accel);

	SendMessage(h, UDM_SETPOS, 0, temp_mem_size >> 10);

	h = GetDlgItem(hdlg, IDC_TEXT_MB);
	SendMessage(h, WM_SETTEXT, 0, win_get_string(IDS_2086));
    }

    settings_enable_window(hdlg, IDC_MEMSPIN, machines[temp_machine].min_ram != machines[temp_machine].max_ram);
    settings_enable_window(hdlg, IDC_MEMTEXT, machines[temp_machine].min_ram != machines[temp_machine].max_ram);

    free(lptsTemp);
}


static char *
machine_type_get_internal_name(int id)
{
    if (id < MACHINE_TYPE_MAX)
	return "";
    else
	return NULL;
}


int
machine_type_available(int id)
{
    int c = 0;

    if ((id > 0) && (id < MACHINE_TYPE_MAX)) {
	while (machine_get_internal_name_ex(c) != NULL) {
		if (machine_available(c) && (machines[c].type == id))
			return 1;
		c++;
	}
    }

    return 0;
}


#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_machine_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND h, h2;
    int c, d;
    int old_machine_type;
    LPTSTR lptsTemp;
    char *stransi;

    switch (message) {
	case WM_INITDIALOG:
		lptsTemp = (LPTSTR) malloc(512 * sizeof(WCHAR));

		c = d = 0;
		settings_reset_content(hdlg, IDC_COMBO_MACHINE_TYPE);
		memset(listtomachinetype, 0x00, sizeof(listtomachinetype));
		while (machine_type_get_internal_name(c) != NULL) {
			if (machine_type_available(c)) {
				stransi = (char *)machine_types[c].name;
				mbstowcs(lptsTemp, stransi, strlen(stransi) + 1);
				settings_add_string(hdlg, IDC_COMBO_MACHINE_TYPE, (LPARAM) lptsTemp);
				listtomachinetype[d] = c;
				if (c == temp_machine_type)
					settings_set_cur_sel(hdlg, IDC_COMBO_MACHINE_TYPE, d);
				d++;
			}
			c++;
		}

		c = d = 0;
		settings_reset_content(hdlg, IDC_COMBO_MACHINE);
		memset(listtomachine, 0x00, sizeof(listtomachine));
		while (machine_get_internal_name_ex(c) != NULL) {
			if (machine_available(c) && (machines[c].type == temp_machine_type)) {
				stransi = (char *)machines[c].name;
				mbstowcs(lptsTemp, stransi, strlen(stransi) + 1);
				settings_add_string(hdlg, IDC_COMBO_MACHINE, (LPARAM) lptsTemp);
				listtomachine[d] = c;
				if (c == temp_machine)
					settings_set_cur_sel(hdlg, IDC_COMBO_MACHINE, d);
				d++;
			}
			c++;
		}

		settings_add_string(hdlg, IDC_COMBO_WS, win_get_string(IDS_2090));
		for (c = 0; c < 8; c++) {
			wsprintf(lptsTemp, plat_get_string(IDS_2091), c);
			settings_add_string(hdlg, IDC_COMBO_WS, (LPARAM) lptsTemp);
		}

		settings_set_cur_sel(hdlg, IDC_COMBO_WS, temp_wait_states);

#ifdef USE_DYNAREC
		settings_set_check(hdlg, IDC_CHECK_DYNAREC, 0);
#endif

		h = GetDlgItem(hdlg, IDC_MEMSPIN);
		h2 = GetDlgItem(hdlg, IDC_MEMTEXT);
		SendMessage(h, UDM_SETBUDDY, (WPARAM)h2, 0);

		if (temp_sync & TIME_SYNC_ENABLED) {
			if (temp_sync & TIME_SYNC_UTC)
				settings_set_check(hdlg, IDC_RADIO_TS_UTC, BST_CHECKED);
			else
				settings_set_check(hdlg, IDC_RADIO_TS_LOCAL, BST_CHECKED);
		} else
			settings_set_check(hdlg, IDC_RADIO_TS_DISABLED, BST_CHECKED);

		win_settings_machine_recalc_machine(hdlg);

		free(lptsTemp);

		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
			case IDC_COMBO_MACHINE_TYPE:
				if (HIWORD(wParam) == CBN_SELCHANGE) {
					old_machine_type = temp_machine_type;
					temp_machine_type = listtomachinetype[settings_get_cur_sel(hdlg, IDC_COMBO_MACHINE_TYPE)];

					lptsTemp = (LPTSTR) malloc(512 * sizeof(WCHAR));

					settings_reset_content(hdlg, IDC_COMBO_MACHINE);
					c = d = 0;
					memset(listtomachine, 0x00, sizeof(listtomachine));
					while (machine_get_internal_name_ex(c) != NULL) {
						if (machine_available(c) && (machines[c].type == temp_machine_type)) {
							stransi = (char *)machines[c].name;
							mbstowcs(lptsTemp, stransi, strlen(stransi) + 1);
							settings_add_string(hdlg, IDC_COMBO_MACHINE, (LPARAM) lptsTemp);
							listtomachine[d] = c;
							if (c == temp_machine)
								settings_set_cur_sel(hdlg, IDC_COMBO_MACHINE, d);
							d++;
						}
						c++;
					}
					if (old_machine_type != temp_machine_type) {
						settings_set_cur_sel(hdlg, IDC_COMBO_MACHINE, 0);
						temp_machine = listtomachine[0];

						win_settings_machine_recalc_machine(hdlg);
					}
				}
				break;
			case IDC_COMBO_MACHINE:
				if (HIWORD(wParam) == CBN_SELCHANGE) {
					temp_machine = listtomachine[settings_get_cur_sel(hdlg, IDC_COMBO_MACHINE)];
					win_settings_machine_recalc_machine(hdlg);
				}
				break;
			case IDC_COMBO_CPU_TYPE:
				if (HIWORD(wParam) == CBN_SELCHANGE) {
					temp_cpu_f = (cpu_family_t *) &cpu_families[listtocpufamily[settings_get_cur_sel(hdlg, IDC_COMBO_CPU_TYPE)]];
					temp_cpu = 0;
					win_settings_machine_recalc_cpu_m(hdlg);
				}
				break;
			case IDC_COMBO_CPU:
				if (HIWORD(wParam) == CBN_SELCHANGE) {
					temp_cpu = listtocpu[settings_get_cur_sel(hdlg, IDC_COMBO_CPU)];
					win_settings_machine_recalc_cpu(hdlg);
				}
				break;
			case IDC_COMBO_FPU:
				if (HIWORD(wParam) == CBN_SELCHANGE) {
					temp_fpu = fpu_get_type_from_index(temp_cpu_f, temp_cpu,
									   settings_get_cur_sel(hdlg, IDC_COMBO_FPU));
				}
				break;
			case IDC_CONFIGURE_MACHINE:
				temp_machine = listtomachine[settings_get_cur_sel(hdlg, IDC_COMBO_MACHINE)];
				temp_deviceconfig |= deviceconfig_open(hdlg, (void *)machine_getdevice(temp_machine));
				break;
		}

		return FALSE;

	case WM_SAVESETTINGS:
		lptsTemp = (LPTSTR) malloc(512 * sizeof(WCHAR));
		stransi = (char *)malloc(512);

#ifdef USE_DYNAREC
		temp_dynarec = settings_get_check(hdlg, IDC_CHECK_DYNAREC);
#endif

		if (settings_get_check(hdlg, IDC_RADIO_TS_DISABLED))
			temp_sync = TIME_SYNC_DISABLED;

		if (settings_get_check(hdlg, IDC_RADIO_TS_LOCAL))
			temp_sync = TIME_SYNC_ENABLED;

		if (settings_get_check(hdlg, IDC_RADIO_TS_UTC))
			temp_sync = TIME_SYNC_ENABLED | TIME_SYNC_UTC;

		temp_wait_states = settings_get_cur_sel(hdlg, IDC_COMBO_WS);

		h = GetDlgItem(hdlg, IDC_MEMTEXT);
		SendMessage(h, WM_GETTEXT, 255, (LPARAM) lptsTemp);
		wcstombs(stransi, lptsTemp, 512);
		sscanf(stransi, "%u", &temp_mem_size);
		if (!(machines[temp_machine].ram_granularity & 1023))
			temp_mem_size = temp_mem_size << 10;
		temp_mem_size &= ~(machines[temp_machine].ram_granularity - 1);
		if (temp_mem_size < machines[temp_machine].min_ram)
			temp_mem_size = machines[temp_machine].min_ram;
		else if (temp_mem_size > machines[temp_machine].max_ram)
			temp_mem_size = machines[temp_machine].max_ram;
		free(stransi);
		free(lptsTemp);

	default:
		return FALSE;
    }

    return FALSE;
}


static void
generate_device_name(const device_t *device, char *internal_name, int bus)
{
    char temp[512];
    WCHAR *wtemp;

    memset(device_name, 0x00, 512 * sizeof(WCHAR));
    memset(temp, 0x00, 512);

    if (!strcmp(internal_name, "none")) {
	/* Translate "None". */
	wtemp = (WCHAR *) win_get_string(IDS_2103);
	memcpy(device_name, wtemp, (wcslen(wtemp) + 1) * sizeof(WCHAR));
	return;
    } else if (!strcmp(internal_name, "internal"))
	memcpy(temp, "Internal", 9);
    else
	device_get_name(device, bus, temp);

    mbstowcs(device_name, temp, strlen(temp) + 1);
}


#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_video_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    int c = 0, d = 0;
    int e;

    switch (message) {
	case WM_INITDIALOG:
		settings_reset_content(hdlg, IDC_COMBO_VIDEO);

		while (1) {
			/* Skip "internal" if machine doesn't have it. */
			if ((c == 1) && !(machines[temp_machine].flags & MACHINE_VIDEO)) {
				c++;
				continue;
			}

			generate_device_name(video_card_getdevice(c), video_get_internal_name(c), 1);

			if (!device_name[0])
				break;

			if (video_card_available(c) &&
			    device_is_valid(video_card_getdevice(c), machines[temp_machine].flags)) {
				if (c == 0)
					settings_add_string(hdlg, IDC_COMBO_VIDEO, win_get_string(IDS_2103));
				else if (c == 1)
					settings_add_string(hdlg, IDC_COMBO_VIDEO, win_get_string(IDS_2118));
				else
					settings_add_string(hdlg, IDC_COMBO_VIDEO, (LPARAM) device_name);
				settings_list_to_device[0][d] = c;
				if ((c == 0) || (c == temp_gfxcard))
					settings_set_cur_sel(hdlg, IDC_COMBO_VIDEO, d);
				d++;
			}

			c++;

			settings_process_messages();
		}

		settings_enable_window(hdlg, IDC_COMBO_VIDEO, !(machines[temp_machine].flags & MACHINE_VIDEO_ONLY));
		e = settings_list_to_device[0][settings_get_cur_sel(hdlg, IDC_COMBO_VIDEO)];
		settings_enable_window(hdlg, IDC_CONFIGURE_VID, video_card_has_config(e));
		settings_enable_window(hdlg, IDC_CHECK_VOODOO, (machines[temp_machine].flags & MACHINE_BUS_PCI));
		settings_set_check(hdlg, IDC_CHECK_VOODOO, temp_voodoo);
		settings_enable_window(hdlg, IDC_BUTTON_VOODOO, (machines[temp_machine].flags & MACHINE_BUS_PCI) && temp_voodoo);
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
			case IDC_COMBO_VIDEO:
				temp_gfxcard = settings_list_to_device[0][settings_get_cur_sel(hdlg, IDC_COMBO_VIDEO)];
				settings_enable_window(hdlg, IDC_CONFIGURE_VID, video_card_has_config(temp_gfxcard));
				break;

			case IDC_CHECK_VOODOO:
				temp_voodoo = settings_get_check(hdlg, IDC_CHECK_VOODOO);
				settings_enable_window(hdlg, IDC_BUTTON_VOODOO, temp_voodoo);
				break;

			case IDC_BUTTON_VOODOO:
				temp_deviceconfig |= deviceconfig_open(hdlg, (void *)&voodoo_device);
				break;

			case IDC_CONFIGURE_VID:
				temp_gfxcard = settings_list_to_device[0][settings_get_cur_sel(hdlg, IDC_COMBO_VIDEO)];
				temp_deviceconfig |= deviceconfig_open(hdlg, (void *)video_card_getdevice(temp_gfxcard));
				break;
		}
		return FALSE;

	case WM_SAVESETTINGS:
		temp_gfxcard = settings_list_to_device[0][settings_get_cur_sel(hdlg, IDC_COMBO_VIDEO)];
		temp_voodoo = settings_get_check(hdlg, IDC_CHECK_VOODOO);

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


#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_input_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    wchar_t str[128];
    char *joy_name;
    int c, d;

    switch (message) {
	case WM_INITDIALOG:
		c = d = 0;
		settings_reset_content(hdlg, IDC_COMBO_MOUSE);
		for (c = 0; c < mouse_get_ndev(); c++) {
			if (mouse_valid(c, temp_machine)) {
				generate_device_name(mouse_get_device(c), mouse_get_internal_name(c), 0);
				if (c == 0)
					settings_add_string(hdlg, IDC_COMBO_MOUSE, win_get_string(IDS_2103));
				else if (c == 1)
					settings_add_string(hdlg, IDC_COMBO_MOUSE, win_get_string(IDS_2118));
				else
					settings_add_string(hdlg, IDC_COMBO_MOUSE, (LPARAM) device_name);
				settings_list_to_device[0][d] = c;
				if ((c == 0) || (c == temp_mouse))
					settings_set_cur_sel(hdlg, IDC_COMBO_MOUSE, d);
				d++;
			}
		}

		settings_enable_window(hdlg, IDC_CONFIGURE_MOUSE, mouse_has_config(temp_mouse));

		c = 0;
		joy_name = joystick_get_name(c);
		while (joy_name)
		{
			mbstowcs(str, joy_name, strlen(joy_name) + 1);
			settings_add_string(hdlg, IDC_COMBO_JOYSTICK, (LPARAM) str);

			c++;
			joy_name = joystick_get_name(c);
		}
		settings_enable_window(hdlg, IDC_COMBO_JOYSTICK, TRUE);
		settings_set_cur_sel(hdlg, IDC_COMBO_JOYSTICK, temp_joystick);

		for (c = 0; c < 4; c++)
			settings_enable_window(hdlg, IDC_JOY1 + c, joystick_get_max_joysticks(temp_joystick) > c);

		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
			case IDC_COMBO_MOUSE:
				temp_mouse = settings_list_to_device[0][settings_get_cur_sel(hdlg, IDC_COMBO_MOUSE)];
				settings_enable_window(hdlg, IDC_CONFIGURE_MOUSE, mouse_has_config(temp_mouse));
				break;

			case IDC_CONFIGURE_MOUSE:
				temp_mouse = settings_list_to_device[0][settings_get_cur_sel(hdlg, IDC_COMBO_MOUSE)];
				temp_deviceconfig |= deviceconfig_open(hdlg, (void *)mouse_get_device(temp_mouse));
				break;

			case IDC_COMBO_JOYSTICK:
				temp_joystick = settings_get_cur_sel(hdlg, IDC_COMBO_JOYSTICK);

				for (c = 0; c < 4; c++)
					settings_enable_window(hdlg, IDC_JOY1 + c, joystick_get_max_joysticks(temp_joystick) > c);
				break;

			case IDC_JOY1: case IDC_JOY2: case IDC_JOY3: case IDC_JOY4:
				temp_joystick = settings_get_cur_sel(hdlg, IDC_COMBO_JOYSTICK);
				temp_deviceconfig |= joystickconfig_open(hdlg, LOWORD(wParam) - IDC_JOY1, temp_joystick);
				break;
		}
		return FALSE;

	case WM_SAVESETTINGS:
		temp_mouse = settings_list_to_device[0][settings_get_cur_sel(hdlg, IDC_COMBO_MOUSE)];
		temp_joystick = settings_get_cur_sel(hdlg, IDC_COMBO_JOYSTICK);

	default:
		return FALSE;
    }
    return FALSE;
}


static int
mpu401_present(void)
{
    return temp_mpu401 ? 1 : 0;
}


int
mpu401_standalone_allow(void)
{
    char *md, *mdin;

    md = midi_device_get_internal_name(temp_midi_device);
    mdin = midi_in_device_get_internal_name(temp_midi_input_device);

    if (md != NULL) {
	if (!strcmp(md, "none") && !strcmp(mdin, "none"))
		return 0;
    }

    return 1;
}

#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_sound_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    int c, d;
    LPTSTR lptsTemp;
    const device_t *sound_dev;

    switch (message) {
	case WM_INITDIALOG:
		lptsTemp = (LPTSTR) malloc(512 * sizeof(WCHAR));

		c = d = 0;
		settings_reset_content(hdlg, IDC_COMBO_SOUND);
		while (1) {
			/* Skip "internal" if machine doesn't have it. */
			if ((c == 1) && !(machines[temp_machine].flags & MACHINE_SOUND)) {
				c++;
				continue;
			}

			generate_device_name(sound_card_getdevice(c), sound_card_get_internal_name(c), 1);

			if (!device_name[0])
				break;

			if (sound_card_available(c)) {
				sound_dev = sound_card_getdevice(c);

				if (device_is_valid(sound_dev, machines[temp_machine].flags)) {
					if (c == 0)
						settings_add_string(hdlg, IDC_COMBO_SOUND, win_get_string(IDS_2103));
					else if (c == 1)
						settings_add_string(hdlg, IDC_COMBO_SOUND, win_get_string(IDS_2118));
					else
						settings_add_string(hdlg, IDC_COMBO_SOUND, (LPARAM) device_name);
					settings_list_to_device[0][d] = c;
					if ((c == 0) || (c == temp_sound_card))
						settings_set_cur_sel(hdlg, IDC_COMBO_SOUND, d);
					d++;
				}
			}

			c++;
		}

		settings_enable_window(hdlg, IDC_COMBO_SOUND, d);
		settings_enable_window(hdlg, IDC_CONFIGURE_SND, sound_card_has_config(temp_sound_card));

		c = d = 0;
		settings_reset_content(hdlg, IDC_COMBO_MIDI);
		while (1) {
			generate_device_name(midi_device_getdevice(c), midi_device_get_internal_name(c), 0);

			if (!device_name[0])
				break;

			if (midi_device_available(c)) {
				if (c == 0)
					settings_add_string(hdlg, IDC_COMBO_MIDI, win_get_string(IDS_2103));
				else
					settings_add_string(hdlg, IDC_COMBO_MIDI, (LPARAM) device_name);
				settings_list_to_midi[d] = c;
				if ((c == 0) || (c == temp_midi_device))
					settings_set_cur_sel(hdlg, IDC_COMBO_MIDI, d);
				d++;
			}

			c++;
		}

		settings_enable_window(hdlg, IDC_CONFIGURE_MIDI, midi_device_has_config(temp_midi_device));

		c = d = 0;
		settings_reset_content(hdlg, IDC_COMBO_MIDI_IN);
		while (1) {
			generate_device_name(midi_in_device_getdevice(c), midi_in_device_get_internal_name(c), 0);

			if (!device_name[0])
				break;

			if (midi_in_device_available(c)) {
				if (c == 0)
					settings_add_string(hdlg, IDC_COMBO_MIDI_IN, win_get_string(IDS_2103));
				else
					settings_add_string(hdlg, IDC_COMBO_MIDI_IN, (LPARAM) device_name);
				settings_list_to_midi_in[d] = c;
				if ((c == 0) || (c == temp_midi_input_device))
					settings_set_cur_sel(hdlg, IDC_COMBO_MIDI_IN, d);
				d++;
			}

			c++;
		}

		settings_enable_window(hdlg, IDC_CONFIGURE_MIDI_IN, midi_in_device_has_config(temp_midi_input_device));
		settings_set_check(hdlg, IDC_CHECK_MPU401, temp_mpu401);
		settings_enable_window(hdlg, IDC_CHECK_MPU401, mpu401_standalone_allow());
		settings_enable_window(hdlg, IDC_CONFIGURE_MPU401, mpu401_standalone_allow() && temp_mpu401);
		settings_set_check(hdlg, IDC_CHECK_CMS, temp_GAMEBLASTER);
		settings_enable_window(hdlg, IDC_CONFIGURE_CMS, temp_GAMEBLASTER);
		settings_set_check(hdlg, IDC_CHECK_GUS, temp_GUS);
		settings_enable_window(hdlg, IDC_CONFIGURE_GUS, temp_GUS);
		settings_set_check(hdlg, IDC_CHECK_SSI, temp_SSI2001);
		settings_set_check(hdlg, IDC_CHECK_FLOAT, temp_float);

		free(lptsTemp);

		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
			case IDC_COMBO_SOUND:
				temp_sound_card = settings_list_to_device[0][settings_get_cur_sel(hdlg, IDC_COMBO_SOUND)];
				settings_enable_window(hdlg, IDC_CONFIGURE_SND, sound_card_has_config(temp_sound_card));
				settings_set_check(hdlg, IDC_CHECK_MPU401, temp_mpu401);
				settings_enable_window(hdlg, IDC_CHECK_MPU401, mpu401_standalone_allow());
				settings_enable_window(hdlg, IDC_CONFIGURE_MPU401, mpu401_standalone_allow() && temp_mpu401);
				break;

			case IDC_CONFIGURE_SND:
				temp_sound_card = settings_list_to_device[0][settings_get_cur_sel(hdlg, IDC_COMBO_SOUND)];
				temp_deviceconfig |= deviceconfig_open(hdlg, (void *)sound_card_getdevice(temp_sound_card));
				break;

			case IDC_COMBO_MIDI:
				temp_midi_device = settings_list_to_midi[settings_get_cur_sel(hdlg, IDC_COMBO_MIDI)];
				settings_enable_window(hdlg, IDC_CONFIGURE_MIDI, midi_device_has_config(temp_midi_device));
				settings_set_check(hdlg, IDC_CHECK_MPU401, temp_mpu401);
				settings_enable_window(hdlg, IDC_CHECK_MPU401, mpu401_standalone_allow());
				settings_enable_window(hdlg, IDC_CONFIGURE_MPU401, mpu401_standalone_allow() && temp_mpu401);
				break;

			case IDC_CONFIGURE_MIDI:
				temp_midi_device = settings_list_to_midi[settings_get_cur_sel(hdlg, IDC_COMBO_MIDI)];
				temp_deviceconfig |= deviceconfig_open(hdlg, (void *)midi_device_getdevice(temp_midi_device));
				break;

			case IDC_COMBO_MIDI_IN:
				temp_midi_input_device = settings_list_to_midi_in[settings_get_cur_sel(hdlg, IDC_COMBO_MIDI_IN)];
				settings_enable_window(hdlg, IDC_CONFIGURE_MIDI_IN, midi_in_device_has_config(temp_midi_input_device));
				settings_set_check(hdlg, IDC_CHECK_MPU401, temp_mpu401);
				settings_enable_window(hdlg, IDC_CHECK_MPU401, mpu401_standalone_allow());
				settings_enable_window(hdlg, IDC_CONFIGURE_MPU401, mpu401_standalone_allow() && temp_mpu401);
				break;

			case IDC_CONFIGURE_MIDI_IN:
				temp_midi_input_device = settings_list_to_midi_in[settings_get_cur_sel(hdlg, IDC_COMBO_MIDI_IN)];
				temp_deviceconfig |= deviceconfig_open(hdlg, (void *)midi_in_device_getdevice(temp_midi_input_device));
				break;

			case IDC_CHECK_MPU401:
				temp_mpu401 = settings_get_check(hdlg, IDC_CHECK_MPU401);

				settings_enable_window(hdlg, IDC_CONFIGURE_MPU401, mpu401_present());
				break;

			case IDC_CONFIGURE_MPU401:
				temp_deviceconfig |= deviceconfig_open(hdlg, (machines[temp_machine].flags & MACHINE_MCA) ?
								       (void *)&mpu401_mca_device : (void *)&mpu401_device);
				break;

			case IDC_CHECK_CMS:
				temp_GAMEBLASTER = settings_get_check(hdlg, IDC_CHECK_CMS);

				settings_enable_window(hdlg, IDC_CONFIGURE_CMS, temp_GAMEBLASTER);
				break;

			case IDC_CONFIGURE_CMS:
				temp_deviceconfig |= deviceconfig_open(hdlg, &cms_device);
				break;

			case IDC_CHECK_GUS:
				temp_GUS = settings_get_check(hdlg, IDC_CHECK_GUS);
				settings_enable_window(hdlg, IDC_CONFIGURE_GUS, temp_GUS);
				break;

			case IDC_CONFIGURE_GUS:
				temp_deviceconfig |= deviceconfig_open(hdlg, (void *)&gus_device);
				break;
		}
		return FALSE;

	case WM_SAVESETTINGS:
		temp_sound_card = settings_list_to_device[0][settings_get_cur_sel(hdlg, IDC_COMBO_SOUND)];
		temp_midi_device = settings_list_to_midi[settings_get_cur_sel(hdlg, IDC_COMBO_MIDI)];
		temp_midi_input_device = settings_list_to_midi_in[settings_get_cur_sel(hdlg, IDC_COMBO_MIDI_IN)];
		temp_mpu401 = settings_get_check(hdlg, IDC_CHECK_MPU401);
		temp_GAMEBLASTER = settings_get_check(hdlg, IDC_CHECK_CMS);
		temp_GUS = settings_get_check(hdlg, IDC_CHECK_GUS);
		temp_SSI2001 = settings_get_check(hdlg, IDC_CHECK_SSI);
		temp_float = settings_get_check(hdlg, IDC_CHECK_FLOAT);

	default:
		return FALSE;
    }
    return FALSE;
}


#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_ports_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    int c, i;
    char *s;
    LPTSTR lptsTemp;

    switch (message) {
	case WM_INITDIALOG:
		lptsTemp = (LPTSTR) malloc(512 * sizeof(WCHAR));

		for (i = 0; i < 3; i++) {
			c = 0;
			while (1) {
				s = lpt_device_get_name(c);

				if (!s)
					break;

				if (c == 0)
					settings_add_string(hdlg, IDC_COMBO_LPT1 + i, win_get_string(IDS_2103));
				else {
					mbstowcs(lptsTemp, s, strlen(s) + 1);
					settings_add_string(hdlg, IDC_COMBO_LPT1 + i, (LPARAM) lptsTemp);
				}

				c++;
			}
			settings_set_cur_sel(hdlg, IDC_COMBO_LPT1 + i, temp_lpt_devices[i]);

			settings_set_check(hdlg, IDC_CHECK_PARALLEL1 + i, temp_lpt[i]);
			settings_enable_window(hdlg, IDC_COMBO_LPT1 + i, temp_lpt[i]);
		}

		for (i = 0; i < 4; i++)
			settings_set_check(hdlg, IDC_CHECK_SERIAL1 + i, temp_serial[i]);

		free(lptsTemp);

		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
			case IDC_CHECK_PARALLEL1:
			case IDC_CHECK_PARALLEL2:
			case IDC_CHECK_PARALLEL3:
				i = LOWORD(wParam) - IDC_CHECK_PARALLEL1;
				settings_enable_window(hdlg, IDC_COMBO_LPT1 + i,
						       settings_get_check(hdlg, IDC_CHECK_PARALLEL1 + i) == BST_CHECKED);
				break;
		}
		break;

	case WM_SAVESETTINGS:
		for (i = 0; i < 3; i++) {
			temp_lpt_devices[i] = settings_get_cur_sel(hdlg, IDC_COMBO_LPT1 + i);
			temp_lpt[i] = settings_get_check(hdlg, IDC_CHECK_PARALLEL1 + i);
		}

		for (i = 0; i < 4; i++)
			temp_serial[i] = settings_get_check(hdlg, IDC_CHECK_SERIAL1 + i);

	default:
		return FALSE;
    }
    return FALSE;
}


#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_storage_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    int c, d;
    int e, is_at;
    LPTSTR lptsTemp;
    char *stransi;
    const device_t *scsi_dev, *fdc_dev;
    const device_t *hdc_dev;

    switch (message) {
	case WM_INITDIALOG:
		lptsTemp = (LPTSTR) malloc(512 * sizeof(WCHAR));
		stransi = (char *) malloc(512);

		/*HD controller config*/
		c = d = 0;
		settings_reset_content(hdlg, IDC_COMBO_HDC);
		while (1) {
			/* Skip "internal" if machine doesn't have it. */
			if ((c == 1) && !(machines[temp_machine].flags & MACHINE_HDC)) {
				c++;
				continue;
			}

			generate_device_name(hdc_get_device(c), hdc_get_internal_name(c), 1);

			if (!device_name[0])
				break;

			if (hdc_available(c)) {
				hdc_dev = hdc_get_device(c);

				if (device_is_valid(hdc_dev, machines[temp_machine].flags)) {
					if (c == 0)
						settings_add_string(hdlg, IDC_COMBO_HDC, win_get_string(IDS_2103));
					else if (c == 1)
						settings_add_string(hdlg, IDC_COMBO_HDC, win_get_string(IDS_2118));
					else
						settings_add_string(hdlg, IDC_COMBO_HDC, (LPARAM) device_name);
					settings_list_to_hdc[d] = c;
					if ((c == 0) || (c == temp_hdc))
						settings_set_cur_sel(hdlg, IDC_COMBO_HDC, d);
					d++;
				}
			}

			c++;
		}

		settings_enable_window(hdlg, IDC_COMBO_HDC, d);
		settings_enable_window(hdlg, IDC_CONFIGURE_HDC, hdc_has_config(temp_hdc));

		/*FD controller config*/
		c = d = 0;
		settings_reset_content(hdlg, IDC_COMBO_FDC);
		while (1) {
			generate_device_name(fdc_card_getdevice(c), fdc_card_get_internal_name(c), 1);

			if (!device_name[0])
				break;

			if (fdc_card_available(c)) {
				fdc_dev = fdc_card_getdevice(c);

				if (device_is_valid(fdc_dev, machines[temp_machine].flags)) {
					if (c == 0)
						settings_add_string(hdlg, IDC_COMBO_FDC, win_get_string(IDS_2118));
					else
						settings_add_string(hdlg, IDC_COMBO_FDC, (LPARAM) device_name);
					settings_list_to_fdc[d] = c;
					if ((c == 0) || (c == temp_fdc_card))
						settings_set_cur_sel(hdlg, IDC_COMBO_FDC, d);
					d++;
				}
			}

			c++;
		}

		settings_enable_window(hdlg, IDC_COMBO_FDC, d);
		settings_enable_window(hdlg, IDC_CONFIGURE_FDC, fdc_card_has_config(temp_fdc_card));

		/*SCSI config*/
		c = d = 0;
		for (e = 0; e < SCSI_BUS_MAX; e++)
			settings_reset_content(hdlg, IDC_COMBO_SCSI_1 + e);
		while (1) {
			generate_device_name(scsi_card_getdevice(c), scsi_card_get_internal_name(c), 1);

			if (!device_name[0])
				break;

			if (scsi_card_available(c)) {
				scsi_dev = scsi_card_getdevice(c);

				if (device_is_valid(scsi_dev, machines[temp_machine].flags)) {
					for (e = 0; e < SCSI_BUS_MAX; e++) {
						if (c == 0)
							settings_add_string(hdlg, IDC_COMBO_SCSI_1 + e, win_get_string(IDS_2103));
						else
							settings_add_string(hdlg, IDC_COMBO_SCSI_1 + e, (LPARAM) device_name);

						if ((c == 0) || (c == temp_scsi_card[e]))
							settings_set_cur_sel(hdlg, IDC_COMBO_SCSI_1 + e, d);
					}

					settings_list_to_device[0][d] = c;
					d++;
				}
			}

			c++;
		}

		for (c = 0; c < SCSI_BUS_MAX; c++) {
			settings_enable_window(hdlg, IDC_COMBO_SCSI_1 + c, d);
			settings_enable_window(hdlg, IDC_CONFIGURE_SCSI_1 + c, scsi_card_has_config(temp_scsi_card[c]));
		}
		is_at = IS_AT(temp_machine);
		settings_enable_window(hdlg, IDC_CHECK_IDE_TER, is_at);
		settings_enable_window(hdlg, IDC_BUTTON_IDE_TER, is_at && temp_ide_ter);
		settings_enable_window(hdlg, IDC_CHECK_IDE_QUA, is_at);
		settings_enable_window(hdlg, IDC_BUTTON_IDE_QUA, is_at && temp_ide_qua);
		settings_set_check(hdlg, IDC_CHECK_IDE_TER, temp_ide_ter);
		settings_set_check(hdlg, IDC_CHECK_IDE_QUA, temp_ide_qua);
		settings_set_check(hdlg, IDC_CHECK_CASSETTE, temp_cassette);

		free(stransi);
		free(lptsTemp);

		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
			case IDC_CONFIGURE_FDC:
				temp_fdc_card = settings_list_to_fdc[settings_get_cur_sel(hdlg, IDC_COMBO_FDC)];
				temp_deviceconfig |= deviceconfig_open(hdlg, (void *)fdc_card_getdevice(temp_fdc_card));
				break;

			case IDC_COMBO_FDC:
				temp_fdc_card = settings_list_to_fdc[settings_get_cur_sel(hdlg, IDC_COMBO_FDC)];
				settings_enable_window(hdlg, IDC_CONFIGURE_FDC, fdc_card_has_config(temp_fdc_card));
				break;		
			
			case IDC_CONFIGURE_HDC:
				temp_hdc = settings_list_to_hdc[settings_get_cur_sel(hdlg, IDC_COMBO_HDC)];
				temp_deviceconfig |= deviceconfig_open(hdlg, (void *)hdc_get_device(temp_hdc));
				break;

			case IDC_COMBO_HDC:
				temp_hdc = settings_list_to_hdc[settings_get_cur_sel(hdlg, IDC_COMBO_HDC)];
				settings_enable_window(hdlg, IDC_CONFIGURE_HDC, hdc_has_config(temp_hdc));
				break;

			case IDC_CONFIGURE_SCSI_1 ... IDC_CONFIGURE_SCSI_4:
				c = LOWORD(wParam) - IDC_CONFIGURE_SCSI_1;
				temp_scsi_card[c] = settings_list_to_device[0][settings_get_cur_sel(hdlg, IDC_COMBO_SCSI_1 + c)];
				temp_deviceconfig |= deviceconfig_inst_open(hdlg, (void *)scsi_card_getdevice(temp_scsi_card[c]), c + 1);
				break;

			case IDC_COMBO_SCSI_1 ... IDC_COMBO_SCSI_4:
				c = LOWORD(wParam) - IDC_COMBO_SCSI_1;
				temp_scsi_card[c] = settings_list_to_device[0][settings_get_cur_sel(hdlg, IDC_COMBO_SCSI_1 + c)];
				settings_enable_window(hdlg, IDC_CONFIGURE_SCSI_1 + c, scsi_card_has_config(temp_scsi_card[c]));
				break;

			case IDC_CHECK_IDE_TER:
				temp_ide_ter = settings_get_check(hdlg, IDC_CHECK_IDE_TER);
				settings_enable_window(hdlg, IDC_BUTTON_IDE_TER, temp_ide_ter);
				break;

			case IDC_BUTTON_IDE_TER:
				temp_deviceconfig |= deviceconfig_open(hdlg, (void *)&ide_ter_device);
				break;

			case IDC_CHECK_IDE_QUA:
				temp_ide_qua = settings_get_check(hdlg, IDC_CHECK_IDE_QUA);
				settings_enable_window(hdlg, IDC_BUTTON_IDE_QUA, temp_ide_qua);
				break;

			case IDC_BUTTON_IDE_QUA:
				temp_deviceconfig |= deviceconfig_open(hdlg, (void *)&ide_qua_device);
				break;
		}
		return FALSE;

	case WM_SAVESETTINGS:
		temp_hdc = settings_list_to_hdc[settings_get_cur_sel(hdlg, IDC_COMBO_HDC)];
		temp_fdc_card = settings_list_to_fdc[settings_get_cur_sel(hdlg, IDC_COMBO_FDC)];
		for (c = 0; c < SCSI_BUS_MAX; c++)
			temp_scsi_card[c] = settings_list_to_device[0][settings_get_cur_sel(hdlg, IDC_COMBO_SCSI_1 + c)];
		temp_ide_ter = settings_get_check(hdlg, IDC_CHECK_IDE_TER);
		temp_ide_qua = settings_get_check(hdlg, IDC_CHECK_IDE_QUA);
		temp_cassette = settings_get_check(hdlg, IDC_CHECK_CASSETTE);

	default:
		return FALSE;
    }
    return FALSE;
}


static void network_recalc_combos(HWND hdlg)
{
    ignore_change = 1;

    settings_enable_window(hdlg, IDC_COMBO_PCAP, temp_net_type == NET_TYPE_PCAP);
    settings_enable_window(hdlg, IDC_COMBO_NET,
				 (temp_net_type == NET_TYPE_SLIRP) ||
				 ((temp_net_type == NET_TYPE_PCAP) && (network_dev_to_id(temp_pcap_dev) > 0)));
    settings_enable_window(hdlg, IDC_CONFIGURE_NET, network_card_has_config(temp_net_card) &&
				 ((temp_net_type == NET_TYPE_SLIRP) ||
				 ((temp_net_type == NET_TYPE_PCAP) && (network_dev_to_id(temp_pcap_dev) > 0))));

    ignore_change = 0;
}


#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_network_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    int c, d;
    LPTSTR lptsTemp;

    switch (message) {
	case WM_INITDIALOG:
		lptsTemp = (LPTSTR) malloc(512 * sizeof(WCHAR));

		settings_add_string(hdlg, IDC_COMBO_NET_TYPE, (LPARAM) L"None");
		settings_add_string(hdlg, IDC_COMBO_NET_TYPE, (LPARAM) L"PCap");
		settings_add_string(hdlg, IDC_COMBO_NET_TYPE, (LPARAM) L"SLiRP");
		settings_set_cur_sel(hdlg, IDC_COMBO_NET_TYPE, temp_net_type);
		settings_enable_window(hdlg, IDC_COMBO_PCAP, temp_net_type == NET_TYPE_PCAP);

		for (c = 0; c < network_ndev; c++) {
			mbstowcs(lptsTemp, network_devs[c].description, strlen(network_devs[c].description) + 1);
			settings_add_string(hdlg, IDC_COMBO_PCAP, (LPARAM) lptsTemp);
		}
		settings_set_cur_sel(hdlg, IDC_COMBO_PCAP, network_dev_to_id(temp_pcap_dev));

		/* NIC config */
		c = d = 0;
		settings_reset_content(hdlg, IDC_COMBO_NET);
		while (1) {
			generate_device_name(network_card_getdevice(c), network_card_get_internal_name(c), 1);

			if (device_name[0] == L'\0')
				break;

			if (network_card_available(c) && device_is_valid(network_card_getdevice(c), machines[temp_machine].flags)) {
				if (c == 0)
					settings_add_string(hdlg, IDC_COMBO_NET, win_get_string(IDS_2103));
				else
					settings_add_string(hdlg, IDC_COMBO_NET, (LPARAM) device_name);
				settings_list_to_device[0][d] = c;
				if ((c == 0) || (c == temp_net_card))
					settings_set_cur_sel(hdlg, IDC_COMBO_NET, d);
				d++;
			}

			c++;
		}

		settings_enable_window(hdlg, IDC_COMBO_NET, d);
		network_recalc_combos(hdlg);
		free(lptsTemp);

		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
			case IDC_COMBO_NET_TYPE:
				if (ignore_change)
					return FALSE;

				temp_net_type = settings_get_cur_sel(hdlg, IDC_COMBO_NET_TYPE);
				network_recalc_combos(hdlg);
				break;

			case IDC_COMBO_PCAP:
				if (ignore_change)
					return FALSE;

				memset(temp_pcap_dev, '\0', sizeof(temp_pcap_dev));
				strcpy(temp_pcap_dev, network_devs[settings_get_cur_sel(hdlg, IDC_COMBO_PCAP)].device);
				network_recalc_combos(hdlg);
				break;

			case IDC_COMBO_NET:
				if (ignore_change)
					return FALSE;

				temp_net_card = settings_list_to_device[0][settings_get_cur_sel(hdlg, IDC_COMBO_NET)];
				network_recalc_combos(hdlg);
				break;

			case IDC_CONFIGURE_NET:
				if (ignore_change)
					return FALSE;

				temp_net_card = settings_list_to_device[0][settings_get_cur_sel(hdlg, IDC_COMBO_NET)];
				temp_deviceconfig |= deviceconfig_open(hdlg, (void *)network_card_getdevice(temp_net_card));
				break;
		}
		return FALSE;

	case WM_SAVESETTINGS:
		temp_net_type = settings_get_cur_sel(hdlg, IDC_COMBO_NET_TYPE);
		memset(temp_pcap_dev, '\0', sizeof(temp_pcap_dev));
		strcpy(temp_pcap_dev, network_devs[settings_get_cur_sel(hdlg, IDC_COMBO_PCAP)].device);
		temp_net_card = settings_list_to_device[0][settings_get_cur_sel(hdlg, IDC_COMBO_NET)];

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
    int i = 0;

    lptsTemp = (LPTSTR) malloc(512 * sizeof(WCHAR));

    for (i = 0; i < 6; i++)
	settings_add_string(hdlg, IDC_COMBO_HD_BUS, win_get_string(IDS_4352 + i));

    for (i = 0; i < 2; i++) {
	wsprintf(lptsTemp, plat_get_string(IDS_4097), i >> 1, i & 1);
	settings_add_string(hdlg, IDC_COMBO_HD_CHANNEL, (LPARAM) lptsTemp);
    }

    for (i = 0; i < 64; i++) {
	wsprintf(lptsTemp, plat_get_string(IDS_4135), i >> 4, i & 15);
	settings_add_string(hdlg, IDC_COMBO_HD_ID, (LPARAM) lptsTemp);
    }

    for (i = 0; i < 8; i++) {
	wsprintf(lptsTemp, plat_get_string(IDS_4097), i >> 1, i & 1);
	settings_add_string(hdlg, IDC_COMBO_HD_CHANNEL_IDE, (LPARAM) lptsTemp);
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

    for (i = 0; i < 64; i++) {
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
    int i = 0, bus = 0;

    for (i = IDT_1722; i <= IDT_1723; i++)
	settings_show_window(hdlg, i, FALSE);
    settings_show_window(hdlg, IDC_COMBO_HD_CHANNEL, FALSE);
    settings_show_window(hdlg, IDC_COMBO_HD_ID, FALSE);
    settings_show_window(hdlg, IDC_COMBO_HD_CHANNEL_IDE, FALSE);

    if ((hd_listview_items > 0) || is_add_dlg) {
	bus = settings_get_cur_sel(hdlg, IDC_COMBO_HD_BUS) + 1;

	switch(bus) {
		case HDD_BUS_MFM:		/* MFM */
			settings_show_window(hdlg, IDT_1722, TRUE);
			settings_show_window(hdlg, IDC_COMBO_HD_CHANNEL, TRUE);

			if (assign_id)
				temp_hdd[lv1_current_sel].mfm_channel = next_free_binary_channel(&mfm_tracking);
			settings_set_cur_sel(hdlg, IDC_COMBO_HD_CHANNEL, is_add_dlg ? new_hdd.mfm_channel : temp_hdd[lv1_current_sel].mfm_channel);
			break;
		case HDD_BUS_XTA:		/* XTA */
			settings_show_window(hdlg, IDT_1722, TRUE);
			settings_show_window(hdlg, IDC_COMBO_HD_CHANNEL, TRUE);

			if (assign_id)
				temp_hdd[lv1_current_sel].xta_channel = next_free_binary_channel(&xta_tracking);
			settings_set_cur_sel(hdlg, IDC_COMBO_HD_CHANNEL, is_add_dlg ? new_hdd.xta_channel : temp_hdd[lv1_current_sel].xta_channel);
			break;
		case HDD_BUS_ESDI:		/* ESDI */
			settings_show_window(hdlg, IDT_1722, TRUE);
			settings_show_window(hdlg, IDC_COMBO_HD_CHANNEL, TRUE);

			if (assign_id)
				temp_hdd[lv1_current_sel].esdi_channel = next_free_binary_channel(&esdi_tracking);
			settings_set_cur_sel(hdlg, IDC_COMBO_HD_CHANNEL, is_add_dlg ? new_hdd.esdi_channel : temp_hdd[lv1_current_sel].esdi_channel);
			break;
		case HDD_BUS_IDE:		/* IDE */
		case HDD_BUS_ATAPI:		/* ATAPI */
			settings_show_window(hdlg, IDT_1722, TRUE);
			settings_show_window(hdlg, IDC_COMBO_HD_CHANNEL_IDE, TRUE);

			if (assign_id)
				temp_hdd[lv1_current_sel].ide_channel = next_free_ide_channel();
			settings_set_cur_sel(hdlg, IDC_COMBO_HD_CHANNEL_IDE, is_add_dlg ? new_hdd.ide_channel : temp_hdd[lv1_current_sel].ide_channel);
			break;
		case HDD_BUS_SCSI:		/* SCSI */
			settings_show_window(hdlg, IDT_1723, TRUE);
			settings_show_window(hdlg, IDT_1724, TRUE);
			settings_show_window(hdlg, IDC_COMBO_HD_ID, TRUE);

			if (assign_id)
				next_free_scsi_id((uint8_t *) (is_add_dlg ? &(new_hdd.scsi_id) : &(temp_hdd[lv1_current_sel].scsi_id)));
			settings_set_cur_sel(hdlg, IDC_COMBO_HD_ID, is_add_dlg ? new_hdd.scsi_id : temp_hdd[lv1_current_sel].scsi_id);
	}
    }

    settings_show_window(hdlg, IDT_1721, (hd_listview_items != 0) || is_add_dlg);
    settings_show_window(hdlg, IDC_COMBO_HD_BUS, (hd_listview_items != 0) || is_add_dlg);
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
		break;
	case 8:
		full = (*tracking & 0xFF00000000000000LL);
		full = full && (*tracking & 0x00FF000000000000LL);
		full = full && (*tracking & 0x0000FF0000000000LL);
		full = full && (*tracking & 0x000000FF00000000LL);
		full = full && (*tracking & 0x00000000FF000000LL);
		full = full && (*tracking & 0x0000000000FF0000LL);
		full = full && (*tracking & 0x000000000000FF00LL);
		full = full && (*tracking & 0x00000000000000FFLL);
		break;
    }

    return full;
}


static void
recalc_next_free_id(HWND hdlg)
{
    int i, enable_add = 0;
    int c_mfm = 0, c_esdi = 0;
    int c_xta = 0, c_ide = 0;
    int c_atapi = 0, c_scsi = 0;

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
	else if (temp_hdd[i].bus == HDD_BUS_ATAPI)
		c_atapi++;
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
				(c_ide < IDE_NUM) || (c_ide < ATAPI_NUM) || (c_scsi < SCSI_NUM));
    enable_add = enable_add && !bus_full(&mfm_tracking, 2);
    enable_add = enable_add && !bus_full(&esdi_tracking, 2);
    enable_add = enable_add && !bus_full(&xta_tracking, 2);
    enable_add = enable_add && !bus_full(&ide_tracking, 8);
    for (i = 0; i < 2; i++)
	enable_add = enable_add && !bus_full(&(scsi_tracking[i]), 8);

    settings_enable_window(hdlg, IDC_BUTTON_HDD_ADD_NEW, enable_add);
    settings_enable_window(hdlg, IDC_BUTTON_HDD_ADD, enable_add);
    settings_enable_window(hdlg, IDC_BUTTON_HDD_REMOVE,
			   (c_mfm != 0) || (c_esdi != 0) || (c_xta != 0) || (c_ide != 0) ||
			   (c_atapi != 0) || (c_scsi != 0));
}


static void
win_settings_hard_disks_update_item(HWND hdlg, int i, int column)
{
    HWND hwndList = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
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
		case HDD_BUS_ATAPI:
			wsprintf(szText, plat_get_string(IDS_4612), temp_hdd[i].ide_channel >> 1, temp_hdd[i].ide_channel & 1);
			break;
		case HDD_BUS_SCSI:
			wsprintf(szText, plat_get_string(IDS_4613), temp_hdd[i].scsi_id >> 4, temp_hdd[i].scsi_id & 15);
			break;
	}
	lvI.pszText = szText;
	lvI.iImage = 0;
    } else if (column == 1) {
	if (!strnicmp(temp_hdd[i].fn, usr_path, strlen(usr_path)))
		mbstoc16s(szText, temp_hdd[i].fn + strlen(usr_path), sizeof_w(szText));
	else
		mbstoc16s(szText, temp_hdd[i].fn, sizeof_w(szText));
	lvI.pszText = szText;
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
win_settings_hard_disks_recalc_list(HWND hdlg)
{
    LVITEM lvI;
    int i, j = 0;
    WCHAR szText[256], usr_path_w[1024];
    HWND hwndList = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);

    mbstoc16s(usr_path_w, usr_path, sizeof_w(usr_path_w));

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
			case HDD_BUS_ATAPI:
				wsprintf(szText, plat_get_string(IDS_4612), temp_hdd[i].ide_channel >> 1, temp_hdd[i].ide_channel & 1);
				break;
			case HDD_BUS_SCSI:
				wsprintf(szText, plat_get_string(IDS_4613), temp_hdd[i].scsi_id >> 4, temp_hdd[i].scsi_id & 15);
				break;
		}
		lvI.pszText = szText;
		lvI.iItem = j;
		lvI.iImage = 0;

		if (ListView_InsertItem(hwndList, &lvI) == -1)
			return FALSE;

		lvI.iSubItem = 1;
		if (!strnicmp(temp_hdd[i].fn, usr_path, strlen(usr_path)))
			mbstoc16s(szText, temp_hdd[i].fn + strlen(usr_path), sizeof_w(szText));
		else
			mbstoc16s(szText, temp_hdd[i].fn, sizeof_w(szText));
		lvI.pszText = szText;

		if (ListView_SetItem(hwndList, &lvI) == -1)
			return FALSE;

		lvI.iSubItem = 2;
		wsprintf(szText, plat_get_string(IDS_4098), temp_hdd[i].tracks);
		lvI.pszText = szText;

		if (ListView_SetItem(hwndList, &lvI) == -1)
			return FALSE;

		lvI.iSubItem = 3;
		wsprintf(szText, plat_get_string(IDS_4098), temp_hdd[i].hpc);
		lvI.pszText = szText;

		if (ListView_SetItem(hwndList, &lvI) == -1)
			return FALSE;

		lvI.iSubItem = 4;
		wsprintf(szText, plat_get_string(IDS_4098), temp_hdd[i].spt);
		lvI.pszText = szText;

		if (ListView_SetItem(hwndList, &lvI) == -1)
			return FALSE;

		lvI.iSubItem = 5;
		wsprintf(szText, plat_get_string(IDS_4098), (temp_hdd[i].tracks * temp_hdd[i].hpc * temp_hdd[i].spt) >> 11);
		lvI.pszText = szText;

		if (ListView_SetItem(hwndList, &lvI) == -1)
			return FALSE;

		j++;
	} else
		hdc_id_to_listview_index[i] = -1;
    }

    hd_listview_items = j;

    return TRUE;
}


static void
win_settings_hard_disks_resize_columns(HWND hdlg)
{
    /* Bus, File, Cylinders, Heads, Sectors, Size */
    int iCol, width[C_COLUMNS_HARD_DISKS] = {104, 177, 50, 26, 32, 50};
    int total = 0;
    HWND hwndList = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);
    RECT r;

    GetWindowRect(hwndList, &r);
    for (iCol = 0; iCol < (C_COLUMNS_HARD_DISKS - 1); iCol++) {
	width[iCol] = MulDiv(width[iCol], dpi, 96);
	total += width[iCol];
	ListView_SetColumnWidth(hwndList, iCol, MulDiv(width[iCol], dpi, 96));
    }
    width[C_COLUMNS_HARD_DISKS - 1] = (r.right - r.left) - 4 - total;
    ListView_SetColumnWidth(hwndList, C_COLUMNS_HARD_DISKS - 1, width[C_COLUMNS_HARD_DISKS - 1]);
}


static BOOL
win_settings_hard_disks_init_columns(HWND hdlg)
{
    LVCOLUMN lvc;
    int iCol;
    HWND hwndList = GetDlgItem(hdlg, IDC_LIST_HARD_DISKS);

    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;

    for (iCol = 0; iCol < C_COLUMNS_HARD_DISKS; iCol++) {
	lvc.iSubItem = iCol;
	lvc.pszText = plat_get_string(IDS_2081 + iCol);

	switch(iCol) {
		case 0: /* Bus */
			lvc.cx = 104;
			lvc.fmt = LVCFMT_LEFT;
			break;
		case 1: /* File */
			lvc.cx = 177;
			lvc.fmt = LVCFMT_LEFT;
			break;
		case 2: /* Cylinders */
			lvc.cx = 50;
			lvc.fmt = LVCFMT_RIGHT;
			break;
		case 3: /* Heads */
			lvc.cx = 26;
			lvc.fmt = LVCFMT_RIGHT;
			break;
		case 4: /* Sectors */
			lvc.cx = 32;
			lvc.fmt = LVCFMT_RIGHT;
			break;
		case 5: /* Size (MB) 8 */
			lvc.cx = 50;
			lvc.fmt = LVCFMT_RIGHT;
			break;
	}

	if (ListView_InsertColumn(hwndList, iCol, &lvc) == -1)
		return FALSE;
    }

    win_settings_hard_disks_resize_columns(hdlg);
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
set_edit_box_contents(HWND hdlg, int id, uint32_t val)
{
    HWND h;
    WCHAR szText[256];

    h = GetDlgItem(hdlg, id);
    wsprintf(szText, plat_get_string(IDS_2106), val);
    SendMessage(h, WM_SETTEXT, (WPARAM) wcslen(szText), (LPARAM) szText);
}

static void set_edit_box_text_contents(HWND hdlg, int id, WCHAR* text)
{
	HWND h = GetDlgItem(hdlg, id);
	SendMessage(h, WM_SETTEXT, (WPARAM) wcslen(text), (LPARAM) text);
}

static void get_edit_box_text_contents(HWND hdlg, int id, WCHAR* text_buffer, int buffer_size)
{
	HWND h = GetDlgItem(hdlg, id);
	SendMessage(h, WM_GETTEXT, (WPARAM) buffer_size, (LPARAM) text_buffer);
}

static int hdconf_initialize_hdt_combo(HWND hdlg)
{
    int i = 0;
    uint64_t temp_size = 0;
    uint32_t size_mb = 0;
    WCHAR szText[256];

    selection = 127;

    for (i = 0; i < 127; i++) {	
	temp_size = ((uint64_t) hdd_table[i][0]) * hdd_table[i][1] * hdd_table[i][2];
	size_mb = (uint32_t) (temp_size >> 11LL);
	wsprintf(szText, plat_get_string(IDS_2107), size_mb, hdd_table[i][0], hdd_table[i][1], hdd_table[i][2]);
	settings_add_string(hdlg, IDC_COMBO_HD_TYPE, (LPARAM) szText);
	if ((tracks == (int) hdd_table[i][0]) && (hpc == (int) hdd_table[i][1]) &&
	    (spt == (int) hdd_table[i][2]))
		selection = i;
    }
    settings_add_string(hdlg, IDC_COMBO_HD_TYPE, win_get_string(IDS_4100));
    settings_add_string(hdlg, IDC_COMBO_HD_TYPE, win_get_string(IDS_4101));
    settings_set_cur_sel(hdlg, IDC_COMBO_HD_TYPE, selection);
    return selection;
}


static void
recalc_selection(HWND hdlg)
{
    int i = 0;

    selection = 127;
    for (i = 0; i < 127; i++) {	
	if ((tracks == (int) hdd_table[i][0]) &&
	    (hpc == (int) hdd_table[i][1]) &&
	    (spt == (int) hdd_table[i][2]))
		selection = i;
    }
    if ((selection == 127) && (hpc == 16) && (spt == 63))
	selection = 128;
    settings_set_cur_sel(hdlg, IDC_COMBO_HD_TYPE, selection);
}

HWND vhd_progress_hdlg;

static void vhd_progress_callback(uint32_t current_sector, uint32_t total_sectors)
{
	MSG msg;
	HWND h = GetDlgItem(vhd_progress_hdlg, IDC_PBAR_IMG_CREATE);
	SendMessage(h, PBM_SETPOS, (WPARAM) current_sector, (LPARAM) 0);
	while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE | PM_NOYIELD)) {
		TranslateMessage(&msg); 
		DispatchMessage(&msg);
	}
}

/* If the disk geometry requested in the 86Box GUI is not compatible with the internal VHD geometry,
 * we adjust it to the next-largest size that is compatible. On average, this will be a difference
 * of about 21 MB, and should only be necessary for VHDs larger than 31.5 GB, so should never be more
 * than a tenth of a percent change in size.
 */
static void adjust_86box_geometry_for_vhd(MVHDGeom *_86box_geometry, MVHDGeom *vhd_geometry)
{
	if (_86box_geometry->cyl <= 65535) {
		vhd_geometry->cyl = _86box_geometry->cyl;
		vhd_geometry->heads = _86box_geometry->heads;
		vhd_geometry->spt = _86box_geometry->spt;
		return;
	}

	int desired_sectors = _86box_geometry->cyl * _86box_geometry->heads * _86box_geometry->spt;
	if (desired_sectors > 267321600)
		desired_sectors = 267321600;

	int remainder = desired_sectors % 85680; /* 8560 is the LCM of 1008 (63*16) and 4080 (255*16) */
	if (remainder > 0)
		desired_sectors += (85680 - remainder);

	_86box_geometry->cyl = desired_sectors / (16 * 63);
	_86box_geometry->heads = 16;
	_86box_geometry->spt = 63;

	vhd_geometry->cyl = desired_sectors / (16 * 255);
	vhd_geometry->heads = 16;
	vhd_geometry->spt = 255;
}

static void adjust_vhd_geometry_for_86box(MVHDGeom *vhd_geometry)
{
	if (vhd_geometry->spt <= 63) 
		return;

	int desired_sectors = vhd_geometry->cyl * vhd_geometry->heads * vhd_geometry->spt;
	if (desired_sectors > 267321600)
		desired_sectors = 267321600;

	int remainder = desired_sectors % 85680; /* 8560 is the LCM of 1008 (63*16) and 4080 (255*16) */
	if (remainder > 0)
		desired_sectors -= remainder;

	vhd_geometry->cyl = desired_sectors / (16 * 63);
	vhd_geometry->heads = 16;
	vhd_geometry->spt = 63;
}

static MVHDGeom create_drive_vhd_fixed(char* filename, int cyl, int heads, int spt)
{
	MVHDGeom _86box_geometry = { .cyl = cyl, .heads = heads, .spt = spt };
	MVHDGeom vhd_geometry;
	adjust_86box_geometry_for_vhd(&_86box_geometry, &vhd_geometry);

	HWND h = GetDlgItem(vhd_progress_hdlg, IDC_PBAR_IMG_CREATE);
	settings_show_window(vhd_progress_hdlg, IDT_1731, FALSE);
	settings_show_window(vhd_progress_hdlg, IDC_EDIT_HD_FILE_NAME, FALSE);
	settings_show_window(vhd_progress_hdlg, IDC_CFILE, FALSE);
	settings_show_window(vhd_progress_hdlg, IDC_PBAR_IMG_CREATE, TRUE);
	settings_enable_window(vhd_progress_hdlg, IDT_1752, TRUE);
	SendMessage(h, PBM_SETRANGE32, (WPARAM) 0, (LPARAM) vhd_geometry.cyl * vhd_geometry.heads * vhd_geometry.spt);
	SendMessage(h, PBM_SETPOS, (WPARAM) 0, (LPARAM) 0);

	int vhd_error = 0;
	MVHDMeta *vhd = mvhd_create_fixed(filename, vhd_geometry, &vhd_error, vhd_progress_callback);
	if (vhd == NULL) {
		_86box_geometry.cyl = 0;
		_86box_geometry.heads = 0;
		_86box_geometry.spt = 0;
	} else {
		mvhd_close(vhd);
	}

	return _86box_geometry;
}

static MVHDGeom create_drive_vhd_dynamic(char* filename, int cyl, int heads, int spt, int blocksize)
{
	MVHDGeom _86box_geometry = { .cyl = cyl, .heads = heads, .spt = spt };
	MVHDGeom vhd_geometry;
	adjust_86box_geometry_for_vhd(&_86box_geometry, &vhd_geometry);
	int vhd_error = 0;
	MVHDCreationOptions options;
	options.block_size_in_sectors = blocksize;
	options.path = filename;
	options.size_in_bytes = 0;
	options.geometry = vhd_geometry;
	options.type = MVHD_TYPE_DYNAMIC;

	MVHDMeta *vhd = mvhd_create_ex(options, &vhd_error);
	if (vhd == NULL) {
		_86box_geometry.cyl = 0;
		_86box_geometry.heads = 0;
		_86box_geometry.spt = 0;
	} else {
		mvhd_close(vhd);
	}

	return _86box_geometry;
}

static MVHDGeom create_drive_vhd_diff(char* filename, char* parent_filename, int blocksize)
{
	int vhd_error = 0;
	MVHDCreationOptions options;
	options.block_size_in_sectors = blocksize;
	options.path = filename;
	options.parent_path = parent_filename;
	options.type = MVHD_TYPE_DIFF;

	MVHDMeta *vhd = mvhd_create_ex(options, &vhd_error);
	MVHDGeom vhd_geometry;
	if (vhd == NULL) {
		vhd_geometry.cyl = 0;
		vhd_geometry.heads = 0;
		vhd_geometry.spt = 0;
	} else {
		vhd_geometry = mvhd_get_geometry(vhd);

		if (vhd_geometry.spt > 63) {
			vhd_geometry.cyl = mvhd_calc_size_sectors(&vhd_geometry) / (16 * 63);
			vhd_geometry.heads = 16;
			vhd_geometry.spt = 63;
		}

		mvhd_close(vhd);
	}

	return vhd_geometry;
}


#if defined(__amd64__) || defined(__aarch64__)
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
	uint64_t r = 0;
	char *big_buf;
	char hd_file_name_multibyte[1200];
	int b = 0;
	int vhd_error = 0;
	uint8_t channel = 0;
	uint8_t id = 0;
	wchar_t *twcs;
	int img_format, block_size;
	WCHAR text_buf[256];
	RECT rect;
	POINT point;
	int dlg_height_adjust;

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
		
		settings_add_string(hdlg, IDC_COMBO_HD_IMG_FORMAT, win_get_string(IDS_4122));
		settings_add_string(hdlg, IDC_COMBO_HD_IMG_FORMAT, win_get_string(IDS_4123));
		settings_add_string(hdlg, IDC_COMBO_HD_IMG_FORMAT, win_get_string(IDS_4124));
		settings_add_string(hdlg, IDC_COMBO_HD_IMG_FORMAT, win_get_string(IDS_4125));
		settings_add_string(hdlg, IDC_COMBO_HD_IMG_FORMAT, win_get_string(IDS_4126));
		settings_add_string(hdlg, IDC_COMBO_HD_IMG_FORMAT, win_get_string(IDS_4127)); 	
		settings_set_cur_sel(hdlg, IDC_COMBO_HD_IMG_FORMAT, 0);

		settings_add_string(hdlg, IDC_COMBO_HD_BLOCK_SIZE, win_get_string(IDS_4128));
		settings_add_string(hdlg, IDC_COMBO_HD_BLOCK_SIZE, win_get_string(IDS_4129));
		settings_set_cur_sel(hdlg, IDC_COMBO_HD_BLOCK_SIZE, 0);

		settings_show_window(hdlg, IDC_COMBO_HD_BLOCK_SIZE, FALSE);
		settings_show_window(hdlg, IDT_1775, FALSE);

		if (existing & 1) {
			settings_enable_window(hdlg, IDC_EDIT_HD_SPT, FALSE);
			settings_enable_window(hdlg, IDC_EDIT_HD_HPC, FALSE);
			settings_enable_window(hdlg, IDC_EDIT_HD_CYL, FALSE);
			settings_enable_window(hdlg, IDC_EDIT_HD_SIZE, FALSE);
			settings_enable_window(hdlg, IDC_COMBO_HD_TYPE, FALSE);
			settings_show_window(hdlg, IDC_COMBO_HD_IMG_FORMAT, FALSE);
			settings_show_window(hdlg, IDT_1774, FALSE);
			
			/* adjust window size */
			GetWindowRect(hdlg, &rect);
			OffsetRect(&rect, -rect.left, -rect.top);
			dlg_height_adjust = rect.bottom / 5;
			SetWindowPos(hdlg, NULL, 0, 0, rect.right, rect.bottom - dlg_height_adjust, SWP_NOMOVE | SWP_NOREPOSITION | SWP_NOZORDER);
			h = GetDlgItem(hdlg, IDOK);
			GetWindowRect(h, &rect);
			point.x = rect.left;
			point.y = rect.top;
			ScreenToClient(hdlg, &point);
			SetWindowPos(h, NULL, point.x, point.y - dlg_height_adjust, 0, 0, SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOZORDER);
			h = GetDlgItem(hdlg, IDCANCEL);
			GetWindowRect(h, &rect);
			point.x = rect.left;
			point.y = rect.top;
			ScreenToClient(hdlg, &point);
			SetWindowPos(h, NULL, point.x, point.y - dlg_height_adjust, 0, 0, SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOZORDER);

			chs_enabled = 0;
		} else
			chs_enabled = 1;

		add_locations(hdlg);
		hdd_ptr->bus = HDD_BUS_IDE;
		max_spt = 63;
		max_hpc = 255;
		settings_set_cur_sel(hdlg, IDC_COMBO_HD_BUS, hdd_ptr->bus - 1);
		max_tracks = 266305;
		recalc_location_controls(hdlg, 1, 0);

		channel = next_free_ide_channel();
		next_free_scsi_id(&id);
		settings_set_cur_sel(hdlg, IDC_COMBO_HD_CHANNEL, 0);
		settings_set_cur_sel(hdlg, IDC_COMBO_HD_ID, id);
		settings_set_cur_sel(hdlg, IDC_COMBO_HD_CHANNEL_IDE, channel);

		new_hdd.mfm_channel = next_free_binary_channel(&mfm_tracking);
		new_hdd.esdi_channel = next_free_binary_channel(&esdi_tracking);
		new_hdd.xta_channel = next_free_binary_channel(&xta_tracking);
		new_hdd.ide_channel = channel;
		new_hdd.scsi_id = id;

		settings_enable_window(hdlg, IDC_EDIT_HD_FILE_NAME, FALSE);
		settings_show_window(hdlg, IDT_1752, FALSE);
		settings_show_window(hdlg, IDC_PBAR_IMG_CREATE, FALSE);

		no_update = 0;
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
			case IDOK:
				hdd_ptr->bus = settings_get_cur_sel(hdlg, IDC_COMBO_HD_BUS) + 1;

				/* Make sure no file name is allowed with removable SCSI hard disks. */
				if (wcslen(hd_file_name) == 0) {
					hdd_ptr->bus = HDD_BUS_DISABLED;
					settings_msgbox_header(MBX_ERROR, (wchar_t *) IDS_2130, (wchar_t *) IDS_4112);
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
						hdd_ptr->mfm_channel = settings_get_cur_sel(hdlg, IDC_COMBO_HD_CHANNEL);
						break;
					case HDD_BUS_ESDI:
						hdd_ptr->esdi_channel = settings_get_cur_sel(hdlg, IDC_COMBO_HD_CHANNEL);
						break;
					case HDD_BUS_XTA:
						hdd_ptr->xta_channel = settings_get_cur_sel(hdlg, IDC_COMBO_HD_CHANNEL);
						break;
					case HDD_BUS_IDE:
					case HDD_BUS_ATAPI:
						hdd_ptr->ide_channel = settings_get_cur_sel(hdlg, IDC_COMBO_HD_CHANNEL_IDE);
						break;
					case HDD_BUS_SCSI:
						hdd_ptr->scsi_id = settings_get_cur_sel(hdlg, IDC_COMBO_HD_ID);
						break;
				}

				memset(hdd_ptr->fn, 0, sizeof(hdd_ptr->fn));
				c16stombs(hdd_ptr->fn, hd_file_name, sizeof(hdd_ptr->fn));
				strcpy(hd_file_name_multibyte, hdd_ptr->fn);

				sector_size = 512;

				if (!(existing & 1) && (wcslen(hd_file_name) > 0)) {
					if (size > 0x1FFFFFFE00ll) {
						settings_msgbox_header(MBX_ERROR, (wchar_t *) IDS_4116, (wchar_t *) IDS_4105);
						return TRUE;
					}

					img_format = settings_get_cur_sel(hdlg, IDC_COMBO_HD_IMG_FORMAT);
					if (img_format < 3) {
						f = _wfopen(hd_file_name, L"wb");
					} else {
						f = (FILE *) 0;
					}

					if (img_format == 1) { /* HDI file */
						if (size >= 0x100000000ll) {
							fclose(f);
							settings_msgbox_header(MBX_ERROR, (wchar_t *) IDS_4116, (wchar_t *) IDS_4104);
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
					} else if (img_format == 2) { /* HDX file */
						fwrite(&signature, 1, 8, f);		/* 00000000: Signature */
						fwrite(&size, 1, 8, f);			/* 00000008: Full size of the data (64-bit) */
						fwrite(&sector_size, 1, 4, f);		/* 00000010: Sector size in bytes */
						fwrite(&spt, 1, 4, f);			/* 00000014: Sectors per cylinder */
						fwrite(&hpc, 1, 4, f);			/* 00000018: Heads per cylinder */
						fwrite(&tracks, 1, 4, f);		/* 0000001C: Cylinders */
						fwrite(&zero, 1, 4, f);			/* 00000020: [Translation] Sectors per cylinder */
						fwrite(&zero, 1, 4, f);			/* 00000004: [Translation] Heads per cylinder */
					} else if (img_format >= 3) { /* VHD file */
						MVHDGeom _86box_geometry;
						block_size = settings_get_cur_sel(hdlg, IDC_COMBO_HD_BLOCK_SIZE) == 0 ? MVHD_BLOCK_LARGE : MVHD_BLOCK_SMALL;
						switch (img_format) {
							case 3:
								vhd_progress_hdlg = hdlg;
								_86box_geometry = create_drive_vhd_fixed(hd_file_name_multibyte, tracks, hpc, spt);
								break;
							case 4:
								_86box_geometry = create_drive_vhd_dynamic(hd_file_name_multibyte, tracks, hpc, spt, block_size);
								break;
							case 5:
								if (file_dlg_w(hdlg, plat_get_string(IDS_4130), L"", plat_get_string(IDS_4131), 0)) {
									return TRUE;
								}
								_86box_geometry = create_drive_vhd_diff(hd_file_name_multibyte, openfilestring, block_size);
								break;
						}

						if (img_format != 5)
							settings_msgbox_header(MBX_INFO, (wchar_t *) IDS_4113, (wchar_t *) IDS_4117);

						hdd_ptr->tracks = _86box_geometry.cyl;
						hdd_ptr->hpc = _86box_geometry.heads;
						hdd_ptr->spt = _86box_geometry.spt;						

						hard_disk_added = 1;
						EndDialog(hdlg, 0);
						return TRUE;
					} 

					big_buf = (char *) malloc(1048576);
					memset(big_buf, 0, 1048576);

					r = size >> 20;
					size &= 0xfffff;

					if (size || r) {
						settings_show_window(hdlg, IDT_1731, FALSE);
						settings_show_window(hdlg, IDC_EDIT_HD_FILE_NAME, FALSE);
						settings_show_window(hdlg, IDC_CFILE, FALSE);
						settings_show_window(hdlg, IDC_PBAR_IMG_CREATE, TRUE);
						settings_enable_window(hdlg, IDT_1752, TRUE);

						h = GetDlgItem(hdlg, IDC_PBAR_IMG_CREATE);
						SendMessage(h, PBM_SETRANGE32, (WPARAM) 0, (LPARAM) r);
						SendMessage(h, PBM_SETPOS, (WPARAM) 0, (LPARAM) 0);
					}

					h = GetDlgItem(hdlg, IDC_PBAR_IMG_CREATE);

					if (size) {
						if (f) {
							fwrite(big_buf, 1, size, f);
						}
						SendMessage(h, PBM_SETPOS, (WPARAM) 1, (LPARAM) 0);
					}

					if (r) {
						for (i = 0; i < r; i++) {
							if (f) {
								fwrite(big_buf, 1, 1048576, f);
							}
							SendMessage(h, PBM_SETPOS, (WPARAM) (i + 1), (LPARAM) 0);

							settings_process_messages();
						}
					}

					free(big_buf);

					if (f) {
						fclose(f);
					}
					settings_msgbox_header(MBX_INFO, (wchar_t *) IDS_4113, (wchar_t *) IDS_4117);
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
				if (!file_dlg_w(hdlg, plat_get_string(IDS_4106), L"", NULL, !(existing & 1))) {
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
							if (settings_msgbox_ex(MBX_QUESTION_YN, (wchar_t *) IDS_4111, (wchar_t *) IDS_4118, (wchar_t *) IDS_4120, (wchar_t *) IDS_4121, NULL) != 0)	/* yes */
								return FALSE;
						}
					}

					f = _wfopen(wopenfilestring, (existing & 1) ? L"rb" : L"wb");
					if (f == NULL) {
hdd_add_file_open_error:
						fclose(f);
						settings_msgbox_header(MBX_ERROR, (existing & 1) ? (wchar_t *) IDS_4114 : (wchar_t *) IDS_4115, (existing & 1) ? (wchar_t *) IDS_4107 : (wchar_t *) IDS_4108);
						return TRUE;
					}
					if (existing & 1) {
						if (image_is_hdi(openfilestring) || image_is_hdx(openfilestring, 1)) {
							fseeko64(f, 0x10, SEEK_SET);
							fread(&sector_size, 1, 4, f);
							if (sector_size != 512) {
								settings_msgbox_header(MBX_ERROR, (wchar_t *) IDS_4119, (wchar_t *) IDS_4109);
								fclose(f);
								return TRUE;
							}
							spt = hpc = tracks = 0;
							fread(&spt, 1, 4, f);
							fread(&hpc, 1, 4, f);
							fread(&tracks, 1, 4, f);
						} else if (image_is_vhd(openfilestring, 1)) {
							fclose(f);
							MVHDMeta* vhd = mvhd_open(openfilestring, 0, &vhd_error);
							if (vhd == NULL) {
								settings_msgbox_header(MBX_ERROR, (existing & 1) ? (wchar_t *) IDS_4114 : (wchar_t *) IDS_4115, (existing & 1) ? (wchar_t *) IDS_4107 : (wchar_t *) IDS_4108);
								return TRUE;
							} else if (vhd_error == MVHD_ERR_TIMESTAMP) {								
								if (settings_msgbox_ex(MBX_QUESTION_YN | MBX_WARNING, plat_get_string(IDS_4133), plat_get_string(IDS_4132), NULL, NULL, NULL) != 0) {
									int ts_res = mvhd_diff_update_par_timestamp(vhd, &vhd_error);
									if (ts_res != 0) {
										settings_msgbox_header(MBX_ERROR, plat_get_string(IDS_2049), plat_get_string(IDS_4134));
										mvhd_close(vhd);
										return TRUE;
									}
								} else {
									mvhd_close(vhd);
									return TRUE;
								}
							}

							MVHDGeom vhd_geom = mvhd_get_geometry(vhd);
							adjust_vhd_geometry_for_86box(&vhd_geom);
							tracks = vhd_geom.cyl;
							hpc = vhd_geom.heads;
							spt = vhd_geom.spt;
							size = (uint64_t)tracks * hpc * spt * 512;
							mvhd_close(vhd);
						} else {
							fseeko64(f, 0, SEEK_END);
							size = ftello64(f);
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

						settings_enable_window(hdlg, IDC_EDIT_HD_SPT, TRUE);
						settings_enable_window(hdlg, IDC_EDIT_HD_HPC, TRUE);
						settings_enable_window(hdlg, IDC_EDIT_HD_CYL, TRUE);
						settings_enable_window(hdlg, IDC_EDIT_HD_SIZE, TRUE);
						settings_enable_window(hdlg, IDC_COMBO_HD_TYPE, TRUE);

						chs_enabled = 1;

						no_update = 0;
					}

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
					size = ((uint64_t) tracks * (uint64_t) hpc * (uint64_t) spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				if (tracks > max_tracks) {
					tracks = max_tracks;
					size = ((uint64_t) tracks * (uint64_t) hpc * (uint64_t) spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, (uint32_t) tracks);
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
					size = ((uint64_t) tracks * (uint64_t) hpc * (uint64_t) spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				if (hpc > max_hpc) {
					hpc = max_hpc;
					size = ((uint64_t) tracks * (uint64_t) hpc * (uint64_t) spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, (uint32_t) hpc);
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
					size = ((uint64_t) tracks * (uint64_t) hpc * (uint64_t) spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				if (spt > max_spt) {
					spt = max_spt;
					size = ((uint64_t) tracks * (uint64_t) hpc * (uint64_t) spt) << 9LL;
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
					set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, (uint32_t) tracks);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, (uint32_t) hpc);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, (uint32_t) spt);
					recalc_selection(hdlg);
				}

				if (tracks > max_tracks) {
					tracks = max_tracks;
					size = ((uint64_t) tracks * (uint64_t) hpc * (uint64_t) spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, (uint32_t) tracks);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				if (hpc > max_hpc) {
					hpc = max_hpc;
					size = ((uint64_t) tracks * (uint64_t) hpc * (uint64_t) spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, (uint32_t) hpc);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				if (spt > max_spt) {
					spt = max_spt;
					size = ((uint64_t) tracks * (uint64_t) hpc * (uint64_t) spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, (uint32_t) spt);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				no_update = 0;
				break;

			case IDC_COMBO_HD_TYPE:
				if (no_update)
					return FALSE;

				no_update = 1;
				temp = settings_get_cur_sel(hdlg, IDC_COMBO_HD_TYPE);
				if ((temp != selection) && (temp != 127) && (temp != 128)) {
					selection = temp;
					tracks = hdd_table[selection][0];
					hpc = hdd_table[selection][1];
					spt = hdd_table[selection][2];
					size = ((uint64_t) tracks * (uint64_t) hpc * (uint64_t) spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, (uint32_t) tracks);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, (uint32_t) hpc);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, (uint32_t) spt);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
				} else if ((temp != selection) && (temp == 127))
					selection = temp;
				else if ((temp != selection) && (temp == 128)) {
					selection = temp;
					hpc = 16;
					spt = 63;
					size = ((uint64_t) tracks * (uint64_t) hpc * (uint64_t) spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, (uint32_t) hpc);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, (uint32_t) spt);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
				}

				if (spt > max_spt) {
					spt = max_spt;
					size = ((uint64_t) tracks * (uint64_t) hpc * (uint64_t) spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, (uint32_t) spt);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				if (hpc > max_hpc) {
					hpc = max_hpc;
					size = ((uint64_t) tracks * (uint64_t) hpc * (uint64_t) spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, (uint32_t) hpc);
					set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
					recalc_selection(hdlg);
				}

				if (tracks > max_tracks) {
					tracks = max_tracks;
					size = ((uint64_t) tracks * (uint64_t) hpc * (uint64_t) spt) << 9LL;
					set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, (uint32_t) tracks);
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
				b = settings_get_cur_sel(hdlg, IDC_COMBO_HD_BUS) + 1;
				if (b != hdd_ptr->bus) {
					hdd_ptr->bus = b;

					switch(hdd_ptr->bus) {
						case HDD_BUS_DISABLED:
						default:
							max_spt = max_hpc = max_tracks = 0;
							break;
						case HDD_BUS_MFM:
							max_spt = 26;	/* 17 for MFM, 26 for RLL. */
							max_hpc = 15;
							max_tracks = 2047;
							break;
						case HDD_BUS_XTA:
							max_spt = 63;
							max_hpc = 16;
							max_tracks = 1023;
							break;
						case HDD_BUS_ESDI:
							max_spt = 99;	/* ESDI drives usually had 32 to 43 sectors per track. */
							max_hpc = 16;
							max_tracks = 266305;
							break;
						case HDD_BUS_IDE:
							max_spt = 63;
							max_hpc = 255;
							max_tracks = 266305;
							break;
						case HDD_BUS_ATAPI:
						case HDD_BUS_SCSI:
							max_spt = 99;
							max_hpc = 255;
							max_tracks = 266305;
							break;
					}

					if (!chs_enabled) {
						settings_enable_window(hdlg, IDC_EDIT_HD_SPT, FALSE);
						settings_enable_window(hdlg, IDC_EDIT_HD_HPC, FALSE);
						settings_enable_window(hdlg, IDC_EDIT_HD_CYL, FALSE);
						settings_enable_window(hdlg, IDC_EDIT_HD_SIZE, FALSE);
						settings_enable_window(hdlg, IDC_COMBO_HD_TYPE, FALSE);
					}

					if (spt > max_spt) {
						spt = max_spt;
						size = ((uint64_t) tracks * (uint64_t) hpc * (uint64_t) spt) << 9LL;
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, (uint32_t) spt);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
						recalc_selection(hdlg);
					}

					if (hpc > max_hpc) {
						hpc = max_hpc;
						size = ((uint64_t) tracks * (uint64_t) hpc * (uint64_t) spt) << 9LL;
						set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, (uint32_t) hpc);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
						recalc_selection(hdlg);
					}

					if (tracks > max_tracks) {
						tracks = max_tracks;
						size = ((uint64_t) tracks * (uint64_t) hpc * (uint64_t) spt) << 9LL;
						set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, (uint32_t) tracks);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) (size >> 20));
						recalc_selection(hdlg);
					}
				}

				no_update = 0;
				break;
			case IDC_COMBO_HD_IMG_FORMAT:
				img_format = settings_get_cur_sel(hdlg, IDC_COMBO_HD_IMG_FORMAT);

				no_update = 1;
				if (img_format == 5) { /* They switched to a diff VHD; disable the geometry fields. */
					settings_enable_window(hdlg, IDC_EDIT_HD_SPT, FALSE);
					set_edit_box_text_contents(hdlg, IDC_EDIT_HD_SPT, L"(N/A)");
					settings_enable_window(hdlg, IDC_EDIT_HD_HPC, FALSE);
					set_edit_box_text_contents(hdlg, IDC_EDIT_HD_HPC, L"(N/A)");
					settings_enable_window(hdlg, IDC_EDIT_HD_CYL, FALSE);
					set_edit_box_text_contents(hdlg, IDC_EDIT_HD_CYL, L"(N/A)");
					settings_enable_window(hdlg, IDC_EDIT_HD_SIZE, FALSE);
					set_edit_box_text_contents(hdlg, IDC_EDIT_HD_SIZE, L"(N/A)");
					settings_enable_window(hdlg, IDC_COMBO_HD_TYPE, FALSE);
					settings_reset_content(hdlg, IDC_COMBO_HD_TYPE);
					settings_add_string(hdlg, IDC_COMBO_HD_TYPE, (LPARAM) L"(use parent)");
					settings_set_cur_sel(hdlg, IDC_COMBO_HD_TYPE, 0);
				} else {
					get_edit_box_text_contents(hdlg, IDC_EDIT_HD_SPT, text_buf, 256);
					if (!wcscmp(text_buf, L"(N/A)")) {
						settings_enable_window(hdlg, IDC_EDIT_HD_SPT, TRUE);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SPT, 17);
						spt = 17;
						settings_enable_window(hdlg, IDC_EDIT_HD_HPC, TRUE);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_HPC, 15);
						hpc = 15;
						settings_enable_window(hdlg, IDC_EDIT_HD_CYL, TRUE);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_CYL, 1023);
						tracks = 1023;
						settings_enable_window(hdlg, IDC_EDIT_HD_SIZE, TRUE);
						set_edit_box_contents(hdlg, IDC_EDIT_HD_SIZE, (uint32_t) ((uint64_t)17 * 15 * 1023 * 512 >> 20));
						size = (uint64_t)17 * 15 * 1023 * 512;
						
						settings_reset_content(hdlg, IDC_COMBO_HD_TYPE);
						hdconf_initialize_hdt_combo(hdlg);
						settings_enable_window(hdlg, IDC_COMBO_HD_TYPE, TRUE);						
					}
				}
				no_update = 0;

				if (img_format == 4 || img_format == 5) { /* For dynamic and diff VHDs, show the block size dropdown. */
					settings_show_window(hdlg, IDC_COMBO_HD_BLOCK_SIZE, TRUE);
					settings_show_window(hdlg, IDT_1775, TRUE);
				} else { /* Hide it otherwise. */
					settings_show_window(hdlg, IDC_COMBO_HD_BLOCK_SIZE, FALSE);
					settings_show_window(hdlg, IDT_1775, FALSE);
				}
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
	case HDD_BUS_ATAPI:
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
	case HDD_BUS_ATAPI:
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


#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_hard_disks_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    int old_sel = 0, b = 0, assign = 0;
    const uint8_t hd_icons[2] = { 80, 0 };

    switch (message) {
	case WM_INITDIALOG:
		ignore_change = 1;

		normalize_hd_list();	/* Normalize the hard disks so that non-disabled hard disks start from index 0, and so they are contiguous.
					   This will cause an emulator reset prompt on the first opening of this category with a messy hard disk list
					   (which can only happen by manually editing the configuration file). */
		win_settings_hard_disks_init_columns(hdlg);
		image_list_init(hdlg, IDC_LIST_HARD_DISKS, (const uint8_t *) hd_icons);
		win_settings_hard_disks_recalc_list(hdlg);
		recalc_next_free_id(hdlg);
		add_locations(hdlg);
		if (hd_listview_items > 0) {
			settings_listview_select(hdlg, IDC_LIST_HARD_DISKS, 0);
			lv1_current_sel = 0;
			settings_set_cur_sel(hdlg, IDC_COMBO_HD_BUS, temp_hdd[0].bus - 1);
		} else
			lv1_current_sel = -1;
		recalc_location_controls(hdlg, 0, 0);

		settings_listview_enable_styles(hdlg, IDC_LIST_HARD_DISKS);

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
			ignore_change = 1;
			settings_set_cur_sel(hdlg, IDC_COMBO_HD_BUS, temp_hdd[lv1_current_sel].bus - 1);
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
				b = settings_get_cur_sel(hdlg, IDC_COMBO_HD_BUS) + 1;
				if (b != temp_hdd[lv1_current_sel].bus) {
					hard_disk_untrack(lv1_current_sel);
					assign = (temp_hdd[lv1_current_sel].bus == b) ? 0 : 1;
					temp_hdd[lv1_current_sel].bus = b;
					recalc_location_controls(hdlg, 0, assign);
					hard_disk_track(lv1_current_sel);
					win_settings_hard_disks_update_item(hdlg, lv1_current_sel, 0);
				}
				ignore_change = 0;
				return FALSE;

			case IDC_COMBO_HD_CHANNEL:
				ignore_change = 1;
				hard_disk_untrack(lv1_current_sel);
				temp_hdd[lv1_current_sel].channel = settings_get_cur_sel(hdlg, IDC_COMBO_HD_CHANNEL);
				hard_disk_track(lv1_current_sel);
				win_settings_hard_disks_update_item(hdlg, lv1_current_sel, 0);
				ignore_change = 0;
				return FALSE;

			case IDC_COMBO_HD_CHANNEL_IDE:
				ignore_change = 1;
				hard_disk_untrack(lv1_current_sel);
				temp_hdd[lv1_current_sel].ide_channel = settings_get_cur_sel(hdlg, IDC_COMBO_HD_CHANNEL_IDE);
				hard_disk_track(lv1_current_sel);
				win_settings_hard_disks_update_item(hdlg, lv1_current_sel, 0);
				ignore_change = 0;
				return FALSE;

			case IDC_COMBO_HD_ID:
				ignore_change = 1;
				hard_disk_untrack(lv1_current_sel);
				temp_hdd[lv1_current_sel].scsi_id = settings_get_cur_sel(hdlg, IDC_COMBO_HD_ID);
				hard_disk_track(lv1_current_sel);
				win_settings_hard_disks_update_item(hdlg, lv1_current_sel, 0);
				ignore_change = 0;
				return FALSE;

			case IDC_BUTTON_HDD_ADD:
			case IDC_BUTTON_HDD_ADD_NEW:
				hard_disk_add_open(hdlg, (LOWORD(wParam) == IDC_BUTTON_HDD_ADD));
				if (hard_disk_added) {
					ignore_change = 1;
					win_settings_hard_disks_recalc_list(hdlg);
					recalc_next_free_id(hdlg);
					hard_disk_track_all();
					ignore_change = 0;
				}
				return FALSE;

			case IDC_BUTTON_HDD_REMOVE:
				temp_hdd[lv1_current_sel].fn[0] = '\0';
				hard_disk_untrack(lv1_current_sel);
				temp_hdd[lv1_current_sel].bus = HDD_BUS_DISABLED;	/* Only set the bus to zero, the list normalize code below will take care of turning this entire entry to a complete zero. */
				normalize_hd_list();			/* Normalize the hard disks so that non-disabled hard disks start from index 0, and so they are contiguous. */
				ignore_change = 1;
				win_settings_hard_disks_recalc_list(hdlg);
				recalc_next_free_id(hdlg);
				if (hd_listview_items > 0) {
					settings_listview_select(hdlg, IDC_LIST_HARD_DISKS, 0);
					lv1_current_sel = 0;
					settings_set_cur_sel(hdlg, IDC_COMBO_HD_BUS, temp_hdd[0].bus - 1);
				} else
					lv1_current_sel = -1;
				recalc_location_controls(hdlg, 0, 0);
				ignore_change = 0;
				return FALSE;
		}

	case WM_DPICHANGED_AFTERPARENT:
		win_settings_hard_disks_resize_columns(hdlg);
		image_list_init(hdlg, IDC_LIST_HARD_DISKS, (const uint8_t *) hd_icons);
		break;
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
win_settings_floppy_drives_recalc_list(HWND hdlg)
{
    LVITEM lvI;
    int i = 0;
    char s[256], *t;
    WCHAR szText[256];
    HWND hwndList = GetDlgItem(hdlg, IDC_LIST_FLOPPY_DRIVES);

    lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
    lvI.stateMask = lvI.state = 0;

    for (i = 0; i < 4; i++) {
	lvI.iSubItem = 0;
	if (temp_fdd_types[i] > 0) {
		t = fdd_getname(temp_fdd_types[i]);
		strncpy(s, t, sizeof(s) - 1);
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
win_settings_cdrom_drives_recalc_list(HWND hdlg)
{
    LVITEM lvI;
    int i = 0, fsid = 0;
    WCHAR szText[256];
    HWND hwndList = GetDlgItem(hdlg, IDC_LIST_CDROM_DRIVES);

    lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
    lvI.stateMask = lvI.iSubItem = lvI.state = 0;

    for (i = 0; i < 4; i++) {
	fsid = combo_id_to_format_string_id(temp_cdrom[i].bus_type);

	lvI.iSubItem = 0;
	switch (temp_cdrom[i].bus_type) {
		case CDROM_BUS_DISABLED:
		default:
			lvI.pszText = plat_get_string(fsid);
			lvI.iImage = 0;
			break;
		case CDROM_BUS_ATAPI:
			wsprintf(szText, plat_get_string(fsid), temp_cdrom[i].ide_channel >> 1, temp_cdrom[i].ide_channel & 1);
			lvI.pszText = szText;
			lvI.iImage = 1;
			break;
		case CDROM_BUS_SCSI:
			wsprintf(szText, plat_get_string(fsid), temp_cdrom[i].scsi_device_id >> 4, temp_cdrom[i].scsi_device_id & 15);
			lvI.pszText = szText;
			lvI.iImage = 1;
			break;
	}

	lvI.iItem = i;

	if (ListView_InsertItem(hwndList, &lvI) == -1)
		return FALSE;

	lvI.iSubItem = 1;
	if (temp_cdrom[i].bus_type == CDROM_BUS_DISABLED)
		lvI.pszText = plat_get_string(IDS_2103);
	else {
		wsprintf(szText, L"%ix", temp_cdrom[i].speed);
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
win_settings_mo_drives_recalc_list(HWND hdlg)
{
    LVITEM lvI;
    int i = 0, fsid = 0;
    WCHAR szText[256];
    char szType[30];
    HWND hwndList = GetDlgItem(hdlg, IDC_LIST_MO_DRIVES);

    lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
	lvI.stateMask = lvI.iSubItem = lvI.state = 0;

    for (i = 0; i < MO_NUM; i++) {
	fsid = combo_id_to_format_string_id(temp_mo_drives[i].bus_type);
	
	lvI.iSubItem = 0;
	switch (temp_mo_drives[i].bus_type) {
		case MO_BUS_DISABLED:
		default:
			lvI.pszText = plat_get_string(fsid);
			lvI.iImage = 0;
			break;
		case MO_BUS_ATAPI:
			wsprintf(szText, plat_get_string(fsid), temp_mo_drives[i].ide_channel >> 1, temp_mo_drives[i].ide_channel & 1);
			lvI.pszText = szText;
			lvI.iImage = 1;
			break;
		case MO_BUS_SCSI:
			wsprintf(szText, plat_get_string(fsid), temp_mo_drives[i].scsi_device_id >> 4, temp_mo_drives[i].scsi_device_id & 15);
			lvI.pszText = szText;
			lvI.iImage = 1;
			break;
	}

	lvI.iItem = i;

	if (ListView_InsertItem(hwndList, &lvI) == -1)
		return FALSE;

	lvI.iSubItem = 1;
	if (temp_mo_drives[i].bus_type == MO_BUS_DISABLED)
		lvI.pszText = plat_get_string(IDS_2103);
	else {
		memset(szType, 0, 30);
		memcpy(szType, mo_drive_types[temp_mo_drives[i].type].vendor, 8);
		szType[strlen(szType)] = ' ';
		memcpy(szType + strlen(szType), mo_drive_types[temp_mo_drives[i].type].model, 16);
		szType[strlen(szType)] = ' ';
		memcpy(szType + strlen(szType), mo_drive_types[temp_mo_drives[i].type].revision, 4);

		mbstowcs(szText, szType, strlen(szType)+1);
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
win_settings_zip_drives_recalc_list(HWND hdlg)
{
    LVITEM lvI;
    int i = 0, fsid = 0;
    WCHAR szText[256];
    HWND hwndList = GetDlgItem(hdlg, IDC_LIST_ZIP_DRIVES);

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
			wsprintf(szText, plat_get_string(fsid), temp_zip_drives[i].scsi_device_id >> 4, temp_zip_drives[i].scsi_device_id & 15);
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


static void
win_settings_floppy_drives_resize_columns(HWND hdlg)
{
    int iCol, width[3] = {292, 58, 89};
    int total = 0;
    HWND hwndList = GetDlgItem(hdlg, IDC_LIST_FLOPPY_DRIVES);
    RECT r;

    GetWindowRect(hwndList, &r);
    for (iCol = 0; iCol < 2; iCol++) {
	width[iCol] = MulDiv(width[iCol], dpi, 96);
	total += width[iCol];
	ListView_SetColumnWidth(hwndList, iCol, MulDiv(width[iCol], dpi, 96));
    }
    width[2] = (r.right - r.left) - 4 - total;
    ListView_SetColumnWidth(hwndList, 2, width[2]);
}


static BOOL
win_settings_floppy_drives_init_columns(HWND hdlg)
{
    LVCOLUMN lvc;
    HWND hwndList = GetDlgItem(hdlg, IDC_LIST_FLOPPY_DRIVES);

    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;

    lvc.iSubItem = 0;
    lvc.pszText = plat_get_string(IDS_2092);

    lvc.cx = 292;
    lvc.fmt = LVCFMT_LEFT;

    if (ListView_InsertColumn(hwndList, 0, &lvc) == -1)
	return FALSE;

    lvc.iSubItem = 1;
    lvc.pszText = plat_get_string(IDS_2059);

    lvc.cx = 58;
    lvc.fmt = LVCFMT_LEFT;

    if (ListView_InsertColumn(hwndList, 1, &lvc) == -1)
	return FALSE;

    lvc.iSubItem = 2;
    lvc.pszText = plat_get_string(IDS_2087);

    lvc.cx = 89;
    lvc.fmt = LVCFMT_LEFT;

    if (ListView_InsertColumn(hwndList, 2, &lvc) == -1)
		return FALSE;

    win_settings_floppy_drives_resize_columns(hdlg);
    return TRUE;
}


static void
win_settings_cdrom_drives_resize_columns(HWND hdlg)
{
    int width[2] = {292, 147};
    HWND hwndList = GetDlgItem(hdlg, IDC_LIST_CDROM_DRIVES);
    RECT r;

    GetWindowRect(hwndList, &r);
    width[0] = MulDiv(width[0], dpi, 96);
    ListView_SetColumnWidth(hwndList, 0, MulDiv(width[0], dpi, 96));
    width[1] = (r.right - r.left) - 4 - width[0];
    ListView_SetColumnWidth(hwndList, 1, width[1]);
}


static BOOL
win_settings_cdrom_drives_init_columns(HWND hdlg)
{
    LVCOLUMN lvc;
    HWND hwndList = GetDlgItem(hdlg, IDC_LIST_CDROM_DRIVES);

    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;

    lvc.iSubItem = 0;
    lvc.pszText = plat_get_string(IDS_2081);

    lvc.cx = 292;
    lvc.fmt = LVCFMT_LEFT;

    if (ListView_InsertColumn(hwndList, 0, &lvc) == -1)
	return FALSE;

    lvc.iSubItem = 1;
    lvc.pszText = plat_get_string(IDS_2053);

    lvc.cx = 147;
    lvc.fmt = LVCFMT_LEFT;

    if (ListView_InsertColumn(hwndList, 1, &lvc) == -1)
		return FALSE;

    win_settings_cdrom_drives_resize_columns(hdlg);
    return TRUE;
}


static void
win_settings_mo_drives_resize_columns(HWND hdlg)
{
    int width[2] = {292, 147};
    HWND hwndList = GetDlgItem(hdlg, IDC_LIST_MO_DRIVES);
    RECT r;

    GetWindowRect(hwndList, &r);
    width[0] = MulDiv(width[0], dpi, 96);
    ListView_SetColumnWidth(hwndList, 0, MulDiv(width[0], dpi, 96));
    width[1] = (r.right - r.left) - 4 - width[0];
    ListView_SetColumnWidth(hwndList, 1, width[1]);
}


static BOOL
win_settings_mo_drives_init_columns(HWND hdlg)
{
    LVCOLUMN lvc;
    HWND hwndList = GetDlgItem(hdlg, IDC_LIST_MO_DRIVES);

    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;

    lvc.iSubItem = 0;
    lvc.pszText = plat_get_string(IDS_2081);

    lvc.cx = 292;
    lvc.fmt = LVCFMT_LEFT;

    if (ListView_InsertColumn(hwndList, 0, &lvc) == -1)
	return FALSE;

    lvc.iSubItem = 1;
    lvc.pszText = plat_get_string(IDS_2092);

    lvc.cx = 147;
    lvc.fmt = LVCFMT_LEFT;

    if (ListView_InsertColumn(hwndList, 1, &lvc) == -1)
		return FALSE;

    win_settings_mo_drives_resize_columns(hdlg);
    return TRUE;
}


static void
win_settings_zip_drives_resize_columns(HWND hdlg)
{
    int width[2] = {292, 147};
    HWND hwndList = GetDlgItem(hdlg, IDC_LIST_ZIP_DRIVES);
    RECT r;

    GetWindowRect(hwndList, &r);
    width[0] = MulDiv(width[0], dpi, 96);
    ListView_SetColumnWidth(hwndList, 0, MulDiv(width[0], dpi, 96));
    width[1] = (r.right - r.left) - 4 - width[0];
    ListView_SetColumnWidth(hwndList, 1, width[1]);
}


static BOOL
win_settings_zip_drives_init_columns(HWND hdlg)
{
    LVCOLUMN lvc;
    HWND hwndList = GetDlgItem(hdlg, IDC_LIST_ZIP_DRIVES);

    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;

    lvc.iSubItem = 0;
    lvc.pszText = plat_get_string(IDS_2081);

    lvc.cx = 292;
    lvc.fmt = LVCFMT_LEFT;

    if (ListView_InsertColumn(hwndList, 0, &lvc) == -1)
	return FALSE;

    lvc.iSubItem = 1;
    lvc.pszText = plat_get_string(IDS_2092);

    lvc.cx = 147;
    lvc.fmt = LVCFMT_LEFT;

    if (ListView_InsertColumn(hwndList, 1, &lvc) == -1)
		return FALSE;

    win_settings_zip_drives_resize_columns(hdlg);
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
win_settings_floppy_drives_update_item(HWND hdlg, int i)
{
    LVITEM lvI;
    char s[256], *t;
    WCHAR szText[256];
    HWND hwndList = GetDlgItem(hdlg, IDC_LIST_FLOPPY_DRIVES);

    lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
    lvI.stateMask = lvI.iSubItem = lvI.state = 0;

    lvI.iSubItem = 0;
    lvI.iItem = i;

    if (temp_fdd_types[i] > 0) {
	t = fdd_getname(temp_fdd_types[i]);
	strncpy(s, t, sizeof(s) - 1);
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
win_settings_cdrom_drives_update_item(HWND hdlg, int i)
{
    LVITEM lvI;
    WCHAR szText[256];
    int fsid;
    HWND hwndList = GetDlgItem(hdlg, IDC_LIST_CDROM_DRIVES);

    lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
    lvI.stateMask = lvI.iSubItem = lvI.state = 0;

    lvI.iSubItem = 0;
    lvI.iItem = i;

    fsid = combo_id_to_format_string_id(temp_cdrom[i].bus_type);

    switch (temp_cdrom[i].bus_type) {
	case CDROM_BUS_DISABLED:
	default:
		lvI.pszText = plat_get_string(fsid);
		lvI.iImage = 0;
		break;
	case CDROM_BUS_ATAPI:
		wsprintf(szText, plat_get_string(fsid), temp_cdrom[i].ide_channel >> 1, temp_cdrom[i].ide_channel & 1);
		lvI.pszText = szText;
		lvI.iImage = 1;
		break;
	case CDROM_BUS_SCSI:
		wsprintf(szText, plat_get_string(fsid), temp_cdrom[i].scsi_device_id >> 4, temp_cdrom[i].scsi_device_id & 15);
		lvI.pszText = szText;
		lvI.iImage = 1;
		break;
    }

    if (ListView_SetItem(hwndList, &lvI) == -1)
	return;

    lvI.iSubItem = 1;
    if (temp_cdrom[i].bus_type == CDROM_BUS_DISABLED)
	lvI.pszText = plat_get_string(IDS_2103);
    else {
	wsprintf(szText, L"%ix", temp_cdrom[i].speed);
	lvI.pszText = szText;
    }
    lvI.iItem = i;
    lvI.iImage = 0;

    if (ListView_SetItem(hwndList, &lvI) == -1)
	return;
}


static void
win_settings_mo_drives_update_item(HWND hdlg, int i)
{
    LVITEM lvI;
    WCHAR szText[256];
    char szType[30];
    int fsid;
    HWND hwndList = GetDlgItem(hdlg, IDC_LIST_MO_DRIVES);

    lvI.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
    lvI.stateMask = lvI.iSubItem = lvI.state = 0;

    lvI.iSubItem = 0;
    lvI.iItem = i;

    fsid = combo_id_to_format_string_id(temp_mo_drives[i].bus_type);

    switch (temp_mo_drives[i].bus_type) {
	case MO_BUS_DISABLED:
	default:
		lvI.pszText = plat_get_string(fsid);
		lvI.iImage = 0;
		break;
	case MO_BUS_ATAPI:
		wsprintf(szText, plat_get_string(fsid), temp_mo_drives[i].ide_channel >> 1, temp_mo_drives[i].ide_channel & 1);
		lvI.pszText = szText;
		lvI.iImage = 1;
		break;
	case MO_BUS_SCSI:
		wsprintf(szText, plat_get_string(fsid), temp_mo_drives[i].scsi_device_id >> 4, temp_mo_drives[i].scsi_device_id & 15);
		lvI.pszText = szText;
		lvI.iImage = 1;
		break;
    }

    if (ListView_SetItem(hwndList, &lvI) == -1)
	return;

    lvI.iSubItem = 1;
    if (temp_mo_drives[i].bus_type == MO_BUS_DISABLED)
	lvI.pszText = plat_get_string(IDS_2103);
    else {
	memset(szType, 0, 30);
	memcpy(szType, mo_drive_types[temp_mo_drives[i].type].vendor, 8);
	szType[strlen(szType)] = ' ';
	memcpy(szType + strlen(szType), mo_drive_types[temp_mo_drives[i].type].model, 16);
	szType[strlen(szType)] = ' ';
	memcpy(szType + strlen(szType), mo_drive_types[temp_mo_drives[i].type].revision, 4);
	mbstowcs(szText, szType, strlen(szType)+1);
	lvI.pszText = szText;
    }
    lvI.iItem = i;
    lvI.iImage = 0;

    if (ListView_SetItem(hwndList, &lvI) == -1)
	return;
}


static void
win_settings_zip_drives_update_item(HWND hdlg, int i)
{
    LVITEM lvI;
    WCHAR szText[256];
    int fsid;
    HWND hwndList = GetDlgItem(hdlg, IDC_LIST_ZIP_DRIVES);

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
		wsprintf(szText, plat_get_string(fsid), temp_zip_drives[i].scsi_device_id >> 4, temp_zip_drives[i].scsi_device_id & 15);
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
    int i = 0;

    lptsTemp = (LPTSTR) malloc(512 * sizeof(WCHAR));

    for (i = CDROM_BUS_DISABLED; i <= CDROM_BUS_SCSI; i++) {
	if ((i == CDROM_BUS_DISABLED) || (i >= CDROM_BUS_ATAPI))
		settings_add_string(hdlg, IDC_COMBO_CD_BUS, win_get_string(combo_id_to_string_id(i)));
    }

    for (i = 1; i <= 72; i++) {
	wsprintf(lptsTemp, L"%ix", i);
	settings_add_string(hdlg, IDC_COMBO_CD_SPEED, (LPARAM) lptsTemp);
    }

    for (i = 0; i < 64; i++) {
	wsprintf(lptsTemp, plat_get_string(IDS_4135), i >> 4, i & 15);
	settings_add_string(hdlg, IDC_COMBO_CD_ID, (LPARAM) lptsTemp);
    }

    for (i = 0; i < 8; i++) {
	wsprintf(lptsTemp, plat_get_string(IDS_4097), i >> 1, i & 1);
	settings_add_string(hdlg, IDC_COMBO_CD_CHANNEL_IDE, (LPARAM) lptsTemp);
    }

    free(lptsTemp);
}


static void
cdrom_recalc_location_controls(HWND hdlg, int assign_id)
{
    int i = 0;
    int bus = temp_cdrom[lv2_current_sel].bus_type;

    for (i = IDT_1741; i < (IDT_1742 + 1); i++)
	settings_show_window(hdlg, i, FALSE);
    settings_show_window(hdlg, IDC_COMBO_CD_ID, FALSE);
    settings_show_window(hdlg, IDC_COMBO_CD_CHANNEL_IDE, FALSE);
    settings_show_window(hdlg, IDC_COMBO_CD_SPEED, bus != CDROM_BUS_DISABLED);
    settings_show_window(hdlg, IDT_1758, bus != CDROM_BUS_DISABLED);

    if (bus != CDROM_BUS_DISABLED)
	settings_set_cur_sel(hdlg, IDC_COMBO_CD_SPEED, temp_cdrom[lv2_current_sel].speed - 1);

    switch(bus) {
	case CDROM_BUS_ATAPI:		/* ATAPI */
		settings_show_window(hdlg, IDT_1742, TRUE);
		settings_show_window(hdlg, IDC_COMBO_CD_CHANNEL_IDE, TRUE);

		if (assign_id)
			temp_cdrom[lv2_current_sel].ide_channel = next_free_ide_channel();

		settings_set_cur_sel(hdlg, IDC_COMBO_CD_CHANNEL_IDE, temp_cdrom[lv2_current_sel].ide_channel);
		break;
	case CDROM_BUS_SCSI:		/* SCSI */
		settings_show_window(hdlg, IDT_1741, TRUE);
		settings_show_window(hdlg, IDC_COMBO_CD_ID, TRUE);

		if (assign_id)
			next_free_scsi_id((uint8_t *) &temp_cdrom[lv2_current_sel].scsi_device_id);

		settings_set_cur_sel(hdlg, IDC_COMBO_CD_ID, temp_cdrom[lv2_current_sel].scsi_device_id);
		break;
    }
}


static void
mo_add_locations(HWND hdlg)
{
    LPTSTR lptsTemp;
    char *temp;
    int i = 0;

    lptsTemp = (LPTSTR) malloc(512 * sizeof(WCHAR));
    temp = (char*) malloc(30*sizeof(char));

    for (i = MO_BUS_DISABLED; i <= MO_BUS_SCSI; i++) {
	if ((i == MO_BUS_DISABLED) || (i >= MO_BUS_ATAPI))
		settings_add_string(hdlg, IDC_COMBO_MO_BUS, win_get_string(combo_id_to_string_id(i)));
    }

    for (i = 0; i < 64; i++) {
	wsprintf(lptsTemp, plat_get_string(IDS_4135), i >> 4, i & 15);
	settings_add_string(hdlg, IDC_COMBO_MO_ID, (LPARAM) lptsTemp);
    }

    for (i = 0; i < 8; i++) {
	wsprintf(lptsTemp, plat_get_string(IDS_4097), i >> 1, i & 1);
	settings_add_string(hdlg, IDC_COMBO_MO_CHANNEL_IDE, (LPARAM) lptsTemp);
    }

    for (int i = 0; i < KNOWN_MO_DRIVE_TYPES; i++) {
	memset(temp, 0, 30);
	memcpy(temp, mo_drive_types[i].vendor, 8);
	temp[strlen(temp)] = ' ';
	memcpy(temp + strlen(temp), mo_drive_types[i].model, 16);
	temp[strlen(temp)] = ' ';
	memcpy(temp + strlen(temp), mo_drive_types[i].revision, 4);

	mbstowcs(lptsTemp, temp, strlen(temp)+1);
	settings_add_string(hdlg, IDC_COMBO_MO_TYPE, (LPARAM) lptsTemp);
    }

    free(temp);
    free(lptsTemp);
}


static void
mo_recalc_location_controls(HWND hdlg, int assign_id)
{
    int i = 0;
    int bus = temp_mo_drives[lv1_current_sel].bus_type;

    for (i = IDT_1771; i < (IDT_1772 + 1); i++)
	settings_show_window(hdlg, i, FALSE);
    settings_show_window(hdlg, IDC_COMBO_MO_ID, FALSE);
    settings_show_window(hdlg, IDC_COMBO_MO_CHANNEL_IDE, FALSE);
    settings_show_window(hdlg, IDC_COMBO_MO_TYPE, bus != MO_BUS_DISABLED);
    settings_show_window(hdlg, IDT_1773, bus != MO_BUS_DISABLED);

    if (bus != MO_BUS_DISABLED)
	settings_set_cur_sel(hdlg, IDC_COMBO_MO_TYPE, temp_mo_drives[lv1_current_sel].type);

    switch(bus) {
	case MO_BUS_ATAPI:		/* ATAPI */
		settings_show_window(hdlg, IDT_1772, TRUE);
		settings_show_window(hdlg, IDC_COMBO_MO_CHANNEL_IDE, TRUE);

		if (assign_id)
			temp_mo_drives[lv1_current_sel].ide_channel = next_free_ide_channel();

		settings_set_cur_sel(hdlg, IDC_COMBO_MO_CHANNEL_IDE, temp_mo_drives[lv1_current_sel].ide_channel);
		break;
	case MO_BUS_SCSI:		/* SCSI */
		settings_show_window(hdlg, IDT_1771, TRUE);
		settings_show_window(hdlg, IDC_COMBO_MO_ID, TRUE);

		if (assign_id)
			next_free_scsi_id((uint8_t *) &temp_mo_drives[lv1_current_sel].scsi_device_id);

		settings_set_cur_sel(hdlg, IDC_COMBO_MO_ID, temp_mo_drives[lv1_current_sel].scsi_device_id);
		break;
    }
}


static void
zip_add_locations(HWND hdlg)
{
    LPTSTR lptsTemp;
    int i = 0;

    lptsTemp = (LPTSTR) malloc(512 * sizeof(WCHAR));

    for (i = ZIP_BUS_DISABLED; i <= ZIP_BUS_SCSI; i++) {
	if ((i == ZIP_BUS_DISABLED) || (i >= ZIP_BUS_ATAPI))
		settings_add_string(hdlg, IDC_COMBO_ZIP_BUS, win_get_string(combo_id_to_string_id(i)));
    }

    for (i = 0; i < 64; i++) {
	wsprintf(lptsTemp, plat_get_string(IDS_4135), i >> 4, i & 15);
	settings_add_string(hdlg, IDC_COMBO_ZIP_ID, (LPARAM) lptsTemp);
    }

    for (i = 0; i < 8; i++) {
	wsprintf(lptsTemp, plat_get_string(IDS_4097), i >> 1, i & 1);
	settings_add_string(hdlg, IDC_COMBO_ZIP_CHANNEL_IDE, (LPARAM) lptsTemp);
    }

    free(lptsTemp);
}


static void
zip_recalc_location_controls(HWND hdlg, int assign_id)
{
    int i = 0;

    int bus = temp_zip_drives[lv2_current_sel].bus_type;

    for (i = IDT_1754; i < (IDT_1755 + 1); i++)
	settings_show_window(hdlg, i, FALSE);
    settings_show_window(hdlg, IDC_COMBO_ZIP_ID, FALSE);
    settings_show_window(hdlg, IDC_COMBO_ZIP_CHANNEL_IDE, FALSE);
    settings_show_window(hdlg, IDC_CHECK250, bus != ZIP_BUS_DISABLED);

    if (bus != ZIP_BUS_DISABLED)
	settings_set_check(hdlg, IDC_CHECK250, temp_zip_drives[lv2_current_sel].is_250);

    switch(bus) {
	case ZIP_BUS_ATAPI:		/* ATAPI */
		settings_show_window(hdlg, IDT_1755, TRUE);
		settings_show_window(hdlg, IDC_COMBO_ZIP_CHANNEL_IDE, TRUE);

		if (assign_id)
			temp_zip_drives[lv2_current_sel].ide_channel = next_free_ide_channel();

		settings_set_cur_sel(hdlg, IDC_COMBO_ZIP_CHANNEL_IDE, temp_zip_drives[lv2_current_sel].ide_channel);
		break;
	case ZIP_BUS_SCSI:		/* SCSI */
		settings_show_window(hdlg, IDT_1754, TRUE);
		settings_show_window(hdlg, IDC_COMBO_ZIP_ID, TRUE);

		if (assign_id)
			next_free_scsi_id((uint8_t *) &temp_zip_drives[lv2_current_sel].scsi_device_id);

		settings_set_cur_sel(hdlg, IDC_COMBO_ZIP_ID, temp_zip_drives[lv2_current_sel].scsi_device_id);
		break;
    }
}


static void
cdrom_track(uint8_t id)
{
    if (temp_cdrom[id].bus_type == CDROM_BUS_ATAPI)
	ide_tracking |= (2 << (temp_cdrom[id].ide_channel << 3));
    else if (temp_cdrom[id].bus_type == CDROM_BUS_SCSI)
	scsi_tracking[temp_cdrom[id].scsi_device_id >> 3] |= (1 << (temp_cdrom[id].scsi_device_id & 0x07));
}


static void
cdrom_untrack(uint8_t id)
{
    if (temp_cdrom[id].bus_type == CDROM_BUS_ATAPI)
	ide_tracking &= ~(2 << (temp_cdrom[id].ide_channel << 3));
    else if (temp_cdrom[id].bus_type == CDROM_BUS_SCSI)
	scsi_tracking[temp_cdrom[id].scsi_device_id >> 3] &= ~(1 << (temp_cdrom[id].scsi_device_id & 0x07));
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


static void
mo_track(uint8_t id)
{
    if (temp_mo_drives[id].bus_type == MO_BUS_ATAPI)
	ide_tracking |= (1 << (temp_zip_drives[id].ide_channel << 3));
    else if (temp_mo_drives[id].bus_type == MO_BUS_SCSI)
	scsi_tracking[temp_mo_drives[id].scsi_device_id >> 3] |= (1 << (temp_mo_drives[id].scsi_device_id & 0x07));
}


static void
mo_untrack(uint8_t id)
{
    if (temp_mo_drives[id].bus_type == MO_BUS_ATAPI)
	ide_tracking &= ~(1 << (temp_zip_drives[id].ide_channel << 3));
    else if (temp_mo_drives[id].bus_type == MO_BUS_SCSI)
	scsi_tracking[temp_mo_drives[id].scsi_device_id >> 3] &= ~(1 << (temp_mo_drives[id].scsi_device_id & 0x07));
}


#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_floppy_and_cdrom_drives_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    int i = 0, old_sel = 0, b = 0, assign = 0;
    uint32_t b2 = 0;
    WCHAR szText[256];
    const uint8_t fd_icons[15] = { 248, 16, 16, 16, 16, 16, 16, 24, 24, 24, 24, 24, 24, 24, 0 };
    const uint8_t cd_icons[3] = { 249, 32, 0 };

    switch (message) {
	case WM_INITDIALOG:
		ignore_change = 1;

		lv1_current_sel = 0;
		win_settings_floppy_drives_init_columns(hdlg);
		image_list_init(hdlg, IDC_LIST_FLOPPY_DRIVES, (const uint8_t *) fd_icons);
		win_settings_floppy_drives_recalc_list(hdlg);
		settings_listview_select(hdlg, IDC_LIST_FLOPPY_DRIVES, 0);
		for (i = 0; i < 14; i++) {
			if (i == 0)
				settings_add_string(hdlg, IDC_COMBO_FD_TYPE, win_get_string(IDS_5376));
			else {
				mbstowcs(szText, fdd_getname(i), strlen(fdd_getname(i)) + 1);
				settings_add_string(hdlg, IDC_COMBO_FD_TYPE, (LPARAM) szText);
			}
		}
		settings_set_cur_sel(hdlg, IDC_COMBO_FD_TYPE, temp_fdd_types[lv1_current_sel]);

		settings_set_check(hdlg, IDC_CHECKTURBO, temp_fdd_turbo[lv1_current_sel]);
		settings_set_check(hdlg, IDC_CHECKBPB, temp_fdd_check_bpb[lv1_current_sel]);

		settings_listview_enable_styles(hdlg, IDC_LIST_FLOPPY_DRIVES);

		lv2_current_sel = 0;
		win_settings_cdrom_drives_init_columns(hdlg);
		image_list_init(hdlg, IDC_LIST_CDROM_DRIVES, (const uint8_t *) cd_icons);
		win_settings_cdrom_drives_recalc_list(hdlg);
		settings_listview_select(hdlg, IDC_LIST_CDROM_DRIVES, 0);
		cdrom_add_locations(hdlg);

		switch (temp_cdrom[lv2_current_sel].bus_type) {
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
		settings_set_cur_sel(hdlg, IDC_COMBO_CD_BUS, b);
		cdrom_recalc_location_controls(hdlg, 0);

		settings_listview_enable_styles(hdlg, IDC_LIST_CDROM_DRIVES);

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
			ignore_change = 1;
			settings_set_cur_sel(hdlg, IDC_COMBO_FD_TYPE, temp_fdd_types[lv1_current_sel]);
			settings_set_check(hdlg, IDC_CHECKTURBO, temp_fdd_turbo[lv1_current_sel]);
			settings_set_check(hdlg, IDC_CHECKBPB, temp_fdd_check_bpb[lv1_current_sel]);
			ignore_change = 0;
		} else if ((((LPNMHDR)lParam)->code == LVN_ITEMCHANGED) && (((LPNMHDR)lParam)->idFrom == IDC_LIST_CDROM_DRIVES)) {
			old_sel = lv2_current_sel;
			lv2_current_sel = get_selected_drive(hdlg, IDC_LIST_CDROM_DRIVES);
			if (lv2_current_sel == old_sel)
				return FALSE;
			ignore_change = 1;

			switch (temp_cdrom[lv2_current_sel].bus_type) {
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
			settings_set_cur_sel(hdlg, IDC_COMBO_CD_BUS, b);

			cdrom_recalc_location_controls(hdlg, 0);
			ignore_change = 0;
		}
		break;

	case WM_COMMAND:
		if (ignore_change)
			return FALSE;

		ignore_change = 1;
		switch (LOWORD(wParam)) {
			case IDC_COMBO_FD_TYPE:
				temp_fdd_types[lv1_current_sel] = settings_get_cur_sel(hdlg, IDC_COMBO_FD_TYPE);
				win_settings_floppy_drives_update_item(hdlg, lv1_current_sel);
				break;

			case IDC_CHECKTURBO:
				temp_fdd_turbo[lv1_current_sel] = settings_get_check(hdlg, IDC_CHECKTURBO);
				win_settings_floppy_drives_update_item(hdlg, lv1_current_sel);
				break;

			case IDC_CHECKBPB:
				temp_fdd_check_bpb[lv1_current_sel] = settings_get_check(hdlg, IDC_CHECKBPB);
				win_settings_floppy_drives_update_item(hdlg, lv1_current_sel);
				break;

			case IDC_COMBO_CD_BUS:
				b = settings_get_cur_sel(hdlg, IDC_COMBO_CD_BUS);
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
				if (b2 == temp_cdrom[lv2_current_sel].bus_type)
					break;
				cdrom_untrack(lv2_current_sel);
				assign = (temp_cdrom[lv2_current_sel].bus_type == b2) ? 0 : 1;
				if (temp_cdrom[lv2_current_sel].bus_type == CDROM_BUS_DISABLED)
					temp_cdrom[lv2_current_sel].speed = 8;
				temp_cdrom[lv2_current_sel].bus_type = b2;
				cdrom_recalc_location_controls(hdlg, assign);
				cdrom_track(lv2_current_sel);
				win_settings_cdrom_drives_update_item(hdlg, lv2_current_sel);
				break;

			case IDC_COMBO_CD_ID:
				cdrom_untrack(lv2_current_sel);
				temp_cdrom[lv2_current_sel].scsi_device_id = settings_get_cur_sel(hdlg, IDC_COMBO_CD_ID);
				cdrom_track(lv2_current_sel);
				win_settings_cdrom_drives_update_item(hdlg, lv2_current_sel);
				break;

			case IDC_COMBO_CD_CHANNEL_IDE:
				cdrom_untrack(lv2_current_sel);
				temp_cdrom[lv2_current_sel].ide_channel = settings_get_cur_sel(hdlg, IDC_COMBO_CD_CHANNEL_IDE);
				cdrom_track(lv2_current_sel);
				win_settings_cdrom_drives_update_item(hdlg, lv2_current_sel);
				break;

			case IDC_COMBO_CD_SPEED:
				temp_cdrom[lv2_current_sel].speed = settings_get_cur_sel(hdlg, IDC_COMBO_CD_SPEED) + 1;
				win_settings_cdrom_drives_update_item(hdlg, lv2_current_sel);
				break;
		}
		ignore_change = 0;

	case WM_DPICHANGED_AFTERPARENT:
		win_settings_floppy_drives_resize_columns(hdlg);
		image_list_init(hdlg, IDC_LIST_FLOPPY_DRIVES, (const uint8_t *) fd_icons);
		win_settings_cdrom_drives_resize_columns(hdlg);
		image_list_init(hdlg, IDC_LIST_CDROM_DRIVES, (const uint8_t *) cd_icons);
		break;
	default:
		return FALSE;
    }

    return FALSE;
}


#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_other_removable_devices_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    int old_sel = 0, b = 0, assign = 0;
    uint32_t b2 = 0;
    const uint8_t mo_icons[3] = { 251, 56, 0 };
    const uint8_t zip_icons[3] = { 250, 48, 0 };

    switch (message) {
	case WM_INITDIALOG:
		ignore_change = 1;

		lv1_current_sel = 0;
		win_settings_mo_drives_init_columns(hdlg);
		image_list_init(hdlg, IDC_LIST_MO_DRIVES, (const uint8_t *) mo_icons);
		win_settings_mo_drives_recalc_list(hdlg);
		settings_listview_select(hdlg, IDC_LIST_MO_DRIVES, 0);
		mo_add_locations(hdlg);

		switch (temp_mo_drives[lv1_current_sel].bus_type) {
			case MO_BUS_DISABLED:
			default:
				b = 0;
				break;
			case MO_BUS_ATAPI:
				b = 1;
				break;
			case MO_BUS_SCSI:
				b = 2;
				break;
		}
		settings_set_cur_sel(hdlg, IDC_COMBO_MO_BUS, b);
		mo_recalc_location_controls(hdlg, 0);

		settings_listview_enable_styles(hdlg, IDC_LIST_MO_DRIVES);

		lv2_current_sel = 0;
		win_settings_zip_drives_init_columns(hdlg);
		image_list_init(hdlg, IDC_LIST_ZIP_DRIVES, (const uint8_t *) zip_icons);
		win_settings_zip_drives_recalc_list(hdlg);
		settings_listview_select(hdlg, IDC_LIST_ZIP_DRIVES, 0);
		zip_add_locations(hdlg);

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
		settings_set_cur_sel(hdlg, IDC_COMBO_ZIP_BUS, b);
		zip_recalc_location_controls(hdlg, 0);

		settings_listview_enable_styles(hdlg, IDC_LIST_ZIP_DRIVES);

		ignore_change = 0;
		return TRUE;

	case WM_NOTIFY:
		if (ignore_change)
			return FALSE;

		if ((((LPNMHDR)lParam)->code == LVN_ITEMCHANGED) && (((LPNMHDR)lParam)->idFrom == IDC_LIST_MO_DRIVES)) {
			old_sel = lv1_current_sel;
			lv1_current_sel = get_selected_drive(hdlg, IDC_LIST_MO_DRIVES);
			if (lv1_current_sel == old_sel)
				return FALSE;
			ignore_change = 1;

			switch (temp_mo_drives[lv1_current_sel].bus_type) {
				case MO_BUS_DISABLED:
				default:
					b = 0;
					break;
				case MO_BUS_ATAPI:
					b = 1;
					break;
				case MO_BUS_SCSI:
					b = 2;
					break;
			}
			settings_set_cur_sel(hdlg, IDC_COMBO_MO_BUS, b);

			mo_recalc_location_controls(hdlg, 0);
			ignore_change = 0;
		} else if ((((LPNMHDR)lParam)->code == LVN_ITEMCHANGED) && (((LPNMHDR)lParam)->idFrom == IDC_LIST_ZIP_DRIVES)) {
			old_sel = lv2_current_sel;
			lv2_current_sel = get_selected_drive(hdlg, IDC_LIST_ZIP_DRIVES);
			if (lv2_current_sel == old_sel)
				return FALSE;
			ignore_change = 1;

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
			settings_set_cur_sel(hdlg, IDC_COMBO_ZIP_BUS, b);

			zip_recalc_location_controls(hdlg, 0);
			ignore_change = 0;
		}
		break;

	case WM_COMMAND:
		if (ignore_change)
			return FALSE;

		ignore_change = 1;
		switch (LOWORD(wParam)) {
			case IDC_COMBO_MO_BUS:
				b = settings_get_cur_sel(hdlg, IDC_COMBO_MO_BUS);
				switch (b) {
					case 0:
						b2 = MO_BUS_DISABLED;
						break;
					case 1:
						b2 = MO_BUS_ATAPI;
						break;
					case 2:
						b2 = MO_BUS_SCSI;
						break;
				}
				if (b2 == temp_mo_drives[lv1_current_sel].bus_type)
					break;
				mo_untrack(lv1_current_sel);
				assign = (temp_mo_drives[lv1_current_sel].bus_type == b2) ? 0 : 1;
				if (temp_mo_drives[lv1_current_sel].bus_type == MO_BUS_DISABLED)
					temp_mo_drives[lv1_current_sel].type = 0;
				temp_mo_drives[lv1_current_sel].bus_type = b2;
				mo_recalc_location_controls(hdlg, assign);
				mo_track(lv1_current_sel);
				win_settings_mo_drives_update_item(hdlg, lv1_current_sel);
				break;

			case IDC_COMBO_MO_ID:
				mo_untrack(lv1_current_sel);
				temp_mo_drives[lv1_current_sel].scsi_device_id = settings_get_cur_sel(hdlg, IDC_COMBO_MO_ID);
				mo_track(lv1_current_sel);
				win_settings_mo_drives_update_item(hdlg, lv1_current_sel);
				break;

			case IDC_COMBO_MO_CHANNEL_IDE:
				mo_untrack(lv1_current_sel);
				temp_mo_drives[lv1_current_sel].ide_channel = settings_get_cur_sel(hdlg, IDC_COMBO_MO_CHANNEL_IDE);
				mo_track(lv1_current_sel);
				win_settings_mo_drives_update_item(hdlg, lv1_current_sel);
				break;

			case IDC_COMBO_MO_TYPE:
				temp_mo_drives[lv1_current_sel].type = settings_get_cur_sel(hdlg, IDC_COMBO_MO_TYPE);
				win_settings_mo_drives_update_item(hdlg, lv1_current_sel);
				break;

			case IDC_COMBO_ZIP_BUS:
				b = settings_get_cur_sel(hdlg, IDC_COMBO_ZIP_BUS);
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
				win_settings_zip_drives_update_item(hdlg, lv2_current_sel);
				break;

			case IDC_COMBO_ZIP_ID:
				zip_untrack(lv2_current_sel);
				temp_zip_drives[lv2_current_sel].scsi_device_id = settings_get_cur_sel(hdlg, IDC_COMBO_ZIP_ID);
				zip_track(lv2_current_sel);
				win_settings_zip_drives_update_item(hdlg, lv2_current_sel);
				break;

			case IDC_COMBO_ZIP_CHANNEL_IDE:
				zip_untrack(lv2_current_sel);
				temp_zip_drives[lv2_current_sel].ide_channel = settings_get_cur_sel(hdlg, IDC_COMBO_ZIP_CHANNEL_IDE);
				zip_track(lv2_current_sel);
				win_settings_zip_drives_update_item(hdlg, lv2_current_sel);
				break;

			case IDC_CHECK250:
				temp_zip_drives[lv2_current_sel].is_250 = settings_get_check(hdlg, IDC_CHECK250);
				win_settings_zip_drives_update_item(hdlg, lv2_current_sel);
				break;
		}
		ignore_change = 0;

	case WM_DPICHANGED_AFTERPARENT:
		win_settings_mo_drives_resize_columns(hdlg);
		image_list_init(hdlg, IDC_LIST_MO_DRIVES, (const uint8_t *) mo_icons);
		win_settings_zip_drives_resize_columns(hdlg);
		image_list_init(hdlg, IDC_LIST_ZIP_DRIVES, (const uint8_t *) zip_icons);
		break;
	default:
		return FALSE;
    }

    return FALSE;
}


#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_peripherals_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    int c, d;
    int e;
    LPTSTR lptsTemp;
    char *stransi;
    const device_t *dev;

    switch (message) {
	case WM_INITDIALOG:
		lptsTemp = (LPTSTR) malloc(512 * sizeof(WCHAR));
		stransi = (char *) malloc(512);

		/* Populate the ISA RTC card dropdown. */
		e = 0;
		settings_reset_content(hdlg, IDC_COMBO_ISARTC);
		for (d = 0; ; d++) {
			generate_device_name(isartc_get_device(d), isartc_get_internal_name(d), 0);

			if (!device_name[0])
				break;

			if (d == 0) {
				settings_add_string(hdlg, IDC_COMBO_ISARTC, win_get_string(IDS_2103));
				settings_set_cur_sel(hdlg, IDC_COMBO_ISARTC, 0);
			} else
				settings_add_string(hdlg, IDC_COMBO_ISARTC, (LPARAM) device_name);
			settings_list_to_device[1][e] = d;
			if (d == temp_isartc)
				settings_set_cur_sel(hdlg, IDC_COMBO_ISARTC, e);
			e++;
		}
		settings_enable_window(hdlg, IDC_CONFIGURE_ISARTC, temp_isartc != 0);

		/* Populate the ISA memory card dropdowns. */
		for (c = 0; c < ISAMEM_MAX; c++) {
			settings_reset_content(hdlg, IDC_COMBO_ISAMEM_1 + c);
			for (d = 0; ; d++) {
				generate_device_name(isamem_get_device(d), (char *) isamem_get_internal_name(d), 0);

				if (!device_name[0])
					break;

				if (d == 0) {
					settings_add_string(hdlg, IDC_COMBO_ISAMEM_1 + c, win_get_string(IDS_2103));
					settings_set_cur_sel(hdlg, IDC_COMBO_ISAMEM_1 + c, 0);
				} else
					settings_add_string(hdlg, IDC_COMBO_ISAMEM_1 + c, (LPARAM) device_name);
			}
			settings_set_cur_sel(hdlg, IDC_COMBO_ISAMEM_1 + c, temp_isamem[c]);
			settings_enable_window(hdlg, IDC_CONFIGURE_ISAMEM_1 + c, temp_isamem[c] != 0);
		}

		settings_set_check(hdlg, IDC_CHECK_BUGGER, temp_bugger);
		settings_set_check(hdlg, IDC_CHECK_POSTCARD, temp_postcard);

		free(stransi);
		free(lptsTemp);

		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
			case IDC_CONFIGURE_ISARTC:
				temp_isartc = settings_list_to_device[1][settings_get_cur_sel(hdlg, IDC_COMBO_ISARTC)];
				temp_deviceconfig |= deviceconfig_open(hdlg, (void *)isartc_get_device(temp_isartc));
				break;

			case IDC_COMBO_ISARTC:
				temp_isartc = settings_list_to_device[1][settings_get_cur_sel(hdlg, IDC_COMBO_ISARTC)];
				settings_enable_window(hdlg, IDC_CONFIGURE_ISARTC, temp_isartc != 0);
				break;

			case IDC_COMBO_ISAMEM_1: case IDC_COMBO_ISAMEM_2:
			case IDC_COMBO_ISAMEM_3: case IDC_COMBO_ISAMEM_4:
				c = LOWORD(wParam) - IDC_COMBO_ISAMEM_1;
				temp_isamem[c] = settings_get_cur_sel(hdlg, LOWORD(wParam));
				settings_enable_window(hdlg, IDC_CONFIGURE_ISAMEM_1 + c, temp_isamem[c] != 0);
				break;

			case IDC_CONFIGURE_ISAMEM_1: case IDC_CONFIGURE_ISAMEM_2:
			case IDC_CONFIGURE_ISAMEM_3: case IDC_CONFIGURE_ISAMEM_4:
				c = LOWORD(wParam) - IDC_CONFIGURE_ISAMEM_1;
				dev = isamem_get_device(temp_isamem[c]);
				temp_deviceconfig |= deviceconfig_inst_open(hdlg, (void *)dev, c + 1);
				break;
		}
		return FALSE;

	case WM_SAVESETTINGS:
		temp_isartc = settings_list_to_device[1][settings_get_cur_sel(hdlg, IDC_COMBO_ISARTC)];
		temp_bugger = settings_get_check(hdlg, IDC_CHECK_BUGGER);
		temp_postcard = settings_get_check(hdlg, IDC_CHECK_POSTCARD);

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
	case SETTINGS_PAGE_STORAGE:
		hwndChildDialog = CreateDialog(hinstance, (LPCWSTR)DLG_CFG_STORAGE, hwndParent, win_settings_storage_proc);
		break;
	case SETTINGS_PAGE_HARD_DISKS:
		hwndChildDialog = CreateDialog(hinstance, (LPCWSTR)DLG_CFG_HARD_DISKS, hwndParent, win_settings_hard_disks_proc);
		break;
	case SETTINGS_PAGE_FLOPPY_AND_CDROM_DRIVES:
		hwndChildDialog = CreateDialog(hinstance, (LPCWSTR)DLG_CFG_FLOPPY_AND_CDROM_DRIVES, hwndParent, win_settings_floppy_and_cdrom_drives_proc);
		break;
	case SETTINGS_PAGE_OTHER_REMOVABLE_DEVICES:
		hwndChildDialog = CreateDialog(hinstance, (LPCWSTR)DLG_CFG_OTHER_REMOVABLE_DEVICES, hwndParent, win_settings_other_removable_devices_proc);
		break;
	case SETTINGS_PAGE_PERIPHERALS:
		hwndChildDialog = CreateDialog(hinstance, (LPCWSTR)DLG_CFG_PERIPHERALS, hwndParent, win_settings_peripherals_proc);
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

    for (i = 0; i < 11; i++) {
		lvI.pszText = plat_get_string(IDS_2065+i);
		lvI.iItem = i;
		lvI.iImage = i;

		if (ListView_InsertItem(hwndList, &lvI) == -1)
			return FALSE;
    }

    return TRUE;
}



#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_confirm(HWND hdlg)
{
    int i;

    SendMessage(hwndChildDialog, WM_SAVESETTINGS, 0, 0);

    if (win_settings_changed()) {
	if (confirm_save && !settings_only)
		i = settings_msgbox_ex(MBX_QUESTION_OK | MBX_WARNING | MBX_DONTASK, (wchar_t *) IDS_2121, (wchar_t *) IDS_2122, (wchar_t *) IDS_2123, NULL, NULL);
	else
		i = 0;

	if (i == 10) {
		confirm_save = 0;
		i = 0;
	}

	if (i == 0)
		win_settings_save();
	else
		return FALSE;
    }

    DestroyWindow(hwndChildDialog);
    EndDialog(hdlg, 0);
    win_notify_dlg_closed();
    return TRUE;
}


static void
win_settings_categories_resize_columns(HWND hdlg)
{
    HWND hwndList = GetDlgItem(hdlg, IDC_SETTINGSCATLIST);
    RECT r;

    GetWindowRect(hwndList, &r);
    ListView_SetColumnWidth(hwndList, 0, (r.right - r.left) + 1 - 5);
}


static BOOL
win_settings_categories_init_columns(HWND hdlg)
{
    LVCOLUMN lvc;
    int iCol = 0;
    HWND hwndList = GetDlgItem(hdlg, IDC_SETTINGSCATLIST);

    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;

    lvc.iSubItem = 0;
    lvc.pszText = plat_get_string(2048);

    lvc.cx = 171;
    lvc.fmt = LVCFMT_LEFT;

    if (ListView_InsertColumn(hwndList, iCol, &lvc) == -1)
	return FALSE;

    win_settings_categories_resize_columns(hdlg);
    return TRUE;
}


#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
win_settings_main_proc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND h = NULL;
    int category, i = 0, j = 0;
    const uint8_t cat_icons[12] = { 240, 241, 242, 243, 96, 244, 252, 80, 246, 247, 245, 0 };

    hwndParentDialog = hdlg;

    switch (message) {
	case WM_INITDIALOG:
		dpi = win_get_dpi(hdlg);
		win_settings_init();
		displayed_category = -1;
		h = GetDlgItem(hdlg, IDC_SETTINGSCATLIST);
		win_settings_categories_init_columns(hdlg);
		image_list_init(hdlg, IDC_SETTINGSCATLIST, (const uint8_t *) cat_icons);
		win_settings_main_insert_categories(h);
		settings_listview_select(hdlg, IDC_SETTINGSCATLIST, first_cat);
		settings_listview_enable_styles(hdlg, IDC_SETTINGSCATLIST);
		return TRUE;
	case WM_NOTIFY:
		if ((((LPNMHDR)lParam)->code == LVN_ITEMCHANGED) && (((LPNMHDR)lParam)->idFrom == IDC_SETTINGSCATLIST)) {
			category = -1;
			for (i = 0; i < 11; i++) {
				h = GetDlgItem(hdlg, IDC_SETTINGSCATLIST);
				j = ListView_GetItemState(h, i, LVIS_SELECTED);
				if (j)
					category = i;
			}
			if (category != -1)
				win_settings_show_child(hdlg, category);
		}
		break;
	case WM_CLOSE:
		DestroyWindow(hwndChildDialog);
		EndDialog(hdlg, 0);
		win_notify_dlg_closed();
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
			case IDOK:
				return win_settings_confirm(hdlg);
			case IDCANCEL:
				DestroyWindow(hwndChildDialog);
				EndDialog(hdlg, 0);
				win_notify_dlg_closed();
				return TRUE;
		}
		break;

	case WM_DPICHANGED:
		dpi = HIWORD(wParam);
		win_settings_categories_resize_columns(hdlg);
		image_list_init(hdlg, IDC_SETTINGSCATLIST, (const uint8_t *) cat_icons);
		break;
	default:
		return FALSE;
    }

    return FALSE;
}


void
win_settings_open_ex(HWND hwnd, int category)
{
    win_notify_dlg_open();

    first_cat = category;
    DialogBox(hinstance, (LPCWSTR)DLG_CONFIG, hwnd, win_settings_main_proc);
}


void
win_settings_open(HWND hwnd)
{
    win_settings_open_ex(hwnd, SETTINGS_PAGE_MACHINE);
}
