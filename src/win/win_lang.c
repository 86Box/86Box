/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handle the dialog for changing the program's language.
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
#include <86box/ui.h>
#include <86box/resource.h>

/* Language */
static LCID temp_language;

static char temp_icon_set[256] = {0};

int enum_helper, c;

HWND hwndProgSett;

BOOL CALLBACK 
EnumResLangProc(HMODULE hModule, LPCTSTR lpszType, LPCTSTR lpszName, WORD wIDLanguage, LONG_PTR lParam)
{
	wchar_t temp[LOCALE_NAME_MAX_LENGTH + 1];
	LCIDToLocaleName(wIDLanguage, temp, LOCALE_NAME_MAX_LENGTH, 0);
	wchar_t dispname[MAX_PATH + 1];
	GetLocaleInfoEx(temp, LOCALE_SENGLISHDISPLAYNAME, dispname, MAX_PATH);
	SendMessage((HWND)lParam, CB_ADDSTRING, 0, (LPARAM)dispname);
	SendMessage((HWND)lParam, CB_SETITEMDATA, c, (LPARAM)wIDLanguage);
	
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
	SendMessage(lang_combo, CB_ADDSTRING, 0, win_get_string(IDS_7168));
	SendMessage(lang_combo, CB_SETITEMDATA, 0, 0xFFFF);
	
	enum_helper = 0; c = 1; 
	//if no one is selected, then it was 0xFFFF or unsupported language, in either case go with index enum_helper=0
	//also start enum index from c=1
	EnumResourceLanguages(hinstance, RT_MENU, L"MainMenu", &EnumResLangProc, (LPARAM)lang_combo);
	
	SendMessage(lang_combo, CB_SETCURSEL, enum_helper, 0);
}

/* Load available iconsets */
static void
progsett_fill_iconsets(HWND hdlg)
{
	HWND icon_combo = GetDlgItem(hdlg, IDC_COMBO_ICON); 
	
	/* Add the default one */
	wchar_t buffer[512] = L"(";
	wcscat(buffer, plat_get_string(IDS_2090));
	wcscat(buffer, L")");
	
	SendMessage(icon_combo, CB_RESETCONTENT, 0, 0);
	SendMessage(icon_combo, CB_ADDSTRING, 0, (LPARAM)buffer);
	SendMessage(icon_combo, CB_SETITEMDATA, 0, (LPARAM)strdup(""));
	
	int combo_index = -1;
	
	/* Find for extra ones */
	HANDLE hFind;
	WIN32_FIND_DATA data;
	
	char icon_path_root[512];
	plat_get_icons_path(icon_path_root);
	
	wchar_t search[512];
	pclog("icon_path_root: %s\n", icon_path_root);
	mbstowcs(search, icon_path_root, strlen(icon_path_root) + 1);
	wcscat(search, L"*.*");
	pclog("search: %ls\n", search);
	
	hFind = FindFirstFile((LPCWSTR)search, &data);
	
	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			if (wcscmp(data.cFileName, L".") && wcscmp(data.cFileName, L"..") && 
			  (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				wchar_t temp[512] = {0}, dispname[512] = {0};
				mbstowcs(temp, icon_path_root, strlen(icon_path_root) + 1);
				wcscat(temp, data.cFileName);
				wcscat(temp, L"\\iconinfo.txt");
				
				pclog("temp: %ls\n", temp);
				
				wcscpy(dispname, data.cFileName);
				FILE *fp = _wfopen(temp, L"r");
				if (fp)
				{
					char line[512];
					if (fgets(line, 511, fp))
					{
						pclog("found! %s\n", line);
						mbstowcs(dispname, line, strlen(line) + 1);
					}
					
					fclose(fp);
				}
				
				char filename[512];
				wcstombs(filename, data.cFileName, 511);
				
				int index = SendMessage(icon_combo, CB_ADDSTRING, 0, (LPARAM)dispname);
				SendMessage(icon_combo, CB_SETITEMDATA, index, (LPARAM)(strdup(filename)));
				
				if (!strcmp(filename, icon_set))
					combo_index = index;
			}
		} while (FindNextFile(hFind, &data));
		FindClose(hFind);
	}
	
	if (combo_index == -1)
	{
		combo_index = 0;
		strcpy(temp_icon_set, "");
	}
	
	SendMessage(icon_combo, CB_SETCURSEL, combo_index, 0);
}

/* This returns 1 if any variable has changed, 0 if not. */
static int
progsett_settings_changed(void)
{
	int i = 0;
	
    /* Language */
    i = i || has_language_changed(temp_language);
	i = i || strcmp(temp_icon_set, icon_set);
	
	return i;
}

/* IndexOf by ItemData */
static int 
progsett_indexof(HWND combo, LPARAM itemdata)
{
	int i;
	for (i = 0; i < SendMessage(combo, CB_GETCOUNT, 0, 0); i++)
		if (SendMessage(combo, CB_GETITEMDATA, i, 0) == itemdata)
			return i;
	
	return -1;
}

/* This saves the settings back to the global variables. */
static void
progsett_settings_save(void)
{	
    /* Language */
    set_language(temp_language);

	/* Iconset */
	strcpy(icon_set, temp_icon_set);
	plat_load_icon_set(hinstance);

    /* Update title bar */
	update_mouse_msg();
	
	/* Update status bar */
	config_changed = 1;	
	ui_sb_set_ready(0);
	ui_sb_update_panes();
	
	/* Save the language changes */
	config_save();
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
	    hwndProgSett = hdlg;
	    /* Language */
		temp_language = lang_id;
		strcpy(temp_icon_set, icon_set);
		pclog("temp_icon_set: %s\n", temp_icon_set);
		progsett_fill_languages(hdlg);
		progsett_fill_iconsets(hdlg);
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
				}
				break; 
				
			case IDC_COMBO_ICON:
				if (HIWORD(wParam) == CBN_SELCHANGE) {
					pclog("dosth\n");
					HWND combo = GetDlgItem(hdlg, IDC_COMBO_ICON);
					int index = SendMessage(combo, CB_GETCURSEL, 0, 0); 
					strcpy(temp_icon_set, (char*)SendMessage(combo, CB_GETITEMDATA, index, 0));
					pclog("temp_icon_set: %s\n", temp_icon_set);
				}
				break; 
				
			case IDC_BUTTON_DEFAULT: {
				HWND combo = GetDlgItem(hdlg, IDC_COMBO_LANG);
				int index = progsett_indexof(combo, DEFAULT_LANGUAGE);
				SendMessage(combo, CB_SETCURSEL, index, 0); 
				temp_language = DEFAULT_LANGUAGE;
				break; 
			}
		
			case IDC_BUTTON_DEFICON: {
				pclog("dosth\n");
				SendMessage(GetDlgItem(hdlg, IDC_COMBO_ICON), CB_SETCURSEL, 0, 0); 
				strcpy(temp_icon_set, "");
				pclog("temp_icon_set: %s\n", temp_icon_set);
				break; 
			}
			default:
				break;
		}
		break;
		
	case WM_DESTROY: {
			int i;
			LRESULT temp;
			HWND combo = GetDlgItem(hdlg, IDC_COMBO_ICON);
			for (i = 0; i < SendMessage(combo, CB_GETCOUNT, 0, 0); i++)
			{
				temp = SendMessage(combo, CB_GETITEMDATA, i, 0);
				if (temp)
				{
					free((void*)temp);
					SendMessage(combo, CB_SETITEMDATA, i, 0);
				}
			}
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
