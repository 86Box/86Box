/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
int config_get_int(char *head, char *name, int def);
char *config_get_string(char *head, char *name, char *def);
wchar_t *config_get_wstring(char *head, char *name, wchar_t *def);
void config_set_int(char *head, char *name, int val);
void config_set_string(char *head, char *name, char *val);
void config_set_wstring(char *head, char *name, wchar_t *val);

char *get_filename(char *s);
wchar_t *get_filename_w(wchar_t *s);
void append_filename(char *dest, char *s1, char *s2, int size);
void append_filename_w(wchar_t *dest, wchar_t *s1, wchar_t *s2, int size);
void put_backslash(char *s);
void put_backslash_w(wchar_t *s);
char *get_extension(char *s);
wchar_t *get_extension_w(wchar_t *s);

void config_load(wchar_t *fn);
void config_save(wchar_t *fn);
void config_dump(void);
void config_free(void);

extern wchar_t config_file_default[256];
