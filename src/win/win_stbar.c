/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implement the application's Status Bar.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 */
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#undef BITMAP
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/config.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/hdd.h>
#include <86box/hdc.h>
#include <86box/fdd.h>
#include <86box/fdd_86f.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/zip.h>
#include <86box/mo.h>
#include <86box/cdrom_image.h>
#include <86box/scsi_disk.h>
#include <86box/network.h>
#include <86box/video.h>
#include <86box/sound.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/win.h>

#ifndef GWL_WNDPROC
#define GWL_WNDPROC GWLP_WNDPROC
#endif


HWND		hwndSBAR;
int		update_icons = 1;


static LONG_PTR	OriginalProcedure;
static WCHAR	**sbTips;
static int	*iStatusWidths;
static int	*sb_part_meanings;
static uint8_t	*sb_part_icons;
static int	sb_parts = 0;
static int	sb_ready = 0;
static uint8_t	sb_map[256];

/* Also used by win_settings.c */
intptr_t
fdd_type_to_icon(int type)
{
    int ret = 248;

    switch(type) {
	case 0:
		break;

	case 1: case 2: case 3: case 4:
	case 5: case 6:
		ret = 16;
		break;

	case 7: case 8: case 9: case 10:
	case 11: case 12: case 13:
		ret = 24;
		break;

	default:
		break;
    }

    return(ret);
}


/* FIXME: should be hdd_count() in hdd.c */
static int
hdd_count(int bus)
{
    int c = 0;
    int i;

    for (i=0; i<HDD_NUM; i++) {
	if (hdd[i].bus == bus)
		c++;
    }

    return(c);
}


void
ui_sb_timer_callback(int pane)
{
    sb_part_icons[pane] &= ~1;

    if (sb_part_icons && sb_part_icons[pane]) {
	SendMessage(hwndSBAR, SB_SETICON, pane,
		    (LPARAM)hIcon[sb_part_icons[pane]]);
    }
}



/* API */
/* API: update one of the icons after activity. */
void
ui_sb_update_icon(int tag, int active)
{
    uint8_t found = 0xff;

    if (!update_icons || !sb_ready)
	return;

    if (((tag & 0xf0) >= SB_TEXT))
	return;

    found = sb_map[tag];
    if ((found != 0xff) && ((sb_part_icons[found] ^ active) & 1) && active) {
	sb_part_icons[found] |= 1;

	SendMessage(hwndSBAR, SB_SETICON, found,
		    (LPARAM)hIcon[sb_part_icons[found]]);

	SetTimer(hwndMain, 0x8000 | found, 75, NULL);
    }
}


/* API: This is for the drive state indicator. */
void
ui_sb_update_icon_state(int tag, int state)
{
    uint8_t found = 0xff;

    if (!sb_ready || ((tag & 0xf0) >= SB_HDD))
	return;

    found = sb_map[tag];
    if (found != 0xff) {
	sb_part_icons[found] &= ~128;
	sb_part_icons[found] |= (state ? 128 : 0);

	SendMessage(hwndSBAR, SB_SETICON, found,
		    (LPARAM)hIcon[sb_part_icons[found]]);
    }
}


static void
StatusBarCreateFloppyTip(int part)
{
    WCHAR wtext[512];
    WCHAR tempTip[512];

    int drive = sb_part_meanings[part] & 0xf;

    mbstowcs(wtext, fdd_getname(fdd_get_type(drive)),
	     strlen(fdd_getname(fdd_get_type(drive))) + 1);
    if (wcslen(floppyfns[drive]) == 0) {
	_swprintf(tempTip, plat_get_string(IDS_2108),
		  drive+1, wtext, plat_get_string(IDS_2057));
    } else {
	_swprintf(tempTip, plat_get_string(IDS_2108),
		  drive+1, wtext, floppyfns[drive]);
    }

    if (sbTips[part] != NULL) {
	free(sbTips[part]);
	sbTips[part] = NULL;
    }
    sbTips[part] = (WCHAR *)malloc((wcslen(tempTip) << 1) + 2);
    wcscpy(sbTips[part], tempTip);
}


static void
StatusBarCreateCdromTip(int part)
{
    WCHAR tempTip[512];
    WCHAR *szText;
    int id;
    int drive = sb_part_meanings[part] & 0xf;
    int bus = cdrom[drive].bus_type;

    id = IDS_5377 + (bus - 1);
    szText = plat_get_string(id);

    if (cdrom[drive].host_drive == 200) {
	if (wcslen(cdrom[drive].image_path) == 0)
		_swprintf(tempTip, plat_get_string(IDS_5120), drive+1, szText, plat_get_string(IDS_2057));
	else
		_swprintf(tempTip, plat_get_string(IDS_5120), drive+1, szText, cdrom[drive].image_path);
    } else
	_swprintf(tempTip, plat_get_string(IDS_5120), drive+1, szText, plat_get_string(IDS_2057));

    if (sbTips[part] != NULL) {
	free(sbTips[part]);
	sbTips[part] = NULL;
    }
    sbTips[part] = (WCHAR *)malloc((wcslen(tempTip) << 1) + 4);
    wcscpy(sbTips[part], tempTip);
}


static void
StatusBarCreateZIPTip(int part)
{
    WCHAR tempTip[512];
    WCHAR *szText;
    int id;
    int drive = sb_part_meanings[part] & 0xf;
    int bus = zip_drives[drive].bus_type;

    id = IDS_5377 + (bus - 1);
    szText = plat_get_string(id);

    int type = zip_drives[drive].is_250 ? 250 : 100;

    if (wcslen(zip_drives[drive].image_path) == 0) {
	_swprintf(tempTip, plat_get_string(IDS_2054),
		  type, drive+1, szText, plat_get_string(IDS_2057));
    } else {
	_swprintf(tempTip, plat_get_string(IDS_2054),
		  type, drive+1, szText, zip_drives[drive].image_path);
    }

    if (sbTips[part] != NULL) {
	free(sbTips[part]);
	sbTips[part] = NULL;
    }
    sbTips[part] = (WCHAR *)malloc((wcslen(tempTip) << 1) + 2);
    wcscpy(sbTips[part], tempTip);
}

static void
StatusBarCreateMOTip(int part)
{
    WCHAR tempTip[512];
    WCHAR *szText;
    int id;
    int drive = sb_part_meanings[part] & 0xf;
    int bus = mo_drives[drive].bus_type;

    id = IDS_5377 + (bus - 1);
    szText = plat_get_string(id);

    if (wcslen(mo_drives[drive].image_path) == 0) {
	_swprintf(tempTip, plat_get_string(IDS_2115),
		  drive+1, szText, plat_get_string(IDS_2057));
    } else {
	_swprintf(tempTip, plat_get_string(IDS_2115),
		  drive+1, szText, mo_drives[drive].image_path);
    }

    if (sbTips[part] != NULL) {
	free(sbTips[part]);
	sbTips[part] = NULL;
    }
    sbTips[part] = (WCHAR *)malloc((wcslen(tempTip) << 1) + 2);
    wcscpy(sbTips[part], tempTip);
}

static void
StatusBarCreateDiskTip(int part)
{
    WCHAR tempTip[512];
    WCHAR *szText;
    int id;
    int bus = sb_part_meanings[part] & 0xf;

    id = IDS_4352 + (bus - 1);
    szText = plat_get_string(id);

    _swprintf(tempTip, plat_get_string(IDS_4096), szText);
    if (sbTips[part] != NULL)
	free(sbTips[part]);
    sbTips[part] = (WCHAR *)malloc((wcslen(tempTip) << 1) + 2);
    wcscpy(sbTips[part], tempTip);
}


static void
StatusBarCreateNetworkTip(int part)
{
    WCHAR tempTip[512];

    _swprintf(tempTip, plat_get_string(IDS_2069));

    if (sbTips[part] != NULL)
	free(sbTips[part]);
    sbTips[part] = (WCHAR *)malloc((wcslen(tempTip) << 1) + 2);
    wcscpy(sbTips[part], tempTip);
}


static void
StatusBarCreateSoundTip(int part)
{
    WCHAR tempTip[512];

    _swprintf(tempTip, plat_get_string(IDS_2068));

    if (sbTips[part] != NULL)
	free(sbTips[part]);
    sbTips[part] = (WCHAR *)malloc((wcslen(tempTip) << 1) + 2);
    wcscpy(sbTips[part], tempTip);
}


/* API */
void
ui_sb_update_tip(int meaning)
{
    uint8_t part = 0xff;

    if (!sb_ready || (sb_parts == 0) || (sb_part_meanings == NULL))
	return;

    part = sb_map[meaning];

    if (part != 0xff) {
	switch(meaning & 0xf0) {
		case SB_FLOPPY:
			StatusBarCreateFloppyTip(part);
			break;

		case SB_CDROM:
			StatusBarCreateCdromTip(part);
			break;

		case SB_ZIP:
			StatusBarCreateZIPTip(part);
			break;

		case SB_MO:
			StatusBarCreateMOTip(part);
			break;

		case SB_HDD:
			StatusBarCreateDiskTip(part);
			break;

		case SB_NETWORK:
			StatusBarCreateNetworkTip(part);
			break;

		case SB_SOUND:
			StatusBarCreateSoundTip(part);
			break;

		default:
			break;
	}

	SendMessage(hwndSBAR, SB_SETTIPTEXT, part, (LPARAM)sbTips[part]);
    }
}


static void
StatusBarDestroyTips(void)
{
    int i;

    if (sb_parts == 0) return;

    if (! sbTips) return;

    for (i=0; i<sb_parts; i++) {
	if (sbTips[i]) {
		free(sbTips[i]);
		sbTips[i] = NULL;
	}
    }

    free(sbTips);
    sbTips = NULL;
}


/* API: mark the status bar as not ready. */
void
ui_sb_set_ready(int ready)
{
    sb_ready = ready;
}


/* API: update the status bar panes. */
void
ui_sb_update_panes(void)
{
    int i, id, hdint;
    int edge = 0;
    int c_mfm, c_esdi, c_xta;
    int c_ide, c_scsi;
    int do_net;
    char *hdc_name;

    if (!config_changed)
	return;

    if (sb_ready) {
	sb_ready = 0;
    }

    hdint = (machines[machine].flags & MACHINE_HDC) ? 1 : 0;
    c_mfm = hdd_count(HDD_BUS_MFM);
    c_esdi = hdd_count(HDD_BUS_ESDI);
    c_xta = hdd_count(HDD_BUS_XTA);
    c_ide = hdd_count(HDD_BUS_IDE);
    c_scsi = hdd_count(HDD_BUS_SCSI);
    do_net = network_available();

    media_menu_reset();

    if (sb_parts > 0) {
	for (i = 0; i < sb_parts; i++)
		SendMessage(hwndSBAR, SB_SETICON, i, (LPARAM)NULL);
	SendMessage(hwndSBAR, SB_SETPARTS, (WPARAM)0, (LPARAM)NULL);

	if (iStatusWidths) {
		free(iStatusWidths);
		iStatusWidths = NULL;
	}
	if (sb_part_meanings) {
		free(sb_part_meanings);
		sb_part_meanings = NULL;
	}
	if (sb_part_icons) {
		free(sb_part_icons);
		sb_part_icons = NULL;
	}
	StatusBarDestroyTips();
    }

    memset(sb_map, 0xff, sizeof(sb_map));

    sb_parts = 0;
    for (i=0; i<FDD_NUM; i++) {
	if (fdd_get_type(i) != 0)
		sb_parts++;
    }
    hdc_name = hdc_get_internal_name(hdc_current);
    for (i=0; i<CDROM_NUM; i++) {
	/* Could be Internal or External IDE.. */
	if ((cdrom[i].bus_type == CDROM_BUS_ATAPI) &&
	    !(hdint || !memcmp(hdc_name, "ide", 3)))
		continue;

	if ((cdrom[i].bus_type == CDROM_BUS_SCSI) &&
	    (scsi_card_current == 0))
		continue;
	if (cdrom[i].bus_type != 0)
		sb_parts++;
    }
    for (i=0; i<ZIP_NUM; i++) {
	/* Could be Internal or External IDE.. */
	if ((zip_drives[i].bus_type == ZIP_BUS_ATAPI) &&
	    !(hdint || !memcmp(hdc_name, "ide", 3)))
		continue;

	if ((zip_drives[i].bus_type == ZIP_BUS_SCSI) &&
	    (scsi_card_current == 0))
		continue;
	if (zip_drives[i].bus_type != 0)
		sb_parts++;
    }
    if (c_mfm && (hdint || !memcmp(hdc_name, "st506", 5))) {
	/* MFM drives, and MFM or Internal controller. */
	sb_parts++;
    }
    if (c_esdi && (hdint || !memcmp(hdc_name, "esdi", 4))) {
	/* ESDI drives, and ESDI or Internal controller. */
	sb_parts++;
    }
    if (c_xta && (hdint || !memcmp(hdc_name, "xta", 3)))
	sb_parts++;
    if (c_ide && (hdint || !memcmp(hdc_name, "xtide", 5) || !memcmp(hdc_name, "ide", 3)))
	sb_parts++;
    if (c_scsi && (scsi_card_current != 0))
	sb_parts++;
    if (do_net)
	sb_parts++;
    sb_parts += 2;

    iStatusWidths = (int *)malloc(sb_parts * sizeof(int));
     memset(iStatusWidths, 0, sb_parts * sizeof(int));
    sb_part_meanings = (int *)malloc(sb_parts * sizeof(int));
     memset(sb_part_meanings, 0, sb_parts * sizeof(int));
    sb_part_icons = (uint8_t *)malloc(sb_parts * sizeof(uint8_t));
     memset(sb_part_icons, 0, sb_parts * sizeof(uint8_t));
    sbTips = (WCHAR **)malloc(sb_parts * sizeof(WCHAR *));
     memset(sbTips, 0, sb_parts * sizeof(WCHAR *));

    sb_parts = 0;
    for (i=0; i<FDD_NUM; i++) {
	if (fdd_get_type(i) != 0) {
		edge += SB_ICON_WIDTH;
		iStatusWidths[sb_parts] = edge;
		sb_part_meanings[sb_parts] = SB_FLOPPY | i;
		sb_map[SB_FLOPPY | i] = sb_parts;
		sb_parts++;
	}
    }
    for (i=0; i<CDROM_NUM; i++) {
	/* Could be Internal or External IDE.. */
	if ((cdrom[i].bus_type == CDROM_BUS_ATAPI) &&
	    !(hdint || !memcmp(hdc_name, "ide", 3))) {
		continue;
	}
	if ((cdrom[i].bus_type == CDROM_BUS_SCSI) && (scsi_card_current == 0))
		continue;
	if (cdrom[i].bus_type != 0) {
		edge += SB_ICON_WIDTH;
		iStatusWidths[sb_parts] = edge;
		sb_part_meanings[sb_parts] = SB_CDROM | i;
		sb_map[SB_CDROM | i] = sb_parts;
		sb_parts++;
	}
    }
    for (i=0; i<ZIP_NUM; i++) {
	/* Could be Internal or External IDE.. */
	if ((zip_drives[i].bus_type == ZIP_BUS_ATAPI) &&
	    !(hdint || !memcmp(hdc_name, "ide", 3)))
		continue;
	if ((zip_drives[i].bus_type == ZIP_BUS_SCSI) && (scsi_card_current == 0))
		continue;
	if (zip_drives[i].bus_type != 0) {
		edge += SB_ICON_WIDTH;
		iStatusWidths[sb_parts] = edge;
		sb_part_meanings[sb_parts] = SB_ZIP | i;
		sb_map[SB_ZIP | i] = sb_parts;
		sb_parts++;
	}
    }
    for (i=0; i<MO_NUM; i++) {
	/* Could be Internal or External IDE.. */
	if ((mo_drives[i].bus_type == MO_BUS_ATAPI) &&
	    !(hdint || !memcmp(hdc_name, "ide", 3)))
		continue;
	if ((mo_drives[i].bus_type == MO_BUS_SCSI) && (scsi_card_current == 0))
		continue;
	if (mo_drives[i].bus_type != 0) {
		edge += SB_ICON_WIDTH;
		iStatusWidths[sb_parts] = edge;
		sb_part_meanings[sb_parts] = SB_MO | i;
		sb_map[SB_MO | i] = sb_parts;
		sb_parts++;
	}
    }    
    if (c_mfm && (hdint || !memcmp(hdc_name, "st506", 5))) {
	edge += SB_ICON_WIDTH;
	iStatusWidths[sb_parts] = edge;
	sb_part_meanings[sb_parts] = SB_HDD | HDD_BUS_MFM;
	sb_map[SB_HDD | HDD_BUS_MFM] = sb_parts;
	sb_parts++;
    }
    if (c_esdi && (hdint || !memcmp(hdc_name, "esdi", 4))) {
	edge += SB_ICON_WIDTH;
	iStatusWidths[sb_parts] = edge;
	sb_part_meanings[sb_parts] = SB_HDD | HDD_BUS_ESDI;
	sb_map[SB_HDD | HDD_BUS_ESDI] = sb_parts;
	sb_parts++;
    }
    if (c_xta && (hdint || !memcmp(hdc_name, "xta", 3))) {
	edge += SB_ICON_WIDTH;
	iStatusWidths[sb_parts] = edge;
	sb_part_meanings[sb_parts] = SB_HDD | HDD_BUS_XTA;
	sb_map[SB_HDD | HDD_BUS_XTA] = sb_parts;
	sb_parts++;
    }
    if (c_ide && (hdint || !memcmp(hdc_name, "xtide", 5) || !memcmp(hdc_name, "ide", 3))) {
	edge += SB_ICON_WIDTH;
	iStatusWidths[sb_parts] = edge;
	sb_part_meanings[sb_parts] = SB_HDD | HDD_BUS_IDE;
	sb_map[SB_HDD | HDD_BUS_IDE] = sb_parts;
	sb_parts++;
    }
    if (c_scsi && (scsi_card_current != 0)) {
	edge += SB_ICON_WIDTH;
	iStatusWidths[sb_parts] = edge;
	sb_part_meanings[sb_parts] = SB_HDD | HDD_BUS_SCSI;
	sb_map[SB_HDD | HDD_BUS_SCSI] = sb_parts;
	sb_parts++;
    }
    if (do_net) {
	edge += SB_ICON_WIDTH;
	iStatusWidths[sb_parts] = edge;
	sb_part_meanings[sb_parts] = SB_NETWORK;
	sb_map[SB_NETWORK] = sb_parts;
	sb_parts++;
    }

    edge += SB_ICON_WIDTH;
    iStatusWidths[sb_parts] = edge;
    sb_part_meanings[sb_parts] = SB_SOUND;
    sb_map[SB_SOUND] = sb_parts;
    sb_parts++;

    if (sb_parts)
	iStatusWidths[sb_parts - 1] += (24 - SB_ICON_WIDTH);
    iStatusWidths[sb_parts] = -1;
    sb_part_meanings[sb_parts] = SB_TEXT;
    sb_map[SB_TEXT] = sb_parts;
    sb_parts ++;

    SendMessage(hwndSBAR, SB_SETPARTS, (WPARAM)sb_parts, (LPARAM)iStatusWidths);

    for (i=0; i<sb_parts; i++) {
	switch (sb_part_meanings[i] & 0xf0) {
		case SB_FLOPPY:		/* Floppy */
			sb_part_icons[i] = (wcslen(floppyfns[sb_part_meanings[i] & 0xf]) == 0) ? 128 : 0;
			sb_part_icons[i] |= fdd_type_to_icon(fdd_get_type(sb_part_meanings[i] & 0xf));
			StatusBarCreateFloppyTip(i);
			break;

		case SB_CDROM:		/* CD-ROM */
			id = sb_part_meanings[i] & 0xf;
			if (cdrom[id].host_drive == 200)
				sb_part_icons[i] = (wcslen(cdrom[id].image_path) == 0) ? 128 : 0;
			else
				sb_part_icons[i] = 128;
			sb_part_icons[i] |= 32;
			StatusBarCreateCdromTip(i);
			break;

		case SB_ZIP:		/* Iomega ZIP */
			sb_part_icons[i] = (wcslen(zip_drives[sb_part_meanings[i] & 0xf].image_path) == 0) ? 128 : 0;
			sb_part_icons[i] |= 48;
			StatusBarCreateZIPTip(i);
			break;
			
		case SB_MO:		/* Magneto-Optical disk */	
			sb_part_icons[i] = (wcslen(mo_drives[sb_part_meanings[i] & 0xf].image_path) == 0) ? 128 : 0;
			sb_part_icons[i] |= 56;
			StatusBarCreateMOTip(i);
			break;

		case SB_HDD:		/* Hard disk */
			sb_part_icons[i] = 64;
			StatusBarCreateDiskTip(i);
			break;

		case SB_NETWORK:	/* Network */
			sb_part_icons[i] = 80;
			StatusBarCreateNetworkTip(i);
			break;

		case SB_SOUND:		/* Sound */
			sb_part_icons[i] = 243;
			StatusBarCreateSoundTip(i);
			break;

		case SB_TEXT:		/* Status text */
			SendMessage(hwndSBAR, SB_SETTEXT, i | SBT_NOBORDERS, (LPARAM)L"");
			sb_part_icons[i] = 255;
			break;
	}

	if (sb_part_icons[i] != 255) {
		SendMessage(hwndSBAR, SB_SETTEXT, i | SBT_NOBORDERS, (LPARAM)L"");
		SendMessage(hwndSBAR, SB_SETICON, i, (LPARAM)hIcon[sb_part_icons[i]]);
		SendMessage(hwndSBAR, SB_SETTIPTEXT, i, (LPARAM)sbTips[i]);
	} else
		SendMessage(hwndSBAR, SB_SETICON, i, (LPARAM)NULL);
    }

    sb_ready = 1;
}


static VOID APIENTRY
StatusBarPopupMenu(HWND hwnd, POINT pt, int id)
{
    HMENU menu;

    if (id >= (sb_parts - 1)) return;

    pt.x = id * SB_ICON_WIDTH;	/* Justify to the left. */
    pt.y = 0;			/* Justify to the top. */
    ClientToScreen(hwnd, (LPPOINT) &pt);

    switch(sb_part_meanings[id] & 0xF0) {
	case SB_FLOPPY:
		menu = media_menu_get_floppy(sb_part_meanings[id] & 0x0F);
		break;
	case SB_CDROM:
		menu = media_menu_get_cdrom(sb_part_meanings[id] & 0x0F);
		break;
	case SB_ZIP:
		menu = media_menu_get_zip(sb_part_meanings[id] & 0x0F);
		break;
	case SB_MO:
		menu = media_menu_get_mo(sb_part_meanings[id] & 0x0F);
		break;
	default:
		return;
    }

    TrackPopupMenu(menu,
		   TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_LEFTBUTTON,
		   pt.x, pt.y, 0, hwndSBAR, NULL);
}


/* Handle messages for the Status Bar window. */
#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
StatusBarProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    RECT rc;
    POINT pt;
    int item_id = 0;

    switch (message) {
	case WM_COMMAND:
		media_menu_proc(hwnd, message, wParam, lParam);
		return(0);

	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
		GetClientRect(hwnd, (LPRECT)& rc);
		pt.x = GET_X_LPARAM(lParam);
		pt.y = GET_Y_LPARAM(lParam);
		if (PtInRect((LPRECT) &rc, pt))
			StatusBarPopupMenu(hwnd, pt, (pt.x / SB_ICON_WIDTH));
		break;

	case WM_LBUTTONDBLCLK:
		GetClientRect(hwnd, (LPRECT)& rc);
		pt.x = GET_X_LPARAM(lParam);
		pt.y = GET_Y_LPARAM(lParam);
		item_id = (pt.x / SB_ICON_WIDTH);
		if (PtInRect((LPRECT) &rc, pt) && (item_id < sb_parts)) {
			if (sb_part_meanings[item_id] == SB_SOUND)
				SoundGainDialogCreate(hwndMain);
		}
		break;

	default:
		return(CallWindowProc((WNDPROC)OriginalProcedure,
				      hwnd, message, wParam, lParam));
    }

    return(0);
}


/* API: Create and set up the Status Bar window. */
void
StatusBarCreate(HWND hwndParent, uintptr_t idStatus, HINSTANCE hInst)
{
    RECT rectDialog;
    int dw, dh;
    uint8_t i;

    /* Load our icons into the cache for faster access. */
    for (i = 16; i < 18; i++)
	hIcon[i] = LoadIconEx((PCTSTR) (uintptr_t) i);
    for (i = 24; i < 26; i++)
	hIcon[i] = LoadIconEx((PCTSTR) (uintptr_t) i);
    for (i = 32; i < 34; i++)
	hIcon[i] = LoadIconEx((PCTSTR) (uintptr_t) i);
    for (i = 48; i < 50; i++)
	hIcon[i] = LoadIconEx((PCTSTR) (uintptr_t) i);
    for (i = 64; i < 66; i++)
	hIcon[i] = LoadIconEx((PCTSTR) (uintptr_t) i);
    for (i = 80; i < 82; i++)
	hIcon[i] = LoadIconEx((PCTSTR) (uintptr_t) i);
    for (i = 144; i < 146; i++)
	hIcon[i] = LoadIconEx((PCTSTR) (uintptr_t) i);
    for (i = 152; i < 154; i++)
	hIcon[i] = LoadIconEx((PCTSTR) (uintptr_t) i);
    for (i = 160; i < 162; i++)
	hIcon[i] = LoadIconEx((PCTSTR) (uintptr_t) i);
    for (i = 176; i < 178; i++)
	hIcon[i] = LoadIconEx((PCTSTR) (uintptr_t) i);
    for (i = 243; i < 244; i++)
	hIcon[i] = LoadIconEx((PCTSTR) (uintptr_t) i);

    GetWindowRect(hwndParent, &rectDialog);
    dw = rectDialog.right - rectDialog.left;
    dh = rectDialog.bottom - rectDialog.top;

    /* Load the Common Controls DLL if needed. */
    InitCommonControls();

    /* Create the window, and make sure it's using the STATUS class. */
    hwndSBAR = CreateWindowEx(0,
			      STATUSCLASSNAME, 
			      (LPCTSTR)NULL,
			      SBARS_SIZEGRIP|WS_CHILD|WS_VISIBLE|SBT_TOOLTIPS,
			      0, dh-17, dw, 17,
			      hwndParent,
			      (HMENU)idStatus, hInst, NULL);

    /* Replace the original procedure with ours. */
    OriginalProcedure = GetWindowLongPtr(hwndSBAR, GWLP_WNDPROC);
    SetWindowLongPtr(hwndSBAR, GWL_WNDPROC, (LONG_PTR)&StatusBarProcedure);

    SendMessage(hwndSBAR, SB_SETMINHEIGHT, (WPARAM)17, (LPARAM)0);

    /* Align the window with the parent (main) window. */
    GetWindowRect(hwndSBAR, &rectDialog);
    SetWindowPos(hwndSBAR,
		 HWND_TOPMOST,
		 rectDialog.left, rectDialog.top,
		 rectDialog.right-rectDialog.left,
		 rectDialog.bottom-rectDialog.top,
		 SWP_SHOWWINDOW);

    /* Initialize the status bar. This is clumsy. */
    sb_parts = 1;
    iStatusWidths = (int *)malloc(sb_parts * sizeof(int));
     memset(iStatusWidths, 0, sb_parts * sizeof(int));
    sb_part_meanings = (int *)malloc(sb_parts * sizeof(int));
     memset(sb_part_meanings, 0, sb_parts * sizeof(int));
    sb_part_icons = (uint8_t *)malloc(sb_parts * sizeof(uint8_t));
     memset(sb_part_icons, 0, sb_parts * sizeof(uint8_t));
    sbTips = (WCHAR **)malloc(sb_parts * sizeof(WCHAR *));
     memset(sbTips, 0, sb_parts * sizeof(WCHAR *));
    sb_parts = 0;
    iStatusWidths[sb_parts] = -1;
    sb_part_meanings[sb_parts] = SB_TEXT;
    sb_part_icons[sb_parts] = 255;
    sb_parts++;
    SendMessage(hwndSBAR, SB_SETPARTS, (WPARAM)sb_parts, (LPARAM)iStatusWidths);
    SendMessage(hwndSBAR, SB_SETTEXT, 0 | SBT_NOBORDERS,
		(LPARAM)plat_get_string(IDS_2117));

    sb_ready = 1;
}


/* API */
void
ui_sb_set_text_w(wchar_t *wstr)
{
    uint8_t part = 0xff;

    if (!sb_ready || (sb_parts == 0) || (sb_part_meanings == NULL))
	return;

    part = sb_map[SB_TEXT];

    if (part != 0xff)
	SendMessage(hwndSBAR, SB_SETTEXT, part | SBT_NOBORDERS, (LPARAM)wstr);
}


/* API */
void
ui_sb_set_text(char *str)
{
    static wchar_t wstr[512];

    memset(wstr, 0x00, sizeof(wstr));
    mbstowcs(wstr, str, strlen(str) + 1);
    ui_sb_set_text_w(wstr);
}


/* API */
void
ui_sb_bugui(char *str)
{
    static wchar_t wstr[512];

    memset(wstr, 0x00, sizeof(wstr));
    mbstowcs(wstr, str, strlen(str) + 1);
    ui_sb_set_text_w(wstr);
}
