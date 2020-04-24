/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handle the About dialog.
 *
 *
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
#undef BITMAP
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/win.h>


#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
AboutDialogProcedure(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND h;
    HANDLE ih;

    switch (message) {
	case WM_INITDIALOG:
		plat_pause(1);
		h = GetDlgItem(hdlg, IDC_ABOUT_ICON);
		ih = LoadImage(hinstance,(PCTSTR)10,IMAGE_ICON,64,64,0);
		SendMessage(h, STM_SETIMAGE, (WPARAM)IMAGE_ICON,
		  (LPARAM)ih);
		break;

	case WM_COMMAND:
                switch (LOWORD(wParam)) {
			case IDOK:
			case IDCANCEL:
				EndDialog(hdlg, 0);
				plat_pause(0);
				return TRUE;

			default:
				break;
		}
		break;
    }

    return(FALSE);
}


void
AboutDialogCreate(HWND hwnd)
{
    DialogBox(hinstance, (LPCTSTR)DLG_ABOUT, hwnd, AboutDialogProcedure);
}
