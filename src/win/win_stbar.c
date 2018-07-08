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
 * Version:	@(#)win_stbar.c	1.0.18	2018/05/25
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
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
#include "../86box.h"
#include "../config.h"
#include "../cpu/cpu.h"
#include "../device.h"
#include "../machine/machine.h"
#include "../disk/hdd.h"
#include "../disk/hdc.h"
#include "../floppy/fdd.h"
#include "../scsi/scsi.h"
#include "../cdrom/cdrom.h"
#include "../disk/zip.h"
#include "../cdrom/cdrom_image.h"
#include "../cdrom/cdrom_null.h"
#include "../scsi/scsi_disk.h"
#include "../network/network.h"
#include "../video/video.h"
#include "../sound/sound.h"
#include "../plat.h"
#include "../ui.h"
#include "win.h"

#ifndef GWL_WNDPROC
#define GWL_WNDPROC GWLP_WNDPROC
#endif


HWND		hwndSBAR;
int		update_icons = 1;


static LONG_PTR	OriginalProcedure;
static HMENU	*sb_menu_handles;
static HMENU	menuSBAR;
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


static void
StatusBarCreateFloppySubmenu(HMENU m, int id)
{
    AppendMenu(m, MF_STRING, IDM_FLOPPY_IMAGE_NEW | id,
	       plat_get_string(IDS_2096));
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_STRING, IDM_FLOPPY_IMAGE_EXISTING | id,
	       plat_get_string(IDS_2097));
    AppendMenu(m, MF_STRING, IDM_FLOPPY_IMAGE_EXISTING_WP | id,
	       plat_get_string(IDS_2098));
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_STRING, IDM_FLOPPY_EXPORT_TO_86F | id,
	       plat_get_string(IDS_2080));
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_STRING, IDM_FLOPPY_EJECT | id,
	       plat_get_string(IDS_2093));

    if (floppyfns[id][0] == 0x0000) {
	EnableMenuItem(m, IDM_FLOPPY_EJECT | id, MF_BYCOMMAND | MF_GRAYED);
	EnableMenuItem(m, IDM_FLOPPY_EXPORT_TO_86F | id, MF_BYCOMMAND | MF_GRAYED);
    }
}


static void
StatusBarCreateCdromSubmenu(HMENU m, int id)
{
    AppendMenu(m, MF_STRING, IDM_CDROM_MUTE | id,
	       plat_get_string(IDS_2092));
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_STRING, IDM_CDROM_EMPTY | id,
	       plat_get_string(IDS_2091));
    AppendMenu(m, MF_STRING, IDM_CDROM_RELOAD | id,
	       plat_get_string(IDS_2090));
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_STRING, IDM_CDROM_IMAGE | id,
	       plat_get_string(IDS_2089));

    if (! cdrom_drives[id].sound_on)
	CheckMenuItem(m, IDM_CDROM_MUTE | id, MF_CHECKED);

    if (cdrom_drives[id].host_drive == 200)
	CheckMenuItem(m, IDM_CDROM_IMAGE | id, MF_CHECKED);
    else {
	cdrom_drives[id].host_drive = 0;
	CheckMenuItem(m, IDM_CDROM_EMPTY | id, MF_CHECKED);
    }
}


static void
StatusBarCreateZIPSubmenu(HMENU m, int id)
{
    AppendMenu(m, MF_STRING, IDM_ZIP_IMAGE_NEW | id,
	       plat_get_string(IDS_2096));
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_STRING, IDM_ZIP_IMAGE_EXISTING | id,
	       plat_get_string(IDS_2097));
    AppendMenu(m, MF_STRING, IDM_ZIP_IMAGE_EXISTING_WP | id,
	       plat_get_string(IDS_2098));
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_STRING, IDM_ZIP_EJECT | id,
	       plat_get_string(IDS_2093));
    AppendMenu(m, MF_STRING, IDM_ZIP_RELOAD | id,
	       plat_get_string(IDS_2090));

    if (zip_drives[id].image_path[0] == 0x0000) {
	EnableMenuItem(m, IDM_ZIP_EJECT | id, MF_BYCOMMAND | MF_GRAYED);
	EnableMenuItem(m, IDM_ZIP_RELOAD | id, MF_BYCOMMAND | MF_ENABLED);
    } else {
	EnableMenuItem(m, IDM_ZIP_EJECT | id, MF_BYCOMMAND | MF_ENABLED);
	EnableMenuItem(m, IDM_ZIP_RELOAD | id, MF_BYCOMMAND | MF_GRAYED);
    }
}


/* API */
/* API: update one of the icons after activity. */
void
ui_sb_update_icon(int tag, int active)
{
    uint8_t found = 0xff;

    if (!update_icons)
	return;

    if (((tag & 0xf0) >= SB_TEXT) || !sb_ready)
	return;

    found = sb_map[tag];
    if (found != 0xff) {
	sb_part_icons[found] &= ~1;
	sb_part_icons[found] |= (uint8_t) active;

	SendMessage(hwndSBAR, SB_SETICON, found,
		    (LPARAM)hIcon[sb_part_icons[found]]);
    }
}


/* API: This is for the drive state indicator. */
void
ui_sb_update_icon_state(int tag, int state)
{
    uint8_t found = 0xff;

    if (((tag & 0xf0) >= SB_HDD) || !sb_ready)
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
	_swprintf(tempTip, plat_get_string(IDS_2117),
		  drive+1, wtext, plat_get_string(IDS_2057));
    } else {
	_swprintf(tempTip, plat_get_string(IDS_2117),
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
    int bus = cdrom_drives[drive].bus_type;

    id = IDS_5377 + (bus - 1);
    szText = plat_get_string(id);

    if (cdrom_drives[drive].host_drive == 200) {
	if (wcslen(cdrom_image[drive].image_path) == 0)
		_swprintf(tempTip, plat_get_string(IDS_5120), drive+1, szText, plat_get_string(IDS_2057));
	else
		_swprintf(tempTip, plat_get_string(IDS_5120), drive+1, szText, cdrom_image[drive].image_path);
    } else
	_swprintf(tempTip, plat_get_string(IDS_5120), drive+1, szText, plat_get_string(IDS_2057));

    if (sbTips[part] != NULL) {
	free(sbTips[part]);
	sbTips[part] = NULL;
    }
    sbTips[part] = (WCHAR *)malloc((wcslen(tempTip) << 1) + 2);
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

    if (!sb_ready || (sb_parts == 0) || (sb_part_meanings == NULL)) return;

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
StatusBarDestroyMenus(void)
{
    int i;

    if (sb_parts == 0) return;

    if (! sb_menu_handles) return;

    for (i=0; i<sb_parts; i++) {
	if (sb_menu_handles[i]) {
		DestroyMenu(sb_menu_handles[i]);
		sb_menu_handles[i] = NULL;
	}
    }

    free(sb_menu_handles);
    sb_menu_handles = NULL;
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


static HMENU
StatusBarCreatePopupMenu(int part)
{
    HMENU h;

    h = CreatePopupMenu();
    AppendMenu(menuSBAR, MF_POPUP, (UINT_PTR)h, 0);

    return(h);
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

    sb_ready = 0;

    hdint = (machines[machine].flags & MACHINE_HDC) ? 1 : 0;
    c_mfm = hdd_count(HDD_BUS_MFM);
    c_esdi = hdd_count(HDD_BUS_ESDI);
    c_xta = hdd_count(HDD_BUS_XTA);
    c_ide = hdd_count(HDD_BUS_IDE);
    c_scsi = hdd_count(HDD_BUS_SCSI);
    do_net = network_available();

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
	StatusBarDestroyMenus();
	StatusBarDestroyTips();
    }

    memset(sb_map, 0xff, sizeof(sb_map));

    sb_parts = 0;
    for (i=0; i<FDD_NUM; i++) {
	if (fdd_get_type(i) != 0)
		sb_parts++;
    }
    for (i=0; i<CDROM_NUM; i++) {
	/* Could be Internal or External IDE.. */
	if ((cdrom_drives[i].bus_type == CDROM_BUS_ATAPI) &&
	    !(hdint || !memcmp(hdc_name, "ide", 3)))
		continue;

	if ((cdrom_drives[i].bus_type == CDROM_BUS_SCSI) &&
	    (scsi_card_current == 0))
		continue;
	if (cdrom_drives[i].bus_type != 0)
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
    if (c_mfm && (hdint || !memcmp(hdc_name, "mfm", 3))) {
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
    sb_menu_handles = (HMENU *)malloc(sb_parts * sizeof(HMENU));
     memset(sb_menu_handles, 0, sb_parts * sizeof(HMENU));
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
	if ((cdrom_drives[i].bus_type == CDROM_BUS_ATAPI) &&
	    !(hdint || !memcmp(hdc_name, "ide", 3))) {
		continue;
	}
	if ((cdrom_drives[i].bus_type == CDROM_BUS_SCSI) && (scsi_card_current == 0))
		continue;
	if (cdrom_drives[i].bus_type != 0) {
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
    if (c_mfm && (hdint || !memcmp(hdc_name, "mfm", 3))) {
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
			sb_menu_handles[i] = StatusBarCreatePopupMenu(i);
			StatusBarCreateFloppySubmenu(sb_menu_handles[i], sb_part_meanings[i] & 0xf);
			EnableMenuItem(sb_menu_handles[i], IDM_FLOPPY_EJECT | (sb_part_meanings[i] & 0xf), MF_BYCOMMAND | ((sb_part_icons[i] & 128) ? MF_GRAYED : MF_ENABLED));
			StatusBarCreateFloppyTip(i);
			break;

		case SB_CDROM:		/* CD-ROM */
			id = sb_part_meanings[i] & 0xf;
			if (cdrom_drives[id].host_drive == 200)
				sb_part_icons[i] = (wcslen(cdrom_image[id].image_path) == 0) ? 128 : 0;
			else
				sb_part_icons[i] = 128;
			sb_part_icons[i] |= 32;
			sb_menu_handles[i] = StatusBarCreatePopupMenu(i);
			StatusBarCreateCdromSubmenu(sb_menu_handles[i], sb_part_meanings[i] & 0xf);
			EnableMenuItem(sb_menu_handles[i], IDM_CDROM_RELOAD | (sb_part_meanings[i] & 0xf), MF_BYCOMMAND | MF_GRAYED);
			StatusBarCreateCdromTip(i);
			break;

		case SB_ZIP:		/* Iomega ZIP */
			sb_part_icons[i] = (wcslen(zip_drives[sb_part_meanings[i] & 0xf].image_path) == 0) ? 128 : 0;
			sb_part_icons[i] |= 48;
			sb_menu_handles[i] = StatusBarCreatePopupMenu(i);
			StatusBarCreateZIPSubmenu(sb_menu_handles[i], sb_part_meanings[i] & 0xf);
			EnableMenuItem(sb_menu_handles[i], IDM_ZIP_EJECT | (sb_part_meanings[i] & 0xf), MF_BYCOMMAND | ((sb_part_icons[i] & 128) ? MF_GRAYED : MF_ENABLED));
			StatusBarCreateZIPTip(i);
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
    if (id >= (sb_parts - 1)) return;

    pt.x = id * SB_ICON_WIDTH;	/* Justify to the left. */
    pt.y = 0;			/* Justify to the top. */
    ClientToScreen(hwnd, (LPPOINT) &pt);
    TrackPopupMenu(sb_menu_handles[id],
		   TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_LEFTBUTTON,
		   pt.x, pt.y, 0, hwndSBAR, NULL);
}


void
ui_sb_mount_floppy_img(uint8_t id, int part, uint8_t wp, wchar_t *file_name)
{
    fdd_close(id);
    ui_writeprot[id] = wp;
    fdd_load(id, file_name);
    ui_sb_update_icon_state(SB_FLOPPY | id, wcslen(floppyfns[id]) ? 0 : 1);
    EnableMenuItem(sb_menu_handles[part], IDM_FLOPPY_EJECT | id, MF_BYCOMMAND | (wcslen(floppyfns[id]) ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(sb_menu_handles[part], IDM_FLOPPY_EXPORT_TO_86F | id, MF_BYCOMMAND | (wcslen(floppyfns[id]) ? MF_ENABLED : MF_GRAYED));
    ui_sb_update_tip(SB_FLOPPY | id);
    config_save();
}


void
ui_sb_mount_zip_img(uint8_t id, int part, uint8_t wp, wchar_t *file_name)
{
    zip_disk_close(zip[id]);
    zip_drives[id].ui_writeprot = wp;
    zip_load(zip[id], file_name);
    zip_insert(zip[id]);
    ui_sb_update_icon_state(SB_ZIP | id, wcslen(zip_drives[id].image_path) ? 0 : 1);
    EnableMenuItem(sb_menu_handles[part], IDM_ZIP_EJECT | id, MF_BYCOMMAND | (wcslen(zip_drives[id].image_path) ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(sb_menu_handles[part], IDM_ZIP_RELOAD | id, MF_BYCOMMAND | (wcslen(zip_drives[id].image_path) ? MF_GRAYED : MF_ENABLED));
    ui_sb_update_tip(SB_ZIP | id);
    config_save();
}


/* Handle messages for the Status Bar window. */
#ifdef __amd64__
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
StatusBarProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    WCHAR temp_path[1024];
    RECT rc;
    POINT pt;
    int ret = 0;
    int item_id = 0;
    int item_params = 0;
    int id = 0;
    uint8_t part = 0;

    switch (message) {
	case WM_COMMAND:
		item_id = LOWORD(wParam) & 0xff00;	/* low 8 bits */
		item_params = LOWORD(wParam) & 0x00ff;	/* high 8 bits */

                switch (item_id) {
			case IDM_FLOPPY_IMAGE_NEW:
				id = item_params & 0x0003;
				part = sb_map[SB_FLOPPY | id];
				NewFloppyDialogCreate(hwnd, id, part);
				break;

			case IDM_FLOPPY_IMAGE_EXISTING:
			case IDM_FLOPPY_IMAGE_EXISTING_WP:
				id = item_params & 0x0003;
				part = sb_map[SB_FLOPPY | id];
				if ((part == 0xff) || (sb_menu_handles == NULL))
					break;

				ret = file_dlg_w_st(hwnd, IDS_2118, floppyfns[id], 0);
				if (! ret)
					ui_sb_mount_floppy_img(id, part, (item_id == IDM_FLOPPY_IMAGE_EXISTING_WP) ? 1 : 0, wopenfilestring);
				break;

			case IDM_FLOPPY_EJECT:
				id = item_params & 0x0003;
				part = sb_map[SB_FLOPPY | id];
				if ((part == 0xff) || (sb_menu_handles == NULL))
						break;

				fdd_close(id);
				ui_sb_update_icon_state(SB_FLOPPY | id, 1);
				EnableMenuItem(sb_menu_handles[part], IDM_FLOPPY_EJECT | id, MF_BYCOMMAND | MF_GRAYED);
				EnableMenuItem(sb_menu_handles[part], IDM_FLOPPY_EXPORT_TO_86F | id, MF_BYCOMMAND | MF_GRAYED);
				ui_sb_update_tip(SB_FLOPPY | id);
				config_save();
				break;

			case IDM_FLOPPY_EXPORT_TO_86F:
				id = item_params & 0x0003;
				part = sb_map[SB_FLOPPY | id];
				if ((part == 0xff) || (sb_menu_handles == NULL))
					break;

				ret = file_dlg_w_st(hwnd, IDS_2076, floppyfns[id], 1);
				if (! ret) {
					plat_pause(1);
					ret = d86f_export(id, wopenfilestring);
					if (!ret)
						ui_msgbox(MBX_ERROR, (wchar_t *)IDS_4108);
					plat_pause(0);
				}
				break;

			case IDM_CDROM_MUTE:
				id = item_params & 0x0007;
				part = sb_map[SB_CDROM | id];
				if ((part == 0xff) || (sb_menu_handles == NULL))
						break;

				cdrom_drives[id].sound_on ^= 1;
				CheckMenuItem(sb_menu_handles[part], IDM_CDROM_MUTE | id, cdrom_drives[id].sound_on ? MF_UNCHECKED : MF_CHECKED);
				config_save();
				sound_cd_thread_reset();
				break;

			case IDM_CDROM_EMPTY:
				id = item_params & 0x0007;
				cdrom_eject(id);
				break;

			case IDM_CDROM_RELOAD:
				id = item_params & 0x0007;
				cdrom_reload(id);
				break;

			case IDM_CDROM_IMAGE:
				id = item_params & 0x0007;
				part = sb_map[SB_CDROM | id];
				if ((part == 0xff) || (sb_menu_handles == NULL))
						break;

				if (!file_dlg_w_st(hwnd, IDS_2075, cdrom_image[id].image_path, 0)) {
					cdrom_drives[id].prev_host_drive = cdrom_drives[id].host_drive;
					wcscpy(temp_path, wopenfilestring);
					if (!cdrom_image[id].prev_image_path)
						cdrom_image[id].prev_image_path = (wchar_t *) malloc(1024);
					wcscpy(cdrom_image[id].prev_image_path, cdrom_image[id].image_path);
					cdrom[id]->handler->exit(id);
					cdrom_close_handler(id);
					memset(cdrom_image[id].image_path, 0, 2048);
					image_open(id, temp_path);
					/* Signal media change to the emulated machine. */
					cdrom_insert(cdrom[id]);
					CheckMenuItem(sb_menu_handles[part], IDM_CDROM_EMPTY | id, MF_UNCHECKED);
					cdrom_drives[id].host_drive = (wcslen(cdrom_image[id].image_path) == 0) ? 0 : 200;
					if (cdrom_drives[id].host_drive == 200) {
						CheckMenuItem(sb_menu_handles[part], IDM_CDROM_IMAGE | id, MF_CHECKED);
						ui_sb_update_icon_state(SB_CDROM | id, 0);
					} else {
						CheckMenuItem(sb_menu_handles[part], IDM_CDROM_IMAGE | id, MF_UNCHECKED);
						CheckMenuItem(sb_menu_handles[part], IDM_CDROM_EMPTY | id, MF_UNCHECKED);
						ui_sb_update_icon_state(SB_CDROM | id, 1);
					}
					EnableMenuItem(sb_menu_handles[part], IDM_CDROM_RELOAD | id, MF_BYCOMMAND | MF_GRAYED);
					ui_sb_update_tip(SB_CDROM | id);
					config_save();
				}
				break;

			case IDM_ZIP_IMAGE_NEW:
				id = item_params & 0x0003;
				part = sb_map[SB_ZIP | id];
				NewFloppyDialogCreate(hwnd, id | 0x80, part);	/* NewZIPDialogCreate */
				break;

			case IDM_ZIP_IMAGE_EXISTING:
			case IDM_ZIP_IMAGE_EXISTING_WP:
				id = item_params & 0x0003;
				part = sb_map[SB_ZIP | id];
				if ((part == 0xff) || (sb_menu_handles == NULL))
					break;

				ret = file_dlg_w_st(hwnd, IDS_2058, zip_drives[id].image_path, 0);
				if (! ret)
					ui_sb_mount_zip_img(id, part, (item_id == IDM_ZIP_IMAGE_EXISTING_WP) ? 1 : 0, wopenfilestring);
				break;

			case IDM_ZIP_EJECT:
				id = item_params & 0x0003;
				zip_eject(id);
				break;

			case IDM_ZIP_RELOAD:
				id = item_params & 0x0003;
				zip_reload(id);
				break;

			default:
				break;
		}
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

    /* Load the dummu menu for this window. */
    menuSBAR = LoadMenu(hInst, SB_MENU_NAME);

    /* Initialize the status bar. This is clumsy. */
    sb_parts = 1;
    iStatusWidths = (int *)malloc(sb_parts * sizeof(int));
     memset(iStatusWidths, 0, sb_parts * sizeof(int));
    sb_part_meanings = (int *)malloc(sb_parts * sizeof(int));
     memset(sb_part_meanings, 0, sb_parts * sizeof(int));
    sb_part_icons = (uint8_t *)malloc(sb_parts * sizeof(uint8_t));
     memset(sb_part_icons, 0, sb_parts * sizeof(uint8_t));
    sb_menu_handles = (HMENU *)malloc(sb_parts * sizeof(HMENU));
     memset(sb_menu_handles, 0, sb_parts * sizeof(HMENU));
    sbTips = (WCHAR **)malloc(sb_parts * sizeof(WCHAR *));
     memset(sbTips, 0, sb_parts * sizeof(WCHAR *));
    sb_parts = 0;
    iStatusWidths[sb_parts] = -1;
    sb_part_meanings[sb_parts] = SB_TEXT;
    sb_part_icons[sb_parts] = 255;
    sb_parts++;
    SendMessage(hwndSBAR, SB_SETPARTS, (WPARAM)sb_parts, (LPARAM)iStatusWidths);
    SendMessage(hwndSBAR, SB_SETTEXT, 0 | SBT_NOBORDERS,
		(LPARAM)L"Welcome to 86Box !");
    sb_ready = 1;
}


/* API (Settings) */
void
ui_sb_check_menu_item(int tag, int id, int chk)
{
    uint8_t part;

    part = sb_map[tag];
    if ((part == 0xff) || (sb_menu_handles == NULL))
        return;

    CheckMenuItem(sb_menu_handles[part], id, chk);
}


/* API (Settings) */
void
ui_sb_enable_menu_item(int tag, int id, int flg)
{
    uint8_t part;

    part = sb_map[tag];
    if ((part == 0xff) || (sb_menu_handles == NULL))
        return;

    EnableMenuItem(sb_menu_handles[part], id, flg);
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
