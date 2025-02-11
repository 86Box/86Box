/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          C-Media CMI8x38 PCI audio controller emulation.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2022 RichardG.
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
#include <86box/dma.h>
#include <86box/pci.h>
#include <86box/sound.h>
#include <86box/snd_sb.h>
#include <86box/snd_sb_dsp.h>
#include <86box/gameport.h>
#include <86box/nmi.h>
#include <86box/ui.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>

enum {
    /* [23:16] = reg 0F [7:0] (reg 0C [31:24])
          [13] = onboard flag
        [12:8] = reg 0B [4:0] (reg 08 [28:24])
         [7:0] = PCI device ID [7:0] */
    CMEDIA_CMI8338     = 0x000000,
    CMEDIA_CMI8738_4CH = 0x040011, /* chip version 039 with 4-channel output */
    CMEDIA_CMI8738_6CH = 0x080011  /* chip version 055 with 6-channel output */
};

enum {
    TRAP_DMA = 0,
    TRAP_PIC,
    TRAP_OPL,
    TRAP_MPU,
    TRAP_MAX
};

typedef struct cmi8x38_dma_t {
    uint8_t           id;
    uint8_t           reg;
    uint8_t           always_run;
    uint8_t           playback_enabled;
    uint8_t           channels;
    struct _cmi8x38_ *dev;

    uint32_t sample_ptr;
    uint32_t fifo_pos;
    uint32_t fifo_end;
    int32_t  frame_count_dma;
    int32_t  frame_count_fragment;
    int32_t  sample_count_out;
    uint8_t  fifo[256];
    uint8_t  restart;

    int16_t  out_fl;
    int16_t  out_fr;
    int16_t  out_rl;
    int16_t  out_rr;
    int16_t  out_c;
    int16_t  out_lfe;
    int      vol_l;
    int      vol_r;
    int      pos;
    int32_t  buffer[SOUNDBUFLEN * 2];
    uint64_t timer_latch;
    double   dma_latch;

    pc_timer_t dma_timer;
    pc_timer_t poll_timer;
} cmi8x38_dma_t;

typedef struct _cmi8x38_ {
    uint32_t type;
    uint16_t io_base;
    uint16_t sb_base;
    uint16_t opl_base;
    uint16_t mpu_base;
    uint8_t  pci_regs[256];
    uint8_t  io_regs[256];
    uint8_t  pci_slot;
    uint8_t  irq_state;

    sb_t *sb;
    void *gameport;
    void *io_traps[TRAP_MAX];

    cmi8x38_dma_t dma[2];
    uint16_t      tdma_base_addr;
    uint16_t      tdma_base_count;
    int           tdma_8;
    int           tdma_16;
    int           tdma_mask;
    int           prev_mask;
    int           tdma_irq_mask;

    int master_vol_l;
    int master_vol_r;
    int cd_vol_l;
    int cd_vol_r;
} cmi8x38_t;

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
#    define cmi8x38_log(fmt, ...)
#endif

static const double   freqs[]             = { 5512.0, 11025.0, 22050.0, 44100.0, 8000.0, 16000.0, 32000.0, 48000.0 };
static const uint16_t opl_ports_cmi8738[] = { 0x388, 0x3c8, 0x3e0, 0x3e8 };

static void cmi8x38_dma_process(void *priv);
static void cmi8x38_speed_changed(void *priv);

static void
cmi8x38_update_irqs(cmi8x38_t *dev)
{
    /* Calculate and use the INTR flag. */
    if (*((uint32_t *) &dev->io_regs[0x10]) & 0x0401c003) {
        dev->io_regs[0x13] |= 0x80;
        pci_set_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
        cmi8x38_log("CMI8x38: Raising IRQ\n");
    } else {
        dev->io_regs[0x13] &= ~0x80;
        pci_clear_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
    }
}

static void
cmi8x38_mpu_irq_update(void *priv, int set)
{
    cmi8x38_t *dev = (cmi8x38_t *) priv;
    if (set)
        dev->io_regs[0x12] |= 0x01;
    else
        dev->io_regs[0x12] &= ~0x01;
    cmi8x38_update_irqs(dev);
}

static int
cmi8x38_mpu_irq_pending(void *priv)
{
    const cmi8x38_t *dev = (cmi8x38_t *) priv;

    return dev->io_regs[0x12] & 0x01;
}

static void
cmi8x38_sb_irq_update(void *priv, int set)
{
    cmi8x38_t *dev = (cmi8x38_t *) priv;
    /* Interrupt flag shared with the first DMA channel. Combining SB and
       PCI DMA is undefined; the VxD driver disables SB if PCI is in use. */
    if (set)
        dev->io_regs[0x10] |= 0x01;
    else
        dev->io_regs[0x10] &= ~0x01;
    cmi8x38_update_irqs(dev);
}

static int
cmi8x38_sb_dma_post(cmi8x38_t *dev, uint16_t *addr, uint16_t *count, int channel)
{
    /* Increment address and decrement count. */
    *addr += 1;
    *count -= 1;

    /* Copy TDMA registers to DMA on CMI8738+. Everything so far suggests that
       those chips use PCI bus mastering to directly write to the DMA registers. */
#if 0 /* TSRs don't set ENWR8237, except for the patched C3DPCI - does that bit have no effect? */
    if ((dev->type != CMEDIA_CMI8338) && (dev->io_regs[0x17] & 0x10))
#else
    if (dev->type != CMEDIA_CMI8338)
#endif
    {
        if (channel & 4)
            dma[channel].ab = (dma[channel].ab & 0xfffe0000) | ((*addr) << 1);
        else
            dma[channel].ab = (dma[channel].ab & 0xffff0000) | *addr;
        dma[channel].ac = dma[channel].ab;
        dma[channel].cc = dma[channel].cb = *count;
    }

    /* Check TDMA position update interrupt if enabled. */
    if (dev->io_regs[0x0e] & 0x04) {
        /* Nothing uses this; I assume it goes by the SB DSP sample counter (forwards instead of backwards). */
        int origlength = (channel & 4) ? dev->sb->dsp.sb_16_origlength : dev->sb->dsp.sb_8_origlength;
        int length     = (channel & 4) ? dev->sb->dsp.sb_16_length : dev->sb->dsp.sb_8_length;
        if ((origlength != length) && (((origlength - length) & dev->tdma_irq_mask) == 0)) { /* skip initial sample */
            /* Fire the interrupt. */
            dev->io_regs[0x11] |= (channel & 4) ? 0x40 : 0x80;
            cmi8x38_update_irqs(dev);
        }
    }

    /* Handle end of DMA. */
    if (*count == 0xffff) {
        if (dma[channel].mode & 0x10) { /* auto-init */
            /* Restart TDMA. */
            *addr  = dev->tdma_base_addr;
            *count = dev->tdma_base_count;
            cmi8x38_log("CMI8x38: Restarting TDMA on DMA %d with addr %08X count %04X\n",
                        channel,
                        (channel & 4) ? ((dma[channel].ab & 0xfffe0000) | ((*addr) << 1)) : ((dma[channel].ab & 0xffff0000) | *addr),
                        *count);
        } else {
            /* Mask TDMA. */
            dev->tdma_mask |= 1 << channel;
        }

        /* Set the mysterious LHBTOG bit, assuming it corresponds
           to the 8237 channel status bit. Nothing reads it. */
        dev->io_regs[0x10] |= 0x40;

        return DMA_OVER;
    }
    return 0;
}

static int
cmi8x38_sb_dma_readb(void *priv)
{
    cmi8x38_t *dev = (cmi8x38_t *) priv;

    /* Stop if the DMA channel is invalid or if TDMA is masked. */
    int channel = dev->tdma_8;
    if ((channel < 0) || (dev->tdma_mask & (1 << channel)))
        return DMA_NODATA;

    /* Get 16-bit address and count registers. */
    uint16_t *addr  = (uint16_t *) &dev->io_regs[0x1c];
    uint16_t *count = (uint16_t *) &dev->io_regs[0x1e];

    /* Read data. */
    int ret = mem_readb_phys((dma[channel].ab & 0xffff0000) | *addr);

    /* Handle address, count and end. */
    ret |= cmi8x38_sb_dma_post(dev, addr, count, channel);

    return ret;
}

static int
cmi8x38_sb_dma_readw(void *priv)
{
    cmi8x38_t *dev = (cmi8x38_t *) priv;

    /* Stop if the DMA channel is invalid or if TDMA is masked. */
    int channel = dev->tdma_16;
    if ((channel < 0) || (dev->tdma_mask & (1 << channel)))
        return DMA_NODATA;

    /* Get 16-bit address and count registers. */
    uint16_t *addr  = (uint16_t *) &dev->io_regs[0x1c];
    uint16_t *count = (uint16_t *) &dev->io_regs[0x1e];

    /* Read data. */
    int ret = mem_readw_phys((dma[channel].ab & 0xfffe0000) | ((*addr) << 1));

    /* Handle address, count and end. */
    ret |= cmi8x38_sb_dma_post(dev, addr, count, channel);

    return ret;
}

static int
cmi8x38_sb_dma_writeb(void *priv, uint8_t val)
{
    cmi8x38_t *dev = (cmi8x38_t *) priv;

    /* Stop if the DMA channel is invalid or if TDMA is masked. */
    int channel = dev->tdma_8;
    if ((channel < 0) || (dev->tdma_mask & (1 << channel)))
        return 1;

    /* Get 16-bit address and count registers. */
    uint16_t *addr  = (uint16_t *) &dev->io_regs[0x1c];
    uint16_t *count = (uint16_t *) &dev->io_regs[0x1e];

    /* Write data. */
    mem_writeb_phys((dma[channel].ab & 0xffff0000) | *addr, val);

    /* Handle address, count and end. */
    cmi8x38_sb_dma_post(dev, addr, count, channel);

    return 0;
}

static int
cmi8x38_sb_dma_writew(void *priv, uint16_t val)
{
    cmi8x38_t *dev = (cmi8x38_t *) priv;

    /* Stop if the DMA channel is invalid or if TDMA is masked. */
    int channel = dev->tdma_16;
    if ((channel < 0) || (dev->tdma_mask & (1 << channel)))
        return 1;

    /* Get 16-bit address and count registers. */
    uint16_t *addr  = (uint16_t *) &dev->io_regs[0x1c];
    uint16_t *count = (uint16_t *) &dev->io_regs[0x1e];

    /* Write data. */
    mem_writew_phys((dma[channel].ab & 0xfffe0000) | ((*addr) << 1), val);

    /* Handle address, count and end. */
    cmi8x38_sb_dma_post(dev, addr, count, channel);

    return 0;
}

static void
cmi8x38_dma_write(uint16_t addr, UNUSED(uint8_t val), void *priv)
{
    cmi8x38_t *dev = (cmi8x38_t *) priv;

    /* Stop if DMA channel auto-detection is disabled. This is required for the CMI8338 TSR,
       which disables auto-detection while copying the TDMA address/count to the SB DMA channel,
       so that those writes don't loop back to the DMA register snoop mechanism implemented here. */
    if (!(dev->io_regs[0x27] & 0x01))
        return;

    /* Stop if this is not a TDMA channel. Also set or
       clear the high channel flag while we're here. */
    int channel;
    if (addr < 0x08) {
        channel = addr >> 1;
        if (channel != dev->tdma_8)
            return;
        dev->io_regs[0x10] &= ~0x20;
    } else {
        channel = 4 | ((addr >> 2) & 3);
        if (channel != dev->tdma_16)
            return;
        dev->io_regs[0x10] |= 0x20;
    }

    /* Write base address and count. */
    uint16_t *daddr = (uint16_t *) &dev->io_regs[0x1c];
    uint16_t *count = (uint16_t *) &dev->io_regs[0x1e];
    *daddr = dev->tdma_base_addr = dma[channel].ab >> !!(channel & 4);
    *count = dev->tdma_base_count = dma[channel].cb;
    cmi8x38_log("CMI8x38: Starting TDMA on DMA %d with addr %08X count %04X\n",
                channel,
                (channel & 4) ? ((dma[channel].ab & 0xfffe0000) | ((*daddr) << 1)) : ((dma[channel].ab & 0xffff0000) | *daddr),
                *count);

    /* Clear the mysterious LHBTOG bit, assuming it corresponds
       to the 8237 channel status bit. Nothing reads it. */
    dev->io_regs[0x10] &= ~0x40;
}

static void
cmi8x38_dma_mask_write(UNUSED(uint16_t addr), UNUSED(uint8_t val), void *priv)
{
    cmi8x38_t *dev = (cmi8x38_t *) priv;

    /* See comment on dma_write above. */
    if (!(dev->io_regs[0x27] & 0x01))
        return;

    /* Unmask TDMA on DMA unmasking edge. */
    if ((dev->tdma_8 >= 0) && (dev->prev_mask & (1 << dev->tdma_8)) && !(dma_m & (1 << dev->tdma_8)))
        dev->tdma_mask &= ~(1 << dev->tdma_8);
    else if ((dev->tdma_16 >= 0) && (dev->prev_mask & (1 << dev->tdma_16)) && !(dma_m & (1 << dev->tdma_16)))
        dev->tdma_mask &= ~(1 << dev->tdma_16);
    dev->prev_mask = dma_m;
}

static void
cmi8338_io_trap(UNUSED(int size), uint16_t addr, uint8_t write, uint8_t val, void *priv)
{
    cmi8x38_t *dev = (cmi8x38_t *) priv;

#ifdef ENABLE_CMI8X38_LOG
    if (write)
        cmi8x38_log("CMI8x38: cmi8338_io_trap(%04X, %02X)\n", addr, val);
    else
        cmi8x38_log("CMI8x38: cmi8338_io_trap(%04X)\n", addr);
#endif

    /* Weird offsets, it's best to just treat the register as a big dword. */
    uint32_t *lcs = (uint32_t *) &dev->io_regs[0x14];
    *lcs &= ~0x0003dff0;
    *lcs |= (addr & 0x0f) << 14;
    if (write)
        *lcs |= 0x1000 | (val << 4);

    /* Raise NMI. */
    nmi = 1;
}

static uint8_t
cmi8x38_sb_mixer_read(uint16_t addr, void *priv)
{
    cmi8x38_t               *dev   = (cmi8x38_t *) priv;
    const sb_ct1745_mixer_t *mixer = &dev->sb->mixer_sb16;
    uint8_t                  ret   = sb_ct1745_mixer_read(addr, dev->sb);

    if (addr & 1) {
        if ((mixer->index == 0x0e) || (mixer->index >= 0xf0))
            ret = mixer->regs[mixer->index];
        cmi8x38_log("CMI8x38: sb_mixer_read(1, %02X) = %02X\n", mixer->index, ret);
    } else {
        cmi8x38_log("CMI8x38: sb_mixer_read(0) = %02X\n", ret);
    }

    return ret;
}

static void
cmi8x38_sb_mixer_write(uint16_t addr, uint8_t val, void *priv)
{
    cmi8x38_t         *dev   = (cmi8x38_t *) priv;
    sb_ct1745_mixer_t *mixer = &dev->sb->mixer_sb16;

    /* Our clone mixer has a few differences. */
    if (addr & 1) {
        cmi8x38_log("CMI8x38: sb_mixer_write(1, %02X, %02X)\n", mixer->index, val);

        switch (mixer->index) {
            /* Reset interleaved stereo flag for SBPro mode. */
            case 0x00:
                mixer->regs[0x0e] = 0x00;
                break;

            /* No dynamic MPU port assignment. */
            case 0x84:
                return;

            /* Some extended registers beyond those accepted by the CT1745. */
            case 0xf0:
                if (dev->type == CMEDIA_CMI8338)
                    val &= 0xfe;
                mixer->regs[mixer->index] = val;
                return;

            case 0xf8 ... 0xff:
                if (dev->type == CMEDIA_CMI8338)
                    mixer->regs[mixer->index] = val;
                fallthrough;

            case 0xf1 ... 0xf7:
                return;

            default:
                break;
        }

        sb_ct1745_mixer_write(addr, val, dev->sb);

        /* No [3F:47] controls. */
        mixer->input_gain_L  = 0;
        mixer->input_gain_R  = 0;
        mixer->output_gain_L = 1.0;
        mixer->output_gain_R = 1.0;
        mixer->bass_l        = 8;
        mixer->bass_r        = 8;
        mixer->treble_l      = 8;
        mixer->treble_r      = 8;

        /* Check interleaved stereo flag for SBPro mode. */
        if ((mixer->index == 0x00) || (mixer->index == 0x0e))
            sb_dsp_set_stereo(&dev->sb->dsp, mixer->regs[0x0e] & 2);

        /* Set TDMA channels if auto-detection is enabled. */
        if ((dev->io_regs[0x27] & 0x01) && (mixer->index == 0x81)) {
            dev->tdma_8 = dev->sb->dsp.sb_8_dmanum;
            if (dev->sb->dsp.sb_type >= SB16_DSP_404)
                dev->tdma_16 = dev->sb->dsp.sb_16_dmanum;
        }
    } else {
        cmi8x38_log("CMI8x38: sb_mixer_write(0, %02X)\n", val);
        sb_ct1745_mixer_write(addr, val, dev->sb);
    }
}

static void
cmi8x38_remap_sb(cmi8x38_t *dev)
{
    if (dev->sb_base) {
        io_removehandler(dev->sb_base, 0x0004, dev->sb->opl.read, NULL, NULL,
                         dev->sb->opl.write, NULL, NULL, dev->sb->opl.priv);
        io_removehandler(dev->sb_base + 8, 0x0002, dev->sb->opl.read, NULL, NULL,
                         dev->sb->opl.write, NULL, NULL, dev->sb->opl.priv);
        io_removehandler(dev->sb_base + 4, 0x0002, cmi8x38_sb_mixer_read, NULL, NULL,
                         cmi8x38_sb_mixer_write, NULL, NULL, dev);

        sb_dsp_setaddr(&dev->sb->dsp, 0);
    }

    dev->sb_base = 0x220;
    if (dev->type == CMEDIA_CMI8338)
        dev->sb_base += (dev->io_regs[0x17] & 0x80) >> 2;
    else
        dev->sb_base += (dev->io_regs[0x17] & 0x0c) << 3;
    if (!(dev->io_regs[0x04] & 0x08))
        dev->sb_base = 0;
    cmi8x38_log("CMI8x38: remap_sb(%04X)\n", dev->sb_base);

    if (dev->sb_base) {
        io_sethandler(dev->sb_base, 0x0004, dev->sb->opl.read, NULL, NULL,
                      dev->sb->opl.write, NULL, NULL, dev->sb->opl.priv);
        io_sethandler(dev->sb_base + 8, 0x0002, dev->sb->opl.read, NULL, NULL,
                      dev->sb->opl.write, NULL, NULL, dev->sb->opl.priv);
        io_sethandler(dev->sb_base + 4, 0x0002, cmi8x38_sb_mixer_read, NULL, NULL,
                      cmi8x38_sb_mixer_write, NULL, NULL, dev);

        sb_dsp_setaddr(&dev->sb->dsp, dev->sb_base);
    }
}

static void
cmi8x38_remap_opl(cmi8x38_t *dev)
{
    if (dev->opl_base) {
        io_removehandler(dev->opl_base, 0x0004, dev->sb->opl.read, NULL, NULL,
                         dev->sb->opl.write, NULL, NULL, dev->sb->opl.priv);
    }

    dev->opl_base = (dev->type == CMEDIA_CMI8338) ? 0x388 : opl_ports_cmi8738[dev->io_regs[0x17] & 0x03];
    io_trap_remap(dev->io_traps[TRAP_OPL], (dev->io_regs[0x04] & 0x01) && (dev->io_regs[0x16] & 0x80), dev->opl_base, 4);
    if (!(dev->io_regs[0x1a] & 0x08))
        dev->opl_base = 0;

    cmi8x38_log("CMI8x38: remap_opl(%04X)\n", dev->opl_base);

    if (dev->opl_base) {
        io_sethandler(dev->opl_base, 0x0004, dev->sb->opl.read, NULL, NULL,
                      dev->sb->opl.write, NULL, NULL, dev->sb->opl.priv);
    }
}

static void
cmi8x38_remap_mpu(cmi8x38_t *dev)
{
    if (dev->mpu_base)
        mpu401_change_addr(dev->sb->mpu, 0);

    /* The CMI8338 datasheet's port range of [300:330] is
       inaccurate. Drivers expect [330:300] like CMI8738. */
    dev->mpu_base = 0x330 - ((dev->io_regs[0x17] & 0x60) >> 1);
    io_trap_remap(dev->io_traps[TRAP_MPU], (dev->io_regs[0x04] & 0x01) && (dev->io_regs[0x16] & 0x20), dev->mpu_base, 2);
    if (!(dev->io_regs[0x04] & 0x04))
        dev->mpu_base = 0;

    cmi8x38_log("CMI8x38: remap_mpu(%04X)\n", dev->mpu_base);

    if (dev->mpu_base)
        mpu401_change_addr(dev->sb->mpu, dev->mpu_base);
}

static void
cmi8x38_remap_traps(cmi8x38_t *dev)
{
    cmi8x38_remap_opl(dev);
    cmi8x38_remap_mpu(dev);
    io_trap_remap(dev->io_traps[TRAP_DMA], (dev->io_regs[0x04] & 0x01) && (dev->io_regs[0x17] & 0x02), 0x0000, 16);
    io_trap_remap(dev->io_traps[TRAP_PIC], (dev->io_regs[0x04] & 0x01) && (dev->io_regs[0x17] & 0x01), 0x0020, 2);
}

static void
cmi8x38_start_playback(cmi8x38_t *dev)
{
    uint8_t i;
    uint8_t val = dev->io_regs[0x00];

    i = !(val & 0x01);
    if (!dev->dma[0].playback_enabled && i)
        timer_set_delay_u64(&dev->dma[0].poll_timer, dev->dma[0].timer_latch);
    dev->dma[0].playback_enabled = i;

    i = !(val & 0x02);
    if (!dev->dma[1].playback_enabled && i)
        timer_set_delay_u64(&dev->dma[1].poll_timer, dev->dma[1].timer_latch);
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
        case 0x23:
            ret = cmi8x38_sb_mixer_read(addr ^ 1, dev);
            break;

        case 0x40 ... 0x4f:
            if (dev->type == CMEDIA_CMI8338)
                goto io_reg;
            else
                ret = mpu401_read(addr, dev->sb->mpu);
            break;

        case 0x50 ... 0x5f:
            if (dev->type == CMEDIA_CMI8338)
                goto io_reg;
            else
                ret = dev->sb->opl.read(addr, dev->sb->opl.priv);
            break;

        case 0x80:
        case 0x88:
            ret = dev->dma[(addr & 0x78) >> 3].sample_ptr;
            break;

        case 0x81:
        case 0x89:
            ret = dev->dma[(addr & 0x78) >> 3].sample_ptr >> 8;
            break;

        case 0x82:
        case 0x8a:
            ret = dev->dma[(addr & 0x78) >> 3].sample_ptr >> 16;
            break;

        case 0x83:
        case 0x8b:
            ret = dev->dma[(addr & 0x78) >> 3].sample_ptr >> 24;
            break;

        case 0x84:
        case 0x8c:
            ret = dev->dma[(addr & 0x78) >> 3].frame_count_dma;
            break;

        case 0x85:
        case 0x8d:
            ret = dev->dma[(addr & 0x78) >> 3].frame_count_dma >> 8;
            break;

        case 0x86:
        case 0x8e:
            ret = dev->dma[(addr & 0x78) >> 3].sample_count_out >> 2;
            break;

        case 0x87:
        case 0x8f:
            ret = dev->dma[(addr & 0x78) >> 3].sample_count_out >> 10;
            break;

        default:
io_reg:
            ret = dev->io_regs[addr];
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
            dev->io_regs[addr] = val;
            cmi8x38_start_playback(dev);
            break;

        case 0x02:
            /* Reset or start DMA channels if requested. */
            dev->io_regs[addr] = val & 0x0f;
            for (int i = 0; i < (sizeof(dev->dma) / sizeof(dev->dma[0])); i++) {
                if (val & (0x04 << i)) {
                    /* Reset DMA channel. */
                    dev->io_regs[addr] &= ~(0x01 << i); /* clear enable */
                    dev->io_regs[0x10] &= ~(0x01 << i); /* clear interrupt */

                    /* Reset Sound Blaster as well when resetting channel 0. */
                    if ((i == 0) && (dev->sb->dsp.sb_8_enable || dev->sb->dsp.sb_16_enable || dev->sb->dsp.sb_irq8 || dev->sb->dsp.sb_irq16))
                        dev->sb->dsp.sb_8_enable = dev->sb->dsp.sb_16_enable = dev->sb->dsp.sb_irq8 = dev->sb->dsp.sb_irq16 = 0;
                } else if (val & (0x01 << i)) {
                    /* Start DMA channel. */
                    cmi8x38_log("CMI8x38: DMA %d trigger\n", i);
                    dev->dma[i].restart = 1;
                    cmi8x38_dma_process(&dev->dma[i]);
                }
            }

            /* Start playback along with DMA channels. */
            if (dev->io_regs[addr] & 0x03)
                cmi8x38_start_playback(dev);

            /* Update interrupts. */
            cmi8x38_update_irqs(dev);
            break;

        case 0x04:
            /* Enable or disable the game port. */
            gameport_remap(dev->gameport, (val & 0x02) ? 0x200 : 0);

            /* Enable or disable the legacy devices. */
            dev->io_regs[addr] = val;
            cmi8x38_remap_sb(dev);
            /* remap_mpu called by remap_traps */

            /* Enable or disable I/O traps. */
            cmi8x38_remap_traps(dev);
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
            if (dev->type == CMEDIA_CMI8338)
                return;

            /* Update sample rate. */
            dev->io_regs[addr] = val;
            cmi8x38_speed_changed(dev);
            break;

        case 0x0a:
        case 0x0b:
            if (dev->type == CMEDIA_CMI8338)
                return;
            else
                val &= 0xe0;

            if (addr == 0x0a) {
                /* Set PCI latency timer if requested. */
                dev->pci_regs[0x0d] = (val & 0x80) ? 0x48 : 0x20; /* clearing SETLAT48 is undefined */
            } else {
                /* Update channel count. */
                dev->io_regs[addr] = val;
                cmi8x38_speed_changed(dev);
            }
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

            /* Update channel count. */
            dev->io_regs[addr] = val;
            cmi8x38_speed_changed(dev);
            break;

        case 0x16:
            if (dev->type == CMEDIA_CMI8338) {
                val &= 0xa0;

                /* Enable or disable I/O traps. */
                dev->io_regs[addr] = val;
                cmi8x38_remap_traps(dev);
            }
            break;

        case 0x17:
            if (dev->type == CMEDIA_CMI8338) {
                val &= 0xf3;

                /* Force IRQ if requested. Clearing this bit is undefined. */
                if (val & 0x10)
                    pci_set_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
                else if ((dev->io_regs[0x17] & 0x10) && !(val & 0x10))
                    pci_clear_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);

                /* Enable or disable I/O traps. */
                dev->io_regs[addr] = val;
                cmi8x38_remap_traps(dev);
            }

            /* Remap the legacy devices. */
            dev->io_regs[addr] = val;
            cmi8x38_remap_sb(dev);
            cmi8x38_remap_mpu(dev);
            break;

        case 0x18:
            if (dev->type == CMEDIA_CMI8338)
                val &= 0x0f;
            else
                val &= 0xdf;

            /* Update the TDMA position update interrupt's sample interval. */
            dev->tdma_irq_mask = 2047 >> ((val >> 2) & 3);
            break;

        case 0x19:
            if (dev->type == CMEDIA_CMI8338)
                return;
            else
                val &= 0xe0;
            break;

        case 0x1a:
            val &= 0xfd;

            /* Enable or disable the OPL. */
            dev->io_regs[addr] = val;
            cmi8x38_remap_opl(dev);
            break;

        case 0x1b:
            if (dev->type == CMEDIA_CMI8338)
                val &= 0xf4; /* bit 2 reserved, mpxplay driver expects writable */
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

            /* Enable or disable SBPro channel swapping. */
            dev->sb->dsp.sbleftright_default = !!(val & 0x02);

            /* Enable or disable SB16 mode. */
            dev->sb->dsp.sb_type = (val & 0x01) ? SBPRO2_DSP_302 : SB16_DSP_405;
            break;

        case 0x22:
        case 0x23:
            cmi8x38_sb_mixer_write(addr ^ 1, val, dev);
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
            if (dev->type != CMEDIA_CMI8338)
                mpu401_write(addr, val, dev->sb->mpu);
            return;

        case 0x50 ... 0x5f:
            if (dev->type != CMEDIA_CMI8338)
                dev->sb->opl.write(addr, val, dev->sb->opl.priv);
            return;

        case 0x80 ... 0x83:
        case 0x88 ... 0x8b:
            dev->io_regs[addr]                      = val;
            dev->dma[(addr & 0x78) >> 3].sample_ptr = *((uint32_t *) &dev->io_regs[addr & 0xfc]);
            return;

        case 0x84 ... 0x85:
        case 0x8c ... 0x8d:
            dev->io_regs[addr]                           = val;
            dev->dma[(addr & 0x78) >> 3].frame_count_dma = dev->dma[(addr & 0x78) >> 3].sample_count_out = *((uint16_t *) &dev->io_regs[addr & 0xfe]) + 1;
            return;

        case 0x86 ... 0x87:
        case 0x8e ... 0x8f:
            dev->io_regs[addr]                                = val;
            dev->dma[(addr & 0x78) >> 3].frame_count_fragment = *((uint16_t *) &dev->io_regs[addr & 0xfe]) + 1;
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

        case 0x25:
        case 0x26:
        case 0x70:
        case 0x71:
            break;

        default:
            return;
    }

    dev->io_regs[addr] = val;
}

static void
cmi8x38_remap(cmi8x38_t *dev)
{
    if (dev->io_base)
        io_removehandler(dev->io_base, 256, cmi8x38_read, NULL, NULL, cmi8x38_write, NULL, NULL, dev);

    dev->io_base = (dev->pci_regs[0x04] & 0x01) ? (dev->pci_regs[0x11] << 8) : 0;
    cmi8x38_log("CMI8x38: remap(%04X)\n", dev->io_base);

    if (dev->io_base)
        io_sethandler(dev->io_base, 256, cmi8x38_read, NULL, NULL, cmi8x38_write, NULL, NULL, dev);
}

static uint8_t
cmi8x38_pci_read(int func, int addr, void *priv)
{
    const cmi8x38_t *dev = (cmi8x38_t *) priv;
    uint8_t          ret = 0xff;

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

            /* Enable or disable the I/O BAR. */
            dev->pci_regs[addr] = val;
            cmi8x38_remap(dev);
            break;

        case 0x05:
            val &= 0x01;
            break;

        case 0x11:
            /* Remap the I/O BAR. */
            dev->pci_regs[addr] = val;
            cmi8x38_remap(dev);
            break;

        case 0x2c:
        case 0x2d:
        case 0x2e:
        case 0x2f:
            if (!(dev->io_regs[0x1a] & 0x01))
                return;
            break;

        case 0x40:
            if (dev->type == CMEDIA_CMI8338)
                val &= 0x0f;
            else
                return;
            break;

        case 0x0c:
        case 0x0d:
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
    const sb_ct1745_mixer_t *mixer = &dev->sb->mixer_sb16;
    int32_t                  l     = (dma->out_fl * mixer->voice_l) * mixer->master_l;
    int32_t                  r     = (dma->out_fr * mixer->voice_r) * mixer->master_r;

    for (; dma->pos < sound_pos_global; dma->pos++) {
        dma->buffer[dma->pos * 2]     = l;
        dma->buffer[dma->pos * 2 + 1] = r;
    }
}

static void
cmi8x38_dma_process(void *priv)
{
    cmi8x38_dma_t *dma = (cmi8x38_dma_t *) priv;
    cmi8x38_t     *dev = dma->dev;

    /* Stop if this DMA channel is not active. */
    uint8_t dma_bit = 0x01 << dma->id;
    if (!(dev->io_regs[0x02] & dma_bit)) {
        cmi8x38_log("CMI8x38: Stopping DMA %d due to inactive channel (%02X)\n", dma->id, dev->io_regs[0x02]);
        return;
    }

    /* Schedule next run. */
    timer_on_auto(&dma->dma_timer, dma->dma_latch);

    /* Process DMA if it's active, and the FIFO has room or is disabled. */
    uint8_t dma_status = dev->io_regs[0x00] >> dma->id;
    if (!(dma_status & 0x04) && (dma->always_run || ((dma->fifo_end - dma->fifo_pos) <= (sizeof(dma->fifo) - 4)))) {
        /* Start DMA if requested. */
        if (dma->restart) {
            /* Set up base address and counters.
               Nothing reads sample_count_out; it's implemented as an assumption. */
            dma->restart         = 0;
            dma->sample_ptr      = *((uint32_t *) &dev->io_regs[dma->reg]);
            dma->frame_count_dma = dma->sample_count_out = *((uint16_t *) &dev->io_regs[dma->reg | 0x4]) + 1;
            dma->frame_count_fragment                    = *((uint16_t *) &dev->io_regs[dma->reg | 0x6]) + 1;

            cmi8x38_log("CMI8x38: Starting DMA %d at %08X (count %04X fragment %04X)\n", dma->id, dma->sample_ptr, dma->frame_count_dma, dma->frame_count_fragment);
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
            /* Reset fragment counter. */
            dma->frame_count_fragment = *((uint16_t *) &dev->io_regs[dma->reg | 0x6]) + 1;
#ifdef ENABLE_CMI8X38_LOG
            if (dma->frame_count_fragment > 1) /* avoid log spam if fragment counting is unused, like on the newer WDM drivers (cmudax3) */
                cmi8x38_log("CMI8x38: DMA %d fragment size reached at %04X frames left", dma->id, dma->frame_count_dma - 1);
#endif
            /* Fire interrupt if requested. */
            if (dev->io_regs[0x0e] & dma_bit) {
#ifdef ENABLE_CMI8X38_LOG
                if (dma->frame_count_fragment > 1)
                    cmi8x38_log(", firing interrupt\n");
#endif
                /* Set channel interrupt flag. */
                dev->io_regs[0x10] |= dma_bit;

                /* Fire interrupt. */
                cmi8x38_update_irqs(dev);
            } else {
#ifdef ENABLE_CMI8X38_LOG
                if (dma->frame_count_fragment > 1)
                    cmi8x38_log("\n");
#endif
            }
        }

        /* Check if the buffer's end was reached. */
        if (--dma->frame_count_dma <= 0) {
            dma->frame_count_dma = 0;
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
    cmi8x38_t     *dev = dma->dev;
    int16_t       *out_l;
    int16_t       *out_r;
    int16_t       *out_ol;
    int16_t       *out_or; /* o = opposite */

    /* Schedule next run if playback is enabled. */
    if (dma->playback_enabled)
        timer_advance_u64(&dma->poll_timer, dma->timer_latch);

    /* Update audio buffer. */
    cmi8x38_update(dev, dma);

    /* Swap stereo pair if this is the rear DMA channel according to ENDBDAC and XCHGDAC. */
    if ((dev->io_regs[0x1a] & 0x80) && (!!(dev->io_regs[0x1a] & 0x40) ^ dma->id)) {
        out_l  = &dma->out_rl;
        out_r  = &dma->out_rr;
        out_ol = &dma->out_fl;
        out_or = &dma->out_fr;
    } else {
        out_l  = &dma->out_fl;
        out_r  = &dma->out_fr;
        out_ol = &dma->out_rl;
        out_or = &dma->out_rr;
    }
    *out_ol = *out_or = dma->out_c = dma->out_lfe = 0;

    /* Feed next sample from the FIFO. */
    switch ((dev->io_regs[0x08] >> (dma->id << 1)) & 0x03) {
        case 0x00: /* Mono, 8-bit PCM */
            if ((dma->fifo_end - dma->fifo_pos) >= 1) {
                *out_l = *out_r = (dma->fifo[dma->fifo_pos++ & (sizeof(dma->fifo) - 1)] ^ 0x80) << 8;
                dma->sample_count_out--;
                goto n4spk3d;
            }
            break;

        case 0x01: /* Stereo, 8-bit PCM */
            if ((dma->fifo_end - dma->fifo_pos) >= 2) {
                *out_l = (dma->fifo[dma->fifo_pos++ & (sizeof(dma->fifo) - 1)] ^ 0x80) << 8;
                *out_r = (dma->fifo[dma->fifo_pos++ & (sizeof(dma->fifo) - 1)] ^ 0x80) << 8;
                dma->sample_count_out -= 2;
                goto n4spk3d;
            }
            break;

        case 0x02: /* Mono, 16-bit PCM */
            if ((dma->fifo_end - dma->fifo_pos) >= 2) {
                *out_l = *out_r = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
                dma->fifo_pos += 2;
                dma->sample_count_out -= 2;
                goto n4spk3d;
            }
            break;

        case 0x03: /* Stereo, 16-bit PCM, with multi-channel capability */
            switch (dma->channels) {
                case 2:
                    if ((dma->fifo_end - dma->fifo_pos) >= 4) {
                        *out_l = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
                        dma->fifo_pos += 2;
                        *out_r = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
                        dma->fifo_pos += 2;
                        dma->sample_count_out -= 4;
                        goto n4spk3d;
                    }
                    break;

                case 4:
                    if ((dma->fifo_end - dma->fifo_pos) >= 8) {
                        dma->out_fl = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
                        dma->fifo_pos += 2;
                        dma->out_fr = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
                        dma->fifo_pos += 2;
                        dma->out_rl = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
                        dma->fifo_pos += 2;
                        dma->out_rr = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
                        dma->fifo_pos += 2;
                        dma->sample_count_out -= 8;
                        return;
                    }
                    break;

                case 5: /* not supported by WDM and Linux drivers; channel layout assumed */
                    if ((dma->fifo_end - dma->fifo_pos) >= 10) {
                        dma->out_fl = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
                        dma->fifo_pos += 2;
                        dma->out_fr = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
                        dma->fifo_pos += 2;
                        dma->out_rl = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
                        dma->fifo_pos += 2;
                        dma->out_rr = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
                        dma->fifo_pos += 2;
                        dma->out_c = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
                        dma->fifo_pos += 2;
                        dma->sample_count_out -= 10;
                        return;
                    }
                    break;

                case 6:
                    if ((dma->fifo_end - dma->fifo_pos) >= 12) {
                        dma->out_fl = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
                        dma->fifo_pos += 2;
                        dma->out_fr = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
                        dma->fifo_pos += 2;
                        dma->out_rl = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
                        dma->fifo_pos += 2;
                        dma->out_rr = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
                        dma->fifo_pos += 2;
                        dma->out_c = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
                        dma->fifo_pos += 2;
                        dma->out_lfe = *((uint16_t *) &dma->fifo[dma->fifo_pos & (sizeof(dma->fifo) - 1)]);
                        dma->fifo_pos += 2;
                        dma->sample_count_out -= 12;
                        return;
                    }
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }

    /* Feed silence if the FIFO is empty. */
    *out_l = *out_r = 0;

    /* Stop playback if DMA is disabled. */
    if ((*((uint32_t *) &dev->io_regs[0x00]) & (0x00010001 << dma->id)) != (0x00010000 << dma->id)) {
        cmi8x38_log("CMI8x38: Stopping playback of DMA channel %d\n", dma->id);
        dma->playback_enabled = 0;
    }

    return;
n4spk3d:
    /* Mirror front and rear channels if requested. */
    if (dev->io_regs[0x1b] & 0x04) {
        *out_ol = *out_l;
        *out_or = *out_r;
    }
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
    double     freq;
    uint8_t    dsr = dev->io_regs[0x09];
    uint8_t    freqreg = dev->io_regs[0x05] >> 2;
    uint8_t    chfmt45 = dev->io_regs[0x0b];
    uint8_t    chfmt6 = dev->io_regs[0x15];

#ifdef ENABLE_CMI8X38_LOG
    char buf[256];
    sprintf(buf, "%02X-%02X-%02X-%02X", dsr, freqreg, chfmt45, chfmt6);
#endif

    /* CMI8338 claims the frequency controls are for DAC (playback) and ADC (recording)
       respectively, while CMI8738 claims they're for channel 0 and channel 1. The Linux
       driver just assumes the latter definition, so that's what we're going to use here. */
    for (int i = 0; i < (sizeof(dev->dma) / sizeof(dev->dma[0])); i++) {
        /* More confusion. The Linux driver implies the sample rate doubling
           bits take precedence over any configured sample rate. 128K with both
           doubling bits set is also supported there, but that's for newer chips. */
        switch (dsr & 0x03) {
            case 0x01:
                freq = 88200.0;
                break;
            case 0x02:
                freq = 96000.0;
                break;
#if 0
            case 0x03:
                freq = 128000.0;
                break;
#endif
            default:
                freq = freqs[freqreg & 0x07];
                break;
        }

        /* Set polling timer period. */
        freq                    = 1000000.0 / freq;
        dev->dma[i].timer_latch = (uint64_t) ((double) TIMER_USEC * freq);

        /* Calculate channel count and set DMA timer period. */
        if ((dev->type == CMEDIA_CMI8338) || (i == 0)) { /* multi-channel requires channel 1 */
stereo:
            dev->dma[i].channels = 2;
        } else {
            if (chfmt45 & 0x80)
                dev->dma[i].channels = (chfmt6 & 0x80) ? 6 : 5;
            else if (chfmt45 & 0x20)
                dev->dma[i].channels = 4;
            else
                goto stereo;
        }
        dev->dma[i].dma_latch = freq / dev->dma[i].channels; /* frequency / approximately(dwords * 2) */

        /* Shift sample rate configuration registers. */
#ifdef ENABLE_CMI8X38_LOG
        sprintf(&buf[strlen(buf)], " %d:%X-%X-%.0f-%dC", i, dsr & 0x03, freqreg & 0x07, 1000000.0 / freq, dev->dma[i].channels);
#endif
        dsr >>= 2;
        freqreg >>= 3;
    }

#ifdef ENABLE_CMI8X38_LOG
    if (cmi8x38_do_log)
        ui_sb_bugui(buf);
#endif
}

static void
cmi8x38_reset(void *priv)
{
    cmi8x38_t *dev = (cmi8x38_t *) priv;

    /* Reset PCI configuration registers. */
    memset(dev->pci_regs, 0, sizeof(dev->pci_regs));
    dev->pci_regs[0x00] = 0xf6;
    dev->pci_regs[0x01] = 0x13;
    dev->pci_regs[0x02] = dev->type;
    dev->pci_regs[0x03] = 0x01;
    dev->pci_regs[0x06] = (dev->type == CMEDIA_CMI8338) ? 0x80 : 0x10;
    dev->pci_regs[0x07] = 0x02;
    dev->pci_regs[0x08] = 0x10;
    dev->pci_regs[0x0a] = 0x01;
    dev->pci_regs[0x0b] = 0x04;
    dev->pci_regs[0x0d] = 0x20;
    dev->pci_regs[0x10] = 0x01;
    dev->pci_regs[0x2c] = 0xf6;
    dev->pci_regs[0x2d] = 0x13;
    if (dev->type == CMEDIA_CMI8338) {
        dev->pci_regs[0x2e] = 0xff;
        dev->pci_regs[0x2f] = 0xff;
    } else {
        dev->pci_regs[0x2e] = dev->type;
        dev->pci_regs[0x2f] = 0x01;
        dev->pci_regs[0x34] = 0x40;
    }
    dev->pci_regs[0x3d] = 0x01;
    dev->pci_regs[0x3e] = 0x02;
    dev->pci_regs[0x3f] = 0x18;

    /* Reset I/O space registers. */
    memset(dev->io_regs, 0, sizeof(dev->io_regs));
    dev->io_regs[0x0b] = (dev->type >> 8) & 0x1f;
    dev->io_regs[0x0f] = dev->type >> 16;
    dev->tdma_irq_mask = 2047;

    /* Reset I/O mappings. */
    cmi8x38_remap(dev);
    cmi8x38_remap_sb(dev);
    /* remap_mpu and remap_opl called by remap_traps */
    cmi8x38_remap_traps(dev);

    /* Reset DMA channels. */
    for (int i = 0; i < (sizeof(dev->dma) / sizeof(dev->dma[0])); i++) {
        dev->dma[i].playback_enabled = 0;
        dev->dma[i].fifo_pos = dev->dma[i].fifo_end = 0;
        memset(dev->dma[i].fifo, 0, sizeof(dev->dma[i].fifo));
    }

    /* Reset TDMA channel. */
    dev->tdma_8    = 1;
    dev->tdma_16   = 5;
    dev->tdma_mask = 0;

    /* Reset Sound Blaster 16 mixer. */
    sb_ct1745_mixer_reset(dev->sb);
}

static void *
cmi8x38_init(const device_t *info)
{
    cmi8x38_t *dev = calloc(1, sizeof(cmi8x38_t));

    /* Set the chip type. */
    if ((info->local == CMEDIA_CMI8738_6CH) && !device_get_config_int("six_channel"))
        dev->type = CMEDIA_CMI8738_4CH;
    else
        dev->type = info->local;
    cmi8x38_log("CMI8x38: init(%06X)\n", dev->type);

    /* Initialize Sound Blaster 16. */
    dev->sb                           = device_add_inst(device_get_config_int("receive_input") ? &sb_16_compat_device : &sb_16_compat_nompu_device, 1);
    dev->sb->opl_enabled              = 1; /* let snd_sb.c handle the OPL3 */
    dev->sb->mixer_sb16.output_filter = 0; /* no output filtering */

    /* Initialize legacy interrupt and DMA handlers. */
    mpu401_irq_attach(dev->sb->mpu, cmi8x38_mpu_irq_update, cmi8x38_mpu_irq_pending, dev);
    sb_dsp_irq_attach(&dev->sb->dsp, cmi8x38_sb_irq_update, dev);
    sb_dsp_dma_attach(&dev->sb->dsp, cmi8x38_sb_dma_readb, cmi8x38_sb_dma_readw, cmi8x38_sb_dma_writeb, cmi8x38_sb_dma_writew, dev);
    io_sethandler(0x00, 8, NULL, NULL, NULL, cmi8x38_dma_write, NULL, NULL, dev);
    io_sethandler(0x08, 8, NULL, NULL, NULL, cmi8x38_dma_mask_write, NULL, NULL, dev);
    io_sethandler(0xc0, 16, NULL, NULL, NULL, cmi8x38_dma_write, NULL, NULL, dev);
    io_sethandler(0xd0, 16, NULL, NULL, NULL, cmi8x38_dma_mask_write, NULL, NULL, dev);

    /* Initialize DMA channels. */
    for (int i = 0; i < (sizeof(dev->dma) / sizeof(dev->dma[0])); i++) {
        dev->dma[i].id  = i;
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

    /* Initialize I/O traps. */
    if (dev->type == CMEDIA_CMI8338) {
        dev->io_traps[TRAP_DMA] = io_trap_add(cmi8338_io_trap, dev);
        dev->io_traps[TRAP_PIC] = io_trap_add(cmi8338_io_trap, dev);
        dev->io_traps[TRAP_OPL] = io_trap_add(cmi8338_io_trap, dev);
        dev->io_traps[TRAP_MPU] = io_trap_add(cmi8338_io_trap, dev);
    }

    /* Add PCI card. */
    pci_add_card((info->local & (1 << 13)) ? PCI_ADD_SOUND : PCI_ADD_NORMAL, cmi8x38_pci_read, cmi8x38_pci_write, dev, &dev->pci_slot);

    /* Perform initial reset. */
    cmi8x38_reset(dev);

    return dev;
}

static void
cmi8x38_close(void *priv)
{
    cmi8x38_t *dev = (cmi8x38_t *) priv;

    cmi8x38_log("CMI8x38: close()\n");

    for (int i = 0; i < TRAP_MAX; i++)
        io_trap_remove(dev->io_traps[i]);

    free(dev);
}

static const device_config_t cmi8x38_config[] = {
  // clang-format off
    {
        .name           = "receive_input",
        .description    = "Receive MIDI input",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_config_t cmi8738_config[] = {
  // clang-format off
    {
        .name           = "six_channel",
        .description    = "6CH variant (6-channel)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "receive_input",
        .description    = "Receive MIDI input",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t cmi8338_device = {
    .name          = "C-Media CMI8338",
    .internal_name = "cmi8338",
    .flags         = DEVICE_PCI,
    .local         = CMEDIA_CMI8338,
    .init          = cmi8x38_init,
    .close         = cmi8x38_close,
    .reset         = cmi8x38_reset,
    .available     = NULL,
    .speed_changed = cmi8x38_speed_changed,
    .force_redraw  = NULL,
    .config        = cmi8x38_config
};

const device_t cmi8338_onboard_device = {
    .name          = "C-Media CMI8338 (On-Board)",
    .internal_name = "cmi8338_onboard",
    .flags         = DEVICE_PCI,
    .local         = CMEDIA_CMI8338 | (1 << 13),
    .init          = cmi8x38_init,
    .close         = cmi8x38_close,
    .reset         = cmi8x38_reset,
    .available     = NULL,
    .speed_changed = cmi8x38_speed_changed,
    .force_redraw  = NULL,
    .config        = cmi8x38_config
};

const device_t cmi8738_device = {
    .name          = "C-Media CMI8738",
    .internal_name = "cmi8738",
    .flags         = DEVICE_PCI,
    .local         = CMEDIA_CMI8738_6CH,
    .init          = cmi8x38_init,
    .close         = cmi8x38_close,
    .reset         = cmi8x38_reset,
    .available     = NULL,
    .speed_changed = cmi8x38_speed_changed,
    .force_redraw  = NULL,
    .config        = cmi8738_config
};

const device_t cmi8738_onboard_device = {
    .name          = "C-Media CMI8738 (On-Board)",
    .internal_name = "cmi8738_onboard",
    .flags         = DEVICE_PCI,
    .local         = CMEDIA_CMI8738_4CH | (1 << 13),
    .init          = cmi8x38_init,
    .close         = cmi8x38_close,
    .reset         = cmi8x38_reset,
    .available     = NULL,
    .speed_changed = cmi8x38_speed_changed,
    .force_redraw  = NULL,
    .config        = cmi8x38_config
};

const device_t cmi8738_6ch_onboard_device = {
    .name          = "C-Media CMI8738-6CH (On-Board)",
    .internal_name = "cmi8738_6ch_onboard",
    .flags         = DEVICE_PCI,
    .local         = CMEDIA_CMI8738_6CH | (1 << 13),
    .init          = cmi8x38_init,
    .close         = cmi8x38_close,
    .reset         = cmi8x38_reset,
    .available     = NULL,
    .speed_changed = cmi8x38_speed_changed,
    .force_redraw  = NULL,
    .config        = cmi8x38_config
};
