/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the OPL interface.
 *
 * Version:	@(#)snd_opl.h	1.0.3	2020/07/15
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2017-2020 Fred N. van Kempen.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2008-2018 Sarah Walker.
 */
#ifndef SOUND_OPL_H
# define SOUND_OPL_H


typedef void	(*tmrfunc)(void *priv, int timer, uint64_t period);

/* Define an OPLx chip. */
typedef struct {
#ifdef SOUND_OPL_NUKED_H
    nuked_t	*opl;
#else
    void	*opl;
#endif
    int8_t	is_opl3, do_cycles;

    uint16_t	port;
    uint8_t	status;
    uint8_t	status_mask;
    uint8_t	timer_ctrl;
    uint16_t	timer_count[2],
		timer_cur_count[2];

    pc_timer_t	timers[2];

    int		pos;
    int32_t	buffer[SOUNDBUFLEN * 2];
} opl_t;


extern void	opl_set_do_cycles(opl_t *dev, int8_t do_cycles);

extern uint8_t	opl2_read(uint16_t port, void *);
extern void	opl2_write(uint16_t port, uint8_t val, void *);
extern void	opl2_init(opl_t *);
extern void	opl2_update(opl_t *);

extern uint8_t	opl3_read(uint16_t port, void *);
extern void	opl3_write(uint16_t port, uint8_t val, void *);
extern void	opl3_init(opl_t *);
extern void	opl3_update(opl_t *);


#endif	/*SOUND_OPL_H*/
