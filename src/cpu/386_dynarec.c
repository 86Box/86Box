#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#ifndef INFINITY
# define INFINITY   (__builtin_inff())
#endif

#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include "x86_ops.h"
#include "x87.h"
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/machine.h>
#ifdef USE_DYNAREC
#include "codegen.h"
#ifdef USE_NEW_DYNAREC
#include "codegen_backend.h"
#endif
#endif
#include "386_common.h"


#define CPU_BLOCK_END() cpu_block_end = 1


int inrecomp = 0, cpu_block_end = 0;
int cpu_end_block_after_ins = 0;


#ifdef ENABLE_386_DYNAREC_LOG
int x386_dynarec_do_log = ENABLE_386_DYNAREC_LOG;


void
x386_dynarec_log(const char *fmt, ...)
{
    va_list ap;

    if (x386_dynarec_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define x386_dynarec_log(fmt, ...)
#endif


static __inline void fetch_ea_32_long(uint32_t rmdat)
{
	eal_r = eal_w = NULL;
	easeg = cpu_state.ea_seg->base;
	if (cpu_rm == 4)
	{
		uint8_t sib = rmdat >> 8;
		
		switch (cpu_mod)
		{
			case 0: 
			cpu_state.eaaddr = cpu_state.regs[sib & 7].l; 
			cpu_state.pc++; 
			break;
			case 1: 
			cpu_state.pc++;
			cpu_state.eaaddr = ((uint32_t)(int8_t)getbyte()) + cpu_state.regs[sib & 7].l; 
			break;
			case 2: 
			cpu_state.eaaddr = (fastreadl(cs + cpu_state.pc + 1)) + cpu_state.regs[sib & 7].l; 
			cpu_state.pc += 5; 
			break;
		}
		/*SIB byte present*/
		if ((sib & 7) == 5 && !cpu_mod) 
			cpu_state.eaaddr = getlong();
		else if ((sib & 6) == 4 && !cpu_state.ssegs)
		{
			easeg = ss;
			cpu_state.ea_seg = &cpu_state.seg_ss;
		}
		if (((sib >> 3) & 7) != 4) 
			cpu_state.eaaddr += cpu_state.regs[(sib >> 3) & 7].l << (sib >> 6);
	}
	else
	{
		cpu_state.eaaddr = cpu_state.regs[cpu_rm].l;
		if (cpu_mod) 
		{
			if (cpu_rm == 5 && !cpu_state.ssegs)
			{
				easeg = ss;
				cpu_state.ea_seg = &cpu_state.seg_ss;
			}
			if (cpu_mod == 1) 
			{ 
				cpu_state.eaaddr += ((uint32_t)(int8_t)(rmdat >> 8)); 
				cpu_state.pc++; 
			}
			else	  
			{
				cpu_state.eaaddr += getlong(); 
			}
		}
		else if (cpu_rm == 5) 
		{
			cpu_state.eaaddr = getlong();
		}
	}
	if (easeg != 0xFFFFFFFF && ((easeg + cpu_state.eaaddr) & 0xFFF) <= 0xFFC)
	{
		uint32_t addr = easeg + cpu_state.eaaddr;
		if ( readlookup2[addr >> 12] != (uintptr_t) -1)
		   eal_r = (uint32_t *)(readlookup2[addr >> 12] + addr);
		if (writelookup2[addr >> 12] != (uintptr_t) -1)
		   eal_w = (uint32_t *)(writelookup2[addr >> 12] + addr);
	}
}

static __inline void fetch_ea_16_long(uint32_t rmdat)
{
	eal_r = eal_w = NULL;
	easeg = cpu_state.ea_seg->base;
	if (!cpu_mod && cpu_rm == 6) 
	{ 
		cpu_state.eaaddr = getword();
	}
	else
	{
		switch (cpu_mod)
		{
			case 0:
			cpu_state.eaaddr = 0;
			break;
			case 1:
			cpu_state.eaaddr = (uint16_t)(int8_t)(rmdat >> 8); cpu_state.pc++;
			break;
			case 2:
			cpu_state.eaaddr = getword();
			break;
		}
		cpu_state.eaaddr += (*mod1add[0][cpu_rm]) + (*mod1add[1][cpu_rm]);
		if (mod1seg[cpu_rm] == &ss && !cpu_state.ssegs)
		{
			easeg = ss;
			cpu_state.ea_seg = &cpu_state.seg_ss;
		}
		cpu_state.eaaddr &= 0xFFFF;
	}
	if (easeg != 0xFFFFFFFF && ((easeg + cpu_state.eaaddr) & 0xFFF) <= 0xFFC)
	{
		uint32_t addr = easeg + cpu_state.eaaddr;
		if (readlookup2[addr >> 12] != (uintptr_t) -1)
		   eal_r = (uint32_t *)(readlookup2[addr >> 12] + addr);
		if (writelookup2[addr >> 12] != (uintptr_t) -1)
		   eal_w = (uint32_t *)(writelookup2[addr >> 12] + addr);
	}
}

#define fetch_ea_16(rmdat)	      cpu_state.pc++; cpu_mod=(rmdat >> 6) & 3; cpu_reg=(rmdat >> 3) & 7; cpu_rm = rmdat & 7; if (cpu_mod != 3) { fetch_ea_16_long(rmdat); if (cpu_state.abrt) return 1; } 
#define fetch_ea_32(rmdat)	      cpu_state.pc++; cpu_mod=(rmdat >> 6) & 3; cpu_reg=(rmdat >> 3) & 7; cpu_rm = rmdat & 7; if (cpu_mod != 3) { fetch_ea_32_long(rmdat); } if (cpu_state.abrt) return 1

#include "x86_flags.h"


/*Prefetch emulation is a fairly simplistic model:
  - All instruction bytes must be fetched before it starts.
  - Cycles used for non-instruction memory accesses are counted and subtracted
    from the total cycles taken
  - Any remaining cycles are used to refill the prefetch queue.

  Note that this is only used for 286 / 386 systems. It is disabled when the
  internal cache on 486+ CPUs is enabled.
*/
static int prefetch_bytes = 0;
static int prefetch_prefixes = 0;

static void prefetch_run(int instr_cycles, int bytes, int modrm, int reads, int reads_l, int writes, int writes_l, int ea32)
{
	int mem_cycles = reads*cpu_cycles_read + reads_l*cpu_cycles_read_l + writes*cpu_cycles_write + writes_l*cpu_cycles_write_l;

	if (instr_cycles < mem_cycles)
		instr_cycles = mem_cycles;

	prefetch_bytes -= prefetch_prefixes;
	prefetch_bytes -= bytes;
	if (modrm != -1)
	{
		if (ea32)
		{
			if ((modrm & 7) == 4)
			{
				if ((modrm & 0x700) == 0x500)
					prefetch_bytes -= 5;
				else if ((modrm & 0xc0) == 0x40)
					prefetch_bytes -= 2;
				else if ((modrm & 0xc0) == 0x80)
					prefetch_bytes -= 5;
			}
			else
			{
				if ((modrm & 0xc7) == 0x05)
					prefetch_bytes -= 4;
				else if ((modrm & 0xc0) == 0x40)
					prefetch_bytes--;
				else if ((modrm & 0xc0) == 0x80)
					prefetch_bytes -= 4;
			}
		}
		else
		{
			if ((modrm & 0xc7) == 0x06)
				prefetch_bytes -= 2;
			else if ((modrm & 0xc0) != 0xc0)
				prefetch_bytes -= ((modrm & 0xc0) >> 6);
		}
	}
	
	/* Fill up prefetch queue */
	while (prefetch_bytes < 0)
	{
		prefetch_bytes += cpu_prefetch_width;
		cycles -= cpu_prefetch_cycles;
	}
	
	/* Subtract cycles used for memory access by instruction */
	instr_cycles -= mem_cycles;
	
	while (instr_cycles >= cpu_prefetch_cycles)
	{
		prefetch_bytes += cpu_prefetch_width;
		instr_cycles -= cpu_prefetch_cycles;
	}
	
	prefetch_prefixes = 0;
	if (prefetch_bytes > 16)
		prefetch_bytes = 16;
}

static void prefetch_flush()
{
	prefetch_bytes = 0;
}

#define PREFETCH_RUN(instr_cycles, bytes, modrm, reads, reads_l, writes, writes_l, ea32) \
	do { if (cpu_prefetch_cycles) prefetch_run(instr_cycles, bytes, modrm, reads, reads_l, writes, writes_l, ea32); } while (0)

#define PREFETCH_PREFIX() do { if (cpu_prefetch_cycles) prefetch_prefixes++; } while (0)
#define PREFETCH_FLUSH() prefetch_flush()


#define OP_TABLE(name) ops_ ## name
#define CLOCK_CYCLES(c) cycles -= (c)
#define CLOCK_CYCLES_ALWAYS(c) cycles -= (c)

#include "386_ops.h"


#define CACHE_ON() (!(cr0 & (1 << 30)) && !(cpu_state.flags & T_FLAG))

#ifdef USE_DYNAREC
int cycles_main = 0;
static int cycles_old = 0;
static uint64_t tsc_old = 0;

#ifdef USE_ACYCS
int acycs = 0;
#endif


void
update_tsc(void)
{
    int cycdiff;
    uint64_t delta;

    cycdiff = cycles_old - cycles;
#ifdef USE_ACYCS
    if (inrecomp)
	cycdiff += acycs;
#endif

    delta = tsc - tsc_old;
    if (delta > 0) {
	/* TSC has changed, this means interim timer processing has happened,
	   see how much we still need to add. */
	cycdiff -= delta;
    }

    if (cycdiff > 0)
	tsc += cycdiff;

    if (cycdiff > 0) {
	if (TIMER_VAL_LESS_THAN_VAL(timer_target, (uint32_t)tsc))
		timer_process_inline();
    }
}


static __inline void
exec386_dynarec_int(void)
{
    cpu_block_end = 0;
    x86_was_reset = 0;

    while (!cpu_block_end) {
#ifndef USE_NEW_DYNAREC
	oldcs = CS;
	oldcpl = CPL;
#endif
	cpu_state.oldpc = cpu_state.pc;
	cpu_state.op32 = use32;

	cpu_state.ea_seg = &cpu_state.seg_ds;
	cpu_state.ssegs = 0;

	fetchdat = fastreadl(cs + cpu_state.pc);
#ifdef ENABLE_386_DYNAREC_LOG
	if (in_smm)
		x386_dynarec_log("[%04X:%08X] fetchdat = %08X\n", CS, cpu_state.pc, fetchdat);
#endif

	if (!cpu_state.abrt) {
		opcode = fetchdat & 0xFF;
		fetchdat >>= 8;

		trap = cpu_state.flags & T_FLAG;

		cpu_state.pc++;
		x86_opcodes[(opcode | cpu_state.op32) & 0x3ff](fetchdat);
	}

#ifndef USE_NEW_DYNAREC
	if (!use32)
		cpu_state.pc &= 0xffff;
#endif

	if (((cs + cpu_state.pc) >> 12) != pccache)
		CPU_BLOCK_END();

	if (cpu_end_block_after_ins) {
		cpu_end_block_after_ins--;
		if (!cpu_end_block_after_ins)
			CPU_BLOCK_END();
	}

	if (cpu_state.abrt)
		CPU_BLOCK_END();
	if (smi_line)
		CPU_BLOCK_END();
	else if (trap)
		CPU_BLOCK_END();
	else if (nmi && nmi_enable && nmi_mask)
		CPU_BLOCK_END();
	else if ((cpu_state.flags & I_FLAG) && pic.int_pending && !cpu_end_block_after_ins)
		CPU_BLOCK_END();
    }

    if (trap) {
	trap = 0;
#ifndef USE_NEW_DYNAREC
	oldcs = CS;
#endif
	cpu_state.oldpc = cpu_state.pc;
	x86_int(1);
    }

    cpu_end_block_after_ins = 0;
}


static __inline void
exec386_dynarec_dyn(void)
{
    uint32_t start_pc = 0, phys_addr = get_phys(cs + cpu_state.pc);
    int hash = HASH(phys_addr);
#ifdef USE_NEW_DYNAREC
    codeblock_t *block = &codeblock[codeblock_hash[hash]];
#else
    codeblock_t *block = codeblock_hash[hash];
#endif
    int valid_block = 0;

#ifdef USE_NEW_DYNAREC
    if (!cpu_state.abrt)
#else
    if (block && !cpu_state.abrt)
#endif
    {
	page_t *page = &pages[phys_addr >> 12];

	/* Block must match current CS, PC, code segment size,
	   and physical address. The physical address check will
	   also catch any page faults at this stage */
	valid_block = (block->pc == cs + cpu_state.pc) && (block->_cs == cs) &&
		      (block->phys == phys_addr) && !((block->status ^ cpu_cur_status) & CPU_STATUS_FLAGS) &&
		      ((block->status & cpu_cur_status & CPU_STATUS_MASK) == (cpu_cur_status & CPU_STATUS_MASK));
	if (!valid_block) {
		uint64_t mask = (uint64_t)1 << ((phys_addr >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);
#ifdef USE_NEW_DYNAREC
		int byte_offset = (phys_addr >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK;
		uint64_t byte_mask = 1ull << (PAGE_BYTE_MASK_MASK & 0x3f);

		if ((page->code_present_mask & mask) || (page->byte_code_present_mask[byte_offset] & byte_mask))
#else
		if (page->code_present_mask[(phys_addr >> PAGE_MASK_INDEX_SHIFT) & PAGE_MASK_INDEX_MASK] & mask)
#endif
		{
			/* Walk page tree to see if we find the correct block */
			codeblock_t *new_block = codeblock_tree_find(phys_addr, cs);
			if (new_block) {
				valid_block = (new_block->pc == cs + cpu_state.pc) && (new_block->_cs == cs) &&
					      (new_block->phys == phys_addr) && !((new_block->status ^ cpu_cur_status) & CPU_STATUS_FLAGS) &&
					      ((new_block->status & cpu_cur_status & CPU_STATUS_MASK) == (cpu_cur_status & CPU_STATUS_MASK));
				if (valid_block) {
					block = new_block;
#ifdef USE_NEW_DYNAREC
					codeblock_hash[hash] = get_block_nr(block);
#endif
				}
			}
		}
	}

	if (valid_block && (block->page_mask & *block->dirty_mask)) {
#ifdef USE_NEW_DYNAREC
		codegen_check_flush(page, page->dirty_mask, phys_addr);
		if (block->pc == BLOCK_PC_INVALID)
			valid_block = 0;
		else if (block->flags & CODEBLOCK_IN_DIRTY_LIST)
			block->flags &= ~CODEBLOCK_WAS_RECOMPILED;
#else
		codegen_check_flush(page, page->dirty_mask[(phys_addr >> 10) & 3], phys_addr);
		page->dirty_mask[(phys_addr >> 10) & 3] = 0;
		if (!block->valid)
			valid_block = 0;
#endif
	}
	if (valid_block && block->page_mask2) {
		/* We don't want the second page to cause a page
		   fault at this stage - that would break any
		   code crossing a page boundary where the first
		   page is present but the second isn't. Instead
		   allow the first page to be interpreted and for
		   the page fault to occur when the page boundary
		   is actually crossed.*/
#ifdef USE_NEW_DYNAREC
		uint32_t phys_addr_2 = get_phys_noabrt(block->pc + ((block->flags & CODEBLOCK_BYTE_MASK) ? 0x40 : 0x400));
#else
		uint32_t phys_addr_2 = get_phys_noabrt(block->endpc);
#endif
		page_t *page_2 = &pages[phys_addr_2 >> 12];

		if ((block->phys_2 ^ phys_addr_2) & ~0xfff)
			valid_block = 0;
		else if (block->page_mask2 & *block->dirty_mask2) {
#ifdef USE_NEW_DYNAREC
			codegen_check_flush(page_2, page_2->dirty_mask, phys_addr_2);
			if (block->pc == BLOCK_PC_INVALID)
				valid_block = 0;
			else if (block->flags & CODEBLOCK_IN_DIRTY_LIST)
				block->flags &= ~CODEBLOCK_WAS_RECOMPILED;
#else
			codegen_check_flush(page_2, page_2->dirty_mask[(phys_addr_2 >> 10) & 3], phys_addr_2);
			page_2->dirty_mask[(phys_addr_2 >> 10) & 3] = 0;
			if (!block->valid)
				valid_block = 0;
#endif
		}
	}
#ifdef USE_NEW_DYNAREC
	if (valid_block && (block->flags & CODEBLOCK_IN_DIRTY_LIST)) {
		block->flags &= ~CODEBLOCK_WAS_RECOMPILED;
		if (block->flags & CODEBLOCK_BYTE_MASK)
			block->flags |= CODEBLOCK_NO_IMMEDIATES;
		else
			block->flags |= CODEBLOCK_BYTE_MASK;
		}
	if (valid_block && (block->flags & CODEBLOCK_WAS_RECOMPILED) && (block->flags & CODEBLOCK_STATIC_TOP) && block->TOP != (cpu_state.TOP & 7))
#else
	if (valid_block && block->was_recompiled && (block->flags & CODEBLOCK_STATIC_TOP) && block->TOP != cpu_state.TOP)
#endif
	{
		/* FPU top-of-stack does not match the value this block was compiled
		   with, re-compile using dynamic top-of-stack*/
#ifdef USE_NEW_DYNAREC
		block->flags &= ~(CODEBLOCK_STATIC_TOP | CODEBLOCK_WAS_RECOMPILED);
#else
		block->flags &= ~CODEBLOCK_STATIC_TOP;
		block->was_recompiled = 0;
#endif
	}
    }

#ifdef USE_NEW_DYNAREC
    if (valid_block && (block->flags & CODEBLOCK_WAS_RECOMPILED))
#else
    if (valid_block && block->was_recompiled)
#endif
    {
	void (*code)() = (void *)&block->data[BLOCK_START];

#ifndef USE_NEW_DYNAREC
	codeblock_hash[hash] = block;
#endif
	inrecomp = 1;
	code();
#ifdef USE_ACYCS
	acycs = 0;
#endif
	inrecomp = 0;

#ifndef USE_NEW_DYNAREC
	if (!use32) cpu_state.pc &= 0xffff;
#endif
    } else if (valid_block && !cpu_state.abrt) {
#ifdef USE_NEW_DYNAREC
	start_pc = cs + cpu_state.pc;
	const int max_block_size = (block->flags & CODEBLOCK_BYTE_MASK) ? ((128 - 25) - (start_pc & 0x3f)) : 1000;
#else
	start_pc = cpu_state.pc;
#endif

	cpu_block_end = 0;
	x86_was_reset = 0;

#if defined(__APPLE__) && defined(__aarch64__)
	pthread_jit_write_protect_np(0);
#endif
	codegen_block_start_recompile(block);
	codegen_in_recompile = 1;

	while (!cpu_block_end) {
#ifndef USE_NEW_DYNAREC
		oldcs = CS;
		oldcpl = CPL;
#endif
		cpu_state.oldpc = cpu_state.pc;
		cpu_state.op32 = use32;

		cpu_state.ea_seg = &cpu_state.seg_ds;
		cpu_state.ssegs = 0;

		fetchdat = fastreadl(cs + cpu_state.pc);
#ifdef ENABLE_386_DYNAREC_LOG
		if (in_smm)
			x386_dynarec_log("[%04X:%08X] fetchdat = %08X\n", CS, cpu_state.pc, fetchdat);
#endif

		if (!cpu_state.abrt) {
			opcode = fetchdat & 0xFF;
			fetchdat >>= 8;

			cpu_state.pc++;

			codegen_generate_call(opcode, x86_opcodes[(opcode | cpu_state.op32) & 0x3ff], fetchdat, cpu_state.pc, cpu_state.pc-1);

			x86_opcodes[(opcode | cpu_state.op32) & 0x3ff](fetchdat);

			if (x86_was_reset)
				break;
		}

#ifndef USE_NEW_DYNAREC
		if (!use32)
			cpu_state.pc &= 0xffff;
#endif

		/* Cap source code at 4000 bytes per block; this
		   will prevent any block from spanning more than
		   2 pages. In practice this limit will never be
		   hit, as host block size is only 2kB*/
#ifdef USE_NEW_DYNAREC
		if (((cs + cpu_state.pc) - start_pc) >= max_block_size)
#else
		if ((cpu_state.pc - start_pc) > 1000)
#endif
			CPU_BLOCK_END();

		if (cpu_state.flags & T_FLAG)
			CPU_BLOCK_END();
		if (smi_line)
			CPU_BLOCK_END();
		if (nmi && nmi_enable && nmi_mask)
			CPU_BLOCK_END();
		if ((cpu_state.flags & I_FLAG) && pic.int_pending && !cpu_end_block_after_ins)
			CPU_BLOCK_END();

		if (cpu_end_block_after_ins) {
			cpu_end_block_after_ins--;
			if (!cpu_end_block_after_ins)
				CPU_BLOCK_END();
		}

		if (cpu_state.abrt) {
			if (!(cpu_state.abrt & ABRT_EXPECTED))
				codegen_block_remove();
			CPU_BLOCK_END();
		}
	}

	cpu_end_block_after_ins = 0;

	if ((!cpu_state.abrt || (cpu_state.abrt & ABRT_EXPECTED)) && !x86_was_reset)
		codegen_block_end_recompile(block);

	if (x86_was_reset)
		codegen_reset();

	codegen_in_recompile = 0;
#if defined(__APPLE__) && defined(__aarch64__)
	pthread_jit_write_protect_np(1);
#endif
    } else if (!cpu_state.abrt) {
	/* Mark block but do not recompile */
#ifdef USE_NEW_DYNAREC
	start_pc = cs + cpu_state.pc;
	const int max_block_size = (block->flags & CODEBLOCK_BYTE_MASK) ? ((128 - 25) - (start_pc & 0x3f)) : 1000;
#else
	start_pc = cpu_state.pc;
#endif

	cpu_block_end = 0;
	x86_was_reset = 0;

	codegen_block_init(phys_addr);

	while (!cpu_block_end) {
#ifndef USE_NEW_DYNAREC
		oldcs = CS;
		oldcpl = CPL;
#endif
		cpu_state.oldpc = cpu_state.pc;
		cpu_state.op32 = use32;

		cpu_state.ea_seg = &cpu_state.seg_ds;
		cpu_state.ssegs = 0;

		codegen_endpc = (cs + cpu_state.pc) + 8;
		fetchdat = fastreadl(cs + cpu_state.pc);

#ifdef ENABLE_386_DYNAREC_LOG
		if (in_smm)
			x386_dynarec_log("[%04X:%08X] fetchdat = %08X\n", CS, cpu_state.pc, fetchdat);
#endif

		if (!cpu_state.abrt) {
			opcode = fetchdat & 0xFF;
			fetchdat >>= 8;

			cpu_state.pc++;

			x86_opcodes[(opcode | cpu_state.op32) & 0x3ff](fetchdat);

			if (x86_was_reset)
				break;
		}

#ifndef USE_NEW_DYNAREC
		if (!use32)
			cpu_state.pc &= 0xffff;
#endif

		/* Cap source code at 4000 bytes per block; this
		   will prevent any block from spanning more than
		   2 pages. In practice this limit will never be
		   hit, as host block size is only 2kB */
#ifdef USE_NEW_DYNAREC
		if (((cs + cpu_state.pc) - start_pc) >= max_block_size)
#else
		if ((cpu_state.pc - start_pc) > 1000)
#endif
			CPU_BLOCK_END();

		if (cpu_state.flags & T_FLAG)
			CPU_BLOCK_END();
		if (smi_line)
			CPU_BLOCK_END();
		if (nmi && nmi_enable && nmi_mask)
			CPU_BLOCK_END();
		if ((cpu_state.flags & I_FLAG) && pic.int_pending && !cpu_end_block_after_ins)
			CPU_BLOCK_END();

		if (cpu_end_block_after_ins) {
			cpu_end_block_after_ins--;
			if (!cpu_end_block_after_ins)
				CPU_BLOCK_END();
		}

		if (cpu_state.abrt) {
			if (!(cpu_state.abrt & ABRT_EXPECTED))
				codegen_block_remove();
			CPU_BLOCK_END();
		}
	}

	cpu_end_block_after_ins = 0;

	if ((!cpu_state.abrt || (cpu_state.abrt & ABRT_EXPECTED)) && !x86_was_reset)
		codegen_block_end();

	if (x86_was_reset)
		codegen_reset();
    }
#ifdef USE_NEW_DYNAREC
    else
	cpu_state.oldpc = cpu_state.pc;
#endif
}


void
exec386_dynarec(int cycs)
{
    int vector, tempi;
    int cycdiff;
    int oldcyc, oldcyc2;
    uint64_t oldtsc, delta;

    int cyc_period = cycs / 2000; /*5us*/

#ifdef USE_ACYCS
    acycs = 0;
#endif
    cycles_main += cycs;
    while (cycles_main > 0) {
	int cycles_start;

	cycles += cyc_period;
	cycles_start = cycles;

	while (cycles > 0) {
#ifndef USE_NEW_DYNAREC
		oldcs = CS;
		cpu_state.oldpc = cpu_state.pc;
		oldcpl = CPL;
		cpu_state.op32 = use32;

		cycdiff = 0;
#endif
		oldcyc = oldcyc2 = cycles;
		cycles_old = cycles;
		oldtsc = tsc;
		tsc_old = tsc;
		if (!CACHE_ON()) /*Interpret block*/
		{
			exec386_dynarec_int();
		}
		else
		{
			exec386_dynarec_dyn();
		}

		if (cpu_state.abrt) {
			flags_rebuild();
			tempi = cpu_state.abrt & ABRT_MASK;
			cpu_state.abrt = 0;
			x86_doabrt(tempi);
			if (cpu_state.abrt) {
				cpu_state.abrt = 0;
				cpu_state.pc = cpu_state.oldpc;
#ifndef USE_NEW_DYNAREC
				CS = oldcs;
#endif
				pmodeint(8, 0);
				if (cpu_state.abrt) {
					cpu_state.abrt = 0;
					softresetx86();
					cpu_set_edx();
#ifdef ENABLE_386_DYNAREC_LOG
					x386_dynarec_log("Triple fault - reset\n");
#endif
				}
			}
		}

		if (smi_line)
			enter_smm_check(0);
		else if (nmi && nmi_enable && nmi_mask) {
			if (AT && (cpu_fast_off_flags & 0x20000000))
				cpu_fast_off_count = cpu_fast_off_val + 1;

#ifndef USE_NEW_DYNAREC
			oldcs = CS;
#endif
			cpu_state.oldpc = cpu_state.pc;
			x86_int(2);
			nmi_enable = 0;
#ifdef OLD_NMI_BEHAVIOR
			if (nmi_auto_clear) {
				nmi_auto_clear = 0;
				nmi = 0;
			}
#else
			nmi = 0;
#endif
		} else if ((cpu_state.flags & I_FLAG) && pic.int_pending) {
			vector = picinterrupt();
			if (vector != -1) {
#ifndef USE_NEW_DYNAREC
				oldcs = CS;
#endif
				cpu_state.oldpc = cpu_state.pc;
				x86_int(vector);
			}
		}

		cycdiff = oldcyc - cycles;
		delta = tsc - oldtsc;
		if (delta > 0) {
			/* TSC has changed, this means interim timer processing has happened,
			   see how much we still need to add. */
			cycdiff -= delta;
			if (cycdiff > 0)
				tsc += cycdiff;
		} else {
			/* TSC has not changed. */
			tsc += cycdiff;
		}

		if (cycdiff > 0) {
			if (TIMER_VAL_LESS_THAN_VAL(timer_target, (uint32_t) tsc))
				timer_process_inline();
		}
	}

	cycles_main -= (cycles_start - cycles);
    }
}
#endif
