extern void	plat_midi_init(void);
extern void	plat_midi_close(void);

extern void	plat_midi_play_msg(uint8_t *msg);
extern void	plat_midi_play_sysex(uint8_t *sysex, unsigned int len);
extern int	plat_midi_write(uint8_t val);

extern int	plat_midi_get_num_devs(void);
extern void	plat_midi_get_dev_name(int num, char *s);

extern void plat_midi_input_init(void);
extern void plat_midi_input_close(void);

extern int plat_midi_in_get_num_devs(void);
extern void plat_midi_in_get_dev_name(int num, char *s);
