/* SPDX-License-Identifier: GPL-2.0-or-later
 * Register access for the Qt debugger. Caller holds the emulator lock.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include <86box/mem.h>
#include <86box/debugger.h>

extern void cpu_386_flags_extract(void);

static uint32_t
debugger_ip(void)
{
    debugger_cpu_info_t info;
    debugger_cpu_info(&info);
    return info.ip;
}
/* Recompute the interpreter/recompiler's derived segment state after an
   out-of-band cache edit. No guest instruction or exception is executed. */
static void
debugger_segment_changed(x86seg *s)
{
    s->checked = 0;
    if ((s->access & 0x1c) == 0x14) {
        s->limit_low  = s->limit + 1;
        s->limit_high = (s->ar_high & 0x40) ? UINT32_MAX : 0xffff;
    } else {
        s->limit_low  = 0;
        s->limit_high = s->limit;
    }
    if (s == &cpu_state.seg_cs) {
        use32          = is386 && (s->ar_high & 0x40) ? 0x300 : 0;
        cpu_cur_status = (cpu_cur_status & ~CPU_STATUS_USE32) | (use32 ? CPU_STATUS_USE32 : 0);
        if (!is286)
            flush_prefetch_808x();
    }
    if (s == &cpu_state.seg_ss) {
        stack32        = is386 && !!(s->ar_high & 0x40);
        cpu_cur_status = (cpu_cur_status & ~CPU_STATUS_STACK32) | (stack32 ? CPU_STATUS_STACK32 : 0);
    }
    const int flat_bit = s == &cpu_state.seg_ds ? CPU_STATUS_NOTFLATDS
        : s == &cpu_state.seg_es                ? CPU_STATUS_NOTFLATES
        : s == &cpu_state.seg_ss                ? CPU_STATUS_NOTFLATSS
                                                : 0;
    cpu_cur_status &= ~flat_bit;
    if (s->base || s->limit_low || s->limit_high != UINT32_MAX)
        cpu_cur_status |= flat_bit;
    flushmmucache();
}

static int
debugger_set_segment(x86seg *s, uint16_t selector, int system)
{
    if (!system && (!is286 || !(msw & 1) || (cpu_state.eflags & VM_FLAG))) {
        s->seg  = selector;
        s->base = (uint32_t) selector << 4;
        /* Real-mode loads retain hidden limits, as the processor does. */
        debugger_segment_changed(s);
        return 1;
    }
    if (!(selector & ~3)) {
        if (s == &cpu_state.seg_cs || s == &cpu_state.seg_ss || s == &tr)
            return 0;
        s->seg     = selector;
        s->base    = UINT32_MAX;
        s->limit   = 0;
        s->access  = 0;
        s->ar_high = 0;
        debugger_segment_changed(s);
        return 1;
    }
    const x86seg *table = selector & 4 ? &ldt : &gdt;
    if ((system && (selector & 4)) || ((selector & ~7) + 7) > table->limit)
        return 0;
    uint8_t bytes[8], cls[8];
    debugger_mem_read(DEBUGGER_SPACE_LINEAR, table->base + (selector & ~7), 8, bytes, cls);
    for (int i = 0; i < 8; i++) {
        if (cls[i] != DEBUGGER_MEM_RAM && cls[i] != DEBUGGER_MEM_ROM)
            return 0;
    }
    const uint8_t access = bytes[5];
    const int     type   = access & 15;
    if (!(access & 0x80) || (!!(access & 0x10) == !!system))
        return 0;
    if (s == &cpu_state.seg_cs && !(type & 8))
        return 0;
    if (s == &cpu_state.seg_ss && (type & 0xa) != 2)
        return 0;
    if (s == &ldt && type != 2)
        return 0;
    if (s == &tr && type != 1 && type != 3 && !(is386 && (type == 9 || type == 11)))
        return 0;
    s->base    = bytes[2] | (bytes[3] << 8) | (bytes[4] << 16);
    s->limit   = bytes[0] | (bytes[1] << 8);
    s->ar_high = is386 ? bytes[6] : 0;
    if (is386) {
        s->base |= (uint32_t) bytes[7] << 24;
        s->limit |= (uint32_t) (bytes[6] & 15) << 16;
        if (bytes[6] & 0x80)
            s->limit = (s->limit << 12) | 0xfff;
    }
    s->access = access;
    s->seg    = selector;
    debugger_segment_changed(s);
    return 1;
}

static int
debugger_register_access(const char *name, uint32_t *val, uint32_t *seg_base, int write)
{
    static const char *r32[8]      = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    static const char *r16[8]      = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di" };
    static const char *segs[6]     = { "cs", "ss", "ds", "es", "fs", "gs" };
    x86seg            *seg_regs[6] = { &cpu_state.seg_cs, &cpu_state.seg_ss, &cpu_state.seg_ds,
                                       &cpu_state.seg_es, &cpu_state.seg_fs, &cpu_state.seg_gs };

    /* A partial REP/HLT has private interpreter state; do not corrupt it.
       NEC 8080 mode has a separate register bank, not these x86 registers. */
    if (write && (!debugger_stopped() || debugger_stopped_partial() || vx0_i8080_state(NULL, NULL, NULL, NULL)))
        return 0;

#define REG_VALUE(expr, max)             \
    do {                                 \
        if (write) {                     \
            if (*val > (uint32_t) (max)) \
                return 0;                \
            (expr) = *val;               \
        } else                           \
            *val = (expr);               \
    } while (0)

    for (int i = 0; i < 8; i++) {
        if (is386 && !strcmp(name, r32[i])) {
            REG_VALUE(cpu_state.regs[i].l, UINT32_MAX);
            return 1;
        }
        if (!strcmp(name, r16[i])) {
            REG_VALUE(cpu_state.regs[i].w, 0xffff);
            return 1;
        }
    }
    static const char *r8[8] = { "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh" };
    for (int i = 0; i < 8; i++) {
        if (!strcmp(name, r8[i])) {
            if (i < 4) {
                REG_VALUE(cpu_state.regs[i].b.l, 0xff);
            } else {
                REG_VALUE(cpu_state.regs[i - 4].b.h, 0xff);
            }
            return 1;
        }
    }
    if (!strcmp(name, "ip") || (is386 && !strcmp(name, "eip"))) {
        if (write) {
            if (name[0] == 'i' && *val > 0xffff)
                return 0;
            if (is286)
                cpu_state.pc = name[0] == 'i' ? (cpu_state.pc & 0xffff0000) | *val : *val;
            else
                set_ip_808x(*val);
        } else {
            *val = debugger_ip();
            if (name[0] == 'i')
                *val &= 0xffff;
        }
        return 1;
    }
    for (int i = 0; i < (is386 ? 6 : 4); i++) {
        if (!strcmp(name, segs[i])) {
            if (write)
                return *val <= 0xffff && debugger_set_segment(seg_regs[i], *val, 0);
            *val = seg_regs[i]->seg;
            if (seg_base)
                *seg_base = seg_regs[i]->base;
            return 1;
        }
    }

    if (!strcmp(name, "flags") || (is386 && !strcmp(name, "eflags"))) {
        if (write) {
            if (name[0] == 'f' && *val > 0xffff)
                return 0;
            cpu_state.flags = (*val & 0xffd5) | 2;
            if (!is286)
                cpu_state.flags |= is_nec ? MD_FLAG : 0xf000;
            if (name[0] == 'e')
                cpu_state.eflags = *val >> 16;
            cpu_386_flags_extract();
            cpu_cur_status = (cpu_cur_status & ~CPU_STATUS_V86) | ((cpu_state.eflags & VM_FLAG) ? CPU_STATUS_V86 : 0);
            flushmmucache();
        } else {
            *val = debugger_flags();
            if (name[0] == 'e')
                *val |= (uint32_t) cpu_state.eflags << 16;
        }
        return 1;
    }

    /* Hidden segment caches and descriptor table registers. Values are
       literal cache contents, allowing inspection/repair of unreal mode. */
    static const char *cache_names[] = { "cs", "ss", "ds", "es", "fs", "gs", "gdtr", "idtr", "ldtr", "tr" };
    x86seg            *caches[]      = { seg_regs[0], seg_regs[1], seg_regs[2], seg_regs[3], seg_regs[4], seg_regs[5], &gdt, &idt, &ldt, &tr };
    for (int i = 0; i < 10; i++) {
        if ((i >= 4 && i <= 5 && !is386) || (i >= 6 && !is286))
            continue;
        const size_t len = strlen(cache_names[i]);
        if (strncmp(name, cache_names[i], len))
            continue;
        x86seg *s = caches[i];
        if (!name[len] && i >= 8) {
            if (write)
                return *val <= 0xffff && debugger_set_segment(s, *val, 1);
            *val = s->seg;
            return 1;
        }
        if (name[len] != '.')
            continue;
        const char *field = name + len + 1;
        if (!strcmp(field, "base")) {
            REG_VALUE(s->base, is386 ? UINT32_MAX : 0xffffff);
        } else if (!strcmp(field, "limit")) {
            REG_VALUE(s->limit, i == 6 || i == 7 || !is386 ? 0xffff : UINT32_MAX);
        } else if (!strcmp(field, "access") && i != 6 && i != 7) {
            REG_VALUE(s->access, 0xff);
        } else if (!strcmp(field, "flags") && is386 && i != 6 && i != 7) {
            REG_VALUE(s->ar_high, 0xff);
        } else
            return 0;
        if (write)
            debugger_segment_changed(s);
        return 1;
    }

    if (is286 && (!strcmp(name, "msw") || (is386 && !strcmp(name, "cr0")))) {
        if (write) {
            uint32_t value = *val;
            if (!strcmp(name, "msw")) {
                if (value > 0xffff)
                    return 0;
                value |= cr0 & 0xffff0000;
            }
            /* As a MOV to CR0 stores it: on the 386 only PG and bits 4-0
               (386 DX Microprocessor Programmer's Reference Manual, 1990,
               figure 4-3, PDF p. 113). */
            if (is386 && !is486 && !isibm486)
                value &= 0x8000001f;
            if ((value & 0x80000001) == 0x80000000)
                return 0;
            if ((value & 0x80000000) && !(cr0 & 0x80000000) && (cr4 & CR4_PAE) && pae_check_load(cr3))
                return 0;
            const uint32_t old = cr0;
            if ((value & 1) && !(old & 1))
                cpu_state.seg_cs.access &= 0x9f;
            cr0                   = value | (cpu_16bitbus ? 0x10 : 0);
            cpu_cache_int_enabled = hascache && !(cr0 & (1U << 30));
            if ((old ^ cr0) & (1U << 30)) {
                cpu_update_waitstates();
                cpu_car_update();
            }
            cpu_cur_status    = (cpu_cur_status & ~CPU_STATUS_PMODE) | ((cr0 & 1) ? CPU_STATUS_PMODE : 0);
            cpu_flush_pending = 0;
            flushmmucache();
        } else
            *val = !strcmp(name, "msw") ? msw : cr0;
        return 1;
    }
    if (is386 && !strcmp(name, "cr2")) {
        REG_VALUE(cr2, UINT32_MAX);
        return 1;
    }
    if (is386 && !strcmp(name, "cr3")) {
        if (write && (cr4 & CR4_PAE) && (cr0 & 0x80000000) && pae_check_load(*val))
            return 0;
        REG_VALUE(cr3, UINT32_MAX);
        if (write)
            flushmmucache();
        return 1;
    }
    if (is386 && cpu_has_feature(CPU_FEATURE_CR4) && !strcmp(name, "cr4")) {
        if (write) {
            if (*val & ~(cpu_CR4_mask | cpu_CR4_ignore))
                return 0;
            /* pae_check_load needs the new PAE setting only when decoding
               page tables later; it validates PDPTEs independently. */
            if ((*val & CR4_PAE) && !(cr4 & CR4_PAE) && (cr0 & 0x80000000) && pae_check_load(cr3))
                return 0;
            cr4 = *val & cpu_CR4_mask;
            flushmmucache();
        } else
            *val = cr4;
        return 1;
    }
    if (is386 && strlen(name) == 3 && name[0] == 'd' && name[1] == 'r' && name[2] >= '0' && name[2] <= '7') {
        int index = name[2] - '0';
        if (index == 4 || index == 5)
            index += 2;
        REG_VALUE(dr[index], UINT32_MAX);
        if (write && index == 7)
            cpu_dr7_update();
        return 1;
    }
#undef REG_VALUE

    return 0;
}

int
debugger_register(const char *name, uint32_t *val, uint32_t *seg_base)
{
    return debugger_register_access(name, val, seg_base, 0);
}

int
debugger_register_write(const char *name, uint32_t val)
{
    return debugger_register_access(name, &val, NULL, 1);
}
