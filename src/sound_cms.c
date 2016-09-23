#include <stdio.h>
#include <stdlib.h>
#include "ibm.h"

#include "device.h"
#include "io.h"
#include "sound.h"
#include "sound_cms.h"

typedef struct cms_t
{
        int addrs[2];
        uint8_t regs[2][32];
        uint16_t latch[2][6];
        int freq[2][6];
        float count[2][6];
        int vol[2][6][2];
        int stat[2][6];
        uint16_t noise[2][2];
        uint16_t noisefreq[2][2];
        int noisecount[2][2];
        int noisetype[2][2];

        int16_t buffer[SOUNDBUFLEN * 2];

        int pos;
} cms_t;

void cms_update(cms_t *cms)
{
        for (; cms->pos < sound_pos_global; cms->pos++)
        {
                int c, d;
                int16_t out_l = 0, out_r = 0;

                for (c = 0; c < 4; c++)
                {
                        switch (cms->noisetype[c >> 1][c & 1])
                        {
                                case 0: cms->noisefreq[c >> 1][c & 1] = 31250; break;
                                case 1: cms->noisefreq[c >> 1][c & 1] = 15625; break;
                                case 2: cms->noisefreq[c >> 1][c & 1] = 7812; break;
                                case 3: cms->noisefreq[c >> 1][c & 1] = cms->freq[c >> 1][(c & 1) * 3]; break;
                        }
                }
                for (c = 0; c < 2; c ++)
                {
                        if (cms->regs[c][0x1C] & 1)
                        {
                                for (d = 0; d < 6; d++)
                                {
                                        if (cms->regs[c][0x14] & (1 << d))
                                        {
                                                if (cms->stat[c][d]) out_l += (cms->vol[c][d][0] * 90);
                                                if (cms->stat[c][d]) out_r += (cms->vol[c][d][1] * 90);
                                                cms->count[c][d] += cms->freq[c][d];
                                                if (cms->count[c][d] >= 24000)
                                                {
                                                        cms->count[c][d] -= 24000;
                                                        cms->stat[c][d] ^= 1;
                                                }
                                        }
                                        else if (cms->regs[c][0x15] & (1 << d))
                                        {
                                                if (cms->noise[c][d / 3] & 1) out_l += (cms->vol[c][d][0] * 90);
                                                if (cms->noise[c][d / 3] & 1) out_r += (cms->vol[c][d][0] * 90);
                                        }
                                }
                                for (d = 0; d < 2; d++)
                                {
                                        cms->noisecount[c][d] += cms->noisefreq[c][d];
                                        while (cms->noisecount[c][d] >= 24000)
                                        {
                                                cms->noisecount[c][d] -= 24000;
                                                cms->noise[c][d] <<= 1;
                                                if (!(((cms->noise[c][d] & 0x4000) >> 8) ^ (cms->noise[c][d] & 0x40))) 
                                                        cms->noise[c][d] |= 1;
                                        }
                                }
                        }
                }
                cms->buffer[(cms->pos << 1)] = out_l;
                cms->buffer[(cms->pos << 1) + 1] = out_r;
        }
}

void cms_get_buffer(int32_t *buffer, int len, void *p)
{
        cms_t *cms = (cms_t *)p;
        
        int c;

        cms_update(cms);
        
        for (c = 0; c < len * 2; c++)
                buffer[c] += cms->buffer[c];

        cms->pos = 0;
}

void cms_write(uint16_t addr, uint8_t val, void *p)
{
        cms_t *cms = (cms_t *)p;
        int voice;
        int chip = (addr & 2) >> 1;
        
        pclog("cms_write : addr %04X val %02X\n", addr, val);
        
        if (addr & 1)
           cms->addrs[chip] = val & 31;
        else
        {
                cms_update(cms);
                cms->regs[chip][cms->addrs[chip] & 31] = val;
                switch (cms->addrs[chip] & 31)
                {
                        case 0x00: case 0x01: case 0x02: /*Volume*/
                        case 0x03: case 0x04: case 0x05:
                        voice = cms->addrs[chip] & 7;
                        cms->vol[chip][voice][0] = val & 0xf;
                        cms->vol[chip][voice][1] = val >> 4;
                        break;
                        case 0x08: case 0x09: case 0x0A: /*Frequency*/
                        case 0x0B: case 0x0C: case 0x0D:
                        voice = cms->addrs[chip] & 7;
                        cms->latch[chip][voice] = (cms->latch[chip][voice] & 0x700) | val;
                        cms->freq[chip][voice] = (15625 << (cms->latch[chip][voice] >> 8)) / (511 - (cms->latch[chip][voice] & 255));
                        break;
                        case 0x10: case 0x11: case 0x12: /*Octave*/
                        voice = (cms->addrs[chip] & 3) << 1;
                        cms->latch[chip][voice] = (cms->latch[chip][voice] & 0xFF) | ((val & 7) << 8);
                        cms->latch[chip][voice + 1] = (cms->latch[chip][voice + 1] & 0xFF) | ((val & 0x70) << 4);
                        cms->freq[chip][voice] = (15625 << (cms->latch[chip][voice] >> 8)) / (511 - (cms->latch[chip][voice] & 255));
                        cms->freq[chip][voice + 1] = (15625 << (cms->latch[chip][voice + 1] >> 8)) / (511 - (cms->latch[chip][voice + 1] & 255));
                        break;
                        case 0x16: /*Noise*/
                        cms->noisetype[chip][0] = val & 3;
                        cms->noisetype[chip][1] = (val >> 4) & 3;
                        break;
                }
        }
}

uint8_t cms_read(uint16_t addr, void *p)
{
        cms_t *cms = (cms_t *)p;
        int chip = (addr & 2) >> 1;
        
        if (addr & 1) 
                return cms->addrs[chip];
                
        return cms->regs[chip][cms->addrs[chip] & 31];
}

void *cms_init()
{
        cms_t *cms = malloc(sizeof(cms_t));
        memset(cms, 0, sizeof(cms_t));

        pclog("cms_init\n");
        io_sethandler(0x0220, 0x0004, cms_read, NULL, NULL, cms_write, NULL, NULL, cms);
        sound_add_handler(cms_get_buffer, cms);
        return cms;
}

void cms_close(void *p)
{
        cms_t *cms = (cms_t *)p;
        
        free(cms);
}

device_t cms_device =
{
        "Creative Music System / Game Blaster",
        0,
        cms_init,
        cms_close,
        NULL,
        NULL,
        NULL,
        NULL
};
