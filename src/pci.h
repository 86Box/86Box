void pci_init(int type);
void pci_slot(int card);
void pci_add_specific(int card, uint8_t (*read)(int func, int addr, void *priv), void (*write)(int func, int addr, uint8_t val, void *priv), void *priv);
int pci_add(uint8_t (*read)(int func, int addr, void *priv), void (*write)(int func, int addr, uint8_t val, void *priv), void *priv);
void pci_set_irq_routing(int card, int irq);
void pci_set_card_routing(int card, int pci_int);
void pci_set_irq(int card, int pci_int);
void pci_clear_irq(int card, int pci_int);

#define PCI_REG_COMMAND 0x04

#define PCI_COMMAND_IO  0x01
#define PCI_COMMAND_MEM 0x02

#define PCI_CONFIG_TYPE_1 1
#define PCI_CONFIG_TYPE_2 2

#define PCI_INTA 1
#define PCI_INTB 2
#define PCI_INTC 3
#define PCI_INTD 4

#define PCI_IRQ_DISABLED -1

extern int pci_burst_time, pci_nonburst_time;
