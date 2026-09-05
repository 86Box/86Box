/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the ESS Technology ES1938S Solo-1 PCI audio controller.
 *
 * Authors: mw308
 *
 *          Copyright 2026 mw308
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/pci.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/sound.h>
#include <86box/timer.h>
#include <86box/snd_sb.h>
#include <86box/plat_unused.h>

/* Solo-1-only hooks for the nested ESS legacy compatibility core. */
void   ess_solo1_legacy_reset(void *priv);
double ess_solo1_legacy_audio2_gain(void *priv, int channel);

typedef struct solo1_t {
    uint8_t pci_regs[256];
    uint8_t pci_slot;
    uint8_t onboard;

    void   *legacy;

    uint16_t io_base_mapped;
    uint16_t sb_base_mapped;
    uint16_t mpu_base_mapped;
    uint16_t gp_base_mapped;
    uint16_t ddma_base_mapped;
    uint8_t  io_regs[0x40];
    uint8_t  sb_regs[0x10];
    uint8_t  sb_reset_asserted;
    uint8_t  sb_reset_response;
    uint8_t  sb_read_fifo[4];
    uint8_t  sb_read_fifo_r;
    uint8_t  sb_read_fifo_w;
    uint8_t  sb_read_fifo_count;
    uint8_t  ess_ext_enabled;
    uint8_t  ess_regs[0x30];          /* A0h-CFh */
    uint8_t  dsp_pending_cmd;
    uint8_t  dsp_pending_bytes;
    uint8_t  dsp_pending_data[2];
    uint8_t  mpu_regs[0x04];
    uint8_t  mpu_ack_pending;
    uint8_t  gp_regs[0x04];
    uint8_t  ddma_regs[0x10];

    /* Real indexed ESS mixer state. */
    uint8_t  mixer_index;
    uint8_t  mixer_regs[256];

    /* Audio 2 native PCI bus-master playback state. */
    uint32_t a2_base_addr;
    uint32_t a2_cur_addr;
    uint16_t a2_base_count;
    uint16_t a2_cur_count;
    uint32_t a2_rate_accum;
    uint32_t a2_irq_bytes_left;
    int16_t  a2_last_l;
    int16_t  a2_last_r;
    uint8_t  a2_irq_pending;
    uint8_t  irq_state;

    /* Audio 2 bus-master read cache. */
    uint8_t  a2_dma_cache[128];
    uint32_t a2_dma_cache_addr;
    uint16_t a2_dma_cache_pos;
    uint16_t a2_dma_cache_len;

} solo1_t;

static uint8_t
solo1_pci_read(int func, int addr, UNUSED(int len), void *priv)
{
    solo1_t *dev = (solo1_t *) priv;

    if (func || addr < 0 || addr > 0xff)
        return 0xff;

    return dev->pci_regs[addr];
}


static int
solo1_decode_irq(unsigned sel)
{
    switch (sel & 7) {
        case 0: return 5;
        case 1: return 7;
        case 2: return 9;
        case 3: return 10;
        case 4: return 11;
        case 5: return 12;
        case 6: return 13;
        case 7: return 14;
        default: return 5;
    }
}

static int
solo1_decode_dma(unsigned sel)
{
    switch (sel & 3) {
        case 0: return 0;
        case 1: return 1;
        case 3: return 3;
        default: return 1; /* 10b is reserved. */
    }
}

static uint16_t
solo1_decode_mpu_base(const solo1_t *dev)
{
    switch ((dev->pci_regs[0x50] >> 3) & 3) {
        case 0: return 0x330;
        case 1: return 0x300;
        case 2: return 0x320;
        case 3: return 0x340;
        default: return 0x330;
    }
}


static uint16_t
solo1_bar_base(const solo1_t *dev, int bar, uint16_t mask)
{
    return (uint16_t) (((uint16_t) dev->pci_regs[bar + 1] << 8) |
                       (dev->pci_regs[bar] & mask));
}

static uint32_t
solo1_a2_sample_rate(const solo1_t *dev)
{
    const uint8_t r = dev->mixer_regs[0x70];
    const uint32_t clock = (r & 0x80) ? 768000U : 793800U;
    uint32_t divisor = 128U - (uint32_t) (r & 0x7f);

    if (!divisor)
        divisor = 1;

    return clock / divisor;
}

static uint32_t
solo1_a2_transfer_reload_bytes(const solo1_t *dev)
{
    uint16_t raw = (uint16_t) dev->mixer_regs[0x74] |
                   ((uint16_t) dev->mixer_regs[0x76] << 8);
    uint32_t n = (uint16_t) (0x10000U - raw);

    if (!n)
        n = 0x10000U;

    return n;
}

static void
solo1_update_irq(solo1_t *dev)
{
    const int a2_enabled = !!(dev->mixer_regs[0x7a] & 0x40);
    const int io_enabled = !!(dev->io_regs[0x07] & 0x20);
    const int active = dev->a2_irq_pending && a2_enabled && io_enabled;

    if (active)
        pci_set_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
    else
        pci_clear_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
}

static void solo1_a2_cache_invalidate(solo1_t *dev);

static void
solo1_a2_reload_dma(solo1_t *dev)
{
    dev->a2_cur_addr = dev->a2_base_addr;
    dev->a2_cur_count = dev->a2_base_count;
    dev->a2_irq_bytes_left = solo1_a2_transfer_reload_bytes(dev);
    solo1_a2_cache_invalidate(dev);
}

static void
solo1_a2_cache_invalidate(solo1_t *dev)
{
    dev->a2_dma_cache_addr = 0;
    dev->a2_dma_cache_pos = 0;
    dev->a2_dma_cache_len = 0;
}

static void
solo1_a2_cache_fill(solo1_t *dev)
{
    uint32_t n;

    solo1_a2_cache_invalidate(dev);

    if (!dev->a2_cur_count)
        return;

    n = dev->a2_cur_count;
    if (n > sizeof(dev->a2_dma_cache))
        n = sizeof(dev->a2_dma_cache);

    dev->a2_dma_cache_addr = dev->a2_cur_addr;

	/* Windows Solo-1 playback buffers go into normal RAM, so use existing backing */
    if (mem_addr_is_ram(dev->a2_cur_addr) &&
        mem_addr_is_ram(dev->a2_cur_addr + n - 1) &&
        ((dev->a2_cur_addr & rammask) + n <= (uint64_t) rammask + 1)) {
        memcpy(dev->a2_dma_cache, ram + (dev->a2_cur_addr & rammask), n);
    } else {
        uint32_t i = 0;

        while ((i + 4) <= n) {
            uint32_t v = mem_readl_phys(dev->a2_cur_addr + i);
            dev->a2_dma_cache[i + 0] = (uint8_t) v;
            dev->a2_dma_cache[i + 1] = (uint8_t) (v >> 8);
            dev->a2_dma_cache[i + 2] = (uint8_t) (v >> 16);
            dev->a2_dma_cache[i + 3] = (uint8_t) (v >> 24);
            i += 4;
        }

        while (i < n) {
            dev->a2_dma_cache[i] = mem_readb_phys(dev->a2_cur_addr + i);
            i++;
        }
    }

    dev->a2_dma_cache_len = (uint16_t) n;
}

static uint8_t
solo1_a2_read_byte(solo1_t *dev)
{
    uint8_t v;

    if ((dev->a2_dma_cache_pos >= dev->a2_dma_cache_len) ||
        (dev->a2_dma_cache_addr + dev->a2_dma_cache_pos != dev->a2_cur_addr)) {
        solo1_a2_cache_fill(dev);
    }

    if (dev->a2_dma_cache_pos < dev->a2_dma_cache_len)
        v = dev->a2_dma_cache[dev->a2_dma_cache_pos++];
    else
        v = mem_readb_phys(dev->a2_cur_addr);

    dev->a2_cur_addr++;

    if (dev->a2_cur_count > 1) {
        dev->a2_cur_count--;
    } else {
        if (dev->io_regs[0x06] & 0x08) {
            dev->a2_cur_addr = dev->a2_base_addr;
            dev->a2_cur_count = dev->a2_base_count;
            solo1_a2_cache_invalidate(dev);
        } else {
            dev->a2_cur_count = 0;
            dev->io_regs[0x06] &= ~0x02;
            solo1_a2_cache_invalidate(dev);
        }
    }

    if (dev->a2_irq_bytes_left > 1) {
        dev->a2_irq_bytes_left--;
    } else {
        dev->a2_irq_bytes_left = solo1_a2_transfer_reload_bytes(dev);
        dev->a2_irq_pending = 1;
        dev->mixer_regs[0x7a] |= 0x80;
        solo1_update_irq(dev);

        if (!(dev->mixer_regs[0x78] & 0x10))
            dev->mixer_regs[0x78] &= ~0x02;
    }

    return v;
}

static void
solo1_a2_fetch_frame(solo1_t *dev)
{
    const uint8_t fmt = dev->mixer_regs[0x7a];
    const int is_signed = !!(fmt & 0x04);
    const int stereo = !!(fmt & 0x02);
    const int sixteen = !!(fmt & 0x01);

    if (sixteen) {
        uint16_t lo, hi;
        int16_t l, r;

        lo = solo1_a2_read_byte(dev);
        hi = solo1_a2_read_byte(dev);
        l = (int16_t) (lo | (hi << 8));
        if (!is_signed)
            l = (int16_t) ((uint16_t) l ^ 0x8000U);

        if (stereo) {
            lo = solo1_a2_read_byte(dev);
            hi = solo1_a2_read_byte(dev);
            r = (int16_t) (lo | (hi << 8));
            if (!is_signed)
                r = (int16_t) ((uint16_t) r ^ 0x8000U);
        } else {
            r = l;
        }

        dev->a2_last_l = l;
        dev->a2_last_r = r;
    } else {
        uint8_t b = solo1_a2_read_byte(dev);
        int16_t l = is_signed ? ((int16_t) (int8_t) b << 8)
                              : ((int16_t) ((uint8_t) (b ^ 0x80)) << 8);
        int16_t r = l;

        if (stereo) {
            b = solo1_a2_read_byte(dev);
            r = is_signed ? ((int16_t) (int8_t) b << 8)
                          : ((int16_t) ((uint8_t) (b ^ 0x80)) << 8);
        }

        dev->a2_last_l = l;
        dev->a2_last_r = r;
    }
}

static void
solo1_mixer_reset_regs(solo1_t *dev)
{
    memset(dev->mixer_regs, 0, sizeof(dev->mixer_regs));

    /* ES1938S mixer reset defaults from the Solo-1 data sheet. */
    dev->mixer_regs[0x14] = 0x88;
    dev->mixer_regs[0x32] = 0x88;
    dev->mixer_regs[0x36] = 0x88;
    dev->mixer_regs[0x38] = 0x00;
    dev->mixer_regs[0x3c] = 0x04;
    dev->mixer_regs[0x60] = 0x36;
    dev->mixer_regs[0x62] = 0x36;
    dev->mixer_regs[0x64] = 0x00;
    dev->mixer_regs[0x7c] = 0x00;
    dev->mixer_regs[0x7d] = 0x08;
    dev->mixer_index      = 0;
}

static void
solo1_get_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    solo1_t *dev = (solo1_t *) priv;
    uint32_t src_rate = solo1_a2_sample_rate(dev);
    const double gain_l = ess_solo1_legacy_audio2_gain(dev->legacy, 0);
    const double gain_r = ess_solo1_legacy_audio2_gain(dev->legacy, 1);

    if (!src_rate)
        src_rate = 8000;

    for (uint16_t i = 0; i < len; i++) {
        const int active = (dev->io_regs[0x06] & 0x02) &&
                           ((dev->mixer_regs[0x78] & 0x03) == 0x03) &&
                           dev->a2_cur_count;

        if (active) {
            dev->a2_rate_accum += src_rate;

            while (dev->a2_rate_accum >= (uint32_t) sound_sample_rate) {
                dev->a2_rate_accum -= (uint32_t) sound_sample_rate;
                solo1_a2_fetch_frame(dev);
            }

            /* Apply the Solo-1 Audio 2 input curve and actual 6-bit master
               volume from the synchronized legacy mixer state. */
            buffer[i * 2]     += (int32_t) ((double) dev->a2_last_l * gain_l);
            buffer[i * 2 + 1] += (int32_t) ((double) dev->a2_last_r * gain_r);
        }
    }
}

static uint8_t
solo1_native_io_read(uint16_t addr, void *priv)
{
    solo1_t *dev = (solo1_t *) priv;
    uint8_t off = (uint8_t) (addr - dev->io_base_mapped);
    uint8_t ret = 0xff;

    switch (off) {
        case 0x00: ret = (uint8_t) (dev->a2_cur_addr); break;
        case 0x01: ret = (uint8_t) (dev->a2_cur_addr >> 8); break;
        case 0x02: ret = (uint8_t) (dev->a2_cur_addr >> 16); break;
        case 0x03: ret = (uint8_t) (dev->a2_cur_addr >> 24); break;
        case 0x04: ret = (uint8_t) (dev->a2_cur_count); break;
        case 0x05: ret = (uint8_t) (dev->a2_cur_count >> 8); break;
        case 0x06: ret = dev->io_regs[0x06]; break;
        case 0x07:
            ret = dev->io_regs[0x07] & 0xf0;
            if (dev->a2_irq_pending)
                ret |= 0x20;
            break;
        default:
            if (off < sizeof(dev->io_regs))
                ret = dev->io_regs[off];
            break;
    }

    return ret;
}

static void
solo1_native_io_write(uint16_t addr, uint8_t val, void *priv)
{
    solo1_t *dev = (solo1_t *) priv;
    uint8_t off = (uint8_t) (addr - dev->io_base_mapped);

    switch (off) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03: {
            const unsigned shift = off * 8;
            dev->a2_base_addr = (dev->a2_base_addr & ~(0xffU << shift)) |
                                ((uint32_t) val << shift);
            dev->a2_base_addr &= ~0x0fU;
            if (!(dev->io_regs[0x06] & 0x02)) {
                dev->a2_cur_addr = dev->a2_base_addr;
                solo1_a2_cache_invalidate(dev);
            }
            break;
        }

        case 0x04:
            dev->a2_base_count = (dev->a2_base_count & 0xff00U) | val;
            dev->a2_base_count &= 0xfff0U;
            if (!(dev->io_regs[0x06] & 0x02)) {
                dev->a2_cur_count = dev->a2_base_count;
                solo1_a2_cache_invalidate(dev);
            }
            break;

        case 0x05:
            dev->a2_base_count = (dev->a2_base_count & 0x00ffU) |
                                 ((uint16_t) val << 8);
            dev->a2_base_count &= 0xfff0U;
            if (!(dev->io_regs[0x06] & 0x02)) {
                dev->a2_cur_count = dev->a2_base_count;
                solo1_a2_cache_invalidate(dev);
            }
            break;

        case 0x06: {
            const uint8_t old = dev->io_regs[0x06];
            dev->io_regs[0x06] = val & 0x0f;

            if (!(old & 0x02) && (dev->io_regs[0x06] & 0x02)) {
                solo1_a2_reload_dma(dev);
                dev->a2_rate_accum = 0;
            }
            break;
        }

        case 0x07:
            dev->io_regs[0x07] = val & 0xf0;
            solo1_update_irq(dev);
            break;

        default:
            if (off < sizeof(dev->io_regs))
                dev->io_regs[off] = val;
            break;
    }
}

static void
solo1_sb_fifo_clear(solo1_t *dev)
{
    dev->sb_read_fifo_r = 0;
    dev->sb_read_fifo_w = 0;
    dev->sb_read_fifo_count = 0;
}

static void
solo1_sb_fifo_put(solo1_t *dev, uint8_t val)
{
    if (dev->sb_read_fifo_count >= sizeof(dev->sb_read_fifo))
        return;

    dev->sb_read_fifo[dev->sb_read_fifo_w] = val;
    dev->sb_read_fifo_w = (dev->sb_read_fifo_w + 1) % sizeof(dev->sb_read_fifo);
    dev->sb_read_fifo_count++;
}

static uint8_t
solo1_sb_fifo_get(solo1_t *dev)
{
    uint8_t ret = 0x00;

    if (dev->sb_read_fifo_count) {
        ret = dev->sb_read_fifo[dev->sb_read_fifo_r];
        dev->sb_read_fifo_r = (dev->sb_read_fifo_r + 1) % sizeof(dev->sb_read_fifo);
        dev->sb_read_fifo_count--;
    }

    return ret;
}

static void
solo1_ess_reset_regs(solo1_t *dev)
{
    memset(dev->ess_regs, 0x00, sizeof(dev->ess_regs));

    dev->ess_regs[0xA5 - 0xA0] = 0xF8;

    dev->ess_ext_enabled = 0;
    dev->dsp_pending_cmd = 0;
    dev->dsp_pending_bytes = 0;
    memset(dev->dsp_pending_data, 0x00, sizeof(dev->dsp_pending_data));
}

static uint8_t
solo1_ess_reg_read(solo1_t *dev, uint8_t reg)
{
    if (reg >= 0xA0 && reg <= 0xCF)
        return dev->ess_regs[reg - 0xA0];
    return 0x00;
}

static void
solo1_ess_reg_write(solo1_t *dev, uint8_t reg, uint8_t val)
{
    if (reg >= 0xA0 && reg <= 0xCF)
        dev->ess_regs[reg - 0xA0] = val;
}

static void
solo1_dsp_command_byte(solo1_t *dev, uint8_t val)
{
    if (dev->dsp_pending_bytes) {
        uint8_t cmd = dev->dsp_pending_cmd;

        dev->dsp_pending_data[0] = val;
        dev->dsp_pending_bytes = 0;

        if (cmd == 0xC0) {
            uint8_t ret = solo1_ess_reg_read(dev, val);
            solo1_sb_fifo_put(dev, ret);
        } else if (cmd == 0xC2) {
            solo1_ess_reg_write(dev, 0xC3, val);
        } else if (cmd == 0xCF) {
            solo1_ess_reg_write(dev, 0xCF, val);
        } else if ((cmd >= 0xA0) && (cmd <= 0xBF) && dev->ess_ext_enabled) {
            solo1_ess_reg_write(dev, cmd, val);
        } else {
        }
        return;
    }

    switch (val) {
        case 0xE1:
            /* Solo-1 reports Sound Blaster Pro DSP version 3.1. */
            solo1_sb_fifo_put(dev, 0x03);
            solo1_sb_fifo_put(dev, 0x01);
            break;

        case 0xC6:
            dev->ess_ext_enabled = 1;
            break;

        case 0xC7:
            dev->ess_ext_enabled = 0;
            break;

        case 0xC0:
        case 0xC2:
        case 0xCF:
            dev->dsp_pending_cmd = val;
            dev->dsp_pending_bytes = 1;
            break;

        case 0xC3: {
            uint8_t ret = solo1_ess_reg_read(dev, 0xC3);
            solo1_sb_fifo_put(dev, ret);
            break;
        }

        case 0xCE: {
            uint8_t ret = solo1_ess_reg_read(dev, 0xCF);
            solo1_sb_fifo_put(dev, ret);
            break;
        }

        default:
            if ((val >= 0xA0) && (val <= 0xBF) && dev->ess_ext_enabled) {
                dev->dsp_pending_cmd = val;
                dev->dsp_pending_bytes = 1;
            } else {
            }
            break;
    }
}

static uint8_t
solo1_sb_read(uint16_t addr, void *priv)
{
    solo1_t *dev = (solo1_t *) priv;
    uint8_t off = (uint8_t) (addr - dev->sb_base_mapped);
    uint8_t ret;

    /* BAR1 exposes the same ESFM register aliases as the legacy SB block. */
    if ((off <= 0x03) || (off == 0x08) || (off == 0x09))
        return ess_solo1_legacy_fm_read(dev->legacy, off);

    if (off == 0x0e) {
        ret = (dev->sb_reset_response || dev->sb_read_fifo_count) ? 0x80 : 0x00;
    } else if (off == 0x0a) {
        if (dev->sb_reset_response) {
            ret = 0xaa;
            dev->sb_reset_response = 0;
        } else if (dev->sb_read_fifo_count) {
            ret = solo1_sb_fifo_get(dev);
        } else {
            ret = 0x00;
        }
    } else if (off == 0x0c) {
        ret = dev->sb_read_fifo_count ? 0x40 : 0x00;
    } else if (off == 0x04) {
        ret = dev->mixer_index;
    } else if (off == 0x05) {
        ret = dev->mixer_regs[dev->mixer_index];
    } else {
        ret = (off < sizeof(dev->sb_regs)) ? dev->sb_regs[off] : 0xff;
    }

    return ret;
}

static void
solo1_sb_write(uint16_t addr, uint8_t val, void *priv)
{
    solo1_t *dev = (solo1_t *) priv;
    uint8_t off = (uint8_t) (addr - dev->sb_base_mapped);

    /* BAR1 exposes the same ESFM register aliases as the legacy SB block. */
    if ((off <= 0x03) || (off == 0x08) || (off == 0x09)) {
        ess_solo1_legacy_fm_write(dev->legacy, off, val);
        return;
    }

    if (off < sizeof(dev->sb_regs))
        dev->sb_regs[off] = val;

    if (off == 0x04) {
        dev->mixer_index = val;
    } else if (off == 0x05) {
        if (dev->mixer_index == 0x7a) {
            /* Bit 7 is the Audio 2 IRQ latch and is cleared by writing 0. */
            if (!(val & 0x80))
                dev->a2_irq_pending = 0;
        }

        if (dev->mixer_index == 0x00)
            solo1_mixer_reset_regs(dev);
        else
            dev->mixer_regs[dev->mixer_index] = val;

        /* Native and legacy views share the physical ESS mixer/ESFM path. */
        ess_solo1_legacy_mixer_write(dev->legacy, dev->mixer_index, val);

        if (dev->mixer_index == 0x7a)
            solo1_update_irq(dev);

        if (dev->mixer_index == 0x78 || dev->mixer_index == 0x7a ||
            dev->mixer_index == 0x70 || dev->mixer_index == 0x74 ||
            dev->mixer_index == 0x76 || dev->mixer_index == 0x7c) {
        }
    }

    if (off == 0x06) {
        if (val & 0x01) {
            dev->sb_reset_asserted = 1;
            dev->sb_reset_response = 0;
            solo1_sb_fifo_clear(dev);
            solo1_ess_reset_regs(dev);
        } else if (dev->sb_reset_asserted) {
            dev->sb_reset_asserted = 0;
            dev->sb_reset_response = 1;
        }
    } else if (off == 0x0c) {
        solo1_dsp_command_byte(dev, val);
    }

}

static uint8_t
solo1_mpu_read(uint16_t addr, void *priv)
{
    solo1_t *dev = (solo1_t *) priv;
    uint16_t off = (uint16_t) (addr - dev->mpu_base_mapped);

    return ess_solo1_legacy_mpu_read(dev->legacy, off);
}

static void
solo1_mpu_write(uint16_t addr, uint8_t val, void *priv)
{
    solo1_t *dev = (solo1_t *) priv;
    uint16_t off = (uint16_t) (addr - dev->mpu_base_mapped);

    ess_solo1_legacy_mpu_write(dev->legacy, off, val);
}

static uint8_t
solo1_gp_read(uint16_t addr, void *priv)
{
    solo1_t *dev = (solo1_t *) priv;
    uint8_t off = (uint8_t) (addr - dev->gp_base_mapped);
    uint8_t ret = (off < sizeof(dev->gp_regs)) ? dev->gp_regs[off] : 0xff;

    return ret;
}

static void
solo1_gp_write(uint16_t addr, uint8_t val, void *priv)
{
    solo1_t *dev = (solo1_t *) priv;
    uint8_t off = (uint8_t) (addr - dev->gp_base_mapped);

    if (off < sizeof(dev->gp_regs))
        dev->gp_regs[off] = val;

}

static uint8_t
solo1_ddma_read(uint16_t addr, void *priv)
{
    solo1_t *dev = (solo1_t *) priv;
    uint8_t off = (uint8_t) (addr - dev->ddma_base_mapped);
    uint8_t ret = (off < sizeof(dev->ddma_regs)) ? dev->ddma_regs[off] : 0xff;

    return ret;
}

static void
solo1_ddma_write(uint16_t addr, uint8_t val, void *priv)
{
    solo1_t *dev = (solo1_t *) priv;
    uint8_t off = (uint8_t) (addr - dev->ddma_base_mapped);

    if (off < sizeof(dev->ddma_regs))
        dev->ddma_regs[off] = val;

    if (off == 0x0d)
        memset(dev->ddma_regs, 0, sizeof(dev->ddma_regs));

}

static void solo1_update_legacy(solo1_t *dev);

static void
solo1_update_native_mappings(solo1_t *dev)
{
    const int pci_io = !!(dev->pci_regs[0x04] & 0x01);
    const uint16_t io_base = solo1_bar_base(dev, 0x10, 0xc0);
    const uint16_t sb_base = solo1_bar_base(dev, 0x14, 0xf0);
    const uint16_t mpu_base = solo1_bar_base(dev, 0x1c, 0xfc);
    const uint16_t gp_base = solo1_bar_base(dev, 0x20, 0xfc);
    const uint16_t ddma_base = (uint16_t) ((((uint16_t) dev->pci_regs[0x61]) << 8) |
                                           (dev->pci_regs[0x60] & 0xf0));
    const int ddma_enable = !!(dev->pci_regs[0x60] & 0x01);

    if (dev->io_base_mapped) {
        io_removehandler(dev->io_base_mapped, 0x40,
                         solo1_native_io_read, NULL, NULL,
                         solo1_native_io_write, NULL, NULL, dev);
        dev->io_base_mapped = 0;
    }
    if (dev->sb_base_mapped) {
        io_removehandler(dev->sb_base_mapped, 0x10,
                         solo1_sb_read, NULL, NULL,
                         solo1_sb_write, NULL, NULL, dev);
        dev->sb_base_mapped = 0;
    }
    if (dev->mpu_base_mapped) {
        io_removehandler(dev->mpu_base_mapped, 0x04,
                         solo1_mpu_read, NULL, NULL,
                         solo1_mpu_write, NULL, NULL, dev);
        dev->mpu_base_mapped = 0;
    }
    if (dev->gp_base_mapped) {
        io_removehandler(dev->gp_base_mapped, 0x04,
                         solo1_gp_read, NULL, NULL,
                         solo1_gp_write, NULL, NULL, dev);
        dev->gp_base_mapped = 0;
    }
    if (dev->ddma_base_mapped) {
        io_removehandler(dev->ddma_base_mapped, 0x10,
                         solo1_ddma_read, NULL, NULL,
                         solo1_ddma_write, NULL, NULL, dev);
        dev->ddma_base_mapped = 0;
    }

    if (pci_io && io_base) {
        dev->io_base_mapped = io_base;
        io_sethandler(io_base, 0x40,
                      solo1_native_io_read, NULL, NULL,
                      solo1_native_io_write, NULL, NULL, dev);
    }
    if (pci_io && sb_base) {
        dev->sb_base_mapped = sb_base;
        io_sethandler(sb_base, 0x10,
                      solo1_sb_read, NULL, NULL,
                      solo1_sb_write, NULL, NULL, dev);
    }
    if (pci_io && mpu_base) {
        dev->mpu_base_mapped = mpu_base;
        io_sethandler(mpu_base, 0x04,
                      solo1_mpu_read, NULL, NULL,
                      solo1_mpu_write, NULL, NULL, dev);
    }
    if (pci_io && gp_base) {
        dev->gp_base_mapped = gp_base;
        io_sethandler(gp_base, 0x04,
                      solo1_gp_read, NULL, NULL,
                      solo1_gp_write, NULL, NULL, dev);
    }
    if (pci_io && ddma_enable && ddma_base) {
        dev->ddma_base_mapped = ddma_base;
        io_sethandler(ddma_base, 0x10,
                      solo1_ddma_read, NULL, NULL,
                      solo1_ddma_write, NULL, NULL, dev);
    }

    /* Separate legacy core from PCI BARs to fix Win98 driver init */
    solo1_update_legacy(dev);

}

static void
solo1_update_legacy(solo1_t *dev)
{
    const uint8_t  lacr_lo = dev->pci_regs[0x40];
    const uint8_t  lacr_hi = dev->pci_regs[0x41];
    const int      legacy_enable = !(lacr_hi & 0x80);
    const uint16_t sb_base = (dev->pci_regs[0x50] & 0x04) ? 0x240 : 0x220;
    const uint16_t mpu_base = solo1_decode_mpu_base(dev);
    const int      sb_irq = solo1_decode_irq(lacr_hi & 0x07);
    const int      mpu_irq = solo1_decode_irq((lacr_hi >> 3) & 0x07);
    const int      dma = solo1_decode_dma((lacr_lo >> 6) & 0x03);
    const int      sb_enable = legacy_enable && (lacr_lo & 0x01);
    const int      fm_enable = legacy_enable && (lacr_lo & 0x02);
    const int      gp_enable = legacy_enable && (lacr_lo & 0x04);
    const int      mpu_enable = legacy_enable && (lacr_lo & 0x08);
    const int      mpu_irq_enable = legacy_enable && (lacr_lo & 0x10);

    /* Legacy resources */
    ess_solo1_legacy_config(dev->legacy,
                            sb_base,
                            sb_enable,
                            fm_enable,
                            fm_enable,
                            mpu_base,
                            mpu_enable,
                            sb_irq,
                            mpu_irq_enable ? mpu_irq : -1,
                            dma,
                            gp_enable ? 0x0200 : 0);

}

static void
solo1_write_bar(solo1_t *dev, int addr, uint8_t val)
{
    int base = addr & ~3;
    int off  = addr & 3;

    /* Solo-1 decodes only 16 bits of each I/O BAR. */
    if (off >= 2) {
        dev->pci_regs[addr] = 0x00;
        return;
    }

    if (off == 0) {
        if (base == 0x10)
            val = (val & 0xc0) | 0x01; /* BAR0: 64-byte I/O region */
        else if (base == 0x14 || base == 0x18)
            val = (val & 0xf0) | 0x01; /* BAR1/BAR2: 16-byte I/O regions */
        else
            val = (val & 0xfc) | 0x01; /* BAR3/BAR4: 4-byte I/O regions */
    }

    dev->pci_regs[addr] = val;
}

static void
solo1_pci_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    solo1_t *dev = (solo1_t *) priv;

    if (func || addr < 0 || addr > 0xff)
        return;


    switch (addr) {
        case 0x04: /* Command low: I/O enable + bus master only. */
            dev->pci_regs[addr] = val & 0x05;
            break;

        case 0x05:
            dev->pci_regs[addr] = 0x00;
            break;

        case 0x06: /* Status low: fixed capability/DEVSEL-related bits. */
            dev->pci_regs[addr] = 0x90;
            break;

        case 0x07: /* Abort status bits are write-one-to-clear. */
            dev->pci_regs[addr] &= ~(val & 0x30);
            break;

        case 0x0d:
            dev->pci_regs[addr] = val & 0xf0;
            break;

        case 0x10: case 0x11: case 0x12: case 0x13:
        case 0x14: case 0x15: case 0x16: case 0x17:
        case 0x18: case 0x19: case 0x1a: case 0x1b:
        case 0x1c: case 0x1d: case 0x1e: case 0x1f:
        case 0x20: case 0x21: case 0x22: case 0x23:
            solo1_write_bar(dev, addr, val);
            break;

        case 0x2c: case 0x2d: case 0x2e: case 0x2f:
            if (dev->pci_regs[0x50] & 0x01)
                dev->pci_regs[addr] = val;
            break;

        case 0x3c:
            /* Force IRQ5 only for onboard */
            dev->pci_regs[addr] = dev->onboard ? 0x05 : val;
            break;

        case 0x40:
        case 0x41:
            dev->pci_regs[addr] = val;
            break;

        case 0x50:
        case 0x51:
        case 0x52:
        case 0x53:
            dev->pci_regs[addr] = val;
            break;

        case 0x60:
            dev->pci_regs[addr] = (val & 0xf0) | (val & 0x01);
            break;

        case 0x61:
            dev->pci_regs[addr] = val;
            break;

        case 0xc4:
            dev->pci_regs[addr] = val & 0x03;
            break;

        case 0xc5:
            dev->pci_regs[addr] = 0x00;
            break;

        default:
            return;
    }

    if ((addr == 0x40) || (addr == 0x41) ||
        ((addr >= 0x50) && (addr <= 0x53)))
        solo1_update_legacy(dev);

    if ((addr == 0x04) || (addr == 0x3c) ||
        ((addr >= 0x10) && (addr <= 0x23)) ||
        ((addr >= 0x40) && (addr <= 0x41)) ||
        ((addr >= 0x50) && (addr <= 0x53)) ||
        ((addr >= 0x60) && (addr <= 0x61)))
        solo1_update_native_mappings(dev);
}

static void
solo1_reset(void *priv)
{
    solo1_t *dev = (solo1_t *) priv;

    if (dev->legacy != NULL)
        ess_solo1_legacy_reset(dev->legacy);

    memset(dev->pci_regs, 0x00, sizeof(dev->pci_regs));
    memset(dev->io_regs, 0x00, sizeof(dev->io_regs));
    memset(dev->sb_regs, 0x00, sizeof(dev->sb_regs));
    memset(dev->mpu_regs, 0x00, sizeof(dev->mpu_regs));
    dev->mpu_ack_pending = 0;
    memset(dev->gp_regs, 0x00, sizeof(dev->gp_regs));
    memset(dev->ddma_regs, 0x00, sizeof(dev->ddma_regs));
    dev->sb_reset_asserted = 0;
    dev->sb_reset_response = 0;
    solo1_sb_fifo_clear(dev);
    solo1_ess_reset_regs(dev);

    dev->pci_regs[0x00] = 0x5d; /* VID 125Dh */
    dev->pci_regs[0x01] = 0x12;
    dev->pci_regs[0x02] = 0x69; /* DID 1969h */
    dev->pci_regs[0x03] = 0x19;

    dev->pci_regs[0x06] = 0x90; /* Status reset 0290h */
    dev->pci_regs[0x07] = 0x02;

    dev->pci_regs[0x08] = 0x00; /* Revision */
    dev->pci_regs[0x09] = 0x00; /* Programming interface */
    dev->pci_regs[0x0a] = 0x01; /* Audio */
    dev->pci_regs[0x0b] = 0x04; /* Multimedia */
    dev->pci_regs[0x0e] = 0x00; /* Single-function */

    dev->pci_regs[0x10] = 0x01;
    dev->pci_regs[0x14] = 0x01;
    dev->pci_regs[0x18] = 0x01;
    dev->pci_regs[0x1c] = 0x01;
    dev->pci_regs[0x20] = 0x01;

    dev->pci_regs[0x2c] = 0x5d; /* SVID 125Dh */
    dev->pci_regs[0x2d] = 0x12;
    dev->pci_regs[0x2e] = 0x18; /* SID 1818h */
    dev->pci_regs[0x2f] = 0x18;

    dev->pci_regs[0x34] = 0xc0;
    dev->pci_regs[0x3c] = 0xff;
    dev->pci_regs[0x3d] = 0x01; /* INTA */
    dev->pci_regs[0x3e] = 0x02;
    dev->pci_regs[0x3f] = 0x18;

    dev->pci_regs[0x40] = 0x7f; /* Legacy Audio Control = 907Fh */
    dev->pci_regs[0x41] = 0x90;

    dev->pci_regs[0xc0] = 0x01; /* PCI PM capability */
    dev->pci_regs[0xc1] = 0x00;
    dev->pci_regs[0xc2] = 0x21; /* PM capabilities = 0621h */
    dev->pci_regs[0xc3] = 0x06;
    dev->pci_regs[0xc4] = 0x00; /* PMCSR = D0 */
    dev->pci_regs[0xc5] = 0x00;

    memset(dev->io_regs, 0, sizeof(dev->io_regs));
    memset(dev->ddma_regs, 0, sizeof(dev->ddma_regs));
    solo1_mixer_reset_regs(dev);

    dev->a2_base_addr = 0;
    dev->a2_cur_addr = 0;
    dev->a2_base_count = 0;
    dev->a2_cur_count = 0;
    dev->a2_rate_accum = 0;
    dev->a2_irq_bytes_left = 0;
    dev->a2_last_l = 0;
    dev->a2_last_r = 0;
    dev->a2_irq_pending = 0;
    solo1_a2_cache_invalidate(dev);
    solo1_update_irq(dev);

    solo1_update_legacy(dev);
    solo1_update_native_mappings(dev);
}

static void *
solo1_init(const device_t *info)
{
    solo1_t *dev = calloc(1, sizeof(solo1_t));

    dev->onboard = (uint8_t) (info->local != 0);

    dev->legacy = ess_solo1_legacy_init();
    sound_add_handler(solo1_get_buffer, dev);

    pci_add_card(PCI_ADD_SOUND, solo1_pci_read, solo1_pci_write,
                 dev, &dev->pci_slot);
    solo1_reset(dev);

    return dev;
}

static void
solo1_close(void *priv)
{
    solo1_t *dev = (solo1_t *) priv;
    dev->pci_regs[0x04] &= ~0x01;
    dev->pci_regs[0x60] &= ~0x01;
    solo1_update_native_mappings(dev);
    if (dev->legacy != NULL)
        ess_solo1_legacy_close(dev->legacy);
    free(dev);
}

const device_t ess_solo1_device = {
    .name          = "ESS ES1938S Solo-1",
    .internal_name = "ess_solo1",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = solo1_init,
    .close         = solo1_close,
    .reset         = solo1_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ess_solo1_onboard_device = {
    .name          = "ESS ES1938S Solo-1 (On-Board)",
    .internal_name = "ess_solo1_onboard",
    .flags         = DEVICE_PCI,
    .local         = 1,
    .init          = solo1_init,
    .close         = solo1_close,
    .reset         = solo1_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
