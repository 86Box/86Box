/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the hardware monitor chips.
 *
 *
 *
 * Author:	RichardG, <richardg867@gmail.com>
 *		Copyright 2020 RichardG.
 */
#ifndef EMU_HWM_H
# define EMU_HWM_H


#define RESISTOR_DIVIDER(v, r1, r2) (((v) * (r2)) / ((r1) + (r2)))


typedef struct _hwm_values_ {
    uint16_t	fans[4];
    uint8_t		temperatures[4];
    uint16_t	voltages[8];
} hwm_values_t;


extern void		hwm_set_values(hwm_values_t new_values);
extern hwm_values_t*	hwm_get_values();


extern const device_t	w83781d_device;
extern const device_t	as99127f_device;


#endif	/*EMU_HWM_H*/
