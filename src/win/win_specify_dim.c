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
#include <86box/win_sdl.h>
#include <86box/win_imgui.h>
#include <SDL2/SDL.h>
extern SDL_Window* sdl_win;
extern float menubarheight;
extern HWND GetHWNDFromSDLWindow();

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
	int width = 0, height = 0;

    switch (message) {
	case WM_INITDIALOG:
		SDL_GetWindowSize(sdl_win, &width, &height);
		height -= menubarheight;
		if (!hide_status_bar) height -= menubarheight * 2;

		h = GetDlgItem(hdlg, IDC_WIDTHSPIN);
		h2 = GetDlgItem(hdlg, IDC_EDIT_WIDTH);
		SendMessage(h, UDM_SETBUDDY, (WPARAM)h2, 0);
		SendMessage(h, UDM_SETRANGE, 0, (120 << 16) | 2048);
		accel.nSec = 0;
		accel.nInc = 8;
		SendMessage(h, UDM_SETACCEL, 1, (LPARAM)&accel);
		SendMessage(h, UDM_SETPOS, 0, width);

		h = GetDlgItem(hdlg, IDC_HEIGHTSPIN);
		h2 = GetDlgItem(hdlg, IDC_EDIT_HEIGHT);
		SendMessage(h, UDM_SETBUDDY, (WPARAM)h2, 0);
		SendMessage(h, UDM_SETRANGE, 0, (120 << 16) | 2048);
		accel2.nSec = 0;
		accel2.nInc = 8;
		SendMessage(h, UDM_SETACCEL, 1, (LPARAM)&accel2);
		SendMessage(h, UDM_SETPOS, 0, height);

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

				if (vid_resize) {
					scale = 1;
					if (vid_resize & 1)	{
						SDL_SetWindowResizable(sdl_win, SDL_TRUE);
					}
				}
				scrnsz_x = fixed_size_x;
				scrnsz_y = fixed_size_y;
				PostMessage(GetHWNDFromSDLWindow(), WM_FORCERESIZE, scrnsz_x, scrnsz_y + menubarheight + (hide_status_bar ? 0 : menubarheight * 2));

				if (window_remember || (vid_resize & 2)) {
					SDL_GetWindowPosition(sdl_win, &window_x, &window_y);
					if (!(vid_resize & 2)) {
						window_w = scrnsz_x;
						window_h = scrnsz_y + menubarheight + (hide_status_bar ? 0 : menubarheight * 2) ;
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
