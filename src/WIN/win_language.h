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
 * Version:	@(#)win_language.h	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

#ifdef __cplusplus
extern "C" {
#endif

LCID dwLanguage;

int msgbox_reset(HWND hwndParent);
int msgbox_reset_yn(HWND hwndParent);
int msgbox_question(HWND hwndParent, int i);
void msgbox_info(HWND hwndParent, int i);
void msgbox_info_wstr(HWND hwndParent, WCHAR *wstr);
void msgbox_error(HWND hwndParent, int i);
void msgbox_error_wstr(HWND hwndParent, WCHAR *wstr);
void msgbox_fatal(HWND hwndParent, char *string);
void msgbox_critical(HWND hwndParent, int i);

int file_dlg_w(HWND hwnd, WCHAR *f, WCHAR *fn, int save);
int file_dlg(HWND hwnd, WCHAR *f, char *fn, int save);
int file_dlg_mb(HWND hwnd, char *f, char *fn, int save);
int file_dlg_w_st(HWND hwnd, int i, WCHAR *fn, int save);
int file_dlg_st(HWND hwnd, int i, char *fn, int save);

void win_language_load_common_strings();
LPTSTR win_language_get_settings_category(int i);

void win_language_update();
void win_language_check();

LPTSTR win_language_get_string_from_id(int i);
LPTSTR win_language_get_string_from_string(char *str);

wchar_t *BrowseFolder(wchar_t *saved_path, wchar_t *title);

#ifdef __cplusplus
}
#endif
