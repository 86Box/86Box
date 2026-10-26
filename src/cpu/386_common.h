/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Common 386 CPU code.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 */
#ifndef _386_COMMON_H_
#define _386_COMMON_H_

#include <stddef.h>
#include <inttypes.h>

#ifdef OPS_286_386
#    define readmemb_n(s, a, b)     readmembl_no_mmut_2386((s) + (a), b)
#    define readmemw_n(s, a, b)     readmemwl_no_mmut_2386((s) + (a), b)
#    define readmeml_n(s, a, b)     readmemll_no_mmut_2386((s) + (a), b)
#    define readmemb_n2(s, a, b)    readmembl_no_mmut_2386((s) + (a), b)
#    define readmemw_n2(s, a, b)    readmemwl_no_mmut_2386((s) + (a), b)
#    define readmeml_n2(s, a, b)    readmemll_no_mmut_2386((s) + (a), b)
#    define readmemb(s, a)          readmembl_2386((s) + (a))
#    define readmemw(s, a)          readmemwl_2386((s) + (a))
#    define readmeml(s, a)          readmemll_2386((s) + (a))
#    define readmemq(s, a)          readmemql_2386((s) + (a))

#    define writememb_n(s, a, b, v) writemembl_no_mmut_2386((s) + (a), b, v)
#    define writememw_n(s, a, b, v) writememwl_no_mmut_2386((s) + (a), b, v)
#    define writememl_n(s, a, b, v) writememll_no_mmut_2386((s) + (a), b, v)
#    define writememb(s, a, v)      writemembl_2386((s) + (a), v)
#    define writememw(s, a, v)      writememwl_2386((s) + (a), v)
#    define writememl(s, a, v)      writememll_2386((s) + (a), v)
#    define writememq(s, a, v)      writememql_2386((s) + (a), v)

#    define do_mmut_rb(s, a, b)     do_mmutranslate_2386((s) + (a), b, 1, 0)
#    define do_mmut_rw(s, a, b)     do_mmutranslate_2386((s) + (a), b, 2, 0)
#    define do_mmut_rl(s, a, b)     do_mmutranslate_2386((s) + (a), b, 4, 0)
#    define do_mmut_rb2(s, a, b)    do_mmutranslate_2386((s) + (a), b, 1, 0)
#    define do_mmut_rw2(s, a, b)    do_mmutranslate_2386((s) + (a), b, 2, 0)
#    define do_mmut_rl2(s, a, b)    do_mmutranslate_2386((s) + (a), b, 4, 0)

#    define do_mmut_wb(s, a, b)     do_mmutranslate_2386((s) + (a), b, 1, 1)
#    define do_mmut_ww(s, a, b)     do_mmutranslate_2386((s) + (a), b, 2, 1)
#    define do_mmut_wl(s, a, b)     do_mmutranslate_2386((s) + (a), b, 4, 1)
#else
/* No test for debug register breakpoints is needed on the fast paths: while
   one is armed no page is entered into the lookup tables, so every access
   takes the slow path, which makes the check (cpu_dr7_update()). */
#    define readmemb_n(s, a, b)  ((readlookup2[(uint32_t) ((s) + (a)) >> 12] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF)) ? readmembl_no_mmut((s) + (a), b) : *(uint8_t *) (readlookup2[(uint32_t) ((s) + (a)) >> 12] + (uintptr_t) ((s) + (a))))
#    define readmemw_n(s, a, b)  ((readlookup2[(uint32_t) ((s) + (a)) >> 12] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF) || (((s) + (a)) & 1)) ? readmemwl_no_mmut((s) + (a), b) : *(uint16_t *) (readlookup2[(uint32_t) ((s) + (a)) >> 12] + (uint32_t) ((s) + (a))))
#    define readmeml_n(s, a, b)  ((readlookup2[(uint32_t) ((s) + (a)) >> 12] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF) || (((s) + (a)) & 3)) ? readmemll_no_mmut((s) + (a), b) : *(uint32_t *) (readlookup2[(uint32_t) ((s) + (a)) >> 12] + (uint32_t) ((s) + (a))))
#    define readmemb_n2(s, a, b) ((readlookup2[1048576 | ((uint32_t) ((s) + (a)) >> 12)] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF)) ? readmembl_no_mmut((s) + (a), b) : *(uint8_t *) (readlookup2[1048576 | ((uint32_t) ((s) + (a)) >> 12)] + (uintptr_t) ((s) + (a))))
#    define readmemw_n2(s, a, b) ((readlookup2[1048576 | ((uint32_t) ((s) + (a)) >> 12)] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF) || (((s) + (a)) & 1)) ? readmemwl_no_mmut((s) + (a), b) : *(uint16_t *) (readlookup2[1048576 | ((uint32_t) ((s) + (a)) >> 12)] + (uint32_t) ((s) + (a))))
#    define readmeml_n2(s, a, b) ((readlookup2[1048576 | ((uint32_t) ((s) + (a)) >> 12)] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF) || (((s) + (a)) & 3)) ? readmemll_no_mmut((s) + (a), b) : *(uint32_t *) (readlookup2[1048576 | ((uint32_t) ((s) + (a)) >> 12)] + (uint32_t) ((s) + (a))))
#    define readmemb(s, a)       ((readlookup2[(uint32_t) ((s) + (a)) >> 12] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF)) ? readmembl((s) + (a)) : *(uint8_t *) (readlookup2[(uint32_t) ((s) + (a)) >> 12] + (uintptr_t) ((s) + (a))))
#    define readmemw(s, a)       ((readlookup2[(uint32_t) ((s) + (a)) >> 12] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF) || (((s) + (a)) & 1)) ? readmemwl((s) + (a)) : *(uint16_t *) (readlookup2[(uint32_t) ((s) + (a)) >> 12] + (uint32_t) ((s) + (a))))
#    define readmeml(s, a)       ((readlookup2[(uint32_t) ((s) + (a)) >> 12] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF) || (((s) + (a)) & 3)) ? readmemll((s) + (a)) : *(uint32_t *) (readlookup2[(uint32_t) ((s) + (a)) >> 12] + (uint32_t) ((s) + (a))))
#    define readmemq(s, a)       ((readlookup2[(uint32_t) ((s) + (a)) >> 12] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF) || (((s) + (a)) & 7)) ? readmemql((s) + (a)) : *(uint64_t *) (readlookup2[(uint32_t) ((s) + (a)) >> 12] + (uintptr_t) ((s) + (a))))

#    define writememb_n(s, a, b, v)                                                                    \
        if (writelookup2[(uint32_t) ((s) + (a)) >> 12] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF)) \
            writemembl_no_mmut((s) + (a), b, v);                                                       \
        else                                                                                           \
            *(uint8_t *) (writelookup2[(uint32_t) ((s) + (a)) >> 12] + (uintptr_t) ((s) + (a))) = v
#    define writememw_n(s, a, b, v)                                                                                         \
        if (writelookup2[(uint32_t) ((s) + (a)) >> 12] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF) || (((s) + (a)) & 1)) \
            writememwl_no_mmut((s) + (a), b, v);                                                                            \
        else                                                                                                                \
            *(uint16_t *) (writelookup2[(uint32_t) ((s) + (a)) >> 12] + (uintptr_t) ((s) + (a))) = v
#    define writememl_n(s, a, b, v)                                                                                         \
        if (writelookup2[(uint32_t) ((s) + (a)) >> 12] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF) || (((s) + (a)) & 3)) \
            writememll_no_mmut((s) + (a), b, v);                                                                            \
        else                                                                                                                \
            *(uint32_t *) (writelookup2[(uint32_t) ((s) + (a)) >> 12] + (uintptr_t) ((s) + (a))) = v
#    define writememb(s, a, v)                                                                         \
        if (writelookup2[(uint32_t) ((s) + (a)) >> 12] == (uintptr_t) LOOKUP_INV || (s) == 0xFFFFFFFF) \
            writemembl((s) + (a), v);                                                                  \
        else                                                                                           \
            *(uint8_t *) (writelookup2[(uint32_t) ((s) + (a)) >> 12] + (uintptr_t) ((s) + (a))) = v
#    define writememw(s, a, v)                                                                                              \
        if (writelookup2[(uint32_t) ((s) + (a)) >> 12] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF) || (((s) + (a)) & 1)) \
            writememwl((s) + (a), v);                                                                                       \
        else                                                                                                                \
            *(uint16_t *) (writelookup2[(uint32_t) ((s) + (a)) >> 12] + (uintptr_t) ((s) + (a))) = v
#    define writememl(s, a, v)                                                                                              \
        if (writelookup2[(uint32_t) ((s) + (a)) >> 12] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF) || (((s) + (a)) & 3)) \
            writememll((s) + (a), v);                                                                                       \
        else                                                                                                                \
            *(uint32_t *) (writelookup2[(uint32_t) ((s) + (a)) >> 12] + (uintptr_t) ((s) + (a))) = v
#    define writememq(s, a, v)                                                                                              \
        if (writelookup2[(uint32_t) ((s) + (a)) >> 12] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF) || (((s) + (a)) & 7)) \
            writememql((s) + (a), v);                                                                                       \
        else                                                                                                                \
            *(uint64_t *) (writelookup2[(uint32_t) ((s) + (a)) >> 12] + (uintptr_t) ((s) + (a))) = v

#    define do_mmut_rb(s, a, b)                                                                       \
        if (readlookup2[(uint32_t) ((s) + (a)) >> 12] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF)) \
        do_mmutranslate((s) + (a), b, 1, 0)
#    define do_mmut_rw(s, a, b)                                                                                            \
        if (readlookup2[(uint32_t) ((s) + (a)) >> 12] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF) || (((s) + (a)) & 1)) \
        do_mmutranslate((s) + (a), b, 2, 0)
#    define do_mmut_rl(s, a, b)                                                                                            \
        if (readlookup2[(uint32_t) ((s) + (a)) >> 12] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF) || (((s) + (a)) & 3)) \
        do_mmutranslate((s) + (a), b, 4, 0)
#    define do_mmut_rb2(s, a, b)                                                                       \
        if (readlookup2[1048576 | ((uint32_t) ((s) + (a)) >> 12)] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF)) \
        do_mmutranslate((s) + (a), b, 1, 0)
#    define do_mmut_rw2(s, a, b)                                                                                            \
        if (readlookup2[1048576 | ((uint32_t) ((s) + (a)) >> 12)] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF) || (((s) + (a)) & 1)) \
        do_mmutranslate((s) + (a), b, 2, 0)
#    define do_mmut_rl2(s, a, b)                                                                                            \
        if (readlookup2[1048576 | ((uint32_t) ((s) + (a)) >> 12)] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF) || (((s) + (a)) & 3)) \
        do_mmutranslate((s) + (a), b, 4, 0)

#    define do_mmut_wb(s, a, b)                                                                        \
        if (writelookup2[(uint32_t) ((s) + (a)) >> 12] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF)) \
        do_mmutranslate((s) + (a), b, 1, 1)
#    define do_mmut_ww(s, a, b)                                                                                             \
        if (writelookup2[(uint32_t) ((s) + (a)) >> 12] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF) || (((s) + (a)) & 1)) \
        do_mmutranslate((s) + (a), b, 2, 1)
#    define do_mmut_wl(s, a, b)                                                                                             \
        if (writelookup2[(uint32_t) ((s) + (a)) >> 12] == (uintptr_t) LOOKUP_INV || ((s) == 0xFFFFFFFF) || (((s) + (a)) & 3)) \
        do_mmutranslate((s) + (a), b, 4, 1)
#endif

#define check_io_perm(port, size)                                    \
    if (msw & 1 && ((CPL > IOPL) || (cpu_state.eflags & VM_FLAG))) { \
        int tempi = checkio(port, (1 << size) - 1);                  \
        if (cpu_state.abrt)                                          \
            return 1;                                                \
        if (tempi) {                                                 \
            if (cpu_state.eflags & VM_FLAG)                          \
                x86gpf_expected(NULL, 0);                            \
            else                                                     \
                x86gpf(NULL, 0);                                     \
            return 1;                                                \
        }                                                            \
    }

/* An operand outside its segment's limit: #SS(0) through SS, #GP(0)
   through the others, in every mode ("Protected Mode Exceptions" and
   "Real-Address Mode Exceptions" of each instruction, 243191). */
#define SEG_LIMIT_FAULT(chseg)              \
    do {                                    \
        if ((chseg) == &cpu_state.seg_ss)   \
            x86ss(NULL, 0);                 \
        else                                \
            x86gpf("Limit check", 0);       \
    } while (0)

/* Data that may not be written: read-only data, or code in protected mode
   (#GP(0)). CHECK_WRITE_COMMON's type test, made before an instruction
   reads what it will write, so one that ends up writing nothing (a shift
   or rotate by 0) faults all the same, as the compiled code does. */
#define SEG_NOT_WRITABLE(seg) \
    (!((seg)->access & 2) || ((msw & 1) && !(cpu_state.eflags & VM_FLAG) && ((seg)->access & 8)))

#define SEG_CHECK_READ(seg)                  \
    do {                                     \
        if ((seg)->base == 0xffffffff) {     \
            x86gpf("Segment can't read", 0); \
            return 1;                        \
        }                                    \
    } while (0)

#define SEG_CHECK_READ_REP(seg)              \
    if ((seg)->base == 0xffffffff) {         \
        x86gpf("Segment can't read", 0);     \
        break;                               \
    }

#define SEG_CHECK_WRITE(seg)                                          \
    do {                                                              \
        if (((seg)->base == 0xffffffff) || SEG_NOT_WRITABLE(seg)) {   \
            x86gpf("Segment can't write", 0);                         \
            return 1;                                                 \
        }                                                             \
    } while (0)

#define SEG_CHECK_WRITE_REP(seg)                                  \
    if (((seg)->base == 0xffffffff) || SEG_NOT_WRITABLE(seg)) {   \
        x86gpf("Segment can't write", 0);                         \
        break;                                                    \
    }

#define CHECK_READ(chseg, low, high)                                                  \
    if ((msw & 1) && !(cpu_state.eflags & VM_FLAG) && (((chseg)->access & 10) == 8)) { \
        x86gpf("Execute-only (READ)", 0);                                             \
        return 1;                                                                     \
    }                                                                                 \
    if ((low < (chseg)->limit_low) || (high > (chseg)->limit_high)) {                 \
        SEG_LIMIT_FAULT(chseg);                                                       \
        return 1;                                                                     \
    }                                                                                 \
    if (msw & 1 && !(cpu_state.eflags & VM_FLAG) && !((chseg)->access & 0x80)) {                                                                       \
        if ((chseg) == &cpu_state.seg_ss)                                                                                                              \
            x86ss(NULL, (chseg)->seg & 0xfffc);                                                                                                        \
        else                                                                                                                                           \
            x86np("Read from seg not present", (chseg)->seg & 0xfffc);                                                                                 \
        return 1;                                                                                                                                      \
    }

/* A memory operand of n bytes, read by its handler directly rather than
   through geteab/w/l/q: the whole of it is checked before any of it is
   read, as CHECK_READ checks a range. */
#define CHECK_READ_EA(n) CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + (n) - 1)

/* Both ends of the n bytes from eaaddr present and readable (n is at most
   512, so two pages at most), before an instruction loading state from them
   (FLDENV, FRSTOR, FXRSTOR) changes any of it: a page fault part way through
   leaves the FPU as it was. "A program-state change does not normally
   accompany a page-fault exception" (SDM 1999 vol. 3, 5.12). */
#define PROBE_READ_EA(n)                                               \
    do {                                                               \
        if (cr0 & 0x80000000) {                                        \
            const uint32_t probe_lo_ = easeg + cpu_state.eaaddr;       \
            const uint32_t probe_hi_ = probe_lo_ + (n) - 1;            \
                                                                       \
            mmutranslatereal(probe_lo_, 0);                            \
            if (!cpu_state.abrt && ((probe_lo_ ^ probe_hi_) & ~0xfff)) \
                mmutranslatereal(probe_hi_, 0);                        \
            if (cpu_state.abrt)                                        \
                return 1;                                              \
        }                                                              \
    } while (0)

#define CHECK_READ_REP(chseg, low, high)                                         \
    if ((low < (chseg)->limit_low) || (high > (chseg)->limit_high)) {            \
        SEG_LIMIT_FAULT(chseg);                                                  \
        break;                                                                   \
    }                                                                            \
    if (msw & 1 && !(cpu_state.eflags & VM_FLAG) && !((chseg)->access & 0x80)) { \
        if ((chseg) == &cpu_state.seg_ss)                                        \
            x86ss(NULL, (chseg)->seg & 0xfffc);                                  \
        else                                                                     \
            x86np("Read from seg not present", (chseg)->seg & 0xfffc);           \
        break;                                                                   \
    }

#define CHECK_WRITE_COMMON(chseg, low, high)                          \
    if (SEG_NOT_WRITABLE(chseg)) {                                    \
        x86gpf("Segment can't write", 0);                             \
        return 1;                                                     \
    }                                                                 \
    if ((low < (chseg)->limit_low) || (high > (chseg)->limit_high)) { \
        SEG_LIMIT_FAULT(chseg);                                       \
        return 1;                                                     \
    }                                                                 \
    if (msw & 1 && !(cpu_state.eflags & VM_FLAG) && !((chseg)->access & 0x80)) {                                                                                         \
        if ((chseg) == &cpu_state.seg_ss)                                                                                                                                \
            x86ss(NULL, (chseg)->seg & 0xfffc);                                                                                                                          \
        else                                                                                                                                                             \
            x86np("Write to seg not present", (chseg)->seg & 0xfffc);                                                                                                    \
        return 1;                                                                                                                                                        \
    }

#define CHECK_WRITE(chseg, low, high) \
    CHECK_WRITE_COMMON(chseg, low, high)

/* A memory operand of n bytes written by its handler directly rather than
   through seteab/w/l/q: the whole of it is checked before any of it is
   written. */
#define CHECK_WRITE_EA(n) CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + (n) - 1)

#define CHECK_WRITE_2OP(chseg, low, high, low2, high2)                                                                                \
    if (SEG_NOT_WRITABLE(chseg)) {                                                                                                    \
        x86gpf("Segment can't write", 0);                                                                                             \
        return 1;                                                                                                                     \
    }                                                                                                                                 \
    if ((low < (chseg)->limit_low) || (high > (chseg)->limit_high) || (low2 < (chseg)->limit_low) || (high2 > (chseg)->limit_high)) { \
        SEG_LIMIT_FAULT(chseg);                                                                                                       \
        return 1;                                                                                                                     \
    }                                                                                                                                 \
    if (msw & 1 && !(cpu_state.eflags & VM_FLAG) && !((chseg)->access & 0x80)) {                                                                                         \
        if ((chseg) == &cpu_state.seg_ss)                                                                                                                                \
            x86ss(NULL, (chseg)->seg & 0xfffc);                                                                                                                          \
        else                                                                                                                                                             \
            x86np("Write to seg not present", (chseg)->seg & 0xfffc);                                                                                                    \
        return 1;                                                                                                                                                        \
    }

#define CHECK_WRITE_REP(chseg, low, high)                             \
    if (SEG_NOT_WRITABLE(chseg)) {                                    \
        x86gpf("Segment can't write (REP)", 0);                       \
        break;                                                        \
    }                                                                 \
    if ((low < (chseg)->limit_low) || (high > (chseg)->limit_high)) { \
        SEG_LIMIT_FAULT(chseg);                                       \
        break;                                                        \
    }                                                                 \
    if (msw & 1 && !(cpu_state.eflags & VM_FLAG) && !((chseg)->access & 0x80)) {                                                                                         \
        if ((chseg) == &cpu_state.seg_ss)                                                                                                                                \
            x86ss(NULL, (chseg)->seg & 0xfffc);                                                                                                                          \
        else                                                                                                                                                             \
            x86np("Write (REP) to seg not present", (chseg)->seg & 0xfffc);                                                                                              \
        break;                                                                                                                                                           \
    }

#define NOTRM                                         \
    if (!(msw & 1) || (cpu_state.eflags & VM_FLAG)) { \
        x86_int(6);                                   \
        return 1;                                     \
    }

#ifdef OPS_286_386
/* TODO: Introduce functions to read exec. */
static __inline uint8_t
fastreadb(uint32_t a)
{
    uint8_t ret;
    read_type = 1;
    ret = readmembl_2386(a);
    read_type = 4;
    if (cpu_state.abrt)
        return 0;
    return ret;
}

static __inline uint16_t
fastreadw(uint32_t a)
{
    uint16_t ret;
    read_type = 1;
    ret = readmemwl_2386(a);
    read_type = 4;
    if (cpu_state.abrt)
        return 0;
    return ret;
}

static __inline uint32_t
fastreadl(uint32_t a)
{
    uint32_t ret;
    read_type = 1;
    ret = readmemll_2386(a);
    read_type = 4;
    if (cpu_state.abrt)
        return 0;
    return ret;
}
#else
static __inline uint8_t
fastreadb(uint32_t a)
{
    uint8_t *t;

    if ((a >> 12) == pccache)
        return *((uint8_t *) (((uintptr_t) &pccache2[a] & 0x00000000ffffffffULL) | ((uintptr_t) &pccache2[0] & 0xffffffff00000000ULL)));

    t = getpccache(a);
    if (cpu_state.abrt)
        return 0;
    if (t == NULL)
        return readmembl(a);
    pccache  = a >> 12;
    pccache2 = t;

    return *((uint8_t *) (((uintptr_t) &pccache2[a] & 0x00000000ffffffffULL) | ((uintptr_t) &pccache2[0] & 0xffffffff00000000ULL)));
}

static __inline uint16_t
fastreadw(uint32_t a)
{
    uint8_t *t;
    uint16_t val;
    if ((a & 0xFFF) > 0xFFE) {
        val = fastreadb(a);
        val |= (fastreadb(a + 1) << 8);
        return val;
    }
    if ((a >> 12) == pccache)
        return *((uint16_t *) (((uintptr_t) &pccache2[a] & 0x00000000ffffffffULL) | ((uintptr_t) &pccache2[0] & 0xffffffff00000000ULL)));

    t = getpccache(a);
    if (cpu_state.abrt)
        return 0;
    if (t == NULL)
        return readmemwl(a);

    pccache  = a >> 12;
    pccache2 = t;

    return *((uint16_t *) (((uintptr_t) &pccache2[a] & 0x00000000ffffffffULL) | ((uintptr_t) &pccache2[0] & 0xffffffff00000000ULL)));
}

static __inline uint32_t
fastreadl(uint32_t a)
{
    uint8_t *t;
    uint32_t val;
    if ((a & 0xFFF) < 0xFFD) {
        if ((a >> 12) != pccache) {
            t = getpccache(a);
            if (cpu_state.abrt)
                return 0;
            if (t == NULL)
                return readmemll(a);
            pccache2 = t;
            pccache  = a >> 12;
        }
        
        return *((uint32_t *) (((uintptr_t) &pccache2[a] & 0x00000000ffffffffULL) | ((uintptr_t) &pccache2[0] & 0xffffffff00000000ULL)));
    }
    val = fastreadw(a);
    val |= (fastreadw(a + 2) << 16);
    return val;
}
#endif

static uint32_t get_ram_ptr_none[2];

static __inline void *
get_ram_ptr(uint32_t a)
{
    if ((a >> 12) == pccache)
        return (void *) (((uintptr_t) &pccache2[a] & 0x00000000ffffffffULL) | ((uintptr_t) &pccache2[0] & 0xffffffff00000000ULL));
    else {
        uint8_t *t = getpccache(a);
        /* Device memory has no host copy; the block that asked is dropped
           (cpu_fetch_device), so it only needs somewhere harmless to point. */
        if (t == NULL)
            return (void *) &get_ram_ptr_none;
        return (void *) (((uintptr_t) &t[a] & 0x00000000ffffffffULL) | ((uintptr_t) &t[0] & 0xffffffff00000000ULL));
    }
}

extern int opcode_has_modrm[256];
extern int opcode_length[256];
extern int opcode_min_length[256];
extern int x86_int_sw_iopl_exempt;

#ifdef OPS_286_386
static __inline uint16_t
fastreadw_fetch(uint32_t a)
{
    uint16_t ret;

    cpu_old_paging = (cpu_flush_pending == 2);
    if ((a & 0xFFF) > 0xFFE) {
        ret = fastreadb(a);
        if (!cpu_state.abrt && (opcode_length[ret & 0xff] > 1))
            ret |= ((uint16_t) fastreadb(a + 1) << 8);
    } else if (cpu_state.abrt)
        ret = 0;
    else {
        read_type = 1;
        ret = readmemwl_2386(a);
        read_type = 4;
    }
    cpu_old_paging = 0;

    return ret;
}

static __inline uint32_t
fastreadl_fetch(uint32_t a)
{
    uint32_t ret;

    if (cpu_16bitbus || ((a & 0xFFF) > 0xFFC)) {
        ret = fastreadw_fetch(a);
        if (!cpu_state.abrt && (opcode_length[ret & 0xff] > 2))
            ret |= ((uint32_t) fastreadw(a + 2) << 16);
    } else if (cpu_state.abrt)
        ret = 0;
    else {
        read_type = 1;
        cpu_old_paging = (cpu_flush_pending == 2);
        ret = readmemll_2386(a);
        cpu_old_paging = 0;
        read_type = 4;
    }

    return ret;
}
#else
static __inline uint16_t
fastreadw_fetch(uint32_t a)
{
    uint8_t *t;
    uint16_t val;
    if ((a & 0xFFF) > 0xFFE) {
        val = fastreadb(a);
        if (opcode_length[val & 0xff] > 1)
            val |= (fastreadb(a + 1) << 8);
        return val;
    }
    if ((a >> 12) == pccache)
        return *((uint16_t *) (((uintptr_t) &pccache2[a] & 0x00000000ffffffffULL) | ((uintptr_t) &pccache2[0] & 0xffffffff00000000ULL)));
    t = getpccache(a);
    if (cpu_state.abrt)
        return 0;
    if (t == NULL)
        return readmemwl(a);

    pccache  = a >> 12;
    pccache2 = t;

    return *((uint16_t *) (((uintptr_t) &pccache2[a] & 0x00000000ffffffffULL) | ((uintptr_t) &pccache2[0] & 0xffffffff00000000ULL)));

}

static __inline uint32_t
fastreadl_fetch(uint32_t a)
{
    uint8_t *t;
    uint32_t val;
    if ((a & 0xFFF) < 0xFFD) {
        if ((a >> 12) != pccache) {
            t = getpccache(a);
            if (cpu_state.abrt)
                return 0;
            if (t == NULL)
                return readmemll(a);
            pccache2 = t;
            pccache  = a >> 12;
        }
        return *((uint32_t *) (((uintptr_t) &pccache2[a] & 0x00000000ffffffffULL) | ((uintptr_t) &pccache2[0] & 0xffffffff00000000ULL)));
    }
    val = fastreadw_fetch(a);
    if (opcode_length[val & 0xff] > 2)
        val |= (fastreadw(a + 2) << 16);
    return val;
}
#endif

/* A memory operand of n bytes must lie inside its segment, whatever the
   instruction then does with it: a shift or rotate by 0 and a BT reading
   past the limit fault as a MOV does (243191, the exceptions of each
   instruction). Raises the fault and answers 1 when it doesn't. */
static __inline int
ea_outside_limit(uint32_t n)
{
    const x86seg  *s    = cpu_state.ea_seg;
    /* 64-bit so that an operand ending at or past 0xffffffff is correctly
       seen as out of range instead of silently wrapping to a low address. */
    const uint64_t high = (uint64_t) cpu_state.eaaddr + n - 1;

    if ((cpu_state.eaaddr >= s->limit_low) && (high <= s->limit_high))
        return 0;
    SEG_LIMIT_FAULT(s);
    return 1;
}

#ifdef OPS_286_386
static __inline uint8_t
getbyte(void)
{
    uint8_t ret;
    cpu_state.pc++;
    cpu_old_paging = (cpu_flush_pending == 2);
    ret = fastreadb(cs + (cpu_state.pc - 1));
    cpu_old_paging = 0;
    return ret;

}

static __inline uint16_t
getword(void)
{
    uint16_t ret;
    cpu_state.pc += 2;
    cpu_old_paging = (cpu_flush_pending == 2);
    ret = fastreadw(cs + (cpu_state.pc - 2));
    cpu_old_paging = 0;
    return ret;
}

static __inline uint32_t
getlong(void)
{
    uint32_t ret;
    cpu_state.pc += 4;
    cpu_old_paging = (cpu_flush_pending == 2);
    ret = fastreadl(cs + (cpu_state.pc - 4));
    cpu_old_paging = 0;
    return ret;
}

static __inline uint64_t
getquad(void)
{
    uint64_t ret;
    cpu_state.pc += 8;
    cpu_old_paging = (cpu_flush_pending == 2);
    ret = fastreadl(cs + (cpu_state.pc - 8)) | ((uint64_t) fastreadl(cs + (cpu_state.pc - 4)) << 32);
    cpu_old_paging = 0;
    return ret;
}

static __inline uint8_t
geteab(void)
{
    if (cpu_mod == 3)
        return (cpu_rm & 4) ? cpu_state.regs[cpu_rm & 3].b.h : cpu_state.regs[cpu_rm & 3].b.l;
    if (ea_outside_limit(1))
        return 0;
    return readmemb(easeg, cpu_state.eaaddr);
}

static __inline uint16_t
geteaw(void)
{
    if (cpu_mod == 3)
        return cpu_state.regs[cpu_rm].w;
    if (ea_outside_limit(2))
        return 0;
    return readmemw(easeg, cpu_state.eaaddr);
}

static __inline uint32_t
geteal(void)
{
    if (cpu_mod == 3)
        return cpu_state.regs[cpu_rm].l;
    if (ea_outside_limit(4))
        return 0;
    return readmeml(easeg, cpu_state.eaaddr);
}

static __inline uint64_t
geteaq(void)
{
    if (ea_outside_limit(8))
        return 0;
    return readmemq(easeg, cpu_state.eaaddr);
}

static __inline uint8_t
geteab_mem(void)
{
    if (ea_outside_limit(1))
        return 0;
    return readmemb(easeg, cpu_state.eaaddr);
}
static __inline uint16_t
geteaw_mem(void)
{
    if (ea_outside_limit(2))
        return 0;
    return readmemw(easeg, cpu_state.eaaddr);
}
static __inline uint32_t
geteal_mem(void)
{
    if (ea_outside_limit(4))
        return 0;
    return readmeml(easeg, cpu_state.eaaddr);
}

static __inline int
seteaq_cwc(void)
{
    /* All eight bytes, as compiled MOVQ checks them */
    CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, (uint64_t) cpu_state.eaaddr + 7);
    return 0;
}

static __inline void
seteaq(uint64_t v)
{
    if (seteaq_cwc())
        return;
    writememql(easeg + cpu_state.eaaddr, v);
}

#    define seteab(v)                                                                 \
        if (cpu_mod != 3) {                                                           \
            CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr); \
            writemembl_2386(easeg + cpu_state.eaaddr, v);                             \
        } else if (cpu_rm & 4)                                                        \
            cpu_state.regs[cpu_rm & 3].b.h = v;                                       \
        else                                                                          \
            cpu_state.regs[cpu_rm].b.l = v
#    define seteaw(v)                                                                     \
        if (cpu_mod != 3) {                                                               \
            CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1); \
            writememwl_2386(easeg + cpu_state.eaaddr, v);                                 \
        } else                                                                            \
            cpu_state.regs[cpu_rm].w = v
#    define seteal(v)                                                                     \
        if (cpu_mod != 3) {                                                               \
            CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3); \
            writememll_2386(easeg + cpu_state.eaaddr, v);                                 \
        } else                                                                            \
            cpu_state.regs[cpu_rm].l = v

#    define seteab_mem(v) writemembl_2386(easeg + cpu_state.eaaddr, v);
#    define seteaw_mem(v) writememwl_2386(easeg + cpu_state.eaaddr, v);
#    define seteal_mem(v) writememll_2386(easeg + cpu_state.eaaddr, v);
#else
static __inline uint8_t
getbyte(void)
{
    cpu_state.pc++;
    return fastreadb(cs + (cpu_state.pc - 1));
}

static __inline uint16_t
getword(void)
{
    cpu_state.pc += 2;
    return fastreadw(cs + (cpu_state.pc - 2));
}

static __inline uint32_t
getlong(void)
{
    cpu_state.pc += 4;
    return fastreadl(cs + (cpu_state.pc - 4));
}

static __inline uint64_t
getquad(void)
{
    cpu_state.pc += 8;
    return fastreadl(cs + (cpu_state.pc - 8)) | ((uint64_t) fastreadl(cs + (cpu_state.pc - 4)) << 32);
}

static __inline uint8_t
geteab(void)
{
    if (cpu_mod == 3)
        return (cpu_rm & 4) ? cpu_state.regs[cpu_rm & 3].b.h : cpu_state.regs[cpu_rm & 3].b.l;
    if (ea_outside_limit(1))
        return 0;
    if (eal_r)
        return *(uint8_t *) eal_r;
    return readmemb(easeg, cpu_state.eaaddr);
}

static __inline uint16_t
geteaw(void)
{
    if (cpu_mod == 3)
        return cpu_state.regs[cpu_rm].w;
    if (ea_outside_limit(2))
        return 0;
    if (eal_r)
        return *(uint16_t *) eal_r;
    return readmemw(easeg, cpu_state.eaaddr);
}

static __inline uint32_t
geteal(void)
{
    if (cpu_mod == 3)
        return cpu_state.regs[cpu_rm].l;
    if (ea_outside_limit(4))
        return 0;
    if (eal_r)
        return *eal_r;
    return readmeml(easeg, cpu_state.eaaddr);
}

static __inline uint64_t
geteaq(void)
{
    if (ea_outside_limit(8))
        return 0;
    return readmemq(easeg, cpu_state.eaaddr);
}

static __inline uint8_t
geteab_mem(void)
{
    if (ea_outside_limit(1))
        return 0;
    if (eal_r)
        return *(uint8_t *) eal_r;
    return readmemb(easeg, cpu_state.eaaddr);
}
static __inline uint16_t
geteaw_mem(void)
{
    if (ea_outside_limit(2))
        return 0;
    if (eal_r)
        return *(uint16_t *) eal_r;
    return readmemw(easeg, cpu_state.eaaddr);
}
static __inline uint32_t
geteal_mem(void)
{
    if (ea_outside_limit(4))
        return 0;
    if (eal_r)
        return *eal_r;
    return readmeml(easeg, cpu_state.eaaddr);
}

static __inline int
seteaq_cwc(void)
{
    /* All eight bytes, as compiled MOVQ checks them */
    CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, (uint64_t) cpu_state.eaaddr + 7);
    return 0;
}

static __inline void
seteaq(uint64_t v)
{
    if (seteaq_cwc())
        return;
    writememql(easeg + cpu_state.eaaddr, v);
}

#    define seteab(v)                                                                 \
        if (cpu_mod != 3) {                                                           \
            CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr); \
            if (eal_w)                                                                \
                *(uint8_t *) eal_w = v;                                               \
            else                                                                      \
                writemembl(easeg + cpu_state.eaaddr, v);                              \
        } else if (cpu_rm & 4)                                                        \
            cpu_state.regs[cpu_rm & 3].b.h = v;                                       \
        else                                                                          \
            cpu_state.regs[cpu_rm].b.l = v
#    define seteaw(v)                                                                     \
        if (cpu_mod != 3) {                                                               \
            CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1); \
            if (eal_w)                                                                    \
                *(uint16_t *) eal_w = v;                                                  \
            else                                                                          \
                writememwl(easeg + cpu_state.eaaddr, v);                                  \
        } else                                                                            \
            cpu_state.regs[cpu_rm].w = v
#    define seteal(v)                                                                     \
        if (cpu_mod != 3) {                                                               \
            CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3); \
            if (eal_w)                                                                    \
                *eal_w = v;                                                               \
            else                                                                          \
                writememll(easeg + cpu_state.eaaddr, v);                                  \
        } else                                                                            \
            cpu_state.regs[cpu_rm].l = v

#    define seteab_mem(v)           \
        if (eal_w)                  \
            *(uint8_t *) eal_w = v; \
        else                        \
            writemembl(easeg + cpu_state.eaaddr, v);
#    define seteaw_mem(v)            \
        if (eal_w)                   \
            *(uint16_t *) eal_w = v; \
        else                         \
            writememwl(easeg + cpu_state.eaaddr, v);
#    define seteal_mem(v) \
        if (eal_w)        \
            *eal_w = v;   \
        else              \
            writememll(easeg + cpu_state.eaaddr, v);
#endif

#define getbytef()          \
    ((uint8_t) (fetchdat)); \
    cpu_state.pc++
#define getwordf()           \
    ((uint16_t) (fetchdat)); \
    cpu_state.pc += 2
#define getbyte2f()              \
    ((uint8_t) (fetchdat >> 8)); \
    cpu_state.pc++
#define getword2f()               \
    ((uint16_t) (fetchdat >> 8)); \
    cpu_state.pc += 2

/* The upper EFLAGS bits, as cpu_state.eflags holds them, that this CPU
   has: RF and VM on every 386, AC from the 486, VIF and VIP with VME, ID
   with CPUID. */
static __inline uint16_t
cpu_eflags_implemented(void)
{
    uint16_t mask = RF_FLAG | VM_FLAG;

    if (is486 || isibm486)
        mask |= AC_FLAG;
    if (cpu_CR4_mask & CR4_VME)
        mask |= VIF_FLAG | VIP_FLAG;
    if (CPUID)
        mask |= VID_FLAG;

    return mask;
}

static __inline int
cpu_cr0_invalid(uint32_t value)
{
    if ((value & 0x80000001) == 0x80000000)
        return 1; /* PG requires PE. */

    /* On the 6x86 family, NW means Not Write-Back: NW=1 selects
       write-through caching even with CD=0 (94175 and 94329, table 2-4).
       The 5x86 instead uses NW=1 for write-back (94246, table 5-1).
       Intel's invalid CD/NW combination must not reject either mode. */
    if ((cpu_s->cpu_type == CPU_Cx5x86) ||
        (cpu_s->cpu_type == CPU_Cx6x86) ||
        (cpu_s->cpu_type == CPU_Cx6x86L) ||
        (cpu_s->cpu_type == CPU_Cx6x86MX))
        return 0;

    return (cpu_s->cpu_type >= CPU_i486SX) &&
           ((value & 0x60000000) == 0x20000000);
}

/* LOCK on the 486 and later: only the forms below that write memory may
   be locked, anything else is #UD (Intel486 Programmer's Reference and
   Pentium Developer's Manual vol. 3, "LOCK prefix"). BT, CMP and TEST
   don't write, so they aren't on the list. */
static __inline int
x86_lock_opcode(int is_0f, uint8_t opcode)
{
    if (is_0f)
        return (opcode == 0xab) || (opcode == 0xb3) || (opcode == 0xbb) || (opcode == 0xba) || /* BTS BTR BTC, BT group */
               ((opcode & 0xfe) == 0xb0) || ((opcode & 0xfe) == 0xc0) || (opcode == 0xc7);    /* CMPXCHG XADD CMPXCHG8B */

    return ((opcode < 0x38) && ((opcode & 0x06) == 0x00)) || /* ADD OR ADC SBB AND SUB XOR r/m,r */
           ((opcode & 0xfc) == 0x80) || ((opcode & 0xfe) == 0x86) || ((opcode & 0xfe) == 0xf6) || ((opcode & 0xfe) == 0xfe);
}

static __inline int
x86_lock_legal(int is_0f, uint8_t opcode, uint8_t modrm)
{
    int reg = (modrm >> 3) & 7;

    if (!x86_lock_opcode(is_0f, opcode) || ((modrm & 0xc0) == 0xc0))
        return 0;

    if (is_0f) {
        if (opcode == 0xba)
            return reg >= 5; /* BTS BTR BTC imm */
        if (opcode == 0xc7)
            return reg == 1; /* CMPXCHG8B */
        return 1;
    }

    switch (opcode) {
        case 0x80 ... 0x83:
            return reg != 7; /* not CMP */
        case 0xf6 ... 0xf7:
            return (reg == 2) || (reg == 3); /* NOT NEG */
        case 0xfe ... 0xff:
            return reg <= 1; /* INC DEC */
        default:
            return 1;
    }
}

#endif

/* Resume Flag handling. */
extern int rf_flag_no_clear;

int cpu_386_check_instruction_fault(void);
