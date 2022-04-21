#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#ifndef INFINITY
# define INFINITY   (__builtin_inff())
#endif

#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include "x86.h"
#include "x86_ops.h"
#include "x87.h"
#include "x86_flags.h"
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/gdbstub.h>
#include "codegen.h"

#define CPU_BLOCK_END() cpu_block_end = 1

#ifndef IS_DYNAREC
#define IS_DYNAREC
#endif

#include "386_common.h"


static __inline void fetch_ea_32_long(uint32_t rmdat)
{
        eal_r = eal_w = NULL;
        easeg = cpu_state.ea_seg->base;
        if (easeg != 0xFFFFFFFF && ((easeg + cpu_state.eaaddr) & 0xFFF) <= 0xFFC)
        {
                uint32_t addr = easeg + cpu_state.eaaddr;
                if ( readlookup2[addr >> 12] != (uintptr_t) LOOKUP_INV)
                   eal_r = (uint32_t *)(readlookup2[addr >> 12] + addr);
                if (writelookup2[addr >> 12] != (uintptr_t) LOOKUP_INV)
                   eal_w = (uint32_t *)(writelookup2[addr >> 12] + addr);
        }
}

static __inline void fetch_ea_16_long(uint32_t rmdat)
{
        eal_r = eal_w = NULL;
        easeg = cpu_state.ea_seg->base;
        if (easeg != 0xFFFFFFFF && ((easeg + cpu_state.eaaddr) & 0xFFF) <= 0xFFC)
        {
                uint32_t addr = easeg + cpu_state.eaaddr;
                if ( readlookup2[addr >> 12] != (uintptr_t) LOOKUP_INV)
                   eal_r = (uint32_t *)(readlookup2[addr >> 12] + addr);
                if (writelookup2[addr >> 12] != (uintptr_t) LOOKUP_INV)
                   eal_w = (uint32_t *)(writelookup2[addr >> 12] + addr);
        }
}

#define fetch_ea_16(rmdat)              cpu_state.pc++; if (cpu_mod != 3) fetch_ea_16_long(rmdat);
#define fetch_ea_32(rmdat)              cpu_state.pc++; if (cpu_mod != 3) fetch_ea_32_long(rmdat);


#define PREFETCH_RUN(instr_cycles, bytes, modrm, reads, read_ls, writes, write_ls, ea32)
#define PREFETCH_PREFIX()
#define PREFETCH_FLUSH()

#define OP_TABLE(name) dynarec_ops_ ## name

#define CLOCK_CYCLES(c)
#if 0
#define CLOCK_CYCLES_FPU(c)
#define CONCURRENCY_CYCLES(c) fpu_cycles = (c)
#else
#define CLOCK_CYCLES_FPU(c)
#define CONCURRENCY_CYCLES(c)
#endif
#define CLOCK_CYCLES_ALWAYS(c) cycles -= (c)

#include "386_ops.h"
