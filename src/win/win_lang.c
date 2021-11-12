/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handle the dialog for specifying the dimensions of the main window.
 *
 *
 *
 * Authors:	Laci bá'
 *
 *		Copyright 2021 Laci bá'
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

/* Language */
static LCID temp_language;

int enum_helper, c;

BOOL CALLBACK 
EnumResLangProc(HMODULE hModule, LPCTSTR lpszType, LPCTSTR lpszName, WORD wIDLanguage, LONG_PTR lParam)
{
	wchar_t temp[LOCALE_NAME_MAX_LENGTH + 1];
	LCIDToLocaleName(wIDLanguage, temp, LOCALE_NAME_MAX_LENGTH, 0);
	wchar_t dispname[MAX_PATH + 1];
	GetLocaleInfoEx(temp, LOCALE_SENGLISHDISPLAYNAME, dispname, MAX_PATH);
	SendMessage((HWND)lParam, CB_ADDSTRING, 0, (LPARAM)dispname);
	SendMessage((HWND)lParam, CB_SETITEMDATA, c, (LPARAM)wIDLanguage);
	
	pclog("widl: %u, langid: %u, c: %u\n", wIDLanguage, lang_id, c);
	if (wIDLanguage == lang_id)
		enum_helper = c;
	c++;
	
	return 1;
}

/* Load available languages */
static void
progsett_fill_languages(HWND hdlg)
{
	temp_language = GetThreadUILanguage();
	HWND lang_combo = GetDlgItem(hdlg, IDC_COMBO_LANG); 
	
	SendMessage(lang_combo, CB_RESETCONTENT, 0, 0);
	
	enum_helper = -1; c = 0;
	EnumResourceLanguages(hinstance, RT_MENU, L"MainMenu", &EnumResLangProc, (LPARAM)lang_combo);
	pclog("enum_helper is %d\n", enum_helper);
	
	SendMessage(lang_combo, CB_SETCURSEL, enum_helper, 0);
	pclog("win_fill_languages\n");
}

/* This returns 1 if any variable has changed, 0 if not. */
static int
progsett_settings_changed(void)
{
	int i = 0;
	
    /* Language */
    i = i || has_language_changed(temp_language);
	
	return i;
}

/* This saves the settings back to the global variables. */
static void
progsett_settings_save(void)
{
    /* Language */
    set_language(temp_language);
}

#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
ProgSettDlgProcedure(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
	case WM_INITDIALOG:
	    /* Language */
		temp_language = lang_id;
		pclog("temp_language is %u\n", lang_id);
		progsett_fill_languages(hdlg);
		break;

	case WM_COMMAND:
                switch (LOWORD(wParam)) {
			case IDOK:
			    if (progsett_settings_changed())
					progsett_settings_save();
				EndDialog(hdlg, 0);
				return TRUE;

			case IDCANCEL:
				EndDialog(hdlg, 0);
				return TRUE;
				
			case IDC_COMBO_LANG:
				if (HIWORD(wParam) == CBN_SELCHANGE) {
					HWND combo = GetDlgItem(hdlg, IDC_COMBO_LANG);
					int index = SendMessage(combo, CB_GETCURSEL, 0, 0); 
					temp_language = SendMessage(combo, CB_GETITEMDATA, index, 0);
					pclog("combobox changed -> temp_language = %u", temp_language);
				}
			default:
				break;
		}
		break;
    }

    return(FALSE);
}


void
ProgSettDlgCreate(HWND hwnd)
{
    DialogBox(hinstance, (LPCTSTR)DLG_PROG_SETT, hwnd, ProgSettDlgProcedure);
}
