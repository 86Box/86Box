/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Built-in debugger core: stopping and stepping the processor,
 *          and looking at memory without side effects, for the memory
 *          viewer.
 *
 *          Memory is read only through the direct pointers the processor
 *          itself fetches code through (_mem_exec[]); a device's read
 *          handlers are never called, since many of them change state
 *          (latches, FIFOs, status bits, the read lookup cache). Bytes
 *          behind handlers only are reported as inaccessible instead.
 *          Linear addresses are translated by a page walk of our own that
 *          reads the tables the same way and sets no accessed or dirty bits.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include "x86_ops.h"
#include <86box/mem.h>
#include <86box/plat.h>
#include <86box/debugger.h>

extern void cpu_386_flags_rebuild(void);
extern const uint8_t opf_0f[256];

volatile int debugger_attached = 0;

/* Written by the viewer and read by the processor's thread, and the other
   way around: the ATOMIC_ accesses order them on hosts that need it. The
   breakpoint list is written before its count, so the processor's thread
   never sees an entry that is not filled in yet. */
static ATOMIC_INT  debugger_state;
static ATOMIC_INT  debugger_steps;
static ATOMIC_INT  debugger_reason;
/* Counts the stops, for a viewer that polls: a step can start and end
   between two looks at the state. */
static ATOMIC_UINT debugger_stops;
static ATOMIC_UINT debugger_bp[DEBUGGER_MAX_BREAKPOINTS];
static ATOMIC_INT  debugger_bp_num;
/* Set while a hard reset tears down and rebuilds the machine. */
static ATOMIC_INT  debugger_reset_busy;
/* The processor was stopped in the middle of an instruction that runs in
   several passes (a REP string instruction, HLT). */
static int         debugger_partial;

static uint32_t
debugger_ip(void)
{
    uint16_t pc;

    if (is286)
        return cpu_state.pc;
    /* In 8080 emulation mode the 8080's PC is the live one. */
    if (vx0_i8080_state(&pc, NULL, NULL, NULL))
        return pc;
    return ip_808x();
}

static uint32_t
debugger_pc(void)
{
    return cs + debugger_ip();
}

/* On the 286 and later, a REP string instruction runs in chunks, and HLT
   with no interrupt to take, by going back to the instruction's start
   (x86_ops_rep.h, opHLT): the instruction at CS:IP is the one that just
   ran. A jump to itself also does that, but is not REP or HLT. */
static int
debugger_insn_repeats(void)
{
    uint8_t b[15];
    uint8_t cls[15];
    int     rep = 0;

    if ((cpu_state.pc != cpu_state.oldpc) || (CS != oldcs))
        return 0;

    debugger_mem_read(DEBUGGER_SPACE_LINEAR, debugger_pc(), sizeof(b), b, cls);
    for (int i = 0; i < (int) sizeof(b); i++) {
        if ((cls[i] != DEBUGGER_MEM_RAM) && (cls[i] != DEBUGGER_MEM_ROM) && (cls[i] != DEBUGGER_MEM_VIDEO))
            return 0;
        switch (b[i]) {
            case 0xf2:
            case 0xf3:
                rep = 1;
                break;
            case 0x26:
            case 0x2e:
            case 0x36:
            case 0x3e:
            case 0x64 ... 0x67:
            case 0xf0:
                break;
            case 0xf4:
                return 1;
            case 0x6c ... 0x6f:
            case 0xa4 ... 0xa7:
            case 0xaa ... 0xaf:
                return rep;
            default:
                return 0;
        }
    }

    return 0;
}

static int
debugger_stop(int reason)
{
    ATOMIC_STORE(debugger_reason, reason);
    ATOMIC_STORE(debugger_state, DEBUGGER_BREAK);
    ATOMIC_INC(debugger_stops);

    return 1;
}

int
debugger_instruction(int partial)
{
    int n;

    /* A hard reset (CF9) that the recompiler's loop would have taken; set
       only with the recompiler on, which this runs in place of. */
    if (cpu_init) {
        cpu_init = 0;
        resetx86();
    }

    /* A pass through an instruction that is not done yet is not an
       instruction boundary for breakpoints or steps; a pause still stops
       there, or it could wait forever on a HLT. */
    if (!partial && is286)
        partial = debugger_insn_repeats();
    debugger_partial = partial;

    if (ATOMIC_LOAD(debugger_state) == DEBUGGER_BREAK)
        return 1;
    if (partial)
        return 0;

    n = ATOMIC_LOAD(debugger_bp_num);
    if (n) {
        uint32_t pc = debugger_pc();

        for (int i = 0; i < n; i++) {
            if (ATOMIC_LOAD(debugger_bp[i]) == pc)
                return debugger_stop(DEBUGGER_STOP_BREAKPOINT);
        }
    }

    if (ATOMIC_LOAD(debugger_state) == DEBUGGER_STEP) {
        n = ATOMIC_LOAD(debugger_steps) - 1;
        ATOMIC_STORE(debugger_steps, n);
        if (n <= 0)
            return debugger_stop(DEBUGGER_STOP_STEP);
    }

    return 0;
}

typedef void (*debugger_exec_t)(int32_t cycs);

/* What runs while attached: the recompiler's own interpreter, which stops
   after every instruction where recompiled blocks run many between checks.
   Not exec386_2386 even where the machine would run it with the recompiler
   off: cpu_use_exec, set for the recompiler, picks exec386's interrupt,
   task switch and memory helpers in the shared code. */
static debugger_exec_t
debugger_exec(void)
{
#ifdef USE_DYNAREC
    if (debugger_attached && (cpu_exec == exec386_dynarec))
        return exec386;
#endif

    return cpu_exec;
}

void
debugger_cpu_exec(int32_t cycs)
{
    debugger_exec_t exec;

    if (!debugger_attached) {
        cpu_exec(cycs);
        return;
    }

    if (ATOMIC_LOAD(debugger_state) == DEBUGGER_BREAK)
        return;

    exec = debugger_exec();

    /* Make sure a step gets to run at least one instruction. */
    if ((ATOMIC_LOAD(debugger_state) == DEBUGGER_STEP) && ((cycles + cycs) <= 0))
        cycs += -(cycles + cycs) + 1;

    exec(cycs);
}

void
debugger_attach(int attach)
{
    if (attach) {
        ATOMIC_STORE(debugger_reason, DEBUGGER_STOP_NONE);
        ATOMIC_STORE(debugger_state, DEBUGGER_RUN);
        debugger_partial  = 0;
        debugger_attached = 1;
    } else {
        debugger_attached = 0;
        ATOMIC_STORE(debugger_bp_num, 0);
        ATOMIC_STORE(debugger_state, DEBUGGER_RUN);
    }
}

int
debugger_get_state(void)
{
    return ATOMIC_LOAD(debugger_state);
}

int
debugger_stopped(void)
{
    return debugger_attached && (ATOMIC_LOAD(debugger_state) == DEBUGGER_BREAK);
}

void
debugger_pause(void)
{
    if (ATOMIC_LOAD(debugger_state) != DEBUGGER_BREAK)
        debugger_stop(DEBUGGER_STOP_PAUSE);
}

void
debugger_continue(void)
{
    ATOMIC_STORE(debugger_reason, DEBUGGER_STOP_NONE);
    ATOMIC_STORE(debugger_state, DEBUGGER_RUN);
}

void
debugger_step(int count)
{
    if (count < 1)
        count = 1;
    ATOMIC_STORE(debugger_reason, DEBUGGER_STOP_NONE);
    ATOMIC_STORE(debugger_steps, count);
    ATOMIC_STORE(debugger_state, DEBUGGER_STEP);
}

int
debugger_stop_reason(void)
{
    return ATOMIC_LOAD(debugger_reason);
}

uint32_t
debugger_stop_count(void)
{
    return ATOMIC_LOAD(debugger_stops);
}

int
debugger_stopped_partial(void)
{
    return debugger_partial;
}

/* A hard reset frees and reallocates memory the viewer reads: the viewer
   checks debugger_resetting() with the blit lock held, and taking the lock
   here waits out a viewer that got it before the flag was set. */
void
debugger_reset_begin(void)
{
    ATOMIC_STORE(debugger_reset_busy, 1);
    startblit();
    endblit();
}

void
debugger_reset_end(void)
{
    ATOMIC_STORE(debugger_reset_busy, 0);
}

int
debugger_resetting(void)
{
    return ATOMIC_LOAD(debugger_reset_busy);
}

int
debugger_breakpoint_present(uint32_t linear)
{
    const int n = ATOMIC_LOAD(debugger_bp_num);

    for (int i = 0; i < n; i++) {
        if (ATOMIC_LOAD(debugger_bp[i]) == linear)
            return 1;
    }

    return 0;
}

/* Returns 1 if the breakpoint is now set, 0 if it was removed, -1 if the
   list is full. The list only ever grows or shrinks by its last entry
   being written before the count changes, so the processor thread sees
   a consistent list. */
int
debugger_breakpoint_toggle(uint32_t linear)
{
    const int n = ATOMIC_LOAD(debugger_bp_num);

    for (int i = 0; i < n; i++) {
        if (ATOMIC_LOAD(debugger_bp[i]) == linear) {
            ATOMIC_STORE(debugger_bp[i], ATOMIC_LOAD(debugger_bp[n - 1]));
            ATOMIC_STORE(debugger_bp_num, n - 1);
            return 0;
        }
    }

    if (n >= DEBUGGER_MAX_BREAKPOINTS)
        return -1;

    ATOMIC_STORE(debugger_bp[n], linear);
    ATOMIC_STORE(debugger_bp_num, n + 1);
    return 1;
}

int
debugger_breakpoints(uint32_t *list, int max)
{
    int n = ATOMIC_LOAD(debugger_bp_num);

    if (n > max)
        n = max;
    for (int i = 0; i < n; i++)
        list[i] = ATOMIC_LOAD(debugger_bp[i]);

    return n;
}

void
debugger_breakpoints_clear(void)
{
    ATOMIC_STORE(debugger_bp_num, 0);
}

/* Where the byte at physical address addr is, as the processor sees memory
   now (SMM included), and what it is. Only the processor's direct code
   pointer is used: it is set for a granule exactly when the granule is
   plain memory behind a mapping with a backing (mem_mapping_exec_granule()),
   so reading through it is what the processor's own fetches do. */
/* Video memory is the one exception to reading only through the direct
   pointers: for the handlers a card registered (mem_debug_add()), it says
   where in its memory a byte is, without the handlers' latches, wait states
   or blitter data. Handlers nothing was registered for stay out of reach. */
static uint8_t *
debugger_video_ptr(const mem_mapping_t *map, uint32_t addr, int write)
{
    const mem_debug_ptr_t debug_ptr = mem_debug_find(map, write);

    return debug_ptr ? debug_ptr(addr, write, map->priv) : NULL;
}

static uint8_t *
debugger_phys_ptr(uint32_t addr, uint8_t *cls)
{
    uint32_t             g    = addr >> MEM_GRANULARITY_BITS;
    const mem_mapping_t *rmap = read_mapping[g];
    uint8_t             *p;

    if (!rmap) {
        *cls = write_mapping[g] ? DEBUGGER_MEM_INACCESSIBLE : DEBUGGER_MEM_UNMAPPED;
        return NULL;
    }

    /* The direct pointer must belong to the mapping reads go to: one can
       be executable from one place and read from another. */
    if (!_mem_exec[g] || !rmap->exec) {
        p    = debugger_video_ptr(rmap, addr, 0);
        *cls = p ? DEBUGGER_MEM_VIDEO : DEBUGGER_MEM_INACCESSIBLE;
        return p;
    }
    p = &_mem_exec[g][addr & MEM_GRANULARITY_MASK];
    if (((uintptr_t) p < (uintptr_t) rmap->exec) ||
        (((uintptr_t) p - (uintptr_t) rmap->exec) >= (uintptr_t) ((rmap->mask == 0xffffffff) ? rmap->size : (rmap->mask + 1)))) {
        *cls = DEBUGGER_MEM_INACCESSIBLE;
        return NULL;
    }

    if ((uintptr_t) p >= (uintptr_t) ram && (uintptr_t) p < ((uintptr_t) ram + mem_ram_size()))
        *cls = DEBUGGER_MEM_RAM;
    else
        *cls = DEBUGGER_MEM_ROM;

    return p;
}

/* A page table entry, read the way the processor's page walk reads it
   (rammap() in mem.c): through the direct pointer whatever the mapping, and
   all ones where there is none. Entries are aligned, so never split. */
static uint32_t
debugger_pt_read32(uint32_t addr)
{
    const uint8_t *page = _mem_exec[addr >> MEM_GRANULARITY_BITS];
    const uint8_t *p;

    if (!page)
        return 0xffffffff;
    p = &page[addr & MEM_GRANULARITY_MASK & ~3];

    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24);
}

static uint64_t
debugger_pt_read64(uint64_t addr)
{
    /* The processor truncates the address to 32 bits. */
    addr &= ~7ULL;
    return debugger_pt_read32((uint32_t) addr) | ((uint64_t) debugger_pt_read32((uint32_t) addr + 4) << 32);
}

/* SMM entry makes rammask the unmasked one and keeps the guest's in
   old_rammask for RSM to restore: rammask is what accesses use either way. */
static uint32_t
debugger_a20_mask(void)
{
    return rammask;
}

/* As mmutranslatereal(), less the permission checks and the accessed and
   dirty bit updates, and without touching CR2 or the abort state. */
int
debugger_translate(uint32_t linear, uint32_t *phys)
{
    uint64_t p;

    if (!is386 || !(cr0 & 0x80000000)) {
        *phys = linear & debugger_a20_mask();
        return 1;
    }

    if (cr4 & CR4_PAE) {
        uint64_t pdpte = pae_pdpte[(linear >> 30) & 3] & 0x000000ffffffffffULL;
        uint64_t pde;
        uint64_t pte;

        if (!(pdpte & 1))
            return 0;
        pde = debugger_pt_read64((pdpte & ~0xfffULL) + ((linear >> 18) & 0xff8));
        if (!(pde & 1))
            return 0;
        pde &= 0x000000ffffffffffULL;
        if (pde & 0x80)
            p = (pde & ~0x1fffffULL) + (linear & 0x1fffff);
        else {
            pte = debugger_pt_read64((pde & ~0xfffULL) + ((linear >> 9) & 0xff8));
            if (!(pte & 1))
                return 0;
            p = (pte & 0x000000fffffff000ULL) + (linear & 0xfff);
        }
    } else {
        uint32_t pde;
        uint32_t pte;

        pde = debugger_pt_read32((cr3 & ~0xfff) + ((linear >> 20) & 0xffc));
        if (!(pde & 1))
            return 0;
        if ((pde & 0x80) && (cr4 & CR4_PSE)) {
            p = pde & ~0x3fffff;
            if (cpu_features & CPU_FEATURE_PSE36)
                p |= (uint64_t) (pde & 0x1e000) << 19;
            p += linear & 0x3fffff;
        } else {
            pte = debugger_pt_read32((pde & ~0xfff) + ((linear >> 10) & 0xffc));
            if (!(pte & 1))
                return 0;
            p = (pte & ~0xfff) + (linear & 0xfff);
        }
    }

    /* Nothing is mapped past 4 GB. */
    if (p > 0xffffffffULL)
        return 0;
    *phys = (uint32_t) p & debugger_a20_mask();

    return 1;
}

void
debugger_mem_read(int space, uint32_t addr, uint32_t len, uint8_t *data, uint8_t *cls)
{
    uint32_t page      = 0;
    uint32_t page_phys = 0;
    int      page_ok   = 0;
    int      have_page = 0;

    for (uint32_t i = 0; i < len; i++) {
        uint32_t       a = addr + i;
        uint8_t        c;
        const uint8_t *p = NULL;

        if (space == DEBUGGER_SPACE_LINEAR) {
            /* One walk per page, not per byte. */
            if (!have_page || ((a & ~0xfff) != page)) {
                page      = a & ~0xfff;
                page_ok   = debugger_translate(page, &page_phys);
                have_page = 1;
            }
            if (!page_ok) {
                c = DEBUGGER_MEM_INACCESSIBLE;
                goto done;
            }
            /* The A20 mask may split a page's bytes; translate each one. */
            if (!is386 || !(cr0 & 0x80000000))
                a &= debugger_a20_mask();
            else
                a = (page_phys & ~0xfff) | (a & 0xfff);
        }

        p = debugger_phys_ptr(a, &c);

done:
        if (data)
            data[i] = p ? *p : 0xff;
        if (cls)
            cls[i] = c;
    }
}

/* The 8086 and 80186 may already hold the byte at addr in their prefetch
   queue: the next bytes from CS:IP on, wrapping within CS as fetches do. */
static void
debugger_flush_queue(uint32_t addr)
{
    const uint32_t ip = debugger_ip();

    if (is286)
        return;

    for (uint32_t i = 0; i < 8; i++) {
        if (((cs + ((ip + i) & 0xffff)) & debugger_a20_mask()) == addr) {
            flush_prefetch_808x();
            return;
        }
    }
}

int
debugger_mem_write(int space, uint32_t addr, uint8_t val)
{
    const mem_mapping_t *rmap;
    uint8_t              cls;
    uint8_t             *p;

    if (space == DEBUGGER_SPACE_LINEAR) {
        if (!debugger_translate(addr, &addr))
            return 0;
    }

    /* Only where writes go to the same place reads come from: shadow RAM
       that is read-only, or reads ROM and writes RAM, is left alone. */
    rmap = read_mapping[addr >> MEM_GRANULARITY_BITS];
    if (!rmap || (write_mapping[addr >> MEM_GRANULARITY_BITS] != rmap))
        return 0;

    p = debugger_phys_ptr(addr, &cls);
    if (cls == DEBUGGER_MEM_VIDEO) {
        /* Video memory, the one other thing that can be written, where the
           write handler is the one registered with the read handler. */
        p = debugger_video_ptr(rmap, addr, 1);
        if (!p)
            return 0;
        *p = val;
    } else {
        if (!p || (cls != DEBUGGER_MEM_RAM))
            return 0;
        *p = val;
    }

    /* Recompiled code made from this page must go, video memory with an
       exec pointer included. */
    mem_invalidate_range(addr, addr);
    debugger_flush_queue(addr);

    return 1;
}

int
debugger_selector_base(uint16_t sel, uint32_t *base)
{
    const x86seg *table;
    uint8_t       desc[8];
    uint8_t       cls[8];

    if (!is286 || !(msw & 1) || (cpu_state.eflags & VM_FLAG)) {
        *base = (uint32_t) sel << 4;
        return 1;
    }

    if (!(sel & ~3))
        return 0;
    table = (sel & 4) ? &ldt : &gdt;
    if (((sel & ~7) + 7) > table->limit)
        return 0;

    debugger_mem_read(DEBUGGER_SPACE_LINEAR, table->base + (sel & ~7), 8, desc, cls);
    for (int i = 0; i < 8; i++) {
        if ((cls[i] != DEBUGGER_MEM_RAM) && (cls[i] != DEBUGGER_MEM_ROM))
            return 0;
    }

    *base = desc[2] | (desc[3] << 8) | (desc[4] << 16);
    if (is386)
        *base |= (uint32_t) desc[7] << 24;

    return 1;
}

void
debugger_decode_info(debugger_decode_info_t *info)
{
    if (!is286) {
        if (is_nec)
            info->isa = vx0_i8080_state(NULL, NULL, NULL, NULL) ? DEBUGGER_ISA_I8080 : DEBUGGER_ISA_V30;
        else
            info->isa = is186 ? DEBUGGER_ISA_80186 : DEBUGGER_ISA_8086;
    } else
        info->isa = is386 ? DEBUGGER_ISA_386 : DEBUGGER_ISA_286;

    switch (fpu_type) {
        case FPU_NONE:
            info->fpu = DEBUGGER_FPU_NONE;
            break;
        case FPU_8087:
        case FPU_80187:
            info->fpu = DEBUGGER_FPU_8087;
            break;
        case FPU_287:
        case FPU_287XL:
            info->fpu = DEBUGGER_FPU_287;
            break;
        default:
            info->fpu = DEBUGGER_FPU_387;
            break;
    }

    info->code32   = is386 && !!use32;
    info->stack32  = is386 && !!stack32;
    info->wrap16   = !is386;
    info->fpu_trap = is286 && !!(cr0 & 0x0c);
    info->vpc      = !!is_vpc;
    info->fpu_ud   = is486 || isibm486;
}

/* The tables the running interpreter dispatches through, and what they hold
   for an opcode the processor does not have: the 286 and 386 interpreter
   (386.c) and the 486 and later one (386_dynarec.c) each have their own. */
static const OpFn *
debugger_op_table(int map, OpFn *illegal)
{
    if (debugger_exec() == exec386_2386) {
        *illegal = x86_2386_illegal_op;
        return map ? x86_2386_opcodes_0f : x86_2386_opcodes;
    }

    *illegal = x86_illegal_op;
    return map ? x86_opcodes_0f : x86_opcodes;
}

/* Whether the x87 tables the running interpreter dispatches through have
   the ESC opcode op (D8-DF) with ModR/M byte modrm: DEBUGGER_OP_ILLEGAL
   where they hold FPU_ILLEGAL, which is #UD on the 486 and later and reads
   the operand and does nothing before (x87_ops.h). */
int
debugger_fpu_kind(uint8_t op, uint8_t modrm, int addr32)
{
    const OpFn *const *tables;
    const OpFn        *t;
    OpFn               illegal;
    static const OpFn *const *const normal[2][8] = {
        { &x86_opcodes_d8_a16, &x86_opcodes_d9_a16, &x86_opcodes_da_a16, &x86_opcodes_db_a16,
          &x86_opcodes_dc_a16, &x86_opcodes_dd_a16, &x86_opcodes_de_a16, &x86_opcodes_df_a16 },
        { &x86_opcodes_d8_a32, &x86_opcodes_d9_a32, &x86_opcodes_da_a32, &x86_opcodes_db_a32,
          &x86_opcodes_dc_a32, &x86_opcodes_dd_a32, &x86_opcodes_de_a32, &x86_opcodes_df_a32 }
    };
    static const OpFn *const *const t2386[2][8] = {
        { &x86_2386_opcodes_d8_a16, &x86_2386_opcodes_d9_a16, &x86_2386_opcodes_da_a16, &x86_2386_opcodes_db_a16,
          &x86_2386_opcodes_dc_a16, &x86_2386_opcodes_dd_a16, &x86_2386_opcodes_de_a16, &x86_2386_opcodes_df_a16 },
        { &x86_2386_opcodes_d8_a32, &x86_2386_opcodes_d9_a32, &x86_2386_opcodes_da_a32, &x86_2386_opcodes_db_a32,
          &x86_2386_opcodes_dc_a32, &x86_2386_opcodes_dd_a32, &x86_2386_opcodes_de_a32, &x86_2386_opcodes_df_a32 }
    };

    if (!is286 || (op < 0xd8) || (op > 0xdf))
        return DEBUGGER_OP_NORMAL;
    addr32 = !!addr32;

    if (debugger_exec() == exec386_2386) {
        tables  = t2386[addr32][op - 0xd8];
        illegal = x86_2386_fpu_illegal_op[addr32];
    } else {
        tables  = normal[addr32][op - 0xd8];
        illegal = x86_fpu_illegal_op[addr32];
    }
    t = *tables;
    if (!t)
        return DEBUGGER_OP_NORMAL;

    /* Indexed as x86_ops_fpu.h does: D8 and DC by reg and mod only. */
    if ((op == 0xd8) || (op == 0xdc))
        return (t[(modrm >> 3) & 0x1f] == illegal) ? DEBUGGER_OP_ILLEGAL : DEBUGGER_OP_NORMAL;
    return (t[modrm] == illegal) ? DEBUGGER_OP_ILLEGAL : DEBUGGER_OP_NORMAL;
}

int
debugger_opcode_kind(int map, uint8_t op, int opsize)
{
    OpFn        illegal;
    const OpFn *t = debugger_op_table(map, &illegal);

    if (!is286 || !t || (t[(op | opsize) & 0x3ff] == illegal))
        return DEBUGGER_OP_ILLEGAL;

    if (!map)
        return ((op == 0xf1) && !is386) ? DEBUGGER_OP_LOCK : DEBUGGER_OP_NORMAL;

    /* These are only in the tables of the processors that have them; where
       one opcode is different things on different processors, which table
       this is tells. */
    switch (op) {
        case 0x05:
            return ((t == ops_286_0f) || (t == ops_2386_286_0f)) ? DEBUGGER_OP_LOADALL : DEBUGGER_OP_NORMAL;
        case 0x07:
            return ((t == ops_386_0f) || (t == ops_486_0f) || (t == ops_c486_0f) || (t == ops_stpc_0f) || (t == ops_ibm486_0f) ||
                    (t == ops_2386_386_0f) || (t == ops_2386_486_0f) || (t == ops_2386_ibm486_0f)) ?
                       DEBUGGER_OP_LOADALL386 : DEBUGGER_OP_NORMAL;
        case 0x0d:
        case 0x18 ... 0x1f:
            return ((t == ops_pentiumpro_0f) || (t == ops_pentium2_0f) || (t == ops_pentium2d_0f)) ? DEBUGGER_OP_HINT_NOP :
                                                                                                    DEBUGGER_OP_NORMAL;
        case 0x10 ... 0x13:
            return DEBUGGER_OP_UMOV;
        case 0x36:
            return DEBUGGER_OP_RDSHR;
        case 0x37:
            return DEBUGGER_OP_WRSHR;
        case 0x38:
            return DEBUGGER_OP_SMINT;
        case 0x3a:
            return DEBUGGER_OP_BB0_RESET;
        case 0x3b:
            return DEBUGGER_OP_BB1_RESET;
        case 0x3c:
            return DEBUGGER_OP_CPU_WRITE;
        case 0x3d:
            return DEBUGGER_OP_CPU_READ;
        case 0x3f:
            return DEBUGGER_OP_VPCEXT;
        case 0x78 ... 0x7d:
            return DEBUGGER_OP_SVDC + (op - 0x78);
        case 0x7e:
            return ((t == ops_c486_0f) || (t == ops_stpc_0f) || (t == ops_c6x86_0f) || (t == ops_c6x86l_0f)) ? DEBUGGER_OP_SMINT :
                                                                                                             DEBUGGER_OP_NORMAL;
        default:
            return DEBUGGER_OP_NORMAL;
    }
}

int
debugger_lock_legal(uint8_t op, uint8_t b1, uint8_t b2)
{
    /* The 8086, 80186 and 286 lock any instruction. */
    if (!is386)
        return 1;

    return is_lock_legal(op | (b1 << 8) | ((uint32_t) b2 << 16));
}

int
debugger_nec_0f_modrm(uint8_t op)
{
    return opf_0f[op] & 1; /* OP_MRM */
}

int
debugger_prefetch_queue(uint8_t *buf, int max)
{
    if (is286 || vx0_i8080_state(NULL, NULL, NULL, NULL))
        return 0;

    return queue_peek_808x(buf, max);
}

void
debugger_cpu_info(debugger_cpu_info_t *info)
{
    info->ip             = debugger_ip();
    info->cs_base        = cs;
    info->cs_sel         = CS;
    info->pc_linear      = cs + info->ip;
    info->code32         = is386 && !!use32;
    info->protected_mode = is286 && (msw & 1);
    info->v86            = is386 && info->protected_mode && !!(cpu_state.eflags & VM_FLAG);
}

/* FLAGS as an instruction would see them, without leaving the lazily
   evaluated flags rebuilt: the processor's state is put back as it was. */
static uint16_t
debugger_flags(void)
{
    int      op  = cpu_state.flags_op;
    uint32_t res = cpu_state.flags_res;
    uint32_t op1 = cpu_state.flags_op1;
    uint32_t op2 = cpu_state.flags_op2;
    uint16_t old = cpu_state.flags;
    uint16_t ret;

    if (!is286)
        return cpu_state.flags;

    cpu_386_flags_rebuild();
    ret = cpu_state.flags;

    cpu_state.flags     = old;
    cpu_state.flags_op  = op;
    cpu_state.flags_res = res;
    cpu_state.flags_op1 = op1;
    cpu_state.flags_op2 = op2;

    return ret;
}

#define APPEND(...)                                                  \
    do {                                                             \
        if (pos < size)                                              \
            pos += snprintf(buf + pos, size - pos, __VA_ARGS__);     \
    } while (0)

static void
debugger_format_seg(char *buf, int size, int *ppos, const char *name, const x86seg *seg)
{
    int pos = *ppos;

    if (is386)
        APPEND("%s %04X  base %08X  limit %08X  access %02X%02X\n", name, seg->seg, seg->base, seg->limit, seg->ar_high, seg->access);
    else if (is286)
        APPEND("%s %04X  base %06X  limit %04X  access %02X\n", name, seg->seg, seg->base, seg->limit & 0xffff, seg->access);
    else
        APPEND("%s %04X  base %05X\n", name, seg->seg, seg->base);

    *ppos = pos;
}

void
debugger_cpu_format(char *buf, int size)
{
    static const char *names32[8] = { "EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI" };
    static const char *names16[8] = { "AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI" };
    static const struct {
        uint16_t    bit;
        const char *name;
    } flag_names[] = {
        { V_FLAG, "OF" }, { D_FLAG, "DF" }, { I_FLAG, "IF" }, { T_FLAG, "TF" },
        { N_FLAG, "SF" }, { Z_FLAG, "ZF" }, { A_FLAG, "AF" }, { P_FLAG, "PF" }, { C_FLAG, "CF" }
    };
    uint16_t flags = debugger_flags();
    int      pos   = 0;
    const char *mode;

    if (size <= 0)
        return;
    buf[0] = '\0';

    if (!is286 || !(msw & 1))
        mode = "real";
    else if (is386 && (cpu_state.eflags & VM_FLAG))
        mode = "virtual-8086";
    else
        mode = "protected";

    {
        uint16_t pc8;
        uint16_t sp8;
        uint8_t  r8[7];
        uint8_t  f8;

        if (vx0_i8080_state(&pc8, &sp8, r8, &f8)) {
            APPEND("CPU 0: 8080 emulation mode\n\n");
            APPEND("A %02X   B %02X  C %02X   D %02X  E %02X   H %02X  L %02X\n", r8[0], r8[1], r8[2], r8[3], r8[4], r8[5], r8[6]);
            APPEND("PC %04X  SP %04X  F %02X  %s %s %s %s %s\n\n", pc8, sp8, f8, (f8 & 0x80) ? "S" : "-", (f8 & 0x40) ? "Z" : "-",
                   (f8 & 0x10) ? "AC" : "--", (f8 & 0x04) ? "P" : "-", (f8 & 0x01) ? "CY" : "--");
            APPEND("Native registers, as on entering 8080 mode (IP is the 8080 PC):\n");
        }
    }

    /* Not cpu_halted: only the recompiler's loop keeps that up to date, and
       this runs the interpreter; a stop in a HLT is told of below. */
    APPEND("CPU 0: %s mode%s%s, CPL %d\n", mode, (is386 && use32) ? ", 32-bit code" : ((is286 && (msw & 1)) ? ", 16-bit code" : ""),
           in_smm ? ", SMM" : "", (is286 && (msw & 1)) ? CPL : 0);
    if (debugger_stopped() && debugger_partial)
        APPEND("Paused partway through a REP string instruction or HLT: it carries on\nwhen the processor runs again.%s\n", is286 ? "" : " On this processor IP already\npoints past it.");
    APPEND("\n");

    for (int i = 0; i < 8; i++) {
        if (is386)
            APPEND("%s %08X%s", names32[i], cpu_state.regs[i].l, (i & 1) ? "\n" : "   ");
        else
            APPEND("%s %04X%s", names16[i], cpu_state.regs[i].w, (i & 3) == 3 ? "\n" : "   ");
    }
    if (is386)
        APPEND("EIP %08X   EFLAGS %08X\n", debugger_ip(), ((uint32_t) cpu_state.eflags << 16) | flags);
    else
        APPEND("IP %04X   FLAGS %04X\n", debugger_ip() & 0xffff, flags);

    APPEND("     ");
    for (unsigned i = 0; i < (sizeof(flag_names) / sizeof(flag_names[0])); i++)
        APPEND("%s ", (flags & flag_names[i].bit) ? flag_names[i].name : "--");
    if (is286)
        APPEND("IOPL=%d%s", (flags >> 12) & 3, (flags & NT_FLAG) ? " NT" : "");
    if (is386)
        APPEND("%s%s%s", (cpu_state.eflags & VM_FLAG) ? " VM" : "", (cpu_state.eflags & RF_FLAG) ? " RF" : "",
               (cpu_state.eflags & AC_FLAG) ? " AC" : "");
    APPEND("\n\n");

    debugger_format_seg(buf, size, &pos, "CS", &cpu_state.seg_cs);
    debugger_format_seg(buf, size, &pos, "SS", &cpu_state.seg_ss);
    debugger_format_seg(buf, size, &pos, "DS", &cpu_state.seg_ds);
    debugger_format_seg(buf, size, &pos, "ES", &cpu_state.seg_es);
    if (is386) {
        debugger_format_seg(buf, size, &pos, "FS", &cpu_state.seg_fs);
        debugger_format_seg(buf, size, &pos, "GS", &cpu_state.seg_gs);
    }

    if (is286) {
        APPEND("\n");
        APPEND("GDTR base %08X  limit %04X\n", gdt.base, gdt.limit & 0xffff);
        APPEND("IDTR base %08X  limit %04X\n", idt.base, idt.limit & 0xffff);
        APPEND("LDTR %04X  base %08X  limit %08X\n", ldt.seg, ldt.base, ldt.limit);
        APPEND("TR   %04X  base %08X  limit %08X\n", tr.seg, tr.base, tr.limit);
        APPEND("\n");
        if (is386) {
            APPEND("CR0 %08X  CR2 %08X\nCR3 %08X  CR4 %08X\n", cr0, cr2, cr3, cr4);
            APPEND("DR0 %08X  DR1 %08X  DR2 %08X  DR3 %08X\nDR6 %08X  DR7 %08X\n", dr[0], dr[1], dr[2], dr[3], dr[6], dr[7]);
        } else
            APPEND("MSW %04X\n", msw);
    }
}

int
debugger_register(const char *name, uint32_t *val, uint32_t *seg_base)
{
    static const char *r32[8]  = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    static const char *r16[8]  = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di" };
    static const char *segs[6] = { "cs", "ss", "ds", "es", "fs", "gs" };
    const x86seg      *seg_regs[6] = { &cpu_state.seg_cs, &cpu_state.seg_ss, &cpu_state.seg_ds,
                                       &cpu_state.seg_es, &cpu_state.seg_fs, &cpu_state.seg_gs };

    for (int i = 0; i < 8; i++) {
        if (is386 && !strcmp(name, r32[i])) {
            *val = cpu_state.regs[i].l;
            return 1;
        }
        if (!strcmp(name, r16[i])) {
            *val = cpu_state.regs[i].w;
            return 1;
        }
    }
    if (!strcmp(name, "ip") || (is386 && !strcmp(name, "eip"))) {
        *val = debugger_ip();
        if (name[0] == 'i')
            *val &= 0xffff;
        return 1;
    }
    for (int i = 0; i < (is386 ? 6 : 4); i++) {
        if (!strcmp(name, segs[i])) {
            *val = seg_regs[i]->seg;
            if (seg_base)
                *seg_base = seg_regs[i]->base;
            return 1;
        }
    }

    return 0;
}
