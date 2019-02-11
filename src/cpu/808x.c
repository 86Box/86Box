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
 * Version:	@(#)808x.c	1.0.8	2018/11/14
 *
 * Authors:	Andrew Jenner, <https://www.reenigne.org>
 *		Miran Grca, <mgrca8@gmail.com>
 * Authors:	Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2015-2018 Andrew Jenner.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2008-2018 Sarah Walker.
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "cpu.h"
#include "x86.h"
#include "../machine/machine.h"
#include "../io.h"
#include "../mem.h"
#include "../rom.h"
#include "../nmi.h"
#include "../pic.h"
#include "../timer.h"


/* The opcode of the instruction currently being executed. */
uint8_t opcode;

/* The tables to speed up the setting of the Z, N, and P flags. */
uint8_t znptable8[256];
uint16_t znptable16[65536];

/* A 16-bit zero, needed because some speed-up arrays contain pointers to it. */
uint16_t zero = 0;

/* MOD and R/M stuff. */
uint16_t *mod1add[2][8];
uint32_t *mod1seg[8];
int rmdat;

/* XT CPU multiplier. */
int xt_cpu_multi;

/* Is the CPU 8088 or 8086. */
int is8086 = 0;

/* Variables for handling the non-maskable interrupts. */
int nmi = 0, nmi_auto_clear = 0;
int nmi_enable = 1;

/* Was the CPU ever reset? */
int x86_was_reset = 0;

/* Amount of instructions executed - used to calculate the % shown in the title bar. */
int ins = 0;

/* Is the TRAP flag on? */
int trap = 0;

/* The current effective address's segment. */
uint32_t easeg;


/* The prefetch queue (4 bytes for 8088, 6 bytes for 8086). */
static uint8_t pfq[6];

/* Variables to aid with the prefetch queue operation. */
static int fetchcycles = 0, pfq_pos = 0;

/* The IP equivalent of the current prefetch queue position. */
static uint16_t pfq_ip;

/* Pointer tables needed for segment overrides. */
static uint32_t *opseg[4];
static x86seg *_opseg[4];

static int takeint = 0, noint = 0;
static int in_lock = 0, halt = 0;
static int cpu_alu_op, pfq_size;

static uint16_t cpu_src = 0, cpu_dest = 0;
static uint16_t cpu_data = 0;

static uint32_t *ovr_seg = NULL;


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


void
dumpregs(int force)
{
    int c;
    char *seg_names[4] = { "ES", "CS", "SS", "DS" };

    /* Only dump when needed, and only once.. */
    if (indump || (!force && !dump_on_exit))
	return;

    x808x_log("EIP=%08X CS=%04X DS=%04X ES=%04X SS=%04X FLAGS=%04X\n",
	      cpu_state.pc, CS, DS, ES, SS, flags);
    x808x_log("Old CS:EIP: %04X:%08X; %i ins\n", oldcs, cpu_state.oldpc, ins);
    for (c = 0; c < 4; c++) {
	x808x_log("%s : base=%06X limit=%08X access=%02X  limit_low=%08X limit_high=%08X\n",
		  seg_names[c], _opseg[c]->base, _opseg[c]->limit,
		  _opseg[c]->access, _opseg[c]->limit_low, _opseg[c]->limit_high);
    }
    if (is386) {
	x808x_log("FS : base=%06X limit=%08X access=%02X  limit_low=%08X limit_high=%08X\n",
		  seg_fs, _fs.limit, _fs.access, _fs.limit_low, _fs.limit_high);
	x808x_log("GS : base=%06X limit=%08X access=%02X  limit_low=%08X limit_high=%08X\n",
		  gs, _gs.limit, _gs.access, _gs.limit_low, _gs.limit_high);
	x808x_log("GDT : base=%06X limit=%04X\n", gdt.base, gdt.limit);
	x808x_log("LDT : base=%06X limit=%04X\n", ldt.base, ldt.limit);
	x808x_log("IDT : base=%06X limit=%04X\n", idt.base, idt.limit);
	x808x_log("TR  : base=%06X limit=%04X\n", tr.base, tr.limit);
	x808x_log("386 in %s mode: %i-bit data, %-i-bit stack\n",
		  (msw & 1) ? ((eflags & VM_FLAG) ? "V86" : "protected") : "real",
		  (use32) ? 32 : 16, (stack32) ? 32 : 16);
	x808x_log("CR0=%08X CR2=%08X CR3=%08X CR4=%08x\n", cr0, cr2, cr3, cr4);
	x808x_log("EAX=%08X EBX=%08X ECX=%08X EDX=%08X\nEDI=%08X ESI=%08X EBP=%08X ESP=%08X\n",
		  EAX, EBX, ECX, EDX, EDI, ESI, EBP, ESP);
    } else {
	x808x_log("808x/286 in %s mode\n", (msw & 1) ? "protected" : "real");
	x808x_log("AX=%04X BX=%04X CX=%04X DX=%04X DI=%04X SI=%04X BP=%04X SP=%04X\n",
		  AX, BX, CX, DX, DI, SI, BP, SP);
    }
    x808x_log("Entries in readlookup : %i    writelookup : %i\n", readlnum, writelnum);
    x87_dumpregs();
    indump = 0;
}
#else
#define x808x_log(fmt, ...)
#endif


static void	pfq_add(int c);
static void	set_pzs(int bits);


static int
irq_pending(void)
{
    if ((nmi && nmi_enable && nmi_mask) || ((flags & I_FLAG) && (pic.pend & ~pic.mask) && !noint))
	return 1;

    return 0;
}


static void
wait(int c, int bus)
{
    cycles -= c;
    if (!bus)
	pfq_add(c);
}


#undef readmemb
#undef readmemw

/* Common read function. */
static uint8_t
readmemb_common(uint32_t a)
{
    uint8_t ret;

    if (readlookup2 == NULL)
	ret = readmembl(a);
    else {
	if (readlookup2[(a) >> 12] == ((uintptr_t) -1))
		ret = readmembl(a);
	else
		ret = *(uint8_t *)(readlookup2[(a) >> 12] + (a));
    }

    return ret;
}

/* Reads a byte from the memory and accounts for memory transfer cycles to
   subtract from the cycles to use for adding to the prefetch queue. */
static uint8_t
readmemb(uint32_t a)
{
    uint8_t ret;

    wait(4, 1);
    ret = readmemb_common(a);

    return ret;
}


/* Reads a byte from the memory but does not accounts for memory transfer
   cycles to subtract from the cycles to use for adding to the prefetch
   queue. */
static uint8_t
readmembf(uint32_t a)
{
    uint8_t ret;

    a = cs + (a & 0xffff);
    ret = readmemb_common(a);

    return ret;
}


/* Reads a word from the memory and accounts for memory transfer cycles to
   subtract from the cycles to use for adding to the prefetch queue. */
static uint16_t
readmemw_common(uint32_t s, uint16_t a)
{
    uint16_t ret;

    ret = readmemb_common(s + a);
    ret |= readmemb_common(s + ((a + 1) & 0xffff)) << 8;

    return ret;
}


static uint16_t
readmemw(uint32_t s, uint16_t a)
{
    uint16_t ret;

    if (is8086 && !(a & 1))
	wait(4, 1);
    else
	wait(8, 1);
    ret = readmemw_common(s, a);

    return ret;
}


static uint16_t
readmemwf(uint16_t a)
{
    uint16_t ret;

    ret = readmemw_common(cs, a & 0xffff);

    return ret;
}



/* Writes a byte from the memory and accounts for memory transfer cycles to
   subtract from the cycles to use for adding to the prefetch queue. */
static void
writememb_common(uint32_t a, uint8_t v)
{
    if (writelookup2 == NULL)
	writemembl(a, v);
    else {
	if (writelookup2[(a) >> 12] == ((uintptr_t) -1))
		writemembl(a, v);
	else
		*(uint8_t *)(writelookup2[a >> 12] + a) = v;
    }
}


static void
writememb(uint32_t a, uint8_t v)
{
    wait(4, 1);
    writememb_common(a, v);
}


/* Writes a word from the memory and accounts for memory transfer cycles to
   subtract from the cycles to use for adding to the prefetch queue. */
static void
writememw(uint32_t s, uint32_t a, uint16_t v)
{
    if (is8086 && !(a & 1))
	wait(4, 1);
    else
	wait(8, 1);
    writememb_common(s + a, v & 0xff);
    writememb_common(s + ((a + 1) & 0xffff), v >> 8);
}


static void
pfq_write(void)
{
    uint16_t tempw;

    /* On 8086 and even IP, fetch *TWO* bytes at once. */
    if (pfq_pos < pfq_size) {
	/* If we're filling the last byte of the prefetch queue, do *NOT*
	   read more than one byte even on the 8086. */
	if (is8086 && !(pfq_ip & 1) && !(pfq_pos & 1)) {
		tempw = readmemwf(pfq_ip);
		*(uint16_t *) &(pfq[pfq_pos]) = tempw;
		pfq_ip += 2;
		pfq_pos += 2;
    	} else {
		pfq[pfq_pos] = readmembf(pfq_ip);
		pfq_ip++;
		pfq_pos++;
	}
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
    cpu_state.pc++;
    return temp;
}


/* Fetches a byte from the prefetch queue, or from memory if the queue has
   been drained. */
static uint8_t
pfq_fetchb(void)
{
    uint8_t temp;

    if (pfq_pos == 0) {
	/* Extra cycles due to having to fetch on read. */
	wait(4 - (fetchcycles & 3), 1);
	fetchcycles = 4;
	/* Reset prefetch queue internal position. */
	pfq_ip = cpu_state.pc;
	/* Fill the queue. */
	pfq_write();
    } else
	fetchcycles -= 4;

    /* Fetch. */
    temp = pfq_read();
    wait(1, 0);
    return temp;
}


/* Fetches a word from the prefetch queue, or from memory if the queue has
   been drained. */
static uint16_t
pfq_fetchw(void)
{
    uint8_t temp = pfq_fetchb();
    return temp | (pfq_fetchb() << 8);
}


/* Adds bytes to the prefetch queue based on the instruction's cycle count. */
static void
pfq_add(int c)
{
    int d;
    if (c < 0)
	return;
    if (pfq_pos >= pfq_size)
	return;
    d = c + (fetchcycles & 3);
    while ((d > 3) && (pfq_pos < pfq_size)) {
	d -= 4;
	pfq_write();
    }
    fetchcycles += c;
    if (fetchcycles > 16)
	fetchcycles = 16;
}


/* Clear the prefetch queue - called on reset and on anything that affects either CS or IP. */
static void
pfq_clear()
{
    pfq_ip = cpu_state.pc;
    pfq_pos = 0;
}


/* Memory refresh read - called by reads and writes on DMA channel 0. */
void
refreshread(void) {
    if (machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].rspeed > 4772728)
	wait(8, 1);	/* Insert extra wait states. */

    /* Do the actual refresh stuff. */
    /* If there's no extra cycles left to consume, return. */
    if (!(fetchcycles & 3))
	return;
    /* If the prefetch queue is full, return. */
    if (pfq_pos >= pfq_size)
	return;
    /* Subtract from 1 to 8 cycles. */
    wait(8 - (fetchcycles % 7), 1);
    /* Write to the prefetch queue. */
    pfq_write();
    /* Add those cycles to fetchcycles. */
    fetchcycles += (4 - (fetchcycles & 3));
}


/* Preparation of the various arrays needed to speed up the MOD and R/M work. */
static void
makemod1table(void)
{
    mod1add[0][0] = &BX;
    mod1add[0][1] = &BX;
    mod1add[0][2] = &BP;
    mod1add[0][3] = &BP;
    mod1add[0][4] = &SI;
    mod1add[0][5] = &DI;
    mod1add[0][6] = &BP;
    mod1add[0][7] = &BX;
    mod1add[1][0] = &SI;
    mod1add[1][1] = &DI;
    mod1add[1][2] = &SI;
    mod1add[1][3] = &DI;
    mod1add[1][4] = &zero;
    mod1add[1][5] = &zero;
    mod1add[1][6] = &zero;
    mod1add[1][7] = &zero;
    mod1seg[0] = &ds;
    mod1seg[1] = &ds;
    mod1seg[2] = &ss;
    mod1seg[3] = &ss;
    mod1seg[4] = &ds;
    mod1seg[5] = &ds;
    mod1seg[6] = &ss;
    mod1seg[7] = &ds;
    opseg[0] = &es;
    opseg[1] = &cs;
    opseg[2] = &ss;
    opseg[3] = &ds;
    _opseg[0] = &_es;
    _opseg[1] = &_cs;
    _opseg[2] = &_ss;
    _opseg[3] = &_ds;
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

    wait(3, 0);

    if (!cpu_mod && (cpu_rm == 6)) {
	wait(2, 0);
	cpu_state.eaaddr = pfq_fetchw();
	easeg = ds;
	wait(1, 0);
    } else {
	switch (cpu_rm) {
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
	easeg = *mod1seg[cpu_rm];

	switch (cpu_mod) {
		case 1:
			wait(4, 0);
			cpu_state.eaaddr += (uint16_t) (int8_t) pfq_fetchb();
			break;
		case 2:
			wait(4, 0);
			cpu_state.eaaddr += pfq_fetchw();
			break;
	}
	wait(2, 0);
    }

    cpu_state.eaaddr &= 0xffff;
    cpu_state.last_ea = cpu_state.eaaddr;

    if (ovr_seg)
	easeg = *ovr_seg;
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
    if (cpu_mod == 3) {
	return (getr8(cpu_rm));
    }

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
    if (bits == 16)
	cpu_data = readmemw(easeg, (cpu_state.eaaddr + 2) & 0xffff);
    else
	cpu_data = readmemb(easeg + ((cpu_state.eaaddr + 2) & 0xffff));
}


/* Writes a byte to the effective address. */
static void
seteab(uint8_t val)
{
    if (cpu_mod == 3) {
	setr8(cpu_rm, val);
    } else
	writememb(easeg + cpu_state.eaaddr, val);
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

/* Prepare the ZNP table needed to speed up the setting of the Z, N, and P flags. */
static void
makeznptable(void)
{
    int c, d;
    for (c = 0; c < 256; c++) {
	d = 0;
	if (c & 1)
		d++;
	if (c & 2)
		d++;
	if (c & 4)
		d++;
	if (c & 8)
		d++;
	if (c & 16)
		d++;
	if (c & 32)
		d++;
	if (c & 64)
		d++;
	if (c & 128)
		d++;
	if (d & 1)
		znptable8[c] = 0;
	else
		znptable8[c] = P_FLAG;
#ifdef ENABLE_808X_LOG
	if (c == 0xb1)
		x808x_log("znp8 b1 = %i %02X\n", d, znptable8[c]);
#endif
	if (!c)
		znptable8[c] |= Z_FLAG;
	if (c & 0x80)
		znptable8[c] |= N_FLAG;
    }

    for (c = 0; c < 65536; c++) {
	d = 0;
	if (c & 1)
		d++;
	if (c & 2)
		d++;
	if (c & 4)
		d++;
	if (c & 8)
		d++;
	if (c & 16)
		d++;
	if (c & 32)
		d++;
	if (c & 64)
		d++;
	if (c & 128)
		d++;
	if (d & 1)
		znptable16[c] = 0;
	else
		znptable16[c] = P_FLAG;
#ifdef ENABLE_808X_LOG
	if (c == 0xb1)
		x808x_log("znp16 b1 = %i %02X\n", d, znptable16[c]);
	if (c == 0x65b1)
		x808x_log("znp16 65b1 = %i %02X\n", d, znptable16[c]);
#endif
	if (!c)
		znptable16[c] |= Z_FLAG;
	if (c & 0x8000)
		znptable16[c] |= N_FLAG;
    }
}


/* Common reset function. */
static void
reset_common(int hard)
{
    if (hard) {
#ifdef ENABLE_808X_LOG
	x808x_log("x86 reset\n");
#endif
	ins = 0;
    }
    use32 = 0;
    cpu_cur_status = 0;
    stack32 = 0;
    msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
    msw = 0;
    if (is486)
	cr0 = 1 << 30;
    else
	cr0 = 0;
    cpu_cache_int_enabled = 0;
    cpu_update_waitstates();
    cr4 = 0;
    eflags = 0;
    cgate32 = 0;
    if (AT) {
	loadcs(0xF000);
	cpu_state.pc = 0xFFF0;
	rammask = cpu_16bitbus ? 0xFFFFFF : 0xFFFFFFFF;
    } else {
	loadcs(0xFFFF);
	cpu_state.pc=0;
	rammask = 0xfffff;
    }
    idt.base = 0;
    idt.limit = is386 ? 0x03FF : 0xFFFF;
    flags = 2;
    trap = 0;
    ovr_seg = NULL;
    in_lock = halt = 0;

    if (hard) {
	makeznptable();
	resetreadlookup();
	makemod1table();
	resetmcr();
	pfq_clear();
	cpu_set_edx();
	EAX = 0;
	ESP = 0;
	mmu_perm = 4;
	pfq_size = (is8086) ? 6 : 4;
    }

    x86seg_reset();
#ifdef USE_DYNAREC
    if (hard)
	codegen_reset();
#endif
    x86_was_reset = 1;
    port_92_clear_reset();
}


/* Hard reset. */
void
resetx86(void)
{
    reset_common(1);
}


/* Soft reset. */
void
softresetx86(void)
{
    reset_common(0);
}


/* Pushes a word to the stack. */
static void
push_ex(uint16_t val)
{
    writememw(ss, (SP & 0xFFFF), val);
    cpu_state.last_ea = SP;
}


static void
push(uint16_t val)
{
    SP -= 2;
    push_ex(val);
}


/* Pops a word from the stack. */
static uint16_t
pop(void)
{
    uint16_t tempw;

    tempw = readmemw(ss, SP);
    SP += 2;
    cpu_state.last_ea = SP;
    return tempw;
}


static void
access(int num, int bits)
{
    switch (num) {
	case 0: case 61: case 63: case 64:
	case 67: case 69: case 71: case 72:
	default:
		break;
	case 1: case 6: case 8: case 9:
	case 17: case 20: case 21: case 24:
	case 28: case 55: case 56:
		wait(1 + (cycles % 3), 0);
		break;
	case 2: case 15: case 22: case 23:
	case 25: case 26: case 46: case 53:
		wait(2 + (cycles % 3), 0);
		break;
	case 3: case 44: case 45: case 52:
	case 54:
		wait(2 + (cycles & 1), 0);
		break;
	case 4:
		wait(5 + (cycles & 1), 0);
		break;
	case 5:
		if (opcode == 0xcc)
			wait(7 + (cycles % 3), 0);
		else
			wait(4 + (cycles & 1), 0);
		break;
	case 7: case 47: case 48: case 49:
	case 50: case 51:
		wait(1 + (cycles % 4), 0);
		break;
	case 10: case 11: case 18: case 19:
	case 43:
		wait(3 + (cycles % 3), 0);
		break;
	case 12: case 13: case 14: case 29:
	case 30: case 33:
		wait(4 + (cycles % 3), 0);
		break;
	case 16:
		if (!(opcode & 1) && (cycles & 1))
			wait(1, 0);
		/* Fall through. */
	case 42:
		wait(3 + (cycles & 1), 0);
		break;
	case 27: case 32: case 37:
		wait(3, 0);
		break;
	case 31:
		wait(6 + (cycles % 3), 0);
		break;
	case 34: case 39: case 41: case 60:
		wait(4, 0);
		break;
	case 35:
		wait(2, 0);
		break;
	case 36:
		wait(5 + (cycles & 1), 0);
		if (cpu_mod != 3)
			wait(1, 0);
		break;
	case 38:
		wait(5 + (cycles % 3), 0);
		break;
	case 40:
		wait(6, 0);
		break;
	case 57:
		if (cpu_mod != 3)
			wait(2, 0);
		wait(4 + (cycles & 1), 0);
		break;
	case 58:
		if (cpu_mod != 3)
			wait(1, 0);
		wait(4 + (cycles & 1), 0);
		break;
	case 59:
		if (cpu_mod != 3)
			wait(1, 0);
		wait(5 + (cycles & 1), 0);
		break;
	case 62:
		wait(1, 0);
		break;
	case 65:
		wait(3 + (cycles & 1), 0);
		if (cpu_mod != 3)
			wait(1, 0);
		break;
	case 70:
		wait(5, 0);
		break;
    }
}


/* Calls an interrupt. */
static void
interrupt(uint16_t addr, int cli)
{
    uint16_t old_cs, old_ip;
    uint16_t new_cs, new_ip;

    addr <<= 2;
    old_cs = CS;
    access(5, 16);
    new_ip = readmemw(0, addr);
    wait(1, 0);
    access(6, 16);
    new_cs = readmemw(0, (addr + 2) & 0xffff);
    access(39, 16);
    push(flags & 0x0fd7);
    if (cli)
	flags &= ~I_FLAG;
    flags &= ~T_FLAG;
    access(40, 16);
    push(old_cs);
    old_ip = cpu_state.pc;
    loadcs(new_cs);
    access(68, 16);
    cpu_state.pc = new_ip;
    access(41, 16);
    push(old_ip);
    pfq_clear();
}


static int
rep_action(int *completed, int *repeating, int in_rep, int bits)
{
    uint16_t t;

    if (in_rep == 0)
	return 0;
    wait(2, 0);
    t = CX;
    if (irq_pending()) {
	access(71, bits);
	pfq_clear();
	cpu_state.pc = cpu_state.pc - 2;
	t = 0;
    }
    if (t == 0) {
	wait(1, 0);
	*completed = 1;
	*repeating = 0;
	return 1;
    }
    --CX;
    *completed = 0;
    wait(2, 0);
    if (!*repeating)
	wait(2, 0);
    return 0;
}


static uint16_t
jump(uint16_t delta)
{
    uint16_t old_ip;
    access(67, 8);
    wait(5, 0);
    old_ip = cpu_state.pc;
    cpu_state.pc = (cpu_state.pc + delta) & 0xffff;
    pfq_clear();
    return old_ip;
}


static uint16_t
sign_extend(uint8_t data)
{
    return data + (data < 0x80 ? 0 : 0xff00);
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
    if ((!cond) == (opcode & 0x01))
	jump_short();
}


static void
set_cf(int cond)
{
    flags = (flags & ~C_FLAG) | (cond ? C_FLAG : 0);
}


static void
set_if(int cond)
{
    flags = (flags & ~I_FLAG) | (cond ? I_FLAG : 0);
}


static void
set_df(int cond)
{
    flags = (flags & ~D_FLAG) | (cond ? D_FLAG : 0);
}


static void
bitwise(int bits, uint16_t data)
{
    cpu_data = data;
    flags &= ~(C_FLAG | A_FLAG | V_FLAG);
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
    flags = (flags & ~0x800) | (of ? 0x800 : 0);
}


static int
top_bit(uint16_t w, int bits)
{
    if (bits == 16)
	return ((w & 0x8000) != 0);
    else
	return ((w & 0x80) != 0);
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
    flags = (flags & ~0x10) | (af ? 0x10 : 0);
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
    if ((cpu_alu_op == 2) && !(cpu_src & size_mask) && (flags & C_FLAG))
	flags |= C_FLAG;
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
    if ((cpu_alu_op == 3) && !(cpu_src & size_mask) && (flags & C_FLAG))
	flags |= C_FLAG;
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
		if (flags & C_FLAG)
			cpu_src++;
		/* Fall through. */
	case 0:
		add(bits);
		break;
	case 3:
		if (flags & C_FLAG)
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
    flags = (flags & ~0x80) | (top_bit(cpu_data, bits) ? 0x80 : 0);
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

    flags = (flags & ~4) | table[cpu_data & 0xff];
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
		carry = !!(flags & C_FLAG);
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
}


static void
set_of_rotate(int bits)
{
    set_of(top_bit(cpu_data ^ cpu_dest, bits));
}


static void
set_zf(int bits)
{
    int size_mask = (1 << bits) - 1;

    flags = (flags & ~0x40) | (((cpu_data & size_mask) == 0) ? 0x40 : 0);
}


static void
set_pzs(int bits)
{
    set_pf();
    set_zf(bits);
    set_sf(bits);
}


static void
set_co_mul(int carry)
{
    set_cf(carry);
    set_of(carry);
    if (!carry)
	wait(1, 0);
}


static int
div(uint16_t l, uint16_t h)
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
	interrupt(0, 1);
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
		interrupt(0, 1);
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


static void
lods(int bits)
{
    if (bits == 16)
	cpu_data = readmemw((ovr_seg ? *ovr_seg : ds), SI);
    else
	cpu_data = readmemb((ovr_seg ? *ovr_seg : ds) + SI);
    if (flags & D_FLAG)
	SI -= (bits >> 3);
    else
	SI += (bits >> 3);
}


static void
stos(int bits)
{
    if (bits == 16)
	writememw(es, DI, cpu_data);
    else
	writememb(es + DI, (uint8_t) (cpu_data & 0xff));
    if (flags & D_FLAG)
	DI -= (bits >> 3);
    else
	DI += (bits >> 3);
}


static void
da(void)
{
    set_pzs(8);
    wait(2, 0);
}


static void
aa(void)
{
    set_of(0);
    AL &= 0x0f;
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


/* Executes instructions up to the specified number of cycles. */
void
execx86(int cycs)
{
    uint8_t temp = 0, temp2;
    uint16_t addr, tempw;
    uint16_t new_cs, new_ip;
    int bits, completed;
    int in_rep, repeating;
    int oldc;

    cycles += cycs;

    while (cycles > 0) {
	timer_start_period(cycles * xt_cpu_multi);
	cpu_state.oldpc = cpu_state.pc;
	in_rep = repeating = 0;
	completed = 0;

opcodestart:
	if (halt) {
		wait(2, 0);
		goto on_halt;
	}

	if (!repeating) {
		opcode = pfq_fetchb();
		oldc = flags & C_FLAG;
		trap = flags & T_FLAG;
		wait(1, 0);

		/* if (!in_rep && !ovr_seg && (CS < 0xf000))
			pclog("%04X:%04X %02X\n", CS, (cpu_state.pc - 1) & 0xFFFF, opcode); */
	}

	switch (opcode) {
		case 0x06: case 0x0E: case 0x16: case 0x1E:	/* PUSH seg */
			access(29, 16);
			push(_opseg[(opcode >> 3) & 0x03]->seg);
			break;
		case 0x07: case 0x0F: case 0x17: case 0x1F:	/* POP seg */
			access(22, 16);
			if (opcode == 0x0F) {
				loadcs(pop());
				pfq_clear();
			} else
				loadseg(pop(), _opseg[(opcode >> 3) & 0x03]);
			wait(1, 0);
			noint = 1;
			break;

		case 0x26:	/*ES:*/
		case 0x2E:	/*CS:*/
		case 0x36:	/*SS:*/
		case 0x3E:	/*DS:*/
			wait(1, 0);
			ovr_seg = opseg[(opcode >> 3) & 0x03];
			goto opcodestart;

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
			if (opcode & 1)
				tempw = geteaw();
			else
				tempw = geteab();
			cpu_alu_op = (opcode >> 3) & 7;
			if ((opcode & 2) == 0) {
				cpu_dest = tempw;
				cpu_src = (opcode & 1) ? cpu_state.regs[cpu_reg].w : getr8(cpu_reg);
			} else {
				cpu_dest = (opcode & 1) ? cpu_state.regs[cpu_reg].w : getr8(cpu_reg);
				cpu_src = tempw;
			}
			if (cpu_mod != 3)
				wait(2, 0);
			wait(1, 0);
			alu_op(bits);
			if (cpu_alu_op != 7) {
				if ((opcode & 2) == 0) {
					access(10, bits);
					if (opcode & 1)
						seteaw(cpu_data);
					else
						seteab((uint8_t) (cpu_data & 0xff));
					if (cpu_mod == 3)
						wait(1, 0);
				} else {
					if (opcode & 1)
						cpu_state.regs[cpu_reg].w = cpu_data;
					else
					       setr8(cpu_reg, (uint8_t) (cpu_data & 0xff));
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
			if (opcode & 1) {
				cpu_data = pfq_fetchw();
				cpu_dest = AX;
			} else {
				cpu_data = pfq_fetchb();
				cpu_dest = AL;
			}
			cpu_src = cpu_data;
			cpu_alu_op = (opcode >> 3) & 7;
			alu_op(bits);
			if (cpu_alu_op != 7) {
				if (opcode & 1)
					AX = cpu_data;
				else
					AL = (uint8_t) (cpu_data & 0xff);
			}
			wait(1, 0);
			break;

		case 0x27:	/*DAA*/
			wait(1, 0);
			if ((flags & A_FLAG) || (AL & 0x0f) > 9) {
				cpu_data = AL + 6;
				AL = (uint8_t) cpu_data;
				set_af(1);
				if ((cpu_data & 0x100) != 0)
					set_cf(1);
			}
			if ((flags & C_FLAG) || AL > 0x9f) {
				AL += 0x60;
				set_cf(1);
			}
			da();
			break;
		case 0x2F:	/*DAS*/
			wait(1, 0);
			temp = AL;
			if ((flags & A_FLAG) || ((AL & 0xf) > 9)) {
				cpu_data = AL - 6;
				AL = (uint8_t) cpu_data;
				set_af(1);
				if ((cpu_data & 0x100) != 0)
					set_cf(1);
			}
			if ((flags & C_FLAG) || temp > 0x9f) {
				AL -= 0x60;
				set_cf(1);
			}
			da();
			break;
		case 0x37:	/*AAA*/
			wait(1, 0);
			if ((flags & A_FLAG) || ((AL & 0xf) > 9)) {
				AL += 6;
				++AH;
				set_ca();
			} else {
				clear_ca();
				wait(1, 0);
			}
			aa();
			break;
		case 0x3F: /*AAS*/
			wait(1, 0);
			if ((flags & A_FLAG) || ((AL & 0xf) > 9)) {
				AL -= 6;
				--AH;
				set_ca();
			} else {
				clear_ca();
				wait(1, 0);
			}
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
			if (opcode == 0x54) {
				SP -= 2;
				push_ex(cpu_state.regs[opcode & 0x07].w);
			} else
				push(cpu_state.regs[opcode & 0x07].w);
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
			jcc(opcode, flags & V_FLAG);
			break;
		case 0x62:	/*JB alias*/
		case 0x72:	/*JB*/
		case 0x63:	/*JNB alias*/
		case 0x73:	/*JNB*/
			jcc(opcode, flags & C_FLAG);
			break;
		case 0x64:	/*JE alias*/
		case 0x74:	/*JE*/
		case 0x65:	/*JNE alias*/
		case 0x75:	/*JNE*/
			jcc(opcode, flags & Z_FLAG);
			break;
		case 0x66:	/*JBE alias*/
		case 0x76:	/*JBE*/
		case 0x67:	/*JNBE alias*/
		case 0x77:	/*JNBE*/
			jcc(opcode, flags & (C_FLAG | Z_FLAG));
			break;
		case 0x68:	/*JS alias*/
		case 0x78:	/*JS*/
		case 0x69:	/*JNS alias*/
		case 0x79:	/*JNS*/
			jcc(opcode, flags & N_FLAG);
			break;
		case 0x6A:	/*JP alias*/
		case 0x7A:	/*JP*/
		case 0x6B: /*JNP alias*/
		case 0x7B: /*JNP*/
			jcc(opcode, flags & P_FLAG);
			break;
		case 0x6C:	/*JL alias*/
		case 0x7C:	/*JL*/
		case 0x6D:	/*JNL alias*/
		case 0x7D:	/*JNL*/
			temp = (flags & N_FLAG) ? 1 : 0;
			temp2 = (flags & V_FLAG) ? 1 : 0;
			jcc(opcode, temp ^ temp2);
			break;
		case 0x6E:	/*JLE alias*/
		case 0x7E:	/*JLE*/
		case 0x6F:	/*JNLE alias*/
		case 0x7F:	/*JNLE*/
			temp = (flags & N_FLAG) ? 1 : 0;
			temp2 = (flags & V_FLAG) ? 1 : 0;
			jcc(opcode, (flags & Z_FLAG) || (temp != temp2));
			break;

		case 0x80: case 0x81: case 0x82: case 0x83:
			/* alu rm, imm */
			bits = 8 << (opcode & 1);
			do_mod_rm();
			access(47, bits);
			if (opcode & 1)
				cpu_data = geteaw();
			else
				cpu_data = geteab();
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
				if (opcode & 1)
					seteaw(cpu_data);
				else
					seteab((uint8_t) (cpu_data & 0xff));
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
			if (opcode & 1) {
				cpu_data = geteaw();
				test(bits, cpu_data, cpu_state.regs[cpu_reg].w);
			} else {
				cpu_data = geteab();
				test(bits, cpu_data, getr8(cpu_reg));
			}
			if (cpu_mod == 3)
				wait(2, 0);
			wait(2, 0);
			break;
		case 0x86: case 0x87:
			/* XCHG rm, reg */
			bits = 8 << (opcode & 1);
			do_mod_rm();
			access(49, bits);
			if (opcode & 1) {
				cpu_data = geteaw();
				cpu_src = cpu_state.regs[cpu_reg].w;
				cpu_state.regs[cpu_reg].w = cpu_data;
			} else {
				cpu_data = geteab();
				cpu_src = getr8(cpu_reg);
				setr8(cpu_reg, cpu_data);
			}
			wait(3, 0);
			access(12, bits);
			if (opcode & 1)
				seteaw(cpu_src);
			else
				seteab((uint8_t) (cpu_src & 0xff));
			break;

		case 0x88: case 0x89:
			/* MOV rm, reg */
			bits = 8 << (opcode & 1);
			do_mod_rm();
			wait(1, 0);
			access(13, bits);
			if (opcode & 1)
				seteaw(cpu_state.regs[cpu_reg].w);
			else
				seteab(getr8((uint8_t) (cpu_reg & 0xff)));
			break;
		case 0x8A: case 0x8B:
			/* MOV reg, rm */
			bits = 8 << (opcode & 1);
			do_mod_rm();
			access(50, bits);
			if (opcode & 1)
				cpu_state.regs[cpu_reg].w = geteaw();
			else
				setr8(cpu_reg, geteab());
			wait(1, 0);
			if (cpu_mod != 3)
				wait(2, 0);
			break;

		case 0x8C:	/*MOV w,sreg*/
			do_mod_rm();
			if (cpu_mod == 3)
				wait(1, 0);
			access(14, 16);
			switch (rmdat & 0x38) {
				case 0x00:	/*ES*/
					seteaw(ES);
					break;
				case 0x08:	/*CS*/
					seteaw(CS);
					break;
				case 0x18:	/*DS*/
					seteaw(DS);
					break;
				case 0x10:	/*SS*/
					seteaw(SS);
					break;
			}
			break;

		case 0x8D:	/*LEA*/
			do_mod_rm();
			cpu_state.regs[cpu_reg].w = (cpu_mod == 3) ? cpu_state.last_ea : cpu_state.eaaddr;
			wait(1, 0);
			if (cpu_mod != 3)
				wait(2, 0);
			break;

		case 0x8E:	/*MOV sreg,w*/
			do_mod_rm();
			access(51, 16);
			tempw = geteaw();
			switch (rmdat & 0x38) {
				case 0x00:	/*ES*/
					loadseg(tempw, &_es);
					break;
				case 0x08:	/*CS - 8088/8086 only*/
					loadcs(tempw);
					pfq_clear();
					break;
				case 0x18:	/*DS*/
					loadseg(tempw, &_ds);
					break;
				case 0x10:	/*SS*/
					loadseg(tempw, &_ss);
					break;
			}
			wait(1, 0);
			if (cpu_mod != 3)
				wait(2, 0);
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
			access(31, 16);
			push(CS);
			access(60, 16);
			cpu_state.oldpc = cpu_state.pc;
			loadcs(new_cs);
			cpu_state.pc = new_ip;
			access(32, 16);
			push(cpu_state.oldpc);
			pfq_clear();
			break;
		case 0x9B:	/*WAIT*/
			wait(4, 0);
			break;
		case 0x9C:	/*PUSHF*/
			access(33, 16);
			push((flags & 0x0fd7) | 0xf000);
			break;
		case 0x9D:	/*POPF*/
			access(25, 16);
			flags = pop() | 2;
			wait(1, 0);
			break;
		case 0x9E:	/*SAHF*/
			wait(1, 0);
			flags = (flags & 0xff02) | AH;
			wait(2, 0);
			break;
		case 0x9F:	/*LAHF*/
			wait(1, 0);
			AH = flags & 0xd7;
			break;

		case 0xA0: case 0xA1:
			/* MOV A, [iw] */
			bits = 8 << (opcode & 1);
			wait(1, 0);
			addr = pfq_fetchw();
			access(1, bits);
			if (opcode & 1)
				AX = readmemw((ovr_seg ? *ovr_seg : ds), addr);
			else
				AL = readmemb((ovr_seg ? *ovr_seg : ds) + addr);
			wait(1, 0);
			break;
		case 0xA2: case 0xA3:
			/* MOV [iw], A */
			bits = 8 << (opcode & 1);
			wait(1, 0);
			addr = pfq_fetchw();
			access(7, bits);
			if (opcode & 1)
				writememw((ovr_seg ? *ovr_seg : ds), addr, AX);
			else
				writememb((ovr_seg ? *ovr_seg : ds) + addr, AL);
			break;

		case 0xA4: case 0xA5:	/* MOVS */
		case 0xAC: case 0xAD:	/* LODS */
			bits = 8 << (opcode & 1);
			if (!repeating) {
				wait(1 /*2*/, 0);
				if ((opcode & 8) == 0 && in_rep != 0)
					wait(1, 0);
			}
			if (rep_action(&completed, &repeating, in_rep, bits)) {
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
				if (opcode & 1)
					AX = cpu_data;
				else
					AL = (uint8_t) (cpu_data & 0xff);
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
			timer_end_period(cycles * xt_cpu_multi);
			goto opcodestart;

		case 0xA6: case 0xA7:	/* CMPS */
		case 0xAE: case 0xAF:	/* SCAS */
			bits = 8 << (opcode & 1);
			if (!repeating)
				wait(1, 0);
			if (rep_action(&completed, &repeating, in_rep, bits)) {
				wait(2, 0);
				break;
			}
			if (in_rep != 0)
				wait(1, 0);
			if (opcode & 1)
				cpu_dest = AX;
			else
				cpu_dest = AL;
			if ((opcode & 8) == 0) {
				access(21, bits);
				lods(bits);
				wait(1, 0);
				cpu_dest = cpu_data;
			}
			access(2, bits);
			if (opcode & 1)
				cpu_data = readmemw(es, DI);
			else
				cpu_data = readmemb(es + DI);
			if (flags & D_FLAG)
				DI -= (bits >> 3);
			else
				DI += (bits >> 3);
			cpu_src = cpu_data;
			sub(bits);
			wait(2, 0);
			if (in_rep == 0) {
				wait(3, 0);
				break;
			}
			if ((!!(flags & Z_FLAG)) == (in_rep == 1)) {
				wait(4, 0);
				break;
			}
			repeating = 1;
			timer_end_period(cycles * xt_cpu_multi);
			goto opcodestart;

		case 0xA8: case 0xA9:
			/* TEST A, imm */
			bits = 8 << (opcode & 1);
			wait(1, 0);
			if (opcode & 1) {
				cpu_data = pfq_fetchw();
				test(bits, AX, cpu_data);
			} else {
				cpu_data = pfq_fetchb();
				test(bits, AL, cpu_data);
			}
			wait(1, 0);
			break;

		case 0xAA: case 0xAB:	/* STOS */
			bits = 8 << (opcode & 1);
			if (!repeating) {
				if (opcode & 1)
					wait(1, 0);
				if (in_rep != 0)
					wait(1, 0);
			}
			if (rep_action(&completed, &repeating, in_rep, bits)) {
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
			timer_end_period(cycles * xt_cpu_multi);
			goto opcodestart;

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
			loadcs(new_cs);
			access(72, bits);
			cpu_state.pc = new_ip;
			pfq_clear();
			break;

		case 0xC4: case 0xC5:
			/* LsS rw, rmd */
			do_mod_rm();
			bits = 16;
			access(52, bits);
			cpu_state.regs[cpu_reg].w = readmemw(easeg, cpu_state.eaaddr);
			tempw = readmemw(easeg, (cpu_state.eaaddr + 2) & 0xFFFF);
			loadseg(tempw, (opcode & 0x01) ? &_ds : &_es);
			wait(1, 0);
			noint = 1;
			break;

		case 0xC6: case 0xC7:
			/* MOV rm, imm */
			bits = 8 << (opcode & 1);
			do_mod_rm();
			wait(1, 0);
			if (cpu_mod != 3)
				wait(2, 0);
			if (opcode & 1)
				cpu_data = pfq_fetchw();
			else
				cpu_data = pfq_fetchb();
			if (cpu_mod == 3)
				wait(1, 0);
			access(16, bits);
			if (opcode & 1)
				seteaw(cpu_data);
			else
				seteab((uint8_t) (cpu_data & 0xff));
			break;

		case 0xCC:	/*INT 3*/
			interrupt(3, 1);
			break;
		case 0xCD:	/*INT*/
			wait(1, 0);
			interrupt(pfq_fetchb(), 1);
			break;
		case 0xCE:	/*INTO*/
			wait(3, 0);
			if (flags & V_FLAG) {
				wait(2, 0);
				interrupt(4, 1);
			}
			break;

		case 0xCF:	/*IRET*/
			access(43, 8);
			new_ip = pop();
			wait(3, 0);
			access(44, 8);
			new_cs = pop();
			loadcs(new_cs);
			access(62, 8);
			cpu_state.pc = new_ip;
			access(45, 8);
			flags = pop() | 2;
			wait(5, 0);
			noint = 1;
			nmi_enable = 1;
			pfq_clear();
			break;

		case 0xD0: case 0xD1: case 0xD2: case 0xD3:
			/* rot rm */
			bits = 8 << (opcode & 1);
			do_mod_rm();
			if (cpu_mod == 3)
				wait(1, 0);
			access(53, bits);
			if (opcode & 1)
				cpu_data = geteaw();
			else
				cpu_data = geteab();
			if ((opcode & 2) == 0) {
				cpu_src = 1;
				wait((cpu_mod != 3) ? 4 : 0, 0);
			} else {
				cpu_src = CL;
				wait((cpu_mod != 3) ? 9 : 6, 0);
			}
			while (cpu_src != 0) {
				cpu_dest = cpu_data;
				oldc = flags & C_FLAG;
				switch (rmdat & 0x38) {
					case 0x00:	/* ROL */
						set_cf(top_bit(cpu_data, bits));
						cpu_data <<= 1;
						cpu_data |= ((flags & C_FLAG) ? 1 : 0);
						set_of_rotate(bits);
						break;
					case 0x08:	/* ROR */
						set_cf((cpu_data & 1) != 0);
						cpu_data >>= 1;
						if (flags & C_FLAG)
							cpu_data |= (!(opcode & 1) ? 0x80 : 0x8000);
						set_of_rotate(bits);
						break;
					case 0x10:	/* RCL */
						set_cf(top_bit(cpu_data, bits));
						cpu_data = (cpu_data << 1) | (oldc ? 1 : 0);
						set_of_rotate(bits);
						break;
					case 0x18: 	/* RCR */
						set_cf((cpu_data & 1) != 0);
						cpu_data >>= 1;
						if (oldc)
							cpu_data |= (!(opcode & 0x01) ? 0x80 : 0x8000);
						set_cf((cpu_dest & 1) != 0);
						set_of_rotate(bits);
						break;
					case 0x20:	/* SHL */
						set_cf(top_bit(cpu_data, bits));
						cpu_data <<= 1;
						set_of_rotate(bits);
						set_pzs(bits);
						break;
					case 0x28:	/* SHR */
						set_cf((cpu_data & 1) != 0);
						cpu_data >>= 1;
						set_of_rotate(bits);
						set_af(1);
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
						set_af(1);
						set_pzs(bits);
						break;
				}
				if ((opcode & 2) != 0)
					wait(4, 0);
				--cpu_src;
			}
			access(17, bits);
			if (opcode & 1)
				seteaw(cpu_data);
			else
				seteab((uint8_t) (cpu_data & 0xff));
			break;

		case 0xD4:	/*AAM*/
			wait(1, 0);
			cpu_src = pfq_fetchb();
			if (div(AL, 0))
				set_pzs(16);
			break;
		case 0xD5:	/*AAD*/
			wait(1, 0);
			mul(pfq_fetchb(), AH);
			AL += cpu_data;
			AH = 0x00;
			set_pzs(16);
			break;
		case 0xD6:	/*SALC*/
			wait(1, 0);
			AL = (flags & C_FLAG) ? 0xff : 0x00;
			wait(1, 0);
			break;
		case 0xD7:	/*XLATB*/
			addr = BX + AL;
			cpu_state.last_ea = addr;
			access(4, 8);
			AL = readmemb((ovr_seg ? *ovr_seg : ds) + addr);
			wait(1, 0);
			break;

		case 0xD8: case 0xD9: case 0xDA: case 0xDB:
		case 0xDD: case 0xDC: case 0xDE: case 0xDF:
			/* esc i, r, rm */
			do_mod_rm();
			access(54, 16);
			geteaw();
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
						if (flags & Z_FLAG)
							oldc = 0;
						break;
					case 0xE1:
						if (!(flags & Z_FLAG))
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
			if ((opcode & 2) == 0) {
				access(3, bits);
				if ((opcode & 1) && is8086 && !(cpu_data & 1)) {
					AX = inw(cpu_data);
					wait(4, 1);		/* I/O access and wait state. */
				} else {
					AL = inb(cpu_data);
					if (opcode & 1)
						AH = inb(temp + 1);
					wait(bits >> 1, 1);	/* I/O access. */
				}
				wait(1, 0);
			} else {
				if ((opcode & 8) == 0)
					access(8, bits);
				else
					access(9, bits);
				if ((opcode & 1) && is8086 && !(cpu_data & 1)) {
					outw(cpu_data, AX);
					wait(4, 1);
				} else {
					outb(cpu_data, AL);
					if (opcode & 1)
						outb(cpu_data + 1, AH);
					wait(bits >> 1, 1);	/* I/O access. */
				}
			}
			break;

		case 0xE8:	/*CALL rel 16*/
			wait(1, 0);
			cpu_state.oldpc = jump_near();
			access(34, 8);
			push(cpu_state.oldpc);
			pfq_clear();
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
			loadcs(tempw);
			access(70, 8);
			cpu_state.pc = addr;
			pfq_clear();
			break;
		case 0xEB:	/*JMP rel*/
			wait(1, 0);
			cpu_data = (int8_t) pfq_fetchb();
			jump_short();
			wait(1, 0);
			pfq_clear();
			break;

		case 0xF0: case 0xF1:	/*LOCK - F1 is alias*/
			in_lock = 1;
			wait(1, 0);
			goto opcodestart;

		case 0xF2:	/*REPNE*/
		case 0xF3:	/*REPE*/
			wait(1, 0);
			in_rep = (opcode == 0xf2 ? 1 : 2);
			repeating = 0;
			completed = 0;
			goto opcodestart;

		case 0xF4:	/*HLT*/
			halt = 1;
			pfq_clear();
			wait(2, 0);
			break;
		case 0xF5:	/*CMC*/
			wait(1, 0);
			flags ^= C_FLAG;
			break;

		case 0xF6: case 0xF7:
			bits = 8 << (opcode & 1);
			do_mod_rm();
			access(55, bits);
			if (opcode & 1)
				cpu_data = geteaw();
			else
				cpu_data = geteab();
			switch (rmdat & 0x38) {
				case 0x00: case 0x08:
					/* TEST */
					wait(2, 0);
					if (cpu_mod != 3)
						wait(1, 0);
					if (opcode & 1)
						cpu_src = pfq_fetchw();
					else
						cpu_src = pfq_fetchb();
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
					if (opcode & 1)
						seteaw(cpu_data);
					else
						seteab((uint8_t) (cpu_data & 0xff));
					break;
				case 0x20:	/* MUL */
				case 0x28:	/* IMUL */
					wait(1, 0);
					if (opcode & 1) {
						mul(AX, cpu_data);
						AX = cpu_data;
						DX = cpu_dest;
						cpu_data |= DX;
						set_co_mul((DX != ((AX & 0x8000) == 0) || ((rmdat & 0x38) == 0x20) ? 0 : 0xffff));
					} else {
						mul(AL, cpu_data);
						AL = (uint8_t) cpu_data;
						AH = (uint8_t) cpu_dest;
						set_co_mul(AH != (((AL & 0x80) == 0) || ((rmdat & 0x38) == 0x20) ? 0 : 0xff));
					}
					set_zf(bits);
					if (cpu_mod != 3)
						wait(1, 0);
					break;
				case 0x30:	/* DIV */
				case 0x38:	/* IDIV */
					if (cpu_mod != 3)
						wait(1, 0);
					cpu_src = cpu_data;
					if (div(AL, AH))
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
					if (opcode & 1)
						seteaw(cpu_data);
					else
						seteab((uint8_t) (cpu_data & 0xff));
					break;
				case 0x10:	/* CALL rm */
					if (!(opcode & 1)) {
						if (cpu_mod != 3)
							cpu_data |= 0xff00;
						else
							cpu_data = cpu_state.regs[cpu_rm].w;
					}
					access(63, bits);
					wait(5, 0);
					if (cpu_mod != 3)
						wait(1, 0);
					wait(1, 0);	/* Wait. */
					cpu_state.oldpc = cpu_state.pc;
					cpu_state.pc = cpu_data;
					wait(2, 0);
					access(35, bits);
					push(cpu_state.oldpc);
					pfq_clear();
					break;
				case 0x18:	/* CALL rmd */
					new_ip = cpu_data;
					access(58, bits);
					read_ea2(bits);
					if (!(opcode & 1))
						cpu_data |= 0xff00;
					new_cs = cpu_data;
					access(36, bits);
					push(CS);
					access(64, bits);
					wait(4, 0);
					cpu_state.oldpc = cpu_state.pc;
					loadcs(new_cs);
					cpu_state.pc = new_ip;
					access(37, bits);
					push(cpu_state.oldpc);
					pfq_clear();
					break;
				case 0x20:	/* JMP rm */
					if (!(opcode & 1)) {
						if (cpu_mod != 3)
							cpu_data |= 0xff00;
						else
							cpu_data = cpu_state.regs[cpu_rm].w;
					}
					access(65, bits);
					cpu_state.pc = cpu_data;
					pfq_clear();
					break;
				case 0x28:	/* JMP rmd */
					new_ip = cpu_data;
					access(59, bits);
					read_ea2(bits);
					if (!(opcode & 1))
						cpu_data |= 0xff00;
					new_cs = cpu_data;
					loadcs(new_cs);
					access(66, bits);
					cpu_state.pc = new_ip;
					pfq_clear();
					break;
				case 0x30:	/* PUSH rm */
				case 0x38:
					if (cpu_mod != 3)
						wait(1, 0);
					access(38, bits);
					if ((cpu_mod == 3) && (cpu_rm == 4))
						push(cpu_data - 2);
					else
						push(cpu_data);
					break;
			}
			break;

		default:
			x808x_log("Illegal opcode: %02X\n", opcode);
			pfq_fetchb();
			wait(8, 0);
			break;
	}

	cpu_state.pc &= 0xFFFF;

on_halt:
	if (ovr_seg)
		ovr_seg = NULL;

	if (in_lock)
		in_lock = 0;

	/* FIXME: Find out why this is needed. */
	if (((romset == ROM_IBMPC) && ((cs + cpu_state.pc) == 0xFE545)) || 
	    ((romset == ROM_IBMPC82) && ((cs + cpu_state.pc) == 0xFE4A7))) {
		/* You didn't seriously think I was going to emulate the cassette, did you? */
		CX = 1;
		BX = 0x500;
	}

	timer_end_period(cycles * xt_cpu_multi);

	if (trap && (flags & T_FLAG) && !noint) {
		halt = 0;
		interrupt(1, 1);
	} else if (nmi && nmi_enable && nmi_mask) {
		halt = 0;
		interrupt(2, 1);
		nmi_enable = 0;
	} else if (takeint && !noint) {
		temp = picinterrupt();
		if (temp != 0xFF) {
			halt = 0;
			interrupt(temp, 1);
		}
	}
	takeint = (flags & I_FLAG) && (pic.pend &~ pic.mask);

	if (noint)
		noint = 0;

	ins++;
    }
}
