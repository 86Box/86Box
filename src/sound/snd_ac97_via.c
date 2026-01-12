/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VIA AC'97 audio controller emulation.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2021-2025 RichardG.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/snd_ac97.h>
#include <86box/sound.h>
#include <86box/timer.h>
#include <86box/plat_unused.h>
#include "cpu.h"

typedef struct ac97_via_sgd_t {
    uint8_t            id;
    uint8_t            always_run;
    struct _ac97_via_ *dev;

    uint32_t entry_ptr;
    uint32_t sample_ptr;
    uint32_t fifo_pos;
    uint32_t fifo_end;
    int32_t  sample_count;
    uint8_t  entry_flags;
    uint8_t  fifo[32];
    uint8_t  restart;

    int16_t  out_l;
    int16_t  out_r;
    int      vol_l;
    int      vol_r;
    int      pos;
    int32_t  buffer[SOUNDBUFLEN * 2];
    uint64_t timer_latch;

    pc_timer_t dma_timer;
    pc_timer_t poll_timer;
} ac97_via_sgd_t;

typedef struct _ac97_via_ {
    uint16_t audio_sgd_base;
    uint16_t audio_codec_base;
    uint16_t modem_sgd_base;
    uint16_t modem_codec_base;
    uint8_t  sgd_regs[256];
    uint8_t  pcm_enabled : 1;
    uint8_t  fm_enabled : 1;
    uint8_t  modem_enabled : 1;
    uint8_t  vsr_enabled : 1;
    uint8_t  codec_shadow[256];
    uint8_t  pci_slot;
    uint8_t  irq_state;
    int      irq_pin;

    ac97_codec_t  *codec[2];
    ac97_codec_t  *audio_codec;
    ac97_codec_t  *modem_codec;
    ac97_via_sgd_t sgd[6];

    int master_vol_l;
    int master_vol_r;
    int cd_vol_l;
    int cd_vol_r;
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
#    define ac97_via_log(fmt, ...)
#endif

static void ac97_via_sgd_process(void *priv);
static void ac97_via_update_codec(ac97_via_t *dev);
static void ac97_via_speed_changed(void *priv);
static void ac97_via_filter_cd_audio(int channel, double *buffer, void *priv);

void
ac97_via_set_slot(void *priv, int slot, int irq_pin)
{
    ac97_via_t *dev = (ac97_via_t *) priv;

    ac97_via_log("AC97 VIA: set_slot(%d, %d)\n", slot, irq_pin);

    dev->pci_slot = slot;
    dev->irq_pin  = irq_pin;
}

uint8_t
ac97_via_read_status(void *priv)
{
    const ac97_via_t *dev = (ac97_via_t *) priv;
    uint8_t           ret = 0x00;

    /* Flag each codec as ready if present. */
    for (uint8_t i = 0; i < (sizeof(dev->codec) / sizeof(dev->codec[0])); i++) {
        if (dev->codec[i])
            ret |= 0x01 << (i << 1);
    }

    ac97_via_log("AC97 VIA: read_status() = %02X\n", ret);

    return ret;
}

void
ac97_via_write_control(void *priv, uint8_t val)
{
    ac97_via_t *dev = (ac97_via_t *) priv;
    uint8_t     i;

    ac97_via_log("AC97 VIA: write_control(%02X)\n", val);

    /* Reset codecs if requested. */
    if (!(val & 0x40)) {
        for (i = 0; i < (sizeof(dev->codec) / sizeof(dev->codec[0])); i++) {
            if (dev->codec[i])
                ac97_codec_reset(dev->codec[i]);
        }
    }

    /* Set the variable sample rate flag. */
    dev->vsr_enabled = (val & 0xf8) == 0xc8;

    /* Start or stop PCM playback. */
    i = (val & 0xf4) == 0xc4;
    if (i && !dev->pcm_enabled)
        timer_advance_u64(&dev->sgd[0].poll_timer, dev->sgd[0].timer_latch);
    dev->pcm_enabled = i;

    /* Start or stop FM playback. */
    i = (val & 0xf2) == 0xc2;
    if (i && !dev->fm_enabled)
        timer_advance_u64(&dev->sgd[2].poll_timer, dev->sgd[2].timer_latch);
    dev->fm_enabled = i;

    /* Update audio codec state. */
    ac97_via_update_codec(dev);
}

static void
ac97_via_update_irqs(ac97_via_t *dev)
{
    /* Check interrupt flags in all SGDs. */
    for (uint8_t i = 0x00; i < ((sizeof(dev->sgd) / sizeof(dev->sgd[0])) << 4); i += 0x10) {
        /* Stop immediately if any flag is set. Doing it this way optimizes
           rising edges for the playback SGD (0 - first to be checked). */
        if (dev->sgd_regs[i] & (dev->sgd_regs[i | 0x2] & 0x03)) {
            pci_set_irq(dev->pci_slot, dev->irq_pin, &dev->irq_state);
            return;
        }
    }

    pci_clear_irq(dev->pci_slot, dev->irq_pin, &dev->irq_state);
}

static void
ac97_via_update_codec(ac97_via_t *dev)
{
    /* Update volumes according to codec registers. */
    if (dev->audio_codec) {
        ac97_codec_getattn(dev->audio_codec, 0x02, &dev->master_vol_l, &dev->master_vol_r);
        ac97_codec_getattn(dev->audio_codec, 0x18, &dev->sgd[0].vol_l, &dev->sgd[0].vol_r);
        ac97_codec_getattn(dev->audio_codec, 0x18, &dev->sgd[2].vol_l, &dev->sgd[2].vol_r); /* VIAFMTSR sets Master, CD and PCM volumes to 0 dB */
        ac97_codec_getattn(dev->audio_codec, 0x12, &dev->cd_vol_l, &dev->cd_vol_r);
        ac97_codec_getattn(dev->audio_codec, 0x0c, &dev->sgd[4].vol_l, &dev->sgd[4].vol_r);
    }

    /* Update sample rate according to codec registers and the variable sample rate flag. */
    ac97_via_speed_changed(dev);
}

static uint8_t
ac97_via_sgd_read(uint16_t addr, void *priv)
{
    const ac97_via_t *dev = (ac97_via_t *) priv;
#ifdef ENABLE_AC97_VIA_LOG
    uint8_t modem         = (addr & 0xff00) == dev->modem_sgd_base;
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
                ret = (dev->sgd_regs[0x00] & 0x01);
                ret |= (dev->sgd_regs[0x10] & 0x01) << 1;
                ret |= (dev->sgd_regs[0x20] & 0x01) << 2;

                ret |= (dev->sgd_regs[0x00] & 0x02) << 3;
                ret |= (dev->sgd_regs[0x10] & 0x02) << 4;
                ret |= (dev->sgd_regs[0x20] & 0x02) << 5;
                break;

            case 0x85:
                ret = (dev->sgd_regs[0x00] & 0x04) >> 2;
                ret |= (dev->sgd_regs[0x10] & 0x04) >> 1;
                ret |= (dev->sgd_regs[0x20] & 0x04);

                ret |= (dev->sgd_regs[0x00] & 0x80) >> 3;
                ret |= (dev->sgd_regs[0x10] & 0x80) >> 2;
                ret |= (dev->sgd_regs[0x20] & 0x80) >> 1;
                break;

            case 0x86:
                ret = (dev->sgd_regs[0x40] & 0x01);
                ret |= (dev->sgd_regs[0x50] & 0x01) << 1;

                ret |= (dev->sgd_regs[0x40] & 0x02) << 3;
                ret |= (dev->sgd_regs[0x50] & 0x02) << 4;
                break;

            case 0x87:
                ret = (dev->sgd_regs[0x40] & 0x04) >> 2;
                ret |= (dev->sgd_regs[0x50] & 0x04) >> 1;

                ret |= (dev->sgd_regs[0x40] & 0x80) >> 3;
                ret |= (dev->sgd_regs[0x50] & 0x80) >> 2;
                break;

            default:
                ret = dev->sgd_regs[addr];
                break;
        }
    }

    ac97_via_log("[%04X:%08X] [%i] AC97 VIA %d: sgd_read(%02X) = %02X\n", CS, cpu_state.pc, msw & 1, modem, addr, ret);

    return ret;
}

static void
ac97_via_sgd_write(uint16_t addr, uint8_t val, void *priv)
{
    ac97_via_t   *dev   = (ac97_via_t *) priv;
    uint8_t       modem = (addr & 0xff00) == dev->modem_sgd_base;
    uint8_t       i;
    ac97_codec_t *codec;
    addr &= 0xff;

    ac97_via_log("[%04X:%08X] [%i] AC97 VIA %d: sgd_write(%02X, %02X)\n", CS, cpu_state.pc, msw & 1, modem, addr, val);

    // if ((CS == 0x10000) && (cpu_state.pc == 0x000073d1))

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
                        dev->sgd_regs[addr & 0xf0] = (dev->sgd_regs[addr & 0xf0] & ~0x47) | 0x80;

                        /* Start at the specified entry pointer. */
                        dev->sgd[addr >> 4].entry_ptr  = AS_U32(dev->sgd_regs[(addr & 0xf0) | 0x4]) & 0xfffffffe;
                        dev->sgd[addr >> 4].restart    = 2;

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

            case 0x3:
            case 0x8 ... 0xf:
                /* Read-only registers. */
                return;

            default:
                break;
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
                i     = !!(dev->sgd_regs[0x83] & 0x40);
                codec = dev->codec[i];

                /* Keep value in register if this codec is not present. */
                if (codec) {
                    /* Set audio and modem codecs according to type. */
                    if (codec->regs[0x3c >> 1]) {
                        if (!dev->modem_codec) {
                            dev->modem_codec = codec;
                            if (val & 0x80)
                                ac97_via_update_codec(dev);
                        }
                        /* Start modem pollers. */
                        if (!dev->modem_enabled) {
                            dev->modem_enabled = 1;
                            timer_advance_u64(&dev->sgd[4].poll_timer, dev->sgd[4].timer_latch);
                            timer_advance_u64(&dev->sgd[5].poll_timer, dev->sgd[5].timer_latch);
                        }
                    } else if (!dev->audio_codec) {
                        dev->audio_codec = codec;
                        if (val & 0x80)
                            ac97_via_update_codec(dev);
                    }

                    /* Read from or write to codec. */
                    if (val & 0x80) {
                        if (val & 1) /* return 0x0000 on unaligned reads (real 686B behavior) */
                            AS_U16(dev->sgd_regs[0x80]) = 0x0000;
                        else
                            AS_U16(dev->codec_shadow[(i << 7) | (val & 0x7f)]) = AS_U16(dev->sgd_regs[0x80]) = ac97_codec_readw(codec, val);

                        /* Flag data/status/index for this codec as valid. */
                        dev->sgd_regs[0x83] |= 0x02 << (i << 1);
                    } else if (!(val & 1)) { /* do nothing on unaligned writes */
                        ac97_codec_writew(codec, val,
                                          AS_U16(dev->codec_shadow[(i << 7) | val]) = AS_U16(dev->sgd_regs[0x80]));

                        /* Update audio codec state. */
                        ac97_via_update_codec(dev);

                        /* Set up CD audio filter if CD volume was written to. Setting it
                           up at init prevents CD audio from working on other cards, but
                           this works as the CD channel is muted by default per AC97 spec. */
                        if (!i && (val == 0x12))
                            sound_set_cd_audio_filter(ac97_via_filter_cd_audio, dev);
                    }
                }
                break;

            case 0x83:
                /* Clear RWC status bits. */
                val = ((dev->sgd_regs[addr] & 0x3f) & ~(val & 0x0a)) | (val & 0xc0);
                break;

            case 0x88 ... 0x89:
                dev->sgd_regs[addr] = val;

                /* Send GPO to codec. */
                for (uint8_t i = 0; i < (sizeof(dev->codec) / sizeof(dev->codec[0])); i++) {
                    if (dev->codec[i])
                        ac97_codec_setgpo(dev->codec[i], AS_U16(dev->sgd_regs[0x88]));
                }
                return;

            case 0x8a ... 0x8b:
                /* Clear RWC status bits. */
                val = dev->sgd_regs[addr] & ~val;
                break;

            case 0x8c ... 0x8d:
                return;

            default:
                break;
        }
    }

    dev->sgd_regs[addr] = val;
}

static void
ac97_via_sgd_writew(uint16_t addr, uint16_t val, void *priv)
{
    if ((addr & 0xfe) == 0x82) {
        /* Invert order on writes to 82-83 to ensure the correct codec ID is set and
           any status bits are cleared before performing the codec register operation. */
        ac97_via_sgd_write(addr + 1, val >> 8,   priv);
        ac97_via_sgd_write(addr,     val & 0xff, priv);
    } else {
        ac97_via_sgd_write(addr,     val & 0xff, priv);
        ac97_via_sgd_write(addr + 1, val >> 8,   priv);
    }
}

static void
ac97_via_sgd_writel(uint16_t addr, uint32_t val, void *priv)
{
    ac97_via_sgd_write(addr,     val & 0xff, priv);
    ac97_via_sgd_write(addr + 1, val >> 8,   priv);
    if ((addr & 0xfc) == 0x80) {
        /* Invert order on writes to 82-83 to ensure the correct codec ID is set and
           any status bits are cleared before performing the codec register operation. */
        ac97_via_sgd_write(addr + 3, val >> 24, priv);
        ac97_via_sgd_write(addr + 2, val >> 16, priv);
    } else {
        ac97_via_sgd_write(addr + 2, val >> 16, priv);
        ac97_via_sgd_write(addr + 3, val >> 24, priv);
    }
}

void
ac97_via_remap_audio_sgd(void *priv, uint16_t new_io_base, uint8_t enable)
{
    ac97_via_t *dev = (ac97_via_t *) priv;

    if (dev->audio_sgd_base)
        io_removehandler(dev->audio_sgd_base, 256, ac97_via_sgd_read, NULL, NULL, ac97_via_sgd_write, ac97_via_sgd_writew, ac97_via_sgd_writel, dev);

    dev->audio_sgd_base = new_io_base;

    if (dev->audio_sgd_base && enable)
        io_sethandler(dev->audio_sgd_base, 256, ac97_via_sgd_read, NULL, NULL, ac97_via_sgd_write, ac97_via_sgd_writew, ac97_via_sgd_writel, dev);
}

void
ac97_via_remap_modem_sgd(void *priv, uint16_t new_io_base, uint8_t enable)
{
    ac97_via_t *dev = (ac97_via_t *) priv;

    if (dev->modem_sgd_base)
        io_removehandler(dev->modem_sgd_base, 256, ac97_via_sgd_read, NULL, NULL, ac97_via_sgd_write, ac97_via_sgd_writew, ac97_via_sgd_writel, dev);

    dev->modem_sgd_base = new_io_base;

    if (dev->modem_sgd_base && enable)
        io_sethandler(dev->modem_sgd_base, 256, ac97_via_sgd_read, NULL, NULL, ac97_via_sgd_write, ac97_via_sgd_writew, ac97_via_sgd_writel, dev);
}

uint8_t
ac97_via_codec_read(uint16_t addr, void *priv)
{
    const ac97_via_t *dev   = (ac97_via_t *) priv;
#ifdef ENABLE_AC97_VIA_LOG
    uint8_t           modem = (addr & 0xff00) == dev->modem_codec_base;
#endif
    uint8_t           ret = 0xff;

    addr &= 0xff;

    ret = dev->codec_shadow[addr];

    ac97_via_log("[%04X:%08X] [%i] AC97 VIA %d: codec_read(%02X) = %02X\n", CS, cpu_state.pc, msw & 1, modem, addr, ret);

    return ret;
}

void
ac97_via_codec_write(uint16_t addr, uint8_t val, void *priv)
{
    ac97_via_t *dev   = (ac97_via_t *) priv;
#ifdef ENABLE_AC97_VIA_LOG
    uint8_t     modem = (addr & 0xff00) == dev->modem_codec_base;
#endif
    addr &= 0xff;

    ac97_via_log("[%04X:%08X] [%i] AC97 VIA %d: codec_write(%02X, %02X)\n", CS, cpu_state.pc, msw & 1, modem, addr, val);

    /* Unknown behavior, maybe it does write to the shadow registers? */
    dev->codec_shadow[addr] = val;
}

void
ac97_via_remap_audio_codec(void *priv, uint16_t new_io_base, uint8_t enable)
{
    ac97_via_t *dev = (ac97_via_t *) priv;

    if (dev->audio_codec_base)
        io_removehandler(dev->audio_codec_base, 4, ac97_via_codec_read, NULL, NULL, ac97_via_codec_write, NULL, NULL, dev);

    dev->audio_codec_base = new_io_base;

    if (dev->audio_codec_base && enable)
        io_sethandler(dev->audio_codec_base, 4, ac97_via_codec_read, NULL, NULL, ac97_via_codec_write, NULL, NULL, dev);
}

void
ac97_via_remap_modem_codec(void *priv, uint16_t new_io_base, uint8_t enable)
{
    ac97_via_t *dev = (ac97_via_t *) priv;

    if (dev->modem_codec_base)
        io_removehandler(dev->modem_codec_base, 4, ac97_via_codec_read, NULL, NULL, ac97_via_codec_write, NULL, NULL, dev);

    dev->modem_codec_base = new_io_base;

    if (dev->modem_codec_base && enable)
        io_sethandler(dev->modem_codec_base, 4, ac97_via_codec_read, NULL, NULL, ac97_via_codec_write, NULL, NULL, dev);
}

static void
ac97_via_update_stereo(ac97_via_t *dev, ac97_via_sgd_t *sgd)
{
#ifdef OLD_CODE
    int32_t l = (((sgd->out_l * sgd->vol_l) >> 15) * dev->master_vol_l) >> 15;
    int32_t r = (((sgd->out_r * sgd->vol_r) >> 15) * dev->master_vol_r) >> 15;
#else
    int32_t l = (((sgd->out_l * sgd->vol_l) / 208925) * dev->master_vol_l) >> 15;
    int32_t r = (((sgd->out_r * sgd->vol_r) / 208925) * dev->master_vol_r) >> 15;
#endif

    if (l < -32768)
        l = -32768;
    else if (l > 32767)
        l = 32767;
    if (r < -32768)
        r = -32768;
    else if (r > 32767)
        r = 32767;

    for (; sgd->pos < sound_pos_global; sgd->pos++) {
        sgd->buffer[sgd->pos * 2]     = l;
        sgd->buffer[sgd->pos * 2 + 1] = r;
    }
}

static void
ac97_via_sgd_process(void *priv)
{
    ac97_via_sgd_t *sgd = (ac97_via_sgd_t *) priv;
    ac97_via_t     *dev = sgd->dev;

    /* Stop if this SGD is not active. */
    uint8_t sgd_status = dev->sgd_regs[sgd->id] & 0xc4;
    if (!(sgd_status & 0x80))
        return;

    /* Schedule next run. */
    timer_on_auto(&sgd->dma_timer, 1.0);

    /* Process SGD if it's active, and the FIFO has room or is disabled. */
    if (((sgd_status & 0xc7) == 0x80) && (sgd->always_run || ((sgd->fifo_end - sgd->fifo_pos) <= (sizeof(sgd->fifo) - 4)))) {
        /* Move on to the next block if no entry is present. */
        if (sgd->restart) {
            /* (Re)load entry pointer if required. */
            if (sgd->restart & 2)
                sgd->entry_ptr = AS_U32(dev->sgd_regs[sgd->id | 0x4]) & 0xfffffffe; /* TODO: probe real hardware - does "even addr" actually mean dword aligned? */
            sgd->restart = 0;

            /* Read entry. */
            // sgd->sample_ptr = mem_readl_phys(sgd->entry_ptr);
            dma_bm_read(sgd->entry_ptr, (uint8_t *) &sgd->sample_ptr, 4, 4);
            sgd->entry_ptr += 4;
            // sgd->sample_count = mem_readl_phys(sgd->entry_ptr);
            dma_bm_read(sgd->entry_ptr, (uint8_t *) &sgd->sample_count, 4, 4);
            sgd->entry_ptr += 4;
#ifdef ENABLE_AC97_VIA_LOG
            if (((sgd->sample_ptr == 0xffffffff) && (sgd->sample_count == 0xffffffff)) || ((sgd->sample_ptr == 0x00000000) && (sgd->sample_count == 0x00000000)))
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
            // mem_writel_phys(sgd->sample_ptr, AS_U32(sgd->fifo[sgd->fifo_end & (sizeof(sgd->fifo) - 1)]));
            dma_bm_write(sgd->sample_ptr, &sgd->fifo[sgd->fifo_end & (sizeof(sgd->fifo) - 1)], 4, 4);
        } else {
            /* Read channel: write data to FIFO. */
            // AS_U32(sgd->fifo[sgd->fifo_end & (sizeof(sgd->fifo) - 1)]) = mem_readl_phys(sgd->sample_ptr);
            dma_bm_read(sgd->sample_ptr, &sgd->fifo[sgd->fifo_end & (sizeof(sgd->fifo) - 1)], 4, 4);
        }
        sgd->fifo_end += 4;
        sgd->sample_ptr += 4;
        sgd->sample_count -= 4;

        /* Check if we've hit the end of this block. */
        if (sgd->sample_count <= 0) {
            ac97_via_log("AC97 VIA: Ending SGD %d block", sgd->id >> 4);

            /* Move on to the next block on the next run, unless overridden below. */
            sgd->restart = 1;

            if (sgd->entry_flags & 0x20) {
                ac97_via_log(" with STOP");

                /* Raise STOP to pause SGD. */
                dev->sgd_regs[sgd->id] |= 0x04;
            }

            if (sgd->entry_flags & 0x40) {
                ac97_via_log(" with FLAG");

                /* Raise FLAG to pause SGD. */
                dev->sgd_regs[sgd->id] |= 0x01;

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

                    /* Go back to the starting block on the next run. */
                    sgd->restart = 2;
                } else {
                    ac97_via_log(" finish");

                    /* Terminate SGD. */
                    dev->sgd_regs[sgd->id] &= ~0x80;
                }
            }
            ac97_via_log("\n");

            /* Fire any requested status interrupts. */
            ac97_via_update_irqs(dev);
        }
    }
}

static void
ac97_via_poll_stereo(void *priv)
{
    ac97_via_sgd_t *sgd = (ac97_via_sgd_t *) priv;
    ac97_via_t     *dev = sgd->dev;

    /* Schedule next run if PCM playback is enabled. */
    if (dev->pcm_enabled)
        timer_advance_u64(&sgd->poll_timer, sgd->timer_latch);

    /* Update stereo audio buffer. */
    ac97_via_update_stereo(dev, sgd);

    /* Feed next sample from the FIFO. */
    switch (dev->sgd_regs[sgd->id | 0x2] & 0x30) {
        case 0x00: /* Mono, 8-bit PCM */
            if ((sgd->fifo_end - sgd->fifo_pos) >= 1) {
                sgd->out_l = sgd->out_r = (sgd->fifo[sgd->fifo_pos++ & (sizeof(sgd->fifo) - 1)] ^ 0x80) << 8;
                return;
            }
            break;

        case 0x10: /* Stereo, 8-bit PCM */
            if ((sgd->fifo_end - sgd->fifo_pos) >= 2) {
                sgd->out_l = (sgd->fifo[sgd->fifo_pos++ & (sizeof(sgd->fifo) - 1)] ^ 0x80) << 8;
                sgd->out_r = (sgd->fifo[sgd->fifo_pos++ & (sizeof(sgd->fifo) - 1)] ^ 0x80) << 8;
                return;
            }
            break;

        case 0x20: /* Mono, 16-bit PCM */
            if ((sgd->fifo_end - sgd->fifo_pos) >= 2) {
                sgd->out_l = sgd->out_r = AS_U16(sgd->fifo[sgd->fifo_pos & (sizeof(sgd->fifo) - 1)]);
                sgd->fifo_pos += 2;
                return;
            }
            break;

        case 0x30: /* Stereo, 16-bit PCM */
            if ((sgd->fifo_end - sgd->fifo_pos) >= 4) {
                sgd->out_l = AS_U16(sgd->fifo[sgd->fifo_pos & (sizeof(sgd->fifo) - 1)]);
                sgd->fifo_pos += 2;
                sgd->out_r = AS_U16(sgd->fifo[sgd->fifo_pos & (sizeof(sgd->fifo) - 1)]);
                sgd->fifo_pos += 2;
                return;
            }
            break;

        default:
            break;
    }

    /* Feed silence if the FIFO is empty. */
    sgd->out_l = sgd->out_r = 0;
}

static void
ac97_via_poll_fm(void *priv)
{
    ac97_via_sgd_t *sgd = (ac97_via_sgd_t *) priv;
    ac97_via_t     *dev = sgd->dev;

    /* Schedule next run if FM playback is enabled. */
    if (dev->fm_enabled)
        timer_advance_u64(&sgd->poll_timer, sgd->timer_latch);

    /* Update FM audio buffer. */
    ac97_via_update_stereo(dev, sgd);

    /* Feed next sample from the FIFO.
       The data format is not documented, but it probes as 16-bit stereo at 24 KHz. */
    if ((sgd->fifo_end - sgd->fifo_pos) >= 4) {
        sgd->out_l = AS_U16(sgd->fifo[sgd->fifo_pos & (sizeof(sgd->fifo) - 1)]);
        sgd->fifo_pos += 2;
        sgd->out_r = AS_U16(sgd->fifo[sgd->fifo_pos & (sizeof(sgd->fifo) - 1)]);
        sgd->fifo_pos += 2;
        return;
    }

    /* Feed silence if the FIFO is empty. */
    sgd->out_l = sgd->out_r = 0;
}

static void
ac97_via_poll_modem(void *priv)
{
    ac97_via_sgd_t *sgd = (ac97_via_sgd_t *) priv;
    ac97_via_t     *dev = sgd->dev;

    /* Schedule next run if modem playback/capture is enabled. */
    if (dev->modem_enabled)
        timer_advance_u64(&sgd->poll_timer, sgd->timer_latch);

    /* Update modem audio buffer. */
    ac97_via_update_stereo(dev, sgd);

    /* Feed next sample from the FIFO.
       The data format is not documented, but it probes as 16-bit mono at the codec sample rate. */
    if ((sgd->fifo_end - sgd->fifo_pos) >= 2) {
        sgd->out_l = sgd->out_r = AS_I16(sgd->fifo[sgd->fifo_pos & (sizeof(sgd->fifo) - 1)]);
        sgd->fifo_pos += 2;
        return;
    }

    /* Feed silence if the FIFO is empty. */
    sgd->out_l = sgd->out_r = 0;
}

static void
ac97_via_poll_modem_capture(void *priv)
{
    ac97_via_sgd_t *sgd = (ac97_via_sgd_t *) priv;
    ac97_via_t     *dev = sgd->dev;

    /* Schedule next run if modem playback/capture is enabled. */
    if (dev->modem_enabled)
        timer_advance_u64(&sgd->poll_timer, sgd->timer_latch);

    /* Feed next sample into the FIFO.
       The data format is not documented, but it probes as 16-bit mono at the codec sample rate. */
    if ((sgd->fifo_end - sgd->fifo_pos) >= 2) {
        AS_I16(sgd->fifo[sgd->fifo_pos & (sizeof(sgd->fifo) - 1)]) = 0;
        sgd->fifo_pos += 2;
    }
}

static void
ac97_via_get_buffer(int32_t *buffer, int len, void *priv)
{
    ac97_via_t *dev = (ac97_via_t *) priv;

    ac97_via_update_stereo(dev, &dev->sgd[0]);
    ac97_via_update_stereo(dev, &dev->sgd[2]);
    ac97_via_update_stereo(dev, &dev->sgd[4]);

    for (int c = 0; c < len * 2; c++) {
        buffer[c] += dev->sgd[0].buffer[c] / 2;
        buffer[c] += dev->sgd[2].buffer[c] / 2;
        buffer[c] += dev->sgd[4].buffer[c] / 2;
    }

    dev->sgd[0].pos = dev->sgd[2].pos = dev->sgd[4].pos = 0;
}

static void
ac97_via_filter_cd_audio(int channel, double *buffer, void *priv)
{
    const ac97_via_t *dev = (ac97_via_t *) priv;
    double            c;
    double            volume = channel ? dev->cd_vol_r : dev->cd_vol_l;

    c       = ((*buffer) * volume) / 65536.0;
    *buffer = c;
}

static void
ac97_via_speed_changed(void *priv)
{
    ac97_via_t *dev = (ac97_via_t *) priv;
    double      freq;

    /* Get variable sample rate if enabled. */
    if (dev->vsr_enabled && dev->audio_codec)
        freq = ac97_codec_getrate(dev->audio_codec, 0x2c);
    else
        freq = (double) SOUND_FREQ;

    dev->sgd[0].timer_latch = (uint64_t) ((double) TIMER_USEC * (1000000.0 / freq));
    dev->sgd[2].timer_latch = (uint64_t) ((double) TIMER_USEC * (1000000.0 / 24000.0)); /* FM operates at a fixed 24 KHz */

    if (dev->modem_codec)
        freq = ac97_codec_getrate(dev->modem_codec, 0x40);
    else
        freq = (double) SOUND_FREQ;

    dev->sgd[4].timer_latch = dev->sgd[5].timer_latch = (uint64_t) ((double) TIMER_USEC * (1000000.0 / freq));
}

static void *
ac97_via_init(UNUSED(const device_t *info))
{
    ac97_via_t *dev = calloc(1, sizeof(ac97_via_t));

    ac97_via_log("AC97 VIA: init()\n");

    /* Set up codecs. */
    ac97_codec       = &dev->codec[0];
    ac97_codec_count = sizeof(dev->codec) / sizeof(dev->codec[0]);
    ac97_codec_id    = 0;

    /* Set up SGD channels. */
    for (uint8_t i = 0; i < (sizeof(dev->sgd) / sizeof(dev->sgd[0])); i++) {
        dev->sgd[i].id  = i << 4;
        dev->sgd[i].dev = dev;

        /* Disable the FIFO on SGDs we don't care about. */
        if ((i != 0) && (i != 2) && (i != 4) && (i != 5))
            dev->sgd[i].always_run = 1;

        timer_add(&dev->sgd[i].dma_timer, ac97_via_sgd_process, &dev->sgd[i], 0);
    }

    /* Set up playback pollers. */
    timer_add(&dev->sgd[0].poll_timer, ac97_via_poll_stereo, &dev->sgd[0], 0);
    timer_add(&dev->sgd[2].poll_timer, ac97_via_poll_fm, &dev->sgd[2], 0);
    timer_add(&dev->sgd[4].poll_timer, ac97_via_poll_modem, &dev->sgd[4], 0);
    timer_add(&dev->sgd[5].poll_timer, ac97_via_poll_modem_capture, &dev->sgd[5], 0);
    ac97_via_speed_changed(dev);

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

const device_t ac97_via_device = {
    .name          = "VIA VT82C686 Integrated AC97 Controller",
    .internal_name = "ac97_via",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = ac97_via_init,
    .close         = ac97_via_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = ac97_via_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};
