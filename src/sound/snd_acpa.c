/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM M-ACPA/A (Audio Capture & Playback Adapter emulation
 *
 * Authors: NexGenDriven, <nxdcaptain@gmail.com>
 *
 *          Copyright 2026 NexGenDriven (nxdcaptain).
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mca.h>
#include <86box/pic.h>
#include <86box/sound.h>
#include <86box/timer.h>
#include <86box/snd_tms320c25.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_ACPA_LOG
int acpa_do_log = ENABLE_ACPA_LOG;

static void
acpa_log(const char *fmt, ...)
{
    va_list ap;

    if (acpa_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define acpa_log(fmt, ...)
#endif

#define ACPA_SAMPLE_RATE 88200.0
#define ACPA_DSP_CYCLES  10000000.0

#define ACPA_OUT_LEN     16384
#define ACPA_IN_RING     16384

#define SM_DACR 0x000
#define SM_DACL 0x200
#define SM_SCR  0x400
#define SM_ADC  0x600

/* DSP status register */
#define DST_DACR 0x01
#define DST_DACL 0x02
#define DST_ADC  0x04
#define DST_HREQ 0x08

typedef struct acpa_t {
    uint8_t  pos_regs[8];
    uint16_t base;
    int      irq;
    int      irq_asserted;

    c25_t    dsp;
    uint16_t shared[0x2000];
    uint16_t sample[0x800];

    /* Host interface */
    uint16_t addr;
    uint8_t  data_lo;
    uint8_t  run;
    uint8_t  hintena;
    uint8_t  spkr_en;
    uint8_t  tms_int_ack; /* Host status bit 0: 1 */
    uint8_t  hst_req;     /* Host status bit 1 */

    /* Converters */
    uint8_t  dsp_status; 
    uint8_t  enable; 
    uint8_t  inp_sel;
    uint8_t  ad_chan;
    uint8_t  mike_gain;
    uint8_t  blk;
    uint16_t pos;

    pc_timer_t timer;
    double     tick_period;
    double     cyc_per_tick;
    double     cyc_acc;

    /* DAC output */
    int16_t  out_l[ACPA_OUT_LEN];
    int16_t  out_r[ACPA_OUT_LEN];
    int      out_wr;

    /* ADC input */
    int16_t  in_ring[ACPA_IN_RING * 2];
    uint32_t in_wr;
    uint32_t in_rd;
    double   in_phase;
    int      in_rate;
    int      in_started;
    int      in_primed;
    int16_t  in_l;
    int16_t  in_r;
} acpa_t;

static const uint16_t acpa_bases[4] = { 0xfdc0, 0xfdc8, 0xfdd0, 0xfdd8 };
static const uint8_t  acpa_irqs[8]  = { 3, 4, 5, 6, 9, 10, 11, 12 };

static void acpa_timer_update(acpa_t *dev);

static void
acpa_update_irq(acpa_t *dev)
{
    const int want = (dev->pos_regs[2] & 1) && dev->hst_req && dev->hintena && dev->run;

    if (want && !dev->irq_asserted) {
        picint(1 << dev->irq);
        dev->irq_asserted = 1;
    } else if (!want && dev->irq_asserted) {
        picintc(1 << dev->irq);
        dev->irq_asserted = 0;
    }
}

static void
acpa_adc_input(acpa_t *dev, int on)
{
    if (on && !dev->in_started) {
        dev->in_wr = dev->in_rd = 0;
        dev->in_phase  = 0.0;
        dev->in_primed = 0;
        dev->in_l = dev->in_r = 0;
        sound_in_start_input();
        dev->in_started = 1;
    } else if (!on && dev->in_started) {
        sound_in_stop_input();
        dev->in_started = 0;
    }
}

static void
acpa_put_buffer(int16_t *buffer, int len, void *priv)
{
    acpa_t    *dev      = (acpa_t *) priv;
    const int  cap_rate = al_capture_get_rate();

    if (!dev->in_started)
        return;

    dev->in_rate = (cap_rate > 0) ? cap_rate : SOUND_FREQ;

    for (int c = 0; c < len; c++) {
        const uint32_t i = dev->in_wr++ % ACPA_IN_RING;
        dev->in_ring[i * 2]     = buffer[c * 2];
        dev->in_ring[i * 2 + 1] = buffer[c * 2 + 1];
    }

    uint32_t keep = (uint32_t) (dev->in_rate / 4);
    if (keep > (ACPA_IN_RING / 2))
        keep = ACPA_IN_RING / 2;
    if ((dev->in_wr - dev->in_rd) > keep)
        dev->in_rd = dev->in_wr - (keep / 2);
}

static void
acpa_adc_next(acpa_t *dev)
{
    const uint32_t avail = dev->in_wr - dev->in_rd;

    if (!dev->in_started || (dev->in_rate <= 0)) {
        dev->in_l = dev->in_r = 0;
        return;
    }

    dev->in_phase += (double) dev->in_rate / ACPA_SAMPLE_RATE;
    while (dev->in_phase >= 1.0) {
        dev->in_phase -= 1.0;
        if (!dev->in_primed && (avail >= (uint32_t) (dev->in_rate / 25)))
            dev->in_primed = 1;
        if (!dev->in_primed || ((dev->in_wr - dev->in_rd) < 1)) {
            dev->in_primed = 0;
            dev->in_l = dev->in_r = 0;
        } else {
            const uint32_t i = dev->in_rd++ % ACPA_IN_RING;
            dev->in_l = dev->in_ring[i * 2];
            dev->in_r = dev->in_ring[i * 2 + 1];
        }
    }
}

static void
acpa_blocks_start(acpa_t *dev)
{
    dev->blk = 0;
    dev->pos = 0;
}

static void
acpa_sample_tick(acpa_t *dev)
{
    int16_t l = 0;
    int16_t r = 0;

    if (dev->enable) {
        const uint16_t idx = (uint16_t) ((dev->blk << 8) | (dev->pos & 0xff));

        if (dev->enable & 1)
            r = (int16_t) dev->sample[SM_DACR + idx];
        if (dev->enable & 2)
            l = (int16_t) dev->sample[SM_DACL + idx];
        if ((dev->enable & 4)) {
            int16_t v;

            acpa_adc_next(dev);
            if (dev->ad_chan) { /* Stereo, 44.1 kHz per channel */
                v = (dev->pos & 1) ? dev->in_r : dev->in_l;
            } else if (dev->inp_sel) /* Line right */
                v = dev->in_r;
            else /* Microphone */
                v = (int16_t) (((int32_t) dev->in_l + dev->in_r) / 2);
            dev->sample[SM_ADC + idx] = (uint16_t) v;
        }

        if (++dev->pos >= 256) {
            dev->pos = 0;
            dev->blk ^= 1;
            dev->dsp_status |= dev->enable & 7;
            c25_set_int(&dev->dsp, C25_INT0);
        }
    }

    if (dev->out_wr < ACPA_OUT_LEN) {
        dev->out_l[dev->out_wr] = l;
        dev->out_r[dev->out_wr] = r;
        dev->out_wr++;
    }
}

static uint16_t
acpa_dsp_prog_read(void *priv, uint16_t addr)
{
    const acpa_t *dev = (acpa_t *) priv;

    return dev->shared[addr & 0x1fff];
}

static void
acpa_dsp_prog_write(void *priv, uint16_t addr, uint16_t val)
{
    acpa_t *dev = (acpa_t *) priv;

    dev->shared[addr & 0x1fff] = val;
}

static uint16_t
acpa_dsp_data_read(void *priv, uint16_t addr)
{
    const acpa_t *dev = (acpa_t *) priv;

    if (addr & 0x4000)
        return dev->sample[addr & 0x7ff];
    return dev->shared[addr & 0x1fff];
}

static void
acpa_dsp_data_write(void *priv, uint16_t addr, uint16_t val)
{
    acpa_t *dev = (acpa_t *) priv;

    if (addr & 0x4000)
        dev->sample[addr & 0x7ff] = val;
    else
        dev->shared[addr & 0x1fff] = val;
}

static int
acpa_dsp_bio(void *priv)
{
    const acpa_t *dev = (acpa_t *) priv;

    return dev->blk;
}

/* TMS320C25 status registers */
static uint16_t
acpa_dsp_io_read(void *priv, UNUSED(uint8_t port))
{
    const acpa_t *dev = (acpa_t *) priv;

    return dev->dsp_status | (dev->hst_req ? DST_HREQ : 0);
}

static void
acpa_dsp_io_write(void *priv, UNUSED(uint8_t port), uint16_t val)
{
    acpa_t *dev = (acpa_t *) priv;
    const uint8_t old_en = dev->enable;

    dev->enable    = val & 7;
    dev->inp_sel   = (val >> 3) & 1;
    dev->ad_chan   = (val >> 8) & 1;
    dev->mike_gain = (val >> 9) & 1;

    if (!(val & 0x10)) { /* SPB IAK */
        dev->dsp_status &= ~7;
        c25_clear_int(&dev->dsp, C25_INT0);
    }
    if (!(val & 0x20)) { /* TMS IAK */
        dev->tms_int_ack = 1;
        c25_clear_int(&dev->dsp, C25_INT1);
    }
    if (!(val & 0x40)) { /* HST REQ */
        dev->hst_req = 1;
        acpa_update_irq(dev);
    }
    if (!(val & 0x80)) /* BLK RST */
        acpa_blocks_start(dev);

    if (dev->enable != old_en) {
        if (!old_en && dev->enable)
            acpa_blocks_start(dev);
        if (dev->enable & ~old_en & 7) {
            dev->dsp_status |= dev->enable & ~old_en & 7;
            c25_set_int(&dev->dsp, C25_INT0);
        }
        acpa_adc_input(dev, !!(dev->enable & 4));
        acpa_timer_update(dev);
    }
}

static void
acpa_tick(void *priv)
{
    acpa_t *dev = (acpa_t *) priv;
    int     n;

    if (dev->run) {
        dev->cyc_acc += dev->cyc_per_tick;
        n = (int) dev->cyc_acc;
        dev->cyc_acc -= n;
        c25_run(&dev->dsp, n);
    }

    acpa_sample_tick(dev);

    if (dev->run && (dev->enable || !dev->dsp.idle || (dev->dsp.ifr & dev->dsp.imr & 0x3f)))
        timer_advance_u64(&dev->timer, (uint64_t) dev->tick_period);
    else
        timer_disable(&dev->timer);
}

static void
acpa_timer_update(acpa_t *dev)
{
    if (dev->run && !timer_is_enabled(&dev->timer))
        timer_set_delay_u64(&dev->timer, (uint64_t) dev->tick_period);
}

static void
acpa_dsp_reset(acpa_t *dev)
{
    c25_reset(&dev->dsp);
    dev->dsp_status  = 0;
    dev->enable      = 0;
    dev->blk         = 0;
    dev->pos         = 0;
    dev->tms_int_ack = 1;
    dev->hst_req     = 0;
    dev->cyc_acc     = 0.0;
    acpa_adc_input(dev, 0);
    acpa_update_irq(dev);
}

static uint8_t
acpa_read(uint16_t port, void *priv)
{
    acpa_t *dev = (acpa_t *) priv;
    uint8_t ret = 0xff;

    switch (port & 7) {
        case 0: /* Data, low byte */
            ret = dev->shared[dev->addr & 0x1fff] & 0xff;
            break;
        case 1: /* Data, high byte */
            ret = dev->shared[dev->addr & 0x1fff] >> 8;
            dev->addr++;
            break;
        case 6: /* Host status */
            ret = (dev->tms_int_ack ? 0x01 : 0x00) | (dev->hst_req ? 0x02 : 0x00);
            break;
        case 7: /* ID */
            ret = 0x6c;
            break;
        default:
            break;
    }
    return ret;
}

static void
acpa_write(uint16_t port, uint8_t val, void *priv)
{
    acpa_t *dev = (acpa_t *) priv;

    switch (port & 7) {
        case 0:
            dev->data_lo = val;
            break;
        case 1:
            dev->shared[dev->addr & 0x1fff] = (uint16_t) ((val << 8) | dev->data_lo);
            dev->addr++;
            break;
        case 4:
            dev->addr = (dev->addr & 0xff00) | val;
            break;
        case 5:
            dev->addr = (dev->addr & 0x00ff) | (val << 8);
            break;
        case 6: {
            const uint8_t run = val & 1;

            dev->hintena = (val >> 2) & 1;
            dev->spkr_en = (val >> 4) & 1;

            if (!run && dev->run) {
                dev->run = 0;
                acpa_dsp_reset(dev);
                timer_disable(&dev->timer);
            } else if (run && !dev->run) {
                dev->run = 1;
                acpa_dsp_reset(dev);
                acpa_timer_update(dev);
            }
            if (dev->run) {
                if (!(val & 0x02)) /* HREQACK */
                    dev->hst_req = 0;
                if (!(val & 0x08)) { /* TMS INT */
                    dev->tms_int_ack = 0;
                    c25_set_int(&dev->dsp, C25_INT1);
                    acpa_timer_update(dev);
                }
            }
            acpa_update_irq(dev);
            break;
        }
        default: /* 7: clears the access error flag */
            break;
    }
}

static void
acpa_set_io(acpa_t *dev, int enable)
{
    if (dev->base) {
        io_removehandler(dev->base, 8, acpa_read, NULL, NULL, acpa_write, NULL, NULL, dev);
        dev->base = 0;
    }
    if (enable) {
        dev->base = acpa_bases[(dev->pos_regs[2] >> 1) & 3];
        io_sethandler(dev->base, 8, acpa_read, NULL, NULL, acpa_write, NULL, NULL, dev);
    }
}

static uint8_t
acpa_mca_read(const uint16_t port, void *priv)
{
    const acpa_t *dev = (acpa_t *) priv;

    return dev->pos_regs[port & 7];
}

static void
acpa_mca_write(const uint16_t port, const uint8_t val, void *priv)
{
    acpa_t *dev = (acpa_t *) priv;

    if (port < 0x102)
        return;

    acpa_log("acpa_mca_write: port=%04x val=%02x\n", port, val);

    if (port == 0x102) {
        if (dev->irq_asserted) {
            picintc(1 << dev->irq);
            dev->irq_asserted = 0;
        }
        dev->pos_regs[2] = val;
        dev->irq         = acpa_irqs[(val >> 3) & 7];
        acpa_set_io(dev, val & 1);
        acpa_update_irq(dev);
    } else
        dev->pos_regs[port & 7] = val;
}

static uint8_t
acpa_mca_feedb(void *priv)
{
    const acpa_t *dev = (acpa_t *) priv;

    return dev->pos_regs[2] & 1;
}

static void
acpa_mca_reset(void *priv)
{
    acpa_t *dev = (acpa_t *) priv;

    dev->run = 0;
    timer_disable(&dev->timer);
    acpa_dsp_reset(dev);
}

static void
acpa_get_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    acpa_t       *dev = (acpa_t *) priv;
    const int     n   = dev->out_wr;

    if ((n > 0) && (len > 0)) {
        const double ratio = (double) n / (double) len;

        for (uint16_t i = 0; i < len; i++) {
            const double p0 = i * ratio;
            const double p1 = (i + 1) * ratio;
            double       sl = 0.0;
            double       sr = 0.0;
            int          j  = (int) p0;

            while ((j < n) && ((double) j < p1)) {
                const double a = ((double) j > p0) ? (double) j : p0;
                const double b = ((double) (j + 1) < p1) ? (double) (j + 1) : p1;
                const double w = b - a;

                sl += w * dev->out_l[j];
                sr += w * dev->out_r[j];
                j++;
            }
            buffer[i * 2]     += (int32_t) (sl / ratio);
            buffer[i * 2 + 1] += (int32_t) (sr / ratio);
        }
    }
    dev->out_wr = 0;
}

static void *
acpa_init(UNUSED(const device_t *info))
{
    acpa_t *dev = calloc(1, sizeof(acpa_t));

    dev->tick_period  = (double) TIMER_USEC * (1000000.0 / ACPA_SAMPLE_RATE);
    dev->cyc_per_tick = ACPA_DSP_CYCLES / ACPA_SAMPLE_RATE;

    dev->pos_regs[0] = 0x6c;
    dev->pos_regs[1] = 0x6e;

    dev->dsp.priv       = dev;
    dev->dsp.prog_read  = acpa_dsp_prog_read;
    dev->dsp.prog_write = acpa_dsp_prog_write;
    dev->dsp.data_read  = acpa_dsp_data_read;
    dev->dsp.data_write = acpa_dsp_data_write;
    dev->dsp.io_read    = acpa_dsp_io_read;
    dev->dsp.io_write   = acpa_dsp_io_write;
    dev->dsp.bio        = acpa_dsp_bio;

    dev->irq = acpa_irqs[0];
    acpa_dsp_reset(dev);

    timer_add(&dev->timer, acpa_tick, dev, 0);

    mca_add(acpa_mca_read, acpa_mca_write, acpa_mca_feedb, acpa_mca_reset, dev);

    sound_add_handler(acpa_get_buffer, dev);
    sound_in_add_handler(acpa_put_buffer, dev);

    return dev;
}

static void
acpa_close(void *priv)
{
    acpa_t *dev = (acpa_t *) priv;

    acpa_adc_input(dev, 0);
    free(dev);
}

const device_t acpa_mca_device = {
    .name          = "IBM M-ACPA/A (Audio Capture & Playback Adapter)",
    .internal_name = "acpa_mca",
    .flags         = DEVICE_MCA | DEVICE_AUDIO_IN,
    .local         = 0,
    .init          = acpa_init,
    .close         = acpa_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
