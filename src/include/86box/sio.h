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
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2017-2020 Fred N. van Kempen.
 */
#ifndef EMU_SIO_H
# define EMU_SIO_H


extern void		vt82c686_sio_write(uint8_t addr, uint8_t val, void *priv);


extern const device_t	acc3221_device;
extern const device_t	f82c710_device;
extern const device_t	fdc37c661_device;
extern const device_t	fdc37c663_device;
extern const device_t	fdc37c665_device;
extern const device_t	fdc37c666_device;
extern const device_t	fdc37c669_device;
extern const device_t	fdc37c931apm_device;
extern const device_t	fdc37c932fr_device;
extern const device_t	fdc37c932qf_device;
extern const device_t	fdc37c935_device;
extern const device_t	i82091aa_device;
extern const device_t	i82091aa_ide_device;
extern const device_t	pc87306_device;
extern const device_t	pc87307_device;
extern const device_t	pc87307_15c_device;
extern const device_t	pc87309_device;
extern const device_t	pc87309_15c_device;
extern const device_t	pc87332_device;
extern const device_t	pc87332_ps1_device;
extern const device_t	pc97307_device;
extern const device_t 	ps1_m2133_sio;
extern const device_t	sio_detect_device;
extern const device_t	um8669f_device;
extern const device_t	via_vt82c686_sio_device;
extern const device_t	w83787f_device;
extern const device_t	w83877f_device;
extern const device_t	w83877f_president_device;
extern const device_t	w83877tf_device;
extern const device_t	w83877tf_acorp_device;
extern const device_t	w83977f_device;
extern const device_t	w83977f_370_device;
extern const device_t	w83977tf_device;
extern const device_t	w83977ef_device;
extern const device_t	w83977ef_370_device;


#endif	/*EMU_SIO_H*/
