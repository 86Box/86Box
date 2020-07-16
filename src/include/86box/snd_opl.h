/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
typedef struct opl_t
{
    int		pos, chip_nr[2];

    int32_t	buffer[SOUNDBUFLEN * 2],
		buffer2[SOUNDBUFLEN * 2];

    pc_timer_t	timers[2][2];
} opl_t;


extern uint8_t	opl2_read(uint16_t a, void *priv);
extern void	opl2_write(uint16_t a, uint8_t v, void *priv);
extern uint8_t	opl2_l_read(uint16_t a, void *priv);
extern void	opl2_l_write(uint16_t a, uint8_t v, void *priv);
extern uint8_t	opl2_r_read(uint16_t a, void *priv);
extern void	opl2_r_write(uint16_t a, uint8_t v, void *priv);
extern uint8_t	opl3_read(uint16_t a, void *priv);
extern void	opl3_write(uint16_t a, uint8_t v, void *priv);

extern void	opl2_poll(opl_t *opl, int16_t *bufl, int16_t *bufr);
extern void	opl3_poll(opl_t *opl, int16_t *bufl, int16_t *bufr);

extern void	opl2_init(opl_t *opl);
extern void	opl3_init(opl_t *opl);

extern void	opl2_update2(opl_t *opl);
extern void	opl3_update2(opl_t *opl);
