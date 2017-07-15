void* plat_midi_init();
void plat_midi_close(void* p);
void plat_midi_play_msg(struct midi_device_t* device, uint8_t* val);
void plat_midi_play_sysex(struct midi_device_t* device, uint8_t* data, unsigned int len);
int plat_midi_write(struct midi_device_t* device, uint8_t val);
int plat_midi_get_num_devs();
void plat_midi_get_dev_name(int num, char *s);
void plat_midi_add_status_info(char *s, int max_len, struct midi_device_t* device);
