/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
void img_init();
void img_load(int drive, char *fn);
void img_close(int drive);
void img_seek(int drive, int track);
void img_readsector(int drive, int sector, int track, int side, int density);
void img_writesector(int drive, int sector, int track, int side, int density);
void img_readaddress(int drive, int sector, int side, int density);
void img_format(int drive, int sector, int side, int density);
int img_hole(int drive);
int img_byteperiod(int drive);
void img_stop(int drive);
void img_poll();
int img_realtrack(int track, int drive);
