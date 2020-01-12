/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation the PCI bus.
 *
 * Version:	@(#)pci.c	1.0.6	2020/01/11
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2017-2020 Fred N. van Kempen.
 *		Copyright 2008-2020 Sarah Walker.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "86box.h"
#include "machine/machine.h"
#include "cpu/cpu.h"
#include "io.h"
#include "pic.h"
#include "mem.h"
#include "device.h"
#include "pci.h"
#include "piix.h"
#include "keyboard.h"


typedef struct {
    uint8_t	id, type;
    uint8_t	irq_routing[4];

    void	*priv;
    void	(*write)(int func, int addr, uint8_t val, void *priv);
    uint8_t	(*read)(int func, int addr, void *priv);
} pci_card_t;

typedef struct {
    uint8_t	enabled;
    uint8_t	irq_line;
} pci_mirq_t;


int			pci_burst_time,
			pci_nonburst_time;

static pci_card_t	pci_cards[32];
static uint8_t		last_pci_card = 0;
static uint8_t		pci_card_to_slot_mapping[32];
static uint8_t		elcr[2] = { 0, 0 };
static uint8_t		pci_irqs[4], pci_irq_level[4];
static uint64_t		pci_irq_hold[16];
static pci_mirq_t	pci_mirqs[3];
static int		pci_type,
			pci_index,
			pci_func,
			pci_card,
			pci_bus,
			pci_enable,
			pci_key;
static int		trc_reg = 0, elcr_enabled = 1;


#ifdef ENABLE_PCI_LOG
int pci_do_log = ENABLE_PCI_LOG;


static void
pci_log(const char *fmt, ...)
{
    va_list ap;

    if (pci_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define pci_log(fmt, ...)
#endif


static void
pci_cf8_write(uint16_t port, uint32_t val, void *priv)
{
    pci_index = val & 0xff;
    pci_func = (val >> 8) & 7;
    pci_card = (val >> 11) & 31;
    pci_bus = (val >> 16) & 0xff;
    pci_enable = (val >> 31) & 1;
}


static uint32_t
pci_cf8_read(uint16_t port, void *priv)
{
    return pci_index | (pci_func << 8) |
	   (pci_card << 11) | (pci_bus << 16) | (pci_enable << 31);
}


static void
pci_write(uint16_t port, uint8_t val, void *priv)
{
    uint8_t slot = 0;

    switch (port) {
	case 0xcfc: case 0xcfd: case 0xcfe: case 0xcff:
		if (! pci_enable) 
			return;
		   
		if (! pci_bus) {
			slot = pci_card_to_slot_mapping[pci_card];
			if (slot != 0xff) {
				if (pci_cards[slot].write) {
					pci_log("Writing to PCI card on slot %02X (pci_cards[%i]) (%02X:%02X)...\n", pci_card, slot, pci_func, pci_index);
					pci_cards[slot].write(pci_func, pci_index | (port & 3), val, pci_cards[slot].priv);
				}
			}
		}
		
		break;
    }
}


static uint8_t
pci_read(uint16_t port, void *priv)
{
    uint8_t slot = 0;

    switch (port) {
	case 0xcfc: case 0xcfd: case 0xcfe: case 0xcff:
		if (! pci_enable) 
			return 0xff;

		if (! pci_bus) {
			slot = pci_card_to_slot_mapping[pci_card];
			if (slot != 0xff) {
				if (pci_cards[slot].read)
					return pci_cards[slot].read(pci_func, pci_index | (port & 3), pci_cards[slot].priv);
			}
		}

		return 0xff;
    }

    return 0xff;
}


static void
elcr_write(uint16_t port, uint8_t val, void *priv)
{
    pci_log("ELCR%i: WRITE %02X\n", port & 1, val);

    if (port & 1)
	val &= 0xde;
    else
	val &= 0xf8;

    elcr[port & 1] = val;

    pci_log("ELCR %i: %c %c %c %c %c %c %c %c\n",
	port & 1,
	(val & 1) ? 'L' : 'E',
	(val & 2) ? 'L' : 'E',
	(val & 4) ? 'L' : 'E',
	(val & 8) ? 'L' : 'E',
	(val & 0x10) ? 'L' : 'E',
	(val & 0x20) ? 'L' : 'E',
	(val & 0x40) ? 'L' : 'E',
	(val & 0x80) ? 'L' : 'E');
}


static uint8_t
elcr_read(uint16_t port, void *priv)
{
    pci_log("ELCR%i: READ %02X\n", port & 1, elcr[port & 1]);

    return elcr[port & 1];
}


static void
elcr_reset(void)
{
    pic_reset();

    elcr[0] = 0x00;
    elcr[1] = 0x00;
}


static void	pci_type2_write(uint16_t port, uint8_t val, void *priv);
static uint8_t	pci_type2_read(uint16_t port, void *priv);


static void
pci_type2_write(uint16_t port, uint8_t val, void *priv)
{
    uint8_t slot = 0;

    if (port == 0xcf8) {
	pci_func = (val >> 1) & 7;

	if (!pci_key && (val & 0xf0))
		io_sethandler(0xc000, 0x1000,
			      pci_type2_read, NULL, NULL,
			      pci_type2_write, NULL, NULL, priv);
	else
		io_removehandler(0xc000, 0x1000,
				 pci_type2_read, NULL, NULL,
				 pci_type2_write, NULL, NULL, priv);

	pci_key = val & 0xf0;
    } else if (port == 0xcfa) {
	pci_bus = val;
    } else {
	pci_card = (port >> 8) & 0xf;
	pci_index = port & 0xff;

	if (! pci_bus) {
		slot = pci_card_to_slot_mapping[pci_card];
		if (slot != 0xff) {
			if (pci_cards[slot].write) {
				pci_cards[slot].write(pci_func, pci_index | (port & 3), val, pci_cards[slot].priv);
			}
		}
	}
    }
}


static uint8_t
pci_type2_read(uint16_t port, void *priv)
{
    uint8_t slot = 0;

    if (port == 0xcf8)
	return pci_key | (pci_func << 1);

    if (port == 0xcfa)
	return pci_bus;

    pci_card = (port >> 8) & 0xf;
    pci_index = port & 0xff;

    if (! pci_bus) {
	slot = pci_card_to_slot_mapping[pci_card];
	if (slot != 0xff) {
		if (pci_cards[slot].read) {
			return pci_cards[slot].read(pci_func, pci_index | (port & 3), pci_cards[slot].priv);
		}
	}
    }

    return 0xff;
}


void
pci_set_irq_routing(int pci_int, int irq)
{
    pci_irqs[pci_int - 1] = irq;
}


void
pci_set_irq_level(int pci_int, int level)
{
    pci_irq_level[pci_int - 1] = !!level;
}


void
pci_enable_mirq(int mirq)
{
    pci_mirqs[mirq].enabled = 1;
}


void
pci_set_mirq_routing(int mirq, int irq)
{
    pci_mirqs[mirq].irq_line = irq;
}


int
pci_irq_is_level(int irq)
{
    int real_irq = irq & 7;

    if (elcr_enabled) {
	if ((irq <= 2) || (irq == 8) || (irq == 13))
		return 0;

	if (irq > 7)
		return !!(elcr[1] & (1 << real_irq));

	return !!(elcr[0] & (1 << real_irq));
    } else {
	if (irq < 8)
		return (pic.icw1 & 8) ? 1 : 0;
	else
		return (pic2.icw1 & 8) ? 1 : 0;
    }
}


uint8_t
pci_use_mirq(uint8_t mirq)
{
    if (!PCI || !pci_mirqs[mirq].enabled)
	return 0;

    if (pci_mirqs[mirq].irq_line & 0x80)
	return 0;

    return 1;
}


void
pci_set_mirq(uint8_t mirq, int level)
{
    uint8_t irq_line = 0;

    if (! pci_mirqs[mirq].enabled) {
	pci_log("pci_set_mirq(%02X): MIRQ0 disabled\n", mirq);
	return;
    }

    if (pci_mirqs[mirq].irq_line > 0x0f) {
	pci_log("pci_set_mirq(%02X): IRQ line is disabled\n", mirq);
	return;
    }

    irq_line = pci_mirqs[mirq].irq_line;
    pci_log("pci_set_mirq(%02X): Using IRQ %i\n", mirq, irq_line);

    if (level && (pci_irq_hold[irq_line] & (1ULL << (0x1E + mirq)))) {
	/* IRQ already held, do nothing. */
	pci_log("pci_set_mirq(%02X): MIRQ is already holding the IRQ\n", mirq);
	return;
    }
    pci_log("pci_set_mirq(%02X): MIRQ not yet holding the IRQ\n", mirq);

    if (!level || !pci_irq_hold[irq_line]) {
	pci_log("pci_set_mirq(%02X): Issuing %s-triggered IRQ (%sheld)\n", mirq, level ? "level" : "edge", pci_irq_hold[irq_line] ? "" : "not ");

	/* Only raise the interrupt if it's edge-triggered or level-triggered and not yet being held. */
	if (level)
		picintlevel(1 << irq_line);
	else
		picint(1 << irq_line);
    } else if (level && pci_irq_hold[irq_line]) {
	pci_log("pci_set_mirq(%02X): IRQ line already being held\n", mirq);
    }

    /* If the IRQ is level-triggered, mark that this MIRQ is holding it. */
    if (level) {
	pci_log("pci_set_mirq(%02X): Marking that this card is holding the IRQ\n", mirq);
	pci_irq_hold[irq_line] |= (1ULL << (0x1E + mirq));
    }

    pci_log("pci_set_mirq(%02X): Edge-triggered interrupt, not marking\n", mirq);
}


void
pci_set_irq(uint8_t card, uint8_t pci_int)
{
    uint8_t slot = 0;
    uint8_t irq_routing = 0;
    uint8_t pci_int_index = pci_int - PCI_INTA;
    uint8_t irq_line = 0;
    uint8_t level = 0;

    if (! last_pci_card) {
	pci_log("pci_set_irq(%02X, %02X): No PCI slots (how are we even here?!)\n", card, pci_int);
	return;
    }
    pci_log("pci_set_irq(%02X, %02X): %i PCI slots\n", card, pci_int, last_pci_card);

    slot = pci_card_to_slot_mapping[card];
    if (slot == 0xff) {
	pci_log("pci_set_irq(%02X, %02X): Card is not on a PCI slot (how are we even here?!)\n", card, pci_int);
	return;
    }
    pci_log("pci_set_irq(%02X, %02X): Card is on PCI slot %02X\n", card, pci_int, slot);

    if (! pci_cards[slot].irq_routing[pci_int_index]) {
	pci_log("pci_set_irq(%02X, %02X): No IRQ routing for this slot and INT pin combination\n", card, pci_int);
	return;
    }

    if (pci_type & PCI_NO_IRQ_STEERING)
	irq_line = pci_cards[slot].read(0, 0x3c, pci_cards[slot].priv);
    else {
	irq_routing = (pci_cards[slot].irq_routing[pci_int_index] - PCI_INTA) & 3;
	pci_log("pci_set_irq(%02X, %02X): IRQ routing for this slot and INT pin combination: %02X\n", card, pci_int, irq_routing);

	irq_line = pci_irqs[irq_routing];
	level = pci_irq_level[irq_routing];
    }

    if (irq_line > 0x0f) {
	pci_log("pci_set_irq(%02X, %02X): IRQ line is disabled\n", card, pci_int);
	return;
    } else
	pci_log("pci_set_irq(%02X, %02X): Using IRQ %i\n", card, pci_int, irq_line);

    if (pci_irq_is_level(irq_line) && (pci_irq_hold[irq_line] & (1ULL << card))) {
	/* IRQ already held, do nothing. */
	pci_log("pci_set_irq(%02X, %02X): Card is already holding the IRQ\n", card, pci_int);
	return;
    }
    pci_log("pci_set_irq(%02X, %02X): Card not yet holding the IRQ\n", card, pci_int);

    if (!level || !pci_irq_hold[irq_line]) {
	pci_log("pci_set_irq(%02X, %02X): Issuing %s-triggered IRQ (%sheld)\n", card, pci_int, level ? "level" : "edge", pci_irq_hold[irq_line] ? "" : "not ");

	/* Only raise the interrupt if it's edge-triggered or level-triggered and not yet being held. */
	if (level)
		picintlevel(1 << irq_line);
	else
		picint(1 << irq_line);
    } else if (level && pci_irq_hold[irq_line]) {
	pci_log("pci_set_irq(%02X, %02X): IRQ line already being held\n", card, pci_int);
    }

    /* If the IRQ is level-triggered, mark that this card is holding it. */
    if (level) {
	pci_log("pci_set_irq(%02X, %02X): Marking that this card is holding the IRQ\n", card, pci_int);
	pci_irq_hold[irq_line] |= (1ULL << card);
    } else {
	pci_log("pci_set_irq(%02X, %02X): Edge-triggered interrupt, not marking\n", card, pci_int);
    }
}


void
pci_clear_mirq(uint8_t mirq, int level)
{
    uint8_t irq_line = 0;

    if (mirq > 1) {
	pci_log("pci_clear_mirq(%02X): Invalid MIRQ\n", mirq);
	return;
    }

    if (! pci_mirqs[mirq].enabled) {
	pci_log("pci_clear_mirq(%02X): MIRQ0 disabled\n", mirq);
	return;
    }

    if (pci_mirqs[mirq].irq_line > 0x0f) {
	pci_log("pci_clear_mirq(%02X): IRQ line is disabled\n", mirq);
	return;
    }

    irq_line = pci_mirqs[mirq].irq_line;
    pci_log("pci_clear_mirq(%02X): Using IRQ %i\n", mirq, irq_line);

    if (level && !(pci_irq_hold[irq_line] & (1ULL << (0x1E + mirq)))) {
	/* IRQ not held, do nothing. */
	pci_log("pci_clear_mirq(%02X): MIRQ is not holding the IRQ\n", mirq);
	return;
    }

    if (level) {
	pci_log("pci_clear_mirq(%02X): Releasing this MIRQ's hold on the IRQ\n", mirq);
	pci_irq_hold[irq_line] &= ~(1 << (0x1E + mirq));

	if (! pci_irq_hold[irq_line]) {
		pci_log("pci_clear_mirq(%02X): IRQ no longer held by any card, clearing it\n", mirq);
       		picintc(1 << irq_line);
	} else {
		pci_log("pci_clear_mirq(%02X): IRQ is still being held\n", mirq);
	}
    } else {
	pci_log("pci_clear_mirq(%02X): Clearing edge-triggered interrupt\n", mirq);
	picintc(1 << irq_line);
   }
}


void
pci_clear_irq(uint8_t card, uint8_t pci_int)
{
    uint8_t slot = 0;
    uint8_t irq_routing = 0;
    uint8_t pci_int_index = pci_int - PCI_INTA;
    uint8_t irq_line = 0;
    uint8_t level = 0;

    if (! last_pci_card) {
	pci_log("pci_clear_irq(%02X, %02X): No PCI slots (how are we even here?!)\n", card, pci_int);
	return;
    }
    pci_log("pci_clear_irq(%02X, %02X): %i PCI slots\n", card, pci_int, last_pci_card);

    slot = pci_card_to_slot_mapping[card];
    if (slot == 0xff) {
	pci_log("pci_clear_irq(%02X, %02X): Card is not on a PCI slot (how are we even here?!)\n", card, pci_int);
	return;
    }
    pci_log("pci_clear_irq(%02X, %02X): Card is on PCI slot %02X\n", card, pci_int, slot);

    if (! pci_cards[slot].irq_routing[pci_int_index]) {
	pci_log("pci_clear_irq(%02X, %02X): No IRQ routing for this slot and INT pin combination\n", card, pci_int);
	return;
    }

    if (pci_type & PCI_NO_IRQ_STEERING)
	irq_line = pci_cards[slot].read(0, 0x3c, pci_cards[slot].priv);
    else {
	irq_routing = (pci_cards[slot].irq_routing[pci_int_index] - PCI_INTA) & 3;
	pci_log("pci_clear_irq(%02X, %02X): IRQ routing for this slot and INT pin combination: %02X\n", card, pci_int, irq_routing);

	irq_line = pci_irqs[irq_routing];
	level = pci_irq_level[irq_routing];
    }

    if (irq_line > 0x0f) {
	pci_log("pci_clear_irq(%02X, %02X): IRQ line is disabled\n", card, pci_int);
	return;
    }

    pci_log("pci_clear_irq(%02X, %02X): Using IRQ %i\n", card, pci_int, irq_line);

    if (level && !(pci_irq_hold[irq_line] & (1ULL << card))) {
	/* IRQ not held, do nothing. */
	pci_log("pci_clear_irq(%02X, %02X): Card is not holding the IRQ\n", card, pci_int);
	return;
    }

    if (level) {
	pci_log("pci_clear_irq(%02X, %02X): Releasing this card's hold on the IRQ\n", card, pci_int);
	pci_irq_hold[irq_line] &= ~(1 << card);

	if (! pci_irq_hold[irq_line]) {
		pci_log("pci_clear_irq(%02X, %02X): IRQ no longer held by any card, clearing it\n", card, pci_int);
       		picintc(1 << irq_line);
	} else {
		pci_log("pci_clear_irq(%02X, %02X): IRQ is still being held\n", card, pci_int);
	}
    } else {
	pci_log("pci_clear_irq(%02X, %02X): Clearing edge-triggered interrupt\n", card, pci_int);
	picintc(1 << irq_line);
    }
}


void
pci_elcr_set_enabled(int enabled)
{
    elcr_enabled = enabled;
}


void
pci_reset(void)
{
    int i;

    for (i = 0; i < 16; i++) {
	if (pci_irq_hold[i]) {
		pci_irq_hold[i] = 0;

		picintc(1 << i);
	}
    }

    elcr_reset();
}


static void
pci_slots_clear(void)
{
    uint8_t i, j;

    last_pci_card = 0;

    for (i = 0; i < 32; i++) {
	pci_cards[i].id = 0xFF;
	pci_cards[i].type = 0xFF;

	for (j = 0; j < 4; j++)
		pci_cards[i].irq_routing[j] = 0;

	pci_cards[i].read = NULL;
	pci_cards[i].write = NULL;
	pci_cards[i].priv = NULL;

	pci_card_to_slot_mapping[i] = 0xFF;
    }
}


static uint8_t
trc_read(uint16_t port, void *priv)
{
    return trc_reg & 0xfb;
}


static void
trc_reset(uint8_t val)
{
    if (val & 2) {
	device_reset_all_pci();

	cpu_alt_reset = 0;

	mem_a20_alt = 0;
	mem_a20_recalc();

	flushmmucache();

	pci_reset();
	keyboard_at_reset();
    }

    resetx86();
}


static void
trc_write(uint16_t port, uint8_t val, void *priv)
{
    pci_log("TRC Write: %02X\n", val);

    if (!(trc_reg & 4) && (val & 4))
	trc_reset(val);

    trc_reg = val & 0xfd;

    if (val & 2)
	trc_reg &= 0xfb;
}


void
trc_init(void)
{
    trc_reg = 0;

    io_sethandler(0x0cf9, 0x0001,
		  trc_read, NULL, NULL, trc_write, NULL, NULL, NULL);
}


void
pci_init(int type)
{
    int c;

    PCI = 1;

    pci_slots_clear();

    pci_reset();

    trc_init();

    pci_type = type;

    if (!(type & PCI_NO_IRQ_STEERING)) {
	io_sethandler(0x04d0, 0x0002,
		      elcr_read,NULL,NULL, elcr_write,NULL,NULL, NULL);
    }

    if ((type & PCI_CONFIG_TYPE_MASK) == PCI_CONFIG_TYPE_1) {
	io_sethandler(0x0cf8, 1,
		      NULL,NULL,pci_cf8_read, NULL,NULL,pci_cf8_write, NULL);
	io_sethandler(0x0cfc, 4,
		      pci_read,NULL,NULL, pci_write,NULL,NULL, NULL);
    } else {
	io_sethandler(0x0cf8, 1,
		      pci_type2_read,NULL,NULL, pci_type2_write,NULL,NULL, NULL);
	io_sethandler(0x0cfa, 1,
		      pci_type2_read,NULL,NULL, pci_type2_write,NULL,NULL, NULL);
    }

    for (c = 0; c < 4; c++) {
	pci_irqs[c] = PCI_IRQ_DISABLED;
	pci_irq_level[c] = (type & PCI_NO_IRQ_STEERING) ? 0 : 1;
    }

    for (c = 0; c < 3; c++) {
	pci_mirqs[c].enabled = 0;
	pci_mirqs[c].irq_line = PCI_IRQ_DISABLED;
    }

    elcr_enabled = 1;
}


void
pci_register_slot(int card, int type, int inta, int intb, int intc, int intd)
{
    pci_card_t *dev = &pci_cards[last_pci_card];

    dev->id = card;
    dev->type = type;
    dev->irq_routing[0] = inta;
    dev->irq_routing[1] = intb;
    dev->irq_routing[2] = intc;
    dev->irq_routing[3] = intd;
    dev->read = NULL;
    dev->write = NULL;
    dev->priv = NULL;
    pci_card_to_slot_mapping[card] = last_pci_card;

    pci_log("pci_register_slot(): pci_cards[%i].id = %02X\n", last_pci_card, card);

    last_pci_card++;
}


uint8_t
pci_add_card(uint8_t add_type, uint8_t (*read)(int func, int addr, void *priv), void (*write)(int func, int addr, uint8_t val, void *priv), void *priv)
{
    pci_card_t *dev;
    uint8_t i;

    if (add_type < PCI_ADD_NORMAL)
	pci_log("pci_add_card(): Adding PCI CARD at specific slot %02X [SPECIFIC]\n", add_type);

    if (! PCI) {
	pci_log("pci_add_card(): Adding PCI CARD failed (non-PCI machine) [%s]\n", (add_type == PCI_ADD_NORMAL) ? "NORMAL" : ((add_type == PCI_ADD_VIDEO) ? "VIDEO" : ((add_type == PCI_ADD_SCSI) ? "SCSI" : "SPECIFIC")));
	return 0xff;
    }

    if (! last_pci_card) {
	pci_log("pci_add_card(): Adding PCI CARD failed (no PCI slots) [%s]\n", (add_type == PCI_ADD_NORMAL) ? "NORMAL" : ((add_type == PCI_ADD_VIDEO) ? "VIDEO" : ((add_type == PCI_ADD_SCSI) ? "SCSI" : "SPECIFIC")));
	return 0xff;
    }

    for (i = 0; i < last_pci_card; i++) {
	dev = &pci_cards[i];

	if (!dev->read && !dev->write) {
		if (((dev->type == PCI_CARD_NORMAL) && (add_type >= PCI_ADD_NORMAL)) ||
		    ((dev->type == PCI_CARD_ONBOARD) && (add_type == PCI_ADD_VIDEO)) ||
		    ((dev->type == PCI_CARD_SCSI) && (add_type == PCI_ADD_SCSI)) ||
		    ((dev->id == add_type) && (add_type < PCI_ADD_NORMAL))) {
			dev->read = read;
			dev->write = write;
			dev->priv = priv;
			pci_log("pci_add_card(): Adding PCI CARD to pci_cards[%i] (slot %02X) [%s]\n", i, dev->id, (add_type == PCI_ADD_NORMAL) ? "NORMAL" : ((add_type == PCI_ADD_VIDEO) ? "VIDEO" : ((add_type == PCI_ADD_SCSI) ? "SCSI" : "SPECIFIC")));
			return dev->id;
		}
	}
    }

    pci_log("pci_add_card(): Adding PCI CARD failed (unable to find a suitable PCI slot) [%s]\n", (add_type == PCI_ADD_NORMAL) ? "NORMAL" : ((add_type == PCI_ADD_VIDEO) ? "VIDEO" : ((add_type == PCI_ADD_SCSI) ? "SCSI" : "SPECIFIC")));

    return 0xff;
}
