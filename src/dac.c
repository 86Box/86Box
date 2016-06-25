#include "ibm.h"

uint8_t dac,dac2;
uint8_t dacctrl;
int lptfifo;
uint8_t dssbuffer[16];
int dssstart=0,dssend=0;
int dssmode=0;

void writedac(uint16_t addr, uint8_t val)
{
        if (dssmode) dac2=val;
        else         dac=val;
}

void writedacctrl(uint16_t addr, uint8_t val)
{
//        printf("Write DAC ctrl %02X %i\n",val,lptfifo);
        if (dacctrl&8 && !(val&8) && (lptfifo!=16))
        {
//                dac=dac2;
                dssbuffer[dssend++]=dac2;
                dssend&=15;
                lptfifo++;
        }
        dacctrl=val;
}

uint8_t readdacfifo()
{
        if (lptfifo==16) return 0x40;
        return 0;
}

void pollss()
{
        if (lptfifo)
        {
                dac=dssbuffer[dssstart++];
                dssstart&=15;
                lptfifo--;
        }
}

int16_t dacbuffer[SOUNDBUFLEN+20];
int dacbufferpos=0;
void getdacsamp()
{
        if (dacbufferpos<SOUNDBUFLEN+20) dacbuffer[dacbufferpos++]=(((int)(unsigned int)dac)-0x80)*0x20;
}

void adddac(int16_t *p)
{
        int c;
        if (dacbufferpos>SOUNDBUFLEN) dacbufferpos=SOUNDBUFLEN;
        for (c=0;c<dacbufferpos;c++)
        {
                p[c<<1]+=(dacbuffer[c]);
                p[(c<<1)+1]+=(dacbuffer[c]);
        }
        for (;c<SOUNDBUFLEN;c++)
        {
                p[c<<1]+=(dacbuffer[dacbufferpos-1]);
                p[(c<<1)+1]+=(dacbuffer[dacbufferpos-1]);
        }
        dacbufferpos=0;
}

