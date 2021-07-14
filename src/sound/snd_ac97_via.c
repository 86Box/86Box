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


typedef struct {
    uint8_t	id;
    struct _ac97_via_ *dev;

    uint64_t	entry;
    uint32_t	entry_ptr, sample_ptr, fifo_pos, fifo_end;
    int32_t	sample_count;
    uint8_t	fifo[32];

    pc_timer_t	timer;
} ac97_via_sgd_t;

typedef struct _ac97_via_ {
    uint16_t	audio_sgd_base, audio_codec_base, modem_sgd_base, modem_codec_base;
    uint8_t	sgd_regs[256], irq_stuck;
    int		slot, irq_pin;

    ac97_codec_t *codec[2][2];
    ac97_via_sgd_t sgd[6];

    pc_timer_t	timer_count;
    uint64_t	timer_latch, timer_fifo_latch;
    int16_t	out_l, out_r;
    double	cd_vol_l, cd_vol_r;
    int16_t	buffer[SOUNDBUFLEN * 2];
    int		pos;
} ac97_via_t;

#define ENABLE_AC97_VIA_LOG 1
#ifdef ENABLE_AC97_VIA_LOG
int ac97_via_do_log = ENABLE_AC97_VIA_LOG;
unsigned int ac97_via_lost_irqs = 0;

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


static void	ac97_via_sgd_process(void *priv);


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

    /* Flag each codec as ready if present. */
    for (uint8_t i = 0; i <= 1; i++) {
	if (dev->codec[modem][i])
		ret |= 0x01 << (i << 1);
    }

    ac97_via_log("AC97 VIA %d: read_status() = %02X\n", modem, ret);

    return ret;
}


static void
ac97_via_update_irqs(ac97_via_t *dev, uint8_t iflag_clear)
{
    /* Check interrupt flags on all SGDs. */
    for (uint8_t i = 0; i < (sizeof(dev->sgd) / sizeof(dev->sgd[0])); i++) {
    	if (dev->sgd_regs[i << 4] & (dev->sgd_regs[(i << 4) | 0x02] & 0x03)) {
    		ac97_via_log("AC97 VIA: Setting IRQ (sgd %d iflags %02X stuck %d)\n",
			     i, dev->sgd_regs[i << 4] & (dev->sgd_regs[(i << 4) | 0x02] & 0x03), dev->irq_stuck);

    		if (dev->irq_stuck && !iflag_clear) {
#ifdef ENABLE_AC97_VIA_LOG
			ac97_via_lost_irqs++;
#endif
			pci_clear_irq(dev->slot, dev->irq_pin);
		} else {
			pci_set_irq(dev->slot, dev->irq_pin);
		}
		dev->irq_stuck = !dev->irq_stuck;

		return;
    	}
    }

    /* No interrupt pending. */
    //ac97_via_log("AC97 VIA: Clearing IRQ\n");
    pci_clear_irq(dev->slot, dev->irq_pin);
    dev->irq_stuck = 0;
}


uint8_t
ac97_via_sgd_read(uint16_t addr, void *priv)
{
    ac97_via_t *dev = (ac97_via_t *) priv;
    uint8_t modem = (addr & 0xff00) == dev->modem_sgd_base;
    addr &= 0xff;
    uint8_t ret;

    if (!(addr & 0x80)) {
    	/* Process SGD channel registers. */
    	switch (addr & 0xf) {
    		case 0x4:
			ret = dev->sgd[addr >> 4].entry_ptr;
			break;

		case 0x5:
			ret = dev->sgd[addr >> 4].entry_ptr >> 8;
			break;

		case 0x6:
			ret = dev->sgd[addr >> 4].entry_ptr >> 16;
			break;

		case 0x7:
			ret = dev->sgd[addr >> 4].entry_ptr >> 24;
			break;

		case 0xc:
			ret = dev->sgd[addr >> 4].sample_count;
			break;

		case 0xd:
			ret = dev->sgd[addr >> 4].sample_count >> 8;
			break;

		case 0xe:
			ret = dev->sgd[addr >> 4].sample_count >> 16;
			break;

		default:
			ret = dev->sgd_regs[addr];
			break;
    	}
    } else {
    	/* Process regular registers. */
	switch (addr) {
		case 0x84:
			ret  = (dev->sgd_regs[0x00] & 0x01);
			ret |= (dev->sgd_regs[0x10] & 0x01) << 1;
			ret |= (dev->sgd_regs[0x20] & 0x01) << 2;

			ret |= (dev->sgd_regs[0x00] & 0x02) << 3;
			ret |= (dev->sgd_regs[0x10] & 0x02) << 4;
			ret |= (dev->sgd_regs[0x20] & 0x02) << 5;
			break;

		case 0x85:
			ret  = (dev->sgd_regs[0x00] & 0x04) >> 2;
			ret |= (dev->sgd_regs[0x10] & 0x04) >> 1;
			ret |= (dev->sgd_regs[0x20] & 0x04);

			ret |= (dev->sgd_regs[0x00] & 0x80) >> 3;
			ret |= (dev->sgd_regs[0x10] & 0x80) >> 2;
			ret |= (dev->sgd_regs[0x20] & 0x80) >> 1;
			break;

		case 0x86:
			ret  = (dev->sgd_regs[0x40] & 0x01);
			ret |= (dev->sgd_regs[0x50] & 0x01) << 1;

			ret |= (dev->sgd_regs[0x40] & 0x02) << 3;
			ret |= (dev->sgd_regs[0x50] & 0x02) << 4;
			break;

		case 0x87:
			ret  = (dev->sgd_regs[0x40] & 0x04) >> 2;
			ret |= (dev->sgd_regs[0x50] & 0x04) >> 1;

			ret |= (dev->sgd_regs[0x40] & 0x80) >> 3;
			ret |= (dev->sgd_regs[0x50] & 0x80) >> 2;
			break;

		default:
			ret = dev->sgd_regs[addr];
			break;
	}
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

    if (!(addr & 0x80)) {
    	/* Process SGD channel registers. */
	switch (addr & 0xf) {
		case 0x0:
			/* Clear RWC status bits. */
			for (i = 0x01; i <= 0x04; i <<= 1) {
				if (val & i)
					dev->sgd_regs[addr] &= ~i;
			}

			ac97_via_update_irqs(dev, 1);

			return;

		case 0x1:
			/* Start SGD if requested. */
			if (val & 0x80) {
				if (dev->sgd_regs[addr & 0xf0] & 0x80) {
					/* Queue SGD trigger. */
					dev->sgd_regs[addr & 0xf0] |= 0x08;
				} else {
					/* Start SGD immediately. */
					dev->sgd_regs[addr & 0xf0] |= 0x80;
					dev->sgd_regs[addr & 0xf0] &= ~0x44;

					/* Start at the specified entry pointer. */
					dev->sgd[addr >> 4].entry = 0;
					dev->sgd[addr >> 4].entry_ptr = (dev->sgd_regs[(addr & 0xf0) | 0x7] << 24) | (dev->sgd_regs[(addr & 0xf0) | 0x6] << 16) | (dev->sgd_regs[(addr & 0xf0) | 0x5] << 8) | (dev->sgd_regs[(addr & 0xf0) | 0x4] & 0xfe);

					/* Start the actual SGD process. */
					timer_advance_u64(&dev->sgd[addr >> 4].timer, 0);
				}
			}
			/* Stop SGD if requested. */
			if (val & 0x40)
				dev->sgd_regs[addr & 0xf0] &= ~0x88;

			val &= 0x04;

			/* (Un)pause SGD if requested. */
			if (val & 0x04)
				dev->sgd_regs[addr & 0xf0] |= 0x40;
			else
				dev->sgd_regs[addr & 0xf0] &= ~0x40;

			break;

		case 0x2:
			if (addr & 0x10)
				val &= 0xf3;
			break;

		case 0x3: case 0x8 ... 0xf:
			/* Read-only registers. */
			return;
	}
    } else {
    	/* Process regular registers. */
	switch (addr) {
		case 0x30 ... 0x3f:
		case 0x60 ... 0x7f:
		case 0x84 ... 0x87:
			/* Read-only registers. */
			return;

		case 0x82:
			/* Determine the selected codec. */
			i = !!(dev->sgd_regs[0x83] & 0x40);
			codec = dev->codec[modem][i];

			/* Read from or write to codec. */
			if (codec) {
				if (val & 0x80) {
					dev->sgd_regs[0x80] = ac97_codec_read(codec, val & 0x7e);
					dev->sgd_regs[0x81] = ac97_codec_read(codec, (val & 0x7e) | 1);
				} else {
					ac97_codec_write(codec, val & 0x7e, dev->sgd_regs[0x80]);
					ac97_codec_write(codec, (val & 0x7e) | 1, dev->sgd_regs[0x81]);
				}

				/* Flag data/status/index for this codec as valid. */
				dev->sgd_regs[0x83] |= 0x02 << (i * 2);
			} else if (val & 0x80) {
				/* Unknown behavior when reading from an absent codec. */
				dev->sgd_regs[0x80] = dev->sgd_regs[0x81] = 0xff;
			}
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
				/* Don't flag data/status/index as valid if the codec is absent. */
				if (dev->codec[modem][!!(val & 0x40)])
					val |= i;
				else
					val &= ~i;
#endif
			}
			break;
	}
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
    ac97_codec_t *codec = dev->codec[modem][addr >> 7];
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
    ac97_codec_t *codec = dev->codec[modem][addr >> 7];
    if (codec)
	ac97_codec_write(codec, addr & 0x7f, val);
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
ac97_via_sgd_process(void *priv)
{
    ac97_via_sgd_t *sgd = (ac97_via_sgd_t *) priv;
    ac97_via_t *dev = sgd->dev;

    /* Process SGD if active, unless this is Audio Read and there's no room in the FIFO. */
    if (((dev->sgd_regs[sgd->id] & 0xc4) == 0x80) && (sgd->id || ((sgd->fifo_end - sgd->fifo_pos) <= (sizeof(sgd->fifo) - 4)))) {
    	/* Move on to the next block if no entry is present. */
	if (!sgd->entry) {
		/* Start at first entry if no pointer is present. */
		if (!sgd->entry_ptr)
			sgd->entry_ptr = (dev->sgd_regs[sgd->id | 0x7] << 24) | (dev->sgd_regs[sgd->id | 0x6] << 16) | (dev->sgd_regs[sgd->id | 0x5] << 8) | (dev->sgd_regs[sgd->id | 0x4] & 0xfe);

		/* Read entry. */
		sgd->entry = ((uint64_t) mem_readl_phys(sgd->entry_ptr + 4) << 32ULL) | (uint64_t) mem_readl_phys(sgd->entry_ptr);
#ifdef ENABLE_AC97_VIA_LOG
		if (sgd->entry == 0xffffffffffffffffULL)
			fatal("AC97 VIA: Invalid SGD %d entry at %08X\n", sgd->id >> 4, sgd->entry_ptr);
#endif

		/* Set sample pointer and count. */
		sgd->sample_ptr = sgd->entry & 0xffffffff;
		sgd->sample_count = (sgd->entry >> 32) & 0xffffff;

		ac97_via_log("AC97 VIA: Starting SGD %d block at %08X entry %08X%08X (start %08X len %06X) lostirqs %d\n", sgd->id >> 4, sgd->entry_ptr,
			     mem_readl_phys(sgd->entry_ptr + 4), mem_readl_phys(sgd->entry_ptr), sgd->sample_ptr, sgd->sample_count, ac97_via_lost_irqs);

		/* Increment entry pointer now, as Linux expects it to be one block ahead. */
		sgd->entry_ptr += 8;
	}

        if (sgd->id & 0x10) {
        	/* Write channel: read data from FIFO. */
        	mem_writel_phys(sgd->sample_ptr, *((uint32_t *) &sgd->fifo[sgd->fifo_end & (sizeof(sgd->fifo) - 1)]));
        } else {
		/* Read channel: write data to FIFO. */
		*((uint32_t *) &sgd->fifo[sgd->fifo_end & (sizeof(sgd->fifo) - 1)]) = mem_readl_phys(sgd->sample_ptr);
	}
	sgd->fifo_end += 4;
	sgd->sample_ptr += 4;
	sgd->sample_count -= 4;

	/* Check if we've hit the end of this block. */
	if (sgd->sample_count <= 0) {
		ac97_via_log("AC97 VIA: Ending SGD %d block", sgd->id >> 4);

		if (sgd->entry & 0x2000000000000000ULL) {
			ac97_via_log(" with STOP");
			dev->sgd_regs[sgd->id] |= 0x04;
		}

		if (sgd->entry & 0x4000000000000000ULL) {
			ac97_via_log(" with FLAG");

			/* Raise FLAG while also pausing SGD. */
			dev->sgd_regs[sgd->id] |= 0x05;

#ifdef ENABLE_AC97_VIA_LOG
			if (dev->sgd_regs[sgd->id | 0x2] & 0x01)
				ac97_via_log(" interrupt");
#endif
		}

		if (sgd->entry & 0x8000000000000000ULL) {
			ac97_via_log(" with EOL");

			/* Raise EOL. */
			dev->sgd_regs[sgd->id] |= 0x02;

#ifdef ENABLE_AC97_VIA_LOG
			if (dev->sgd_regs[sgd->id | 0x2] & 0x02)
				ac97_via_log(" interrupt");
#endif

			/* Restart SGD if a trigger is queued or auto-start is enabled. */
			if ((dev->sgd_regs[sgd->id] & 0x08) || (dev->sgd_regs[sgd->id | 0x2] & 0x80)) {
				ac97_via_log(" restart");

				/* Un-queue trigger. */
				dev->sgd_regs[sgd->id] &= ~0x08;

				/* Go back to the starting block. */
				sgd->entry_ptr = 0;
			} else {
				ac97_via_log(" finish");

				/* Terminate SGD. */
				dev->sgd_regs[sgd->id] &= ~0x80;
			}
		}
		ac97_via_log("\n");

		/* Move on to a new block on the next run. */
		sgd->entry = sgd->sample_count = 0;
	}
    }

    ac97_via_update_irqs(dev, 0);

    /* Continue SGD processing if active or an interrupt is pending. */
    if (dev->sgd_regs[sgd->id] & (0x80 | (dev->sgd_regs[sgd->id | 0x02] & 0x03)))
	timer_advance_u64(&sgd->timer, dev->timer_fifo_latch);
}


static void
ac97_via_poll(void *priv)
{
    ac97_via_t *dev = (ac97_via_t *) priv;
    ac97_via_sgd_t *sgd = &dev->sgd[0]; /* Audio Read */

    timer_advance_u64(&dev->timer_count, dev->timer_latch);

    ac97_via_update(dev);

    dev->out_l = dev->out_r = 0;

    pclog("fifo_end - fifo_pos = %d\n", sgd->fifo_end - sgd->fifo_pos);
    switch (dev->sgd_regs[0x02] & 0x30) {
	case 0x00: /* Mono, 8-bit PCM */
		if ((sgd->fifo_end - sgd->fifo_pos) >= 1)
			dev->out_l = dev->out_r = (sgd->fifo[sgd->fifo_pos++ & (sizeof(sgd->fifo) - 1)] ^ 0x80) << 8;
		break;

	case 0x10: /* Stereo, 8-bit PCM */
		if ((sgd->fifo_end - sgd->fifo_pos) >= 2) {
			dev->out_l = (sgd->fifo[sgd->fifo_pos++ & (sizeof(sgd->fifo) - 1)] ^ 0x80) << 8;
			dev->out_r = (sgd->fifo[sgd->fifo_pos++ & (sizeof(sgd->fifo) - 1)] ^ 0x80) << 8;
		}
		break;

	case 0x20: /* Mono, 16-bit PCM */
		if ((sgd->fifo_end - sgd->fifo_pos) >= 2) {
			dev->out_l = dev->out_r = *((uint16_t *) &sgd->fifo[sgd->fifo_pos & (sizeof(sgd->fifo) - 1)]);
			sgd->fifo_pos += 2;
		}
		break;

	case 0x30: /* Stereo, 16-bit PCM */
		if ((sgd->fifo_end - sgd->fifo_pos) >= 4) {
			dev->out_l = *((uint16_t *) &sgd->fifo[sgd->fifo_pos & (sizeof(sgd->fifo) - 1)]);
			sgd->fifo_pos += 2;
			dev->out_r = *((uint16_t *) &sgd->fifo[sgd->fifo_pos & (sizeof(sgd->fifo) - 1)]);
			sgd->fifo_pos += 2;
		}
		break;
    }
}


static void
ac97_via_get_buffer(int32_t *buffer, int len, void *priv)
{
    ac97_via_t *dev = (ac97_via_t *) priv;

    ac97_via_update(dev);

    for (int c = 0; c < len * 2; c++)
	buffer[c] += dev->buffer[c];

    dev->pos = 0;
}


static void
ac97_via_speed_changed(void *priv)
{
    ac97_via_t *dev = (ac97_via_t *) priv;
    dev->timer_latch = (uint64_t) ((double) TIMER_USEC * (1000000.0 / 48000.0));
    dev->timer_fifo_latch = (uint64_t) ((double) TIMER_USEC * 10.0);
}


static void *
ac97_via_init(const device_t *info)
{
    ac97_via_t *dev = malloc(sizeof(ac97_via_t));
    memset(dev, 0, sizeof(ac97_via_t));

    ac97_via_log("AC97 VIA: init()\n");

    /* Set up codecs. */
    ac97_codec = &dev->codec[0][0];
    ac97_modem_codec = &dev->codec[1][0];
    ac97_codec_count = ac97_modem_codec_count = sizeof(dev->codec[0]) / sizeof(dev->codec[0][0]);

    /* Set up SGD channels. */
    for (uint8_t i = 0; i < (sizeof(dev->sgd) / sizeof(dev->sgd[0])); i++) {
    	dev->sgd[i].id = i << 4;
    	dev->sgd[i].dev = dev;

    	timer_add(&dev->sgd[i].timer, ac97_via_sgd_process, &dev->sgd[i], 0);
    }

    /* Set up playback poller. */
    timer_add(&dev->timer_count, ac97_via_poll, dev, 0);
    ac97_via_speed_changed(dev);
    timer_advance_u64(&dev->timer_count, dev->timer_latch);

    /* Set up playback handler. */
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
    "VIA VT82C686 Integrated AC97 Controller",
    DEVICE_PCI,
    0,
    ac97_via_init, ac97_via_close, NULL,
    { NULL },
    ac97_via_speed_changed,
    NULL,
    NULL
};
