/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Several dialogs for the application.
 *
 *
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 */
#define UNICODE
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <commdlg.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/win.h>

#define STRING_OR_RESOURCE(s) ((!(s)) ? (NULL) : ((((uintptr_t) s) < ((uintptr_t) 65636)) ? (MAKEINTRESOURCE((uintptr_t) s)) : (s)))

WCHAR   wopenfilestring[512];
char    openfilestring[512];
uint8_t filterindex = 0;

int
ui_msgbox(int flags, void *message)
{
    return ui_msgbox_ex(flags, NULL, message, NULL, NULL, NULL);
}

int
ui_msgbox_header(int flags, void *header, void *message)
{
    return ui_msgbox_ex(flags, header, message, NULL, NULL, NULL);
}

int
ui_msgbox_ex(int flags, void *header, void *message, void *btn1, void *btn2, void *btn3)
{
    WCHAR             temp[512];
    TASKDIALOGCONFIG  tdconfig = { 0 };
    TASKDIALOG_BUTTON tdbuttons[3],
        tdb_yes    = { IDYES, STRING_OR_RESOURCE(btn1) },
        tdb_no     = { IDNO, STRING_OR_RESOURCE(btn2) },
        tdb_cancel = { IDCANCEL, STRING_OR_RESOURCE(btn3) },
        tdb_exit   = { IDCLOSE, MAKEINTRESOURCE(IDS_2119) };
    int ret = 0, checked = 0;

    /* Configure the default OK button. */
    tdconfig.cButtons = 0;
    if (btn1)
        tdbuttons[tdconfig.cButtons++] = tdb_yes;
    else
        tdconfig.dwCommonButtons = TDCBF_OK_BUTTON;

    /* Configure the message type. */
    switch (flags & 0x1f) {
        case MBX_INFO: /* just an informational message */
            tdconfig.pszMainIcon = TD_INFORMATION_ICON;
            break;

        case MBX_ERROR: /* error message */
            if (flags & MBX_FATAL) {
                tdconfig.pszMainIcon        = TD_ERROR_ICON;
                tdconfig.pszMainInstruction = MAKEINTRESOURCE(IDS_2050); /* "Fatal error" */

                /* replace default "OK" button with "Exit" button */
                if (btn1)
                    tdconfig.cButtons = 0;
                else
                    tdconfig.dwCommonButtons = 0;
                tdbuttons[tdconfig.cButtons++] = tdb_exit;
            } else {
                tdconfig.pszMainIcon        = TD_WARNING_ICON;
                tdconfig.pszMainInstruction = MAKEINTRESOURCE(IDS_2049); /* "Error" */
            }
            break;

        case MBX_QUESTION: /* question */
        case MBX_QUESTION_YN:
        case MBX_QUESTION_OK:
            if (!btn1) /* replace default "OK" button with "Yes" button */
                tdconfig.dwCommonButtons = (flags & MBX_QUESTION_OK) ? TDCBF_OK_BUTTON : TDCBF_YES_BUTTON;

            if (btn2) /* "No" button */
                tdbuttons[tdconfig.cButtons++] = tdb_no;
            else
                tdconfig.dwCommonButtons |= (flags & MBX_QUESTION_OK) ? TDCBF_CANCEL_BUTTON : TDCBF_NO_BUTTON;

            if (flags & MBX_QUESTION) {
                if (btn3) /* "Cancel" button */
                    tdbuttons[tdconfig.cButtons++] = tdb_cancel;
                else
                    tdconfig.dwCommonButtons |= TDCBF_CANCEL_BUTTON;
            }

            if (flags & MBX_WARNING)
                tdconfig.pszMainIcon = TD_WARNING_ICON;
            break;
    }

    /* If the message is an ANSI string, convert it. */
    tdconfig.pszContent = (WCHAR *) STRING_OR_RESOURCE(message);
    if (flags & MBX_ANSI) {
        mbstoc16s(temp, (char *) message, strlen((char *) message) + 1);
        tdconfig.pszContent = temp;
    }

    /* Configure the rest of the TaskDialog. */
    tdconfig.cbSize     = sizeof(tdconfig);
    tdconfig.hwndParent = hwndMain;
    if (flags & MBX_LINKS)
        tdconfig.dwFlags = TDF_USE_COMMAND_LINKS;
    tdconfig.pszWindowTitle = MAKEINTRESOURCE(IDS_STRINGS);
    if (header)
        tdconfig.pszMainInstruction = STRING_OR_RESOURCE(header);
    tdconfig.pButtons = tdbuttons;
    if (flags & MBX_DONTASK)
        tdconfig.pszVerificationText = MAKEINTRESOURCE(IDS_2135);

    /* Run the TaskDialog. */
    TaskDialogIndirect(&tdconfig, &ret, NULL, &checked);

    /* Convert return values to generic ones. */
    if (ret == IDNO)
        ret = 1;
    else if (ret == IDCANCEL)
        ret = -1;
    else
        ret = 0;

    /* 10 is added to the return value if "don't show again" is checked. */
    if (checked)
        ret += 10;

    return (ret);
}

int
file_dlg_w(HWND hwnd, WCHAR *f, WCHAR *fn, WCHAR *title, int save)
{
    OPENFILENAME ofn;
    BOOL         r;
    /* DWORD err; */
    int old_dopause;

    /* Initialize OPENFILENAME */
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFile   = wopenfilestring;

    /*
     * Set lpstrFile[0] to '\0' so that GetOpenFileName does
     * not use the contents of szFile to initialize itself.
     */
    memset(ofn.lpstrFile, 0x00, 512 * sizeof(WCHAR));
    if (fn)
        memcpy(ofn.lpstrFile, fn, (wcslen(fn) << 1) + 2);
    ofn.nMaxFile        = sizeof_w(wopenfilestring);
    ofn.lpstrFilter     = f;
    ofn.nFilterIndex    = 1;
    ofn.lpstrFileTitle  = NULL;
    ofn.nMaxFileTitle   = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags           = OFN_PATHMUSTEXIST;
    if (!save)
        ofn.Flags |= OFN_FILEMUSTEXIST;
    if (title)
        ofn.lpstrTitle = title;

    /* Display the Open dialog box. */
    old_dopause = dopause;
    plat_pause(1);
    if (save)
        r = GetSaveFileName(&ofn);
    else
        r = GetOpenFileName(&ofn);
    plat_pause(old_dopause);

    plat_chdir(usr_path);

    if (r) {
        c16stombs(openfilestring, wopenfilestring, sizeof(openfilestring));
        filterindex = ofn.nFilterIndex;

        return (0);
    }

    return (1);
}

int
file_dlg(HWND hwnd, WCHAR *f, char *fn, char *title, int save)
{
    WCHAR ufn[512], title_buf[512];

    if (fn)
        mbstoc16s(ufn, fn, strlen(fn) + 1);
    if (title)
        mbstoc16s(title_buf, title, sizeof title_buf);

    return (file_dlg_w(hwnd, f, fn ? ufn : NULL, title ? title_buf : NULL, save));
}

int
file_dlg_mb(HWND hwnd, char *f, char *fn, char *title, int save)
{
    WCHAR uf[512], ufn[512], title_buf[512];

    mbstoc16s(uf, f, strlen(f) + 1);
    mbstoc16s(ufn, fn, strlen(fn) + 1);
    if (title)
        mbstoc16s(title_buf, title, sizeof title_buf);

    return (file_dlg_w(hwnd, uf, ufn, title ? title_buf : NULL, save));
}

int
file_dlg_w_st(HWND hwnd, int id, WCHAR *fn, char *title, int save)
{
    WCHAR title_buf[512];
    if (title)
        mbstoc16s(title_buf, title, sizeof title_buf);
    return (file_dlg_w(hwnd, plat_get_string(id), fn, title ? title_buf : NULL, save));
}

int
file_dlg_st(HWND hwnd, int id, char *fn, char *title, int save)
{
    return (file_dlg(hwnd, plat_get_string(id), fn, title, save));
}
