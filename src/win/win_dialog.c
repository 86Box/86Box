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
 * Version:	@(#)win_dialog.c	1.0.7	2017/12/28
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#define UNICODE
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../device.h"
#include "../plat.h"
#include "../ui.h"
#include "win.h"


WCHAR	path[MAX_PATH];
WCHAR	wopenfilestring[260];
char	openfilestring[260];


static int CALLBACK
BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{
    if (uMsg == BFFM_INITIALIZED)
	SendMessage(hwnd, BFFM_SETSELECTION, TRUE, lpData);

    return(0);
}


wchar_t *
BrowseFolder(wchar_t *saved_path, wchar_t *title)
{
    BROWSEINFO bi = { 0 };
    LPITEMIDLIST pidl;
    IMalloc *imalloc;

    bi.lpszTitle  = title;
    bi.ulFlags    = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn       = BrowseCallbackProc;
    bi.lParam     = (LPARAM) saved_path;

    pidl = SHBrowseForFolder(&bi);
    if (pidl != 0) {
	/* Get the name of the folder and put it in path. */
	SHGetPathFromIDList(pidl, path);

	/* Free memory used. */
	imalloc = 0;
	if (SUCCEEDED(SHGetMalloc(&imalloc))) {
		imalloc->lpVtbl->Free(imalloc, pidl);
		imalloc->lpVtbl->Release(imalloc);
	}

	return(path);
    }

    return(L"");
}


int
ui_msgbox(int flags, void *arg)
{
    WCHAR temp[512];
    DWORD fl = 0;
    WCHAR *str = NULL;
    WCHAR *cap = NULL;

    switch(flags & 0x1f) {
	case MBX_INFO:		/* just an informational message */
		fl = (MB_OK | MB_ICONINFORMATION);
		cap = plat_get_string(IDS_STRINGS);	    /* "86Box" */
		break;

	case MBX_ERROR:		/* error message */
		if (flags & MBX_FATAL) {
			fl = (MB_OK | MB_ICONERROR);
			cap = plat_get_string(IDS_2050);    /* "Fatal Error"*/
		} else {
			fl = (MB_OK | MB_ICONWARNING);
			cap = plat_get_string(IDS_2049);    /* "Error" */
		}
		break;

	case MBX_QUESTION:	/* question */
		fl = (MB_YESNOCANCEL | MB_ICONQUESTION);
		cap = plat_get_string(IDS_STRINGS);	    /* "86Box" */
		break;
    }

    /* If ANSI string, convert it. */
    str = (WCHAR *)arg;
    if (flags & MBX_ANSI) {
	mbstowcs(temp, (char *)arg, strlen((char *)arg)+1);
	str = temp;
    } else {
	/*
	 * It's a Unicode string.
	 *
	 * Well, no, maybe not. It could also be one of the
	 * strings stored in the Resources. Those are wide,
	 * but referenced by a numeric ID.
	 *
	 * The good news is, that strings are usually stored
	 * in the executable near the end of the code/rodata
	 * segment. This means, that *real* string pointers
	 * usually have a pretty high (numeric) value, much
	 * higher than the numeric ID's.  So, we guesswork
	 * that if the value of 'arg' is low, its an ID..
	 */
	if (((uintptr_t)arg) < ((uintptr_t)65636))
		str = plat_get_string((intptr_t)arg);
    }

    /* At any rate, we do have a valid (wide) string now. */
    fl = MessageBox(hwndMain,		/* our main window */
		    str,		/* error message etc */
		    cap,		/* window caption */
		    fl);

    /* Convert return values to generic ones. */
    if (fl == IDNO) fl = 1;
     else if (fl == IDCANCEL) fl = -1;
     else fl = 0;

    return(fl);
}


#if 0
int
msgbox_reset_yn(HWND hwnd)
{
    return(MessageBox(hwnd, plat_get_string(IDS_2051),
#endif


int
file_dlg_w(HWND hwnd, WCHAR *f, WCHAR *fn, int save)
{
    OPENFILENAME ofn;
    BOOL r;
    /* DWORD err; */

    /* Initialize OPENFILENAME */
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = wopenfilestring;

    /*
     * Set lpstrFile[0] to '\0' so that GetOpenFileName does
     * not use the contents of szFile to initialize itself.
     */
    memcpy(ofn.lpstrFile, fn, (wcslen(fn) << 1) + 2);
    ofn.nMaxFile = 259;
    ofn.lpstrFilter = f;
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST;
    if (! save)
	ofn.Flags |= OFN_FILEMUSTEXIST;

    /* Display the Open dialog box. */
    if (save) {
//	pclog("GetSaveFileName - lpstrFile = %s\n", ofn.lpstrFile);
	r = GetSaveFileName(&ofn);
    } else {
//	pclog("GetOpenFileName - lpstrFile = %s\n", ofn.lpstrFile);
	r = GetOpenFileName(&ofn);
    }

    plat_chdir(usr_path);

    if (r) {
	wcstombs(openfilestring, wopenfilestring, sizeof(openfilestring));
//	pclog("File dialog return true\n");

	return(0);
    }

    /* pclog("File dialog return false\n"); */
    /* err = CommDlgExtendedError();
    pclog("CommDlgExtendedError return %04X\n", err); */

    return(1);
}


int
file_dlg(HWND hwnd, WCHAR *f, char *fn, int save)
{
    WCHAR ufn[512];

    mbstowcs(ufn, fn, strlen(fn) + 1);

    return(file_dlg_w(hwnd, f, ufn, save));
}


int
file_dlg_mb(HWND hwnd, char *f, char *fn, int save)
{
    WCHAR uf[512], ufn[512];

    mbstowcs(uf, f, strlen(fn) + 1);
    mbstowcs(ufn, fn, strlen(fn) + 1);

    return(file_dlg_w(hwnd, uf, ufn, save));
}


int
file_dlg_w_st(HWND hwnd, int id, WCHAR *fn, int save)
{
    return(file_dlg_w(hwnd, plat_get_string(id), fn, save));
}


int
file_dlg_st(HWND hwnd, int id, char *fn, int save)
{
    return(file_dlg(hwnd, plat_get_string(id), fn, save));
}
