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
#include "../86box.h"
#include "cpu.h"
#include "x86.h"
#include "x86_ops.h"
#include "x87.h"
#include "../io.h"
#include "../mem.h"
#include "../nmi.h"
#include "../pic.h"
#include "../timer.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#ifdef USE_DYNAREC
#include "codegen.h"
#include "codegen_backend.h"
#endif
#include "386_common.h"


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


#define CPU_BLOCK_END() cpu_block_end = 1

int inrecomp = 0;
int cpu_recomp_blocks, cpu_recomp_full_ins, cpu_new_blocks;
int cpu_recomp_blocks_latched, cpu_recomp_ins_latched, cpu_recomp_full_ins_latched, cpu_new_blocks_latched;

int cpu_block_end = 0;


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

#define fetch_ea_16(rmdat)              cpu_state.pc++; cpu_mod=(rmdat >> 6) & 3; cpu_reg=(rmdat >> 3) & 7; cpu_rm = rmdat & 7; if (cpu_mod != 3) { fetch_ea_16_long(rmdat); if (cpu_state.abrt) return 1; } 
#define fetch_ea_32(rmdat)              cpu_state.pc++; cpu_mod=(rmdat >> 6) & 3; cpu_reg=(rmdat >> 3) & 7; cpu_rm = rmdat & 7; if (cpu_mod != 3) { fetch_ea_32_long(rmdat); } if (cpu_state.abrt) return 1

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
static int cycles_main = 0;


void exec386_dynarec(int cycs)
{
        int vector;
        uint32_t addr;
        int tempi;
        int cycdiff;
        int oldcyc;
        int cyc_period = cycs / 2000; /*5us*/

        cycles_main += cycs;
        while (cycles_main > 0)
        {
                int cycles_start;
                
                cycles += cyc_period;
                cycles_start = cycles;

                while (cycles>0)
                {
                        oldcyc=cycles;
                        if (!CACHE_ON()) /*Interpret block*/
                        {
                                cpu_block_end = 0;
                                x86_was_reset = 0;
                                while (!cpu_block_end)
                                {
                                        cpu_state.oldpc = cpu_state.pc;
                                        cpu_state.op32 = use32;

                                        cpu_state.ea_seg = &cpu_state.seg_ds;
                                        cpu_state.ssegs = 0;
                
                                        fetchdat = fastreadl(cs + cpu_state.pc);

                                        if (!cpu_state.abrt)
                                        {
                                                uint8_t opcode = fetchdat & 0xFF;
                                                fetchdat >>= 8;
                                                trap = cpu_state.flags & T_FLAG;

                                                cpu_state.pc++;
                                                x86_opcodes[(opcode | cpu_state.op32) & 0x3ff](fetchdat);
                                        }

                                        if (((cs + cpu_state.pc) >> 12) != pccache)
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
                                codeblock_t *block = &codeblock[codeblock_hash[hash]];
                                int valid_block = 0;

                                if (!cpu_state.abrt)
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
                                                int byte_offset = (phys_addr >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK;
                                                uint64_t byte_mask = 1ull << (PAGE_BYTE_MASK_MASK & 0x3f);

                                                if ((page->code_present_mask & mask) || (page->byte_code_present_mask[byte_offset] & byte_mask))
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
                                                                        codeblock_hash[hash] = get_block_nr(block);
                                                                }
                                                        }
                                                }
                                        }

                                        if (valid_block && (block->page_mask & *block->dirty_mask))
                                        {
                                                codegen_check_flush(page, page->dirty_mask, phys_addr);
                                                if (block->pc == BLOCK_PC_INVALID)
                                                        valid_block = 0;
                                                else if (block->flags & CODEBLOCK_IN_DIRTY_LIST)
                                                        block->flags &= ~CODEBLOCK_WAS_RECOMPILED;
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
                                                uint32_t phys_addr_2 = get_phys_noabrt(block->pc + ((block->flags & CODEBLOCK_BYTE_MASK) ? 0x40 : 0x400));
                                                page_t *page_2 = &pages[phys_addr_2 >> 12];
                                                if ((block->phys_2 ^ phys_addr_2) & ~0xfff)
                                                        valid_block = 0;
                                                else if (block->page_mask2 & *block->dirty_mask2)
                                                {
                                                        codegen_check_flush(page_2, page_2->dirty_mask, phys_addr_2);
                                                        if (block->pc == BLOCK_PC_INVALID)
                                                                valid_block = 0;
                                                        else if (block->flags & CODEBLOCK_IN_DIRTY_LIST)
                                                                block->flags &= ~CODEBLOCK_WAS_RECOMPILED;
                                                }
                                        }
                                        if (valid_block && (block->flags & CODEBLOCK_IN_DIRTY_LIST))
                                        {
                                                block->flags &= ~CODEBLOCK_WAS_RECOMPILED;
                                                if (block->flags & CODEBLOCK_BYTE_MASK)
                                                        block->flags |= CODEBLOCK_NO_IMMEDIATES;
                                                else
                                                        block->flags |= CODEBLOCK_BYTE_MASK;
                                        }
                                        if (valid_block && (block->flags & CODEBLOCK_WAS_RECOMPILED) && (block->flags & CODEBLOCK_STATIC_TOP) && block->TOP != (cpu_state.TOP & 7))
                                        {
                                                /*FPU top-of-stack does not match the value this block was compiled
                                                  with, re-compile using dynamic top-of-stack*/
                                                block->flags &= ~(CODEBLOCK_STATIC_TOP | CODEBLOCK_WAS_RECOMPILED);
                                        }
                                }

                                if (valid_block && (block->flags & CODEBLOCK_WAS_RECOMPILED))
                                {
                                        void (*code)() = (void *)&block->data[BLOCK_START];

                                        inrecomp=1;
                                        code();
                                        inrecomp=0;

                                        cpu_recomp_blocks++;
                                }
                                else if (valid_block && !cpu_state.abrt)
                                {
                                        uint32_t start_pc = cs+cpu_state.pc;
                                        const int max_block_size = (block->flags & CODEBLOCK_BYTE_MASK) ? ((128 - 25) - (start_pc & 0x3f)) : 1000;
                                        
                                        cpu_block_end = 0;
                                        x86_was_reset = 0;

                                        cpu_new_blocks++;
                        
                                        codegen_block_start_recompile(block);
                                        codegen_in_recompile = 1;

                                        while (!cpu_block_end)
                                        {
                                                cpu_state.oldpc = cpu_state.pc;
                                                cpu_state.op32 = use32;

                                                cpu_state.ea_seg = &cpu_state.seg_ds;
                                                cpu_state.ssegs = 0;
                
                                                fetchdat = fastreadl(cs + cpu_state.pc);

                                                if (!cpu_state.abrt)
                                                {
                                                        uint8_t opcode = fetchdat & 0xFF;
                                                        fetchdat >>= 8;
                                                        
                                                        trap = cpu_state.flags & T_FLAG;

                                                        cpu_state.pc++;
                                                
                                                        codegen_generate_call(opcode, x86_opcodes[(opcode | cpu_state.op32) & 0x3ff], fetchdat, cpu_state.pc, cpu_state.pc-1);

                                                        x86_opcodes[(opcode | cpu_state.op32) & 0x3ff](fetchdat);

                                                        if (x86_was_reset)
                                                                break;
                                                }

                                                /*Cap source code at 4000 bytes per block; this
                                                  will prevent any block from spanning more than
                                                  2 pages. In practice this limit will never be
                                                  hit, as host block size is only 2kB*/
                                                if (((cs+cpu_state.pc) - start_pc) >= max_block_size)
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
                                        uint32_t start_pc = cs+cpu_state.pc;
                                        const int max_block_size = (block->flags & CODEBLOCK_BYTE_MASK) ? ((128 - 25) - (start_pc & 0x3f)) : 1000;

                                        cpu_block_end = 0;
                                        x86_was_reset = 0;

                                        codegen_block_init(phys_addr);

                                        while (!cpu_block_end)
                                        {
                                                cpu_state.oldpc = cpu_state.pc;
                                                cpu_state.op32 = use32;

                                                cpu_state.ea_seg = &cpu_state.seg_ds;
                                                cpu_state.ssegs = 0;
                
                                                codegen_endpc = (cs + cpu_state.pc) + 8;
                                                fetchdat = fastreadl(cs + cpu_state.pc);

                                                if (!cpu_state.abrt)
                                                {
                                                        uint8_t opcode = fetchdat & 0xFF;
                                                        fetchdat >>= 8;

                                                        trap = cpu_state.flags & T_FLAG;

                                                        cpu_state.pc++;
                                                
                                                        x86_opcodes[(opcode | cpu_state.op32) & 0x3ff](fetchdat);

                                                        if (x86_was_reset)
                                                                break;
                                                }

                                                /*Cap source code at 4000 bytes per block; this
                                                  will prevent any block from spanning more than
                                                  2 pages. In practice this limit will never be
                                                  hit, as host block size is only 2kB*/
                                                if (((cs+cpu_state.pc) - start_pc) >= max_block_size)
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
                                else
                                        cpu_state.oldpc = cpu_state.pc;

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
                                        x386_dynarec_log("Double fault %i\n", ins);
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
                
                        if (trap)
                        {
                                trap = 0;
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
                                                cpu_state.pc=readmemw(0,addr);
                                                loadcs(readmemw(0,addr+2));
                                        }
                                }
                        }
                }
        
		if (TIMER_VAL_LESS_THAN_VAL(timer_target, (uint32_t)tsc))
			timer_process();
                cycles_main -= (cycles_start - cycles);
        }
}
#endif
