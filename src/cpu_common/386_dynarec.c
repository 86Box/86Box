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
#include "86box.h"
#include "cpu.h"
#include "x86.h"
#include "x86_ops.h"
#include "x87.h"
#include "86box_io.h"
#include "mem.h"
#include "nmi.h"
#include "pic.h"
#include "timer.h"
#include "fdd.h"
#include "fdc.h"
#ifdef USE_DYNAREC
#include "codegen.h"
#ifdef USE_NEW_DYNAREC
#include "codegen_backend.h"
#endif
#endif
#include "386_common.h"


#define CPU_BLOCK_END() cpu_block_end = 1


/* These #define's and enum have been borrowed from Bochs. */
/* SMM feature masks */
#define SMM_IO_INSTRUCTION_RESTART  (0x00010000)
#define SMM_SMBASE_RELOCATION       (0x00020000)

/* TODO: Which CPU added SMBASE relocation? */
#define SMM_REVISION_ID SMM_SMBASE_RELOCATION
// #define SMM_REVISION_ID 0

#define SMM_SAVE_STATE_MAP_SIZE 128


enum SMMRAM_Fields {
    SMRAM_FIELD_SMBASE_OFFSET = 0,
    SMRAM_FIELD_SMM_REVISION_ID,
    SMRAM_FIELD_EAX,
    SMRAM_FIELD_ECX,
    SMRAM_FIELD_EDX,
    SMRAM_FIELD_EBX,
    SMRAM_FIELD_ESP,
    SMRAM_FIELD_EBP,
    SMRAM_FIELD_ESI,
    SMRAM_FIELD_EDI,
    SMRAM_FIELD_EIP,
    SMRAM_FIELD_EFLAGS,
    SMRAM_FIELD_DR6,
    SMRAM_FIELD_DR7,
    SMRAM_FIELD_CR0,
    SMRAM_FIELD_CR3,
    SMRAM_FIELD_CR4,
    SMRAM_FIELD_EFER,
    SMRAM_FIELD_IO_INSTRUCTION_RESTART,
    SMRAM_FIELD_AUTOHALT_RESTART,
    SMRAM_FIELD_NMI_MASK,
    SMRAM_FIELD_TR_SELECTOR,
    SMRAM_FIELD_TR_BASE,
    SMRAM_FIELD_TR_LIMIT,
    SMRAM_FIELD_TR_SELECTOR_AR,
    SMRAM_FIELD_LDTR_SELECTOR,
    SMRAM_FIELD_LDTR_BASE,
    SMRAM_FIELD_LDTR_LIMIT,
    SMRAM_FIELD_LDTR_SELECTOR_AR,
    SMRAM_FIELD_IDTR_BASE,
    SMRAM_FIELD_IDTR_LIMIT,
    SMRAM_FIELD_GDTR_BASE,
    SMRAM_FIELD_GDTR_LIMIT,
    SMRAM_FIELD_ES_SELECTOR,
    SMRAM_FIELD_ES_BASE,
    SMRAM_FIELD_ES_LIMIT,
    SMRAM_FIELD_ES_SELECTOR_AR,
    SMRAM_FIELD_CS_SELECTOR,
    SMRAM_FIELD_CS_BASE,
    SMRAM_FIELD_CS_LIMIT,
    SMRAM_FIELD_CS_SELECTOR_AR,
    SMRAM_FIELD_SS_SELECTOR,
    SMRAM_FIELD_SS_BASE,
    SMRAM_FIELD_SS_LIMIT,
    SMRAM_FIELD_SS_SELECTOR_AR,
    SMRAM_FIELD_DS_SELECTOR,
    SMRAM_FIELD_DS_BASE,
    SMRAM_FIELD_DS_LIMIT,
    SMRAM_FIELD_DS_SELECTOR_AR,
    SMRAM_FIELD_FS_SELECTOR,
    SMRAM_FIELD_FS_BASE,
    SMRAM_FIELD_FS_LIMIT,
    SMRAM_FIELD_FS_SELECTOR_AR,
    SMRAM_FIELD_GS_SELECTOR,
    SMRAM_FIELD_GS_BASE,
    SMRAM_FIELD_GS_LIMIT,
    SMRAM_FIELD_GS_SELECTOR_AR,
    SMRAM_FIELD_LAST
};


int inrecomp = 0, cpu_block_end = 0;
int cpu_recomp_blocks, cpu_recomp_full_ins, cpu_new_blocks;
int cpu_recomp_blocks_latched, cpu_recomp_ins_latched, cpu_recomp_full_ins_latched, cpu_new_blocks_latched;


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
		if ( readlookup2[addr >> 12] != -1)
		   eal_r = (uint32_t *)(readlookup2[addr >> 12] + addr);
		if (writelookup2[addr >> 12] != -1)
		   eal_w = (uint32_t *)(writelookup2[addr >> 12] + addr);
	}
	cpu_state.last_ea = cpu_state.eaaddr;
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
		if ( readlookup2[addr >> 12] != -1)
		   eal_r = (uint32_t *)(readlookup2[addr >> 12] + addr);
		if (writelookup2[addr >> 12] != -1)
		   eal_w = (uint32_t *)(writelookup2[addr >> 12] + addr);
	}
	cpu_state.last_ea = cpu_state.eaaddr;
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


static void set_stack32(int s)
{
        stack32 = s;
	if (stack32)
	       cpu_cur_status |= CPU_STATUS_STACK32;
	else
	       cpu_cur_status &= ~CPU_STATUS_STACK32;
}


static void set_use32(int u)
{
        if (u) 
        {
                use32 = 0x300;
                cpu_cur_status |= CPU_STATUS_USE32;
        }
        else
        {
                use32 = 0;
                cpu_cur_status &= ~CPU_STATUS_USE32;
        }
}


void smm_seg_load(x86seg *s)
{
        if (!is386)
                s->base &= 0x00ffffff;

        if ((s->access & 0x18) != 0x10 || !(s->access & (1 << 2))) /*expand-down*/
        {
                s->limit_high = s->limit;
                s->limit_low = 0;
        }
        else
        {
                s->limit_high = (s->ar_high & 0x40) ? 0xffffffff : 0xffff;
                s->limit_low = s->limit + 1;
        }

	if ((cr0 & 1) && !(cpu_state.eflags & VM_FLAG))
		s->checked = s->seg ? 1 : 0;
	else
		s->checked = 1;

        if (s == &cpu_state.seg_cs)
		set_use32(s->ar_high & 0x40);
        if (s == &cpu_state.seg_ds)
        {
                if (s->base == 0 && s->limit_low == 0 && s->limit_high == 0xffffffff)
                        cpu_cur_status &= ~CPU_STATUS_NOTFLATDS;
                else
                        cpu_cur_status |= CPU_STATUS_NOTFLATDS;
        }
        if (s == &cpu_state.seg_ss)
        {
                if (s->base == 0 && s->limit_low == 0 && s->limit_high == 0xffffffff)
                        cpu_cur_status &= ~CPU_STATUS_NOTFLATSS;
                else
                        cpu_cur_status |= CPU_STATUS_NOTFLATSS;
		set_stack32((s->ar_high & 0x40) ? 1 : 0);
        }
}


void smram_save_state(uint32_t *saved_state)
{
	int n = 0;

	saved_state[SMRAM_FIELD_SMM_REVISION_ID] = SMM_REVISION_ID;
	saved_state[SMRAM_FIELD_SMBASE_OFFSET] = smbase;

	for (n = 0; n < 8; n++)
		saved_state[SMRAM_FIELD_EAX + n] = cpu_state.regs[n].l;

	saved_state[SMRAM_FIELD_EIP] = cpu_state.pc;
	saved_state[SMRAM_FIELD_EFLAGS] = (cpu_state.eflags << 16) | (cpu_state.flags);

	saved_state[SMRAM_FIELD_CR0] = cr0;
	saved_state[SMRAM_FIELD_CR3] = cr3;
	if (is_pentium) {
		saved_state[SMRAM_FIELD_CR4] = cr4;
		/* TODO: Properly implement EFER */
		/* saved_state[SMRAM_FIELD_EFER] = efer; */
	}
	saved_state[SMRAM_FIELD_DR6] = dr[6];
	saved_state[SMRAM_FIELD_DR7] = dr[7];

	/* TR */
	saved_state[SMRAM_FIELD_TR_SELECTOR] = tr.seg;
	saved_state[SMRAM_FIELD_TR_BASE] = tr.base;
	saved_state[SMRAM_FIELD_TR_LIMIT] = tr.limit;
	saved_state[SMRAM_FIELD_TR_SELECTOR_AR] = (tr.ar_high << 24) | (tr.access << 16) | tr.seg;

	/* LDTR */
	saved_state[SMRAM_FIELD_LDTR_SELECTOR] = ldt.seg;
	saved_state[SMRAM_FIELD_LDTR_BASE] = ldt.base;
	saved_state[SMRAM_FIELD_LDTR_LIMIT] = ldt.limit;
	saved_state[SMRAM_FIELD_LDTR_SELECTOR_AR] = (ldt.ar_high << 24) | (ldt.access << 16) | ldt.seg;

	/* IDTR */
	saved_state[SMRAM_FIELD_IDTR_BASE] = idt.base;
	saved_state[SMRAM_FIELD_IDTR_LIMIT] = idt.limit;

	/* GDTR */
	saved_state[SMRAM_FIELD_GDTR_BASE] = gdt.base;
	saved_state[SMRAM_FIELD_GDTR_LIMIT] = gdt.limit;

	/* ES */
	saved_state[SMRAM_FIELD_ES_SELECTOR] = cpu_state.seg_es.seg;
	saved_state[SMRAM_FIELD_ES_BASE] = cpu_state.seg_es.base;
	saved_state[SMRAM_FIELD_ES_LIMIT] = cpu_state.seg_es.limit;
	saved_state[SMRAM_FIELD_ES_SELECTOR_AR] =
		(cpu_state.seg_es.ar_high << 24) | (cpu_state.seg_es.access << 16) | cpu_state.seg_es.seg;

	/* CS */
	saved_state[SMRAM_FIELD_CS_SELECTOR] = cpu_state.seg_cs.seg;
	saved_state[SMRAM_FIELD_CS_BASE] = cpu_state.seg_cs.base;
	saved_state[SMRAM_FIELD_CS_LIMIT] = cpu_state.seg_cs.limit;
	saved_state[SMRAM_FIELD_CS_SELECTOR_AR] =
		(cpu_state.seg_cs.ar_high << 24) | (cpu_state.seg_cs.access << 16) | cpu_state.seg_cs.seg;

	/* DS */
	saved_state[SMRAM_FIELD_DS_SELECTOR] = cpu_state.seg_ds.seg;
	saved_state[SMRAM_FIELD_DS_BASE] = cpu_state.seg_ds.base;
	saved_state[SMRAM_FIELD_DS_LIMIT] = cpu_state.seg_ds.limit;
	saved_state[SMRAM_FIELD_DS_SELECTOR_AR] =
		(cpu_state.seg_ds.ar_high << 24) | (cpu_state.seg_ds.access << 16) | cpu_state.seg_ds.seg;

	/* SS */
	saved_state[SMRAM_FIELD_SS_SELECTOR] = cpu_state.seg_ss.seg;
	saved_state[SMRAM_FIELD_SS_BASE] = cpu_state.seg_ss.base;
	saved_state[SMRAM_FIELD_SS_LIMIT] = cpu_state.seg_ss.limit;
	saved_state[SMRAM_FIELD_SS_SELECTOR_AR] =
		(cpu_state.seg_ss.ar_high << 24) | (cpu_state.seg_ss.access << 16) | cpu_state.seg_ss.seg;

	/* FS */
	saved_state[SMRAM_FIELD_FS_SELECTOR] = cpu_state.seg_fs.seg;
	saved_state[SMRAM_FIELD_FS_BASE] = cpu_state.seg_fs.base;
	saved_state[SMRAM_FIELD_FS_LIMIT] = cpu_state.seg_fs.limit;
	saved_state[SMRAM_FIELD_FS_SELECTOR_AR] =
		(cpu_state.seg_fs.ar_high << 24) | (cpu_state.seg_fs.access << 16) | cpu_state.seg_fs.seg;

	/* GS */
	saved_state[SMRAM_FIELD_GS_SELECTOR] = cpu_state.seg_gs.seg;
	saved_state[SMRAM_FIELD_GS_BASE] = cpu_state.seg_gs.base;
	saved_state[SMRAM_FIELD_GS_LIMIT] = cpu_state.seg_gs.limit;
	saved_state[SMRAM_FIELD_GS_SELECTOR_AR] =
		(cpu_state.seg_gs.ar_high << 24) | (cpu_state.seg_gs.access << 16) | cpu_state.seg_gs.seg;
}


void smram_restore_state(uint32_t *saved_state)
{
	int n = 0;

	for (n = 0; n < 8; n++)
		cpu_state.regs[n].l = saved_state[SMRAM_FIELD_EAX + n];

	cpu_state.pc = saved_state[SMRAM_FIELD_EIP];
	cpu_state.eflags = saved_state[SMRAM_FIELD_EFLAGS] >> 16;
	cpu_state.flags = saved_state[SMRAM_FIELD_EFLAGS] & 0xffff;

	cr0 = saved_state[SMRAM_FIELD_CR0];
	cr3 = saved_state[SMRAM_FIELD_CR3];
	if (is_pentium) {
		cr4 = saved_state[SMRAM_FIELD_CR4];
		/* TODO: Properly implement EFER */
		/* efer = saved_state[SMRAM_FIELD_EFER]; */
	}
	dr[6] = saved_state[SMRAM_FIELD_DR6];
	dr[7] = saved_state[SMRAM_FIELD_DR7];

	/* TR */
	tr.seg = saved_state[SMRAM_FIELD_TR_SELECTOR];
	tr.base = saved_state[SMRAM_FIELD_TR_BASE];
	tr.limit = saved_state[SMRAM_FIELD_TR_LIMIT];
	tr.access = (saved_state[SMRAM_FIELD_TR_SELECTOR_AR] >> 16) & 0xff;
	tr.ar_high = (saved_state[SMRAM_FIELD_TR_SELECTOR_AR] >> 24) & 0xff;
	smm_seg_load(&tr);

	/* LDTR */
	ldt.seg = saved_state[SMRAM_FIELD_LDTR_SELECTOR];
	ldt.base = saved_state[SMRAM_FIELD_LDTR_BASE];
	ldt.limit = saved_state[SMRAM_FIELD_LDTR_LIMIT];
	ldt.access = (saved_state[SMRAM_FIELD_LDTR_SELECTOR_AR] >> 16) & 0xff;
	ldt.ar_high = (saved_state[SMRAM_FIELD_LDTR_SELECTOR_AR] >> 24) & 0xff;
	smm_seg_load(&ldt);

	/* IDTR */
	idt.base = saved_state[SMRAM_FIELD_IDTR_BASE];
	idt.limit = saved_state[SMRAM_FIELD_IDTR_LIMIT];

	/* GDTR */
	gdt.base = saved_state[SMRAM_FIELD_GDTR_BASE];
	gdt.limit = saved_state[SMRAM_FIELD_GDTR_LIMIT];

	/* ES */
	cpu_state.seg_es.seg = saved_state[SMRAM_FIELD_ES_SELECTOR];
	cpu_state.seg_es.base = saved_state[SMRAM_FIELD_ES_BASE];
	cpu_state.seg_es.limit = saved_state[SMRAM_FIELD_ES_LIMIT];
	cpu_state.seg_es.access = (saved_state[SMRAM_FIELD_ES_SELECTOR_AR] >> 16) & 0xff;
	cpu_state.seg_es.ar_high = (saved_state[SMRAM_FIELD_ES_SELECTOR_AR] >> 24) & 0xff;
	smm_seg_load(&cpu_state.seg_es);

	/* CS */
	cpu_state.seg_cs.seg = saved_state[SMRAM_FIELD_CS_SELECTOR];
	cpu_state.seg_cs.base = saved_state[SMRAM_FIELD_CS_BASE];
	cpu_state.seg_cs.limit = saved_state[SMRAM_FIELD_CS_LIMIT];
	cpu_state.seg_cs.access = (saved_state[SMRAM_FIELD_CS_SELECTOR_AR] >> 16) & 0xff;
	cpu_state.seg_cs.ar_high = (saved_state[SMRAM_FIELD_CS_SELECTOR_AR] >> 24) & 0xff;
	smm_seg_load(&cpu_state.seg_cs);

	/* DS */
	cpu_state.seg_ds.seg = saved_state[SMRAM_FIELD_DS_SELECTOR];
	cpu_state.seg_ds.base = saved_state[SMRAM_FIELD_DS_BASE];
	cpu_state.seg_ds.limit = saved_state[SMRAM_FIELD_DS_LIMIT];
	cpu_state.seg_ds.access = (saved_state[SMRAM_FIELD_DS_SELECTOR_AR] >> 16) & 0xff;
	cpu_state.seg_ds.ar_high = (saved_state[SMRAM_FIELD_DS_SELECTOR_AR] >> 24) & 0xff;
	smm_seg_load(&cpu_state.seg_ds);

	/* SS */
	cpu_state.seg_ss.seg = saved_state[SMRAM_FIELD_SS_SELECTOR];
	cpu_state.seg_ss.base = saved_state[SMRAM_FIELD_SS_BASE];
	cpu_state.seg_ss.limit = saved_state[SMRAM_FIELD_SS_LIMIT];
	cpu_state.seg_ss.access = (saved_state[SMRAM_FIELD_SS_SELECTOR_AR] >> 16) & 0xff;
	cpu_state.seg_ss.ar_high = (saved_state[SMRAM_FIELD_SS_SELECTOR_AR] >> 24) & 0xff;
	smm_seg_load(&cpu_state.seg_ss);

	/* FS */
	cpu_state.seg_fs.seg = saved_state[SMRAM_FIELD_FS_SELECTOR];
	cpu_state.seg_fs.base = saved_state[SMRAM_FIELD_FS_BASE];
	cpu_state.seg_fs.limit = saved_state[SMRAM_FIELD_FS_LIMIT];
	cpu_state.seg_fs.access = (saved_state[SMRAM_FIELD_FS_SELECTOR_AR] >> 16) & 0xff;
	cpu_state.seg_fs.ar_high = (saved_state[SMRAM_FIELD_FS_SELECTOR_AR] >> 24) & 0xff;
	smm_seg_load(&cpu_state.seg_fs);

	/* GS */
	cpu_state.seg_gs.seg = saved_state[SMRAM_FIELD_GS_SELECTOR];
	cpu_state.seg_gs.base = saved_state[SMRAM_FIELD_GS_BASE];
	cpu_state.seg_gs.limit = saved_state[SMRAM_FIELD_GS_LIMIT];
	cpu_state.seg_gs.access = (saved_state[SMRAM_FIELD_GS_SELECTOR_AR] >> 16) & 0xff;
	cpu_state.seg_gs.ar_high = (saved_state[SMRAM_FIELD_GS_SELECTOR_AR] >> 24) & 0xff;
	smm_seg_load(&cpu_state.seg_gs);

	if (SMM_REVISION_ID & SMM_SMBASE_RELOCATION)
		smbase = saved_state[SMRAM_FIELD_SMBASE_OFFSET];
}


void enter_smm()
{
	uint32_t saved_state[SMM_SAVE_STATE_MAP_SIZE], n;
	uint32_t smram_state = smbase + 0x10000;

	x386_dynarec_log("enter_smm(): smbase = %08X\n", smbase);
	x386_dynarec_log("CS : seg = %04X, base = %08X, limit = %08X, limit_low = %08X, limit_high = %08X, access = %02X, ar_high = %02X\n",
		cpu_state.seg_cs.seg, cpu_state.seg_cs.base, cpu_state.seg_cs.limit, cpu_state.seg_cs.limit_low,
		cpu_state.seg_cs.limit_high, cpu_state.seg_cs.access, cpu_state.seg_cs.ar_high);
	x386_dynarec_log("DS : seg = %04X, base = %08X, limit = %08X, limit_low = %08X, limit_high = %08X, access = %02X, ar_high = %02X\n",
		cpu_state.seg_ds.seg, cpu_state.seg_ds.base, cpu_state.seg_ds.limit, cpu_state.seg_ds.limit_low,
		cpu_state.seg_ds.limit_high, cpu_state.seg_ds.access, cpu_state.seg_ds.ar_high);
	x386_dynarec_log("ES : seg = %04X, base = %08X, limit = %08X, limit_low = %08X, limit_high = %08X, access = %02X, ar_high = %02X\n",
		cpu_state.seg_es.seg, cpu_state.seg_es.base, cpu_state.seg_es.limit, cpu_state.seg_es.limit_low,
		cpu_state.seg_es.limit_high, cpu_state.seg_es.access, cpu_state.seg_es.ar_high);
	x386_dynarec_log("FS : seg = %04X, base = %08X, limit = %08X, limit_low = %08X, limit_high = %08X, access = %02X, ar_high = %02X\n",
		cpu_state.seg_fs.seg, cpu_state.seg_fs.base, cpu_state.seg_fs.limit, cpu_state.seg_fs.limit_low,
		cpu_state.seg_fs.limit_high, cpu_state.seg_fs.access, cpu_state.seg_fs.ar_high);
	x386_dynarec_log("GS : seg = %04X, base = %08X, limit = %08X, limit_low = %08X, limit_high = %08X, access = %02X, ar_high = %02X\n",
		cpu_state.seg_gs.seg, cpu_state.seg_gs.base, cpu_state.seg_gs.limit, cpu_state.seg_gs.limit_low,
		cpu_state.seg_gs.limit_high, cpu_state.seg_gs.access, cpu_state.seg_gs.ar_high);
	x386_dynarec_log("SS : seg = %04X, base = %08X, limit = %08X, limit_low = %08X, limit_high = %08X, access = %02X, ar_high = %02X\n",
		cpu_state.seg_ss.seg, cpu_state.seg_ss.base, cpu_state.seg_ss.limit, cpu_state.seg_ss.limit_low,
		cpu_state.seg_ss.limit_high, cpu_state.seg_ss.access, cpu_state.seg_ss.ar_high);
	x386_dynarec_log("TR : seg = %04X, base = %08X, limit = %08X, limit_low = %08X, limit_high = %08X, access = %02X, ar_high = %02X\n",
		tr.seg, tr.base, tr.limit, tr.limit_low, tr.limit_high, tr.access, tr.ar_high);
	x386_dynarec_log("LDT: seg = %04X, base = %08X, limit = %08X, limit_low = %08X, limit_high = %08X, access = %02X, ar_high = %02X\n",
		ldt.seg, ldt.base, ldt.limit, ldt.limit_low, ldt.limit_high, ldt.access, ldt.ar_high);
	x386_dynarec_log("GDT: seg = %04X, base = %08X, limit = %08X, limit_low = %08X, limit_high = %08X, access = %02X, ar_high = %02X\n",
		gdt.seg, gdt.base, gdt.limit, gdt.limit_low, gdt.limit_high, gdt.access, gdt.ar_high);
	x386_dynarec_log("IDT: seg = %04X, base = %08X, limit = %08X, limit_low = %08X, limit_high = %08X, access = %02X, ar_high = %02X\n",
		idt.seg, idt.base, idt.limit, idt.limit_low, idt.limit_high, idt.access, idt.ar_high);
	x386_dynarec_log("CR0 = %08X, CR3 = %08X, CR4 = %08X, DR6 = %08X, DR7 = %08X\n", cr0, cr3, cr4, dr[6], dr[7]);
	x386_dynarec_log("EIP = %08X, EFLAGS = %04X%04X\n", cpu_state.pc, cpu_state.eflags, cpu_state.flags);
	x386_dynarec_log("EAX = %08X, EBX = %08X, ECX = %08X, EDX = %08X, ESI = %08X, EDI = %08X, ESP = %08X, EBP = %08X\n",
		EAX, EBX, ECX, EDX, ESI, EDI, ESP, EBP);

	memset(saved_state, 0x00, SMM_SAVE_STATE_MAP_SIZE * sizeof(uint32_t));
	smram_save_state(saved_state);
	for (n = 0; n < SMM_SAVE_STATE_MAP_SIZE; n++) {
		smram_state -= 4;
		mem_writel_phys(smram_state, saved_state[n]);
	}

	cr0 &= ~0x8000000d;
	cpu_state.flags = 2;
	cpu_state.eflags = 0;

	/* Intel 4x0 chipset stuff. */
	mem_set_mem_state(0xa0000, 131072, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
	flushmmucache_cr3();

	if (is_pentium)
		cr4 = 0;
	dr[7] = 0x400;
	cpu_state.pc = 0x8000;

	cpu_state.seg_ds.seg = 0x00000000;
	cpu_state.seg_ds.base = 0x00000000;
	cpu_state.seg_ds.limit = 0xffffffff;
	cpu_state.seg_ds.access = 0x93;
	cpu_state.seg_ds.ar_high = 0x80;

	memcpy(&cpu_state.seg_es, &cpu_state.seg_ds, sizeof(x86seg));
	memcpy(&cpu_state.seg_ss, &cpu_state.seg_ds, sizeof(x86seg));
	memcpy(&cpu_state.seg_fs, &cpu_state.seg_ds, sizeof(x86seg));
	memcpy(&cpu_state.seg_gs, &cpu_state.seg_ds, sizeof(x86seg));

	CS = (smbase >> 4);
	cpu_state.seg_cs.base = smbase;
	cpu_state.seg_cs.limit = 0xffffffff;
	cpu_state.seg_cs.access = 0x93;
	cpu_state.seg_cs.ar_high = 0x80;
	cpu_state.seg_cs.checked = 1;

	smm_seg_load(&cpu_state.seg_es);
	smm_seg_load(&cpu_state.seg_cs);
	smm_seg_load(&cpu_state.seg_ds);
	smm_seg_load(&cpu_state.seg_ss);
	smm_seg_load(&cpu_state.seg_fs);
	smm_seg_load(&cpu_state.seg_gs);

	nmi_mask = 0;

	in_smm = 1;
	CPU_BLOCK_END();
}


void leave_smm()
{
	uint32_t saved_state[SMM_SAVE_STATE_MAP_SIZE], n;
	uint32_t smram_state = smbase + 0x10000;

	memset(saved_state, 0x00, SMM_SAVE_STATE_MAP_SIZE * sizeof(uint32_t));
	for (n = 0; n < SMM_SAVE_STATE_MAP_SIZE; n++) {
		smram_state -= 4;
		saved_state[n] = mem_readl_phys(smram_state);
	}
	smram_restore_state(saved_state);

	/* Intel 4x0 chipset stuff. */
	mem_restore_mem_state(0xa0000, 131072);
	flushmmucache_cr3();

	nmi_mask = 1;

	in_smm = 0;
	CPU_BLOCK_END();

	x386_dynarec_log("CS : seg = %04X, base = %08X, limit = %08X, limit_low = %08X, limit_high = %08X, access = %02X, ar_high = %02X\n",
		cpu_state.seg_cs.seg, cpu_state.seg_cs.base, cpu_state.seg_cs.limit, cpu_state.seg_cs.limit_low,
		cpu_state.seg_cs.limit_high, cpu_state.seg_cs.access, cpu_state.seg_cs.ar_high);
	x386_dynarec_log("DS : seg = %04X, base = %08X, limit = %08X, limit_low = %08X, limit_high = %08X, access = %02X, ar_high = %02X\n",
		cpu_state.seg_ds.seg, cpu_state.seg_ds.base, cpu_state.seg_ds.limit, cpu_state.seg_ds.limit_low,
		cpu_state.seg_ds.limit_high, cpu_state.seg_ds.access, cpu_state.seg_ds.ar_high);
	x386_dynarec_log("ES : seg = %04X, base = %08X, limit = %08X, limit_low = %08X, limit_high = %08X, access = %02X, ar_high = %02X\n",
		cpu_state.seg_es.seg, cpu_state.seg_es.base, cpu_state.seg_es.limit, cpu_state.seg_es.limit_low,
		cpu_state.seg_es.limit_high, cpu_state.seg_es.access, cpu_state.seg_es.ar_high);
	x386_dynarec_log("FS : seg = %04X, base = %08X, limit = %08X, limit_low = %08X, limit_high = %08X, access = %02X, ar_high = %02X\n",
		cpu_state.seg_fs.seg, cpu_state.seg_fs.base, cpu_state.seg_fs.limit, cpu_state.seg_fs.limit_low,
		cpu_state.seg_fs.limit_high, cpu_state.seg_fs.access, cpu_state.seg_fs.ar_high);
	x386_dynarec_log("GS : seg = %04X, base = %08X, limit = %08X, limit_low = %08X, limit_high = %08X, access = %02X, ar_high = %02X\n",
		cpu_state.seg_gs.seg, cpu_state.seg_gs.base, cpu_state.seg_gs.limit, cpu_state.seg_gs.limit_low,
		cpu_state.seg_gs.limit_high, cpu_state.seg_gs.access, cpu_state.seg_gs.ar_high);
	x386_dynarec_log("SS : seg = %04X, base = %08X, limit = %08X, limit_low = %08X, limit_high = %08X, access = %02X, ar_high = %02X\n",
		cpu_state.seg_ss.seg, cpu_state.seg_ss.base, cpu_state.seg_ss.limit, cpu_state.seg_ss.limit_low,
		cpu_state.seg_ss.limit_high, cpu_state.seg_ss.access, cpu_state.seg_ss.ar_high);
	x386_dynarec_log("TR : seg = %04X, base = %08X, limit = %08X, limit_low = %08X, limit_high = %08X, access = %02X, ar_high = %02X\n",
		tr.seg, tr.base, tr.limit, tr.limit_low, tr.limit_high, tr.access, tr.ar_high);
	x386_dynarec_log("LDT: seg = %04X, base = %08X, limit = %08X, limit_low = %08X, limit_high = %08X, access = %02X, ar_high = %02X\n",
		ldt.seg, ldt.base, ldt.limit, ldt.limit_low, ldt.limit_high, ldt.access, ldt.ar_high);
	x386_dynarec_log("GDT: seg = %04X, base = %08X, limit = %08X, limit_low = %08X, limit_high = %08X, access = %02X, ar_high = %02X\n",
		gdt.seg, gdt.base, gdt.limit, gdt.limit_low, gdt.limit_high, gdt.access, gdt.ar_high);
	x386_dynarec_log("IDT: seg = %04X, base = %08X, limit = %08X, limit_low = %08X, limit_high = %08X, access = %02X, ar_high = %02X\n",
		idt.seg, idt.base, idt.limit, idt.limit_low, idt.limit_high, idt.access, idt.ar_high);
	x386_dynarec_log("CR0 = %08X, CR3 = %08X, CR4 = %08X, DR6 = %08X, DR7 = %08X\n", cr0, cr3, cr4, dr[6], dr[7]);
	x386_dynarec_log("EIP = %08X, EFLAGS = %04X%04X\n", cpu_state.pc, cpu_state.eflags, cpu_state.flags);
	x386_dynarec_log("EAX = %08X, EBX = %08X, ECX = %08X, EDX = %08X, ESI = %08X, EDI = %08X, ESP = %08X, EBP = %08X\n",
		EAX, EBX, ECX, EDX, ESI, EDI, ESP, EBP);
	x386_dynarec_log("leave_smm()\n");
}


#define OP_TABLE(name) ops_ ## name
#define CLOCK_CYCLES(c) cycles -= (c)
#define CLOCK_CYCLES_ALWAYS(c) cycles -= (c)

#include "386_ops.h"


#define CACHE_ON() (!(cr0 & (1 << 30)) && !(cpu_state.flags & T_FLAG))

#ifdef USE_DYNAREC
static int cycles_main = 0;


void exec386_dynarec(int cycs)
{
	int vector;
	uint32_t addr;
	int tempi;
	int cycdiff;
	int oldcyc;
	uint32_t start_pc = 0;

	int cyc_period = cycs / 2000; /*5us*/

	cycles_main += cycs;
	while (cycles_main > 0)
	{
		int cycles_start;

		cycles += cyc_period;
		cycles_start = cycles;

		while (cycles>0)
		{
#ifndef USE_NEW_DYNAREC
			oldcs = CS;
			cpu_state.oldpc = cpu_state.pc;
			oldcpl = CPL;
			cpu_state.op32 = use32;

			cycdiff=0;
#endif
			oldcyc=cycles;
			if (!CACHE_ON()) /*Interpret block*/
			{
				cpu_block_end = 0;
				x86_was_reset = 0;
				while (!cpu_block_end)
				{
#ifndef USE_NEW_DYNAREC
					oldcs = CS;
					oldcpl = CPL;
#endif
					cpu_state.oldpc = cpu_state.pc;
					cpu_state.op32 = use32;

					cpu_state.ea_seg = &cpu_state.seg_ds;
					cpu_state.ssegs = 0;
		
					fetchdat = fastreadl(cs + cpu_state.pc);

					if (!cpu_state.abrt)
					{
						opcode = fetchdat & 0xFF;
						fetchdat >>= 8;
						trap = cpu_state.flags & T_FLAG;

						cpu_state.pc++;
						x86_opcodes[(opcode | cpu_state.op32) & 0x3ff](fetchdat);
					}

#ifndef USE_NEW_DYNAREC
					if (!use32) cpu_state.pc &= 0xffff;
#endif

					if (((cs + cpu_state.pc) >> 12) != pccache)
						CPU_BLOCK_END();

					if (!in_smm && smi_line/* && is_pentium*/)
						CPU_BLOCK_END();

					if (cpu_state.abrt)
						CPU_BLOCK_END();
					if (trap)
						CPU_BLOCK_END();

					if (nmi && nmi_enable && nmi_mask)
						CPU_BLOCK_END();
					
					ins++;
				}
			}
			else
			{
				uint32_t phys_addr = get_phys(cs+cpu_state.pc);
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
				trap = 0;

				if (block && !cpu_state.abrt)
#endif
				{
					page_t *page = &pages[phys_addr >> 12];

					/*Block must match current CS, PC, code segment size,
					  and physical address. The physical address check will
					  also catch any page faults at this stage*/
					valid_block = (block->pc == cs + cpu_state.pc) && (block->_cs == cs) &&
						      (block->phys == phys_addr) && !((block->status ^ cpu_cur_status) & CPU_STATUS_FLAGS) &&
						      ((block->status & cpu_cur_status & CPU_STATUS_MASK) == (cpu_cur_status & CPU_STATUS_MASK));
					if (!valid_block)
					{
						uint64_t mask = (uint64_t)1 << ((phys_addr >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);
#ifdef USE_NEW_DYNAREC
						int byte_offset = (phys_addr >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK;
						uint64_t byte_mask = 1ull << (PAGE_BYTE_MASK_MASK & 0x3f);

						if ((page->code_present_mask & mask) || (page->byte_code_present_mask[byte_offset] & byte_mask))
#else
						if (page->code_present_mask[(phys_addr >> PAGE_MASK_INDEX_SHIFT) & PAGE_MASK_INDEX_MASK] & mask)
#endif
						{
							/*Walk page tree to see if we find the correct block*/
							codeblock_t *new_block = codeblock_tree_find(phys_addr, cs);
							if (new_block)
							{
								valid_block = (new_block->pc == cs + cpu_state.pc) && (new_block->_cs == cs) &&
										(new_block->phys == phys_addr) && !((new_block->status ^ cpu_cur_status) & CPU_STATUS_FLAGS) &&
										((new_block->status & cpu_cur_status & CPU_STATUS_MASK) == (cpu_cur_status & CPU_STATUS_MASK));
								if (valid_block)
								{
									block = new_block;
#ifdef USE_NEW_DYNAREC
									codeblock_hash[hash] = get_block_nr(block);
#endif
								}
							}
						}
					}

					if (valid_block && (block->page_mask & *block->dirty_mask))
					{
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
					if (valid_block && block->page_mask2)
					{
						/*We don't want the second page to cause a page
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
						else if (block->page_mask2 & *block->dirty_mask2)
						{
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
					if (valid_block && (block->flags & CODEBLOCK_IN_DIRTY_LIST))
					{
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
						/*FPU top-of-stack does not match the value this block was compiled
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

					inrecomp=1;
					code();
					inrecomp=0;

#ifndef USE_NEW_DYNAREC
					if (!use32) cpu_state.pc &= 0xffff;
#endif
					cpu_recomp_blocks++;
				}
				else if (valid_block && !cpu_state.abrt)
				{
#ifdef USE_NEW_DYNAREC
					start_pc = cs+cpu_state.pc;
					const int max_block_size = (block->flags & CODEBLOCK_BYTE_MASK) ? ((128 - 25) - (start_pc & 0x3f)) : 1000;
#else
					start_pc = cpu_state.pc;
#endif

					cpu_block_end = 0;
					x86_was_reset = 0;

					cpu_new_blocks++;
			
					codegen_block_start_recompile(block);
					codegen_in_recompile = 1;

					while (!cpu_block_end)
					{
#ifndef USE_NEW_DYNAREC
						oldcs = CS;
						oldcpl = CPL;
#endif
						cpu_state.oldpc = cpu_state.pc;
						cpu_state.op32 = use32;

						cpu_state.ea_seg = &cpu_state.seg_ds;
						cpu_state.ssegs = 0;
		
						fetchdat = fastreadl(cs + cpu_state.pc);

						if (!cpu_state.abrt)
						{
							opcode = fetchdat & 0xFF;
							fetchdat >>= 8;
							
							trap = cpu_state.flags & T_FLAG;

							cpu_state.pc++;

							codegen_generate_call(opcode, x86_opcodes[(opcode | cpu_state.op32) & 0x3ff], fetchdat, cpu_state.pc, cpu_state.pc-1);

							x86_opcodes[(opcode | cpu_state.op32) & 0x3ff](fetchdat);

							if (x86_was_reset)
								break;
						}

#ifndef USE_NEW_DYNAREC
						if (!use32) cpu_state.pc &= 0xffff;
#endif

						/*Cap source code at 4000 bytes per block; this
						  will prevent any block from spanning more than
						  2 pages. In practice this limit will never be
						  hit, as host block size is only 2kB*/
#ifdef USE_NEW_DYNAREC
						if (((cs+cpu_state.pc) - start_pc) >= max_block_size)
#else
						if ((cpu_state.pc - start_pc) > 1000)
#endif
							CPU_BLOCK_END();
						
						if (!in_smm && smi_line/* && is_pentium*/)
							CPU_BLOCK_END();

						if (trap)
							CPU_BLOCK_END();

						if (nmi && nmi_enable && nmi_mask)
							CPU_BLOCK_END();

						if (cpu_state.abrt)
						{
							codegen_block_remove();
							CPU_BLOCK_END();
						}

						ins++;
					}
			
					if (!cpu_state.abrt && !x86_was_reset)
						codegen_block_end_recompile(block);
			
					if (x86_was_reset)
						codegen_reset();

					codegen_in_recompile = 0;
				}
				else if (!cpu_state.abrt)
				{
					/*Mark block but do not recompile*/
#ifdef USE_NEW_DYNAREC
					start_pc = cs+cpu_state.pc;
					const int max_block_size = (block->flags & CODEBLOCK_BYTE_MASK) ? ((128 - 25) - (start_pc & 0x3f)) : 1000;
#else
					start_pc = cpu_state.pc;
#endif

					cpu_block_end = 0;
					x86_was_reset = 0;

					codegen_block_init(phys_addr);

					while (!cpu_block_end)
					{
#ifndef USE_NEW_DYNAREC
						oldcs=CS;
						oldcpl = CPL;
#endif
						cpu_state.oldpc = cpu_state.pc;
						cpu_state.op32 = use32;

						cpu_state.ea_seg = &cpu_state.seg_ds;
						cpu_state.ssegs = 0;
		
						codegen_endpc = (cs + cpu_state.pc) + 8;
						fetchdat = fastreadl(cs + cpu_state.pc);

						if (!cpu_state.abrt)
						{
							opcode = fetchdat & 0xFF;
							fetchdat >>= 8;

							trap = cpu_state.flags & T_FLAG;

							cpu_state.pc++;
						
							x86_opcodes[(opcode | cpu_state.op32) & 0x3ff](fetchdat);

							if (x86_was_reset)
								break;
						}

#ifndef USE_NEW_DYNAREC
						if (!use32) cpu_state.pc &= 0xffff;
#endif

						/*Cap source code at 4000 bytes per block; this
						  will prevent any block from spanning more than
						  2 pages. In practice this limit will never be
						  hit, as host block size is only 2kB*/
#ifdef USE_NEW_DYNAREC
						if (((cs+cpu_state.pc) - start_pc) >= max_block_size)
#else
						if ((cpu_state.pc - start_pc) > 1000)
#endif
							CPU_BLOCK_END();

						if (!in_smm && smi_line/* && is_pentium*/)
							CPU_BLOCK_END();

						if (trap)
							CPU_BLOCK_END();

						if (nmi && nmi_enable && nmi_mask)
							CPU_BLOCK_END();

						if (cpu_state.abrt)
						{
							codegen_block_remove();
							CPU_BLOCK_END();
						}

						ins++;
					}
			
					if (!cpu_state.abrt && !x86_was_reset)
						codegen_block_end();
			
					if (x86_was_reset)
						codegen_reset();
				}
#ifdef USE_NEW_DYNAREC
				else
					cpu_state.oldpc = cpu_state.pc;
#endif
			}

			cycdiff=oldcyc-cycles;
			tsc += cycdiff;
		
			if (cpu_state.abrt)
			{
				flags_rebuild();
				tempi = cpu_state.abrt;
				cpu_state.abrt = 0;
				x86_doabrt(tempi);
				if (cpu_state.abrt)
				{
					cpu_state.abrt = 0;
					cpu_state.pc = cpu_state.oldpc;
#ifndef USE_NEW_DYNAREC
					CS = oldcs;
#endif
					pmodeint(8, 0);
					if (cpu_state.abrt)
					{
						cpu_state.abrt = 0;
						softresetx86();
						cpu_set_edx();
#ifdef ENABLE_386_DYNAREC_LOG
						x386_dynarec_log("Triple fault - reset\n");
#endif
					}
				}
			}

			if (!in_smm && smi_line/* && is_pentium*/) {
				enter_smm();
				smi_line = 0;
			} else if (in_smm && smi_line/* && is_pentium*/) {
				smi_latched = 1;
				smi_line = 0;
			}

			else if (trap)
			{
#ifdef USE_NEW_DYNAREC
				trap = 0;
#endif
				flags_rebuild();
				if (msw&1)
				{
					pmodeint(1,0);
				}
				else
				{
					writememw(ss,(SP-2)&0xFFFF,cpu_state.flags);
					writememw(ss,(SP-4)&0xFFFF,CS);
					writememw(ss,(SP-6)&0xFFFF,cpu_state.pc);
					SP-=6;
					addr = (1 << 2) + idt.base;
					cpu_state.flags &= ~I_FLAG;
					cpu_state.flags &= ~T_FLAG;
					cpu_state.pc=readmemw(0,addr);
					loadcs(readmemw(0,addr+2));
				}
			}
			else if (nmi && nmi_enable && nmi_mask)
			{
				cpu_state.oldpc = cpu_state.pc;
#ifndef USE_NEW_DYNAREC
				oldcs = CS;
#endif
				x86_int(2);
				nmi_enable = 0;
				if (nmi_auto_clear)
				{
					nmi_auto_clear = 0;
					nmi = 0;
				}
			}
			else if ((cpu_state.flags & I_FLAG) && pic_intpending)
			{
				vector = picinterrupt();
				if (vector != -1)
				{
					CPU_BLOCK_END();
					flags_rebuild();
					if (msw&1)
					{
						pmodeint(vector,0);
					}
					else
					{
						writememw(ss,(SP-2)&0xFFFF,cpu_state.flags);
						writememw(ss,(SP-4)&0xFFFF,CS);
						writememw(ss,(SP-6)&0xFFFF,cpu_state.pc);
						SP-=6;
						addr=vector<<2;
						cpu_state.flags &= ~I_FLAG;
						cpu_state.flags &= ~T_FLAG;
#ifndef USE_NEW_DYNAREC
						oxpc=cpu_state.pc;
#endif
						cpu_state.pc=readmemw(0,addr);
						loadcs(readmemw(0,addr+2));
					}
				}
			}

			if (TIMER_VAL_LESS_THAN_VAL(timer_target, (uint32_t)tsc))
				timer_process();
		}

		cycles_main -= (cycles_start - cycles);
	}
}
#endif
