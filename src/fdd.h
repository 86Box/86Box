#define SEEK_RECALIBRATE -999
void fdd_seek(int drive, int track_diff);
int fdd_track0(int drive);
int fdd_getrpm(int drive);
void fdd_set_densel(int densel);
int fdd_can_read_medium(int drive);
int fdd_doublestep_40(int drive);
int fdd_is_525(int drive);
int fdd_is_ed(int drive);
void fdd_set_head(int drive, int head);
int fdd_get_head(int drive);

void fdd_set_type(int drive, int type);
int fdd_get_type(int drive);

int fdd_get_flags(int drive);

extern int fdd_swap;

void fdd_init();