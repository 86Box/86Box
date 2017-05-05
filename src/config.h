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
void append_filename(char *dest, char *s1, char *s2, int size);
void put_backslash(char *s);
char *get_extension(char *s);
wchar_t *get_extension_w(wchar_t *s);

void config_load(char *fn);
void config_save(char *fn);
void config_dump();
void config_free();

extern char config_file_default[256];
