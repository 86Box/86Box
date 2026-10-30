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
#include <86box/mem.h>
#include <86box/debugger.h>

extern void cpu_386_flags_rebuild(void);

volatile int debugger_attached = 0;
volatile int debugger_state    = DEBUGGER_RUN;

static volatile int debugger_steps       = 0;
static volatile int debugger_reason      = DEBUGGER_STOP_NONE;
/* Counts the stops, for a viewer that polls: a step can start and end
   between two looks at debugger_state. */
static volatile uint32_t debugger_stops  = 0;
static uint32_t     debugger_bp[DEBUGGER_MAX_BREAKPOINTS];
static volatile int debugger_bp_num      = 0;

static uint32_t
debugger_ip(void)
{
    return is286 ? cpu_state.pc : ip_808x();
}

static uint32_t
debugger_pc(void)
{
    return cs + debugger_ip();
}

int
debugger_instruction(void)
{
    int n = debugger_bp_num;

    if (n) {
        uint32_t pc = debugger_pc();

        for (int i = 0; i < n; i++) {
            if (debugger_bp[i] == pc) {
                debugger_reason = DEBUGGER_STOP_BREAKPOINT;
                debugger_state  = DEBUGGER_BREAK;
                debugger_stops++;
                return 1;
            }
        }
    }

    switch (debugger_state) {
        case DEBUGGER_STEP:
            if (--debugger_steps > 0)
                return 0;
            debugger_reason = DEBUGGER_STOP_STEP;
            debugger_state  = DEBUGGER_BREAK;
            debugger_stops++;
            return 1;
        case DEBUGGER_BREAK:
            return 1;
        default:
            return 0;
    }
}

void
debugger_cpu_exec(int32_t cycs)
{
    void (*exec)(int32_t cycs) = cpu_exec;

    if (!debugger_attached) {
        exec(cycs);
        return;
    }

    if (debugger_state == DEBUGGER_BREAK)
        return;

#ifdef USE_DYNAREC
    /* The recompiled blocks run many instructions between checks; the
       interpreter that shares their opcode tables stops after each one. */
    if (exec == exec386_dynarec)
        exec = exec386;
#endif

    /* Make sure a step gets to run at least one instruction. */
    if ((debugger_state == DEBUGGER_STEP) && ((cycles + cycs) <= 0))
        cycs += -(cycles + cycs) + 1;

    exec(cycs);
}

void
debugger_attach(int attach)
{
    if (attach) {
        debugger_reason   = DEBUGGER_STOP_NONE;
        debugger_state    = DEBUGGER_RUN;
        debugger_attached = 1;
    } else {
        debugger_attached = 0;
        debugger_bp_num   = 0;
        debugger_state    = DEBUGGER_RUN;
    }
}

void
debugger_pause(void)
{
    if (debugger_state != DEBUGGER_BREAK) {
        debugger_reason = DEBUGGER_STOP_PAUSE;
        debugger_state  = DEBUGGER_BREAK;
        debugger_stops++;
    }
}

void
debugger_continue(void)
{
    debugger_reason = DEBUGGER_STOP_NONE;
    debugger_state  = DEBUGGER_RUN;
}

void
debugger_step(int count)
{
    if (count < 1)
        count = 1;
    debugger_reason = DEBUGGER_STOP_NONE;
    debugger_steps  = count;
    debugger_state  = DEBUGGER_STEP;
}

int
debugger_stop_reason(void)
{
    return debugger_reason;
}

uint32_t
debugger_stop_count(void)
{
    return debugger_stops;
}

int
debugger_breakpoint_present(uint32_t linear)
{
    for (int i = 0; i < debugger_bp_num; i++) {
        if (debugger_bp[i] == linear)
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
    int n = debugger_bp_num;

    for (int i = 0; i < n; i++) {
        if (debugger_bp[i] == linear) {
            debugger_bp[i]  = debugger_bp[n - 1];
            debugger_bp_num = n - 1;
            return 0;
        }
    }

    if (n >= DEBUGGER_MAX_BREAKPOINTS)
        return -1;

    debugger_bp[n]  = linear;
    debugger_bp_num = n + 1;
    return 1;
}

int
debugger_breakpoints(uint32_t *list, int max)
{
    int n = debugger_bp_num;

    if (n > max)
        n = max;
    memcpy(list, debugger_bp, n * sizeof(uint32_t));

    return n;
}

void
debugger_breakpoints_clear(void)
{
    debugger_bp_num = 0;
}

/* Where the byte at physical address addr is, as the processor sees memory
   now (SMM included), and what it is. Only the processor's direct code
   pointer is used: it is set for a granule exactly when the granule is
   plain memory behind a mapping with a backing (mem_mapping_exec_granule()),
   so reading through it is what the processor's own fetches do. */
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
        *cls = DEBUGGER_MEM_INACCESSIBLE;
        return NULL;
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

static int
debugger_phys_read32(uint32_t addr, uint32_t *val)
{
    uint8_t cls;
    uint8_t b[4];

    for (int i = 0; i < 4; i++) {
        const uint8_t *p = debugger_phys_ptr(addr + i, &cls);
        if (!p)
            return 0;
        b[i] = *p;
    }
    *val = b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t) b[3] << 24);

    return 1;
}

static int
debugger_phys_read64(uint64_t addr, uint64_t *val)
{
    uint32_t lo;
    uint32_t hi;

    if ((addr + 7) > 0xffffffffULL)
        return 0;
    if (!debugger_phys_read32((uint32_t) addr, &lo) || !debugger_phys_read32((uint32_t) addr + 4, &hi))
        return 0;
    *val = ((uint64_t) hi << 32) | lo;

    return 1;
}

static uint32_t
debugger_a20_mask(void)
{
    return (in_smm && unmask_a20_in_smm) ? old_rammask : rammask;
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
        if (!debugger_phys_read64((pdpte & ~0xfffULL) + ((linear >> 18) & 0xff8), &pde) || !(pde & 1))
            return 0;
        pde &= 0x000000ffffffffffULL;
        if (pde & 0x80)
            p = (pde & ~0x1fffffULL) + (linear & 0x1fffff);
        else {
            if (!debugger_phys_read64((pde & ~0xfffULL) + ((linear >> 9) & 0xff8), &pte) || !(pte & 1))
                return 0;
            p = (pte & 0x000000fffffff000ULL) + (linear & 0xfff);
        }
    } else {
        uint32_t pde;
        uint32_t pte;

        if (!debugger_phys_read32((cr3 & ~0xfff) + ((linear >> 20) & 0xffc), &pde) || !(pde & 1))
            return 0;
        if ((pde & 0x80) && (cr4 & CR4_PSE)) {
            p = pde & ~0x3fffff;
            if (cpu_features & CPU_FEATURE_PSE36)
                p |= (uint64_t) (pde & 0x1e000) << 19;
            p += linear & 0x3fffff;
        } else {
            if (!debugger_phys_read32((pde & ~0xfff) + ((linear >> 10) & 0xffc), &pte) || !(pte & 1))
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

int
debugger_mem_write(int space, uint32_t addr, uint8_t val)
{
    uint8_t  cls;
    uint8_t *p;

    if (space == DEBUGGER_SPACE_LINEAR) {
        if (!debugger_translate(addr, &addr))
            return 0;
    }

    p = debugger_phys_ptr(addr, &cls);
    /* RAM only, and only where writes go to the same place reads come
       from: shadow RAM that is read-only, or reads ROM and writes RAM,
       is left alone. */
    if (!p || (cls != DEBUGGER_MEM_RAM) || (write_mapping[addr >> MEM_GRANULARITY_BITS] != read_mapping[addr >> MEM_GRANULARITY_BITS]))
        return 0;

    *p = val;
    /* Recompiled code made from this page must go. */
    mem_invalidate_range(addr, addr);
    /* The 8086 and 80186 may already hold this byte in their prefetch queue. */
    if (!is286 && ((addr - ((cs + debugger_ip()) & debugger_a20_mask())) < 8))
        flush_prefetch_808x();

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

    APPEND("CPU 0: %s mode%s%s%s, CPL %d\n", mode, (is386 && use32) ? ", 32-bit code" : ((is286 && (msw & 1)) ? ", 16-bit code" : ""),
           in_smm ? ", SMM" : "", cpu_halted ? ", halted" : "", (is286 && (msw & 1)) ? CPL : 0);
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
