/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		808x CPU emulation, mostly ported from reenigne's XTCE, which
 *		is cycle-accurate.
 *
 * Authors:	Andrew Jenner, <https://www.reenigne.org>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2015-2020 Andrew Jenner.
 *		Copyright 2016-2020 Miran Grca.
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include <86box/machine.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/ppi.h>
#include <86box/timer.h>
#include <86box/gdbstub.h>

/* Is the CPU 8088 or 8086. */
int is8086 = 0;

uint8_t use_custom_nmi_vector = 0;
uint32_t custom_nmi_vector = 0x00000000;


/* The prefetch queue (4 bytes for 8088, 6 bytes for 8086). */
static uint8_t pfq[6];

/* Variables to aid with the prefetch queue operation. */
static int biu_cycles = 0, pfq_pos = 0;

/* The IP equivalent of the current prefetch queue position. */
static uint16_t pfq_ip;

/* Pointer tables needed for segment overrides. */
static uint32_t *opseg[4];
static x86seg *_opseg[4];

static int noint = 0;
static int in_lock = 0;
static int cpu_alu_op, pfq_size;

static uint32_t cpu_src = 0, cpu_dest = 0;
static uint32_t cpu_data = 0;

static uint16_t last_addr = 0x0000;

static uint32_t *ovr_seg = NULL;
static int prefetching = 1, completed = 1;
static int in_rep = 0, repeating = 0;
static int oldc, clear_lock = 0;
static int refresh = 0, cycdiff;


/* Various things needed for 8087. */
#define OP_TABLE(name) ops_ ## name

#define CPU_BLOCK_END()
#define	SEG_CHECK_READ(seg)
#define	SEG_CHECK_WRITE(seg)
#define	CHECK_READ(a, b, c)
#define	CHECK_WRITE(a, b, c)
#define	UN_USED(x)	(void)(x)
#define	fetch_ea_16(val)
#define	fetch_ea_32(val)
#define	PREFETCH_RUN(a, b, c, d, e, f, g, h)

#define CYCLES(val)		\
	{			\
		wait(val, 0);	\
	}

#define CLOCK_CYCLES_ALWAYS(val)		\
	{			\
		wait(val, 0);	\
	}

#if 0
#define CLOCK_CYCLES_FPU(val)		\
	{			\
		wait(val, 0);	\
	}


#define CLOCK_CYCLES(val) \
	{	\
		if (fpu_cycles > 0) {	\
			fpu_cycles -= (val);	\
			if (fpu_cycles < 0) {	\
				wait(val, 0);	\
			}	\
		} else {	\
			wait(val, 0);	\
		}	\
	}

#define CONCURRENCY_CYCLES(c) fpu_cycles = (c)
#else
#define CLOCK_CYCLES(val)		\
	{			\
		wait(val, 0);	\
	}

#define CLOCK_CYCLES_FPU(val)		\
	{			\
		wait(val, 0);	\
	}

#define CONCURRENCY_CYCLES(c)
#endif


typedef	int (*OpFn)(uint32_t fetchdat);


static int	tempc_fpu = 0;


#ifdef ENABLE_808X_LOG
void	dumpregs(int);

int x808x_do_log = ENABLE_808X_LOG;
int indump = 0;


static void
x808x_log(const char *fmt, ...)
{
    va_list ap;

    if (x808x_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define x808x_log(fmt, ...)
#endif


static void	pfq_add(int c, int add);
static void	set_pzs(int bits);


uint16_t
get_last_addr(void)
{
    return last_addr;
}


static void
clock_start(void)
{
    cycdiff = cycles;
}


static void
clock_end(void)
{
    int diff = cycdiff - cycles;

    /* On 808x systems, clock speed is usually crystal frequency divided by an integer. */
    tsc += (uint64_t)diff * ((uint64_t)xt_cpu_multi >> 32ULL);		/* Shift xt_cpu_multi by 32 bits to the right and then multiply. */
    if (TIMER_VAL_LESS_THAN_VAL(timer_target, (uint32_t)tsc))
	timer_process();
}


static void
fetch_and_bus(int c, int bus)
{
    if (refresh > 0) {
	/* Finish the current fetch, if any. */
	cycles -= ((4 - (biu_cycles & 3)) & 3);
	pfq_add((4 - (biu_cycles & 3)) & 3, 1);
	/* Add 4 memory access cycles. */
	cycles -= 4;
	pfq_add(4, 0);

	refresh--;
    }

    pfq_add(c, !bus);
    if (bus < 2) {
	clock_end();
	clock_start();
    }
}


static void
wait(int c, int bus)
{
    cycles -= c;
    fetch_and_bus(c, bus);
}


/* This is for external subtraction of cycles. */
void
sub_cycles(int c)
{
    if (c <= 0)
	return;

    cycles -= c;

    if (!is286)
	fetch_and_bus(c, 2);
}


void
resub_cycles(int old_cycles)
{
    int cyc_diff = 0;

    if (old_cycles > cycles) {
	cyc_diff = old_cycles - cycles;
	cycles = old_cycles;
	sub_cycles(cyc_diff);
    }
}


#undef readmemb
#undef readmemw
#undef readmeml
#undef readmemq


static void
cpu_io(int bits, int out, uint16_t port)
{
    int old_cycles = cycles;

    if (out) {
	wait(4, 1);
	if (bits == 16) {
		if (is8086 && !(port & 1)) {
			old_cycles = cycles;
			outw(port, AX);
		} else {
			wait(4, 1);
			old_cycles = cycles;
			outb(port++, AL);
			outb(port, AH);
		}
	} else {
		old_cycles = cycles;
		outb(port, AL);
	}
    } else {
	wait(4, 1);
	if (bits == 16) {
		if (is8086 && !(port & 1)) {
			old_cycles = cycles;
			AX = inw(port);
		} else {
			wait(4, 1);
			old_cycles = cycles;
			AL = inb(port++);
			AH = inb(port);
		}
	} else {
		old_cycles = cycles;
		AL = inb(port);
	}
    }

    resub_cycles(old_cycles);
}


/* Reads a byte from the memory and advances the BIU. */
static uint8_t
readmemb(uint32_t a)
{
    uint8_t ret;

    wait(4, 1);
    ret = read_mem_b(a);

    return ret;
}


/* Reads a byte from the memory but does not advance the BIU. */
static uint8_t
readmembf(uint32_t a)
{
    uint8_t ret;

    a = cs + (a & 0xffff);
    ret = read_mem_b(a);

    return ret;
}


/* Reads a word from the memory and advances the BIU. */
static uint16_t
readmemw(uint32_t s, uint16_t a)
{
    uint16_t ret;

    wait(4, 1);
    if (is8086 && !(a & 1))
	ret = read_mem_w(s + a);
    else {
	wait(4, 1);
	ret = read_mem_b(s + a);
	ret |= read_mem_b(s + ((a + 1) & 0xffff)) << 8;
    }

    return ret;
}


static uint16_t
readmemwf(uint16_t a)
{
    uint16_t ret;

    ret = read_mem_w(cs + (a & 0xffff));

    return ret;
}


static uint16_t
readmem(uint32_t s)
{
    if (opcode & 1)
	return readmemw(s, cpu_state.eaaddr);
    else
	return (uint16_t) readmemb(s + cpu_state.eaaddr);
}


static uint32_t
readmeml(uint32_t s, uint16_t a)
{
    uint32_t temp;

    temp = (uint32_t) (readmemw(s, a + 2)) << 16;
    temp |= readmemw(s, a);

    return temp;
}


static uint64_t
readmemq(uint32_t s, uint16_t a)
{
    uint64_t temp;

    temp = (uint64_t) (readmeml(s, a + 4)) << 32;
    temp |= readmeml(s, a);

    return temp;
}


/* Writes a byte to the memory and advances the BIU. */
static void
writememb(uint32_t s, uint32_t a, uint8_t v)
{
    uint32_t addr = s + a;

    wait(4, 1);
    write_mem_b(addr, v);

    if ((addr >= 0xf0000) && (addr <= 0xfffff))
	last_addr = addr & 0xffff;
}


/* Writes a word to the memory and advances the BIU. */
static void
writememw(uint32_t s, uint32_t a, uint16_t v)
{
    uint32_t addr = s + a;

    wait(4, 1);
    if (is8086 && !(a & 1))
	write_mem_w(addr, v);
    else {
	write_mem_b(addr, v & 0xff);
	wait(4, 1);
	addr = s + ((a + 1) & 0xffff);
	write_mem_b(addr, v >> 8);
    }

    if ((addr >= 0xf0000) && (addr <= 0xfffff))
	last_addr = addr & 0xffff;
}


static void
writemem(uint32_t s, uint16_t v)
{
    if (opcode & 1)
	writememw(s, cpu_state.eaaddr, v);
    else
	writememb(s, cpu_state.eaaddr, (uint8_t) (v & 0xff));
}


static void
writememl(uint32_t s, uint32_t a, uint32_t v)
{
    writememw(s, a, v & 0xffff);
    writememw(s, a + 2, v >> 16);
}


static void
writememq(uint32_t s, uint32_t a, uint64_t v)
{
    writememl(s, a, v & 0xffffffff);
    writememl(s, a + 4, v >> 32);
}


static void
pfq_write(void)
{
    uint16_t tempw;

    if (is8086 && (pfq_pos < (pfq_size - 1))) {
	/* The 8086 fetches 2 bytes at a time, and only if there's at least 2 bytes
	   free in the queue. */
	tempw = readmemwf(pfq_ip);
	*(uint16_t *) &(pfq[pfq_pos]) = tempw;
	pfq_ip += 2;
	pfq_pos += 2;
    } else if (!is8086 && (pfq_pos < pfq_size)) {
	/* The 8088 fetches 1 byte at a time, and only if there's at least 1 byte
	   free in the queue. */
	pfq[pfq_pos] = readmembf(pfq_ip);
	pfq_ip++;
	pfq_pos++;
    }
}


static uint8_t
pfq_read(void)
{
    uint8_t temp, i;

    temp = pfq[0];
    for (i = 0; i < (pfq_size - 1); i++)
	pfq[i] = pfq[i + 1];
    pfq_pos--;
    cpu_state.pc = (cpu_state.pc + 1) & 0xffff;
    return temp;
}


/* Fetches a byte from the prefetch queue, or from memory if the queue has
   been drained. */
static uint8_t
pfq_fetchb_common(void)
{
    uint8_t temp;

    if (pfq_pos == 0) {
	/* Reset prefetch queue internal position. */
	pfq_ip = cpu_state.pc;
	/* Fill the queue. */
	wait(4 - (biu_cycles & 3), 0);
    }

    /* Fetch. */
    temp = pfq_read();
    return temp;
}


static uint8_t
pfq_fetchb(void)
{
    uint8_t ret;

    ret = pfq_fetchb_common();
    wait(1, 0);
    return ret;
}


/* Fetches a word from the prefetch queue, or from memory if the queue has
   been drained. */
static uint16_t
pfq_fetchw(void)
{
    uint16_t temp;

    temp = pfq_fetchb_common();
    wait(1, 0);
    temp |= (pfq_fetchb_common() << 8);

    return temp;
}


static uint16_t
pfq_fetch()
{
    if (opcode & 1)
	return pfq_fetchw();
    else
	return (uint16_t) pfq_fetchb();
}


/* Adds bytes to the prefetch queue based on the instruction's cycle count. */
static void
pfq_add(int c, int add)
{
    int d;

    if ((c <= 0) || (pfq_pos >= pfq_size))
	return;

    for (d = 0; d < c; d++) {
	biu_cycles = (biu_cycles + 1) & 0x03;
	if (prefetching && add && (biu_cycles == 0x00))
		pfq_write();
    }
}


/* Clear the prefetch queue - called on reset and on anything that affects either CS or IP. */
static void
pfq_clear()
{
    pfq_pos = 0;
    prefetching = 0;
}


static void
load_cs(uint16_t seg)
{
    cpu_state.seg_cs.base = seg << 4;
    cpu_state.seg_cs.seg = seg & 0xffff;
}


static void
load_seg(uint16_t seg, x86seg *s)
{
    s->base = seg << 4;
    s->seg = seg & 0xffff;
}


void
reset_808x(int hard)
{
    biu_cycles = 0;
    in_rep = 0;
    in_lock = 0;
    completed = 1;
    repeating = 0;
    clear_lock = 0;
    refresh = 0;
    ovr_seg = NULL;

    if (hard) {
	opseg[0] = &es;
	opseg[1] = &cs;
	opseg[2] = &ss;
	opseg[3] = &ds;
	_opseg[0] = &cpu_state.seg_es;
	_opseg[1] = &cpu_state.seg_cs;
	_opseg[2] = &cpu_state.seg_ss;
	_opseg[3] = &cpu_state.seg_ds;

	pfq_size = (is8086) ? 6 : 4;
	pfq_clear();
    }

    load_cs(0xFFFF);
    cpu_state.pc = 0;
    rammask = 0xfffff;

    prefetching = 1;
    cpu_alu_op = 0;

    use_custom_nmi_vector = 0x00;
    custom_nmi_vector = 0x00000000;
}


static void
set_ip(uint16_t new_ip) {
    pfq_ip = cpu_state.pc = new_ip;
    prefetching = 1;
}


/* Memory refresh read - called by reads and writes on DMA channel 0. */
void
refreshread(void) {
    refresh++;
}


static uint16_t
get_accum(int bits)
{
    return (bits == 16) ? AX : AL;
}


static void
set_accum(int bits, uint16_t val)
{
    if (bits == 16)
	AX = val;
    else
	AL = val;
}


static uint16_t
sign_extend(uint8_t data)
{
    return data + (data < 0x80 ? 0 : 0xff00);
}


/* Fetches the effective address from the prefetch queue according to MOD and R/M. */
static void
do_mod_rm(void)
{
    rmdat = pfq_fetchb();
    cpu_reg = (rmdat >> 3) & 7;
    cpu_mod = (rmdat >> 6) & 3;
    cpu_rm = rmdat & 7;

    if (cpu_mod == 3)
	return;

    wait(1, 0);
    if ((rmdat & 0xc7) == 0x06) {
	wait(1, 0);
	cpu_state.eaaddr = pfq_fetchw();
	easeg = ovr_seg ? *ovr_seg : ds;
	wait(1, 0);
	return;
    } else switch (cpu_rm) {
	case 0:
	case 3:
		wait(2, 0);
		break;
	case 1:
	case 2:
		wait(3, 0);
		break;
    }
    cpu_state.eaaddr = (*mod1add[0][cpu_rm]) + (*mod1add[1][cpu_rm]);
    easeg = ovr_seg ? *ovr_seg : *mod1seg[cpu_rm];
    switch (rmdat & 0xc0) {
	case 0x40:
		wait(3, 0);
		cpu_state.eaaddr += sign_extend(pfq_fetchb());
		break;
	case 0x80:
		wait(3, 0);
		cpu_state.eaaddr += pfq_fetchw();
		break;
    }
    cpu_state.eaaddr &= 0xffff;
    wait(2, 0);
}


#undef getr8
#define getr8(r)   ((r & 4) ? cpu_state.regs[r & 3].b.h : cpu_state.regs[r & 3].b.l)

#undef setr8
#define setr8(r,v) if (r & 4) cpu_state.regs[r & 3].b.h = v; \
                   else       cpu_state.regs[r & 3].b.l = v;


/* Reads a byte from the effective address. */
static uint8_t
geteab(void)
{
    if (cpu_mod == 3)
	return (getr8(cpu_rm));

    return readmemb(easeg + cpu_state.eaaddr);
}


/* Reads a word from the effective address. */
static uint16_t
geteaw(void)
{
    if (cpu_mod == 3)
	return cpu_state.regs[cpu_rm].w;

    return readmemw(easeg, cpu_state.eaaddr);
}


/* Neede for 8087 - memory only. */
static uint32_t
geteal(void)
{
    if (cpu_mod == 3) {
	fatal("808x register geteal()\n");
	return 0xffffffff;
    }

    return readmeml(easeg, cpu_state.eaaddr);
}


/* Neede for 8087 - memory only. */
static uint64_t
geteaq(void)
{
    if (cpu_mod == 3) {
	fatal("808x register geteaq()\n");
	return 0xffffffff;
    }

    return readmemq(easeg, cpu_state.eaaddr);
}


static void
read_ea(int memory_only, int bits)
{
    if (cpu_mod != 3) {
	if (bits == 16)
		cpu_data = readmemw(easeg, cpu_state.eaaddr);
	else
		cpu_data = readmemb(easeg + cpu_state.eaaddr);
	return;
    }
    if (!memory_only) {
	if (bits == 8) {
		cpu_data = getr8(cpu_rm);
	} else
		cpu_data = cpu_state.regs[cpu_rm].w;
    }
}


static void
read_ea2(int bits)
{
    cpu_state.eaaddr = (cpu_state.eaaddr + 2) & 0xffff;
    if (bits == 16)
	cpu_data = readmemw(easeg, cpu_state.eaaddr);
    else
	cpu_data = readmemb(easeg + cpu_state.eaaddr);
}


/* Writes a byte to the effective address. */
static void
seteab(uint8_t val)
{
    if (cpu_mod == 3) {
	setr8(cpu_rm, val);
    } else
	writememb(easeg, cpu_state.eaaddr, val);
}


/* Writes a word to the effective address. */
static void
seteaw(uint16_t val)
{
    if (cpu_mod == 3)
	cpu_state.regs[cpu_rm].w = val;
    else
	writememw(easeg, cpu_state.eaaddr, val);
}


static void
seteal(uint32_t val)
{
    if (cpu_mod == 3) {
	fatal("808x register seteal()\n");
	return;
    } else
	writememl(easeg, cpu_state.eaaddr, val);
}


static void
seteaq(uint64_t val)
{
    if (cpu_mod == 3) {
	fatal("808x register seteaq()\n");
	return;
    } else
	writememq(easeg, cpu_state.eaaddr, val);
}


/* Leave out the 686 stuff as it's not needed and
   complicates compiling. */
#define FPU_8087
#define tempc tempc_fpu
#include "x87.h"
#include "x87_ops.h"
#undef tempc
#undef FPU_8087


/* Pushes a word to the stack. */
static void
push(uint16_t *val)
{
    SP -= 2;
    cpu_state.eaaddr = (SP & 0xffff);
    writememw(ss, cpu_state.eaaddr, *val);
}


/* Pops a word from the stack. */
static uint16_t
pop(void)
{
    cpu_state.eaaddr = (SP & 0xffff);
    SP += 2;
    return readmemw(ss, cpu_state.eaaddr);
}


static void
access(int num, int bits)
{
    switch (num) {
	case 0: case 61: case 63: case 64:
	case 67: case 69: case 71: case 72:
	default:
		break;
	case 1: case 6: case 7: case 8:
	case 9: case 17: case 20: case 21:
	case 24: case 28: case 47: case 48:
	case 49: case 50: case 51: case 55:
	case 56: case 62: case 66: case 68:
		wait(1, 0);
		break;
	case 3: case 11: case 15: case 22:
	case 23: case 25: case 26: case 35:
	case 44: case 45: case 46: case 52:
	case 53: case 54:
		wait(2, 0);
		break;
	case 16: case 18: case 19: case 27:
	case 32: case 37: case 42:
		wait(3, 0);
		break;
	case 10: case 12: case 13: case 14:
	case 29: case 30: case 33: case 34:
	case 39: case 41: case 60:
		wait(4, 0);
		break;
	case 4: case 70:
		wait(5, 0);
		break;
	case 31: case 38: case 40:
		wait(6, 0);
		break;
	case 5:
		if (opcode == 0xcc)
			wait(7, 0);
		else
			wait(4, 0);
		break;
	case 36:
		wait(1, 0);
		pfq_clear();
		wait (1, 0);
		if (cpu_mod != 3)
			wait(1, 0);
		wait(3, 0);
		break;
	case 43:
		wait(2, 0);
		pfq_clear();
		wait(1, 0);
		break;
	case 57:
		if (cpu_mod != 3)
			wait(2, 0);
		wait(4, 0);
		break;
	case 58:
		if (cpu_mod != 3)
			wait(1, 0);
		wait(4, 0);
		break;
	case 59:
		wait(2, 0);
		pfq_clear();
		if (cpu_mod != 3)
			wait(1, 0);
		wait(3, 0);
		break;
	case 65:
		wait(1, 0);
		pfq_clear();
		wait(2, 0);
		if (cpu_mod != 3)
			wait(1, 0);
		break;
    }
}


/* Calls an interrupt. */
static void
interrupt(uint16_t addr)
{
    uint16_t old_cs, old_ip;
    uint16_t new_cs, new_ip;
    uint16_t tempf;

    addr <<= 2;
    cpu_state.eaaddr = addr;
    old_cs = CS;
    access(5, 16);
    new_ip = readmemw(0, cpu_state.eaaddr);
    wait(1, 0);
    cpu_state.eaaddr = (cpu_state.eaaddr + 2) & 0xffff;
    access(6, 16);
    new_cs = readmemw(0, cpu_state.eaaddr);
    prefetching = 0;
    pfq_clear();
    ovr_seg = NULL;
    access(39, 16);
    tempf = cpu_state.flags & 0x0fd7;
    push(&tempf);
    cpu_state.flags &= ~(I_FLAG | T_FLAG);
    access(40, 16);
    push(&old_cs);
    old_ip = cpu_state.pc;
    load_cs(new_cs);
    access(68, 16);
    set_ip(new_ip);
    access(41, 16);
    push(&old_ip);
}


static void
custom_nmi(void)
{
    uint16_t old_cs, old_ip;
    uint16_t new_cs, new_ip;
    uint16_t tempf;

    cpu_state.eaaddr = 0x0002;
    old_cs = CS;
    access(5, 16);
    (void) readmemw(0, cpu_state.eaaddr);
    new_ip = custom_nmi_vector & 0xffff;
    wait(1, 0);
    cpu_state.eaaddr = (cpu_state.eaaddr + 2) & 0xffff;
    access(6, 16);
    (void) readmemw(0, cpu_state.eaaddr);
    new_cs = custom_nmi_vector >> 16;
    prefetching = 0;
    pfq_clear();
    ovr_seg = NULL;
    access(39, 16);
    tempf = cpu_state.flags & 0x0fd7;
    push(&tempf);
    cpu_state.flags &= ~(I_FLAG | T_FLAG);
    access(40, 16);
    push(&old_cs);
    old_ip = cpu_state.pc;
    load_cs(new_cs);
    access(68, 16);
    set_ip(new_ip);
    access(41, 16);
    push(&old_ip);
}


static int
irq_pending(void)
{
    uint8_t temp;

    temp = (nmi && nmi_enable && nmi_mask) || ((cpu_state.flags & T_FLAG) && !noint) ||
	   ((cpu_state.flags & I_FLAG) && pic.int_pending && !noint);

    return temp;
}


static void
check_interrupts(void)
{
    int temp;

    if (irq_pending()) {
	if ((cpu_state.flags & T_FLAG) && !noint) {
		interrupt(1);
		return;
	}
	if (nmi && nmi_enable && nmi_mask) {
		nmi_enable = 0;
		if (use_custom_nmi_vector)
			custom_nmi();
		else
			interrupt(2);
#ifndef OLD_NMI_BEHAVIOR
		nmi = 0;
#endif
		return;
	}
	if ((cpu_state.flags & I_FLAG) && pic.int_pending && !noint) {
		repeating = 0;
		completed = 1;
		ovr_seg = NULL;
		wait(3, 0);
		/* ACK to PIC */
		temp = pic_irq_ack();
		wait(4, 1);
		wait(1, 0);
		/* ACK to PIC */
		temp = pic_irq_ack();
		wait(4, 1);
		wait(1, 0);
		in_lock = 0;
		clear_lock = 0;
		wait(1, 0);
		/* Here is where temp should be filled, but we cheat. */
		wait(3, 0);
		opcode = 0x00;
		interrupt(temp);
	}
    }
}


static int
rep_action(int bits)
{
    uint16_t t;

    if (in_rep == 0)
	return 0;
    wait(2, 0);
    t = CX;
    if (irq_pending() && (repeating != 0)) {
	access(71, bits);
	pfq_clear();
	set_ip(cpu_state.pc - 2);
	t = 0;
    }
    if (t == 0) {
	wait(1, 0);
	completed = 1;
	repeating = 0;
	return 1;
    }
    --CX;
    completed = 0;
    wait(2, 0);
    if (!repeating)
	wait(2, 0);
    return 0;
}


static uint16_t
jump(uint16_t delta)
{
    uint16_t old_ip;
    access(67, 8);
    pfq_clear();
    wait(5, 0);
    old_ip = cpu_state.pc;
    set_ip((cpu_state.pc + delta) & 0xffff);
    return old_ip;
}


static void
jump_short(void)
{
    jump(sign_extend((uint8_t) cpu_data));
}


static uint16_t
jump_near(void)
{
    return jump(pfq_fetchw());
}


/* Performs a conditional jump. */
static void
jcc(uint8_t opcode, int cond)
{
    /* int8_t offset; */

    wait(1, 0);
    cpu_data = pfq_fetchb();
    wait(1, 0);
    if ((!cond) == !!(opcode & 0x01))
	jump_short();
}


static void
set_cf(int cond)
{
    cpu_state.flags = (cpu_state.flags & ~C_FLAG) | (cond ? C_FLAG : 0);
}


static void
set_if(int cond)
{
    cpu_state.flags = (cpu_state.flags & ~I_FLAG) | (cond ? I_FLAG : 0);
}


static void
set_df(int cond)
{
    cpu_state.flags = (cpu_state.flags & ~D_FLAG) | (cond ? D_FLAG : 0);
}


static void
bitwise(int bits, uint16_t data)
{
    cpu_data = data;
    cpu_state.flags &= ~(C_FLAG | A_FLAG | V_FLAG);
    set_pzs(bits);
}


static void
test(int bits, uint16_t dest, uint16_t src)
{
    cpu_dest = dest;
    cpu_src = src;
    bitwise(bits, (cpu_dest & cpu_src));
}


static void
set_of(int of)
{
    cpu_state.flags = (cpu_state.flags & ~0x800) | (of ? 0x800 : 0);
}


static int
top_bit(uint16_t w, int bits)
{
    return (w & (1 << (bits - 1)));
}


static void
set_of_add(int bits)
{
    set_of(top_bit((cpu_data ^ cpu_src) & (cpu_data ^ cpu_dest), bits));
}


static void
set_of_sub(int bits)
{
    set_of(top_bit((cpu_dest ^ cpu_src) & (cpu_data ^ cpu_dest), bits));
}


static void
set_af(int af)
{
    cpu_state.flags = (cpu_state.flags & ~0x10) | (af ? 0x10 : 0);
}


static void
do_af(void)
{
    set_af(((cpu_data ^ cpu_src ^ cpu_dest) & 0x10) != 0);
}


static void
set_apzs(int bits)
{
    set_pzs(bits);
    do_af();
}


static void
add(int bits)
{
    int size_mask = (1 << bits) - 1;

    cpu_data = cpu_dest + cpu_src;
    set_apzs(bits);
    set_of_add(bits);

    /* Anything - FF with carry on is basically anything + 0x100: value stays
       unchanged but carry goes on. */
    if ((cpu_alu_op == 2) && !(cpu_src & size_mask) && (cpu_state.flags & C_FLAG))
	cpu_state.flags |= C_FLAG;
    else
	set_cf((cpu_src & size_mask) > (cpu_data & size_mask));
}


static void
sub(int bits)
{
    int size_mask = (1 << bits) - 1;

    cpu_data = cpu_dest - cpu_src;
    set_apzs(bits);
    set_of_sub(bits);

    /* Anything - FF with carry on is basically anything - 0x100: value stays
       unchanged but carry goes on. */
    if ((cpu_alu_op == 3) && !(cpu_src & size_mask) && (cpu_state.flags & C_FLAG))
	cpu_state.flags |= C_FLAG;
    else
	set_cf((cpu_src & size_mask) > (cpu_dest & size_mask));
}


static void
alu_op(int bits)
{
    switch(cpu_alu_op) {
	case 1:
		bitwise(bits, (cpu_dest | cpu_src));
		break;
	case 2:
		if (cpu_state.flags & C_FLAG)
			cpu_src++;
		/* Fall through. */
	case 0:
		add(bits);
		break;
	case 3:
		if (cpu_state.flags & C_FLAG)
			cpu_src++;
		/* Fall through. */
	case 5: case 7:
		sub(bits);
		break;
	case 4:
		test(bits, cpu_dest, cpu_src);
		break;
	case 6:
		bitwise(bits, (cpu_dest ^ cpu_src));
		break;
    }
}


static void
set_sf(int bits)
{
    cpu_state.flags = (cpu_state.flags & ~0x80) | (top_bit(cpu_data, bits) ? 0x80 : 0);
}


static void
set_pf(void)
{
    static uint8_t table[0x100] = {
	4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
	0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
	0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
	4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
	0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
	4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
	4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
	0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
	0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
	4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
	4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
	0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
	4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
	0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
	0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
	4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4};

    cpu_state.flags = (cpu_state.flags & ~4) | table[cpu_data & 0xff];
}


static void
mul(uint16_t a, uint16_t b)
{
    int negate = 0;
    int bit_count = 8;
    int carry, i;
    uint16_t high_bit = 0x80;
    uint16_t size_mask;
    uint16_t c, r;

    size_mask = (1 << bit_count) - 1;

    if (opcode != 0xd5) {
	if (opcode & 1) {
		bit_count = 16;
		high_bit = 0x8000;
	} else
		wait(8, 0);

	size_mask = (1 << bit_count) - 1;

	if ((rmdat & 0x38) == 0x28) {
		if (!top_bit(a, bit_count)) {
			if (top_bit(b, bit_count)) {
				wait(1, 0);
				if ((b & size_mask) != ((opcode & 1) ? 0x8000 : 0x80))
					wait(1, 0);
				b = ~b + 1;
				negate = 1;
			}
		} else {
			wait(1, 0);
			a = ~a + 1;
			negate = 1;
			if (top_bit(b, bit_count)) {
				b = ~b + 1;
				negate = 0;
			} else
				wait(4, 0);
		}
		wait(10, 0);
	}
	wait(3, 0);
    }

    c = 0;
    a &= size_mask;
    carry = (a & 1) != 0;
    a >>= 1;
    for (i = 0; i < bit_count; ++i) {
	wait(7, 0);
	if (carry) {
		cpu_src = c;
		cpu_dest = b;
		add(bit_count);
		c = cpu_data & size_mask;
		wait(1, 0);
		carry = !!(cpu_state.flags & C_FLAG);
	}
	r = (c >> 1) + (carry ? high_bit : 0);
	carry = (c & 1) != 0;
	c = r;
	r = (a >> 1) + (carry ?  high_bit : 0);
	carry = (a & 1) != 0;
	a = r;
    }
    if (negate) {
	c = ~c;
	a = (~a + 1) & size_mask;
	if (a == 0)
		++c;
	wait(9, 0);
    }
    cpu_data = a;
    cpu_dest = c;

    set_sf(bit_count);
    set_pf();
    set_af(0);
}


static void
set_of_rotate(int bits)
{
    set_of(top_bit(cpu_data ^ cpu_dest, bits));
}


static void
set_zf_ex(int zf)
{
    cpu_state.flags = (cpu_state.flags & ~0x40) | (zf ? 0x40 : 0);
}


static void
set_zf(int bits)
{
    int size_mask = (1 << bits) - 1;

    set_zf_ex((cpu_data & size_mask) == 0);
}


static void
set_pzs(int bits)
{
    set_pf();
    set_zf(bits);
    set_sf(bits);
}


static void
set_co_mul(int bits, int carry)
{
    set_cf(carry);
    set_of(carry);
    /* NOTE: When implementing the V20, care should be taken to not change
	     the zero flag. */
    set_zf_ex(!carry);
    if (!carry)
	wait(1, 0);
}


/* Was div(), renamed to avoid conflicts with stdlib div(). */
static int
x86_div(uint16_t l, uint16_t h)
{
    int b, bit_count = 8;
    int negative = 0;
    int dividend_negative = 0;
    int size_mask, carry;
    uint16_t r;

    if (opcode & 1) {
	l = AX;
	h = DX;
	bit_count = 16;
    }

    size_mask = (1 << bit_count) - 1;

    if (opcode != 0xd4) {
	if ((rmdat & 0x38) == 0x38) {
		if (top_bit(h, bit_count)) {
			h = ~h;
			l = (~l + 1) & size_mask;
			if (l == 0)
				++h;
			h &= size_mask;
			negative = 1;
			dividend_negative = 1;
			wait(4, 0);
		}
		if (top_bit(cpu_src, bit_count)) {
			cpu_src = ~cpu_src + 1;
			negative = !negative;
		} else
			wait(1, 0);
		wait(9, 0);
	}
	wait(3, 0);
    }
    wait(8, 0);
    cpu_src &= size_mask;
    if (h >= cpu_src) {
	if (opcode != 0xd4)
		wait(1, 0);
	interrupt(0);
	return 0;
    }
    if (opcode != 0xd4)
	wait(1, 0);
    wait(2, 0);
    carry = 1;
    for (b = 0; b < bit_count; ++b) {
	r = (l << 1) + (carry ? 1 : 0);
	carry = top_bit(l, bit_count);
	l = r;
	r = (h << 1) + (carry ? 1 : 0);
	carry = top_bit(h, bit_count);
	h = r;
	wait(8, 0);
	if (carry) {
		carry = 0;
		h -= cpu_src;
		if (b == bit_count - 1)
			wait(2, 0);
	} else {
		carry = cpu_src > h;
		if (!carry) {
			h -= cpu_src;
			wait(1, 0);
			if (b == bit_count - 1)
				wait(2, 0);
		}
	}
    }
    l = ~((l << 1) + (carry ? 1 : 0));
    if (opcode != 0xd4 && (rmdat & 0x38) == 0x38) {
	wait(4, 0);
	if (top_bit(l, bit_count)) {
		if (cpu_mod == 3)
			wait(1, 0);
		interrupt(0);
		return 0;
	}
	wait(7, 0);
	if (negative)
		l = ~l + 1;
	if (dividend_negative)
		h = ~h + 1;
    }
    if (opcode == 0xd4) {
	AL = h & 0xff;
	AH = l & 0xff;
    } else {
	AH = h & 0xff;
	AL = l & 0xff;
	if (opcode & 1) {
		DX = h;
		AX = l;
	}
    }
    return 1;
}


static uint16_t
string_increment(int bits)
{
    int d = bits >> 3;
    if (cpu_state.flags & D_FLAG)
	cpu_state.eaaddr -= d;
    else
	cpu_state.eaaddr += d;
    cpu_state.eaaddr &= 0xffff;
    return cpu_state.eaaddr;
}


static void
lods(int bits)
{
    cpu_state.eaaddr = SI;
    if (bits == 16)
	cpu_data = readmemw((ovr_seg ? *ovr_seg : ds), cpu_state.eaaddr);
    else
	cpu_data = readmemb((ovr_seg ? *ovr_seg : ds) + cpu_state.eaaddr);
    SI = string_increment(bits);
}


static void
stos(int bits)
{
    cpu_state.eaaddr = DI;
    if (bits == 16)
	writememw(es, cpu_state.eaaddr, cpu_data);
    else
	writememb(es, cpu_state.eaaddr, (uint8_t) (cpu_data & 0xff));
    DI = string_increment(bits);
}


static void
aa(void)
{
    set_pzs(8);
    AL = cpu_data & 0x0f;
    wait(6, 0);
}


static void
set_ca(void)
{
    set_cf(1);
    set_af(1);
}


static void
clear_ca(void)
{
    set_cf(0);
    set_af(0);
}


static uint16_t
get_ea(void)
{
    if (opcode & 1)
	return geteaw();
    else
	return (uint16_t) geteab();
}


static uint16_t
get_reg(uint8_t reg)
{
    if (opcode & 1)
	return cpu_state.regs[reg].w;
    else
	return (uint16_t) getr8(reg);
}


static void
set_ea(uint16_t val)
{
    if (opcode & 1)
	seteaw(val);
    else
	seteab((uint8_t) (val & 0xff));
}


static void
set_reg(uint8_t reg, uint16_t val)
{
    if (opcode & 1)
	cpu_state.regs[reg].w = val;
    else
	setr8(reg, (uint8_t) (val & 0xff));
}


static void
cpu_data_opff_rm(void) {
    if (!(opcode & 1)) {
	if (cpu_mod != 3)
		cpu_data |= 0xff00;
	else
		cpu_data = cpu_state.regs[cpu_rm].w;
    }
}


/* Executes instructions up to the specified number of cycles. */
void
execx86(int cycs)
{
    uint8_t temp = 0, temp2;
    uint8_t old_af;
    uint16_t addr, tempw;
    uint16_t new_cs, new_ip;
    int bits;

    cycles += cycs;

    while (cycles > 0) {
	clock_start();

	if (!repeating) {
		cpu_state.oldpc = cpu_state.pc;
		opcode = pfq_fetchb();
		oldc = cpu_state.flags & C_FLAG;
		if (clear_lock) {
			in_lock = 0;
			clear_lock = 0;
		}
		wait(1, 0);
	}

	completed = 1;
	// pclog("[%04X:%04X] Opcode: %02X\n", CS, cpu_state.pc, opcode);
	switch (opcode) {
		case 0x06: case 0x0E: case 0x16: case 0x1E:	/* PUSH seg */
			access(29, 16);
			push(&(_opseg[(opcode >> 3) & 0x03]->seg));
			break;
		case 0x07: case 0x0F: case 0x17: case 0x1F:	/* POP seg */
			access(22, 16);
			if (opcode == 0x0F) {
				load_cs(pop());
				pfq_pos = 0;
			} else
				load_seg(pop(), _opseg[(opcode >> 3) & 0x03]);
			wait(1, 0);
			/* All POP segment instructions suppress interrupts for one instruction. */
			noint = 1;
			break;

		case 0x26:	/*ES:*/
		case 0x2E:	/*CS:*/
		case 0x36:	/*SS:*/
		case 0x3E:	/*DS:*/
			wait(1, 0);
			ovr_seg = opseg[(opcode >> 3) & 0x03];
			completed = 0;
			break;

		case 0x00: case 0x01: case 0x02: case 0x03:
		case 0x08: case 0x09: case 0x0a: case 0x0b:
		case 0x10: case 0x11: case 0x12: case 0x13:
		case 0x18: case 0x19: case 0x1a: case 0x1b:
		case 0x20: case 0x21: case 0x22: case 0x23:
		case 0x28: case 0x29: case 0x2a: case 0x2b:
		case 0x30: case 0x31: case 0x32: case 0x33:
		case 0x38: case 0x39: case 0x3a: case 0x3b:
			/* alu rm, r / r, rm */
			bits = 8 << (opcode & 1);
			do_mod_rm();
			access(46, bits);
			tempw = get_ea();
			cpu_alu_op = (opcode >> 3) & 7;
			if ((opcode & 2) == 0) {
				cpu_dest = tempw;
				cpu_src = get_reg(cpu_reg);
			} else {
				cpu_dest = get_reg(cpu_reg);
				cpu_src = tempw;
			}
			if (cpu_mod != 3)
				wait(2, 0);
			wait(1, 0);
			alu_op(bits);
			if (cpu_alu_op != 7) {
				if ((opcode & 2) == 0) {
					access(10, bits);
					set_ea(cpu_data);
					if (cpu_mod == 3)
						wait(1, 0);
				} else {
					set_reg(cpu_reg, cpu_data);
					wait(1, 0);
				}
			} else
				wait(1, 0);
			break;

		case 0x04: case 0x05: case 0x0c: case 0x0d:
		case 0x14: case 0x15: case 0x1c: case 0x1d:
		case 0x24: case 0x25: case 0x2c: case 0x2d:
		case 0x34: case 0x35: case 0x3c: case 0x3d:
			/* alu A, imm */
			bits = 8 << (opcode & 1);
			wait(1, 0);
			cpu_data = pfq_fetch();
			cpu_dest = get_accum(bits);	/* AX/AL */
			cpu_src = cpu_data;
			cpu_alu_op = (opcode >> 3) & 7;
			alu_op(bits);
			if (cpu_alu_op != 7)
				set_accum(bits, cpu_data);
			wait(1, 0);
			break;

		case 0x27:	/*DAA*/
			cpu_dest = AL;
			set_of(0);
			old_af = !!(cpu_state.flags & A_FLAG);
			if ((cpu_state.flags & A_FLAG) || (AL & 0x0f) > 9) {
				cpu_src = 6;
				cpu_data = cpu_dest + cpu_src;
				set_of_add(8);
				cpu_dest = cpu_data;
				set_af(1);
			}
			if ((cpu_state.flags & C_FLAG) || AL > (old_af ? 0x9f : 0x99)) {
				cpu_src = 0x60;
				cpu_data = cpu_dest + cpu_src;
				set_of_add(8);
				cpu_dest = cpu_data;
				set_cf(1);
			}
			AL = cpu_dest;
			set_pzs(8);
			wait(3, 0);
			break;
		case 0x2F:	/*DAS*/
			cpu_dest = AL;
			set_of(0);
			old_af = !!(cpu_state.flags & A_FLAG);
			if ((cpu_state.flags & A_FLAG) || ((AL & 0xf) > 9)) {
				cpu_src = 6;
				cpu_data = cpu_dest - cpu_src;
				set_of_sub(8);
				cpu_dest = cpu_data;
				set_af(1);
			}
			if ((cpu_state.flags & C_FLAG) || AL > (old_af ? 0x9f : 0x99)) {
				cpu_src = 0x60;
				cpu_data = cpu_dest - cpu_src;
				set_of_sub(8);
				cpu_dest = cpu_data;
				set_cf(1);
			}
			AL = cpu_dest;
			set_pzs(8);
			wait(3, 0);
			break;
		case 0x37:	/*AAA*/
			wait(1, 0);
			if ((cpu_state.flags & A_FLAG) || ((AL & 0xf) > 9)) {
				cpu_src = 6;
				++AH;
				set_ca();
			} else {
				cpu_src = 0;
				clear_ca();
				wait(1, 0);
			}
			cpu_dest = AL;
			cpu_data = cpu_dest + cpu_src;
			set_of_add(8);
			aa();
			break;
		case 0x3F: /*AAS*/
			wait(1, 0);
			if ((cpu_state.flags & A_FLAG) || ((AL & 0xf) > 9)) {
				cpu_src = 6;
				--AH;
				set_ca();
			} else {
				cpu_src = 0;
				clear_ca();
				wait(1, 0);
			}
			cpu_dest = AL;
			cpu_data = cpu_dest - cpu_src;
			set_of_sub(8);
			aa();
			break;

		case 0x40: case 0x41: case 0x42: case 0x43:
		case 0x44: case 0x45: case 0x46: case 0x47:
		case 0x48: case 0x49: case 0x4A: case 0x4B:
		case 0x4C: case 0x4D: case 0x4E: case 0x4F:
			/* INCDEC rw */
			wait(1, 0);
			cpu_dest = cpu_state.regs[opcode & 7].w;
			cpu_src = 1;
			bits = 16;
			if ((opcode & 8) == 0) {
				cpu_data = cpu_dest + cpu_src;
				set_of_add(bits);
			} else {
				cpu_data = cpu_dest - cpu_src;
				set_of_sub(bits);
			}
			do_af();
			set_pzs(16);
			cpu_state.regs[opcode & 7].w = cpu_data;
			break;

		case 0x50: case 0x51: case 0x52: case 0x53:	/*PUSH r16*/
		case 0x54: case 0x55: case 0x56: case 0x57:
			access(30, 16);
			push(&(cpu_state.regs[opcode & 0x07].w));
			break;
		case 0x58: case 0x59: case 0x5A: case 0x5B:	/*POP r16*/
		case 0x5C: case 0x5D: case 0x5E: case 0x5F:
			access(23, 16);
			cpu_state.regs[opcode & 0x07].w = pop();
			wait(1, 0);
			break;

		case 0x60:	/*JO alias*/
		case 0x70:	/*JO*/
		case 0x61:	/*JNO alias*/
		case 0x71:	/*JNO*/
			jcc(opcode, cpu_state.flags & V_FLAG);
			break;
		case 0x62:	/*JB alias*/
		case 0x72:	/*JB*/
		case 0x63:	/*JNB alias*/
		case 0x73:	/*JNB*/
			jcc(opcode, cpu_state.flags & C_FLAG);
			break;
		case 0x64:	/*JE alias*/
		case 0x74:	/*JE*/
		case 0x65:	/*JNE alias*/
		case 0x75:	/*JNE*/
			jcc(opcode, cpu_state.flags & Z_FLAG);
			break;
		case 0x66:	/*JBE alias*/
		case 0x76:	/*JBE*/
		case 0x67:	/*JNBE alias*/
		case 0x77:	/*JNBE*/
			jcc(opcode, cpu_state.flags & (C_FLAG | Z_FLAG));
			break;
		case 0x68:	/*JS alias*/
		case 0x78:	/*JS*/
		case 0x69:	/*JNS alias*/
		case 0x79:	/*JNS*/
			jcc(opcode, cpu_state.flags & N_FLAG);
			break;
		case 0x6A:	/*JP alias*/
		case 0x7A:	/*JP*/
		case 0x6B: /*JNP alias*/
		case 0x7B: /*JNP*/
			jcc(opcode, cpu_state.flags & P_FLAG);
			break;
		case 0x6C:	/*JL alias*/
		case 0x7C:	/*JL*/
		case 0x6D:	/*JNL alias*/
		case 0x7D:	/*JNL*/
			temp = (cpu_state.flags & N_FLAG) ? 1 : 0;
			temp2 = (cpu_state.flags & V_FLAG) ? 1 : 0;
			jcc(opcode, temp ^ temp2);
			break;
		case 0x6E:	/*JLE alias*/
		case 0x7E:	/*JLE*/
		case 0x6F:	/*JNLE alias*/
		case 0x7F:	/*JNLE*/
			temp = (cpu_state.flags & N_FLAG) ? 1 : 0;
			temp2 = (cpu_state.flags & V_FLAG) ? 1 : 0;
			jcc(opcode, (cpu_state.flags & Z_FLAG) || (temp != temp2));
			break;

		case 0x80: case 0x81: case 0x82: case 0x83:
			/* alu rm, imm */
			bits = 8 << (opcode & 1);
			do_mod_rm();
			access(47, bits);
			cpu_data = get_ea();
			cpu_dest = cpu_data;
			if (cpu_mod != 3)
				wait(3, 0);
			if (opcode == 0x81) {
				if (cpu_mod == 3)
					wait(1, 0);
				cpu_src = pfq_fetchw();
			} else {
				if (cpu_mod == 3)
					wait(1, 0);
				if (opcode == 0x83)
					cpu_src = sign_extend(pfq_fetchb());
				else
					cpu_src = pfq_fetchb() | 0xff00;
			}
			wait(1, 0);
			cpu_alu_op = (rmdat & 0x38) >> 3;
			alu_op(bits);
			if (cpu_alu_op != 7) {
				access(11, bits);
				set_ea(cpu_data);
			} else {
				if (cpu_mod != 3)
					wait(1, 0);
			}
			break;

		case 0x84: case 0x85:
			/* TEST rm, reg */
			bits = 8 << (opcode & 1);
			do_mod_rm();
			access(48, bits);
			cpu_data = get_ea();
			test(bits, cpu_data, get_reg(cpu_reg));
			if (cpu_mod == 3)
				wait(2, 0);
			wait(2, 0);
			break;
		case 0x86: case 0x87:
			/* XCHG rm, reg */
			bits = 8 << (opcode & 1);
			do_mod_rm();
			access(49, bits);
			cpu_data = get_ea();
			cpu_src = get_reg(cpu_reg);
			set_reg(cpu_reg, cpu_data);
			wait(3, 0);
			access(12, bits);
			set_ea(cpu_src);
			break;

		case 0x88: case 0x89:
			/* MOV rm, reg */
			bits = 8 << (opcode & 1);
			do_mod_rm();
			wait(1, 0);
			access(13, bits);
			set_ea(get_reg(cpu_reg));
			break;
		case 0x8A: case 0x8B:
			/* MOV reg, rm */
			bits = 8 << (opcode & 1);
			do_mod_rm();
			access(50, bits);
			set_reg(cpu_reg, get_ea());
			wait(1, 0);
			if (cpu_mod != 3)
				wait(2, 0);
			break;

		case 0x8C:	/*MOV w,sreg*/
			do_mod_rm();
			if (cpu_mod == 3)
				wait(1, 0);
			access(14, 16);
			seteaw(_opseg[(rmdat & 0x18) >> 3]->seg);
			break;

		case 0x8D:	/*LEA*/
			do_mod_rm();
			cpu_state.regs[cpu_reg].w = cpu_state.eaaddr;
			wait(1, 0);
			if (cpu_mod != 3)
				wait(2, 0);
			break;

		case 0x8E:	/*MOV sreg,w*/
			do_mod_rm();
			access(51, 16);
			tempw = geteaw();
			if ((rmdat & 0x18) == 0x08) {
				load_cs(tempw);
				pfq_pos = 0;
			} else
				load_seg(tempw, _opseg[(rmdat & 0x18) >> 3]);
			wait(1, 0);
			if (cpu_mod != 3)
				wait(2, 0);
			if (((rmdat & 0x18) >> 3) == 2)
				noint = 1;
			break;

		case 0x8F:	/*POPW*/
			do_mod_rm();
			wait(1, 0);
			cpu_src = cpu_state.eaaddr;
			access(24, 16);
			if (cpu_mod != 3)
				wait(2, 0);
			cpu_data = pop();
			cpu_state.eaaddr = cpu_src;
			wait(2, 0);
			access(15, 16);
			seteaw(cpu_data);
			break;

		case 0x90: case 0x91: case 0x92: case 0x93:
		case 0x94: case 0x95: case 0x96: case 0x97:
			/* XCHG AX, rw */
			wait(1, 0);
			cpu_data = cpu_state.regs[opcode & 7].w;
			cpu_state.regs[opcode & 7].w = AX;
			AX = cpu_data;
			wait(1, 0);
			break;

		case 0x98:	/*CBW*/
			wait(1, 0);
			AX = sign_extend(AL);
			break;
		case 0x99:	/*CWD*/
			wait(4, 0);
			if (!top_bit(AX, 16))
				DX = 0;
			else {
				wait(1, 0);
				DX = 0xffff;
			}
			break;
		case 0x9A:	/*CALL FAR*/
			wait(1, 0);
			new_ip = pfq_fetchw();
			wait(1, 0);
			new_cs = pfq_fetchw();
			pfq_clear();
			access(31, 16);
			push(&(CS));
			access(60, 16);
			cpu_state.oldpc = cpu_state.pc;
			load_cs(new_cs);
			set_ip(new_ip);
			access(32, 16);
			push((uint16_t *) &(cpu_state.oldpc));
			break;
		case 0x9B:	/*WAIT*/
			if (!repeating)
				wait(2, 0);
			wait(5, 0);
#ifdef NO_HACK
			if (irq_pending()) {
				wait(7, 0);
				check_interrupts();
			} else {
				repeating = 1;
				completed = 0;
				clock_end();
			}
#else
			wait(7, 0);
			check_interrupts();
#endif
			break;
		case 0x9C:	/*PUSHF*/
			access(33, 16);
			tempw = (cpu_state.flags & 0x0fd7) | 0xf000;
			push(&tempw);
			break;
		case 0x9D:	/*POPF*/
			access(25, 16);
			cpu_state.flags = pop() | 2;
			wait(1, 0);
			break;
		case 0x9E:	/*SAHF*/
			wait(1, 0);
			cpu_state.flags = (cpu_state.flags & 0xff02) | AH;
			wait(2, 0);
			break;
		case 0x9F:	/*LAHF*/
			wait(1, 0);
			AH = cpu_state.flags & 0xd7;
			break;

		case 0xA0: case 0xA1:
			/* MOV A, [iw] */
			bits = 8 << (opcode & 1);
			wait(1, 0);
			cpu_state.eaaddr = pfq_fetchw();
			access(1, bits);
			set_accum(bits, readmem((ovr_seg ? *ovr_seg : ds)));
			wait(1, 0);
			break;
		case 0xA2: case 0xA3:
			/* MOV [iw], A */
			bits = 8 << (opcode & 1);
			wait(1, 0);
			cpu_state.eaaddr = pfq_fetchw();
			access(7, bits);
			writemem((ovr_seg ? *ovr_seg : ds), get_accum(bits));
			break;

		case 0xA4: case 0xA5:	/* MOVS */
		case 0xAC: case 0xAD:	/* LODS */
			bits = 8 << (opcode & 1);
			if (!repeating) {
				wait(1, 0);
				if ((opcode & 8) == 0 && in_rep != 0)
					wait(1, 0);
			}
			if (rep_action(bits)) {
				wait(1, 0);
				if ((opcode & 8) != 0)
					wait(1, 0);
				break;
			}
			if (in_rep != 0 && (opcode & 8) != 0)
				wait(1, 0);
			access(20, bits);
			lods(bits);
			if ((opcode & 8) == 0) {
				access(27, bits);
				stos(bits);
			} else {
				set_accum(bits, cpu_data);
				if (in_rep != 0)
					wait(2, 0);
			}
			if (in_rep == 0) {
				wait(3, 0);
				if ((opcode & 8) != 0)
					wait(1, 0);
				break;
			}
			repeating = 1;
			clock_end();
			break;

		case 0xA6: case 0xA7:	/* CMPS */
		case 0xAE: case 0xAF:	/* SCAS */
			bits = 8 << (opcode & 1);
			if (!repeating)
				wait(1, 0);
			if (rep_action(bits)) {
				wait(2, 0);
				break;
			}
			if (in_rep != 0)
				wait(1, 0);
			wait(1, 0);
			cpu_dest = get_accum(bits);
			if ((opcode & 8) == 0) {
				access(21, bits);
				lods(bits);
				wait(1, 0);
				cpu_dest = cpu_data;
			}
			access(2, bits);
			cpu_state.eaaddr = DI;
			cpu_data = readmem(es);
			DI = string_increment(bits);
			cpu_src = cpu_data;
			sub(bits);
			wait(2, 0);
			if (in_rep == 0) {
				wait(3, 0);
				break;
			}
			if ((!!(cpu_state.flags & Z_FLAG)) == (in_rep == 1)) {
				completed = 1;
				wait(4, 0);
				break;
			}
			repeating = 1;
			clock_end();
			break;

		case 0xA8: case 0xA9:
			/* TEST A, imm */
			bits = 8 << (opcode & 1);
			wait(1, 0);
			cpu_data = pfq_fetch();
			test(bits, get_accum(bits), cpu_data);
			wait(1, 0);
			break;

		case 0xAA: case 0xAB:	/* STOS */
			bits = 8 << (opcode & 1);
			if (!repeating) {
				wait(1, 0);
				if (in_rep != 0)
					wait(1, 0);
			}
			if (rep_action(bits)) {
				wait(1, 0);
				break;
			}
			cpu_data = AX;
			access(28, bits);
			stos(bits);
			if (in_rep == 0) {
				wait(3, 0);
				break;
			}
			repeating = 1;
			clock_end();
			break;

		case 0xB0: case 0xB1: case 0xB2: case 0xB3:	/*MOV cpu_reg,#8*/
		case 0xB4: case 0xB5: case 0xB6: case 0xB7:
			wait(1, 0);
			if (opcode & 0x04)
				cpu_state.regs[opcode & 0x03].b.h = pfq_fetchb();
			else
				cpu_state.regs[opcode & 0x03].b.l = pfq_fetchb();
			wait(1, 0);
			break;

		case 0xB8: case 0xB9: case 0xBA: case 0xBB:	/*MOV cpu_reg,#16*/
		case 0xBC: case 0xBD: case 0xBE: case 0xBF:
			wait(1, 0);
			cpu_state.regs[opcode & 0x07].w = pfq_fetchw();
			wait(1, 0);
			break;

		case 0xC0: case 0xC1: case 0xC2: case 0xC3:
		case 0xC8: case 0xC9: case 0xCA: case 0xCB:
			/* RET */
			bits = 8 + (opcode & 0x08);
			if ((opcode & 9) != 1)
				wait(1, 0);
			if (!(opcode & 1)) {
				cpu_src = pfq_fetchw();
				wait(1, 0);
			}
			if ((opcode & 9) == 9)
				wait(1, 0);
			pfq_clear();
			access(26, bits);
			new_ip = pop();
			wait(2, 0);
			if ((opcode & 8) == 0)
				new_cs = CS;
			else {
				access(42, bits);
				new_cs = pop();
				if (opcode & 1)
					wait(1, 0);
			}
			if (!(opcode & 1)) {
				SP += cpu_src;
				wait(1, 0);
			}
			load_cs(new_cs);
			access(72, bits);
			set_ip(new_ip);
			break;

		case 0xC4: case 0xC5:
			/* LsS rw, rmd */
			do_mod_rm();
			bits = 16;
			access(52, bits);
			read_ea(1, bits);
			cpu_state.regs[cpu_reg].w = cpu_data;
			access(57, bits);
			read_ea2(bits);
			load_seg(cpu_data, (opcode & 0x01) ? &cpu_state.seg_ds : &cpu_state.seg_es);
			wait(1, 0);
			break;

		case 0xC6: case 0xC7:
			/* MOV rm, imm */
			bits = 8 << (opcode & 1);
			do_mod_rm();
			wait(1, 0);
			if (cpu_mod != 3)
				wait(2, 0);
			cpu_data = pfq_fetch();
			if (cpu_mod == 3)
				wait(1, 0);
			access(16, bits);
			set_ea(cpu_data);
			break;

		case 0xCC:	/*INT 3*/
			interrupt(3);
			break;
		case 0xCD:	/*INT*/
			wait(1, 0);
			interrupt(pfq_fetchb());
			break;
		case 0xCE:	/*INTO*/
			wait(3, 0);
			if (cpu_state.flags & V_FLAG) {
				wait(2, 0);
				interrupt(4);
			}
			break;

		case 0xCF:	/*IRET*/
			access(43, 8);
			new_ip = pop();
			wait(3, 0);
			access(44, 8);
			new_cs = pop();
			load_cs(new_cs);
			access(62, 8);
			set_ip(new_ip);
			access(45, 8);
			cpu_state.flags = pop() | 2;
			wait(5, 0);
			noint = 1;
			nmi_enable = 1;
			break;

		case 0xD0: case 0xD1: case 0xD2: case 0xD3:
			/* rot rm */
			bits = 8 << (opcode & 1);
			do_mod_rm();
			if (cpu_mod == 3)
				wait(1, 0);
			access(53, bits);
			cpu_data = get_ea();
			if ((opcode & 2) == 0) {
				cpu_src = 1;
				wait((cpu_mod != 3) ? 4 : 0, 0);
			} else {
				cpu_src = CL;
				wait((cpu_mod != 3) ? 9 : 6, 0);
			}
			while (cpu_src != 0) {
				cpu_dest = cpu_data;
				oldc = cpu_state.flags & C_FLAG;
				switch (rmdat & 0x38) {
					case 0x00:	/* ROL */
						set_cf(top_bit(cpu_data, bits));
						cpu_data <<= 1;
						cpu_data |= ((cpu_state.flags & C_FLAG) ? 1 : 0);
						set_of_rotate(bits);
						set_af(0);
						break;
					case 0x08:	/* ROR */
						set_cf((cpu_data & 1) != 0);
						cpu_data >>= 1;
						if (cpu_state.flags & C_FLAG)
							cpu_data |= (!(opcode & 1) ? 0x80 : 0x8000);
						set_of_rotate(bits);
						set_af(0);
						break;
					case 0x10:	/* RCL */
						set_cf(top_bit(cpu_data, bits));
						cpu_data = (cpu_data << 1) | (oldc ? 1 : 0);
						set_of_rotate(bits);
						set_af(0);
						break;
					case 0x18: 	/* RCR */
						set_cf((cpu_data & 1) != 0);
						cpu_data >>= 1;
						if (oldc)
							cpu_data |= (!(opcode & 0x01) ? 0x80 : 0x8000);
						set_cf((cpu_dest & 1) != 0);
						set_of_rotate(bits);
						set_af(0);
						break;
					case 0x20:	/* SHL */
						set_cf(top_bit(cpu_data, bits));
						cpu_data <<= 1;
						set_of_rotate(bits);
						set_af((cpu_data & 0x10) != 0);
						set_pzs(bits);
						break;
					case 0x28:	/* SHR */
						set_cf((cpu_data & 1) != 0);
						cpu_data >>= 1;
						set_of_rotate(bits);
						set_af(0);
						set_pzs(bits);
						break;
					case 0x30:	/* SETMO - undocumented? */
						bitwise(bits, 0xffff);
						set_cf(0);
						set_of_rotate(bits);
						set_af(0);
						set_pzs(bits);
						break;
					case 0x38:	/* SAR */
						set_cf((cpu_data & 1) != 0);
						cpu_data >>= 1;
						if (!(opcode & 1))
							cpu_data |= (cpu_dest & 0x80);
						else
							cpu_data |= (cpu_dest & 0x8000);
						set_of_rotate(bits);
						set_af(0);
						set_pzs(bits);
						break;
				}
				if ((opcode & 2) != 0)
					wait(4, 0);
				--cpu_src;
			}
			access(17, bits);
			set_ea(cpu_data);
			break;

		case 0xD4:	/*AAM*/
			wait(1, 0);
			cpu_src = pfq_fetchb();
			if (x86_div(AL, 0))
				set_pzs(16);
			break;
		case 0xD5:	/*AAD*/
			wait(1, 0);
			mul(pfq_fetchb(), AH);
			cpu_dest = AL;
			cpu_src = cpu_data;
			add(8);
			AL = cpu_data;
			AH = 0x00;
			break;
		case 0xD6:	/*SALC*/
			wait(1, 0);
			AL = (cpu_state.flags & C_FLAG) ? 0xff : 0x00;
			wait(1, 0);
			break;
		case 0xD7:	/*XLATB*/
			cpu_state.eaaddr = (BX + AL) & 0xffff;
			access(4, 8);
			AL = readmemb((ovr_seg ? *ovr_seg : ds) + cpu_state.eaaddr);
			wait(1, 0);
			break;

		case 0xD8: case 0xD9: case 0xDA: case 0xDB:
		case 0xDD: case 0xDC: case 0xDE: case 0xDF:
			/* esc i, r, rm */
			do_mod_rm();
			access(54, 16);
			tempw = cpu_state.pc;
			if (!hasfpu)
				geteaw();
			else switch(opcode) {
				case 0xD8:
					ops_fpu_8087_d8[(rmdat >> 3) & 0x1f]((uint32_t) rmdat);
					break;
				case 0xD9:
					ops_fpu_8087_d9[rmdat & 0xff]((uint32_t) rmdat);
					break;
				case 0xDA:
					ops_fpu_8087_da[rmdat & 0xff]((uint32_t) rmdat);
					break;
				case 0xDB:
					ops_fpu_8087_db[rmdat & 0xff]((uint32_t) rmdat);
					break;
				case 0xDC:
					ops_fpu_8087_dc[(rmdat >> 3) & 0x1f]((uint32_t) rmdat);
					break;
				case 0xDD:
					ops_fpu_8087_dd[rmdat & 0xff]((uint32_t) rmdat);
					break;
				case 0xDE:
					ops_fpu_8087_de[rmdat & 0xff]((uint32_t) rmdat);
					break;
				case 0xDF:
					ops_fpu_8087_df[rmdat & 0xff]((uint32_t) rmdat);
					break;
			}
			cpu_state.pc = tempw;	/* Do this as the x87 code advances it, which is needed on
						   the 286+ core, but not here. */
			wait(1, 0);
			if (cpu_mod != 3)
				wait(2, 0);
			break;

		case 0xE0: case 0xE1: case 0xE2: case 0xE3:
			/* LOOP */
			wait(3, 0);
			cpu_data = pfq_fetchb();
			if (opcode != 0xe2)
				wait(1, 0);
			if (opcode != 0xe3) {
				--CX;
				oldc = (CX != 0);
				switch (opcode) {
					case 0xE0:
						if (cpu_state.flags & Z_FLAG)
							oldc = 0;
						break;
					case 0xE1:
						if (!(cpu_state.flags & Z_FLAG))
							oldc = 0;
						break;
				}
			} else
				oldc = (CX == 0);
			if (oldc)
				jump_short();
			break;

		case 0xE4: case 0xE5: case 0xE6: case 0xE7:
		case 0xEC: case 0xED: case 0xEE: case 0xEF:
			bits = 8 << (opcode & 1);
			if ((opcode & 0x0e) != 0x0c)
				wait(1, 0);
			if ((opcode & 8) == 0)
				cpu_data = pfq_fetchb();
			else
				cpu_data = DX;
			cpu_state.eaaddr = cpu_data;
			if ((opcode & 2) == 0) {
				access(3, bits);
				if (opcode & 1)
					cpu_io(16, 0, cpu_data);
				else
					cpu_io(8, 0, cpu_data);
				wait(1, 0);
			} else {
				if ((opcode & 8) == 0)
					access(8, bits);
				else
					access(9, bits);
				if (opcode & 1)
					cpu_io(16, 1, cpu_data);
				else
					cpu_io(8, 1, cpu_data);
			}
			break;

		case 0xE8:	/*CALL rel 16*/
			wait(1, 0);
			cpu_state.oldpc = jump_near();
			access(34, 8);
			push((uint16_t *) &(cpu_state.oldpc));
			break;
		case 0xE9:	/*JMP rel 16*/
			wait(1, 0);
			jump_near();
			break;
		case 0xEA:	/*JMP far*/
			wait(1, 0);
			addr = pfq_fetchw();
			wait(1, 0);
			tempw = pfq_fetchw();
			load_cs(tempw);
			access(70, 8);
			pfq_clear();
			set_ip(addr);
			break;
		case 0xEB:	/*JMP rel*/
			wait(1, 0);
			cpu_data = (int8_t) pfq_fetchb();
			jump_short();
			wait(1, 0);
			break;

		case 0xF0: case 0xF1:	/*LOCK - F1 is alias*/
			in_lock = 1;
			wait(1, 0);
			completed = 0;
			break;

		case 0xF2:	/*REPNE*/
		case 0xF3:	/*REPE*/
			wait(1, 0);
			in_rep = (opcode == 0xf2 ? 1 : 2);
			completed = 0;
			break;

		case 0xF4:	/*HLT*/
			if (!repeating) {
				wait(1, 0);
				pfq_clear();
			}
			wait(1, 0);
			if (irq_pending()) {
				wait(cycles & 1, 0);
				check_interrupts();
			} else {
				repeating = 1;
				completed = 0;
				clock_end();
			}
			break;
		case 0xF5:	/*CMC*/
			wait(1, 0);
			cpu_state.flags ^= C_FLAG;
			break;

		case 0xF6: case 0xF7:
			bits = 8 << (opcode & 1);
			do_mod_rm();
			access(55, bits);
			cpu_data = get_ea();
			switch (rmdat & 0x38) {
				case 0x00: case 0x08:
					/* TEST */
					wait(2, 0);
					if (cpu_mod != 3)
						wait(1, 0);
					cpu_src = pfq_fetch();
					wait(1, 0);
					test(bits, cpu_data, cpu_src);
					if (cpu_mod != 3)
						wait(1, 0);
					break;
				case 0x10:	/* NOT */
				case 0x18:	/* NEG */
					wait(2, 0);
					if ((rmdat & 0x38) == 0x10)
						cpu_data = ~cpu_data;
					else {
						cpu_src = cpu_data;
						cpu_dest = 0;
						sub(bits);
					}
					access(18, bits);
					set_ea(cpu_data);
					break;
				case 0x20:	/* MUL */
				case 0x28:	/* IMUL */
					wait(1, 0);
					mul(get_accum(bits), cpu_data);
					if (opcode & 1) {
						AX = cpu_data;
						DX = cpu_dest;
						set_co_mul(bits, DX != ((AX & 0x8000) == 0 || (rmdat & 0x38) == 0x20 ? 0 : 0xffff));
						cpu_data = DX;
					} else {
						AL = (uint8_t) cpu_data;
						AH = (uint8_t) cpu_dest;
						set_co_mul(bits, AH != ((AL & 0x80) == 0 || (rmdat & 0x38) == 0x20 ? 0 : 0xff));
						cpu_data = AH;
					}
					/* NOTE: When implementing the V20, care should be taken to not change
						 the zero flag. */
					set_sf(bits);
					set_pf();
					if (cpu_mod != 3)
						wait(1, 0);
					break;
				case 0x30:	/* DIV */
				case 0x38:	/* IDIV */
					if (cpu_mod != 3)
						wait(1, 0);
					cpu_src = cpu_data;
					if (x86_div(AL, AH))
						wait(1, 0);
					break;
			}
			break;

		case 0xF8: case 0xF9:
			/* CLCSTC */
			wait(1, 0);
			set_cf(opcode & 1);
			break;
		case 0xFA: case 0xFB:
			/* CLISTI */
			wait(1, 0);
			set_if(opcode & 1);
			break;
		case 0xFC: case 0xFD:
			/* CLDSTD */
			wait(1, 0);
			set_df(opcode & 1);
			break;

		case 0xFE: case 0xFF:
			/* misc */
			bits = 8 << (opcode & 1);
			do_mod_rm();
			access(56, bits);
			read_ea(((rmdat & 0x38) == 0x18) || ((rmdat & 0x38) == 0x28), bits);
			switch (rmdat & 0x38) {
				case 0x00:	/* INC rm */
				case 0x08:	/* DEC rm */
					cpu_dest = cpu_data;
					cpu_src = 1;
					if ((rmdat & 0x38) == 0x00) {
						cpu_data = cpu_dest + cpu_src;
						set_of_add(bits);
					} else {
						cpu_data = cpu_dest - cpu_src;
						set_of_sub(bits);
					}
					do_af();
					set_pzs(bits);
					wait(2, 0);
					access(19, bits);
					set_ea(cpu_data);
					break;
				case 0x10:	/* CALL rm */
					cpu_data_opff_rm();
					access(63, bits);
					wait(1, 0);
					pfq_clear();
					wait(4, 0);
					if (cpu_mod != 3)
						wait(1, 0);
					wait(1, 0);	/* Wait. */
					cpu_state.oldpc = cpu_state.pc;
					set_ip(cpu_data);
					wait(2, 0);
					access(35, bits);
					push((uint16_t *) &(cpu_state.oldpc));
					break;
				case 0x18:	/* CALL rmd */
					new_ip = cpu_data;
					access(58, bits);
					read_ea2(bits);
					if (!(opcode & 1))
						cpu_data |= 0xff00;
					new_cs = cpu_data;
					access(36, bits);
					push(&(CS));
					access(64, bits);
					wait(4, 0);
					cpu_state.oldpc = cpu_state.pc;
					load_cs(new_cs);
					set_ip(new_ip);
					access(37, bits);
					push((uint16_t *) &(cpu_state.oldpc));
					break;
				case 0x20:	/* JMP rm */
					cpu_data_opff_rm();
					access(65, bits);
					set_ip(cpu_data);
					break;
				case 0x28:	/* JMP rmd */
					new_ip = cpu_data;
					access(59, bits);
					read_ea2(bits);
					if (!(opcode & 1))
						cpu_data |= 0xff00;
					new_cs = cpu_data;
					load_cs(new_cs);
					access(66, bits);
					set_ip(new_ip);
					break;
				case 0x30:	/* PUSH rm */
				case 0x38:
					if (cpu_mod != 3)
						wait(1, 0);
					access(38, bits);
					push((uint16_t *) &(cpu_data));
					break;
			}
			break;

		default:
			x808x_log("Illegal opcode: %02X\n", opcode);
			pfq_fetchb();
			wait(8, 0);
			break;
	}

	if (completed) {
		repeating = 0;
		ovr_seg = NULL;
		in_rep = 0;
		if (in_lock)
			clear_lock = 1;
		clock_end();
		check_interrupts();

		if (noint)
			noint = 0;

		cpu_alu_op = 0;
	}

#ifdef USE_GDBSTUB
	if (gdbstub_instruction())
		return;
#endif
    }
}
