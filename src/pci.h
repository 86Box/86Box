void pci_set_irq_routing(int pci_int, int irq);

void pci_enable_mirq(int mirq);
void pci_set_mirq_routing(int mirq, int irq);

uint8_t pci_use_mirq(uint8_t mirq);

int pci_irq_is_level(int irq);

void pci_set_mirq(uint8_t mirq);
void pci_set_irq(uint8_t card, uint8_t pci_int);
void pci_clear_mirq(uint8_t mirq);
void pci_clear_irq(uint8_t card, uint8_t pci_int);

void pci_reset(void);
void pci_init(int type);
void pci_register_slot(int card, int type, int inta, int intb, int intc, int intd);
uint8_t pci_add_card(uint8_t add_type, uint8_t (*read)(int func, int addr, void *priv), void (*write)(int func, int addr, uint8_t val, void *priv), void *priv);

#define PCI_REG_COMMAND 0x04

#define PCI_COMMAND_IO  0x01
#define PCI_COMMAND_MEM 0x02

#define PCI_CONFIG_TYPE_1 1
#define PCI_CONFIG_TYPE_2 2

#define PCI_INTA 1
#define PCI_INTB 2
#define PCI_INTC 3
#define PCI_INTD 4

#define PCI_MIRQ0 0
#define PCI_MIRQ1 1

#define PCI_IRQ_DISABLED -1

enum
{
	PCI_CARD_NORMAL = 0,
	PCI_CARD_ONBOARD,
	PCI_CARD_SPECIAL
};


#define PCI_ADD_NORMAL	0x80
#define PCI_ADD_VIDEO	0x81

extern int pci_burst_time, pci_nonburst_time;

typedef union {
    uint32_t addr;
    uint8_t addr_regs[4];
} bar_t;

typedef struct PCI_RESET
{
        void (*pci_master_reset)(void);
        void (*pci_set_reset)(void);
        void (*super_io_reset)(void);
} PCI_RESET;

extern PCI_RESET pci_reset_handler;

extern void     trc_init(void);
