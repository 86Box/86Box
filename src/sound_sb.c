/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "pci.h"
#include "sound_emu8k.h"
#include "sound_mpu401_uart.h"
#include "sound_opl.h"
#include "sound_sb.h"
#include "sound_sb_dsp.h"

#include "filters.h"

typedef struct sb_mixer_t
{
        int master_l, master_r;
        int voice_l,  voice_r;
        int fm_l,     fm_r;
        int cd_l,     cd_r;
        int bass_l,   bass_r;
        int treble_l, treble_r;
        int filter;

        int index;
        uint8_t regs[256];
} sb_mixer_t;

typedef struct sb_t
{
        opl_t           opl;
        sb_dsp_t        dsp;
        sb_mixer_t      mixer;
        mpu401_uart_t   mpu;
        emu8k_t         emu8k;

        int pos;
} sb_t;

static int sb_att[]=
{
        310,368,437,520,618,735,873,1038,1234,1467,1743,2072,2463,2927,3479,
        4134,4914,5840,6941,8250,9805,11653,13850,16461,19564,23252,27635,32845,
        39036,46395,55140,65535
};

static void sb_get_buffer_opl2(int32_t *buffer, int len, void *p)
{
        sb_t *sb = (sb_t *)p;
        sb_mixer_t *mixer = &sb->mixer;
                
        int c;

        opl2_update2(&sb->opl);
        sb_dsp_update(&sb->dsp);
        for (c = 0; c < len * 2; c += 2)
        {
                int32_t out_l, out_r;
                
                out_l = ((sb->opl.buffer[c]     * mixer->fm_l) >> 16);
                out_r = ((sb->opl.buffer[c + 1] * mixer->fm_r) >> 16);

                if (sb->mixer.filter)
                {
                        out_l += (int)(((sb_iir(0, (float)sb->dsp.buffer[c])     / 1.3) * mixer->voice_l) / 3) >> 16;
                        out_r += (int)(((sb_iir(1, (float)sb->dsp.buffer[c + 1]) / 1.3) * mixer->voice_r) / 3) >> 16;
                }
                else
                {
                        out_l += ((int32_t)(sb->dsp.buffer[c]     * mixer->voice_l) / 3) >> 16;
                        out_r += ((int32_t)(sb->dsp.buffer[c + 1] * mixer->voice_r) / 3) >> 16;
                }
                
                out_l = (out_l * mixer->master_l) >> 16;
                out_r = (out_r * mixer->master_r) >> 16;

                if (mixer->bass_l != 8 || mixer->bass_r != 8 || mixer->treble_l != 8 || mixer->treble_r != 8)
                {
                        if (mixer->bass_l>8)   out_l = (out_l + (((int16_t)     low_iir(0, (float)out_l) * (mixer->bass_l   - 8)) >> 1)) * ((15 - mixer->bass_l)   + 16) >> 5;
                        if (mixer->bass_r>8)   out_r = (out_r + (((int16_t)     low_iir(1, (float)out_r) * (mixer->bass_r   - 8)) >> 1)) * ((15 - mixer->bass_r)   + 16) >> 5;
                        if (mixer->treble_l>8) out_l = (out_l + (((int16_t)    high_iir(0, (float)out_l) * (mixer->treble_l - 8)) >> 1)) * ((15 - mixer->treble_l) + 16) >> 5;
                        if (mixer->treble_r>8) out_r = (out_r + (((int16_t)    high_iir(1, (float)out_r) * (mixer->treble_r - 8)) >> 1)) * ((15 - mixer->treble_r) + 16) >> 5;
                        if (mixer->bass_l<8)   out_l = (out_l + (((int16_t) low_cut_iir(0, (float)out_l) * (8 - mixer->bass_l))   >> 1)) * (mixer->bass_l   + 16)        >> 5;
                        if (mixer->bass_r<8)   out_r = (out_r + (((int16_t) low_cut_iir(1, (float)out_r) * (8 - mixer->bass_r))   >> 1)) * (mixer->bass_r   + 16)        >> 5;
                        if (mixer->treble_l<8) out_l = (out_l + (((int16_t)high_cut_iir(0, (float)out_l) * (8 - mixer->treble_l)) >> 1)) * (mixer->treble_l + 16)        >> 5;
                        if (mixer->treble_r<8) out_r = (out_r + (((int16_t)high_cut_iir(1, (float)out_r) * (8 - mixer->treble_r)) >> 1)) * (mixer->treble_r + 16)        >> 5;
                }
                        
                buffer[c]     += out_l;
                buffer[c + 1] += out_r;
        }

        sb->pos = 0;
        sb->opl.pos = 0;
        sb->dsp.pos = 0;
}

static void sb_get_buffer_opl3(int32_t *buffer, int len, void *p)
{
        sb_t *sb = (sb_t *)p;
        sb_mixer_t *mixer = &sb->mixer;
                
        int c;

        opl3_update2(&sb->opl);
        sb_dsp_update(&sb->dsp);
        for (c = 0; c < len * 2; c += 2)
        {
                int c_emu8k = (((c/2) * 44100) / 48000)*2;
                int32_t out_l, out_r;
                
                out_l = ((sb->opl.buffer[c]     * mixer->fm_l) >> 16);
                out_r = ((sb->opl.buffer[c + 1] * mixer->fm_r) >> 16);

                if (sb->mixer.filter)
                {
                        out_l += (int)(((sb_iir(0, (float)sb->dsp.buffer[c])     / 1.3) * mixer->voice_l) / 3) >> 16;
                        out_r += (int)(((sb_iir(1, (float)sb->dsp.buffer[c + 1]) / 1.3) * mixer->voice_r) / 3) >> 16;
                }
                else
                {
                        out_l += ((int32_t)(sb->dsp.buffer[c]     * mixer->voice_l) / 3) >> 16;
                        out_r += ((int32_t)(sb->dsp.buffer[c + 1] * mixer->voice_r) / 3) >> 16;
                }
                
                out_l = (out_l * mixer->master_l) >> 16;
                out_r = (out_r * mixer->master_r) >> 16;

                if (mixer->bass_l != 8 || mixer->bass_r != 8 || mixer->treble_l != 8 || mixer->treble_r != 8)
                {
                        if (mixer->bass_l>8)   out_l = (out_l + (((int16_t)     low_iir(0, (float)out_l) * (mixer->bass_l   - 8)) >> 1)) * ((15 - mixer->bass_l)   + 16) >> 5;
                        if (mixer->bass_r>8)   out_r = (out_r + (((int16_t)     low_iir(1, (float)out_r) * (mixer->bass_r   - 8)) >> 1)) * ((15 - mixer->bass_r)   + 16) >> 5;
                        if (mixer->treble_l>8) out_l = (out_l + (((int16_t)    high_iir(0, (float)out_l) * (mixer->treble_l - 8)) >> 1)) * ((15 - mixer->treble_l) + 16) >> 5;
                        if (mixer->treble_r>8) out_r = (out_r + (((int16_t)    high_iir(1, (float)out_r) * (mixer->treble_r - 8)) >> 1)) * ((15 - mixer->treble_r) + 16) >> 5;
                        if (mixer->bass_l<8)   out_l = (out_l + (((int16_t) low_cut_iir(0, (float)out_l) * (8 - mixer->bass_l))   >> 1)) * (mixer->bass_l   + 16)        >> 5;
                        if (mixer->bass_r<8)   out_r = (out_r + (((int16_t) low_cut_iir(1, (float)out_r) * (8 - mixer->bass_r))   >> 1)) * (mixer->bass_r   + 16)        >> 5;
                        if (mixer->treble_l<8) out_l = (out_l + (((int16_t)high_cut_iir(0, (float)out_l) * (8 - mixer->treble_l)) >> 1)) * (mixer->treble_l + 16)        >> 5;
                        if (mixer->treble_r<8) out_r = (out_r + (((int16_t)high_cut_iir(1, (float)out_r) * (8 - mixer->treble_r)) >> 1)) * (mixer->treble_r + 16)        >> 5;
                }
                        
                buffer[c]     += out_l;
                buffer[c + 1] += out_r;
        }

        sb->pos = 0;
        sb->opl.pos = 0;
        sb->dsp.pos = 0;
        sb->emu8k.pos = 0;
}

static void sb_get_buffer_emu8k(int32_t *buffer, int len, void *p)
{
        sb_t *sb = (sb_t *)p;
        sb_mixer_t *mixer = &sb->mixer;
                
        int c;

        opl3_update2(&sb->opl);
        sb_dsp_update(&sb->dsp);
        emu8k_update(&sb->emu8k);
        for (c = 0; c < len * 2; c += 2)
        {
                int c_emu8k = (((c/2) * 44100) / 48000)*2;
                int32_t out_l, out_r;
                
                out_l = (((int32_t)sb->opl.buffer[c]     * (int32_t)mixer->fm_l) >> 16);
                out_r = (((int32_t)sb->opl.buffer[c + 1] * (int32_t)mixer->fm_r) >> 16);

                out_l += ((sb->emu8k.buffer[c_emu8k]     * mixer->fm_l) >> 16);
                out_r += ((sb->emu8k.buffer[c_emu8k + 1] * mixer->fm_l) >> 16);

                if (sb->mixer.filter)
                {
                        out_l += (int)(((sb_iir(0, (float)sb->dsp.buffer[c])     / 1.3) * mixer->voice_l) / 3) >> 16;
                        out_r += (int)(((sb_iir(1, (float)sb->dsp.buffer[c + 1]) / 1.3) * mixer->voice_r) / 3) >> 16;
                }
                else
                {
                        out_l += ((int32_t)(sb->dsp.buffer[c]     * mixer->voice_l) / 3) >> 16;
                        out_r += ((int32_t)(sb->dsp.buffer[c + 1] * mixer->voice_r) / 3) >> 16;
                }
                
                out_l = (out_l * mixer->master_l) >> 16;
                out_r = (out_r * mixer->master_r) >> 16;

                if (mixer->bass_l != 8 || mixer->bass_r != 8 || mixer->treble_l != 8 || mixer->treble_r != 8)
                {
                        if (mixer->bass_l>8)   out_l = (out_l + (((int16_t)     low_iir(0, (float)out_l) * (mixer->bass_l   - 8)) >> 1)) * ((15 - mixer->bass_l)   + 16) >> 5;
                        if (mixer->bass_r>8)   out_r = (out_r + (((int16_t)     low_iir(1, (float)out_r) * (mixer->bass_r   - 8)) >> 1)) * ((15 - mixer->bass_r)   + 16) >> 5;
                        if (mixer->treble_l>8) out_l = (out_l + (((int16_t)    high_iir(0, (float)out_l) * (mixer->treble_l - 8)) >> 1)) * ((15 - mixer->treble_l) + 16) >> 5;
                        if (mixer->treble_r>8) out_r = (out_r + (((int16_t)    high_iir(1, (float)out_r) * (mixer->treble_r - 8)) >> 1)) * ((15 - mixer->treble_r) + 16) >> 5;
                        if (mixer->bass_l<8)   out_l = (out_l + (((int16_t) low_cut_iir(0, (float)out_l) * (8 - mixer->bass_l))   >> 1)) * (mixer->bass_l   + 16)        >> 5;
                        if (mixer->bass_r<8)   out_r = (out_r + (((int16_t) low_cut_iir(1, (float)out_r) * (8 - mixer->bass_r))   >> 1)) * (mixer->bass_r   + 16)        >> 5;
                        if (mixer->treble_l<8) out_l = (out_l + (((int16_t)high_cut_iir(0, (float)out_l) * (8 - mixer->treble_l)) >> 1)) * (mixer->treble_l + 16)        >> 5;
                        if (mixer->treble_r<8) out_r = (out_r + (((int16_t)high_cut_iir(1, (float)out_r) * (8 - mixer->treble_r)) >> 1)) * (mixer->treble_r + 16)        >> 5;
                }
                        
                buffer[c]     += out_l;
                buffer[c + 1] += out_r;
        }

        sb->pos = 0;
        sb->opl.pos = 0;
        sb->dsp.pos = 0;
        sb->emu8k.pos = 0;
}

void sb_pro_mixer_write(uint16_t addr, uint8_t val, void *p)
{
        sb_t *sb = (sb_t *)p;
        sb_mixer_t *mixer = &sb->mixer;
        
        if (!(addr & 1))
                mixer->index = val & 0xff;
        else
        {
                mixer->regs[mixer->index] = val;
  
                mixer->master_l = sb_att[(mixer->regs[0x22] >> 4)  | 0x11];
                mixer->master_r = sb_att[(mixer->regs[0x22] & 0xf) | 0x11];
                mixer->voice_l  = sb_att[(mixer->regs[0x04] >> 4)  | 0x11];
                mixer->voice_r  = sb_att[(mixer->regs[0x04] & 0xf) | 0x11];
                mixer->fm_l     = sb_att[(mixer->regs[0x26] >> 4)  | 0x11];
                mixer->fm_r     = sb_att[(mixer->regs[0x26] & 0xf) | 0x11];
                mixer->cd_l     = sb_att[(mixer->regs[0x28] >> 4)  | 0x11];
                mixer->cd_r     = sb_att[(mixer->regs[0x28] & 0xf) | 0x11];
                mixer->filter   = !(mixer->regs[0xe] & 0x20);
                mixer->bass_l   = mixer->bass_r   = 8;
                mixer->treble_l = mixer->treble_r = 8;
                sound_set_cd_volume(((uint32_t)mixer->master_l * (uint32_t)mixer->cd_l) / 65535,
                                    ((uint32_t)mixer->master_r * (uint32_t)mixer->cd_r) / 65535);
//                pclog("%02X %02X %02X\n", mixer->regs[0x04], mixer->regs[0x22], mixer->regs[0x26]);
//                pclog("Mixer - %04X %04X %04X %04X %04X %04X\n", mixer->master_l, mixer->master_r, mixer->voice_l, mixer->voice_r, mixer->fm_l, mixer->fm_r);
                if (mixer->index == 0xe)
                        sb_dsp_set_stereo(&sb->dsp, val & 2);
        }
}

uint8_t sb_pro_mixer_read(uint16_t addr, void *p)
{
        sb_t *sb = (sb_t *)p;
        sb_mixer_t *mixer = &sb->mixer;

        if (!(addr & 1))
                return mixer->index;

        return mixer->regs[mixer->index];                
}

void sb_16_mixer_write(uint16_t addr, uint8_t val, void *p)
{
        sb_t *sb = (sb_t *)p;
        sb_mixer_t *mixer = &sb->mixer;
        
        if (!(addr & 1))
                mixer->index = val;
        else
        {
                mixer->regs[mixer->index] = val;
                switch (mixer->index)
                {
                        case 0x22:
                        mixer->regs[0x30] = ((mixer->regs[0x22] >> 4)  | 0x11) << 3;
                        mixer->regs[0x31] = ((mixer->regs[0x22] & 0xf) | 0x11) << 3;
                        break;
                        case 0x04:
                        mixer->regs[0x32] = ((mixer->regs[0x04] >> 4)  | 0x11) << 3;
                        mixer->regs[0x33] = ((mixer->regs[0x04] & 0xf) | 0x11) << 3;
                        break;
                        case 0x26:
                        mixer->regs[0x34] = ((mixer->regs[0x26] >> 4)  | 0x11) << 3;
                        mixer->regs[0x35] = ((mixer->regs[0x26] & 0xf) | 0x11) << 3;
                        break;
                        case 0x28:
                        mixer->regs[0x36] = ((mixer->regs[0x28] >> 4)  | 0x11) << 3;
                        mixer->regs[0x37] = ((mixer->regs[0x28] & 0xf) | 0x11) << 3;
                        break;
                        case 0x80:
                        if (val & 1) sb->dsp.sb_irqnum = 2;
                        if (val & 2) sb->dsp.sb_irqnum = 5;
                        if (val & 4) sb->dsp.sb_irqnum = 7;
                        if (val & 8) sb->dsp.sb_irqnum = 10;
                        break;
                }
                mixer->master_l = sb_att[mixer->regs[0x30] >> 3];
                mixer->master_r = sb_att[mixer->regs[0x31] >> 3];
                mixer->voice_l  = sb_att[mixer->regs[0x32] >> 3];
                mixer->voice_r  = sb_att[mixer->regs[0x33] >> 3];
                mixer->fm_l     = sb_att[mixer->regs[0x34] >> 3];
                mixer->fm_r     = sb_att[mixer->regs[0x35] >> 3];
                mixer->cd_l     = sb_att[mixer->regs[0x36] >> 3];
                mixer->cd_r     = sb_att[mixer->regs[0x37] >> 3];
                mixer->bass_l   = mixer->regs[0x46] >> 4;
                mixer->bass_r   = mixer->regs[0x47] >> 4;
                mixer->treble_l = mixer->regs[0x44] >> 4;
                mixer->treble_r = mixer->regs[0x45] >> 4;
                mixer->filter = 0;
                sound_set_cd_volume(((uint32_t)mixer->master_l * (uint32_t)mixer->cd_l) / 65535,
                                    ((uint32_t)mixer->master_r * (uint32_t)mixer->cd_r) / 65535);
//                pclog("%02X %02X %02X %02X %02X %02X\n", mixer->regs[0x30], mixer->regs[0x31], mixer->regs[0x32], mixer->regs[0x33], mixer->regs[0x34], mixer->regs[0x35]);
//                pclog("Mixer - %04X %04X %04X %04X %04X %04X\n", mixer->master_l, mixer->master_r, mixer->voice_l, mixer->voice_r, mixer->fm_l, mixer->fm_r);
        }
}

uint8_t sb_16_mixer_read(uint16_t addr, void *p)
{
        sb_t *sb = (sb_t *)p;
        sb_mixer_t *mixer = &sb->mixer;

        if (!(addr & 1))
                return mixer->index;

        switch (mixer->index)
        {
                case 0x80:
                switch (sb->dsp.sb_irqnum)
                {
                        case 2: return 1; /*IRQ 7*/
                        case 5: return 2; /*IRQ 7*/
                        case 7: return 4; /*IRQ 7*/
                        case 10: return 8; /*IRQ 7*/
                }
                break;
                case 0x81:
                return 0x22; /*DMA 1 and 5*/
                case 0x82:
                return ((sb->dsp.sb_irq8) ? 1 : 0) | ((sb->dsp.sb_irq16) ? 2 : 0);
        }
        return mixer->regs[mixer->index];                
}

void sb_mixer_init(sb_mixer_t *mixer)
{
        mixer->master_l = mixer->master_r = 65535;
        mixer->voice_l  = mixer->voice_r  = 65535;
        mixer->fm_l     = mixer->fm_r     = 65535;
        mixer->bass_l   = mixer->bass_r   = 8;
        mixer->treble_l = mixer->treble_r = 8;
        mixer->filter = 1;
}
        
void *sb_1_init()
{
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_int("addr");        
        memset(sb, 0, sizeof(sb_t));
        
        opl2_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SB1);
        sb_dsp_setaddr(&sb->dsp, addr);
        sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
        sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
        sb_mixer_init(&sb->mixer);
        io_sethandler(addr+8, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
        io_sethandler(0x0388, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
        sound_add_handler(sb_get_buffer_opl2, sb);
        return sb;
}
void *sb_15_init()
{
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_int("addr");
        memset(sb, 0, sizeof(sb_t));

        opl2_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SB15);
        sb_dsp_setaddr(&sb->dsp, addr);
        sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
        sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
        sb_mixer_init(&sb->mixer);
        io_sethandler(addr+8, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
        io_sethandler(0x0388, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
        sound_add_handler(sb_get_buffer_opl2, sb);
        return sb;
}
void *sb_2_init()
{
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_int("addr");
        memset(sb, 0, sizeof(sb_t));

        opl2_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SB2);
        sb_dsp_setaddr(&sb->dsp, addr);
        sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
        sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
        sb_mixer_init(&sb->mixer);
        io_sethandler(addr+8, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
        io_sethandler(0x0388, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
        sound_add_handler(sb_get_buffer_opl2, sb);
        return sb;
}

void *sb_pro_v1_init()
{
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_int("addr");
        memset(sb, 0, sizeof(sb_t));

        opl2_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SBPRO);
        sb_dsp_setaddr(&sb->dsp, addr);
        sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
        sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
        sb_mixer_init(&sb->mixer);
        io_sethandler(addr+0, 0x0002, opl2_l_read, NULL, NULL, opl2_l_write, NULL, NULL, &sb->opl);
        io_sethandler(addr+2, 0x0002, opl2_r_read, NULL, NULL, opl2_r_write, NULL, NULL, &sb->opl);
        io_sethandler(addr+8, 0x0002, opl2_read,   NULL, NULL, opl2_write,   NULL, NULL, &sb->opl);
        io_sethandler(0x0388, 0x0002, opl2_read,   NULL, NULL, opl2_write,   NULL, NULL, &sb->opl);
        io_sethandler(addr+4, 0x0002, sb_pro_mixer_read, NULL, NULL, sb_pro_mixer_write, NULL, NULL, sb);
        sound_add_handler(sb_get_buffer_opl2, sb);

        sb->mixer.regs[0x22] = 0xff;
        sb->mixer.regs[0x04] = 0xff;
        sb->mixer.regs[0x26] = 0xff;
        sb->mixer.regs[0xe]  = 0;

        return sb;
}

void *sb_pro_v2_init()
{
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_int("addr");
        memset(sb, 0, sizeof(sb_t));

        opl3_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SBPRO2);
        sb_dsp_setaddr(&sb->dsp, addr);
        sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
        sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
        sb_mixer_init(&sb->mixer);
        io_sethandler(addr+0, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(addr+8, 0x0002, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(0x0388, 0x0002, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(addr+4, 0x0002, sb_pro_mixer_read, NULL, NULL, sb_pro_mixer_write, NULL, NULL, sb);
        sound_add_handler(sb_get_buffer_opl3, sb);

        sb->mixer.regs[0x22] = 0xff;
        sb->mixer.regs[0x04] = 0xff;
        sb->mixer.regs[0x26] = 0xff;
        sb->mixer.regs[0xe]  = 0;

        return sb;
}

void *sb_16_init()
{
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_int("addr");        
        memset(sb, 0, sizeof(sb_t));

        opl3_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SB16);
        sb_dsp_setaddr(&sb->dsp, addr);
        sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
        sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
        sb_mixer_init(&sb->mixer);
        io_sethandler(0x0220, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(0x0228, 0x0002, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(0x0388, 0x0002, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(0x0224, 0x0002, sb_16_mixer_read, NULL, NULL, sb_16_mixer_write, NULL, NULL, sb);
        sound_add_handler(sb_get_buffer_opl3, sb);
        mpu401_uart_init(&sb->mpu, 0x330);

        sb->mixer.regs[0x30] = 31 << 3;
        sb->mixer.regs[0x31] = 31 << 3;
        sb->mixer.regs[0x32] = 31 << 3;
        sb->mixer.regs[0x33] = 31 << 3;
        sb->mixer.regs[0x34] = 31 << 3;
        sb->mixer.regs[0x35] = 31 << 3;
        sb->mixer.regs[0x44] =  8 << 4;
        sb->mixer.regs[0x45] =  8 << 4;
        sb->mixer.regs[0x46] =  8 << 4;
        sb->mixer.regs[0x47] =  8 << 4;
        sb->mixer.regs[0x22] = (sb->mixer.regs[0x30] & 0xf0) | (sb->mixer.regs[0x31] >> 4);
        sb->mixer.regs[0x04] = (sb->mixer.regs[0x32] & 0xf0) | (sb->mixer.regs[0x33] >> 4);
        sb->mixer.regs[0x26] = (sb->mixer.regs[0x34] & 0xf0) | (sb->mixer.regs[0x35] >> 4);

        return sb;
}

int sb_awe32_available()
{
        return rom_present("roms/awe32.raw");
}

void *sb_awe32_init()
{
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_int("addr");        
        int onboard_ram = device_get_config_int("onboard_ram");
        memset(sb, 0, sizeof(sb_t));

        opl3_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SB16 + 1);
        sb_dsp_setaddr(&sb->dsp, addr);
        sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
        sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
        sb_mixer_init(&sb->mixer);
        io_sethandler(0x0220, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(0x0228, 0x0002, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(0x0388, 0x0002, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(0x0224, 0x0002, sb_16_mixer_read, NULL, NULL, sb_16_mixer_write, NULL, NULL, sb);
        sound_add_handler(sb_get_buffer_emu8k, sb);
        mpu401_uart_init(&sb->mpu, 0x330);       
        emu8k_init(&sb->emu8k, onboard_ram);

        sb->mixer.regs[0x30] = 31 << 3;
        sb->mixer.regs[0x31] = 31 << 3;
        sb->mixer.regs[0x32] = 31 << 3;
        sb->mixer.regs[0x33] = 31 << 3;
        sb->mixer.regs[0x34] = 31 << 3;
        sb->mixer.regs[0x35] = 31 << 3;
        sb->mixer.regs[0x44] =  8 << 4;
        sb->mixer.regs[0x45] =  8 << 4;
        sb->mixer.regs[0x46] =  8 << 4;
        sb->mixer.regs[0x47] =  8 << 4;
        sb->mixer.regs[0x22] = (sb->mixer.regs[0x30] & 0xf0) | (sb->mixer.regs[0x31] >> 4);
        sb->mixer.regs[0x04] = (sb->mixer.regs[0x32] & 0xf0) | (sb->mixer.regs[0x33] >> 4);
        sb->mixer.regs[0x26] = (sb->mixer.regs[0x34] & 0xf0) | (sb->mixer.regs[0x35] >> 4);
        
        return sb;
}

typedef union
{
	uint16_t word;
	uint8_t byte_regs[2];
} word_t;

typedef union
{
	uint32_t addr;
	uint8_t addr_regs[4];
} bar_t;

uint8_t sb_awe64_pci_regs[256];

word_t sb_awe64_pci_words[6];

bar_t sb_awe64_pci_bar[6];

uint8_t sb_awe64_pci_read(int func, int addr, void *p)
{
	switch (addr)
	{
		case 0: case 1:
			return sb_awe64_pci_words[0].byte_regs[addr & 1];
		case 2: case 3:
			return sb_awe64_pci_words[1].byte_regs[addr & 1];
		case 8: case 9:
			return sb_awe64_pci_words[2].byte_regs[addr & 1];
		case 0xA: case 0xB:
			return sb_awe64_pci_words[3].byte_regs[addr & 1];
		case 0x10: case 0x11: case 0x12: case 0x13:
		case 0x14: case 0x15: case 0x16: case 0x17:
		case 0x18: case 0x19: case 0x1A: case 0x1B:
		case 0x1C: case 0x1D: case 0x1E: case 0x1F:
		case 0x20: case 0x21: case 0x22: case 0x23:
		case 0x24: case 0x25: case 0x26: case 0x27:
			return sb_awe64_pci_bar[(addr - 0x10) >> 2].addr_regs[addr & 3];
		case 0x2C: case 0x2D:
			return sb_awe64_pci_words[4].byte_regs[addr & 1];
		case 0x2E: case 0x2F:
			return sb_awe64_pci_words[5].byte_regs[addr & 1];
		default:
			return sb_awe64_pci_regs[addr];
	}
	return 0;
}

uint32_t sb_base_addr, old_base_addr;

void sb_awe64_io_remove(uint32_t base_addr, sb_t *sb)
{
        io_removehandler(base_addr, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_removehandler(base_addr + 4, 0x0002, sb_16_mixer_read, NULL, NULL, sb_16_mixer_write, NULL, NULL, sb);
        io_removehandler(base_addr + 8, 0x0002, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        mpu401_uart_remove(&sb->mpu, base_addr + 8);
}

void sb_awe64_io_set(uint32_t base_addr, sb_t *sb)
{
	sb_dsp_setaddr(&sb->dsp, base_addr);

        io_sethandler(base_addr, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(base_addr + 4, 0x0002, sb_16_mixer_read, NULL, NULL, sb_16_mixer_write, NULL, NULL, sb);
        io_sethandler(base_addr + 8, 0x0002, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        mpu401_uart_set(&sb->mpu, base_addr + 8);

	old_base_addr = base_addr;
}

void sb_awe64_pci_write(int func, int addr, uint8_t val, void *p)
{
        sb_t *sb = (sb_t *) p;

	switch (addr)
	{
		case 4:
		        if (val & PCI_COMMAND_IO)
			{
				sb_awe64_io_remove(sb_base_addr, sb);
				sb_awe64_io_set(sb_base_addr, sb);
			}
                	else
			{
				sb_awe64_io_remove(sb_base_addr, sb);
			}
			sb_awe64_pci_regs[addr] = val;
			return;
		case 0x10:
			val &= 0xfc;
			val |= 1;
		case 0x11: case 0x12: case 0x13:
		/* case 0x14: case 0x15: case 0x16: case 0x17:
		case 0x18: case 0x19: case 0x1A: case 0x1B:
		case 0x1C: case 0x1D: case 0x1E: case 0x1F:
		case 0x20: case 0x21: case 0x22: case 0x23:
		case 0x24: case 0x25: case 0x26: case 0x27: */
			sb_awe64_pci_bar[(addr - 0x10) >> 2].addr_regs[addr & 3] = val;
			sb_awe64_pci_bar[(addr - 0x10) >> 2].addr &= 0xffc0;
			sb_awe64_pci_bar[(addr - 0x10) >> 2].addr |= 1;
			if ((addr & 0x14) == 0x10)
			{
				sb_awe64_io_remove(sb_base_addr, sb);
				sb_base_addr = sb_awe64_pci_bar[0].addr & 0xffc0;
		                pclog("SB AWE64 PCI: New I/O base %i is %04X\n" , ((addr - 0x10) >> 2), sb_base_addr);
			}
			return;
                case 0x3C:
	                sb_awe64_pci_regs[addr] = val;
        	        if (val != 0xFF)
                	{
				pclog("SB AWE64 PCI IRQ now: %i\n", val);
				sb_dsp_setirq(&sb->dsp, val);
	                }
        	        return;
		default:
			return;
	}
	return 0;
}

void *sb_awe64pci_init()
{
        sb_t *sb = malloc(sizeof(sb_t));
	uint16_t i = 0;
        int onboard_ram = device_get_config_int("onboard_ram");
        memset(sb, 0, sizeof(sb_t));

        opl3_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SB16 + 1);
        sb_dsp_setirq(&sb->dsp, 5);
        sb_dsp_setdma8(&sb->dsp, 1);
        mpu401_uart_init(&sb->mpu, 0x228);
        mpu401_uart_remove(&sb->mpu, 0x228);
	sb_awe64_io_set(0x220, sb);
        sb_mixer_init(&sb->mixer);
        io_sethandler(0x0388, 0x0002, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        sound_add_handler(sb_get_buffer_emu8k, sb);
        emu8k_init(&sb->emu8k, onboard_ram);

        sb->mixer.regs[0x30] = 31 << 3;
        sb->mixer.regs[0x31] = 31 << 3;
        sb->mixer.regs[0x32] = 31 << 3;
        sb->mixer.regs[0x33] = 31 << 3;
        sb->mixer.regs[0x34] = 31 << 3;
        sb->mixer.regs[0x35] = 31 << 3;
        sb->mixer.regs[0x44] =  8 << 4;
        sb->mixer.regs[0x45] =  8 << 4;
        sb->mixer.regs[0x46] =  8 << 4;
        sb->mixer.regs[0x47] =  8 << 4;
        sb->mixer.regs[0x22] = (sb->mixer.regs[0x30] & 0xf0) | (sb->mixer.regs[0x31] >> 4);
        sb->mixer.regs[0x04] = (sb->mixer.regs[0x32] & 0xf0) | (sb->mixer.regs[0x33] >> 4);
        sb->mixer.regs[0x26] = (sb->mixer.regs[0x34] & 0xf0) | (sb->mixer.regs[0x35] >> 4);

        pci_add(sb_awe64_pci_read, sb_awe64_pci_write, sb);

	sb_awe64_pci_words[0].word = 0x1102;	/* Creative Labs */
	sb_awe64_pci_words[1].word = 0x0003;	/* CT-8800-SAT chip (EMU8008) */
	sb_awe64_pci_words[3].word = 0x0401;	/* Multimedia device, audio device */
	sb_awe64_pci_words[4].word = 0x1102;	/* Creative Labs */
	sb_awe64_pci_words[5].word = device_get_config_int("revision");		/* Revision */
	sb_awe64_pci_bar[0].addr = 1;
	sb_awe64_pci_regs[0x3C] = 5;
        sb_awe64_pci_regs[0x3D] = 1;
        sb_awe64_pci_regs[0x3E] = 0x0c;
        sb_awe64_pci_regs[0x3F] = 0x80;
        
        return sb;
}

void sb_close(void *p)
{
        sb_t *sb = (sb_t *)p;
        
        free(sb);
}

void sb_awe32_close(void *p)
{
        sb_t *sb = (sb_t *)p;
        
        emu8k_close(&sb->emu8k);

        free(sb);
}

void sb_speed_changed(void *p)
{
        sb_t *sb = (sb_t *)p;
        
        sb_dsp_speed_changed(&sb->dsp);
}

void sb_add_status_info(char *s, int max_len, void *p)
{
        sb_t *sb = (sb_t *)p;
        
        sb_dsp_add_status_info(s, max_len, &sb->dsp);
}

static device_config_t sb_config[] =
{
        {
                .name = "addr",
                .description = "Address",
                .type = CONFIG_BINARY,
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "0x220",
                                .value = 0x220
                        },
                        {
                                .description = "0x240",
                                .value = 0x240
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 0x220
        },
        {
                .name = "irq",
                .description = "IRQ",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "IRQ 2",
                                .value = 2
                        },
                        {
                                .description = "IRQ 3",
                                .value = 3
                        },
                        {
                                .description = "IRQ 5",
                                .value = 5
                        },
                        {
                                .description = "IRQ 7",
                                .value = 7
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 5
        },
        {
                .name = "dma",
                .description = "DMA",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "DMA 1",
                                .value = 1
                        },
                        {
                                .description = "DMA 3",
                                .value = 3
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 1
        },
        {
                .type = -1
        }
};

static device_config_t sb_pro_config[] =
{
        {
                .name = "addr",
                .description = "Address",
                .type = CONFIG_BINARY,
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "0x220",
                                .value = 0x220
                        },
                        {
                                .description = "0x240",
                                .value = 0x240
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 0x220
        },
        {
                .name = "irq",
                .description = "IRQ",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "IRQ 2",
                                .value = 2
                        },
                        {
                                .description = "IRQ 5",
                                .value = 5
                        },
                        {
                                .description = "IRQ 7",
                                .value = 7
                        },
                        {
                                .description = "IRQ 10",
                                .value = 10
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 5
        },
        {
                .name = "dma",
                .description = "DMA",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "DMA 1",
                                .value = 1
                        },
                        {
                                .description = "DMA 3",
                                .value = 3
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 1
        },
        {
                .type = -1
        }
};

static device_config_t sb_16_config[] =
{
        {
                .name = "addr",
                .description = "Address",
                .type = CONFIG_BINARY,
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "0x220",
                                .value = 0x220
                        },
                        {
                                .description = "0x240",
                                .value = 0x240
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 0x220
        },
        {
                .name = "irq",
                .description = "IRQ",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "IRQ 2",
                                .value = 2
                        },
                        {
                                .description = "IRQ 5",
                                .value = 5
                        },
                        {
                                .description = "IRQ 7",
                                .value = 7
                        },
                        {
                                .description = "IRQ 10",
                                .value = 10
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 5
        },
        {
                .name = "dma",
                .description = "DMA",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "DMA 1",
                                .value = 1
                        },
                        {
                                .description = "DMA 3",
                                .value = 3
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 1
        },
        {
                .name = "midi",
                .description = "MIDI out device",
                .type = CONFIG_MIDI,
                .default_int = 0
        },
        {
                .type = -1
        }
};

static device_config_t sb_awe32_config[] =
{
        {
                .name = "addr",
                .description = "Address",
                .type = CONFIG_BINARY,
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "0x220",
                                .value = 0x220
                        },
                        {
                                .description = "0x240",
                                .value = 0x240
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 0x220
        },
        {
                .name = "irq",
                .description = "IRQ",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "IRQ 2",
                                .value = 2
                        },
                        {
                                .description = "IRQ 5",
                                .value = 5
                        },
                        {
                                .description = "IRQ 7",
                                .value = 7
                        },
                        {
                                .description = "IRQ 10",
                                .value = 10
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 5
        },
        {
                .name = "dma",
                .description = "DMA",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "DMA 1",
                                .value = 1
                        },
                        {
                                .description = "DMA 3",
                                .value = 3
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 1
        },
        {
                .name = "midi",
                .description = "MIDI out device",
                .type = CONFIG_MIDI,
                .default_int = 0
        },
        {
                .name = "onboard_ram",
                .description = "Onboard RAM",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "None",
                                .value = 0
                        },
                        {
                                .description = "512 KB",
                                .value = 512
                        },
                        {
                                .description = "2 MB",
                                .value = 2048
                        },
                        {
                                .description = "8 MB",
                                .value = 8192
                        },
                        {
                                .description = "28 MB",
                                .value = 28*1024
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 512
        },
        {
                .type = -1
        }
};

static device_config_t sb_awe64pci_config[] =
{
        {
                .name = "revision",
                .description = "Revision",
                .type = CONFIG_BINARY,
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "CT4600",
                                .value = 0x0010
                        },
                        {
                                .description = "CT4650",
                                .value = 0x0030
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 0x0010
        },
        {
                .name = "midi",
                .description = "MIDI out device",
                .type = CONFIG_MIDI,
                .default_int = 0
        },
        {
                .name = "onboard_ram",
                .description = "Onboard RAM",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "None",
                                .value = 0
                        },
                        {
                                .description = "512 KB",
                                .value = 512
                        },
                        {
                                .description = "2 MB",
                                .value = 2048
                        },
                        {
                                .description = "8 MB",
                                .value = 8192
                        },
                        {
                                .description = "28 MB",
                                .value = 28*1024
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 512
        },
        {
                .type = -1
        }
};

device_t sb_1_device =
{
        "Sound Blaster v1.0",
        0,
        sb_1_init,
        sb_close,
        NULL,
        sb_speed_changed,
        NULL,
        sb_add_status_info,
        sb_config
};
device_t sb_15_device =
{
        "Sound Blaster v1.5",
        0,
        sb_15_init,
        sb_close,
        NULL,
        sb_speed_changed,
        NULL,
        sb_add_status_info,
        sb_config
};
device_t sb_2_device =
{
        "Sound Blaster v2.0",
        0,
        sb_2_init,
        sb_close,
        NULL,
        sb_speed_changed,
        NULL,
        sb_add_status_info,
        sb_config
};
device_t sb_pro_v1_device =
{
        "Sound Blaster Pro v1",
        0,
        sb_pro_v1_init,
        sb_close,
        NULL,
        sb_speed_changed,
        NULL,
        sb_add_status_info,
        sb_pro_config
};
device_t sb_pro_v2_device =
{
        "Sound Blaster Pro v2",
        0,
        sb_pro_v2_init,
        sb_close,
        NULL,
        sb_speed_changed,
        NULL,
        sb_add_status_info,
        sb_pro_config
};
device_t sb_16_device =
{
        "Sound Blaster 16",
        0,
        sb_16_init,
        sb_close,
        NULL,
        sb_speed_changed,
        NULL,
        sb_add_status_info,
        sb_16_config
};
device_t sb_awe32_device =
{
        "Sound Blaster AWE32",
        0,
        sb_awe32_init,
        sb_close,
        sb_awe32_available,
        sb_speed_changed,
        NULL,
        sb_add_status_info,
        sb_awe32_config
};
device_t sb_awe64pci_device =
{
        "Sound Blaster AWE64 PCI",
        0,
        sb_awe64pci_init,
        sb_close,
        sb_awe32_available,
        sb_speed_changed,
        NULL,
        sb_add_status_info,
        sb_awe64pci_config
};
