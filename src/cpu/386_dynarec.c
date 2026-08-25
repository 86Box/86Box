#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#if defined(__APPLE__) && defined(__aarch64__)
#    include <pthread.h>
#endif
#include <wchar.h>
#include <math.h>
#ifndef INFINITY
#    define INFINITY (__builtin_inff())
#endif

#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include "x86_ops.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "x87_sf.h"
#include "x87.h"
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/machine.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>
#include <86box/gdbstub.h>
#ifdef USE_DYNAREC
#    include "codegen.h"
#    ifdef USE_NEW_DYNAREC
#        include "codegen_backend.h"
#    endif
#endif

#ifdef IS_DYNAREC
#    undef IS_DYNAREC
#endif

#include "386_common.h"

#if defined(__APPLE__) && defined(__aarch64__)
#    include <pthread.h>
#endif

#define CPU_BLOCK_END() cpu_block_end = 1

int cpu_force_interpreter   = 0;
int cpu_override_dynarec    = 0;
int inrecomp                = 0;
int cpu_block_end           = 0;
int cpu_end_block_after_ins = 0;

#if defined(__aarch64__) || defined(_M_ARM64)
/* ARM64-only epoch: monotonically advances on dirty-list transitions so
   per-block retry state can distinguish dense bursts from stale retries. */
static uint32_t dynarec_s03e_dirty_epoch             = 0;
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
/* ARM64-only policy: require repeated BYTE_MASK dirty-list hits before
   NO_IMMEDIATES promotion to avoid premature slow-immediate escalation. */
/* Tuning: raise threshold to 3 consecutive dirty-list retries so transient
   churn is more likely to recover via retry-decay before forcing NO_IMMEDIATES. */
#    define DYNAREC_S03B_NO_IMM_THRESHOLD 3
/* Tuning: retry bursts must stay temporally dense; large gaps reset burst
   accumulation instead of carrying stale debt into later promotions. */
#    define DYNAREC_S03E_BURST_GAP_MAX 64
#endif

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
#    define x386_dynarec_log(fmt, ...)
#endif

static __inline void
fetch_ea_32_long(uint32_t rmdat)
{
    eal_r = eal_w = NULL;
    easeg         = cpu_state.ea_seg->base;
    if (cpu_rm == 4) {
        uint8_t sib = rmdat >> 8;

        switch (cpu_mod) {
            case 0:
                cpu_state.eaaddr = cpu_state.regs[sib & 7].l;
                cpu_state.pc++;
                break;
            case 1:
                cpu_state.pc++;
                cpu_state.eaaddr = ((uint32_t) (int8_t) getbyte()) + cpu_state.regs[sib & 7].l;
                break;
            case 2:
                cpu_state.eaaddr = (fastreadl(cs + cpu_state.pc + 1)) + cpu_state.regs[sib & 7].l;
                cpu_state.pc += 5;
                break;
        }
        /*SIB byte present*/
        if ((sib & 7) == 5 && !cpu_mod)
            cpu_state.eaaddr = getlong();
        else if ((sib & 6) == 4 && !cpu_state.ssegs) {
            easeg            = ss;
            cpu_state.ea_seg = &cpu_state.seg_ss;
        }
        if (((sib >> 3) & 7) != 4)
            cpu_state.eaaddr += cpu_state.regs[(sib >> 3) & 7].l << (sib >> 6);
    } else {
        cpu_state.eaaddr = cpu_state.regs[cpu_rm].l;
        if (cpu_mod) {
            if (cpu_rm == 5 && !cpu_state.ssegs) {
                easeg            = ss;
                cpu_state.ea_seg = &cpu_state.seg_ss;
            }
            if (cpu_mod == 1) {
                cpu_state.eaaddr += ((uint32_t) (int8_t) (rmdat >> 8));
                cpu_state.pc++;
            } else {
                cpu_state.eaaddr += getlong();
            }
        } else if (cpu_rm == 5) {
            cpu_state.eaaddr = getlong();
        }
    }
    if (easeg != 0xFFFFFFFF && ((easeg + cpu_state.eaaddr) & 0xFFF) <= 0xFFC) {
        uint32_t addr = easeg + cpu_state.eaaddr;
        if (readlookup2[addr >> 12] != (uintptr_t) -1)
            eal_r = (uint32_t *) (readlookup2[addr >> 12] + addr);
        if (writelookup2[addr >> 12] != (uintptr_t) -1)
            eal_w = (uint32_t *) (writelookup2[addr >> 12] + addr);
    }
}

static __inline void
fetch_ea_16_long(uint32_t rmdat)
{
    eal_r = eal_w = NULL;
    easeg         = cpu_state.ea_seg->base;
    if (!cpu_mod && cpu_rm == 6) {
        cpu_state.eaaddr = getword();
    } else {
        switch (cpu_mod) {
            case 0:
                cpu_state.eaaddr = 0;
                break;
            case 1:
                cpu_state.eaaddr = (uint16_t) (int8_t) (rmdat >> 8);
                cpu_state.pc++;
                break;
            case 2:
                cpu_state.eaaddr = getword();
                break;
        }
        cpu_state.eaaddr += (*mod1add[0][cpu_rm]) + (*mod1add[1][cpu_rm]);
        if (mod1seg[cpu_rm] == &ss && !cpu_state.ssegs) {
            easeg            = ss;
            cpu_state.ea_seg = &cpu_state.seg_ss;
        }
        cpu_state.eaaddr &= 0xFFFF;
    }
    if (easeg != 0xFFFFFFFF && ((easeg + cpu_state.eaaddr) & 0xFFF) <= 0xFFC) {
        uint32_t addr = easeg + cpu_state.eaaddr;
        if (readlookup2[addr >> 12] != (uintptr_t) -1)
            eal_r = (uint32_t *) (readlookup2[addr >> 12] + addr);
        if (writelookup2[addr >> 12] != (uintptr_t) -1)
            eal_w = (uint32_t *) (writelookup2[addr >> 12] + addr);
    }
}

#define fetch_ea_16(rmdat)       \
    cpu_state.pc++;              \
    cpu_mod = (rmdat >> 6) & 3;  \
    cpu_reg = (rmdat >> 3) & 7;  \
    cpu_rm  = rmdat & 7;         \
    if (cpu_mod != 3) {          \
        fetch_ea_16_long(rmdat); \
        if (cpu_state.abrt)      \
            return 1;            \
    }
#define fetch_ea_32(rmdat)       \
    cpu_state.pc++;              \
    cpu_mod = (rmdat >> 6) & 3;  \
    cpu_reg = (rmdat >> 3) & 7;  \
    cpu_rm  = rmdat & 7;         \
    if (cpu_mod != 3) {          \
        fetch_ea_32_long(rmdat); \
    }                            \
    if (cpu_state.abrt)          \
    return 1

#include "x86_flags.h"

#define PREFETCH_RUN(instr_cycles, bytes, modrm, reads, reads_l, writes, writes_l, ea32)      \
    do {                                                                                      \
        if (cpu_prefetch_cycles)                                                              \
            prefetch_run(instr_cycles, bytes, modrm, reads, reads_l, writes, writes_l, ea32); \
    } while (0)

#define PREFETCH_PREFIX()        \
    do {                         \
        if (cpu_prefetch_cycles) \
            prefetch_prefixes++; \
    } while (0)
#define PREFETCH_FLUSH() prefetch_flush()

#define OP_TABLE(name)   ops_##name
#if 0
#    define CLOCK_CYCLES(c)               \
        {                                 \
            if (fpu_cycles > 0) {         \
                fpu_cycles -= (c);        \
                if (fpu_cycles < 0) {     \
                    cycles += fpu_cycles; \
                }                         \
            } else {                      \
                cycles -= (c);            \
            }                             \
        }
#    define CLOCK_CYCLES_FPU(c)   cycles -= (c)
#    define CONCURRENCY_CYCLES(c) fpu_cycles = (c)
#else
#    define CLOCK_CYCLES(c)     cycles -= (c)
#    define CLOCK_CYCLES_FPU(c) cycles -= (c)
#    define CONCURRENCY_CYCLES(c)
#endif
#define CLOCK_CYCLES_ALWAYS(c) cycles -= (c)

#include "386_ops.h"

#ifdef USE_DEBUG_REGS_486
#    define CACHE_ON() (!(cr0 & (1 << 30)) && !(cpu_state.flags & T_FLAG) && !(dr[7] & 0xFF))
#else
#    define CACHE_ON() (!(cr0 & (1 << 30)) && !(cpu_state.flags & T_FLAG))
#endif

#ifdef USE_DYNAREC
int32_t         cycles_main = 0;
static int32_t  cycles_old  = 0;
static uint64_t tsc_old     = 0;

#    ifdef USE_ACYCS
int32_t acycs = 0;
#    endif

int
codegen_mmx_enter(void)
{
    MMX_ENTER();
    return 0;
}

int
codegen_femms(void)
{
    if (!cpu_has_feature(CPU_FEATURE_MMX)) {
        x86illegal();
        return 1;
    }
    if (cr0 & 0xc) {
        x86_int(7);
        return 1;
    }

    x87_emms();
    return 0;
}

int
codegen_fp_enter(void)
{
    FP_ENTER();
    return 0;
}

void
update_tsc(void)
{
    int      cycdiff;
    uint64_t delta;

    cycdiff = cycles_old - cycles;
#    ifdef USE_ACYCS
    if (inrecomp)
        cycdiff += acycs;
#    endif

    delta = tsc - tsc_old;
    if (delta > 0) {
        /* TSC has changed, this means interim timer processing has happened,
           see how much we still need to add. */
        cycdiff -= delta;
    }

    if (cycdiff > 0)
        tsc += cycdiff;

    if (cycdiff > 0) {
        if (TIMER_VAL_LESS_THAN_VAL(timer_target, (uint64_t) tsc))
            timer_process();
    }
}

static __inline void
exec386_dynarec_int(void)
{
    cpu_block_end = 0;
    x86_was_reset = 0;

    if (trap == 2) {
        /* Handle the T bit in the new TSS first. */
        CPU_BLOCK_END();
        goto block_ended;
    }

    while (!cpu_block_end) {
        oldcs  = CS;
        oldcpl = CPL;
        cpu_state.oldpc = cpu_state.pc;
        cpu_state.op32  = use32;

        cpu_state.ea_seg = &cpu_state.seg_ds;
        cpu_state.ssegs  = 0;

        fetchdat = fastreadl_fetch(cs + cpu_state.pc);
#    ifdef ENABLE_386_DYNAREC_LOG
        if (in_smm)
            x386_dynarec_log("[%04X:%08X] fetchdat = %08X\n", CS, cpu_state.pc, fetchdat);
#    endif

        if (!cpu_state.abrt) {
            /* Temp variables for FPU exception reporting. */
            cpu_state.temp_CS = CS;
            cpu_state.temp_cs = cs;
            cpu_state.temp_pc = cpu_state.pc;

            opcode = fetchdat & 0xFF;
            fetchdat >>= 8;

#    ifdef USE_DEBUG_REGS_486
            trap = (trap & ~1) | (!!(cpu_state.flags & T_FLAG));
#    else
            trap = cpu_state.flags & T_FLAG;
#    endif

            cpu_state.pc++;
#    ifdef USE_DEBUG_REGS_486
            cpu_state.eflags &= ~(RF_FLAG);
#    endif
            x86_opcodes[(opcode | cpu_state.op32) & 0x3ff](fetchdat);
        }

#    ifndef USE_NEW_DYNAREC
        if (!use32)
            cpu_state.pc &= 0xffff;
#    endif

#    ifdef USE_DEBUG_REGS_486
        if (!cpu_state.abrt) {
            if (!rf_flag_no_clear) {
                cpu_state.eflags &= ~RF_FLAG;
            }

            rf_flag_no_clear = 0;
        }
#    endif

        if (((cs + cpu_state.pc) >> 12) != pccache)
            CPU_BLOCK_END();

        if (cpu_end_block_after_ins) {
            cpu_end_block_after_ins--;
            if (!cpu_end_block_after_ins)
                CPU_BLOCK_END();
        }

        if (cpu_init)
            CPU_BLOCK_END();

        if (cpu_state.abrt)
            CPU_BLOCK_END();
        if (smi_line)
            CPU_BLOCK_END();
        else if (new_ne)
            CPU_BLOCK_END();
        else if (trap)
            CPU_BLOCK_END();
        else if (nmi && nmi_enable && nmi_mask)
            CPU_BLOCK_END();
        else if ((cpu_state.flags & I_FLAG) && pic.int_pending && !cpu_end_block_after_ins)
            CPU_BLOCK_END();
    }

block_ended:
    if (!cpu_state.abrt && !new_ne && trap) {
        if (trap & 2) dr[6] |= 0x8000;
        if (trap & 1) dr[6] |= 0x4000;
        if (trap & 16) dr[6] |= 0x2000;

        trap = 0;
        oldcs = CS;
        cpu_state.oldpc = cpu_state.pc;
        x86_int(1);
    }

    cpu_end_block_after_ins = 0;
}

#if defined(__linux__) && !defined(__clang__) && defined(USE_NEW_DYNAREC)
static inline void __attribute__((optimize("O2")))
#else
static __inline void
#endif
exec386_dynarec_dyn(void)
{
    uint32_t start_pc  = 0;
    uint32_t phys_addr = get_phys(cs + cpu_state.pc);
    int      hash      = HASH(phys_addr);
#    ifdef USE_NEW_DYNAREC
    codeblock_t *block = &codeblock[codeblock_hash[hash]];
#    else
    codeblock_t *block = codeblock_hash[hash];
#    endif
    int valid_block = 0;

    /* Refresh before the lookup AND before a fresh compile: the old
       dynarec skips the lookup on an empty hash slot, and the new block
       still takes its key from cpu_cur_status. */
    if (cpu_state.npxc & 0x300)
        cpu_cur_status &= ~CPU_STATUS_FPU_PC24;
    else
        cpu_cur_status |= CPU_STATUS_FPU_PC24;

#    ifdef USE_NEW_DYNAREC
    if (!cpu_state.abrt)
#    else
    if (block && !cpu_state.abrt)
#    endif
    {
        page_t *page = &pages[phys_addr >> 12];

        /* Block must match current CS, PC, code segment size,
           and physical address. The physical address check will
           also catch any page faults at this stage */
        valid_block = (block->pc == cs + cpu_state.pc) && (block->_cs == cs) && (block->phys == phys_addr) && !((block->status ^ cpu_cur_status) & CPU_STATUS_FLAGS) && ((block->status & cpu_cur_status & CPU_STATUS_MASK) == (cpu_cur_status & CPU_STATUS_MASK));
        if (!valid_block) {
            uint64_t mask = (uint64_t) 1 << ((phys_addr >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);
#    ifdef USE_NEW_DYNAREC
            int      byte_offset = (phys_addr >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK;
            uint64_t byte_mask   = 1ULL << (phys_addr & PAGE_BYTE_MASK_MASK);

            if ((page->code_present_mask & mask) ||
                ((page->mem != page_ff) && (page->byte_code_present_mask[byte_offset] & byte_mask)))
#    else
            if (page->code_present_mask[(phys_addr >> PAGE_MASK_INDEX_SHIFT) & PAGE_MASK_INDEX_MASK] & mask)
#    endif
            {
                /* Walk page tree to see if we find the correct block */
                codeblock_t *new_block = codeblock_tree_find(phys_addr, cs);
                if (new_block) {
                    valid_block = (new_block->pc == cs + cpu_state.pc) && (new_block->_cs == cs) && (new_block->phys == phys_addr) && !((new_block->status ^ cpu_cur_status) & CPU_STATUS_FLAGS) && ((new_block->status & cpu_cur_status & CPU_STATUS_MASK) == (cpu_cur_status & CPU_STATUS_MASK));
                    if (valid_block) {
                        block = new_block;
#    ifdef USE_NEW_DYNAREC
                        codeblock_hash[hash] = get_block_nr(block);
#    endif
                    }
                }
            }
        }

        if (valid_block && (block->page_mask & *block->dirty_mask)) {
#    ifdef USE_NEW_DYNAREC
            codegen_check_flush(page, page->dirty_mask, phys_addr);
            if (block->valid && (block->flags & CODEBLOCK_IN_DIRTY_LIST))
                block->flags &= ~CODEBLOCK_WAS_RECOMPILED;
            else
#    else
            codegen_check_flush(page, page->dirty_mask[(phys_addr >> 10) & 3], phys_addr);
            page->dirty_mask[(phys_addr >> 10) & 3] = 0;
#    endif
            if (!block->valid)
                valid_block = 0;
        }
        if (valid_block && block->page_mask2) {
            /* We don't want the second page to cause a page
               fault at this stage - that would break any
               code crossing a page boundary where the first
               page is present but the second isn't. Instead
               allow the first page to be interpreted and for
               the page fault to occur when the page boundary
               is actually crossed.*/
#    ifdef USE_NEW_DYNAREC
            uint32_t phys_addr_2 = get_phys_noabrt(block->pc + ((block->flags & CODEBLOCK_BYTE_MASK) ? 0x40 : 0x400));
#    else
            uint32_t phys_addr_2 = get_phys_noabrt(block->endpc);
#    endif
            page_t *page_2 = &pages[phys_addr_2 >> 12];

            if ((block->phys_2 ^ phys_addr_2) & ~0xfff)
                valid_block = 0;
            else if (block->page_mask2 & *block->dirty_mask2) {
#    ifdef USE_NEW_DYNAREC
                codegen_check_flush(page_2, page_2->dirty_mask, phys_addr_2);
                if (block->valid && (block->flags & CODEBLOCK_IN_DIRTY_LIST))
                    block->flags &= ~CODEBLOCK_WAS_RECOMPILED;
                else
#    else
                codegen_check_flush(page_2, page_2->dirty_mask[(phys_addr_2 >> 10) & 3], phys_addr_2);
                page_2->dirty_mask[(phys_addr_2 >> 10) & 3] = 0;
#    endif
                if (!block->valid)
                    valid_block = 0;
            }
        }
#    ifdef USE_NEW_DYNAREC
        /* ARM64-only: if a BYTE_MASK block executes stably outside the dirty
           list, clear stale retry debt so a distant future dirty hit does not
           trigger premature NO_IMMEDIATES promotion. */
#        if defined(__aarch64__) || defined(_M_ARM64)
        if (valid_block && !(block->flags & CODEBLOCK_IN_DIRTY_LIST) && (block->flags & CODEBLOCK_BYTE_MASK)
            && !(block->flags & CODEBLOCK_NO_IMMEDIATES) && block->dirty_list_recompile_hits) {
            block->dirty_list_recompile_hits = 0;
            block->dirty_list_last_epoch     = 0;
        }
#        endif

        if (valid_block && (block->flags & CODEBLOCK_IN_DIRTY_LIST)) {
            const int had_byte_mask     = !!(block->flags & CODEBLOCK_BYTE_MASK);
            const int had_no_immediates = !!(block->flags & CODEBLOCK_NO_IMMEDIATES);
#if defined(__aarch64__) || defined(_M_ARM64)
            const uint16_t last_epoch_before = block->dirty_list_last_epoch;
#endif
            block->flags &= ~CODEBLOCK_WAS_RECOMPILED;
            if (had_byte_mask) {
                if (!had_no_immediates) {
#if defined(__aarch64__) || defined(_M_ARM64)
                    /* ARM64-only: wait for repeated dirty-list BYTE_MASK
                       hits before NO_IMMEDIATES promotion. */
                    /* Require retries to occur in a dense burst window;
                       stale widely-spaced retries are reset. */
                    dynarec_s03e_dirty_epoch++;
                    {
                        const uint16_t cur_epoch = (uint16_t) dynarec_s03e_dirty_epoch;

                        if (last_epoch_before != 0) {
                            const uint16_t epoch_gap = (uint16_t) (cur_epoch - last_epoch_before);
                            if (epoch_gap > DYNAREC_S03E_BURST_GAP_MAX) {
                                block->dirty_list_recompile_hits = 0;
                            }
                        }
                        block->dirty_list_last_epoch = cur_epoch;
                    }
                    block->dirty_list_recompile_hits++;
                    if (block->dirty_list_recompile_hits >= DYNAREC_S03B_NO_IMM_THRESHOLD) {
                        block->flags |= CODEBLOCK_NO_IMMEDIATES;
                        block->dirty_list_last_epoch = 0;
                    }
#else
                    block->flags |= CODEBLOCK_NO_IMMEDIATES;
#endif
                }
            } else {
#if defined(__aarch64__) || defined(_M_ARM64)
                block->dirty_list_recompile_hits = 0;
                block->dirty_list_last_epoch     = 0;
#endif
                block->flags |= CODEBLOCK_BYTE_MASK;
            }
        }
        if (valid_block && (block->flags & CODEBLOCK_WAS_RECOMPILED) && (block->flags & CODEBLOCK_STATIC_TOP) && block->TOP != (cpu_state.TOP & 7))
#    else
        if (valid_block && block->was_recompiled && (block->flags & CODEBLOCK_STATIC_TOP) && block->TOP != cpu_state.TOP)
#    endif
        {
            /* FPU top-of-stack does not match the value this block was compiled
               with, re-compile using dynamic top-of-stack*/
#    ifdef USE_NEW_DYNAREC
            block->flags &= ~(CODEBLOCK_STATIC_TOP | CODEBLOCK_WAS_RECOMPILED);
#    else
            block->flags &= ~CODEBLOCK_STATIC_TOP;
            block->was_recompiled = 0;
#    endif
        }
    }

#    ifdef USE_NEW_DYNAREC
    if (valid_block && (block->flags & CODEBLOCK_WAS_RECOMPILED))
#    else
    if (valid_block && block->was_recompiled)
#    endif
    {
        void (*code)(void) = (void *) &block->data[BLOCK_START];

#    ifndef USE_NEW_DYNAREC
        codeblock_hash[hash] = block;
#    endif
        inrecomp = 1;
        code();
#    ifdef USE_ACYCS
        acycs = 0;
#    endif
        inrecomp = 0;

#    ifndef USE_NEW_DYNAREC
        if (!use32)
            cpu_state.pc &= 0xffff;
#    endif
    } else if (valid_block && !cpu_state.abrt) {
#    ifdef USE_NEW_DYNAREC
        start_pc                 = cs + cpu_state.pc;
        const int max_block_size = (block->flags & CODEBLOCK_BYTE_MASK) ? ((128 - 25) - (start_pc & 0x3f)) : 1000;
#    else
        start_pc = cpu_state.pc;
#    endif

        cpu_block_end = 0;
        x86_was_reset = 0;

#    if defined(__APPLE__) && defined(__aarch64__)
        if (__builtin_available(macOS 11.0, *)) {
            pthread_jit_write_protect_np(0);
        }
#    endif
        codegen_block_start_recompile(block);
        codegen_in_recompile = 1;

        while (!cpu_block_end) {
            oldcs  = CS;
            oldcpl = CPL;
            cpu_state.oldpc = cpu_state.pc;
            cpu_state.op32  = use32;

            cpu_state.ea_seg = &cpu_state.seg_ds;
            cpu_state.ssegs  = 0;

            fetchdat = fastreadl_fetch(cs + cpu_state.pc);
#    ifdef ENABLE_386_DYNAREC_LOG
            if (in_smm)
                x386_dynarec_log("[%04X:%08X] fetchdat = %08X\n", CS, cpu_state.pc, fetchdat);
#    endif

            if (!cpu_state.abrt) {
                /* Temp variables for FPU exception reporting. */
                cpu_state.temp_CS = CS;
                cpu_state.temp_cs = cs;
                cpu_state.temp_pc = cpu_state.pc;

                opcode = fetchdat & 0xFF;
                fetchdat >>= 8;

                cpu_state.pc++;

                codegen_generate_call(opcode, x86_opcodes[(opcode | cpu_state.op32) & 0x3ff], fetchdat, cpu_state.pc, cpu_state.pc - 1);

                x86_opcodes[(opcode | cpu_state.op32) & 0x3ff](fetchdat);

                if (x86_was_reset)
                    break;
            }

#    ifndef USE_NEW_DYNAREC
            if (!use32)
                cpu_state.pc &= 0xffff;
#    endif

                /* Cap source code at 4000 bytes per block; this
                   will prevent any block from spanning more than
                   2 pages. In practice this limit will never be
                   hit, as host block size is only 2kB*/
#    ifdef USE_NEW_DYNAREC
            if (((cs + cpu_state.pc) - start_pc) >= max_block_size)
#    else
            if ((cpu_state.pc - start_pc) > 1000)
#    endif
                CPU_BLOCK_END();

            if (cpu_init)
                CPU_BLOCK_END();

            if (new_ne)
                CPU_BLOCK_END();
            if ((cpu_state.flags & T_FLAG) || (trap == 2))
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

        if ((!cpu_state.abrt || (cpu_state.abrt & ABRT_EXPECTED)) && !new_ne && !x86_was_reset)
            codegen_block_end_recompile(block);

        if (x86_was_reset)
            codegen_reset();

        codegen_in_recompile = 0;
#    if defined(__APPLE__) && defined(__aarch64__)
        if (__builtin_available(macOS 11.0, *)) {
            pthread_jit_write_protect_np(1);
        }
#    endif
    } else if (!cpu_state.abrt) {
        /* Mark block but do not recompile */
#    ifdef USE_NEW_DYNAREC
        start_pc                 = cs + cpu_state.pc;
        const int max_block_size = (block->flags & CODEBLOCK_BYTE_MASK) ? ((128 - 25) - (start_pc & 0x3f)) : 1000;
#    else
        start_pc = cpu_state.pc;
#    endif

        cpu_block_end = 0;
        x86_was_reset = 0;

        codegen_block_init(phys_addr);

        while (!cpu_block_end) {
            oldcs  = CS;
            oldcpl = CPL;
            cpu_state.oldpc = cpu_state.pc;
            cpu_state.op32  = use32;

            cpu_state.ea_seg = &cpu_state.seg_ds;
            cpu_state.ssegs  = 0;

            codegen_endpc = (cs + cpu_state.pc) + 8;

            fetchdat      = fastreadl_fetch(cs + cpu_state.pc);

#    ifdef ENABLE_386_DYNAREC_LOG
            if (in_smm)
                x386_dynarec_log("[%04X:%08X] fetchdat = %08X\n", CS, cpu_state.pc, fetchdat);
#    endif

            if (!cpu_state.abrt) {
                /* Temp variables for FPU exception reporting. */
                cpu_state.temp_CS = CS;
                cpu_state.temp_cs = cs;
                cpu_state.temp_pc = cpu_state.pc;

                opcode = fetchdat & 0xFF;
                fetchdat >>= 8;

                cpu_state.pc++;

                x86_opcodes[(opcode | cpu_state.op32) & 0x3ff](fetchdat);

                if (x86_was_reset)
                    break;
            }

#    ifndef USE_NEW_DYNAREC
            if (!use32)
                cpu_state.pc &= 0xffff;
#    endif

                /* Cap source code at 4000 bytes per block; this
                   will prevent any block from spanning more than
                   2 pages. In practice this limit will never be
                   hit, as host block size is only 2kB */
#    ifdef USE_NEW_DYNAREC
            if (((cs + cpu_state.pc) - start_pc) >= max_block_size)
#    else
            if ((cpu_state.pc - start_pc) > 1000)
#    endif
                CPU_BLOCK_END();

            if (cpu_init)
                CPU_BLOCK_END();

            if (new_ne)
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

        if ((!cpu_state.abrt || (cpu_state.abrt & ABRT_EXPECTED)) && !new_ne && !x86_was_reset)
            codegen_block_end();

        if (x86_was_reset)
            codegen_reset();
    }
#    ifdef USE_NEW_DYNAREC
    else
        cpu_state.oldpc = cpu_state.pc;
#    endif

}

void
exec386_dynarec(int32_t cycs)
{
    int      vector;
    int      tempi;
    int32_t  cycdiff;
    int32_t  oldcyc;
    int32_t  oldcyc2;
    uint64_t oldtsc;
    uint64_t delta;

    int32_t cyc_period = cycs / (force_10ms ? 2000 : 200); /*5us*/

#    ifdef USE_ACYCS
    acycs = 0;
#    endif
    cycles_main += cycs;
    while (cycles_main > 0) {
        int32_t cycles_start;

        cycles += cyc_period;
        cycles_start = cycles;

        while (cycles > 0) {
            oldcs           = CS;
            oldcpl          = CPL;
#    ifndef USE_NEW_DYNAREC
            cpu_state.oldpc = cpu_state.pc;
            cpu_state.op32  = use32;

            cycdiff = 0;
#    endif
            oldcyc = oldcyc2 = cycles;
            cycles_old       = cycles;
            oldtsc           = tsc;
            tsc_old          = tsc;
            if (cpu_force_interpreter || cpu_override_dynarec ||  (!CACHE_ON())) /*Interpret block*/
            {
                exec386_dynarec_int();
            } else {
                exec386_dynarec_dyn();
            }

            if (cpu_init) {
                cpu_init = 0;
                resetx86();
            }

            if (cpu_state.abrt) {
                flags_rebuild();
                tempi          = cpu_state.abrt & ABRT_MASK;
                cpu_state.abrt = 0;
                x86_doabrt(tempi);
                if (cpu_state.abrt) {
                    cpu_state.abrt = 0;
                    cpu_state.pc   = cpu_state.oldpc;
#    ifndef USE_NEW_DYNAREC
                    CS = oldcs;
#    endif
                    pmodeint(8, 0);
                    if (cpu_state.abrt) {
                        cpu_state.abrt = 0;
                        softresetx86();
                        cpu_set_edx();
#    ifdef ENABLE_386_DYNAREC_LOG
                        x386_dynarec_log("Triple fault - reset\n");
#    endif
                    }
                }
            }

            if (new_ne) {
                oldcs = CS;
                cpu_state.oldpc = cpu_state.pc;
                new_ne = 0;
                x86_int(16);
            }

            if (smi_line)
                enter_smm_check(0);
            else if (nmi && nmi_enable && nmi_mask) {
                oldcs = CS;
                cpu_state.oldpc = cpu_state.pc;
                x86_int(2);
                nmi_enable = 0;
#    ifdef OLD_NMI_BEHAVIOR
                if (nmi_auto_clear) {
                    nmi_auto_clear = 0;
                    nmi            = 0;
                }
#    else
                nmi = 0;
#    endif
            } else if ((cpu_state.flags & I_FLAG) && pic.int_pending) {
                vector = picinterrupt();
                if (vector != -1) {
                    oldcs = CS;
                    cpu_state.oldpc = cpu_state.pc;
                    x86_int(vector);
                }
            }

            cycdiff = oldcyc - cycles;
            delta   = tsc - oldtsc;
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
                if (TIMER_VAL_LESS_THAN_VAL(timer_target, (uint64_t) tsc))
                    timer_process();
            }

#    ifdef USE_GDBSTUB
            if (gdbstub_instruction())
                return;
#    endif
        }

        cycles_main -= (cycles_start - cycles);
    }
}
#endif

int
is_dynarec_active(void)
{
#ifndef USE_DYNAREC
    return false;
#else
    return cpu_exec == exec386_dynarec && cpu_use_dynarec && !(cpu_force_interpreter || cpu_override_dynarec || (!CACHE_ON()));
#endif
}

/* Intel Inboard 386/PC POST fix-ups.

   These are address-gated corrections to this specific 1986 IBM XT BIOS's own POST
   self-tests and to the ATI Mach8 option ROM's self-test, needed because the Inboard's
   accelerated CPU breaks blind, instruction-counted delay loops those routines were
   calibrated against on a genuine 4.77 MHz 8088.

   Kept in ONE function called from BOTH interpreter loops (exec386() here and
   exec386_2386() in 386.c). cpu.c's cpu_set() routes 386DX/386SX-class CPUs to
   exec386_2386() and 486BL/486DLC-class ones to exec386(); when these fix-ups lived only
   in exec386(), selecting a plain 386DX/386SX - the CPU this card was actually sold to
   pair with - meant none of them ran at all, and POST hung in the Mach8 option ROM's PIT
   delay loop before even reaching the RAM count.

   Both call sites are gated on inboard386_present (set only while the card's device is
   instantiated), so on every other machine this costs one predictable branch per
   instruction and nothing here can run. That gate is what makes it safe: the individual
   fix-ups below are address-gated to this BIOS's and this option ROM's own code, but the
   segment-scoped ones (CS==0xC000, CS==0x0EAF) would otherwise be reachable by unrelated
   guests. */
void
inboard_post_fixups(void)
{
    /* Mach8 option-ROM self-test speed fix (2026-07-26, see
       INBOARD_86BOX_PORT_PLAN.md). `io_waitstates`/`reg_op_waitstates`
       (inboard386.c) exist to make the *system BIOS's* own blind, instruction-
       counted delay loops - calibrated against real 4.77MHz-ISA-bus timing -
       take roughly the same real wall-clock time regardless of the Inboard's
       configured accelerator speed. The Mach8 option ROM's own self-test is a
       different case entirely: once its PIT-readback delay loop is fixed (the
       C000:7B37 fix below) to resolve on the guest's own terms, its remaining
       delays are governed by genuine, correctly-real-time-paced PIT ticks, not
       blind instruction counts - so it needs no compensation at all, and
       applying the same inflation this project needs elsewhere in POST to the
       option ROM's own hundreds of individual I/O operations is exactly what
       was stretching a real-hardware-instant self-test into 65-100+ real
       seconds (confirmed by the user's own real hardware: banner shows
       immediately, no visible delay). Scoped to CS==0xC000 only - restores the
       real values the instant execution leaves the option ROM's own segment,
       so every other POST-timing fix elsewhere in this project (all tuned
       against the real, uncompensated io_waitstates/reg_op_waitstates values)
       is completely unaffected. */
    {
        static int c000_ws_saved        = 0;
        static int saved_io_ws          = 0;
        static int saved_regop_ws       = 0;
        static int saved_prefetch       = 0;
        static int saved_mem_prefetch   = 0;
        static int saved_rom_prefetch   = 0;
        static int saved_cycles_read    = 0;
        static int saved_cycles_read_l  = 0;
        static int saved_cycles_write   = 0;
        static int saved_cycles_write_l = 0;
        static int saved_isa_cycles     = 0;
        if (CS == 0xC000) {
            if (!c000_ws_saved) {
                c000_ws_saved           = 1;
                saved_io_ws             = io_waitstates;
                saved_regop_ws          = reg_op_waitstates;
                saved_prefetch          = cpu_prefetch_cycles;
                saved_mem_prefetch      = cpu_mem_prefetch_cycles;
                saved_rom_prefetch      = cpu_rom_prefetch_cycles;
                saved_cycles_read       = cpu_cycles_read;
                saved_cycles_read_l     = cpu_cycles_read_l;
                saved_cycles_write      = cpu_cycles_write;
                saved_cycles_write_l    = cpu_cycles_write_l;
                saved_isa_cycles        = isa_cycles;
                io_waitstates           = 0;
                reg_op_waitstates       = 0;
                cpu_prefetch_cycles     = 1;
                cpu_mem_prefetch_cycles = 1;
                cpu_rom_prefetch_cycles = 1;
                cpu_cycles_read         = 1;
                cpu_cycles_read_l       = 1;
                cpu_cycles_write        = 1;
                cpu_cycles_write_l      = 1;
                isa_cycles              = 1;
            }
        } else if (c000_ws_saved) {
            c000_ws_saved           = 0;
            io_waitstates           = saved_io_ws;
            reg_op_waitstates       = saved_regop_ws;
            cpu_prefetch_cycles     = saved_prefetch;
            cpu_mem_prefetch_cycles = saved_mem_prefetch;
            cpu_rom_prefetch_cycles = saved_rom_prefetch;
            cpu_cycles_read         = saved_cycles_read;
            cpu_cycles_read_l       = saved_cycles_read_l;
            cpu_cycles_write        = saved_cycles_write;
            cpu_cycles_write_l      = saved_cycles_write_l;
            isa_cycles              = saved_isa_cycles;
        }
    }

    /* Mach8/ATI Graphics Ultra option-ROM PIT-readback delay-loop fix (2026-07-26,
       omitted from PR #7626 - 386_dynarec.c was not part of that submission's file
       list). The option ROM's own self-test does a real PIT-elapsed-ticks busy-wait
       (OUT 43h,0 / IN 40h / IN 40h / SUB / NEG / CMP / JBE) which desyncs from this
       project's CPU-speed/waitstate timing overrides and never resolves on its own.
       Zero blast radius: only touches CS=C000 (the option ROM's own segment) at this
       exact loop's compare instruction, forces the elapsed-ticks register past the
       loop's own target so the guest's own CMP/JBE resolves and exits on its own
       terms - the same as a real, unaccelerated system's PIT eventually ticking past
       the target. 0x7B37/0x7B23 are two previously-encountered ROM revisions; 0x7B16
       is a third, found via live CS:PC tracing against this clone's own ROM copy. */
    if ((CS == 0xC000) && ((cpu_state.pc == 0x7B37) || (cpu_state.pc == 0x7B23) ||
                            (cpu_state.pc == 0x7B16)) && (AX <= BX)) {
        AX = (uint16_t) (BX + 1);
    }

    /* Intel Inboard 386/PC follow-up POST self-test fixes (2026-07-26), omitted from
       the original PR #7626 - 386_dynarec.c was not part of that submission's file list.
       The base PIC-IMR/DMA-refresh timing fix (dma_force_xt/force_xt_imr_timing) makes
       the first of three back-to-back BIOS self-tests pass, but two more chained ones
       were found to still intermittently fail on real timing:
       1. F000:E362-E3AC: the BIOS's own IRQ0-delivery and "no spurious interrupt" checks
          can be contaminated by a genuine, unrelated IRQ1 (keyboard controller's own
          power-on self-test byte) landing during this narrow window, before this BIOS
          ever unmasks interrupts at all - only IRQ1 is suppressed while IRQ0 is verified,
          then both are suppressed during the immediately-following negative check.
       2. F000:E507: the DMA channel-0 (DRAM refresh) status flag is a read-and-clear
          register: something else reads port 8 between the last real refresh cycle and
          this check, consuming the flag before the BIOS's own AND/JNE gets to see it,
          even though refresh itself (PIT channel 1 -> DREQ0) is working correctly. Forces
          the bit the guest's own check consumes, rather than the read that clears it.
       Address-gated to this exact 1986 XT BIOS's own self-test byte ranges - inert on any
       other BIOS content or machine, the same technique already used by this file's
       existing Mach8-specific timing fixes. This self-test's own "test passed" exit can
       land on any of three adjacent addresses (E38E/E3AD/E3AE) depending on a data-
       dependent micro-branch a few instructions earlier; only E3AE proceeds into the
       negative-test phase (2026-08-22 correction - the original port only recognized
       E3AE/E38E, missing E3AD, which left IRQ1 suppressed for the rest of execution
       whenever this exact ROM took that path). */
    {
        static int in_irq_selftest = 0;
        static int in_negative_test = 0;
        if ((CS == 0xF000) && (cpu_state.pc == 0xE362) && !in_irq_selftest) {
            in_irq_selftest = 1;
        }
        /* Safety net: the explicit exit addresses below are reached via a data-dependent
           micro-branch, so the exact one taken varies with POST timing (e.g. whether a
           large video option ROM ran first). If execution leaves this self-test's own
           address range by any path we didn't enumerate, disarm here rather than leave
           IRQ1/IRQ0 suppressed for the rest of the session - a stuck gate silently breaks
           the keyboard for the whole run (observed as a POST keyboard error requiring F1).
           This can only ever shorten suppression, never extend it. */
        if ((in_irq_selftest || in_negative_test) &&
            ((CS != 0xF000) || (cpu_state.pc < 0xE362) || (cpu_state.pc > 0xE3C6))) {
            in_irq_selftest  = 0;
            in_negative_test = 0;
        }
        if (in_irq_selftest) {
            picintc(2); /* IRQ1 (keyboard) only - bit 1. IRQ0 (bit 0) untouched. */
            if ((CS == 0xF000) && ((cpu_state.pc == 0xE3AE) || (cpu_state.pc == 0xE38E)
                                    || (cpu_state.pc == 0xE3AD))) {
                in_irq_selftest  = 0;
                in_negative_test = (cpu_state.pc == 0xE3AE);
            }
        }
        if (in_negative_test) {
            picintc(1);
            picintc(2); /* both IRQ0 and IRQ1 - this test wants total silence. */
            if ((CS == 0xF000) && ((cpu_state.pc == 0xE3C6) || (cpu_state.pc == 0xE38E))) {
                in_negative_test = 0;
            }
        }
    }
    if ((CS == 0xF000) && (cpu_state.pc == 0xE507))
        AL |= 0x01;

    /* Segment-650B/INT-68h wild-jump fix (2026-08-04, Windows 95 boot). VMM32's real-mode
       VxD-loader startup code (segment 0EAF) executes `INT 68h` (a private multiplex-style
       call, AH=function selector) whose IVT vector (offset 0x68*4=0x1A0) is never
       initialized by anything earlier in boot, so the CPU walks off into the raw IVT
       table as code until it coincidentally hits a real CALL FAR into segment 650B -
       legitimate code, but reached with completely bogus calling context, causing erratic
       wild jumping. Pre-initializes the vector to point at an IRET, so INT 68h becomes a
       harmless no-op instead.

       Points at the BIOS's own IRET at F000:FF53, as suggested by Michal Necasek in review
       of this PR, rather than writing a 0xCF stub into IVT slot 0xF0's vector-table entry
       (physical 0x3C0) and pointing there. Verified: the byte at file offset 0x7F53 of the
       U18/F800 chip is 0xCF (IRET) in both 1986 ROM revisions, 09MAY86 and 10JAN86, which
       are the only BIOSes this machine accepts. Strictly better - no injected code, and no
       assumption that INT 0F0h is unused this early in boot.

       Fires the moment CS first becomes 0x0EAF (empirically confirmed reliable trigger -
       firing earlier, e.g. at the very first instruction of boot, gets clobbered by
       ordinary BIOS POST/DOS kernel low-memory init before INT 68h is ever reached). */
    {
        static int patchint68_done = 0;
        if (!patchint68_done && (CS == 0x0EAF)) {
            patchint68_done = 1;
            /* Point INT 68h at the BIOS's own IRET at F000:FF53. No stub is injected. */
            mem_writeb_phys(0x1A0, 0x53); /* INT 68h vector offset lo  = 0xFF53 */
            mem_writeb_phys(0x1A1, 0xFF); /* INT 68h vector offset hi */
            mem_writeb_phys(0x1A2, 0x00); /* INT 68h vector segment lo = 0xF000 */
            mem_writeb_phys(0x1A3, 0xF0); /* INT 68h vector segment hi */
        }
    }

}

void
exec386(int32_t cycs)
{
    int      vector;
    int      tempi;
    int32_t  cycdiff;
    int32_t  oldcyc;
    int32_t  cycle_period;
    int32_t  ins_cycles;
    uint32_t addr;

    cycles += cycs;

    while (cycles > 0) {
        cycle_period = (timer_target - (uint64_t) tsc) + 1;

        x86_was_reset = 0;
        cycdiff       = 0;
        oldcyc        = cycles;
        while (cycdiff < cycle_period) {
#ifdef USE_DEBUG_REGS_486
            int ins_fetch_fault = 0;
#endif
            ins_cycles = cycles;

            oldcs  = CS;
            oldcpl = CPL;
            cpu_state.oldpc = cpu_state.pc;
            cpu_state.op32  = use32;

#ifndef USE_NEW_DYNAREC
            x86_was_reset = 0;
#endif

            cpu_state.ea_seg = &cpu_state.seg_ds;
            cpu_state.ssegs  = 0;

            if (inboard386_present)
                inboard_post_fixups();

#ifdef USE_DEBUG_REGS_486
            if (is386)
                ins_fetch_fault = cpu_386_check_instruction_fault();

            /* Breakpoint fault has priority over other faults. */
            if ((cpu_state.abrt == 0) & ins_fetch_fault) {
                x86gen();
                ins_fetch_fault = 0;
                /* No instructions executed at this point. */
                goto block_ended;
            }
#endif

            fetchdat = fastreadl_fetch(cs + cpu_state.pc);

            if (!cpu_state.abrt) {
                /* Temp variables for FPU exception reporting. */
                cpu_state.temp_CS = CS;
                cpu_state.temp_cs = cs;
                cpu_state.temp_pc = cpu_state.pc;

#ifdef ENABLE_386_LOG
                if (in_smm)
                    x386_dynarec_log("[%04X:%08X] %08X\n", CS, cpu_state.pc, fetchdat);
#endif
                opcode = fetchdat & 0xFF;
                fetchdat >>= 8;
#ifdef USE_DEBUG_REGS_486
                trap = (trap & ~1) | (!!(cpu_state.flags & T_FLAG));
#else
                trap = cpu_state.flags & T_FLAG;
#endif

                cpu_state.pc++;
#ifdef USE_DEBUG_REGS_486
                cpu_state.eflags &= ~(RF_FLAG);
#endif
                x86_opcodes[(opcode | cpu_state.op32) & 0x3ff](fetchdat);
                if (x86_was_reset)
                    break;
            }
#ifdef ENABLE_386_LOG
            else if (in_smm)
                x386_dynarec_log("[%04X:%08X] ABRT\n", CS, cpu_state.pc);
#endif

            if (cpu_flush_pending == 1)
                cpu_flush_pending++;
            else if (cpu_flush_pending == 2) {
                cpu_flush_pending = 0;
                flushmmucache_pc();
            }

#ifndef USE_NEW_DYNAREC
            if (!use32)
                cpu_state.pc &= 0xffff;
#endif

            if (cpu_end_block_after_ins)
                cpu_end_block_after_ins--;

#ifdef USE_DEBUG_REGS_486
block_ended:
#endif
            if (cpu_state.abrt) {
#ifdef ENABLE_386_LOG
                uint8_t oop    = opcode;
#endif
                flags_rebuild();
                tempi          = cpu_state.abrt & ABRT_MASK;
                cpu_state.abrt = 0;
                x86_doabrt(tempi);
                if (cpu_state.abrt) {
                    x386_dynarec_log("Double fault - %02X\n", oop);
                    cpu_state.abrt = 0;
#ifndef USE_NEW_DYNAREC
                    CS = oldcs;
#endif
                    cpu_state.pc = cpu_state.oldpc;
                    x386_dynarec_log("Double fault\n");
                    pmodeint(8, 0);
                    if (cpu_state.abrt) {
                        cpu_state.abrt = 0;
                        softresetx86();
                        cpu_set_edx();
#ifdef ENABLE_386_LOG
                        x386_dynarec_log("Triple fault - reset\n");
#endif
                    }
                }

#ifdef USE_DEBUG_REGS_486
                if (is386 && !x86_was_reset  && ins_fetch_fault)
                    x86gen();
#endif
            } else if (new_ne) {
                flags_rebuild();

                new_ne = 0;
                oldcs = CS;
                cpu_state.oldpc = cpu_state.pc;
                x86_int(16);
            } else if (trap) {
                flags_rebuild();
#ifdef USE_DEBUG_REGS_486
                if (trap & 2) dr[6] |= 0x8000;
                if (trap & 1) dr[6] |= 0x4000;
                if (trap & 16) dr[6] |= 0x2000;
#endif
                trap = 0;
                oldcs = CS;
                cpu_state.oldpc = cpu_state.pc;
                x86_int(1);
            }

            if (smi_line)
                enter_smm_check(0);
            else if (nmi && nmi_enable && nmi_mask) {
                oldcs = CS;
                cpu_state.oldpc = cpu_state.pc;
                x86_int(2);
                nmi_enable = 0;
#ifdef OLD_NMI_BEHAVIOR
                if (nmi_auto_clear) {
                    nmi_auto_clear = 0;
                    nmi            = 0;
                }
#else
                nmi = 0;
#endif
            } else if ((cpu_state.flags & I_FLAG) && pic.int_pending && !cpu_end_block_after_ins) {
                vector = picinterrupt();
                if (vector != -1) {
                    flags_rebuild();
                    if (msw & 1)
                        pmodeint(vector, 0);
                    else {
                        writememw(ss, (SP - 2) & 0xFFFF, cpu_state.flags);
                        writememw(ss, (SP - 4) & 0xFFFF, CS);
                        writememw(ss, (SP - 6) & 0xFFFF, cpu_state.pc);
                        SP -= 6;
                        addr = (vector << 2) + idt.base;
                        cpu_state.flags &= ~I_FLAG;
                        cpu_state.flags &= ~T_FLAG;
                        cpu_state.pc = readmemw(0, addr);
                        loadcs(readmemw(0, addr + 2));
                    }
                }
            }

            ins_cycles -= cycles;
            tsc += ins_cycles;

            cycdiff = oldcyc - cycles;

            if (timetolive) {
                timetolive--;
                if (!timetolive)
                    fatal("Life expired\n");
            }

            if (TIMER_VAL_LESS_THAN_VAL(timer_target, (uint64_t) tsc))
                timer_process();

#ifdef USE_GDBSTUB
            if (gdbstub_instruction())
                return;
#endif
        }
    }
}
