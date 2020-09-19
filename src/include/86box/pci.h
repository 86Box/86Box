/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the PCI handler module.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2017-2020 Fred N. van Kempen.
 *		Copyright 2008-2020 Sarah Walker.
 */
#ifndef EMU_PCI_H
# define EMU_PCI_H


#define PCI_REG_COMMAND 0x04

#define PCI_COMMAND_IO  0x01
#define PCI_COMMAND_MEM 0x02

#define PCI_NO_IRQ_STEERING 0x8000
#define PCI_CAN_SWITCH_TYPE 0x10000

#define PCI_CONFIG_TYPE_1 1
#define PCI_CONFIG_TYPE_2 2

#define PCI_CONFIG_TYPE_MASK 0x7fff

#define PCI_INTA 1
#define PCI_INTB 2
#define PCI_INTC 3
#define PCI_INTD 4

#define PCI_MIRQ0 0
#define PCI_MIRQ1 1
#define PCI_MIRQ2 2

#define PCI_IRQ_DISABLED -1

enum {
    PCI_CARD_NORTHBRIDGE = 0,
    PCI_CARD_SOUTHBRIDGE,
    PCI_CARD_NORMAL,
    PCI_CARD_ONBOARD,
    PCI_CARD_SCSI,
    PCI_CARD_SOUND,
    PCI_CARD_IDE,
    PCI_CARD_SPECIAL
};

enum {
    PCI_ADD_NORTHBRIDGE = 0x80,
    PCI_ADD_SOUTHBRIDGE,
    PCI_ADD_NORMAL,
    PCI_ADD_VIDEO,
    PCI_ADD_SCSI,
    PCI_ADD_SOUND,
    PCI_ADD_IDE,
    PCI_ADD_BRIDGE
};

typedef union {
    uint32_t addr;
    uint8_t addr_regs[4];
} bar_t;


extern int	pci_burst_time,
		pci_nonburst_time;
extern uint8_t	last_pci_bus;
extern uint8_t	pci_bus_number_to_index_mapping[256];


extern void	pci_set_irq_routing(int pci_int, int irq);
extern void	pci_set_irq_level(int pci_int, int level);

extern void	pci_enable_mirq(int mirq);
extern void	pci_set_mirq_routing(int mirq, int irq);

extern uint8_t	pci_use_mirq(uint8_t mirq);

extern int	pci_irq_is_level(int irq);

extern void	pci_set_mirq(uint8_t mirq, int level);
extern void	pci_set_irq(int card, uint8_t pci_int);
extern void	pci_clear_mirq(uint8_t mirq, int level);
extern void	pci_clear_irq(int card, uint8_t pci_int);
extern uint8_t	pci_get_int(int card, uint8_t pci_int);

extern void	pci_reset(void);
extern void	pci_init(int type);
extern void	pci_register_slot(int card, int type,
				  int inta, int intb, int intc, int intd);
extern void	pci_register_bus_slot(int bus, int card, int type,
				      int inta, int intb, int intc, int intd);
extern void	pci_close(void);
extern int	pci_add_card(uint8_t add_type, uint8_t (*read)(int func, int addr, void *priv), void (*write)(int func, int addr, uint8_t val, void *priv), void *priv);

extern void     trc_init(void);

extern uint8_t	trc_read(uint16_t port, void *priv);
extern void	trc_write(uint16_t port, uint8_t val, void *priv);

extern void	pci_elcr_set_enabled(int enabled);
extern void	pci_elcr_io_disable(void);
extern void	elcr_write(uint16_t port, uint8_t val, void *priv);
extern uint8_t	elcr_read(uint16_t port, void *priv);


#ifdef EMU_DEVICE_H
extern const device_t dec21150_device;

extern const device_t i440lx_agp_device;
extern const device_t i440bx_agp_device;
extern const device_t i440gx_agp_device;
#endif


#endif	/*EMU_PCI_H*/
