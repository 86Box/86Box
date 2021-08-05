/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Crystal CS423x (SBPro/WSS compatible sound chips) emulation.
 *
 *
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2021 RichardG.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/pic.h>
#include <86box/dma.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/i2c.h>
#include <86box/isapnp.h>
#include <86box/sound.h>
#include <86box/midi.h>
#include <86box/snd_ad1848.h>
#include <86box/snd_opl.h>
#include <86box/snd_sb.h>
#include <86box/nvr.h>


enum {
    CRYSTAL_CS4236B = 0xcb,
    CRYSTAL_CS4237B = 0xc8,
    CRYSTAL_CS4238B = 0xc9
};
enum {
    CRYSTAL_SLAM_NONE  = 0,
    CRYSTAL_SLAM_INDEX = 1,
    CRYSTAL_SLAM_BYTE1 = 2,
    CRYSTAL_SLAM_BYTE2 = 3
};


static const uint8_t slam_init_key[32] = { 0x96, 0x35, 0x9A, 0xCD, 0xE6, 0xF3, 0x79, 0xBC,
					   0x5E, 0xAF, 0x57, 0x2B, 0x15, 0x8A, 0xC5, 0xE2,
					   0xF1, 0xF8, 0x7C, 0x3E, 0x9F, 0x4F, 0x27, 0x13,
					   0x09, 0x84, 0x42, 0xA1, 0xD0, 0x68, 0x34, 0x1A };
static const uint8_t cs4236b_eeprom[] = {
    /* Chip configuration */
    0x55, 0xbb, /* magic */
    0x00, 0x00, /* length */
    0x00, 0x03, /* CD-ROM and modem decode */
    0x80, /* misc. config */
    0x80, /* global config */
    0x0b, /* chip ID */
    0x20, 0x04, 0x08, 0x10, 0x80, 0x00, 0x00, /* reserved */
    0x00, /* external decode length */
    0x48, /* reserved */
    0x75, 0xb9, 0xfc, /* IRQ routing */
    0x10, 0x03, /* DMA routing */

    /* PnP resources */
    0x0e, 0x63, 0x42, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, /* CSC4236, dummy checksum (filled in by isapnp_add_card) */
    0x0a, 0x10, 0x01, /* PnP version 1.0, vendor version 0.1 */
    0x82, 0x0e, 0x00, 'C', 'r', 'y', 's', 't', 'a', 'l', ' ', 'C', 'o', 'd', 'e' ,'c', 0x00, /* ANSI identifier */

    0x15, 0x0e, 0x63, 0x00, 0x00, 0x00, /* logical device CSC0000 */
	0x82, 0x07, 0x00, 'W', 'S', 'S', '/', 'S', 'B', 0x00, /* ANSI identifier */
	0x31, 0x00, /* start dependent functions, preferred */
		0x2a, 0x02, 0x28, /* DMA 1, type A, no count by word, count by byte, not bus master, 8-bit only */
		0x2a, 0x09, 0x28, /* DMA 0/3, type A, no count by word, count by byte, not bus master, 8-bit only */
		0x22, 0x20, 0x00, /* IRQ 5 */
		0x47, 0x01, 0x34, 0x05, 0x34, 0x05, 0x04, 0x04, /* I/O 0x534, decodes 16-bit, 4-byte alignment, 4 addresses */
		0x47, 0x01, 0x88, 0x03, 0x88, 0x03, 0x08, 0x04, /* I/O 0x388, decodes 16-bit, 8-byte alignment, 4 addresses */
		0x47, 0x01, 0x20, 0x02, 0x20, 0x02, 0x20, 0x10, /* I/O 0x220, decodes 16-bit, 32-byte alignment, 16 addresses */
	0x31, 0x01, /* start dependent functions, acceptable */
		0x2a, 0x0a, 0x28, /* DMA 1/3, type A, no count by word, count by byte, not bus master, 8-bit only */
		0x2a, 0x0b, 0x28, /* DMA 0/1/3, type A, no count by word, count by byte, not bus master, 8-bit only */
		0x22, 0xa0, 0x9a, /* IRQ 5/7/9/11/12/15 */
		0x47, 0x01, 0x34, 0x05, 0xfc, 0x0f, 0x04, 0x04, /* I/O 0x534-0xFFC, decodes 16-bit, 4-byte alignment, 4 addresses */
		0x47, 0x01, 0x88, 0x03, 0x88, 0x03, 0x08, 0x04, /* I/O 0x388, decodes 16-bit, 8-byte alignment, 4 addresses */
		0x47, 0x01, 0x20, 0x02, 0x60, 0x02, 0x20, 0x10, /* I/O 0x220-0x260, decodes 16-bit, 32-byte alignment, 16 addresses */
	0x31, 0x02, /* start dependent functions, sub-optimal */
		0x2a, 0x0b, 0x28, /* DMA 0/1/3, type A, no count by word, count by byte, not bus master, 8-bit only */
		0x22, 0xa0, 0x9a, /* IRQ 5/7/9/11/12/15 */
		0x47, 0x01, 0x34, 0x05, 0xfc, 0x0f, 0x04, 0x04, /* I/O 0x534-0xFFC, decodes 16-bit, 4-byte alignment, 4 addresses */
		0x47, 0x01, 0x88, 0x03, 0xf8, 0x03, 0x08, 0x04, /* I/O 0x388-0x3F8, decodes 16-bit, 8-byte alignment, 4 addresses */
		0x47, 0x01, 0x20, 0x02, 0x00, 0x03, 0x20, 0x10, /* I/O 0x220-0x300, decodes 16-bit, 32-byte alignment, 16 addresses */
	0x38, /* end dependent functions */

    0x15, 0x0e, 0x63, 0x00, 0x01, 0x00, /* logical device CSC0001 */
	0x82, 0x05, 0x00, 'G', 'A', 'M', 'E', 0x00, /* ANSI identifier */
	0x31, 0x00, /* start dependent functions, preferred */
		0x47, 0x01, 0x00, 0x02, 0x00, 0x02, 0x08, 0x08, /* I/O 0x200, decodes 16-bit, 8-byte alignment, 8 addresses */
	0x31, 0x01, /* start dependent functions, acceptable */
		0x47, 0x01, 0x08, 0x02, 0x08, 0x02, 0x08, 0x08, /* I/O 0x208, decodes 16-bit, 8-byte alignment, 8 addresses */
	0x38, /* end dependent functions */

    0x15, 0x0e, 0x63, 0x00, 0x10, 0x00, /* logical device CSC0010 */
	0x82, 0x05, 0x00, 'C', 'T', 'R', 'L', 0x00, /* ANSI identifier */
	0x47, 0x01, 0x20, 0x01, 0xf8, 0x0f, 0x08, 0x08, /* I/O 0x120-0xFF8, decodes 16-bit, 8-byte alignment, 8 addresses */

    0x15, 0x0e, 0x63, 0x00, 0x03, 0x00, /* logical device CSC0003 */
	0x82, 0x04, 0x00, 'M', 'P', 'U', 0x00, /* ANSI identifier */
	0x31, 0x00, /* start dependent functions, preferred */
		0x22, 0x00, 0x02, /* IRQ 9 */
		0x47, 0x01, 0x30, 0x03, 0x30, 0x03, 0x08, 0x02, /* I/O 0x330, decodes 16-bit, 8-byte alignment, 2 addresses */
	0x31, 0x01, /* start dependent functions, acceptable */
		0x22, 0x00, 0x9a, /* IRQ 9/11/12/15 */
		0x47, 0x01, 0x30, 0x03, 0x60, 0x03, 0x08, 0x02, /* I/O 0x330-0x360, decodes 16-bit, 8-byte alignment, 2 addresses */
	0x31, 0x02, /* start dependent functions, sub-optimal */
		0x47, 0x01, 0x30, 0x03, 0xe0, 0x03, 0x08, 0x02, /* I/O 0x330-0x3E0, decodes 16-bit, 8-byte alignment, 2 addresses */
	0x38, /* end dependent functions */

    0x79, 0x00 /* end tag, dummy checksum (filled in by isapnp_add_card) */
};


typedef struct cs423x_t
{
    void	*pnp_card;
    ad1848_t	ad1848;
    sb_t	*sb;
    void	*gameport;
    void	*i2c, *eeprom;

    uint16_t	wss_base, opl_base, sb_base, ctrl_base, ram_addr, eeprom_size: 11, pnp_offset;
    uint8_t	type, ad1848_type, regs[8], indirect_regs[16],
		eeprom_data[2048], ram_data[65536], ram_dl: 2, opl_wss: 1;
    char	*nvr_path;

    uint8_t	pnp_enable: 1, key_pos: 5, slam_enable: 1, slam_state: 2, slam_ld, slam_reg;
    isapnp_device_config_t *slam_config;
} cs423x_t;


static void	cs423x_slam_enable(cs423x_t *dev, uint8_t enable);
static void	cs423x_pnp_enable(cs423x_t *dev, uint8_t update_rom, uint8_t update_hwconfig);
static void	cs423x_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv);


static void
cs423x_nvram(cs423x_t *dev, uint8_t save)
{
    FILE *f = nvr_fopen(dev->nvr_path, save ? "wb" : "rb");
    if (f) {
	if (save)
		fwrite(dev->eeprom_data, sizeof(dev->eeprom_data), 1, f);
	else
		fread(dev->eeprom_data, sizeof(dev->eeprom_data), 1, f);
	fclose(f);
    }
}


static uint8_t
cs423x_read(uint16_t addr, void *priv)
{
    cs423x_t *dev = (cs423x_t *) priv;
    uint8_t reg = addr & 7;
    uint8_t ret = dev->regs[reg];

    switch (reg) {
	case 1: /* EEPROM Interface */
		ret &= ~0x04;
		if ((dev->regs[1] & 0x04) && i2c_gpio_get_sda(dev->i2c))
			ret |= 0x04;
		break;

	case 4: /* Control Indirect Data Register */
		ret = dev->indirect_regs[dev->regs[3]];
		break;

	case 5: /* Control/RAM Access */
		/* Reading RAM is undocumented; the Windows drivers do so. */
		if (dev->ram_dl == 3)
			ret = dev->ram_data[dev->ram_addr++];
		break;

	case 7: /* Global Status */
		/* Context switching: take active context and interrupt flag, then clear interrupt flag. */
		ret &= 0xc0;
		dev->regs[7] &= 0x80;

		if (dev->sb->mpu->state.irq_pending) /* MPU interrupt */
			ret |= 0x08;
		if (dev->ad1848.status & 0x01) /* WSS interrupt */
			ret |= 0x10;
		if (dev->sb->dsp.sb_irq8 || dev->sb->dsp.sb_irq16 || dev->sb->dsp.sb_irq401) /* SBPro interrupt */
			ret |= 0x20;

		break;
    }

    return ret;
}


static void
cs423x_write(uint16_t addr, uint8_t val, void *priv)
{
    cs423x_t *dev = (cs423x_t *) priv;
    uint8_t reg = addr & 0x07;

    switch (reg) {
	case 1: /* EEPROM Interface */
		if (val & 0x04)
			i2c_gpio_set(dev->i2c, val & 0x01, val & 0x02);
		break;

	case 3: /* Control Indirect Access Register */
		val &= 0x0f;
		break;

	case 4: /* Control Indirect Data Register */
		switch (dev->regs[3] & 0x0f) {
			case 0: /* WSS Master Control */
				if (val & 0x80)
					ad1848_init(&dev->ad1848, dev->ad1848_type);
				val = 0x00;
				break;

			case 1: /* Version / Chip ID */
			case 7: /* Reserved */
			case 9 ... 15: /* unspecified */
				return;

			case 2: /* 3D Space and {Center|Volume} */
			case 6: /* Upper Channel Status */
				if (dev->type < CRYSTAL_CS4237B)
					return;
				break;

			case 3: /* 3D Enable */
				if (dev->type < CRYSTAL_CS4237B)
					return;
				val &= 0xe0;
				break;

			case 4: /* Consumer Serial Port Enable */
				if (dev->type < CRYSTAL_CS4237B)
					return;
				val &= 0xf0;
				break;

			case 5: /* Lower Channel Status */
				if (dev->type < CRYSTAL_CS4237B)
					return;
				val &= 0xfe;
				break;

			case 8: /* CS9236 Wavetable Control */
				val &= 0x0f;
				cs423x_pnp_enable(dev, 0, 0);

				/* Update WTEN state on the WSS codec. */
				dev->ad1848.wten = !!(val & 0x08);
				ad1848_updatevolmask(&dev->ad1848);
				break;
		}
		dev->indirect_regs[dev->regs[3]] = val;
		break;

	case 5: /* Control/RAM Access */
		switch (dev->ram_dl) {
			case 0: /* commands */
				switch (val) {
					case 0x55: /* Disable PnP Key */
						dev->pnp_enable = 0;
						/* fall-through */

					case 0x5a: /* Update Hardware Configuration Data */
						cs423x_pnp_enable(dev, 0, 1);
						break;

					case 0x56: /* Disable Crystal Key */
						cs423x_slam_enable(dev, 0);
						break;

					case 0x57: /* Jump to ROM */
						break;

					case 0xaa: /* Download RAM */
						dev->ram_dl = 1;
						break;
				}
				break;

			case 1: /* low address byte */
				dev->ram_addr = val;
				dev->ram_dl++;
				break;

			case 2: /* high address byte */
				dev->ram_addr |= (val << 8);
				dev->ram_dl++;
				break;

			case 3: /* data */
				dev->ram_data[dev->ram_addr++] = val;
				break;
		}
		break;

	case 6: /* RAM Access End */
		if (!val) {
			dev->ram_dl = 0;

			/* Update PnP state and resource data. */
			cs423x_pnp_enable(dev, 1, 0);
		}
		break;

	case 7: /* Global Status */
		return;
    }

    dev->regs[reg] = val;
}


static void
cs423x_slam_write(uint16_t addr, uint8_t val, void *priv)
{
    cs423x_t *dev = (cs423x_t *) priv;
    uint8_t idx;

    switch (dev->slam_state) {
	case CRYSTAL_SLAM_NONE:
		/* Not in SLAM: read and compare Crystal key. */
		if (val == slam_init_key[dev->key_pos]) {
			dev->key_pos++;
			/* Was the key successfully written? */
			if (!dev->key_pos) {
				/* Discard any pending logical device configuration, just to be safe. */
				if (dev->slam_config) {
					free(dev->slam_config);
					dev->slam_config = NULL;
				}

				/* Enter SLAM. */
				dev->slam_state = CRYSTAL_SLAM_INDEX;
			}
		} else {
			dev->key_pos = 0;
		}
		break;

	case CRYSTAL_SLAM_INDEX:
		/* Write register index. */
		dev->slam_reg = val;
		dev->slam_state = CRYSTAL_SLAM_BYTE1;
		break;

	case CRYSTAL_SLAM_BYTE1:
	case CRYSTAL_SLAM_BYTE2:
		/* Write register value: two bytes for I/O ports, single byte otherwise. */
		switch (dev->slam_reg) {
			case 0x06: /* Card Select Number */
				isapnp_set_csn(dev->pnp_card, val);
				break;

			case 0x15: /* Logical Device ID */
				/* Apply the previous logical device's configuration, and reuse its config structure. */
				if (dev->slam_config)
					cs423x_pnp_config_changed(dev->slam_ld, dev->slam_config, dev);
				else
					dev->slam_config = (isapnp_device_config_t *) malloc(sizeof(isapnp_device_config_t));

				/* Start new logical device. */
				memset(dev->slam_config, 0, sizeof(isapnp_device_config_t));
				dev->slam_ld = val;
				break;

			case 0x47: /* I/O Port Base Address 0 */
			case 0x48: /* I/O Port Base Address 1 */
			case 0x42: /* I/O Port Base Address 2 */
				idx = (dev->slam_reg == 0x42) ? 2 : (dev->slam_reg - 0x47);
				if (dev->slam_state == CRYSTAL_SLAM_BYTE1) {
					/* Set high byte, or ignore it if no logical device is selected. */
					if (dev->slam_config)
						dev->slam_config->io[idx].base = val << 8;

					/* Prepare for the second (low byte) write. */
					dev->slam_state = CRYSTAL_SLAM_BYTE2;
					return;
				} else if (dev->slam_config) {
					/* Set low byte, or ignore it if no logical device is selected. */
					dev->slam_config->io[idx].base |= val;
				}
				break;

			case 0x22: /* Interrupt Select 0 */
			case 0x27: /* Interrupt Select 1 */
				/* Stop if no logical device is selected. */
				if (!dev->slam_config)
					break;

				/* Set IRQ value. */
				idx = (dev->slam_reg == 0x22) ? 0 : 1;
				dev->slam_config->irq[idx].irq = val & 15;
				break;

			case 0x2a: /* DMA Select 0 */
			case 0x25: /* DMA Select 1 */
				/* Stop if no logical device is selected. */
				if (!dev->slam_config)
					break;

				/* Set DMA value. */
				idx = (dev->slam_reg == 0x2a) ? 0 : 1;
				dev->slam_config->dma[idx].dma = val & 7;
				break;

			case 0x33: /* Activate Device */
				/* Stop if no logical device is selected. */
				if (!dev->slam_config)
					break;

				/* Activate or deactivate the device. */
				dev->slam_config->activate = val & 0x01;
				break;

			case 0x79: /* activate chip */
				/* Apply the last logical device's configuration. */
				if (dev->slam_config) {
					cs423x_pnp_config_changed(dev->slam_ld, dev->slam_config, dev);
					free(dev->slam_config);
					dev->slam_config = NULL;
				}

				/* Exit out of SLAM. */
				dev->slam_state = CRYSTAL_SLAM_NONE;
				break;
		}

		/* Prepare for the next register, unless a two-byte read returns above. */
		dev->slam_state = CRYSTAL_SLAM_INDEX;
		break;
    }
}


static void
cs423x_slam_enable(cs423x_t *dev, uint8_t enable)
{
    /* Disable SLAM. */
    if (dev->slam_enable) {
	dev->slam_state = CRYSTAL_SLAM_NONE;
	dev->slam_enable = 0;
	io_removehandler(0x279, 1, NULL, NULL, NULL, cs423x_slam_write, NULL, NULL, dev);
    }

    /* Enable SLAM if the CKD bit is not set. */
    if (enable && !(dev->ram_data[0x4002] & 0x10)) {
	dev->slam_enable = 1;
	io_sethandler(0x279, 1, NULL, NULL, NULL, cs423x_slam_write, NULL, NULL, dev);
    }
}


static void
cs423x_ctxswitch_write(uint16_t addr, uint8_t val, void *priv)
{
    cs423x_t *dev = (cs423x_t *) priv;
    uint8_t ctx = (dev->regs[7] & 0x80),
	    enable_opl = (dev->ad1848.xregs[4] & 0x10) && !(dev->indirect_regs[2] & 0x85);

    /* Check if a context switch (WSS=1 <-> SBPro=0) occurred through the address being written. */
    if ((dev->regs[7] & 0x80) ? ((addr & 0xfff0) == dev->sb_base) : ((addr & 0xfffc) == dev->wss_base)) {
	/* Flip context bit. */
	dev->regs[7] ^= 0x80;
	ctx ^= 0x80;

	/* Update CD audio filter.
	   FIXME: not thread-safe: filter function TOCTTOU in sound_cd_thread! */
	sound_set_cd_audio_filter(NULL, NULL);
	if (ctx) /* WSS */
		sound_set_cd_audio_filter(ad1848_filter_cd_audio, &dev->ad1848);
	else /* SBPro */
		sound_set_cd_audio_filter(sbpro_filter_cd_audio, dev->sb);

	/* Fire a context switch interrupt if enabled. */
	if ((dev->regs[0] & 0x20) && (dev->ad1848.irq > 0)) {
		dev->regs[7] |= 0x40; /* set interrupt flag */
		picint(1 << dev->ad1848.irq); /* control device shares IRQ with WSS and SBPro */
	}
    }

    /* Update OPL ownership and state regardless of context switch,
       to trap writes to other registers which may disable the OPL. */
    dev->sb->opl_enabled = !ctx && enable_opl;
    dev->opl_wss = ctx && enable_opl;
}


static void
cs423x_get_buffer(int32_t *buffer, int len, void *priv)
{
    cs423x_t *dev = (cs423x_t *) priv;
    int c, opl_wss = dev->opl_wss;

    /* Output audio from the WSS codec, and also the OPL if we're in charge of it. */
    ad1848_update(&dev->ad1848);
    if (opl_wss)
	opl3_update(&dev->sb->opl);

    /* Don't output anything if the analog section is powered down. */
    if (!(dev->indirect_regs[2] & 0xa4)) {
	for (c = 0; c < len * 2; c += 2) {
		if (opl_wss) {
			buffer[c]     += (dev->sb->opl.buffer[c]     * dev->ad1848.fm_vol_l) >> 16;
			buffer[c + 1] += (dev->sb->opl.buffer[c + 1] * dev->ad1848.fm_vol_r) >> 16;
		}

		buffer[c]     += dev->ad1848.buffer[c]     / 2;
		buffer[c + 1] += dev->ad1848.buffer[c + 1] / 2;
	}
    }

    dev->ad1848.pos = 0;
    if (opl_wss)
	dev->sb->opl.pos = 0;
}


static void
cs423x_pnp_enable(cs423x_t *dev, uint8_t update_rom, uint8_t update_hwconfig)
{
    if (dev->pnp_card) {
	/* Update PnP resource data if requested. */
	if (update_rom)
		isapnp_update_card_rom(dev->pnp_card, &dev->ram_data[dev->pnp_offset], 384);

	/* Hide PnP card if the PKD bit is set, or if PnP was disabled by command 0x55. */
	if ((dev->ram_data[0x4002] & 0x20) || !dev->pnp_enable)
		isapnp_enable_card(dev->pnp_card, ISAPNP_CARD_DISABLE);
	else
		isapnp_enable_card(dev->pnp_card, ISAPNP_CARD_ENABLE);
    }

    /* Update some register bits based on the config data in RAM if requested. */
    if (update_hwconfig) {
	/* Update WTEN. */
	if (dev->ram_data[0x4003] & 0x08) {
		dev->indirect_regs[8] |= 0x08;
		dev->ad1848.wten = 1;
	} else {
		dev->indirect_regs[8] &= ~0x08;
		dev->ad1848.wten = 0;
	}

	/* Update SPS. */
	if (dev->ram_data[0x4003] & 0x04)
		dev->indirect_regs[8] |= 0x04;
	else
		dev->indirect_regs[8] &= ~0x04;

	/* Update IFM. */
	if (dev->ram_data[0x4003] & 0x80)
		dev->ad1848.xregs[4] |= 0x10;
	else
		dev->ad1848.xregs[4] &= ~0x10;

	/* Inform WSS codec of the changes. */
	ad1848_updatevolmask(&dev->ad1848);
    }
}


static void
cs423x_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    cs423x_t *dev = (cs423x_t *) priv;

    switch (ld) {
	case 0: /* WSS, OPL3 and SBPro */
		if (dev->wss_base) {
			io_removehandler(dev->wss_base, 4, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &dev->ad1848);
			io_removehandler(dev->wss_base, 4, NULL, NULL, NULL, cs423x_ctxswitch_write, NULL, NULL, dev);
			dev->wss_base = 0;
		}

		if (dev->opl_base) {
			io_removehandler(dev->opl_base, 4, opl3_read, NULL, NULL, opl3_write, NULL, NULL, &dev->sb->opl);
			dev->opl_base = 0;
		}

		if (dev->sb_base) {
			sb_dsp_setaddr(&dev->sb->dsp, 0);
			io_removehandler(dev->sb_base,     4, opl3_read, NULL, NULL, opl3_write, NULL, NULL, &dev->sb->opl);
			io_removehandler(dev->sb_base + 8, 2, opl3_read, NULL, NULL, opl3_write, NULL, NULL, &dev->sb->opl);
			io_removehandler(dev->sb_base + 4, 2, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, dev->sb);
			io_removehandler(dev->sb_base,    16, NULL, NULL, NULL, cs423x_ctxswitch_write, NULL, NULL, dev);
			dev->sb_base = 0;
		}

		ad1848_setirq(&dev->ad1848, 0);
		sb_dsp_setirq(&dev->sb->dsp, 0);

		ad1848_setdma(&dev->ad1848, 0);
		sb_dsp_setdma8(&dev->sb->dsp, 0);

		if (config->activate) {
			if (config->io[0].base != ISAPNP_IO_DISABLED) {
				dev->wss_base = config->io[0].base;
				io_sethandler(dev->wss_base, 4, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &dev->ad1848);
				io_sethandler(dev->wss_base, 4, NULL, NULL, NULL, cs423x_ctxswitch_write, NULL, NULL, dev);
			}

			if (config->io[1].base != ISAPNP_IO_DISABLED) {
				dev->opl_base = config->io[1].base;
				io_sethandler(dev->opl_base, 4, opl3_read, NULL, NULL, opl3_write, NULL, NULL, &dev->sb->opl);
			}

			if (config->io[2].base != ISAPNP_IO_DISABLED) {
				dev->sb_base = config->io[2].base;
				sb_dsp_setaddr(&dev->sb->dsp, dev->sb_base);
				io_sethandler(dev->sb_base,     4, opl3_read, NULL, NULL, opl3_write, NULL, NULL, &dev->sb->opl);
				io_sethandler(dev->sb_base + 8, 2, opl3_read, NULL, NULL, opl3_write, NULL, NULL, &dev->sb->opl);
				io_sethandler(dev->sb_base + 4, 2, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, dev->sb);
				io_sethandler(dev->sb_base,    16, NULL, NULL, NULL, cs423x_ctxswitch_write, NULL, NULL, dev);
			}

			if (config->irq[0].irq != ISAPNP_IRQ_DISABLED) {
				ad1848_setirq(&dev->ad1848, config->irq[0].irq);
				sb_dsp_setirq(&dev->sb->dsp, config->irq[0].irq);
			}

			if (config->dma[0].dma != ISAPNP_DMA_DISABLED) {
				ad1848_setdma(&dev->ad1848, config->dma[0].dma);
				sb_dsp_setdma8(&dev->sb->dsp, config->dma[0].dma);
			}
		}
		break;

	case 1: /* Game Port */
		if (dev->gameport)
			gameport_remap(dev->gameport, (config->activate && (config->io[0].base != ISAPNP_IO_DISABLED)) ? config->io[0].base : 0);
		break;

	case 2: /* Control Registers */
		if (dev->ctrl_base) {
			io_removehandler(dev->ctrl_base, 8, cs423x_read, NULL, NULL, cs423x_write, NULL, NULL, dev);
			dev->ctrl_base = 0;
		}

		if (config->activate && (config->io[0].base != ISAPNP_IO_DISABLED)) {
			dev->ctrl_base = config->io[0].base;
			io_sethandler(dev->ctrl_base, 8, cs423x_read, NULL, NULL, cs423x_write, NULL, NULL, dev);
		}

		break;

	case 3: /* MPU-401 */
		mpu401_change_addr(dev->sb->mpu, 0);
		mpu401_setirq(dev->sb->mpu, 0);

		if (config->activate) {
			if (config->io[0].base != ISAPNP_IO_DISABLED)
				mpu401_change_addr(dev->sb->mpu, config->io[0].base);

			if (config->irq[0].irq != ISAPNP_IRQ_DISABLED)
				mpu401_setirq(dev->sb->mpu, config->irq[0].irq);
		}

		break;
    }
}


static void
cs423x_reset(void *priv)
{
    cs423x_t *dev = (cs423x_t *) priv;

    /* Clear RAM. */
    memset(dev->ram_data, 0, sizeof(dev->ram_data));

    if (dev->eeprom) {
	/* Load EEPROM data to RAM. */
	memcpy(&dev->ram_data[0x4000], &dev->eeprom_data[4], MIN(384, ((dev->eeprom_data[2] << 8) | dev->eeprom_data[3]) - 4));

	/* Save EEPROM contents to file. */
	cs423x_nvram(dev, 1);
    }

    /* Reset registers. */
    memset(dev->regs, 0, sizeof(dev->regs));
    dev->regs[1] = 0x80;
    memset(dev->indirect_regs, 0, sizeof(dev->indirect_regs));
    dev->indirect_regs[1] = dev->type;
    if (dev->type == CRYSTAL_CS4238B)
	dev->indirect_regs[2] = 0x20;

    /* Reset WSS codec. */
    ad1848_init(&dev->ad1848, dev->ad1848_type);

    /* Reset PnP resource data, state and logical devices. */
    dev->pnp_enable = 1;
    cs423x_pnp_enable(dev, 1, 1);
    if (dev->pnp_card)
	isapnp_reset_card(dev->pnp_card);

    /* Reset SLAM. */
    cs423x_slam_enable(dev, 1);
}


static void *
cs423x_init(const device_t *info)
{
    cs423x_t *dev = malloc(sizeof(cs423x_t));
    memset(dev, 0, sizeof(cs423x_t));

    /* Initialize model-specific data. */
    dev->type = info->local;
    switch (dev->type) {
	case CRYSTAL_CS4236B:
	case CRYSTAL_CS4237B:
	case CRYSTAL_CS4238B:
		/* Same WSS codec and EEPROM structure. */
		dev->ad1848_type = AD1848_TYPE_CS4236;
		dev->pnp_offset = 0x4013;

		/* Different Chip Version and ID registers, which shouldn't be reset by ad1848_init */
		dev->ad1848.xregs[25] = dev->type;

		/* Load EEPROM contents from template. */
		memcpy(dev->eeprom_data, cs4236b_eeprom, sizeof(cs4236b_eeprom));

		/* Set content size. */
		dev->eeprom_data[2] = sizeof(cs4236b_eeprom) >> 8;
		dev->eeprom_data[3] = sizeof(cs4236b_eeprom) & 0xff;

		/* Set PnP card ID and EEPROM file name. */
		switch (dev->type) {
			case CRYSTAL_CS4236B:
				dev->nvr_path = "cs4236b.nvr";
				break;

			case CRYSTAL_CS4237B:
				dev->eeprom_data[26] = 0x37;
				dev->nvr_path = "cs4237b.nvr";
				break;

			case CRYSTAL_CS4238B:
				dev->eeprom_data[26] = 0x38;
				dev->nvr_path = "cs4238b.nvr";
				break;
		}

		/* Load EEPROM contents from file if present. */
		cs423x_nvram(dev, 0);

		/* Initialize game port. The '7B and '8B game port only responds to 6 I/O ports; the remaining
		   2 ports are reserved on those chips, and probably connected to the Digital Assist feature. */
		dev->gameport = gameport_add((dev->type == CRYSTAL_CS4236B) ? &gameport_pnp_device : &gameport_pnp_6io_device);

		break;
    }

    /* Initialize I2C bus for the EEPROM. */
    dev->i2c = i2c_gpio_init("nvr_cs423x");

    /* Initialize I2C EEPROM if the contents are valid. */
    if ((dev->eeprom_data[0] == 0x55) && (dev->eeprom_data[1] == 0xbb))
	dev->eeprom = i2c_eeprom_init(i2c_gpio_get_bus(dev->i2c), 0x50, dev->eeprom_data, sizeof(dev->eeprom_data), 1);

    /* Initialize ISAPnP. */
    dev->pnp_card = isapnp_add_card(NULL, 0, cs423x_pnp_config_changed, NULL, NULL, NULL, dev);

    /* Initialize SBPro codec first to get the correct CD audio filter for the default
       context, which is SBPro. The WSS codec is initialized later by cs423x_reset */
    dev->sb = device_add(&sb_pro_compat_device);

    /* Initialize RAM, registers and WSS codec. */
    cs423x_reset(dev);
    sound_add_handler(cs423x_get_buffer, dev);

    return dev;
}


static void
cs423x_close(void *priv)
{
    cs423x_t *dev = (cs423x_t *) priv;

    /* Save EEPROM contents to file. */
    if (dev->eeprom) {
	cs423x_nvram(dev, 1);
	i2c_eeprom_close(dev->eeprom);
    }

    i2c_gpio_close(dev->i2c);

    free(dev);
}


static void
cs423x_speed_changed(void *priv)
{
    cs423x_t *dev = (cs423x_t *) priv;

    ad1848_speed_changed(&dev->ad1848);
}


const device_t cs4236b_device =
{
    "Crystal CS4236B",
    DEVICE_ISA | DEVICE_AT,
    CRYSTAL_CS4236B,
    cs423x_init, cs423x_close, cs423x_reset,
    { NULL },
    cs423x_speed_changed,
    NULL,
    NULL
};

const device_t cs4237b_device =
{
    "Crystal CS4237B",
    DEVICE_ISA | DEVICE_AT,
    CRYSTAL_CS4237B,
    cs423x_init, cs423x_close, cs423x_reset,
    { NULL },
    cs423x_speed_changed,
    NULL,
    NULL
};

const device_t cs4238b_device =
{
    "Crystal CS4238B",
    DEVICE_ISA | DEVICE_AT,
    CRYSTAL_CS4238B,
    cs423x_init, cs423x_close, cs423x_reset,
    { NULL },
    cs423x_speed_changed,
    NULL,
    NULL
};
