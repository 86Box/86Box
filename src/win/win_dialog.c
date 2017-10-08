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
 * Version:	@(#)win_dialog.c	1.0.1	2017/10/07
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#undef BITMAP
#include <commdlg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../ibm.h"
#include "../device.h"
#include "plat_ui.h"
#include "win.h"


WCHAR path[MAX_PATH];


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
msgbox_reset(HWND hwnd)
{
    return(MessageBox(hwnd, win_get_string(IDS_2051),
		      win_get_string(IDS_STRINGS),
		      MB_YESNOCANCEL | MB_ICONQUESTION));
}


int
msgbox_reset_yn(HWND hwnd)
{
    return(MessageBox(hwnd, win_get_string(IDS_2051),
		      win_get_string(IDS_STRINGS),
		      MB_YESNO | MB_ICONQUESTION));
}


int
msgbox_question(HWND hwnd, int i)
{
    return(MessageBox(hwnd, win_get_string(i),
		      win_get_string(IDS_STRINGS),
		      MB_YESNO | MB_ICONQUESTION));
}


void
msgbox_info(HWND hwnd, int i)
{
    MessageBox(hwnd, win_get_string(i), win_get_string(IDS_STRINGS),
	       MB_OK | MB_ICONINFORMATION);
}


void
msgbox_info_wstr(HWND hwnd, WCHAR *wstr)
{
    MessageBox(hwnd, wstr, win_get_string(IDS_STRINGS),
	       MB_OK | MB_ICONINFORMATION);
}


void
msgbox_error(HWND hwnd, int i)
{
    MessageBox(hwnd, win_get_string(i), win_get_string(IDS_2049),
	       MB_OK | MB_ICONWARNING);
}


void
plat_msgbox_error(int i)
{
    msgbox_error(hwndMain, i);
}


void
msgbox_error_wstr(HWND hwnd, WCHAR *wstr)
{
    MessageBox(hwnd, wstr, win_get_string(IDS_2049), MB_OK | MB_ICONWARNING);
}


void
msgbox_critical(HWND hwnd, int i)
{
    MessageBox(hwnd, win_get_string(i), win_get_string(IDS_2050),
	       MB_OK | MB_ICONERROR);
}


void
msgbox_fatal(HWND hwnd, char *string)
{
    LPTSTR temp;

    temp = (LPTSTR)malloc(512);
    mbstowcs(temp, string, strlen(string)+1);

    MessageBox(hwnd, temp, win_get_string(IDS_2050), MB_OK | MB_ICONERROR);

    free(temp);
}


void
plat_msgbox_fatal(char *string)
{
    msgbox_fatal(hwndMain, string);
}


int
file_dlg_w(HWND hwnd, WCHAR *f, WCHAR *fn, int save)
{
    OPENFILENAME ofn;
    BOOL r;
    DWORD err;

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

    if (r) {
	wcstombs(openfilestring, wopenfilestring, sizeof(openfilestring));
//	pclog("File dialog return true\n");

	return(0);
    }

    pclog("File dialog return false\n");
    err = CommDlgExtendedError();
    pclog("CommDlgExtendedError return %04X\n", err);

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
    return(file_dlg_w(hwnd, win_get_string(id), fn, save));
}


int
file_dlg_st(HWND hwnd, int id, char *fn, int save)
{
    return(file_dlg(hwnd, win_get_string(id), fn, save));
}
