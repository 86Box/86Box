/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		The Emulator's Windows core.
 *
 * Version:	@(#)win.c	1.0.15	2017/10/05
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#undef BITMAP
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../config.h"
#include "../cpu/cpu.h"
#include "../ibm.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "../nvr.h"
#include "../mouse.h"
#include "../machine/machine.h"
#include "../cdrom/cdrom.h"
#include "../cdrom/cdrom_ioctl.h"
#include "../cdrom/cdrom_image.h"
#include "../cdrom/cdrom_null.h"
#include "../disk/hdd.h"
#include "../disk/hdc.h"
#include "../disk/hdc_ide.h"
#include "../floppy/floppy.h"
#include "../floppy/fdd.h"
#include "../scsi/scsi.h"
#include "../scsi/scsi_disk.h"
#include "../network/network.h"
#include "../video/video.h"
#include "../video/vid_ega.h"
#include "../sound/sound.h"
#include "../sound/snd_dbopl.h"
#include "plat_keyboard.h"
#include "plat_iodev.h"
#include "plat_mouse.h"
#include "plat_midi.h"
#include "plat_thread.h"
#include "plat_ticks.h"
#include "plat_ui.h"
#include "win.h"
#include "win_cgapal.h"
#include "win_ddraw.h"
#include "win_d3d.h"
#include "win_language.h"


#ifndef MAPVK_VK_TO_VSC
#define MAPVK_VK_TO_VSC 0
#endif


#define TIMER_1SEC	1

extern int	updatestatus;


int		recv_key[272];
HWND		hwndMain;
HMENU		menuMain;
HANDLE		ghMutex;
HANDLE		slirpMutex;
HINSTANCE	hinstance;
RECT		oldclip;

int		pause = 0;
int		scale = 0;
uint64_t	timer_freq;
int		winsizex=640, winsizey=480;
int		efwinsizey=480;
int		gfx_present[GFX_MAX];
int		infocus=1;
int		drawits=0;
int		quited=0;
int		mousecapture=0;
uint64_t	main_time;


static RAWINPUTDEVICE	device;
static HHOOK	hKeyboardHook;
static int	hook_enabled = 0;

static HANDLE	thMain;
static HICON	hIcon[512];
static HWND	hwndRender;		/* machine render window */
static wchar_t	wTitle[512];
static int	save_window_pos = 0;
static int	win_doresize = 0;
static int	leave_fullscreen_flag = 0;
static int	unscaled_size_x = 0;
static int	unscaled_size_y = 0;
static uint64_t	start_time;
static uint64_t	end_time;
static uint8_t	host_cdrom_drive_available[26];
static uint8_t	host_cdrom_drive_available_num = 0;
static wchar_t	**argv;
static int	argc;
static wchar_t	*argbuf;
static struct {
    int		(*init)(HWND h);
    void	(*close)(void);
    void	(*resize)(int x, int y);
} vid_apis[2][2] = {
  {
    {	ddraw_init, ddraw_close, NULL		},
    {	d3d_init, d3d_close, d3d_resize		}
  },
  {
    {	ddraw_fs_init, ddraw_fs_close, NULL	},
    {	d3d_fs_init, d3d_fs_close, NULL		}
  }
};


static HICON
LoadIconEx(PCTSTR pszIconName)
{
    return((HICON)LoadImage(hinstance, pszIconName, IMAGE_ICON,
						16, 16, LR_SHARED));
}


static HICON
LoadIconBig(PCTSTR pszIconName)
{
    return((HICON)LoadImage(hinstance, pszIconName, IMAGE_ICON, 64, 64, 0));
}


/****************************************************************
 *			Status Bar Handling			*
 ****************************************************************/

static HWND	hwndSBAR;		/* status bar window */
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
	       win_language_get_string_from_id(IDS_2161));
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_STRING, IDM_FLOPPY_IMAGE_EXISTING | id,
	       win_language_get_string_from_id(IDS_2162));
    AppendMenu(m, MF_STRING, IDM_FLOPPY_IMAGE_EXISTING_WP | id,
	       win_language_get_string_from_id(IDS_2163));
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_STRING, IDM_FLOPPY_EJECT | id,
	       win_language_get_string_from_id(IDS_2164));
}


static void
StatusBarCreateCdromSubmenu(HMENU m, int id)
{
    WCHAR s[64];
    int i;

    AppendMenu(m, MF_STRING, IDM_CDROM_MUTE | id,
	       win_language_get_string_from_id(IDS_2165));
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_STRING, IDM_CDROM_EMPTY | id,
	       win_language_get_string_from_id(IDS_2166));
    AppendMenu(m, MF_STRING, IDM_CDROM_RELOAD | id,
	       win_language_get_string_from_id(IDS_2167));
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_STRING, IDM_CDROM_IMAGE | id,
	       win_language_get_string_from_id(IDS_2168));

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
	       win_language_get_string_from_id(IDS_2166));
    AppendMenu(m, MF_STRING, IDM_RDISK_RELOAD | id,
	       win_language_get_string_from_id(IDS_2167));
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_STRING, IDM_RDISK_SEND_CHANGE | id,
	       win_language_get_string_from_id(IDS_2142));
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_STRING, IDM_RDISK_IMAGE | id,
	       win_language_get_string_from_id(IDS_2168));
    AppendMenu(m, MF_STRING, IDM_RDISK_IMAGE_WP | id,
	       win_language_get_string_from_id(IDS_2169));
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
	_swprintf(tempTip,  win_language_get_string_from_id(IDS_2158),
		  drive+1, wtext, win_language_get_string_from_id(IDS_2057));
    } else {
	_swprintf(tempTip,  win_language_get_string_from_id(IDS_2158),
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
    szText = (WCHAR *)win_language_get_string_from_id(id);

    if (cdrom_drives[drive].host_drive == 200) {
	if (wcslen(cdrom_image[drive].image_path) == 0) {
		_swprintf(tempTip, win_language_get_string_from_id(IDS_5120), drive + 1, szText, win_language_get_string_from_id(IDS_2057));
	} else {
		_swprintf(tempTip, win_language_get_string_from_id(IDS_5120), drive + 1, szText, cdrom_image[drive].image_path);
	}
    } else if ((cdrom_drives[drive].host_drive >= 'A') && (cdrom_drives[drive].host_drive <= 'Z')) {
	_swprintf(wtext, win_language_get_string_from_id(IDS_2058), cdrom_drives[drive].host_drive & ~0x20);
	_swprintf(tempTip, win_language_get_string_from_id(IDS_5120), drive + 1, szText, wtext);
    } else {
	_swprintf(tempTip, win_language_get_string_from_id(IDS_5120), drive + 1, szText, win_language_get_string_from_id(IDS_2057));
    }

    if (sbTips[part] != NULL)
	free(sbTips[part]);
    sbTips[part] = (WCHAR *) malloc((wcslen(tempTip) << 1) + 2);
    wcscpy(sbTips[part], tempTip);
}


static void
StatusBarCreateRemovableDiskTip(int part)
{
    WCHAR tempTip[512];
    int drive = sb_part_meanings[part] & 0x1f;

    if (wcslen(hdd[drive].fn) == 0) {
	_swprintf(tempTip,  win_language_get_string_from_id(IDS_4115), drive, win_language_get_string_from_id(IDS_2057));
    } else {
	_swprintf(tempTip,  win_language_get_string_from_id(IDS_4115), drive, hdd[drive].fn);
    }

    if (sbTips[part] != NULL)
	free(sbTips[part]);
    sbTips[part] = (WCHAR *) malloc((wcslen(tempTip) << 1) + 2);
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
    szText = (WCHAR *)win_language_get_string_from_id(id);

    _swprintf(tempTip, win_language_get_string_from_id(IDS_4096), szText);
    if (sbTips[part] != NULL)
	free(sbTips[part]);
    sbTips[part] = (WCHAR *) malloc((wcslen(tempTip) << 1) + 2);
    wcscpy(sbTips[part], tempTip);
}


static void
StatusBarCreateNetworkTip(int part)
{
    WCHAR tempTip[512];

    _swprintf(tempTip, win_language_get_string_from_id(IDS_2069));

    if (sbTips[part] != NULL)
	free(sbTips[part]);
    sbTips[part] = (WCHAR *) malloc((wcslen(tempTip) << 1) + 2);
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


/* Create and set up the Status Bar window. */
static void
StatusBarCreate(HWND hwndParent, int idStatus, HINSTANCE hInst)
{
    RECT rectDialog;
    int dw, dh, i;

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


/****************************************************************
 *			About Window Handling			*
 ****************************************************************/

static BOOL CALLBACK
AboutDialogProcedure(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND h;

    switch (message) {
	case WM_INITDIALOG:
		pause = 1;
		h = GetDlgItem(hdlg, IDC_ABOUT_ICON);
		SendMessage(h, STM_SETIMAGE, (WPARAM)IMAGE_ICON,
			    (LPARAM)LoadIconBig((PCTSTR)100));
		break;

	case WM_COMMAND:
                switch (LOWORD(wParam)) {
			case IDOK:
				EndDialog(hdlg, 0);
				pause = 0;
				return TRUE;

			default:
				break;
		}
		break;
    }

    return(FALSE);
}


static void
AboutDialogCreate(HWND hwnd)
{
    DialogBox(hinstance, (LPCTSTR)DLG_ABOUT, hwnd, AboutDialogProcedure);
}


/****************************************************************
 *			Main Window Handling			*
 ****************************************************************/

#if 0
static void
win_menu_update(void)
{
#if 0
        menu = LoadMenu(hThisInstance, TEXT("MainMenu"));

	smenu = LoadMenu(hThisInstance, TEXT("StatusBarMenu"));
        initmenu();

	SetMenu(hwndMain, menu);

	win_title_update = 1;
#endif
}
#endif


static void
releasemouse(void)
{
    if (mousecapture) {
	ClipCursor(&oldclip);
	ShowCursor(TRUE);
	mousecapture = 0;
    }
}


static void
win_pc_reset(int hard)
{
    pause = 1;

    Sleep(100);

    nvr_save();

    config_save();

    if (hard)
	pc_reset_hard();
      else
	pc_send_cad();

    pause = 0;
}


static void
video_toggle_option(HMENU h, int *val, int id)
{
    startblit();
    video_wait_for_blit();
    *val ^= 1;
    CheckMenuItem(h, id, *val ? MF_CHECKED : MF_UNCHECKED);
    endblit();
    config_save();
    device_force_redraw();
}


/* The main thread runs the actual emulator code. */
static void
MainThread(LPVOID param)
{
    int sb_borders[3];
    DWORD old_time, new_time;
    int frames = 0;
    RECT r;

    drawits = 0;
    old_time = GetTickCount();
    while (! quited) {
	if (updatestatus) {
		if (hwndStatus != NULL)
			SendMessage(hwndStatus, WM_USER, 0, 0);
		updatestatus = 0;
	}

	new_time = GetTickCount();
	drawits += new_time - old_time;
	old_time = new_time;
	if (drawits > 0 && !pause) {
		start_time = timer_read();
		drawits -= 10;
		if (drawits > 50) drawits = 0;
		pc_run();

		if (++frames >= 200 && nvr_dosave) {
			frames = 0;
			nvr_save();
			nvr_dosave = 0;
		}

		end_time = timer_read();
		main_time += end_time - start_time;
	} else
		Sleep(1);

	if (!video_fullscreen && win_doresize && (winsizex>0) && (winsizey>0)) {
		SendMessage(hwndSBAR, SB_GETBORDERS, 0, (LPARAM) sb_borders);
		GetWindowRect(hwndMain, &r);
		MoveWindow(hwndRender, 0, 0, winsizex, winsizey, TRUE);
		GetWindowRect(hwndRender, &r);
		MoveWindow(hwndSBAR,
			   0, r.bottom + GetSystemMetrics(SM_CYEDGE),
			   winsizex, 17, TRUE);
		GetWindowRect(hwndMain, &r);

		MoveWindow(hwndMain, r.left, r.top,
			   winsizex + (GetSystemMetrics(vid_resize ? SM_CXSIZEFRAME : SM_CXFIXEDFRAME) * 2),
			   winsizey + (GetSystemMetrics(SM_CYEDGE) * 2) + (GetSystemMetrics(vid_resize ? SM_CYSIZEFRAME : SM_CYFIXEDFRAME) * 2) + GetSystemMetrics(SM_CYMENUSIZE) + GetSystemMetrics(SM_CYCAPTION) + 17 + sb_borders[1] + 1,
			   TRUE);

		if (mousecapture) {
			GetWindowRect(hwndRender, &r);
			ClipCursor(&r);
		}

		win_doresize = 0;
	}

	if (leave_fullscreen_flag) {
		leave_fullscreen_flag = 0;

		SendMessage(hwndMain, WM_LEAVEFULLSCREEN, 0, 0);
	}

	if (video_fullscreen && infocus)
		SetCursorPos(9999, 9999);
    }
}


static void
ResetAllMenus(void)
{
#ifdef ENABLE_LOG_TOGGLES
# ifdef ENABLE_BUSLOGIC_LOG
    CheckMenuItem(menuMain, IDM_LOG_BUSLOGIC, MF_UNCHECKED);
# endif
# ifdef ENABLE_CDROM_LOG
    CheckMenuItem(menuMain, IDM_LOG_CDROM, MF_UNCHECKED);
# endif
# ifdef ENABLE_D86F_LOG
    CheckMenuItem(menuMain, IDM_LOG_D86F, MF_UNCHECKED);
# endif
# ifdef ENABLE_FDC_LOG
    CheckMenuItem(menuMain, IDM_LOG_FDC, MF_UNCHECKED);
# endif
# ifdef ENABLE_IDE_LOG
    CheckMenuItem(menuMain, IDM_LOG_IDE, MF_UNCHECKED);
# endif
# ifdef ENABLE_SERIAL_LOG
    CheckMenuItem(menuMain, IDM_LOG_SERIAL, MF_UNCHECKED);
# endif
# ifdef ENABLE_NIC_LOG
    /*FIXME: should be network_setlog(1:0) */
    CheckMenuItem(menuMain, IDM_LOG_NIC, MF_UNCHECKED);
# endif
#endif

    CheckMenuItem(menuMain, IDM_VID_FORCE43, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_OVERSCAN, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_INVERT, MF_UNCHECKED);

    CheckMenuItem(menuMain, IDM_VID_RESIZE, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_DDRAW+0, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_DDRAW+1, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_FS_FULL+0, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_FS_FULL+1, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_FS_FULL+2, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_FS_FULL+3, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_REMEMBER, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_SCALE_1X+0, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_SCALE_1X+1, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_SCALE_1X+2, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_SCALE_1X+3, MF_UNCHECKED);

    CheckMenuItem(menuMain, IDM_VID_CGACON, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAYCT_601+0, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAYCT_601+1, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAYCT_601+2, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAY_RGB+0, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAY_RGB+1, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAY_RGB+2, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAY_RGB+3, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAY_RGB+4, MF_UNCHECKED);

#ifdef ENABLE_LOG_TOGGLES
# ifdef ENABLE_BUSLOGIC_LOG
    CheckMenuItem(menuMain, IDM_LOG_BUSLOGIC, buslogic_do_log?MF_CHECKED:MF_UNCHECKED);
# endif
# ifdef ENABLE_CDROM_LOG
    CheckMenuItem(menuMain, IDM_LOG_CDROM, cdrom_do_log?MF_CHECKED:MF_UNCHECKED);
# endif
# ifdef ENABLE_D86F_LOG
    CheckMenuItem(menuMain, IDM_LOG_D86F, d86f_do_log?MF_CHECKED:MF_UNCHECKED);
# endif
# ifdef ENABLE_FDC_LOG
    CheckMenuItem(menuMain, IDM_LOG_FDC, fdc_do_log?MF_CHECKED:MF_UNCHECKED);
# endif
# ifdef ENABLE_IDE_LOG
    CheckMenuItem(menuMain, IDM_LOG_IDE, ide_do_log?MF_CHECKED:MF_UNCHECKED);
# endif
# ifdef ENABLE_SERIAL_LOG
    CheckMenuItem(menuMain, IDM_LOG_SERIAL, serial_do_log?MF_CHECKED:MF_UNCHECKED);
# endif
# ifdef ENABLE_NIC_LOG
    /*FIXME: should be network_setlog(1:0) */
    CheckMenuItem(menuMain, IDM_LOG_NIC, nic_do_log?MF_CHECKED:MF_UNCHECKED);
# endif
#endif

    CheckMenuItem(menuMain, IDM_VID_FORCE43, force_43?MF_CHECKED:MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_OVERSCAN, enable_overscan?MF_CHECKED:MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_INVERT, invert_display ? MF_CHECKED : MF_UNCHECKED);

    if (vid_resize)
	CheckMenuItem(menuMain, IDM_VID_RESIZE, MF_CHECKED);
    CheckMenuItem(menuMain, IDM_VID_DDRAW + vid_api, MF_CHECKED);
    CheckMenuItem(menuMain, IDM_VID_FS_FULL + video_fullscreen_scale, MF_CHECKED);
    CheckMenuItem(menuMain, IDM_VID_REMEMBER, window_remember?MF_CHECKED:MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_SCALE_1X + scale, MF_CHECKED);

    CheckMenuItem(menuMain, IDM_VID_CGACON, vid_cga_contrast?MF_CHECKED:MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAYCT_601 + video_graytype, MF_CHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAY_RGB + video_grayscale, MF_CHECKED);
}


static LRESULT CALLBACK
LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    BOOL bControlKeyDown;
    KBDLLHOOKSTRUCT* p;

    if (nCode < 0 || nCode != HC_ACTION)
	return(CallNextHookEx(hKeyboardHook, nCode, wParam, lParam));
	
    p = (KBDLLHOOKSTRUCT*)lParam;

    /* disable alt-tab */
    if (p->vkCode == VK_TAB && p->flags & LLKHF_ALTDOWN) return(1);

    /* disable alt-space */
    if (p->vkCode == VK_SPACE && p->flags & LLKHF_ALTDOWN) return(1);

    /* disable alt-escape */
    if (p->vkCode == VK_ESCAPE && p->flags & LLKHF_ALTDOWN) return(1);

    /* disable windows keys */
    if((p->vkCode == VK_LWIN) || (p->vkCode == VK_RWIN)) return(1);

    /* checks ctrl key pressed */
    bControlKeyDown = GetAsyncKeyState(VK_CONTROL)>>((sizeof(SHORT)*8)-1);

    /* disable ctrl-escape */
    if (p->vkCode == VK_ESCAPE && bControlKeyDown) return(1);

    return(CallNextHookEx(hKeyboardHook, nCode, wParam, lParam));
}


static LRESULT CALLBACK
MainWindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static wchar_t wOldTitle[512];
    HMENU hmenu;
    RECT rect;
    int i = 0;

    switch (message) {
	case WM_CREATE:
		SetTimer(hwnd, TIMER_1SEC, 1000, NULL);
		hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL,
						 LowLevelKeyboardProc,
						 GetModuleHandle(NULL), 0);
		hook_enabled = 1;
		break;

	case WM_COMMAND:
		hmenu = GetMenu(hwnd);
		switch (LOWORD(wParam)) {
			case IDM_ACTION_SCREENSHOT:
				take_screenshot();
				break;

			case IDM_ACTION_HRESET:
				win_pc_reset(1);
				break;

			case IDM_ACTION_RESET_CAD:
				win_pc_reset(0);
				break;

			case IDM_ACTION_EXIT:
				PostQuitMessage(0);
				break;

			case IDM_ACTION_CTRL_ALT_ESC:
				pc_send_cae();
				break;

			case IDM_ACTION_PAUSE:
				pause ^= 1;
				if (pause) {
					wcscpy(wOldTitle, wTitle);
					wcscat(wTitle, L" - PAUSED -");

					set_window_title(NULL);
				} else
					set_window_title(wOldTitle);
				break;

			case IDM_CONFIG:
				win_settings_open(hwnd);
				break;

			case IDM_ABOUT:
				AboutDialogCreate(hwnd);
				break;

			case IDM_STATUS:
				StatusWindowCreate(hwnd);
				break;

			case IDM_VID_RESIZE:
				vid_resize = !vid_resize;
				CheckMenuItem(hmenu, IDM_VID_RESIZE, (vid_resize)? MF_CHECKED : MF_UNCHECKED);
				if (vid_resize)
					SetWindowLongPtr(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX) | WS_VISIBLE);
				  else
					SetWindowLongPtr(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW & ~WS_SIZEBOX & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX) | WS_VISIBLE);
				GetWindowRect(hwnd, &rect);
				SetWindowPos(hwnd, 0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_FRAMECHANGED);
				GetWindowRect(hwndSBAR,&rect);
				SetWindowPos(hwndSBAR, 0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_FRAMECHANGED);
				if (vid_resize) {
					CheckMenuItem(hmenu, IDM_VID_SCALE_1X + scale, MF_UNCHECKED);
					CheckMenuItem(hmenu, IDM_VID_SCALE_2X, MF_CHECKED);
					scale = 1;
				}
				EnableMenuItem(hmenu, IDM_VID_SCALE_1X, vid_resize ? MF_GRAYED : MF_ENABLED);
				EnableMenuItem(hmenu, IDM_VID_SCALE_2X, vid_resize ? MF_GRAYED : MF_ENABLED);
				EnableMenuItem(hmenu, IDM_VID_SCALE_3X, vid_resize ? MF_GRAYED : MF_ENABLED);
				EnableMenuItem(hmenu, IDM_VID_SCALE_4X, vid_resize ? MF_GRAYED : MF_ENABLED);
				win_doresize = 1;
				config_save();
				break;

			case IDM_VID_REMEMBER:
				window_remember = !window_remember;
				CheckMenuItem(hmenu, IDM_VID_REMEMBER, window_remember ? MF_CHECKED : MF_UNCHECKED);
				GetWindowRect(hwnd, &rect);
				if (window_remember) {
					window_x = rect.left;
					window_y = rect.top;
					window_w = rect.right - rect.left;
					window_h = rect.bottom - rect.top;
				}
				config_save();
				break;

			case IDM_VID_DDRAW:
			case IDM_VID_D3D:
				startblit();
				video_wait_for_blit();
				CheckMenuItem(hmenu, IDM_VID_DDRAW + vid_api, MF_UNCHECKED);
				vid_apis[0][vid_api].close();
				vid_api = LOWORD(wParam) - IDM_VID_DDRAW;
				CheckMenuItem(hmenu, IDM_VID_DDRAW + vid_api, MF_CHECKED);
				vid_apis[0][vid_api].init(hwndRender);
				endblit();
				config_save();
				device_force_redraw();
				cgapal_rebuild();
				break;

			case IDM_VID_FULLSCREEN:
				if (video_fullscreen == 1) break;

				if (video_fullscreen_first) {
					video_fullscreen_first = 0;
					msgbox_info(hwndMain, IDS_2074);
				}

				startblit();
				video_wait_for_blit();
				mouse_close();
				vid_apis[0][vid_api].close();
				video_fullscreen = 1;
				vid_apis[1][vid_api].init(hwndMain);
				mouse_init();
				leave_fullscreen_flag = 0;
				endblit();
				config_save();
				device_force_redraw();
				cgapal_rebuild();
				break;

			case IDM_VID_FS_FULL:
			case IDM_VID_FS_43:
			case IDM_VID_FS_SQ:                                
			case IDM_VID_FS_INT:
				CheckMenuItem(hmenu, IDM_VID_FS_FULL + video_fullscreen_scale, MF_UNCHECKED);
				video_fullscreen_scale = LOWORD(wParam) - IDM_VID_FS_FULL;
				CheckMenuItem(hmenu, IDM_VID_FS_FULL + video_fullscreen_scale, MF_CHECKED);
				config_save();
				device_force_redraw();
				break;

			case IDM_VID_SCALE_1X:
			case IDM_VID_SCALE_2X:
			case IDM_VID_SCALE_3X:
			case IDM_VID_SCALE_4X:
				CheckMenuItem(hmenu, IDM_VID_SCALE_1X + scale, MF_UNCHECKED);
				scale = LOWORD(wParam) - IDM_VID_SCALE_1X;
				CheckMenuItem(hmenu, IDM_VID_SCALE_1X + scale, MF_CHECKED);
				config_save();
				device_force_redraw();
				break;

			case IDM_VID_FORCE43:
				video_toggle_option(hmenu, &force_43, IDM_VID_FORCE43);
				break;

			case IDM_VID_INVERT:
				video_toggle_option(hmenu, &invert_display, IDM_VID_INVERT);
				break;

			case IDM_VID_OVERSCAN:
				update_overscan = 1;
				video_toggle_option(hmenu, &enable_overscan, IDM_VID_OVERSCAN);
				break;

			case IDM_VID_CGACON:
				vid_cga_contrast ^= 1;
				CheckMenuItem(hmenu, IDM_VID_CGACON, vid_cga_contrast ? MF_CHECKED : MF_UNCHECKED);
				cgapal_rebuild();
				config_save();
				break;

			case IDM_VID_GRAYCT_601:
			case IDM_VID_GRAYCT_709:
			case IDM_VID_GRAYCT_AVE:
				CheckMenuItem(hmenu, IDM_VID_GRAYCT_601 + video_graytype, MF_UNCHECKED);
				video_graytype = LOWORD(wParam) - IDM_VID_GRAYCT_601;
				CheckMenuItem(hmenu, IDM_VID_GRAYCT_601 + video_graytype, MF_CHECKED);
				config_save();
				device_force_redraw();
				break;

			case IDM_VID_GRAY_RGB:
			case IDM_VID_GRAY_MONO:
			case IDM_VID_GRAY_AMBER:
			case IDM_VID_GRAY_GREEN:
			case IDM_VID_GRAY_WHITE:
				CheckMenuItem(hmenu, IDM_VID_GRAY_RGB + video_grayscale, MF_UNCHECKED);
				video_grayscale = LOWORD(wParam) - IDM_VID_GRAY_RGB;
				CheckMenuItem(hmenu, IDM_VID_GRAY_RGB + video_grayscale, MF_CHECKED);
				config_save();
				device_force_redraw();
				break;

#ifdef ENABLE_LOG_TOGGLES
# ifdef ENABLE_BUSLOGIC_LOG
			case IDM_LOG_BUSLOGIC:
				buslogic_do_log ^= 1;
				CheckMenuItem(hmenu, IDM_LOG_BUSLOGIC, buslogic_do_log ? MF_CHECKED : MF_UNCHECKED);
				break;
# endif

# ifdef ENABLE_CDROM_LOG
			case IDM_LOG_CDROM:
				cdrom_do_log ^= 1;
				CheckMenuItem(hmenu, IDM_LOG_CDROM, cdrom_do_log ? MF_CHECKED : MF_UNCHECKED);
				break;
# endif

# ifdef ENABLE_D86F_LOG
			case IDM_LOG_D86F:
				d86f_do_log ^= 1;
				CheckMenuItem(hmenu, IDM_LOG_D86F, d86f_do_log ? MF_CHECKED : MF_UNCHECKED);
				break;
# endif

# ifdef ENABLE_FDC_LOG
			case IDM_LOG_FDC:
				fdc_do_log ^= 1;
				CheckMenuItem(hmenu, IDM_LOG_FDC, fdc_do_log ? MF_CHECKED : MF_UNCHECKED);
				break;
# endif

# ifdef ENABLE_IDE_LOG
			case IDM_LOG_IDE:
				ide_do_log ^= 1;
				CheckMenuItem(hmenu, IDM_LOG_IDE, ide_do_log ? MF_CHECKED : MF_UNCHECKED);
				break;
# endif

# ifdef ENABLE_SERIAL_LOG
			case IDM_LOG_SERIAL:
				serial_do_log ^= 1;
				CheckMenuItem(hmenu, IDM_LOG_SERIAL, serial_do_log ? MF_CHECKED : MF_UNCHECKED);
				break;
# endif

# ifdef ENABLE_NIC_LOG
			case IDM_LOG_NIC:
				/*FIXME: should be network_setlog() */
				nic_do_log ^= 1;
				CheckMenuItem(hmenu, IDM_LOG_NIC, nic_do_log ? MF_CHECKED : MF_UNCHECKED);
				break;
# endif
#endif

#ifdef ENABLE_LOG_BREAKPOINT
			case IDM_LOG_BREAKPOINT:
				pclog("---- LOG BREAKPOINT ----\n");
				break;
#endif

#ifdef ENABLE_VRAM_DUMP
			case IDM_DUMP_VRAM:
				svga_dump_vram();
				break;
#endif

			case IDM_CONFIG_LOAD:
				pause = 1;
				if (! file_dlg_st(hwnd, IDS_2160, "", 0)) {
					if (msgbox_reset_yn(hwndMain) == IDYES) {
						config_write(config_file_default);
						for (i = 0; i < FDD_NUM; i++)
						{
							floppy_close(i);
						}
						for (i = 0; i < CDROM_NUM; i++)
						{
							cdrom_drives[i].handler->exit(i);
							if (cdrom_drives[i].host_drive == 200)
							{
								image_close(i);
							}
							else if ((cdrom_drives[i].host_drive >= 'A') && (cdrom_drives[i].host_drive <= 'Z'))
							{
								ioctl_close(i);
							}
							else
							{
								null_close(i);
							}
						}
						pc_reset_hard_close();
						config_load(wopenfilestring);
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

						floppy_load(0, floppyfns[0]);
						floppy_load(1, floppyfns[1]);
						floppy_load(2, floppyfns[2]);
						floppy_load(3, floppyfns[3]);

						mem_resize();
						rom_load_bios(romset);
						network_init();
						ResetAllMenus();
						StatusBarUpdatePanes();
						pc_reset_hard_init();
					}
				}
				pause = 0;
				break;                        

			case IDM_CONFIG_SAVE:
				pause = 1;
				if (! file_dlg_st(hwnd, IDS_2160, "", 1)) {
					config_write(wopenfilestring);
				}
				pause = 0;
				break;                                                
		}
		return(0);

	case WM_INPUT:
		process_raw_input(lParam, infocus);
		break;

	case WM_SETFOCUS:
		infocus = 1;
		if (! hook_enabled) {
			hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL,
							 LowLevelKeyboardProc,
							 GetModuleHandle(NULL),
							 0);
			hook_enabled = 1;
		}
		break;

	case WM_KILLFOCUS:
		infocus = 0;
		releasemouse();
		memset(recv_key, 0, sizeof(recv_key));
		if (video_fullscreen)
			leave_fullscreen_flag = 1;
		if (hook_enabled) {
			UnhookWindowsHookEx(hKeyboardHook);
			hook_enabled = 0;
		}
		break;

	case WM_LBUTTONUP:
		if (!mousecapture && !video_fullscreen) {
			GetClipCursor(&oldclip);
			GetWindowRect(hwndRender, &rect);

			ClipCursor(&rect);
			mousecapture = 1;
			while (1) {
				if (ShowCursor(FALSE) < 0) break;
			}
		}
		break;

	case WM_MBUTTONUP:
		if (!(mouse_get_type(mouse_type) & MOUSE_TYPE_3BUTTON)) {
			releasemouse();
		}
		break;

	case WM_ENTERMENULOOP:
		break;

	case WM_SIZE:
		winsizex = (lParam & 0xFFFF);
		winsizey = (lParam >> 16) - (17 + 6);
		if (winsizey < 0)
			winsizey = 0;

		MoveWindow(hwndSBAR, 0, winsizey + 6, winsizex, 17, TRUE);

		if (hwndRender != NULL) {
			MoveWindow(hwndRender, 0, 0, winsizex, winsizey, TRUE);

			if (vid_apis[video_fullscreen][vid_api].resize) {
				startblit();
				video_wait_for_blit();
				vid_apis[video_fullscreen][vid_api].resize(winsizex, winsizey);
				endblit();
			}

			if (mousecapture) {
				GetWindowRect(hwndRender, &rect);

				ClipCursor(&rect);
			}
		}

		if (window_remember) {
			GetWindowRect(hwnd, &rect);
			window_x = rect.left;
			window_y = rect.top;
			window_w = rect.right - rect.left;
			window_h = rect.bottom - rect.top;
			save_window_pos = 1;
		}

		config_save();
		break;

	case WM_MOVE:
		if (window_remember) {
			GetWindowRect(hwnd, &rect);
			window_x = rect.left;
			window_y = rect.top;
			window_w = rect.right - rect.left;
			window_h = rect.bottom - rect.top;
			save_window_pos = 1;
		}
		break;
                
	case WM_TIMER:
		if (wParam == TIMER_1SEC) {
			onesec();
		}
		break;

	case WM_RESETD3D:
		startblit();
		if (video_fullscreen)
			d3d_fs_reset();
		  else
			d3d_reset();
		endblit();
		break;

	case WM_LEAVEFULLSCREEN:
		startblit();
		mouse_close();
		vid_apis[1][vid_api].close();
		video_fullscreen = 0;
		config_save();
		vid_apis[0][vid_api].init(hwndRender);
		mouse_init();
		endblit();
		device_force_redraw();
		cgapal_rebuild();
		break;

	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYUP:
		return 0;

	case WM_DESTROY:
		UnhookWindowsHookEx(hKeyboardHook);
		KillTimer(hwnd, TIMER_1SEC);
		PostQuitMessage (0);	/* send WM_QUIT to message queue */
		break;

	case WM_SYSCOMMAND:
		/*
		 * Disable ALT key *ALWAYS*,
		 * I don't think there's any use for
		 * reaching the menu that way.
		 */
		if (wParam == SC_KEYMENU && HIWORD(lParam) <= 0) {
			return 0; /*disable ALT key for menu*/
		}

	default:
		return(DefWindowProc(hwnd, message, wParam, lParam));
    }

    return(0);
}


static LRESULT CALLBACK
SubWindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return(DefWindowProc(hwnd, message, wParam, lParam));
}


/* Create a console if we don't already have one. */
static void
CreateConsole(int init)
{
    HANDLE h;
    FILE *fp;
    fpos_t p;
    int i;

    if (! init) {
	FreeConsole();
	return;
    }

    /* Are we logging to a file? */
    p = 0;
    (void)fgetpos(stdout, &p);
    if (p != -1) return;

    /* Not logging to file, attach to console. */
    if (! AttachConsole(ATTACH_PARENT_PROCESS)) {
	/* Parent has no console, create one. */
	AllocConsole();
    }

    h = GetStdHandle(STD_OUTPUT_HANDLE);
    i = _open_osfhandle((long)h, _O_TEXT);
    fp = _fdopen(i, "w");
    setvbuf(fp, NULL, _IONBF, 1);
    *stdout = *fp;

    h = GetStdHandle(STD_ERROR_HANDLE);
    i = _open_osfhandle((long)h, _O_TEXT);
    fp = _fdopen(i, "w");
    setvbuf(fp, NULL, _IONBF, 1);
    *stderr = *fp;

#if 0
    /* Set up stdin as well. */
    h = GetStdHandle(STD_INPUT_HANDLE);
    i = _open_osfhandle((long)h, _O_TEXT);
    fp = _fdopen(i, "r");
    setvbuf(fp, NULL, _IONBF, 128);
    *stdin = *fp;
#endif
}


/* Process the commandline, and create standard argc/argv array. */
static void
ProcessCommandLine(void)
{
    WCHAR *cmdline;
    int argc_max;
    int i, q;

    cmdline = GetCommandLine();
    i = wcslen(cmdline) + 1;
    argbuf = malloc(i * 2);
    memcpy(argbuf, cmdline, i * 2);

    argc = 0;
    argc_max = 64;
    argv = malloc(sizeof(wchar_t *) * argc_max);
    if (argv == NULL) {
	free(argbuf);
	return;
    }

    /* parse commandline into argc/argv format */
    i = 0;
    while (argbuf[i]) {
	while (argbuf[i] == L' ')
		  i++;

	if (argbuf[i]) {
		if ((argbuf[i] == L'\'') || (argbuf[i] == L'"')) {
			q = argbuf[i++];
			if (!argbuf[i])
				break;
		} else
			q = 0;

		argv[argc++] = &argbuf[i];

		if (argc >= argc_max) {
			argc_max += 64;
			argv = realloc(argv, sizeof(wchar_t *) * argc_max);
			if (argv == NULL) {
				free(argbuf);
				return;
			}
		}

		while ((argbuf[i]) && ((q)
			? (argbuf[i]!=q) : (argbuf[i]!=L' '))) i++;

		if (argbuf[i]) {
			argbuf[i] = 0;
			i++;
		}
	}
    }

    argv[argc] = NULL;
}


/* @@@
 * For the Windows platform, this is the start of the application.
 */
int WINAPI
WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpszArg, int nFunsterStil)
{
    WCHAR title[200];
    WNDCLASSEX wincl;			/* buffer for main window's class */
    MSG messages;			/* received-messages buffer */
    HWND hwnd;				/* handle for our window */
    HACCEL haccel;			/* handle to accelerator table */
    int bRet;
    LARGE_INTEGER qpc_freq;

    /* Create console window. */
    CreateConsole(1);

    /* Process the command line for options. */
    ProcessCommandLine();

    /* Pre-initialize the system, this loads the config file. */
    if (! pc_init(argc, argv)) {
	/* Detach from console. */
	CreateConsole(0);
	return(1);
    }

    /* We need this later. */
    hinstance = hInst;

    /* Load common strings from the resource file. */
    win_language_load_common_strings();

    /* Create our main window's class and register it. */
    wincl.hInstance = hInst;
    wincl.lpszClassName = CLASS_NAME;
    wincl.lpfnWndProc = MainWindowProcedure;
    wincl.style = CS_DBLCLKS;		/* Catch double-clicks */
    wincl.cbSize = sizeof(WNDCLASSEX);
    wincl.hIcon = LoadIcon(hInst, (LPCTSTR)100);
    wincl.hIconSm = LoadIcon(hInst, (LPCTSTR)100);
    wincl.hCursor = NULL;
    wincl.lpszMenuName = NULL;
    wincl.cbClsExtra = 0;
    wincl.cbWndExtra = 0;
    wincl.hbrBackground = CreateSolidBrush(RGB(0,0,0));
    if (! RegisterClassEx(&wincl))
			return(2);
    wincl.lpszClassName = SUB_CLASS_NAME;
    wincl.lpfnWndProc = SubWindowProcedure;
    if (! RegisterClassEx(&wincl))
			return(2);

    /* Load the Window Menu(s) from the resources. */
    menuMain = LoadMenu(hInst, MENU_NAME);

    /* Set the initial title for the program's main window. */
    _swprintf(title, L"%s v%s", EMU_NAME_W, EMU_VERSION_W);

    /* Now create our main window. */
    hwnd = CreateWindowEx (
		0,			/* no extended possibilites */
		CLASS_NAME,		/* class name */
		title,			/* Title Text */
#if 0
		(WS_OVERLAPPEDWINDOW & ~WS_SIZEBOX),	/* default window */
#else
		(WS_OVERLAPPEDWINDOW & ~WS_SIZEBOX) | DS_3DLOOK,
#endif
		CW_USEDEFAULT,		/* Windows decides the position */
		CW_USEDEFAULT,		/* where window ends up on the screen */
		640+(GetSystemMetrics(SM_CXFIXEDFRAME)*2),	/* width */
		480+(GetSystemMetrics(SM_CYFIXEDFRAME)*2)+GetSystemMetrics(SM_CYMENUSIZE)+GetSystemMetrics(SM_CYCAPTION)+1,	/* and height in pixels */
		HWND_DESKTOP,		/* window is a child to desktop */
		menuMain,		/* menu */
		hInst,			/* Program Instance handler */
		NULL);			/* no Window Creation data */
    hwndMain = hwnd;

    /* Resize the window if needed. */
    if (vid_resize)
	SetWindowLongPtr(hwnd, GWL_STYLE,
			(WS_OVERLAPPEDWINDOW&~WS_MINIMIZEBOX));
      else
	SetWindowLongPtr(hwnd, GWL_STYLE,
			(WS_OVERLAPPEDWINDOW&~WS_SIZEBOX&~WS_THICKFRAME&~WS_MAXIMIZEBOX&~WS_MINIMIZEBOX));

    if (window_remember)
	MoveWindow(hwnd, window_x, window_y, window_w, window_h, TRUE);

    /* Reset all menus to their defaults. */
    ResetAllMenus();

    /* Make the window visible on the screen. */
    ShowWindow(hwnd, nFunsterStil);

    /* Load the accelerator table */
    haccel = LoadAccelerators(hInst, ACCEL_NAME);
    if (haccel == NULL) {
	MessageBox(hwndMain,
		   win_language_get_string_from_id(IDS_2053),
		   win_language_get_string_from_id(IDS_2050),
		   MB_OK | MB_ICONERROR);
	return(3);
    }

    /* Initialize the input (keyboard, mouse, game) module. */
    memset(recv_key, 0x00, sizeof(recv_key));
    device.usUsagePage = 0x01;
    device.usUsage = 0x06;
    device.dwFlags = RIDEV_NOHOTKEYS;
    device.hwndTarget = hwnd;
    if (! RegisterRawInputDevices(&device, 1, sizeof(device))) {
	MessageBox(hwndMain,
		   win_language_get_string_from_id(IDS_2054),
		   win_language_get_string_from_id(IDS_2050),
		   MB_OK | MB_ICONERROR);
	return(4);
    }
    get_registry_key_map();

    /* Create the status bar window. */
    StatusBarCreate(hwndMain, IDC_STATUS, hInst);

    /*
     * Before we can create the Render window, we first have
     * to prepare some other things that it depends on.
     */
    ghMutex = CreateMutex(NULL, FALSE, L"86Box.BlitMutex");

    /* Create the Machine Rendering window. */
    hwndRender = CreateWindow(L"STATIC", NULL, WS_VISIBLE|WS_CHILD|SS_BITMAP,
			      0, 0, 1, 1, hwnd, NULL, hInst, NULL);
    MoveWindow(hwndRender, 0, 0, winsizex, winsizey, TRUE);

    /* Select the best system renderer available. */
    if (! vid_apis[0][vid_api].init(hwndRender)) {
	vid_api ^= 1;
	if (! vid_apis[0][vid_api].init(hwndRender)) {
		MessageBox(hwnd,
			   win_language_get_string_from_id(IDS_2095),
			   win_language_get_string_from_id(IDS_2050),
			   MB_OK | MB_ICONERROR);
		return(5);
	}
    }

    /* Initialize the rendering window, or fullscreen. */
    if (start_in_fullscreen) {
	startblit();
	vid_apis[0][vid_api].close();
	video_fullscreen = 1;
	vid_apis[1][vid_api].init(hwndRender);
	leave_fullscreen_flag = 0;
	endblit();
	device_force_redraw();
    }
    if (vid_apis[video_fullscreen][vid_api].resize) {
	startblit();
	vid_apis[video_fullscreen][vid_api].resize(winsizex, winsizey);
	endblit();
    }

    /* Move this to SLiRP. */
    slirpMutex = CreateMutex(NULL, FALSE, L"86Box.SlirpMutex");

    /* All done, fire up the actual emulated machine. */
    if (! pc_init_modules()) {
	/* Dang, no ROMs found at all! */
	MessageBox(hwnd,
		   win_language_get_string_from_id(IDS_2056),
		   win_language_get_string_from_id(IDS_2050),
		   MB_OK | MB_ICONERROR);
	return(6);
    }

    /* Fire up the machine. */
    pc_reset_hard();

    /*
     * Everything has been configured, and all seems to work,
     * so now it is time to start the main thread to do some
     * real work, and we will hang in here, dealing with the
     * UI until we're done.
     */
    timeBeginPeriod(1);

    atexit(releasemouse);

    thMain = (HANDLE)_beginthread(MainThread, 0, NULL);
    SetThreadPriority(thMain, THREAD_PRIORITY_HIGHEST);

    QueryPerformanceFrequency(&qpc_freq);
    timer_freq = qpc_freq.QuadPart;

    /* Run the message loop. It will run until GetMessage() returns 0 */
    while (! quited) {
	bRet = GetMessage(&messages, NULL, 0, 0);
	if ((bRet == 0) || quited) break;

	if (bRet == -1) {
		fatal("bRet is -1\n");
	}

	if (messages.message == WM_QUIT) {
		quited = 1;
		break;
	}

	if (! TranslateAccelerator(hwnd, haccel, &messages)) {
                TranslateMessage(&messages);
                DispatchMessage(&messages);
	}

	if (recv_key[0x58] && recv_key[0x42] && mousecapture) {
		ClipCursor(&oldclip);
		ShowCursor(TRUE);
		mousecapture = 0;
        }

         if ((recv_key[0x1D] || recv_key[0x9D]) &&
	     (recv_key[0x38] || recv_key[0xB8]) &&
	     (recv_key[0x51] || recv_key[0xD1]) && video_fullscreen) {
		leave_fullscreen();
	}
    }

    /* Why start??? --FvK */
    startblit();

    Sleep(200);
    TerminateThread(thMain, 0);

    nvr_save();

    config_save();

    pc_close();

    vid_apis[video_fullscreen][vid_api].close();
        
    timeEndPeriod(1);

    if (mousecapture) {
	ClipCursor(&oldclip);
	ShowCursor(TRUE);
    }
        
    UnregisterClass(SUB_CLASS_NAME, hInst);
    UnregisterClass(CLASS_NAME, hInst);

    return(messages.wParam);
}


void
get_executable_name(wchar_t *s, int size)
{
    GetModuleFileName(hinstance, s, size);
}


void
set_window_title(wchar_t *s)
{
    if (! video_fullscreen) {
	if (s != NULL)
		wcscpy(wTitle, s);
	  else
		s = wTitle;

       	SetWindowText(hwndMain, s);
    }
}


uint64_t
timer_read(void)
{
    LARGE_INTEGER li;

    QueryPerformanceCounter(&li);

    return(li.QuadPart);
}


uint32_t
get_ticks(void)
{
    return(GetTickCount());
}


void
delay_ms(uint32_t count)
{
    Sleep(count);
}


void
startblit(void)
{
    WaitForSingleObject(ghMutex, INFINITE);
}


void
endblit(void)
{
    ReleaseMutex(ghMutex);
}


void
startslirp(void)
{
    WaitForSingleObject(slirpMutex, INFINITE);
}


void
endslirp(void)
{
    ReleaseMutex(slirpMutex);
}


void
updatewindowsize(int x, int y)
{
    int owsx = winsizex;
    int owsy = winsizey;
    int temp_overscan_x = overscan_x;
    int temp_overscan_y = overscan_y;
    double dx, dy, dtx, dty;

    if (vid_resize) return;

    if (x < 160)  x = 160;
    if (y < 100)  y = 100;
    if (x > 2048)  x = 2048;
    if (y > 2048)  y = 2048;

    if (suppress_overscan)
	temp_overscan_x = temp_overscan_y = 0;

    unscaled_size_x=x; efwinsizey=y;

    if (force_43) {
	dx = (double) x;
	dtx = (double) temp_overscan_x;

	dy = (double) y;
	dty = (double) temp_overscan_y;

	/* Account for possible overscan. */
	if (temp_overscan_y == 16) {
		/* CGA */
		dy = (((dx - dtx) / 4.0) * 3.0) + dty;
	} else if (temp_overscan_y < 16) {
		/* MDA/Hercules */
		dy = (x / 4.0) * 3.0;
	} else {
		if (enable_overscan) {
			/* EGA/(S)VGA with overscan */
			dy = (((dx - dtx) / 4.0) * 3.0) + dty;
		} else {
			/* EGA/(S)VGA without overscan */
			dy = (x / 4.0) * 3.0;
		}
	}
	unscaled_size_y = (int) dy;
    } else {
	unscaled_size_y = efwinsizey;
    }

    switch(scale) {
	case 0:
		winsizex = unscaled_size_x >> 1;
		winsizey = unscaled_size_y >> 1;
		break;

	case 1:
		winsizex = unscaled_size_x;
		winsizey = unscaled_size_y;
		break;

	case 2:
		winsizex = (unscaled_size_x * 3) >> 1;
		winsizey = (unscaled_size_y * 3) >> 1;
		break;

	case 3:
		winsizex = unscaled_size_x << 1;
		winsizey = unscaled_size_y << 1;
		break;
    }

    if ((owsx != winsizex) || (owsy != winsizey))
	win_doresize = 1;
      else
	win_doresize = 0;
}


void
uws_natural(void)
{
    updatewindowsize(unscaled_size_x, efwinsizey);
}


void
leave_fullscreen(void)
{
    leave_fullscreen_flag = 1;
}


/*
 * IODEV stuff, clean up!
 */
void
cdrom_init_host_drives(void)
{
    WCHAR s[64];
    int i = 0;

    host_cdrom_drive_available_num = 0;
    for (i='A'; i<='Z'; i++) {
	_swprintf(s, L"%c:\\", i);

	if (GetDriveType(s)==DRIVE_CDROM) {
		host_cdrom_drive_available[i - 'A'] = 1;

		host_cdrom_drive_available_num++;
	} else {
		host_cdrom_drive_available[i - 'A'] = 0;
	}
    }
}


void
cdrom_close(uint8_t id)
{
    switch (cdrom_drives[id].host_drive) {
	case 0:
		null_close(id);
		break;

	case 200:
		image_close(id);
		break;

	default:
		ioctl_close(id);
		break;
    }
}


void
cdrom_eject(uint8_t id)
{
    int part;

    part = StatusBarFindPart(SB_CDROM | id);
    if ((part == -1) || (sb_menu_handles == NULL))
	return;

    if (cdrom_drives[id].host_drive == 0) {
	/* Switch from empty to empty. Do nothing. */
	return;
    }

    if ((cdrom_drives[id].host_drive >= 'A') &&
	(cdrom_drives[id].host_drive <= 'Z')) {
	CheckMenuItem(sb_menu_handles[part], IDM_CDROM_HOST_DRIVE | id | ((cdrom_drives[id].host_drive - 'A') << 3), MF_UNCHECKED);
	}
	if (cdrom_drives[id].host_drive == 200) {
		wcscpy(cdrom_image[id].prev_image_path, cdrom_image[id].image_path);
	}
	cdrom_drives[id].prev_host_drive = cdrom_drives[id].host_drive;
	cdrom_drives[id].handler->exit(id);
	cdrom_close(id);
	cdrom_null_open(id, 0);
	if (cdrom_drives[id].bus_type) {
		/* Signal disc change to the emulated machine. */
		cdrom_insert(id);
	}
	CheckMenuItem(sb_menu_handles[part], IDM_CDROM_IMAGE | id, MF_UNCHECKED);
	cdrom_drives[id].host_drive=0;
	CheckMenuItem(sb_menu_handles[part], IDM_CDROM_EMPTY | id, MF_CHECKED);
	StatusBarUpdateIconState(SB_CDROM | id, 1);
	EnableMenuItem(sb_menu_handles[part], IDM_CDROM_RELOAD | id, MF_BYCOMMAND | MF_ENABLED);
	StatusBarUpdateTip(SB_CDROM | id);

	config_save();
}


void
cdrom_reload(uint8_t id)
{
    int new_cdrom_drive;
    int part;

    part = StatusBarFindPart(SB_CDROM | id);
    if ((part == -1) || (sb_menu_handles == NULL)) return;

    if ((cdrom_drives[id].host_drive == cdrom_drives[id].prev_host_drive) || (cdrom_drives[id].prev_host_drive == 0) || (cdrom_drives[id].host_drive != 0)) {
	/* Switch from empty to empty. Do nothing. */
	return;
    }

    cdrom_close(id);
    if (cdrom_drives[id].prev_host_drive == 200) {
	wcscpy(cdrom_image[id].image_path, cdrom_image[id].prev_image_path);
	image_open(id, cdrom_image[id].image_path);
	if (cdrom_drives[id].bus_type) {
		/* Signal disc change to the emulated machine. */
		cdrom_insert(id);
	}
	if (wcslen(cdrom_image[id].image_path) == 0) {
		CheckMenuItem(sb_menu_handles[part], IDM_CDROM_EMPTY | id, MF_CHECKED);
		cdrom_drives[id].host_drive = 0;
		CheckMenuItem(sb_menu_handles[part], IDM_CDROM_IMAGE | id, MF_UNCHECKED);
		StatusBarUpdateIconState(SB_CDROM | id, 1);
	} else {
		CheckMenuItem(sb_menu_handles[part], IDM_CDROM_EMPTY | id, MF_UNCHECKED);
		cdrom_drives[id].host_drive = 200;
		CheckMenuItem(sb_menu_handles[part], IDM_CDROM_IMAGE | id, MF_CHECKED);
		StatusBarUpdateIconState(SB_CDROM | id, 0);
	}
    } else {
	new_cdrom_drive = cdrom_drives[id].prev_host_drive;
	ioctl_open(id, new_cdrom_drive);
	if (cdrom_drives[id].bus_type) {
		/* Signal disc change to the emulated machine. */
		cdrom_insert(id);
	}
	CheckMenuItem(sb_menu_handles[part], IDM_CDROM_EMPTY | id, MF_UNCHECKED);
	cdrom_drives[id].host_drive = new_cdrom_drive;
	CheckMenuItem(sb_menu_handles[part], IDM_CDROM_HOST_DRIVE | id | ((cdrom_drives[id].host_drive - 'A') << 3), MF_CHECKED);
	StatusBarUpdateIconState(SB_CDROM | id, 0);
    }

    EnableMenuItem(sb_menu_handles[part], IDM_CDROM_RELOAD | id, MF_BYCOMMAND | MF_GRAYED);
    StatusBarUpdateTip(SB_CDROM | id);

    config_save();
}


void
removable_disk_unload(uint8_t id)
{
    if (wcslen(hdd[id].fn) == 0) {
	/* Switch from empty to empty. Do nothing. */
	return;
    }

    scsi_unloadhd(hdd[id].scsi_id, hdd[id].scsi_lun, id);
    scsi_disk_insert(id);
}


void
removable_disk_eject(uint8_t id)
{
    int part = 0;

    part = StatusBarFindPart(SB_CDROM | id);
    if ((part == -1) || (sb_menu_handles == NULL)) return;

    removable_disk_unload(id);
    StatusBarUpdateIconState(SB_RDISK | id, 1);
    EnableMenuItem(sb_menu_handles[part], IDM_RDISK_EJECT | id, MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(sb_menu_handles[part], IDM_RDISK_RELOAD | id, MF_BYCOMMAND | MF_ENABLED);
    EnableMenuItem(sb_menu_handles[part], IDM_RDISK_SEND_CHANGE | id, MF_BYCOMMAND | MF_GRAYED);

    StatusBarUpdateTip(SB_RDISK | id);

    config_save();
}


void
removable_disk_reload(uint8_t id)
{
    int part = 0;

    part = StatusBarFindPart(SB_CDROM | id);
    if ((part == -1) || (sb_menu_handles == NULL)) return;

    if (wcslen(hdd[id].fn) != 0) {
	/* Attempting to reload while an image is already loaded. Do nothing. */
	return;
    }

    scsi_reloadhd(id);
#if 0
    scsi_disk_insert(id);
#endif

    StatusBarUpdateIconState(SB_RDISK | id, wcslen(hdd[id].fn) ? 0 : 1);

    EnableMenuItem(sb_menu_handles[part], IDM_RDISK_EJECT | id, MF_BYCOMMAND | (wcslen(hdd[id].fn) ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(sb_menu_handles[part], IDM_RDISK_RELOAD | id, MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(sb_menu_handles[part], IDM_RDISK_SEND_CHANGE | id, MF_BYCOMMAND | (wcslen(hdd[id].fn) ? MF_ENABLED : MF_GRAYED));

    StatusBarUpdateTip(SB_RDISK | id);

    config_save();
}
