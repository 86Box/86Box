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
    uint8_t	id, always_run;
    struct _ac97_via_ *dev;

    uint32_t	entry_ptr, sample_ptr, fifo_pos, fifo_end;
    int32_t	sample_count;
    uint8_t	entry_flags, fifo[32], restart;

    pc_timer_t	timer;
} ac97_via_sgd_t;

typedef struct _ac97_via_ {
    uint16_t	audio_sgd_base, audio_codec_base, modem_sgd_base, modem_codec_base;
    uint8_t	sgd_regs[256], pcm_enabled: 1, fm_enabled: 1, vsr_enabled: 1;
    struct {
	union {
		uint8_t regs_codec[2][128];
		uint8_t regs_linear[256];
	};
    } codec_shadow[2];
    int		slot, irq_pin;

    ac97_codec_t *codec[2][2];
    ac97_via_sgd_t sgd[6];

    pc_timer_t	timer_count, timer_count_fm;
    uint64_t	timer_latch, timer_latch_fm;
    int16_t	out_l, out_r, fm_out_l, fm_out_r;
    int		master_vol_l, master_vol_r, pcm_vol_l, pcm_vol_r, cd_vol_l, cd_vol_r;
    int32_t	buffer[SOUNDBUFLEN * 2], fm_buffer[SOUNDBUFLEN * 2];
    int		pos, fm_pos;
} ac97_via_t;


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


static void	ac97_via_sgd_process(void *priv);
static void	ac97_via_update_codec(ac97_via_t *dev);
static void	ac97_via_speed_changed(void *priv);


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


void
ac97_via_write_control(void *priv, uint8_t modem, uint8_t val)
{
    ac97_via_t *dev = (ac97_via_t *) priv;
    uint8_t i;

    ac97_via_log("AC97 VIA %d: write_control(%02X)\n", modem, val);

    /* Reset codecs if requested. */
    if (!(val & 0x40)) {
	for (i = 0; i <= 1; i++) {
		if (dev->codec[modem][i])
			ac97_codec_reset(dev->codec[modem][i]);
	}
    }

    if (!modem) {
	/* Set the variable sample rate flag. */
	dev->vsr_enabled = (val & 0xf8) == 0xc8;

	/* Start or stop PCM playback. */
	i = (val & 0xf4) == 0xc4;
	if (i && !dev->pcm_enabled)
		timer_advance_u64(&dev->timer_count, dev->timer_latch);
	dev->pcm_enabled = i;

	/* Start or stop FM playback. */
	i = (val & 0xf2) == 0xc2;
	if (i && !dev->fm_enabled)
		timer_advance_u64(&dev->timer_count_fm, dev->timer_latch);
	dev->fm_enabled = i;

	/* Update primary audio codec state. */
	if (dev->codec[0][0])
		ac97_via_update_codec(dev);
    }
}


static void
ac97_via_update_irqs(ac97_via_t *dev)
{
    /* Check interrupt flags in all SGDs. */
    for (uint8_t i = 0x00; i < ((sizeof(dev->sgd) / sizeof(dev->sgd[0])) << 4); i += 0x10) {
	/* Stop immediately if any flag is set. Doing it this way optimizes
	   rising edges for the playback SGD (0 - first to be checked). */
	if (dev->sgd_regs[i] & (dev->sgd_regs[i | 0x2] & 0x03)) {
		pci_set_irq(dev->slot, dev->irq_pin);
		return;
	}
    }

    pci_clear_irq(dev->slot, dev->irq_pin);
}


static void
ac97_via_update_codec(ac97_via_t *dev) {
    /* Get primary audio codec. */
    ac97_codec_t *codec = dev->codec[0][0];

    /* Update volumes according to codec registers. */
    ac97_codec_getattn(codec, 0x02, &dev->master_vol_l, &dev->master_vol_r);
    ac97_codec_getattn(codec, 0x18, &dev->pcm_vol_l, &dev->pcm_vol_r);
    ac97_codec_getattn(codec, 0x12, &dev->cd_vol_l, &dev->cd_vol_r);

    /* Update sample rate according to codec registers and the variable sample rate flag. */
    ac97_via_speed_changed(dev);
}


uint8_t
ac97_via_sgd_read(uint16_t addr, void *priv)
{
    ac97_via_t *dev = (ac97_via_t *) priv;
#ifdef ENABLE_AC97_VIA_LOG
    uint8_t modem = (addr & 0xff00) == dev->modem_sgd_base;
#endif
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
			dev->sgd_regs[addr] &= ~(val & 0x07);

			/* Update status interrupts. */
			ac97_via_update_irqs(dev);

			return;

		case 0x1:
			/* Start SGD if requested. */
			if (val & 0x80) {
				if (dev->sgd_regs[addr & 0xf0] & 0x80) {
					/* Queue SGD trigger if already running. */
					dev->sgd_regs[addr & 0xf0] |= 0x08;
				} else {
					/* Start SGD immediately. */
					dev->sgd_regs[addr & 0xf0] |= 0x80;
					dev->sgd_regs[addr & 0xf0] &= ~0x44;

					/* Start at the specified entry pointer. */
					dev->sgd[addr >> 4].sample_ptr = 0;
					dev->sgd[addr >> 4].entry_ptr = *((uint32_t *) &dev->sgd_regs[(addr & 0xf0) | 0x4]) & 0xfffffffe;
					dev->sgd[addr >> 4].restart = 1;

					/* Start the actual SGD process. */
					ac97_via_sgd_process(&dev->sgd[addr >> 4]);
				}
			}
			/* Stop SGD if requested. */
			if (val & 0x40)
				dev->sgd_regs[addr & 0xf0] &= ~0x88;

			val &= 0x08;

			/* (Un)pause SGD if requested. */
			if (val & 0x08)
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

			/* Keep value in register if this codec is not present. */
			if (codec) {
				/* Read from or write to codec. */
				if (val & 0x80) {
					if (val & 1) { /* return 0x00 on unaligned reads */
						dev->sgd_regs[0x80] = dev->sgd_regs[0x81] = 0x00;
					} else {
						dev->sgd_regs[0x80] = dev->codec_shadow[modem].regs_codec[i][val]     = ac97_codec_read(codec, val);
						dev->sgd_regs[0x81] = dev->codec_shadow[modem].regs_codec[i][val | 1] = ac97_codec_read(codec, val | 1);
					}

					/* Flag data/status/index for this codec as valid. */
					if (val & 0x80)
						dev->sgd_regs[0x83] |= 0x02 << (i << 1);
				} else if (!(val & 1)) { /* do nothing on unaligned writes */
					ac97_codec_write(codec, val,     dev->codec_shadow[modem].regs_codec[i][val]     = dev->sgd_regs[0x80]);
					ac97_codec_write(codec, val | 1, dev->codec_shadow[modem].regs_codec[i][val | 1] = dev->sgd_regs[0x81]);

					/* Update primary audio codec state if that codec was written to. */
					if (!modem && !i)
						ac97_via_update_codec(dev);
				}
			}

			break;

		case 0x83:
			/* Clear RWC status bits. */
#if 0 /* race condition with Linux accessing a register and clearing status bits on the same dword write */
			val = (dev->sgd_regs[addr] & ~(val & 0x0a)) | (val & 0xc0);
#else
			val = dev->sgd_regs[addr] | (val & 0xc0);
#endif
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

    ret = dev->codec_shadow[modem].regs_linear[addr];

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

    /* Unknown behavior, maybe it does write to the shadow registers? */
    dev->codec_shadow[modem].regs_linear[addr] = val;
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
    int32_t l = (((dev->out_l * dev->pcm_vol_l) >> 15) * dev->master_vol_l) >> 15,
	    r = (((dev->out_r * dev->pcm_vol_r) >> 15) * dev->master_vol_r) >> 15;

    if (l < -32768)
	l = -32768;
    else if (l > 32767)
	l = 32767;
    if (r < -32768)
	r = -32768;
    else if (r > 32767)
	r = 32767;

    for (; dev->pos < sound_pos_global; dev->pos++) {
	dev->buffer[dev->pos*2]     = l;
	dev->buffer[dev->pos*2 + 1] = r;
    }
}


static void
ac97_via_update_fm(ac97_via_t *dev)
{
    for (; dev->fm_pos < sound_pos_global; dev->fm_pos++) {
	dev->fm_buffer[dev->fm_pos*2]     = dev->fm_out_l;
	dev->fm_buffer[dev->fm_pos*2 + 1] = dev->fm_out_r;
    }
}


static void
ac97_via_sgd_process(void *priv)
{
    ac97_via_sgd_t *sgd = (ac97_via_sgd_t *) priv;
    ac97_via_t *dev = sgd->dev;

    /* Stop if this SGD is not active. */
    uint8_t sgd_status = dev->sgd_regs[sgd->id] & 0xc4;
    if (!(sgd_status & 0x80))
	return;

    /* Schedule next run. */
    timer_on_auto(&sgd->timer, 10.0);

    /* Process SGD if it's active, and the FIFO has room or is disabled. */
    if ((sgd_status == 0x80) && (sgd->always_run || ((sgd->fifo_end - sgd->fifo_pos) <= (sizeof(sgd->fifo) - 4)))) {
	/* Move on to the next block if no entry is present. */
	if (sgd->restart) {
		sgd->restart = 0;

		/* Start at first entry if no pointer is present. */
		if (!sgd->entry_ptr)
			sgd->entry_ptr = *((uint32_t *) &dev->sgd_regs[sgd->id | 0x4]) & 0xfffffffe;

		/* Read entry. */
		sgd->sample_ptr = mem_readl_phys(sgd->entry_ptr);
		sgd->entry_ptr += 4;
		sgd->sample_count = mem_readl_phys(sgd->entry_ptr);
		sgd->entry_ptr += 4;
#ifdef ENABLE_AC97_VIA_LOG
		if (((sgd->sample_ptr == 0xffffffff) && (sgd->sample_count == 0xffffffff)) ||
		    ((sgd->sample_ptr == 0x00000000) && (sgd->sample_count == 0x00000000)))
			fatal("AC97 VIA: Invalid SGD %d entry %08X%08X at %08X\n", sgd->id >> 4,
			      sgd->sample_ptr, sgd->sample_count, sgd->entry_ptr - 8);
#endif

		/* Extract flags from the most significant byte. */
		sgd->entry_flags = sgd->sample_count >> 24;
		sgd->sample_count &= 0xffffff;

		ac97_via_log("AC97 VIA: Starting SGD %d block at %08X start %08X len %06X flags %02X\n", sgd->id >> 4,
			     sgd->entry_ptr - 8, sgd->sample_ptr, sgd->sample_count, sgd->entry_flags);
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

		if (sgd->entry_flags & 0x20) {
			ac97_via_log(" with STOP");

			/* Raise STOP to pause SGD. */
			dev->sgd_regs[sgd->id] |= 0x04;
		}

		if (sgd->entry_flags & 0x40) {
			ac97_via_log(" with FLAG");

			/* Raise FLAG and STOP. */
			dev->sgd_regs[sgd->id] |= 0x05;

#ifdef ENABLE_AC97_VIA_LOG
			if (dev->sgd_regs[sgd->id | 0x2] & 0x01)
				ac97_via_log(" interrupt");
#endif
		}

		if (sgd->entry_flags & 0x80) {
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
				sgd->entry_ptr = 0; /* ugly, but Windows XP plays too fast if the pointer is reloaded now */
			} else {
				ac97_via_log(" finish");

				/* Terminate SGD. */
				dev->sgd_regs[sgd->id] &= ~0x80;
			}
		}
		ac97_via_log("\n");

		/* Fire any requested status interrupts. */
		ac97_via_update_irqs(dev);

		/* Move on to a new block on the next run. */
		sgd->restart = 1;
	}
    }
}


static void
ac97_via_poll(void *priv)
{
    ac97_via_t *dev = (ac97_via_t *) priv;
    ac97_via_sgd_t *sgd = &dev->sgd[0]; /* Audio Read */

    /* Schedule next run if PCM playback is enabled. */
    if (dev->pcm_enabled)
	timer_advance_u64(&dev->timer_count, dev->timer_latch);

    /* Update audio buffer. */
    ac97_via_update(dev);

    /* Feed next sample from the FIFO. */
    switch (dev->sgd_regs[0x02] & 0x30) {
	case 0x00: /* Mono, 8-bit PCM */
		if ((sgd->fifo_end - sgd->fifo_pos) >= 1) {
			dev->out_l = dev->out_r = (sgd->fifo[sgd->fifo_pos++ & (sizeof(sgd->fifo) - 1)] ^ 0x80) << 8;
			return;
		}
		break;

	case 0x10: /* Stereo, 8-bit PCM */
		if ((sgd->fifo_end - sgd->fifo_pos) >= 2) {
			dev->out_l = (sgd->fifo[sgd->fifo_pos++ & (sizeof(sgd->fifo) - 1)] ^ 0x80) << 8;
			dev->out_r = (sgd->fifo[sgd->fifo_pos++ & (sizeof(sgd->fifo) - 1)] ^ 0x80) << 8;
			return;
		}
		break;

	case 0x20: /* Mono, 16-bit PCM */
		if ((sgd->fifo_end - sgd->fifo_pos) >= 2) {
			dev->out_l = dev->out_r = *((uint16_t *) &sgd->fifo[sgd->fifo_pos & (sizeof(sgd->fifo) - 1)]);
			sgd->fifo_pos += 2;
			return;
		}
		break;

	case 0x30: /* Stereo, 16-bit PCM */
		if ((sgd->fifo_end - sgd->fifo_pos) >= 4) {
			dev->out_l = *((uint16_t *) &sgd->fifo[sgd->fifo_pos & (sizeof(sgd->fifo) - 1)]);
			sgd->fifo_pos += 2;
			dev->out_r = *((uint16_t *) &sgd->fifo[sgd->fifo_pos & (sizeof(sgd->fifo) - 1)]);
			sgd->fifo_pos += 2;
			return;
		}
		break;
    }

    /* Feed silence if the FIFO is empty. */
    dev->out_l = dev->out_r = 0;
}


static void
ac97_via_poll_fm(void *priv)
{
    ac97_via_t *dev = (ac97_via_t *) priv;
    ac97_via_sgd_t *sgd = &dev->sgd[2]; /* FM Read */

    /* Schedule next run if FM playback is enabled. */
    if (dev->fm_enabled)
	timer_advance_u64(&dev->timer_count_fm, dev->timer_latch_fm);

    /* Update FM audio buffer. */
    ac97_via_update_fm(dev);

    /* Feed next sample from the FIFO.
       The data format is not documented, but it probes as 16-bit stereo at 24 KHz. */
    if ((sgd->fifo_end - sgd->fifo_pos) >= 4) {
	dev->out_l = *((uint16_t *) &sgd->fifo[sgd->fifo_pos & (sizeof(sgd->fifo) - 1)]);
	sgd->fifo_pos += 2;
	dev->out_r = *((uint16_t *) &sgd->fifo[sgd->fifo_pos & (sizeof(sgd->fifo) - 1)]);
	sgd->fifo_pos += 2;
	return;
    }

    /* Feed silence if the FIFO is empty. */
    dev->fm_out_l = dev->fm_out_r = 0;
}


static void
ac97_via_get_buffer(int32_t *buffer, int len, void *priv)
{
    ac97_via_t *dev = (ac97_via_t *) priv;

    ac97_via_update(dev);
    ac97_via_update_fm(dev);

    for (int c = 0; c < len * 2; c++) {
	buffer[c] += dev->buffer[c] / 2;
	buffer[c] += dev->fm_buffer[c] / 2;
    }

    dev->pos = dev->fm_pos = 0;
}


static void
via_ac97_filter_cd_audio(int channel, double *buffer, void *priv)
{
    ac97_via_t *dev = (ac97_via_t *) priv;
    double c, volume = channel ? dev->cd_vol_r : dev->cd_vol_l;

    c = ((*buffer) * volume) / 65536.0;
    *buffer = c;
}


static void
ac97_via_speed_changed(void *priv)
{
    ac97_via_t *dev = (ac97_via_t *) priv;
    double freq;

    /* Get variable sample rate if enabled. */
    if (dev->vsr_enabled && dev->codec[0][0])
	freq = ac97_codec_getrate(dev->codec[0][0], 0x2c);
    else
	freq = 48000.0;

    dev->timer_latch = (uint64_t) ((double) TIMER_USEC * (1000000.0 / freq));
    dev->timer_latch_fm = (uint64_t) ((double) TIMER_USEC * (1000000.0 / 24000.0));
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
    ac97_codec_id = ac97_modem_codec_id = 0;

    /* Set up SGD channels. */
    for (uint8_t i = 0; i < (sizeof(dev->sgd) / sizeof(dev->sgd[0])); i++) {
	dev->sgd[i].id = i << 4;
	dev->sgd[i].dev = dev;

	/* Disable the FIFO on SGDs we don't care about. */
	if ((i != 0) && (i != 2))
		dev->sgd[i].always_run = 1;

	timer_add(&dev->sgd[i].timer, ac97_via_sgd_process, &dev->sgd[i], 0);
    }

    /* Set up playback pollers. */
    timer_add(&dev->timer_count, ac97_via_poll, dev, 0);
    timer_add(&dev->timer_count_fm, ac97_via_poll_fm, dev, 0);
    ac97_via_speed_changed(dev);

    /* Set up playback handler. */
    sound_add_handler(ac97_via_get_buffer, dev);

    /* Set up CD audio filter. */
    sound_set_cd_audio_filter(via_ac97_filter_cd_audio, dev);

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
