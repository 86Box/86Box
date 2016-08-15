/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
//Quake timedemo demo1 - 8.1FPS

//11A00 - D_SCAlloc
//11C1C - D_CacheSurface

//36174 - SCR_CalcRefdef

//SCR_CalcRefdef
//Calls R_SetVrect and R_ViewChanged

#define fplog 0

#include <math.h>
#include "ibm.h"
#include "pic.h"
#include "x86.h"
#include "x86_flags.h"
#include "x86_ops.h"
#include "x87.h"
#include "386_common.h"

double ST[8];
uint64_t ST_i64[8];
MMX_REG MM[8];
int ismmx;
uint16_t npxs,npxc;
uint8_t tag[8];

int TOP;

uint16_t x87_gettag()
{
        uint16_t ret = 0;
        int c;
        
        for (c = 0; c < 8; c++)
        {
                if (tag[c] & TAG_UINT64)
                        ret |= 2 << (c*2);
                else
                        ret |= (tag[c] << (c*2));
        }

        return ret;
}

void x87_settag(uint16_t new_tag)
{
        tag[0] = new_tag & 3;
        tag[1] = (new_tag >> 2) & 3;
        tag[2] = (new_tag >> 4) & 3;
        tag[3] = (new_tag >> 6) & 3;
        tag[4] = (new_tag >> 8) & 3;
        tag[5] = (new_tag >> 10) & 3;
        tag[6] = (new_tag >> 12) & 3;
        tag[7] = (new_tag >> 14) & 3;
}

void x87_dumpregs()
{
        if (ismmx)
        {
                pclog("MM0=%016llX\tMM1=%016llX\tMM2=%016llX\tMM3=%016llX\n", MM[0].q, MM[1].q, MM[2].q, MM[3].q);
                pclog("MM4=%016llX\tMM5=%016llX\tMM6=%016llX\tMM7=%016llX\n", MM[4].q, MM[5].q, MM[6].q, MM[7].q);
        }
        else
        {
                pclog("ST(0)=%f\tST(1)=%f\tST(2)=%f\tST(3)=%f\t\n",ST[TOP],ST[(TOP+1)&7],ST[(TOP+2)&7],ST[(TOP+3)&7]);
                pclog("ST(4)=%f\tST(5)=%f\tST(6)=%f\tST(7)=%f\t\n",ST[(TOP+4)&7],ST[(TOP+5)&7],ST[(TOP+6)&7],ST[(TOP+7)&7]);
        }
        pclog("Status = %04X  Control = %04X  Tag = %04X\n",npxs,npxc,x87_gettag());
}

void x87_print()
{
        if (ismmx)
        {
                pclog("\tMM0=%016llX\tMM1=%016llX\tMM2=%016llX\tMM3=%016llX\t", MM[0].q, MM[1].q, MM[2].q, MM[3].q);
                pclog("MM4=%016llX\tMM5=%016llX\tMM6=%016llX\tMM7=%016llX\n", MM[4].q, MM[5].q, MM[6].q, MM[7].q);
        }
        else
        {
                pclog("\tST(0)=%.20f\tST(1)=%.20f\tST(2)=%f\tST(3)=%f\t",ST[TOP&7],ST[(TOP+1)&7],ST[(TOP+2)&7],ST[(TOP+3)&7]);
                pclog("ST(4)=%f\tST(5)=%f\tST(6)=%f\tST(7)=%f\t TOP=%i CR=%04X SR=%04X TAG=%04X\n",ST[(TOP+4)&7],ST[(TOP+5)&7],ST[(TOP+6)&7],ST[(TOP+7)&7], TOP, npxc, npxs, x87_gettag());
        }
}

void x87_reset()
{
}
