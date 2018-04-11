extern void	plat_midi_init(void);
extern void	plat_midi_close(void);

extern void	plat_midi_play_msg(uint8_t* val);
extern void	plat_midi_play_sysex(uint8_t* data, unsigned int len);
extern int	plat_midi_write(uint8_t val);
extern int	plat_midi_get_num_devs();
extern void	plat_midi_get_dev_name(int num, char *s);
