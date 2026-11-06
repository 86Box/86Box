/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          Emulation of the AMD Am7990 LANCE Ethernet controller for
 *          both the 8-bit and 16-bit ISA buses, and the DEC VAXmate
 *          onboard network controller.
 *
 * Authors: WNT50
 *          Patrick Mackinlay
 *
 *          Copyright 2026 WNT50.
 *          Copyright 2018-2025 Patrick Mackinlay.
 */
#ifndef NET_LANCE_H
#define NET_LANCE_H

typedef struct lance_t {
    uint16_t   csr[4];
    uint16_t   base;
    int        irq_num; /* IRQ line used by the board */
    int        irq_gate; /* NICSR interrupt enable (power-on: off) */
    int        irq;
    uint8_t    rap;
    uint8_t    mac[6];
    uint8_t    padr[6];
    uint8_t    ladrf[8];
    uint16_t   mode;
    uint32_t   init_addr;
    uint32_t   rx_ring_base;
    uint32_t   tx_ring_base;
    uint8_t    rx_ring_mask;
    uint8_t    tx_ring_mask;
    uint8_t    rx_ring_pos;
    uint8_t    tx_ring_pos;
    uint16_t   tx_md[4];
    uint16_t   rx_md[4];
    uint32_t   dma_limit; /* highest address the board can master to */
    pc_timer_t tx_timer;
    netcard_t *card;
} lance_t;

/* Generic Am7990. The board that instantiates it wires the ports itself. */
extern const device_t lance_device;

extern void     lance_reset(void *priv);
extern uint16_t lance_rdp_read(uint16_t addr, void *priv);
extern void     lance_rdp_write(uint16_t addr, uint16_t val, void *priv);
extern uint16_t lance_rap_read(uint16_t addr, void *priv);
extern void     lance_rap_write(uint16_t addr, uint16_t val, void *priv);
extern int      lance_rx(void *card_drv, uint8_t *buf, int length);

/* DEC interface logic (VAXmate NICSR) gates the interrupt line. */
extern void lance_set_irq_gate(void *priv, int on);
extern int  lance_get_irq_gate(void *priv);

/* VAXmate onboard controller: Am7990 at 0C60, IRQ10, NICSR at 0C68. */
extern const device_t lance_vaxmate_device;

#endif /*NET_LANCE_H*/
