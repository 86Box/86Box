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
 * Version:	@(#)io.c	1.0.3	2018/02/02
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "io.h"
#include "cpu/cpu.h"


#define NPORTS		65536		/* PC/AT supports 64K ports */


typedef struct _io_ {
	uint8_t  (*inb)(uint16_t addr, void *priv);
	uint16_t (*inw)(uint16_t addr, void *priv);
	uint32_t (*inl)(uint16_t addr, void *priv);

	void     (*outb)(uint16_t addr, uint8_t  val, void *priv);
	void     (*outw)(uint16_t addr, uint16_t val, void *priv);
	void     (*outl)(uint16_t addr, uint32_t val, void *priv);

	void	*priv;

	struct _io_ *prev, *next;
} io_t;

int initialized = 0;
io_t *io[NPORTS], *io_last[NPORTS];


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
    io_t *p, *q;

    if (!initialized) {
	for (c=0; c<NPORTS; c++)
		io[c] = io_last[c] = NULL;
	initialized = 1;
    }

    for (c=0; c<NPORTS; c++) {
        if (io_last[c]) {
		/* Port c has at least one handler. */
		p = io_last[c];
		/* After this loop, p will have the pointer to the first handler. */
		while (p) {
			q = p->prev;
			free(p);
			p = q;
		}
		p = NULL;
	}

#ifdef IO_CATCH
	/* io[c] should be the only handler, pointing at the NULL catch handler. */
	p = (io_t *) malloc(sizeof(io_t));
	memset(p, 0, sizeof(io_t));
	io[c] = io_last[c] = p;
	p->next = NULL;
	p->prev = NULL;
	p->inb  = null_inb;
	p->outb = null_outb;
	p->inw  = null_inw;
	p->outw = null_outw;
	p->inl  = null_inl;
	p->outl = null_outl;
	p->priv = NULL;
#else
	/* io[c] should be NULL. */
	io[c] = io_last[c] = NULL;
#endif
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
    io_t *p, *q = NULL;

    for (c = 0; c < size; c++) {
	p = io_last[base + c];
	q = (io_t *) malloc(sizeof(io_t));
	memset(q, 0, sizeof(io_t));
	if (p) {
		p->next = q;
		q->prev = p;
	} else {
		io[base + c] = q;
		q->prev = NULL;
	}

	q->inb = inb;
	q->inw = inw;
	q->inl = inl;

	q->outb = outb;
	q->outw = outw;
	q->outl = outl;

	q->priv = priv;
	q->next = NULL;

	io_last[base + c] = q;
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
    io_t *p;

    for (c = 0; c < size; c++) {
	p = io[base + c];
	if (!p)
		continue;
	while(p) {
		if ((p->inb == inb) && (p->inw == inw) &&
		    (p->inl == inl) && (p->outb == outb) &&
		    (p->outw == outw) && (p->outl == outl) &&
		    (p->priv == priv)) {
			if (p->prev)
				p->prev->next = p->next;
			else
				io[base + c] = p->next;
			if (p->next)
				p->next->prev = p->prev;
			else
				io_last[base + c] = p->prev;
			free(p);
			p = NULL;
			break;
		}
		p = p->next;
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
    io_t *p, *q;

    size <<= 2;
    for (c=0; c<size; c+=2) {
	p = last_handler(base + c);
	q = (io_t *) malloc(sizeof(io_t));
	memset(q, 0, sizeof(io_t));
	if (p) {
		p->next = q;
		q->prev = p;
	} else {
		io[base + c] = q;
		q->prev = NULL;
	}

	q->inb = inb;
	q->inw = inw;
	q->inl = inl;

	q->outb = outb;
	q->outw = outw;
	q->outl = outl;

	q->priv = priv;
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
    io_t *p;

    size <<= 2;
    for (c = 0; c < size; c += 2) {
	p = io[base + c];
	if (!p)
		return;
	while(p) {
		if ((p->inb == inb) && (p->inw == inw) &&
		    (p->inl == inl) && (p->outb == outb) &&
		    (p->outw == outw) && (p->outl == outl) &&
		    (p->priv == priv)) {
			if (p->prev)
				p->prev->next = p->next;
			if (p->next)
				p->next->prev = p->prev;
			free(p);
			break;
		}
		p = p->next;
	}
    }
}
#endif


uint8_t
inb(uint16_t port)
{
    uint8_t r = 0xff;
    io_t *p;

    p = io[port];
    if (p) {
	while(p) {
		if (p->inb)
			r &= p->inb(port, p->priv);
		p = p->next;
	}
    }

#ifdef IO_TRACE
    if (CS == IO_TRACE)
	pclog("IOTRACE(%04X): inb(%04x)=%02x\n", IO_TRACE, port, r);
#endif

    return(r);
}


void
outb(uint16_t port, uint8_t val)
{
    io_t *p;

    if (io[port]) {
	p = io[port];
	while(p) {
		if (p->outb)
			p->outb(port, val, p->priv);
		p = p->next;
	}
    }
	
#ifdef IO_TRACE
    if (CS == IO_TRACE)
	pclog("IOTRACE(%04X): outb(%04x,%02x)\n", IO_TRACE, port, val);
#endif
    return;
}


uint16_t
inw(uint16_t port)
{
    io_t *p;

    p = io[port];
    if (p) {
	while(p) {
		if (p->inw)
			return p->inw(port, p->priv);
		p = p->next;
	}
    }

    return(inb(port) | (inb(port + 1) << 8));
}


void
outw(uint16_t port, uint16_t val)
{
    io_t *p;

    p = io[port];
    if (p) {
	while(p) {
		if (p->outw) {
			p->outw(port, val, p->priv);
			return;
		}
		p = p->next;
	}
    }

    outb(port,val);
    outb(port+1,val>>8);

    return;
}


uint32_t
inl(uint16_t port)
{
    io_t *p;

    p = io[port];
    if (p) {
	while(p) {
		if (p->inl)
			return p->inl(port, p->priv);
		p = p->next;
	}
    }

    return(inw(port) | (inw(port + 2) << 16));
}


void
outl(uint16_t port, uint32_t val)
{
    io_t *p;

    p = io[port];
    if (p) {
	while(p) {
		if (p->outl) {
			p->outl(port, val, p->priv);
			return;
		}
		p = p->next;
	}
    }

    outw(port, val);
    outw(port + 2, val >> 16);

    return;
}
