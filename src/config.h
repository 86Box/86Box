/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
int config_get_int(char *head, char *name, int def);
char *config_get_string(char *head, char *name, char *def);
void config_set_int(char *head, char *name, int val);
void config_set_string(char *head, char *name, char *val);

char *get_filename(char *s);
void append_filename(char *dest, char *s1, char *s2, int size);
void put_backslash(char *s);
char *get_extension(char *s);

void config_load(char *fn);
void config_save(char *fn);
void config_dump();
void config_free();

extern char config_file_default[256];
