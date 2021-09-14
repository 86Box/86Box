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
    int dpi = 96, lock;
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

		h = GetDlgItem(hdlg, IDC_CHECK_LOCK_SIZE);
		SendMessage(h, BM_SETCHECK, !!(vid_resize & 2), 0);
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
				fixed_size_x = temp_x;

				h = GetDlgItem(hdlg, IDC_EDIT_HEIGHT);
				SendMessage(h, WM_GETTEXT, 255, (LPARAM) lptsTemp);
				wcstombs(stransi, lptsTemp, 512);
				sscanf(stransi, "%u", &temp_y);
				fixed_size_y = temp_y;

				h = GetDlgItem(hdlg, IDC_CHECK_LOCK_SIZE);
				lock = SendMessage(h, BM_GETCHECK, 0, 0);

				if (lock) {
					vid_resize = 2;
					window_remember = 0;
				} else {
					vid_resize = 1;
					window_remember = 1;
				}
				hmenu = GetMenu(hwndMain);
				CheckMenuItem(hmenu, IDM_VID_REMEMBER, (window_remember == 1) ? MF_CHECKED : MF_UNCHECKED);
				CheckMenuItem(hmenu, IDM_VID_RESIZE, (vid_resize == 1) ? MF_CHECKED : MF_UNCHECKED);
				EnableMenuItem(hmenu, IDM_VID_RESIZE, (vid_resize & 2) ? MF_GRAYED : MF_ENABLED);

				if (vid_resize == 1)
					SetWindowLongPtr(hwndMain, GWL_STYLE, (WS_OVERLAPPEDWINDOW) | WS_VISIBLE);
				else
					SetWindowLongPtr(hwndMain, GWL_STYLE, (WS_OVERLAPPEDWINDOW & ~WS_SIZEBOX & ~WS_MAXIMIZEBOX) | WS_VISIBLE);

				/* scale the screen base on DPI */
				if (dpi_scale) {
					dpi = win_get_dpi(hwndMain);
					temp_x = MulDiv(temp_x, dpi, 96);
					temp_y = MulDiv(temp_y, dpi, 96);
				}

				ResizeWindowByClientArea(hwndMain, temp_x, temp_y + sbar_height);

				if (vid_resize) {
					CheckMenuItem(hmenu, IDM_VID_SCALE_1X + scale, MF_UNCHECKED);
					CheckMenuItem(hmenu, IDM_VID_SCALE_2X, MF_CHECKED);
					scale = 1;
				}
				EnableMenuItem(hmenu, IDM_VID_SCALE_1X, vid_resize ? MF_GRAYED : MF_ENABLED);
				EnableMenuItem(hmenu, IDM_VID_SCALE_2X, vid_resize ? MF_GRAYED : MF_ENABLED);
				EnableMenuItem(hmenu, IDM_VID_SCALE_3X, vid_resize ? MF_GRAYED : MF_ENABLED);
				EnableMenuItem(hmenu, IDM_VID_SCALE_4X, vid_resize ? MF_GRAYED : MF_ENABLED);

				scrnsz_x = fixed_size_x;
				scrnsz_y = fixed_size_y;
				doresize = 1;

				GetWindowRect(hwndMain, &r);

				if (mouse_capture)
					ClipCursor(&r);

				if (window_remember || (vid_resize & 2)) {
					window_x = r.left;
					window_y = r.top;
					if (!(vid_resize & 2)) {
						window_w = r.right - r.left;
						window_h = r.bottom - r.top;
					}
				}

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
