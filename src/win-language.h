int msgbox_reset(HWND hwndParent);
int msgbox_reset_yn(HWND hwndParent);
int msgbox_question(HWND hwndParent, int i);
void msgbox_info(HWND hwndParent, int i);
void msgbox_error(HWND hwndParent, int i);
void msgbox_fatal(HWND hwndParent, char *string);
void msgbox_critical(HWND hwndParent, int i);

int file_dlg(HWND hwnd, WCHAR *f, char *fn, int save);
int file_dlg_st(HWND hwnd, int i, char *fn, int save);

void win_language_load_common_strings();
LPTSTR win_language_get_settings_category(int i);

void win_language_update();
void win_language_check();

LPTSTR win_language_get_string_from_id(int i);
LPTSTR win_language_get_string_from_string(char *str);
