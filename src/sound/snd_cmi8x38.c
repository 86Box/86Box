/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		C-Media CMI8x38 PCI audio controller emulation.
 *
 *
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2022 RichardG.
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
#include <86box/snd_sb.h>


enum {
    CMEDIA_CMI8338 = 0x00,
    CMEDIA_CMI8738 = 0x11
};

typedef struct {
    uint8_t	id, reg, always_run, playback_enabled;
    struct _cmi8x38_ *dev;

    uint32_t	sample_ptr, fifo_pos, fifo_end;
    int32_t	frame_count_dma, frame_count_fragment, sample_count_out;
    uint8_t	fifo[32], restart;

    int16_t	out_l, out_r;
    int		vol_l, vol_r, pos;
    int32_t	buffer[SOUNDBUFLEN * 2];
    uint64_t	timer_latch;

    pc_timer_t	dma_timer, poll_timer;
} cmi8x38_dma_t;

typedef struct _cmi8x38_ {
    uint16_t	io_base;
    uint8_t	type, pci_regs[256], io_regs[256], mixer_ext_regs[16];
    int		slot;

    sb_t	*sb;
    void	*gameport;
    cmi8x38_dma_t dma[2];

    int		master_vol_l, master_vol_r, cd_vol_l, cd_vol_r;
} cmi8x38_t;

#define ENABLE_CMI8X38_LOG 1
#ifdef ENABLE_CMI8X38_LOG
int cmi8x38_do_log = ENABLE_CMI8X38_LOG;

static void
cmi8x38_log(const char *fmt, ...)
{
    va_list ap;

    if (cmi8x38_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define cmi8x38_log(fmt, ...)
#endif

static const double freqs[] = {5512.0, 11025.0, 22050.0, 44100.0, 8000.0, 16000.0, 32000.0, 48000.0};


static void	cmi8x38_dma_process(void *priv);
static void	cmi8x38_speed_changed(void *priv);


static void
cmi8x38_update_irqs(cmi8x38_t *dev)
{
    /* Calculate and use the any interrupt flag. */
    if (*((uint32_t *) &dev->io_regs[0x10]) & 0x0401c003) {
	dev->io_regs[0x13] |= 0x80;
	pci_set_irq(dev->slot, PCI_INTA);
	cmi8x38_log("CMI8x38: Raising IRQ\n");
    } else {
	dev->io_regs[0x13] &= ~0x80;
	pci_clear_irq(dev->slot, PCI_INTA);
    }
}


static void
cmi8x38_start_playback(cmi8x38_t *dev, uint8_t val)
{
    uint8_t i;

    i = !(val & 0x01);
    if (!dev->dma[0].playback_enabled && i)
	timer_advance_u64(&dev->dma[0].poll_timer, dev->dma[0].timer_latch);
    dev->dma[0].playback_enabled = i;

    i = !(val & 0x02);
    if (!dev->dma[1].playback_enabled && i)
	timer_advance_u64(&dev->dma[1].poll_timer, dev->dma[1].timer_latch);
    dev->dma[1].playback_enabled = i;
}


static uint8_t
cmi8x38_read(uint16_t addr, void *priv)
{
    cmi8x38_t *dev = (cmi8x38_t *) priv;
    addr &= 0xff;
    uint8_t ret;

    switch (addr) {
	case 0x22:
		sb_ct1745_mixer_t *mixer = &dev->sb->mixer_sb16;
		if (mixer->index >= 0xf0)
			ret = dev->mixer_ext_regs[mixer->index & 0x0f];
		else
			ret = sb_ct1745_mixer_read(1, dev->sb);
		break;

	case 0x23:
		ret = sb_ct1745_mixer_read(0, dev->sb);
		break;

	case 0x40 ... 0x4f:
		if (dev->type == CMEDIA_CMI8338)
			goto io_reg;
		else
			ret = mpu401_read(addr, val, dev->sb->mpu);
		break;

	case 0x50 ... 0x5f:
		if (dev->type == CMEDIA_CMI8338)
			goto io_reg;
		else
			ret = opl3_read(addr, &dev->sb->opl);
		break;

	case 0x80: case 0x88:
		ret = dev->dma[(addr & 0x78) >> 3].sample_ptr;
		break;

	case 0x81: case 0x89:
		ret = dev->dma[(addr & 0x78) >> 3].sample_ptr >> 8;
		break;

	case 0x82: case 0x8a:
		ret = dev->dma[(addr & 0x78) >> 3].sample_ptr >> 16;
		break;

	case 0x83: case 0x8b:
		ret = dev->dma[(addr & 0x78) >> 3].sample_ptr >> 24;
		break;

	case 0x84: case 0x8c:
		ret = dev->dma[(addr & 0x78) >> 3].frame_count_dma;
		break;

	case 0x85: case 0x8d:
		ret = dev->dma[(addr & 0x78) >> 3].frame_count_dma >> 8;
		break;

	case 0x86: case 0x8e:
		ret = dev->dma[(addr & 0x78) >> 3].sample_count_out >> 2;
		break;

	case 0x87: case 0x8f:
		ret = dev->dma[(addr & 0x78) >> 3].sample_count_out >> 10;
		break;

	default:
io_reg:		ret = dev->io_regs[addr];
		break;
    }
 
    cmi8x38_log("CMI8x38: read(%02X) = %02X\n", addr, ret);
    return ret;
}


static void
cmi8x38_write(uint16_t addr, uint8_t val, void *priv)
{
    cmi8x38_t *dev = (cmi8x38_t *) priv;
    addr &= 0xff;
    cmi8x38_log("CMI8x38: write(%02X, %02X)\n", addr, val);

    switch (addr) {
	case 0x00:
		val &= 0x0f;

		/* Don't care about recording DMA. */
		dev->dma[0].always_run = val & 0x01;
		dev->dma[1].always_run = val & 0x02;

		/* Start playback if requested. */
		cmi8x38_start_playback(dev, val);
		break;

	case 0x02:
		/* Reset DMA channels if requested. */
		if (val & 0x04)
			val &= ~0x01;
		if (val & 0x08)
			val &= ~0x02;

		val &= 0x03;
		dev->io_regs[addr] = val;

		/* Start DMA channels if requested. */
		if (val & 0x01) {
			cmi8x38_log("CMI8x38: DMA 0 trigger\n");
			dev->dma[0].restart = 1;
			cmi8x38_dma_process(&dev->dma[0]);
		}
		if (val & 0x02) {
			cmi8x38_log("CMI8x38: DMA 1 trigger\n");
			dev->dma[1].restart = 1;
			cmi8x38_dma_process(&dev->dma[1]);
		}

		/* Start playback along with DMA channels. */
		if (val & 0x03)
			cmi8x38_start_playback(dev, dev->io_regs[0x00]);
		break;

	case 0x04:
		/* Enable or disable the game port. */
		gameport_remap(dev->gameport, (val & 0x02) ? 0x200 : 0);
		break;

	case 0x05:
		dev->io_regs[addr] = val;
		cmi8x38_speed_changed(dev);
		break;

	case 0x08:
		if (dev->type == CMEDIA_CMI8338)
			val &= 0x0f;
		break;

	case 0x09:
#if 0 /* actual CMI8338 behavior unconfirmed; this register is required for the Windows XP driver which outputs 96K */
		if (dev->type == CMEDIA_CMI8338)
			return;
#endif
		cmi8x38_speed_changed(dev);
		break;

	case 0x0a: case 0x0b:
		if (dev->type == CMEDIA_CMI8338)
			return;
		else
			val &= 0xe0;

		if (addr == 0x0a)
			dev->pci_regs[0x0d] = (val & 0x80) ? 0x48 : 0x20; /* clearing SETLAT48 is undefined */
		break;

	case 0x0e:
		val &= 0x07;

		/* Clear interrupts. */
		dev->io_regs[0x10] &= val | 0xfc;
		if (!(val & 0x04))
			dev->io_regs[0x11] &= ~0xc0;
		cmi8x38_update_irqs(dev);
		break;

	case 0x15:
		if (dev->type == CMEDIA_CMI8338)
			return;
		else
			val &= 0xf0;
		break;

	case 0x16:
		if (dev->type == CMEDIA_CMI8338)
			val &= 0xa0;
		break;

	case 0x17:
		if (dev->type == CMEDIA_CMI8338) {
			val &= 0xf3;

			/* Force IRQ if requested. Clearing this bit is undefined. */
			if (val & 0x10)
				pci_set_irq(dev->slot, PCI_INTA);
			else if ((dev->io_regs[0x17] & 0x10) && !(val & 0x10))
				pci_clear_irq(dev->slot, PCI_INTA);
		}
		break;

	case 0x18:
		if (dev->type == CMEDIA_CMI8338)
			val &= 0x0f;
		else
			val &= 0xdf;
		break;

	case 0x19:
		if (dev->type == CMEDIA_CMI8338)
			return;
		else
			val &= 0xe0;
		break;

	case 0x1a:
		val &= 0xfd;
		break;

	case 0x1b:
		if (dev->type == CMEDIA_CMI8338)
			val &= 0xf0;
		else
			val &= 0xd7;
		break;

	case 0x20:
		/* ??? */
		break;

	case 0x21:
		if (dev->type == CMEDIA_CMI8338)
			val &= 0xf7;
		else
			val &= 0x07;
		break;

	case 0x22:
		sb_ct1745_mixer_t *mixer = &dev->sb->mixer_sb16;
		switch (mixer->index) {
			case 0xf0:
				if (dev->type == CMEDIA_CMI8338)
					val &= 0xfe;
				dev->mixer_ext_regs[dev->sb->mixer_sb16.index & 0x0f] = val;
				break;

			case 0xf8 ... 0xff:
				if (dev->type == CMEDIA_CMI8338)
					dev->mixer_ext_regs[dev->sb->mixer_sb16.index & 0x0f] = val;
				/* fall-through */

			case 0xf1 ... 0xf7:
				break;

			default:
				sb_ct1745_mixer_write(1, val, dev->sb);

				/* Our clone mixer lacks the [3F:47] controls. */
				mixer->input_gain_L = 0;
				mixer->input_gain_R = 0;
				mixer->output_gain_L = (double) 1.0;
				mixer->output_gain_R = (double) 1.0;
				mixer->bass_l   = 8;
				mixer->bass_r   = 8;
				mixer->treble_l = 8;
				mixer->treble_r = 8;
				break;
		}
		return;

	case 0x23:
		sb_ct1745_mixer_write(0, val, dev->sb);
		return;

	case 0x24:
		if (dev->type == CMEDIA_CMI8338)
			val &= 0xcf;
		break;

	case 0x27:
		if (dev->type == CMEDIA_CMI8338)
			val &= 0x03;
		else
			val &= 0x27;
		break;

	case 0x40 ... 0x4f:
		if (dev->type == CMEDIA_CMI8338)
			break;
		else
			mpu401_write(addr, val, dev->sb->mpu);
		return;

	case 0x50 ... 0x5f:
		if (dev->type == CMEDIA_CMI8338)
			break;
		else
			opl3_write(addr, val, &dev->sb->opl);
		return;

	case 0x92:
		if (dev->type == CMEDIA_CMI8338)
			return;
		else
			val &= 0x1f;
		break;

	case 0x93:
		if (dev->type == CMEDIA_CMI8338)
			return;
		else
			val &= 0x10;
		break;

	case 0x25: case 0x26:
	case 0x70: case 0x71:
	case 0x80 ... 0x8f:
		break;

	default:
		return;
    }

    dev->io_regs[addr] = val;
}


static void
cmi8x38_remap(cmi8x38_t *dev, uint8_t io_msb, uint8_t enable)
{
    if (dev->io_base)
	io_removehandler(dev->io_base, 256, cmi8x38_read, NULL, NULL, cmi8x38_write, NULL, NULL, dev);

    if (enable & 0x01)
	dev->io_base = io_msb << 8;
    else
	dev->io_base = 0;
    cmi8x38_log("CMI8x38: remap(%04X)\n", dev->io_base);

    if (dev->io_base)
	io_sethandler(dev->io_base, 256, cmi8x38_read, NULL, NULL, cmi8x38_write, NULL, NULL, dev);
}


static uint8_t
cmi8x38_pci_read(int func, int addr, void *priv)
{
    cmi8x38_t *dev = (cmi8x38_t *) priv;
    uint8_t ret = 0xff;

    if (!func) {
	ret = dev->pci_regs[addr];
	cmi8x38_log("CMI8x38: pci_read(%02X) = %02X\n", addr, ret);
    }

    return ret;
}


static void
cmi8x38_pci_write(int func, int addr, uint8_t val, void *priv)
{
    cmi8x38_t *dev = (cmi8x38_t *) priv;

    if (func)
	return;

    cmi8x38_log("CMI8x38: pci_write(%02X, %02X)\n", addr, val);

    switch (addr) {
	case 0x04:
		val &= 0x05;
		cmi8x38_remap(dev, dev->pci_regs[0x11], val);
		break;

	case 0x05:
		val &= 0x01;
		break;

	case 0x11:
		cmi8x38_remap(dev, val, dev->pci_regs[0x04]);
		break;

	case 0x2c: case 0x2d: case 0x2e: case 0x2f:
		if (!(dev->io_regs[0x1a] & 0x01))
			return;
		break;

	case 0x40:
		if (dev->type == CMEDIA_CMI8338)
			val &= 0x0f;
		else
			return;
		break;

	case 0x0c: case 0x0d:
	case 0x3c:
		break;

	default:
		return;
    }

    dev->pci_regs[addr] = val;
}


static void
cmi8x38_update(cmi8x38_t *dev, cmi8x38_dma_t *dma)
{
    sb_ct1745_mixer_t *mixer = &dev->sb->mixer_sb16;
    int32_t l = (dma->out_l * mixer->voice_l) * mixer->master_l,
	    r = (dma->out_r * mixer->voice_r) * mixer->master_r;

    for (; dma->pos < sound_pos_global; dma->pos++) {
	dma->buffer[dma->pos*2]     = l;
	dma->buffer[dma->pos*2 + 1] = r;
    }
}


static void
cmi8x38_dma_process(void *priv)
{
    cmi8x38_dma_t *dma = (cmi8x38_dma_t *) priv;
    cmi8x38_t *dev = dma->dev;

    /* Stop if this DMA channel is not active. */
    uint8_t dma_bit = 0x01 << dma->id;
    if (!(dev->io_regs[0x02] & dma_bit)) {
	cmi8x38_log("CMI8x38: Stopping DMA %d due to inactive channel (%02X)\n", dma->id, dev->io_regs[0x02]);
	return;
    }

    /* Schedule next run. */
    timer_on_auto(&dma->dma_timer, 10.0);

    /* Process DMA if it's active, and the FIFO has room or is disabled. */
    uint8_t dma_status = dev->io_regs[0x00] >> dma->id;
    if (!(dma_status & 0x04) && (dma->always_run || ((dma->fifo_end - dma->fifo_pos) <= (sizeof(dma->fifo) - 4)))) {
	/* Start DMA if requested. */
	if (dma->restart) {
		/* Set up base address and counters.
		   I have no idea how sample_count_out is supposed to work,
		   nothing consumes it, so it's implemented as an assumption. */
		dma->restart = 0;
		dma->sample_ptr = *((uint32_t *) &dev->io_regs[dma->reg]);
		dma->frame_count_dma = dma->sample_count_out = *((uint16_t *) &dev->io_regs[dma->reg | 0x4]);
		dma->frame_count_fragment = *((uint16_t *) &dev->io_regs[dma->reg | 0x6]);

		cmi8x38_log("CMI8x38: Starting DMA %d at %08X\n", dma->id, dma->sample_ptr);
	}

	if (dma_status & 0x01) {
		/* Write channel: read data from FIFO. */
		mem_writel_phys(dma->sample_ptr, *((uint32_t *) &dma->fifo[dma->fifo_end & (sizeof(dma->fifo) - 1)]));
	} else {
		/* Read channel: write data to FIFO. */
		*((uint32_t *) &dma->fifo[dma->fifo_end & (sizeof(dma->fifo) - 1)]) = mem_readl_phys(dma->sample_ptr);
	}
	dma->fifo_end += 4;
	dma->sample_ptr += 4;

	/* Check if the fragment size was reached. */
	if (--dma->frame_count_fragment <= 0) {
		cmi8x38_log("CMI8x38: DMA %d fragment size reached at %04X frames left", dma->id, dma->frame_count_dma - 1);

		/* Reset fragment counter. */
		dma->frame_count_fragment = *((uint16_t *) &dev->io_regs[dma->reg | 0x6]);

		/* Fire interrupt if requested. */
		if (dev->io_regs[0x0e] & dma_bit) {
			cmi8x38_log(", firing interrupt\n");

			/* Set channel interrupt flag. */
			dev->io_regs[0x10] |= dma_bit;

			/* Fire interrupt. */
			cmi8x38_update_irqs(dev);
		} else {
			cmi8x38_log("\n");
		}
	}

	/* Check if the buffer's end was reached. */
	if (--dma->frame_count_dma <= 0) {
		cmi8x38_log("CMI8x38: DMA %d end reached, restarting\n", dma->id);

		/* Restart DMA on the next run. */
		dma->restart = 1;
	}
    }
}


static void
cmi8x38_poll(void *priv)
{
    cmi8x38_dma_t *dma = (cmi8x38_dma_t *) priv;
    cmi8x38_t *dev = dma->dev;

    /* Schedule next run if playback is enabled. */
    if (dev->io_regs[0x00] & (1 << dma->id))
	dma->playback_enabled = 0;
    else
	timer_advance_u64(&dma->poll_timer, dma->timer_latch);

    /* Update audio buffer. */
    cmi8x38_update(dev, dma);

    /* Feed next sample from the FIFO. */
    switch ((dev->io_regs[0x08] >> (dma->id << 1)) & 0x03) {
	case 0x00: /* Mono, 8-bit PCM */
		if ((dma->fifo_end - dma->fifo_pos) >= 1) {
			dma->out_l = dma->out_r = (dma->fifo[dma->fifo_pos++ & (sizeof(dma->fifo) - 1)] ^ 0x80) << 8;
			dma->sample_count_out--;
			return;
		}
		break;

	case 0x01: /* Stereo, 8-bit PCM */
		if ((dma->fifo_end - dma->fifo_pos) >= 2) {
			dma->out_l = (dma->fifo[dma->fifo_pos++ & (sizeof(dma->fifo) - 1)] ^ 0x80) << 8;
			dma->out_r = (dma->fifo[dma->fifo_pos++ & (sizeof(dma->fifo) - 1)] ^ 0x80) << 8;
			dma->sample_count_out -= 2;
			return;
		}
		break;

	case 0x02: /* Mono, 16-bit PCM */
		if ((dma->fifo_end - dma->fifo_pos) >= 2) {
			dma->out_l = dma->out_r = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
			dma->fifo_pos += 2;
			dma->sample_count_out -= 2;
			return;
		}
		break;

	case 0x03: /* Stereo, 16-bit PCM */
		if ((dma->fifo_end - dma->fifo_pos) >= 4) {
			dma->out_l = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
			dma->fifo_pos += 2;
			dma->out_r = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
			dma->fifo_pos += 2;
			dma->sample_count_out -= 4;
			return;
		}
		break;
    }

    /* Feed silence if the FIFO is empty. */
    dma->out_l = dma->out_r = 0;
}


static void
cmi8x38_get_buffer(int32_t *buffer, int len, void *priv)
{
    cmi8x38_t *dev = (cmi8x38_t *) priv;

    /* Update wave playback channels. */
    cmi8x38_update(dev, &dev->dma[0]);
    cmi8x38_update(dev, &dev->dma[1]);

    /* Apply wave mute. */
    if (!(dev->io_regs[0x24] & 0x40)) {
	/* Fill buffer. */
	for (int c = 0; c < len * 2; c++) {
		buffer[c] += dev->dma[0].buffer[c];
		buffer[c] += dev->dma[1].buffer[c];
	}
    }

    dev->dma[0].pos = dev->dma[1].pos = 0;
}


static void
cmi8x38_speed_changed(void *priv)
{
    cmi8x38_t *dev = (cmi8x38_t *) priv;
    double freq;
    uint8_t dsr = dev->io_regs[0x09], freqreg = dev->io_regs[0x05] >> 2;

    /* CMI8338 claims the frequency controls are for DAC (playback) and ADC (recording)
       respectively, while CMI8738 claims they're for channel 0 and channel 1. The Linux
       driver just assumes the latter definition, so that's what we're going to use here. */
    for (int i = 0; i < (sizeof(dev->dma) / sizeof(dev->dma[0])); i++) {
	/* More confusion. The Linux driver implies the sample rate doubling
	   bits take precedence over any configured sample rate. It also
	   supports 128K with both doubling bits set, which is undocumented. */
	switch (dsr & 0x03) {
		case 0x01: freq = 88200.0; break;
		case 0x02: freq = 96000.0; break;
		case 0x03: freq = 128000.0; break;
		default:   freq = freqs[freqreg & 0x07]; break;
	}
	dsr >>= 2;
	freqreg >>= 3;

	/* Set period. */
	dev->dma[i].timer_latch = (uint64_t) ((double) TIMER_USEC * (1000000.0 / freq));
    }
}


static void
cmi8x38_reset(void *priv)
{
    cmi8x38_t *dev = (cmi8x38_t *) priv;

    /* Reset PCI configuration registers. */
    memset(dev->pci_regs, 0, sizeof(dev->pci_regs));
    dev->pci_regs[0x00] = 0xf6; dev->pci_regs[0x01] = 0x13;
    dev->pci_regs[0x02] = dev->type; dev->pci_regs[0x03] = 0x01;
    dev->pci_regs[0x06] = (dev->type == CMEDIA_CMI8338) ? 0x80 : 0x10; dev->pci_regs[0x07] = 0x02;
    dev->pci_regs[0x08] = 0x10;
    dev->pci_regs[0x0a] = 0x01; dev->pci_regs[0x0b] = 0x04;
    dev->pci_regs[0x0d] = 0x20;
    dev->pci_regs[0x10] = 0x01;
    dev->pci_regs[0x2c] = 0xf6; dev->pci_regs[0x2d] = 0x13;
    if (dev->type == CMEDIA_CMI8338) {
	dev->pci_regs[0x2e] = 0xff; dev->pci_regs[0x2f] = 0xff;
    } else {
	dev->pci_regs[0x2e] = dev->type; dev->pci_regs[0x2f] = 0x01;
	dev->pci_regs[0x34] = 0x40;
    }
    dev->pci_regs[0x3d] = 0x01;
    dev->pci_regs[0x3e] = 0x02;
    dev->pci_regs[0x3f] = 0x18;

    /* Reset I/O space registers. */
    memset(dev->io_regs, 0, sizeof(dev->io_regs));
    if (dev->type == CMEDIA_CMI8738)
	dev->io_regs[0x0f] = 0x04; /* chip version 039 with 4-channel support */

    /* Reset DMA channels. */
    for (int i = 0; i < (sizeof(dev->dma) / sizeof(dev->dma[0])); i++) {
	dev->dma[i].playback_enabled = 0;

	dev->dma[i].fifo_pos = dev->dma[i].fifo_end = 0;
	memset(dev->dma[i].fifo, 0, sizeof(dev->dma[i].fifo));
    }

    /* Reset Sound Blaster 16 mixer. */
    sb_ct1745_mixer_reset(dev->sb);
}


static void *
cmi8x38_init(const device_t *info)
{
    cmi8x38_t *dev = malloc(sizeof(cmi8x38_t));
    memset(dev, 0, sizeof(cmi8x38_t));

    /* Set the chip type. */
    dev->type = info->local;
    cmi8x38_log("CMI8x38: init(%02X)\n", dev->type);

    /* Initialize Sound Blaster 16. */
    dev->sb = device_add_inst(&sb_16_compat_device, 1);
    dev->sb->opl_enabled = 1; /* let snd_sb.c handle the OPL3 */
    dev->sb->mixer_sb16.output_filter = 0; /* no output filtering */

    /* Initialize DMA channels. */
    for (int i = 0; i < (sizeof(dev->dma) / sizeof(dev->dma[0])); i++) {
	dev->dma[i].id = i;
	dev->dma[i].reg = 0x80 + (8 * i);
	dev->dma[i].dev = dev;

	timer_add(&dev->dma[i].dma_timer, cmi8x38_dma_process, &dev->dma[i], 0);
	timer_add(&dev->dma[i].poll_timer, cmi8x38_poll, &dev->dma[i], 0);
    }
    cmi8x38_speed_changed(dev);

    /* Initialize playback handler and CD audio filter. */
    sound_add_handler(cmi8x38_get_buffer, dev);
    sound_set_cd_audio_filter(sb16_awe32_filter_cd_audio, dev->sb);

    /* Initialize game port. */
    dev->gameport = gameport_add(&gameport_pnp_device);

    /* Add PCI card. */
    dev->slot = pci_add_card((info->local & 0x100) ? PCI_ADD_SOUND : PCI_ADD_NORMAL, cmi8x38_pci_read, cmi8x38_pci_write, dev);

    /* Perform initial reset. */
    cmi8x38_reset(dev);

    return dev;
}


static void
cmi8x38_close(void *priv)
{
    cmi8x38_t *dev = (cmi8x38_t *) priv;

    cmi8x38_log("CMI8x38: close()\n");

    free(dev);
}


const device_t cmi8338_device =
{
    "C-Media CMI8338",
    "cmi8338",
    DEVICE_PCI,
    CMEDIA_CMI8338,
    cmi8x38_init, cmi8x38_close, cmi8x38_reset,
    { NULL },
    cmi8x38_speed_changed,
    NULL,
    NULL
};

const device_t cmi8738_device =
{
    "C-Media CMI8738",
    "cmi8738",
    DEVICE_PCI,
    CMEDIA_CMI8738,
    cmi8x38_init, cmi8x38_close, cmi8x38_reset,
    { NULL },
    cmi8x38_speed_changed,
    NULL,
    NULL
};
