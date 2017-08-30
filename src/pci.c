#include <stdarg.h>

#include "ibm.h"
#include "io.h"
#include "mem.h"
#include "pic.h"
#include "pci.h"


static uint64_t pci_irq_hold[16];

typedef struct
{
	uint8_t			id, type;
	uint8_t			irq_routing[4];
	void (*write)		(int func, int addr, uint8_t val, void *priv);
	uint8_t (*read)		(int func, int addr, void *priv);
	void *			priv;
} pci_card_t;

static pci_card_t pci_cards[32];
static uint8_t last_pci_card = 0;

static uint8_t pci_card_to_slot_mapping[32];

static uint8_t elcr[2] = { 0, 0 };

static uint8_t pci_irqs[4];

static int pci_index, pci_func, pci_card, pci_bus, pci_enable, pci_key;
int pci_burst_time, pci_nonburst_time;

int pci_do_log = 0;

void pci_log(const char *format, ...)
{
#ifdef ENABLE_PCI_LOG
	if (pci_do_log)
	{
		va_list ap;
		va_start(ap, format);
		vprintf(format, ap);
		va_end(ap);
		fflush(stdout);
	}
#endif
}

static void pci_cf8_write(uint16_t port, uint32_t val, void *p)
{
        pci_index = val & 0xff;
        pci_func = (val >> 8) & 7;
        pci_card = (val >> 11) & 31;
        pci_bus = (val >> 16) & 0xff;
        pci_enable = (val >> 31) & 1;
}

static uint32_t pci_cf8_read(uint16_t port, void *p)
{
        return pci_index | (pci_func << 8) | (pci_card << 11) | (pci_bus << 16) | (pci_enable << 31);
}

static void pci_write(uint16_t port, uint8_t val, void *priv)
{
	uint8_t slot = 0;

        switch (port)
        {
                case 0xcfc: case 0xcfd: case 0xcfe: case 0xcff:
                if (!pci_enable) 
                   return;
                   
		if (!pci_bus)
		{
			slot = pci_card_to_slot_mapping[pci_card];
			if (slot != 0xFF)
			{
				if (pci_cards[slot].write)
				{
					/* pci_log("Reading PCI card on slot %02X (pci_cards[%i])...\n", pci_card, slot); */
					pci_cards[slot].write(pci_func, pci_index | (port & 3), val, pci_cards[slot].priv);
				}
			}
		}
                
                break;
        }
}

static uint8_t pci_read(uint16_t port, void *priv)
{
	uint8_t slot = 0;

        switch (port)
        {
                case 0xcfc: case 0xcfd: case 0xcfe: case 0xcff:
                if (!pci_enable) 
                   return 0xff;

		if (!pci_bus)
		{
			slot = pci_card_to_slot_mapping[pci_card];
			if (slot != 0xFF)
			{
				if (pci_cards[slot].read)
				{
					return pci_cards[slot].read(pci_func, pci_index | (port & 3), pci_cards[slot].priv);
				}
			}
		}

                return 0xff;
        }
        return 0xff;
}

static void elcr_write(uint16_t port, uint8_t val, void *priv)
{
	/* pci_log("ELCR%i: WRITE %02X\n", port & 1, val); */
	elcr[port & 1] = val;

	/* printf("ELCR %i: %c %c %c %c %c %c %c %c\n", port & 1, (val & 1) ? 'L' : 'E', (val & 2) ? 'L' : 'E', (val & 4) ? 'L' : 'E', (val & 8) ? 'L' : 'E', (val & 0x10) ? 'L' : 'E', (val & 0x20) ? 'L' : 'E', (val & 0x40) ? 'L' : 'E', (val & 0x80) ? 'L' : 'E'); */
}

static uint8_t elcr_read(uint16_t port, void *priv)
{
	/* pci_log("ELCR%i: READ %02X\n", port & 1, elcr[port & 1]); */
	return elcr[port & 1];
}

static void elcr_reset(void)
{
	pic_reset();
	elcr[0] = elcr[1] = 0;
}

static void pci_type2_write(uint16_t port, uint8_t val, void *priv);
static uint8_t pci_type2_read(uint16_t port, void *priv);

static void pci_type2_write(uint16_t port, uint8_t val, void *priv)
{
	uint8_t slot = 0;

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

		if (!pci_bus)
		{
			slot = pci_card_to_slot_mapping[pci_card];
			if (slot != 0xFF)
			{
				if (pci_cards[slot].write)
				{
					pci_cards[slot].write(pci_func, pci_index | (port & 3), val, pci_cards[slot].priv);
				}
			}
		}
        }
}

static uint8_t pci_type2_read(uint16_t port, void *priv)
{
	uint8_t slot = 0;

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

		if (!pci_bus)
		{
			slot = pci_card_to_slot_mapping[pci_card];
			if (slot != 0xFF)
			{
				if (pci_cards[slot].read)
				{
					return pci_cards[slot].read(pci_func, pci_index | (port & 3), pci_cards[slot].priv);
				}
			}
		}
        }
        return 0xff;
}

void pci_set_irq_routing(int pci_int, int irq)
{
        pci_irqs[pci_int - 1] = irq;
}

static int pci_irq_is_level(int irq)
{
	int real_irq = irq & 7;

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
	/* pci_log("Issuing PCI IRQ %i: ", irq); */
	if (pci_irq_is_level(irq))
	{
		/* pci_log("Level\n"); */
		picintlevel(1 << irq);
	}
	else
	{
		/* pci_log("Edge\n"); */
		picint(1 << irq);
	}
}

void pci_ide_set_irq(int ide_board, int irq)
{
	if (pci_irq_is_level(irq) && (pci_irq_hold[irq] & (1LL << (0x20LL + ide_board))))
	{
		/* IRQ already held, do nothing. */
		return;
	}

	if (!pci_irq_is_level(irq) || !pci_irq_hold[irq])
	{
		/* Only raise the interrupt if it's edge-triggered or level-triggered and not yet being held. */
		pci_issue_irq(irq);
	}

	/* If the IRQ is level-triggered, mark that this card is holding it. */
	if (pci_irq_is_level(irq))
	{
		pci_irq_hold[irq] |= (1LL << (0x20LL + ide_board));
	}
}

void pci_set_irq(uint8_t card, uint8_t pci_int)
{
	uint8_t slot = 0;
	uint8_t irq_routing = 0;
	uint8_t pci_int_index = pci_int - PCI_INTA;
	uint8_t irq_line = 0;
	uint8_t level = 0;

	if (!last_pci_card)
	{
		pci_log("pci_set_irq(%02X, %02X): No PCI slots (how are we even here?!)\n", card, pci_int);
		return;
	}
	else
	{
		pci_log("pci_set_irq(%02X, %02X): %i PCI slots\n", card, pci_int, last_pci_card);
	}

	slot = pci_card_to_slot_mapping[card];

	if (slot == 0xFF)
	{
		pci_log("pci_set_irq(%02X, %02X): Card is not on a PCI slot (how are we even here?!)\n", card, pci_int);
		return;
	}
	else
	{
		pci_log("pci_set_irq(%02X, %02X): Card is on PCI slot %02X\n", card, pci_int, slot);
	}

	if (!pci_cards[slot].irq_routing[pci_int_index])
	{
		pci_log("pci_set_irq(%02X, %02X): No IRQ routing for this slot and INT pin combination\n", card, pci_int);
		return;
	}
	else
	{
		irq_routing = (pci_cards[slot].irq_routing[pci_int_index] - PCI_INTA) & 3;
		pci_log("pci_set_irq(%02X, %02X): IRQ routing for this slot and INT pin combination: %02X\n", card, pci_int, irq_routing);
	}

	if (pci_irqs[irq_routing] > 0x0F)
	{
		pci_log("pci_set_irq(%02X, %02X): IRQ line is disabled\n", card, pci_int);
		return;
	}
	else
	{
		irq_line = pci_irqs[irq_routing];
		pci_log("pci_set_irq(%02X, %02X): Using IRQ %i\n", card, pci_int, irq_line);
	}

	if (pci_irq_is_level(irq_line) && (pci_irq_hold[irq_line] & (1 << card)))
	{
		/* IRQ already held, do nothing. */
		pci_log("pci_set_irq(%02X, %02X): Card is already holding the IRQ\n", card, pci_int);
		return;
	}
	else
	{
		pci_log("pci_set_irq(%02X, %02X): Card not yet holding the IRQ\n", card, pci_int);
	}

	level = pci_irq_is_level(irq_line);

	if (!level || !pci_irq_hold[irq_line])
	{
		pci_log("pci_set_irq(%02X, %02X): Issuing %s-triggered IRQ (%sheld)\n", card, pci_int, level ? "level" : "edge", pci_irq_hold[irq_line] ? "" : "not ");

		/* Only raise the interrupt if it's edge-triggered or level-triggered and not yet being held. */
		pci_issue_irq(irq_line);
	}
	else if (level && pci_irq_hold[irq_line])
	{
		pci_log("pci_set_irq(%02X, %02X): IRQ line already being held\n", card, pci_int);
	}

	/* If the IRQ is level-triggered, mark that this card is holding it. */
	if (pci_irq_is_level(irq_line))
	{
		pci_log("pci_set_irq(%02X, %02X): Marking that this card is holding the IRQ\n", card, pci_int);
		pci_irq_hold[irq_line] |= (1 << card);
	}
	else
	{
		pci_log("pci_set_irq(%02X, %02X): Edge-triggered interrupt, not marking\n", card, pci_int);
	}
}

void pci_ide_clear_irq(int ide_board, int irq)
{
	if (pci_irq_is_level(irq))
	{
		pci_irq_hold[irq] &= ~(1LL << (0x20LL + ide_board));
                if (!pci_irq_hold[irq])
		{
               	        picintc(1 << irq);
		}
	}
	else
	{
		picintc(1 << irq);
	}
}

void pci_clear_irq(uint8_t card, uint8_t pci_int)
{
	uint8_t slot = 0;
	uint8_t irq_routing = 0;
	uint8_t pci_int_index = pci_int - PCI_INTA;
	uint8_t irq_line = 0;
	uint8_t level = 0;

	if (!last_pci_card)
	{
		pci_log("pci_clear_irq(%02X, %02X): No PCI slots (how are we even here?!)\n", card, pci_int);
		return;
	}
	else
	{
		pci_log("pci_clear_irq(%02X, %02X): %i PCI slots\n", card, pci_int, last_pci_card);
	}

	slot = pci_card_to_slot_mapping[card];

	if (slot == 0xFF)
	{
		pci_log("pci_clear_irq(%02X, %02X): Card is not on a PCI slot (how are we even here?!)\n", card, pci_int);
		return;
	}
	else
	{
		pci_log("pci_clear_irq(%02X, %02X): Card is on PCI slot %02X\n", card, pci_int, slot);
	}

	if (!pci_cards[slot].irq_routing[pci_int_index])
	{
		pci_log("pci_clear_irq(%02X, %02X): No IRQ routing for this slot and INT pin combination\n", card, pci_int);
		return;
	}
	else
	{
		irq_routing = (pci_cards[slot].irq_routing[pci_int_index] - PCI_INTA) & 3;
		pci_log("pci_clear_irq(%02X, %02X): IRQ routing for this slot and INT pin combination: %02X\n", card, pci_int, irq_routing);
	}

	if (pci_irqs[irq_routing] > 0x0F)
	{
		pci_log("pci_clear_irq(%02X, %02X): IRQ line is disabled\n", card, pci_int);
		return;
	}
	else
	{
		irq_line = pci_irqs[irq_routing];
		pci_log("pci_clear_irq(%02X, %02X): Using IRQ %i\n", card, pci_int, irq_line);
	}

	if (pci_irq_is_level(irq_line) && !(pci_irq_hold[irq_line] & (1 << card)))
	{
		/* IRQ not held, do nothing. */
		pci_log("pci_clear_irq(%02X, %02X): Card is not holding the IRQ\n", card, pci_int);
		return;
	}

	level = pci_irq_is_level(irq_line);

	if (level)
	{
		pci_log("pci_clear_irq(%02X, %02X): Releasing this card's hold on the IRQ\n", card, pci_int);
		pci_irq_hold[irq_line] &= ~(1 << card);

                if (!pci_irq_hold[irq_line])
		{
			pci_log("pci_clear_irq(%02X, %02X): IRQ no longer held by any card, clearing it\n", card, pci_int);
               	        picintc(1 << irq_line);
		}
		else
		{
			pci_log("pci_clear_irq(%02X, %02X): IRQ is still being held\n", card, pci_int);
		}
	}
	else
	{
		pci_log("pci_clear_irq(%02X, %02X): Clearing edge-triggered interrupt\n", card, pci_int);
		picintc(1 << irq_line);
	}
}

void pci_reset(void)
{
	int i = 0;

	for (i = 0; i < 16; i++)
	{
		if (pci_irq_hold[i])
		{
			pci_irq_hold[i] = 0;

			picintc(1 << i);
		}
	}

	elcr_reset();
}

static void pci_slots_clear(void)
{
	uint8_t i = 0;
	uint8_t j = 0;

	last_pci_card = 0;

	for (i = 0; i < 32; i++)
	{
		pci_cards[i].id = 0xFF;
		pci_cards[i].type = 0xFF;

		for (j = 0; j < 4; j++)
		{
			pci_cards[i].irq_routing[j] = 0;
		}

		pci_cards[i].read = NULL;
		pci_cards[i].write = NULL;
		pci_cards[i].priv = NULL;

		pci_card_to_slot_mapping[i] = 0xFF;
	}
}

void pci_init(int type)
{
        int c;

        PCI = 1;

	pci_slots_clear();

	pci_reset();

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
        
	for (c = 0; c < 4; c++)
	{
		pci_irqs[c] = PCI_IRQ_DISABLED;
	}
}

void pci_register_slot(int card, int type, int inta, int intb, int intc, int intd)
{
	pci_cards[last_pci_card].id = card;
	pci_cards[last_pci_card].type = type;
	pci_cards[last_pci_card].irq_routing[0] = inta;
	pci_cards[last_pci_card].irq_routing[1] = intb;
	pci_cards[last_pci_card].irq_routing[2] = intc;
	pci_cards[last_pci_card].irq_routing[3] = intd;
	pci_cards[last_pci_card].read = NULL;
	pci_cards[last_pci_card].write = NULL;
	pci_cards[last_pci_card].priv = NULL;
	pci_card_to_slot_mapping[card] = last_pci_card;
	pci_log("pci_register_slot(): pci_cards[%i].id = %02X\n", last_pci_card, card);
	last_pci_card++;
}

uint8_t pci_add_card(uint8_t add_type, uint8_t (*read)(int func, int addr, void *priv), void (*write)(int func, int addr, uint8_t val, void *priv), void *priv)
{
	uint8_t i = 0;

	if (add_type < PCI_ADD_NORMAL)
	{
		pci_log("pci_add_card(): Adding PCI CARD at specific slot %02X [SPECIFIC]\n", add_type);
	}

	if (!PCI)
	{
		pci_log("pci_add_card(): Adding PCI CARD failed (non-PCI machine) [%s]\n", (add_type == PCI_ADD_NORMAL) ? "NORMAL" : ((add_type == PCI_ADD_VIDEO) ? "VIDEO" : "SPECIFIC"));
		return 0xFF;
	}

	if (!last_pci_card)
	{
		pci_log("pci_add_card(): Adding PCI CARD failed (no PCI slots) [%s]\n", (add_type == PCI_ADD_NORMAL) ? "NORMAL" : ((add_type == PCI_ADD_VIDEO) ? "VIDEO" : "SPECIFIC"));
		return 0xFF;
	}

	for (i = 0; i < last_pci_card; i++)
	{
                if (!pci_cards[i].read && !pci_cards[i].write)
		{
			if (((pci_cards[i].type == PCI_CARD_NORMAL) && (add_type >= PCI_ADD_NORMAL)) ||
			    ((pci_cards[i].type == PCI_CARD_ONBOARD) && (add_type == PCI_ADD_VIDEO)) ||
			    ((pci_cards[i].id == add_type) && (add_type < PCI_ADD_NORMAL)))
			{
				pci_cards[i].read = read;
				pci_cards[i].write = write;
				pci_cards[i].priv = priv;
				pci_log("pci_add_card(): Adding PCI CARD to pci_cards[%i] (slot %02X) [%s]\n", i, pci_cards[i].id, (add_type == PCI_ADD_NORMAL) ? "NORMAL" : ((add_type == PCI_ADD_VIDEO) ? "VIDEO" : "SPECIFIC"));
				return pci_cards[i].id;
			}
		}
	}

	pci_log("pci_add_card(): Adding PCI CARD failed (unable to find a suitable PCI slot) [%s]\n", (add_type == PCI_ADD_NORMAL) ? "NORMAL" : ((add_type == PCI_ADD_VIDEO) ? "VIDEO" : "SPECIFIC"));
	return 0xFF;
}
