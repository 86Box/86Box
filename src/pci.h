void pci_init(int type, int min_card, int max_card);
void pci_add_specific(int card, uint8_t (*read)(int func, int addr, void *priv), void (*write)(int func, int addr, uint8_t val, void *priv), void *priv);
void pci_add(uint8_t (*read)(int func, int addr, void *priv), void (*write)(int func, int addr, uint8_t val, void *priv), void *priv);

#define PCI_REG_COMMAND 0x04

#define PCI_COMMAND_IO  0x01
#define PCI_COMMAND_MEM 0x02

#define PCI_CONFIG_TYPE_1 1
#define PCI_CONFIG_TYPE_2 2

extern int pci_burst_time, pci_nonburst_time;
