/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the SFF-8038i IDE Bus Master.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2020 Miran Grca.
 */
#ifndef EMU_HDC_IDE_SFF8038I_H
#define EMU_HDC_IDE_SFF8038I_H

enum {
    IRQ_MODE_LEGACY = 0,
    IRQ_MODE_PCI_IRQ_PIN,
    IRQ_MODE_PCI_IRQ_LINE,
    IRQ_MODE_ALI_ALADDIN,
    IRQ_MODE_MIRQ_0,
    IRQ_MODE_MIRQ_1,
    IRQ_MODE_MIRQ_2,
    IRQ_MODE_MIRQ_3,
    IRQ_MODE_SIS_551X
};

typedef struct sff8038i_t {
    uint8_t  command;
    uint8_t  status;
    uint8_t  ptr0;
    uint8_t  enabled;
    uint8_t  dma_mode;
    uint8_t  irq_state;
    uint8_t  channel;
    uint8_t  irq_line;
    uint8_t  mirq;
    uint16_t base;
    uint16_t pad;
    uint32_t ptr;
    uint32_t ptr_cur;
    uint32_t addr;
    int      count;
    int      eot;
    int      slot;
    int      irq_mode;
    int      irq_level;
    int      irq_pin;
    int      pci_irq_line;

    uint8_t  (*ven_write)(uint16_t port, uint8_t val, void *priv);
    uint8_t  (*ven_read)(uint16_t port, uint8_t val, void *priv);

    void     *priv;
} sff8038i_t;

extern const device_t sff8038i_device;

extern void sff_bus_master_handler(sff8038i_t *dev, int enabled, uint16_t base);

extern void sff_bus_master_set_irq(uint8_t status, void *priv);
extern int  sff_bus_master_dma(uint8_t *data, int transfer_length, int total_length, int out, void *priv);

extern void    sff_bus_master_write(uint16_t port, uint8_t val, void *priv);
extern uint8_t sff_bus_master_read(uint16_t port, void *priv);

extern void sff_bus_master_reset(sff8038i_t *dev);

extern void sff_set_slot(sff8038i_t *dev, int slot);

extern void sff_set_irq_line(sff8038i_t *dev, int irq_line);
extern void sff_set_irq_mode(sff8038i_t *dev, int irq_mode);
extern void sff_set_irq_pin(sff8038i_t *dev, int irq_pin);
extern void sff_set_irq_level(sff8038i_t *dev, int irq_level);
extern void sff_set_mirq(sff8038i_t *dev, uint8_t mirq);

extern void sff_set_ven_handlers(sff8038i_t *dev, uint8_t (*ven_write)(uint16_t port, uint8_t val, void *priv),
                                 uint8_t (*ven_read)(uint16_t port, uint8_t val, void *priv), void *priv);

#endif /*EMU_HDC_IDE_SFF8038I_H*/
