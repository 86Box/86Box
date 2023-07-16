/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the PCI handler module.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          Sarah Walker, <https://pcem-emulator.co.uk/>
 *
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2017-2020 Fred N. van Kempen.
 *          Copyright 2008-2020 Sarah Walker.
 */

#ifndef EMU_PCI_H
#define EMU_PCI_H

#define PCI_REG_COMMAND        0x04

#define PCI_COMMAND_IO         0x01
#define PCI_COMMAND_MEM        0x02

#define PCI_NO_IRQ_STEERING    0x8000
#define PCI_CAN_SWITCH_TYPE    0x10000
#define PCI_NO_BRIDGES         0x20000
#define PCI_ALWAYS_EXPOSE_DEV0 0x40000

#define PCI_CONFIG_TYPE_1      1
#define PCI_CONFIG_TYPE_2      2

#define PCI_CONFIG_TYPE_MASK   0x7fff

#define PCI_INTA               1
#define PCI_INTB               2
#define PCI_INTC               3
#define PCI_INTD               4

#define PCI_MIRQ0              0
#define PCI_MIRQ1              1
#define PCI_MIRQ2              2
#define PCI_MIRQ3              3
#define PCI_MIRQ4              4
#define PCI_MIRQ5              5
#define PCI_MIRQ6              6
#define PCI_MIRQ7              7

#define PCI_IRQ_DISABLED       -1

#define PCI_ADD_STRICT         0x80

enum {
    PCI_CARD_NORTHBRIDGE     = 0,
    PCI_CARD_AGPBRIDGE       = 1,
    PCI_CARD_SOUTHBRIDGE     = 2,
    PCI_CARD_SOUTHBRIDGE_IDE = 3,
    PCI_CARD_SOUTHBRIDGE_PMU = 4,
    PCI_CARD_SOUTHBRIDGE_USB = 5,
    PCI_CARD_AGP             = 0x0f,
    PCI_CARD_NORMAL          = 0x10,
    PCI_CARD_VIDEO           = 0x11,
    PCI_CARD_SCSI            = 0x12,
    PCI_CARD_SOUND           = 0x13,
    PCI_CARD_IDE             = 0x14,
    PCI_CARD_NETWORK         = 0x15,
    PCI_CARD_BRIDGE          = 0x16,
};

enum {
    PCI_ADD_NORTHBRIDGE     = 0,
    PCI_ADD_AGPBRIDGE       = 1,
    PCI_ADD_SOUTHBRIDGE     = 2,
    PCI_ADD_SOUTHBRIDGE_IDE = 3,
    PCI_ADD_SOUTHBRIDGE_PMU = 4,
    PCI_ADD_SOUTHBRIDGE_USB = 5,
    PCI_ADD_AGP             = 0x0f,
    PCI_ADD_NORMAL          = 0x10,
    PCI_ADD_VIDEO           = 0x11,
    PCI_ADD_SCSI            = 0x12,
    PCI_ADD_SOUND           = 0x13,
    PCI_ADD_IDE             = 0x14,
    PCI_ADD_NETWORK         = 0x15,
    PCI_ADD_BRIDGE          = 0x16
};

typedef union {
    uint32_t addr;
    uint8_t addr_regs[4];
} bar_t;


#define PCI_IO_ON   0x01
#define PCI_IO_DEV0 0x02


extern int pci_burst_time;
extern int agp_burst_time;
extern int pci_nonburst_time;
extern int agp_nonburst_time;
extern int pci_take_over_io;

extern uint32_t pci_base;
extern uint32_t pci_size;


extern void     pci_type2_write(uint16_t port, uint8_t val, void *priv);
extern void     pci_type2_writew(uint16_t port, uint16_t val, void *priv);
extern void     pci_type2_writel(uint16_t port, uint32_t val, void *priv);
extern uint8_t  pci_type2_read(uint16_t port, void *priv);
extern uint16_t pci_type2_readw(uint16_t port, void *priv);
extern uint32_t pci_type2_readl(uint16_t port, void *priv);

extern void pci_set_irq_routing(int pci_int, int irq);
extern void pci_set_irq_level(int pci_int, int level);

extern void pci_enable_mirq(int mirq);
extern void pci_set_mirq_routing(int mirq, int irq);

extern int pci_irq_is_level(int irq);

extern void    pci_set_mirq(uint8_t mirq, int level);
extern void    pci_set_irq(uint8_t card, uint8_t pci_int);
extern void    pci_clear_mirq(uint8_t mirq, int level);
extern void    pci_clear_irq(uint8_t card, uint8_t pci_int);
extern uint8_t pci_get_int(uint8_t card, uint8_t pci_int);

extern void    pci_reset(void);
extern void    pci_init(int type);
extern uint8_t pci_register_bus(void);
extern void    pci_set_pmc(uint8_t pmc);
extern void    pci_remap_bus(uint8_t bus_index, uint8_t bus_number);
extern void    pci_relocate_slot(int type, int new_slot);
extern void    pci_register_slot(int card, int type,
                                 int inta, int intb, int intc, int intd);
extern void    pci_register_bus_slot(int bus, int card, int type,
                                     int inta, int intb, int intc, int intd);
extern void    pci_close(void);
extern uint8_t pci_add_card(uint8_t add_type, uint8_t (*read)(int func, int addr, void *priv), void (*write)(int func, int addr, uint8_t val, void *priv), void *priv);

extern void trc_init(void);

extern uint8_t trc_read(uint16_t port, void *priv);
extern void    trc_write(uint16_t port, uint8_t val, void *priv);

extern void pci_bridge_set_ctl(void *priv, uint8_t ctl);

extern void pci_pic_reset(void);

#ifdef EMU_DEVICE_H
extern const device_t dec21150_device;

extern const device_t ali5243_agp_device;
extern const device_t ali5247_agp_device;
extern const device_t i440lx_agp_device;
extern const device_t i440bx_agp_device;
extern const device_t i440gx_agp_device;
extern const device_t via_vp3_agp_device;
extern const device_t via_mvp3_agp_device;
extern const device_t via_apro_agp_device;
extern const device_t via_vt8601_agp_device;
#endif

#endif /*EMU_PCI_H*/
