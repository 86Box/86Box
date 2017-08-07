/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Windows localization core.
 *
 * Version:	@(#)win_language.c	1.0.0	2017/05/30
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */
#include <inttypes.h>
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#undef BITMAP

#include <commdlg.h>

#include "../ibm.h"
#include "../device.h"
#include "../ide.h"
#include "plat_ui.h"
#include "win.h"
#include "win_language.h"


LCID dwLanguage;

uint32_t dwLangID, dwSubLangID;

typedef struct
{
	WCHAR lpString[512];
} resource_string_t;

resource_string_t *lpResourceString2048;
resource_string_t *lpResourceString3072;
resource_string_t *lpResourceString4096;
resource_string_t *lpResourceString4352;
resource_string_t *lpResourceString4608;
resource_string_t *lpResourceString5120;
resource_string_t *lpResourceString5376;
resource_string_t *lpResourceString5632;
resource_string_t *lpResourceString6144;

char openfilestring[260];
WCHAR wopenfilestring[260];

void win_language_set()
{
	SetThreadLocale(dwLanguage);
}

void win_language_load_common_strings()
{
	int i = 0;

	lpResourceString2048 = (resource_string_t *) malloc(STRINGS_NUM_2048 * sizeof(resource_string_t));
	lpResourceString3072 = (resource_string_t *) malloc(STRINGS_NUM_3072 * sizeof(resource_string_t));
	lpResourceString4096 = (resource_string_t *) malloc(STRINGS_NUM_4096 * sizeof(resource_string_t));
	lpResourceString4352 = (resource_string_t *) malloc(STRINGS_NUM_4352 * sizeof(resource_string_t));
	lpResourceString4608 = (resource_string_t *) malloc(STRINGS_NUM_4608 * sizeof(resource_string_t));
	lpResourceString5120 = (resource_string_t *) malloc(STRINGS_NUM_5120 * sizeof(resource_string_t));
	lpResourceString5376 = (resource_string_t *) malloc(STRINGS_NUM_5376 * sizeof(resource_string_t));
	lpResourceString5632 = (resource_string_t *) malloc(STRINGS_NUM_5632 * sizeof(resource_string_t));
	lpResourceString6144 = (resource_string_t *) malloc(STRINGS_NUM_6144 * sizeof(resource_string_t));

	for (i = 0; i < STRINGS_NUM_2048; i++)
	{
		LoadString(hinstance, 2048 + i, lpResourceString2048[i].lpString, 512);
	}

	for (i = 0; i < STRINGS_NUM_3072; i++)
	{
		LoadString(hinstance, 3072 + i, lpResourceString3072[i].lpString, 512);
	}

	for (i = 0; i < STRINGS_NUM_4096; i++)
	{
		LoadString(hinstance, 4096 + i, lpResourceString4096[i].lpString, 512);
	}

	for (i = 0; i < STRINGS_NUM_4352; i++)
	{
		LoadString(hinstance, 4352 + i, lpResourceString4352[i].lpString, 512);
	}

	for (i = 0; i < STRINGS_NUM_4608; i++)
	{
		LoadString(hinstance, 4608 + i, lpResourceString4608[i].lpString, 512);
	}

	for (i = 0; i < STRINGS_NUM_5120; i++)
	{
		LoadString(hinstance, 5120 + i, lpResourceString5120[i].lpString, 512);
	}

	for (i = 0; i < STRINGS_NUM_5376; i++)
	{
		LoadString(hinstance, 5376 + i, lpResourceString5376[i].lpString, 512);
	}

	for (i = 0; i < STRINGS_NUM_5632; i++)
	{
		LoadString(hinstance, 5632 + i, lpResourceString5632[i].lpString, 512);
	}

	for (i = 0; i < STRINGS_NUM_6144; i++)
	{
		LoadString(hinstance, 6144 + i, lpResourceString6144[i].lpString, 512);
	}
}

LPTSTR win_language_get_settings_category(int i)
{
	return lpResourceString2048[17 + i].lpString;
}

void win_language_update()
{
	win_language_set();
	win_menu_update();
	win_language_load_common_strings();
}

void win_language_check()
{
	LCID dwLanguageNew = MAKELCID(dwLangID, dwSubLangID);
	if (dwLanguageNew != dwLanguage)
	{
		dwLanguage = dwLanguageNew;
		win_language_update();
	}
}

LPTSTR win_language_get_string_from_id(int i)
{
	if ((i >= 2048) && (i <= 3071))
	{
		return lpResourceString2048[i - 2048].lpString;
	}
	else if ((i >= 3072) && (i <= 4095))
	{
		return lpResourceString3072[i - 3072].lpString;
	}
	else if ((i >= 4096) && (i <= 4351))
	{
		return lpResourceString4096[i - 4096].lpString;
	}
	else if ((i >= 4352) && (i <= 4607))
	{
		return lpResourceString4352[i - 4352].lpString;
	}
	else if ((i >= 4608) && (i <= 5119))
	{
		return lpResourceString4608[i - 4608].lpString;
	}
	else if ((i >= 5120) && (i <= 5375))
	{
		return lpResourceString5120[i - 5120].lpString;
	}
	else if ((i >= 5376) && (i <= 5631))
	{
		return lpResourceString5376[i - 5376].lpString;
	}
	else if ((i >= 5632) && (i <= 6143))
	{
		return lpResourceString5632[i - 5632].lpString;
	}
	else
	{
		return lpResourceString6144[i - 6144].lpString;
	}
}

wchar_t *plat_get_string_from_id(int i)
{
	return (wchar_t *) win_language_get_string_from_id(i);
}

LPTSTR win_language_get_string_from_string(char *str)
{
	return win_language_get_string_from_id(atoi(str));
}

int msgbox_reset(HWND hwndParent)
{
	return MessageBox(hwndParent, lpResourceString2048[3].lpString, lpResourceString2048[0].lpString, MB_YESNOCANCEL | MB_ICONQUESTION);
}

int msgbox_reset_yn(HWND hwndParent)
{
	return MessageBox(hwndParent, lpResourceString2048[3].lpString, lpResourceString2048[0].lpString, MB_YESNO | MB_ICONQUESTION);
}

int msgbox_question(HWND hwndParent, int i)
{
	return MessageBox(hwndParent, win_language_get_string_from_id(i), lpResourceString2048[0].lpString, MB_YESNO | MB_ICONQUESTION);
}

void msgbox_info(HWND hwndParent, int i)
{
	MessageBox(hwndParent, win_language_get_string_from_id(i), lpResourceString2048[0].lpString, MB_OK | MB_ICONINFORMATION);
}

void msgbox_info_wstr(HWND hwndParent, WCHAR *wstr)
{
	MessageBox(hwndParent, wstr, lpResourceString2048[0].lpString, MB_OK | MB_ICONINFORMATION);
}

void msgbox_error(HWND hwndParent, int i)
{
	MessageBox(hwndParent, win_language_get_string_from_id(i), lpResourceString2048[1].lpString, MB_OK | MB_ICONWARNING);
}

void plat_msgbox_error(int i)
{
	msgbox_error(ghwnd, i);
}

void msgbox_error_wstr(HWND hwndParent, WCHAR *wstr)
{
	MessageBox(hwndParent, wstr, lpResourceString2048[1].lpString, MB_OK | MB_ICONWARNING);
}

void msgbox_critical(HWND hwndParent, int i)
{
	MessageBox(hwndParent, win_language_get_string_from_id(i), lpResourceString2048[2].lpString, MB_OK | MB_ICONERROR);
}

void msgbox_fatal(HWND hwndParent, char *string)
{
	LPTSTR lptsTemp;
	lptsTemp = (LPTSTR) malloc(512);

	mbstowcs(lptsTemp, string, strlen(string) + 1);

	MessageBox(hwndParent, lptsTemp, lpResourceString2048[2].lpString, MB_OK | MB_ICONERROR);

	free(lptsTemp);
}

void plat_msgbox_fatal(char *string)
{
	msgbox_fatal(ghwnd, string);
}

int file_dlg_w(HWND hwnd, WCHAR *f, WCHAR *fn, int save)
{
        OPENFILENAME ofn;       /* common dialog box structure */
        BOOL r;
        DWORD err;

        /* Initialize OPENFILENAME */
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = wopenfilestring;
        /*
           Set lpstrFile[0] to '\0' so that GetOpenFileName does not
           use the contents of szFile to initialize itself.
        */
	memcpy(ofn.lpstrFile, fn, (wcslen(fn) << 1) + 2);
        ofn.nMaxFile = 259;
        ofn.lpstrFilter = f;
        ofn.nFilterIndex = 1;
        ofn.lpstrFileTitle = NULL;
        ofn.nMaxFileTitle = 0;
        ofn.lpstrInitialDir = NULL;
        ofn.Flags = OFN_PATHMUSTEXIST;
	if (!save)
	{
		ofn.Flags |= OFN_FILEMUSTEXIST;
	}

        /* Display the Open dialog box. */

	if (save)
	{
	        pclog("GetSaveFileName - lpstrFile = %s\n", ofn.lpstrFile);
        	r = GetSaveFileName(&ofn);
	}
	else
	{
	        pclog("GetOpenFileName - lpstrFile = %s\n", ofn.lpstrFile);
       	r = GetOpenFileName(&ofn);
	}
        if (r)
        {
		wcstombs(openfilestring, wopenfilestring, sizeof(openfilestring));
                pclog("File dialog return true\n");
                return 0;
        }
        pclog("File dialog return false\n");
        err = CommDlgExtendedError();
        pclog("CommDlgExtendedError return %04X\n", err);
        return 1;
}

int file_dlg(HWND hwnd, WCHAR *f, char *fn, int save)
{
	WCHAR ufn[512];
	mbstowcs(ufn, fn, strlen(fn) + 1);
	return file_dlg_w(hwnd, f, ufn, save);
}

int file_dlg_mb(HWND hwnd, char *f, char *fn, int save)
{
	WCHAR uf[512];
	WCHAR ufn[512];
	mbstowcs(uf, f, strlen(fn) + 1);
	mbstowcs(ufn, fn, strlen(fn) + 1);
	return file_dlg_w(hwnd, uf, ufn, save);
}

int file_dlg_w_st(HWND hwnd, int i, WCHAR *fn, int save)
{
	return file_dlg_w(hwnd, win_language_get_string_from_id(i), fn, save);
}

int file_dlg_st(HWND hwnd, int i, char *fn, int save)
{
	return file_dlg(hwnd, win_language_get_string_from_id(i), fn, save);
}

static int CALLBACK BrowseCallbackProc(HWND hwnd,UINT uMsg, LPARAM lParam, LPARAM lpData)
{
	if(uMsg == BFFM_INITIALIZED)
	{
		SendMessage(hwnd, BFFM_SETSELECTION, TRUE, lpData);
	}

	return 0;
}

WCHAR path[MAX_PATH];

wchar_t *BrowseFolder(wchar_t *saved_path, wchar_t *title)
{
	BROWSEINFO bi = { 0 };
	bi.lpszTitle  = title;
	bi.ulFlags    = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	bi.lpfn       = BrowseCallbackProc;
	bi.lParam     = (LPARAM) saved_path;

	LPITEMIDLIST pidl = SHBrowseForFolder(&bi);

	if (pidl != 0)
	{
		/* Get the name of the folder and put it in path. */
		SHGetPathFromIDList(pidl, path);

		/* Free memory used. */
		IMalloc *imalloc = 0;
		if (SUCCEEDED(SHGetMalloc(&imalloc)))
		{
			imalloc->lpVtbl->Free(imalloc, pidl);
			imalloc->lpVtbl->Release(imalloc);
		}

		return path;
	}

	return L"";
}
