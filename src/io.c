/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include "ibm.h"
#include "ide.h"
#include "io.h"
#include "video.h"
#include "cpu.h"

uint8_t  (*port_inb[0x10000][2])(uint16_t addr, void *priv);
uint16_t (*port_inw[0x10000][2])(uint16_t addr, void *priv);
uint32_t (*port_inl[0x10000][2])(uint16_t addr, void *priv);

void (*port_outb[0x10000][2])(uint16_t addr, uint8_t  val, void *priv);
void (*port_outw[0x10000][2])(uint16_t addr, uint16_t val, void *priv);
void (*port_outl[0x10000][2])(uint16_t addr, uint32_t val, void *priv);

void *port_priv[0x10000][2];

void io_init()
{
        int c;
        pclog("io_init\n");
        for (c = 0; c < 0x10000; c++)
        {
                port_inb[c][0]  = port_inw[c][0]  = port_inl[c][0]  = NULL;
                port_outb[c][0] = port_outw[c][0] = port_outl[c][0] = NULL;
                port_inb[c][1]  = port_inw[c][1]  = port_inl[c][1]  = NULL;
                port_outb[c][1] = port_outw[c][1] = port_outl[c][1] = NULL;
                port_priv[c][0] = port_priv[c][1] = NULL;
        }
}

void io_sethandler(uint16_t base, int size, 
                   uint8_t  (*inb)(uint16_t addr, void *priv), 
                   uint16_t (*inw)(uint16_t addr, void *priv), 
                   uint32_t (*inl)(uint16_t addr, void *priv), 
                   void (*outb)(uint16_t addr, uint8_t  val, void *priv),
                   void (*outw)(uint16_t addr, uint16_t val, void *priv),
                   void (*outl)(uint16_t addr, uint32_t val, void *priv),
                   void *priv)
{
        int c;
        for (c = 0; c < size; c++)
        {
                if (!port_inb[ base + c][0] && !port_inw[ base + c][0] && !port_inl[ base + c][0] &&
                    !port_outb[base + c][0] && !port_outw[base + c][0] && !port_outl[base + c][0])
                {
                        port_inb[ base + c][0] = inb;
                        port_inw[ base + c][0] = inw;
                        port_inl[ base + c][0] = inl;
                        port_outb[base + c][0] = outb;
                        port_outw[base + c][0] = outw;
                        port_outl[base + c][0] = outl;
                        port_priv[base + c][0] = priv;
                }
                else if (!port_inb[ base + c][1] && !port_inw[ base + c][1] && !port_inl[ base + c][1] &&
                         !port_outb[base + c][1] && !port_outw[base + c][1] && !port_outl[base + c][1])
                {
                        port_inb[ base + c][1] = inb;
                        port_inw[ base + c][1] = inw;
                        port_inl[ base + c][1] = inl;
                        port_outb[base + c][1] = outb;
                        port_outw[base + c][1] = outw;
                        port_outl[base + c][1] = outl;
                        port_priv[base + c][1] = priv;
                }
        }
}

void io_removehandler(uint16_t base, int size, 
                   uint8_t  (*inb)(uint16_t addr, void *priv), 
                   uint16_t (*inw)(uint16_t addr, void *priv), 
                   uint32_t (*inl)(uint16_t addr, void *priv), 
                   void (*outb)(uint16_t addr, uint8_t  val, void *priv),
                   void (*outw)(uint16_t addr, uint16_t val, void *priv),
                   void (*outl)(uint16_t addr, uint32_t val, void *priv),
                   void *priv)
{
        int c;
        for (c = 0; c < size; c++)
        {
                if (port_priv[base + c][0] == priv)
                {
                        if (port_inb[ base + c][0] == inb)
                           port_inb[ base + c][0] = NULL;
                        if (port_inw[ base + c][0] == inw)
                           port_inw[ base + c][0] = NULL;
                        if (port_inl[ base + c][0] == inl)
                           port_inl[ base + c][0] = NULL;
                        if (port_outb[ base + c][0] == outb)
                           port_outb[ base + c][0] = NULL;
                        if (port_outw[ base + c][0] == outw)
                           port_outw[ base + c][0] = NULL;
                        if (port_outl[ base + c][0] == outl)
                           port_outl[ base + c][0] = NULL;
                }
                if (port_priv[base + c][1] == priv)
                {
                        if (port_inb[ base + c][1] == inb)
                           port_inb[ base + c][1] = NULL;
                        if (port_inw[ base + c][1] == inw)
                           port_inw[ base + c][1] = NULL;
                        if (port_inl[ base + c][1] == inl)
                           port_inl[ base + c][1] = NULL;
                        if (port_outb[ base + c][1] == outb)
                           port_outb[ base + c][1] = NULL;
                        if (port_outw[ base + c][1] == outw)
                           port_outw[ base + c][1] = NULL;
                        if (port_outl[ base + c][1] == outl)
                           port_outl[ base + c][1] = NULL;
                }
        }
}

uint8_t cgamode,cgastat=0,cgacol;
int hsync;
uint8_t lpt2dat;
int sw9;
int t237=0;
uint8_t inb(uint16_t port)
{
        uint8_t temp = 0xff;

        if (port_inb[port][0])
           temp &= port_inb[port][0](port, port_priv[port][0]);
        if (port_inb[port][1])
           temp &= port_inb[port][1](port, port_priv[port][1]);
           
           /* if (!port_inb[port][0] && !port_inb[port][1])
           	pclog("Bad INB %04X %04X:%04X\n", port, CS, cpu_state.pc); */
           	
           /* if (port_inb[port][0] || port_inb[port][1])
           	pclog("Good INB %04X %04X:%04X\n", port, CS, cpu_state.pc); */
           	
        return temp;
}

uint8_t cpu_readport(uint32_t port) { return inb(port); }

void outb(uint16_t port, uint8_t val)
{
        if (port_outb[port][0])
           port_outb[port][0](port, val, port_priv[port][0]);
        if (port_outb[port][1])
           port_outb[port][1](port, val, port_priv[port][1]);
        
        /* if (!port_outb[port][0] && !port_outb[port][1])
        	pclog("Bad OUTB %04X %02X %04X:%08X\n", port, val, CS, cpu_state.pc); */

        /* if (port_outb[port][0] || port_outb[port][1])
        	pclog("Good OUTB %04X %02X %04X:%08X\n", port, val, CS, cpu_state.pc); */

        return;
}

uint16_t inw(uint16_t port)
{
//        pclog("INW %04X\n", port);
        if (port_inw[port][0])
           return port_inw[port][0](port, port_priv[port][0]);
        if (port_inw[port][1])
           return port_inw[port][1](port, port_priv[port][1]);
           
        return inb(port) | (inb(port + 1) << 8);
}

void outw(uint16_t port, uint16_t val)
{
//        printf("OUTW %04X %04X %04X:%08X\n",port,val, CS, pc);
/*        if ((port & ~0xf) == 0xf000)
           pclog("OUTW %04X %04X\n", port, val);*/

        if (port_outw[port][0])
           port_outw[port][0](port, val, port_priv[port][0]);
        if (port_outw[port][1])
           port_outw[port][1](port, val, port_priv[port][1]);

        if (port_outw[port][0] || port_outw[port][1])
           return;

        outb(port,val);
        outb(port+1,val>>8);
}

uint32_t inl(uint16_t port)
{
//        pclog("INL %04X\n", port);
        if (port_inl[port][0])
           return port_inl[port][0](port, port_priv[port][0]);
        if (port_inl[port][1])
           return port_inl[port][1](port, port_priv[port][1]);
           
        return inw(port) | (inw(port + 2) << 16);
}

void outl(uint16_t port, uint32_t val)
{
/*        if ((port & ~0xf) == 0xf000)
           pclog("OUTL %04X %08X\n", port, val);*/

        if (port_outl[port][0])
           port_outl[port][0](port, val, port_priv[port][0]);
        if (port_outl[port][1])
           port_outl[port][1](port, val, port_priv[port][1]);

        if (port_outl[port][0] || port_outl[port][1])
           return;
                
        outw(port, val);
        outw(port + 2, val >> 16);
}
