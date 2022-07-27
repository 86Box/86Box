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
#include <86box/timer.h>
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

typedef struct {
    uint8_t	enable;
    uint16_t	base, size;
    void	(*func)(int size, uint16_t addr, uint8_t write, uint8_t val, void *priv),
		*priv;
} io_trap_t;

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

	/* io[c] should be NULL. */
	io[c] = io_last[c] = NULL;
    }
}


void
io_sethandler_common(uint16_t base, int size,
	      uint8_t (*inb)(uint16_t addr, void *priv),
	      uint16_t (*inw)(uint16_t addr, void *priv),
	      uint32_t (*inl)(uint16_t addr, void *priv),
	      void (*outb)(uint16_t addr, uint8_t val, void *priv),
	      void (*outw)(uint16_t addr, uint16_t val, void *priv),
	      void (*outl)(uint16_t addr, uint32_t val, void *priv),
	      void *priv, int step)
{
    int c;
    io_t *p, *q = NULL;

    for (c = 0; c < size; c += step) {
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
io_removehandler_common(uint16_t base, int size,
			uint8_t (*inb)(uint16_t addr, void *priv),
			uint16_t (*inw)(uint16_t addr, void *priv),
			uint32_t (*inl)(uint16_t addr, void *priv),
			void (*outb)(uint16_t addr, uint8_t val, void *priv),
			void (*outw)(uint16_t addr, uint16_t val, void *priv),
			void (*outl)(uint16_t addr, uint32_t val, void *priv),
			void *priv, int step)
{
    int c;
    io_t *p, *q;

    for (c = 0; c < size; c += step) {
	p = io[base + c];
	if (!p)
		continue;
	while(p) {
		q = p->next;
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
		p = q;
	}
    }
}


void
io_handler_common(int set, uint16_t base, int size,
		  uint8_t (*inb)(uint16_t addr, void *priv),
		  uint16_t (*inw)(uint16_t addr, void *priv),
		  uint32_t (*inl)(uint16_t addr, void *priv),
		  void (*outb)(uint16_t addr, uint8_t val, void *priv),
		  void (*outw)(uint16_t addr, uint16_t val, void *priv),
		  void (*outl)(uint16_t addr, uint32_t val, void *priv),
		  void *priv, int step)
{
    if (set)
	io_sethandler_common(base, size, inb, inw, inl, outb, outw, outl, priv, step);
    else
	io_removehandler_common(base, size, inb, inw, inl, outb, outw, outl, priv, step);
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
    io_sethandler_common(base, size, inb, inw, inl, outb, outw, outl, priv, 1);
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
    io_removehandler_common(base, size, inb, inw, inl, outb, outw, outl, priv, 1);
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
    io_handler_common(set, base, size, inb, inw, inl, outb, outw, outl, priv, 1);
}


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
    io_sethandler_common(base, size, inb, inw, inl, outb, outw, outl, priv, 2);
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
    io_removehandler_common(base, size, inb, inw, inl, outb, outw, outl, priv, 2);
}


void
io_handler_interleaved(int set, uint16_t base, int size,
		       uint8_t (*inb)(uint16_t addr, void *priv),
		       uint16_t (*inw)(uint16_t addr, void *priv),
		       uint32_t (*inl)(uint16_t addr, void *priv),
		       void (*outb)(uint16_t addr, uint8_t val, void *priv),
		       void (*outw)(uint16_t addr, uint16_t val, void *priv),
		       void (*outl)(uint16_t addr, uint32_t val, void *priv),
		       void *priv)
{
    io_handler_common(set, base, size, inb, inw, inl, outb, outw, outl, priv, 2);
}


uint8_t
inb(uint16_t port)
{
    uint8_t ret = 0xff;
    io_t *p, *q;
    int found = 0;
    int qfound = 0;

    p = io[port];
    while(p) {
	q = p->next;
	if (p->inb) {
		ret &= p->inb(port, p->priv);
		found |= 1;
		qfound++;
	}
	p = q;
    }

    if (port & 0x80)
	amstrad_latch = AMSTRAD_NOLATCH;
    else if (port & 0x4000)
	amstrad_latch = AMSTRAD_SW10;
    else
	amstrad_latch = AMSTRAD_SW9;

    if (!found)
	cycles -= io_delay;

    /* TriGem 486-BIOS MHz output. */
    if (port == 0x1ed)
	ret = 0xfe;

    io_log("[%04X:%08X] (%i, %i, %04i) in b(%04X) = %02X\n", CS, cpu_state.pc, in_smm, found, qfound, port, ret);

    return(ret);
}


void
outb(uint16_t port, uint8_t val)
{
    io_t *p, *q;
    int found = 0;
    int qfound = 0;

    p = io[port];
    while(p) {
	q = p->next;
	if (p->outb) {
		p->outb(port, val, p->priv);
		found |= 1;
		qfound++;
	}
	p = q;
    }

    if (!found) {
	cycles -= io_delay;
#ifdef USE_DYNAREC
	if (cpu_use_dynarec && ((port == 0xeb) || (port == 0xed)))
		update_tsc();
#endif
    }

    io_log("[%04X:%08X] (%i, %i, %04i) outb(%04X, %02X)\n", CS, cpu_state.pc, in_smm, found, qfound, port, val);

    return;
}


uint16_t
inw(uint16_t port)
{
    io_t *p, *q;
    uint16_t ret = 0xffff;
    int found = 0;
    int qfound = 0;
    uint8_t ret8[2];
    int i = 0;

    p = io[port];
    while(p) {
	q = p->next;
	if (p->inw) {
		ret &= p->inw(port, p->priv);
		found |= 2;
		qfound++;
	}
	p = q;
    }

    ret8[0] = ret & 0xff;
    ret8[1] = (ret >> 8) & 0xff;
    for (i = 0; i < 2; i++) {
	p = io[(port + i) & 0xffff];
	while(p) {
		q = p->next;
		if (p->inb && !p->inw) {
			ret8[i] &= p->inb(port + i, p->priv);
			found |= 1;
			qfound++;
		}
		p = q;
	}
    }
    ret = (ret8[1] << 8) | ret8[0];

    if (port & 0x80)
	amstrad_latch = AMSTRAD_NOLATCH;
    else if (port & 0x4000)
	amstrad_latch = AMSTRAD_SW10;
    else
	amstrad_latch = AMSTRAD_SW9;

    if (!found)
	cycles -= io_delay;

    io_log("[%04X:%08X] (%i, %i, %04i) in w(%04X) = %04X\n", CS, cpu_state.pc, in_smm, found, qfound, port, ret);

    return ret;
}


void
outw(uint16_t port, uint16_t val)
{
    io_t *p, *q;
    int found = 0;
    int qfound = 0;
    int i = 0;

    p = io[port];
    while(p) {
	q = p->next;
	if (p->outw) {
		p->outw(port, val, p->priv);
		found |= 2;
		qfound++;
	}
	p = q;
    }

    for (i = 0; i < 2; i++) {
	p = io[(port + i) & 0xffff];
	while(p) {
		q = p->next;
		if (p->outb && !p->outw) {
			p->outb(port + i, val >> (i << 3), p->priv);
			found |= 1;
			qfound++;
		}
		p = q;
	}
    }

    if (!found) {
	cycles -= io_delay;
#ifdef USE_DYNAREC
	if (cpu_use_dynarec && ((port == 0xeb) || (port == 0xed)))
		update_tsc();
#endif
    }

    io_log("[%04X:%08X] (%i, %i, %04i) outw(%04X, %04X)\n", CS, cpu_state.pc, in_smm, found, qfound, port, val);

    return;
}


uint32_t
inl(uint16_t port)
{
    io_t *p, *q;
    uint32_t ret = 0xffffffff;
    uint16_t ret16[2];
    uint8_t ret8[4];
    int found = 0;
    int qfound = 0;
    int i = 0;

    p = io[port];
    while(p) {
	q = p->next;
	if (p->inl) {
		ret &= p->inl(port, p->priv);
		found |= 4;
		qfound++;
	}
	p = q;
    }

    ret16[0] = ret & 0xffff;
    ret16[1] = (ret >> 16) & 0xffff;
    for (i = 0; i < 4; i += 2) {
	p = io[(port + i) & 0xffff];
	while(p) {
		q = p->next;
		if (p->inw && !p->inl) {
			ret16[i >> 1] &= p->inw(port + i, p->priv);
			found |= 2;
			qfound++;
		}
		p = q;
	}
    }
    ret = (ret16[1] << 16) | ret16[0];

    ret8[0] = ret & 0xff;
    ret8[1] = (ret >> 8) & 0xff;
    ret8[2] = (ret >> 16) & 0xff;
    ret8[3] = (ret >> 24) & 0xff;
    for (i = 0; i < 4; i++) {
	p = io[(port + i) & 0xffff];
	while(p) {
		q = p->next;
		if (p->inb && !p->inw && !p->inl) {
			ret8[i] &= p->inb(port + i, p->priv);
			found |= 1;
			qfound++;
		}
		p = q;
	}
    }
    ret = (ret8[3] << 24) | (ret8[2] << 16) | (ret8[1] << 8) | ret8[0];

    if (port & 0x80)
	amstrad_latch = AMSTRAD_NOLATCH;
    else if (port & 0x4000)
	amstrad_latch = AMSTRAD_SW10;
    else
	amstrad_latch = AMSTRAD_SW9;

    if (!found)
	cycles -= io_delay;

    io_log("[%04X:%08X] (%i, %i, %04i) in l(%04X) = %08X\n", CS, cpu_state.pc, in_smm, found, qfound, port, ret);

    return ret;
}


void
outl(uint16_t port, uint32_t val)
{
    io_t *p, *q;
    int found = 0;
    int qfound = 0;
    int i = 0;

    p = io[port];
    if (p) {
	while(p) {
		q = p->next;
		if (p->outl) {
			p->outl(port, val, p->priv);
			found |= 4;
			qfound++;
		}
		p = q;
	}
    }

    for (i = 0; i < 4; i += 2) {
	p = io[(port + i) & 0xffff];
	while(p) {
		q = p->next;
		if (p->outw && !p->outl) {
			p->outw(port + i, val >> (i << 3), p->priv);
			found |= 2;
			qfound++;
		}
		p = q;
	}
    }

    for (i = 0; i < 4; i++) {
	p = io[(port + i) & 0xffff];
	while(p) {
		q = p->next;
		if (p->outb && !p->outw && !p->outl) {
			p->outb(port + i, val >> (i << 3), p->priv);
			found |= 1;
			qfound++;
		}
		p = q;
	}
    }

    if (!found) {
	cycles -= io_delay;
#ifdef USE_DYNAREC
	if (cpu_use_dynarec && ((port == 0xeb) || (port == 0xed)))
		update_tsc();
#endif
    }

    io_log("[%04X:%08X] (%i, %i, %04i) outl(%04X, %08X)\n", CS, cpu_state.pc, in_smm, found, qfound, port, val);

    return;
}


static uint8_t
io_trap_readb(uint16_t addr, void *priv)
{
    io_trap_t *trap = (io_trap_t *) priv;
    trap->func(1, addr, 0, 0, trap->priv);
    return 0xff;
}


static uint16_t
io_trap_readw(uint16_t addr, void *priv)
{
    io_trap_t *trap = (io_trap_t *) priv;
    trap->func(2, addr, 0, 0, trap->priv);
    return 0xffff;
}


static uint32_t
io_trap_readl(uint16_t addr, void *priv)
{
    io_trap_t *trap = (io_trap_t *) priv;
    trap->func(4, addr, 0, 0, trap->priv);
    return 0xffffffff;
}


static void
io_trap_writeb(uint16_t addr, uint8_t val, void *priv)
{
    io_trap_t *trap = (io_trap_t *) priv;
    trap->func(1, addr, 1, val, trap->priv);
}


static void
io_trap_writew(uint16_t addr, uint16_t val, void *priv)
{
    io_trap_t *trap = (io_trap_t *) priv;
    trap->func(2, addr, 1, val, trap->priv);
}


static void
io_trap_writel(uint16_t addr, uint32_t val, void *priv)
{
    io_trap_t *trap = (io_trap_t *) priv;
    trap->func(4, addr, 1, val, trap->priv);
}


void *
io_trap_add(void (*func)(int size, uint16_t addr, uint8_t write, uint8_t val, void *priv),
	    void *priv)
{
    /* Instantiate new I/O trap. */
    io_trap_t *trap = (io_trap_t *) malloc(sizeof(io_trap_t));
    trap->enable = 0;
    trap->base = trap->size = 0;
    trap->func = func;
    trap->priv = priv;

    return trap;
}


void
io_trap_remap(void *handle, int enable, uint16_t addr, uint16_t size)
{
    io_trap_t *trap = (io_trap_t *) handle;
    if (!trap)
	return;

    io_log("I/O: Remapping trap from %04X-%04X (enable %d) to %04X-%04X (enable %d)\n",
	   trap->base, trap->base + trap->size - 1, trap->enable, addr, addr + size - 1, enable);

    /* Remove old I/O mapping. */
    if (trap->enable && trap->size) {
	io_removehandler(trap->base, trap->size,
			 io_trap_readb, io_trap_readw, io_trap_readl,
			 io_trap_writeb, io_trap_writew, io_trap_writel,
			 trap);
    }

    /* Set trap enable flag, base address and size. */
    trap->enable = !!enable;
    trap->base = addr;
    trap->size = size;

    /* Add new I/O mapping. */
    if (trap->enable && trap->size) {
	io_sethandler(trap->base, trap->size,
		      io_trap_readb, io_trap_readw, io_trap_readl,
		      io_trap_writeb, io_trap_writew, io_trap_writel,
		      trap);
    }
}


void
io_trap_remove(void *handle)
{
    io_trap_t *trap = (io_trap_t *) handle;
    if (!trap)
	return;

    /* Unmap I/O trap before freeing it. */
    io_trap_remap(trap, 0, 0, 0);

    free(trap);
}
