/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implement SMBus (System Management Bus) and its operations.
 *
 * Version:	@(#)smbus.c	1.0.0	2020/03/21
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "86box.h"
#include "smbus.h"


#define NADDRS		128		/* SMBus supports 128 addresses */
#define MAX(a, b) ((a) > (b) ? (a) : (b))


typedef struct _smbus_ {
    	uint8_t  (*read_byte)(uint8_t addr, void *priv);
    	uint8_t  (*read_byte_cmd)(uint8_t addr, uint8_t cmd, void *priv);
    	uint16_t (*read_word_cmd)(uint8_t addr, uint8_t cmd, void *priv);
    	uint8_t  (*read_block_cmd)(uint8_t addr, uint8_t cmd, uint8_t *data, void *priv);

    	void	 (*write_byte)(uint8_t addr, uint8_t val, void *priv);
    	void	 (*write_byte_cmd)(uint8_t addr, uint8_t cmd, uint8_t val, void *priv);
    	void	 (*write_word_cmd)(uint8_t addr, uint8_t cmd, uint16_t val, void *priv);
    	void	 (*write_block_cmd)(uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv);

    	void	 *priv;

    	struct _smbus_ *prev, *next;
} smbus_t;

int smbus_initialized = 0;
smbus_t *smbus[NADDRS], *smbus_last[NADDRS];

#define ENABLE_SMBUS_LOG 1
#ifdef ENABLE_SMBUS_LOG
int smbus_do_log = ENABLE_SMBUS_LOG;


static void
smbus_log(const char *fmt, ...)
{
    va_list ap;

    if (smbus_do_log) {
    	va_start(ap, fmt);
    	pclog_ex(fmt, ap);
    	va_end(ap);
    }
}
#else
#define smbus_log(fmt, ...)
#endif


#ifdef ENABLE_SMBUS_LOG
static uint8_t smbus_null_read_byte(uint8_t addr, void *priv) { smbus_log("SMBus: read_byte(%02x)\n", addr); return(0xff); }
static uint8_t smbus_null_read_byte_cmd(uint8_t addr, uint8_t cmd, void *priv) { smbus_log("SMBus: read_byte_cmd(%02x, %02x)\n", addr, cmd); return(0xff); }
static uint16_t smbus_null_read_word_cmd(uint8_t addr, uint8_t cmd, void *priv) { smbus_log("SMBus: read_word_cmd(%02x, %02x)\n", addr, cmd); return(0xffff); }
static uint8_t smbus_null_read_block_cmd(uint8_t addr, uint8_t cmd, uint8_t *data, void *priv) { smbus_log("SMBus: read_block_cmd(%02x, %02x)\n", addr, cmd); return(0x00); };
static void smbus_null_write_byte(uint8_t addr, uint8_t val, void *priv) { smbus_log("SMBus: write_byte(%02x, %02x)\n", addr, val); }
static void smbus_null_write_byte_cmd(uint8_t addr, uint8_t cmd, uint8_t val, void *priv) { smbus_log("SMBus: write_byte_cmd(%02x, %02x, %02x)\n", addr, cmd, val); }
static void smbus_null_write_word_cmd(uint8_t addr, uint8_t cmd, uint16_t val, void *priv) { smbus_log("SMBus: write_word_cmd(%02x, %02x, %04x)\n", addr, cmd, val); }
static void smbus_null_write_block_cmd(uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv) { smbus_log("SMBus: write_block_cmd(%02x, %02x, %02x)\n", addr, cmd, len); }
#endif


void
smbus_init(void)
{
    int c;
    smbus_t *p, *q;

    if (!smbus_initialized) {
    	for (c=0; c<NADDRS; c++)
    		smbus[c] = smbus_last[c] = NULL;
    	smbus_initialized = 1;
    }

    for (c=0; c<NADDRS; c++) {
    	if (smbus_last[c]) {
    		/* Address c has at least one handler. */
    		p = smbus_last[c];
    		/* After this loop, p will have the pointer to the first handler. */
    		while (p) {
    			q = p->prev;
    			free(p);
    			p = q;
    		}
    		p = NULL;
    	}

#ifdef ENABLE_SMBUS_LOG
    	/* smbus[c] should be the only handler, pointing at the NULL catch handler. */
    	p = (smbus_t *) malloc(sizeof(smbus_t));
    	memset(p, 0, sizeof(smbus_t));
    	smbus[c] = smbus_last[c] = p;
    	p->next = NULL;
    	p->prev = NULL;
    	p->read_byte	   = smbus_null_read_byte;
    	p->read_byte_cmd   = smbus_null_read_byte_cmd;
    	p->read_word_cmd   = smbus_null_read_word_cmd;
    	p->read_block_cmd  = smbus_null_read_block_cmd;
    	p->write_byte	   = smbus_null_write_byte;
    	p->write_byte_cmd  = smbus_null_write_byte_cmd;
    	p->write_word_cmd  = smbus_null_write_word_cmd;
    	p->write_block_cmd = smbus_null_write_block_cmd;
    	p->priv = NULL;
#else
    	/* smbus[c] should be NULL. */
    	smbus[c] = smbus_last[c] = NULL;
#endif
    }
}


void
smbus_sethandler(uint8_t base, int size,
    		 uint8_t (*read_byte)(uint8_t addr, void *priv),
    		 uint8_t (*read_byte_cmd)(uint8_t addr, uint8_t cmd, void *priv),
    		 uint16_t (*read_word_cmd)(uint8_t addr, uint8_t cmd, void *priv),
    		 uint8_t (*read_block_cmd)(uint8_t addr, uint8_t cmd, uint8_t *data, void *priv),
    		 void (*write_byte)(uint8_t addr, uint8_t val, void *priv),
    		 void (*write_byte_cmd)(uint8_t addr, uint8_t cmd, uint8_t val, void *priv),
    		 void (*write_word_cmd)(uint8_t addr, uint8_t cmd, uint16_t val, void *priv),
    		 void (*write_block_cmd)(uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv),
    		 void *priv)
{
    int c;
    smbus_t *p, *q = NULL;

    for (c = 0; c < size; c++) {
    	p = smbus_last[base + c];
    	q = (smbus_t *) malloc(sizeof(smbus_t));
    	memset(q, 0, sizeof(smbus_t));
    	if (p) {
    		p->next = q;
    		q->prev = p;
    	} else {
    		smbus[base + c] = q;
    		q->prev = NULL;
    	}

    	q->read_byte = read_byte;
    	q->read_byte_cmd = read_byte_cmd;
    	q->read_word_cmd = read_word_cmd;
    	q->read_block_cmd = read_block_cmd;

    	q->write_byte = write_byte;
    	q->write_byte_cmd = write_byte_cmd;
    	q->write_word_cmd = write_word_cmd;
    	q->write_block_cmd = write_block_cmd;

    	q->priv = priv;
    	q->next = NULL;

    	smbus_last[base + c] = q;
    }
}


void
smbus_removehandler(uint8_t base, int size,
    		    uint8_t (*read_byte)(uint8_t addr, void *priv),
    		    uint8_t (*read_byte_cmd)(uint8_t addr, uint8_t cmd, void *priv),
    		    uint16_t (*read_word_cmd)(uint8_t addr, uint8_t cmd, void *priv),
    		    uint8_t (*read_block_cmd)(uint8_t addr, uint8_t cmd, uint8_t *data, void *priv),
    		    void (*write_byte)(uint8_t addr, uint8_t val, void *priv),
    		    void (*write_byte_cmd)(uint8_t addr, uint8_t cmd, uint8_t val, void *priv),
    		    void (*write_word_cmd)(uint8_t addr, uint8_t cmd, uint16_t val, void *priv),
    		    void (*write_block_cmd)(uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv),
    		    void *priv)
{
    int c;
    smbus_t *p;

    for (c = 0; c < size; c++) {
    	p = smbus[base + c];
    	if (!p)
    		continue;
    	while(p) {
    		if ((p->read_byte == read_byte) && (p->read_byte_cmd == read_byte_cmd) &&
    		    (p->read_word_cmd == read_word_cmd) && (p->read_block_cmd == read_block_cmd) &&
    		    (p->write_byte == write_byte) && (p->write_byte_cmd == write_byte_cmd) &&
    		    (p->write_word_cmd == write_word_cmd) && (p->write_block_cmd == write_block_cmd) &&
    		    (p->priv == priv)) {
    			if (p->prev)
    				p->prev->next = p->next;
    			else
    				smbus[base + c] = p->next;
    			if (p->next)
    				p->next->prev = p->prev;
    			else
    				smbus_last[base + c] = p->prev;
    			free(p);
    			p = NULL;
    			break;
    		}
    		p = p->next;
    	}
    }
}


void
smbus_handler(int set, uint8_t base, int size,
    	      uint8_t (*read_byte)(uint8_t addr, void *priv),
    	      uint8_t (*read_byte_cmd)(uint8_t addr, uint8_t cmd, void *priv),
    	      uint16_t (*read_word_cmd)(uint8_t addr, uint8_t cmd, void *priv),
    	      uint8_t (*read_block_cmd)(uint8_t addr, uint8_t cmd, uint8_t *data, void *priv),
    	      void (*write_byte)(uint8_t addr, uint8_t val, void *priv),
    	      void (*write_byte_cmd)(uint8_t addr, uint8_t cmd, uint8_t val, void *priv),
    	      void (*write_word_cmd)(uint8_t addr, uint8_t cmd, uint16_t val, void *priv),
    	      void (*write_block_cmd)(uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv),
    	      void *priv)
{
    if (set)
    	smbus_sethandler(base, size, read_byte, read_byte_cmd, read_word_cmd, read_block_cmd, write_byte, write_byte_cmd, write_word_cmd, write_block_cmd, priv);
    else
    	smbus_removehandler(base, size, read_byte, read_byte_cmd, read_word_cmd, read_block_cmd, write_byte, write_byte_cmd, write_word_cmd, write_block_cmd, priv);
}


uint8_t
smbus_has_device(uint8_t addr)
{
    return(!!smbus[addr]);
}


uint8_t
smbus_read_byte(uint8_t addr)
{
    uint8_t ret = 0xff;
    smbus_t *p;
    int found = 0;

    p = smbus[addr];
    if (p) {
    	while(p) {
    		if (p->read_byte) {
    			ret &= p->read_byte(addr, p->priv);
    			found++;
    		}
    		p = p->next;
    	}
    }

    return(ret);
}

uint8_t
smbus_read_byte_cmd(uint8_t addr, uint8_t cmd)
{
    uint8_t ret = 0xff;
    smbus_t *p;
    int found = 0;

    p = smbus[addr];
    if (p) {
    	while(p) {
    		if (p->read_byte_cmd) {
    			ret &= p->read_byte_cmd(addr, cmd, p->priv);
    			found++;
    		}
    		p = p->next;
    	}
    }

    return(ret);
}

uint16_t
smbus_read_word_cmd(uint8_t addr, uint8_t cmd)
{
    uint16_t ret = 0xffff;
    smbus_t *p;
    int found = 0;

    p = smbus[addr];
    if (p) {
    	while(p) {
    		if (p->read_word_cmd) {
    			ret &= p->read_word_cmd(addr, cmd, p->priv);
    			found++;
    		}
    		p = p->next;
    	}
    }

    return(ret);
}

uint8_t
smbus_read_block_cmd(uint8_t addr, uint8_t cmd, uint8_t *data)
{
    uint8_t ret = 0;
    smbus_t *p;
    int found = 0;

    p = smbus[addr];
    if (p) {
    	while(p) {
    		if (p->read_block_cmd) {
    			ret = MAX(ret, p->read_block_cmd(addr, cmd, data, p->priv));
    			found++;
    		}
    		p = p->next;
    	}
    }

    return(ret);
}


void
smbus_write_byte(uint8_t addr, uint8_t val)
{
    smbus_t *p;
    int found = 0;

    if (smbus[addr]) {
    	p = smbus[addr];
    	while(p) {
    		if (p->write_byte) {
    			p->write_byte(addr, val, p->priv);
    			found++;
    		}
    		p = p->next;
    	}
    }

    return;
}

void
smbus_write_byte_cmd(uint8_t addr, uint8_t cmd, uint8_t val)
{
    smbus_t *p;
    int found = 0;

    if (smbus[addr]) {
    	p = smbus[addr];
    	while(p) {
    		if (p->write_byte_cmd) {
    			p->write_byte_cmd(addr, cmd, val, p->priv);
    			found++;
    		}
    		p = p->next;
    	}
    }

    return;
}

void
smbus_write_word_cmd(uint8_t addr, uint8_t cmd, uint16_t val)
{
    smbus_t *p;
    int found = 0;

    if (smbus[addr]) {
    	p = smbus[addr];
    	while(p) {
    		if (p->write_word_cmd) {
    			p->write_word_cmd(addr, cmd, val, p->priv);
    			found++;
    		}
    		p = p->next;
    	}
    }

    return;
}

void
smbus_write_block_cmd(uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len)
{
    smbus_t *p;
    int found = 0;

    p = smbus[addr];
    if (p) {
    	while(p) {
    		if (p->write_block_cmd) {
    			p->write_block_cmd(addr, cmd, data, len, p->priv);
    			found++;
    		}
    		p = p->next;
    	}
    }

    return;
}
