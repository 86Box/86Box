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
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include "cpu.h"
#include <86box/m_amstrad.h>


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


#ifdef ENABLE_IO_LOG
int io_do_log = ENABLE_IO_LOG;


static void
io_log(const char *fmt, ...)
{
    va_list ap;

    if (io_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define io_log(fmt, ...)
#endif


#ifdef ENABLE_IO_LOG
static uint8_t null_inb(uint16_t addr, void *priv) { io_log("IO: read(%04x)\n"); return(0xff); }
static uint16_t null_inw(uint16_t addr, void *priv) { io_log("IO: readw(%04x)\n"); return(0xffff); }
static uint32_t null_inl(uint16_t addr, void *priv) { io_log("IO: readl(%04x)\n"); return(0xffffffff); }
static void null_outb(uint16_t addr, uint8_t val, void *priv) { io_log("IO: write(%04x, %02x)\n", val); }
static void null_outw(uint16_t addr, uint16_t val, void *priv) { io_log("IO: writew(%04x, %04x)\n", val); }
static void null_outl(uint16_t addr, uint32_t val, void *priv) { io_log("IO: writel(%04x, %08lx)\n", val); }
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

#ifdef ENABLE_IO_LOG
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


void
io_handler(int set, uint16_t base, int size, 
	   uint8_t (*inb)(uint16_t addr, void *priv),
	   uint16_t (*inw)(uint16_t addr, void *priv),
	   uint32_t (*inl)(uint16_t addr, void *priv),
	   void (*outb)(uint16_t addr, uint8_t val, void *priv),
	   void (*outw)(uint16_t addr, uint16_t val, void *priv),
	   void (*outl)(uint16_t addr, uint32_t val, void *priv),
	   void *priv)
{
    if (set)
	io_sethandler(base, size, inb, inw, inl, outb, outw, outl, priv);
    else
	io_removehandler(base, size, inb, inw, inl, outb, outw, outl, priv);
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
    uint8_t ret = 0xff;
    io_t *p;
    int found = 0;

    p = io[port];
    if (p) {
	while(p) {
		if (p->inb) {
			ret &= p->inb(port, p->priv);
			found++;
		}
		p = p->next;
	}
    }

    if (port & 0x80)
	amstrad_latch = AMSTRAD_NOLATCH;
    else if (port & 0x4000)   
	amstrad_latch = AMSTRAD_SW10;
    else
	amstrad_latch = AMSTRAD_SW9;

#ifdef ENABLE_IO_LOG
    if (CS == IO_TRACE)
	io_log("IOTRACE(%04X): inb(%04x)=%02x\n", IO_TRACE, port, ret);
#endif

    /* if (port == 0x386) {
	ret = 0x00;
	found = 1;
    }

    if (port == 0x406) {
	ret = 0x00;
	found = 1;
    }

    if (port == 0xf87) {
	ret = 0x00;
	found = 1;
    } */

    if (!found)
	sub_cycles(io_delay);

    // if (!found)
	// pclog("inb(%04X) = %02X\n", port, ret);

    // if (in_smm)
	// pclog("inb(%04X) = %02X\n", port, ret);

    return(ret);
}


void
outb(uint16_t port, uint8_t val)
{
    io_t *p;
    int found = 0;

    // if (in_smm)
	// pclog("outb(%04X, %02X)\n", port, val);

    if (io[port]) {
	p = io[port];
	while(p) {
		if (p->outb) {
			p->outb(port, val, p->priv);
			found++;
		}
		p = p->next;
	}
    }
	
#ifdef ENABLE_IO_LOG
    if (CS == IO_TRACE)
	io_log("IOTRACE(%04X): outb(%04x,%02x)\n", IO_TRACE, port, val);
#endif

    if (!found)
	sub_cycles(io_delay);

    // if (!found)
	// pclog("outb(%04X, %02X)\n", port, val);

    return;
}


uint16_t
inw(uint16_t port)
{
    io_t *p;
    uint16_t ret = 0xffff;
    int found = 0;

    p = io[port];
    if (p) {
	while(p) {
		if (p->inw) {
			ret = p->inw(port, p->priv);
			found = 1;
		}
		p = p->next;
	}
    }

    if (!found)
	ret = (inb(port) | (inb(port + 1) << 8));

    // if (in_smm)
	// pclog("inw(%04X) = %04X\n", port, ret);

    return ret;
}


void
outw(uint16_t port, uint16_t val)
{
    io_t *p;

    // if (in_smm)
	// pclog("outw(%04X, %04X)\n", port, val);

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
    uint32_t ret = 0xffffffff;
    int found = 0;

    p = io[port];
    if (p) {
	while(p) {
		if (p->inl) {
			ret = p->inl(port, p->priv);
			found = 1;
		}
		p = p->next;
	}
    }

    if (!found)
	ret = (inw(port) | (inw(port + 2) << 16));

    // if (in_smm)
	// pclog("inl(%04X) = %08X\n", port, ret);

    return ret;
}


void
outl(uint16_t port, uint32_t val)
{
    io_t *p;

    // if (in_smm)
	// pclog("outl(%04X, %08X)\n", port, val);

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
