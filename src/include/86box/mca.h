#ifndef EMU_MCA_H
# define EMU_MCA_H

extern void mca_init(int nr_cards);
extern void mca_add(uint8_t (*read)(int addr, void *priv), void (*write)(int addr, uint8_t val, void *priv), uint8_t (*feedb)(void *priv), void (*reset)(void *priv), void *priv);
extern void mca_set_index(int index);
extern uint8_t mca_read(uint16_t port);
extern uint8_t mca_read_index(uint16_t port, int index);
extern void mca_write(uint16_t port, uint8_t val);
extern uint8_t mca_feedb(void);
extern int mca_get_nr_cards(void);
extern void mca_reset(void);

extern void ps2_cache_clean(void);

#endif /*EMU_MCA_H*/
