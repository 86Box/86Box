/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for clock generator chips.
 *
 *
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */
#ifndef EMU_CLOCK_H
# define EMU_CLOCK_H

/* clock_ics9xxx.c */
enum {
    ICS9xxx_xx,
    ICS9150_08,
    ICS9248_39,
    ICS9248_81,
    ICS9248_95,
    ICS9248_98,
    ICS9248_101,
    ICS9248_103,
    ICS9248_107,
    ICS9248_112,
    ICS9248_138,
    ICS9248_141,
    ICS9248_143,
    ICS9248_151,
    ICS9248_192,
    ICS9250_08,
    ICS9250_10,
    ICS9250_13,
    ICS9250_14,
    ICS9250_16,
    ICS9250_18,
    ICS9250_19,
    ICS9250_23,
    ICS9250_25,
    ICS9250_26,
    ICS9250_27,
    ICS9250_28,
    ICS9250_29,
    ICS9250_30,
    ICS9250_32,
    ICS9250_38,
    ICS9250_50,
    ICS9xxx_MAX
};


/* clock_ics9xxx.c */
extern device_t	*ics9xxx_get(uint8_t model);


#endif	/*EMU_CLOCK_H*/
