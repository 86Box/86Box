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
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2023 Miran Grca.
 */
#ifndef EMU_PCI_H
#define EMU_PCI_H

#define PCI_REG_VENDOR_ID_L       0x00
#define PCI_REG_VENDOR_ID_H       0x01
#define PCI_REG_DEVICE_ID_L       0x02
#define PCI_REG_DEVICE_ID_H       0x03
#define PCI_REG_COMMAND_L         0x04
#define PCI_REG_COMMAND_H         0x05
#define PCI_REG_STATUS_L          0x06
#define PCI_REG_STATUS_H          0x07
#define PCI_REG_REVISION          0x08
#define PCI_REG_PROG_IF           0x09
#define PCI_REG_SUBCLASS          0x0a
#define PCI_REG_CLASS             0x0b
#define PCI_REG_CACHELINE_SIZE    0x0c
#define PCI_REG_LATENCY_TIMER     0x0d
#define PCI_REG_HEADER_TYPE       0x0e
#define PCI_REG_BIST              0x0f
#define PCI_REG_BAR0_BYTE0        0x10
#define PCI_REG_BAR0_BYTE1        0x11
#define PCI_REG_BAR0_BYTE2        0x12
#define PCI_REG_BAR0_BYTE3        0x13
#define PCI_REG_BAR1_BYTE0        0x14
#define PCI_REG_BAR1_BYTE1        0x15
#define PCI_REG_BAR1_BYTE2        0x16
#define PCI_REG_BAR1_BYTE3        0x17
#define PCI_REG_BAR2_BYTE0        0x18
#define PCI_REG_BAR2_BYTE1        0x19
#define PCI_REG_BAR2_BYTE2        0x1a
#define PCI_REG_BAR2_BYTE3        0x1b
#define PCI_REG_BAR3_BYTE0        0x1c
#define PCI_REG_BAR3_BYTE1        0x1d
#define PCI_REG_BAR3_BYTE2        0x1e
#define PCI_REG_BAR3_BYTE3        0x1e
#define PCI_REG_BAR4_BYTE0        0x20
#define PCI_REG_BAR4_BYTE1        0x21
#define PCI_REG_BAR4_BYTE2        0x22
#define PCI_REG_BAR4_BYTE3        0x23
#define PCI_REG_BAR5_BYTE0        0x24
#define PCI_REG_BAR5_BYTE1        0x25
#define PCI_REG_BAR5_BYTE2        0x26
#define PCI_REG_BAR5_BYTE3        0x27
#define PCI_REG_SUBVEN_ID_L       0x2c
#define PCI_REG_SUBVEN_ID_H       0x2d
#define PCI_REG_SUBSYS_ID_L       0x2e
#define PCI_REG_SUBSYS_ID_H       0x2f
#define PCI_REG_ROM_BAR_BYTE0     0x30
#define PCI_REG_ROM_BAR_BYTE1     0x31
#define PCI_REG_ROM_BAR_BYTE2     0x32
#define PCI_REG_ROM_BAR_BYTE3     0x33
#define PCI_REG_CAPS_PTR          0x34
#define PCI_REG_INT_LINE          0x3c
#define PCI_REG_INT_PIN           0x3d
#define PCI_REG_MIN_GRANT         0x3e
#define PCI_REG_MAX_LAT           0x3f

#define PCI_COMMAND_L_IO          0x01
#define PCI_COMMAND_L_MEM         0x02
#define PCI_COMMAND_L_BM          0x04
#define PCI_COMMAND_L_SPECIAL     0x08
#define PCI_COMMAND_L_MEM_WIEN    0x10
#define PCI_COMMAND_L_VGASNOOP    0x20
#define PCI_COMMAND_L_PARITY      0x40

#define PCI_COMMAND_H_SERR        0x01
#define PCI_COMMAND_H_FAST_B2B    0x02
#define PCI_COMMAND_H_INT_DIS     0x04

#define PCI_STATUS_L_INT          0x08
#define PCI_STATUS_L_CAPAB        0x10
#define PCI_STATUS_L_66MHZ        0x20
#define PCI_STATUS_L_FAST_B2B     0x80

#define PCI_STATUS_H_MDPERR       0x01    /* Master Data Parity Error */
#define PCI_STATUS_H_DEVSEL       0x06
#define PCI_STATUS_H_STA          0x08    /* Signaled Target Abort */
#define PCI_STATUS_H_RTA          0x10    /* Received Target Abort */
#define PCI_STATUS_H_RMA          0x20    /* Received Master Abort */
#define PCI_STATUS_H_SSE          0x40    /* Signaled System Error */
#define PCI_STATUS_H_DPERR        0x80    /* Detected Parity Error */

#define PCI_DEVSEL_FAST           0x00
#define PCI_DEVSEL_MEDIUM         0x02
#define PCI_DEVSEL_SLOW           0x04

#define FLAG_MECHANISM_1          0x00000001
#define FLAG_MECHANISM_2          0x00000002
#define FLAG_MECHANISM_SWITCH     0x00000004
#define FLAG_CONFIG_IO_ON         0x00000008
#define FLAG_CONFIG_DEV0_IO_ON    0x00000010
#define FLAG_CONFIG_M1_IO_ON      0x00000020
#define FLAG_NO_IRQ_STEERING      0x00000040
#define FLAG_NO_BRIDGES           0x00000080
#define FLAG_TRC_CONTROLS_CPURST  0x00000100

#define FLAG_MECHANISM_MASK       FLAG_MECHANISM_1 | FLAG_MECHANISM_2
#define FLAG_MASK                 0x0000007f

#define PCI_INTA                  1
#define PCI_INTB                  2
#define PCI_INTC                  3
#define PCI_INTD                  4

#define PCI_MIRQ0                 0
#define PCI_MIRQ1                 1
#define PCI_MIRQ2                 2
#define PCI_MIRQ3                 3
#define PCI_MIRQ4                 4
#define PCI_MIRQ5                 5
#define PCI_MIRQ6                 6
#define PCI_MIRQ7                 7

#define PCI_IRQ_DISABLED          -1

#define PCI_ADD_STRICT            0x40
#define PCI_ADD_MASK              (PCI_ADD_STRICT - 1)
#define PCI_ADD_VFIO              0x80
#define PCI_ADD_VFIO_MASK         (PCI_ADD_VFIO - 1)

#define PCI_CARD_VFIO             PCI_ADD_VFIO

#define PCI_BUS_INVALID           0xff

#define PCI_IGNORE_NO_SLOT        0xff

/* The number of an invalid PCI card. */
#define PCI_CARD_INVALID          0xef
/* PCI cards (currently 32). */
#define PCI_CARDS_NUM             0x20
#define PCI_CARD_MAX              (PCI_CARDS_NUM - 1)
/* The number of PCI card INT pins - always at 4 per the PCI specification. */
#define PCI_INT_PINS_NUM          4
#define PCI_INT_PINS_MAX          (PCI_INT_PINS_NUM - 1)
/* The base for MIRQ lines accepted by pci_irq(). */
#define PCI_MIRQ_BASE             PCI_CARDS_NUM
/* PCI MIRQ lines (currently 8, this many are needed by the ALi M1543(C). */
#define PCI_MIRQS_NUM             8
#define PCI_MIRQ_MAX              (PCI_MIRQS_NUM - 1)
/* The base for internal IRQ lines accepted by pci_irq(). */
#define PCI_IIRQ_BASE             0x80
/* PCI direct IRQ lines - always at 4 per the PCI specification. */
#define PCI_IIRQS_NUM             4
#define PCI_IIRQ_MAX              (PCI_IIRQS_NUM - 1)
/* The base for direct IRQ lines accepted by pci_irq(). */
#define PCI_DIRQ_BASE             0xf0
/* PCI direct IRQ lines (currently 16 because we only emulate the legacy PIC). */
#define PCI_DIRQS_NUM             16
#define PCI_DIRQ_MAX              (PCI_DIRQS_NUM - 1)
/* PCI IRQ routings (currently 16, this many are needed by the OPTi 822). */
#define PCI_IRQS_NUM              16
#define PCI_IRQ_MAX               (PCI_IRQS_NUM - 1)

/* Legacy flags. */
#define PCI_REG_COMMAND           PCI_REG_COMMAND_L

#define PCI_COMMAND_IO            PCI_COMMAND_L_IO
#define PCI_COMMAND_MEM           PCI_COMMAND_L_MEM

#define PCI_CONFIG_TYPE_1         FLAG_MECHANISM_1
#define PCI_CONFIG_TYPE_2         FLAG_MECHANISM_2

#define PCI_CAN_SWITCH_TYPE       FLAG_MECHANISM_SWITCH
#define PCI_ALWAYS_EXPOSE_DEV0    FLAG_CONFIG_DEV0_IO_ON
#define PCI_NO_IRQ_STEERING       FLAG_NO_IRQ_STEERING
#define PCI_NO_BRIDGES            FLAG_NO_BRIDGES

#define PCI_CONFIG_TYPE_MASK      FLAG_MECHANISM_MASK

#define bar_t                     pci_bar_t
#define trc_init                  pci_trc_init

#define pci_register_slot(card, type, inta, intb, intc, intd) \
        pci_register_bus_slot(0, card, type, inta, intb, intc, intd)

#define pci_set_mirq(mirq, level, irq_state) \
        pci_irq(PCI_MIRQ_BASE | (mirq), 0, level, 1, irq_state)
#define pci_set_iirq(pci_int, irq_state) \
        pci_irq(PCI_IIRQ_BASE | (pci_int), 0, 0, 1, irq_state)
#define pci_set_dirq(irq, irq_state) \
        pci_irq(PCI_DIRQ_BASE | (irq), 0, 1, 1, irq_state)
#define pci_set_irq(slot, pci_int, irq_state) \
        pci_irq(slot, pci_int, 0, 1, irq_state)
#define pci_clear_mirq(mirq, level, irq_state) \
        pci_irq(PCI_MIRQ_BASE | (mirq), 0, level, 0, irq_state)
#define pci_clear_iirq(pci_int, irq_state) \
        pci_irq(PCI_IIRQ_BASE | (pci_int), 0, 0, 0, irq_state)
#define pci_clear_dirq(dirq, irq_state) \
        pci_irq(PCI_DIRQ_BASE | (irq), 0, 1, 0, irq_state)
#define pci_clear_irq(slot, pci_int, irq_state) \
        pci_irq(slot, pci_int, 0, 0, irq_state)

enum {
    PCI_CARD_NORTHBRIDGE     = 0,
    PCI_CARD_NORTHBRIDGE_SEC = 1,
    PCI_CARD_AGPBRIDGE       = 2,
    PCI_CARD_SOUTHBRIDGE     = 3,
    PCI_CARD_SOUTHBRIDGE_IDE = 4,
    PCI_CARD_SOUTHBRIDGE_PMU = 5,
    PCI_CARD_SOUTHBRIDGE_USB = 6,
    PCI_CARD_AGP             = 0x0f,
    PCI_CARD_NORMAL          = 0x10,
    PCI_CARD_VIDEO           = 0x11,
    PCI_CARD_HANGUL          = 0x12,
    PCI_CARD_IDE             = 0x13,
    PCI_CARD_SCSI            = 0x14,
    PCI_CARD_SOUND           = 0x15,
    PCI_CARD_MODEM           = 0x16,
    PCI_CARD_NETWORK         = 0x17,
    PCI_CARD_UART            = 0x18,
    PCI_CARD_USB             = 0x19,
    PCI_CARD_BRIDGE          = 0x1a
};

enum {
    PCI_ADD_NORTHBRIDGE     = 0,
    PCI_ADD_NORTHBRIDGE_SEC = 1,
    PCI_ADD_AGPBRIDGE       = 2,
    PCI_ADD_SOUTHBRIDGE     = 3,
    PCI_ADD_SOUTHBRIDGE_IDE = 4,
    PCI_ADD_SOUTHBRIDGE_PMU = 5,
    PCI_ADD_SOUTHBRIDGE_USB = 6,
    PCI_ADD_AGP             = 0x0f,
    PCI_ADD_NORMAL          = 0x10,
    PCI_ADD_VIDEO           = 0x11,
    PCI_ADD_HANGUL          = 0x12,
    PCI_ADD_IDE             = 0x13,
    PCI_ADD_SCSI            = 0x14,
    PCI_ADD_SOUND           = 0x15,
    PCI_ADD_MODEM           = 0x16,
    PCI_ADD_NETWORK         = 0x17,
    PCI_ADD_UART            = 0x18,
    PCI_ADD_USB             = 0x19,
    PCI_ADD_BRIDGE          = 0x1a
};

typedef union {
    uint32_t addr;
    uint8_t addr_regs[4];
} pci_bar_t;

extern int         pci_burst_time;
extern int         agp_burst_time;
extern int         pci_nonburst_time;
extern int         agp_nonburst_time;

extern int         pci_flags;

extern uint32_t    pci_base;
extern uint32_t    pci_size;

extern void        pci_set_irq_routing(int pci_int, int irq);
extern void        pci_set_irq_level(int pci_int, int level);
extern void        pci_enable_mirq(int mirq);
extern void        pci_set_mirq_routing(int mirq, uint8_t irq);
extern uint8_t     pci_get_mirq_level(int mirq);
extern void        pci_set_mirq_level(int mirq, uint8_t irq);

/* PCI raise IRQ: the first parameter is slot if < PCI_MIRQ_BASE, MIRQ if >= PCI_MIRQ_BASE
                  and < PCI_DIRQ_BASE, and direct IRQ line if >= PCI_DIRQ_BASE (RichardG's
                  hack that may no longer be needed). */
extern void        pci_irq(uint8_t slot, uint8_t pci_int, int level, int set, uint8_t *irq_state);

extern uint8_t     pci_get_int(uint8_t slot, uint8_t pci_int);

/* Relocate a PCI device to a new slot, required for the configurable
   IDSEL's of ALi M1543(c). */
extern void        pci_relocate_slot(int type, int new_slot);

/* Write PCI enable/disable key, split for the ALi M1435. */
extern void        pci_key_write(uint8_t val);

/* Set PMC (ie. change PCI configuration mechanism), 0 = #2, 1 = #1. */
extern void        pci_set_pmc(uint8_t pmc);

extern void        pci_pic_reset(void);
extern void        pci_reset(void);

/* Needed for the io.c handling of configuration mechanism #2 ports C000-CFFF. */
extern void        pci_write(uint16_t port, uint8_t val, void *priv);
extern void        pci_writew(uint16_t port, uint16_t val, void *priv);
extern void        pci_writel(uint16_t port, uint32_t val, void *priv);
extern uint8_t     pci_read(uint16_t port, void *priv);
extern uint16_t    pci_readw(uint16_t port, void *priv);
extern uint32_t    pci_readl(uint16_t port, void *priv);

extern uint8_t     pci_register_bus(void);
extern void        pci_remap_bus(uint8_t bus_index, uint8_t bus_number);
extern void        pci_register_bus_slot(int bus, int card, int type, int inta, int intb, int intc, int intd);

/* Add a PCI card. */
extern void        pci_add_card(uint8_t add_type, uint8_t (*read)(int func, int addr, int len, void *priv),
                                void (*write)(int func, int addr, int len, uint8_t val, void *priv), void *priv, uint8_t *slot);

/* Add an instance of the PCI bridge. */
extern void        pci_add_bridge(uint8_t agp, uint8_t (*read)(int func, int addr, int len, void *priv),
                                  void (*write)(int func, int addr, int len, uint8_t val, void *priv), void *priv,
                                  uint8_t *slot);

/* Register the cards that have been added into slots. */
extern void        pci_register_cards(void);

extern void        pci_init(int flags);

/* PCI bridge stuff. */
extern void        pci_bridge_set_ctl(void *priv, uint8_t ctl);
extern uint8_t     pci_bridge_get_bus_index(void *priv);

#ifdef EMU_DEVICE_H
extern const device_t dec21150_device;
extern const device_t dec21152_device;

extern const device_t ali5243_agp_device;
extern const device_t ali5247_agp_device;
extern const device_t i440lx_agp_device;
extern const device_t i440bx_agp_device;
extern const device_t i440gx_agp_device;
extern const device_t via_vp3_agp_device;
extern const device_t via_mvp3_agp_device;
extern const device_t via_apro_agp_device;
extern const device_t via_vt8601_agp_device;
extern const device_t sis_5xxx_agp_device;
#endif

#endif /*EMU_PCI_H*/
