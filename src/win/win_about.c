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
 * Version:	@(#)win_about.c	1.0.1	2017/10/06
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
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
#include "../86box.h"
#include "../ibm.h"
#include "win.h"
#include "win_language.h"


#ifndef MAPVK_VK_TO_VSC
#define MAPVK_VK_TO_VSC 0
#endif


static BOOL CALLBACK
AboutDialogProcedure(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND h;

    switch (message) {
	case WM_INITDIALOG:
		pause = 1;
		h = GetDlgItem(hdlg, IDC_ABOUT_ICON);
		SendMessage(h, STM_SETIMAGE, (WPARAM)IMAGE_ICON,
		  (LPARAM)LoadImage(hinstance,(PCTSTR)100,IMAGE_ICON,64,64,0));
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


void
AboutDialogCreate(HWND hwnd)
{
    DialogBox(hinstance, (LPCTSTR)DLG_ABOUT, hwnd, AboutDialogProcedure);
}
