/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ibm.h"

#include "device.h"
#include "dma.h"
#include "io.h"
#include "pic.h"
#include "sound.h"
#include "sound_gus.h"
#include "timer.h"

typedef struct gus_t
{
        int reset;
        
        int global;
        uint32_t addr,dmaaddr;
        int voice;
        uint32_t start[32],end[32],cur[32];
        uint32_t startx[32],endx[32],curx[32];
        int rstart[32],rend[32];
        int rcur[32];
        uint16_t freq[32];
        uint16_t rfreq[32];
        uint8_t ctrl[32];
        uint8_t rctrl[32];
        int curvol[32];
        int pan_l[32], pan_r[32];
        int t1on,t2on;
        uint8_t tctrl;
        uint16_t t1,t2,t1l,t2l;
        uint8_t irqstatus,irqstatus2;
        uint8_t adcommand;
        int waveirqs[32],rampirqs[32];
        int voices;
        uint8_t dmactrl;

        int32_t out_l, out_r;
        
        int16_t buffer[2][SOUNDBUFLEN];
        int pos;
        
        int64_t samp_timer, samp_latch;
        
        uint8_t *ram;
        
        int irqnext;
        
        int64_t timer_1, timer_2;
        
        int irq, dma, irq_midi;
        int latch_enable;
        
        uint8_t sb_2xa, sb_2xc, sb_2xe;
        uint8_t sb_ctrl;
        int sb_nmi;
        
        uint8_t reg_ctrl;
        
        uint8_t ad_status, ad_data;
        uint8_t ad_timer_ctrl;
        
        uint8_t midi_ctrl, midi_status;
        uint8_t midi_data;
        int midi_loopback;
        
        uint8_t gp1, gp2;
        uint16_t gp1_addr, gp2_addr;
        
        uint8_t usrr;
} gus_t;

static int gus_irqs[8] = {-1, 2, 5, 3, 7, 11, 12, 15};
static int gus_irqs_midi[8] = {-1, 2, 5, 3, 7, 11, 12, 15};
static int gus_dmas[8] = {-1, 1, 3, 5, 6, 7, -1, -1};

int gusfreqs[]=
{
        44100,41160,38587,36317,34300,32494,30870,29400,28063,26843,25725,24696,
        23746,22866,22050,21289,20580,19916,19293
};

double vol16bit[4096];

void pollgusirqs(gus_t *gus)
{
        int c;

        gus->irqstatus&=~0x60;
        for (c=0;c<32;c++)
        {
                if (gus->waveirqs[c])
                {
//                        gus->waveirqs[c]=0;
                        gus->irqstatus2=0x60|c;
                        if (gus->rampirqs[c]) gus->irqstatus2 |= 0x80;
                        gus->irqstatus|=0x20;
//                        printf("Voice IRQ %i %02X %i\n",c,gus->irqstatus2,ins);
                        if (gus->irq != -1)
                                picint(1 << gus->irq);
                        return;
                }
                if (gus->rampirqs[c])
                {
//                        gus->rampirqs[c]=0;
                        gus->irqstatus2=0xA0|c;
                        gus->irqstatus|=0x40;
//                        printf("Ramp IRQ %i %02X %i\n",c,gus->irqstatus2,ins);
                        if (gus->irq != -1)
                                picint(1 << gus->irq);
                        return;
                }
        }
        gus->irqstatus2=0xE0;
//        gus->irqstatus&=~0x20;
        if (!gus->irqstatus && gus->irq != -1) picintc(1 << gus->irq);
}

enum
{
        MIDI_INT_RECEIVE = 0x01,
        MIDI_INT_TRANSMIT = 0x02,
        MIDI_INT_MASTER = 0x80
};

enum
{
        MIDI_CTRL_TRANSMIT_MASK = 0x60,
        MIDI_CTRL_TRANSMIT = 0x20,
        MIDI_CTRL_RECEIVE = 0x80
};

enum
{
        GUS_INT_MIDI_TRANSMIT = 0x01,
        GUS_INT_MIDI_RECEIVE  = 0x02
};

enum
{
        GUS_TIMER_CTRL_AUTO = 0x01
};

void gus_midi_update_int_status(gus_t *gus)
{
        gus->midi_status &= ~MIDI_INT_MASTER;
        if ((gus->midi_ctrl & MIDI_CTRL_TRANSMIT_MASK) == MIDI_CTRL_TRANSMIT && (gus->midi_status & MIDI_INT_TRANSMIT))
        {
                gus->midi_status |= MIDI_INT_MASTER;
                gus->irqstatus |= GUS_INT_MIDI_TRANSMIT;
        }
        else
                gus->irqstatus &= ~GUS_INT_MIDI_TRANSMIT;
                
        if ((gus->midi_ctrl & MIDI_CTRL_RECEIVE) && (gus->midi_status & MIDI_INT_RECEIVE))
        {
                gus->midi_status |= MIDI_INT_MASTER;
                gus->irqstatus |= GUS_INT_MIDI_RECEIVE;
        }
        else
                gus->irqstatus &= ~GUS_INT_MIDI_RECEIVE;

        if ((gus->midi_status & MIDI_INT_MASTER) && (gus->irq_midi != -1))
        {
//                pclog("Take MIDI IRQ\n");
                picint(1 << gus->irq_midi);
        }
}
        
void writegus(uint16_t addr, uint8_t val, void *p)
{
        gus_t *gus = (gus_t *)p;
        int c, d;
        int old;
//        pclog("Write GUS %04X %02X %04X:%04X\n",addr,val,CS,pc);
        if (gus->latch_enable && addr != 0x24b)
                gus->latch_enable = 0;
        switch (addr)
        {
                case 0x340: /*MIDI control*/
                old = gus->midi_ctrl;
                gus->midi_ctrl = val;
                
                if ((val & 3) == 3)
                        gus->midi_status = 0;
                else if ((old & 3) == 3)
                {
                        gus->midi_status |= MIDI_INT_TRANSMIT;
//                        pclog("MIDI_INT_TRANSMIT\n");
                }                
                gus_midi_update_int_status(gus);
                break;
                
                case 0x341: /*MIDI data*/
                if (gus->midi_loopback)
                {
                        gus->midi_status |= MIDI_INT_RECEIVE;
                        gus->midi_data = val;
                }
                else
                        gus->midi_status |= MIDI_INT_TRANSMIT;
                break;
                
                case 0x342: /*Voice select*/
                gus->voice=val&31;
                break;
                case 0x343: /*Global select*/
                gus->global=val;
                break;
                case 0x344: /*Global low*/
//                if (gus->global!=0x43 && gus->global!=0x44) printf("Writing register %02X %02X %02X %i\n",gus->global,gus->voice,val, ins);
                switch (gus->global)
                {
                        case 0: /*Voice control*/
//                        if (val&1 && !(gus->ctrl[gus->voice]&1)) printf("Voice on %i\n",gus->voice);
                        gus->ctrl[gus->voice]=val;
                        break;
                        case 1: /*Frequency control*/
                        gus->freq[gus->voice]=(gus->freq[gus->voice]&0xFF00)|val;
                        break;
                        case 2: /*Start addr high*/
                        gus->startx[gus->voice]=(gus->startx[gus->voice]&0xF807F)|(val<<7);
                        gus->start[gus->voice]=(gus->start[gus->voice]&0x1F00FFFF)|(val<<16);
//                        printf("Write %i start %08X %08X\n",gus->voice,gus->start[gus->voice],gus->startx[gus->voice]);
                        break;
                        case 3: /*Start addr low*/
                        gus->start[gus->voice]=(gus->start[gus->voice]&0x1FFFFF00)|val;
//                        printf("Write %i start %08X %08X\n",gus->voice,gus->start[gus->voice],gus->startx[gus->voice]);
                        break;
                        case 4: /*End addr high*/
                        gus->endx[gus->voice]=(gus->endx[gus->voice]&0xF807F)|(val<<7);
                        gus->end[gus->voice]=(gus->end[gus->voice]&0x1F00FFFF)|(val<<16);
//                        printf("Write %i end %08X %08X\n",gus->voice,gus->end[gus->voice],gus->endx[gus->voice]);
                        break;
                        case 5: /*End addr low*/
                        gus->end[gus->voice]=(gus->end[gus->voice]&0x1FFFFF00)|val;
//                        printf("Write %i end %08X %08X\n",gus->voice,gus->end[gus->voice],gus->endx[gus->voice]);
                        break;

                        case 0x6: /*Ramp frequency*/
                        gus->rfreq[gus->voice] = (int)( (double)((val & 63)*512)/(double)(1 << (3*(val >> 6))));
//                        printf("RFREQ %02X %i %i %f\n",val,gus->voice,gus->rfreq[gus->voice],(double)(val & 63)/(double)(1 << (3*(val >> 6))));
                        break;

                        case 0x9: /*Current volume*/
                        gus->curvol[gus->voice] = gus->rcur[gus->voice] = (gus->rcur[gus->voice] & ~(0xff << 6)) | (val << 6);
//                        printf("Vol %i is %04X\n",gus->voice,gus->curvol[gus->voice]);
                        break;

                        case 0xA: /*Current addr high*/
                        gus->cur[gus->voice]=(gus->cur[gus->voice]&0x1F00FFFF)|(val<<16);
gus->curx[gus->voice]=(gus->curx[gus->voice]&0xF807F00)|((val<<7)<<8);
//                      gus->cur[gus->voice]=(gus->cur[gus->voice]&0x0F807F00)|((val<<7)<<8);
//                        printf("Write %i cur %08X\n",gus->voice,gus->cur[gus->voice],gus->curx[gus->voice]);
                        break;
                        case 0xB: /*Current addr low*/
                        gus->cur[gus->voice]=(gus->cur[gus->voice]&0x1FFFFF00)|val;
//                        printf("Write %i cur %08X\n",gus->voice,gus->cur[gus->voice],gus->curx[gus->voice]);
                        break;

                        case 0x42: /*DMA address low*/
                        gus->dmaaddr=(gus->dmaaddr&0xFF000)|(val<<4);
                        break;

                        case 0x43: /*Address low*/
                        gus->addr=(gus->addr&0xFFF00)|val;
                        break;
                        case 0x45: /*Timer control*/
//                        printf("Timer control %02X\n",val);
                        gus->tctrl=val;
                        break;
                }
                break;
                case 0x345: /*Global high*/
//                if (gus->global!=0x43 && gus->global!=0x44) printf("HWriting register %02X %02X %02X %04X:%04X %i %X\n",gus->global,gus->voice,val,CS,pc, ins, gus->rcur[1] >> 10);
                switch (gus->global)
                {
                        case 0: /*Voice control*/
                        if (!(val&1) && gus->ctrl[gus->voice]&1)
                        {
//                                printf("Voice on %i - start %05X end %05X freq %04X\n",gus->voice,gus->start[gus->voice],gus->end[gus->voice],gus->freq[gus->voice]);
//                                if (val&0x40) gus->cur[gus->voice]=gus->end[gus->voice]<<8;
//                                else          gus->cur[gus->voice]=gus->start[gus->voice]<<8;
                        }

                        gus->ctrl[gus->voice] = val & 0x7f;

                        old = gus->waveirqs[gus->voice];                        
                        gus->waveirqs[gus->voice] = ((val & 0xa0) == 0xa0) ? 1 : 0;                        
                        if (gus->waveirqs[gus->voice] != old) 
                                pollgusirqs(gus);
                        break;
                        case 1: /*Frequency control*/
                        gus->freq[gus->voice]=(gus->freq[gus->voice]&0xFF)|(val<<8);
                        break;
                        case 2: /*Start addr high*/
                        gus->startx[gus->voice]=(gus->startx[gus->voice]&0x07FFF)|(val<<15);
                        gus->start[gus->voice]=(gus->start[gus->voice]&0x00FFFFFF)|((val&0x1F)<<24);
//                        printf("Write %i start %08X %08X %02X\n",gus->voice,gus->start[gus->voice],gus->startx[gus->voice],val);
                        break;
                        case 3: /*Start addr low*/
                        gus->startx[gus->voice]=(gus->startx[gus->voice]&0xFFF80)|(val&0x7F);
                        gus->start[gus->voice]=(gus->start[gus->voice]&0x1FFF00FF)|(val<<8);
//                        printf("Write %i start %08X %08X\n",gus->voice,gus->start[gus->voice],gus->startx[gus->voice]);
                        break;
                        case 4: /*End addr high*/
                        gus->endx[gus->voice]=(gus->endx[gus->voice]&0x07FFF)|(val<<15);
                        gus->end[gus->voice]=(gus->end[gus->voice]&0x00FFFFFF)|((val&0x1F)<<24);
//                        printf("Write %i end %08X %08X %02X\n",gus->voice,gus->end[gus->voice],gus->endx[gus->voice],val);
                        break;
                        case 5: /*End addr low*/
                        gus->endx[gus->voice]=(gus->endx[gus->voice]&0xFFF80)|(val&0x7F);
                        gus->end[gus->voice]=(gus->end[gus->voice]&0x1FFF00FF)|(val<<8);
//                        printf("Write %i end %08X %08X\n",gus->voice,gus->end[gus->voice],gus->endx[gus->voice]);
                        break;

                        case 0x6: /*Ramp frequency*/
                        gus->rfreq[gus->voice] = (int)( (double)((val & 63) * (1 << 10))/(double)(1 << (3 * (val >> 6))));
//                        pclog("Ramp freq %02X %i %i %f %i\n", val, gus->voice, gus->rfreq[gus->voice], (double)(val & 63)/(double)(1 << (3*(val >> 6))), ins);
                        break;
                        case 0x7: /*Ramp start*/
                        gus->rstart[gus->voice] = val << 14;
//                        pclog("Ramp start %04X\n", gus->rstart[gus->voice] >> 10);
                        break;
                        case 0x8: /*Ramp end*/
                        gus->rend[gus->voice] = val << 14;
//                        pclog("Ramp end %04X\n", gus->rend[gus->voice] >> 10);
                        break;
                        case 0x9: /*Current volume*/
                        gus->curvol[gus->voice] = gus->rcur[gus->voice] = (gus->rcur[gus->voice] & ~(0xff << 14)) | (val << 14);
//                        printf("Vol %i is %04X\n",gus->voice,gus->curvol[gus->voice]);
                        break;

                        case 0xA: /*Current addr high*/
                        gus->cur[gus->voice]=(gus->cur[gus->voice]&0x00FFFFFF)|((val&0x1F)<<24);
                        gus->curx[gus->voice]=(gus->curx[gus->voice]&0x07FFF00)|((val<<15)<<8);
//                        printf("Write %i cur %08X %08X %02X\n",gus->voice,gus->cur[gus->voice],gus->curx[gus->voice],val);
//                      gus->cur[gus->voice]=(gus->cur[gus->voice]&0x007FFF00)|((val<<15)<<8);
                        break;
                        case 0xB: /*Current addr low*/
                        gus->cur[gus->voice]=(gus->cur[gus->voice]&0x1FFF00FF)|(val<<8);
gus->curx[gus->voice]=(gus->curx[gus->voice]&0xFFF8000)|((val&0x7F)<<8);
//                      gus->cur[gus->voice]=(gus->cur[gus->voice]&0x0FFF8000)|((val&0x7F)<<8);
//                        printf("Write %i cur %08X %08X\n",gus->voice,gus->cur[gus->voice],gus->curx[gus->voice]);
                        break;
                        case 0xC: /*Pan*/
                        gus->pan_l[gus->voice] = 15 - (val & 0xf);
                        gus->pan_r[gus->voice] = (val & 0xf);
                        break;
                        case 0xD: /*Ramp control*/
                        old = gus->rampirqs[gus->voice];
                        gus->rctrl[gus->voice] = val & 0x7F;
                        gus->rampirqs[gus->voice] = ((val & 0xa0) == 0xa0) ? 1 : 0;                        
                        if (gus->rampirqs[gus->voice] != old)
                                pollgusirqs(gus);
//                        printf("Ramp control %02i %02X %02X %i\n",gus->voice,val,gus->rampirqs[gus->voice],ins);
                        break;

                        case 0xE:
                        gus->voices=(val&63)+1;
                        if (gus->voices>32) gus->voices=32;
                        if (gus->voices<14) gus->voices=14;
                        gus->global=val;
//                        printf("GUS voices %i\n",val&31);
                        if (gus->voices < 14)
                                gus->samp_latch = (int)(TIMER_USEC * (1000000.0 / 44100.0));
                        else
                                gus->samp_latch = (int)(TIMER_USEC * (1000000.0 / gusfreqs[gus->voices - 14]));
                        break;

                        case 0x41: /*DMA*/
                        if (val&1 && gus->dma != -1)
                        {
//                                printf("DMA start! %05X %02X\n",gus->dmaaddr,val);
                                if (val & 2)
                                {
                                        c=0;
                                        while (c<65536)
                                        {
                                                int dma_result;
                                                d = gus->ram[gus->dmaaddr];
                                                if (val & 0x80) d ^= 0x80;
                                                dma_result = dma_channel_write(gus->dma, d);
                                                if (dma_result == DMA_NODATA)
                                                        break;
                                                gus->dmaaddr++;
                                                gus->dmaaddr&=0xFFFFF;
                                                c++;
                                                if (dma_result & DMA_OVER)
                                                        break;
                                        }
//                                        printf("GUS->MEM Transferred %i bytes\n",c);
                                        gus->dmactrl=val&~0x40;
                                        if (val&0x20) gus->irqnext=1;
                                }
                                else
                                {
                                        c=0;
                                        while (c<65536)
                                        {
                                                d = dma_channel_read(gus->dma);
                                                if (d == DMA_NODATA)
                                                        break;
                                                if (val&0x80) d^=0x80;
                                                gus->ram[gus->dmaaddr]=d;
                                                gus->dmaaddr++;
                                                gus->dmaaddr&=0xFFFFF;
                                                c++;
                                                if (d & DMA_OVER)
                                                        break;
                                        }
//                                        printf("MEM->GUS Transferred %i bytes\n",c);
                                        gus->dmactrl=val&~0x40;
                                        if (val&0x20) gus->irqnext=1;
                                }
//                                exit(-1);
                        }
                        break;

                        case 0x42: /*DMA address low*/
                        gus->dmaaddr=(gus->dmaaddr&0xFF0)|(val<<12);
                        break;

                        case 0x43: /*Address low*/
                        gus->addr=(gus->addr&0xF00FF)|(val<<8);
                        break;
                        case 0x44: /*Address high*/
                        gus->addr=(gus->addr&0xFFFF)|((val<<16)&0xF0000);
                        break;
                        case 0x45: /*Timer control*/
                        if (!(val&4)) gus->irqstatus&=~4;
                        if (!(val&8)) gus->irqstatus&=~8;
                        if (!(val & 0x20))
                        {
                                gus->ad_status &= ~0x18;
                                nmi = 0;
                        }
                        if (!(val & 0x02))
                        {
                                gus->ad_status &= ~0x01;
                                nmi = 0;
                        }
//                        printf("Timer control %02X\n",val);
/*                        if ((val&4) && !(gus->tctrl&4))
                        {
                                gus->t1=gus->t1l;
                                gus->t1on=1;
                        }*/
                        gus->tctrl=val;
                        gus->sb_ctrl = val;
                        break;
                        case 0x46: /*Timer 1*/
                        gus->t1 = gus->t1l = val;
                        gus->t1on = 1;
//                        printf("GUS timer 1 %i\n",val);
                        break;
                        case 0x47: /*Timer 2*/
                        gus->t2 = gus->t2l = val;
                        gus->t2on = 1;
//                        printf("GUS timer 2 %i\n",val);
                        break;
                        
                        case 0x4c: /*Reset*/
                        gus->reset = val;
                        break;
                }
                break;
                case 0x347: /*DRAM access*/
                gus->ram[gus->addr]=val;
//                pclog("GUS RAM write %05X %02X\n",gus->addr,val);
                gus->addr&=0xFFFFF;
                break;
                case 0x248: case 0x388: 
                gus->adcommand = val; 
//                pclog("Setting ad command %02X %02X %p\n", val, gus->adcommand, &gus->adcommand);
                break;
                
                case 0x389:
                if ((gus->tctrl & GUS_TIMER_CTRL_AUTO) || gus->adcommand != 4)
                {
                        gus->ad_data = val;
                        gus->ad_status |= 0x01;
                        if (gus->sb_ctrl & 0x02)
                        {
                                if (gus->sb_nmi)
                                        nmi = 1;
                                else if (gus->irq != -1)
                                        picint(1 << gus->irq);
                        }
                }
                else if (!(gus->tctrl & GUS_TIMER_CTRL_AUTO) && gus->adcommand == 4)
                {
                        if (val & 0x80)
                        {
                                gus->ad_status &= ~0x60;
                        }
                        else
                        {
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
                                
                case 0x240:
                gus->midi_loopback = val & 0x20;
                gus->latch_enable = (val & 0x40) ? 2 : 1;
                break;
                
                case 0x24b:
                switch (gus->reg_ctrl & 0x07)
                {
                        case 0:
                        if (gus->latch_enable == 1)
                                gus->dma = gus_dmas[val & 7];
                        if (gus->latch_enable == 2)
                        {
                                gus->irq = gus_irqs[val & 7];
                                if (val & 0x40)
                                        gus->irq_midi = gus->irq;
                                else
                                        gus->irq_midi = gus_irqs_midi[(val >> 3) & 7];
                        
                                gus->sb_nmi = val & 0x80;
                        }
                        gus->latch_enable = 0;
//                        pclog("IRQ %i DMA %i\n", gus->irq, gus->dma);
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
                }                
                break;
                
                case 0x246:
                gus->ad_status |= 0x08;
                if (gus->sb_ctrl & 0x20)
                {
                        if (gus->sb_nmi)
                                nmi = 1;
                        else if (gus->irq != -1)
                                picint(1 << gus->irq);
                }
                break;
                case 0x24a:
                gus->sb_2xa = val;
                break;
                case 0x24c:
                gus->ad_status |= 0x10;
                if (gus->sb_ctrl & 0x20)
                {
                        if (gus->sb_nmi)
                                nmi = 1;
                        else if (gus->irq != -1)
                                picint(1 << gus->irq);
                }
                case 0x24d:
                gus->sb_2xc = val;
                break;
                case 0x24e:
                gus->sb_2xe = val;
                break;
                case 0x24f:
                gus->reg_ctrl = val;
                break;
        }
}

uint8_t readgus(uint16_t addr, void *p)
{
        gus_t *gus = (gus_t *)p;
        uint8_t val;
//        /*if (addr!=0x246) */printf("Read GUS %04X %04X(%06X):%04X %02X\n",addr,CS,cs,pc,gus->global);
        switch (addr)
        {
                case 0x340: /*MIDI status*/
                val = gus->midi_status;
//                pclog("Read MIDI status %02X\n", val);
                break;

                case 0x341: /*MIDI data*/
                val = gus->midi_data;
                gus->midi_status &= ~MIDI_INT_RECEIVE;
                gus_midi_update_int_status(gus);
                break;
                
                case 0x240: return 0;
                case 0x246: /*IRQ status*/
                val = gus->irqstatus & ~0x10;
                if (gus->ad_status & 0x19)
                        val |= 0x10;
//                pclog("Read IRQ status %02X\n", val);
                return val;

                case 0x24F: return 0;
                case 0x342: return gus->voice;
                case 0x343: return gus->global;
                case 0x344: /*Global low*/
//                /*if (gus->global!=0x43 && gus->global!=0x44) */printf("Reading register %02X %02X\n",gus->global,gus->voice);
                switch (gus->global)
                {
                        case 0x82: /*Start addr high*/
                        return gus->start[gus->voice]>>16;
                        case 0x83: /*Start addr low*/
                        return gus->start[gus->voice]&0xFF;

                        case 0x89: /*Current volume*/
                        return gus->rcur[gus->voice]>>6;
                        case 0x8A: /*Current addr high*/
                        return gus->cur[gus->voice]>>16;
                        case 0x8B: /*Current addr low*/
                        return gus->cur[gus->voice]&0xFF;

                        case 0x8F: /*IRQ status*/
                        val=gus->irqstatus2;
//                        pclog("Read IRQ status - %02X\n",val);
                        gus->rampirqs[gus->irqstatus2&0x1F]=0;
                        gus->waveirqs[gus->irqstatus2&0x1F]=0;
                        pollgusirqs(gus);
                        return val;
                        
                        case 0x00: case 0x01: case 0x02: case 0x03:
                        case 0x04: case 0x05: case 0x06: case 0x07:
                        case 0x08: case 0x09: case 0x0a: case 0x0b:
                        case 0x0c: case 0x0d: case 0x0e: case 0x0f:
                        val = 0xff;
                        break;
                        
//                        default:
//                        fatal("Bad GUS global low read %02X\n",gus->global);
                }
                break;
                case 0x345: /*Global high*/
//                /*if (gus->global!=0x43 && gus->global!=0x44) */printf("HReading register %02X %02X\n",gus->global,gus->voice);
                switch (gus->global)
                {
                        case 0x80: /*Voice control*/
//                        pclog("Read voice control %02i %02X\n", gus->voice, gus->ctrl[gus->voice]|(gus->waveirqs[gus->voice]?0x80:0));
                        return gus->ctrl[gus->voice]|(gus->waveirqs[gus->voice]?0x80:0);

                        case 0x82: /*Start addr high*/
                        return gus->start[gus->voice]>>24;
                        case 0x83: /*Start addr low*/
                        return gus->start[gus->voice]>>8;

                        case 0x89: /*Current volume*/
//                        pclog("Read current volume %i\n", gus->rcur[gus->voice] >> 14);
                        return gus->rcur[gus->voice]>>14;

                        case 0x8A: /*Current addr high*/
                        return gus->cur[gus->voice]>>24;
                        case 0x8B: /*Current addr low*/
                        return gus->cur[gus->voice]>>8;

                        case 0x8C: /*Pan*/
                        return gus->pan_r[gus->voice];

                        case 0x8D:
//                        pclog("Read ramp control %02X %04X %08X  %08X %08X\n",gus->rctrl[gus->voice]|(gus->rampirqs[gus->voice]?0x80:0),gus->rcur[gus->voice] >> 14,gus->rfreq[gus->voice],gus->rstart[gus->voice],gus->rend[gus->voice]);
                        return gus->rctrl[gus->voice]|(gus->rampirqs[gus->voice]?0x80:0);

                        case 0x8F: /*IRQ status*/
//                        pclog("Read IRQ 1\n");
                        val=gus->irqstatus2;
                        gus->rampirqs[gus->irqstatus2&0x1F]=0;
                        gus->waveirqs[gus->irqstatus2&0x1F]=0;
                        pollgusirqs(gus);
//                        pclog("Read IRQ status - %02X  %i %i\n",val, gus->waveirqs[gus->irqstatus2&0x1F], gus->rampirqs[gus->irqstatus2&0x1F]);
                        return val;

                        case 0x41: /*DMA control*/
                        val=gus->dmactrl|((gus->irqstatus&0x80)?0x40:0);
                        gus->irqstatus&=~0x80;
                        return val;
                        case 0x45: /*Timer control*/
                        return gus->tctrl;
                        case 0x49: /*Sampling control*/
                        return 0;

                        case 0x00: case 0x01: case 0x02: case 0x03:
                        case 0x04: case 0x05: case 0x06: case 0x07:
                        case 0x08: case 0x09: case 0x0a: case 0x0b:
                        case 0x0c: case 0x0d: case 0x0e: case 0x0f:
                        val = 0xff;
                        break;
                        
//                        default:
//                        fatal("Bad GUS global high read %02X\n",gus->global);
                }                
                break;
                case 0x346: return 0xff;
                case 0x347: /*DRAM access*/
                val=gus->ram[gus->addr];
//                pclog("GUS RAM read %05X %02X\n",gus->addr,val);
                gus->addr&=0xFFFFF;
                return val;
                case 0x349: return 0;
                case 0x746: /*Revision level*/
                return 0xff; /*Pre 3.7 - no mixer*/

                case 0x24b:
                switch (gus->reg_ctrl & 0x07)
                {
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
                }                
                break;

                case 0x24c:
                val = gus->sb_2xc;
                if (gus->reg_ctrl & 0x20)
                        gus->sb_2xc &= 0x80;
                break;
                case 0x24e:
/*                gus->ad_status |= 0x10;
                if (gus->reg_ctrl & 0x80)
                {
                        gus->reg_ctrl_r |= 0x80;
                        if (gus->sb_nmi)
                                nmi = 1;
                        else
                                picint(1 << gus->irq);
                }*/
                return gus->sb_2xe;
                
                case 0x248: case 0x388:
//                pclog("Read ad_status %02X\n", gus->ad_status);
                if (gus->tctrl & GUS_TIMER_CTRL_AUTO)
                        val = gus->sb_2xa;
                else
                {
                        val = gus->ad_status & ~(gus->ad_timer_ctrl & 0x60);
                        if (val & 0x60)
                                val |= 0x80;
                }
                break;

                case 0x249: 
                gus->ad_status &= ~0x01;
                nmi = 0;
                case 0x389:
                val = gus->ad_data;
                break;
                
                case 0x24A:
                val = gus->adcommand;
//                pclog("Read ad command %02X %02X %p\n", gus->adcommand, val, &gus->adcommand);
                break;

        }
//        printf("Bad GUS read %04X! %02X\n",addr,gus->global);
//        exit(-1);
        return val;
}

void gus_poll_timer_1(void *p)
{
        gus_t *gus = (gus_t *)p;
        
	gus->timer_1 += (TIMER_USEC * 80);
//	pclog("gus_poll_timer_1 %i %i  %i %i %02X\n", gustime, gus->t1on, gus->t1, gus->t1l, gus->tctrl);
        if (gus->t1on)
        {
                gus->t1++;
                if (gus->t1 > 0xFF)
                {                        
//                        gus->t1on=0;
                        gus->t1=gus->t1l;
                        gus->ad_status |= 0x40;
                        if (gus->tctrl&4)
                        {
                                if (gus->irq != -1)
                                        picint(1 << gus->irq);
                                gus->ad_status |= 0x04;
                                gus->irqstatus |= 0x04;
//                                pclog("GUS T1 IRQ!\n");
                        }
                }
        }
        if (gus->irqnext)
        {
//                pclog("Take IRQ\n");
                gus->irqnext=0;
                gus->irqstatus|=0x80;
                if (gus->irq != -1)
                        picint(1 << gus->irq);
        }
        gus_midi_update_int_status(gus);
}

void gus_poll_timer_2(void *p)
{
        gus_t *gus = (gus_t *)p;
        
	gus->timer_2 += (TIMER_USEC * 320);
//	pclog("pollgus2 %i %i  %i %i %02X\n", gustime, gus->t2on, gus->t2, gus->t2l, gus->tctrl);
        if (gus->t2on)
        {
                gus->t2++;
                if (gus->t2 > 0xFF)
                {                        
//                        gus->t2on=0;
                        gus->t2=gus->t2l;
                        gus->ad_status |= 0x20;
                        if (gus->tctrl&8)
                        {
                                if (gus->irq != -1)
                                        picint(1 << gus->irq);
                                gus->ad_status |= 0x02;
                                gus->irqstatus |= 0x08;
//                                pclog("GUS T2 IRQ!\n");
                        }
                }
        }
        if (gus->irqnext)
        {
//                pclog("Take IRQ\n");
                gus->irqnext=0;
                gus->irqstatus|=0x80;
                if (gus->irq != -1)
                        picint(1 << gus->irq);
        }
}

static void gus_update(gus_t *gus)
{
        for (; gus->pos < sound_pos_global; gus->pos++)
        {
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

void gus_poll_wave(void *p)
{
        gus_t *gus = (gus_t *)p;
        uint32_t addr;
        int d;
        int16_t v;
        int32_t vl;
        int update_irqs = 0;
        
        gus_update(gus);
        
        gus->samp_timer += gus->samp_latch;
        
        gus->out_l = gus->out_r = 0;

        if ((gus->reset & 3) != 3)
                return;
//pclog("gus_poll_wave\n");        
        for (d=0;d<32;d++)
        {
                if (!(gus->ctrl[d] & 3))
                {
                        if (gus->ctrl[d] & 4)
                        {
                                addr = gus->cur[d] >> 9;
                                addr = (addr & 0xC0000) | ((addr << 1) & 0x3FFFE);
                                if (!(gus->freq[d] >> 10)) /*Interpolate*/
                                {
                                        vl  = (int16_t)(int8_t)((gus->ram[(addr + 1) & 0xFFFFF] ^ 0x80) - 0x80) * (511 - (gus->cur[d] & 511));
                                        vl += (int16_t)(int8_t)((gus->ram[(addr + 3) & 0xFFFFF] ^ 0x80) - 0x80) * (gus->cur[d] & 511);
                                        v = vl >> 9;
                                }
                                else
                                        v = (int16_t)(int8_t)((gus->ram[(addr + 1) & 0xFFFFF] ^ 0x80) - 0x80);
                        }
                        else
                        {
                                if (!(gus->freq[d] >> 10)) /*Interpolate*/
                                {
                                        vl  = ((int8_t)((gus->ram[(gus->cur[d] >> 9) & 0xFFFFF] ^ 0x80) - 0x80)) * (511 - (gus->cur[d] & 511));
                                        vl += ((int8_t)((gus->ram[((gus->cur[d] >> 9) + 1) & 0xFFFFF] ^ 0x80) - 0x80)) * (gus->cur[d] & 511);
                                        v = vl >> 9;
                                }
                                else
                                        v = (int16_t)(int8_t)((gus->ram[(gus->cur[d] >> 9) & 0xFFFFF] ^ 0x80) - 0x80);
                        }

//                        pclog("Voice %i : %04X %05X %04X ", d, v, gus->cur[d] >> 9, gus->rcur[d] >> 10);
                        if ((gus->rcur[d] >> 14) > 4095) v = (int16_t)(float)(v) * 24.0 * vol16bit[4095];
                        else                            v = (int16_t)(float)(v) * 24.0 * vol16bit[(gus->rcur[d]>>10) & 4095];
//                        pclog("%f %04X\n", vol16bit[(gus->rcur[d]>>10) & 4095], v);

                        gus->out_l += (v * gus->pan_l[d]) / 7;
                        gus->out_r += (v * gus->pan_r[d]) / 7;

                        if (gus->ctrl[d]&0x40)
                        {
                                gus->cur[d] -= (gus->freq[d] >> 1);
                                if (gus->cur[d] <= gus->start[d])
                                {
                                        int diff = gus->start[d] - gus->cur[d];
                                        if (!(gus->rctrl[d]&4))
                                        {
                                                if (!(gus->ctrl[d]&8)) 
                                                {
                                                        gus->ctrl[d] |= 1;
                                                        gus->cur[d] = (gus->ctrl[d] & 0x40) ? gus->end[d] : gus->start[d];
                                                }                                                
                                                else 
                                                {
                                                        if (gus->ctrl[d]&0x10) gus->ctrl[d]^=0x40;
                                                        gus->cur[d] = (gus->ctrl[d] & 0x40) ? (gus->end[d] - diff) : (gus->start[d] + diff);
                                                }
                                        }
                                        if ((gus->ctrl[d] & 0x20) && !gus->waveirqs[d])
                                        {
                                                gus->waveirqs[d] = 1;
                                                update_irqs = 1;
//                                                pclog("Causing wave IRQ %02X %i\n", gus->ctrl[d], d);
                                        }
                                }
                        }
                        else
                        {
                                gus->cur[d] += (gus->freq[d] >> 1);

                                if (gus->cur[d] >= gus->end[d])
                                {
                                        int diff = gus->cur[d] - gus->end[d];
                                        if (!(gus->rctrl[d]&4))
                                        {
                                                if (!(gus->ctrl[d]&8)) 
                                                {
                                                        gus->ctrl[d] |= 1;
                                                        gus->cur[d] = (gus->ctrl[d] & 0x40) ? gus->end[d] : gus->start[d];
                                                }                                                
                                                else 
                                                {
                                                        if (gus->ctrl[d]&0x10) gus->ctrl[d]^=0x40;
                                                        gus->cur[d] = (gus->ctrl[d] & 0x40) ? (gus->end[d] - diff) : (gus->start[d] + diff);
                                                }
                                        }
                                        if ((gus->ctrl[d] & 0x20) && !gus->waveirqs[d]) 
                                        {
                                                gus->waveirqs[d] = 1;
                                                update_irqs = 1;
//                                                pclog("Causing wave IRQ %02X %i\n", gus->ctrl[d], d);
                                        }
                                }
                        }
                }
                if (!(gus->rctrl[d] & 3))
                {
                        if (gus->rctrl[d] & 0x40)
                        {
                                gus->rcur[d] -= gus->rfreq[d];
                                if (gus->rcur[d] <= gus->rstart[d])
                                {
                                        int diff = gus->rstart[d] - gus->rcur[d];
                                        if (!(gus->rctrl[d] & 8)) 
                                        {
                                                gus->rctrl[d] |= 1;
                                                gus->rcur[d] = (gus->rctrl[d] & 0x40) ? gus->rstart[d] : gus->rend[d];
                                        }                                                
                                        else 
                                        {
                                                if (gus->rctrl[d] & 0x10) gus->rctrl[d] ^= 0x40;
                                                gus->rcur[d] = (gus->rctrl[d] & 0x40) ? (gus->rend[d] - diff) : (gus->rstart[d] + diff);
                                        }

                                        if ((gus->rctrl[d] & 0x20) && !gus->rampirqs[d])
                                        {
                                                gus->rampirqs[d] = 1;
                                                update_irqs = 1;
//                                                pclog("Causing ramp IRQ %02X %i\n",gus->rctrl[d], d);
                                        }
                                }
                        }
                        else
                        {
                                gus->rcur[d] += gus->rfreq[d];
//                                        if (d == 1) printf("RCUR+ %i %08X %08X %08X %08X\n",d,gus->rfreq[d],gus->rcur[d],gus->rstart[d],gus->rend[d]);
                                if (gus->rcur[d] >= gus->rend[d])
                                {
                                        int diff = gus->rcur[d] - gus->rend[d];
                                        if (!(gus->rctrl[d] & 8)) 
                                        {
                                                gus->rctrl[d] |= 1;
                                                gus->rcur[d] = (gus->rctrl[d] & 0x40) ? gus->rstart[d] : gus->rend[d];
                                        }                                                
                                        else 
                                        {
                                                if (gus->rctrl[d] & 0x10) gus->rctrl[d] ^= 0x40;
                                                gus->rcur[d] = (gus->rctrl[d] & 0x40) ? (gus->rend[d] - diff) : (gus->rstart[d] + diff);
                                        }

                                        if ((gus->rctrl[d] & 0x20) && !gus->rampirqs[d])
                                        {
                                                gus->rampirqs[d] = 1;
                                                update_irqs = 1;
//                                                        pclog("Causing ramp IRQ %02X %i\n",gus->rctrl[d], d);
                                        }
                                }
                        }
                }
        }

        if (update_irqs)
                pollgusirqs(gus);
}

static void gus_get_buffer(int32_t *buffer, int len, void *p)
{
        gus_t *gus = (gus_t *)p;
        int c;

        gus_update(gus);
        
        for (c = 0; c < len * 2; c++)
        {
                buffer[c] += (int32_t)gus->buffer[c & 1][c >> 1];
        }

        gus->pos = 0;
}


void *gus_init()
{
        int c;
	double out = 1.0;
        gus_t *gus = malloc(sizeof(gus_t));
        memset(gus, 0, sizeof(gus_t));

        gus->ram = malloc(1 << 20);
        memset(gus->ram, 0, 1 << 20);
        
        pclog("gus_init\n");
        
        for (c=0;c<32;c++)
        {
                gus->ctrl[c]=1;
                gus->rctrl[c]=1;
                gus->rfreq[c]=63*512;
        }

	for (c=4095;c>=0;c--) {
		vol16bit[c]=out;//(float)c/4095.0;//out;
		out/=1.002709201;		/* 0.0235 dB Steps */
	}

	printf("Top volume %f %f %f %f\n",vol16bit[4095],vol16bit[3800],vol16bit[3000],vol16bit[2048]);
	gus->voices=14;

        gus->samp_timer = gus->samp_latch = (int)(TIMER_USEC * (1000000.0 / 44100.0));

        gus->t1l = gus->t2l = 0xff;
                
        io_sethandler(0x0240, 0x0010, readgus, NULL, NULL, writegus, NULL, NULL,  gus);
        io_sethandler(0x0340, 0x0010, readgus, NULL, NULL, writegus, NULL, NULL,  gus);
        io_sethandler(0x0746, 0x0001, readgus, NULL, NULL, writegus, NULL, NULL,  gus);        
        io_sethandler(0x0388, 0x0002, readgus, NULL, NULL, writegus, NULL, NULL,  gus);
        timer_add(gus_poll_wave, &gus->samp_timer, TIMER_ALWAYS_ENABLED,  gus);
        timer_add(gus_poll_timer_1, &gus->timer_1, TIMER_ALWAYS_ENABLED,  gus);
        timer_add(gus_poll_timer_2, &gus->timer_2, TIMER_ALWAYS_ENABLED,  gus);

        sound_add_handler(gus_get_buffer, gus);
        
        return gus;
}

void gus_close(void *p)
{
        gus_t *gus = (gus_t *)p;
        
        free(gus->ram);
        free(gus);
}

void gus_speed_changed(void *p)
{
        gus_t *gus = (gus_t *)p;

        if (gus->voices < 14)
                gus->samp_latch = (int)(TIMER_USEC * (1000000.0 / 44100.0));
        else
                gus->samp_latch = (int)(TIMER_USEC * (1000000.0 / gusfreqs[gus->voices - 14]));
}

device_t gus_device =
{
        "Gravis UltraSound",
        0,
        gus_init,
        gus_close,
        NULL,
        gus_speed_changed,
        NULL,
        NULL
};
