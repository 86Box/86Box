/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of ISA Plug and Play.
 *
 *
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2021 RichardG.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/isapnp.h>


#define CHECK_CURRENT_LD()	if (!dev->current_ld) { \
					isapnp_log("ISAPnP: No logical device selected\n"); \
					break; \
				}

#define CHECK_CURRENT_CARD()	if (1) { \
					card = dev->first_card; \
					while (card) { \
						if (card->enable && (card->state == PNP_STATE_CONFIG)) \
							break; \
						card = card->next; \
					} \
					if (!card) { \
						isapnp_log("ISAPnP: No card in CONFIG state\n"); \
						break; \
					} \
				}


static const uint8_t pnp_init_key[32] = { 0x6A, 0xB5, 0xDA, 0xED, 0xF6, 0xFB, 0x7D, 0xBE,
					  0xDF, 0x6F, 0x37, 0x1B, 0x0D, 0x86, 0xC3, 0x61,
					  0xB0, 0x58, 0x2C, 0x16, 0x8B, 0x45, 0xA2, 0xD1,
					  0xE8, 0x74, 0x3A, 0x9D, 0xCE, 0xE7, 0x73, 0x39 };
static const device_t isapnp_device;


#ifdef ENABLE_ISAPNP_LOG
int isapnp_do_log = ENABLE_ISAPNP_LOG;


static void
isapnp_log(const char *fmt, ...)
{
    va_list ap;

    if (isapnp_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define isapnp_log(fmt, ...)
#endif


enum {
    PNP_STATE_WAIT_FOR_KEY = 0,
    PNP_STATE_CONFIG,
    PNP_STATE_ISOLATION,
    PNP_STATE_SLEEP
};

typedef struct _isapnp_device_ {
    uint8_t	number;
    uint8_t	regs[256];
    uint8_t	mem_upperlimit, irq_types, io_16bit, io_len[8];
    const isapnp_device_config_t *defaults;

    struct _isapnp_device_ *next;
} isapnp_device_t;

typedef struct _isapnp_card_ {
    uint8_t	enable, state, csn, id_checksum, serial_read, serial_read_pair, serial_read_pos, *rom;
    uint16_t	rom_pos, rom_size;
    void	*priv;

    /* ISAPnP memory and I/O addresses are awkwardly big endian, so we populate this
       structure whenever something on some device changes, and pass it on instead. */
    isapnp_device_config_t config;
    void	(*config_changed)(uint8_t ld, isapnp_device_config_t *config, void *priv);
    void	(*csn_changed)(uint8_t csn, void *priv);
    uint8_t	(*read_vendor_reg)(uint8_t ld, uint8_t reg, void *priv);
    void	(*write_vendor_reg)(uint8_t ld, uint8_t reg, uint8_t val, void *priv);

    isapnp_device_t *first_ld;
    struct _isapnp_card_ *next;
} isapnp_card_t;

typedef struct {
    uint8_t	reg, key_pos: 5;
    uint16_t	read_data_addr;

    isapnp_card_t *first_card, *isolated_card, *current_ld_card;
    isapnp_device_t *current_ld;
} isapnp_t;


static void
isapnp_device_config_changed(isapnp_card_t *card, isapnp_device_t *ld)
{
    /* Ignore card if it hasn't signed up for configuration changes. */
    if (!card->config_changed)
	return;

    /* Populate config structure, performing endianness conversion as needed. */
    card->config.activate = ld->regs[0x30] & 0x01;
    uint8_t i, reg_base;
    for (i = 0; i < 4; i++) {
	reg_base = 0x40 + (8 * i);
	card->config.mem[i].base = (ld->regs[reg_base] << 16) | (ld->regs[reg_base + 1] << 8);
	card->config.mem[i].size = (ld->regs[reg_base + 3] << 16) | (ld->regs[reg_base + 4] << 8);
	if (ld->regs[reg_base + 2] & 0x01) /* upper limit */
		card->config.mem[i].size -= card->config.mem[i].base;
    }
    for (i = 0; i < 4; i++) {
	reg_base = (i == 0) ? 0x76 : (0x80 + (16 * i));
	card->config.mem32[i].base = (ld->regs[reg_base] << 24) | (ld->regs[reg_base + 1] << 16) | (ld->regs[reg_base + 2] << 8) | ld->regs[reg_base + 3];
	card->config.mem32[i].size = (ld->regs[reg_base + 5] << 24) | (ld->regs[reg_base + 6] << 16) | (ld->regs[reg_base + 7] << 8) | ld->regs[reg_base + 8];
	if (ld->regs[reg_base + 4] & 0x01) /* upper limit */
		card->config.mem32[i].size -= card->config.mem32[i].base;
    }
    for (i = 0; i < 8; i++) {
	reg_base = 0x60 + (2 * i);
	if (ld->regs[0x31] & 0x02)
		card->config.io[i].base = 0; /* let us handle I/O range check reads */
	else
		card->config.io[i].base = (ld->regs[reg_base] << 8) | ld->regs[reg_base + 1];
    }
    for (i = 0; i < 2; i++) {
	reg_base = 0x70 + (2 * i);
	card->config.irq[i].irq = ld->regs[reg_base];
	card->config.irq[i].level = ld->regs[reg_base + 1] & 0x02;
	card->config.irq[i].type = ld->regs[reg_base + 1] & 0x01;
    }
    for (i = 0; i < 2; i++) {
	reg_base = 0x74 + i;
	card->config.dma[i].dma = ld->regs[reg_base];
    }

    /* Signal the configuration change. */
    card->config_changed(ld->number, &card->config, card->priv);
}


static void
isapnp_reset_ld_config(isapnp_device_t *ld)
{
    /* Do nothing if there's no default configuration for this device. */
    const isapnp_device_config_t *config = ld->defaults;
    if (!config)
	return;

    /* Populate configuration registers. */
    ld->regs[0x30] = !!config->activate;
    uint8_t i, reg_base;
    uint32_t size;
    for (i = 0; i < 4; i++) {
	reg_base = 0x40 + (8 * i);
	ld->regs[reg_base] = config->mem[i].base >> 16;
	ld->regs[reg_base + 1] = config->mem[i].base >> 8;
	size = config->mem[i].size;
	if (ld->regs[reg_base + 2] & 0x01) /* upper limit */
		size += config->mem[i].base;
	ld->regs[reg_base + 3] = size >> 16;
	ld->regs[reg_base + 4] = size >> 8;
    }
    for (i = 0; i < 4; i++) {
	reg_base = (i == 0) ? 0x76 : (0x80 + (16 * i));
	ld->regs[reg_base] = config->mem32[i].base >> 24;
	ld->regs[reg_base + 1] = config->mem32[i].base >> 16;
	ld->regs[reg_base + 2] = config->mem32[i].base >> 8;
	ld->regs[reg_base + 3] = config->mem32[i].base;
	size = config->mem32[i].size;
	if (ld->regs[reg_base + 4] & 0x01) /* upper limit */
		size += config->mem32[i].base;
	ld->regs[reg_base + 5] = size >> 24;
	ld->regs[reg_base + 6] = size >> 16;
	ld->regs[reg_base + 7] = size >> 8;
	ld->regs[reg_base + 8] = size;
    }
    for (i = 0; i < 8; i++) {
	reg_base = 0x60 + (2 * i);
	ld->regs[reg_base] = config->io[i].base >> 8;
	ld->regs[reg_base + 1] = config->io[i].base;
    }
    for (i = 0; i < 2; i++) {
	reg_base = 0x70 + (2 * i);
	ld->regs[reg_base] = config->irq[i].irq;
	ld->regs[reg_base + 1] = (!!config->irq[i].level << 1) | !!config->irq[i].type;
    }
    for (i = 0; i < 2; i++) {
	reg_base = 0x74 + i;
	ld->regs[reg_base] = config->dma[i].dma;
    }
}


static void
isapnp_reset_ld_regs(isapnp_device_t *ld)
{
    memset(ld->regs, 0, sizeof(ld->regs));

    /* DMA disable uses a non-zero value. */
    ld->regs[0x74] = ld->regs[0x75] = ISAPNP_DMA_DISABLED;

    /* Set the upper limit bit on memory ranges which require it. */
    uint8_t i;
    for (i = 0; i < 4; i++)
	ld->regs[0x42 + (8 * i)] |= !!(ld->mem_upperlimit & (1 << i));
    ld->regs[0x7a] |= !!(ld->mem_upperlimit & (1 << 4));
    for (i = 1; i < 4; i++)
	ld->regs[0x84 + (16 * i)] |= !!(ld->mem_upperlimit & (1 << (4 + i)));

    /* Set the default IRQ type bits. */
    for (i = 0; i < 2; i++) {
	if (ld->irq_types & (0x1 << (4 * i)))
		ld->regs[0x70 + (2 * i)] = 0x02;
	else if (ld->irq_types & (0x2 << (4 * i)))
		ld->regs[0x70 + (2 * i)] = 0x00;
	else if (ld->irq_types & (0x4 << (4 * i)))
		ld->regs[0x70 + (2 * i)] = 0x03;
	else if (ld->irq_types & (0x8 << (4 * i)))
		ld->regs[0x70 + (2 * i)] = 0x01;
    }

    /* Reset configuration registers to match the default configuration. */
    isapnp_reset_ld_config(ld);
}


static uint8_t
isapnp_read_rangecheck(uint16_t addr, void *priv)
{
    isapnp_device_t *dev = (isapnp_device_t *) priv;
    return (dev->regs[0x31] & 0x01) ? 0x55 : 0xaa;
}


static uint8_t
isapnp_read_data(uint16_t addr, void *priv)
{
    isapnp_t *dev = (isapnp_t *) priv;
    uint8_t ret = 0xff, bit, next_shift;
    isapnp_card_t *card;

    switch (dev->reg) {
	case 0x01: /* Serial Isolation */
		card = dev->first_card;
		while (card) {
			if (card->enable && (card->state == PNP_STATE_ISOLATION))
				break;
			card = card->next;
		}
		dev->isolated_card = card;

		if (card) {
			if (card->serial_read_pair) { /* second byte (aa/00) */
				card->serial_read <<= 1;
				if (!card->serial_read_pos)
					card->rom_pos = 0x09;
			} else { /* first byte (55/00) */
				if (card->serial_read_pos < 64) { /* reading 64-bit vendor/serial */
					bit = (card->rom[card->serial_read_pos >> 3] >> (card->serial_read_pos & 0x7)) & 0x01;
					next_shift = (!!(card->id_checksum & 0x02) ^ !!(card->id_checksum & 0x01) ^ bit) & 0x01;
					card->id_checksum >>= 1;
					card->id_checksum |= (next_shift << 7);
				} else { /* reading 8-bit checksum */
					if (card->serial_read_pos == 64) /* populate ID checksum in ROM */
						card->rom[0x08] = card->id_checksum;
					bit = (card->id_checksum >> (card->serial_read_pos & 0x7)) & 0x01;
				}
				isapnp_log("ISAPnP: Read bit %d of byte %02X (%02X) = %d\n", card->serial_read_pos & 0x7, card->serial_read_pos >> 3, card->rom[card->serial_read_pos >> 3], bit);
				card->serial_read = bit ? 0x55 : 0x00;
				card->serial_read_pos = (card->serial_read_pos + 1) % 72;
			}
			card->serial_read_pair ^= 1;
			ret = card->serial_read;
		}

		break;

	case 0x04: /* Resource Data */
		CHECK_CURRENT_CARD();

		isapnp_log("ISAPnP: Read resource data index %02X (%02X) from CSN %02X\n", card->rom_pos, card->rom[card->rom_pos], card->csn);
		if (card->rom_pos >= card->rom_size)
			ret = 0xff;
		else
			ret = card->rom[card->rom_pos++];

		break;

	case 0x05: /* Status */
		ret = 0x00;
		CHECK_CURRENT_CARD();

		isapnp_log("ISAPnP: Query status for CSN %02X\n", card->csn);
		ret = 0x01;

		break;

	case 0x06: /* Card Select Number */
		ret = 0x00;
		CHECK_CURRENT_CARD();

		isapnp_log("ISAPnP: Query CSN %02X\n", card->csn);
		ret = card->csn;

		break;

	case 0x07: /* Logical Device Number */
		ret = 0x00;
		CHECK_CURRENT_LD();

		isapnp_log("ISAPnP: Query LDN for CSN %02X device %02X\n", dev->current_ld_card->csn, dev->current_ld->number);
		ret = dev->current_ld->number;

		break;

	case 0x20: case 0x21: case 0x22: case 0x23:
	case 0x24: case 0x25: case 0x26: case 0x27:
	case 0x28: case 0x29: case 0x2a: case 0x2b:
	case 0x2c: case 0x2d: case 0x2e: case 0x2f:
		CHECK_CURRENT_CARD();

		isapnp_log("ISAPnP: Read vendor-defined register %02X from CSN %02X\n", dev->reg, card->csn);

		if (card->read_vendor_reg)
			ret = card->read_vendor_reg(0, dev->reg, card->priv);
		break;

	case 0x38: case 0x39: case 0x3a: case 0x3b:
	case 0x3c: case 0x3d: case 0x3e: case 0x3f:
	case 0xf0: case 0xf1: case 0xf2: case 0xf3:
	case 0xf4: case 0xf5: case 0xf6: case 0xf7:
	case 0xf8: case 0xf9: case 0xfa: case 0xfb:
	case 0xfc: case 0xfd: case 0xfe:
		CHECK_CURRENT_LD();
		isapnp_log("ISAPnP: Read vendor-defined register %02X from CSN %02X device %02X\n", dev->reg, dev->current_ld_card->csn, dev->current_ld->number);
		if (dev->current_ld_card->read_vendor_reg)
			ret = dev->current_ld_card->read_vendor_reg(dev->current_ld->number, dev->reg, dev->current_ld_card->priv);
		break;

	default:
		if (dev->reg >= 0x30) {
			CHECK_CURRENT_LD();
			isapnp_log("ISAPnP: Read register %02X from CSN %02X device %02X\n", dev->reg, dev->current_ld_card->csn, dev->current_ld->number);
			ret = dev->current_ld->regs[dev->reg];
		}
		break;
    }

    isapnp_log("ISAPnP: read_data(%02X) = %02X\n", dev->reg, ret);

    return ret;
}


static void
isapnp_set_read_data(uint16_t addr, isapnp_t *dev)
{
    /* Remove existing READ_DATA port if set. */
    if (dev->read_data_addr) {
	io_removehandler(dev->read_data_addr, 1, isapnp_read_data, NULL, NULL, NULL, NULL, NULL, dev);
	dev->read_data_addr = 0;
    }

    /* Set new READ_DATA port if within range. */
    if ((addr >= 0x203) && (addr <= 0x3ff)) {
	dev->read_data_addr = addr;
	io_sethandler(dev->read_data_addr, 1, isapnp_read_data, NULL, NULL, NULL, NULL, NULL, dev);
    }
}


static void
isapnp_write_addr(uint16_t addr, uint8_t val, void *priv)
{
    isapnp_t *dev = (isapnp_t *) priv;
    isapnp_card_t *card = dev->first_card;

    isapnp_log("ISAPnP: write_addr(%02X)\n", val);

    if (!card) /* don't do anything if we have no PnP cards */
	return;

    dev->reg = val;

    if (card->state == PNP_STATE_WAIT_FOR_KEY) { /* checking only the first card should be fine */
	/* Check written value against LFSR key. */
	if (val == pnp_init_key[dev->key_pos]) {
		dev->key_pos++;
		if (!dev->key_pos) {
			isapnp_log("ISAPnP: Key unlocked, putting cards to SLEEP\n");
			while (card) {
				if (card->enable && (card->state == PNP_STATE_WAIT_FOR_KEY))
					card->state = PNP_STATE_SLEEP;
				card = card->next;
			}
		}
	} else {
		dev->key_pos = 0;
	}
    }
}


static void
isapnp_write_data(uint16_t addr, uint8_t val, void *priv)
{
    isapnp_t *dev = (isapnp_t *) priv;
    isapnp_card_t *card;
    isapnp_device_t *ld;
    uint16_t io_addr, reset_cards = 0;

    isapnp_log("ISAPnP: write_data(%02X)\n", val);

    switch (dev->reg) {
	case 0x00: /* Set RD_DATA Port */
		isapnp_set_read_data((val << 2) | 3, dev);
		isapnp_log("ISAPnP: Read data port set to %04X\n", dev->read_data_addr);
		break;

	case 0x02: /* Config Control */
		if (val & 0x01) {
			isapnp_log("ISAPnP: Reset\n");

			card = dev->first_card;
			while (card) {
				ld = card->first_ld;
				while (ld) {
					if (card->state != PNP_STATE_WAIT_FOR_KEY) {
						isapnp_reset_ld_regs(ld);
						isapnp_device_config_changed(card, ld);
						reset_cards++;
					}
					ld = ld->next;
				}
				card = card->next;
			}

			if (reset_cards != 0) {
				dev->current_ld = NULL;
				dev->current_ld_card = NULL;
				dev->isolated_card = NULL;
			}
		}
		if (val & 0x02) {
			isapnp_log("ISAPnP: Return to WAIT_FOR_KEY\n");
			card = dev->first_card;
			while (card) {
				card->state = PNP_STATE_WAIT_FOR_KEY;
				card = card->next;
			}
		}
		if (val & 0x04) {
			isapnp_log("ISAPnP: Reset CSN\n");
			card = dev->first_card;
			while (card) {
				isapnp_set_csn(card, 0);
				card = card->next;
			}
		}
		break;

	case 0x03: /* Wake[CSN] */
		isapnp_log("ISAPnP: Wake[%02X]\n", val);
		card = dev->first_card;
		while (card) {
			if (card->csn == val) {
				card->rom_pos = 0;
				card->id_checksum = pnp_init_key[0];
				if (card->state == PNP_STATE_SLEEP)
					card->state = (val == 0) ? PNP_STATE_ISOLATION : PNP_STATE_CONFIG;
			} else {
				card->state = PNP_STATE_SLEEP;
			}

			card = card->next;
		}
		break;

	case 0x06: /* Card Select Number */
		if (dev->isolated_card) {
			isapnp_log("ISAPnP: Set CSN %02X\n", val);
			isapnp_set_csn(dev->isolated_card, val);
			dev->isolated_card->state = PNP_STATE_CONFIG;
			dev->isolated_card = NULL;
		} else {
			isapnp_log("ISAPnP: Set CSN %02X but no card is isolated\n", val);
		}
		break;

	case 0x07: /* Logical Device Number */
		CHECK_CURRENT_CARD();

		ld = card->first_ld;
		while (ld) {
			if (ld->number == val) {
				isapnp_log("ISAPnP: Select CSN %02X device %02X\n", card->csn, val);
				dev->current_ld_card = card;
				dev->current_ld = ld;
				break;
			}
			ld = ld->next;
		}

		if (!ld)
			isapnp_log("ISAPnP: CSN %02X has no device %02X\n", card->csn, val);

		break;

	case 0x30: /* Activate */
		CHECK_CURRENT_LD();

		isapnp_log("ISAPnP: %sctivate CSN %02X device %02X\n", (val & 0x01) ? "A" : "Dea", dev->current_ld_card->csn, dev->current_ld->number);

		dev->current_ld->regs[dev->reg] = val & 0x01;
		isapnp_device_config_changed(dev->current_ld_card, dev->current_ld);

		break;

	case 0x31: /* I/O Range Check */
		CHECK_CURRENT_LD();

		for (uint8_t i = 0; i < 8; i++) {
			if (!dev->current_ld->io_len[i])
				continue;

			io_addr = (dev->current_ld->regs[0x60 + (2 * i)] << 8) | dev->current_ld->regs[0x61 + (2 * i)];
			if (dev->current_ld->regs[dev->reg] & 0x02)
				io_removehandler(io_addr, dev->current_ld->io_len[i], isapnp_read_rangecheck, NULL, NULL, NULL, NULL, NULL, dev->current_ld);
			if (val & 0x02)
				io_sethandler(io_addr, dev->current_ld->io_len[i], isapnp_read_rangecheck, NULL, NULL, NULL, NULL, NULL, dev->current_ld);
		}

		dev->current_ld->regs[dev->reg] = val & 0x03;
		isapnp_device_config_changed(dev->current_ld_card, dev->current_ld);

		break;

	case 0x20: case 0x21: case 0x22: case 0x23:
	case 0x24: case 0x25: case 0x26: case 0x27:
	case 0x28: case 0x29: case 0x2a: case 0x2b:
	case 0x2c: case 0x2d: case 0x2e: case 0x2f:
		CHECK_CURRENT_CARD();

		isapnp_log("ISAPnP: Write %02X to vendor-defined register %02X on CSN %02X\n", val, dev->reg, card->csn);

		if (card->write_vendor_reg)
			card->write_vendor_reg(0, dev->reg, val, card->priv);
		break;

	case 0x38: case 0x39: case 0x3a: case 0x3b:
	case 0x3c: case 0x3d: case 0x3e: case 0x3f:
	case 0xf0: case 0xf1: case 0xf2: case 0xf3:
	case 0xf4: case 0xf5: case 0xf6: case 0xf7:
	case 0xf8: case 0xf9: case 0xfa: case 0xfb:
	case 0xfc: case 0xfd: case 0xfe:
		CHECK_CURRENT_LD();
		isapnp_log("ISAPnP: Write %02X to vendor-defined register %02X on CSN %02X device %02X\n", val, dev->reg, dev->current_ld_card->csn, dev->current_ld->number);
		if (dev->current_ld_card->write_vendor_reg)
			dev->current_ld_card->write_vendor_reg(dev->current_ld->number, dev->reg, val, dev->current_ld_card->priv);
		break;

	default:
		if (dev->reg >= 0x40) {
			CHECK_CURRENT_LD();
			isapnp_log("ISAPnP: Write %02X to register %02X on CSN %02X device %02X\n", val, dev->reg, dev->current_ld_card->csn, dev->current_ld->number);

			switch (dev->reg) {
				case 0x42: case 0x4a: case 0x52: case 0x5a:
				case 0x7a: case 0x84: case 0x94: case 0xa4:
					/* Read-only memory range length / upper limit bit. */
					val = (val & 0xfe) | (dev->current_ld->regs[dev->reg] & 0x01);
					break;

				case 0x60: case 0x62: case 0x64: case 0x66: case 0x68: case 0x6a: case 0x6c: case 0x6e:
					/* Discard upper address bits if this I/O range can only decode 10-bit. */
					if (!(dev->current_ld->io_16bit & (1 << ((dev->reg >> 1) & 0x07))))
						val &= 0x03;
					break;

				case 0x71: case 0x73:
					/* Limit IRQ types to supported ones. */
					if ((val & 0x01) && !(dev->current_ld->irq_types & ((dev->reg == 0x71) ? 0x0c : 0xc0))) /* level, not supported = force edge */
						val &= ~0x01;
					else if (!(val & 0x01) && !(dev->current_ld->irq_types & ((dev->reg == 0x71) ? 0x03 : 0x30))) /* edge, not supported = force level */
						val |= 0x01;

					if ((val & 0x02) && !(dev->current_ld->irq_types & ((dev->reg == 0x71) ? 0x05 : 0x50))) /* high, not supported = force low */
						val &= ~0x02;
					else if (!(val & 0x02) && !(dev->current_ld->irq_types & ((dev->reg == 0x71) ? 0x0a : 0xa0))) /* low, not supported = force high */
						val |= 0x02;

					break;
			}

			dev->current_ld->regs[dev->reg] = val;
			isapnp_device_config_changed(dev->current_ld_card, dev->current_ld);
		}
		break;
    }
}


static void *
isapnp_init(const device_t *info)
{
    isapnp_t *dev = (isapnp_t *) malloc(sizeof(isapnp_t));
    memset(dev, 0, sizeof(isapnp_t));

    io_sethandler(0x279, 1, NULL, NULL, NULL, isapnp_write_addr, NULL, NULL, dev);
    io_sethandler(0xa79, 1, NULL, NULL, NULL, isapnp_write_data, NULL, NULL, dev);

    return dev;
}


static void
isapnp_close(void *priv)
{
    isapnp_t *dev = (isapnp_t *) priv;
    isapnp_card_t *card = dev->first_card, *next_card;
    isapnp_device_t *ld, *next_ld;

    while (card) {
	ld = card->first_ld;
	while (ld) {
		next_ld = ld->next;
		free(ld);
		ld = next_ld;
	}

	next_card = card->next;
	free(card);
	card = next_card;
    }

    io_removehandler(0x279, 1, NULL, NULL, NULL, isapnp_write_addr, NULL, NULL, dev);
    io_removehandler(0xa79, 1, NULL, NULL, NULL, isapnp_write_data, NULL, NULL, dev);

    free(dev);
}


void *
isapnp_add_card(uint8_t *rom, uint16_t rom_size,
		void (*config_changed)(uint8_t ld, isapnp_device_config_t *config, void *priv),
		void (*csn_changed)(uint8_t csn, void *priv),
		uint8_t (*read_vendor_reg)(uint8_t ld, uint8_t reg, void *priv),
		void (*write_vendor_reg)(uint8_t ld, uint8_t reg, uint8_t val, void *priv),
		void *priv)
{
    isapnp_t *dev = (isapnp_t *) device_get_priv(&isapnp_device);
    if (!dev)
	dev = (isapnp_t *) device_add(&isapnp_device);

    isapnp_card_t *card = (isapnp_card_t *) malloc(sizeof(isapnp_card_t));
    memset(card, 0, sizeof(isapnp_card_t));

    card->enable = 1;
    card->priv = priv;
    card->config_changed = config_changed;
    card->csn_changed = csn_changed;
    card->read_vendor_reg = read_vendor_reg;
    card->write_vendor_reg = write_vendor_reg;

    if (!dev->first_card) {
	dev->first_card = card;
    } else {
	isapnp_card_t *prev_card = dev->first_card;
	while (prev_card->next)
		prev_card = prev_card->next;
	prev_card->next = card;
    }

    if (rom && rom_size)
	isapnp_update_card_rom(card, rom, rom_size);

    return card;
}


void
isapnp_update_card_rom(void *priv, uint8_t *rom, uint16_t rom_size)
{
    isapnp_card_t *card = (isapnp_card_t *) priv;
    card->rom = rom;
    card->rom_size = rom_size;

    /* Parse resources in ROM to allocate logical devices,
       and determine the state of read-only register bits. */
#ifdef ENABLE_ISAPNP_LOG
    uint16_t vendor = (card->rom[0] << 8) | card->rom[1];
    isapnp_log("ISAPnP: Parsing ROM resources for card %c%c%c%02X%02X (serial %08X)\n", '@' + ((vendor >> 10) & 0x1f), '@' + ((vendor >> 5) & 0x1f), '@' + (vendor & 0x1f), card->rom[2], card->rom[3], (card->rom[7] << 24) | (card->rom[6] << 16) | (card->rom[5] << 8) | card->rom[4]);
#endif
    uint16_t i = 9, j;
    uint8_t ldn = 0, res, in_df = 0;
    uint8_t irq = 0, io = 0, mem_range = 0, mem_range_32 = 0, irq_df = 0, io_df = 0, mem_range_df = 0, mem_range_32_df = 0;
    uint32_t len;
    isapnp_device_t *ld = card->first_ld, *prev_ld = NULL;

    /* Clear any existing logical devices. */
    while (ld) {
	prev_ld = ld->next;
	free(ld);
	ld = prev_ld;
    }

    /* Iterate through ROM resources. */
    while (i < card->rom_size) {
	if (card->rom[i] & 0x80) { /* large resource */
		res = card->rom[i] & 0x7f;
		len = (card->rom[i + 2] << 8) | card->rom[i + 1];

		switch (res) {
			case 0x01: /* memory range */
			case 0x05: /* 32-bit memory range */
				if (res == 0x01) {
					if (!ld) {
						isapnp_log("ISAPnP: >>%s Memory descriptor with no logical device\n", in_df ? ">" : "");
						break;
					}

					if (mem_range > 3) {
						isapnp_log("ISAPnP: >>%s Memory descriptor overflow (%d)\n", in_df ? ">" : "", mem_range++);
						break;
					}

					isapnp_log("ISAPnP: >>%s Memory range %d uses upper limit = ", in_df ? ">" : "", mem_range);
					res = 1 << mem_range;
					mem_range++;
				} else {
					if (!ld) {
						isapnp_log("ISAPnP: >>%s 32-bit memory descriptor with no logical device\n", in_df ? ">" : "");
						break;
					}

					if (mem_range_32 > 3) {
						isapnp_log("ISAPnP: >>%s 32-bit memory descriptor overflow (%d)\n", in_df ? ">" : "", mem_range_32++);
						break;
					}

					isapnp_log("ISAPnP: >>%s 32-bit memory range %d uses upper limit = ", in_df ? ">" : "", mem_range_32);
					res = 1 << (4 + mem_range_32);
					mem_range_32++;
				}

				if (card->rom[i + 3] & 0x4) {
					isapnp_log("yes\n");
					ld->mem_upperlimit |= res;
				} else {
					isapnp_log("no\n");
					ld->mem_upperlimit &= ~res;
				}

				break;

#ifdef ENABLE_ISAPNP_LOG
			case 0x02: /* ANSI identifier */
				res = card->rom[i + 3 + len];
				card->rom[i + 3 + len] = '\0';
				isapnp_log("ISAPnP: >%s ANSI identifier: \"%s\"\n", ldn ? ">" : "", &card->rom[i + 3]);
				card->rom[i + 3 + len] = res;
				break;

			default:
				isapnp_log("ISAPnP: >%s%s Large resource %02X (length %d)\n", ldn ? ">" : "", in_df ? ">" : "", res, (card->rom[i + 2] << 8) | card->rom[i + 1]);
				break;
#endif
		}

		i += 3; /* header */
	} else { /* small resource */
		res = (card->rom[i] >> 3) & 0x0f;
		len = card->rom[i] & 0x07;

		switch (res) {
			case 0x02:
#ifdef ENABLE_ISAPNP_LOG
				vendor = (card->rom[i + 1] << 8) | card->rom[i + 2];
				isapnp_log("ISAPnP: > Logical device %02X: %c%c%c%02X%02X\n", ldn, '@' + ((vendor >> 10) & 0x1f), '@' + ((vendor >> 5) & 0x1f), '@' + (vendor & 0x1f), card->rom[i + 3], card->rom[i + 4]);
#endif

				/* We're done with the previous logical device. */
				if (ld) {
					prev_ld = ld;
					isapnp_reset_ld_regs(ld);
				}

				/* Create logical device. */
				ld = (isapnp_device_t *) malloc(sizeof(isapnp_device_t));
				memset(ld, 0, sizeof(isapnp_device_t));

				ld->number = ldn++;

				if (prev_ld)
					prev_ld->next = ld;
				else
					card->first_ld = ld;

				/* Start the position counts over. */
				irq = io = mem_range = mem_range_32 = irq_df = io_df = mem_range_df = mem_range_32_df = 0;

				break;

#ifdef ENABLE_ISAPNP_LOG
			case 0x03: /* compatible device ID */
				if (!ld) {
					isapnp_log("ISAPnP: >> Compatible device ID with no logical device\n");
					break;
				}

				vendor = (card->rom[i + 1] << 8) | card->rom[i + 2];
				isapnp_log("ISAPnP: >> Compatible device ID: %c%c%c%02X%02X\n", '@' + ((vendor >> 10) & 0x1f), '@' + ((vendor >> 5) & 0x1f), '@' + (vendor & 0x1f), card->rom[i + 3], card->rom[i + 4]);
				break;
#endif

			case 0x04: /* IRQ */
				if (!ld) {
					isapnp_log("ISAPnP: >>%s IRQ descriptor with no logical device\n", in_df ? ">" : "");
					break;
				}

				if (irq > 1) {
					isapnp_log("ISAPnP: >>%s IRQ descriptor overflow (%d)\n", in_df ? ">" : "", irq++);
					break;
				}

				if (len == 2) /* default */
					res = 0x01; /* high true edge sensitive */
				else /* specific */
					res = card->rom[i + 3] & 0x0f;

				isapnp_log("ISAPnP: >>%s IRQ index %d interrupt types = %01X\n", in_df ? ">" : "", irq, res);

				ld->irq_types &= ~(0x0f << (4 * irq));
				ld->irq_types |= res << (4 * irq);

				irq++;

				break;

			case 0x06: /* start dependent function */
				if (!ld) {
					isapnp_log("ISAPnP: >> Start dependent function with no logical device\n");
					break;
				}

				isapnp_log("ISAPnP: >> Start dependent function: %s\n", (((len == 0) || (card->rom[i + 1] == 1)) ? "acceptable" : ((card->rom[i + 1] == 0) ? "good" : ((card->rom[i + 1] == 2) ? "sub-optimal" : "unknown priority"))));

				if (in_df) {
					/* We're in a dependent function and this is the next one starting.
					   Walk positions back to the saved values. */
					irq = irq_df;
					io = io_df;
					mem_range = mem_range_df;
					mem_range_32 = mem_range_32_df;
				} else {
					/* Save current positions to restore at the next DF. */
					irq_df = irq;
					io_df = io;
					mem_range_df = mem_range;
					mem_range_32_df = mem_range_32;
					in_df = 1;
				}

				break;

			case 0x07: /* end dependent function */
				isapnp_log("ISAPnP: >> End dependent function\n");
				in_df = 0;
				break;

			case 0x08: /* I/O port */
				if (!ld) {
					isapnp_log("ISAPnP: >>%s I/O descriptor with no logical device\n", in_df ? ">" : "");
					break;
				}

				if (io > 7) {
					isapnp_log("ISAPnP: >>%s I/O descriptor overflow (%d)\n", in_df ? ">" : "", io++);
					break;
				}

				isapnp_log("ISAPnP: >>%s I/O range %d %d-bit decode, %d ports\n", in_df ? ">" : "", io, (card->rom[i + 1] & 0x01) ? 16 : 10, card->rom[i + 7]);

				if (card->rom[i + 1] & 0x01)
					ld->io_16bit |= 1 << io;
				else
					ld->io_16bit &= ~(1 << io);

				if (card->rom[i + 7] > ld->io_len[io])
					ld->io_len[io] = card->rom[i + 7];

				io++;

				break;

			case 0x0f: /* end tag */
				/* Calculate checksum. */
				res = 0x00;
				for (j = 9; j <= i; j++)
					res += card->rom[j];
				card->rom[i + 1] = -res;

				isapnp_log("ISAPnP: End card resources (checksum %02X)\n", card->rom[i + 1]);

				/* Stop parsing here. */
				card->rom_size = i + 2;
				break;

#ifdef ENABLE_ISAPNP_LOG
			default:
				isapnp_log("ISAPnP: >%s%s Small resource %02X (length %d)\n", ldn ? ">" : "", in_df ? ">" : "", res, card->rom[i] & 0x07);
				break;
#endif
		}

		i++; /* header */
	}
	i += len; /* specified length */
    }

    /* We're done with the last logical device. */
    if (ld)
	isapnp_reset_ld_regs(ld);
}


void
isapnp_enable_card(void *priv, uint8_t enable)
{
    isapnp_t *dev = (isapnp_t *) device_get_priv(&isapnp_device);
    if (!dev)
	return;

    /* Look for a matching card. */
    isapnp_card_t *card = dev->first_card;
    uint8_t will_enable;
    while (card) {
	if (card == priv) {
		/* Enable or disable the card. */
		will_enable = (enable >= ISAPNP_CARD_ENABLE);
		if (will_enable ^ card->enable)
			card->state = (enable == ISAPNP_CARD_FORCE_CONFIG) ? PNP_STATE_CONFIG : PNP_STATE_WAIT_FOR_KEY;
		card->enable = will_enable;

		/* Invalidate other references if we're disabling this card. */
		if (!card->enable) {
			if (dev->isolated_card == card)
				dev->isolated_card = NULL;
			if (dev->current_ld_card == card) {
				dev->current_ld = NULL;
				dev->current_ld_card = NULL;
			}
		}

		break;
	}

	card = card->next;
    }
}


void
isapnp_set_csn(void *priv, uint8_t csn)
{
    isapnp_card_t *card = (isapnp_card_t *) priv;

    card->csn = csn;
    if (card->csn_changed)
	card->csn_changed(card->csn, card->priv);
}


void
isapnp_set_device_defaults(void *priv, uint8_t ldn, const isapnp_device_config_t *config)
{
    isapnp_card_t *card = (isapnp_card_t *) priv;
    isapnp_device_t *ld = card->first_ld;

    /* Look for a logical device with this number. */
    while (ld && (ld->number != ldn))
	ld = ld->next;

    if (!ld) /* none found */
	return;

    ld->defaults = config;
}


void
isapnp_reset_card(void *priv)
{
    isapnp_card_t *card = (isapnp_card_t *) priv;
    isapnp_device_t *ld = card->first_ld;

    /* Reset all logical devices. */
    while (ld) {
	/* Reset the logical device's configuration. */
	isapnp_reset_ld_config(ld);
	isapnp_device_config_changed(card, ld);

	ld = ld->next;
    }
}


void
isapnp_reset_device(void *priv, uint8_t ldn)
{
    isapnp_card_t *card = (isapnp_card_t *) priv;
    isapnp_device_t *ld = card->first_ld;

    /* Look for a logical device with this number. */
    while (ld && (ld->number != ldn))
	ld = ld->next;

    if (!ld) /* none found */
	return;

    /* Reset the logical device's configuration. */
    isapnp_reset_ld_config(ld);
    isapnp_device_config_changed(card, ld);
}


static const device_t isapnp_device = {
    "ISA Plug and Play",
    0,
    0,
    isapnp_init, isapnp_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};
