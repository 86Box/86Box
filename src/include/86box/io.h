/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the I/O handler.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#ifndef EMU_IO_H
# define EMU_IO_H


extern void	io_init(void);

extern void	io_sethandler_common(uint16_t base, int size,
			uint8_t (*inb)(uint16_t addr, void *priv),
			uint16_t (*inw)(uint16_t addr, void *priv),
			uint32_t (*inl)(uint16_t addr, void *priv),
			void (*outb)(uint16_t addr, uint8_t val, void *priv),
			void (*outw)(uint16_t addr, uint16_t val, void *priv),
			void (*outl)(uint16_t addr, uint32_t val, void *priv),
			void *priv, int step);

extern void	io_removehandler_common(uint16_t base, int size,
			uint8_t (*inb)(uint16_t addr, void *priv),
			uint16_t (*inw)(uint16_t addr, void *priv),
			uint32_t (*inl)(uint16_t addr, void *priv),
			void (*outb)(uint16_t addr, uint8_t val, void *priv),
			void (*outw)(uint16_t addr, uint16_t val, void *priv),
			void (*outl)(uint16_t addr, uint32_t val, void *priv),
			void *priv, int step);

extern void	io_handler_common(int set, uint16_t base, int size,
			uint8_t (*inb)(uint16_t addr, void *priv),
			uint16_t (*inw)(uint16_t addr, void *priv),
			uint32_t (*inl)(uint16_t addr, void *priv),
			void (*outb)(uint16_t addr, uint8_t val, void *priv),
			void (*outw)(uint16_t addr, uint16_t val, void *priv),
			void (*outl)(uint16_t addr, uint32_t val, void *priv),
			void *priv, int step);

extern void	io_sethandler(uint16_t base, int size,
			uint8_t (*inb)(uint16_t addr, void *priv),
			uint16_t (*inw)(uint16_t addr, void *priv),
			uint32_t (*inl)(uint16_t addr, void *priv),
			void (*outb)(uint16_t addr, uint8_t val, void *priv),
			void (*outw)(uint16_t addr, uint16_t val, void *priv),
			void (*outl)(uint16_t addr, uint32_t val, void *priv),
			void *priv);

extern void	io_removehandler(uint16_t base, int size,
			uint8_t (*inb)(uint16_t addr, void *priv),
			uint16_t (*inw)(uint16_t addr, void *priv),
			uint32_t (*inl)(uint16_t addr, void *priv),
			void (*outb)(uint16_t addr, uint8_t val, void *priv),
			void (*outw)(uint16_t addr, uint16_t val, void *priv),
			void (*outl)(uint16_t addr, uint32_t val, void *priv),
			void *priv);

extern void	io_handler(int set, uint16_t base, int size,
			uint8_t (*inb)(uint16_t addr, void *priv),
			uint16_t (*inw)(uint16_t addr, void *priv),
			uint32_t (*inl)(uint16_t addr, void *priv),
			void (*outb)(uint16_t addr, uint8_t val, void *priv),
			void (*outw)(uint16_t addr, uint16_t val, void *priv),
			void (*outl)(uint16_t addr, uint32_t val, void *priv),
			void *priv);

extern void	io_sethandler_interleaved(uint16_t base, int size,
			uint8_t (*inb)(uint16_t addr, void *priv),
			uint16_t (*inw)(uint16_t addr, void *priv),
			uint32_t (*inl)(uint16_t addr, void *priv),
			void (*outb)(uint16_t addr, uint8_t val, void *priv),
			void (*outw)(uint16_t addr, uint16_t val, void *priv),
			void (*outl)(uint16_t addr, uint32_t val, void *priv),
			void *priv);

extern void	io_removehandler_interleaved(uint16_t base, int size,
			uint8_t (*inb)(uint16_t addr, void *priv),
			uint16_t (*inw)(uint16_t addr, void *priv),
			uint32_t (*inl)(uint16_t addr, void *priv),
			void (*outb)(uint16_t addr, uint8_t val, void *priv),
			void (*outw)(uint16_t addr, uint16_t val, void *priv),
			void (*outl)(uint16_t addr, uint32_t val, void *priv),
			void *priv);

extern void	io_handler_interleaved(int set, uint16_t base, int size,
			uint8_t (*inb)(uint16_t addr, void *priv),
			uint16_t (*inw)(uint16_t addr, void *priv),
			uint32_t (*inl)(uint16_t addr, void *priv),
			void (*outb)(uint16_t addr, uint8_t val, void *priv),
			void (*outw)(uint16_t addr, uint16_t val, void *priv),
			void (*outl)(uint16_t addr, uint32_t val, void *priv),
			void *priv);

extern uint8_t	inb(uint16_t port);
extern void	outb(uint16_t port, uint8_t  val);
extern uint16_t	inw(uint16_t port);
extern void	outw(uint16_t port, uint16_t val);
extern uint32_t	inl(uint16_t port);
extern void	outl(uint16_t port, uint32_t val);


#endif	/*EMU_IO_H*/
