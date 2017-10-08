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
 * Version:	@(#)win_stbar.c	1.0.2	2017/10/07
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
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
#include "../config.h"
#include "../ibm.h"
#include "../cpu/cpu.h"
#include "../device.h"
#include "../machine/machine.h"
#include "../cdrom/cdrom.h"
#include "../cdrom/cdrom_image.h"
#include "../cdrom/cdrom_null.h"
#include "../disk/hdd.h"
#include "../disk/hdc.h"
#include "../floppy/floppy.h"
#include "../floppy/fdd.h"
#include "../scsi/scsi.h"
#include "../scsi/scsi_disk.h"
#include "../network/network.h"
#include "../video/video.h"
#include "../sound/sound.h"
#include "plat_iodev.h"
#include "plat_ui.h"
#include "win.h"
#include "win_cdrom_ioctl.h"


HWND		hwndSBAR;


static LONG_PTR	OriginalStatusBarProcedure;
static HMENU	*sb_menu_handles;
static HMENU	menuSBAR;
static WCHAR	**sbTips;
static int	*iStatusWidths;
static int	*sb_icon_flags;
static int	*sb_part_meanings;
static int	*sb_part_icons;
static int	sb_parts = 0;
static int	sb_ready = 0;


/* Also used by win_settings.c */
int
fdd_type_to_icon(int type)
{
    int ret = 512;

    switch(type) {
	case 0:
		break;

	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
		ret = 128;
		break;

	case 7:
	case 8:
	case 9:
	case 10:
	case 11:
	case 12:
	case 13:
		ret = 144;
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


static int
display_network_icon(void)
{
    if ((network_type == 0) || (network_card == 0)) return(0);

    return(network_test());
}


static void
StatusBarCreateFloppySubmenu(HMENU m, int id)
{
    AppendMenu(m, MF_STRING, IDM_FLOPPY_IMAGE_NEW | id,
	       win_get_string(IDS_2161));
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_STRING, IDM_FLOPPY_IMAGE_EXISTING | id,
	       win_get_string(IDS_2162));
    AppendMenu(m, MF_STRING, IDM_FLOPPY_IMAGE_EXISTING_WP | id,
	       win_get_string(IDS_2163));
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_STRING, IDM_FLOPPY_EJECT | id,
	       win_get_string(IDS_2164));
}


static void
StatusBarCreateCdromSubmenu(HMENU m, int id)
{
    WCHAR s[64];
    int i;

    AppendMenu(m, MF_STRING, IDM_CDROM_MUTE | id,
	       win_get_string(IDS_2165));
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_STRING, IDM_CDROM_EMPTY | id,
	       win_get_string(IDS_2166));
    AppendMenu(m, MF_STRING, IDM_CDROM_RELOAD | id,
	       win_get_string(IDS_2167));
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_STRING, IDM_CDROM_IMAGE | id,
	       win_get_string(IDS_2168));

    if (host_cdrom_drive_available_num == 0) {
	if ((cdrom_drives[id].host_drive >= 'A') &&
	    (cdrom_drives[id].host_drive <= 'Z')) {
		cdrom_drives[id].host_drive = 0;
	}

	goto check_menu_items;
    } else {
	if ((cdrom_drives[id].host_drive >= 'A') &&
	    (cdrom_drives[id].host_drive <= 'Z')) {
		if (!host_cdrom_drive_available[cdrom_drives[id].host_drive - 'A']) {
			cdrom_drives[id].host_drive = 0;
		}
	}
    }

    AppendMenu(m, MF_SEPARATOR, 0, 0);

    for (i=0; i<26; i++) {
	_swprintf(s, L"Host CD/DVD Drive (%c:)", i+'A');
	if (host_cdrom_drive_available[i])
		AppendMenu(m, MF_STRING, IDM_CDROM_HOST_DRIVE | (i<<3)|id, s);
    }

check_menu_items:
    if (! cdrom_drives[id].sound_on)
	CheckMenuItem(m, IDM_CDROM_MUTE | id, MF_CHECKED);

    if (cdrom_drives[id].host_drive == 200)
	CheckMenuItem(m, IDM_CDROM_IMAGE | id, MF_CHECKED);
      else
    if ((cdrom_drives[id].host_drive >= 'A') && (cdrom_drives[id].host_drive <= 'Z')) {
	CheckMenuItem(m, IDM_CDROM_HOST_DRIVE | id |
		((cdrom_drives[id].host_drive - 'A') << 3), MF_CHECKED);
    } else {
	cdrom_drives[id].host_drive = 0;
	CheckMenuItem(m, IDM_CDROM_EMPTY | id, MF_CHECKED);
    }
}


static void
StatusBarCreateRemovableDiskSubmenu(HMENU m, int id)
{
    AppendMenu(m, MF_STRING, IDM_RDISK_EJECT | id,
	       win_get_string(IDS_2166));
    AppendMenu(m, MF_STRING, IDM_RDISK_RELOAD | id,
	       win_get_string(IDS_2167));
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_STRING, IDM_RDISK_SEND_CHANGE | id,
	       win_get_string(IDS_2142));
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_STRING, IDM_RDISK_IMAGE | id,
	       win_get_string(IDS_2168));
    AppendMenu(m, MF_STRING, IDM_RDISK_IMAGE_WP | id,
	       win_get_string(IDS_2169));
}


/* API */
int
StatusBarFindPart(int tag)
{
    int found = -1;
    int i;

    if (!sb_ready || (sb_parts == 0) || (sb_part_meanings == NULL)) {
	return -1;
    }

    for (i=0; i<sb_parts; i++) {
	if (sb_part_meanings[i] == tag) {
		found = i;
		break;
	}
    }

    return(found);
}


/* API: update one of the icons after activity. */
void
StatusBarUpdateIcon(int tag, int active)
{
    int temp_flags = 0;
    int found;

    if (((tag & 0xf0) >= SB_TEXT) || !sb_ready || (sb_parts == 0) || (sb_icon_flags == NULL) || (sb_part_icons == NULL)) {
	return;
    }

    temp_flags |= active;

    found = StatusBarFindPart(tag);
    if (found != -1) {
	if (temp_flags != (sb_icon_flags[found] & 1)) {
		sb_icon_flags[found] &= ~1;
		sb_icon_flags[found] |= active;

		sb_part_icons[found] &= ~257;
		sb_part_icons[found] |= sb_icon_flags[found];

		SendMessage(hwndSBAR, SB_SETICON, found,
			    (LPARAM)hIcon[sb_part_icons[found]]);
	}
    }
}


/* API: This is for the drive state indicator. */
void
StatusBarUpdateIconState(int tag, int state)
{
    int found = -1;

    if (((tag & 0xf0) >= SB_HDD) || !sb_ready || (sb_parts == 0) || (sb_icon_flags == NULL) || (sb_part_icons == NULL)) {
	return;
    }

    found = StatusBarFindPart(tag);
    if (found != -1) {
	sb_icon_flags[found] &= ~256;
	sb_icon_flags[found] |= state ? 256 : 0;

	sb_part_icons[found] &= ~257;
	sb_part_icons[found] |= sb_icon_flags[found];

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
	_swprintf(tempTip, win_get_string(IDS_2158),
		  drive+1, wtext, win_get_string(IDS_2057));
    } else {
	_swprintf(tempTip, win_get_string(IDS_2158),
		  drive+1, wtext, floppyfns[drive]);
    }

    if (sbTips[part] != NULL)
	free(sbTips[part]);
    sbTips[part] = (WCHAR *) malloc((wcslen(tempTip) << 1) + 2);
    wcscpy(sbTips[part], tempTip);
}


static void
StatusBarCreateCdromTip(int part)
{
    WCHAR wtext[512];
    WCHAR tempTip[512];
    WCHAR *szText;
    int id;
    int drive = sb_part_meanings[part] & 0xf;
    int bus = cdrom_drives[drive].bus_type;

    id = IDS_4352 + (bus - 1);
    szText = (WCHAR *)win_get_string(id);

    if (cdrom_drives[drive].host_drive == 200) {
	if (wcslen(cdrom_image[drive].image_path) == 0) {
		_swprintf(tempTip, win_get_string(IDS_5120), drive + 1, szText, win_get_string(IDS_2057));
	} else {
		_swprintf(tempTip, win_get_string(IDS_5120), drive + 1, szText, cdrom_image[drive].image_path);
	}
    } else if ((cdrom_drives[drive].host_drive >= 'A') && (cdrom_drives[drive].host_drive <= 'Z')) {
	_swprintf(wtext, win_get_string(IDS_2058), cdrom_drives[drive].host_drive & ~0x20);
	_swprintf(tempTip, win_get_string(IDS_5120), drive + 1, szText, wtext);
    } else {
	_swprintf(tempTip, win_get_string(IDS_5120), drive + 1, szText, win_get_string(IDS_2057));
    }

    if (sbTips[part] != NULL)
	free(sbTips[part]);
    sbTips[part] = (WCHAR *)malloc((wcslen(tempTip) << 1) + 2);
    wcscpy(sbTips[part], tempTip);
}


static void
StatusBarCreateRemovableDiskTip(int part)
{
    WCHAR tempTip[512];
    int drive = sb_part_meanings[part] & 0x1f;

    if (wcslen(hdd[drive].fn) == 0) {
	_swprintf(tempTip, win_get_string(IDS_4115), drive, win_get_string(IDS_2057));
    } else {
	_swprintf(tempTip, win_get_string(IDS_4115), drive, hdd[drive].fn);
    }

    if (sbTips[part] != NULL)
	free(sbTips[part]);
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
    szText = (WCHAR *)win_get_string(id);

    _swprintf(tempTip, win_get_string(IDS_4096), szText);
    if (sbTips[part] != NULL)
	free(sbTips[part]);
    sbTips[part] = (WCHAR *)malloc((wcslen(tempTip) << 1) + 2);
    wcscpy(sbTips[part], tempTip);
}


static void
StatusBarCreateNetworkTip(int part)
{
    WCHAR tempTip[512];

    _swprintf(tempTip, win_get_string(IDS_2069));

    if (sbTips[part] != NULL)
	free(sbTips[part]);
    sbTips[part] = (WCHAR *)malloc((wcslen(tempTip) << 1) + 2);
    wcscpy(sbTips[part], tempTip);
}


/* API */
void
StatusBarUpdateTip(int meaning)
{
    int part = -1;
    int i;

    if (!sb_ready || (sb_parts == 0) || (sb_part_meanings == NULL)) return;

    for (i=0; i<sb_parts; i++) {
	if (sb_part_meanings[i] == meaning) {
		part = i;
	}
    }

    if (part != -1) {
	switch(meaning & 0xf0) {
		case SB_FLOPPY:
			StatusBarCreateFloppyTip(part);
			break;

		case SB_CDROM:
			StatusBarCreateCdromTip(part);
			break;

		case SB_RDISK:
			StatusBarCreateRemovableDiskTip(part);
			break;

		case SB_HDD:
			StatusBarCreateDiskTip(part);
			break;

		case SB_NETWORK:
			StatusBarCreateNetworkTip(part);
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
StatusBarUpdatePanes(void)
{
    int i, id, hdint;
    int edge = 0;
    int c_mfm, c_esdi, c_scsi;
    int c_xtide, c_ide_pio, c_ide_dma;
    int do_net;

    sb_ready = 0;

    hdint = (machines[machine].flags & MACHINE_HAS_HDC) ? 1 : 0;
    c_mfm = hdd_count(HDD_BUS_MFM);
    c_esdi = hdd_count(HDD_BUS_ESDI);
    c_xtide = hdd_count(HDD_BUS_XTIDE);
    c_ide_pio = hdd_count(HDD_BUS_IDE_PIO_ONLY);
    c_ide_dma = hdd_count(HDD_BUS_IDE_PIO_AND_DMA);
    c_scsi = hdd_count(HDD_BUS_SCSI);
    do_net = display_network_icon();

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
	if (sb_icon_flags) {
		free(sb_icon_flags);
		sb_icon_flags = NULL;
	}
	StatusBarDestroyMenus();
	StatusBarDestroyTips();
    }
    sb_parts = 0;

    for (i=0; i<FDD_NUM; i++) {
	if (fdd_get_type(i) != 0)
		sb_parts++;
    }
    for (i=0; i<CDROM_NUM; i++) {
	/* Could be Internal or External IDE.. */
	if ((cdrom_drives[i].bus_type==CDROM_BUS_ATAPI_PIO_ONLY) &&
	    !(hdint || !memcmp(hdc_name, "ide", 3))) {
		continue;
	}

	/* Could be Internal or External IDE.. */
	if ((cdrom_drives[i].bus_type==CDROM_BUS_ATAPI_PIO_AND_DMA) &&
	    !(hdint || !memcmp(hdc_name, "ide", 3))) {
		continue;
	}

	if ((cdrom_drives[i].bus_type == CDROM_BUS_SCSI) &&
	    (scsi_card_current == 0)) {
		continue;
	}
	if (cdrom_drives[i].bus_type != 0) {
		sb_parts++;
	}
    }
    for (i=0; i<HDD_NUM; i++) {
	if ((hdd[i].bus==HDD_BUS_SCSI_REMOVABLE) && (scsi_card_current != 0)) {
		sb_parts++;
	}
    }
    if (c_mfm && (hdint || !memcmp(hdc_name, "mfm", 3))) {
	/* MFM drives, and MFM or Internal controller. */
	sb_parts++;
    }
    if (c_esdi && (hdint || !memcmp(hdc_name, "esdi", 4))) {
	/* ESDI drives, and ESDI or Internal controller. */
	sb_parts++;
    }
    if (c_xtide && !memcmp(hdc_name, "xtide", 5)) {
	sb_parts++;
    }
    if (c_ide_pio && (hdint || !memcmp(hdc_name, "ide", 3))) {
	/* IDE_PIO drives, and IDE or Internal controller. */
	sb_parts++;
    }
    if (c_ide_dma && (hdint || !memcmp(hdc_name, "ide", 3))) {
	/* IDE_DMA drives, and IDE or Internal controller. */
	sb_parts++;
    }
    if (c_scsi && (scsi_card_current != 0)) {
	sb_parts++;
    }
    if (do_net) {
	sb_parts++;
    }
    sb_parts++;

    iStatusWidths = (int *)malloc(sb_parts * sizeof(int));
     memset(iStatusWidths, 0, sb_parts * sizeof(int));
    sb_part_meanings = (int *)malloc(sb_parts * sizeof(int));
     memset(sb_part_meanings, 0, sb_parts * sizeof(int));
    sb_part_icons = (int *)malloc(sb_parts * sizeof(int));
     memset(sb_part_icons, 0, sb_parts * sizeof(int));
    sb_icon_flags = (int *)malloc(sb_parts * sizeof(int));
     memset(sb_icon_flags, 0, sb_parts * sizeof(int));
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
		sb_parts++;
	}
    }
    for (i=0; i<CDROM_NUM; i++) {
	/* Could be Internal or External IDE.. */
	if ((cdrom_drives[i].bus_type==CDROM_BUS_ATAPI_PIO_ONLY) &&
	    !(hdint || !memcmp(hdc_name, "ide", 3))) {
		continue;
	}
	/* Could be Internal or External IDE.. */
	if ((cdrom_drives[i].bus_type==CDROM_BUS_ATAPI_PIO_AND_DMA) &&
	    !(hdint || !memcmp(hdc_name, "ide", 3))) {
		continue;
	}
	if ((cdrom_drives[i].bus_type == CDROM_BUS_SCSI) && (scsi_card_current == 0)) {
		continue;
	}
	if (cdrom_drives[i].bus_type != 0) {
		edge += SB_ICON_WIDTH;
		iStatusWidths[sb_parts] = edge;
		sb_part_meanings[sb_parts] = SB_CDROM | i;
		sb_parts++;
	}
    }
    for (i=0; i<HDD_NUM; i++) {
	if ((hdd[i].bus==HDD_BUS_SCSI_REMOVABLE) && (scsi_card_current != 0)) {
		edge += SB_ICON_WIDTH;
		iStatusWidths[sb_parts] = edge;
		sb_part_meanings[sb_parts] = SB_RDISK | i;
		sb_parts++;
	}
    }
    if (c_mfm && (hdint || !memcmp(hdc_name, "mfm", 3))) {
	edge += SB_ICON_WIDTH;
	iStatusWidths[sb_parts] = edge;
	sb_part_meanings[sb_parts] = SB_HDD | HDD_BUS_MFM;
	sb_parts++;
    }
    if (c_esdi && (hdint || !memcmp(hdc_name, "esdi", 4))) {
	edge += SB_ICON_WIDTH;
	iStatusWidths[sb_parts] = edge;
	sb_part_meanings[sb_parts] = SB_HDD | HDD_BUS_ESDI;
	sb_parts++;
    }
    if (c_xtide && !memcmp(hdc_name, "xtide", 5)) {
	edge += SB_ICON_WIDTH;
	iStatusWidths[sb_parts] = edge;
	sb_part_meanings[sb_parts] = SB_HDD | HDD_BUS_XTIDE;
	sb_parts++;
    }
    if (c_ide_pio && (hdint || !memcmp(hdc_name, "ide", 3))) {
	edge += SB_ICON_WIDTH;
	iStatusWidths[sb_parts] = edge;
	sb_part_meanings[sb_parts] = SB_HDD | HDD_BUS_IDE_PIO_ONLY;
	sb_parts++;
    }
    if (c_ide_dma && (hdint || !memcmp(hdc_name, "ide", 3))) {
	edge += SB_ICON_WIDTH;
	iStatusWidths[sb_parts] = edge;
	sb_part_meanings[sb_parts] = SB_HDD | HDD_BUS_IDE_PIO_AND_DMA;
	sb_parts++;
    }
    if (c_scsi && (scsi_card_current != 0)) {
	edge += SB_ICON_WIDTH;
	iStatusWidths[sb_parts] = edge;
	sb_part_meanings[sb_parts] = SB_HDD | HDD_BUS_SCSI;
	sb_parts++;
    }
    if (do_net) {
	edge += SB_ICON_WIDTH;
	iStatusWidths[sb_parts] = edge;
	sb_part_meanings[sb_parts] = SB_NETWORK;
	sb_parts++;
    }

    if (sb_parts)
	iStatusWidths[sb_parts - 1] += (24 - SB_ICON_WIDTH);
    iStatusWidths[sb_parts] = -1;
    sb_part_meanings[sb_parts] = SB_TEXT;
    sb_parts++;

    SendMessage(hwndSBAR, SB_SETPARTS, (WPARAM)sb_parts, (LPARAM)iStatusWidths);

    for (i=0; i<sb_parts; i++) {
	switch (sb_part_meanings[i] & 0xf0) {
		case SB_FLOPPY:		/* Floppy */
			sb_icon_flags[i] = (wcslen(floppyfns[sb_part_meanings[i] & 0xf]) == 0) ? 256 : 0;
			sb_part_icons[i] = fdd_type_to_icon(fdd_get_type(sb_part_meanings[i] & 0xf)) | sb_icon_flags[i];
			sb_menu_handles[i] = StatusBarCreatePopupMenu(i);
			StatusBarCreateFloppySubmenu(sb_menu_handles[i], sb_part_meanings[i] & 0xf);
			EnableMenuItem(sb_menu_handles[i], IDM_FLOPPY_EJECT | (sb_part_meanings[i] & 0xf), MF_BYCOMMAND | ((sb_icon_flags[i] & 256) ? MF_GRAYED : MF_ENABLED));
			StatusBarCreateFloppyTip(i);
			break;

		case SB_CDROM:		/* CD-ROM */
			id = sb_part_meanings[i] & 0xf;
			if (cdrom_drives[id].host_drive == 200) {
				sb_icon_flags[i] = (wcslen(cdrom_image[id].image_path) == 0) ? 256 : 0;
			} else if ((cdrom_drives[id].host_drive >= 'A') && (cdrom_drives[id].host_drive <= 'Z')) {
				sb_icon_flags[i] = 0;
			} else {
				sb_icon_flags[i] = 256;
			}
			sb_part_icons[i] = 160 | sb_icon_flags[i];
			sb_menu_handles[i] = StatusBarCreatePopupMenu(i);
			StatusBarCreateCdromSubmenu(sb_menu_handles[i], sb_part_meanings[i] & 0xf);
			EnableMenuItem(sb_menu_handles[i], IDM_CDROM_RELOAD | (sb_part_meanings[i] & 0xf), MF_BYCOMMAND | MF_GRAYED);
			StatusBarCreateCdromTip(i);
			break;

		case SB_RDISK:		/* Removable hard disk */
			sb_icon_flags[i] = (wcslen(hdd[sb_part_meanings[i] & 0x1f].fn) == 0) ? 256 : 0;
			sb_part_icons[i] = 176 + sb_icon_flags[i];
			sb_menu_handles[i] = StatusBarCreatePopupMenu(i);
			StatusBarCreateRemovableDiskSubmenu(sb_menu_handles[i], sb_part_meanings[i] & 0x1f);
			EnableMenuItem(sb_menu_handles[i], IDM_RDISK_EJECT | (sb_part_meanings[i] & 0x1f), MF_BYCOMMAND | ((sb_icon_flags[i] & 256) ? MF_GRAYED : MF_ENABLED));
			EnableMenuItem(sb_menu_handles[i], IDM_RDISK_RELOAD | (sb_part_meanings[i] & 0x1f), MF_BYCOMMAND | MF_GRAYED);
			EnableMenuItem(sb_menu_handles[i], IDM_RDISK_SEND_CHANGE | (sb_part_meanings[i] & 0x1f), MF_BYCOMMAND | ((sb_icon_flags[i] & 256) ? MF_GRAYED : MF_ENABLED));
			StatusBarCreateRemovableDiskTip(i);
			break;

		case SB_HDD:		/* Hard disk */
			sb_part_icons[i] = 192;
			StatusBarCreateDiskTip(i);
			break;

		case SB_NETWORK:	/* Network */
			sb_part_icons[i] = 208;
			StatusBarCreateNetworkTip(i);
			break;

		case SB_TEXT:		/* Status text */
			SendMessage(hwndSBAR, SB_SETTEXT, i | SBT_NOBORDERS, (LPARAM) L"");
			sb_part_icons[i] = -1;
			break;
	}

	if (sb_part_icons[i] != -1) {
		SendMessage(hwndSBAR, SB_SETTEXT, i | SBT_NOBORDERS, (LPARAM)"");
		SendMessage(hwndSBAR, SB_SETICON, i, (LPARAM)hIcon[sb_part_icons[i]]);
		SendMessage(hwndSBAR, SB_SETTIPTEXT, i, (LPARAM)sbTips[i]);
	} else {
		SendMessage(hwndSBAR, SB_SETICON, i, (LPARAM)NULL);
	}
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


/* Handle messages for the Status Bar window. */
static LRESULT CALLBACK
StatusBarProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    WCHAR temp_image_path[1024];
    RECT rc;
    POINT pt;
    int new_cdrom_drive;
    int ret = 0;
    int item_id = 0;
    int item_params = 0;
    int id = 0;
    int part = 0;
    int letter = 0;

    switch (message) {
	case WM_COMMAND:
		item_id = LOWORD(wParam) & 0xff00;	/* low 8 bits */
		item_params = LOWORD(wParam) & 0x00ff;	/* high 8 bits */

                switch (item_id) {
			case IDM_FLOPPY_IMAGE_EXISTING:
			case IDM_FLOPPY_IMAGE_EXISTING_WP:
				id = item_params & 0x0003;
				part = StatusBarFindPart(SB_FLOPPY | id);
				if ((part == -1) || (sb_menu_handles == NULL))
					break;

				ret = file_dlg_w_st(hwnd, IDS_2159, floppyfns[id], 0);
				if (! ret) {
					floppy_close(id);
					ui_writeprot[id] = (item_id == IDM_FLOPPY_IMAGE_EXISTING_WP) ? 1 : 0;
					floppy_load(id, wopenfilestring);
					StatusBarUpdateIconState(SB_FLOPPY | id, wcslen(floppyfns[id]) ? 0 : 1);
					EnableMenuItem(sb_menu_handles[part], IDM_FLOPPY_EJECT | id, MF_BYCOMMAND | (wcslen(floppyfns[id]) ? MF_ENABLED : MF_GRAYED));
					StatusBarUpdateTip(SB_FLOPPY | id);
					config_save();
				}
				break;

			case IDM_FLOPPY_EJECT:
				id = item_params & 0x0003;
				part = StatusBarFindPart(SB_FLOPPY | id);
				if ((part == -1) || (sb_menu_handles == NULL))
						break;

				floppy_close(id);
				StatusBarUpdateIconState(SB_FLOPPY | id, 1);
				EnableMenuItem(sb_menu_handles[part], IDM_FLOPPY_EJECT | id, MF_BYCOMMAND | MF_GRAYED);
				StatusBarUpdateTip(SB_FLOPPY | id);
				config_save();
				break;

			case IDM_CDROM_MUTE:
				id = item_params & 0x0007;
				part = StatusBarFindPart(SB_CDROM | id);
				if ((part == -1) || (sb_menu_handles == NULL))
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
				part = StatusBarFindPart(SB_CDROM | id);
				if ((part == -1) || (sb_menu_handles == NULL))
						break;

				if (!file_dlg_w_st(hwnd, IDS_2075, cdrom_image[id].image_path, 0)) {
					cdrom_drives[id].prev_host_drive = cdrom_drives[id].host_drive;
					wcscpy(temp_image_path, wopenfilestring);
					if ((wcscmp(cdrom_image[id].image_path, temp_image_path) == 0) && (cdrom_drives[id].host_drive == 200)) {
						/* Switching from image to the same image. Do nothing. */
						break;
					}
					wcscpy(cdrom_image[id].prev_image_path, cdrom_image[id].image_path);
					cdrom_drives[id].handler->exit(id);
					cdrom_close(id);
					image_open(id, temp_image_path);
					/* Signal media change to the emulated machine. */
					cdrom_insert(id);
					CheckMenuItem(sb_menu_handles[part], IDM_CDROM_EMPTY | id, MF_UNCHECKED);
					if ((cdrom_drives[id].host_drive >= 'A') && (cdrom_drives[id].host_drive <= 'Z')) {
						CheckMenuItem(sb_menu_handles[part], IDM_CDROM_HOST_DRIVE | id | ((cdrom_drives[id].host_drive - 'A') << 3), MF_UNCHECKED);
					}
					cdrom_drives[id].host_drive = (wcslen(cdrom_image[id].image_path) == 0) ? 0 : 200;
					if (cdrom_drives[id].host_drive == 200) {
						CheckMenuItem(sb_menu_handles[part], IDM_CDROM_IMAGE | id, MF_CHECKED);
						StatusBarUpdateIconState(SB_CDROM | id, 0);
					} else {
						CheckMenuItem(sb_menu_handles[part], IDM_CDROM_IMAGE | id, MF_UNCHECKED);
						CheckMenuItem(sb_menu_handles[part], IDM_CDROM_EMPTY | id, MF_UNCHECKED);
						StatusBarUpdateIconState(SB_CDROM | id, 1);
					}
					EnableMenuItem(sb_menu_handles[part], IDM_CDROM_RELOAD | id, MF_BYCOMMAND | MF_GRAYED);
					StatusBarUpdateTip(SB_CDROM | id);
					config_save();
				}
				break;

			case IDM_CDROM_HOST_DRIVE:
				id = item_params & 0x0007;
				letter = ((item_params >> 3) & 0x001f) + 'A';
				part = StatusBarFindPart(SB_CDROM | id);
				if ((part == -1) || (sb_menu_handles == NULL))
				{
					break;
				}

				new_cdrom_drive = letter;
				if (cdrom_drives[id].host_drive == new_cdrom_drive)
				{
					/* Switching to the same drive. Do nothing. */
					break;
				}
				cdrom_drives[id].prev_host_drive = cdrom_drives[id].host_drive;
				cdrom_drives[id].handler->exit(id);
				cdrom_close(id);
				ioctl_open(id, new_cdrom_drive);
				/* Signal media change to the emulated machine. */
				cdrom_insert(id);
				CheckMenuItem(sb_menu_handles[part], IDM_CDROM_EMPTY | id, MF_UNCHECKED);
				if ((cdrom_drives[id].host_drive >= 'A') && (cdrom_drives[id].host_drive <= 'Z'))
				{
					CheckMenuItem(sb_menu_handles[part], IDM_CDROM_HOST_DRIVE | id | ((cdrom_drives[id].host_drive - 'A') << 3), MF_UNCHECKED);
				}
				CheckMenuItem(sb_menu_handles[part], IDM_CDROM_IMAGE | id, MF_UNCHECKED);
				cdrom_drives[id].host_drive = new_cdrom_drive;
				CheckMenuItem(sb_menu_handles[part], IDM_CDROM_HOST_DRIVE | id | ((cdrom_drives[id].host_drive - 'A') << 3), MF_CHECKED);
				EnableMenuItem(sb_menu_handles[part], IDM_CDROM_RELOAD | id, MF_BYCOMMAND | MF_GRAYED);
				StatusBarUpdateIconState(SB_CDROM | id, 0);
				StatusBarUpdateTip(SB_CDROM | id);
				config_save();
				break;

			case IDM_RDISK_EJECT:
				id = item_params & 0x001f;
				removable_disk_eject(id);
				break;

			case IDM_RDISK_RELOAD:
				id = item_params & 0x001f;
				removable_disk_reload(id);
				break;

			case IDM_RDISK_SEND_CHANGE:
				id = item_params & 0x001f;
				scsi_disk_insert(id);
				break;

			case IDM_RDISK_IMAGE:
			case IDM_RDISK_IMAGE_WP:
				id = item_params & 0x001f;
				ret = file_dlg_w_st(hwnd, IDS_4106, hdd[id].fn, id);
				if (!ret) {
					removable_disk_unload(id);
					memset(hdd[id].fn, 0, sizeof(hdd[id].fn));
					wcscpy(hdd[id].fn, wopenfilestring);
					hdd[id].wp = (item_id == IDM_RDISK_IMAGE_WP) ? 1 : 0;
					scsi_loadhd(hdd[id].scsi_id, hdd[id].scsi_lun, id);
					scsi_disk_insert(id);
					if (wcslen(hdd[id].fn) > 0) {
						StatusBarUpdateIconState(SB_RDISK | id, 0);
						EnableMenuItem(sb_menu_handles[part], IDM_RDISK_EJECT | id, MF_BYCOMMAND | MF_ENABLED);
						EnableMenuItem(sb_menu_handles[part], IDM_RDISK_RELOAD | id, MF_BYCOMMAND | MF_GRAYED);
						EnableMenuItem(sb_menu_handles[part], IDM_RDISK_SEND_CHANGE | id, MF_BYCOMMAND | MF_ENABLED);
					}
					else {
						StatusBarUpdateIconState(SB_RDISK | id, 1);
						EnableMenuItem(sb_menu_handles[part], IDM_RDISK_EJECT | id, MF_BYCOMMAND | MF_GRAYED);
						EnableMenuItem(sb_menu_handles[part], IDM_RDISK_RELOAD | id, MF_BYCOMMAND | MF_GRAYED);
						EnableMenuItem(sb_menu_handles[part], IDM_RDISK_SEND_CHANGE | id, MF_BYCOMMAND | MF_GRAYED);
					}
					StatusBarUpdateTip(SB_RDISK | id);
					config_save();
				}
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

	default:
		return(CallWindowProc((WNDPROC)OriginalStatusBarProcedure,
				      hwnd, message, wParam, lParam));
    }

    return(0);
}


/* API: Create and set up the Status Bar window. */
void
StatusBarCreate(HWND hwndParent, int idStatus, HINSTANCE hInst)
{
    RECT rectDialog;
    int dw, dh, i;

    /* Load our icons into the cache for faster access. */
    for (i = 128; i < 130; i++)
	hIcon[i] = LoadIconEx((PCTSTR) i);
    for (i = 144; i < 146; i++)
	hIcon[i] = LoadIconEx((PCTSTR) i);
    for (i = 160; i < 162; i++)
	hIcon[i] = LoadIconEx((PCTSTR) i);
    for (i = 176; i < 178; i++)
	hIcon[i] = LoadIconEx((PCTSTR) i);
    for (i = 192; i < 194; i++)
	hIcon[i] = LoadIconEx((PCTSTR) i);
    for (i = 208; i < 210; i++)
	hIcon[i] = LoadIconEx((PCTSTR) i);
    for (i = 384; i < 386; i++)
	hIcon[i] = LoadIconEx((PCTSTR) i);
    for (i = 400; i < 402; i++)
	hIcon[i] = LoadIconEx((PCTSTR) i);
    for (i = 416; i < 418; i++)
	hIcon[i] = LoadIconEx((PCTSTR) i);
    for (i = 432; i < 434; i++)
	hIcon[i] = LoadIconEx((PCTSTR) i);

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
    OriginalStatusBarProcedure = GetWindowLongPtr(hwndSBAR, GWLP_WNDPROC);
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

    /* Initialize the status bar and populate the icons and menus. */
    sb_parts = 0;
    StatusBarUpdatePanes();
}


/* API */
void
StatusBarCheckMenuItem(int tag, int id, int chk)
{
    int part;

    part = StatusBarFindPart(tag);
    if ((part == -1) || (sb_menu_handles == NULL))
        return;

    CheckMenuItem(sb_menu_handles[part], id, chk);
}


/* API */
void
StatusBarEnableMenuItem(int tag, int id, int flg)
{
    int part;

    part = StatusBarFindPart(tag);
    if ((part == -1) || (sb_menu_handles == NULL))
        return;

    EnableMenuItem(sb_menu_handles[part], id, flg);
}


/* API */
void
StatusBarSetTextW(wchar_t *wstr)
{
    int part = -1;
    int i;

    if (!sb_ready || (sb_parts == 0) || (sb_part_meanings == NULL)) return;

    for (i=0; i<sb_parts; i++) {
	if (sb_part_meanings[i] == SB_TEXT) {
		part = i;
	}
    }

    if (part != -1)
	SendMessage(hwndSBAR, SB_SETTEXT, part | SBT_NOBORDERS, (LPARAM)wstr);
}



/* API */
void
StatusBarSetText(char *str)
{
    static wchar_t cwstr[512];

    memset(cwstr, 0, 1024);
    mbstowcs(cwstr, str, strlen(str) + 1);
    StatusBarSetTextW(cwstr);
}
