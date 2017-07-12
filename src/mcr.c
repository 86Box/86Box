/*INTEL 82355 MCR emulation
  This chip was used as part of many 386 chipsets
  It controls memory addressing and shadowing*/
#include "ibm.h"

int nextreg6;
uint8_t mcr22;
int mcrlock,mcrfirst;
void resetmcr()
{
        mcrlock=0;
        mcrfirst=1;
        shadowbios=0;
}

void writemcr(uint16_t addr, uint8_t val)
{
        printf("Write MCR %04X %02X %04X:%04X\n",addr,val,CS,cpu_state.pc);
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

