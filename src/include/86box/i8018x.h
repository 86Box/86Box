/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          8018x peripherals - the peripheral part of the 80186/80188;
 *          the CPU core itself is in 808x.c.
 *
 * Authors: WNT50
 *
 *          Copyright 2026 WNT50.
 */
#ifndef EMU_I8018X_H
#define EMU_I8018X_H

struct i8018x_pcb_t;
typedef int (*ext_ack_t)(void *priv);
/* The controller a machine has brought up, NULL when there is none; the CPU
   core asks through this instead of waiting to be handed one. */
extern struct i8018x_pcb_t *i8018x_pcb_current;
extern const  device_t i8018x_pcb_device;
extern int    i8018x_pcb_cascade;

extern int  i8018x_pcb_icu_pending(void);
extern int  i8018x_pcb_icu_int_type(void);
extern int  i8018x_pcb_icu_acknowledge(struct i8018x_pcb_t *pcb);
extern void i8018x_pcb_icu_int(struct i8018x_pcb_t *pcb, int line, int level);
extern void i8018x_pcb_set_irq_map(struct i8018x_pcb_t *pcb, int pic_irq, int line);
extern void i8018x_pcb_set_dma_channels(struct i8018x_pcb_t *pcb, int dma0_ch, int dma1_ch);
extern void i8018x_pcb_set_ext_ack(struct i8018x_pcb_t *pcb, int line, ext_ack_t ack, void *priv);

#endif /* EMU_I8018X_H */
