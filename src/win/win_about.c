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
#include <commctrl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/win.h>


void
AboutDialogCreate(HWND hwnd)
{
    TASKDIALOGCONFIG tdconfig = {0};
    TASKDIALOG_BUTTON tdbuttons[] = {{IDCANCEL, MAKEINTRESOURCE(IDS_2127)}};

    tdconfig.cbSize = sizeof(tdconfig);
    tdconfig.hwndParent = hwnd;
    tdconfig.hInstance = hinstance;
    tdconfig.dwCommonButtons = 0;
    tdconfig.pszWindowTitle = MAKEINTRESOURCE(IDS_2124);
    tdconfig.pszMainIcon = (PCWSTR) 10;
    tdconfig.pszMainInstruction = MAKEINTRESOURCE(IDS_2125);
    tdconfig.pszContent = MAKEINTRESOURCE(IDS_2126);
    tdconfig.cButtons = ARRAYSIZE(tdbuttons);
    tdconfig.pButtons = tdbuttons;
    TaskDialogIndirect(&tdconfig, NULL, NULL, NULL);
}
