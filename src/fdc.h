/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
void fdc_init();
void fdc_add();
void fdc_add_for_superio();
void fdc_add_pcjr();
void fdc_add_tandy();
void fdc_remove();
void fdc_reset();
void fdc_poll();
void fdc_abort();
void fdc_discchange_clear(int drive);
void fdc_set_dskchg_activelow();
void fdc_3f1_enable(int enable);
void fdc_set_ps1();
int fdc_get_bit_rate();
int fdc_get_bitcell_period();

/* A few functions to communicate between Super I/O chips and the FDC. */
void fdc_update_is_nsc(int is_nsc);
void fdc_update_max_track(int max_track);
void fdc_update_enh_mode(int enh_mode);
int fdc_get_rwc(int drive);
void fdc_update_rwc(int drive, int rwc);
int fdc_get_boot_drive();
void fdc_update_boot_drive(int boot_drive);
void fdc_update_densel_polarity(int densel_polarity);
uint8_t fdc_get_densel_polarity();
void fdc_update_densel_force(int densel_force);
void fdc_update_drvrate(int drive, int drvrate);
void fdc_update_drv2en(int drv2en);

int fdc_get_perp();
int fdc_get_format_n();
int fdc_is_mfm();
double fdc_get_hut();
double fdc_get_hlt();
void fdc_request_next_sector_id();
void fdc_stop_id_request();
int fdc_get_gap();
int fdc_get_dtl();
int fdc_get_format_sectors();

void fdc_finishread();
void fdc_sector_finishread();
