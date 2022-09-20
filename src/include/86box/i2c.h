/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the I2C handler.
 *
 *
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */
#ifndef EMU_I2C_H
#define EMU_I2C_H

/* i2c.c */
extern void *i2c_smbus;

/* i2c.c */
extern void *i2c_addbus(char *name);
extern void  i2c_removebus(void *bus_handle);
extern char *i2c_getbusname(void *bus_handle);

extern void i2c_sethandler(void *bus_handle, uint8_t base, int size,
                           uint8_t (*start)(void *bus, uint8_t addr, uint8_t read, void *priv),
                           uint8_t (*read)(void *bus, uint8_t addr, void *priv),
                           uint8_t (*write)(void *bus, uint8_t addr, uint8_t data, void *priv),
                           void (*stop)(void *bus, uint8_t addr, void *priv),
                           void *priv);

extern void i2c_removehandler(void *bus_handle, uint8_t base, int size,
                              uint8_t (*start)(void *bus, uint8_t addr, uint8_t read, void *priv),
                              uint8_t (*read)(void *bus, uint8_t addr, void *priv),
                              uint8_t (*write)(void *bus, uint8_t addr, uint8_t data, void *priv),
                              void (*stop)(void *bus, uint8_t addr, void *priv),
                              void *priv);

extern void i2c_handler(int set, void *bus_handle, uint8_t base, int size,
                        uint8_t (*start)(void *bus, uint8_t addr, uint8_t read, void *priv),
                        uint8_t (*read)(void *bus, uint8_t addr, void *priv),
                        uint8_t (*write)(void *bus, uint8_t addr, uint8_t data, void *priv),
                        void (*stop)(void *bus, uint8_t addr, void *priv),
                        void *priv);

extern uint8_t i2c_start(void *bus_handle, uint8_t addr, uint8_t read);
extern uint8_t i2c_read(void *bus_handle, uint8_t addr);
extern uint8_t i2c_write(void *bus_handle, uint8_t addr, uint8_t data);
extern void    i2c_stop(void *bus_handle, uint8_t addr);

/* i2c_eeprom.c */
extern uint8_t log2i(uint32_t i);
extern void   *i2c_eeprom_init(void *i2c, uint8_t addr, uint8_t *data, uint32_t size, uint8_t writable);
extern void    i2c_eeprom_close(void *dev_handle);

/* i2c_gpio.c */
extern void   *i2c_gpio_init(char *bus_name);
extern void    i2c_gpio_close(void *dev_handle);
extern void    i2c_gpio_set(void *dev_handle, uint8_t scl, uint8_t sda);
extern uint8_t i2c_gpio_get_scl(void *dev_handle);
extern uint8_t i2c_gpio_get_sda(void *dev_handle);
extern void   *i2c_gpio_get_bus();

#endif /*EMU_I2C_H*/
