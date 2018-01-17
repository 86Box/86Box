/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handle the sound gain dialog.
 *
 * Version:	@(#)win_snd_gain.c	1.0.0	2018/01/17
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
#include "../86box.h"
#include "../config.h"
#include "../plat.h"
#include "../sound/sound.h"
#include "win.h"


static uint8_t	old_gain[3];


#ifdef __amd64__
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
SoundGainDialogProcedure(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND h;
    int i;

    switch (message) {
	case WM_INITDIALOG:
		plat_pause(1);
		for (i = 0; i < 3; i++) {
			old_gain[i] = sound_gain[i];
			h = GetDlgItem(hdlg, IDC_SLIDER_MAIN + i);
			SendMessage(h, TBM_SETRANGE, (WPARAM)1, (LPARAM)MAKELONG(0, 9));
			SendMessage(h, TBM_SETPOS, (WPARAM)1, 9 - (sound_gain[i] >> 1));
			SendMessage(h, TBM_SETTICFREQ, (WPARAM)1, 0);
			SendMessage(h, TBM_SETLINESIZE, (WPARAM)0, 1);
			SendMessage(h, TBM_SETPAGESIZE, (WPARAM)0, 2);
		}
		break;

	case WM_VSCROLL:
		for (i = 0; i < 3; i++) {
			h = GetDlgItem(hdlg, IDC_SLIDER_MAIN + i);
			sound_gain[i] = (9 - SendMessage(h, TBM_GETPOS, (WPARAM)0, 0)) << 1;
		}
		break;

	case WM_COMMAND:
                switch (LOWORD(wParam)) {
			case IDOK:
				for (i = 0; i < 3; i++) {
					h = GetDlgItem(hdlg, IDC_SLIDER_MAIN + i);
					sound_gain[i] = (9 - SendMessage(h, TBM_GETPOS, (WPARAM)0, 0)) << 1;
				}
				config_save();
				EndDialog(hdlg, 0);
				return TRUE;

			case IDCANCEL:
				for (i = 0; i < 3; i++)
					sound_gain[i] = old_gain[i];
				config_save();
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
SoundGainDialogCreate(HWND hwnd)
{
    DialogBox(hinstance, (LPCTSTR)DLG_SND_GAIN, hwnd, SoundGainDialogProcedure);
}
