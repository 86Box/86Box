/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		A better random number generation, used for floppy weak bits
 *		and network MAC address generation.
 *
 * Version:	@(#)disc_random.h	1.0.1	2017/08/23
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2016,2017 Miran Grca.
 */
#ifndef EMU_DISC_RANDOM_H
# define EMU_DISC_RANDOM_H


extern uint8_t disc_random_generate(void);
extern void disc_random_init(void);


#endif	/*EMU_DISC_RANDOM_H*/
