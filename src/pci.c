#include "ibm.h"
#include "io.h"
#include "mem.h"
#include "pic.h"
#include "pci.h"


void    (*pci_card_write[32])(int func, int addr, uint8_t val, void *priv);
uint8_t  (*pci_card_read[32])(int func, int addr, void *priv);
void           *pci_priv[32];
static int pci_irq_routing[32];
/* static int pci_irq_active[32]; */
static int pci_irqs[4];
static int pci_card_valid[32];
static int pci_irq_hold[16];

static int pci_index, pci_func, pci_card, pci_bus, pci_enable, pci_key;
int pci_burst_time, pci_nonburst_time;

void pci_cf8_write(uint16_t port, uint32_t val, void *p)
{
        pci_index = val & 0xff;
        pci_func = (val >> 8) & 7;
        pci_card = (val >> 11) & 31;
        pci_bus = (val >> 16) & 0xff;
        pci_enable = (val >> 31) & 1;
}

uint32_t pci_cf8_read(uint16_t port, void *p)
{
        return pci_index | (pci_func << 8) | (pci_card << 11) | (pci_bus << 16) | (pci_enable << 31);
}

void pci_write(uint16_t port, uint8_t val, void *priv)
{
        switch (port)
        {
                case 0xcfc: case 0xcfd: case 0xcfe: case 0xcff:
                if (!pci_enable) 
                   return;
                   
                if (!pci_bus && pci_card_write[pci_card])
                   pci_card_write[pci_card](pci_func, pci_index | (port & 3), val, pci_priv[pci_card]);
                
                break;
        }
}

uint8_t pci_read(uint16_t port, void *priv)
{
        switch (port)
        {
                case 0xcfc: case 0xcfd: case 0xcfe: case 0xcff:
                if (!pci_enable) 
                   return 0xff;

                if (!pci_bus && pci_card_read[pci_card])
                   return pci_card_read[pci_card](pci_func, pci_index | (port & 3), pci_priv[pci_card]);

                return 0xff;
        }
        return 0xff;
}

uint8_t elcr[2] = { 0, 0 };

void elcr_write(uint16_t port, uint8_t val, void *priv)
{
	/* pclog("ELCR%i: WRITE %02X\n", port & 1, val); */
	elcr[port & 1] = val;

	/* printf("ELCR %i: %c %c %c %c %c %c %c %c\n", port & 1, (val & 1) ? 'L' : 'E', (val & 2) ? 'L' : 'E', (val & 4) ? 'L' : 'E', (val & 8) ? 'L' : 'E', (val & 0x10) ? 'L' : 'E', (val & 0x20) ? 'L' : 'E', (val & 0x40) ? 'L' : 'E', (val & 0x80) ? 'L' : 'E'); */
}

uint8_t elcr_read(uint16_t port, void *priv)
{
	/* pclog("ELCR%i: READ %02X\n", port & 1, elcr[port & 1]); */
	return elcr[port & 1];
}

void elcr_reset(void)
{
	int i = 0;

	pic_reset();
	elcr[0] = elcr[1] = 0;

#if 0
	for (i = 0; i < 32; i++)
	{
		pci_irq_active[i] = 0;
	}
#endif

	for (i = 0; i < 16; i++)
	{
		pci_irq_hold[i] = 0;
	}
}

void pci_type2_write(uint16_t port, uint8_t val, void *priv);
uint8_t pci_type2_read(uint16_t port, void *priv);

void pci_type2_write(uint16_t port, uint8_t val, void *priv)
{
        if (port == 0xcf8)
        {
                pci_func = (val >> 1) & 7;
                if (!pci_key && (val & 0xf0))
                        io_sethandler(0xc000, 0x1000, pci_type2_read, NULL, NULL, pci_type2_write, NULL, NULL, NULL);
                else
                        io_removehandler(0xc000, 0x1000, pci_type2_read, NULL, NULL, pci_type2_write, NULL, NULL, NULL);
                pci_key = val & 0xf0;
        }
        else if (port == 0xcfa)
        {
                pci_bus = val;
        }
        else
        {
                pci_card = (port >> 8) & 0xf;
                pci_index = port & 0xff;

                if (!pci_bus && pci_card_write[pci_card])
                        pci_card_write[pci_card](pci_func, pci_index | (port & 3), val, pci_priv[pci_card]);
        }
}

uint8_t pci_type2_read(uint16_t port, void *priv)
{
        if (port == 0xcf8)
        {
                return pci_key | (pci_func << 1);
        }
        else if (port == 0xcfa)
        {
                return pci_bus;
        }
        else
        {
                pci_card = (port >> 8) & 0xf;
                pci_index = port & 0xff;

                if (!pci_bus && pci_card_write[pci_card])
                        return pci_card_read[pci_card](pci_func, pci_index | (port & 3), pci_priv[pci_card]);
        }
        return 0xff;
}

void pci_set_irq_routing(int pci_int, int irq)
{
        pci_irqs[pci_int - 1] = irq;
}

void pci_set_card_routing(int card, int pci_int)
{
        pci_irq_routing[card] = pci_int;
}

int pci_irq_is_level(int irq)
{
	int real_irq = irq & 7;
	/* int irq_elcr = 0; */

	if (irq > 7)
	{
		return !!(elcr[1] & (1 << real_irq));
	}
	else
	{
		return !!(elcr[0] & (1 << real_irq));
	}
}

void pci_issue_irq(int irq)
{
	if (pci_irq_is_level(irq))
	{
		picintlevel(1 << irq);
	}
	else
	{
		picint(1 << irq);
	}
}

void pci_set_irq(int card, int pci_int)
{
	int irq = ((pci_int - PCI_INTA) + (pci_irq_routing[card] - PCI_INTA)) & 3;

        if (pci_irq_routing[card])
        {
                if (pci_irqs[irq] != PCI_IRQ_DISABLED/* && !pci_irq_active[card] */)
			pci_issue_irq(pci_irqs[irq]);
                /* pci_irq_active[card] = 1; */
		/* If the IRQ is set to edge, there is no need to hold it. */
		if (!pci_irq_is_level(pci_irqs[irq]))
		{
			pci_irq_hold[pci_irqs[irq]] |= (1 << card);
		}
        }
}

void pci_clear_irq(int card, int pci_int)
{
	int irq = ((pci_int - PCI_INTA) + (pci_irq_routing[card] - PCI_INTA)) & 3;

	/* Do not clear the interrupt until we're the last card being serviced. */
        if (pci_irq_routing[card])
        {
		pci_irq_hold[pci_irqs[irq]] &= ~(1 << card);
                if (pci_irqs[irq] != PCI_IRQ_DISABLED/* && pci_irq_active[card]*/ && !pci_irq_hold[pci_irqs[irq]])
                        picintc(1 << pci_irqs[irq]);
                /* pci_irq_active[card] = 0; */
        }
}

void pci_init(int type)
{
        int c;

        PCI = 1;

	elcr_reset();

	io_sethandler(0x04d0, 0x0002, elcr_read, NULL, NULL, elcr_write, NULL, NULL,  NULL);
        
        if (type == PCI_CONFIG_TYPE_1)
        {
                io_sethandler(0x0cf8, 0x0001, NULL, NULL, pci_cf8_read, NULL, NULL, pci_cf8_write,  NULL);
                io_sethandler(0x0cfc, 0x0004, pci_read, NULL, NULL, pci_write, NULL, NULL,  NULL);
        }
        else
        {
                io_sethandler(0x0cf8, 0x0001, pci_type2_read, NULL, NULL, pci_type2_write, NULL, NULL, NULL);
                io_sethandler(0x0cfa, 0x0001, pci_type2_read, NULL, NULL, pci_type2_write, NULL, NULL, NULL);
        }
        
        for (c = 0; c < 32; c++)
        {
                pci_card_read[c] = NULL;
                pci_card_write[c] = NULL;
                pci_priv[c] = NULL;
                pci_irq_routing[c] = 0;
                /* pci_irq_active[c] = 0; */
                pci_card_valid[c] = 0;
        }
        
        for (c = 0; c < 4; c++)
                pci_irqs[c] = PCI_IRQ_DISABLED;
}

void pci_slot(int card)
{
        pci_card_valid[card] = 1;
}

void pci_add_specific(int card, uint8_t (*read)(int func, int addr, void *priv), void (*write)(int func, int addr, uint8_t val, void *priv), void *priv)
{
         pci_card_read[card] = read;
        pci_card_write[card] = write;
              pci_priv[card] = priv;
}

int pci_add(uint8_t (*read)(int func, int addr, void *priv), void (*write)(int func, int addr, uint8_t val, void *priv), void *priv)
{
        int c;
        
        for (c = 0; c < 32; c++)
        {
                if (pci_card_valid[c] && !pci_card_read[c] && !pci_card_write[c])
                {
                         pci_card_read[c] = read;
                        pci_card_write[c] = write;
                              pci_priv[c] = priv;
                        return c;
                }
        }
        
        return -1;
}
