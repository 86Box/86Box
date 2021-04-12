/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handle the dialog for specifying the dimensions of the main window.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 */
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#undef BITMAP
#include <commctrl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/plat.h>
#include <86box/sound.h>
#include <86box/win.h>


#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
SpecifyDimensionsDialogProcedure(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND h, h2;
    HMENU hmenu;
    UDACCEL accel, accel2;
    RECT r;
    uint32_t temp_x = 0, temp_y = 0;
    int dpi = 96;
    LPTSTR lptsTemp;
    char *stransi;

    switch (message) {
	case WM_INITDIALOG:
		GetWindowRect(hwndRender, &r);

		h = GetDlgItem(hdlg, IDC_WIDTHSPIN);
		h2 = GetDlgItem(hdlg, IDC_EDIT_WIDTH);
		SendMessage(h, UDM_SETBUDDY, (WPARAM)h2, 0);
		SendMessage(h, UDM_SETRANGE, 0, (120 << 16) | 2048);
		accel.nSec = 0;
		accel.nInc = 8;
		SendMessage(h, UDM_SETACCEL, 1, (LPARAM)&accel);
		SendMessage(h, UDM_SETPOS, 0, r.right - r.left);

		h = GetDlgItem(hdlg, IDC_HEIGHTSPIN);
		h2 = GetDlgItem(hdlg, IDC_EDIT_HEIGHT);
		SendMessage(h, UDM_SETBUDDY, (WPARAM)h2, 0);
		SendMessage(h, UDM_SETRANGE, 0, (120 << 16) | 2048);
		accel2.nSec = 0;
		accel2.nInc = 8;
		SendMessage(h, UDM_SETACCEL, 1, (LPARAM)&accel2);
		SendMessage(h, UDM_SETPOS, 0, r.bottom - r.top);
		break;

	case WM_COMMAND:
                switch (LOWORD(wParam)) {
			case IDOK:
				lptsTemp = (LPTSTR) malloc(512 * sizeof(WCHAR));
				stransi = (char *)malloc(512);

				h = GetDlgItem(hdlg, IDC_EDIT_WIDTH);
				SendMessage(h, WM_GETTEXT, 255, (LPARAM) lptsTemp);
				wcstombs(stransi, lptsTemp, 512);
				sscanf(stransi, "%u", &temp_x);

				h = GetDlgItem(hdlg, IDC_EDIT_HEIGHT);
				SendMessage(h, WM_GETTEXT, 255, (LPARAM) lptsTemp);
				wcstombs(stransi, lptsTemp, 512);
				sscanf(stransi, "%u", &temp_y);

				window_remember = 1;
				vid_resize = 1;
				hmenu = GetMenu(hwndMain);
				CheckMenuItem(hmenu, IDM_VID_RESIZE, MF_CHECKED);

				SetWindowLongPtr(hwndMain, GWL_STYLE, (WS_OVERLAPPEDWINDOW) | WS_VISIBLE);

				/* scale the screen base on DPI */
				if (dpi_scale) {
					dpi = win_get_dpi(hwndMain);
					temp_x = MulDiv(temp_x, dpi, 96);
					temp_y = MulDiv(temp_y, dpi, 96);
				} else {
					temp_x = temp_x;
					temp_y = temp_y;
				}

				ResizeWindowByClientArea(hwndMain, temp_x, temp_y + sbar_height);

				if (mouse_capture)
					ClipCursor(&r);

				CheckMenuItem(hmenu, IDM_VID_SCALE_1X + scale, MF_UNCHECKED);
				CheckMenuItem(hmenu, IDM_VID_SCALE_2X, MF_CHECKED);
				scale = 1;
				EnableMenuItem(hmenu, IDM_VID_SCALE_1X, MF_GRAYED);
				EnableMenuItem(hmenu, IDM_VID_SCALE_2X, MF_GRAYED);
				EnableMenuItem(hmenu, IDM_VID_SCALE_3X, MF_GRAYED);
				EnableMenuItem(hmenu, IDM_VID_SCALE_4X, MF_GRAYED);

				scrnsz_x = temp_x;
				scrnsz_y = temp_y;
				doresize = 1;

				CheckMenuItem(hmenu, IDM_VID_REMEMBER, MF_CHECKED);
				GetWindowRect(hwndMain, &r);
				window_x = r.left;
				window_y = r.top;
				window_w = r.right - r.left;
				window_h = r.bottom - r.top;

				config_save();

				free(stransi);
				free(lptsTemp);

				EndDialog(hdlg, 0);
				return TRUE;

			case IDCANCEL:
				EndDialog(hdlg, 0);
				return TRUE;

			default:
				break;
		}
		break;
    }

    return(FALSE);
}


void
SpecifyDimensionsDialogCreate(HWND hwnd)
{
    DialogBox(hinstance, (LPCTSTR)DLG_SPECIFY_DIM, hwnd, SpecifyDimensionsDialogProcedure);
}
