/*
 * 86Box     A hypervisor and IBM PC system emulator that specializes in
 *           running old operating systems and software designed for IBM
 *           PC systems and compatibles from 1981 through fairly recent
 *           system designs based on the PCI bus.
 *
 *           This file is part of the 86Box distribution.
 *
 *           Pro Audio Spectrum Plus and 16 emulation.
 *
 *           Original PAS uses:
 *               - 2 x OPL2;
 *               - PIT - sample rate/count;
 *               - LMC835N/LMC1982 - mixer;
 *               - YM3802 - MIDI Control System.
 *
 *           9A01 - I/O base:
 *               - base >> 2.
 *
 *           All below + I/O base.
 *
 *           B89 - Interrupt status / clear:
 *               - Bit 2 - sample rate;
 *               - Bit 3 - PCM;
 *               - Bit 4 - MIDI.
 *
 *           B88 - Audio mixer control register.
 *
 *           B8A - Audio filter control:
 *               - Bit 5 - mute?.
 *
 *           B8B - Interrupt mask / board ID:
 *               - Bits 5-7 - board ID (read only on PAS16).
 *
 *           F88 - PCM data (low).
 *
 *           F89 - PCM data (high).
 *
 *           F8A - PCM control?:
 *               - Bit 4 - input/output select (1 = output);
 *               - Bit 5 - mono/stereo select;
 *               - Bit 6 - PCM enable.
 *
 *           1388-138B - PIT clocked at 1193180 Hz:
 *               - 1388 - Sample rate;
 *               - 1389 - Sample count.
 *
 *           178B - ????.
 *           2789 - Board revision.
 *
 *           8389:
 *               - Bit 2 - 8/16 bit.
 *
 *           BF88 - Wait states.
 *
 *           EF8B:
 *               - Bit 3 - 16 bits okay ?.
 *
 *           F388:
 *               - Bit 6 - joystick enable.
 *
 *           F389:
 *               - Bits 0-2 - DMA.
 *
 *           F38A:
 *               - Bits 0-3 - IRQ.
 *
 *           F788:
 *               - Bit 1 - SB emulation;
 *               - Bit 0 - MPU401 emulation.
 *
 *           F789 - SB base address:
 *               - Bits 0-3 - Address bits 4-7.
 *
 *           FB8A - SB IRQ/DMA:
 *               - Bits 3-5 - IRQ;
 *               - Bits 6-7 - DMA.
 *
 *           FF88 - board model:
 *               - 3 = PAS16.
 *
 * Authors:  Sarah Walker, <https://pcem-emulator.co.uk/>
 *           Miran Grca, <mgrca8@gmail.com>
 *           TheCollector1995, <mariogplayer@gmail.com>
 *
 *           Copyright 2008-2024 Sarah Walker.
 *           Copyright 2024 Miran Grca.
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H

#include "cpu.h"
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/filters.h>
#include <86box/io.h>
#include <86box/midi.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/pit_fast.h>
#include <86box/snd_mpu401.h>
#include <86box/sound.h>
#include <86box/snd_opl.h>
#include <86box/snd_sb.h>
#include <86box/snd_sb_dsp.h>
#include <86box/plat_unused.h>

typedef struct pas16_t {
    uint8_t  this_id;
    uint8_t  board_id;
    uint8_t  master_ff;
    uint8_t  irq;
    uint8_t  dma;
    uint8_t  sb_irqdma;
    uint8_t  type;
    uint8_t  filter;

    uint8_t  audiofilt;
    uint8_t  audio_mixer;
    uint8_t  compat;
    uint8_t  compat_base;
    uint8_t  io_conf_1;
    uint8_t  io_conf_2;
    uint8_t  io_conf_3;
    uint8_t  io_conf_4;

    uint8_t  irq_stat;
    uint8_t  irq_ena;
    uint8_t  pcm_ctrl;
    uint8_t  prescale_div;
    uint8_t  stereo_lr;
    uint8_t  dma8_ff;
    uint8_t  waitstates;
    uint8_t  enhancedscsi;

    uint8_t  sys_conf_1;
    uint8_t  sys_conf_2;
    uint8_t  sys_conf_3;
    uint8_t  sys_conf_4;
    uint8_t  midi_ctrl;
    uint8_t  midi_stat;
    uint8_t  midi_data;
    uint8_t  fifo_stat;

    uint8_t  midi_queue[256];

    uint16_t base;
    uint16_t new_base;
    uint16_t sb_compat_base;
    uint16_t mpu401_base;
    uint16_t dma8_dat;
    uint16_t ticks;
    uint16_t pcm_dat_l;
    uint16_t pcm_dat_r;

    int32_t  pcm_buffer[2][SOUNDBUFLEN * 2];

    int      pos;
    int      midi_r;
    int      midi_w;
    int      midi_uart_out;
    int      midi_uart_in;
    int      sysex;

    fm_drv_t opl;
    sb_dsp_t dsp;

    mpu_t *  mpu;

    pitf_t * pit;
} pas16_t;

static uint8_t pas16_next = 0;

static void    pas16_update(pas16_t *pas16);

static int pas16_dmas[8]    = { 4, 1, 2, 3, 0, 5, 6, 7 };
static int pas16_sb_irqs[8] = { 0, 2, 3, 5, 7, 10, 11, 12 };
static int pas16_sb_dmas[8] = { 0, 1, 2, 3 };

enum {
    PAS16_INT_SAMP = 0x04,
    PAS16_INT_PCM  = 0x08,
    PAS16_INT_MIDI = 0x10
};

enum {
    PAS16_PCM_MONO = 0x20,
    PAS16_PCM_ENA  = 0x40,
    PAS16_PCM_DMA_ENA = 0x80
};

enum {
    PAS16_SC2_16BIT  = 0x04,
    PAS16_SC2_MSBINV = 0x10
};

enum {
    PAS16_FILT_MUTE = 0x20
};

#define PAS16_PCM_AND_DMA_ENA (PAS16_PCM_ENA | PAS16_PCM_DMA_ENA)

double low_fir_pas16_coef[4][SB16_NCoef];

static __inline double
sinc(double x)
{
    return sin(M_PI * x) / (M_PI * x);
}

static void
recalc_pas16_filter(int c, int playback_freq)
{
    /* Cutoff frequency = playback / 2 */
    int    n;
    double w;
    double h;
    double fC = ((double) playback_freq) / (double) FREQ_96000;
    double gain;

    for (n = 0; n < SB16_NCoef; n++) {
        /* Blackman window */
        w = 0.42 - (0.5 * cos((2.0 * n * M_PI) / (double) (SB16_NCoef - 1))) + (0.08 * cos((4.0 * n * M_PI) / (double) (SB16_NCoef - 1)));
        /* Sinc filter */
        h = sinc(2.0 * fC * ((double) n - ((double) (SB16_NCoef - 1) / 2.0)));

        /* Create windowed-sinc filter */
        low_fir_pas16_coef[c][n] = w * h;
    }

    low_fir_pas16_coef[c][(SB16_NCoef - 1) / 2] = 1.0;

    gain = 0.0;
    for (n = 0; n < SB16_NCoef; n++)
        gain += low_fir_pas16_coef[c][n];

    /* Normalise filter, to produce unity gain */
    for (n = 0; n < SB16_NCoef; n++)
        low_fir_pas16_coef[c][n] /= gain;
}

#ifdef ENABLE_PAS16_LOG
int pas16_do_log = ENABLE_PAS16_LOG;

static void
pas16_log(const char *fmt, ...)
{
    va_list ap;

    if (pas16_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define pas16_log(fmt, ...)
#endif

static uint8_t
pas16_in(uint16_t port, void *priv);
static void
pas16_out(uint16_t port, uint8_t val, void *priv);

static void
pas16_update_irq(pas16_t *pas16)
{
    if (pas16->midi_uart_out && (pas16->midi_stat & 0x18)) {
        pas16->irq_stat |= PAS16_INT_MIDI;
        if (pas16->irq_ena & PAS16_INT_MIDI)
            picint(1 << pas16->irq);
    }
    if (pas16->midi_uart_in && (pas16->midi_stat & 0x04)) {
        pas16->irq_stat |= PAS16_INT_MIDI;
        if (pas16->irq_ena & PAS16_INT_MIDI)
            picint(1 << pas16->irq);
    }
}

static uint8_t
pas16_in(uint16_t port, void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;
    uint8_t  ret   = 0xff;

    port -= pas16->base;

    switch (port) {
        case 0x0000 ... 0x0003:
            ret = pas16->opl.read(port + 0x0388, pas16->opl.priv);
            break;

        case 0x0800:
            ret = pas16->audio_mixer;
            break;
        case 0x0801:
            ret = pas16->irq_stat & 0xdf;
            break;
        case 0x0802:
            ret = pas16->audiofilt;
            break;
        case 0x0803:
            ret = pas16->irq_ena | (pas16->type ? 0x20 : 0x00);
            pas16_log("IRQ Mask read=%02x.\n", ret);
            break;

        case 0x0c02:
            ret = pas16->pcm_ctrl;
            break;

        case 0x1401:
        case 0x1403:
            ret = pas16->midi_ctrl;
            break;
        case 0x1402:
        case 0x1802:
            ret = 0;
            if (pas16->midi_uart_in) {
                if ((pas16->midi_data == 0xaa) && (pas16->midi_ctrl & 0x04))
                    ret = pas16->midi_data;
                else {
                    ret = pas16->midi_queue[pas16->midi_r];
                    if (pas16->midi_r != pas16->midi_w) {
                        pas16->midi_r++;
                        pas16->midi_r &= 0xff;
                    }
                }
                pas16->midi_stat &= ~0x04;
                pas16_update_irq(pas16);
            }
            break;

        case 0x1800:
            ret = pas16->midi_stat;
            break;
        case 0x1801:
            ret = pas16->fifo_stat;
            break;

        case 0x2401:    /* Board revision */
            ret = 0x00;
            break;

        case 0x7c01:
            ret = pas16->enhancedscsi & ~0x01;
            break;

        case 0x8000:
            ret = pas16->sys_conf_1;
            break;
        case 0x8001:
            ret = pas16->sys_conf_2;
            break;
        case 0x8002:
            ret = pas16->sys_conf_3;
            break;
        case 0x8003:
            ret = pas16->sys_conf_4;
            break;

        case 0xbc00:
            ret = pas16->waitstates;
            break;
        case 0xbc02:
            ret = pas16->prescale_div;
            break;

        case 0xec03:
#ifdef NEWER_PAS16
            ret = pas16->type ? 0x0c : 0x06;
#else
            ret = pas16->type ? 0x0f : 0x06;
#endif
            break;

        case 0xf000:
            ret = pas16->io_conf_1;
            break;
        case 0xf001:
            ret = pas16->io_conf_2;
            pas16_log("pas16_in : set PAS DMA %i\n", pas16->dma);
            break;
        case 0xf002:
            ret = pas16->io_conf_3;
            pas16_log("pas16_in : set PAS IRQ %i\n", pas16->irq);
            break;
        case 0xf003:
            ret = pas16->io_conf_4;
            break;

        case 0xf400:
            ret = (pas16->compat & 0xf3);

            if (pas16->dsp.sb_irqm8 || pas16->dsp.sb_irqm16 || pas16->dsp.sb_irqm401)
                ret |= 0x04;

            if (pas16->mpu->mode == M_UART)
                ret |= 0x08;
            break;
        case 0xf401:
            ret = pas16->compat_base;
            break;

        case 0xf802:
            ret = pas16->sb_irqdma;
            break;

        case 0xfc00:    /* Board model */
            /* PAS16 or PASPlus */
            ret = pas16->type ? 0x0c : 0x01;
            break;
        case 0xfc03:    /* Master mode read */
            /* AT bus, XT/AT timing */
            ret = 0x11;
            if (pas16->type)
                ret |= 0x20;
            break;

        default:
            break;
    }

    pas16_log("[%04X:%08X] PAS16: [R] %04X (%04X) = %02X\n",
              CS, cpu_state.pc, port + pas16->base, port, ret);

    return ret;
}

static void
pas16_change_pit_clock_speed(void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;
    pitf_t *pit = (pitf_t *) pas16->pit;

    if (pas16->type && (pas16->sys_conf_1 & 0x02) && pas16->prescale_div) {
        pit_change_pas16_consts((double) pas16->prescale_div);
        if (pas16->sys_conf_3 & 0x02)
            pitf_set_pit_const(pit, PAS16CONST2);
        else
            pitf_set_pit_const(pit, PAS16CONST);
    } else
        pitf_set_pit_const(pit, PITCONST);
}

static void
pas16_io_handler(pas16_t *pas16, int set)
{
    if (pas16->base != 0x0000) {
        for (uint32_t addr = 0x0000; addr <= 0xffff; addr += 0x0400) {
            pas16_log("%04X-%04X: %i\n", pas16->base + addr, pas16->base + addr + 3, set);
            if (addr != 0x1000)
                io_handler(set, pas16->base + addr, 0x0004,
                           pas16_in, NULL, NULL, pas16_out, NULL, NULL, pas16);
        }

        pitf_handler(set, pas16->base + 0x1000, 0x0004, pas16->pit);
    }
}

static void
pas16_reset_pcm(void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;

    pas16->pcm_ctrl = 0x00;

    pas16->stereo_lr = 0;

    pas16->irq_stat &= 0xd7;

    if (!pas16->irq_stat)
        picintc(1 << pas16->irq);
}

static void
pas16_reset_regs(void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;
    pitf_t *pit = (pitf_t *) pas16->pit;

    picintc(1 << pas16->irq);

    pas16->sys_conf_1 &= 0xfd;

    pas16->sys_conf_2 = 0x00;
    pas16->sys_conf_3 = 0x00;

    pas16->prescale_div = 0x00;

    pitf_set_pit_const(pit, PITCONST);

    pas16->audiofilt = 0x00;
    pas16->filter = 0;

    pitf_ctr_set_gate(pit, 0, 0);
    pitf_ctr_set_gate(pit, 1, 0);

    pas16_reset_pcm(pas16);
    pas16->dma8_ff = 0;

    pas16->irq_ena = 0x00;
    pas16->irq_stat = 0x00;
}

static void
pas16_reset_common(void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;

    pas16_reset_regs(pas16);

    picintc(1 << pas16->irq);

    pas16_io_handler(pas16, 0);
    pas16->base = 0x0000;
}

static void
pas16_reset(void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;

    pas16_reset_common(priv);

    pas16->board_id = 0;
    pas16->master_ff = 0;

    pas16->base = 0x0388;
    pas16_io_handler(pas16, 1);

    pas16->new_base = 0x0388;

    pas16->sb_compat_base = 0x0220;
    pas16->compat = 0x02;
    pas16->compat_base = 0x02;
    sb_dsp_setaddr(&pas16->dsp, pas16->sb_compat_base);
}

static void
pas16_out(uint16_t port, uint8_t val, void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;

    pas16_log("[%04X:%08X] PAS16: [W] %04X (%04X) = %02X\n",
              CS, cpu_state.pc, port, port - pas16->base, val);

    port -= pas16->base;

    switch (port) {
        case 0x0000 ... 0x0003:
            pas16->opl.write(port + 0x0388, val, pas16->opl.priv);
            break;

        case 0x0800:
            pas16->audio_mixer = val;
            if (!(val & 0x01))
                pas16_reset_pcm(pas16);
            break;
        case 0x0801:
            pas16->irq_stat &= ~val;
            if (!(pas16->irq_stat & 0x1f))
                picintc(1 << pas16->irq);
            break;
        case 0x0802:
            pas16_update(pas16);

            pitf_ctr_set_gate(pas16->pit, 1, !!(val & 0x80));
            pitf_ctr_set_gate(pas16->pit, 0, !!(val & 0x40));

            pas16->stereo_lr = 0;
            pas16->dma8_ff = 0;

            if ((val & 0x20) && !(pas16->audiofilt & 0x20)) {
                pas16_log("Reset.\n");
                pas16_reset_regs(pas16);
            }

            pas16->audiofilt = val;

            if (val & 0x1f) {
                pas16->filter = 1;
                switch (val & 0x1f) {
                    default:
                        pas16->filter = 0;
                        break;
                    case 0x01:
                        recalc_pas16_filter(0, 17897);
                        break;
                    case 0x02:
                        recalc_pas16_filter(0, 15909);
                        break;
                    case 0x04:
                        recalc_pas16_filter(0, 2982);
                        break;
                    case 0x09:
                        recalc_pas16_filter(0, 11931);
                        break;
                    case 0x11:
                        recalc_pas16_filter(0, 8948);
                        break;
                    case 0x19:
                        recalc_pas16_filter(0, 5965);
                        break;
                }
            } else
                pas16->filter = 0;
            break;
        case 0x0803:
            pas16->irq_ena = val & 0x1f;
            pas16->irq_stat &= ((val & 0x1f) | 0xe0);

            if (!(pas16->irq_stat & 0x1f))
                picintc(1 << pas16->irq);
            break;

        case 0x0c00:
            pas16_update(pas16);
            break;
        case 0x0c01:
            pas16_update(pas16);
            break;
        case 0x0c02:
            if ((val & PAS16_PCM_ENA) && !(pas16->pcm_ctrl & PAS16_PCM_ENA)) {
                /* Guess */
                pas16->stereo_lr = 0;
                pas16->irq_stat &= 0xd7;
                /* Needed for 8-bit DMA to work correctly on a 16-bit DMA channel. */
                pas16->dma8_ff = 0;
            }

            pas16->pcm_ctrl = val;
            pas16_log("Now in: %s (%02X)\n", (pas16->pcm_ctrl & PAS16_PCM_MONO) ? "Mono" : "Stereo", val);
            break;

        case 0x1401:
        case 0x1403:
            pas16->midi_ctrl = val;
            if ((val & 0x60) == 0x60) {
                pas16->midi_uart_out = 0;
                pas16->midi_uart_in = 0;
            } else if (val & 0x18)
                pas16->midi_uart_out = 1;
            else if (val & 0x04)
                pas16->midi_uart_in = 1;
            else
                pas16->midi_uart_out = 1;

            pas16_update_irq(pas16);
            break;
        case 0x1402:
        case 0x1802:
            pas16->midi_data = val;
            pas16_log("UART OUT=%d.\n", pas16->midi_uart_out);
            if (pas16->midi_uart_out)
                midi_raw_out_byte(val);
            break;

        case 0x1800:
            pas16->midi_stat = val;
            pas16_update_irq(pas16);
            break;
        case 0x1801:
            pas16->fifo_stat = val;
            break;

        case 0x7c01:
            pas16->enhancedscsi = val;
            break;

        case 0x8000:
            if ((val & 0xc0) && !(pas16->sys_conf_1 & 0xc0)) {
                pas16_log("Reset.\n");
                val = 0x00;
                pas16_reset_common(pas16);
                pas16->base = pas16->new_base;
                pas16_io_handler(pas16, 1);
            }

            pas16->sys_conf_1 = val;
            pas16_change_pit_clock_speed(pas16);
            pas16_log("Now in: %s mode\n", (pas16->sys_conf_1 & 0x02) ? "native" : "compatibility");
            break;
        case 0x8001:
            pas16->sys_conf_2 = val;
            pas16_log("Now in: %i bits (%02X)\n",
                      (pas16->sys_conf_2 & 0x04) ? ((pas16->sys_conf_2 & 0x08) ? 12 : 16) : 8, val);
            break;
        case 0x8002:
            pas16->sys_conf_3 = val;
            pas16_change_pit_clock_speed(pas16);
            pas16_log("Use 1.008 MHz clok for PCM: %c\n", (val & 0x02) ? 'Y' : 'N');
            break;
        case 0x8003:
            pas16->sys_conf_4 = val;
            break;

        case 0xbc00:
            pas16->waitstates = val;
            break;
        case 0xbc02:
            pas16->prescale_div = val;
            pas16_change_pit_clock_speed(pas16);
            pas16_log("Prescale divider now: %i\n", val);
            break;

        case 0xf000:
            pas16->io_conf_1 = val;
            break;
        case 0xf001:
            pas16->io_conf_2 = val;
            pas16->dma       = pas16_dmas[val & 0x7];
            pas16_change_pit_clock_speed(pas16);
            pas16_log("pas16_out : set PAS DMA %i\n", pas16->dma);
            break;
        case 0xf002:
            pas16->io_conf_3 = val;
            pas16->irq       = val & 0x0f;
            if (pas16->irq <= 6)
                pas16->irq++;
            else if ((pas16->irq > 6) && (pas16->irq < 0x0b))
                pas16->irq += 3;
            else
                pas16->irq += 4;

            pas16_log("pas16_out : set PAS IRQ %i, val=%02x\n", pas16->irq, val & 0x0f);
            break;
        case 0xf003:
            pas16->io_conf_4 = val;
            break;

        case 0xf400:
            pas16->compat = val & 0xf3;
            pas16_log("PCM compression is now %sabled\n", (val & 0x10) ? "en" : "dis");
            if (pas16->compat & 0x02)
                sb_dsp_setaddr(&pas16->dsp, pas16->sb_compat_base);
            else
                sb_dsp_setaddr(&pas16->dsp, 0);
            if (pas16->compat & 0x01)
                mpu401_change_addr(pas16->mpu, ((pas16->compat_base & 0xf0) | 0x300));
            else
                mpu401_change_addr(pas16->mpu, 0);
            break;
        case 0xf401:
            pas16->compat_base = val;
            pas16->sb_compat_base = ((pas16->compat_base & 0xf) << 4) | 0x200;
            pas16_log("SB Compatibility base: %04X\n", pas16->sb_compat_base);
            if (pas16->compat & 0x02)
                sb_dsp_setaddr(&pas16->dsp, pas16->sb_compat_base);
            if (pas16->compat & 0x01)
                mpu401_change_addr(pas16->mpu, ((pas16->compat_base & 0xf0) | 0x300));
            break;

        case 0xf802:
            pas16->sb_irqdma = val;
            mpu401_setirq(pas16->mpu, pas16_sb_irqs[val & 7]);
            sb_dsp_setirq(&pas16->dsp, pas16_sb_irqs[(val >> 3) & 7]);
            sb_dsp_setdma8(&pas16->dsp, pas16_sb_dmas[(val >> 6) & 3]);
            pas16_log("pas16_out : set SB IRQ %i DMA %i.\n", pas16_sb_irqs[(val >> 3) & 7],
                      pas16_sb_dmas[(val >> 6) & 3]);
            break;

        default:
            pas16_log("pas16_out : unknown %04X\n", port);
    }
}

/*
   8-bit mono:
    - 8-bit DMA : On every timer 0 over, read the 8-bit sample and ctr_clock();
      (One ctr_clock() per timer 0 over)
    - 16-bit DMA: On every even timer 0 over, read two 8-bit samples at once and ctr_clock();
                  On every odd timer 0 over, read the MSB of the previously read sample word.
      (One ctr_clock() per two timer 0 overs)
   8-bit stereo:
    - 8-bit DMA : On every timer 0, read two 8-bit samples and ctr_clock() twice;
      (Two ctr_clock()'s per timer 0 over)
    - 16-bit DMA: On every timer 0, read two 8-bit samples and ctr_clock() once.
      (One ctr_clock() per timer 0 over)
   16-bit mono (to be verified):
    - 8-bit DMA : On every timer 0, read one 16-bit sample and ctr_clock() twice;
      (Two ctr_clock()'s per timer 0 over)
    - 16-bit DMA: On every timer 0, read one 16-bit sample and ctr_clock() once.
      (One ctr_clock() per timer 0 over)
   16-bit stereo:
    - 8-bit DMA : On every timer 0, read one 16-bit sample and ctr_clock() twice;
      (Two ctr_clock()'s per timer 0 over)
    - 16-bit DMA: On every timer 0, read one 16-bit sample and ctr_clock() twice.
      (Two ctr_clock()'s per timer 0 over)

   What we can conclude from this is:
    - Maximum 16 bits per timer 0 over;
    - A 8-bit sample always takes one ctr_clock() tick, unless it has been read
      alongside the previous sample;
    - A 16-bit sample always takes two ctr_clock() ticks.
 */
static uint16_t
pas16_dma_channel_read(pas16_t *pas16, int channel)
{
    int status;
    uint16_t ret;

    if (pas16->pcm_ctrl & PAS16_PCM_DMA_ENA) {
        if (pas16->dma >= 5) {
            dma_channel_advance(pas16->dma);
            status = dma_channel_read_only(pas16->dma);
        } else
            status = dma_channel_read(pas16->dma);
        ret = (status == DMA_NODATA) ? 0x0000 : (status & 0xffff);
    } else
        ret = 0x0000;

    return ret;
}

static uint16_t
pas16_dma_readb(pas16_t *pas16, uint8_t timer1_ticks)
{
    uint16_t ret;

    ret = pas16_dma_channel_read(pas16, pas16->dma);

    pas16->ticks += timer1_ticks;

    return ret;
}

static uint16_t
pas16_dma_readw(pas16_t *pas16, uint8_t timer1_ticks)
{
    uint16_t ret;

    if (pas16->dma >= 5)
        ret = pas16_dma_channel_read(pas16, pas16->dma);
    else {
        ret = pas16_dma_channel_read(pas16, pas16->dma);
        ret |= (pas16_dma_channel_read(pas16, pas16->dma) << 8);
    }

    pas16->ticks += timer1_ticks;

    return ret;
}

static uint16_t
pas16_readdmab(pas16_t *pas16)
{
    uint16_t ret;

    if (pas16->dma >= 5) {
        if (pas16->dma8_ff)
            pas16->dma8_dat >>= 8;
        else
            pas16->dma8_dat = pas16_dma_readb(pas16, 1);

        pas16->dma8_ff = !pas16->dma8_ff;
    } else
        pas16->dma8_dat = pas16_dma_readb(pas16, 1);

    ret = ((pas16->dma8_dat & 0xff) ^ 0x80) << 8;

    return ret;
}

static uint16_t
pas16_readdmaw_mono(pas16_t *pas16)
{
    uint16_t ret;

    ret = pas16_dma_readw(pas16, 1 + (pas16->dma < 5));

    return ret;
}

static uint16_t
pas16_readdmaw_stereo(pas16_t *pas16)
{
    uint16_t ret;
    uint16_t ticks = (pas16->sys_conf_1 & 0x02) ? (1 + (pas16->dma < 5)) : 2;

    ret = pas16_dma_readw(pas16, ticks);

    return ret;
}

static uint16_t
pas16_readdma_mono(pas16_t *pas16)
{
    uint16_t ret;

    if (pas16->sys_conf_2 & 0x04) {
        ret = pas16_readdmaw_mono(pas16);

        if (pas16->sys_conf_2 & 0x08)
            ret &= 0xfff0;
    } else
        ret = pas16_readdmab(pas16);

    if (pas16->sys_conf_2 & PAS16_SC2_MSBINV)
        ret ^= 0x8000;

    return ret;
}

static uint16_t
pas16_readdma_stereo(pas16_t *pas16)
{
    uint16_t ret;

    if (pas16->sys_conf_2 & 0x04) {
        ret = pas16_readdmaw_stereo(pas16);

        if (pas16->sys_conf_2 & 0x08)
            ret &= 0xfff0;
    } else
        ret = pas16_readdmab(pas16);

    if (pas16->sys_conf_2 & PAS16_SC2_MSBINV)
        ret ^= 0x8000;

    return ret;
}

static void
pas16_pit_timer0(int new_out, UNUSED(int old_out), void *priv)
{
    pitf_t *pit = (pitf_t *) priv;
    pas16_t *pas16 = (pas16_t *) pit->dev_priv;
    uint16_t temp;

    if (!pas16->pit->counters[0].gate)
        return;

    if (!dma_channel_readable(pas16->dma))
        return;

    pas16_update_irq(pas16);

    if (((pas16->pcm_ctrl & PAS16_PCM_ENA) == PAS16_PCM_ENA) && (pit->counters[1].m & 2) && new_out) {
        pas16->ticks = 0;

        if (pas16->pcm_ctrl & PAS16_PCM_MONO) {
            temp = pas16_readdma_mono(pas16);

            pas16->pcm_dat_l = pas16->pcm_dat_r = temp;
        } else {
            temp = pas16_readdma_stereo(pas16);

            if (pas16->sys_conf_1 & 0x02) {
                pas16->pcm_dat_l = temp;

                temp = pas16_readdma_stereo(pas16);

                pas16->pcm_dat_r = temp;
            } else {
                if (pas16->stereo_lr)
                    pas16->pcm_dat_r = temp;
                else
                    pas16->pcm_dat_l = temp;

                pas16->stereo_lr = !pas16->stereo_lr;
                pas16->irq_stat = (pas16->irq_stat & 0xdf) | (pas16->stereo_lr << 5);
            }
        }

        if (pas16->ticks) {
            for (uint8_t i = 0; i < pas16->ticks; i++)
                pitf_ctr_clock(pas16->pit, 1);

            pas16->ticks = 0;
        }

        pas16->irq_stat |= PAS16_INT_SAMP;
        if (pas16->irq_ena & PAS16_INT_SAMP) {
            pas16_log("INT SAMP.\n");
            picint(1 << pas16->irq);
        }

        pas16_update(pas16);
    }
}

static void
pas16_pit_timer1(int new_out, UNUSED(int old_out), void *priv)
{
    pitf_t *pit = (pitf_t * )priv;
    pas16_t *pas16 = (pas16_t *) pit->dev_priv;

    if (!pas16->pit->counters[1].gate)
        return;

    /* At new_out = 0, it's in the counter reload phase. */
    if ((pas16->pcm_ctrl & PAS16_PCM_ENA) && (pit->counters[1].m & 2) && new_out) {
        if (pas16->irq_ena & PAS16_INT_PCM) {
            pas16->irq_stat |= PAS16_INT_PCM;
            pas16_log("pas16_pcm_poll : cause IRQ %i %02X\n", pas16->irq, 1 << pas16->irq);
            picint(1 << pas16->irq);
        }
    }
}

static void
pas16_out_base(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;

    pas16_log("[%04X:%08X] PAS16: [W] %04X = %02X\n", CS, cpu_state.pc, port, val);

    if (pas16->master_ff && (pas16->board_id == pas16->this_id))
        pas16->new_base = val << 2;
    else if (!pas16->master_ff)
        pas16->board_id = val;

    pas16->master_ff = !pas16->master_ff;
}

static void
pas16_input_msg(void *priv, uint8_t *msg, uint32_t len)
{
    pas16_t  *pas16 = (pas16_t *) priv;

    if (pas16->sysex)
        return;

    if (pas16->midi_uart_in) {
        pas16->midi_stat |= 0x04;

        for (uint32_t i = 0; i < len; i++) {
            pas16->midi_queue[pas16->midi_w++] = msg[i];
            pas16->midi_w &= 0xff;
        }

        pas16_update_irq(pas16);
    }
}

static int
pas16_input_sysex(void *priv, uint8_t *buffer, uint32_t len, int abort)
{
    pas16_t  *pas16 = (pas16_t *) priv;

    if (abort) {
        pas16->sysex = 0;
        return 0;
    }
    pas16->sysex = 1;
    for (uint32_t i = 0; i < len; i++) {
        if (pas16->midi_r == pas16->midi_w)
            return (len - i);
        pas16->midi_queue[pas16->midi_w++] = buffer[i];
        pas16->midi_w &= 0xff;
    }
    pas16->sysex = 0;
    return 0;
}

static void
pas16_update(pas16_t *pas16)
{
    if (!(pas16->audiofilt & PAS16_FILT_MUTE)) {
        for (; pas16->pos < sound_pos_global; pas16->pos++) {
            pas16->pcm_buffer[0][pas16->pos] = 0;
            pas16->pcm_buffer[1][pas16->pos] = 0;
        }
    } else {
        for (; pas16->pos < sound_pos_global; pas16->pos++) {
            pas16->pcm_buffer[0][pas16->pos] = 0;
            pas16->pcm_buffer[1][pas16->pos] = 0;
#ifdef CROSS_CHANNEL
            if (pas16->pcm_ctrl & 0x08)
                pas16->pcm_buffer[0][pas16->pos] += (int16_t) pas16->pcm_dat_l;
            if (pas16->pcm_ctrl & 0x04)
                pas16->pcm_buffer[0][pas16->pos] += (int16_t) pas16->pcm_dat_r;
            if (pas16->pcm_ctrl & 0x02)
                pas16->pcm_buffer[1][pas16->pos] += (int16_t) pas16->pcm_dat_l;
            if (pas16->pcm_ctrl & 0x01)
                pas16->pcm_buffer[1][pas16->pos] += (int16_t) pas16->pcm_dat_r;
#else
            pas16->pcm_buffer[0][pas16->pos] += (int16_t) pas16->pcm_dat_l;
            pas16->pcm_buffer[1][pas16->pos] += (int16_t) pas16->pcm_dat_r;
#endif
        }
    }
}

void
pas16_get_buffer(int32_t *buffer, int len, void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;

    sb_dsp_update(&pas16->dsp);
    pas16_update(pas16);
    for (int c = 0; c < len * 2; c++) {
        buffer[c] += (int32_t) (sb_iir(0, c & 1, (double) pas16->dsp.buffer[c]) / 1.3) / 2;
        if (pas16->filter)
            buffer[c] += (low_fir_pas16(0, c & 1, (double) pas16->pcm_buffer[c & 1][c >> 1]) / 1.3) / 2.0;
        else
            buffer[c] += ((pas16->pcm_buffer[c & 1][c >> 1] / 1.3) / 2);
    }

    pas16->pos = 0;
    pas16->dsp.pos = 0;
}

void
pas16_get_music_buffer(int32_t *buffer, int len, void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;

    const int32_t *opl_buf = pas16->opl.update(pas16->opl.priv);
    for (int c = 0; c < len * 2; c++)
        buffer[c] += opl_buf[c];

    pas16->opl.reset_buffer(pas16->opl.priv);
}

static void
pas16_speed_changed(void *priv)
{
    pas16_change_pit_clock_speed(priv);
}

static void *
pas16_init(const device_t *info)
{
    pas16_t *pas16 = calloc(1, sizeof(pas16_t));

    if (pas16_next > 3) {
        fatal("Attempting to add a Pro Audio Spectrum instance beyond the maximum amount\n");

        free(pas16);
        return NULL;
    }

    pas16->type = info->local & 0xff;
    fm_driver_get(FM_YMF262, &pas16->opl);
    sb_dsp_set_real_opl(&pas16->dsp, 1);
    sb_dsp_init(&pas16->dsp, SB2, SB_SUBTYPE_DEFAULT, pas16);
    pas16->mpu = (mpu_t *) malloc(sizeof(mpu_t));
    memset(pas16->mpu, 0, sizeof(mpu_t));
    mpu401_init(pas16->mpu, 0, 0, M_INTELLIGENT, device_get_config_int("receive_input401"));
    sb_dsp_set_mpu(&pas16->dsp, pas16->mpu);

    pas16->sb_compat_base = 0x0000;

    io_sethandler(0x9a01, 0x0001, NULL, NULL, NULL, pas16_out_base, NULL, NULL, pas16);
    pas16->this_id = 0xbc + pas16_next;

    pas16->pit = device_add(&i8254_ext_io_fast_device);
    pas16_reset(pas16);
    pas16->pit->dev_priv = pas16;
    pas16->irq = pas16->type ? 10 : 5;
    pas16->dma = 3;
    for (uint8_t i = 0; i < 3; i++)
        pitf_ctr_set_gate(pas16->pit, i, 0);

    pitf_ctr_set_out_func(pas16->pit, 0, pas16_pit_timer0);
    pitf_ctr_set_out_func(pas16->pit, 1, pas16_pit_timer1);
    pitf_ctr_set_using_timer(pas16->pit, 0, 1);
    pitf_ctr_set_using_timer(pas16->pit, 1, 0);
    pitf_ctr_set_using_timer(pas16->pit, 2, 0);

    sound_add_handler(pas16_get_buffer, pas16);
    music_add_handler(pas16_get_music_buffer, pas16);

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, pas16_input_msg, pas16_input_sysex, pas16);

    pas16_next++;

    return pas16;
}

static void
pas16_close(void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;

    free(pas16);

    pas16_next = 0;
}

static const device_config_t pas16_config[] = {
    {
        .name = "receive_input401",
        .description = "Receive input (MPU-401)",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 0
    },
    {
        .name = "receive_input",
        .description = "Receive input (PAS MIDI)",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t pasplus_device = {
    .name          = "Pro Audio Spectrum Plus",
    .internal_name = "pasplus",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = pas16_init,
    .close         = pas16_close,
    .reset         = pas16_reset,
    { .available = NULL },
    .speed_changed = pas16_speed_changed,
    .force_redraw  = NULL,
    .config        = pas16_config
};


const device_t pas16_device = {
    .name          = "Pro Audio Spectrum 16",
    .internal_name = "pas16",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 1,
    .init          = pas16_init,
    .close         = pas16_close,
    .reset         = pas16_reset,
    { .available = NULL },
    .speed_changed = pas16_speed_changed,
    .force_redraw  = NULL,
    .config        = pas16_config
};
