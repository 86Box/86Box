/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for hardware monitoring chips.
 *
 *
 *
 * Author:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */
#ifndef EMU_HWM_H
# define EMU_HWM_H
# include <stdint.h>


#define RESISTOR_DIVIDER(v, r1, r2) (((v) * (r2)) / ((r1) + (r2)))


typedef struct {
    uint16_t	fans[4];
    uint8_t	temperatures[4];
    uint16_t	voltages[10];
} hwm_values_t;

typedef struct {
    uint32_t	 local;
    hwm_values_t *values;

    uint8_t	 regs[8];
    uint8_t	 addr_register;
    uint8_t	 temp_idx;
    uint8_t	 i2c_addr;

    uint8_t	 as99127f_i2c_addr;
} lm75_t;


extern uint16_t		hwm_get_vcore();

extern void		lm75_remap(lm75_t *dev, uint8_t addr);
extern uint8_t		lm75_read(lm75_t *dev, uint8_t reg);
extern uint8_t		lm75_write(lm75_t *dev, uint8_t reg, uint8_t val);

extern void		vt82c686_hwm_write(uint8_t addr, uint8_t val, void *priv);


/* Refer to specific hardware monitor implementations for the meaning of hwm_values. */
extern hwm_values_t	hwm_values;

extern const device_t	lm75_1_4a_device;
extern const device_t	lm75_w83781d_device;

extern const device_t	lm78_device;
extern const device_t	w83781d_device;
extern const device_t	as99127f_device;
extern const device_t	as99127f_rev2_device;
extern const device_t	w83782d_device;

extern const device_t	gl518sm_2c_device;
extern const device_t	gl518sm_2d_device;

extern const device_t	via_vt82c686_hwm_device;


#endif	/*EMU_HWM_H*/
