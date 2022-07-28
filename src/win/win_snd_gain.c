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

static uint8_t old_gain;

#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
SoundGainDialogProcedure(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND h;

    switch (message) {
        case WM_INITDIALOG:
            old_gain = sound_gain;
            h        = GetDlgItem(hdlg, IDC_SLIDER_GAIN);
            SendMessage(h, TBM_SETRANGE, (WPARAM) 1, (LPARAM) MAKELONG(0, 9));
            SendMessage(h, TBM_SETPOS, (WPARAM) 1, 9 - (sound_gain >> 1));
            SendMessage(h, TBM_SETTICFREQ, (WPARAM) 1, 0);
            SendMessage(h, TBM_SETLINESIZE, (WPARAM) 0, 1);
            SendMessage(h, TBM_SETPAGESIZE, (WPARAM) 0, 2);
            break;

        case WM_VSCROLL:
            h          = GetDlgItem(hdlg, IDC_SLIDER_GAIN);
            sound_gain = (9 - SendMessage(h, TBM_GETPOS, (WPARAM) 0, 0)) << 1;
            break;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK:
                    h          = GetDlgItem(hdlg, IDC_SLIDER_GAIN);
                    sound_gain = (9 - SendMessage(h, TBM_GETPOS, (WPARAM) 0, 0)) << 1;
                    config_save();
                    EndDialog(hdlg, 0);
                    return TRUE;

                case IDCANCEL:
                    sound_gain = old_gain;
                    config_save();
                    EndDialog(hdlg, 0);
                    return TRUE;

                default:
                    break;
            }
            break;
    }

    return (FALSE);
}

void
SoundGainDialogCreate(HWND hwnd)
{
    DialogBox(hinstance, (LPCTSTR) DLG_SND_GAIN, hwnd, SoundGainDialogProcedure);
}
