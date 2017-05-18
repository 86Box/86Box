/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
void midi_init();
void midi_close();
void midi_write(uint8_t val);
int midi_get_num_devs();
void midi_get_dev_name(int num, char *s);

extern int midi_id;
