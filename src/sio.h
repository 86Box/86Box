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
 * Version:	@(#)sio.h	1.0.6	2019/05/17
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef EMU_SIO_H
# define EMU_SIO_H


extern const device_t	acc3221_device;
extern const device_t	fdc37c663_device;
extern const device_t	fdc37c665_device;
extern const device_t	fdc37c666_device;
extern const device_t	fdc37c669_device;
extern const device_t	fdc37c932fr_device;
extern const device_t	fdc37c932qf_device;
extern const device_t	fdc37c935_device;
extern const device_t	pc87306_device;
extern const device_t	sio_detect_device;
extern const device_t	um8669f_device;
extern const device_t	w83877f_device;
extern const device_t	w83877f_president_device;
extern const device_t	w83877tf_device;


#endif	/*EMU_SIO_H*/
