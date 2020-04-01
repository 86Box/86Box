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
#include <86box/keyboard.h>
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

#define SMM_SAVE_STATE_MAP_SIZE 128


enum SMMRAM_Fields_386_To_P5 {
    SMRAM_FIELD_P5_CR0 = 0,		/* 1FC */
    SMRAM_FIELD_P5_CR3,			/* 1F8 */
    SMRAM_FIELD_P5_EFLAGS,		/* 1F4 */
    SMRAM_FIELD_P5_EIP,			/* 1F0 */
    SMRAM_FIELD_P5_EDI,			/* 1EC */
    SMRAM_FIELD_P5_ESI,			/* 1E8 */
    SMRAM_FIELD_P5_EBP,			/* 1E4 */
    SMRAM_FIELD_P5_ESP,			/* 1E0 */
    SMRAM_FIELD_P5_EBX,			/* 1DC */
    SMRAM_FIELD_P5_EDX,			/* 1D8 */
    SMRAM_FIELD_P5_ECX,			/* 1D4 */
    SMRAM_FIELD_P5_EAX,			/* 1D0 */
    SMRAM_FIELD_P5_DR6,			/* 1CC */
    SMRAM_FIELD_P5_DR7,			/* 1C8 */
    SMRAM_FIELD_P5_TR_SELECTOR,		/* 1C4 */
    SMRAM_FIELD_P5_LDTR_SELECTOR,	/* 1C0 */
    SMRAM_FIELD_P5_GS_SELECTOR,		/* 1BC */
    SMRAM_FIELD_P5_FS_SELECTOR,		/* 1B8 */
    SMRAM_FIELD_P5_DS_SELECTOR,		/* 1B4 */
    SMRAM_FIELD_P5_SS_SELECTOR,		/* 1B0 */
    SMRAM_FIELD_P5_CS_SELECTOR,		/* 1AC */
    SMRAM_FIELD_P5_ES_SELECTOR,		/* 1A8 */
    SMRAM_FIELD_P5_TR_ACCESS,		/* 1A4 */
    SMRAM_FIELD_P5_TR_BASE,		/* 1A0 */
    SMRAM_FIELD_P5_TR_LIMIT,		/* 19C */
    SMRAM_FIELD_P5_IDTR_ACCESS,		/* 198 */
    SMRAM_FIELD_P5_IDTR_BASE,		/* 194 */
    SMRAM_FIELD_P5_IDTR_LIMIT,		/* 190 */
    SMRAM_FIELD_P5_GDTR_ACCESS,		/* 18C */
    SMRAM_FIELD_P5_GDTR_BASE,		/* 188 */
    SMRAM_FIELD_P5_GDTR_LIMIT,		/* 184 */
    SMRAM_FIELD_P5_LDTR_ACCESS,		/* 180 */
    SMRAM_FIELD_P5_LDTR_BASE,		/* 17C */
    SMRAM_FIELD_P5_LDTR_LIMIT,		/* 178 */
    SMRAM_FIELD_P5_GS_ACCESS,		/* 174 */
    SMRAM_FIELD_P5_GS_BASE,		/* 170 */
    SMRAM_FIELD_P5_GS_LIMIT,		/* 16C */
    SMRAM_FIELD_P5_FS_ACCESS,		/* 168 */
    SMRAM_FIELD_P5_FS_BASE,		/* 164 */
    SMRAM_FIELD_P5_FS_LIMIT,		/* 160 */
    SMRAM_FIELD_P5_DS_ACCESS,		/* 15C */
    SMRAM_FIELD_P5_DS_BASE,		/* 158 */
    SMRAM_FIELD_P5_DS_LIMIT,		/* 154 */
    SMRAM_FIELD_P5_SS_ACCESS,		/* 150 */
    SMRAM_FIELD_P5_SS_BASE,		/* 14C */
    SMRAM_FIELD_P5_SS_LIMIT,		/* 148 */
    SMRAM_FIELD_P5_CS_ACCESS,		/* 144 */
    SMRAM_FIELD_P5_CS_BASE,		/* 140 */
    SMRAM_FIELD_P5_CS_LIMIT,		/* 13C */
    SMRAM_FIELD_P5_ES_ACCESS,		/* 138 */
    SMRAM_FIELD_P5_ES_BASE,		/* 134 */
    SMRAM_FIELD_P5_ES_LIMIT,		/* 130 */
    SMRAM_FIELD_P5_UNWRITTEN_1,		/* 12C */
    SMRAM_FIELD_P5_CR4,			/* 128 */
    SMRAM_FIELD_P5_ALTERNATE_DR6,	/* 124 */
    SMRAM_FIELD_P5_RESERVED_1,		/* 120 */
    SMRAM_FIELD_P5_RESERVED_2,		/* 11C */
    SMRAM_FIELD_P5_RESERVED_3,		/* 118 */
    SMRAM_FIELD_P5_RESERVED_4,		/* 114 */
    SMRAM_FIELD_P5_IO_RESTART_EIP,	/* 110 */
    SMRAM_FIELD_P5_IO_RESTART_ESI,	/* 10C */
    SMRAM_FIELD_P5_IO_RESTART_ECX,	/* 108 */
    SMRAM_FIELD_P5_IO_RESTART_EDI,	/* 104 */
    SMRAM_FIELD_P5_AUTOHALT_RESTART,	/* 100 */
    SMRAM_FIELD_P5_SMM_REVISION_ID,	/* 0FC */
    SMRAM_FIELD_P5_SMBASE_OFFSET,	/* 0F8 */
    SMRAM_FIELD_P5_LAST
};

enum SMMRAM_Fields_P6 {
    SMRAM_FIELD_P6_CR0 = 0,		/* 1FC */
    SMRAM_FIELD_P6_CR3,			/* 1F8 */
    SMRAM_FIELD_P6_EFLAGS,		/* 1F4 */
    SMRAM_FIELD_P6_EIP,			/* 1F0 */
    SMRAM_FIELD_P6_EDI,			/* 1EC */
    SMRAM_FIELD_P6_ESI,			/* 1E8 */
    SMRAM_FIELD_P6_EBP,			/* 1E4 */
    SMRAM_FIELD_P6_ESP,			/* 1E0 */
    SMRAM_FIELD_P6_EBX,			/* 1DC */
    SMRAM_FIELD_P6_EDX,			/* 1D8 */
    SMRAM_FIELD_P6_ECX,			/* 1D4 */
    SMRAM_FIELD_P6_EAX,			/* 1D0 */
    SMRAM_FIELD_P6_DR6,			/* 1CC */
    SMRAM_FIELD_P6_DR7,			/* 1C8 */
    SMRAM_FIELD_P6_TR_SELECTOR,		/* 1C4 */
    SMRAM_FIELD_P6_LDTR_SELECTOR,	/* 1C0 */
    SMRAM_FIELD_P6_GS_SELECTOR,		/* 1BC */
    SMRAM_FIELD_P6_FS_SELECTOR,		/* 1B8 */
    SMRAM_FIELD_P6_DS_SELECTOR,		/* 1B4 */
    SMRAM_FIELD_P6_SS_SELECTOR,		/* 1B0 */
    SMRAM_FIELD_P6_CS_SELECTOR,		/* 1AC */
    SMRAM_FIELD_P6_ES_SELECTOR,		/* 1A8 */
    SMRAM_FIELD_P6_SS_BASE,		/* 1A4 */
    SMRAM_FIELD_P6_SS_LIMIT,		/* 1A0 */
    SMRAM_FIELD_P6_SS_SELECTOR_AR,	/* 19C */
    SMRAM_FIELD_P6_CS_BASE,		/* 198 */
    SMRAM_FIELD_P6_CS_LIMIT,		/* 194 */
    SMRAM_FIELD_P6_CS_SELECTOR_AR,	/* 190 */
    SMRAM_FIELD_P6_ES_BASE,		/* 18C */
    SMRAM_FIELD_P6_ES_LIMIT,		/* 188 */
    SMRAM_FIELD_P6_ES_SELECTOR_AR,	/* 184 */
    SMRAM_FIELD_P6_LDTR_BASE,		/* 180 */
    SMRAM_FIELD_P6_LDTR_LIMIT,		/* 17C */
    SMRAM_FIELD_P6_LDTR_SELECTOR_AR,	/* 178 */
    SMRAM_FIELD_P6_GDTR_BASE,		/* 174 */
    SMRAM_FIELD_P6_GDTR_LIMIT,		/* 170 */
    SMRAM_FIELD_P6_GDTR_SELECTOR_AR,	/* 16C */
    SMRAM_FIELD_P6_SREG_STATUS1,	/* 168 */
    SMRAM_FIELD_P6_TR_BASE,		/* 164 */
    SMRAM_FIELD_P6_TR_LIMIT,		/* 160 */
    SMRAM_FIELD_P6_TR_SELECTOR_AR,	/* 15C */
    SMRAM_FIELD_P6_IDTR_BASE,		/* 158 */
    SMRAM_FIELD_P6_IDTR_LIMIT,		/* 154 */
    SMRAM_FIELD_P6_IDTR_SELECTOR_AR,	/* 150 */
    SMRAM_FIELD_P6_GS_BASE,		/* 14C */
    SMRAM_FIELD_P6_GS_LIMIT,		/* 148 */
    SMRAM_FIELD_P6_GS_SELECTOR_AR,	/* 144 */
    SMRAM_FIELD_P6_FS_BASE,		/* 140 */
    SMRAM_FIELD_P6_FS_LIMIT,		/* 13C */
    SMRAM_FIELD_P6_FS_SELECTOR_AR,	/* 138 */
    SMRAM_FIELD_P6_DS_BASE,		/* 134 */
    SMRAM_FIELD_P6_DS_LIMIT,		/* 130 */
    SMRAM_FIELD_P6_DS_SELECTOR_AR,	/* 12C */
    SMRAM_FIELD_P6_SREG_STATUS0,	/* 128 */
    SMRAM_FIELD_P6_ALTERNATIVE_DR6,	/* 124 */
    SMRAM_FIELD_P6_CPL,			/* 120 */
    SMRAM_FIELD_P6_SMM_STATUS,		/* 11C */
    SMRAM_FIELD_P6_A20M,		/* 118 */
    SMRAM_FIELD_P6_CR4,			/* 114 */
    SMRAM_FIELD_P6_IO_RESTART_EIP,	/* 110 */
    SMRAM_FIELD_P6_IO_RESTART_ESI,	/* 10C */
    SMRAM_FIELD_P6_IO_RESTART_ECX,	/* 108 */
    SMRAM_FIELD_P6_IO_RESTART_EDI,	/* 104 */
    SMRAM_FIELD_P6_AUTOHALT_RESTART,	/* 100 */
    SMRAM_FIELD_P6_SMM_REVISION_ID,	/* 0FC */
    SMRAM_FIELD_P6_SMBASE_OFFSET,	/* 0F8 */
    SMRAM_FIELD_P6_LAST
};

enum SMMRAM_Fields_AMD_K {
    SMRAM_FIELD_AMD_K_CR0 = 0,		/* 1FC */
    SMRAM_FIELD_AMD_K_CR3,		/* 1F8 */
    SMRAM_FIELD_AMD_K_EFLAGS,		/* 1F4 */
    SMRAM_FIELD_AMD_K_EIP,		/* 1F0 */
    SMRAM_FIELD_AMD_K_EDI,		/* 1EC */
    SMRAM_FIELD_AMD_K_ESI,		/* 1E8 */
    SMRAM_FIELD_AMD_K_EBP,		/* 1E4 */
    SMRAM_FIELD_AMD_K_ESP,		/* 1E0 */
    SMRAM_FIELD_AMD_K_EBX,		/* 1DC */
    SMRAM_FIELD_AMD_K_EDX,		/* 1D8 */
    SMRAM_FIELD_AMD_K_ECX,		/* 1D4 */
    SMRAM_FIELD_AMD_K_EAX,		/* 1D0 */
    SMRAM_FIELD_AMD_K_DR6,		/* 1CC */
    SMRAM_FIELD_AMD_K_DR7,		/* 1C8 */
    SMRAM_FIELD_AMD_K_TR_SELECTOR,	/* 1C4 */
    SMRAM_FIELD_AMD_K_LDTR_SELECTOR,	/* 1C0 */
    SMRAM_FIELD_AMD_K_GS_SELECTOR,	/* 1BC */
    SMRAM_FIELD_AMD_K_FS_SELECTOR,	/* 1B8 */
    SMRAM_FIELD_AMD_K_DS_SELECTOR,	/* 1B4 */
    SMRAM_FIELD_AMD_K_SS_SELECTOR,	/* 1B0 */
    SMRAM_FIELD_AMD_K_CS_SELECTOR,	/* 1AC */
    SMRAM_FIELD_AMD_K_ES_SELECTOR,	/* 1A8 */
    SMRAM_FIELD_AMD_K_IO_RESTART_DWORD,	/* 1A4 */
    SMRAM_FIELD_AMD_K_RESERVED_1,	/* 1A0 */
    SMRAM_FIELD_AMD_K_IO_RESTART_EIP,	/* 19C */
    SMRAM_FIELD_AMD_K_RESERVED_2,	/* 198 */
    SMRAM_FIELD_AMD_K_RESERVED_3,	/* 194 */
    SMRAM_FIELD_AMD_K_IDTR_BASE,	/* 190 */
    SMRAM_FIELD_AMD_K_IDTR_LIMIT,	/* 18C */
    SMRAM_FIELD_AMD_K_GDTR_BASE,	/* 188 */
    SMRAM_FIELD_AMD_K_GDTR_LIMIT,	/* 184 */
    SMRAM_FIELD_AMD_K_TR_ACCESS,	/* 180 */
    SMRAM_FIELD_AMD_K_TR_BASE,		/* 17C */
    SMRAM_FIELD_AMD_K_TR_LIMIT,		/* 178 */
    SMRAM_FIELD_AMD_K_LDTR_ACCESS,	/* 174 - reserved on K6 */
    SMRAM_FIELD_AMD_K_LDTR_BASE,	/* 170 */
    SMRAM_FIELD_AMD_K_LDTR_LIMIT,	/* 16C */
    SMRAM_FIELD_AMD_K_GS_ACCESS,	/* 168 */
    SMRAM_FIELD_AMD_K_GS_BASE,		/* 164 */
    SMRAM_FIELD_AMD_K_GS_LIMIT,		/* 160 */
    SMRAM_FIELD_AMD_K_FS_ACCESS,	/* 15C */
    SMRAM_FIELD_AMD_K_FS_BASE,		/* 158 */
    SMRAM_FIELD_AMD_K_FS_LIMIT,		/* 154 */
    SMRAM_FIELD_AMD_K_DS_ACCESS,	/* 150 */
    SMRAM_FIELD_AMD_K_DS_BASE,		/* 14C */
    SMRAM_FIELD_AMD_K_DS_LIMIT,		/* 148 */
    SMRAM_FIELD_AMD_K_SS_ACCESS,	/* 144 */
    SMRAM_FIELD_AMD_K_SS_BASE,		/* 140 */
    SMRAM_FIELD_AMD_K_SS_LIMIT,		/* 13C */
    SMRAM_FIELD_AMD_K_CS_ACCESS,	/* 138 */
    SMRAM_FIELD_AMD_K_CS_BASE,		/* 134 */
    SMRAM_FIELD_AMD_K_CS_LIMIT,		/* 130 */
    SMRAM_FIELD_AMD_K_ES_ACCESS,	/* 12C */
    SMRAM_FIELD_AMD_K_ES_BASE,		/* 128 */
    SMRAM_FIELD_AMD_K_ES_LIMIT,		/* 124 */
    SMRAM_FIELD_AMD_K_RESERVED_4,	/* 120 */
    SMRAM_FIELD_AMD_K_RESERVED_5,	/* 11C */
    SMRAM_FIELD_AMD_K_RESERVED_6,	/* 118 */
    SMRAM_FIELD_AMD_K_CR2,		/* 114 */
    SMRAM_FIELD_AMD_K_CR4,		/* 110 */
    SMRAM_FIELD_AMD_K_IO_RESTART_ESI,	/* 10C */
    SMRAM_FIELD_AMD_K_IO_RESTART_ECX,	/* 108 */
    SMRAM_FIELD_AMD_K_IO_RESTART_EDI,	/* 104 */
    SMRAM_FIELD_AMD_K_AUTOHALT_RESTART,	/* 100 */
    SMRAM_FIELD_AMD_K_SMM_REVISION_ID,	/* 0FC */
    SMRAM_FIELD_AMD_K_SMBASE_OFFSET,	/* 0F8 */
    SMRAM_FIELD_AMD_K_LAST
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


void smram_save_state_p5(uint32_t *saved_state)
{
	int n = 0;

	saved_state[SMRAM_FIELD_P5_SMM_REVISION_ID] = SMM_REVISION_ID;
	saved_state[SMRAM_FIELD_P5_SMBASE_OFFSET] = smbase;

	for (n = 0; n < 8; n++)
		saved_state[SMRAM_FIELD_P5_EAX - n] = cpu_state.regs[n].l;

	if (in_hlt) {
		saved_state[SMRAM_FIELD_P5_AUTOHALT_RESTART] = 1;
		saved_state[SMRAM_FIELD_P5_EIP] = cpu_state.pc + 1;
	} else {
		saved_state[SMRAM_FIELD_P5_AUTOHALT_RESTART] = 0;
		saved_state[SMRAM_FIELD_P5_EIP] = cpu_state.pc;
	}

	saved_state[SMRAM_FIELD_P5_EFLAGS] = (cpu_state.eflags << 16) | (cpu_state.flags);

	saved_state[SMRAM_FIELD_P5_CR0] = cr0;
	saved_state[SMRAM_FIELD_P5_CR3] = cr3;
	saved_state[SMRAM_FIELD_P5_CR4] = cr4;
	saved_state[SMRAM_FIELD_P5_DR6] = dr[6];
	saved_state[SMRAM_FIELD_P5_DR7] = dr[7];

	/* TR */
	saved_state[SMRAM_FIELD_P5_TR_SELECTOR] = tr.seg;
	saved_state[SMRAM_FIELD_P5_TR_BASE] = tr.base;
	saved_state[SMRAM_FIELD_P5_TR_LIMIT] = tr.limit;
	saved_state[SMRAM_FIELD_P5_TR_ACCESS] = (tr.ar_high << 16) | (tr.access << 8);

	/* LDTR */
	saved_state[SMRAM_FIELD_P5_LDTR_SELECTOR] = ldt.seg;
	saved_state[SMRAM_FIELD_P5_LDTR_BASE] = ldt.base;
	saved_state[SMRAM_FIELD_P5_LDTR_LIMIT] = ldt.limit;
	saved_state[SMRAM_FIELD_P5_LDTR_ACCESS] = (ldt.ar_high << 16) | (ldt.access << 8);

	/* IDTR */
	saved_state[SMRAM_FIELD_P5_IDTR_BASE] = idt.base;
	saved_state[SMRAM_FIELD_P5_IDTR_LIMIT] = idt.limit;
	saved_state[SMRAM_FIELD_P5_IDTR_ACCESS] = (idt.ar_high << 16) | (idt.access << 8);

	/* GDTR */
	saved_state[SMRAM_FIELD_P5_GDTR_BASE] = gdt.base;
	saved_state[SMRAM_FIELD_P5_GDTR_LIMIT] = gdt.limit;
	saved_state[SMRAM_FIELD_P5_GDTR_ACCESS] = (gdt.ar_high << 16) | (gdt.access << 8);

	/* ES */
	saved_state[SMRAM_FIELD_P5_ES_SELECTOR] = cpu_state.seg_es.seg;
	saved_state[SMRAM_FIELD_P5_ES_BASE] = cpu_state.seg_es.base;
	saved_state[SMRAM_FIELD_P5_ES_LIMIT] = cpu_state.seg_es.limit;
	saved_state[SMRAM_FIELD_P5_ES_ACCESS] = (cpu_state.seg_es.ar_high << 16) | (cpu_state.seg_es.access << 8);

	/* CS */
	saved_state[SMRAM_FIELD_P5_CS_SELECTOR] = cpu_state.seg_cs.seg;
	saved_state[SMRAM_FIELD_P5_CS_BASE] = cpu_state.seg_cs.base;
	saved_state[SMRAM_FIELD_P5_CS_LIMIT] = cpu_state.seg_cs.limit;
	saved_state[SMRAM_FIELD_P5_CS_ACCESS] = (cpu_state.seg_cs.ar_high << 16) | (cpu_state.seg_cs.access << 8);

	/* DS */
	saved_state[SMRAM_FIELD_P5_DS_SELECTOR] = cpu_state.seg_ds.seg;
	saved_state[SMRAM_FIELD_P5_DS_BASE] = cpu_state.seg_ds.base;
	saved_state[SMRAM_FIELD_P5_DS_LIMIT] = cpu_state.seg_ds.limit;
	saved_state[SMRAM_FIELD_P5_DS_ACCESS] = (cpu_state.seg_ds.ar_high << 16) | (cpu_state.seg_ds.access << 8);

	/* SS */
	saved_state[SMRAM_FIELD_P5_SS_SELECTOR] = cpu_state.seg_ss.seg;
	saved_state[SMRAM_FIELD_P5_SS_BASE] = cpu_state.seg_ss.base;
	saved_state[SMRAM_FIELD_P5_SS_LIMIT] = cpu_state.seg_ss.limit;
	saved_state[SMRAM_FIELD_P5_SS_ACCESS] = (cpu_state.seg_ss.ar_high << 16) | (cpu_state.seg_ss.access << 8);

	/* FS */
	saved_state[SMRAM_FIELD_P5_FS_SELECTOR] = cpu_state.seg_fs.seg;
	saved_state[SMRAM_FIELD_P5_FS_BASE] = cpu_state.seg_fs.base;
	saved_state[SMRAM_FIELD_P5_FS_LIMIT] = cpu_state.seg_fs.limit;
	saved_state[SMRAM_FIELD_P5_FS_ACCESS] = (cpu_state.seg_fs.ar_high << 16) | (cpu_state.seg_fs.access << 8);

	/* GS */
	saved_state[SMRAM_FIELD_P5_GS_SELECTOR] = cpu_state.seg_gs.seg;
	saved_state[SMRAM_FIELD_P5_GS_BASE] = cpu_state.seg_gs.base;
	saved_state[SMRAM_FIELD_P5_GS_LIMIT] = cpu_state.seg_gs.limit;
	saved_state[SMRAM_FIELD_P5_GS_ACCESS] = (cpu_state.seg_gs.ar_high << 16) | (cpu_state.seg_gs.access << 8);
}


void smram_restore_state_p5(uint32_t *saved_state)
{
	int n = 0;

	for (n = 0; n < 8; n++)
		cpu_state.regs[n].l = saved_state[SMRAM_FIELD_P5_EAX - n];

	if (saved_state[SMRAM_FIELD_P5_AUTOHALT_RESTART] & 0xffff)
		cpu_state.pc = saved_state[SMRAM_FIELD_P5_EIP] - 1;
	else
		cpu_state.pc = saved_state[SMRAM_FIELD_P5_EIP];

	cpu_state.eflags = saved_state[SMRAM_FIELD_P5_EFLAGS] >> 16;
	cpu_state.flags = saved_state[SMRAM_FIELD_P5_EFLAGS] & 0xffff;

	cr0 = saved_state[SMRAM_FIELD_P5_CR0];
	cr3 = saved_state[SMRAM_FIELD_P5_CR3];
	cr4 = saved_state[SMRAM_FIELD_P5_CR4];
	dr[6] = saved_state[SMRAM_FIELD_P5_DR6];
	dr[7] = saved_state[SMRAM_FIELD_P5_DR7];

	/* TR */
	tr.seg = saved_state[SMRAM_FIELD_P5_TR_SELECTOR];
	tr.base = saved_state[SMRAM_FIELD_P5_TR_BASE];
	tr.limit = saved_state[SMRAM_FIELD_P5_TR_LIMIT];
	tr.access = (saved_state[SMRAM_FIELD_P5_TR_ACCESS] >> 8) & 0xff;
	tr.ar_high = (saved_state[SMRAM_FIELD_P5_TR_ACCESS] >> 16) & 0xff;
	smm_seg_load(&tr);

	/* LDTR */
	ldt.seg = saved_state[SMRAM_FIELD_P5_LDTR_SELECTOR];
	ldt.base = saved_state[SMRAM_FIELD_P5_LDTR_BASE];
	ldt.limit = saved_state[SMRAM_FIELD_P5_LDTR_LIMIT];
	ldt.access = (saved_state[SMRAM_FIELD_P5_LDTR_ACCESS] >> 8) & 0xff;
	ldt.ar_high = (saved_state[SMRAM_FIELD_P5_LDTR_ACCESS] >> 16) & 0xff;
	smm_seg_load(&ldt);

	/* IDTR */
	idt.base = saved_state[SMRAM_FIELD_P5_IDTR_BASE];
	idt.limit = saved_state[SMRAM_FIELD_P5_IDTR_LIMIT];
	idt.access = (saved_state[SMRAM_FIELD_P5_IDTR_ACCESS] >> 8) & 0xff;
	idt.ar_high = (saved_state[SMRAM_FIELD_P5_IDTR_ACCESS] >> 16) & 0xff;

	/* GDTR */
	gdt.base = saved_state[SMRAM_FIELD_P5_GDTR_BASE];
	gdt.limit = saved_state[SMRAM_FIELD_P5_GDTR_LIMIT];
	gdt.access = (saved_state[SMRAM_FIELD_P5_GDTR_ACCESS] >> 8) & 0xff;
	gdt.ar_high = (saved_state[SMRAM_FIELD_P5_GDTR_ACCESS] >> 16) & 0xff;

	/* ES */
	cpu_state.seg_es.seg = saved_state[SMRAM_FIELD_P5_ES_SELECTOR];
	cpu_state.seg_es.base = saved_state[SMRAM_FIELD_P5_ES_BASE];
	cpu_state.seg_es.limit = saved_state[SMRAM_FIELD_P5_ES_LIMIT];
	cpu_state.seg_es.access = (saved_state[SMRAM_FIELD_P5_ES_ACCESS] >> 8) & 0xff;
	cpu_state.seg_es.ar_high = (saved_state[SMRAM_FIELD_P5_ES_ACCESS] >> 16) & 0xff;
	smm_seg_load(&cpu_state.seg_es);

	/* CS */
	cpu_state.seg_cs.seg = saved_state[SMRAM_FIELD_P5_CS_SELECTOR];
	cpu_state.seg_cs.base = saved_state[SMRAM_FIELD_P5_CS_BASE];
	cpu_state.seg_cs.limit = saved_state[SMRAM_FIELD_P5_CS_LIMIT];
	cpu_state.seg_cs.access = (saved_state[SMRAM_FIELD_P5_CS_ACCESS] >> 8) & 0xff;
	cpu_state.seg_cs.ar_high = (saved_state[SMRAM_FIELD_P5_CS_ACCESS] >> 16) & 0xff;
	smm_seg_load(&cpu_state.seg_cs);

	/* DS */
	cpu_state.seg_ds.seg = saved_state[SMRAM_FIELD_P5_DS_SELECTOR];
	cpu_state.seg_ds.base = saved_state[SMRAM_FIELD_P5_DS_BASE];
	cpu_state.seg_ds.limit = saved_state[SMRAM_FIELD_P5_DS_LIMIT];
	cpu_state.seg_ds.access = (saved_state[SMRAM_FIELD_P5_DS_ACCESS] >> 8) & 0xff;
	cpu_state.seg_ds.ar_high = (saved_state[SMRAM_FIELD_P5_DS_ACCESS] >> 16) & 0xff;
	smm_seg_load(&cpu_state.seg_ds);

	/* SS */
	cpu_state.seg_ss.seg = saved_state[SMRAM_FIELD_P5_SS_SELECTOR];
	cpu_state.seg_ss.base = saved_state[SMRAM_FIELD_P5_SS_BASE];
	cpu_state.seg_ss.limit = saved_state[SMRAM_FIELD_P5_SS_LIMIT];
	cpu_state.seg_ss.access = (saved_state[SMRAM_FIELD_P5_SS_ACCESS] >> 8) & 0xff;
	/* The actual CPL (DPL of CS) is overwritten with DPL of SS. */
	cpu_state.seg_cs.access = (cpu_state.seg_cs.access & ~0x60) | (cpu_state.seg_ss.access & 0x60);
	cpu_state.seg_ss.ar_high = (saved_state[SMRAM_FIELD_P5_SS_ACCESS] >> 16) & 0xff;
	smm_seg_load(&cpu_state.seg_ss);

	/* FS */
	cpu_state.seg_fs.seg = saved_state[SMRAM_FIELD_P5_FS_SELECTOR];
	cpu_state.seg_fs.base = saved_state[SMRAM_FIELD_P5_FS_BASE];
	cpu_state.seg_fs.limit = saved_state[SMRAM_FIELD_P5_FS_LIMIT];
	cpu_state.seg_fs.access = (saved_state[SMRAM_FIELD_P5_FS_ACCESS] >> 8) & 0xff;
	cpu_state.seg_fs.ar_high = (saved_state[SMRAM_FIELD_P5_FS_ACCESS] >> 16) & 0xff;
	smm_seg_load(&cpu_state.seg_fs);

	/* GS */
	cpu_state.seg_gs.seg = saved_state[SMRAM_FIELD_P5_GS_SELECTOR];
	cpu_state.seg_gs.base = saved_state[SMRAM_FIELD_P5_GS_BASE];
	cpu_state.seg_gs.limit = saved_state[SMRAM_FIELD_P5_GS_LIMIT];
	cpu_state.seg_gs.access = (saved_state[SMRAM_FIELD_P5_GS_ACCESS] >> 8) & 0xff;
	cpu_state.seg_gs.ar_high = (saved_state[SMRAM_FIELD_P5_GS_ACCESS] >> 16) & 0xff;
	smm_seg_load(&cpu_state.seg_gs);

	if (SMM_REVISION_ID & SMM_SMBASE_RELOCATION)
		smbase = saved_state[SMRAM_FIELD_P5_SMBASE_OFFSET];
}


void smram_save_state_p6(uint32_t *saved_state)
{
	int n = 0;

	saved_state[SMRAM_FIELD_P6_SMM_REVISION_ID] = SMM_REVISION_ID;
	saved_state[SMRAM_FIELD_P6_SMBASE_OFFSET] = smbase;

	for (n = 0; n < 8; n++)
		saved_state[SMRAM_FIELD_P6_EAX - n] = cpu_state.regs[n].l;

	if (in_hlt) {
		saved_state[SMRAM_FIELD_P6_AUTOHALT_RESTART] = 1;
		saved_state[SMRAM_FIELD_P6_EIP] = cpu_state.pc + 1;
	} else {
		saved_state[SMRAM_FIELD_P6_AUTOHALT_RESTART] = 0;
		saved_state[SMRAM_FIELD_P6_EIP] = cpu_state.pc;
	}

	saved_state[SMRAM_FIELD_P6_EFLAGS] = (cpu_state.eflags << 16) | (cpu_state.flags);

	saved_state[SMRAM_FIELD_P6_CR0] = cr0;
	saved_state[SMRAM_FIELD_P6_CR3] = cr3;
	saved_state[SMRAM_FIELD_P6_CR4] = cr4;
	saved_state[SMRAM_FIELD_P6_DR6] = dr[6];
	saved_state[SMRAM_FIELD_P6_DR7] = dr[7];
	saved_state[SMRAM_FIELD_P6_CPL] = CPL;
	saved_state[SMRAM_FIELD_P6_A20M] = !mem_a20_state;

	/* TR */
	saved_state[SMRAM_FIELD_P6_TR_SELECTOR] = tr.seg;
	saved_state[SMRAM_FIELD_P6_TR_BASE] = tr.base;
	saved_state[SMRAM_FIELD_P6_TR_LIMIT] = tr.limit;
	saved_state[SMRAM_FIELD_P6_TR_SELECTOR_AR] = (tr.ar_high << 24) | (tr.access << 16) | tr.seg;

	/* LDTR */
	saved_state[SMRAM_FIELD_P6_LDTR_SELECTOR] = ldt.seg;
	saved_state[SMRAM_FIELD_P6_LDTR_BASE] = ldt.base;
	saved_state[SMRAM_FIELD_P6_LDTR_LIMIT] = ldt.limit;
	saved_state[SMRAM_FIELD_P6_LDTR_SELECTOR_AR] = (ldt.ar_high << 24) | (ldt.access << 16) | ldt.seg;

	/* IDTR */
	saved_state[SMRAM_FIELD_P6_IDTR_BASE] = idt.base;
	saved_state[SMRAM_FIELD_P6_IDTR_LIMIT] = idt.limit;
	saved_state[SMRAM_FIELD_P6_IDTR_SELECTOR_AR] = (idt.ar_high << 24) | (idt.access << 16) | idt.seg;

	/* GDTR */
	saved_state[SMRAM_FIELD_P6_GDTR_BASE] = gdt.base;
	saved_state[SMRAM_FIELD_P6_GDTR_LIMIT] = gdt.limit;
	saved_state[SMRAM_FIELD_P6_GDTR_SELECTOR_AR] = (gdt.ar_high << 24) | (gdt.access << 16) | gdt.seg;

	/* ES */
	saved_state[SMRAM_FIELD_P6_ES_SELECTOR] = cpu_state.seg_es.seg;
	saved_state[SMRAM_FIELD_P6_ES_BASE] = cpu_state.seg_es.base;
	saved_state[SMRAM_FIELD_P6_ES_LIMIT] = cpu_state.seg_es.limit;
	saved_state[SMRAM_FIELD_P6_ES_SELECTOR_AR] =
		(cpu_state.seg_es.ar_high << 24) | (cpu_state.seg_es.access << 16) | cpu_state.seg_es.seg;

	/* CS */
	saved_state[SMRAM_FIELD_P6_CS_SELECTOR] = cpu_state.seg_cs.seg;
	saved_state[SMRAM_FIELD_P6_CS_BASE] = cpu_state.seg_cs.base;
	saved_state[SMRAM_FIELD_P6_CS_LIMIT] = cpu_state.seg_cs.limit;
	saved_state[SMRAM_FIELD_P6_CS_SELECTOR_AR] =
		(cpu_state.seg_cs.ar_high << 24) | (cpu_state.seg_cs.access << 16) | cpu_state.seg_cs.seg;

	/* DS */
	saved_state[SMRAM_FIELD_P6_DS_SELECTOR] = cpu_state.seg_ds.seg;
	saved_state[SMRAM_FIELD_P6_DS_BASE] = cpu_state.seg_ds.base;
	saved_state[SMRAM_FIELD_P6_DS_LIMIT] = cpu_state.seg_ds.limit;
	saved_state[SMRAM_FIELD_P6_DS_SELECTOR_AR] =
		(cpu_state.seg_ds.ar_high << 24) | (cpu_state.seg_ds.access << 16) | cpu_state.seg_ds.seg;

	/* SS */
	saved_state[SMRAM_FIELD_P6_SS_SELECTOR] = cpu_state.seg_ss.seg;
	saved_state[SMRAM_FIELD_P6_SS_BASE] = cpu_state.seg_ss.base;
	saved_state[SMRAM_FIELD_P6_SS_LIMIT] = cpu_state.seg_ss.limit;
	saved_state[SMRAM_FIELD_P6_SS_SELECTOR_AR] =
		(cpu_state.seg_ss.ar_high << 24) | (cpu_state.seg_ss.access << 16) | cpu_state.seg_ss.seg;

	/* FS */
	saved_state[SMRAM_FIELD_P6_FS_SELECTOR] = cpu_state.seg_fs.seg;
	saved_state[SMRAM_FIELD_P6_FS_BASE] = cpu_state.seg_fs.base;
	saved_state[SMRAM_FIELD_P6_FS_LIMIT] = cpu_state.seg_fs.limit;
	saved_state[SMRAM_FIELD_P6_FS_SELECTOR_AR] =
		(cpu_state.seg_fs.ar_high << 24) | (cpu_state.seg_fs.access << 16) | cpu_state.seg_fs.seg;

	/* GS */
	saved_state[SMRAM_FIELD_P6_GS_SELECTOR] = cpu_state.seg_gs.seg;
	saved_state[SMRAM_FIELD_P6_GS_BASE] = cpu_state.seg_gs.base;
	saved_state[SMRAM_FIELD_P6_GS_LIMIT] = cpu_state.seg_gs.limit;
	saved_state[SMRAM_FIELD_P6_GS_SELECTOR_AR] =
		(cpu_state.seg_gs.ar_high << 24) | (cpu_state.seg_gs.access << 16) | cpu_state.seg_gs.seg;
}


void smram_restore_state_p6(uint32_t *saved_state)
{
	int n = 0;

	for (n = 0; n < 8; n++)
		cpu_state.regs[n].l = saved_state[SMRAM_FIELD_P6_EAX - n];

	if (saved_state[SMRAM_FIELD_P6_AUTOHALT_RESTART] & 0xffff)
		cpu_state.pc = saved_state[SMRAM_FIELD_P6_EIP] - 1;
	else
		cpu_state.pc = saved_state[SMRAM_FIELD_P6_EIP];

	cpu_state.eflags = saved_state[SMRAM_FIELD_P6_EFLAGS] >> 16;
	cpu_state.flags = saved_state[SMRAM_FIELD_P6_EFLAGS] & 0xffff;

	cr0 = saved_state[SMRAM_FIELD_P6_CR0];
	cr3 = saved_state[SMRAM_FIELD_P6_CR3];
	cr4 = saved_state[SMRAM_FIELD_P6_CR4];
	dr[6] = saved_state[SMRAM_FIELD_P6_DR6];
	dr[7] = saved_state[SMRAM_FIELD_P6_DR7];

	/* TR */
	tr.seg = saved_state[SMRAM_FIELD_P6_TR_SELECTOR];
	tr.base = saved_state[SMRAM_FIELD_P6_TR_BASE];
	tr.limit = saved_state[SMRAM_FIELD_P6_TR_LIMIT];
	tr.access = (saved_state[SMRAM_FIELD_P6_TR_SELECTOR_AR] >> 16) & 0xff;
	tr.ar_high = (saved_state[SMRAM_FIELD_P6_TR_SELECTOR_AR] >> 24) & 0xff;
	smm_seg_load(&tr);

	/* LDTR */
	ldt.seg = saved_state[SMRAM_FIELD_P6_LDTR_SELECTOR];
	ldt.base = saved_state[SMRAM_FIELD_P6_LDTR_BASE];
	ldt.limit = saved_state[SMRAM_FIELD_P6_LDTR_LIMIT];
	ldt.access = (saved_state[SMRAM_FIELD_P6_LDTR_SELECTOR_AR] >> 16) & 0xff;
	ldt.ar_high = (saved_state[SMRAM_FIELD_P6_LDTR_SELECTOR_AR] >> 24) & 0xff;
	smm_seg_load(&ldt);

	/* IDTR */
	idt.base = saved_state[SMRAM_FIELD_P6_IDTR_BASE];
	idt.limit = saved_state[SMRAM_FIELD_P6_IDTR_LIMIT];
	idt.access = (saved_state[SMRAM_FIELD_P6_IDTR_SELECTOR_AR] >> 16) & 0xff;
	idt.ar_high = (saved_state[SMRAM_FIELD_P6_IDTR_SELECTOR_AR] >> 24) & 0xff;

	/* GDTR */
	gdt.base = saved_state[SMRAM_FIELD_P6_GDTR_BASE];
	gdt.limit = saved_state[SMRAM_FIELD_P6_GDTR_LIMIT];
	gdt.access = (saved_state[SMRAM_FIELD_P6_GDTR_SELECTOR_AR] >> 16) & 0xff;
	gdt.ar_high = (saved_state[SMRAM_FIELD_P6_GDTR_SELECTOR_AR] >> 24) & 0xff;

	/* ES */
	cpu_state.seg_es.seg = saved_state[SMRAM_FIELD_P6_ES_SELECTOR];
	cpu_state.seg_es.base = saved_state[SMRAM_FIELD_P6_ES_BASE];
	cpu_state.seg_es.limit = saved_state[SMRAM_FIELD_P6_ES_LIMIT];
	cpu_state.seg_es.access = (saved_state[SMRAM_FIELD_P6_ES_SELECTOR_AR] >> 16) & 0xff;
	cpu_state.seg_es.ar_high = (saved_state[SMRAM_FIELD_P6_ES_SELECTOR_AR] >> 24) & 0xff;
	smm_seg_load(&cpu_state.seg_es);

	/* CS */
	cpu_state.seg_cs.seg = saved_state[SMRAM_FIELD_P6_CS_SELECTOR];
	cpu_state.seg_cs.base = saved_state[SMRAM_FIELD_P6_CS_BASE];
	cpu_state.seg_cs.limit = saved_state[SMRAM_FIELD_P6_CS_LIMIT];
	cpu_state.seg_cs.access = (saved_state[SMRAM_FIELD_P6_CS_SELECTOR_AR] >> 16) & 0xff;
	cpu_state.seg_cs.ar_high = (saved_state[SMRAM_FIELD_P6_CS_SELECTOR_AR] >> 24) & 0xff;
	smm_seg_load(&cpu_state.seg_cs);
	cpu_state.seg_cs.access = (cpu_state.seg_cs.access & ~0x60) | ((saved_state[SMRAM_FIELD_P6_CPL] & 0x03) << 5);

	/* DS */
	cpu_state.seg_ds.seg = saved_state[SMRAM_FIELD_P6_DS_SELECTOR];
	cpu_state.seg_ds.base = saved_state[SMRAM_FIELD_P6_DS_BASE];
	cpu_state.seg_ds.limit = saved_state[SMRAM_FIELD_P6_DS_LIMIT];
	cpu_state.seg_ds.access = (saved_state[SMRAM_FIELD_P6_DS_SELECTOR_AR] >> 16) & 0xff;
	cpu_state.seg_ds.ar_high = (saved_state[SMRAM_FIELD_P6_DS_SELECTOR_AR] >> 24) & 0xff;
	smm_seg_load(&cpu_state.seg_ds);

	/* SS */
	cpu_state.seg_ss.seg = saved_state[SMRAM_FIELD_P6_SS_SELECTOR];
	cpu_state.seg_ss.base = saved_state[SMRAM_FIELD_P6_SS_BASE];
	cpu_state.seg_ss.limit = saved_state[SMRAM_FIELD_P6_SS_LIMIT];
	cpu_state.seg_ss.access = (saved_state[SMRAM_FIELD_P6_SS_SELECTOR_AR] >> 16) & 0xff;
	cpu_state.seg_ss.ar_high = (saved_state[SMRAM_FIELD_P6_SS_SELECTOR_AR] >> 24) & 0xff;
	smm_seg_load(&cpu_state.seg_ss);

	/* FS */
	cpu_state.seg_fs.seg = saved_state[SMRAM_FIELD_P6_FS_SELECTOR];
	cpu_state.seg_fs.base = saved_state[SMRAM_FIELD_P6_FS_BASE];
	cpu_state.seg_fs.limit = saved_state[SMRAM_FIELD_P6_FS_LIMIT];
	cpu_state.seg_fs.access = (saved_state[SMRAM_FIELD_P6_FS_SELECTOR_AR] >> 16) & 0xff;
	cpu_state.seg_fs.ar_high = (saved_state[SMRAM_FIELD_P6_FS_SELECTOR_AR] >> 24) & 0xff;
	smm_seg_load(&cpu_state.seg_fs);

	/* GS */
	cpu_state.seg_gs.seg = saved_state[SMRAM_FIELD_P6_GS_SELECTOR];
	cpu_state.seg_gs.base = saved_state[SMRAM_FIELD_P6_GS_BASE];
	cpu_state.seg_gs.limit = saved_state[SMRAM_FIELD_P6_GS_LIMIT];
	cpu_state.seg_gs.access = (saved_state[SMRAM_FIELD_P6_GS_SELECTOR_AR] >> 16) & 0xff;
	cpu_state.seg_gs.ar_high = (saved_state[SMRAM_FIELD_P6_GS_SELECTOR_AR] >> 24) & 0xff;
	smm_seg_load(&cpu_state.seg_gs);

	mem_a20_alt = 0;
	keyboard_at_set_a20_key(!saved_state[SMRAM_FIELD_P6_A20M]);
	mem_a20_recalc();

	if (SMM_REVISION_ID & SMM_SMBASE_RELOCATION)
		smbase = saved_state[SMRAM_FIELD_P6_SMBASE_OFFSET];
}


void smram_save_state_amd_k(uint32_t *saved_state)
{
	int n = 0;

	saved_state[SMRAM_FIELD_AMD_K_SMM_REVISION_ID] = SMM_REVISION_ID;
	saved_state[SMRAM_FIELD_AMD_K_SMBASE_OFFSET] = smbase;

	for (n = 0; n < 8; n++)
		saved_state[SMRAM_FIELD_AMD_K_EAX - n] = cpu_state.regs[n].l;

	if (in_hlt) {
		saved_state[SMRAM_FIELD_AMD_K_AUTOHALT_RESTART] = 1;
		saved_state[SMRAM_FIELD_AMD_K_EIP] = cpu_state.pc + 1;
	} else {
		saved_state[SMRAM_FIELD_AMD_K_AUTOHALT_RESTART] = 0;
		saved_state[SMRAM_FIELD_AMD_K_EIP] = cpu_state.pc;
	}

	saved_state[SMRAM_FIELD_AMD_K_EFLAGS] = (cpu_state.eflags << 16) | (cpu_state.flags);

	saved_state[SMRAM_FIELD_AMD_K_CR0] = cr0;
	saved_state[SMRAM_FIELD_AMD_K_CR2] = cr2;
	saved_state[SMRAM_FIELD_AMD_K_CR3] = cr3;
	saved_state[SMRAM_FIELD_AMD_K_CR4] = cr4;
	saved_state[SMRAM_FIELD_AMD_K_DR6] = dr[6];
	saved_state[SMRAM_FIELD_AMD_K_DR7] = dr[7];

	/* TR */
	saved_state[SMRAM_FIELD_AMD_K_TR_SELECTOR] = tr.seg;
	saved_state[SMRAM_FIELD_AMD_K_TR_BASE] = tr.base;
	saved_state[SMRAM_FIELD_AMD_K_TR_LIMIT] = tr.limit;
	saved_state[SMRAM_FIELD_AMD_K_TR_ACCESS] = (tr.ar_high << 16) | (tr.access << 8);

	/* LDTR */
	saved_state[SMRAM_FIELD_AMD_K_LDTR_SELECTOR] = ldt.seg;
	saved_state[SMRAM_FIELD_AMD_K_LDTR_BASE] = ldt.base;
	saved_state[SMRAM_FIELD_AMD_K_LDTR_LIMIT] = ldt.limit;
	if (!is_k6)
		saved_state[SMRAM_FIELD_AMD_K_LDTR_ACCESS] = (ldt.ar_high << 16) | (ldt.access << 8);

	/* IDTR */
	saved_state[SMRAM_FIELD_AMD_K_IDTR_BASE] = idt.base;
	saved_state[SMRAM_FIELD_AMD_K_IDTR_LIMIT] = idt.limit;

	/* GDTR */
	saved_state[SMRAM_FIELD_AMD_K_GDTR_BASE] = gdt.base;
	saved_state[SMRAM_FIELD_AMD_K_GDTR_LIMIT] = gdt.limit;

	/* ES */
	saved_state[SMRAM_FIELD_AMD_K_ES_SELECTOR] = cpu_state.seg_es.seg;
	saved_state[SMRAM_FIELD_AMD_K_ES_BASE] = cpu_state.seg_es.base;
	saved_state[SMRAM_FIELD_AMD_K_ES_LIMIT] = cpu_state.seg_es.limit;
	saved_state[SMRAM_FIELD_AMD_K_ES_ACCESS] = (cpu_state.seg_es.ar_high << 16) | (cpu_state.seg_es.access << 8);

	/* CS */
	saved_state[SMRAM_FIELD_AMD_K_CS_SELECTOR] = cpu_state.seg_cs.seg;
	saved_state[SMRAM_FIELD_AMD_K_CS_BASE] = cpu_state.seg_cs.base;
	saved_state[SMRAM_FIELD_AMD_K_CS_LIMIT] = cpu_state.seg_cs.limit;
	saved_state[SMRAM_FIELD_AMD_K_CS_ACCESS] = (cpu_state.seg_cs.ar_high << 16) | (cpu_state.seg_cs.access << 8);

	/* DS */
	saved_state[SMRAM_FIELD_AMD_K_DS_SELECTOR] = cpu_state.seg_ds.seg;
	saved_state[SMRAM_FIELD_AMD_K_DS_BASE] = cpu_state.seg_ds.base;
	saved_state[SMRAM_FIELD_AMD_K_DS_LIMIT] = cpu_state.seg_ds.limit;
	saved_state[SMRAM_FIELD_AMD_K_DS_ACCESS] = (cpu_state.seg_ds.ar_high << 16) | (cpu_state.seg_ds.access << 8);

	/* SS */
	saved_state[SMRAM_FIELD_AMD_K_SS_SELECTOR] = cpu_state.seg_ss.seg;
	saved_state[SMRAM_FIELD_AMD_K_SS_BASE] = cpu_state.seg_ss.base;
	saved_state[SMRAM_FIELD_AMD_K_SS_LIMIT] = cpu_state.seg_ss.limit;
	saved_state[SMRAM_FIELD_AMD_K_SS_ACCESS] = (cpu_state.seg_ss.ar_high << 16) | (cpu_state.seg_ss.access << 8);

	/* FS */
	saved_state[SMRAM_FIELD_AMD_K_FS_SELECTOR] = cpu_state.seg_fs.seg;
	saved_state[SMRAM_FIELD_AMD_K_FS_BASE] = cpu_state.seg_fs.base;
	saved_state[SMRAM_FIELD_AMD_K_FS_LIMIT] = cpu_state.seg_fs.limit;
	saved_state[SMRAM_FIELD_AMD_K_FS_ACCESS] = (cpu_state.seg_fs.ar_high << 16) | (cpu_state.seg_fs.access << 8);

	/* GS */
	saved_state[SMRAM_FIELD_AMD_K_GS_SELECTOR] = cpu_state.seg_gs.seg;
	saved_state[SMRAM_FIELD_AMD_K_GS_BASE] = cpu_state.seg_gs.base;
	saved_state[SMRAM_FIELD_AMD_K_GS_LIMIT] = cpu_state.seg_gs.limit;
	saved_state[SMRAM_FIELD_AMD_K_GS_ACCESS] = (cpu_state.seg_gs.ar_high << 16) | (cpu_state.seg_gs.access << 8);
}


void smram_restore_state_amd_k(uint32_t *saved_state)
{
	int n = 0;

	for (n = 0; n < 8; n++)
		cpu_state.regs[n].l = saved_state[SMRAM_FIELD_AMD_K_EAX - n];

	if (saved_state[SMRAM_FIELD_AMD_K_AUTOHALT_RESTART] & 0xffff)
		cpu_state.pc = saved_state[SMRAM_FIELD_AMD_K_EIP] - 1;
	else
		cpu_state.pc = saved_state[SMRAM_FIELD_AMD_K_EIP];

	cpu_state.eflags = saved_state[SMRAM_FIELD_AMD_K_EFLAGS] >> 16;
	cpu_state.flags = saved_state[SMRAM_FIELD_AMD_K_EFLAGS] & 0xffff;

	cr0 = saved_state[SMRAM_FIELD_AMD_K_CR0];
	cr2 = saved_state[SMRAM_FIELD_AMD_K_CR2];
	cr3 = saved_state[SMRAM_FIELD_AMD_K_CR3];
	cr4 = saved_state[SMRAM_FIELD_AMD_K_CR4];
	dr[6] = saved_state[SMRAM_FIELD_AMD_K_DR6];
	dr[7] = saved_state[SMRAM_FIELD_AMD_K_DR7];

	/* TR */
	tr.seg = saved_state[SMRAM_FIELD_AMD_K_TR_SELECTOR];
	tr.base = saved_state[SMRAM_FIELD_AMD_K_TR_BASE];
	tr.limit = saved_state[SMRAM_FIELD_AMD_K_TR_LIMIT];
	tr.access = (saved_state[SMRAM_FIELD_AMD_K_TR_ACCESS] >> 8) & 0xff;
	tr.ar_high = (saved_state[SMRAM_FIELD_AMD_K_TR_ACCESS] >> 16) & 0xff;
	smm_seg_load(&tr);

	/* LDTR */
	ldt.seg = saved_state[SMRAM_FIELD_AMD_K_LDTR_SELECTOR];
	ldt.base = saved_state[SMRAM_FIELD_AMD_K_LDTR_BASE];
	ldt.limit = saved_state[SMRAM_FIELD_AMD_K_LDTR_LIMIT];
	if (!is_k6) {
		ldt.access = (saved_state[SMRAM_FIELD_AMD_K_LDTR_ACCESS] >> 8) & 0xff;
		ldt.ar_high = (saved_state[SMRAM_FIELD_AMD_K_LDTR_ACCESS] >> 16) & 0xff;
	}
	smm_seg_load(&ldt);

	/* IDTR */
	idt.base = saved_state[SMRAM_FIELD_AMD_K_IDTR_BASE];
	idt.limit = saved_state[SMRAM_FIELD_AMD_K_IDTR_LIMIT];

	/* GDTR */
	gdt.base = saved_state[SMRAM_FIELD_AMD_K_GDTR_BASE];
	gdt.limit = saved_state[SMRAM_FIELD_AMD_K_GDTR_LIMIT];

	/* ES */
	cpu_state.seg_es.seg = saved_state[SMRAM_FIELD_AMD_K_ES_SELECTOR];
	cpu_state.seg_es.base = saved_state[SMRAM_FIELD_AMD_K_ES_BASE];
	cpu_state.seg_es.limit = saved_state[SMRAM_FIELD_AMD_K_ES_LIMIT];
	cpu_state.seg_es.access = (saved_state[SMRAM_FIELD_AMD_K_ES_ACCESS] >> 8) & 0xff;
	cpu_state.seg_es.ar_high = (saved_state[SMRAM_FIELD_AMD_K_ES_ACCESS] >> 16) & 0xff;
	smm_seg_load(&cpu_state.seg_es);

	/* CS */
	cpu_state.seg_cs.seg = saved_state[SMRAM_FIELD_AMD_K_CS_SELECTOR];
	cpu_state.seg_cs.base = saved_state[SMRAM_FIELD_AMD_K_CS_BASE];
	cpu_state.seg_cs.limit = saved_state[SMRAM_FIELD_AMD_K_CS_LIMIT];
	cpu_state.seg_cs.access = (saved_state[SMRAM_FIELD_AMD_K_CS_ACCESS] >> 8) & 0xff;
	cpu_state.seg_cs.ar_high = (saved_state[SMRAM_FIELD_AMD_K_CS_ACCESS] >> 16) & 0xff;
	smm_seg_load(&cpu_state.seg_cs);

	/* DS */
	cpu_state.seg_ds.seg = saved_state[SMRAM_FIELD_AMD_K_DS_SELECTOR];
	cpu_state.seg_ds.base = saved_state[SMRAM_FIELD_AMD_K_DS_BASE];
	cpu_state.seg_ds.limit = saved_state[SMRAM_FIELD_AMD_K_DS_LIMIT];
	cpu_state.seg_ds.access = (saved_state[SMRAM_FIELD_AMD_K_DS_ACCESS] >> 8) & 0xff;
	cpu_state.seg_ds.ar_high = (saved_state[SMRAM_FIELD_AMD_K_DS_ACCESS] >> 16) & 0xff;
	smm_seg_load(&cpu_state.seg_ds);

	/* SS */
	cpu_state.seg_ss.seg = saved_state[SMRAM_FIELD_AMD_K_SS_SELECTOR];
	cpu_state.seg_ss.base = saved_state[SMRAM_FIELD_AMD_K_SS_BASE];
	cpu_state.seg_ss.limit = saved_state[SMRAM_FIELD_AMD_K_SS_LIMIT];
	cpu_state.seg_ss.access = (saved_state[SMRAM_FIELD_AMD_K_SS_ACCESS] >> 8) & 0xff;
	/* The actual CPL (DPL of CS) is overwritten with DPL of SS. */
	cpu_state.seg_cs.access = (cpu_state.seg_cs.access & ~0x60) | (cpu_state.seg_ss.access & 0x60);
	cpu_state.seg_ss.ar_high = (saved_state[SMRAM_FIELD_AMD_K_SS_ACCESS] >> 16) & 0xff;
	smm_seg_load(&cpu_state.seg_ss);

	/* FS */
	cpu_state.seg_fs.seg = saved_state[SMRAM_FIELD_AMD_K_FS_SELECTOR];
	cpu_state.seg_fs.base = saved_state[SMRAM_FIELD_AMD_K_FS_BASE];
	cpu_state.seg_fs.limit = saved_state[SMRAM_FIELD_AMD_K_FS_LIMIT];
	cpu_state.seg_fs.access = (saved_state[SMRAM_FIELD_AMD_K_FS_ACCESS] >> 8) & 0xff;
	cpu_state.seg_fs.ar_high = (saved_state[SMRAM_FIELD_AMD_K_FS_ACCESS] >> 16) & 0xff;
	smm_seg_load(&cpu_state.seg_fs);

	/* GS */
	cpu_state.seg_gs.seg = saved_state[SMRAM_FIELD_AMD_K_GS_SELECTOR];
	cpu_state.seg_gs.base = saved_state[SMRAM_FIELD_AMD_K_GS_BASE];
	cpu_state.seg_gs.limit = saved_state[SMRAM_FIELD_AMD_K_GS_LIMIT];
	cpu_state.seg_gs.access = (saved_state[SMRAM_FIELD_AMD_K_GS_ACCESS] >> 8) & 0xff;
	cpu_state.seg_gs.ar_high = (saved_state[SMRAM_FIELD_AMD_K_GS_ACCESS] >> 16) & 0xff;
	smm_seg_load(&cpu_state.seg_gs);

	if (SMM_REVISION_ID & SMM_SMBASE_RELOCATION)
		smbase = saved_state[SMRAM_FIELD_AMD_K_SMBASE_OFFSET];
}


void enter_smm()
{
	uint32_t saved_state[SMM_SAVE_STATE_MAP_SIZE], n;
	uint32_t smram_state = smbase + 0x10000;

	/* If it's a CPU on which SMM is not supporter, do nothing. */
	if (!is_pentium && !is_k5 && !is_k6 && !is_p6)
		return;

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

	in_smm = 1;
	mem_mapping_recalc(0x00030000, 0x00020000);
	mem_mapping_recalc(0x000a0000, 0x00060000);
	if (!cpu_16bitbus)
		mem_mapping_recalc(0x100a0000, 0x00060000);
	if (mem_size >= 1024)
		mem_mapping_recalc((mem_size << 10) - (1 << 20), (1 << 20));
	flushmmucache();

	memset(saved_state, 0x00, SMM_SAVE_STATE_MAP_SIZE * sizeof(uint32_t));
	if (is_pentium)			/* Intel P5 (Pentium) */
		smram_save_state_p5(saved_state);
	else if (is_k5 || is_k6)	/* AMD K5 and K6 */
		smram_save_state_amd_k(saved_state);
	else if (is_p6)			/* Intel P6 (Pentium Pro, Pentium II, Celeron) */
		smram_save_state_p6(saved_state);
	for (n = 0; n < SMM_SAVE_STATE_MAP_SIZE; n++) {
		smram_state -= 4;
		mem_writel_phys(smram_state, saved_state[n]);
	}

	cr0 &= ~0x8000000d;
	cpu_state.flags = 2;
	cpu_state.eflags = 0;

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

	if (is_p6)
		cpu_state.seg_cs.seg = (smbase >> 4);
	else
		cpu_state.seg_cs.seg = 0x3000;
		/* On Pentium, CS selector in SMM is always 3000, regardless of SMBASE. */
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

	cpu_state.op32 = use32;

	nmi_mask = 0;

	if (smi_latched) {
		in_smm = 2;
		smi_latched = 0;
	} else
		in_smm = 1;

	CPU_BLOCK_END();
}


void leave_smm()
{
	uint32_t saved_state[SMM_SAVE_STATE_MAP_SIZE], n;
	uint32_t smram_state = smbase + 0x10000;

	/* If it's a CPU on which SMM is not supporter, do nothing. */
	if (!is_pentium && !is_k5 && !is_k6 && !is_p6)
		return;

	memset(saved_state, 0x00, SMM_SAVE_STATE_MAP_SIZE * sizeof(uint32_t));
	for (n = 0; n < SMM_SAVE_STATE_MAP_SIZE; n++) {
		smram_state -= 4;
		saved_state[n] = mem_readl_phys(smram_state);
		x386_dynarec_log("Reading %08X from memory at %08X to array element %i\n", saved_state[n], smram_state, n);
	}
#ifdef ENABLE_386_DYNAREC_LOG
	for (n = 0; n < SMM_SAVE_STATE_MAP_SIZE; n++) {
		if (saved_state[n] == 0x000a0000)
			x86_dynarec_log("SMBASE found in array element %i (we have %i)\n", n, SMRAM_FIELD_P5_SMBASE_OFFSET);
	}
#endif
	x386_dynarec_log("New SMBASE: %08X (%08X)\n", saved_state[SMRAM_FIELD_P5_SMBASE_OFFSET], saved_state[66]);
	if (is_pentium)			/* Intel P5 (Pentium) */
		smram_restore_state_p5(saved_state);
	else if (is_k5 || is_k6)	/* AMD K5 and K6 */
		smram_restore_state_amd_k(saved_state);
	else if (is_p6)			/* Intel P6 (Pentium Pro, Pentium II, Celeron) */
		smram_restore_state_p6(saved_state);

	in_smm = 0;
	mem_mapping_recalc(0x00030000, 0x00020000);
	mem_mapping_recalc(0x000a0000, 0x00060000);
	if (!cpu_16bitbus)
		mem_mapping_recalc(0x100a0000, 0x00060000);
	if (mem_size >= 1024)
		mem_mapping_recalc((mem_size << 10) - (1 << 20), (1 << 20));
	flushmmucache();

	cpu_state.op32 = use32;

	nmi_mask = 1;

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
#define CLOCK_CYCLES(c) do { cycles -= (c); in_hlt = 0; } while(0)
#define CLOCK_CYCLES_ALWAYS(c) do { cycles -= (c); in_hlt = 0; } while(0)

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

					if ((in_smm == 0) && smi_line)
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
						
						if ((in_smm == 0) && smi_line)
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

						if ((in_smm == 0) && smi_line)
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

			if ((in_smm == 0) && smi_line) {
#ifdef ENABLE_386_DYNAREC_LOG
				x386_dynarec_log("SMI while not in SMM\n");
#endif
				enter_smm();
				smi_line = 0;
			} else if ((in_smm == 1) && smi_line) {
				/* Mark this so that we don't latch more than one SMI. */
#ifdef ENABLE_386_DYNAREC_LOG
				x386_dynarec_log("SMI while in unlatched SMM\n");
#endif
				smi_latched = 1;
				smi_line = 0;
			} else if ((in_smm == 2) && smi_line) {
				/* Mark this so that we don't latch more than one SMI. */
#ifdef ENABLE_386_DYNAREC_LOG
				x386_dynarec_log("SMI while in latched SMM\n");
#endif
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
