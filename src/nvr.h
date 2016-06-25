void nvr_init();

extern int nvr_dosave;
extern int enable_sync;

void time_sleep(int count);

void time_get(char *nvrram);
void nvr_add_10sec();

void update_sync();
