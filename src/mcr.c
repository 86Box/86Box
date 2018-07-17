/*INTEL 82355 MCR emulation
  This chip was used as part of many 386 chipsets
  It controls memory addressing and shadowing*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "cpu/cpu.h"
#include "mem.h"


int nextreg6;
uint8_t mcr22;
int mcrlock,mcrfirst;


void resetmcr(void)
{
        mcrlock=0;
        mcrfirst=1;
        shadowbios=0;
}

void writemcr(uint16_t addr, uint8_t val)
{
        switch (addr)
        {
                case 0x22:
                if (val==6 && mcr22==6) nextreg6=1;
                else                    nextreg6=0;
                break;
                case 0x23:
                if (nextreg6) shadowbios=!val;
                break;
        }
        mcr22=val;
}

