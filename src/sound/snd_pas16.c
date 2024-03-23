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
#include <86box/snd_mpu401.h>
#include <86box/sound.h>
#include <86box/snd_opl.h>
#include <86box/snd_sb.h>
#include <86box/snd_sb_dsp.h>
#include <86box/plat_unused.h>

/*      Original PAS uses
                2 x OPL2
                PIT - sample rate/count
                LMC835N/LMC1982 - mixer
                YM3802 - MIDI Control System


        9A01 - IO base
                base >> 2

        All below + IO base

        B89 - interrupt status / clear
                bit 2 - sample rate
                bit 3 - PCM
                bit 4 - MIDI

        B88 - Audio mixer control register

        B8A - Audio filter control
                bit 5 - mute?

        B8B - interrupt mask / board ID
                bits 5-7 - board ID (read only on PAS16)

        F88 - PCM data (low)

        F89 - PCM data (high)

        F8A - PCM control?
                bit 4 - input/output select (1 = output)
                bit 5 - mono/stereo select
                bit 6 - PCM enable

        1388-138b - PIT clocked at 1193180 Hz
                1388 - sample rate
                1389 - sample count

        178b -
        2789 - board revision

        8389 -
                bit 2 - 8/16 bit

        BF88 - wait states

        EF8B -
                bit 3 - 16 bits okay ?

        F388 -
                bit 6 - joystick enable

        F389 -
                bits 0-2 - DMA

        F38A -
                bits 0-3 - IRQ

        F788 -
                bit 1 - SB emulation
                bit 0 - MPU401 emulation

        F789 - SB base addr
                bits 0-3 - addr bits 4-7

        FB8A - SB IRQ/DMA
                bits 3-5 - IRQ
                bits 6-7 - DMA

        FF88 - board model
                3 = PAS16
*/

typedef struct pas16_t {
    uint16_t base;

    int irq;
    int dma;

    uint8_t audiofilt;

    uint8_t audio_mixer;

    uint8_t compat;
    uint8_t compat_base;

    uint8_t enhancedscsi;

    uint8_t io_conf_1;
    uint8_t io_conf_2;
    uint8_t io_conf_3;
    uint8_t io_conf_4;

    uint8_t irq_stat;
    uint8_t irq_ena;

    uint8_t  pcm_ctrl;
    uint16_t pcm_dat;

    uint16_t pcm_dat_l;
    uint16_t pcm_dat_r;

    uint8_t sb_irqdma;

    int stereo_lr;

    uint8_t sys_conf_1;
    uint8_t sys_conf_2;
    uint8_t sys_conf_3;
    uint8_t sys_conf_4;
    uint8_t waitstates;
    uint8_t midi_ctrl;
    uint8_t midi_stat;
    uint8_t midi_data;
    uint8_t fifo_stat;
    int     midi_r;
    int     midi_w;
    int     midi_uart_out;
    int     midi_uart_in;
    uint8_t midi_queue[256];
    int     sysex;

    fm_drv_t opl;
    sb_dsp_t dsp;
    mpu_t *mpu;
    pc_timer_t timer;

    int16_t pcm_buffer[2][SOUNDBUFLEN];

    int pos;

    pit_t *pit;
} pas16_t;

static void    pas16_update(pas16_t *pas16);

static int pas16_dmas[8]    = { 4, 1, 2, 3, 0, 5, 6, 7 };
static int pas16_sb_irqs[8] = { 0, 2, 3, 5, 7, 10, 11, 12 };
static int pas16_sb_dmas[8] = { 0, 1, 2, 3 };

enum {
    PAS16_INT_SAMP = 0x04,
    PAS16_INT_PCM  = 0x08,
    PAS16_INT_MIDI = 0x10,
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
    uint8_t  temp  = 0xff;
    switch (port) {
        case 0x388:
        case 0x389:
        case 0x38a:
        case 0x38b:
            temp = pas16->opl.read(port, pas16->opl.priv);
            break;

        case 0xb88:
            temp = pas16->audio_mixer;
            break;
        case 0xb89:
            temp = pas16->irq_stat;
            break;
        case 0xb8a:
            temp = pas16->audiofilt;
            break;
        case 0xb8b:
            temp = pas16->irq_ena | 0x20;
            pas16_log("IRQ Mask read=%02x.\n", temp);
            break;

        case 0xf8a:
            temp = pas16->pcm_ctrl;
            break;

        case 0x1789:
        case 0x178b:
            temp = pas16->midi_ctrl;
            break;
        case 0x178a:
        case 0x1b8a:
            temp = 0;
            if (pas16->midi_uart_in) {
                if ((pas16->midi_data == 0xaa) && (pas16->midi_ctrl & 0x04))
                    temp = pas16->midi_data;
                else {
                    temp = pas16->midi_queue[pas16->midi_r];
                    if (pas16->midi_r != pas16->midi_w) {
                        pas16->midi_r++;
                        pas16->midi_r &= 0xff;
                    }
                }
                pas16->midi_stat &= ~0x04;
                pas16_update_irq(pas16);
            }
            break;

        case 0x1b88:
            temp = pas16->midi_stat;
            break;
        case 0x1b89:
            temp = pas16->fifo_stat;
            break;

        case 0x2789: /*Board revision*/
            temp = 0x00;
            break;

        case 0x7f89:
            temp = pas16->enhancedscsi & ~0x01;
            break;

        case 0x8388:
            temp = pas16->sys_conf_1;
            break;
        case 0x8389:
            temp = pas16->sys_conf_2;
            break;
        case 0x838a:
            temp = pas16->sys_conf_3;
            break;
        case 0x838b:
            temp = pas16->sys_conf_4;
            break;

        case 0xbf88:
            temp = pas16->waitstates;
            break;
        case 0xef8b:
            temp = 0x00;
            break;

        case 0xf388:
            temp = pas16->io_conf_1;
            break;
        case 0xf389:
            temp = pas16->io_conf_2;
            pas16_log("pas16_in : set PAS DMA %i\n", pas16->dma);
            break;
        case 0xf38a:
            temp = pas16->io_conf_3;
            pas16_log("pas16_in : set PAS IRQ %i\n", pas16->irq);
            break;
        case 0xf38b:
            temp = pas16->io_conf_4;
            break;

        case 0xf788:
            temp = pas16->compat;
            break;
        case 0xf789:
            temp = pas16->compat_base;
            break;

        case 0xfb8a:
            temp = pas16->sb_irqdma;
            break;

        case 0xff88:  /*Board model*/
            temp = 0x04; /*PAS16*/
            break;
        case 0xff8b:                   /*Master mode read*/
            temp = 0x20 | 0x10 | 0x01; /*AT bus, XT/AT timing*/
            break;

        default:
            break;
    }
    pas16_log("pas16_in : port %04X return %02X  %04X:%04X\n", port, temp, CS, cpu_state.pc);
    return temp;
}

static void
pas16_out(uint16_t port, uint8_t val, void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;
    pas16_log("pas16_out : port %04X val %02X  %04X:%04X\n", port, val, CS, cpu_state.pc);
    switch (port) {
        case 0x388:
        case 0x389:
        case 0x38a:
        case 0x38b:
            pas16->opl.write(port, val, pas16->opl.priv);
            break;

        case 0xb88:
            pas16->audio_mixer = val;
            break;
        case 0xb89:
            pas16->irq_stat &= ~val;
            break;
        case 0xb8a:
            pas16_update(pas16);
            pas16->audiofilt = val;
            break;
        case 0xb8b:
            pas16->irq_ena = val & 0x1f;
            break;

        case 0xf88:
            pas16_update(pas16);
            pas16->pcm_dat = (pas16->pcm_dat & 0xff00) | val;
            break;
        case 0xf89:
            pas16_update(pas16);
            pas16->pcm_dat = (pas16->pcm_dat & 0x00ff) | (val << 8);
            break;
        case 0xf8a:
            if ((val & PAS16_PCM_ENA) && !(pas16->pcm_ctrl & PAS16_PCM_ENA)) /*Guess*/
                pas16->stereo_lr = 0;

            pas16->pcm_ctrl = val;
            break;

        case 0x1789:
        case 0x178b:
            pas16->midi_ctrl = val;
            if ((val & 0x60) == 0x60) {
                pas16->midi_uart_out = 0;
                pas16->midi_uart_in = 0;
            } else if (val & 0x18) {
                pas16->midi_uart_out = 1;
            } else if (val & 0x04)
                pas16->midi_uart_in = 1;
            else
                pas16->midi_uart_out = 1;

            pas16_update_irq(pas16);
            break;
        case 0x178a:
        case 0x1b8a:
            pas16->midi_data = val;
            pas16_log("UART OUT=%d.\n", pas16->midi_uart_out);
            if (pas16->midi_uart_out)
                midi_raw_out_byte(val);
            break;

        case 0x1b88:
            pas16->midi_stat = val;
            pas16_update_irq(pas16);
            break;
        case 0x1b89:
            pas16->fifo_stat = val;
            break;

        case 0x7f89:
            pas16->enhancedscsi = val;
            break;

        case 0x8388:
            if ((val & 0xc0) && !(pas16->sys_conf_1 & 0xc0)) {
                pas16_log("Reset.\n");
                picintc(1 << pas16->irq);
                val = 0x00;
            }
            pas16->sys_conf_1 = val;
            break;
        case 0x8389:
            pas16->sys_conf_2 = val;
            break;
        case 0x838a:
            pas16->sys_conf_3 = val;
            break;
        case 0x838b:
            pas16->sys_conf_4 = val;
            break;

        case 0xbf88:
            pas16->waitstates = val;
            break;

        case 0xf388:
            pas16->io_conf_1 = val;
            break;
        case 0xf389:
            pas16->io_conf_2 = val;
            pas16->dma       = pas16_dmas[val & 0x7];
            pas16_log("pas16_out : set PAS DMA %i\n", pas16->dma);
            break;
        case 0xf38a:
            pas16->io_conf_3 = val;
            pas16->irq       = val & 0x0f;
            if (pas16->irq <= 6) {
                pas16->irq++;
            } else if ((pas16->irq > 6) && (pas16->irq < 0x0b))
                pas16->irq += 3;
            else
                pas16->irq += 4;

            pas16_log("pas16_out : set PAS IRQ %i, val=%02x\n", pas16->irq, val & 0x0f);
            break;
        case 0xf38b:
            pas16->io_conf_4 = val;
            break;

        case 0xf788:
            pas16->compat = val;
            if (pas16->compat & 0x02)
                sb_dsp_setaddr(&pas16->dsp, ((pas16->compat_base & 0xf) << 4) | 0x200);
            else
                sb_dsp_setaddr(&pas16->dsp, 0);
            if (pas16->compat & 0x01)
                mpu401_change_addr(pas16->mpu, ((pas16->compat_base & 0xf0) | 0x300));
            else
                mpu401_change_addr(pas16->mpu, 0);
            break;
        case 0xf789:
            pas16->compat_base = val;
            if (pas16->compat & 0x02)
                sb_dsp_setaddr(&pas16->dsp, ((pas16->compat_base & 0xf) << 4) | 0x200);
            if (pas16->compat & 0x01)
                mpu401_change_addr(pas16->mpu, ((pas16->compat_base & 0xf0) | 0x300));
            break;

        case 0xfb8a:
            pas16->sb_irqdma = val;
            sb_dsp_setirq(&pas16->dsp, pas16_sb_irqs[(val >> 3) & 7]);
            sb_dsp_setdma8(&pas16->dsp, pas16_sb_dmas[(val >> 6) & 3]);
            pas16_log("pas16_out : set SB IRQ %i DMA %i.\n", pas16_sb_irqs[(val >> 3) & 7], pas16_sb_dmas[(val >> 6) & 3]);
            break;

        default:
            pas16_log("pas16_out : unknown %04X\n", port);
    }
}

static uint8_t
pas16_readdma(pas16_t *pas16)
{
    return dma_channel_read(pas16->dma);
}

static void
pas16_pcm_poll(void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;
    pit_t *pit = (pit_t *) pas16->pit;
    int data;
    uint16_t temp = 0x0000;

    pas16_update(pas16);
    if (pit->counters[0].m & 0x02) {
        if (pit->counters[0].l & 0xff) {
            if (pas16->dma >= 5)
                timer_advance_u64(&pas16->timer, (pit->counters[0].l & 0xff) * (PITCONST << 1ULL));
            else
                timer_advance_u64(&pas16->timer, (pit->counters[0].l & 0xff) * PITCONST);
        } else {
            if (pas16->dma >= 5)
                timer_advance_u64(&pas16->timer, 0x100 * (PITCONST << 1ULL));
            else
                timer_advance_u64(&pas16->timer, 0x100 * PITCONST);
        }
    }

    pas16_update_irq(pas16);

    pas16->irq_stat |= PAS16_INT_SAMP;
    if (pas16->irq_ena & PAS16_INT_SAMP) {
        pas16_log("INT SAMP.\n");
        picint(1 << pas16->irq);
    }

    /*Update sample rate counter*/
    pas16_log("T1=%d, master bit 1=%x, counter0=%d, counter1=%d, pcm dma ena=%02x 16bit?=%02x.\n", pit->counters[1].enable, pit->counters[0].m & 0x02, pit->counters[0].l, pit->counters[1].l, pas16->pcm_ctrl & 0xc0, pas16->sys_conf_2 & PAS16_SC2_16BIT);
    if (pit->counters[1].enable) {
        if ((pas16->pcm_ctrl & (PAS16_PCM_ENA | PAS16_PCM_DMA_ENA))) {
            if (pas16->sys_conf_2 & PAS16_SC2_16BIT) {
                data = pas16_readdma(pas16) << 8;
                data |= pas16_readdma(pas16);
                temp = data;
            } else {
                data = pas16_readdma(pas16);
                temp = (data ^ 0x80) << 8;
            }

            if (pas16->sys_conf_2 & PAS16_SC2_MSBINV)
                temp ^= 0x8000;
            if (pas16->pcm_ctrl & PAS16_PCM_MONO)
                pas16->pcm_dat_l = pas16->pcm_dat_r = temp;
            else {
                if (pas16->stereo_lr)
                    pas16->pcm_dat_r = temp;
                else
                    pas16->pcm_dat_l = temp;

                pas16->stereo_lr = !pas16->stereo_lr;
            }
        }
        if (pas16->sys_conf_2 & PAS16_SC2_16BIT) {
            pit->counters[1].lback -= 2;
            if (!pit->counters[1].lback) {
                if (pit->counters[1].m & 0x02) {
                    if (pit->counters[1].lback2 & 0xfffe)
                        pit->counters[1].lback = pit->counters[1].lback2 & 0xfffe;
                    else
                        pit->counters[1].lback = 0;
                } else {
                    pit->counters[1].lback = 0;
                    pit->counters[1].enable = 0;
                }
                pas16_log("16-bit: New counter=%d, mode=%x.\n", pit->counters[1].lback, pit->counters[1].m & 0x03);
                pas16->irq_stat |= PAS16_INT_PCM;
                if (pas16->irq_ena & PAS16_INT_PCM) {
                    pas16_log("16-bit: INT PCM.\n");
                    picint(1 << pas16->irq);
                }
            }
        } else {
            pit->counters[1].lback--;
            if (!pit->counters[1].lback) {
                if (pit->counters[1].m & 0x02) {
                    if (pit->counters[1].lback2 & 0xffff)
                        pit->counters[1].lback = pit->counters[1].lback2 & 0xffff;
                    else
                        pit->counters[1].lback = 0;
                } else {
                    pit->counters[1].lback = 0;
                    pit->counters[1].enable = 0;
                }
                pas16_log("8-bit: New counter=%d, mode=%x.\n", pit->counters[1].lback, pit->counters[1].m & 0x03);
                pas16->irq_stat |= PAS16_INT_PCM;
                if (pas16->irq_ena & PAS16_INT_PCM) {
                    pas16_log("8-bit: INT PCM.\n");
                    picint(1 << pas16->irq);
                }
            }
        }
    }
}

static void
pas16_pit_timer0(int new_out, UNUSED(int old_out), void *priv)
{
    pit_t *pit = (pit_t *)priv;
    pas16_t *pas16 = (pas16_t *)pit->dev_priv;

    pas16_log("PAS16 pit timer0 out=%x, cnt0=%d, cnt1=%d.\n", new_out, pit->counters[0].l, pit->counters[1].l);
    pit_ctr_set_clock(&pit->counters[0], new_out, pit);
    pit->counters[1].enable = new_out;
    if (!timer_is_enabled(&pas16->timer)) {
        if (pas16->dma >= 5)
            timer_set_delay_u64(&pas16->timer, (pit->counters[0].l & 0xff) * (PITCONST << 1ULL));
        else
            timer_set_delay_u64(&pas16->timer, (pit->counters[0].l & 0xff) * PITCONST);
    }
}

static void
pas16_out_base(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;

    for (uint32_t addr = 0x000; addr < 0x10000; addr += 0x400) {
        if (addr != 0x1000) {
            io_removehandler(pas16->base + addr, 0x0004, pas16_in, NULL, NULL, pas16_out, NULL, NULL, pas16);
            io_sethandler(pas16->base + addr, 0x0004, pas16_in, NULL, NULL, pas16_out, NULL, NULL, pas16);
        }
    }
    pit_handler(0, pas16->base + 0x1000, 0x0004, pas16->pit);
    pit_handler(1, pas16->base + 0x1000, 0x0004, pas16->pit);

    pas16->base = val << 2;
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
            pas16->pcm_buffer[0][pas16->pos] = (int16_t) pas16->pcm_dat_l;
            pas16->pcm_buffer[1][pas16->pos] = (int16_t) pas16->pcm_dat_r;
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
        buffer[c] += (int16_t) (sb_iir(0, c & 1, (double) pas16->dsp.buffer[c]) / 1.3) / 2;
        buffer[c] += (pas16->pcm_buffer[c & 1][c >> 1] / 2);
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
    pas16_t *pas16 = (pas16_t *) priv;

    /* TODO: In PAS16 mode:
        pit_change_pas16_const(prescale);
        pit_set_pit_const(pas16->pit, PAS16CONST);
     */

    pit_set_pit_const(pas16->pit, PITCONST);
}

static void
pas16_reset(void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;

    /* TODO: Reset the entire PAS16 state here. */
    pit_set_pit_const(pas16->pit, PITCONST);
}

static void *
pas16_init(UNUSED(const device_t *info))
{
    pas16_t *pas16 = malloc(sizeof(pas16_t));
    memset(pas16, 0, sizeof(pas16_t));

    fm_driver_get(FM_YMF262, &pas16->opl);
    sb_dsp_set_real_opl(&pas16->dsp, 1);
    sb_dsp_init(&pas16->dsp, SB2, SB_SUBTYPE_DEFAULT, pas16);
    pas16->mpu = (mpu_t *) malloc(sizeof(mpu_t));
    memset(pas16->mpu, 0, sizeof(mpu_t));
    mpu401_init(pas16->mpu, 0, 0, M_UART, device_get_config_int("receive_input401"));
    sb_dsp_set_mpu(&pas16->dsp, pas16->mpu);

    pas16->pit = device_add(&i8254_ext_io_device);
    pit_set_pit_const(pas16->pit, PITCONST);
    pas16->pit->dev_priv = pas16;
    pas16->irq = 10;
    pas16->dma = 3;
    pas16->base = 0x0388;

    io_sethandler(0x9a01, 0x0001, NULL, NULL, NULL, pas16_out_base, NULL, NULL, pas16);
    pit_ctr_set_out_func(pas16->pit, 0, pas16_pit_timer0);
    pit_ctr_set_using_timer(pas16->pit, 0, 1);
    pit_ctr_set_using_timer(pas16->pit, 1, 0);
    pit_ctr_set_using_timer(pas16->pit, 2, 0);

    timer_add(&pas16->timer, pas16_pcm_poll, pas16, 0);

    sound_add_handler(pas16_get_buffer, pas16);
    music_add_handler(pas16_get_music_buffer, pas16);

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, pas16_input_msg, pas16_input_sysex, pas16);

    return pas16;
}

static void
pas16_close(void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;

    free(pas16);
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
        .description = "Receive input (PAS16 MIDI)",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t pas16_device = {
    .name          = "Pro Audio Spectrum 16",
    .internal_name = "pas16",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0,
    .init          = pas16_init,
    .close         = pas16_close,
    .reset         = pas16_reset,
    { .available = NULL },
    .speed_changed = pas16_speed_changed,
    .force_redraw  = NULL,
    .config        = pas16_config
};
