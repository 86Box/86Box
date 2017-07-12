char *hdd_controller_get_name(int hdd);
char *hdd_controller_get_internal_name(int hdd);
int hdd_controller_get_flags(int hdd);
int hdd_controller_available(int hdd);
int hdd_controller_current_is_mfm();
void hdd_controller_init(char *internal_name);

extern char hdd_controller_name[16];
