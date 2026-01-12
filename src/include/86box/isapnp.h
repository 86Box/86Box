/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for ISA Plug and Play.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2021 RichardG.
 */
#ifndef EMU_ISAPNP_H
#define EMU_ISAPNP_H
#include <stdint.h>

#define ISAPNP_MEM_DISABLED 0
#define ISAPNP_IO_DISABLED  0
#define ISAPNP_IRQ_DISABLED 0
#define ISAPNP_DMA_DISABLED 4

enum {
    ISAPNP_CARD_DISABLE      = 0,
    ISAPNP_CARD_ENABLE       = 1,
    ISAPNP_CARD_FORCE_CONFIG = 2, /* cheat code for UMC UM8669F */
    ISAPNP_CARD_NO_KEY       = 3, /* cheat code for Crystal CS423x */
    ISAPNP_CARD_FORCE_SLEEP  = 4  /* cheat code for Yamaha YMF-71x */
};

typedef struct isapnp_device_config_t {
    uint8_t activate;
    struct pnp_mem_t {
        uint32_t base : 24;
        uint32_t size : 24;
    } mem[4];
    struct pnp_mem32_t {
        uint32_t base;
        uint32_t size;
    } mem32[4];
    struct pnp_io_t {
        uint16_t base;
    } io[8];
    struct pnp_irq_t {
        uint8_t irq : 4;
        uint8_t level : 1;
        uint8_t type : 1;
    } irq[2];
    struct pnp_dma_t {
        uint8_t dma : 3;
    } dma[2];
} isapnp_device_config_t;

extern const uint8_t isapnp_init_key[32];

extern void    *isapnp_add_card(uint8_t *rom, uint16_t rom_size,
                                void (*config_changed)(uint8_t ld, isapnp_device_config_t *config, void *priv),
                                void (*csn_changed)(uint8_t csn, void *priv),
                                uint8_t (*read_vendor_reg)(uint8_t ld, uint8_t reg, void *priv),
                                void (*write_vendor_reg)(uint8_t ld, uint8_t reg, uint8_t val, void *priv),
                                void *priv);
extern void     isapnp_update_card_rom(void *priv, uint8_t *rom, uint16_t rom_size);
extern void     isapnp_enable_card(void *priv, uint8_t enable);
extern void     isapnp_set_csn(void *priv, uint8_t csn);
extern uint8_t  isapnp_read_reg(void *priv, uint8_t ldn, uint8_t reg);
extern void     isapnp_write_reg(void *priv, uint8_t ldn, uint8_t reg, uint8_t val);
extern void     isapnp_set_device_defaults(void *priv, uint8_t ldn, const isapnp_device_config_t *config);
extern void     isapnp_reset_card(void *priv);
extern void     isapnp_reset_device(void *priv, uint8_t ld);
extern void     isapnp_set_rt(void *priv, uint8_t is_rt);
extern void     isapnp_set_normal(void *priv, uint8_t normal);
extern void     isapnp_activate(void *priv, uint16_t base, uint8_t irq, int active);
extern void     isapnp_set_single_ld(void *priv);
extern uint8_t *isapnp_get_csnsav(void *priv);

#endif /*EMU_ISAPNP_H*/
