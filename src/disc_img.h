/* Copyright holders: Sarah Walker, Kiririn
   see COPYING for more details
*/
void img_init();
void img_load(int drive, wchar_t *fn);
void img_close(int drive);
void img_seek(int drive, int track);
