/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the Super I/O chips.
 *
 * Version:	@(#)sio.h	1.0.3	2018/09/15
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef EMU_SIO_H
# define EMU_SIO_H


extern void	superio_detect_init(void);
extern void	fdc37c663_init(void);
extern void	fdc37c665_init(void);
extern void	fdc37c669_init(void);
extern void	fdc37c932fr_init(void);
extern void	fdc37c935_init(void);
extern void	pc87306_init(void);
extern void	um8669f_init(void);
extern void	w83877f_init(uint8_t reg16init);


#endif	/*EMU_SIO_H*/
