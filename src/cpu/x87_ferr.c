/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The FPU's error output, FERR#, and the IRQ 13 it raises.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/pic.h>
#include "x87_sf.h"
#include "x87.h"

/* FERR#: with CR0.NE clear, an unmasked exception asserts it, and the board
   turns its rising edge into IRQ 13. It stays up until ES clears, so an
   error is reported once: fpu_ferr, which a new error (ES setting from
   clear) lowers before it raises it again. When it goes up depends on the
   processor (fpu_ferr_timing, cpu.c): at the faulting instruction, at the
   next waiting one, or for the 486 and the Pentium by the instruction and
   the exception. */

/* The exceptions of the instruction op that assert FERR# as they occur on
   the 486 (p5 clear) or the Pentium; the rest wait for the next waiting
   instruction. The 486's books (Intel486 databook 1995, Embedded Intel486
   manual, 9.2.15.1): "the stack fault, invalid operation, and denormal
   exceptions on all transcendental instructions, integer arithmetic
   instructions, FSQRT, FSCALE, FPREM(1), FXTRACT, FBLD, and FBSTP" and
   "any exceptions on store instructions", the basic arithmetic, load,
   compare and control instructions deferred. The Pentium's (vol. 1,
   5.1.21): stack fault, invalid and denormal on the transcendentals,
   FSCALE, FXTRACT, FPREM(1), FBLD, FLD extended and FRNDINT, stack fault and
   invalid on arithmetic with an integer operand, and every exception on
   FST(P), FIST(P) and FBSTP but precision. FLDENV and FRSTOR assert it as
   they load ES ("activated immediately", 486 databook), FLDCW as it unmasks
   a flag. Where neither book names an instruction: FST(P) ST(i) as the
   stores, FICOM(P) with the integer arithmetic on the Pentium and with the
   compares on the 486 (whose manual lists it among them), FLD ST(i), FILD,
   FXCH and the constants as loads. */
static uint16_t
x87_ferr_immediate(uint16_t op, int p5)
{
    const uint16_t all   = 0x3f;
    const uint16_t store = p5 ? (all & ~FPU_SW_Precision) : all;
    const uint16_t ivd   = FPU_SW_Invalid | FPU_SW_Denormal_Op; /* stack fault is IE too */
    const unsigned esc   = (op >> 8) & 7;
    const unsigned modrm = op & 0xff;
    const unsigned reg   = (modrm >> 3) & 7;

    if (modrm < 0xc0) {
        switch (esc) {
            case 1: /* FLD m32, FST(P) m32, FLDENV, FLDCW */
                return ((reg == 2) || (reg == 3)) ? store : ((reg == 4) || (reg == 5)) ? all : 0;
            case 2: /* integer arithmetic m32 */
            case 6: /* integer arithmetic m16 */
                if ((reg == 2) || (reg == 3)) /* FICOM, FICOMP */
                    return p5 ? FPU_SW_Invalid : 0;
                return p5 ? FPU_SW_Invalid : ivd;
            case 3: /* FILD m32, FIST(P) m32, FLD m80, FSTP m80 */
                return ((reg == 2) || (reg == 3) || (reg == 7)) ? store : ((reg == 5) && p5) ? ivd : 0;
            case 5: /* FLD m64, FST(P) m64, FRSTOR */
                return ((reg == 2) || (reg == 3)) ? store : (reg == 4) ? all : 0;
            case 7: /* FILD, FIST(P) m16, FBLD, FILD m64, FBSTP, FISTP m64 */
                return ((reg == 2) || (reg == 3) || (reg == 6) || (reg == 7)) ? store : (reg == 4) ? ivd : 0;
            default: /* basic arithmetic */
                return 0;
        }
    }
    /* FST(P) ST(i), and the FSTP aliases at D9 D8 and DF D0-DF */
    if (((esc == 5) && ((modrm & 0xf0) == 0xd0)) || ((esc == 1) && ((modrm & 0xf8) == 0xd8)) || ((esc == 7) && ((modrm & 0xf0) == 0xd0)))
        return store;
    if ((esc == 1) && (modrm >= 0xf0)) {
        switch (modrm) {
            case 0xf6: /* FDECSTP */
            case 0xf7: /* FINCSTP */
                return 0;
            case 0xfa: /* FSQRT: the 486 names it, the Pentium has it basic arithmetic */
                return p5 ? 0 : ivd;
            case 0xfc: /* FRNDINT: the Pentium names it, the 486 does not */
                return p5 ? ivd : 0;
            default: /* the transcendentals, FXTRACT, FPREM(1), FSCALE */
                return ivd;
        }
    }
    return 0;
}

/* FERR# up, and IRQ 13 with it: not with CR0.NE set, which reports the
   error as #MF, nor while IGNNE# holds it off ("the processor disregards
   the exception, does not assert FERR#", SDM 1999 vol. 1, E.2.1.1) */
void
x87_ferr_assert(void)
{
    /* An 8087's or 80187's error is its own INT output (the PC's NMI), which
       its WAIT raises */
    if ((cr0 & 0x20) || fpu_ignne || fpu_ferr || (fpu_type == FPU_8087) || (fpu_type == FPU_80187))
        return;
    fpu_ferr = 1;
    picint(1 << 13);
}

/* ES cleared (FNCLEX, FNINIT, FNSAVE, or an environment or control word
   leaving no error): FERR# goes down, and the board's IGNNE# latch, which
   inactive FERR# clears, with it: "IGNNE# can never be active when FERR# is
   inactive" (SDM 1999 vol. 1, E.2.1.2, Figure E-1) */
void
x87_ferr_clear(void)
{
    fpu_ferr  = 0;
    fpu_ignne = 0;
}

/* FXRSTOR loads an error without reporting it: "FXRSTOR does not flush
   pending x87-FP exceptions. To check and raise exceptions when loading a
   new operating environment, use FWAIT after FXRSTOR" (SDM 1999 vol. 2,
   FXRSTOR). A newly loaded one lowers FERR#, and the next waiting
   instruction, finding it pending, asserts it. */
void
x87_ferr_loaded(int was_es, int es_now)
{
    if (!es_now)
        x87_ferr_clear();
    else if (!was_es)
        fpu_ferr = 0;
}

/* An unmasked exception has set ES (was_es: it was set already) in the
   instruction op: a new error lowers FERR#, and it goes up now where the
   processor asserts it at the fault. */
void
x87_ferr_raised(int was_es, uint16_t unmasked, uint16_t op)
{
    int now;

    if (!was_es)
        fpu_ferr = 0;
    switch (fpu_ferr_timing) {
        case FPU_FERR_AT_FAULT:
            now = 1;
            break;
        case FPU_FERR_486:
            now = !!(unmasked & x87_ferr_immediate(op & 0x7ff, 0));
            break;
        case FPU_FERR_P5:
            now = !!(unmasked & x87_ferr_immediate(op & 0x7ff, 1));
            break;
        default:
            now = 0;
            break;
    }
    if (now)
        x87_ferr_assert();
}

/* A no-wait instruction meeting an error FERR# has not reported asserts it,
   and goes on: "In the event of a pending unmasked floating-point exception
   the FNINIT, FNCLEX, FNSTENV, FNSAVE, FNSTSW, FNSTCW, FNENI, FNDISI, and
   FNSETPM instructions assert the FERR# pin" (Pentium vol. 1, 5.1.21); the
   486's list has no FNENI, FNDISI or FNSETPM, and the K5 and K6 assert it
   at "the next floating-point instruction" of any kind. #NM comes first. */
void
x87_ferr_nowait(uint16_t op)
{
    uint16_t sw = fpu_softfloat ? fpu_state.swd : cpu_state.npxs;

    if (!(sw & FPU_SW_Summary) || (cr0 & 0xc))
        return;
    op &= 0x7ff;
    if ((fpu_ferr_timing == FPU_FERR_486) && ((op == 0x3e0) || (op == 0x3e1) || (op == 0x3e4)))
        return;
    x87_ferr_assert();
}

/* A softfloat store's exception, held until its write is done
   (FPU_exception, FPU_store_done): a faulting write raises nothing */
static uint16_t x87_ferr_store_unmasked;
static int      x87_ferr_store_was_es;

void
x87_ferr_store_hold(int was_es, uint16_t unmasked)
{
    x87_ferr_store_unmasked = unmasked;
    x87_ferr_store_was_es   = was_es;
}

void
x87_ferr_store_done(void)
{
    if (x87_ferr_store_unmasked)
        x87_ferr_raised(x87_ferr_store_was_es, x87_ferr_store_unmasked, x87_op);
    x87_ferr_store_unmasked = 0;
}

/* The legacy FPU's ES and B as the flags its control word leaves unmasked
   make them, not as an environment image or the last control word did:
   "the value of ES (bit 7) and its reflection in the B bit (bit 15) are not
   derived from the values loaded from memory" (Intel486 databook, FLDENV
   and FRSTOR); a FLDCW unmasking a set flag "will result in a floating-
   point error condition" (486 PRM, FLDCW). was_es: ES before the load. */
void
x87_legacy_status_es(int was_es, uint16_t op)
{
    const uint16_t unmasked = cpu_state.npxs & ~cpu_state.npxc & 0x3f;

    if (unmasked) {
        cpu_state.npxs |= 0x8080;
        x87_ferr_raised(was_es, unmasked, op);
    } else {
        cpu_state.npxs &= ~0x8080;
        x87_ferr_clear();
    }
}

/* The softfloat FPU's, after an environment, a control word or an FXRSTOR
   image is loaded */
void
x87_sf_status_es(int was_es, uint16_t op)
{
    const uint16_t unmasked = fpu_state.swd & ~fpu_state.cwd & FPU_CW_Exceptions_Mask;

    if (unmasked) {
        fpu_state.swd |= (FPU_SW_Summary | FPU_SW_Backward);
        x87_ferr_raised(was_es, unmasked, op);
    } else {
        fpu_state.swd &= ~(FPU_SW_Summary | FPU_SW_Backward);
        x87_ferr_clear();
    }
}
