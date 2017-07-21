/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdio.h>
#include <string.h>
#include "ibm.h"
#include "cpu/cpu.h"
#include "io.h"
#include "device.h"
#include "model.h"


uint8_t europcdat[16];

struct 
{
        uint8_t dat[16];
        int stat;
        int addr;
} europc_rtc;


static void writejim(uint16_t addr, uint8_t val, void *p)
{
        if ((addr&0xFF0)==0x250) europcdat[addr&0xF]=val;
        switch (addr)
        {
                case 0x25A:
                switch (europc_rtc.stat)
                {
                        case 0:
                        europc_rtc.addr=val&0xF;
                        europc_rtc.stat++;
                        break;
                        case 1:
                        europc_rtc.dat[europc_rtc.addr]=(europc_rtc.dat[europc_rtc.addr]&0xF)|(val<<4);
                        europc_rtc.stat++;
                        break;
                        case 2:
                        europc_rtc.dat[europc_rtc.addr]=(europc_rtc.dat[europc_rtc.addr]&0xF0)|(val&0xF);
                        europc_rtc.stat=0;
                        break;
                }
                break;
        }
}


static uint8_t readjim(uint16_t addr, void *p)
{
        switch (addr)
        {
                case 0x250: case 0x251: case 0x252: case 0x253: return 0;
                case 0x254: case 0x255: case 0x256: case 0x257: return europcdat[addr&0xF];
                case 0x25A:
                if (europc_rtc.stat==1)
                {
                        europc_rtc.stat=2;
                        return europc_rtc.dat[europc_rtc.addr]>>4;
                }
                if (europc_rtc.stat==2)
                {
                        europc_rtc.stat=0;
                        return europc_rtc.dat[europc_rtc.addr]&0xF;
                }
                return 0;
        }
        return 0;
}


void jim_init(void)
{
        uint8_t viddat;
        memset(europc_rtc.dat,0,16);
        europc_rtc.dat[0xF]=1;
        europc_rtc.dat[3]=1;
        europc_rtc.dat[4]=1;
        europc_rtc.dat[5]=0x88;
        if (gfxcard==GFX_CGA || gfxcard == GFX_COLORPLUS) viddat=0x12;
        else if (gfxcard==GFX_MDA || gfxcard==GFX_HERCULES || gfxcard==GFX_INCOLOR) viddat=3;
        else viddat=0x10;
        europc_rtc.dat[0xB]=viddat;
        europc_rtc.dat[0xD]=viddat; /*Checksum*/
        io_sethandler(0x250, 0x10, readjim, NULL, NULL, writejim, NULL, NULL, NULL);
}
