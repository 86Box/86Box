#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../ibm.h"
#include "../io.h"
#include "../dma.h"
#include "../pic.h"
#include "../pit.h"
#include "../mem.h"
#include "../rom.h"
#include "../nvr.h"
#include "../timer.h"
#include "../device.h"
#include "sound.h"
#include "filters.h"
#include "snd_opl.h"
#include "snd_ym7128.h"


typedef struct adgold_t
{
        int adgold_irq_status;

        uint8_t adgold_eeprom[0x19];

        uint8_t adgold_status;
        int adgold_38x_state, adgold_38x_addr;
        uint8_t adgold_38x_regs[0x19];

        int adgold_mma_addr;
        uint8_t adgold_mma_regs[2][0xe];

        int adgold_mma_enable[2];
        uint8_t adgold_mma_fifo[2][256];
        int adgold_mma_fifo_start[2], adgold_mma_fifo_end[2];
        uint8_t adgold_mma_status;

        int16_t adgold_mma_out[2];
        int adgold_mma_intpos[2];

        int adgold_mma_timer_count;

        struct
        {
                int timer0_latch, timer0_count;
                int timerbase_latch, timerbase_count;
                int timer1_latch, timer1_count;
                int timer2_latch, timer2_count, timer2_read;
        
                int voice_count[2], voice_latch[2];
        } adgold_mma;

        opl_t    opl;
        ym7128_t ym7128;
        
        int fm_vol_l, fm_vol_r;
        int samp_vol_l, samp_vol_r;
        int vol_l, vol_r;
        int treble, bass;

        int16_t opl_buffer[SOUNDBUFLEN * 2];
        int16_t mma_buffer[2][SOUNDBUFLEN];

        int pos;
        
        int surround_enabled;
} adgold_t;

static int attenuation[0x40];
static int bass_attenuation[0x10] =
{
        (int)(1.995 * 16384), /*12 dB - filter output is at +6 dB so we use 6 dB here*/
        (int)(1.995 * 16384),
        (int)(1.995 * 16384),
        (int)(1.413 * 16384), /*9 dB*/
        (int)(1 * 16384),     /*6 dB*/
        (int)(0.708 * 16384), /*3 dB*/
        (int)(0 * 16384),     /*0 dB*/
        (int)(0.708 * 16384), /*3 dB*/
        (int)(1 * 16384),     /*6 dB*/        
        (int)(1.413 * 16384), /*9 dB*/
        (int)(1.995 * 16384), /*12 dB*/
        (int)(2.819 * 16384), /*15 dB*/
        (int)(2.819 * 16384),
        (int)(2.819 * 16384),
        (int)(2.819 * 16384),
        (int)(2.819 * 16384)
};

static int bass_cut[6] =
{
        (int)(0.126 * 16384), /*-12 dB*/
        (int)(0.126 * 16384), /*-12 dB*/
        (int)(0.126 * 16384), /*-12 dB*/
        (int)(0.178 * 16384), /*-9 dB*/
        (int)(0.251 * 16384), /*-6 dB*/
        (int)(0.354 * 16384)  /*-3 dB - filter output is at +6 dB*/
};

static int treble_attenuation[0x10] =
{
        (int)(1.995 * 16384), /*12 dB - filter output is at +6 dB so we use 6 dB here*/
        (int)(1.995 * 16384),
        (int)(1.995 * 16384),
        (int)(1.413 * 16384), /*9 dB*/
        (int)(1 * 16384),     /*6 dB*/
        (int)(0.708 * 16384), /*3 dB*/
        (int)(0 * 16384),     /*0 dB*/
        (int)(0.708 * 16384), /*3 dB*/
        (int)(1 * 16384),     /*6 dB*/        
        (int)(1.413 * 16384), /*9 dB*/
        (int)(1.995 * 16384), /*12 dB*/
        (int)(1.995 * 16384),
        (int)(1.995 * 16384),
        (int)(1.995 * 16384),
        (int)(1.995 * 16384),
        (int)(1.995 * 16384)
};

static int treble_cut[6] =
{
        (int)(0.126 * 16384), /*-12 dB*/
        (int)(0.126 * 16384), /*-12 dB*/
        (int)(0.126 * 16384), /*-12 dB*/
        (int)(0.178 * 16384), /*-9 dB*/
        (int)(0.251 * 16384), /*-6 dB*/
        (int)(0.354 * 16384)  /*-3 dB - filter output is at +6 dB*/
};

void adgold_timer_poll();
void adgold_update(adgold_t *adgold);

void adgold_update_irq_status(adgold_t *adgold)
{
        uint8_t temp = 0xf;

        if (!(adgold->adgold_mma_regs[0][8] & 0x10) && (adgold->adgold_mma_status & 0x10))      /*Timer 0*/
                temp &= ~2;
        if (!(adgold->adgold_mma_regs[0][8] & 0x20) && (adgold->adgold_mma_status & 0x20))      /*Timer 1*/
                temp &= ~2;
        if (!(adgold->adgold_mma_regs[0][8] & 0x40) && (adgold->adgold_mma_status & 0x40))      /*Timer 2*/
                temp &= ~2;

        if ((adgold->adgold_mma_status & 0x01) &&  !(adgold->adgold_mma_regs[0][0xc] & 2))
                temp &= ~2;
        if ((adgold->adgold_mma_status & 0x02) &&  !(adgold->adgold_mma_regs[1][0xc] & 2))
                temp &= ~2;
        adgold->adgold_status = temp;
        
        if ((adgold->adgold_status ^ 0xf) && !adgold->adgold_irq_status)
        {
                picint(0x80);
        }
                
        adgold->adgold_irq_status = adgold->adgold_status ^ 0xf;
}

void adgold_getsamp_dma(adgold_t *adgold, int channel)
{
        int temp;
        
        if ((adgold->adgold_mma_regs[channel][0xc] & 0x60) && (((adgold->adgold_mma_fifo_end[channel] - adgold->adgold_mma_fifo_start[channel]) & 255) >= 127))
                return;
                
        temp = dma_channel_read(1);
        if (temp == DMA_NODATA) return;
        adgold->adgold_mma_fifo[channel][adgold->adgold_mma_fifo_end[channel]] = temp;
        adgold->adgold_mma_fifo_end[channel] = (adgold->adgold_mma_fifo_end[channel] + 1) & 255;
        if (adgold->adgold_mma_regs[channel][0xc] & 0x60)
        {
                temp = dma_channel_read(1);
                adgold->adgold_mma_fifo[channel][adgold->adgold_mma_fifo_end[channel]] = temp;
                adgold->adgold_mma_fifo_end[channel] = (adgold->adgold_mma_fifo_end[channel] + 1) & 255;
        }      
        if (((adgold->adgold_mma_fifo_end[channel] - adgold->adgold_mma_fifo_start[channel]) & 255) >= adgold->adgold_mma_intpos[channel])
        {
                adgold->adgold_mma_status &= ~(0x01 << channel);
                adgold_update_irq_status(adgold);
        }
}

void adgold_write(uint16_t addr, uint8_t val, void *p)
{
        adgold_t *adgold = (adgold_t *)p;
        switch (addr & 7)
        {
                case 0: case 1:
                opl3_write(addr, val, &adgold->opl);
                break;

                case 2:
                if (val == 0xff)
                {
                        adgold->adgold_38x_state = 1;
                        return;
                }
                if (val == 0xfe)
                {                
                        adgold->adgold_38x_state = 0;
                        return;
                }
                if (adgold->adgold_38x_state)   /*Write to control chip*/
                        adgold->adgold_38x_addr = val;
                else
                        opl3_write(addr, val, &adgold->opl);
                break;
                case 3:
                if (adgold->adgold_38x_state)
                {
                        if (adgold->adgold_38x_addr >= 0x19) break;
                        switch (adgold->adgold_38x_addr)
                        {
                                case 0x00: /*Control/ID*/
                                if (val & 1)
                                        memcpy(adgold->adgold_38x_regs, adgold->adgold_eeprom, 0x19);
                                if (val & 2)
                                        memcpy(adgold->adgold_eeprom, adgold->adgold_38x_regs, 0x19);
                                break;
                                
                                case 0x04: /*Final output volume left*/
                                adgold->adgold_38x_regs[0x04] = val;
                                adgold->vol_l = attenuation[val & 0x3f];
                                break;
                                case 0x05: /*Final output volume right*/
                                adgold->adgold_38x_regs[0x05] = val;
                                adgold->vol_r = attenuation[val & 0x3f];
                                break;
                                case 0x06: /*Bass*/
                                adgold->adgold_38x_regs[0x06] = val;
                                adgold->bass = val & 0xf;
                                break;
                                case 0x07: /*Treble*/
                                adgold->adgold_38x_regs[0x07] = val;
                                adgold->treble = val & 0xf;
                                break;
                                
                                case 0x09: /*FM volume left*/
                                adgold->adgold_38x_regs[0x09] = val;
                                adgold->fm_vol_l = (int)(int8_t)(val - 128);
                                break;
                                case 0x0a: /*FM volume right*/
                                adgold->adgold_38x_regs[0x0a] = val;
                                adgold->fm_vol_r = (int)(int8_t)(val - 128);
                                break;
                                case 0x0b: /*Sample volume left*/
                                adgold->adgold_38x_regs[0x0b] = val;
                                adgold->samp_vol_l = (int)(int8_t)(val - 128);
                                break;
                                case 0x0c: /*Sample volume right*/
                                adgold->adgold_38x_regs[0x0c] = val;
                                adgold->samp_vol_r = (int)(int8_t)(val - 128);
                                break;
                                
                                case 0x18: /*Surround*/
                                adgold->adgold_38x_regs[0x18] = val;
                                ym7128_write(&adgold->ym7128, val);
                                break;                                
                                
                                default:
                                adgold->adgold_38x_regs[adgold->adgold_38x_addr] = val;
                                break;
                        }
                }
                else
                        opl3_write(addr, val, &adgold->opl);
                break;
                case 4: case 6:
                adgold->adgold_mma_addr = val;
                break;
                case 5:
                if (adgold->adgold_mma_addr >= 0xf) break;
                switch (adgold->adgold_mma_addr)
                {
                        case 0x2:
                        adgold->adgold_mma.timer0_latch = (adgold->adgold_mma.timer0_latch & 0xff00) | val;
                        break;
                        case 0x3:
                        adgold->adgold_mma.timer0_latch = (adgold->adgold_mma.timer0_latch & 0xff) | (val << 8);
                        break;
                        case 0x4:
                        adgold->adgold_mma.timerbase_latch = (adgold->adgold_mma.timerbase_latch & 0xf00) | val;
                        break;
                        case 0x5:
                        adgold->adgold_mma.timerbase_latch = (adgold->adgold_mma.timerbase_latch & 0xff) | ((val & 0xf) << 8);
                        adgold->adgold_mma.timer1_latch = val >> 4;
                        break;
                        case 0x6:
                        adgold->adgold_mma.timer2_latch = (adgold->adgold_mma.timer2_latch & 0xff00) | val;
                        break;
                        case 0x7:
                        adgold->adgold_mma.timer2_latch = (adgold->adgold_mma.timer2_latch & 0xff) | (val << 8);
                        break;
                        
                        case 0x8: 
                        if ((val & 1) && !(adgold->adgold_mma_regs[0][8] & 1)) /*Reload timer 0*/
                                adgold->adgold_mma.timer0_count = adgold->adgold_mma.timer0_latch;
                                
                        if ((val & 2) && !(adgold->adgold_mma_regs[0][8] & 2)) /*Reload timer 1*/
                                adgold->adgold_mma.timer1_count = adgold->adgold_mma.timer1_latch;

                        if ((val & 4) && !(adgold->adgold_mma_regs[0][8] & 4)) /*Reload timer 2*/
                                adgold->adgold_mma.timer2_count = adgold->adgold_mma.timer2_latch;

                        if ((val & 8) && !(adgold->adgold_mma_regs[0][8] & 8)) /*Reload base timer*/
                                adgold->adgold_mma.timerbase_count = adgold->adgold_mma.timerbase_latch;
                        break;
                        
                        case 0x9:
                        switch (val & 0x18)
                        {
                                case 0x00: adgold->adgold_mma.voice_latch[0] = 12; break; /*44100 Hz*/
                                case 0x08: adgold->adgold_mma.voice_latch[0] = 24; break; /*22050 Hz*/
                                case 0x10: adgold->adgold_mma.voice_latch[0] = 48; break; /*11025 Hz*/
                                case 0x18: adgold->adgold_mma.voice_latch[0] = 72; break; /* 7350 Hz*/
                        }
                        if (val & 0x80)
                        {
                                adgold->adgold_mma_enable[0] = 0;
                                adgold->adgold_mma_fifo_end[0] = adgold->adgold_mma_fifo_start[0] = 0;
                                adgold->adgold_mma_status &= ~0x01;
                                adgold_update_irq_status(adgold);
                        }
                        if ((val & 0x01))     /*Start playback*/
                        {
                                if (!(adgold->adgold_mma_regs[0][0x9] & 1))
                                        adgold->adgold_mma.voice_count[0] = adgold->adgold_mma.voice_latch[0];
                                        
                                if (adgold->adgold_mma_regs[0][0xc] & 1)
                                {
                                        if (adgold->adgold_mma_regs[0][0xc] & 0x80)
                                        {
                                                adgold->adgold_mma_enable[1] = 1;
                                                adgold->adgold_mma.voice_count[1] = adgold->adgold_mma.voice_latch[1];

                                                while (((adgold->adgold_mma_fifo_end[0] - adgold->adgold_mma_fifo_start[0]) & 255) < 128)
                                                {
                                                        adgold_getsamp_dma(adgold, 0);
                                                        adgold_getsamp_dma(adgold, 1);
                                                }
                                                if (((adgold->adgold_mma_fifo_end[0] - adgold->adgold_mma_fifo_start[0]) & 255) >= adgold->adgold_mma_intpos[0])
                                                {
                                                        adgold->adgold_mma_status &= ~0x01;
                                                        adgold_update_irq_status(adgold);
                                                }
                                                if (((adgold->adgold_mma_fifo_end[1] - adgold->adgold_mma_fifo_start[1]) & 255) >= adgold->adgold_mma_intpos[1])
                                                {
                                                        adgold->adgold_mma_status &= ~0x02;
                                                        adgold_update_irq_status(adgold);
                                                }
                                        }
                                        else
                                        {
                                                while (((adgold->adgold_mma_fifo_end[0] - adgold->adgold_mma_fifo_start[0]) & 255) < 128)
                                                {
                                                        adgold_getsamp_dma(adgold, 0);
                                                }
                                                if (((adgold->adgold_mma_fifo_end[0] - adgold->adgold_mma_fifo_start[0]) & 255) >= adgold->adgold_mma_intpos[0])
                                                {
                                                        adgold->adgold_mma_status &= ~0x01;
                                                        adgold_update_irq_status(adgold);
                                                }
                                        }
                                }
                        }
                        adgold->adgold_mma_enable[0] = val & 0x01;
                        break;
                        
                        case 0xb:
                        if (((adgold->adgold_mma_fifo_end[0] - adgold->adgold_mma_fifo_start[0]) & 255) < 128)
                        {
                                adgold->adgold_mma_fifo[0][adgold->adgold_mma_fifo_end[0]] = val;
                                adgold->adgold_mma_fifo_end[0] = (adgold->adgold_mma_fifo_end[0] + 1) & 255;
                                if (((adgold->adgold_mma_fifo_end[0] - adgold->adgold_mma_fifo_start[0]) & 255) >= adgold->adgold_mma_intpos[0])
                                {
                                        adgold->adgold_mma_status &= ~0x01;
                                        adgold_update_irq_status(adgold);
                                }
                        }
                        break;
                        
                        case 0xc:
                        adgold->adgold_mma_intpos[0] = (7 - ((val >> 2) & 7)) * 8;
                        break;
                }
                adgold->adgold_mma_regs[0][adgold->adgold_mma_addr] = val;
                break;
                case 7:
                if (adgold->adgold_mma_addr >= 0xf) break;
                switch (adgold->adgold_mma_addr)
                {
                        case 0x9:
                        adgold_update(adgold);
                        switch (val & 0x18)
                        {
                                case 0x00: adgold->adgold_mma.voice_latch[1] = 12; break; /*44100 Hz*/
                                case 0x08: adgold->adgold_mma.voice_latch[1] = 24; break; /*22050 Hz*/
                                case 0x10: adgold->adgold_mma.voice_latch[1] = 48; break; /*11025 Hz*/
                                case 0x18: adgold->adgold_mma.voice_latch[1] = 72; break; /* 7350 Hz*/
                        }
                        if (val & 0x80)
                        {
                                adgold->adgold_mma_enable[1] = 0;
                                adgold->adgold_mma_fifo_end[1] = adgold->adgold_mma_fifo_start[1] = 0;
                                adgold->adgold_mma_status &= ~0x02;
                                adgold_update_irq_status(adgold);
                        }
                        if ((val & 0x01))     /*Start playback*/
                        {
                                if (!(adgold->adgold_mma_regs[1][0x9] & 1)) 
                                        adgold->adgold_mma.voice_count[1] = adgold->adgold_mma.voice_latch[1];
                                        
                                if (adgold->adgold_mma_regs[1][0xc] & 1)
                                {
                                        while (((adgold->adgold_mma_fifo_end[1] - adgold->adgold_mma_fifo_start[1]) & 255) < 128)
                                        {
                                                adgold_getsamp_dma(adgold, 1);
                                        }
                                }
                        }
                        adgold->adgold_mma_enable[1] = val & 0x01;
                        break;
                        
                        case 0xb:
                        if (((adgold->adgold_mma_fifo_end[1] - adgold->adgold_mma_fifo_start[1]) & 255) < 128)
                        {
                                adgold->adgold_mma_fifo[1][adgold->adgold_mma_fifo_end[1]] = val;
                                adgold->adgold_mma_fifo_end[1] = (adgold->adgold_mma_fifo_end[1] + 1) & 255;
                                if (((adgold->adgold_mma_fifo_end[1] - adgold->adgold_mma_fifo_start[1]) & 255) >= adgold->adgold_mma_intpos[1])
                                {
                                        adgold->adgold_mma_status &= ~0x02;
                                        adgold_update_irq_status(adgold);
                                }
                        }
                        break;

                        case 0xc:
                        adgold->adgold_mma_intpos[1] = (7 - ((val >> 2) & 7)) * 8;
                        break;
                }
                adgold->adgold_mma_regs[1][adgold->adgold_mma_addr] = val;
                break;
        }
}

uint8_t adgold_read(uint16_t addr, void *p)
{
        adgold_t *adgold = (adgold_t *)p;
        uint8_t temp = 0;
        
        switch (addr & 7)
        {
                case 0: case 1:
                temp = opl3_read(addr, &adgold->opl);
                break;
                
                case 2:
                if (adgold->adgold_38x_state)   /*Read from control chip*/
                        temp = adgold->adgold_status;
                else
                        temp = opl3_read(addr, &adgold->opl);
                break;
                
                case 3:
                if (adgold->adgold_38x_state)
                {
                        if (adgold->adgold_38x_addr >= 0x19) temp = 0xff;
                        switch (adgold->adgold_38x_addr)
                        {
                                case 0x00: /*Control/ID*/
                                if (adgold->surround_enabled)
                                        temp = 0x50; /*16-bit ISA, surround module, no telephone/CDROM*/
                                else
                                        temp = 0x70; /*16-bit ISA, no telephone/surround/CD-ROM*/
                                break;
                                
                                default:
                                temp = adgold->adgold_38x_regs[adgold->adgold_38x_addr];
                        }                
                }
                else
                        temp = opl3_read(addr, &adgold->opl);
                break;

                case 4: case 6:
                temp = adgold->adgold_mma_status;
                adgold->adgold_mma_status = 0; /*JUKEGOLD expects timer status flags to auto-clear*/
                adgold_update_irq_status(adgold);
                break;
                case 5:
                if (adgold->adgold_mma_addr >= 0xf) temp = 0xff;
                switch (adgold->adgold_mma_addr)
                {
                        case 6: /*Timer 2 low*/
                        adgold->adgold_mma.timer2_read = adgold->adgold_mma.timer2_count;
                        temp = adgold->adgold_mma.timer2_read & 0xff;
                        break;
                        case 7: /*Timer 2 high*/
                        temp = adgold->adgold_mma.timer2_read >> 8;
                        break;
                        
                        default:
                        temp = adgold->adgold_mma_regs[0][adgold->adgold_mma_addr];
                        break;
                }
                break;
                case 7:
                if (adgold->adgold_mma_addr >= 0xf) temp = 0xff;
                temp = adgold->adgold_mma_regs[1][adgold->adgold_mma_addr];
                break;
        }
        return temp;
}

void adgold_update(adgold_t *adgold)
{
        for (; adgold->pos < sound_pos_global; adgold->pos++)
        {
                adgold->mma_buffer[0][adgold->pos] = adgold->mma_buffer[1][adgold->pos] = 0;
        
                if (adgold->adgold_mma_regs[0][9] & 0x20)
                        adgold->mma_buffer[0][adgold->pos] += adgold->adgold_mma_out[0] / 2;
                if (adgold->adgold_mma_regs[0][9] & 0x40)
                        adgold->mma_buffer[1][adgold->pos] += adgold->adgold_mma_out[0] / 2;

                if (adgold->adgold_mma_regs[1][9] & 0x20)
                        adgold->mma_buffer[0][adgold->pos] += adgold->adgold_mma_out[1] / 2;
                if (adgold->adgold_mma_regs[1][9] & 0x40)
                        adgold->mma_buffer[1][adgold->pos] += adgold->adgold_mma_out[1] / 2;
        }
}

void adgold_mma_poll(adgold_t *adgold, int channel)
{
        int16_t dat;

        adgold_update(adgold);
        
        if (adgold->adgold_mma_fifo_start[channel] != adgold->adgold_mma_fifo_end[channel])
        {
                switch (adgold->adgold_mma_regs[channel][0xc] & 0x60)
                {
                        case 0x00: /*8-bit*/
                        dat = adgold->adgold_mma_fifo[channel][adgold->adgold_mma_fifo_start[channel]] * 256;
                        adgold->adgold_mma_out[channel] = dat;
                        adgold->adgold_mma_fifo_start[channel] = (adgold->adgold_mma_fifo_start[channel] + 1) & 255;
                        break;
                        
                        case 0x40: /*12-bit sensible format*/
                        if (((adgold->adgold_mma_fifo_end[channel] - adgold->adgold_mma_fifo_start[channel]) & 255) < 2)
                                return;
                                
                        dat = adgold->adgold_mma_fifo[channel][adgold->adgold_mma_fifo_start[channel]] & 0xf0;
                        adgold->adgold_mma_fifo_start[channel] = (adgold->adgold_mma_fifo_start[channel] + 1) & 255;
                        dat |= (adgold->adgold_mma_fifo[channel][adgold->adgold_mma_fifo_start[channel]] << 8);
                        adgold->adgold_mma_fifo_start[channel] = (adgold->adgold_mma_fifo_start[channel] + 1) & 255;
                        adgold->adgold_mma_out[channel] = dat;
                        break;
                }
                
                if (adgold->adgold_mma_regs[channel][0xc] & 1)
                {
                        adgold_getsamp_dma(adgold, channel);
                }
                if (((adgold->adgold_mma_fifo_end[channel] - adgold->adgold_mma_fifo_start[channel]) & 255) < adgold->adgold_mma_intpos[channel] && !(adgold->adgold_mma_status & 0x01))
                {
                        adgold->adgold_mma_status |= 1 << channel;
                        adgold_update_irq_status(adgold);
                }
        }
        if (adgold->adgold_mma_fifo_start[channel] == adgold->adgold_mma_fifo_end[channel])
        {
                adgold->adgold_mma_enable[channel] = 0;
        }
}

void adgold_timer_poll(void *p)
{
        adgold_t *adgold = (adgold_t *)p;
        
        while (adgold->adgold_mma_timer_count <= 0)
        {
                adgold->adgold_mma_timer_count += (int)((double)TIMER_USEC * 1.88964);
                if (adgold->adgold_mma_regs[0][8] & 0x01) /*Timer 0*/
                {
                        adgold->adgold_mma.timer0_count--;
                        if (!adgold->adgold_mma.timer0_count)
                        {
                                adgold->adgold_mma.timer0_count = adgold->adgold_mma.timer0_latch;
                                adgold->adgold_mma_status |= 0x10;
                                adgold_update_irq_status(adgold);
                        }
                }
                if (adgold->adgold_mma_regs[0][8] & 0x08) /*Base timer*/
                {
                        adgold->adgold_mma.timerbase_count--;
                        if (!adgold->adgold_mma.timerbase_count)
                        {
                                adgold->adgold_mma.timerbase_count = adgold->adgold_mma.timerbase_latch;
                                if (adgold->adgold_mma_regs[0][8] & 0x02) /*Timer 1*/
                                {
                                        adgold->adgold_mma.timer1_count--;
                                        if (!adgold->adgold_mma.timer1_count)
                                        {
                                                adgold->adgold_mma.timer1_count = adgold->adgold_mma.timer1_latch;
                                                adgold->adgold_mma_status |= 0x20;
                                                adgold_update_irq_status(adgold);
                                        }
                                }
                                if (adgold->adgold_mma_regs[0][8] & 0x04) /*Timer 2*/
                                {
                                        adgold->adgold_mma.timer2_count--;
                                        if (!adgold->adgold_mma.timer2_count)
                                        {
                                                adgold->adgold_mma.timer2_count = adgold->adgold_mma.timer2_latch;
                                                adgold->adgold_mma_status |= 0x40;
                                                adgold_update_irq_status(adgold);
                                        }
                                }
                        }
                }

                if (adgold->adgold_mma_enable[0])
                {
                        adgold->adgold_mma.voice_count[0]--;
                        if (!adgold->adgold_mma.voice_count[0])
                        {
                                adgold->adgold_mma.voice_count[0] = adgold->adgold_mma.voice_latch[0];
                                adgold_mma_poll(adgold, 0);
                        }
                }
                if (adgold->adgold_mma_enable[1])
                {
                        adgold->adgold_mma.voice_count[1]--;
                        if (!adgold->adgold_mma.voice_count[1])
                        {
                                adgold->adgold_mma.voice_count[1] = adgold->adgold_mma.voice_latch[1];
                                adgold_mma_poll(adgold, 1);
                        }
                }
        }
}

static void adgold_get_buffer(int32_t *buffer, int len, void *p)
{
        adgold_t *adgold = (adgold_t *)p;
        int16_t adgold_buffer[len*2];
        
        int c;

        opl3_update2(&adgold->opl);
        adgold_update(adgold);
        
        for (c = 0; c < len * 2; c += 2)
        {
                adgold_buffer[c] = ((adgold->opl.buffer[c] * adgold->fm_vol_l) >> 7) / 2;
                adgold_buffer[c] += ((adgold->mma_buffer[0][c >> 1] * adgold->samp_vol_l) >> 7) / 4;
                adgold_buffer[c+1] = ((adgold->opl.buffer[c+1] * adgold->fm_vol_r) >> 7) / 2;
                adgold_buffer[c+1] += ((adgold->mma_buffer[1][c >> 1] * adgold->samp_vol_r) >> 7) / 4;
        }

        if (adgold->surround_enabled)
                ym7128_apply(&adgold->ym7128, adgold_buffer, len);
        
        switch (adgold->adgold_38x_regs[0x8] & 6)
        {
                case 0:
                for (c = 0; c < len * 2; c++)
                        adgold_buffer[c] = 0;
                break;
                case 2: /*Left channel only*/
                for (c = 0; c < len * 2; c += 2)
                        adgold_buffer[c+1] = adgold_buffer[c];
                break;
                case 4: /*Right channel only*/
                for (c = 0; c < len * 2; c += 2)
                        adgold_buffer[c] = adgold_buffer[c+1];
                break;
                case 6: /*Left and right channels*/
                break;
        }
                
        switch (adgold->adgold_38x_regs[0x8] & 0x18)
        {
                case 0x00: /*Forced mono*/
                for (c = 0; c < len * 2; c += 2)
                        adgold_buffer[c] = adgold_buffer[c+1] = ((int32_t)adgold_buffer[c] + (int32_t)adgold_buffer[c+1]) / 2;
                break;
                case 0x08: /*Linear stereo*/
                break;
                case 0x10: /*Pseudo stereo*/
                /*Filter left channel, leave right channel unchanged*/
                /*Filter cutoff is largely a guess*/
                for (c = 0; c < len * 2; c += 2)
                        adgold_buffer[c] += adgold_pseudo_stereo_iir(adgold_buffer[c]);
                break;
                case 0x18: /*Spatial stereo*/
                /*Quite probably wrong, I only have the diagram in the TDA8425 datasheet
                  and a very vague understanding of how op-amps work to go on*/
                for (c = 0; c < len * 2; c += 2)
                {
                        int16_t l = adgold_buffer[c];
                        int16_t r = adgold_buffer[c+1];
                        
                        adgold_buffer[c]   += (r / 3) + ((l * 2) / 3);
                        adgold_buffer[c+1] += (l / 3) + ((r * 2) / 3);
                }
                break;
        }

        for (c = 0; c < len * 2; c += 2)
        {
                int32_t temp, lowpass, highpass;
                
                /*Output is deliberately halved to avoid clipping*/
                temp = ((int32_t)adgold_buffer[c] * adgold->vol_l) >> 17;
                lowpass = adgold_lowpass_iir(0, temp);
                highpass = adgold_highpass_iir(0, temp);
                if (adgold->bass > 6)
                        temp += (lowpass * bass_attenuation[adgold->bass]) >> 14;
                else if (adgold->bass < 6)
                        temp = highpass + ((temp * bass_cut[adgold->bass]) >> 14);
                if (adgold->treble > 6)
                        temp += (highpass * treble_attenuation[adgold->treble]) >> 14;
                else if (adgold->treble < 6)
                        temp = lowpass + ((temp * treble_cut[adgold->treble]) >> 14);
                if (temp < -32768)
                        temp = -32768;
                if (temp > 32767)
                        temp = 32767;
                buffer[c] += temp;

                temp = ((int32_t)adgold_buffer[c+1] * adgold->vol_r) >> 17;
                lowpass = adgold_lowpass_iir(1, temp);
                highpass = adgold_highpass_iir(1, temp);
                if (adgold->bass > 6)
                        temp += (lowpass * bass_attenuation[adgold->bass]) >> 14;
                else if (adgold->bass < 6)
                        temp = highpass + ((temp * bass_cut[adgold->bass]) >> 14);
                if (adgold->treble > 6)
                        temp += (highpass * treble_attenuation[adgold->treble]) >> 14;
                else if (adgold->treble < 6)
                        temp = lowpass + ((temp * treble_cut[adgold->treble]) >> 14);
                if (temp < -32768)
                        temp = -32768;
                if (temp > 32767)
                        temp = 32767;
                buffer[c+1] += temp;
        }

        adgold->opl.pos = 0;
        adgold->pos = 0;
}


void *adgold_init(device_t *info)
{
        FILE *f;
        int c;
        double out;
        adgold_t *adgold = malloc(sizeof(adgold_t));
        memset(adgold, 0, sizeof(adgold_t));

        adgold->surround_enabled = device_get_config_int("surround");
        
        opl3_init(&adgold->opl);
        if (adgold->surround_enabled)
                ym7128_init(&adgold->ym7128);

        out = 65536.0; /*Main volume control ranges from +6 dB to -64 dB in 2 dB steps, then remaining settings are -80 dB (effectively 0)*/
        for (c = 0x3f; c >= 0x1c; c--)
        {
                attenuation[c] = (int)out;
                out /= 1.25963; /*2 dB steps*/
        }
        for (; c >= 0; c--)
                attenuation[c] = 0;

        f = nvr_fopen(L"adgold.bin", L"rb");
        if (f)
        {
                fread(adgold->adgold_eeprom, 0x18, 1, f);
                fclose(f);
        }

        adgold->adgold_status = 0xf;
        adgold->adgold_38x_addr = 0;
        adgold->adgold_eeprom[0x13] = 3 | (1 << 4);     /*IRQ 7, DMA 1*/
        adgold->adgold_eeprom[0x14] = 3 << 4;           /*DMA 3*/
        adgold->adgold_eeprom[0x15] = 0x388 / 8;        /*Present at 388-38f*/
        memcpy(adgold->adgold_38x_regs, adgold->adgold_eeprom, 0x19);
        adgold->vol_l = attenuation[adgold->adgold_eeprom[0x04] & 0x3f];
        adgold->vol_r = attenuation[adgold->adgold_eeprom[0x05] & 0x3f];
        adgold->bass = adgold->adgold_eeprom[0x06] & 0xf;
        adgold->treble = adgold->adgold_eeprom[0x07] & 0xf;
        adgold->fm_vol_l = (int)(int8_t)(adgold->adgold_eeprom[0x09] - 128);
        adgold->fm_vol_r = (int)(int8_t)(adgold->adgold_eeprom[0x0a] - 128);
        adgold->samp_vol_l = (int)(int8_t)(adgold->adgold_eeprom[0x0b] - 128);
        adgold->samp_vol_r = (int)(int8_t)(adgold->adgold_eeprom[0x0c] - 128);

        adgold->adgold_mma_enable[0] = 0;
        adgold->adgold_mma_fifo_start[0] = adgold->adgold_mma_fifo_end[0] = 0;
        
        /*388/389 are handled by adlib_init*/
        io_sethandler(0x0388, 0x0008, adgold_read, NULL, NULL, adgold_write, NULL, NULL, adgold);
        
        timer_add(adgold_timer_poll, &adgold->adgold_mma_timer_count, TIMER_ALWAYS_ENABLED, adgold);

        sound_add_handler(adgold_get_buffer, adgold);
        
        return adgold;
}

void adgold_close(void *p)
{
        FILE *f;
        adgold_t *adgold = (adgold_t *)p;
        
        f = nvr_fopen(L"adgold.bin", L"wb");
        if (f)
        {
                fwrite(adgold->adgold_eeprom, 0x18, 1, f);
                fclose(f);
        }

        free(adgold);
}

static device_config_t adgold_config[] =
{
        {
                "surround", "Surround module", CONFIG_BINARY, "", 1
        },
        {
                "", "", -1
        }
};

device_t adgold_device =
{
        "AdLib Gold",
        0, 0,
        adgold_init,
        adgold_close,
	NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        adgold_config
};
