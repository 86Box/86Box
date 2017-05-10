/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <inttypes.h>
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#undef BITMAP

#include <commdlg.h>

#include "ibm.h"
#include "device.h"
#include "ide.h"
#include "resource.h"
#include "win.h"
#include "win-language.h"

LCID dwLanguage;

uint32_t dwLangID, dwSubLangID;

#define STRINGS_NUM 152

WCHAR lpResourceString[STRINGS_NUM][512];

char openfilestring[260];
WCHAR wopenfilestring[260];

void win_language_set()
{
	SetThreadLocale(dwLanguage);
}

void win_language_load_common_strings()
{
	int i = 0;

	for (i = 0; i < STRINGS_NUM; i++)
	{
		LoadString(hinstance, 2048 + i, lpResourceString[i], 512);
	}
}

LPTSTR win_language_get_settings_category(int i)
{
	return lpResourceString[17 + i];
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
	return lpResourceString[i - 2048];
}

LPTSTR win_language_get_string_from_string(char *str)
{
	return lpResourceString[atoi(str) - 2048];
}

int msgbox_reset(HWND hwndParent)
{
	return MessageBox(hwndParent, lpResourceString[3], lpResourceString[0], MB_YESNOCANCEL | MB_ICONQUESTION);
}

int msgbox_reset_yn(HWND hwndParent)
{
	return MessageBox(hwndParent, lpResourceString[3], lpResourceString[0], MB_YESNO | MB_ICONQUESTION);
}

int msgbox_question(HWND hwndParent, int i)
{
	return MessageBox(hwndParent, win_language_get_string_from_id(i), lpResourceString[0], MB_YESNO | MB_ICONQUESTION);
}

void msgbox_info(HWND hwndParent, int i)
{
	MessageBox(hwndParent, win_language_get_string_from_id(i), lpResourceString[0], MB_OK | MB_ICONINFORMATION);
}

void msgbox_info_wstr(HWND hwndParent, WCHAR *wstr)
{
	MessageBox(hwndParent, wstr, lpResourceString[0], MB_OK | MB_ICONINFORMATION);
}

void msgbox_error(HWND hwndParent, int i)
{
	MessageBox(hwndParent, win_language_get_string_from_id(i), lpResourceString[1], MB_OK | MB_ICONWARNING);
}

void msgbox_error_wstr(HWND hwndParent, WCHAR *wstr)
{
	MessageBox(hwndParent, wstr, lpResourceString[1], MB_OK | MB_ICONWARNING);
}

void msgbox_critical(HWND hwndParent, int i)
{
	MessageBox(hwndParent, win_language_get_string_from_id(i), lpResourceString[2], MB_OK | MB_ICONERROR);
}

void msgbox_fatal(HWND hwndParent, char *string)
{
	LPTSTR lptsTemp;
	lptsTemp = (LPTSTR) malloc(512);

	mbstowcs(lptsTemp, string, strlen(string) + 1);

	MessageBox(hwndParent, lptsTemp, lpResourceString[2], MB_OK | MB_ICONERROR);

	free(lptsTemp);
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
		wcstombs(openfilestring, wopenfilestring, 520);
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

int file_dlg_w_st(HWND hwnd, int i, WCHAR *fn, int save)
{
	file_dlg_w(hwnd, win_language_get_string_from_id(i), fn, save);
}

int file_dlg_st(HWND hwnd, int i, char *fn, int save)
{
	file_dlg(hwnd, win_language_get_string_from_id(i), fn, save);
}
