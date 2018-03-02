void nvr_tc8521_init();

extern int enable_sync;

extern int nvr_dosave;

void tc8521_loadnvr();
void tc8521_savenvr();

void tc8521_nvr_recalc();

FILE *nvrfopen(char *fn, char *mode);
