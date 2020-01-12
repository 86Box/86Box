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
#endif
#include "386_common.h"


#define CPU_BLOCK_END() cpu_block_end = 1

uint32_t cpu_cur_status = 0;

int cpu_reps, cpu_reps_latched;
int cpu_notreps, cpu_notreps_latched;

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
#define x86_dynarec_log (fmt, ...)
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

#define fetch_ea_16(rmdat)              cpu_state.pc++; cpu_mod=(rmdat >> 6) & 3; cpu_reg=(rmdat >> 3) & 7; cpu_rm = rmdat & 7; if (cpu_mod != 3) { fetch_ea_16_long(rmdat); if (cpu_state.abrt) return 1; } 
#define fetch_ea_32(rmdat)              cpu_state.pc++; cpu_mod=(rmdat >> 6) & 3; cpu_reg=(rmdat >> 3) & 7; cpu_rm = rmdat & 7; if (cpu_mod != 3) { fetch_ea_32_long(rmdat); } if (cpu_state.abrt) return 1

#include "x86_flags.h"

void x86_int(int num)
{
        uint32_t addr;
        flags_rebuild();
        cpu_state.pc=cpu_state.oldpc;
        if (msw&1)
        {
                pmodeint(num,0);
        }
        else
        {
                addr = (num << 2) + idt.base;

                if ((num << 2) + 3 > idt.limit)
                {
                        if (idt.limit < 35)
                        {
                                cpu_state.abrt = 0;
                                softresetx86();
                                cpu_set_edx();
#ifdef ENABLE_386_DYNAREC_LOG
                                x386_dynarec_log("Triple fault in real mode - reset\n");
#endif
                        }
                        else
                                x86_int(8);
                }
                else
                {
                        if (stack32)
                        {
                                writememw(ss,ESP-2,cpu_state.flags);
                                writememw(ss,ESP-4,CS);
                                writememw(ss,ESP-6,cpu_state.pc);
                                ESP-=6;
                        }
                        else
                        {
                                writememw(ss,((SP-2)&0xFFFF),cpu_state.flags);
                                writememw(ss,((SP-4)&0xFFFF),CS);
                                writememw(ss,((SP-6)&0xFFFF),cpu_state.pc);
                                SP-=6;
                        }

                        cpu_state.flags&=~I_FLAG;
                        cpu_state.flags&=~T_FLAG;
						oxpc=cpu_state.pc;
                        cpu_state.pc=readmemw(0,addr);
                        loadcs(readmemw(0,addr+2));
                }
        }
        cycles-=70;
        CPU_BLOCK_END();
}

void x86_int_sw(int num)
{
        uint32_t addr;
        flags_rebuild();
        cycles -= timing_int;
        if (msw&1)
        {
                pmodeint(num,1);
        }
        else
        {
                addr = (num << 2) + idt.base;

                if ((num << 2) + 3 > idt.limit)
                {
                        x86_int(13);
                }
                else
                {
                        if (stack32)
                        {
                                writememw(ss,ESP-2,cpu_state.flags);
                                writememw(ss,ESP-4,CS);
                                writememw(ss,ESP-6,cpu_state.pc);
                                ESP-=6;
                        }
                        else
                        {
                                writememw(ss,((SP-2)&0xFFFF),cpu_state.flags);
                                writememw(ss,((SP-4)&0xFFFF),CS);
                                writememw(ss,((SP-6)&0xFFFF),cpu_state.pc);
                                SP-=6;
                        }

                        cpu_state.flags&=~I_FLAG;
                        cpu_state.flags&=~T_FLAG;
						oxpc=cpu_state.pc;
                        cpu_state.pc=readmemw(0,addr);
                        loadcs(readmemw(0,addr+2));
                        cycles -= timing_int_rm;
                }
        }
        trap = 0;
        CPU_BLOCK_END();
}

int x86_int_sw_rm(int num)
{
        uint32_t addr;
        uint16_t new_pc, new_cs;
        
        flags_rebuild();
        cycles -= timing_int;

        addr = num << 2;
        new_pc = readmemw(0, addr);
        new_cs = readmemw(0, addr + 2);

        if (cpu_state.abrt) return 1;

        writememw(ss,((SP-2)&0xFFFF),cpu_state.flags);
	if (cpu_state.abrt) {
#ifdef ENABLE_386_DYNAREC_LOG
		x386_dynarec_log("abrt5\n");
#endif
		return 1;
	}
        writememw(ss,((SP-4)&0xFFFF),CS);
        writememw(ss,((SP-6)&0xFFFF),cpu_state.pc);
	if (cpu_state.abrt) {
#ifdef ENABLE_386_DYNAREC_LOG
		x386_dynarec_log("abrt6\n");
#endif
		return 1;
	}
        SP-=6;

        cpu_state.eflags &= ~VIF_FLAG;
        cpu_state.flags &= ~T_FLAG;
        cpu_state.pc = new_pc;
        loadcs(new_cs);
		oxpc=cpu_state.pc;

        cycles -= timing_int_rm;
        trap = 0;
        CPU_BLOCK_END();
        
        return 0;
}

void x86illegal()
{
        x86_int(6);
}

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


int checkio(int port)
{
        uint16_t t;
        uint8_t d;
        cpl_override = 1;
        t = readmemw(tr.base, 0x66);
        cpl_override = 0;
        if (cpu_state.abrt) return 0;
        if ((t+(port>>3))>tr.limit) return 1;
        cpl_override = 1;
        d = readmemb386l(0, tr.base + t + (port >> 3));
        cpl_override = 0;
        return d&(1<<(port&7));
}

int xout=0;


#define divexcp() { \
                x86_int(0); \
}

int divl(uint32_t val)
{
         uint64_t num, quo;
         uint32_t rem, quo32;
 
        if (val==0) 
        {
                divexcp();
                return 1;
        }

         num=(((uint64_t)EDX)<<32)|EAX;
         quo=num/val;
         rem=num%val;
         quo32=(uint32_t)(quo&0xFFFFFFFF);

        if (quo!=(uint64_t)quo32) 
        {
                divexcp();
                return 1;
        }
        EDX=rem;
        EAX=quo32;
        return 0;
}
int idivl(int32_t val)
{
         int64_t num, quo;
         int32_t rem, quo32;
 
        if (val==0) 
        {       
                divexcp();
                return 1;
        }

         num=(((uint64_t)EDX)<<32)|EAX;
         quo=num/val;
         rem=num%val;
         quo32=(int32_t)(quo&0xFFFFFFFF);

        if (quo!=(int64_t)quo32) 
        {
                divexcp();
                return 1;
        }
        EDX=rem;
        EAX=quo32;
        return 0;
}


void cpu_386_flags_extract()
{
        flags_extract();
}
void cpu_386_flags_rebuild()
{
        flags_rebuild();
}

int oldi;

uint32_t testr[9];
int dontprint=0;

void enter_smm()
{
        uint32_t smram_state = smbase + 0xfe00;
        uint32_t old_cr0 = cr0;
        uint32_t old_flags = cpu_state.flags | ((uint32_t)cpu_state.eflags << 16);

        cr0 &= ~0x8000000d;
        cpu_state.flags = 2;
        cpu_state.eflags = 0;

        in_smm = 1;
        smi_latched = 1;

        mem_writel_phys(smram_state + 0xf8, smbase);
        mem_writel_phys(smram_state + 0x128, cr4);
        mem_writel_phys(smram_state + 0x130, cpu_state.seg_es.limit);
        mem_writel_phys(smram_state + 0x134, cpu_state.seg_es.base);
        mem_writel_phys(smram_state + 0x138, cpu_state.seg_es.access);
        mem_writel_phys(smram_state + 0x13c, cpu_state.seg_cs.limit);
        mem_writel_phys(smram_state + 0x140, cpu_state.seg_cs.base);
        mem_writel_phys(smram_state + 0x144, cpu_state.seg_cs.access);
        mem_writel_phys(smram_state + 0x148, cpu_state.seg_ss.limit);
        mem_writel_phys(smram_state + 0x14c, cpu_state.seg_ss.base);
        mem_writel_phys(smram_state + 0x150, cpu_state.seg_ss.access);
        mem_writel_phys(smram_state + 0x154, cpu_state.seg_ds.limit);
        mem_writel_phys(smram_state + 0x158, cpu_state.seg_ds.base);
        mem_writel_phys(smram_state + 0x15c, cpu_state.seg_ds.access);
        mem_writel_phys(smram_state + 0x160, cpu_state.seg_fs.limit);
        mem_writel_phys(smram_state + 0x164, cpu_state.seg_fs.base);
        mem_writel_phys(smram_state + 0x168, cpu_state.seg_fs.access);
        mem_writel_phys(smram_state + 0x16c, cpu_state.seg_gs.limit);
        mem_writel_phys(smram_state + 0x170, cpu_state.seg_gs.base);
        mem_writel_phys(smram_state + 0x174, cpu_state.seg_gs.access);
        mem_writel_phys(smram_state + 0x178, ldt.limit);
        mem_writel_phys(smram_state + 0x17c, ldt.base);
        mem_writel_phys(smram_state + 0x180, ldt.access);
        mem_writel_phys(smram_state + 0x184, gdt.limit);
        mem_writel_phys(smram_state + 0x188, gdt.base);
        mem_writel_phys(smram_state + 0x18c, gdt.access);
        mem_writel_phys(smram_state + 0x190, idt.limit);
        mem_writel_phys(smram_state + 0x194, idt.base);
        mem_writel_phys(smram_state + 0x198, idt.access);
        mem_writel_phys(smram_state + 0x19c, tr.limit);
        mem_writel_phys(smram_state + 0x1a0, tr.base);
        mem_writel_phys(smram_state + 0x1a4, tr.access);

        mem_writel_phys(smram_state + 0x1a8, cpu_state.seg_es.seg);
        mem_writel_phys(smram_state + 0x1ac, cpu_state.seg_cs.seg);
        mem_writel_phys(smram_state + 0x1b0, cpu_state.seg_ss.seg);
        mem_writel_phys(smram_state + 0x1b4, cpu_state.seg_ds.seg);
        mem_writel_phys(smram_state + 0x1b8, cpu_state.seg_fs.seg);
        mem_writel_phys(smram_state + 0x1bc, cpu_state.seg_gs.seg);
        mem_writel_phys(smram_state + 0x1c0, ldt.seg);
        mem_writel_phys(smram_state + 0x1c4, tr.seg);

        mem_writel_phys(smram_state + 0x1c8, dr[7]);
        mem_writel_phys(smram_state + 0x1cc, dr[6]);
        mem_writel_phys(smram_state + 0x1d0, EAX);
        mem_writel_phys(smram_state + 0x1d4, ECX);
        mem_writel_phys(smram_state + 0x1d8, EDX);
        mem_writel_phys(smram_state + 0x1dc, EBX);
        mem_writel_phys(smram_state + 0x1e0, ESP);
        mem_writel_phys(smram_state + 0x1e4, EBP);
        mem_writel_phys(smram_state + 0x1e8, ESI);
        mem_writel_phys(smram_state + 0x1ec, EDI);
        mem_writel_phys(smram_state + 0x1f0, cpu_state.pc);
        mem_writel_phys(smram_state + 0x1d0, old_flags);
        mem_writel_phys(smram_state + 0x1f8, cr3);
        mem_writel_phys(smram_state + 0x1fc, old_cr0);

        ds = es = fs_seg = gs = ss = 0;

        DS = ES = FS = GS = SS = 0;

        cpu_state.seg_ds.limit = cpu_state.seg_es.limit = cpu_state.seg_fs.limit = cpu_state.seg_gs.limit
        = cpu_state.seg_ss.limit = 0xffffffff;

        cpu_state.seg_ds.limit_high = cpu_state.seg_es.limit_high = cpu_state.seg_fs.limit_high
        = cpu_state.seg_gs.limit_high = cpu_state.seg_ss.limit_high = 0xffffffff;

        cpu_state.seg_ds.limit_low = cpu_state.seg_es.limit_low = cpu_state.seg_fs.limit_low
        = cpu_state.seg_gs.limit_low = cpu_state.seg_ss.limit_low = 0;

        cpu_state.seg_ds.access = cpu_state.seg_es.access = cpu_state.seg_fs.access
        = cpu_state.seg_gs.access = cpu_state.seg_ss.access = 0x93;

        cpu_state.seg_ds.checked = cpu_state.seg_es.checked = cpu_state.seg_fs.checked
        = cpu_state.seg_gs.checked = cpu_state.seg_ss.checked = 1;

        CS = 0x3000;
        cs = smbase;
        cpu_state.seg_cs.limit = cpu_state.seg_cs.limit_high = 0xffffffff;
        cpu_state.seg_cs.limit_low = 0;
        cpu_state.seg_cs.access = 0x93;
        cpu_state.seg_cs.checked = 1;

        cr4 = 0;
        dr[7] = 0x400;
        cpu_state.pc = 0x8000;

        nmi_mask = 0;
}

void leave_smm()
{
        uint32_t smram_state = smbase + 0xfe00;

        smbase = mem_readl_phys(smram_state + 0xf8);
        cr4 = mem_readl_phys(smram_state + 0x128);

        cpu_state.seg_es.limit = cpu_state.seg_es.limit_high = mem_readl_phys(smram_state + 0x130);
        cpu_state.seg_es.base = mem_readl_phys(smram_state + 0x134);
        cpu_state.seg_es.limit_low = cpu_state.seg_es.base;
        cpu_state.seg_es.access = mem_readl_phys(smram_state + 0x138);

        cpu_state.seg_cs.limit = cpu_state.seg_cs.limit_high = mem_readl_phys(smram_state + 0x13c);
        cpu_state.seg_cs.base = mem_readl_phys(smram_state + 0x140);
        cpu_state.seg_cs.limit_low = cpu_state.seg_cs.base;
        cpu_state.seg_cs.access = mem_readl_phys(smram_state + 0x144);

        cpu_state.seg_ss.limit = cpu_state.seg_ss.limit_high = mem_readl_phys(smram_state + 0x148);
        cpu_state.seg_ss.base = mem_readl_phys(smram_state + 0x14c);
        cpu_state.seg_ss.limit_low = cpu_state.seg_ss.base;     
        cpu_state.seg_ss.access = mem_readl_phys(smram_state + 0x150);

        cpu_state.seg_ds.limit = cpu_state.seg_ds.limit_high = mem_readl_phys(smram_state + 0x154);
        cpu_state.seg_ds.base = mem_readl_phys(smram_state + 0x158);
        cpu_state.seg_ds.limit_low = cpu_state.seg_ds.base;
        cpu_state.seg_ds.access = mem_readl_phys(smram_state + 0x15c);

        cpu_state.seg_fs.limit = cpu_state.seg_fs.limit_high = mem_readl_phys(smram_state + 0x160);
        cpu_state.seg_fs.base = mem_readl_phys(smram_state + 0x164);
        cpu_state.seg_fs.limit_low = cpu_state.seg_fs.base;
        cpu_state.seg_fs.access = mem_readl_phys(smram_state + 0x168);

        cpu_state.seg_gs.limit = cpu_state.seg_gs.limit_high = mem_readl_phys(smram_state + 0x16c);
        cpu_state.seg_gs.base = mem_readl_phys(smram_state + 0x170);
        cpu_state.seg_gs.limit_low = cpu_state.seg_gs.base;
        cpu_state.seg_gs.access = mem_readl_phys(smram_state + 0x174);

        ldt.limit = ldt.limit_high = mem_readl_phys(smram_state + 0x178);
        ldt.base = mem_readl_phys(smram_state + 0x17c);
        ldt.limit_low = ldt.base;
        ldt.access = mem_readl_phys(smram_state + 0x180);

        gdt.limit = gdt.limit_high = mem_readl_phys(smram_state + 0x184);
        gdt.base = mem_readl_phys(smram_state + 0x188);
        gdt.limit_low = gdt.base;
        gdt.access = mem_readl_phys(smram_state + 0x18c);

        idt.limit = idt.limit_high = mem_readl_phys(smram_state + 0x190);
        idt.base = mem_readl_phys(smram_state + 0x194);
        idt.limit_low = idt.base;
        idt.access = mem_readl_phys(smram_state + 0x198);

        tr.limit = tr.limit_high = mem_readl_phys(smram_state + 0x19c);
        tr.base = mem_readl_phys(smram_state + 0x1a0);
        tr.limit_low = tr.base;
        tr.access = mem_readl_phys(smram_state + 0x1a4);

        ES = mem_readl_phys(smram_state + 0x1a8);
        CS = mem_readl_phys(smram_state + 0x1ac);
        SS = mem_readl_phys(smram_state + 0x1b0);
        DS = mem_readl_phys(smram_state + 0x1b4);
        FS = mem_readl_phys(smram_state + 0x1b8);
        GS = mem_readl_phys(smram_state + 0x1bc);
        ldt.seg = mem_readl_phys(smram_state + 0x1c0);
        tr.seg = mem_readl_phys(smram_state + 0x1c4);

        dr[7] = mem_readl_phys(smram_state + 0x1c8);
        dr[6] = mem_readl_phys(smram_state + 0x1cc);
        EAX = mem_readl_phys(smram_state + 0x1d0);
        ECX = mem_readl_phys(smram_state + 0x1d4);
        EDX = mem_readl_phys(smram_state + 0x1d8);
        EBX = mem_readl_phys(smram_state + 0x1dc);
        ESP = mem_readl_phys(smram_state + 0x1e0);
        EBP = mem_readl_phys(smram_state + 0x1e4);
        ESI = mem_readl_phys(smram_state + 0x1e8);
        EDI = mem_readl_phys(smram_state + 0x1ec);

        cpu_state.pc = mem_readl_phys(smram_state + 0x1f0);
        uint32_t new_flags = mem_readl_phys(smram_state + 0x1f4);
        cpu_state.flags = new_flags & 0xffff;
        cpu_state.eflags = new_flags >> 16;
        cr3 = mem_readl_phys(smram_state + 0x1f8);
        cr0 = mem_readl_phys(smram_state + 0x1fc);

        cpu_state.seg_cs.access &= ~0x60;
        cpu_state.seg_cs.access |= cpu_state.seg_ss.access & 0x60; //cpl is dpl of ss

        if((cr0 & 1) && !(cpu_state.eflags&VM_FLAG))
        {
                cpu_state.seg_cs.checked = CS ? 1 : 0;
                cpu_state.seg_ds.checked = DS ? 1 : 0;
                cpu_state.seg_es.checked = ES ? 1 : 0;
                cpu_state.seg_fs.checked = FS ? 1 : 0;
                cpu_state.seg_gs.checked = GS ? 1 : 0;
                cpu_state.seg_ss.checked = SS ? 1 : 0;
        }
        else
        {
                cpu_state.seg_cs.checked = cpu_state.seg_ds.checked = cpu_state.seg_es.checked
                = cpu_state.seg_fs.checked = cpu_state.seg_gs.checked = cpu_state.seg_ss.checked = 1;
        }

        in_smm = 0;

        nmi_mask = 1;
}

#define OP_TABLE(name) ops_ ## name
#define CLOCK_CYCLES(c) cycles -= (c)
#define CLOCK_CYCLES_ALWAYS(c) cycles -= (c)

#include "386_ops.h"


#define CACHE_ON() (!(cr0 & (1 << 30)) /*&& (cr0 & 1)*/ && !(cpu_state.flags & T_FLAG))

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
                oldcs = CS;
                cpu_state.oldpc = cpu_state.pc;
                oldcpl = CPL;
                cpu_state.op32 = use32;


                cycdiff=0;
                oldcyc=cycles;
                if (!CACHE_ON()) /*Interpret block*/
                {
                        cpu_block_end = 0;
			x86_was_reset = 0;
                        while (!cpu_block_end)
                        {
                                oldcs=CS;
                                cpu_state.oldpc = cpu_state.pc;
								oldcpl = CPL;
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

                                if (!use32) cpu_state.pc &= 0xffff;

                                if (((cs + cpu_state.pc) >> 12) != pccache)
                                        CPU_BLOCK_END();

/*                                if (ssegs)
                                {
                                        ds=oldds;
                                        ss=oldss;
                                        ssegs=0;
                                }*/

                                if (in_smm && smi_line && is_pentium)
                                        CPU_BLOCK_END();

                                if (cpu_state.abrt)
                                        CPU_BLOCK_END();
                                if (trap)
                                        CPU_BLOCK_END();

                                if (nmi && nmi_enable && nmi_mask)
                                        CPU_BLOCK_END();

                                ins++;
                                
/*                                if ((cs + pc) == 4)
                                        fatal("4\n");*/
/*                                if (ins >= 141400000)
                                        output = 3;*/
                        }
                }
                else
                {
                uint32_t phys_addr = get_phys(cs+cpu_state.pc);
                int hash = HASH(phys_addr);
                codeblock_t *block = codeblock_hash[hash];
                int valid_block = 0;
				trap = 0;

                if (block && !cpu_state.abrt)
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
                                
                                if (page->code_present_mask[(phys_addr >> PAGE_MASK_INDEX_SHIFT) & PAGE_MASK_INDEX_MASK] & mask)
                                {
                                        /*Walk page tree to see if we find the correct block*/
                                        codeblock_t *new_block = codeblock_tree_find(phys_addr, cs);
                                        if (new_block)
                                        {
                                                valid_block = (new_block->pc == cs + cpu_state.pc) && (new_block->_cs == cs) &&
                                                                (new_block->phys == phys_addr) && !((new_block->status ^ cpu_cur_status) & CPU_STATUS_FLAGS) &&
                                                                ((new_block->status & cpu_cur_status & CPU_STATUS_MASK) == (cpu_cur_status & CPU_STATUS_MASK));
                                                if (valid_block)
                                                        block = new_block;
                                        }
                                }
                        }

                        if (valid_block && (block->page_mask & *block->dirty_mask))
                        {
                                codegen_check_flush(page, page->dirty_mask[(phys_addr >> 10) & 3], phys_addr);
                                page->dirty_mask[(phys_addr >> 10) & 3] = 0;
                                if (!block->valid)
                                        valid_block = 0;
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
                                uint32_t phys_addr_2 = get_phys_noabrt(block->endpc);
                                page_t *page_2 = &pages[phys_addr_2 >> 12];

                                if ((block->phys_2 ^ phys_addr_2) & ~0xfff)
                                        valid_block = 0;
                                else if (block->page_mask2 & *block->dirty_mask2)
                                {
                                        codegen_check_flush(page_2, page_2->dirty_mask[(phys_addr_2 >> 10) & 3], phys_addr_2);
                                        page_2->dirty_mask[(phys_addr_2 >> 10) & 3] = 0;
                                        if (!block->valid)
                                                valid_block = 0;
                                }
                        }
                        if (valid_block && block->was_recompiled && (block->flags & CODEBLOCK_STATIC_TOP) && block->TOP != cpu_state.TOP)
                        {
                                /*FPU top-of-stack does not match the value this block was compiled
                                  with, re-compile using dynamic top-of-stack*/
                                block->flags &= ~CODEBLOCK_STATIC_TOP;
                                block->was_recompiled = 0;
                        }
                }

                if (valid_block && block->was_recompiled)
                {
                        void (*code)() = (void *)&block->data[BLOCK_START];

                        codeblock_hash[hash] = block;

inrecomp=1;
                        code();
inrecomp=0;
                        if (!use32) cpu_state.pc &= 0xffff;
                        cpu_recomp_blocks++;
                }
                else if (valid_block && !cpu_state.abrt)
                {
                        start_pc = cpu_state.pc;
                        
                        cpu_block_end = 0;
                        x86_was_reset = 0;

                        cpu_new_blocks++;
                        
                        codegen_block_start_recompile(block);
                        codegen_in_recompile = 1;

                        while (!cpu_block_end)
                        {
                                oldcs=CS;
                                cpu_state.oldpc = cpu_state.pc;
								oldcpl = CPL;
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

                                if (!use32) cpu_state.pc &= 0xffff;

                                /*Cap source code at 4000 bytes per block; this
                                  will prevent any block from spanning more than
                                  2 pages. In practice this limit will never be
                                  hit, as host block size is only 2kB*/
                                if ((cpu_state.pc - start_pc) > 1000)
                                        CPU_BLOCK_END();
                                
                                if (in_smm && smi_line && is_pentium)
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
                        start_pc = cpu_state.pc;

                        cpu_block_end = 0;
                        x86_was_reset = 0;

                        codegen_block_init(phys_addr);

                        while (!cpu_block_end)
                        {
                                oldcs=CS;
                                cpu_state.oldpc = cpu_state.pc;
								oldcpl = CPL;
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

                                if (!use32) cpu_state.pc &= 0xffff;

                                /*Cap source code at 4000 bytes per block; this
                                  will prevent any block from spanning more than
                                  2 pages. In practice this limit will never be
                                  hit, as host block size is only 2kB*/
                                if ((cpu_state.pc - start_pc) > 1000)
                                        CPU_BLOCK_END();

                                if (in_smm && smi_line && is_pentium)
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
                                CS = oldcs;
                                cpu_state.pc = cpu_state.oldpc;
#ifdef ENABLE_386_DYNAREC_LOG
                                x386_dynarec_log("Double fault %i\n", ins);
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
                
                if (in_smm && smi_line && is_pentium)
                {
                        enter_smm();
                }

                if (trap)
                {
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
                                cpu_state.flags&=~I_FLAG;
                                cpu_state.flags&=~T_FLAG;
                                cpu_state.pc=readmemw(0,addr);
                                loadcs(readmemw(0,addr+2));
                        }
                }
                else if (nmi && nmi_enable && nmi_mask)
                {
                        cpu_state.oldpc = cpu_state.pc;
                        oldcs = CS;
                        x86_int(2);
                        nmi_enable = 0;
                        if (nmi_auto_clear)
                        {
                                nmi_auto_clear = 0;
                                nmi = 0;
                        }
                }
                else if ((cpu_state.flags&I_FLAG) && pic_intpending)
                {
                        vector=picinterrupt();
                        if (vector!=-1)
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
                                        cpu_state.flags&=~I_FLAG;
                                        cpu_state.flags&=~T_FLAG;
										oxpc=cpu_state.pc;
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
