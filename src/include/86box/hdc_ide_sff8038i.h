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
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2020 Miran Grca.
 */

#ifndef EMU_HDC_IDE_SFF8038I_H
#define EMU_HDC_IDE_SFF8038I_H

typedef struct sff8038i_t {
    uint8_t  command;
    uint8_t  status;
    uint8_t  ptr0;
    uint8_t  enabled;
    uint8_t  dma_mode;
    uint8_t  irq_state;
    uint8_t  pad;
    uint8_t  pad0;
    uint16_t base;
    uint16_t pad1;
    uint32_t ptr;
    uint32_t ptr_cur;
    uint32_t addr;
    int      count;
    int      eot;
    int      slot;
    int      irq_mode[2];
    int      irq_level[2];
    int      irq_pin;
    int      irq_line;
} sff8038i_t;

extern const device_t sff8038i_device;

extern void sff_bus_master_handler(sff8038i_t *dev, int enabled, uint16_t base);

extern int sff_bus_master_dma_read(int channel, uint8_t *data, int transfer_length, void *priv);
extern int sff_bus_master_dma_write(int channel, uint8_t *data, int transfer_length, void *priv);

extern void sff_bus_master_set_irq(int channel, void *priv);

extern int sff_bus_master_dma(int channel, uint8_t *data, int transfer_length, int out, void *priv);

extern void    sff_bus_master_write(uint16_t port, uint8_t val, void *priv);
extern uint8_t sff_bus_master_read(uint16_t port, void *priv);

extern void sff_bus_master_reset(sff8038i_t *dev, uint16_t old_base);

extern void sff_set_slot(sff8038i_t *dev, int slot);

extern void sff_set_irq_line(sff8038i_t *dev, int irq_line);

extern void sff_set_irq_mode(sff8038i_t *dev, int channel, int irq_mode);
extern void sff_set_irq_pin(sff8038i_t *dev, int irq_pin);

extern void sff_set_irq_level(sff8038i_t *dev, int channel, int irq_level);

#endif /*EMU_HDC_IDE_SFF8038I_H*/
