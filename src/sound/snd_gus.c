#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <math.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/midi.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/sound.h>
#include <86box/timer.h>
#ifdef USE_GUSMAX
#    include <86box/snd_ad1848.h>
#endif /*USE_GUSMAX */
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>

enum {
    MIDI_INT_RECEIVE  = 0x01,
    MIDI_INT_TRANSMIT = 0x02,
    MIDI_INT_MASTER   = 0x80
};

enum {
    MIDI_CTRL_TRANSMIT_MASK = 0x60,
    MIDI_CTRL_TRANSMIT      = 0x20,
    MIDI_CTRL_RECEIVE       = 0x80
};

enum {
    GUS_INT_MIDI_TRANSMIT = 0x01,
    GUS_INT_MIDI_RECEIVE  = 0x02
};

enum {
    GUS_TIMER_CTRL_AUTO = 0x01
};

enum {
    GUS_CLASSIC = 0,
    GUS_MAX     = 1,
};

typedef struct gus_t {
    int reset;

    int      global;
    uint32_t addr;
    uint32_t dmaaddr;
    int      voice;
    uint32_t start[32];
    uint32_t end[32];
    uint32_t cur[32];
    uint32_t startx[32];
    uint32_t endx[32];
    uint32_t curx[32];
    int      rstart[32];
    int      rend[32];
    int      rcur[32];
    uint16_t freq[32];
    uint16_t rfreq[32];
    uint8_t  ctrl[32];
    uint8_t  rctrl[32];
    int      curvol[32];
    int      pan_l[32];
    int      pan_r[32];
    int      t1on;
    int      t2on;
    uint8_t  tctrl;
    uint16_t t1;
    uint16_t t2;
    uint16_t t1l;
    uint16_t t2l;
    uint8_t  irqstatus;
    uint8_t  irqstatus2;
    uint8_t  adcommand;
    int      waveirqs[32];
    int      rampirqs[32];
    int      voices;
    uint8_t  dmactrl;

    int32_t out_l;
    int32_t out_r;

    int16_t buffer[2][SOUNDBUFLEN];
    int     pos;

    pc_timer_t samp_timer;
    uint64_t   samp_latch;

    uint8_t *ram;
    uint32_t gus_end_ram;

    int irqnext;

    uint8_t irq_state;
    uint8_t midi_irq_state;

    pc_timer_t timer_1;
    pc_timer_t timer_2;

    uint8_t  type;

    int      irq;
    int      dma;
    int      irq_midi;
    int      dma2;
    uint16_t base;
    int      latch_enable;

    uint8_t sb_2xa;
    uint8_t sb_2xc;
    uint8_t sb_2xe;
    uint8_t sb_ctrl;
    int     sb_nmi;

    uint8_t reg_ctrl;

    uint8_t ad_status;
    uint8_t ad_data;
    uint8_t ad_timer_ctrl;

    uint8_t midi_ctrl;
    uint8_t midi_status;
    uint8_t midi_queue[64];
    uint8_t midi_data;
    int     midi_r;
    int     midi_w;
    int     uart_in;
    int     uart_out;
    int     sysex;

    uint8_t  gp1;
    uint8_t  gp2;
    uint16_t gp1_addr;
    uint16_t gp2_addr;

    uint8_t usrr;

#ifdef USE_GUSMAX
    uint8_t max_ctrl;

    ad1848_t ad1848;
#endif /*USE_GUSMAX */
} gus_t;

static int gus_gf1_irqs[8]  = { -1, 2, 5, 3, 7, 11, 12, 15 };
static int gus_midi_irqs[8] = { -1, 2, 5, 3, 7, 11, 12, 15 };
static int gus_dmas[8]      = { -1, 1, 3, 5, 6, 7, -1, -1 };

int gusfreqs[] = {
    44100, 41160, 38587, 36317, 34300, 32494, 30870, 29400, 28063, 26843, 25725, 24696,
    23746, 22866, 22050, 21289, 20580, 19916, 19293
};

double vol16bit[4096];

void
gus_update_int_status(gus_t *gus)
{
    int irq_pending       = 0;
    int midi_irq_pending  = 0;
    int intr_pending      = 0;
    int midi_intr_pending = 0;

    gus->irqstatus &= ~0x60;
    gus->irqstatus2 = 0xE0;
    for (uint8_t c = 0; c < 32; c++) {
        if (gus->waveirqs[c]) {
            gus->irqstatus2 = 0x60 | c;
            if (gus->rampirqs[c])
                gus->irqstatus2 |= 0x80;
            gus->irqstatus |= 0x20;
            irq_pending = 1;
            break;
        }
        if (gus->rampirqs[c]) {
            gus->irqstatus2 = 0xA0 | c;
            gus->irqstatus |= 0x40;
            irq_pending = 1;
            break;
        }
    }
    if ((gus->tctrl & 4) && (gus->irqstatus & 0x04))
        irq_pending = 1; /*Timer 1 interrupt pending*/
    if ((gus->tctrl & 8) && (gus->irqstatus & 0x08))
        irq_pending = 1; /*Timer 2 interrupt pending*/
    if ((gus->irqstatus & 0x80) && (gus->dmactrl & 0x20))
        irq_pending = 1; /*DMA TC interrupt pending*/

    midi_irq_pending = gus->midi_status & MIDI_INT_MASTER;

    if (gus->irq == gus->irq_midi) {
        if (irq_pending || midi_irq_pending)
            intr_pending = 1;
        else
            intr_pending = 0;
    } else {
        if (irq_pending)
            intr_pending = 1;
        else
            intr_pending = 0;

        if (midi_irq_pending)
            midi_intr_pending = 1;
        else
            midi_intr_pending = 0;
    }

    if (gus->irq != -1) {
        if (intr_pending)
            picint(1 << gus->irq);
        else
            picintc(1 << gus->irq);
    }

    if ((gus->irq_midi != -1) && (gus->irq_midi != gus->irq)) {
        if (midi_intr_pending)
            picint(1 << gus->irq_midi);
        else
            picintc(1 << gus->irq_midi);
    }
}

void
gus_midi_update_int_status(gus_t *gus)
{
    gus->midi_status &= ~MIDI_INT_MASTER;
    if ((gus->midi_ctrl & MIDI_CTRL_TRANSMIT_MASK) == MIDI_CTRL_TRANSMIT && (gus->midi_status & MIDI_INT_TRANSMIT)) {
        gus->midi_status |= MIDI_INT_MASTER;
        gus->irqstatus |= GUS_INT_MIDI_TRANSMIT;
    } else
        gus->irqstatus &= ~GUS_INT_MIDI_TRANSMIT;

    if ((gus->midi_ctrl & MIDI_CTRL_RECEIVE) && (gus->midi_status & MIDI_INT_RECEIVE)) {
        gus->midi_status |= MIDI_INT_MASTER;
        gus->irqstatus |= GUS_INT_MIDI_RECEIVE;
    } else
        gus->irqstatus &= ~GUS_INT_MIDI_RECEIVE;

    gus_update_int_status(gus);
}

void
writegus(uint16_t addr, uint8_t val, void *priv)
{
    gus_t   *gus = (gus_t *) priv;
    int      c;
    int      d;
    int      old;
    uint16_t port;
#ifdef USE_GUSMAX
    uint16_t csioport;
#endif /*USE_GUSMAX */

    if ((addr == 0x388) || (addr == 0x389))
        port = addr;
    else
        port = addr & 0xf0f;

    switch (port) {
        case 0x300: /*MIDI control*/
            old            = gus->midi_ctrl;
            gus->midi_ctrl = val;
            gus->uart_out  = 1;

            if ((val & 3) == 3) { /*Master reset*/
                gus->uart_in     = 0;
                gus->midi_status = 0;
                gus->midi_r      = 0;
                gus->midi_w      = 0;
            } else if ((old & 3) == 3) {
                gus->midi_status |= MIDI_INT_TRANSMIT;
            } else if (gus->midi_ctrl & MIDI_CTRL_RECEIVE) {
                gus->uart_in = 1;
            }
            gus_midi_update_int_status(gus);
            break;
        case 0x301: /*MIDI data*/
            gus->midi_data = val;
            if (gus->uart_out) {
                midi_raw_out_byte(val);
            }
            if (gus->latch_enable & 0x20) {
                gus->midi_status |= MIDI_INT_RECEIVE;
            } else
                gus->midi_status |= MIDI_INT_TRANSMIT;
            break;
        case 0x302: /*Voice select*/
            gus->voice = val & 31;
            break;
        case 0x303: /*Global select*/
            gus->global = val;
            break;
        case 0x304: /*Global low*/
            switch (gus->global) {
                case 0: /*Voice control*/
                    gus->ctrl[gus->voice] = val;
                    break;
                case 1: /*Frequency control*/
                    gus->freq[gus->voice] = (gus->freq[gus->voice] & 0xFF00) | val;
                    break;
                case 2: /*Start addr high*/
                    gus->startx[gus->voice] = (gus->startx[gus->voice] & 0xF807F) | (val << 7);
                    gus->start[gus->voice]  = (gus->start[gus->voice] & 0x1F00FFFF) | (val << 16);
                    break;
                case 3: /*Start addr low*/
                    gus->start[gus->voice] = (gus->start[gus->voice] & 0x1FFFFF00) | val;
                    break;
                case 4: /*End addr high*/
                    gus->endx[gus->voice] = (gus->endx[gus->voice] & 0xF807F) | (val << 7);
                    gus->end[gus->voice]  = (gus->end[gus->voice] & 0x1F00FFFF) | (val << 16);
                    break;
                case 5: /*End addr low*/
                    gus->end[gus->voice] = (gus->end[gus->voice] & 0x1FFFFF00) | val;
                    break;

                case 6: /*Ramp frequency*/
                    gus->rfreq[gus->voice] = (int) ((double) ((val & 63) * 512) / (double) (1 << (3 * (val >> 6))));
                    break;

                case 9: /*Current volume*/
                    gus->curvol[gus->voice] = gus->rcur[gus->voice] = (gus->rcur[gus->voice] & ~(0xff << 6)) | (val << 6);
                    break;

                case 0xA: /*Current addr high*/
                    gus->cur[gus->voice]  = (gus->cur[gus->voice] & 0x1F00FFFF) | (val << 16);
                    gus->curx[gus->voice] = (gus->curx[gus->voice] & 0xF807F00) | ((val << 7) << 8);
                    break;
                case 0xB: /*Current addr low*/
                    gus->cur[gus->voice] = (gus->cur[gus->voice] & 0x1FFFFF00) | val;
                    break;

                case 0x42: /*DMA address low*/
                    gus->dmaaddr = (gus->dmaaddr & 0xFF000) | (val << 4);
                    break;

                case 0x43: /*Address low*/
                    gus->addr = (gus->addr & 0xFFF00) | val;
                    break;
                case 0x45: /*Timer control*/
                    gus->tctrl = val;
                    gus_update_int_status(gus);
                    break;

                default:
                    break;
            }
            break;
        case 0x305: /*Global high*/
            switch (gus->global) {
                case 0: /*Voice control*/
                    gus->ctrl[gus->voice] = val & 0x7f;

                    old                       = gus->waveirqs[gus->voice];
                    gus->waveirqs[gus->voice] = ((val & 0xa0) == 0xa0) ? 1 : 0;
                    if (gus->waveirqs[gus->voice] != old)
                        gus_update_int_status(gus);
                    break;
                case 1: /*Frequency control*/
                    gus->freq[gus->voice] = (gus->freq[gus->voice] & 0xFF) | (val << 8);
                    break;
                case 2: /*Start addr high*/
                    gus->startx[gus->voice] = (gus->startx[gus->voice] & 0x07FFF) | (val << 15);
                    gus->start[gus->voice]  = (gus->start[gus->voice] & 0x00FFFFFF) | ((val & 0x1F) << 24);
                    break;
                case 3: /*Start addr low*/
                    gus->startx[gus->voice] = (gus->startx[gus->voice] & 0xFFF80) | (val & 0x7F);
                    gus->start[gus->voice]  = (gus->start[gus->voice] & 0x1FFF00FF) | (val << 8);
                    break;
                case 4: /*End addr high*/
                    gus->endx[gus->voice] = (gus->endx[gus->voice] & 0x07FFF) | (val << 15);
                    gus->end[gus->voice]  = (gus->end[gus->voice] & 0x00FFFFFF) | ((val & 0x1F) << 24);
                    break;
                case 5: /*End addr low*/
                    gus->endx[gus->voice] = (gus->endx[gus->voice] & 0xFFF80) | (val & 0x7F);
                    gus->end[gus->voice]  = (gus->end[gus->voice] & 0x1FFF00FF) | (val << 8);
                    break;

                case 6: /*Ramp frequency*/
                    gus->rfreq[gus->voice] = (int) ((double) ((val & 63) * (1 << 10)) / (double) (1 << (3 * (val >> 6))));
                    break;
                case 7: /*Ramp start*/
                    gus->rstart[gus->voice] = val << 14;
                    break;
                case 8: /*Ramp end*/
                    gus->rend[gus->voice] = val << 14;
                    break;
                case 9: /*Current volume*/
                    gus->curvol[gus->voice] = gus->rcur[gus->voice] = (gus->rcur[gus->voice] & ~(0xff << 14)) | (val << 14);
                    break;

                case 0xA: /*Current addr high*/
                    gus->cur[gus->voice]  = (gus->cur[gus->voice] & 0x00FFFFFF) | ((val & 0x1F) << 24);
                    gus->curx[gus->voice] = (gus->curx[gus->voice] & 0x07FFF00) | ((val << 15) << 8);
                    break;
                case 0xB: /*Current addr low*/
                    gus->cur[gus->voice]  = (gus->cur[gus->voice] & 0x1FFF00FF) | (val << 8);
                    gus->curx[gus->voice] = (gus->curx[gus->voice] & 0xFFF8000) | ((val & 0x7F) << 8);
                    break;
                case 0xC: /*Pan*/
                    gus->pan_l[gus->voice] = 15 - (val & 0xf);
                    gus->pan_r[gus->voice] = (val & 0xf);
                    break;
                case 0xD: /*Ramp control*/
                    old                       = gus->rampirqs[gus->voice];
                    gus->rctrl[gus->voice]    = val & 0x7F;
                    gus->rampirqs[gus->voice] = ((val & 0xa0) == 0xa0) ? 1 : 0;
                    if (gus->rampirqs[gus->voice] != old)
                        gus_update_int_status(gus);
                    break;

                case 0xE:
                    gus->voices = (val & 63) + 1;
                    if (gus->voices > 32)
                        gus->voices = 32;
                    if (gus->voices < 14)
                        gus->voices = 14;
                    gus->global = val;
                    if (gus->voices < 14)
                        gus->samp_latch = (uint64_t) (TIMER_USEC * (1000000.0 / 44100.0));
                    else
                        gus->samp_latch = (uint64_t) (TIMER_USEC *
                                                      (1000000.0 / gusfreqs[gus->voices - 14]));
                    break;

                case 0x41: /*DMA*/
                    if (val & 1 && gus->dma != -1) {
                        if (val & 2) {
                            c = 0;
                            while (c < 65536) {
                                int dma_result;
                                if (val & 0x04) {
                                    uint32_t gus_addr = (gus->dmaaddr & 0xc0000) |
                                                        ((gus->dmaaddr & 0x1ffff) << 1);

                                    if (gus_addr < gus->gus_end_ram)
                                        d                 = gus->ram[gus_addr];
                                    else
                                        d                 = 0x00;

                                    if ((gus_addr + 1) < gus->gus_end_ram)
                                        d                 |= (gus->ram[gus_addr + 1] << 8);

                                    if (val & 0x80)
                                        d ^= 0x8080;
                                    dma_result = dma_channel_write(gus->dma, d);
                                    if (dma_result == DMA_NODATA)
                                        break;
                                } else {
                                    if (gus->dmaaddr < gus->gus_end_ram)
                                        d = gus->ram[gus->dmaaddr];
                                    else
                                        d = 0x00;

                                    if (val & 0x80)
                                        d ^= 0x80;
                                    dma_result = dma_channel_write(gus->dma, d);
                                    if (dma_result == DMA_NODATA)
                                        break;
                                }
                                gus->dmaaddr++;
                                gus->dmaaddr &= 0xfffff;
                                c++;
                                if (dma_result & DMA_OVER)
                                    break;
                            }
                            gus->dmactrl = val & ~0x40;
                            gus->irqnext = 1;
                        } else {
                            c = 0;
                            while (c < 65536) {
                                d = dma_channel_read(gus->dma);
                                if (d == DMA_NODATA)
                                    break;
                                if (val & 0x04) {
                                    uint32_t gus_addr = (gus->dmaaddr & 0xc0000) |
                                                        ((gus->dmaaddr & 0x1ffff) << 1);
                                    if (val & 0x80)
                                        d ^= 0x8080;

                                    if (gus_addr < gus->gus_end_ram)
                                        gus->ram[gus_addr]     = d & 0xff;

                                    if ((gus_addr + 1) < gus->gus_end_ram)
                                        gus->ram[gus_addr + 1] = (d >> 8) & 0xff;
                                } else {
                                    if (val & 0x80)
                                        d ^= 0x80;

                                    if (gus->dmaaddr < gus->gus_end_ram)
                                        gus->ram[gus->dmaaddr] = d;
                                }
                                gus->dmaaddr++;
                                gus->dmaaddr &= 0xfffff;
                                c++;
                                if (d & DMA_OVER)
                                    break;
                            }
                            gus->dmactrl = val & ~0x40;
                            gus->irqnext = 1;
                        }
                    }
                    break;

                case 0x42: /*DMA address low*/
                    gus->dmaaddr = (gus->dmaaddr & 0xFF0) | (val << 12);
                    break;

                case 0x43: /*Address low*/
                    gus->addr = (gus->addr & 0xf00ff) | (val << 8);
                    break;
                case 0x44: /*Address high*/
                    gus->addr = (gus->addr & 0x0ffff) | ((val << 16) & 0xf0000);
                    break;
                case 0x45: /*Timer control*/
                    if (!(val & 4))
                        gus->irqstatus &= ~4;
                    if (!(val & 8))
                        gus->irqstatus &= ~8;
                    if (!(val & 0x20))
                        gus->ad_status &= ~0x18;
                    if (!(val & 0x02))
                        gus->ad_status &= ~0x01;
                    gus->tctrl   = val;
                    gus->sb_ctrl = val;
                    gus_update_int_status(gus);
                    break;
                case 0x46: /*Timer 1*/
                    gus->t1 = gus->t1l = val;
                    gus->t1on          = 1;
                    break;
                case 0x47: /*Timer 2*/
                    gus->t2 = gus->t2l = val;
                    gus->t2on          = 1;
                    break;

                case 0x4c: /*Reset*/
                    gus->reset = val;
                    break;

                default:
                    break;
            }
            break;
        case 0x307: /*DRAM access*/
            if (gus->addr < gus->gus_end_ram)
                gus->ram[gus->addr] = val;
            gus->addr &= 0xfffff;
            break;
        case 0x208:
        case 0x388:
            gus->adcommand = val;
            break;

        case 0x389:
            if ((gus->tctrl & GUS_TIMER_CTRL_AUTO) || gus->adcommand != 4) {
                gus->ad_data = val;
                gus->ad_status |= 0x01;
                if (gus->sb_ctrl & 0x02) {
                    if (gus->sb_nmi)
                        nmi_raise();
                    else if (gus->irq != -1)
                        picint(1 << gus->irq);
                }
            } else if (!(gus->tctrl & GUS_TIMER_CTRL_AUTO) && gus->adcommand == 4) {
                if (val & 0x80) {
                    gus->ad_status &= ~0x60;
                } else {
                    gus->ad_timer_ctrl = val;

                    if (val & 0x01)
                        gus->t1on = 1;
                    else
                        gus->t1 = gus->t1l;

                    if (val & 0x02)
                        gus->t2on = 1;
                    else
                        gus->t2 = gus->t2l;
                }
            }
            break;

        case 0x200:
            gus->latch_enable = val;
            break;

        case 0x20b:
            switch (gus->reg_ctrl & 0x07) {
                case 0:
                    if (gus->latch_enable & 0x40) {
                        gus->irq = gus_gf1_irqs[val & 7];

                        if (val & 0x40) {
                            if (gus->irq == -1)
                                gus->irq = gus->irq_midi = gus_gf1_irqs[(val >> 3) & 7];
                            else
                                gus->irq_midi = gus->irq;
                        } else
                            gus->irq_midi = gus_midi_irqs[(val >> 3) & 7];
#ifdef USE_GUSMAX
                        if (gus->type == GUS_MAX)
                            ad1848_setirq(&gus->ad1848, gus->irq);
#endif /*USE_GUSMAX */

                        gus->sb_nmi = val & 0x80;
                    } else {
                        gus->dma = gus_dmas[val & 7];

                        if (val & 0x40) {
                            if (gus->dma == -1)
                                gus->dma = gus->dma2 = gus_dmas[(val >> 3) & 7];
                            else
                                gus->dma2 = gus->dma;
                        } else
                            gus->dma2 = gus_dmas[(val >> 3) & 7];
#ifdef USE_GUSMAX
                        if (gus->type == GUS_MAX)
                            ad1848_setdma(&gus->ad1848, gus->dma2);
#endif /*USE_GUSMAX */
                    }
                    break;
                case 1:
                    gus->gp1 = val;
                    break;
                case 2:
                    gus->gp2 = val;
                    break;
                case 3:
                    gus->gp1_addr = val;
                    break;
                case 4:
                    gus->gp2_addr = val;
                    break;
                case 5:
                    gus->usrr = 0;
                    break;
                case 6:
                    break;

                default:
                    break;
            }
            break;

        case 0x206:
            gus->ad_status |= 0x08;
            if (gus->sb_ctrl & 0x20) {
                if (gus->sb_nmi)
                    nmi_raise();
                else if (gus->irq != -1)
                    picint(1 << gus->irq);
            }
            break;
        case 0x20a:
            gus->sb_2xa = val;
            break;
        case 0x20c:
            gus->ad_status |= 0x10;
            if (gus->sb_ctrl & 0x20) {
                if (gus->sb_nmi)
                    nmi_raise();
                else if (gus->irq != -1)
                    picint(1 << gus->irq);
            }
            fallthrough;
        case 0x20d:
            gus->sb_2xc = val;
            break;
        case 0x20e:
            gus->sb_2xe = val;
            break;
        case 0x20f:
            gus->reg_ctrl = val;
            break;
        case 0x306:
        case 0x706:
#ifdef USE_GUSMAX
            if (gus->type == GUS_MAX) {
                if (gus->dma >= 4)
                    val |= 0x10;
                if (gus->dma2 >= 4)
                    val |= 0x20;
                gus->max_ctrl = (val >> 6) & 1;
                if (val & 0x40) {
                    if ((val & 0xF) != ((addr >> 4) & 0xF)) {
                        csioport = 0x30c | ((addr >> 4) & 0xf);
                        io_removehandler(csioport, 4,
                                         ad1848_read, NULL, NULL,
                                         ad1848_write, NULL, NULL, &gus->ad1848);
                        csioport = 0x30c | ((val & 0xf) << 4);
                        io_sethandler(csioport, 4,
                                      ad1848_read, NULL, NULL,
                                      ad1848_write, NULL, NULL, &gus->ad1848);
                    }
                }
            }
#endif /*USE_GUSMAX */
            break;

        default:
            break;
    }
}

uint8_t
readgus(uint16_t addr, void *priv)
{
    gus_t   *gus = (gus_t *) priv;
    uint8_t  val = 0xff;
    uint16_t port;

    if ((addr == 0x388) || (addr == 0x389))
        port = addr;
    else
        port = addr & 0xf0f;

    switch (port) {
        case 0x300: /*MIDI status*/
            val = gus->midi_status;
            break;

        case 0x301: /*MIDI data*/
            val = 0;
            if (gus->uart_in) {
                if ((gus->midi_data == 0xaa) && (gus->midi_ctrl & MIDI_CTRL_RECEIVE)) /*Handle master reset*/
                    val = gus->midi_data;
                else {
                    val = gus->midi_queue[gus->midi_r];
                    if (gus->midi_r != gus->midi_w) {
                        gus->midi_r++;
                        gus->midi_r &= 63;
                    }
                }
                gus->midi_status &= ~MIDI_INT_RECEIVE;
                gus_midi_update_int_status(gus);
            }
            break;

        case 0x200:
            return 0;

        case 0x206: /*IRQ status*/
            val = gus->irqstatus & ~0x10;
            if (gus->ad_status & 0x19)
                val |= 0x10;
            return val;

        case 0x20F:
#ifdef USE_GUSMAX
            if (gus->type == GUS_MAX)
                val = 0x02;
            else
#endif /*USE_GUSMAX */
                val = 0x00;
            break;

        case 0x302:
            return gus->voice;

        case 0x303:
            return gus->global;

        case 0x304: /*Global low*/
            switch (gus->global) {
                case 0x82: /*Start addr high*/
                    return gus->start[gus->voice] >> 16;
                case 0x83: /*Start addr low*/
                    return gus->start[gus->voice] & 0xFF;

                case 0x89: /*Current volume*/
                    return gus->rcur[gus->voice] >> 6;
                case 0x8A: /*Current addr high*/
                    return gus->cur[gus->voice] >> 16;
                case 0x8B: /*Current addr low*/
                    return gus->cur[gus->voice] & 0xFF;

                case 0x8F: /*IRQ status*/
                    val                                   = gus->irqstatus2;
                    gus->rampirqs[gus->irqstatus2 & 0x1F] = 0;
                    gus->waveirqs[gus->irqstatus2 & 0x1F] = 0;
                    gus_update_int_status(gus);
                    return val;

                case 0x00:
                case 0x01:
                case 0x02:
                case 0x03:
                case 0x04:
                case 0x05:
                case 0x06:
                case 0x07:
                case 0x08:
                case 0x09:
                case 0x0a:
                case 0x0b:
                case 0x0c:
                case 0x0d:
                case 0x0e:
                case 0x0f:
                    val = 0xff;
                    break;

                default:
                    break;
            }
            break;
        case 0x305: /*Global high*/
            switch (gus->global) {
                case 0x80: /*Voice control*/
                    return gus->ctrl[gus->voice] | (gus->waveirqs[gus->voice] ? 0x80 : 0);

                case 0x82: /*Start addr high*/
                    return gus->start[gus->voice] >> 24;
                case 0x83: /*Start addr low*/
                    return gus->start[gus->voice] >> 8;

                case 0x89: /*Current volume*/
                    return gus->rcur[gus->voice] >> 14;

                case 0x8A: /*Current addr high*/
                    return gus->cur[gus->voice] >> 24;
                case 0x8B: /*Current addr low*/
                    return gus->cur[gus->voice] >> 8;

                case 0x8C: /*Pan*/
                    return gus->pan_r[gus->voice];

                case 0x8D:
                    return gus->rctrl[gus->voice] | (gus->rampirqs[gus->voice] ? 0x80 : 0);

                case 0x8F: /*IRQ status*/
                    val                                   = gus->irqstatus2;
                    gus->rampirqs[gus->irqstatus2 & 0x1F] = 0;
                    gus->waveirqs[gus->irqstatus2 & 0x1F] = 0;
                    gus_update_int_status(gus);
                    return val;

                case 0x41: /*DMA control*/
                    val = gus->dmactrl | ((gus->irqstatus & 0x80) ? 0x40 : 0);
                    gus->irqstatus &= ~0x80;
                    return val;
                case 0x45: /*Timer control*/
                    return gus->tctrl;
                case 0x49: /*Sampling control*/
                    return 0;

                case 0x00:
                case 0x01:
                case 0x02:
                case 0x03:
                case 0x04:
                case 0x05:
                case 0x06:
                case 0x07:
                case 0x08:
                case 0x09:
                case 0x0a:
                case 0x0b:
                case 0x0c:
                case 0x0d:
                case 0x0e:
                case 0x0f:
                    val = 0xff;
                    break;

                default:
                    break;
            }
            break;
        case 0x306:
        case 0x706:
#ifdef USE_GUSMAX
            if (gus->type == GUS_MAX)
                val = 0x0a; /* GUS MAX */
            else
#endif /*USE_GUSMAX */
                val = 0xff; /*Pre 3.7 - no mixer*/
            break;

        case 0x307: /*DRAM access*/
            gus->addr &= 0xfffff;
            if (gus->addr < gus->gus_end_ram)
                val = gus->ram[gus->addr];
            else
                val = 0;
            return val;
        case 0x309:
            return 0;

        case 0x20b:
            switch (gus->reg_ctrl & 0x07) {
                case 1:
                    val = gus->gp1;
                    break;
                case 2:
                    val = gus->gp2;
                    break;
                case 3:
                    val = gus->gp1_addr;
                    break;
                case 4:
                    val = gus->gp2_addr;
                    break;

                default:
                    break;
            }
            break;

        case 0x20c:
            val = gus->sb_2xc;
            if (gus->reg_ctrl & 0x20)
                gus->sb_2xc &= 0x80;
            break;
        case 0x20e:
            return gus->sb_2xe;

        case 0x208:
        case 0x388:
            if (gus->tctrl & GUS_TIMER_CTRL_AUTO)
                val = gus->sb_2xa;
            else {
                val = gus->ad_status & ~(gus->ad_timer_ctrl & 0x60);
                if (val & 0x60)
                    val |= 0x80;
            }
            break;

        case 0x209:
            gus->ad_status &= ~0x01;
#ifdef OLD_NMI_BEHAVIOR
            nmi = 0;
#endif /* OLD_NMI_BEHAVIOR */
            fallthrough;
        case 0x389:
            val = gus->ad_data;
            break;

        case 0x20A:
            val = gus->adcommand;
            break;

        default:
            break;
    }
    return val;
}

void
gus_poll_timer_1(void *priv)
{
    gus_t *gus = (gus_t *) priv;

    timer_advance_u64(&gus->timer_1, (uint64_t) (TIMER_USEC * 80));
    if (gus->t1on) {
        gus->t1++;
        if (gus->t1 > 0xFF) {
            gus->t1 = gus->t1l;
            gus->ad_status |= 0x40;
            if (gus->tctrl & 4) {
                gus->ad_status |= 0x04;
                gus->irqstatus |= 0x04;
            }
        }
    }
    if (gus->irqnext) {
        gus->irqnext = 0;
        gus->irqstatus |= 0x80;
    }

    gus_midi_update_int_status(gus);
    gus_update_int_status(gus);
}

void
gus_poll_timer_2(void *priv)
{
    gus_t *gus = (gus_t *) priv;

    timer_advance_u64(&gus->timer_2, (uint64_t) (TIMER_USEC * 320));
    if (gus->t2on) {
        gus->t2++;
        if (gus->t2 > 0xFF) {
            gus->t2 = gus->t2l;
            gus->ad_status |= 0x20;
            if (gus->tctrl & 8) {
                gus->ad_status |= 0x02;
                gus->irqstatus |= 0x08;
            }
        }
    }
    if (gus->irqnext) {
        gus->irqnext = 0;
        gus->irqstatus |= 0x80;
    }
    gus_update_int_status(gus);
}

static void
gus_update(gus_t *gus)
{
    for (; gus->pos < sound_pos_global; gus->pos++) {
        if (gus->out_l < -32768)
            gus->buffer[0][gus->pos] = -32768;
        else if (gus->out_l > 32767)
            gus->buffer[0][gus->pos] = 32767;
        else
            gus->buffer[0][gus->pos] = gus->out_l;
        if (gus->out_r < -32768)
            gus->buffer[1][gus->pos] = -32768;
        else if (gus->out_r > 32767)
            gus->buffer[1][gus->pos] = 32767;
        else
            gus->buffer[1][gus->pos] = gus->out_r;
    }
}

void
gus_poll_wave(void *priv)
{
    gus_t   *gus = (gus_t *) priv;
    uint32_t addr;
    int16_t  v;
    int32_t  vl;
    int      update_irqs = 0;

    gus_update(gus);

    timer_advance_u64(&gus->samp_timer, gus->samp_latch);

    gus->out_l = gus->out_r = 0;

    if ((gus->reset & 3) != 3)
        return;
    for (uint8_t d = 0; d < 32; d++) {
        if (!(gus->ctrl[d] & 3)) {
            if (gus->ctrl[d] & 4) {
                addr = gus->cur[d] >> 9;
                addr = (addr & 0xC0000) | ((addr << 1) & 0x3FFFE);
                if (!(gus->freq[d] >> 10)) {
                    /* Interpolate */
                    if (((addr + 1) & 0xfffff) < gus->gus_end_ram)
                        vl = (int16_t) (int8_t) ((gus->ram[(addr + 1) & 0xfffff] ^ 0x80) - 0x80) *
                             (511 - (gus->cur[d] & 511));
                    else
                        vl = 0;

                    if (((addr + 3) & 0xfffff) < gus->gus_end_ram)
                        vl += (int16_t) (int8_t) ((gus->ram[(addr + 3) & 0xfffff] ^ 0x80) - 0x80) *
                              (gus->cur[d] & 511);

                    v = vl >> 9;
                } else if (((addr + 1) & 0xfffff) < gus->gus_end_ram)
                    v = (int16_t) (int8_t) ((gus->ram[(addr + 1) & 0xfffff] ^ 0x80) - 0x80);
                else
                    v = 0x0000;
            } else {
                if (!(gus->freq[d] >> 10)) {
                    /* Interpolate */
                    if (((gus->cur[d] >> 9) & 0xfffff) < gus->gus_end_ram)
                        vl = ((int8_t) ((gus->ram[(gus->cur[d] >> 9) & 0xfffff] ^ 0x80) - 0x80)) *
                                       (511 - (gus->cur[d] & 511));
                    else
                        vl = 0;

                    if ((((gus->cur[d] >> 9) + 1) & 0xfffff) < gus->gus_end_ram)
                        vl += ((int8_t) ((gus->ram[((gus->cur[d] >> 9) + 1) & 0xfffff] ^ 0x80) - 0x80)) *
                              (gus->cur[d] & 511);

                    v = vl >> 9;
                } else if (((gus->cur[d] >> 9) & 0xfffff) < gus->gus_end_ram)
                    v = (int16_t) (int8_t) ((gus->ram[(gus->cur[d] >> 9) & 0xfffff] ^ 0x80) - 0x80);
                else
                    v = 0x0000;
            }

            if ((gus->rcur[d] >> 14) > 4095)
                v = (int16_t) (float) (v) *24.0 * vol16bit[4095];
            else
                v = (int16_t) (float) (v) *24.0 * vol16bit[(gus->rcur[d] >> 10) & 4095];

            gus->out_l += (v * gus->pan_l[d]) / 7;
            gus->out_r += (v * gus->pan_r[d]) / 7;

            if (gus->ctrl[d] & 0x40) {
                gus->cur[d] -= (gus->freq[d] >> 1);
                if (gus->cur[d] <= gus->start[d]) {
                    int diff = gus->start[d] - gus->cur[d];

                    if (gus->ctrl[d] & 8) {
                        if (gus->ctrl[d] & 0x10)
                            gus->ctrl[d] ^= 0x40;
                        gus->cur[d] = (gus->ctrl[d] & 0x40) ? (gus->end[d] - diff) : (gus->start[d] + diff);
                    } else if (!(gus->rctrl[d] & 4)) {
                        gus->ctrl[d] |= 1;
                        gus->cur[d] = (gus->ctrl[d] & 0x40) ? gus->end[d] : gus->start[d];
                    }

                    if ((gus->ctrl[d] & 0x20) && !gus->waveirqs[d]) {
                        gus->waveirqs[d] = 1;
                        update_irqs      = 1;
                    }
                }
            } else {
                gus->cur[d] += (gus->freq[d] >> 1);

                if (gus->cur[d] >= gus->end[d]) {
                    int diff = gus->cur[d] - gus->end[d];

                    if (gus->ctrl[d] & 8) {
                        if (gus->ctrl[d] & 0x10)
                            gus->ctrl[d] ^= 0x40;
                        gus->cur[d] = (gus->ctrl[d] & 0x40) ? (gus->end[d] - diff) : (gus->start[d] + diff);
                    } else if (!(gus->rctrl[d] & 4)) {
                        gus->ctrl[d] |= 1;
                        gus->cur[d] = (gus->ctrl[d] & 0x40) ? gus->end[d] : gus->start[d];
                    }

                    if ((gus->ctrl[d] & 0x20) && !gus->waveirqs[d]) {
                        gus->waveirqs[d] = 1;
                        update_irqs      = 1;
                    }
                }
            }
        }
        if (!(gus->rctrl[d] & 3)) {
            if (gus->rctrl[d] & 0x40) {
                gus->rcur[d] -= gus->rfreq[d];
                if (gus->rcur[d] <= gus->rstart[d]) {
                    int diff = gus->rstart[d] - gus->rcur[d];
                    if (!(gus->rctrl[d] & 8)) {
                        gus->rctrl[d] |= 1;
                        gus->rcur[d] = (gus->rctrl[d] & 0x40) ? gus->rstart[d] : gus->rend[d];
                    } else {
                        if (gus->rctrl[d] & 0x10)
                            gus->rctrl[d] ^= 0x40;
                        gus->rcur[d] = (gus->rctrl[d] & 0x40) ? (gus->rend[d] - diff) : (gus->rstart[d] + diff);
                    }

                    if ((gus->rctrl[d] & 0x20) && !gus->rampirqs[d]) {
                        gus->rampirqs[d] = 1;
                        update_irqs      = 1;
                    }
                }
            } else {
                gus->rcur[d] += gus->rfreq[d];
                if (gus->rcur[d] >= gus->rend[d]) {
                    int diff = gus->rcur[d] - gus->rend[d];
                    if (!(gus->rctrl[d] & 8)) {
                        gus->rctrl[d] |= 1;
                        gus->rcur[d] = (gus->rctrl[d] & 0x40) ? gus->rstart[d] : gus->rend[d];
                    } else {
                        if (gus->rctrl[d] & 0x10)
                            gus->rctrl[d] ^= 0x40;
                        gus->rcur[d] = (gus->rctrl[d] & 0x40) ? (gus->rend[d] - diff) : (gus->rstart[d] + diff);
                    }

                    if ((gus->rctrl[d] & 0x20) && !gus->rampirqs[d]) {
                        gus->rampirqs[d] = 1;
                        update_irqs      = 1;
                    }
                }
            }
        }
    }

    if (update_irqs)
        gus_update_int_status(gus);
}

static void
gus_get_buffer(int32_t *buffer, int len, void *priv)
{
    gus_t *gus = (gus_t *) priv;

#ifdef USE_GUSMAX
    if ((gus->type == GUS_MAX) && (gus->max_ctrl))
        ad1848_update(&gus->ad1848);
#endif /*USE_GUSMAX */
    gus_update(gus);

    for (int c = 0; c < len * 2; c++) {
#ifdef USE_GUSMAX
        if ((gus->type == GUS_MAX) && (gus->max_ctrl))
            buffer[c] += (int32_t) (gus->ad1848.buffer[c] / 2);
#endif /*USE_GUSMAX */
        buffer[c] += (int32_t) gus->buffer[c & 1][c >> 1];
    }

#ifdef USE_GUSMAX
    if ((gus->type == GUS_MAX) && (gus->max_ctrl))
        gus->ad1848.pos = 0;
#endif /*USE_GUSMAX */
    gus->pos = 0;
}

static void
gus_input_msg(void *priv, uint8_t *msg, uint32_t len)
{
    gus_t  *gus = (gus_t *) priv;

    if (gus->sysex)
        return;

    if (gus->uart_in) {
        gus->midi_status |= MIDI_INT_RECEIVE;

        for (uint32_t i = 0; i < len; i++) {
            gus->midi_queue[gus->midi_w++] = msg[i];
            gus->midi_w &= 63;
        }

        gus_midi_update_int_status(gus);
    }
}

static int
gus_input_sysex(void *priv, uint8_t *buffer, uint32_t len, int abort)
{
    gus_t   *gus = (gus_t *) priv;

    if (abort) {
        gus->sysex = 0;
        return 0;
    }
    gus->sysex = 1;
    for (uint32_t i = 0; i < len; i++) {
        if (gus->midi_r == gus->midi_w)
            return (len - i);
        gus->midi_queue[gus->midi_w++] = buffer[i];
        gus->midi_w &= 63;
    }
    gus->sysex = 0;
    return 0;
}

static void
gus_reset(void *priv)
{
    gus_t   *gus = (gus_t *) priv;
    int      c;
    double   out     = 1.0;

    if (gus == NULL)
        return;

    memset(gus->ram, 0x00, (gus->gus_end_ram));

    for (c = 0; c < 32; c++) {
        gus->ctrl[c]  = 1;
        gus->rctrl[c] = 1;
        gus->rfreq[c] = 63 * 512;
    }

    for (c = 4095; c >= 0; c--) {
        vol16bit[c] = out;
        out /= 1.002709201; /* 0.0235 dB Steps */
    }

    gus->voices = 14;

    gus->samp_latch = (uint64_t) (TIMER_USEC * (1000000.0 / 44100.0));

    gus->t1l = gus->t2l = 0xff;

    gus->global = 0;
    gus->addr = 0;
    gus->dmaaddr = 0;
    gus->voice = 0;
    memset(gus->start, 0x00, 32 * sizeof(uint32_t));
    memset(gus->end, 0x00, 32 * sizeof(uint32_t));
    memset(gus->cur, 0x00, 32 * sizeof(uint32_t));
    memset(gus->startx, 0x00, 32 * sizeof(uint32_t));
    memset(gus->endx, 0x00, 32 * sizeof(uint32_t));
    memset(gus->curx, 0x00, 32 * sizeof(uint32_t));
    memset(gus->rstart, 0x00, 32 * sizeof(int));
    memset(gus->rend, 0x00, 32 * sizeof(int));
    memset(gus->rcur, 0x00, 32 * sizeof(int));
    memset(gus->freq, 0x00, 32 * sizeof(uint16_t));
    memset(gus->curvol, 0x00, 32 * sizeof(int));
    memset(gus->pan_l, 0x00, 32 * sizeof(int));
    memset(gus->pan_r, 0x00, 32 * sizeof(int));
    gus->t1on = 0;
    gus->t2on = 0;
    gus->tctrl = 0;
    gus->t1 = 0;
    gus->t2 = 0;
    gus->irqstatus = 0;
    gus->irqstatus2 = 0;
    gus->adcommand = 0;
    memset(gus->waveirqs, 0x00, 32 * sizeof(int));
    memset(gus->rampirqs, 0x00, 32 * sizeof(int));
    gus->dmactrl = 0;

    gus->uart_out = 1;

    gus->sb_2xa = 0;
    gus->sb_2xc = 0;
    gus->sb_2xe = 0;
    gus->sb_ctrl = 0;
    gus->sb_nmi = 0;

    gus->reg_ctrl = 0;

    gus->ad_status = 0;
    gus->ad_data = 0;
    gus->ad_timer_ctrl = 0;

    gus->midi_ctrl = 0;
    gus->midi_status = 0;
    memset(gus->midi_queue, 0x00, 64 * sizeof(uint8_t));
    gus->midi_data = 0;
    gus->midi_r = 0;
    gus->midi_w = 0;
    gus->uart_in = 0;
    gus->uart_out = 0;
    gus->sysex = 0;

    gus->gp1 = 0;
    gus->gp2 = 0;
    gus->gp1_addr = 0;
    gus->gp2_addr = 0;

    gus->usrr = 0;

#ifdef USE_GUSMAX
    gus->max_ctrl = 0;
#endif /*USE_GUSMAX */

    gus->irq_state = 0;
    gus->midi_irq_state = 0;

    gus_update_int_status(gus);
}

void *
gus_init(UNUSED(const device_t *info))
{
    int     c;
    double  out     = 1.0;
    uint8_t gus_ram = device_get_config_int("gus_ram");
    gus_t  *gus     = calloc(1, sizeof(gus_t));

    gus->gus_end_ram = 1 << (18 + gus_ram);
    gus->ram         = (uint8_t *) calloc(1, gus->gus_end_ram);

    for (c = 0; c < 32; c++) {
        gus->ctrl[c]  = 1;
        gus->rctrl[c] = 1;
        gus->rfreq[c] = 63 * 512;
    }

    for (c = 4095; c >= 0; c--) {
        vol16bit[c] = out;
        out /= 1.002709201; /* 0.0235 dB Steps */
    }

    gus->voices = 14;

    gus->samp_latch = (uint64_t) (TIMER_USEC * (1000000.0 / 44100.0));

    gus->t1l = gus->t2l = 0xff;

    gus->uart_out = 1;

    gus->type = device_get_config_int("type");

    gus->base = device_get_config_hex16("base");

    io_sethandler(gus->base, 0x0010, readgus, NULL, NULL, writegus, NULL, NULL, gus);
    io_sethandler(0x0100 + gus->base, 0x0010, readgus, NULL, NULL, writegus, NULL, NULL, gus);
    io_sethandler(0x0506 + gus->base, 0x0001, readgus, NULL, NULL, writegus, NULL, NULL, gus);
    io_sethandler(0x0388, 0x0002, readgus, NULL, NULL, writegus, NULL, NULL, gus);

#ifdef USE_GUSMAX
    if (gus->type == GUS_MAX) {
        ad1848_init(&gus->ad1848, AD1848_TYPE_CS4231);
        ad1848_setirq(&gus->ad1848, 5);
        ad1848_setdma(&gus->ad1848, 3);
        io_sethandler(0x10C + gus->base, 4,
                      ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &gus->ad1848);
    }
#endif /*USE_GUSMAX */

    timer_add(&gus->samp_timer, gus_poll_wave, gus, 1);
    timer_add(&gus->timer_1, gus_poll_timer_1, gus, 1);
    timer_add(&gus->timer_2, gus_poll_timer_2, gus, 1);

    sound_add_handler(gus_get_buffer, gus);

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, gus_input_msg, gus_input_sysex, gus);

    return gus;
}

void
gus_close(void *priv)
{
    gus_t *gus = (gus_t *) priv;

    free(gus->ram);
    free(gus);
}

void
gus_speed_changed(void *priv)
{
    gus_t *gus = (gus_t *) priv;

    if (gus->voices < 14)
        gus->samp_latch = (uint64_t) (TIMER_USEC * (1000000.0 / 44100.0));
    else
        gus->samp_latch = (uint64_t) (TIMER_USEC * (1000000.0 / gusfreqs[gus->voices - 14]));

#ifdef USE_GUSMAX
    if ((gus->type == GUS_MAX) && (gus->max_ctrl))
        ad1848_speed_changed(&gus->ad1848);
#endif /*USE_GUSMAX */
}

static const device_config_t gus_config[] = {
    // clang-format off
    {
        .name           = "type",
        .description    = "GUS type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Classic", .value = GUS_CLASSIC },
#ifdef USE_GUSMAX
            { .description = "MAX",     .value = GUS_MAX     },
#endif /*USE_GUSMAX */
            { NULL                                           }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x220,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "210H", .value = 0x210 },
            { .description = "220H", .value = 0x220 },
            { .description = "230H", .value = 0x230 },
            { .description = "240H", .value = 0x240 },
            { .description = "250H", .value = 0x250 },
            { .description = "260H", .value = 0x260 },
            { NULL                                  }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "gus_ram",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "256 KB", .value = 0 },
            { .description = "512 KB", .value = 1 },
            { .description = "1 MB",   .value = 2 },
            { NULL                                }
        },
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
// clang-format off
};

const device_t gus_device = {
    .name          = "Gravis UltraSound",
    .internal_name = "gus",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0,
    .init          = gus_init,
    .close         = gus_close,
    .reset         = gus_reset,
    .available     = NULL,
    .speed_changed = gus_speed_changed,
    .force_redraw  = NULL,
    .config        = gus_config
};
