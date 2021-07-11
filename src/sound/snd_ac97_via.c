/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		VIA AC'97 audio controller emulation.
 *
 *
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2021 RichardG.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/pci.h>
#include <86box/sound.h>
#include <86box/snd_ac97.h>

#define SGD_UNPAUSED	(dev->sgd_regs[0x00] & 0xc4) == 0x80


typedef struct {
    uint16_t	audio_sgd_base, audio_codec_base, modem_sgd_base, modem_codec_base;
    uint8_t	sgd_regs[256], irq_stuck;
    int		slot, irq_pin, losticount;
    uint64_t	sgd_entry;
    uint32_t	sgd_entry_ptr, sgd_sample_ptr;
    int32_t	sgd_sample_count;

    ac97_codec_t *codec[4];

    pc_timer_t	timer_count;
    uint64_t	timer_latch;
    int16_t	out_l, out_r;
    double	cd_vol_l, cd_vol_r;
    int16_t	buffer[SOUNDBUFLEN * 2];
    int		pos;
} ac97_via_t;

#define ENABLE_AC97_VIA_LOG 1
#ifdef ENABLE_AC97_VIA_LOG
int ac97_via_do_log = ENABLE_AC97_VIA_LOG;

static void
ac97_via_log(const char *fmt, ...)
{
    va_list ap;

    if (ac97_via_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define ac97_via_log(fmt, ...)
#endif


static void	ac97_via_poll(void *priv);


void
ac97_via_set_slot(void *priv, int slot, int irq_pin)
{
    ac97_via_t *dev = (ac97_via_t *) priv;

    ac97_via_log("AC97 VIA: set_slot(%d, %d)\n", slot, irq_pin);

    dev->slot = slot;
    dev->irq_pin = irq_pin;
}


uint8_t
ac97_via_read_status(void *priv, uint8_t modem)
{
    ac97_via_t *dev = (ac97_via_t *) priv;
    uint8_t ret = 0x00;

    /* Flag codecs as ready if present. */
    for (uint8_t i = 0; i <= 1; i++) {
	if (dev->codec[(modem << 1) | i])
		ret |= 0x01 << (i << 1);
    }

    ac97_via_log("AC97 VIA %d: read_status() = %02X\n", modem, ret);

    return ret;
}


static void
ac97_via_sgd_startstop(ac97_via_t *dev)
{
    /* Start polling timer if SGD is unpaused. */
#if 0
    if (SGD_UNPAUSED) {
	ac97_via_log("AC97 VIA: Starting SGD at %08X\n", dev->sgd_entry_ptr);
	timer_set_delay_u64(&dev->timer_count, dev->timer_latch);
    } else {
	ac97_via_log("AC97 VIA: Stopping SGD\n");
	timer_disable(&dev->timer_count);
	dev->out_l = dev->out_r = 0;
    }
#endif
}


static void
ac97_via_sgd_block_start(ac97_via_t *dev)
{
    /* Start at first entry. */
    if (!dev->sgd_entry_ptr)
	dev->sgd_entry_ptr = (dev->sgd_regs[0x07] << 24) | (dev->sgd_regs[0x06] << 16) | (dev->sgd_regs[0x05] << 8) | (dev->sgd_regs[0x04] & 0xfe);

    /* Read entry. */
    dev->sgd_entry = ((uint64_t) mem_readl_phys(dev->sgd_entry_ptr + 4) << 32ULL) | (uint64_t) mem_readl_phys(dev->sgd_entry_ptr);
    if (dev->sgd_entry == 0xffffffffffffffffULL)
	fatal("AC97 VIA: Invalid SGD entry at %08X\n", dev->sgd_entry_ptr);

    /* Set sample pointer and count. */
    dev->sgd_sample_ptr = dev->sgd_entry & 0xffffffff;
    dev->sgd_sample_count = (dev->sgd_entry >> 32) & 0xffffff;

    ac97_via_log("AC97 VIA: Starting SGD block at %08X entry %08X%08X (start %08X len %06X) losticount %d\n",
		 dev->sgd_entry_ptr, mem_readl_phys(dev->sgd_entry_ptr + 4), mem_readl_phys(dev->sgd_entry_ptr), dev->sgd_sample_ptr, dev->sgd_sample_count, dev->losticount);
}


uint8_t
ac97_via_sgd_read(uint16_t addr, void *priv)
{
    ac97_via_t *dev = (ac97_via_t *) priv;
    uint8_t modem = (addr & 0xff00) == dev->modem_sgd_base;
    addr &= 0xff;
    uint8_t ret = 0x00;

    switch (addr) {
	case 0x04:
		ret = dev->sgd_entry_ptr;
		break;

	case 0x05:
		ret = dev->sgd_entry_ptr >> 8;
		break;

	case 0x06:
		ret = dev->sgd_entry_ptr >> 16;
		break;

	case 0x07:
		ret = dev->sgd_entry_ptr >> 24;
		/*pclog("sgd state %02X unpaused %d rct %02X\n", dev->sgd_regs[0x00], SGD_UNPAUSED, dev->sgd_regs[0x02]);
		pci_clear_irq(dev->slot, dev->irq_pin);*/
		break;

	case 0x0c:
		ret = dev->sgd_sample_count;
		break;

	case 0x0d:
		ret = dev->sgd_sample_count >> 8;
		break;

	case 0x0e:
		ret = dev->sgd_sample_count >> 16;
		break;

	case 0x84:
		ret |= (dev->sgd_regs[0x00] & 0x01);
		ret |= (dev->sgd_regs[0x10] & 0x01) << 1;
		ret |= (dev->sgd_regs[0x20] & 0x01) << 2;

		ret |= (dev->sgd_regs[0x00] & 0x02) << 3;
		ret |= (dev->sgd_regs[0x10] & 0x02) << 4;
		ret |= (dev->sgd_regs[0x20] & 0x02) << 5;
		break;

	case 0x85:
		ret |= (dev->sgd_regs[0x00] & 0x04) >> 2;
		ret |= (dev->sgd_regs[0x10] & 0x04) >> 1;
		ret |= (dev->sgd_regs[0x20] & 0x04);

		ret |= (dev->sgd_regs[0x00] & 0x80) >> 3;
		ret |= (dev->sgd_regs[0x10] & 0x80) >> 2;
		ret |= (dev->sgd_regs[0x20] & 0x80) >> 1;
		break;

	case 0x86:
		ret |= (dev->sgd_regs[0x40] & 0x01);
		ret |= (dev->sgd_regs[0x50] & 0x01) << 1;

		ret |= (dev->sgd_regs[0x40] & 0x02) << 3;
		ret |= (dev->sgd_regs[0x50] & 0x02) << 4;
		break;

	case 0x87:
		ret |= (dev->sgd_regs[0x40] & 0x04) >> 2;
		ret |= (dev->sgd_regs[0x50] & 0x04) >> 1;

		ret |= (dev->sgd_regs[0x40] & 0x80) >> 3;
		ret |= (dev->sgd_regs[0x50] & 0x80) >> 2;
		break;

	default:
		ret = dev->sgd_regs[addr];
		break;
    }

    ac97_via_log("AC97 VIA %d: sgd_read(%02X) = %02X\n", modem, addr, ret);

    return ret;
}


void
ac97_via_sgd_write(uint16_t addr, uint8_t val, void *priv)
{
    ac97_via_t *dev = (ac97_via_t *) priv;
    uint8_t modem = (addr & 0xff00) == dev->modem_sgd_base, i;
    ac97_codec_t *codec;
    addr &= 0xff;

    ac97_via_log("AC97 VIA %d: sgd_write(%02X, %02X)\n", modem, addr, val);

    /* Check function-specific read only registers. */
    if ((addr >= (modem ? 0x00 : 0x40)) && (addr < (modem ? 0x40 : 0x60)))
	return;
    if (addr >= (modem ? 0x90 : 0x88))
	return;

    /* Check read-only registers for each SGD channel. */
    if (!(addr & 0x80)) {
	switch (addr & 0xf) {
		case 0x0:
			/* Clear RWC status bits. */
			for (i = 0x01; i <= 0x04; i <<= 1) {
				if (val & i)
					dev->sgd_regs[addr] &= ~i;
			}

			if (addr == 0x00) {
				if (!(dev->sgd_regs[0x00] & (dev->sgd_regs[0x02] & 0x03))) {
					ac97_via_log("AC97 VIA: Clearing IRQ (iflags %02X)\n", dev->sgd_regs[0x00] & (dev->sgd_regs[0x02] & 0x03));
					pci_clear_irq(dev->slot, dev->irq_pin);
					dev->irq_stuck = 0;
				}

				/* Resume SGD if requested. */
				if (val & 0x04)
					ac97_via_sgd_startstop(dev);
			}

			/* fall-through */

		case 0x3: case 0x8 ... 0xf:
			return;
	}
    }

    switch (addr) {
	case 0x30 ... 0x3f:
	case 0x60 ... 0x7f:
		/* Read-only registers. */
		return;

	case 0x01:
		/* Start SGD if requested. */
		if (val & 0x80) {
			if (dev->sgd_regs[0x00] & 0x80) {
				/* Queue SGD trigger. */
				dev->sgd_regs[0x00] |= 0x08;
			} else {
				/* Start SGD immediately. */
				dev->sgd_regs[0x00] |= 0x80;
				dev->sgd_regs[0x00] &= ~0x44;

				dev->sgd_entry = 0;
				dev->sgd_entry_ptr = 0;
			}
		}
		/* Stop SGD if requested. */
		if (val & 0x40) {
			dev->sgd_regs[0x00] &= ~0x88;
		}

		val &= 0x04;

		/* (Un)pause SGD if requested. */
		if (val & 0x04)
			dev->sgd_regs[0x00] |= 0x40;
		else
			dev->sgd_regs[0x00] &= ~0x40;

		ac97_via_sgd_startstop(dev);
		break;

	case 0x82:
		/* Determine the selected codec. */
		i = !!(dev->sgd_regs[0x83] & 0x40);
		codec = dev->codec[(modem << 1) | i];

		/* Read from or write to codec. */
		if (codec) {
			if (val & 0x80) {
				val <<= 1;
				dev->sgd_regs[0x80] = ac97_codec_read(codec, val);
				dev->sgd_regs[0x81] = ac97_codec_read(codec, val | 1);
			} else {
				val <<= 1;
				ac97_codec_write(codec, val, dev->sgd_regs[0x80]);
				ac97_codec_write(codec, val | 1, dev->sgd_regs[0x81]);
			}
		} else if (val & 0x80) {
			/* Unknown behavior when reading from a non-existent codec. */
			dev->sgd_regs[0x80] = dev->sgd_regs[0x81] = 0xff;
		}

		/* Flag data/status/index for this codec as valid. */
		dev->sgd_regs[0x83] |= 0x02 << (i * 2);
		break;

	case 0x83:
		val &= 0xca;

		/* Clear RWC bits. */
		for (i = 0x02; i <= 0x08; i <<= 2) {
#if 0 /* race condition with Linux clearing bits and starting SGD on the same dword write */
			if (val & i)
				val &= ~i;
			else
				val |= dev->sgd_regs[addr] & i;
#else
			val |= i;
#endif
		}
		break;
    }

    dev->sgd_regs[addr] = val;
}


void
ac97_via_remap_audio_sgd(void *priv, uint16_t new_io_base, uint8_t enable)
{
    ac97_via_t *dev = (ac97_via_t *) priv;

    if (dev->audio_sgd_base)
	io_removehandler(dev->audio_sgd_base, 256, ac97_via_sgd_read, NULL, NULL, ac97_via_sgd_write, NULL, NULL, dev);

    dev->audio_sgd_base = new_io_base;

    if (dev->audio_sgd_base && enable)
	io_sethandler(dev->audio_sgd_base, 256, ac97_via_sgd_read, NULL, NULL, ac97_via_sgd_write, NULL, NULL, dev);
}


void
ac97_via_remap_modem_sgd(void *priv, uint16_t new_io_base, uint8_t enable)
{
    ac97_via_t *dev = (ac97_via_t *) priv;

    if (dev->modem_sgd_base)
	io_removehandler(dev->modem_sgd_base, 256, ac97_via_sgd_read, NULL, NULL, ac97_via_sgd_write, NULL, NULL, dev);

    dev->modem_sgd_base = new_io_base;

    if (dev->modem_sgd_base && enable)
	io_sethandler(dev->modem_sgd_base, 256, ac97_via_sgd_read, NULL, NULL, ac97_via_sgd_write, NULL, NULL, dev);
}


uint8_t
ac97_via_codec_read(uint16_t addr, void *priv)
{
    ac97_via_t *dev = (ac97_via_t *) priv;
    uint8_t modem = (addr & 0xff00) == dev->modem_codec_base;
    addr &= 0xff;
    uint8_t ret = 0xff;

    /* Bit 7 selects secondary codec. */
    ac97_codec_t *codec = dev->codec[(modem << 1) | (addr >> 7)];
    if (codec)
	ret = ac97_codec_read(codec, addr & 0x7f);

    ac97_via_log("AC97 VIA %d: codec_read(%02X) = %02X\n", modem, addr, ret);

    return ret;
}


void
ac97_via_codec_write(uint16_t addr, uint8_t val, void *priv)
{
    ac97_via_t *dev = (ac97_via_t *) priv;
    uint8_t modem = (addr & 0xff00) == dev->modem_codec_base;
    addr &= 0xff;

    ac97_via_log("AC97 VIA %d: codec_write(%02X, %02X)\n", modem, addr, val);

    /* Bit 7 selects secondary codec. */
    ac97_codec_t *codec = dev->codec[(modem << 1) | (addr >> 7)];
    if (codec)
	ac97_codec_write(codec, addr, val);
}


void
ac97_via_remap_audio_codec(void *priv, uint16_t new_io_base, uint8_t enable)
{
    ac97_via_t *dev = (ac97_via_t *) priv;

    if (dev->audio_codec_base)
	io_removehandler(dev->audio_codec_base, 256, ac97_via_codec_read, NULL, NULL, ac97_via_codec_write, NULL, NULL, dev);

    dev->audio_codec_base = new_io_base;

    if (dev->audio_codec_base && enable)
	io_sethandler(dev->audio_codec_base, 256, ac97_via_codec_read, NULL, NULL, ac97_via_codec_write, NULL, NULL, dev);
}


void
ac97_via_remap_modem_codec(void *priv, uint16_t new_io_base, uint8_t enable)
{
    ac97_via_t *dev = (ac97_via_t *) priv;

    if (dev->modem_codec_base)
	io_removehandler(dev->modem_codec_base, 256, ac97_via_codec_read, NULL, NULL, ac97_via_codec_write, NULL, NULL, dev);

    dev->modem_codec_base = new_io_base;

    if (dev->modem_codec_base && enable)
	io_sethandler(dev->modem_codec_base, 256, ac97_via_codec_read, NULL, NULL, ac97_via_codec_write, NULL, NULL, dev);
}


static void
ac97_via_update(ac97_via_t *dev)
{
    for (; dev->pos < sound_pos_global; dev->pos++) {
	dev->buffer[dev->pos*2]     = dev->out_l;
	dev->buffer[dev->pos*2 + 1] = dev->out_r;
    }
}


static void
ac97_via_poll(void *priv)
{
    ac97_via_t *dev = (ac97_via_t *) priv;

    timer_advance_u64(&dev->timer_count, dev->timer_latch);

    ac97_via_update(dev);

    /* Read only if SGD is active and not paused. */
    if (SGD_UNPAUSED) {
	if (!dev->sgd_entry) {
		/* Move on to the next block. */
		ac97_via_sgd_block_start(dev);
	}

	switch (dev->sgd_regs[0x02] & 0x30) {
		case 0x00: /* Mono, 8-bit PCM */
			dev->out_l = dev->out_r = (mem_readb_phys(dev->sgd_sample_ptr++) ^ 0x80) * 256;
			dev->sgd_sample_count--;
			break;

		case 0x10: /* Stereo, 8-bit PCM */
			dev->out_l = (mem_readb_phys(dev->sgd_sample_ptr++) ^ 0x80) * 256;
			dev->out_r = (mem_readb_phys(dev->sgd_sample_ptr++) ^ 0x80) * 256;
			dev->sgd_sample_count -= 2;
			break;

		case 0x20: /* Mono, 16-bit PCM */
			dev->out_l = dev->out_r = mem_readw_phys(dev->sgd_sample_ptr);
			dev->sgd_sample_ptr += 2;
			dev->sgd_sample_count -= 2;
			break;

		case 0x30: /* Stereo, 16-bit PCM */
			dev->out_l = mem_readw_phys(dev->sgd_sample_ptr);
			dev->sgd_sample_ptr += 2;
			dev->out_r = mem_readw_phys(dev->sgd_sample_ptr);
			dev->sgd_sample_ptr += 2;
			dev->sgd_sample_count -= 4;
			break;
	}

	/* Check if we've hit the end of this block. */
	if (dev->sgd_sample_count <= 0) {
		ac97_via_log("AC97 VIA: Ending SGD block");

		/* Move on to the next block on the next poll. */
		dev->sgd_entry_ptr += 8;

		if (dev->sgd_entry & 0x2000000000000000ULL) {
			ac97_via_log(" with STOP");
			dev->sgd_regs[0x00] |= 0x04;
		}
		if (dev->sgd_entry & 0x4000000000000000ULL) {
			ac97_via_log(" with FLAG");
			dev->sgd_regs[0x00] |= 0x01;

			/* Pause SGD. */
			dev->sgd_regs[0x00] |= 0x04;

			/* Fire interrupt if requested. */
			if (dev->sgd_regs[0x02] & 0x01)
				ac97_via_log(" interrupt");
		}
		if (dev->sgd_entry & 0x8000000000000000ULL) {
			ac97_via_log(" with EOL");
			dev->sgd_regs[0x00] |= 0x02;

			/* Fire interrupt if requested. */
			if (dev->sgd_regs[0x02] & 0x02)
				ac97_via_log(" interrupt");

			/* Restart SGD if a trigger is queued or auto-start is enabled. */
			if ((dev->sgd_regs[0x00] & 0x08) || (dev->sgd_regs[0x02] & 0x80)) {
				ac97_via_log(" restart\n");
				dev->sgd_regs[0x00] &= ~0x08;

				dev->sgd_entry_ptr = 0;
			} else {
				ac97_via_log(" finish\n");
				dev->sgd_regs[0x00] &= ~0x80;
			}
		} else {
			ac97_via_log("\n");
		}

		dev->sgd_entry = dev->sgd_sample_count = 0;
	}
    } else {
	dev->out_l = dev->out_r = 0;
	dev->cd_vol_l = dev->cd_vol_r = 0;
    }

    if (dev->sgd_regs[0x00] & (dev->sgd_regs[0x02] & 0x03)) {
	ac97_via_log("AC97 VIA: Setting IRQ (iflags %02X stuck %d)\n", dev->sgd_regs[0x00] & (dev->sgd_regs[0x02] & 0x03), dev->irq_stuck);
	if (dev->irq_stuck) {
		dev->losticount++;
		pci_clear_irq(dev->slot, dev->irq_pin);
	} else {
		pci_set_irq(dev->slot, dev->irq_pin);
	}
	dev->irq_stuck = !dev->irq_stuck;
    }
}


static void
ac97_via_get_buffer(int32_t *buffer, int len, void *priv)
{
    ac97_via_t *dev = (ac97_via_t *) priv;

    ac97_via_update(dev);

    for (int c = 0; c < len * 2; c++) {
	buffer[c] += dev->buffer[c];
    }

    dev->pos = 0;
}


static void
ac97_via_speed_changed(void *priv)
{
    ac97_via_t *dev = (ac97_via_t *) priv;
    dev->timer_latch = (uint64_t) ((double) TIMER_USEC * (1000000.0 / 48000.0));
}


static void *
ac97_via_init(const device_t *info)
{
    ac97_via_t *dev = malloc(sizeof(ac97_via_t));
    memset(dev, 0, sizeof(ac97_via_t));

    ac97_via_log("AC97 VIA: init()\n");

    ac97_codec = &dev->codec[0];
    ac97_modem_codec = &dev->codec[2];
    ac97_codec_count = ac97_modem_codec_count = 2;

    timer_add(&dev->timer_count, ac97_via_poll, dev, 0);
    ac97_via_speed_changed(dev);
    timer_advance_u64(&dev->timer_count, dev->timer_latch);

    sound_add_handler(ac97_via_get_buffer, dev);

    return dev;
}


static void
ac97_via_close(void *priv)
{
    ac97_via_t *dev = (ac97_via_t *) priv;

    ac97_via_log("AC97 VIA: close()\n");

    free(dev);
}


const device_t ac97_via_device =
{
    "VIA VT82C686 AC97 Controller",
    DEVICE_PCI,
    0,
    ac97_via_init, ac97_via_close, NULL,
    { NULL },
    ac97_via_speed_changed,
    NULL,
    NULL
};
