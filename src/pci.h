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
 * Version:	@(#)pci.h	1.0.2	2020/01/11
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
    PCI_CARD_NORMAL = 0,
    PCI_CARD_ONBOARD,
    PCI_CARD_SCSI,
    PCI_CARD_SPECIAL
};


#define PCI_ADD_NORMAL	0x80
#define PCI_ADD_VIDEO	0x81
#define PCI_ADD_SCSI	0x82

typedef union {
    uint32_t addr;
    uint8_t addr_regs[4];
} bar_t;


extern int	pci_burst_time,
		pci_nonburst_time;


extern void	pci_set_irq_routing(int pci_int, int irq);
extern void	pci_set_irq_level(int pci_int, int level);

extern void	pci_enable_mirq(int mirq);
extern void	pci_set_mirq_routing(int mirq, int irq);

extern uint8_t	pci_use_mirq(uint8_t mirq);

extern int	pci_irq_is_level(int irq);

extern void	pci_set_mirq(uint8_t mirq, int level);
extern void	pci_set_irq(uint8_t card, uint8_t pci_int);
extern void	pci_clear_mirq(uint8_t mirq, int level);
extern void	pci_clear_irq(uint8_t card, uint8_t pci_int);

extern void	pci_reset(void);
extern void	pci_init(int type);
extern void	pci_register_slot(int card, int type,
				  int inta, int intb, int intc, int intd);
extern void	pci_close(void);
extern uint8_t	pci_add_card(uint8_t add_type, uint8_t (*read)(int func, int addr, void *priv), void (*write)(int func, int addr, uint8_t val, void *priv), void *priv);

extern void     trc_init(void);
extern void	pci_elcr_set_enabled(int enabled);


#endif	/*EMU_PCI_H*/
