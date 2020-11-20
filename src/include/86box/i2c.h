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
# define EMU_I2C_H


/* i2c.c */
extern void	*i2c_smbus;


/* i2c.c */
extern void	*i2c_addbus(char *name);
extern void	i2c_removebus(void *bus_handle);

extern void	i2c_sethandler(void *bus_handle, uint8_t base, int size,
			       void (*read_quick)(void *bus, uint8_t addr, void *priv),
			       uint8_t (*read_byte)(void *bus, uint8_t addr, void *priv),
			       uint8_t (*read_byte_cmd)(void *bus, uint8_t addr, uint8_t cmd, void *priv),
			       uint16_t (*read_word_cmd)(void *bus, uint8_t addr, uint8_t cmd, void *priv),
			       uint8_t (*read_block_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv),
			       void (*write_quick)(void *bus, uint8_t addr, void *priv),
			       void (*write_byte)(void *bus, uint8_t addr, uint8_t val, void *priv),
			       void (*write_byte_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t val, void *priv),
			       void (*write_word_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint16_t val, void *priv),
			       void (*write_block_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv),
			       void *priv);

extern void	i2c_removehandler(void *bus_handle, uint8_t base, int size,
				  void (*read_quick)(void *bus, uint8_t addr, void *priv),
				  uint8_t (*read_byte)(void *bus, uint8_t addr, void *priv),
				  uint8_t (*read_byte_cmd)(void *bus, uint8_t addr, uint8_t cmd, void *priv),
				  uint16_t (*read_word_cmd)(void *bus, uint8_t addr, uint8_t cmd, void *priv),
				  uint8_t (*read_block_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv),
				  void (*write_quick)(void *bus, uint8_t addr, void *priv),
				  void (*write_byte)(void *bus, uint8_t addr, uint8_t val, void *priv),
				  void (*write_byte_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t val, void *priv),
				  void (*write_word_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint16_t val, void *priv),
				  void (*write_block_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv),
				  void *priv);

extern void	i2c_handler(int set, void *bus_handle, uint8_t base, int size,
			    void (*read_quick)(void *bus, uint8_t addr, void *priv),
			    uint8_t (*read_byte)(void *bus, uint8_t addr, void *priv),
			    uint8_t (*read_byte_cmd)(void *bus, uint8_t addr, uint8_t cmd, void *priv),
			    uint16_t (*read_word_cmd)(void *bus, uint8_t addr, uint8_t cmd, void *priv),
			    uint8_t (*read_block_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv),
			    void (*write_quick)(void *bus, uint8_t addr, void *priv),
			    void (*write_byte)(void *bus, uint8_t addr, uint8_t val, void *priv),
			    void (*write_byte_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t val, void *priv),
			    void (*write_word_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint16_t val, void *priv),
			    void (*write_block_cmd)(void *bus, uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv),
			    void *priv);

extern uint8_t	i2c_has_device(void *bus_handle, uint8_t addr);
extern void	i2c_read_quick(void *bus_handle, uint8_t addr);
extern uint8_t	i2c_read_byte(void *bus_handle, uint8_t addr);
extern uint8_t	i2c_read_byte_cmd(void *bus_handle, uint8_t addr, uint8_t cmd);
extern uint16_t	i2c_read_word_cmd(void *bus_handle, uint8_t addr, uint8_t cmd);
extern uint8_t	i2c_read_block_cmd(void *bus_handle, uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len);
extern void	i2c_write_quick(void *bus_handle, uint8_t addr);
extern void	i2c_write_byte(void *bus_handle, uint8_t addr, uint8_t val);
extern void	i2c_write_byte_cmd(void *bus_handle, uint8_t addr, uint8_t cmd, uint8_t val);
extern void	i2c_write_word_cmd(void *bus_handle, uint8_t addr, uint8_t cmd, uint16_t val);
extern void	i2c_write_block_cmd(void *bus_handle, uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len);

/* i2c_gpio.c */
extern void	*i2c_gpio_init(char *bus_name);
extern void	i2c_gpio_close(void *dev_handle);
extern void	i2c_gpio_set(void *dev_handle, uint8_t scl, uint8_t sda);
extern uint8_t	i2c_gpio_get_scl(void *dev_handle);
extern uint8_t	i2c_gpio_get_sda(void *dev_handle);
extern void	*i2c_gpio_get_bus();


#endif	/*EMU_I2C_H*/
