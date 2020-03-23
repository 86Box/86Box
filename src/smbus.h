/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the SMBus handler.
 *
 * Version:	@(#)smbus.h	1.0.0	2020/03/21
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */
#ifndef EMU_SMBUS_H
# define EMU_SMBUS_H


extern void	smbus_init(void);

extern void	smbus_sethandler(uint8_t base, int size,
    			uint8_t (*read_byte)(uint8_t addr, void *priv),
    			uint8_t (*read_byte_cmd)(uint8_t addr, uint8_t cmd, void *priv),
    			uint16_t (*read_word_cmd)(uint8_t addr, uint8_t cmd, void *priv),
    			uint8_t (*read_block_cmd)(uint8_t addr, uint8_t cmd, uint8_t *data, void *priv),
    			void (*write_byte)(uint8_t addr, uint8_t val, void *priv),
    			void (*write_byte_cmd)(uint8_t addr, uint8_t cmd, uint8_t val, void *priv),
    			void (*write_word_cmd)(uint8_t addr, uint8_t cmd, uint16_t val, void *priv),
    			void (*write_block_cmd)(uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv),
    			void *priv);

extern void	smbus_removehandler(uint8_t base, int size,
    			uint8_t (*read_byte)(uint8_t addr, void *priv),
    			uint8_t (*read_byte_cmd)(uint8_t addr, uint8_t cmd, void *priv),
    			uint16_t (*read_word_cmd)(uint8_t addr, uint8_t cmd, void *priv),
    			uint8_t (*read_block_cmd)(uint8_t addr, uint8_t cmd, uint8_t *data, void *priv),
    			void (*write_byte)(uint8_t addr, uint8_t val, void *priv),
    			void (*write_byte_cmd)(uint8_t addr, uint8_t cmd, uint8_t val, void *priv),
    			void (*write_word_cmd)(uint8_t addr, uint8_t cmd, uint16_t val, void *priv),
    			void (*write_block_cmd)(uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv),
    			void *priv);

extern void	smbus_handler(int set, uint8_t base, int size,
    			uint8_t (*read_byte)(uint8_t addr, void *priv),
    			uint8_t (*read_byte_cmd)(uint8_t addr, uint8_t cmd, void *priv),
    			uint16_t (*read_word_cmd)(uint8_t addr, uint8_t cmd, void *priv),
    			uint8_t (*read_block_cmd)(uint8_t addr, uint8_t cmd, uint8_t *data, void *priv),
    			void (*write_byte)(uint8_t addr, uint8_t val, void *priv),
    			void (*write_byte_cmd)(uint8_t addr, uint8_t cmd, uint8_t val, void *priv),
    			void (*write_word_cmd)(uint8_t addr, uint8_t cmd, uint16_t val, void *priv),
    			void (*write_block_cmd)(uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv),
    			void *priv);

extern uint8_t	smbus_has_device(uint8_t addr);
extern uint8_t	smbus_read_byte(uint8_t addr);
extern uint8_t	smbus_read_byte_cmd(uint8_t addr, uint8_t cmd);
extern uint16_t	smbus_read_word_cmd(uint8_t addr, uint8_t cmd);
extern uint8_t	smbus_read_block_cmd(uint8_t addr, uint8_t cmd, uint8_t *data);
extern void	smbus_write_byte(uint8_t addr, uint8_t val);
extern void	smbus_write_byte_cmd(uint8_t addr, uint8_t cmd, uint8_t val);
extern void	smbus_write_word_cmd(uint8_t addr, uint8_t cmd, uint16_t val);
extern void	smbus_write_block_cmd(uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len);


#endif	/*EMU_SMBUS_H*/
