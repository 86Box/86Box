/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Sound Blaster emulation.
 *
 * Version:	@(#)sound_sb.c	1.0.2	2017/10/04
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../ibm.h"
#include "../io.h"
#include "../mca.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "sound.h"
#include "filters.h"
#include "snd_dbopl.h"
#include "snd_emu8k.h"
#include "snd_mpu401.h"
#include "snd_opl.h"
#include "snd_sb.h"
#include "snd_sb_dsp.h"


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
        mpu_t   mpu;
        emu8k_t         emu8k;

        int pos;

        uint8_t pos_regs[8];
} sb_t;

/* 0 to 7 -> -14dB to 0dB i 2dB steps. 8 to 15 -> 0 to +14dB in 2dB steps.
  Note that for positive dB values, this is not amplitude, it is amplitude-1. */
const float sb_bass_treble_4bits[]= {
   0.199526231, 0.25, 0.316227766, 0.398107170, 0.5, 0.63095734, 0.794328234, 1, 
    0, 0.25892541, 0.584893192, 1, 1.511886431, 2.16227766, 3, 4.011872336
};

static int sb_att[]=
{
        50,65,82,103,130,164,207,260,328,413,520,655,825,1038,1307,
        1645,2072,2608,3283,4134,5205,6553,8250,10385,13075,16461,20724,26089,
        32845,41349,52055,65535
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
                
                out_l = ((((sb->opl.buffer[c]     * mixer->fm_l) >> 16) * 51000) >> 16);
                out_r = ((((sb->opl.buffer[c + 1] * mixer->fm_r) >> 16) * 51000) >> 16);

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
                
                out_l = (out_l * mixer->master_l) >> 15;
                out_r = (out_r * mixer->master_r) >> 15;

                if (mixer->bass_l != 8 || mixer->bass_r != 8 || mixer->treble_l != 8 || mixer->treble_r != 8)
                {
                        /* This is not exactly how one does bass/treble controls, but the end result is like it. A better implementation would reduce the cpu usage */
                        if (mixer->bass_l>8) out_l += (int32_t)(low_iir(0, (float)out_l)*sb_bass_treble_4bits[mixer->bass_l]);
                        if (mixer->bass_r>8)  out_r += (int32_t)(low_iir(1, (float)out_r)*sb_bass_treble_4bits[mixer->bass_r]);
                        if (mixer->treble_l>8) out_l += (int32_t)(high_iir(0, (float)out_l)*sb_bass_treble_4bits[mixer->treble_l]);
                        if (mixer->treble_r>8) out_r += (int32_t)(high_iir(1, (float)out_r)*sb_bass_treble_4bits[mixer->treble_r]);
                        if (mixer->bass_l<8)   out_l = (int32_t)((out_l )*sb_bass_treble_4bits[mixer->bass_l] + low_cut_iir(0, (float)out_l)*(1.0-sb_bass_treble_4bits[mixer->bass_l]));
                        if (mixer->bass_r<8)   out_r = (int32_t)((out_r )*sb_bass_treble_4bits[mixer->bass_r] + low_cut_iir(1, (float)out_r)*(1.0-sb_bass_treble_4bits[mixer->bass_r])); 
                        if (mixer->treble_l<8) out_l = (int32_t)((out_l )*sb_bass_treble_4bits[mixer->treble_l] + high_cut_iir(0, (float)out_l)*(1.0-sb_bass_treble_4bits[mixer->treble_l]));
                        if (mixer->treble_r<8) out_r = (int32_t)((out_r )*sb_bass_treble_4bits[mixer->treble_r] + high_cut_iir(1, (float)out_r)*(1.0-sb_bass_treble_4bits[mixer->treble_r]));
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
                int32_t out_l, out_r;
                
                out_l = ((((sb->opl.buffer[c]     * mixer->fm_l) >> 16) * (opl3_type ? 47000 : 51000)) >> 16);
                out_r = ((((sb->opl.buffer[c + 1] * mixer->fm_r) >> 16) * (opl3_type ? 47000 : 51000)) >> 16);

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
                
                out_l = (out_l * mixer->master_l) >> 15;
                out_r = (out_r * mixer->master_r) >> 15;

                if (mixer->bass_l != 8 || mixer->bass_r != 8 || mixer->treble_l != 8 || mixer->treble_r != 8)
                {
                        /* This is not exactly how one does bass/treble controls, but the end result is like it. A better implementation would reduce the cpu usage */
                        if (mixer->bass_l>8) out_l += (int32_t)(low_iir(0, (float)out_l)*sb_bass_treble_4bits[mixer->bass_l]);
                        if (mixer->bass_r>8)  out_r += (int32_t)(low_iir(1, (float)out_r)*sb_bass_treble_4bits[mixer->bass_r]);
                        if (mixer->treble_l>8) out_l += (int32_t)(high_iir(0, (float)out_l)*sb_bass_treble_4bits[mixer->treble_l]);
                        if (mixer->treble_r>8) out_r += (int32_t)(high_iir(1, (float)out_r)*sb_bass_treble_4bits[mixer->treble_r]);
                        if (mixer->bass_l<8)   out_l = (int32_t)((out_l )*sb_bass_treble_4bits[mixer->bass_l] + low_cut_iir(0, (float)out_l)*(1.0-sb_bass_treble_4bits[mixer->bass_l]));
                        if (mixer->bass_r<8)   out_r = (int32_t)((out_r )*sb_bass_treble_4bits[mixer->bass_r] + low_cut_iir(1, (float)out_r)*(1.0-sb_bass_treble_4bits[mixer->bass_r])); 
                        if (mixer->treble_l<8) out_l = (int32_t)((out_l )*sb_bass_treble_4bits[mixer->treble_l] + high_cut_iir(0, (float)out_l)*(1.0-sb_bass_treble_4bits[mixer->treble_l]));
                        if (mixer->treble_r<8) out_r = (int32_t)((out_r )*sb_bass_treble_4bits[mixer->treble_r] + high_cut_iir(1, (float)out_r)*(1.0-sb_bass_treble_4bits[mixer->treble_r]));
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
                
                out_l = ((((sb->opl.buffer[c]     * mixer->fm_l) >> 16) * (opl3_type ? 47000 : 51000)) >> 16);
                out_r = ((((sb->opl.buffer[c + 1] * mixer->fm_r) >> 16) * (opl3_type ? 47000 : 51000)) >> 16);

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
                
                out_l = (out_l * mixer->master_l) >> 15;
                out_r = (out_r * mixer->master_r) >> 15;

                if (mixer->bass_l != 8 || mixer->bass_r != 8 || mixer->treble_l != 8 || mixer->treble_r != 8)
                {
                        /* This is not exactly how one does bass/treble controls, but the end result is like it. A better implementation would reduce the cpu usage */
                        if (mixer->bass_l>8) out_l += (int32_t)(low_iir(0, (float)out_l)*sb_bass_treble_4bits[mixer->bass_l]);
                        if (mixer->bass_r>8)  out_r += (int32_t)(low_iir(1, (float)out_r)*sb_bass_treble_4bits[mixer->bass_r]);
                        if (mixer->treble_l>8) out_l += (int32_t)(high_iir(0, (float)out_l)*sb_bass_treble_4bits[mixer->treble_l]);
                        if (mixer->treble_r>8) out_r += (int32_t)(high_iir(1, (float)out_r)*sb_bass_treble_4bits[mixer->treble_r]);
                        if (mixer->bass_l<8)   out_l = (int32_t)((out_l )*sb_bass_treble_4bits[mixer->bass_l] + low_cut_iir(0, (float)out_l)*(1.0-sb_bass_treble_4bits[mixer->bass_l]));
                        if (mixer->bass_r<8)   out_r = (int32_t)((out_r )*sb_bass_treble_4bits[mixer->bass_r] + low_cut_iir(1, (float)out_r)*(1.0-sb_bass_treble_4bits[mixer->bass_r])); 
                        if (mixer->treble_l<8) out_l = (int32_t)((out_l )*sb_bass_treble_4bits[mixer->treble_l] + high_cut_iir(0, (float)out_l)*(1.0-sb_bass_treble_4bits[mixer->treble_l]));
                        if (mixer->treble_r<8) out_r = (int32_t)((out_r )*sb_bass_treble_4bits[mixer->treble_r] + high_cut_iir(1, (float)out_r)*(1.0-sb_bass_treble_4bits[mixer->treble_r]));
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

        switch (mixer->index)
        {
                case 0x00: case 0x04: case 0x0a: case 0x0c: case 0x0e:
                case 0x22: case 0x26: case 0x28: case 0x2e:
                return mixer->regs[mixer->index];
        }
        
        return 0xff;
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
			case 0x81:
			if (val & 1) sb->dsp.sb_8_dmanum = 0;
			if (val & 2) sb->dsp.sb_8_dmanum = 1;
			if (val & 8) sb->dsp.sb_8_dmanum = 3;
			if (val & 0x20) sb->dsp.sb_16_dmanum = 5;
			if (val & 0x40) sb->dsp.sb_16_dmanum = 6;
			if (val & 0x80) sb->dsp.sb_16_dmanum = 7;
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
        }
}

uint8_t sb_16_mixer_read(uint16_t addr, void *p)
{
        sb_t *sb = (sb_t *)p;
        sb_mixer_t *mixer = &sb->mixer;
	uint8_t temp = 0;

        if (!(addr & 1))
                return mixer->index;

        switch (mixer->index)
        {
                case 0x80:
                switch (sb->dsp.sb_irqnum)
                {
                        case 2: return 1; /*IRQ 2*/
                        case 5: return 2; /*IRQ 5*/
                        case 7: return 4; /*IRQ 7*/
                        case 10: return 8; /*IRQ 10*/
                }
                break;
                case 0x81:
		switch (sb->dsp.sb_8_dmanum)
		{
			case 0:
				temp = 1;
				break;
			case 1:
				temp = 2;
				break;
			case 3:
				temp = 8;
				break;
			default:
				temp = 0;
				break;
		}
		switch (sb->dsp.sb_16_dmanum)
		{
			case 5:
				temp |= 0x20;
				break;
			case 6:
				temp |= 0x40;
				break;
			case 7:
				temp |= 0x80;
				break;
			default:
				temp |= 0x00;
				break;
		}
		return temp;
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
        mixer->cd_l     = mixer->cd_r     = 65535;
        mixer->bass_l   = mixer->bass_r   = 8;
        mixer->treble_l = mixer->treble_r = 8;
        mixer->filter = 1;
	sound_set_cd_volume(((uint32_t)mixer->master_l * (uint32_t)mixer->cd_l) / 65535,
		((uint32_t)mixer->master_r * (uint32_t)mixer->cd_r) / 65535);
}

static uint16_t sb_mcv_addr[8] = {0x200, 0x210, 0x220, 0x230, 0x240, 0x250, 0x260, 0x270};

uint8_t sb_mcv_read(int port, void *p)
{
        sb_t *sb = (sb_t *)p;
        
        pclog("sb_mcv_read: port=%04x\n", port);
        
        return sb->pos_regs[port & 7];
}

void sb_mcv_write(int port, uint8_t val, void *p)
{
        uint16_t addr;
        sb_t *sb = (sb_t *)p;

        if (port < 0x102)
                return;
        
        pclog("sb_mcv_write: port=%04x val=%02x\n", port, val);

        addr = sb_mcv_addr[sb->pos_regs[4] & 7];
        io_removehandler(addr+8, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
        io_removehandler(0x0388, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
        sb_dsp_setaddr(&sb->dsp, 0);

        sb->pos_regs[port & 7] = val;

        if (sb->pos_regs[2] & 1)
        {
                addr = sb_mcv_addr[sb->pos_regs[4] & 7];
                
                io_sethandler(addr+8, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
                io_sethandler(0x0388, 0x0002, opl2_read, NULL, NULL, opl2_write, NULL, NULL, &sb->opl);
                sb_dsp_setaddr(&sb->dsp, addr);
        }
}

static int sb_pro_mcv_irqs[4] = {7, 5, 3, 3};

uint8_t sb_pro_mcv_read(int port, void *p)
{
        sb_t *sb = (sb_t *)p;
        
        pclog("sb_pro_mcv_read: port=%04x\n", port);
        
        return sb->pos_regs[port & 7];
}

void sb_pro_mcv_write(int port, uint8_t val, void *p)
{
        uint16_t addr;
        sb_t *sb = (sb_t *)p;

        if (port < 0x102)
                return;
        
        pclog("sb_pro_mcv_write: port=%04x val=%02x\n", port, val);

        addr = (sb->pos_regs[2] & 0x20) ? 0x220 : 0x240;
        io_removehandler(addr+0, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_removehandler(addr+8, 0x0002, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_removehandler(0x0388, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_removehandler(addr+4, 0x0002, sb_pro_mixer_read, NULL, NULL, sb_pro_mixer_write, NULL, NULL, sb);
        sb_dsp_setaddr(&sb->dsp, 0);

        sb->pos_regs[port & 7] = val;

        if (sb->pos_regs[2] & 1)
        {
                addr = (sb->pos_regs[2] & 0x20) ? 0x220 : 0x240;

                io_sethandler(addr+0, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
                io_sethandler(addr+8, 0x0002, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
                io_sethandler(0x0388, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
                io_sethandler(addr+4, 0x0002, sb_pro_mixer_read, NULL, NULL, sb_pro_mixer_write, NULL, NULL, sb);
                
                sb_dsp_setaddr(&sb->dsp, addr);
        }
        sb_dsp_setirq(&sb->dsp, sb_pro_mcv_irqs[(sb->pos_regs[5] >> 4) & 3]);
        sb_dsp_setdma8(&sb->dsp, sb->pos_regs[4] & 3);
}
        
void *sb_1_init(device_t *info)
{
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_hex16("base");        
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
void *sb_15_init(device_t *info)
{
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_hex16("base");
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

void *sb_mcv_init(device_t *info)
{
        sb_t *sb = malloc(sizeof(sb_t));
        memset(sb, 0, sizeof(sb_t));

        opl2_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SB15);
        sb_dsp_setaddr(&sb->dsp, 0);
        sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
        sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
        sb_mixer_init(&sb->mixer);
        sound_add_handler(sb_get_buffer_opl2, sb);
        mca_add(sb_mcv_read, sb_mcv_write, sb);
        sb->pos_regs[0] = 0x84;
        sb->pos_regs[1] = 0x50;
        return sb;
}
void *sb_2_init(device_t *info)
{
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_hex16("base");
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

void *sb_pro_v1_init(device_t *info)
{
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_hex16("base");
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
        sb->mixer.regs[0x28] = 0xff;
        sb->mixer.regs[0xe]  = 0;

        return sb;
}

void *sb_pro_v2_init(device_t *info)
{
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_hex16("base");
        memset(sb, 0, sizeof(sb_t));

        opl3_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SBPRO2);
        sb_dsp_setaddr(&sb->dsp, addr);
        sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
        sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
        sb_mixer_init(&sb->mixer);
        io_sethandler(addr+0, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(addr+8, 0x0002, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(0x0388, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(addr+4, 0x0002, sb_pro_mixer_read, NULL, NULL, sb_pro_mixer_write, NULL, NULL, sb);
        sound_add_handler(sb_get_buffer_opl3, sb);

        sb->mixer.regs[0x22] = 0xff;
        sb->mixer.regs[0x04] = 0xff;
        sb->mixer.regs[0x26] = 0xff;
        sb->mixer.regs[0x28] = 0xff;
        sb->mixer.regs[0xe]  = 0;

        return sb;
}

void *sb_pro_mcv_init(device_t *info)
{
        sb_t *sb = malloc(sizeof(sb_t));
        memset(sb, 0, sizeof(sb_t));

        opl3_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SBPRO2);
        sb_mixer_init(&sb->mixer);
        io_sethandler(0x0388, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        sound_add_handler(sb_get_buffer_opl3, sb);

        sb->mixer.regs[0x22] = 0xff;
        sb->mixer.regs[0x04] = 0xff;
        sb->mixer.regs[0x26] = 0xff;
        sb->mixer.regs[0xe]  = 0;

        mca_add(sb_pro_mcv_read, sb_pro_mcv_write, sb);
        sb->pos_regs[0] = 0x03;
        sb->pos_regs[1] = 0x51;

        return sb;
}

void *sb_16_init(device_t *info)
{
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_hex16("base");
        memset(sb, 0, sizeof(sb_t));

        opl3_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SB16);
        sb_dsp_setaddr(&sb->dsp, addr);
        sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
        sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
        sb_dsp_setdma16(&sb->dsp, device_get_config_int("dma16"));
        sb_mixer_init(&sb->mixer);
        io_sethandler(addr + 0, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(addr + 8, 0x0002, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(0x0388, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(addr + 4, 0x0002, sb_16_mixer_read, NULL, NULL, sb_16_mixer_write, NULL, NULL, sb);
        sound_add_handler(sb_get_buffer_opl3, sb);
        mpu401_init(&sb->mpu, device_get_config_hex16("base401"), device_get_config_int("irq401"), device_get_config_int("mode401"));

        sb->mixer.regs[0x30] = 31 << 3;
        sb->mixer.regs[0x31] = 31 << 3;
        sb->mixer.regs[0x32] = 31 << 3;
        sb->mixer.regs[0x33] = 31 << 3;
        sb->mixer.regs[0x34] = 31 << 3;
        sb->mixer.regs[0x35] = 31 << 3;
        sb->mixer.regs[0x36] = 31 << 3;
        sb->mixer.regs[0x37] = 31 << 3;
        sb->mixer.regs[0x44] =  8 << 4;
        sb->mixer.regs[0x45] =  8 << 4;
        sb->mixer.regs[0x46] =  8 << 4;
        sb->mixer.regs[0x47] =  8 << 4;
        sb->mixer.regs[0x22] = (sb->mixer.regs[0x30] & 0xf0) | (sb->mixer.regs[0x31] >> 4);
        sb->mixer.regs[0x04] = (sb->mixer.regs[0x32] & 0xf0) | (sb->mixer.regs[0x33] >> 4);
        sb->mixer.regs[0x26] = (sb->mixer.regs[0x34] & 0xf0) | (sb->mixer.regs[0x35] >> 4);
        sb->mixer.regs[0x28] = (sb->mixer.regs[0x36] & 0xf0) | (sb->mixer.regs[0x37] >> 4);

        return sb;
}

int sb_awe32_available(void)
{
        return rom_present(L"roms/sound/awe32.raw");
}

void *sb_awe32_init(device_t *info)
{
        sb_t *sb = malloc(sizeof(sb_t));
        uint16_t addr = device_get_config_hex16("base");        
        int onboard_ram = device_get_config_int("onboard_ram");
        memset(sb, 0, sizeof(sb_t));

        opl3_init(&sb->opl);
        sb_dsp_init(&sb->dsp, SB16 + 1);
        sb_dsp_setaddr(&sb->dsp, addr);
        sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
        sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
        sb_dsp_setdma16(&sb->dsp, device_get_config_int("dma16"));
        sb_mixer_init(&sb->mixer);
        io_sethandler(addr + 0, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(addr + 8, 0x0002, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(0x0388, 0x0004, opl3_read,   NULL, NULL, opl3_write,   NULL, NULL, &sb->opl);
        io_sethandler(addr + 4, 0x0002, sb_16_mixer_read, NULL, NULL, sb_16_mixer_write, NULL, NULL, sb);
        sound_add_handler(sb_get_buffer_emu8k, sb);
        mpu401_init(&sb->mpu, device_get_config_hex16("base401"), device_get_config_int("irq401"), device_get_config_int("mode401"));
        emu8k_init(&sb->emu8k, onboard_ram);

        sb->mixer.regs[0x30] = 31 << 3;
        sb->mixer.regs[0x31] = 31 << 3;
        sb->mixer.regs[0x32] = 31 << 3;
        sb->mixer.regs[0x33] = 31 << 3;
        sb->mixer.regs[0x34] = 31 << 3;
        sb->mixer.regs[0x35] = 31 << 3;
        sb->mixer.regs[0x36] = 31 << 3;
        sb->mixer.regs[0x37] = 31 << 3;
        sb->mixer.regs[0x44] =  8 << 4;
        sb->mixer.regs[0x45] =  8 << 4;
        sb->mixer.regs[0x46] =  8 << 4;
        sb->mixer.regs[0x47] =  8 << 4;
        sb->mixer.regs[0x22] = (sb->mixer.regs[0x30] & 0xf0) | (sb->mixer.regs[0x31] >> 4);
        sb->mixer.regs[0x04] = (sb->mixer.regs[0x32] & 0xf0) | (sb->mixer.regs[0x33] >> 4);
        sb->mixer.regs[0x26] = (sb->mixer.regs[0x34] & 0xf0) | (sb->mixer.regs[0x35] >> 4);
        sb->mixer.regs[0x28] = (sb->mixer.regs[0x36] & 0xf0) | (sb->mixer.regs[0x37] >> 4);
        
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
                "base", "Address", CONFIG_HEX16, "", 0x220,
                {
                        {
                                "0x220", 0x220
                        },
                        {
                                "0x240", 0x240
                        },
                        {
                                ""
                        }
                }
        },
        {
                "irq", "IRQ", CONFIG_SELECTION, "", 7,
                {
                        {
                                "IRQ 2", 2
                        },
                        {
                                "IRQ 3", 3
                        },
                        {
                                "IRQ 5", 5
                        },
                        {
                                "IRQ 7", 7
                        },
                        {
                                ""
                        }
                }
        },
        {
                "dma", "DMA", CONFIG_SELECTION, "", 1,
                {
                        {
                                "DMA 1", 1
                        },
                        {
                                "DMA 3", 3
                        },
                        {
                                ""
                        }
                }
        },
        {
                "", "", -1
        }
};

static device_config_t sb_mcv_config[] =
{
        {
                "irq", "IRQ", CONFIG_SELECTION, "", 7,
                {
                        {
                                "IRQ 3", 3
                        },
                        {
                                "IRQ 5", 5
                        },
                        {
                                "IRQ 7", 7
                        },
                        {
                                ""
                        }
                }
        },
        {
                "dma", "DMA", CONFIG_SELECTION, "", 1,
                {
                        {
                                "DMA 1", 1
                        },
                        {
                                "DMA 3", 3
                        },
                        {
                                ""
                        }
                }
        },
        {
                "", "", -1
        }
};

static device_config_t sb_pro_config[] =
{
        {
                "base", "Address", CONFIG_HEX16, "", 0x220,
                {
                        {
                                "0x220", 0x220
                        },
                        {
                                "0x240", 0x240
                        },
                        {
                                ""
                        }
                }
        },
        {
                "irq", "IRQ", CONFIG_SELECTION, "", 7,
                {
                        {
                                "IRQ 2", 2
                        },
                        {
                                "IRQ 5", 5
                        },
                        {
                                "IRQ 7", 7
                        },
                        {
                                "IRQ 10", 10
                        },
                        {
                                ""
                        }
                }
        },
        {
                "dma", "DMA", CONFIG_SELECTION, "", 1,
                {
                        {
                                "DMA 1", 1
                        },
                        {
                                "DMA 3", 3
                        },
                        {
                                ""
                        }
                }
        },
        {
                "", "", -1
        }
};

static device_config_t sb_16_config[] =
{
        {
                "base", "Address", CONFIG_HEX16, "", 0x220,
                {
                        {
                                "0x220", 0x220
                        },
                        {
                                "0x240", 0x240
                        },
                        {
                                "0x260", 0x260
                        },
                        {
                                "0x280", 0x280
                        },
                        {
                                ""
                        }
                }
        },
        {
                "base401", "MPU-401 Address", CONFIG_HEX16, "", 0x330,
                {
                        {
                                "0x300", 0x300
                        },
                        {
                                "0x330", 0x330
                        },
                        {
                                ""
                        }
                }
        },
        {
                "irq", "IRQ", CONFIG_SELECTION, "", 5,
                {
                        {
                                "IRQ 2", 2
                        },
                        {
                                "IRQ 5", 5
                        },
                        {
                                "IRQ 7", 7
                        },
                        {
                                "IRQ 10", 10
                        },
                        {
                                ""
                        }
                }
        },
        {
                "irq401", "MPU-401 IRQ", CONFIG_SELECTION, "", 9,
                {
                        {
                                "IRQ 9", 9
                        },
                        {
                                "IRQ 3", 3
                        },
                        {
                                "IRQ 4", 4
                        },
                        {
                                "IRQ 5", 5
                        },
                        {
                                "IRQ 7", 7
                        },
                        {
                                "IRQ 10", 10
                        },
                        {
                                ""
                        }
                }
        },
        {
                "dma", "Low DMA channel", CONFIG_SELECTION, "", 1,
                {
                        {
                                "DMA 0", 0
                        },
                        {
                                "DMA 1", 1
                        },
                        {
                                "DMA 3", 3
                        },
                        {
                                ""
                        }
                }
        },
        {
                "dma16", "High DMA channel", CONFIG_SELECTION, "", 5,
                {
                        {
                                "DMA 5", 5
                        },
                        {
                                "DMA 6", 6
                        },
                        {
                                "DMA 7", 7
                        },
                        {
                                ""
                        }
                }
        },
        {
                "mode401", "MPU-401 mode", CONFIG_SELECTION, "", 1,
                {
                        {
                                "UART", M_UART
                        },
                        {
                                "Intelligent", M_INTELLIGENT
                        },
                        {
                                ""
                        }
                }
        },
        {
                "", "", -1
        }
};

static device_config_t sb_awe32_config[] =
{
        {
                "base", "Address", CONFIG_HEX16, "", 0x220,
                {
                        {
                                "0x220", 0x220
                        },
                        {
                                "0x240", 0x240
                        },
                        {
                                "0x260", 0x260
                        },
                        {
                                "0x280", 0x280
                        },
                        {
                                ""
                        }
                }
        },
        {
                "base401", "MPU-401 Address", CONFIG_HEX16, "", 0x330,
                {
                        {
                                "0x300", 0x300
                        },
                        {
                                "0x330", 0x330
                        },
                        {
                                ""
                        }
                }
        },
        {
                "irq", "IRQ", CONFIG_SELECTION, "", 5,
                {
                        {
                                "IRQ 2", 2
                        },
                        {
                                "IRQ 5", 5
                        },
                        {
                                "IRQ 7", 7
                        },
                        {
                                "IRQ 10", 10
                        },
                        {
                                ""
                        }
                }
        },
        {
                "irq401", "MPU-401 IRQ", CONFIG_SELECTION, "", 9,
                {
                        {
                                "IRQ 9", 9
                        },
                        {
                                "IRQ 3", 3
                        },
                        {
                                "IRQ 4", 4
                        },
                        {
                                "IRQ 5", 5
                        },
                        {
                                "IRQ 7", 7
                        },
                        {
                                "IRQ 10", 10
                        },
                        {
                                ""
                        }
                }
        },
        {
                "dma", "Low DMA channel", CONFIG_SELECTION, "", 1,
                {
                        {
                                "DMA 0", 0
                        },
                        {
                                "DMA 1", 1
                        },
                        {
                                "DMA 3", 3
                        },
                        {
                                ""
                        }
                }
        },
        {
                "dma16", "High DMA channel", CONFIG_SELECTION, "", 5,
                {
                        {
                                "DMA 5", 5
                        },
                        {
                                "DMA 6", 6
                        },
                        {
                                "DMA 7", 7
                        },
                        {
                                ""
                        }
                }
        },
        {
                "mode401", "MPU-401 mode", CONFIG_SELECTION, "", 1,
                {
                        {
                                "UART", M_UART
                        },
                        {
                                "Intelligent", M_INTELLIGENT
                        },
                        {
                                ""
                        }
                }
        },
        {
                "onboard_ram", "Onboard RAM", CONFIG_SELECTION, "", 512,
                {
                        {
                                "None", 0
                        },
                        {
                                "512 KB", 512
                        },
                        {
                                "2 MB", 2048
                        },
                        {
                                "8 MB", 8192
                        },
                        {
                                "28 MB", 28*1024
                        },
                        {
                                ""
                        }
                }
        },
        {
                "", "", -1
        }
};

device_t sb_1_device =
{
        "Sound Blaster v1.0",
        DEVICE_ISA,
	0,
        sb_1_init, sb_close, NULL, NULL,
        sb_speed_changed,
        NULL,
        sb_add_status_info,
        sb_config
};
device_t sb_15_device =
{
        "Sound Blaster v1.5",
        DEVICE_ISA,
	0,
        sb_15_init, sb_close, NULL, NULL,
        sb_speed_changed,
        NULL,
        sb_add_status_info,
        sb_config
};
device_t sb_mcv_device =
{
        "Sound Blaster MCV",
        DEVICE_MCA,
	0,
        sb_mcv_init, sb_close, NULL, NULL,
        sb_speed_changed,
        NULL,
        sb_add_status_info,
        sb_mcv_config
};
device_t sb_2_device =
{
        "Sound Blaster v2.0",
        DEVICE_ISA,
	0,
        sb_2_init, sb_close, NULL, NULL,
        sb_speed_changed,
        NULL,
        sb_add_status_info,
        sb_config
};
device_t sb_pro_v1_device =
{
        "Sound Blaster Pro v1",
        DEVICE_ISA,
	0,
        sb_pro_v1_init, sb_close, NULL, NULL,
        sb_speed_changed,
        NULL,
        sb_add_status_info,
        sb_pro_config
};
device_t sb_pro_v2_device =
{
        "Sound Blaster Pro v2",
        DEVICE_ISA,
	0,
        sb_pro_v2_init, sb_close, NULL, NULL,
        sb_speed_changed,
        NULL,
        sb_add_status_info,
        sb_pro_config
};
device_t sb_pro_mcv_device =
{
        "Sound Blaster Pro MCV",
        DEVICE_MCA,
	0,
        sb_pro_mcv_init, sb_close, NULL, NULL,
        sb_speed_changed,
        NULL,
        sb_add_status_info,
        NULL
};
device_t sb_16_device =
{
        "Sound Blaster 16",
        DEVICE_ISA,
	0,
        sb_16_init, sb_close, NULL, NULL,
        sb_speed_changed,
        NULL,
        sb_add_status_info,
        sb_16_config
};
device_t sb_awe32_device =
{
        "Sound Blaster AWE32",
        DEVICE_ISA,
	0,
        sb_awe32_init, sb_close, NULL,
        sb_awe32_available,
        sb_speed_changed,
        NULL,
        sb_add_status_info,
        sb_awe32_config
};
