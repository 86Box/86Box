/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Tandy Sensation 1 (25-1650)/VIS custom audio emulation.
 *
 *          Combination of a Yamaha OPL3, MMA (YMZ263B) and the undocumented
 *          VIS 16-bit DAC/mixer hardware. The mixer is based around a pair of
 *          Fujitsu MB87077s accessed through two sets of indexed registers on
 *          the DAC.
 *
 *          Despite the presence of both an OPL3 and an MMA the hardware is
 *          not Adlib Gold-compatible (it lacks the EEPROM, Philips TDA8425
 *          volume/tone control and the Adlib Gold control regsisters.)
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          TheCollector1995, <mariogplayer@gmail.com>
 *          win2kgamer
 *
 *          Copyright 2013-2020 Sarah Walker.
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2018-2025 TheCollector1995.
 *          Copyright 2026 win2kgamer
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/filters.h>
#include <86box/io.h>
#include <86box/midi.h>
#include <86box/timer.h>
#include <86box/pic.h>
#include <86box/sound.h>
#include <86box/snd_opl.h>
#include <86box/plat_unused.h>
#include "cpu.h"
#include <86box/log.h>

#ifdef ENABLE_SENSATION_LOG
int sensation_do_log = ENABLE_SENSATION_LOG;

static void
sensation_log(void *priv, const char *fmt, ...)
{
    if (sensation_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define sensation_log(fmt, ...)
#endif

static int visdac_vols_6bits[64];

typedef struct sensation_t {
    int mma_irq;
    int mma_dma;
    int visdac_irq;
    int visdac_dma;

    int     sensation_mma_addr;
    uint8_t sensation_mma_regs[2][0xe];

    int     sensation_mma_enable[2];
    uint8_t sensation_mma_fifo[2][256];
    int     sensation_mma_fifo_start[2];
    int     sensation_mma_fifo_end[2];
    uint8_t sensation_mma_status;

    int16_t sensation_mma_out[2];
    int     sensation_mma_intpos[2];

    pc_timer_t sensation_mma_timer_count;

    uint8_t sensation_midi_ctrl;
    uint8_t midi_queue[16];
    int     midi_r;
    int     midi_w;
    int     uart_in;
    int     uart_out;
    int     sysex;

    struct {
        int timer0_latch;
        int timer0_count;
        int timerbase_latch;
        int timerbase_count;
        int timer1_latch;
        int timer1_count;
        int timer2_latch;
        int timer2_count;
        int timer2_read;

        int voice_count[2];
        int voice_latch[2];
    } sensation_mma;

    fm_drv_t opl;

    int16_t mma_buffer[2][SOUNDBUFLEN];

    int pos;

    int finish_dma;
    int mma_irq_status;

    uint8_t visdac_regs[16];
    uint8_t visdac_iregs0[16];
    uint8_t visdac_iregs1[16];
    uint8_t visdac_mode;
    uint8_t visdac_ctrl;
    uint8_t visdac_dma_ff;
    uint32_t visdac_dma_data;
    uint16_t visdac_freq;

    pc_timer_t visdac_timer_count;
    uint64_t visdac_timer_latch;
    int visdac_enable;

    int16_t visdac_buffer[SOUNDBUFLEN * 2];
    int visdac_pos;
    uint8_t visdac_playback_pos : 1;
    int visdac_irq_status;
    uint8_t visdac_dma_nodata;

    int16_t visdac_out_l;
    int16_t visdac_out_r;

    int visdac_count;

    double cd_vol_l;
    double cd_vol_r;
    int    fm_vol_l;
    int    fm_vol_r;
    int    wave_vol_l;
    int    wave_vol_r;

    void * log; /* New logging system */
} sensation_t;

void sensation_mma_timer_poll(void *priv);
void sensation_mma_update(sensation_t *dev);

void
sensation_filter_cd_audio(int channel, double *buffer, void *priv)
{
    sensation_t *dev = (sensation_t *) priv;

    const double cd_vol = channel ? dev->cd_vol_r : dev->cd_vol_l;
    double       c      = ((*buffer  * cd_vol / 4.0)) / 65536.0;

    *buffer = c;
}

void
sensation_visdac_update(sensation_t *dev)
{
    for (; dev->visdac_pos < sound_pos_global; dev->visdac_pos++) {
        dev->visdac_buffer[dev->visdac_pos * 2]     = dev->visdac_out_l;
        dev->visdac_buffer[dev->visdac_pos * 2 + 1] = dev->visdac_out_r;
    }
}

static uint32_t
sensation_visdac_dmaread(sensation_t *dev, int channel)
{
    uint32_t ret = 0;

    if (dev->visdac_dma_ff) {
        ret = (dev->visdac_dma_data & 0xff00) >> 8;
        ret |= (dev->visdac_dma_data & 0xffff0000);
    } else {
        dev->visdac_dma_data = dma_channel_read(dev->visdac_dma);

        if (dev->visdac_dma_data == DMA_NODATA) {
            sensation_log(dev->log, "VISDAC DMA: no data!\n");
            dev->visdac_dma_nodata = 1;
            return DMA_NODATA;
        } else
            dev->visdac_dma_nodata = 0;

        ret = dev->visdac_dma_data & 0xff;
    }

    dev->visdac_dma_ff = !dev->visdac_dma_ff;

    return ret;
}

static void
sensation_visdac_updatefreq(sensation_t *dev)
{
    uint8_t rate = ((dev->visdac_mode >> 5) & 0x03);
    sensation_log(dev->log, "VISDAC frequency update: val %02X\n", rate);

    switch (rate) {
        case 0x00: /* 44KHz */
            dev->visdac_freq = 44100;
            break;
        case 0x01: /* 22KHz */
            dev->visdac_freq = 22050;
            break;
        case 0x02: /* 11KHz */
            dev->visdac_freq = 11025;
            break;
        case 0x03: /* 5.0125KHz? VISBIOS.DOC mentions this sample rate being supported */
            dev->visdac_freq = 5012;
            break;
        default:
            break;
    }

    sensation_log(dev->log, "VISDAC frequency set to %i\n", dev->visdac_freq);
    dev->visdac_timer_latch = (uint64_t) ((double) TIMER_USEC * (1000000.0 / (double) dev->visdac_freq));
}

static void
sensation_visdac_poll(void *priv)
{
    sensation_t *dev = (sensation_t *) priv;

    if (dev->visdac_timer_latch) {
        timer_advance_u64(&dev->visdac_timer_count, dev->visdac_timer_latch);
    }
    else
        timer_advance_u64(&dev->visdac_timer_count, (uint64_t) ((double) TIMER_USEC * 1000));

    sensation_visdac_update(dev);

    if (dev->visdac_mode & 0x10) {

        int32_t temp;
        uint8_t format;

        format = (dev->visdac_mode & 0x88);

        switch (format) {
            case 0x80: /* 8-bit Mono PCM */
                dev->visdac_out_l = dev->visdac_out_r = (int16_t) ((sensation_visdac_dmaread(dev, dev->visdac_dma) ^ 0x80) << 8);
                if (dev->visdac_dma_nodata) {
                    dev->visdac_out_l = dev->visdac_out_r = 0;
                }
                dev->visdac_playback_pos++;
                break;
            case 0x00: /* 8-bit Stereo PCM */
                dev->visdac_out_l = (int16_t) ((sensation_visdac_dmaread(dev, dev->visdac_dma) ^ 0x80) << 8);
                dev->visdac_out_r = (int16_t) ((sensation_visdac_dmaread(dev, dev->visdac_dma) ^ 0x80) << 8);
                if (dev->visdac_dma_nodata) {
                    dev->visdac_out_l = dev->visdac_out_r = 0;
                }
                break;
            case 0x88: /* 16-bit Mono PCM */
                temp = (int32_t) sensation_visdac_dmaread(dev, dev->visdac_dma);
                dev->visdac_out_l = dev->visdac_out_r = (int16_t) ((sensation_visdac_dmaread(dev, dev->visdac_dma) << 8) | temp);
                if (dev->visdac_dma_nodata) {
                    dev->visdac_out_l = dev->visdac_out_r = 0;
                }
                break;
            case 0x08: /* 16-bit Stereo PCM */
                temp = (int32_t) sensation_visdac_dmaread(dev, dev->visdac_dma);
                dev->visdac_out_l = (int16_t) ((sensation_visdac_dmaread(dev, dev->visdac_dma) << 8) | temp);
                temp = (int32_t) sensation_visdac_dmaread(dev, dev->visdac_dma);
                dev->visdac_out_r = (int16_t) ((sensation_visdac_dmaread(dev, dev->visdac_dma) << 8) | temp);
                if (dev->visdac_dma_nodata) {
                    dev->visdac_out_l = dev->visdac_out_r = 0;
                }
                break;
            default:
                break;
        }
        if (dev->wave_vol_l == 0)
            dev->visdac_out_l = 0;
        else
            dev->visdac_out_l = (int16_t) ((dev->visdac_out_l * dev->wave_vol_l) >> 16);
        if (dev->wave_vol_r == 0)
            dev->visdac_out_r = 0;
        else
            dev->visdac_out_r = (int16_t) ((dev->visdac_out_r * dev->wave_vol_r) >> 16);
        if (dev->visdac_count < 0) {
            dev->visdac_count = ((dev->visdac_regs[0x0e] << 8) | dev->visdac_regs[0x0c]);
            dev->visdac_ctrl |= 0x04;
            if (dev->visdac_ctrl & 0x02) {
                sensation_log(dev->log, "VISDAC IRQ fired\n");
                dev->visdac_irq_status = 1;
                picint(1 << dev->visdac_irq);
            }
        }
        switch (format) {
            case 0x80: /* 8-bit Mono */
                if (!(dev->visdac_playback_pos & 0x01))
                    dev->visdac_count--;
                break;
            case 0x00: /* 8-bit Stereo */
            case 0x88: /* 16-bit Mono */
                    dev->visdac_count--;
                    break;
            case 0x08: /* 16-bit Stereo */
                    dev->visdac_count -= 2;
                    break;
            default:
                    break;
        }
    } else {
        dev->visdac_out_l = dev->visdac_out_r = 0;
    }
}

void
sensation_update_mma_irq_status(sensation_t *dev)
{
    uint8_t interrupt = 0;

    if (!(dev->sensation_mma_regs[0][8] & 0x10) && (dev->sensation_mma_status & 0x10)) /*Timer 0*/
        interrupt = 1;
    if (!(dev->sensation_mma_regs[0][8] & 0x20) && (dev->sensation_mma_status & 0x20)) /*Timer 1*/
        interrupt = 1;
    if (!(dev->sensation_mma_regs[0][8] & 0x40) && (dev->sensation_mma_status & 0x40)) /*Timer 2*/
        interrupt = 1;
    if (!(dev->sensation_mma_regs[0][0xd] & 0x01) && (dev->sensation_mma_status & 0x04))
        interrupt = 1;
    if (!(dev->sensation_mma_regs[0][0xd] & 0x04) && (dev->sensation_mma_status & 0x08))
        interrupt = 1;
    if (!(dev->sensation_mma_regs[0][0xd] & 0x10) && (dev->sensation_mma_status & 0x80))
        interrupt = 1;
    if ((dev->sensation_mma_status & 0x01) && !(dev->sensation_mma_regs[0][0xc] & 2))
        interrupt = 1;
    if ((dev->sensation_mma_status & 0x02) && !(dev->sensation_mma_regs[1][0xc] & 2))
        interrupt = 1;

    if (interrupt && !dev->mma_irq_status) {
        picint(1 << dev->mma_irq);
        dev->mma_irq_status = 1;
    }
    else if (!interrupt && dev->mma_irq_status) {
        picintc(1 << dev->mma_irq);
        dev->mma_irq_status = 0;
    }
}

void
sensation_getsamp_dma(sensation_t *dev, int channel)
{
    int dma_dat;

    dev->finish_dma = 0;

    if ((dev->sensation_mma_regs[channel][0xc] & 0x60) && (((dev->sensation_mma_fifo_end[channel] - dev->sensation_mma_fifo_start[channel]) & 255) >= 127)) {
        dev->finish_dma = 1;
        return;
    }

    dma_set_drq(dev->mma_dma, 1);
    dma_dat = dma_channel_read(dev->mma_dma);
    if (dma_dat == DMA_NODATA)
        return;

    dev->sensation_mma_fifo[channel][dev->sensation_mma_fifo_end[channel]] = dma_dat;
    dev->sensation_mma_fifo_end[channel] = (dev->sensation_mma_fifo_end[channel] + 1) & 255;
    if (dev->sensation_mma_regs[channel][0xc] & 0x60) {
        dma_dat = dma_channel_read(dev->mma_dma);
        if (dma_dat == DMA_NODATA)
            return;
        dev->sensation_mma_fifo[channel][dev->sensation_mma_fifo_end[channel]] = dma_dat;
        dev->sensation_mma_fifo_end[channel]                                   = (dev->sensation_mma_fifo_end[channel] + 1) & 255;
    }
    if (((dev->sensation_mma_fifo_end[channel] - dev->sensation_mma_fifo_start[channel]) & 255) >= dev->sensation_mma_intpos[channel]) {
        dev->sensation_mma_status &= ~(0x01 << channel);
        sensation_update_mma_irq_status(dev);
        dma_set_drq(dev->mma_dma, 0);
    }
}

void
sensation_visdac_write(uint16_t port, uint8_t val, void *priv)
{
    sensation_t *visdac = (sensation_t *) priv;

    sensation_log(visdac->log, "[%04X:%08X] Sensation VISDAC Write: port = %02X, val = %02X\n", CS, cpu_state.pc, port, val);

    switch (port & 0xF) {
        case 0x00: /* Mode/Enable register */
            visdac->visdac_regs[0x00] = val;
            visdac->visdac_mode = val;
            sensation_visdac_updatefreq(visdac);
            if (!(visdac->visdac_mode & 0x10)) {
                sensation_log(visdac->log, "VISDAC timer disable\n");
                timer_disable(&visdac->visdac_timer_count);
                visdac->visdac_out_l = visdac->visdac_out_r = 0;
            }
            if (visdac->visdac_mode & 0x10) {
                sensation_log(visdac->log, "VISDAC timer enable\n");
                if (visdac->visdac_timer_latch)
                    timer_set_delay_u64(&visdac->visdac_timer_count, visdac->visdac_timer_latch);
                else
                    timer_set_delay_u64(&visdac->visdac_timer_count, (uint64_t) ((double) TIMER_USEC * 1000));
            }
            break;
        case 0x01:
            visdac->visdac_regs[0x01] = val;
            break;
        case 0x02: /* Indirect register set 0 data */
            visdac->visdac_regs[0x02] = val;
            visdac->visdac_iregs0[visdac->visdac_regs[0x03] & 0x0f] = val;
            switch (visdac->visdac_regs[0x03]) {
                case 0: /* CD L Mute */
                    visdac->cd_vol_l = 0;
                    break;
                case 1: /* CD R Mute */
                    visdac->cd_vol_r = 0;
                    break;
                case 2: /* MIDI L Mute */
                    visdac->fm_vol_l = 0;
                    break;
                case 3: /* MIDI R Mute */
                    visdac->fm_vol_r = 0;
                    break;
                case 4: /* CD L Volume */
                    if (visdac->visdac_iregs0[0x04] == 0x00)
                        visdac->cd_vol_l = 0;
                    else
                        visdac->cd_vol_l = visdac_vols_6bits[(visdac->visdac_iregs0[0x04] & 0x3f)];
                    break;
                case 5: /* CD R Volume */
                    if (visdac->visdac_iregs0[0x05] == 0x00)
                        visdac->cd_vol_r = 0;
                    else
                        visdac->cd_vol_r = visdac_vols_6bits[(visdac->visdac_iregs0[0x05] & 0x3f)];
                    break;
                case 6: /* MIDI L Volume */
                    if (visdac->visdac_iregs0[0x06] == 0x00)
                        visdac->fm_vol_l = 0;
                    else
                        visdac->fm_vol_l = visdac_vols_6bits[(visdac->visdac_iregs0[0x06] & 0x3f)];
                    break;
                case 7: /* MIDI R Volume */
                    if (visdac->visdac_iregs0[0x07] == 0x00)
                        visdac->fm_vol_r = 0;
                    else
                        visdac->fm_vol_r = visdac_vols_6bits[(visdac->visdac_iregs0[0x07] & 0x3f)];
                    break;
                default:
                    break;
            }
            break;
        case 0x03: /* Indirect register set 0 index */
            visdac->visdac_regs[0x03] = val;
            break;
        case 0x04: /* Indirect register set 1 data */
            visdac->visdac_regs[0x04] = val;
            visdac->visdac_iregs1[visdac->visdac_regs[0x05] & 0x0f] = val;
            switch (visdac->visdac_regs[0x05]) {
                case 0: /* Wave L Mute */
                    visdac->wave_vol_l = 0;
                    break;
                case 1: /* Wave R Mute */
                    visdac->wave_vol_r = 0;
                    break;
                case 2: /* Mic/Line/Phone L Mute */
                    break;
                case 3: /* Mic/Line/Phone R Mute */
                    break;
                case 4: /* Wave L Volume */
                    visdac->wave_vol_l = visdac_vols_6bits[(visdac->visdac_iregs1[0x04] & 0x3f)];
                    break;
                case 5: /* Wave R Volume */
                    visdac->wave_vol_r = visdac_vols_6bits[(visdac->visdac_iregs1[0x05] & 0x3f)];
                    break;
                case 6: /* Mic/Line/Phone L Volume */
                    break;
                case 7: /* Mic/Line/Phone R Volume */
                    break;
                default:
                    break;
            }
            break;
        case 0x05: /* Indirect register set 1 index */
            visdac->visdac_regs[0x05] = val;
            break;
        case 0x06:
            visdac->visdac_regs[0x06] = val;
            break;
        case 0x07: /* Mixer Source Select */
            /* Known valid source values:
             * 0x08 = Mic
             * 0x09 = Line
             * 0x0A = CD
             * 0x0B = Wave
             * 0x0C = Phone
             * 0x0E = MMA Wave (written when the MMA DAC is in use under Win3.1)
             *
             * 0x07 and 0x0F are written to this register during POST but seem unused by the drivers
             */
            visdac->visdac_regs[0x07] = val;
            break;
        case 0x08: /* Mixer Crossover Control */
            /* Connects the selected source's left/right channels to the left and/or right speakers
             * Any combination of the below values can be used:
             * 0x05 = Route left channel to left speaker
             * 0x10 = Route right channel to left speaker
             * 0x28 = Route left channel to right speaker
             * 0x42 = Route right channel to right speaker
             */
            visdac->visdac_regs[0x08] = val;
            break;
        case 0x09: /* Control register */
            visdac->visdac_regs[0x09] = val;
            visdac->visdac_ctrl = val;
            break;
        case 0x0a:
            visdac->visdac_regs[0x0a] = val;
            break;
        case 0x0b:
            visdac->visdac_regs[0x0b] = val;
            break;
        case 0x0c: /* Sample Count Low Byte */
            visdac->visdac_regs[0x0c] = val;
            visdac->visdac_count = (visdac->visdac_count & 0xFF00) | val;
            break;
        case 0x0d:
            visdac->visdac_regs[0x0d] = val;
            break;
        case 0x0e: /* Sample Count High Byte */
            visdac->visdac_regs[0x0e] = val;
            visdac->visdac_count = (visdac->visdac_count & 0xFF) | (val << 8);
            break;
        case 0x0f:
            visdac->visdac_regs[0x0f] = val;
            break;
        default:
            break;
    }
}

void
sensation_mma_write(uint16_t addr, uint8_t val, void *priv)
{
    sensation_t *dev = (sensation_t *) priv;

    sensation_log(dev->log, "Sensation MMA Write: port = %02X, val = %02X\n", addr, val);

    switch (addr & 7) {
        case 0 ... 3:
            dev->opl.write(addr, val, dev->opl.priv);
            break;
        case 4:
        case 6:
            dev->sensation_mma_addr = val;
            break;
        case 5:
            if (dev->sensation_mma_addr >= 0xf)
                break;
            switch (dev->sensation_mma_addr) {
                case 0x2:
                    dev->sensation_mma.timer0_latch = (dev->sensation_mma.timer0_latch & 0xff00) | val;
                    break;
                case 0x3:
                    dev->sensation_mma.timer0_latch = (dev->sensation_mma.timer0_latch & 0xff) | (val << 8);
                    break;
                case 0x4:
                    dev->sensation_mma.timerbase_latch = (dev->sensation_mma.timerbase_latch & 0xf00) | val;
                    break;
                case 0x5:
                    dev->sensation_mma.timerbase_latch = (dev->sensation_mma.timerbase_latch & 0xff) | ((val & 0xf) << 8);
                    dev->sensation_mma.timer1_latch    = val >> 4;
                    break;
                case 0x6:
                    dev->sensation_mma.timer2_latch = (dev->sensation_mma.timer2_latch & 0xff00) | val;
                    break;
                case 0x7:
                    dev->sensation_mma.timer2_latch = (dev->sensation_mma.timer2_latch & 0xff) | (val << 8);
                    break;

                case 0x8:
                    if ((val & 1) && !(dev->sensation_mma_regs[0][8] & 1)) /*Reload timer 0*/
                        dev->sensation_mma.timer0_count = dev->sensation_mma.timer0_latch;

                    if ((val & 2) && !(dev->sensation_mma_regs[0][8] & 2)) /*Reload timer 1*/
                        dev->sensation_mma.timer1_count = dev->sensation_mma.timer1_latch;

                    if ((val & 4) && !(dev->sensation_mma_regs[0][8] & 4)) /*Reload timer 2*/
                        dev->sensation_mma.timer2_count = dev->sensation_mma.timer2_latch;

                    if ((val & 8) && !(dev->sensation_mma_regs[0][8] & 8)) /*Reload base timer*/
                        dev->sensation_mma.timerbase_count = dev->sensation_mma.timerbase_latch;
                    break;

                case 0x9:
                    switch (val & 0x18) {
                        case 0x00:
                            dev->sensation_mma.voice_latch[0] = 12;
                            break; /*44100 Hz*/
                        case 0x08:
                            dev->sensation_mma.voice_latch[0] = 24;
                            break; /*22050 Hz*/
                        case 0x10:
                            dev->sensation_mma.voice_latch[0] = 48;
                            break; /*11025 Hz*/
                        case 0x18:
                            dev->sensation_mma.voice_latch[0] = 72;
                            break; /* 7350 Hz*/

                        default:
                            break;
                    }
                    if (val & 0x80) {
                        dev->sensation_mma_enable[0]   = 0;
                        dev->sensation_mma_fifo_end[0] = dev->sensation_mma_fifo_start[0] = 0;
                        dev->sensation_mma_status &= ~0x01;
                        sensation_update_mma_irq_status(dev);
                        dma_set_drq(dev->mma_dma, 0);
                    }
                    if (val & 0x01) /*Start playback*/
                    {
                        if (!(dev->sensation_mma_regs[0][0x9] & 1))
                            dev->sensation_mma.voice_count[0] = dev->sensation_mma.voice_latch[0];

                        if (dev->sensation_mma_regs[0][0xc] & 1) {
                            if (dev->sensation_mma_regs[0][0xc] & 0x80) {
                                dev->sensation_mma_enable[1]      = 1;
                                dev->sensation_mma.voice_count[1] = dev->sensation_mma.voice_latch[1];

                                while (((dev->sensation_mma_fifo_end[0] - dev->sensation_mma_fifo_start[0]) & 255) < 128) {
                                    sensation_getsamp_dma(dev, 0);
                                    sensation_getsamp_dma(dev, 1);
                                    if (dev->finish_dma)
                                        break;
                                }
                                if (((dev->sensation_mma_fifo_end[0] - dev->sensation_mma_fifo_start[0]) & 255) >= dev->sensation_mma_intpos[0]) {
                                    dev->sensation_mma_status &= ~0x01;
                                    sensation_update_mma_irq_status(dev);
                                    dma_set_drq(dev->mma_dma, 0);
                                }
                                if (((dev->sensation_mma_fifo_end[1] - dev->sensation_mma_fifo_start[1]) & 255) >= dev->sensation_mma_intpos[1]) {
                                    dev->sensation_mma_status &= ~0x02;
                                    sensation_update_mma_irq_status(dev);
                                    dma_set_drq(dev->mma_dma, 0);
                                }
                            } else {
                                while (((dev->sensation_mma_fifo_end[0] - dev->sensation_mma_fifo_start[0]) & 255) < 128) {
                                    sensation_getsamp_dma(dev, 0);
                                    if (dev->finish_dma)
                                        break;
                                }
                                if (((dev->sensation_mma_fifo_end[0] - dev->sensation_mma_fifo_start[0]) & 255) >= dev->sensation_mma_intpos[0]) {
                                    dev->sensation_mma_status &= ~0x01;
                                    sensation_update_mma_irq_status(dev);
                                    dma_set_drq(dev->mma_dma, 0);
                                }
                            }
                        }
                    }
                    dev->sensation_mma_enable[0] = val & 0x01;
                    break;

                case 0xb:
                    if (((dev->sensation_mma_fifo_end[0] - dev->sensation_mma_fifo_start[0]) & 255) < 128) {
                        dev->sensation_mma_fifo[0][dev->sensation_mma_fifo_end[0]] = val;
                        dev->sensation_mma_fifo_end[0]                             = (dev->sensation_mma_fifo_end[0] + 1) & 255;
                        if (((dev->sensation_mma_fifo_end[0] - dev->sensation_mma_fifo_start[0]) & 255) >= dev->sensation_mma_intpos[0]) {
                            dev->sensation_mma_status &= ~0x01;
                            sensation_update_mma_irq_status(dev);
                            dma_set_drq(dev->mma_dma, 0);
                        }
                    }
                    break;

                case 0xc:
                    dev->sensation_mma_intpos[0] = (7 - ((val >> 2) & 7)) * 8;
                    break;

                case 0xd:
                    dev->sensation_midi_ctrl = val & 0x3f;

                    if ((dev->sensation_midi_ctrl & 0x0f) != 0x0f) {
                        if ((dev->sensation_midi_ctrl & 0x0f) == 0x00) {
                            dev->uart_out = 0;
                            dev->uart_in  = 0;
                            dev->midi_w   = 0;
                            dev->midi_r   = 0;
                            dev->sensation_mma_status &= ~0x8c;
                        } else {
                            if (dev->sensation_midi_ctrl & 0x01)
                                dev->uart_in = 1;
                            if (dev->sensation_midi_ctrl & 0x04)
                                dev->uart_out = 1;
                            if (dev->sensation_midi_ctrl & 0x02) {
                                dev->uart_in = 0;
                                dev->midi_w  = 0;
                                dev->midi_r  = 0;
                            }
                            if (dev->sensation_midi_ctrl & 0x08)
                                dev->uart_out = 0;
                            dev->sensation_mma_status &= ~0x80;
                        }
                    } else
                        dev->sensation_mma_status &= ~0x8c;

                    sensation_update_mma_irq_status(dev);
                    break;

                case 0xe:
                    if (dev->uart_out) {
                        midi_raw_out_byte(val);

                        dev->sensation_mma_status &= ~0x08;
                        sensation_update_mma_irq_status(dev);
                    }
                    break;

                default:
                    break;
            }
            dev->sensation_mma_regs[0][dev->sensation_mma_addr] = val;
            break;
        case 7:
            if (dev->sensation_mma_addr >= 0xe)
                break;
            switch (dev->sensation_mma_addr) {
                case 0x9:
                    sensation_mma_update(dev);
                    switch (val & 0x18) {
                        case 0x00:
                            dev->sensation_mma.voice_latch[1] = 12;
                            break; /*44100 Hz*/
                        case 0x08:
                            dev->sensation_mma.voice_latch[1] = 24;
                            break; /*22050 Hz*/
                        case 0x10:
                            dev->sensation_mma.voice_latch[1] = 48;
                            break; /*11025 Hz*/
                        case 0x18:
                            dev->sensation_mma.voice_latch[1] = 72;
                            break; /* 7350 Hz*/

                        default:
                            break;
                    }
                    if (val & 0x80) {
                        dev->sensation_mma_enable[1]   = 0;
                        dev->sensation_mma_fifo_end[1] = dev->sensation_mma_fifo_start[1] = 0;
                        dev->sensation_mma_status &= ~0x02;
                        sensation_update_mma_irq_status(dev);
                        dma_set_drq(dev->mma_dma, 0);
                    }
                    if (val & 0x01) /*Start playback*/
                    {
                        if (!(dev->sensation_mma_regs[1][0x9] & 1))
                            dev->sensation_mma.voice_count[1] = dev->sensation_mma.voice_latch[1];

                        if (dev->sensation_mma_regs[1][0xc] & 1) {
                            while (((dev->sensation_mma_fifo_end[1] - dev->sensation_mma_fifo_start[1]) & 255) < 128) {
                                sensation_getsamp_dma(dev, 1);
                                if (dev->finish_dma)
                                    break;
                            }
                        }
                    }
                    dev->sensation_mma_enable[1] = val & 0x01;
                    break;

                case 0xb:
                    if (((dev->sensation_mma_fifo_end[1] - dev->sensation_mma_fifo_start[1]) & 255) < 128) {
                        dev->sensation_mma_fifo[1][dev->sensation_mma_fifo_end[1]] = val;
                        dev->sensation_mma_fifo_end[1]                             = (dev->sensation_mma_fifo_end[1] + 1) & 255;
                        if (((dev->sensation_mma_fifo_end[1] - dev->sensation_mma_fifo_start[1]) & 255) >= dev->sensation_mma_intpos[1]) {
                            dev->sensation_mma_status &= ~0x02;
                            sensation_update_mma_irq_status(dev);
                            dma_set_drq(dev->mma_dma, 0);
                        }
                    }
                    break;

                case 0xc:
                    dev->sensation_mma_intpos[1] = (7 - ((val >> 2) & 7)) * 8;
                    break;

                default:
                    break;
            }
            dev->sensation_mma_regs[1][dev->sensation_mma_addr] = val;
            break;

        default:
            break;
    }
}

static uint8_t
sensation_visdac_read(uint16_t port, void *priv)
{
    sensation_t *visdac = (sensation_t *) priv;

    uint8_t ret;
    ret = 0xff;

    switch (port & 0xF) {
        case 0x00:
            visdac->visdac_irq_status = 0;
            picintc(1 << visdac->visdac_irq);
            visdac->visdac_ctrl &= ~4;
            ret = visdac->visdac_mode;
            break;
        case 0x01:
            ret = visdac->visdac_regs[0x01];
            break;
        case 0x02:
            ret = visdac->visdac_iregs0[visdac->visdac_regs[0x03] & 0x0f];
            break;
        case 0x03:
            ret = visdac->visdac_regs[0x03];
            break;
        case 0x04:
            ret = visdac->visdac_iregs1[visdac->visdac_regs[0x05] & 0x0f];
            break;
        case 0x05:
            ret = visdac->visdac_regs[0x05];
            break;
        case 0x06:
            ret = visdac->visdac_regs[0x06];
            break;
        case 0x07:
            ret = visdac->visdac_regs[0x07];
            break;
        case 0x08:
            ret = visdac->visdac_regs[0x08];
            break;
        case 0x09:
            ret = visdac->visdac_ctrl;
            visdac->visdac_irq_status = 0;
            picintc(1 << visdac->visdac_irq);
            visdac->visdac_ctrl &= ~4;
            break;
        case 0x0a:
            ret = visdac->visdac_regs[0x0a];
            break;
        case 0x0b:
            ret = visdac->visdac_regs[0x0b];
            break;
        case 0x0c:
            ret = visdac->visdac_count & 0xFF;
            break;
        case 0x0d:
            ret = visdac->visdac_regs[0x0d];
            break;
        case 0x0e:
            ret = visdac->visdac_count >> 8;
            break;
        case 0x0f:
            ret = visdac->visdac_regs[0x0f];
            break;
        default:
            break;
    }
    sensation_log(visdac->log, "[%04X:%08X] Sensation VISDAC Read: port = %02X, ret = %02X\n", CS, cpu_state.pc, port, ret);
    return ret;
}

uint8_t
sensation_mma_read(uint16_t addr, void *priv)
{
    sensation_t *dev = (sensation_t *) priv;
    uint8_t   temp   = 0;

    switch (addr & 7) {
        case 0 ... 3:
            temp = dev->opl.read(addr, dev->opl.priv);
            break;
        case 4:
        case 6:
            temp = dev->sensation_mma_status;
            dev->sensation_mma_status &= ~0xf3;
            sensation_update_mma_irq_status(dev);
            break;
        case 5:
            if (dev->sensation_mma_addr >= 0xf)
                temp = 0xff;
            switch (dev->sensation_mma_addr) {
                case 6: /*Timer 2 low*/
                    dev->sensation_mma.timer2_read = dev->sensation_mma.timer2_count;
                    dev->sensation_mma_status |= 0x40;
                    temp = dev->sensation_mma.timer2_read & 0xff;
                    break;
                case 7: /*Timer 2 high*/
                    temp = dev->sensation_mma.timer2_read >> 8;
                    break;

                case 0xe:
                    temp = 0;
                    if (dev->uart_in) {
                        temp = dev->midi_queue[dev->midi_r];
                        if (dev->midi_r != dev->midi_w) {
                            dev->midi_r++;
                            dev->midi_r &= 0x0f;
                        }
                        dev->sensation_mma_status &= ~0x04;
                        sensation_update_mma_irq_status(dev);
                    }
                    break;

                default:
                    temp = dev->sensation_mma_regs[0][dev->sensation_mma_addr];
                    break;
            }
            break;
        case 7:
            if (dev->sensation_mma_addr >= 0xf)
                temp = 0xff;
            else
                temp = dev->sensation_mma_regs[1][dev->sensation_mma_addr];
            break;
        default:
            break;
    }
    sensation_log(dev->log, "Sensation MMA Read: port = %02X, ret = %02X\n", addr, temp);
    return temp;
}

void
sensation_mma_update(sensation_t *dev)
{
    for (; dev->pos < sound_pos_global; dev->pos++) {
        dev->mma_buffer[0][dev->pos] = dev->mma_buffer[1][dev->pos] = 0;

        if (dev->sensation_mma_regs[0][9] & 0x20)
            dev->mma_buffer[0][dev->pos] += dev->sensation_mma_out[0] / 2;
        if (dev->sensation_mma_regs[0][9] & 0x40)
            dev->mma_buffer[1][dev->pos] += dev->sensation_mma_out[0] / 2;

        if (dev->sensation_mma_regs[1][9] & 0x20)
            dev->mma_buffer[0][dev->pos] += dev->sensation_mma_out[1] / 2;
        if (dev->sensation_mma_regs[1][9] & 0x40)
            dev->mma_buffer[1][dev->pos] += dev->sensation_mma_out[1] / 2;
    }
}

void
sensation_mma_poll(sensation_t *dev, int channel)
{
    int16_t dat;

    sensation_mma_update(dev);

    if (dev->sensation_mma_fifo_start[channel] != dev->sensation_mma_fifo_end[channel]) {
        switch (dev->sensation_mma_regs[channel][0xc] & 0x60) {
            case 0x00: /*8-bit*/
                dat                                    = dev->sensation_mma_fifo[channel][dev->sensation_mma_fifo_start[channel]] * 128;
                dev->sensation_mma_out[channel]        = dat;
                dev->sensation_mma_fifo_start[channel] = (dev->sensation_mma_fifo_start[channel] + 1) & 255;
                break;

            case 0x40: /*12-bit sensible format*/
                if (((dev->sensation_mma_fifo_end[channel] - dev->sensation_mma_fifo_start[channel]) & 255) < 2)
                    return;

                dat                                    = dev->sensation_mma_fifo[channel][dev->sensation_mma_fifo_start[channel]] & 0xf0;
                dev->sensation_mma_fifo_start[channel] = (dev->sensation_mma_fifo_start[channel] + 1) & 255;
                dat |= (dev->sensation_mma_fifo[channel][dev->sensation_mma_fifo_start[channel]] << 8);
                dev->sensation_mma_fifo_start[channel] = (dev->sensation_mma_fifo_start[channel] + 1) & 255;
                dev->sensation_mma_out[channel]        = dat;
                break;

            default:
                break;
        }

        if (dev->sensation_mma_regs[channel][0xc] & 1) {
            sensation_getsamp_dma(dev, channel);
            if (dev->finish_dma)
                return;
        }
        if (((dev->sensation_mma_fifo_end[channel] - dev->sensation_mma_fifo_start[channel]) & 255) < dev->sensation_mma_intpos[channel] && !(dev->sensation_mma_status & 0x01)) {
            dev->sensation_mma_status |= (1 << channel);
            sensation_update_mma_irq_status(dev);
        }
    }
    if (dev->sensation_mma_fifo_start[channel] == dev->sensation_mma_fifo_end[channel]) {
        dev->sensation_mma_enable[channel] = 0;
    }
}

void
sensation_mma_timer_poll(void *priv)
{
    sensation_t *dev = (sensation_t *) priv;

    /*A small timer period will result in hangs.*/
    timer_advance_u64(&dev->sensation_mma_timer_count, (uint64_t) ((double) TIMER_USEC * 1.88964));

    if (dev->sensation_midi_ctrl & 0x3f) {
        if ((dev->sensation_midi_ctrl & 0x3f) != 0x3f) {
            if (dev->uart_out)
                dev->sensation_mma_status |= 0x08;
            if (dev->sensation_midi_ctrl & 0x10)
                dev->sensation_mma_status |= 0x80;
        }
        sensation_update_mma_irq_status(dev);
    }

    if (dev->sensation_mma_regs[0][8] & 0x01) /*Timer 0*/
    {
        dev->sensation_mma.timer0_count--;
        if (!dev->sensation_mma.timer0_count) {
            dev->sensation_mma.timer0_count = dev->sensation_mma.timer0_latch;
            dev->sensation_mma_status |= 0x10;
            sensation_update_mma_irq_status(dev);
        }
    }
    if (dev->sensation_mma_regs[0][8] & 0x08) /*Base timer*/
    {
        dev->sensation_mma.timerbase_count--;
        if (!dev->sensation_mma.timerbase_count) {
            dev->sensation_mma.timerbase_count = dev->sensation_mma.timerbase_latch;
            if (dev->sensation_mma_regs[0][8] & 0x02) /*Timer 1*/
            {
                dev->sensation_mma.timer1_count--;
                if (!dev->sensation_mma.timer1_count) {
                    dev->sensation_mma.timer1_count = dev->sensation_mma.timer1_latch;
                    dev->sensation_mma_status |= 0x20;
                    sensation_update_mma_irq_status(dev);
                }
            }
            if (dev->sensation_mma_regs[0][8] & 0x04) /*Timer 2*/
            {
                dev->sensation_mma.timer2_count--;
                if (!dev->sensation_mma.timer2_count) {
                    dev->sensation_mma.timer2_count = dev->sensation_mma.timer2_latch;
                    dev->sensation_mma_status |= 0x40;
                    sensation_update_mma_irq_status(dev);
                }
            }
        }
    }

    if (dev->sensation_mma_enable[0]) {
        dev->sensation_mma.voice_count[0]--;
        if (!dev->sensation_mma.voice_count[0]) {
            dev->sensation_mma.voice_count[0] = dev->sensation_mma.voice_latch[0];
            sensation_mma_poll(dev, 0);
        }
    }
    if (dev->sensation_mma_enable[1]) {
        dev->sensation_mma.voice_count[1]--;
        if (!dev->sensation_mma.voice_count[1]) {
            dev->sensation_mma.voice_count[1] = dev->sensation_mma.voice_latch[1];
            sensation_mma_poll(dev, 1);
        }
    }
}


static void
sensation_get_buffer(int32_t *buffer, int len, void *priv)
{
    sensation_t *dev = (sensation_t *) priv;
    int16_t *mma_buffer = malloc(sizeof(int16_t) * len * 2);
    if (mma_buffer == NULL)
        fatal("mma_buffer = NULL");

    int c;

    int32_t *opl_buf = dev->opl.update(dev->opl.priv);
    sensation_mma_update(dev);
    sensation_visdac_update(dev);

    for (c = 0; c < len * 2; c += 2) {
        mma_buffer[c] = (dev->mma_buffer[0][c >> 1] * dev->wave_vol_l) / 32768.0;
        mma_buffer[c + 1] = (dev->mma_buffer[1][c >> 1] * dev->wave_vol_r) / 32768.0;

        if ((dev->sensation_mma_regs[0][9] & 0x60) == 0x40)
            mma_buffer[c] = mma_buffer[c + 1];
        if ((dev->sensation_mma_regs[0][9] & 0x60) == 0x20)
            mma_buffer[c + 1] = mma_buffer[c];

        mma_buffer[c] += (opl_buf[c] * dev->fm_vol_l) / 65536.0;
        mma_buffer[c + 1] += (opl_buf[c + 1] * dev->fm_vol_r) / 65536.0;

        buffer[c] += mma_buffer[c];
        buffer[c + 1] += mma_buffer[c + 1];

        if (dev->visdac_mode & 0x10) {
            buffer[c] += dev->visdac_buffer[c];
            buffer[c + 1] += dev->visdac_buffer[c + 1];
        }
    }

    dev->opl.reset_buffer(dev->opl.priv);
    dev->pos = 0;
    dev->visdac_pos = 0;

    free(mma_buffer);
}

static void
sensation_input_msg(void *priv, uint8_t *msg, uint32_t len)
{
    sensation_t *dev = (sensation_t *) priv;

    if (dev->sysex)
        return;

    if (dev->uart_in) {
        dev->sensation_mma_status |= 0x04;

        for (uint32_t i = 0; i < len; i++) {
            dev->midi_queue[dev->midi_w++] = msg[i];
            dev->midi_w &= 0x0f;
        }

        sensation_update_mma_irq_status(dev);
    }
}

static int
sensation_input_sysex(void *priv, uint8_t *buffer, uint32_t len, int abort)
{
    sensation_t *dev = (sensation_t *) priv;

    if (abort) {
        dev->sysex = 0;
        return 0;
    }
    dev->sysex = 1;
    for (uint32_t i = 0; i < len; i++) {
        if (dev->midi_r == dev->midi_w)
            return (len - i);
        dev->midi_queue[dev->midi_w++] = buffer[i];
        dev->midi_w &= 0x0f;
    }
    dev->sysex = 0;
    return 0;
}

void *
sensation_init(const device_t *info)
{
    sensation_t *dev = calloc(1, sizeof(sensation_t));

    dev->mma_irq = 5;
    dev->mma_dma = 3;
    dev->visdac_irq = 5;
    dev->visdac_dma = 7;

    dev->log = log_open("SensationAud");

    /* Set up VIS DAC registers at 0x220-0x22F */
    io_sethandler(0x0220, 0x000F, sensation_visdac_read, NULL, NULL, sensation_visdac_write, NULL, NULL, dev);

    /* Set up MMA registers at 0x38C-0x38F */
    io_sethandler(0x388, 0x08, sensation_mma_read, NULL, NULL, sensation_mma_write, NULL, NULL, dev);
    dev->sensation_mma_enable[0] = 0;
    dev->sensation_mma_fifo_start[0] = dev->sensation_mma_fifo_end[0] = 0;

    fm_driver_get_ex(FM_YMF262, &dev->opl, 1);

    timer_add(&dev->sensation_mma_timer_count, sensation_mma_timer_poll, dev, 1);
    timer_add(&dev->visdac_timer_count, sensation_visdac_poll, dev, 1);

    sound_add_handler(sensation_get_buffer, dev);

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sensation_input_msg, sensation_input_sysex, dev);

    /* Calculate attenuation values for the 6-bit volume control */
    int c;
    double attenuation;
    for (c = 0; c < 64; c++) {
        attenuation = -31.5;
        if (c & 0x01)
            attenuation += 0.5;
        if (c & 0x02)
            attenuation += 1.0;
        if (c & 0x04)
            attenuation += 2.0;
        if (c & 0x08)
            attenuation += 4.0;
        if (c & 0x10)
            attenuation += 8.0;
        if (c & 0x20)
            attenuation += 16.0;

        attenuation = pow(10, attenuation / 10);

        visdac_vols_6bits[c] = (int) (attenuation * 65536);
    }

    /* Set up audio filters */
    sound_set_cd_audio_filter(NULL, NULL);
    sound_set_cd_audio_filter(sensation_filter_cd_audio, dev);

    return dev;
}

static void
sensation_speed_changed(void *priv)
{
    sensation_t *dev = (sensation_t *) priv;

    dev->visdac_timer_latch = (uint64_t) ((double) TIMER_USEC * (1000000.0 / (double) dev->visdac_freq));
}

void
sensation_close(void *priv)
{
    sensation_t *dev = (sensation_t *) priv;

    if (dev->log != NULL) {
        log_close(dev->log);
        dev->log = NULL;
    }

    free(dev);
}

static const device_config_t sensation_config[] = {
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

const device_t sensationaud_device = {
    .name          = "Tandy Sensation/VIS audio",
    .internal_name = "sensationaud",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = sensation_init,
    .close         = sensation_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = sensation_speed_changed,
    .force_redraw  = NULL,
    .config        = sensation_config
};
