/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Header of the implementation of the Intel PIC chip emulation,
 *		partially ported from reenigne's XTCE.
 *
 * Authors:	Andrew Jenner, <https://www.reenigne.org>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2015-2020 Andrew Jenner.
 *		Copyright 2016-2020 Miran Grca.
 */
#ifndef EMU_PIC_H
# define EMU_PIC_H


typedef struct pic {
    uint8_t	icw1, icw2, icw3, icw4,
		imr, isr, irr, ocw2,
		ocw3, int_pending, is_master, elcr,
		state, ack_bytes, priority, special_mask_mode,
		auto_eoi_rotate, interrupt, lines, data_bus;
    struct pic	*slaves[8];
} pic_t;


extern pic_t	pic, pic2;


extern int	pic_elcr_get_enabled(void);
extern void	pic_elcr_set_enabled(int enabled);
extern void	pic_elcr_io_handler(int set);
extern void	pic_elcr_write(uint16_t port, uint8_t val, void *priv);
extern uint8_t	pic_elcr_read(uint16_t port, void *priv);

extern void	pic_set_shadow(int sh);
extern void	pic_init(void);
extern void	pic_init_pcjr(void);
extern void	pic2_init(void);
extern void	pic_reset(void);

extern int	picint_is_level(int irq);
extern void	picint_common(uint16_t num, int level, int set);
extern void	picint(uint16_t num);
extern void	picintlevel(uint16_t num);
extern void	picintc(uint16_t num);
extern int	picinterrupt(void);

extern uint8_t	pic_irq_ack(void);


#endif	/*EMU_PIC_H*/
