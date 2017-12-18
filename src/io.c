/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implement I/O ports and their operations.
 *
 * Version:	@(#)io.c	1.0.1	2017/12/16
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "io.h"
#include "cpu/cpu.h"


#define NPORTS		65536		/* PC/AT supports 64K ports */


/*
 * This should be redone using a
 *
 * typedef struct _io_ {
 *	uint8_t  (*inb)(uint16_t addr, void *priv);
 *	uint16_t (*inw)(uint16_t addr, void *priv);
 *	uint32_t (*inl)(uint16_t addr, void *priv);
 *
 *	void     (*outb)(uint16_t addr, uint8_t  val, void *priv);
 *	void     (*outw)(uint16_t addr, uint16_t val, void *priv);
 *	void     (*outl)(uint16_t addr, uint32_t val, void *priv);
 *
 *	void	*priv;
 *
 *	struct _io_ *next;
 * } io_t;
 *
 * at some point. We keep one base entry per I/O port, and if
 * more than one entry is needed (some ports are like that),
 * we just add a handler to the list for that port.
 */
static uint8_t  (*port_inb[NPORTS][2])(uint16_t addr, void *priv);
static uint16_t (*port_inw[NPORTS][2])(uint16_t addr, void *priv);
static uint32_t (*port_inl[NPORTS][2])(uint16_t addr, void *priv);

static void (*port_outb[NPORTS][2])(uint16_t addr, uint8_t  val, void *priv);
static void (*port_outw[NPORTS][2])(uint16_t addr, uint16_t val, void *priv);
static void (*port_outl[NPORTS][2])(uint16_t addr, uint32_t val, void *priv);

static void *port_priv[NPORTS][2];


#ifdef IO_CATCH
static uint8_t null_inb(uint16_t addr, void *priv) { pclog("IO: read(%04x)\n"); return(0xff); }
static uint16_t null_inw(uint16_t addr, void *priv) { pclog("IO: readw(%04x)\n"); return(0xffff); }
static uint32_t null_inl(uint16_t addr, void *priv) { pclog("IO: readl(%04x)\n"); return(0xffffffff); }
static void null_outb(uint16_t addr, uint8_t val, void *priv) { pclog("IO: write(%04x, %02x)\n", val); }
static void null_outw(uint16_t addr, uint16_t val, void *priv) { pclog("IO: writew(%04x, %04x)\n", val); }
static void null_outl(uint16_t addr, uint32_t val, void *priv) { pclog("IO: writel(%04x, %08lx)\n", val); }
#endif


void
io_init(void)
{
    int c;

    pclog("IO: initializing\n");
    for (c=0; c<NPORTS; c++) {
#ifdef IO_CATCH
	port_inb[c][0]  = port_inb[c][1]  = null_inb;
	port_outb[c][0] = port_outb[c][1] = null_outb;
	port_inw[c][0]  = port_inw[c][1]  = null_inw;
	port_outw[c][0] = port_outw[c][1] = null_outw;
	port_inl[c][0]  = port_inl[c][1]  = null_inl;
	port_outl[c][0] = port_outl[c][1] = null_outl;
#else
	port_inb[c][0]  = port_inb[c][1]  = NULL;
	port_outb[c][0] = port_outb[c][1] = NULL;
	port_inw[c][0]  = port_inw[c][1]  = NULL;
	port_outw[c][0] = port_outw[c][1] = NULL;
	port_inl[c][0]  = port_inl[c][1]  = NULL;
	port_outl[c][0] = port_outl[c][1] = NULL;
#endif
	port_priv[c][0] = port_priv[c][1] = NULL;
    }
}


void
io_sethandler(uint16_t base, int size, 
	      uint8_t (*inb)(uint16_t addr, void *priv),
	      uint16_t (*inw)(uint16_t addr, void *priv),
	      uint32_t (*inl)(uint16_t addr, void *priv),
	      void (*outb)(uint16_t addr, uint8_t val, void *priv),
	      void (*outw)(uint16_t addr, uint16_t val, void *priv),
	      void (*outl)(uint16_t addr, uint32_t val, void *priv),
	      void *priv)
{
    int c;

    for (c=0; c<size; c++) {
	if (!port_inb[base+c][0] && !port_inw[base+c][0] &&
	    !port_inl[base+c][0] && !port_outb[base+c][0] &&
	    !port_outw[base+c][0] && !port_outl[base+c][0]) {
		port_inb[base+c][0] = inb;
		port_inw[base+c][0] = inw;
		port_inl[base+c][0] = inl;

		port_outb[base+c][0] = outb;
		port_outw[base+c][0] = outw;
		port_outl[base+c][0] = outl;

		port_priv[base+c][0] = priv;
	} else if (!port_inb[base+c][1] && !port_inw[base+c][1] &&
		   !port_inl[base+c][1] && !port_outb[base+c][1] &&
		   !port_outw[base+c][1] && !port_outl[base+c][1]) {
		port_inb[base+c][1] = inb;
		port_inw[base+c][1] = inw;
		port_inl[base+c][1] = inl;

		port_outb[base+c][1] = outb;
		port_outw[base+c][1] = outw;
		port_outl[base+c][1] = outl;

		port_priv[base+c][1] = priv;
	}
    }
}


void
io_removehandler(uint16_t base, int size,
	uint8_t (*inb)(uint16_t addr, void *priv),
	uint16_t (*inw)(uint16_t addr, void *priv),
	uint32_t (*inl)(uint16_t addr, void *priv),
	void (*outb)(uint16_t addr, uint8_t val, void *priv),
	void (*outw)(uint16_t addr, uint16_t val, void *priv),
	void (*outl)(uint16_t addr, uint32_t val, void *priv),
	void *priv)
{
    int c;

    for (c=0; c<size; c++) {
	if (port_priv[base+c][0] == priv) {
		if (port_inb[base+c][0] == inb)
			port_inb[ base + c][0] = NULL;
		if (port_inw[base+c][0] == inw)
			port_inw[ base + c][0] = NULL;
		if (port_inl[base+c][0] == inl)
			port_inl[base+c][0] = NULL;
		if (port_outb[base+c][0] == outb)
			port_outb[base + c][0] = NULL;
		if (port_outw[base+c][0] == outw)
			port_outw[base + c][0] = NULL;
		if (port_outl[base+c][0] == outl)
			port_outl[base+c][0] = NULL;
		port_priv[base+c][0] = NULL;
	}
	if (port_priv[base+c][1] == priv) {
		if (port_inb[base+c][1] == inb)
		port_inb[ base+c][1] = NULL;
		if (port_inw[base+c][1] == inw)
		port_inw[ base+c][1] = NULL;
		if (port_inl[base+c][1] == inl)
		port_inl[base+c][1] = NULL;
		if (port_outb[base+c][1] == outb)
		port_outb[base+c][1] = NULL;
		if (port_outw[base+c][1] == outw)
		port_outw[base+c][1] = NULL;
		if (port_outl[base+c][1] == outl)
		port_outl[base+c][1] = NULL;
		port_priv[base+c][1] = NULL;
	}
    }
}


#ifdef PC98
void
io_sethandler_interleaved(uint16_t base, int size,
	uint8_t (*inb)(uint16_t addr, void *priv),
	uint16_t (*inw)(uint16_t addr, void *priv),
	uint32_t (*inl)(uint16_t addr, void *priv),
	void (*outb)(uint16_t addr, uint8_t val, void *priv),
	void (*outw)(uint16_t addr, uint16_t val, void *priv),
	void (*outl)(uint16_t addr, uint32_t val, void *priv),
	void *priv)
{
    int c;

    size <<= 2;
    for (c=0; c<size; c+=2) {
	if (!port_inb[base+c][0] && !port_inw[base+c][0] &&
	    !port_inl[base+c][0] && !port_outb[base+c][0] &&
	    !port_outw[base+c][0] && !port_outl[base+c][0]) {
		port_inb[ base + c][0] = inb;
		port_inw[ base + c][0] = inw;
		port_inl[ base + c][0] = inl;
		port_outb[base + c][0] = outb;
		port_outw[base + c][0] = outw;
		port_outl[base + c][0] = outl;
		port_priv[base + c][0] = priv;
	} else if (!port_inb[base+c][1] && !port_inw[base+c][1] &&
		   !port_inl[base+c][1] && !port_outb[base+c][1] &&
		   !port_outw[base+c][1] && !port_outl[base+c][1]) {
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


void
io_removehandler_interleaved(uint16_t base, int size,
	uint8_t (*inb)(uint16_t addr, void *priv),
	uint16_t (*inw)(uint16_t addr, void *priv),
	uint32_t (*inl)(uint16_t addr, void *priv),
	void (*outb)(uint16_t addr, uint8_t val, void *priv),
	void (*outw)(uint16_t addr, uint16_t val, void *priv),
	void (*outl)(uint16_t addr, uint32_t val, void *priv),
	void *priv)
{
    int c;

    size <<= 2;
    for (c=0; c<size; c+=2) {
	if (port_priv[base+c][0] == priv) {
		if (port_inb[base+c][0] == inb)
			port_inb[base+c][0] = NULL;
		if (port_inw[base+c][0] == inw)
			port_inw[base+c][0] = NULL;
		if (port_inl[base+c][0] == inl)
			port_inl[base+c][0] = NULL;
		if (port_outb[base+c][0] == outb)
			port_outb[base+c][0] = NULL;
		if (port_outw[base+c][0] == outw)
			port_outw[base+c][0] = NULL;
		if (port_outl[base+c][0] == outl)
			port_outl[base+c][0] = NULL;
		port_priv[base+c][0] = NULL;
	}
	if (port_priv[base+c][1] == priv) {
		if (port_inb[base+c][1] == inb)
			port_inb[base+c][1] = NULL;
		if (port_inw[base+c][1] == inw)
			port_inw[base+c][1] = NULL;
		if (port_inl[base+c][1] == inl)
			port_inl[base+c][1] = NULL;
		if (port_outb[base+c][1] == outb)
			port_outb[base+c][1] = NULL;
		if (port_outw[base+c][1] == outw)
			port_outw[base+c][1] = NULL;
		if (port_outl[base+c][1] == outl)
			port_outl[base+c][1] = NULL;
		port_priv[base+c][1] = NULL;
	}
    }
}
#endif


uint8_t
inb(uint16_t port)
{
    uint8_t r = 0xff;

    if (port_inb[port][0])
	r &= port_inb[port][0](port, port_priv[port][0]);
    if (port_inb[port][1])
	r &= port_inb[port][1](port, port_priv[port][1]);

#ifdef IO_TRACE
    if (CS == IO_TRACE)
	pclog("IOTRACE(%04X): inb(%04x)=%02x\n", IO_TRACE, port, r);
#endif

    return(r);
}


void
outb(uint16_t port, uint8_t val)
{
    if (port_outb[port][0])
	port_outb[port][0](port, val, port_priv[port][0]);
    if (port_outb[port][1])
	port_outb[port][1](port, val, port_priv[port][1]);
	
#ifdef IO_TRACE
    if (CS == IO_TRACE)
	pclog("IOTRACE(%04X): outb(%04x,%02x)\n", IO_TRACE, port, val);
#endif
}


uint16_t
inw(uint16_t port)
{
    if (port_inw[port][0])
	return(port_inw[port][0](port, port_priv[port][0]));
    if (port_inw[port][1])
	return(port_inw[port][1](port, port_priv[port][1]));

    return(inb(port) | (inb(port + 1) << 8));
}


void
outw(uint16_t port, uint16_t val)
{
    if (port_outw[port][0]) {
	port_outw[port][0](port, val, port_priv[port][0]);
	return;
    }
    if (port_outw[port][1]) {
	port_outw[port][1](port, val, port_priv[port][1]);
	return;
    }

    outb(port,val);
    outb(port+1,val>>8);
}


uint32_t
inl(uint16_t port)
{
    if (port_inl[port][0])
	return(port_inl[port][0](port, port_priv[port][0]));
    if (port_inl[port][1])
	return(port_inl[port][1](port, port_priv[port][1]));

    return(inw(port) | (inw(port + 2) << 16));
}


void
outl(uint16_t port, uint32_t val)
{
    if (port_outl[port][0]) {
	port_outl[port][0](port, val, port_priv[port][0]);
	return;
    }
    if (port_outl[port][1]) {
	port_outl[port][1](port, val, port_priv[port][1]);
	return;
    }

    outw(port, val);
    outw(port + 2, val >> 16);
}
