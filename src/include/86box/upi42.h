/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Intel UPI-42/MCS-48 microcontroller emulation.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *          WNT50, <wnt2195@gmail.com>
 *
 *          Copyright 2022 RichardG.
 *          Copyright 2026 WNT50.
 */
#ifndef EMU_UPI42_H
#define EMU_UPI42_H

#include <stdint.h>

#define UPI42_ROM_SHIFT         0  /* actually the mask */
#define UPI42_RAM_SHIFT         16 /* actually the mask */
#define UPI42_TYPE_MCS          (0u << 24)
#define UPI42_TYPE_UPI          (1u << 24)
#define UPI42_EXT_C42           (1u << 25)

#define UPI42_8048              ((1023u << UPI42_ROM_SHIFT) | (63u << UPI42_RAM_SHIFT) | UPI42_TYPE_MCS)
#define UPI42_8049              ((2047u << UPI42_ROM_SHIFT) | (127u << UPI42_RAM_SHIFT) | UPI42_TYPE_MCS)
#define UPI42_8041              ((1023u << UPI42_ROM_SHIFT) | (127u << UPI42_RAM_SHIFT) | UPI42_TYPE_UPI)
#define UPI42_8042              ((2047u << UPI42_ROM_SHIFT) | (255u << UPI42_RAM_SHIFT) | UPI42_TYPE_UPI)
#define UPI42_80C42             ((4095u << UPI42_ROM_SHIFT) | (255u << UPI42_RAM_SHIFT) | UPI42_TYPE_UPI | UPI42_EXT_C42)

typedef struct upi42 upi42_t;

typedef struct {
    int (*t0_in)(void *priv);      /* external T0 level, may be NULL */
    int (*t1_in)(void *priv);      /* external T1 level, may be NULL */
    uint8_t (*p1_in)(void *priv);  /* P1 input, may be NULL (defaults to ports_in[1]) */
    uint8_t (*p2_in)(void *priv);  /* P2 input, may be NULL (defaults to ports_in[2]) */
    void (*port_out)(void *priv, int port, uint8_t val); /* P1/P2/P4-P7 output changed */
    void (*irq_out)(void *priv, int level);              /* INT pin */
} upi42_hooks;

/* rom is owned by the caller and is not copied or freed. */
upi42_t *upi42_create(uint32_t type, uint8_t *rom);
void     upi42_close(upi42_t *upi42);
void     upi42_reset(upi42_t *upi42);
void     upi42_attach(upi42_t *upi42, const upi42_hooks *hooks, void *priv);

/* Executes one instruction and returns the number of machine cycles consumed
   (0 if the core is suspended). */
int  upi42_step(upi42_t *upi42);
void upi42_run(upi42_t *upi42, uint32_t cycles);

/* Host (UPI master) side. */
uint8_t upi42_master_read_status(upi42_t *upi42);
uint8_t upi42_master_read_data(upi42_t *upi42);               /* reads DBB, clears OBF */
void    upi42_master_write_data(upi42_t *upi42, uint8_t val); /* writes DBB, sets IBF, clears F1 */
void    upi42_master_write_cmd(upi42_t *upi42, uint8_t val);  /* writes DBB, sets IBF and F1 */

/* Pin side. */
uint8_t upi42_port_read(upi42_t *upi42, int port);
void    upi42_port_write(upi42_t *upi42, int port, uint8_t val);

/* Observability. */
uint8_t  upi42_get_sts(upi42_t *upi42);
uint16_t upi42_get_pc(upi42_t *upi42);
uint8_t  upi42_get_psw(upi42_t *upi42);
uint8_t  upi42_get_ram(upi42_t *upi42, uint8_t addr);

#endif /* EMU_UPI42_H */
