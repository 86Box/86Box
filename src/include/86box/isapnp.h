/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for ISA Plug and Play.
 *
 *
 *
 * Author:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2021 RichardG.
 */
#ifndef EMU_ISAPNP_H
# define EMU_ISAPNP_H
# include <stdint.h>


#define ISAPNP_MEM_DISABLED	0
#define ISAPNP_IO_DISABLED	0
#define ISAPNP_IRQ_DISABLED	0
#define ISAPNP_DMA_DISABLED	4


typedef struct {
    uint8_t	activate;
    struct {
    	uint32_t base: 24, size: 24;
    } mem[4];
    struct {
    	uint32_t base, size;
    } mem32[4];
    struct {
    	uint16_t base;
    } io[8];
    struct {
    	uint8_t irq: 4, level: 1, type: 1;
    } irq[2];
    struct {
    	uint8_t dma: 3;
    } dma[2];
} isapnp_device_config_t;


void	*isapnp_add_card(uint8_t *rom, uint16_t rom_size,
			 void (*config_changed)(uint8_t ld, isapnp_device_config_t *config, void *priv),
			 void (*csn_changed)(uint8_t csn, void *priv),
			 uint8_t (*read_vendor_reg)(uint8_t ld, uint8_t reg, void *priv),
			 void (*write_vendor_reg)(uint8_t ld, uint8_t reg, uint8_t val, void *priv),
			 void *priv);
void	isapnp_set_csn(void *priv, uint8_t csn);
uint8_t	isapnp_get_rom_checksum(void *priv);


#endif	/*EMU_ISAPNP_H*/
