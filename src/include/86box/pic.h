/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header of the implementation of the Intel PIC chip emulation,
 *          partially ported from reenigne's XTCE.
 *
 * Authors: Andrew Jenner, <https://www.reenigne.org>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2015-2020 Andrew Jenner.
 *          Copyright 2016-2020 Miran Grca.
 */
#ifndef EMU_PIC_H
#define EMU_PIC_H

typedef struct pic_latch {
    uint8_t d;
    uint8_t e;
    uint8_t q;
    uint8_t nq;
} pic_latch_t;

typedef struct pic {
    uint8_t     icw1;
    uint8_t     icw2;
    uint8_t     icw3;
    uint8_t     icw4;
    uint8_t     imr;
    uint8_t     isr;
    uint8_t     irr;
    uint8_t     ocw2;
    uint8_t     ocw3;
    uint8_t     int_pending;
    uint8_t     is_master;
    uint8_t     elcr;
    uint8_t     state;
    uint8_t     ack_bytes;
    uint8_t     priority;
    uint8_t     special_mask_mode;
    uint8_t     auto_eoi_rotate;
    uint8_t     interrupt;
    uint8_t     data_bus;
    uint8_t     irq_latch;
    uint8_t     has_slaves;
    uint8_t     flags;
    uint8_t     edge_lines;
    uint8_t     pad;
    uint32_t    lines[8];
    uint32_t    at;
    struct pic *slaves[8];
} pic_t;

extern pic_t pic;
extern pic_t pic2;

extern void     pic_reset_smi_irq_mask(void);
extern void     pic_set_smi_irq_mask(int irq, int set);
extern uint16_t pic_get_smi_irq_status(void);
extern void     pic_clear_smi_irq_status(int irq);

extern int     pic_elcr_get_enabled(void);
extern void    pic_elcr_set_enabled(int enabled);
extern void    pic_elcr_io_handler(int set);
extern void    pic_elcr_write(uint16_t port, uint8_t val, void *priv);
extern uint8_t pic_elcr_read(uint16_t port, void *priv);

extern void pic_set_shadow(int sh);
extern int  pic_get_pci_flag(void);
extern void pic_set_pci_flag(int pci);
extern void pic_set_pci(void);
extern void pic_kbd_latch(int enable);
extern void pic_mouse_latch(int enable);
extern void pic_init(void);
extern void pic_init_pcjr(void);
extern void pic2_init(void);
extern void pic_reset(void);

extern uint8_t pic_read_icw(uint8_t pic_id, uint8_t icw);
extern uint8_t pic_read_ocw(uint8_t pic_id, uint8_t ocw);
extern int     picint_is_level(int irq);
extern void    picint_common(uint16_t num, int level, int set, uint8_t *irq_state);
extern int     picinterrupt(void);

#define PIC_IRQ_EDGE                    0
#define PIC_IRQ_LEVEL                   1

#define PIC_SLAVE_PENDING               0x01
#define PIC_FREEZE                      0x02
#define PIC_MASTER_CLEAR                0x04

/* Legacy defines. */
#define picint(num)                     picint_common(num, PIC_IRQ_EDGE,  1, NULL)
#define picintlevel(num, irq_state)     picint_common(num, PIC_IRQ_LEVEL, 1, irq_state)
#define picintc(num)                    picint_common(num, PIC_IRQ_EDGE,  0, NULL)
#define picintclevel(num, irq_state)    picint_common(num, PIC_IRQ_LEVEL, 0, irq_state)

extern uint8_t pic_irq_ack(void);

extern void    pic_toggle_latch(int is_ps2);

#endif /*EMU_PIC_H*/
