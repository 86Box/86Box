/* Copyright holders: Mahod, Tenshi
   see COPYING for more details
*/
void nvr_init();

extern int enable_sync;

extern int nvr_dosave;

void time_get(char *nvrram);

void nvr_recalc();

void loadnvr();
void savenvr();
