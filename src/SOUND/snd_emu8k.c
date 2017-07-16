/*12log2(r) * 4096

  freq = 2^((in - 0xe000) / 4096)*/
/*LFO - lowest (0.042 Hz) = 2^20 steps = 1048576
        highest (10.72 Hz) = 2^12 steps = 4096*/
#include <stdlib.h>
#include <math.h>
#include "../ibm.h"
#include "../io.h"
#include "../mem.h"
#include "../rom.h"
#include "../timer.h"
#include "../device.h"
#include "sound.h"
#include "snd_emu8k.h"


enum
{
        ENV_STOPPED = 0,
        ENV_ATTACK  = 1,
        ENV_DECAY   = 2,
        ENV_SUSTAIN = 3,
        ENV_RELEASE = 4
};

static int64_t freqtable[65536];
static int attentable[256];
static int envtable[4096];
static int lfotable[4096];

static int32_t filt_w0[256];
/*static float filt_w0[256];*/

#define READ16(addr, var)       switch ((addr) & 2)                                     \
                                {                                                       \
                                        case 0: ret = (var) & 0xffff;         break;    \
                                        case 2: ret = ((var) >> 16) & 0xffff; break;    \
                                }
                                
#define WRITE16(addr, var, val) switch ((addr) & 2)                                               \
                                {                                                                 \
                                        case 0: var = (var & 0xffff0000) | (val);         break;  \
                                        case 2: var = (var & 0x0000ffff) | ((val) << 16); break;  \
                                }

static __inline int16_t EMU8K_READ(emu8k_t *emu8k, uint16_t addr)
{
        addr &= EMU8K_MEM_ADDRESS_MASK;
        /* TODO: I've read that the AWE64 Gold model had a 4MB Rom. It would be interesting
           to find that rom and do some tests with it. */
        if (addr < EMU8K_ROM_MEM_1MB_END)
                return emu8k->rom[addr];
        if (addr < EMU8K_RAM_MEM_START || addr >= emu8k->ram_end_addr)
                return 0;
        if (!emu8k->ram)
                return 0;
        return emu8k->ram[addr - EMU8K_RAM_MEM_START];
}

static __inline int16_t EMU8K_READ_INTERP(emu8k_t *emu8k, uint16_t addr)
{
        int16_t dat1 = EMU8K_READ(emu8k, addr >> 8);
        int16_t dat2 = EMU8K_READ(emu8k, (addr >> 8) + 1);
        return ((dat1 * (0xff - (addr & 0xff))) + (dat2 * (addr & 0xff))) >> 8;
}

static __inline void EMU8K_WRITE(emu8k_t *emu8k, uint16_t addr, uint16_t val)
{
        if ( !emu8k->ram || addr < EMU8K_RAM_MEM_START)
                return;

        /* It looks like if an application writes to a memory part outside of the available amount on the card, 
           it wraps, and opencubicplayer uses that to detect the amount of memory, as opposed to simply check
           at the address that it has just tried to write. */
        addr &= EMU8K_MEM_ADDRESS_MASK;
        while (addr >= emu8k->ram_end_addr) {
                addr -= emu8k->ram_end_addr - EMU8K_RAM_MEM_START;
        }
        emu8k->ram[addr - EMU8K_RAM_MEM_START] = val;
}

static int ff = 0;

uint16_t emu8k_inw(uint16_t addr, void *p)
{
        emu8k_t *emu8k = (emu8k_t *)p;
        uint16_t ret = 0xffff;
/*        pclog("emu8k_inw %04X  reg=%i voice=%i\n", addr, emu8k->cur_reg, emu8k->cur_voice);*/

        addr -= 0x220;
        switch (addr & 0xc02)
        {
                case 0x400: case 0x402: /*Data0*/
                switch (emu8k->cur_reg)
                {
                        case 0:
                        {
			        uint32_t var = (emu8k->voice[emu8k->cur_voice].cpf & 0xFFFF0000) | ((emu8k->voice[emu8k->cur_voice].addr >> 16) & 0xFFFF);
                                READ16(addr, var);
                                return ret;
                        }
                        
                        case 1:
                        READ16(addr, emu8k->voice[emu8k->cur_voice].ptrx);
                        return ret;
                        
                        case 2:
                        READ16(addr, emu8k->voice[emu8k->cur_voice].cvcf);
                        return ret;
                        
                        case 3:
                        READ16(addr, emu8k->voice[emu8k->cur_voice].vtft);
                        return ret;
                        
                        case 4: case 5: /*???*/
                        return 0xffff;
                        
                        case 6:
                        READ16(addr, emu8k->voice[emu8k->cur_voice].psst);
                        return ret;
                                                
                        case 7:
                        READ16(addr, emu8k->voice[emu8k->cur_voice].csl);
                        return ret;
                }
                break;

                case 0x800: /*Data1*/
                switch (emu8k->cur_reg)
                {
                        case 0:
                        {
				emu8k->voice[emu8k->cur_voice].ccca = 
					(emu8k->voice[emu8k->cur_voice].ccca & 0xFF000000) | ((emu8k->voice[emu8k->cur_voice].addr >> 32) & EMU8K_MEM_ADDRESS_MASK);
                                READ16(addr, emu8k->voice[emu8k->cur_voice].ccca);
                                return ret;
                        }

                        case 1:
                        switch (emu8k->cur_voice)
                        {
                                case 20:
                                READ16(addr, emu8k->smalr);
                                return ret;
                                case 21:
                                READ16(addr, emu8k->smarr);
                                return ret;
                                case 22:
                                READ16(addr, emu8k->smalw);
                                return ret;
                                case 23:
                                READ16(addr, emu8k->smarw);
                                return ret;
                                
                                case 26:
                                {
                                        uint16_t val = emu8k->smld_buffer;
                                        emu8k->smld_buffer = EMU8K_READ(emu8k, emu8k->smalr);
/*                                        pclog("emu8k_SMLR in %04X (%04X) %08X\n", val, emu8k->smld_buffer, emu8k->smalr);*/
                                        emu8k->smalr++;
                                        return val;
                                }
                                /*The EMU8000 PGM describes the return values of these registers as 'a VLSI error'*/
                                case 29: /*Configuration Word 1*/
                                return (emu8k->hwcf1 & 0xfe) | (emu8k->hwcf3 & 0x01);
                                case 30: /*Configuration Word 2*/
                                return ((emu8k->hwcf2 >> 4) & 0x0e) | (emu8k->hwcf1 & 0x01) | ((emu8k->hwcf3 & 0x02) ? 0x10 : 0) | ((emu8k->hwcf3 & 0x04) ? 0x40 : 0) | ((emu8k->hwcf3 & 0x08) ? 0x20 : 0) | ((emu8k->hwcf3 & 0x10) ? 0x80 : 0);
                                case 31: /*Configuration Word 3*/
                                return emu8k->hwcf2 & 0x1f;
                        }
                        break;

                        case 2: /*INIT1*/
                        case 3: /*INIT3*/
                        return 0xffff; /*Can we read anything useful from here?*/
                        
                        case 5:
                        return emu8k->voice[emu8k->cur_voice].dcysusv;

                        case 7:
                        return emu8k->voice[emu8k->cur_voice].dcysus;
                }
                break;

                case 0x802: /*Data2*/
                switch (emu8k->cur_reg)
                {
                        case 0:
                        {
                                READ16(addr, emu8k->voice[emu8k->cur_voice].ccca);
                                return ret;
                        }

                        case 1:
                        switch (emu8k->cur_voice)
                        {
                                case 20:
                                READ16(addr, emu8k->smalr | ff);
                                ff ^= 0x80000000;
                                return ret;
                                case 21:
                                READ16(addr, emu8k->smarr | ff);
                                ff ^= 0x80000000;
                                return ret;
                                case 22:
                                READ16(addr, emu8k->smalw);
                                return ret;
                                case 23:
                                READ16(addr, emu8k->smarw);
                                return ret;
                                
                                case 26:
                                {
                                        uint16_t val = emu8k->smrd_buffer;
                                        emu8k->smrd_buffer = EMU8K_READ(emu8k, emu8k->smarr);
/*                                        pclog("emu8k_SMRR in %04X (%04X) %08X\n", val, emu8k->smrd_buffer, emu8k->smarr);*/
                                        emu8k->smarr++;
                                        return val;
                                }

                                case 27: /*Sample Counter*/
                                return emu8k->wc;
                        }
                        break;

                        case 2: /*INIT2*/
                        case 3: /*INIT4*/
                        return 0xffff; /*Can we read anything useful from here?*/
                        
                        case 4:                        
                        return emu8k->voice[emu8k->cur_voice].atkhldv;
                }
                break;
                                
                case 0xc00: /*Data3*/
                switch (emu8k->cur_reg)
                {
                        case 0:
                        return emu8k->voice[emu8k->cur_voice].ip;

                        case 1:
                        return emu8k->voice[emu8k->cur_voice].ifatn;

                        case 2:
                        return emu8k->voice[emu8k->cur_voice].pefe;

                        case 3:
                        return emu8k->voice[emu8k->cur_voice].fmmod;

                        case 4:
                        return emu8k->voice[emu8k->cur_voice].tremfrq;

                        case 5:
                        return emu8k->voice[emu8k->cur_voice].fm2frq2;

                        case 6:
                        return 0xffff;
                        
                        case 7: /*ID?*/
                        return 0x1c | ((emu8k->id & 0x0002) ? 0xff02 : 0);
                }
                break;
                case 0xc02:
                /* LS five bits = channel number, next 3 bits = register number
                   and MS 8 bits = VLSI test register.
                   Impulse tracker tests the non variability of the LS byte and the variability 
                   of the MS byte to determine that it really is an AWE32. */
                return ((rand()&0xFF) << 8) | (emu8k->cur_reg << 5) | emu8k->cur_voice;
        }
/*        fatal("Bad EMU8K inw from %08X\n", addr);*/
        return 0xffff;
}

void emu8k_outw(uint16_t addr, uint16_t val, void *p)
{
	float q;
        emu8k_t *emu8k = (emu8k_t *)p;

        emu8k_update(emu8k);
/*        pclog("emu8k_outw : addr=%08X reg=%i voice=%i  val=%04X\n", addr, emu8k->cur_reg, emu8k->cur_voice, val);*/
        addr -= 0x220;
        switch (addr & 0xc02)
        {
                case 0x400: case 0x402: /*Data0*/
                switch (emu8k->cur_reg)
                {
                        case 0:
                        WRITE16(addr, emu8k->voice[emu8k->cur_voice].cpf, val);
			/* Ignoring any effect over writing to the "fractional address". The docs says that this value is constantly
			   updating, so it has no actual effect. */
                        return;
                        
                        case 1:
                        WRITE16(addr, emu8k->voice[emu8k->cur_voice].ptrx, val);
                        return;
                        
                        case 2:
                        WRITE16(addr, emu8k->voice[emu8k->cur_voice].cvcf, val);
                        return;
                        
                        case 3:
                        WRITE16(addr, emu8k->voice[emu8k->cur_voice].vtft, val);
                        return;
                        
                        case 6:
                        WRITE16(addr, emu8k->voice[emu8k->cur_voice].psst, val);
                        /* TODO: Should we update only on MSB update, or this could be used as some sort of hack by applications? */
                        emu8k->voice[emu8k->cur_voice].loop_start = (uint64_t)(emu8k->voice[emu8k->cur_voice].psst & EMU8K_MEM_ADDRESS_MASK) << 32;
                        if (addr & 2)
                        {
                                emu8k->voice[emu8k->cur_voice].vol_l = val >> 8;
                                emu8k->voice[emu8k->cur_voice].vol_r = 255 - (val >> 8);
                        }
/*                        pclog("emu8k_outl : write PSST %08X l %i r %i\n", emu8k->voice[emu8k->cur_voice].psst, emu8k->voice[emu8k->cur_voice].vol_l, emu8k->voice[emu8k->cur_voice].vol_r);*/
                        return;
                        
                        case 7:
                        WRITE16(addr, emu8k->voice[emu8k->cur_voice].csl, val);
                        /* TODO: Should we update only on MSB update, or this could be used as some sort of hack by applications? */
	                emu8k->voice[emu8k->cur_voice].loop_end = (uint64_t)(emu8k->voice[emu8k->cur_voice].csl & EMU8K_MEM_ADDRESS_MASK) << 32;
/*                        pclog("emu8k_outl : write CSL %08X\n", emu8k->voice[emu8k->cur_voice].csl);*/
                        return;
                }
                break;

                case 0x800: /*Data1*/
                switch (emu8k->cur_reg)
                {
                        case 0:
                        WRITE16(addr, emu8k->voice[emu8k->cur_voice].ccca, val);
                        /* TODO: Should we update only on MSB update, or this could be used as some sort of hack by applications? */
                        emu8k->voice[emu8k->cur_voice].addr = (uint64_t)(emu8k->voice[emu8k->cur_voice].ccca & EMU8K_MEM_ADDRESS_MASK) << 32;
/*                        pclog("emu8k_outl : write CCCA %08X\n", emu8k->voice[emu8k->cur_voice].ccca);*/
                        return;

                        case 1:
                        switch (emu8k->cur_voice)
                        {
                                case 20:
                                WRITE16(addr, emu8k->smalr, val);
                                return;
                                case 21:
                                WRITE16(addr, emu8k->smarr, val);
                                return;
                                case 22:
                                WRITE16(addr, emu8k->smalw, val);
                                return;
                                case 23:
                                WRITE16(addr, emu8k->smarw, val);
                                return;
                                
                                case 26:
                                EMU8K_WRITE(emu8k, emu8k->smalw, val);
/*                                pclog("emu8k_SMLW %04X %08X\n", val, emu8k->smalw);*/
/*                                if (val = 0xffff && emu8k->smalw == 0x200000)
                                        output = 3;*/
                                emu8k->smalw++;
                                break;

                                case 29: /*Configuration Word 1*/
                                emu8k->hwcf1 = val;
                                return;
                                case 30: /*Configuration Word 2*/
                                emu8k->hwcf2 = val;
                                return;
                                case 31: /*Configuration Word 3*/
                                emu8k->hwcf3 = val;
                                return;
                        }
                        break;

                        case 5:
/*                        pclog("emu8k_outw : write DCYSUSV %04X\n", val);*/
                        emu8k->voice[emu8k->cur_voice].dcysusv = val;
                        emu8k->voice[emu8k->cur_voice].env_sustain = (((val >> 8) & 0x7f) << 5) << 9;
                        if (val & 0x8000) /*Release*/
                        {
                                emu8k->voice[emu8k->cur_voice].env_state = ENV_RELEASE;
                                emu8k->voice[emu8k->cur_voice].env_release = val & 0x7f;
                        }
                        else    /*Decay*/
                                emu8k->voice[emu8k->cur_voice].env_decay = val & 0x7f;
                        if (val & 0x80)
                                emu8k->voice[emu8k->cur_voice].env_state = ENV_STOPPED;
                        return;

                        case 7:
/*                        pclog("emu8k_outw : write DCYSUS %04X\n", val);*/
                        emu8k->voice[emu8k->cur_voice].dcysus = val;
                        emu8k->voice[emu8k->cur_voice].menv_sustain = (((val >> 8) & 0x7f) << 5) << 9;
                        if (val & 0x8000) /*Release*/
                        {
                                emu8k->voice[emu8k->cur_voice].menv_state = ENV_RELEASE;
                                emu8k->voice[emu8k->cur_voice].menv_release = val & 0x7f;
                        }
                        else    /*Decay*/
                                emu8k->voice[emu8k->cur_voice].menv_decay = val & 0x7f;
                        if (val & 0x80)
                                emu8k->voice[emu8k->cur_voice].menv_state = ENV_STOPPED;
                        return;
                }
                break;
                
                case 0x802: /*Data2*/
                switch (emu8k->cur_reg)
                {
                        case 0:
                        {
                                WRITE16(addr, emu8k->voice[emu8k->cur_voice].ccca, val);
	                        emu8k->voice[emu8k->cur_voice].addr = (uint64_t)(emu8k->voice[emu8k->cur_voice].ccca & EMU8K_MEM_ADDRESS_MASK) << 32;
				/* TODO: Since "fractional address" is a separate register (cpf), should we add its contents to .addr or we assume that it
				   is reset to zero? */

                                q = (float)(emu8k->voice[emu8k->cur_voice].ccca >> 28) / 15.0f;
                                q /= 10.0f; /*Horrible and wrong hack*/
                                emu8k->voice[emu8k->cur_voice].q = (int32_t)((1.0f / (0.707f + q)) * 256.0f);

/*                                pclog("emu8k_outl : write CCCA %08X Q %f invQ %X\n", emu8k->voice[emu8k->cur_voice].ccca, q, emu8k->voice[emu8k->cur_voice].q);*/
                        }
                        return;

                        case 1:
                        switch (emu8k->cur_voice)
                        {
                                case 20:
                                WRITE16(addr, emu8k->smalr, val);
                                return;
                                case 21:
                                WRITE16(addr, emu8k->smarr, val);
                                return;
                                case 22:
                                WRITE16(addr, emu8k->smalw, val);
                                return;
                                case 23:
                                WRITE16(addr, emu8k->smarw, val);
                                return;

                                case 26:
                                EMU8K_WRITE(emu8k, emu8k->smarw, val);
/*                                pclog("emu8k_SMRW %04X %08X\n", val, emu8k->smarw);*/
                                emu8k->smarw++;
                                break;
                        }
                        break;

                        case 4:
/*                        pclog("emu8k_outw : write ATKHLDV %04X\n", val);*/
                        emu8k->voice[emu8k->cur_voice].atkhldv = val;
                        emu8k->voice[emu8k->cur_voice].env_attack = (val & 0x7f) << 6;
                        if (!(val & 0x8000)) /*Trigger attack*/
                                emu8k->voice[emu8k->cur_voice].env_state = ENV_ATTACK;
                        return;

                        case 6:
/*                        pclog("emu8k_outw : write ATKHLD %04X\n", val);*/
                        emu8k->voice[emu8k->cur_voice].atkhld = val;
                        emu8k->voice[emu8k->cur_voice].menv_attack = (val & 0x7f) << 6;
                        if (!(val & 0x8000)) /*Trigger attack*/
                                emu8k->voice[emu8k->cur_voice].menv_state = ENV_ATTACK;
                        return;
                }
                break;
                
                case 0xc00: /*Data3*/
                switch (emu8k->cur_reg)
                {
                        case 0:
                        emu8k->voice[emu8k->cur_voice].ip = val;
                        emu8k->voice[emu8k->cur_voice].pitch = val;
                        return;
                        
                        case 1:
                        emu8k->voice[emu8k->cur_voice].ifatn = val;
                        emu8k->voice[emu8k->cur_voice].attenuation = attentable[val & 0xff];
                        emu8k->voice[emu8k->cur_voice].cutoff = (val >> 8);
/*                        pclog("Attenuation now %02X %i\n", val & 0xff, emu8k->voice[emu8k->cur_voice].attenuation);*/
                        return;

                        case 2:
                        emu8k->voice[emu8k->cur_voice].pefe = val;
                        emu8k->voice[emu8k->cur_voice].fe_height = (int8_t)(val & 0xff);
                        return;

                        case 3:
                        emu8k->voice[emu8k->cur_voice].fmmod = val;
                        emu8k->voice[emu8k->cur_voice].lfo1_fmmod = (val >> 8);
                        return;

                        case 4:
                        emu8k->voice[emu8k->cur_voice].tremfrq = val;
                        emu8k->voice[emu8k->cur_voice].lfo1_trem = (val >> 8);
                        return;

                        case 5:
                        emu8k->voice[emu8k->cur_voice].fm2frq2 = val;
                        emu8k->voice[emu8k->cur_voice].lfo2_fmmod = (val >> 8);
                        return;

                        case 7: /*ID?*/
                        emu8k->id = val;
                        return;
                }
                break;
                
                case 0xc02: /*Pointer*/
                emu8k->cur_voice = (val & 31);
                emu8k->cur_reg   = ((val >> 5) & 7);
                return;
        }
}

uint8_t emu8k_inb(uint16_t addr, void *p)
{
        if (addr & 1)
                return emu8k_inw(addr & ~1, p) >> 1;
        return emu8k_inw(addr, p) & 0xff;
}

void emu8k_outb(uint16_t addr, uint8_t val, void *p)
{
        if (addr & 1)
                emu8k_outw(addr & ~1, val << 8, p);
        else
                emu8k_outw(addr, val, p);
}

void emu8k_update(emu8k_t *emu8k)
{
	int32_t *buf;
	int pos;
	int c;

        int new_pos = (sound_pos_global * 44100) / 48000;
        if (emu8k->pos < new_pos)
        {

                buf = &emu8k->buffer[emu8k->pos*2];
                
                for (pos = emu8k->pos; pos < new_pos; pos++)        
                        emu8k->buffer[pos*2] = emu8k->buffer[pos*2 + 1] = 0;

                for (c = 0; c < 32; c++)
                {
                        buf = &emu8k->buffer[emu8k->pos*2];
                        
                        for (pos = emu8k->pos; pos < new_pos; pos++)
                        {
                                int32_t voice_l, voice_r;
                                int32_t dat;
                                int lfo1_vibrato, lfo2_vibrato;
                
                                if (freqtable[emu8k->voice[c].pitch] >> 32)
                                        dat = EMU8K_READ(emu8k, emu8k->voice[c].addr >> 32);
                                else
                                        dat = EMU8K_READ_INTERP(emu8k, emu8k->voice[c].addr >> 24);

                                dat = (dat * emu8k->voice[c].attenuation) >> 16;

                                dat = (dat * envtable[emu8k->voice[c].env_vol >> 9]) >> 16;
                        
                                if ((emu8k->voice[c].ccca >> 28) || (emu8k->voice[c].cutoff != 0xff))
                                {
                                        int cutoff = emu8k->voice[c].cutoff + ((emu8k->voice[c].menv_vol * emu8k->voice[c].fe_height) >> 20);
                                        if (cutoff < 0)
                                                cutoff = 0;
                                        if (cutoff > 255)
                                                cutoff = 255;

                                        emu8k->voice[c].vhp = ((-emu8k->voice[c].vbp * emu8k->voice[c].q) >> 8) - emu8k->voice[c].vlp - dat;
                                        emu8k->voice[c].vlp += (emu8k->voice[c].vbp * filt_w0[cutoff]) >> 8;
                                        emu8k->voice[c].vbp += (emu8k->voice[c].vhp * filt_w0[cutoff]) >> 8;
                                        if (emu8k->voice[c].vlp < -32767)
                                                dat = -32767;
                                        else if (emu8k->voice[c].vlp > 32767)
                                                dat = 32767;
                                        else
                                                dat = (int16_t)emu8k->voice[c].vlp;
                                }
                        
                                voice_l = (dat * emu8k->voice[c].vol_l) >> 7;
                                voice_r = (dat * emu8k->voice[c].vol_r) >> 7;
                        
                                (*buf++) += voice_l * 8192;
                                (*buf++) += voice_r * 8192;

                                switch (emu8k->voice[c].env_state)
                                {
                                        case ENV_ATTACK:
                                        emu8k->voice[c].env_vol += emu8k->voice[c].env_attack;
                                        emu8k->voice[c].vtft |= 0xffff0000;
                                        if (emu8k->voice[c].env_vol >= (1 << 21))
                                        {
                                                emu8k->voice[c].env_vol = 1 << 21;
                                                emu8k->voice[c].env_state = ENV_DECAY;
                                        }
                                        break;
                        
                                        case ENV_DECAY:
                                        emu8k->voice[c].env_vol -= emu8k->voice[c].env_decay;
                                        emu8k->voice[c].vtft = (emu8k->voice[c].vtft & ~0xffff0000) | ((emu8k->voice[c].env_sustain >> 5) << 16);
                                        if (emu8k->voice[c].env_vol <= emu8k->voice[c].env_sustain)
                                        {
                                                emu8k->voice[c].env_vol = emu8k->voice[c].env_sustain;
                                                emu8k->voice[c].env_state = ENV_SUSTAIN;
                                        }
                                        break;

                                        case ENV_RELEASE:
                                        emu8k->voice[c].env_vol -= emu8k->voice[c].env_release;
                                        emu8k->voice[c].vtft &= ~0xffff0000;
                                        if (emu8k->voice[c].env_vol <= 0)
                                        {
                                                emu8k->voice[c].env_vol = 0;
                                                emu8k->voice[c].env_state = ENV_STOPPED;
                                        }
                                        break;
                                }

                                if (emu8k->voice[c].env_vol >= (1 << 21))
                                        emu8k->voice[c].cvcf &= ~0xffff0000;
                                else
                                        emu8k->voice[c].cvcf = (emu8k->voice[c].cvcf & ~0xffff0000) | ((emu8k->voice[c].env_vol >> 5) << 16);

                                switch (emu8k->voice[c].menv_state)
                                {
                                        case ENV_ATTACK:
                                        emu8k->voice[c].menv_vol += emu8k->voice[c].menv_attack;
                                        if (emu8k->voice[c].menv_vol >= (1 << 21))
                                        {
                                                emu8k->voice[c].menv_vol = 1 << 21;
                                                emu8k->voice[c].menv_state = ENV_DECAY;
                                        }
                                        break;
                        
                                        case ENV_DECAY:
                                        emu8k->voice[c].menv_vol -= emu8k->voice[c].menv_decay;
                                        if (emu8k->voice[c].menv_vol <= emu8k->voice[c].menv_sustain)
                                        {
                                                emu8k->voice[c].menv_vol = emu8k->voice[c].menv_sustain;
                                                emu8k->voice[c].menv_state = ENV_SUSTAIN;
                                        }
                                        break;

                                        case ENV_RELEASE:
                                        emu8k->voice[c].menv_vol -= emu8k->voice[c].menv_release;
                                        if (emu8k->voice[c].menv_vol <= 0)
                                        {
                                                emu8k->voice[c].menv_vol = 0;
                                                emu8k->voice[c].menv_state = ENV_STOPPED;
                                        }
                                        break;
                                }

                                lfo1_vibrato = (lfotable[(emu8k->voice[c].lfo1_count >> 8) & 4095] * emu8k->voice[c].lfo1_fmmod) >> 9;
                                lfo2_vibrato = (lfotable[(emu8k->voice[c].lfo2_count >> 8) & 4095] * emu8k->voice[c].lfo2_fmmod) >> 9;
                                
                                emu8k->voice[c].addr += freqtable[(emu8k->voice[c].pitch + lfo1_vibrato + lfo2_vibrato) & 0xffff];
                                if (emu8k->voice[c].addr >= emu8k->voice[c].loop_end)
                                        emu8k->voice[c].addr -= (emu8k->voice[c].loop_end - emu8k->voice[c].loop_start);

                                emu8k->voice[c].lfo1_count += (emu8k->voice[c].tremfrq & 0xff);
                                emu8k->voice[c].lfo2_count += (emu8k->voice[c].fm2frq2 & 0xff);
                        }
                }

                buf = &emu8k->buffer[emu8k->pos*2];
                
                for (pos = emu8k->pos; pos < new_pos; pos++)        
                {
                        buf[0] >>= 15;
                        buf[1] >>= 15;
        
                        if (buf[0] < -32768)
                                buf[0] = -32768;
                        else if (buf[0] > 32767)
                                buf[0] = 32767;
                
                        if (buf[1] < -32768)
                                buf[1] = -32768;
                        else if (buf[1] > 32767)
                                buf[1] = 32767;
                                
                        buf += 2;
                }

                emu8k->wc += (new_pos - emu8k->pos);
                
                emu8k->pos = new_pos;
        }
}

void emu8k_init(emu8k_t *emu8k, int onboard_ram)
{
        FILE *f;
        int c;
        double out;
        
        f = romfopen(L"roms/sound/awe32.raw", L"rb");
        if (!f)
                fatal("ROMS/SOUND/AWE32.RAW not found\n");
        
        if (onboard_ram)
        {
                /* Clip to 28MB, since that's the max that we can address. */
                if (onboard_ram > 0x7000) onboard_ram = 0x7000;
                emu8k->ram = malloc(onboard_ram * 1024);
                emu8k->ram_end_addr = EMU8K_RAM_MEM_START + ((onboard_ram * 1024) / 2);
        }
        else
        {
                emu8k->ram = 0;
                emu8k->ram_end_addr = EMU8K_RAM_MEM_START;
        }

        emu8k->rom = malloc(1024 * 1024);        
        
        fread(emu8k->rom, 1024 * 1024, 1, f);
        fclose(f);
        
        /*AWE-DUMP creates ROM images offset by 2 bytes, so if we detect this
          then correct it*/
        if (emu8k->rom[3] == 0x314d && emu8k->rom[4] == 0x474d)
        {
                memcpy(&emu8k->rom[0], &emu8k->rom[1], (1024 * 1024) - 2);
                emu8k->rom[0x7ffff] = 0;
        }
        io_sethandler(0x0620, 0x0004, emu8k_inb, emu8k_inw, NULL, emu8k_outb, emu8k_outw, NULL, emu8k);
        io_sethandler(0x0a20, 0x0004, emu8k_inb, emu8k_inw, NULL, emu8k_outb, emu8k_outw, NULL, emu8k);
        io_sethandler(0x0e20, 0x0004, emu8k_inb, emu8k_inw, NULL, emu8k_outb, emu8k_outw, NULL, emu8k);

        /*Create frequency table*/
        for (c = 0; c < 0x10000; c++)
        {
                freqtable[c] = (uint64_t)(exp2((double)(c - 0xe000) / 4096.0) * 65536.0 * 65536.0);
        }

        out = 65536.0;
        
        for (c = 0; c < 256; c++)
        {
                attentable[c] = (int)out;     
                out /= sqrt(1.09018); /*0.375 dB steps*/
        }
        
        out = 65536;
        
        for (c = 0; c < 4096; c++)
        {
                envtable[4095 - c] = (int)out;
                out /= 1.002709201; /*0.0235 dB Steps*/
        }
        
        for (c = 0; c < 4096; c++)
        {
                int d = (c + 1024) & 4095;
                if (d >= 2048)
                        lfotable[c] = 4096 - ((2048 - d) * 4);
                else
                        lfotable[c] = (d * 4) - 4096;
        }

        out = 125.0;
        for (c = 0; c < 256; c++)
        {
/*                filt_w0[c] = (int32_t)((2.0 * 3.142 * (out / 44100.0)) * 0.707 * 256.0);*/
/*                filt_w0[c] = 2.0 * 3.142 * (out / 44100.0);*/
                filt_w0[c] = (int32_t)(2.0 * 3.142 * (out / 44100.0) * 256.0);
                out *= 1.016378315;
        }
        
        emu8k->hwcf1 = 0x59;
        emu8k->hwcf2 = 0x20;
        emu8k->hwcf3 = 0x04;
}

void emu8k_close(emu8k_t *emu8k)
{
        free(emu8k->rom);
        free(emu8k->ram);
}
