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
#include <86box/cassette.h>
#include <86box/cartridge.h>
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


static LONG_PTR	OriginalProcedure;
static WCHAR	**sbTips;
static int	*iStatusWidths;
static int	*sb_part_meanings;
static uint8_t	*sb_part_icons;
static int	sb_parts = 0;
static int	sb_ready = 0;
static uint8_t	sb_map[256];
static int  dpi = 96;
static int  icon_width = 24;
static wchar_t	sb_text[512] = L"\0";
static wchar_t	sb_bugtext[512] = L"\0";

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







static void
StatusBarCreateCassetteTip(int part)
{
    WCHAR tempTip[512];
    WCHAR fn[512];

    if (strlen(cassette_fname) == 0)
	_swprintf(tempTip, plat_get_string(IDS_2148), plat_get_string(IDS_2057));
    else {
	mbstoc16s(fn, cassette_fname, sizeof_w(fn));
	_swprintf(tempTip, plat_get_string(IDS_2148), fn);
    }

    if (sbTips[part] != NULL) {
	free(sbTips[part]);
	sbTips[part] = NULL;
    }
    sbTips[part] = (WCHAR *)malloc((wcslen(tempTip) << 1) + 2);
    wcscpy(sbTips[part], tempTip);
}


static void
StatusBarCreateCartridgeTip(int part)
{
    WCHAR tempTip[512];
    WCHAR fn[512];
    int drive = sb_part_meanings[part] & 0xf;

    if (strlen(cart_fns[drive]) == 0) {
	_swprintf(tempTip, plat_get_string(IDS_2150),
		  drive+1, plat_get_string(IDS_2057));
    } else {
	mbstoc16s(fn, cart_fns[drive], sizeof_w(fn));
	_swprintf(tempTip, plat_get_string(IDS_2150),
		  drive+1, fn);
    }

    if (sbTips[part] != NULL) {
	free(sbTips[part]);
	sbTips[part] = NULL;
    }
    sbTips[part] = (WCHAR *)malloc((wcslen(tempTip) << 1) + 2);
    wcscpy(sbTips[part], tempTip);
}


static void
StatusBarCreateFloppyTip(int part)
{
    WCHAR wtext[512];
    WCHAR tempTip[512];
    WCHAR fn[512];

    int drive = sb_part_meanings[part] & 0xf;

    mbstoc16s(wtext, fdd_getname(fdd_get_type(drive)),
	     strlen(fdd_getname(fdd_get_type(drive))) + 1);
    if (strlen(floppyfns[drive]) == 0) {
	_swprintf(tempTip, plat_get_string(IDS_2108),
		  drive+1, wtext, plat_get_string(IDS_2057));
    } else {
	mbstoc16s(fn, floppyfns[drive], sizeof_w(fn));
	_swprintf(tempTip, plat_get_string(IDS_2108),
		  drive+1, wtext, fn);
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
    WCHAR fn[512];
    int id;
    int drive = sb_part_meanings[part] & 0xf;
    int bus = cdrom[drive].bus_type;

    id = IDS_5377 + (bus - 1);
    szText = plat_get_string(id);

    if (cdrom[drive].host_drive == 200) {
	if (strlen(cdrom[drive].image_path) == 0) {
		_swprintf(tempTip, plat_get_string(IDS_5120),
			  drive+1, szText, plat_get_string(IDS_2057));
	} else {
		mbstoc16s(fn, cdrom[drive].image_path, sizeof_w(fn));
		_swprintf(tempTip, plat_get_string(IDS_5120),
			  drive+1, szText, fn);
	}
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
    WCHAR fn[512];
    int id;
    int drive = sb_part_meanings[part] & 0xf;
    int bus = zip_drives[drive].bus_type;

    id = IDS_5377 + (bus - 1);
    szText = plat_get_string(id);

    int type = zip_drives[drive].is_250 ? 250 : 100;

    if (strlen(zip_drives[drive].image_path) == 0) {
	_swprintf(tempTip, plat_get_string(IDS_2054),
		  type, drive+1, szText, plat_get_string(IDS_2057));
    } else {
	mbstoc16s(fn, zip_drives[drive].image_path, sizeof_w(fn));
	_swprintf(tempTip, plat_get_string(IDS_2054),
		  type, drive+1, szText, fn);
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
    WCHAR fn[512];
    int id;
    int drive = sb_part_meanings[part] & 0xf;
    int bus = mo_drives[drive].bus_type;

    id = IDS_5377 + (bus - 1);
    szText = plat_get_string(id);

    if (strlen(mo_drives[drive].image_path) == 0) {
	_swprintf(tempTip, plat_get_string(IDS_2115),
		  drive+1, szText, plat_get_string(IDS_2057));
    } else {
	mbstoc16s(fn, mo_drives[drive].image_path, sizeof_w(fn));
	_swprintf(tempTip, plat_get_string(IDS_2115),
		  drive+1, szText, fn);
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
		case SB_CASSETTE:
			StatusBarCreateCassetteTip(part);
			break;

		case SB_CARTRIDGE:
			StatusBarCreateCartridgeTip(part);
			break;

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






static VOID APIENTRY
StatusBarPopupMenu(HWND hwnd, POINT pt, int id)
{
    HMENU menu;

    if (id >= (sb_parts - 1)) return;

    pt.x = id * icon_width;	/* Justify to the left. */
    pt.y = 0;			/* Justify to the top. */
    ClientToScreen(hwnd, (LPPOINT) &pt);

    switch(sb_part_meanings[id] & 0xF0) {
	case SB_CASSETTE:
		menu = media_menu_get_cassette();
		break;
	case SB_CARTRIDGE:
		menu = media_menu_get_cartridge(sb_part_meanings[id] & 0x0F);
		break;
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

/* API: Load status bar icons */
void
StatusBarLoadIcon(HINSTANCE hInst) {
	int i;
	int x = win_get_system_metrics(SM_CXSMICON, dpi);

	for (i=0; i<256; i++) {
		if (hIcon[i] != 0)
			DestroyIcon(hIcon[i]);
	}
	
    for (i = 16; i < 18; i++)
	hIcon[i] = LoadImage(hInst, MAKEINTRESOURCE(i), IMAGE_ICON, x, x, LR_DEFAULTCOLOR);
    for (i = 24; i < 26; i++)
	hIcon[i] = LoadImage(hInst, MAKEINTRESOURCE(i), IMAGE_ICON, x, x, LR_DEFAULTCOLOR);
    for (i = 32; i < 34; i++)
	hIcon[i] = LoadImage(hInst, MAKEINTRESOURCE(i), IMAGE_ICON, x, x, LR_DEFAULTCOLOR);
    for (i = 48; i < 50; i++)
	hIcon[i] = LoadImage(hInst, MAKEINTRESOURCE(i), IMAGE_ICON, x, x, LR_DEFAULTCOLOR);
    for (i = 56; i < 58; i++)
	hIcon[i] = LoadImage(hInst, MAKEINTRESOURCE(i), IMAGE_ICON, x, x, LR_DEFAULTCOLOR);
    for (i = 64; i < 66; i++)
	hIcon[i] = LoadImage(hInst, MAKEINTRESOURCE(i), IMAGE_ICON, x, x, LR_DEFAULTCOLOR);
    for (i = 80; i < 82; i++)
	hIcon[i] = LoadImage(hInst, MAKEINTRESOURCE(i), IMAGE_ICON, x, x, LR_DEFAULTCOLOR);
    for (i = 96; i < 98; i++)
	hIcon[i] = LoadImage(hInst, MAKEINTRESOURCE(i), IMAGE_ICON, x, x, LR_DEFAULTCOLOR);
    hIcon[104] = LoadImage(hInst, MAKEINTRESOURCE(104), IMAGE_ICON, x, x, LR_DEFAULTCOLOR);
    for (i = 144; i < 146; i++)
	hIcon[i] = LoadImage(hInst, MAKEINTRESOURCE(i), IMAGE_ICON, x, x, LR_DEFAULTCOLOR);
    for (i = 152; i < 154; i++)
	hIcon[i] = LoadImage(hInst, MAKEINTRESOURCE(i), IMAGE_ICON, x, x, LR_DEFAULTCOLOR);
    for (i = 160; i < 162; i++)
	hIcon[i] = LoadImage(hInst, MAKEINTRESOURCE(i), IMAGE_ICON, x, x, LR_DEFAULTCOLOR);
    for (i = 176; i < 178; i++)
	hIcon[i] = LoadImage(hInst, MAKEINTRESOURCE(i), IMAGE_ICON, x, x, LR_DEFAULTCOLOR);
    for (i = 184; i < 186; i++)
	hIcon[i] = LoadImage(hInst, MAKEINTRESOURCE(i), IMAGE_ICON, x, x, LR_DEFAULTCOLOR);
    for (i = 192; i < 194; i++)
	hIcon[i] = LoadImage(hInst, MAKEINTRESOURCE(i), IMAGE_ICON, x, x, LR_DEFAULTCOLOR);
    hIcon[232] = LoadImage(hInst, MAKEINTRESOURCE(232), IMAGE_ICON, x, x, LR_DEFAULTCOLOR);
    for (i = 243; i < 244; i++)
	hIcon[i] = LoadImage(hInst, MAKEINTRESOURCE(i), IMAGE_ICON, x, x, LR_DEFAULTCOLOR);
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
	int i;
	HINSTANCE hInst;

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
			StatusBarPopupMenu(hwnd, pt, (pt.x / icon_width));
		break;

	case WM_LBUTTONDBLCLK:
		GetClientRect(hwnd, (LPRECT)& rc);
		pt.x = GET_X_LPARAM(lParam);
		pt.y = GET_Y_LPARAM(lParam);
		item_id = (pt.x / icon_width);
		if (PtInRect((LPRECT) &rc, pt) && (item_id < sb_parts)) {
			if (sb_part_meanings[item_id] == SB_SOUND)
				SoundGainDialogCreate(hwndMain);
		}
		break;

	case WM_DPICHANGED_AFTERPARENT:
		/* DPI changed, reload icons */
		hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
		dpi = win_get_dpi(hwnd);
		icon_width = MulDiv(SB_ICON_WIDTH, dpi, 96);
		StatusBarLoadIcon(hInst);

		for (i=0; i<sb_parts; i++) {
			if (sb_part_icons[i] != 255) {
				SendMessage(hwndSBAR, SB_SETICON, i, (LPARAM)hIcon[sb_part_icons[i]]);
			}

			iStatusWidths[i] = (i+1)*icon_width;
		}
		iStatusWidths[i-1] = -1;
		SendMessage(hwndSBAR, SB_SETPARTS, (WPARAM)sb_parts, (LPARAM)iStatusWidths);
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

	/* Get current DPI and calculate icon sizes */
	dpi = win_get_dpi(hwndParent);
	icon_width = MulDiv(SB_ICON_WIDTH, dpi, 96);

    /* Load our icons into the cache for faster access. */
	StatusBarLoadIcon(hInst);

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


static void
ui_sb_update_text()
{
    uint8_t part = 0xff;

    if (!sb_ready || (sb_parts == 0) || (sb_part_meanings == NULL))
	return;

    part = sb_map[SB_TEXT];

    if (part != 0xff)
	SendMessage(hwndSBAR, SB_SETTEXT, part | SBT_NOBORDERS, (LPARAM)((sb_text[0] != L'\0') ? sb_text : sb_bugtext));
}

#if 0
/* API */
void
ui_sb_set_text(char *str)
{
    if (str)
	mbstowcs(sb_text, str, strlen(str) + 1);
    else
	memset(sb_text, 0x00, sizeof(sb_text));
    ui_sb_update_text();
}
#endif

