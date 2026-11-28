/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Weitek WTL 3167 floating-point coprocessor ("Abacus"), from its
 *          data sheet (Preliminary Data, September 1988). The WTL 4167
 *          uses the same programming interface on the 486 bus (4167
 *          Advance Data, July 1989, section "3167 and 4167 Compatibility").
 *
 *          A memory-mapped device in the 121-pin socket beside the 386DX:
 *          it decodes only A31-A25 (C0000000-C1FFFFFF; by convention
 *          C0000000-C000FFFF is used), and the rest of the address is the
 *          instruction -- A15-A10 the opcode, A6-A2 Source2/Destination,
 *          A9-A7 and the byte enables Source1 (figure 36). A write executes
 *          it with the data bus as Source1 when that field is zero; a read
 *          executes a store and returns the result. 32 single-precision
 *          registers (double-precision pairs, the high word in the even
 *          one) and the process context register (PCR); IEEE arithmetic
 *          through softfloat in "fast mode" (denormals flushed to zero),
 *          and an exception interrupt ORed into the 387's IRQ 13.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/plat_unused.h>
#include <86box/weitek.h>
#include "softfloat3e/softfloat.h"

/* The opcodes (figure 37); .D is bit 5 but for the irregular ones. */
enum {
    W_ADD   = 0x00, /* T = T + S1 */
    W_LOAD  = 0x01, /* T = S1 */
    W_MUL   = 0x02, /* T = T * S1 */
    W_STOR  = 0x03, /* read: T */
    W_SUBR  = 0x04, /* T = S1 - T */
    W_DIV   = 0x05, /* T = S1 / T */
    W_MULN  = 0x06, /* T = -(T * S1) */
    W_FLOAT = 0x07, /* T = (float) integer S1 */
    W_CMPT  = 0x08, /* CC = S1 ? T, invalid on a NaN */
    W_TSTT  = 0x09, /* CC = S1 ? 0, invalid on a NaN */
    W_NEG   = 0x0a, /* T = -S1 */
    W_ABS   = 0x0b, /* T = |S1| */
    W_CMP   = 0x0c,
    W_TST   = 0x0d,
    W_AMUL  = 0x0e, /* T = |T * S1| */
    W_FIX   = 0x0f, /* integer T = S1 */
    /* .S page only */
    W_CVTSD  = 0x10, /* single T = double S1 */
    W_CVTDS  = 0x11, /* double T = single S1 */
    W_MACS   = 0x12, /* ws2 = ws2 + T * S1 */
    W_SQRTS  = 0x13, /* T = sqrt(S1) */
    W_MACDD  = 0x14, /* wd2 = wd2 + wdT * wdS1 */
    W_SUBS   = 0x15, /* T = T - S1 */
    W_LDCTX  = 0x30, /* PCR = S1 */
    W_STCTX  = 0x31, /* read: PCR */
    W_MACDS  = 0x32, /* wd2 = wd2 + (double) (T * S1) */
    W_SQRTD  = 0x33,
    W_LOADDD = 0x34, /* LOADD.D on a write, STORD.D on a read */
    W_SUBD   = 0x35
};

/* The PCR (figure 23): MOSEL 31-28, MD 27-24, the exception
   mask EM 23-16, the condition code CC 15-8, the accumulated exceptions AE
   7-0. AE (figure 25) and EM (figure 26, the same bits at 16 up). */
#define AE_IE  0x01 /* invalid operation */
#define AE_EE  0x02 /* enabled exception: reads INTR */
#define AE_ZE  0x04 /* zero divide */
#define AE_OE  0x08 /* overflow */
#define AE_UE  0x10 /* underflow */
#define AE_PE  0x20 /* precision */
#define AE_UOE 0x40 /* undefined opcode */
#define AE_DE  0x80 /* data chain, never flagged by the 3167 */
#define AE_ALL (AE_IE | AE_ZE | AE_OE | AE_UE | AE_PE | AE_UOE | AE_DE)

/* The condition code (figure 27): Z bit 14, C2 bit 10, C0 bit 8 */
#define CC_EQUAL     0x40
#define CC_LESS      0x01
#define CC_GREATER   0x00
#define CC_UNORDERED 0x45

/* The result of an invalid operation, or of any NaN operand: a NaN with
   exponent and fraction all ones, sign zero ("Invalid Operation Exception") */
#define NAN_S 0x7fffffffU
#define NAN_D 0x7fffffffffffffffULL

typedef struct weitek_t {
    int           is_4167;
    uint32_t      r[32];
    uint8_t       mosel;
    uint8_t       md;
    uint8_t       em;
    uint8_t       cc;
    uint8_t       ae;
    int           intr;
    mem_mapping_t mapping;
} weitek_t;

int weitek_3167 = 0;
int weitek_4167 = 0;

#ifdef ENABLE_WEITEK_LOG
int weitek_do_log = ENABLE_WEITEK_LOG;

static void
weitek_log(const char *fmt, ...)
{
    va_list ap;

    if (weitek_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define weitek_log(fmt, ...)
#endif

/* The socket is the 386DX's companion: offered with a CPU for that socket. */
int
weitek_cpu_family_ok(const cpu_family_t *family)
{
    return (family != NULL) && (family->package & (CPU_PKG_386DX | CPU_PKG_386DX_DESKPRO386));
}

int
weitek_fitted(void)
{
    return weitek_3167 && weitek_cpu_family_ok(cpu_f) && !machine_has_flags(machine, MACHINE_NO_FPU_SOCKET);
}

int
weitek_4167_supported(const cpu_family_t *family, int machine_id)
{
    /* The option supplies the 4167 socket on a native 486 bus. A 486
       upgrade in a 386 socket still uses the 386 bus. */
    const uint32_t packages = CPU_PKG_SOCKET1 | CPU_PKG_SOCKET3 | CPU_PKG_SOCKET3_PC330;
    uint32_t       board;

    if ((family == NULL) || machine_has_flags(machine_id, MACHINE_NO_FPU_SOCKET))
        return 0;

    /* A Socket 3 board also takes the Socket 1 parts (as cpu_is_eligible()). */
    board = machines[machine_id].cpu.package;
    if (board & CPU_PKG_SOCKET3)
        board |= CPU_PKG_SOCKET1;

    /* The Pentium OverDrive has no 4167 interface. */
    if (family->cpus[0].cpu_type == CPU_P24T)
        return 0;

    return !!(family->package & board & packages);
}

int
weitek_4167_fitted(void)
{
    return weitek_4167 && weitek_4167_supported(cpu_f, machine);
}

/* INTR: an exception accumulated in AE whose mask bit in EM is low. It is a
   level: masking the exception or clearing AE drops it ("Exception
   Handling", Systems Programmer's Section). The board ORs it with the 387's
   error into IRQ 13. */
static void
weitek_update_intr(weitek_t *dev)
{
    int intr = !!(dev->ae & ~dev->em & AE_ALL);

    if (intr == dev->intr)
        return;
    dev->intr = intr;
    if (intr)
        picint(1 << 13);
    else if (!fpu_ferr)
        picintc(1 << 13);
}

static void
weitek_raise(weitek_t *dev, uint8_t flags)
{
    dev->ae |= flags;
    weitek_update_intr(dev);
}

static uint32_t
weitek_pcr(weitek_t *dev)
{
    return ((uint32_t) dev->mosel << 28) | ((uint32_t) dev->md << 24) | ((uint32_t) dev->em << 16) | ((uint32_t) dev->cc << 8) |
           (dev->ae & ~AE_EE) | (dev->intr ? AE_EE : 0);
}

/* STCTX with T = ws31 tells the chips apart by PCR bit 9, outside the
   condition code: Weitek's 67TEST takes it clear for a 3167-HC and, with
   the undefined opcode trapping, reads bits 15-8 = 2 as a 4167-H1A. With
   any other T (67DIAG's) the PCR reads plain. */
#define PCR_ID_4167 0x00000200

/* LDCTX loads all 32 bits, and MOSEL (bits 31-28) reads back as written.
   The data sheet's MOSEL values (0000 loads MD too, 1100 all but MD, the
   rest set up the WTL 1167's chips) do not hold for the 3167 and 4167:
   Weitek's 67DIAG loads C0000000h, C1FF0001h, 80000000h and D0000000h
   and expects each back unchanged, MD included. */
static void
weitek_load_pcr(weitek_t *dev, uint32_t val)
{
    dev->mosel = val >> 28;
    dev->md    = (val >> 24) & 0x0f;
    dev->em    = val >> 16;
    dev->cc    = val >> 8;
    dev->ae    = val & ~AE_EE;
    weitek_update_intr(dev);
}

/* softfloat set up as the 3167 works: the PCR's rounding mode (RND, MD bits
   3-2: RN, RZ, RP, RM), denormal operands and results flushed to zero
   (always "fast mode"). */
static void
weitek_status(const weitek_t *dev, struct softfloat_status_t *st)
{
    static const uint8_t rnd[4] = { softfloat_round_near_even, softfloat_round_to_zero, softfloat_round_up, softfloat_round_down };

    memset(st, 0, sizeof(struct softfloat_status_t));
    st->softfloat_roundingMode            = rnd[(dev->md >> 2) & 3];
    st->softfloat_exceptionMasks          = softfloat_all_exceptions_mask;
    st->softfloat_denormals_are_zeros     = true;
    st->softfloat_flush_underflow_to_zero = true;
}

/* softfloat's flags as AE bits */
static uint8_t
weitek_flags(const struct softfloat_status_t *st)
{
    int     f = st->softfloat_exceptionFlags;
    uint8_t ae = 0;

    if (f & softfloat_flag_invalid)
        ae |= AE_IE;
    if (f & softfloat_flag_divbyzero)
        ae |= AE_ZE;
    if (f & softfloat_flag_overflow)
        ae |= AE_OE;
    if (f & softfloat_flag_underflow)
        ae |= AE_UE;
    if (f & softfloat_flag_inexact)
        ae |= AE_PE;
    return ae;
}

static int
is_nan_s(uint32_t v)
{
    return ((v & 0x7f800000) == 0x7f800000) && (v & 0x007fffff);
}

static int
is_nan_d(uint64_t v)
{
    return ((v & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL) && (v & 0x000fffffffffffffULL);
}

/* Denormals read as zero in every operation ("Fast Mode") */
static uint32_t
daz_s(uint32_t v)
{
    return ((v & 0x7f800000) == 0) ? (v & 0x80000000) : v;
}

static uint64_t
daz_d(uint64_t v)
{
    return ((v & 0x7ff0000000000000ULL) == 0) ? (v & 0x8000000000000000ULL) : v;
}

static uint64_t
get_d(const weitek_t *dev, int reg)
{
    reg &= 0x1e;
    return ((uint64_t) dev->r[reg] << 32) | dev->r[reg | 1];
}

static void
set_d(weitek_t *dev, int reg, uint64_t v)
{
    reg &= 0x1e;
    dev->r[reg]     = v >> 32;
    dev->r[reg | 1] = (uint32_t) v;
}

/* Source1: a register, or with field 0 the data bus -- for a double the bus
   is the high word and ws1 the low ("(386 data, ws1)", figure 35). */
static uint32_t
src_s(const weitek_t *dev, int f, uint32_t data)
{
    return f ? dev->r[f] : data;
}

static uint64_t
src_d(const weitek_t *dev, int f, uint32_t data)
{
    return f ? get_d(dev, f) : (((uint64_t) data << 32) | dev->r[1]);
}

static uint8_t
weitek_compare(int rel)
{
    switch (rel) {
        case softfloat_relation_less:
            return CC_LESS;
        case softfloat_relation_equal:
            return CC_EQUAL;
        case softfloat_relation_greater:
            return CC_GREATER;
        default:
            return CC_UNORDERED;
    }
}

/* A single-precision arithmetic result: any NaN operand or invalid
   operation gives the 3167's NaN with IE */
static void
put_s(weitek_t *dev, int t, uint32_t res, int nan_in, struct softfloat_status_t *st)
{
    uint8_t ae = weitek_flags(st);

    if (nan_in || (ae & AE_IE)) {
        res = NAN_S;
        ae  = AE_IE;
    }
    dev->r[t] = res;
    weitek_raise(dev, ae);
}

static void
put_d(weitek_t *dev, int t, uint64_t res, int nan_in, struct softfloat_status_t *st)
{
    uint8_t ae = weitek_flags(st);

    if (nan_in || (ae & AE_IE)) {
        res = NAN_D;
        ae  = AE_IE;
    }
    set_d(dev, t, res);
    weitek_raise(dev, ae);
}

/* FIX: to a 32-bit integer by RND, or toward zero with IRND (MD bit 1). A
   result that does not fit is an overflow (OE), as is the partial loss of
   significance a precision exception (PE). An overflow loses significance
   too: 67DIAG's FIX of -infinity expects OE and PE together. */
static void
put_fix(weitek_t *dev, int t, int32_t res, int nan_in, struct softfloat_status_t *st)
{
    int     f  = st->softfloat_exceptionFlags;
    uint8_t ae = (f & softfloat_flag_inexact) ? AE_PE : 0;

    if (nan_in)
        ae = AE_IE;
    else if (f & softfloat_flag_invalid)
        ae = AE_OE | AE_PE;
    dev->r[t] = (uint32_t) res;
    weitek_raise(dev, ae);
}

static uint8_t
fix_rounding(const weitek_t *dev, const struct softfloat_status_t *st)
{
    return (dev->md & 0x02) ? softfloat_round_to_zero : st->softfloat_roundingMode;
}

/* A bus write: execute the instruction the address holds */
static void
weitek_execute(weitek_t *dev, uint16_t addr, uint32_t data)
{
    struct softfloat_status_t st;
    int                       op = (addr >> 10) & 0x3f;
    int                       t  = (addr >> 2) & 0x1f;
    int                       f  = (((addr >> 7) & 7) << 2) | (addr & 3);
    uint32_t                  a;
    uint32_t                  b;
    uint64_t                  ad;
    uint64_t                  bd;
    int                       rel;

    weitek_status(dev, &st);

    weitek_log("Weitek: op %02X T%i F%i data %08X\n", op, t, f, data);

    switch (op) {
        /* Single precision */
        case W_ADD:
        case W_MUL:
        case W_SUBR:
        case W_DIV:
        case W_MULN:
        case W_AMUL:
        case W_SUBS:
            a = src_s(dev, f, data);
            b = dev->r[t];
            {
                int      nan = is_nan_s(a) || is_nan_s(b);
                uint32_t r;

                switch (op) {
                    case W_ADD:
                        r = f32_add(daz_s(b), daz_s(a), &st);
                        break;
                    case W_SUBR:
                        r = f32_sub(daz_s(a), daz_s(b), &st);
                        break;
                    case W_SUBS:
                        r = f32_sub(daz_s(b), daz_s(a), &st);
                        break;
                    case W_DIV:
                        r = f32_div(daz_s(a), daz_s(b), &st);
                        break;
                    default:
                        r = f32_mul(daz_s(b), daz_s(a), &st);
                        if (!(st.softfloat_exceptionFlags & softfloat_flag_invalid))
                            r = (op == W_MULN) ? (r ^ 0x80000000) : (op == W_AMUL) ? (r & 0x7fffffff) : r;
                        break;
                }
                put_s(dev, t, r, nan, &st);
            }
            break;

        case W_LOAD:
            dev->r[t] = src_s(dev, f, data);
            break;

        case W_FLOAT:
            put_s(dev, t, i32_to_f32((int32_t) src_s(dev, f, data), &st), 0, &st);
            break;

        case W_CMPT:
        case W_CMP:
            a   = src_s(dev, f, data);
            b   = dev->r[t];
            rel = f32_compare_quiet(daz_s(a), daz_s(b), &st);
            dev->cc = weitek_compare(rel);
            if ((op == W_CMPT) && (rel == softfloat_relation_unordered))
                weitek_raise(dev, AE_IE);
            break;

        case W_TSTT:
        case W_TST:
            a   = src_s(dev, f, data);
            rel = f32_compare_quiet(daz_s(a), 0, &st);
            dev->cc = weitek_compare(rel);
            if ((op == W_TSTT) && (rel == softfloat_relation_unordered))
                weitek_raise(dev, AE_IE);
            break;

        case W_NEG:
        case W_ABS:
            a = src_s(dev, f, data);
            if (is_nan_s(a))
                put_s(dev, t, 0, 1, &st);
            else if (op == W_NEG) /* 0 - S1: zero negates to +0 (67DIAG) */
                dev->r[t] = f32_sub(0, daz_s(a), &st);
            else
                dev->r[t] = daz_s(a) & 0x7fffffff;
            break;

        case W_FIX:
            a = src_s(dev, f, data);
            put_fix(dev, t, f32_to_i32(daz_s(a), fix_rounding(dev, &st), true, &st), is_nan_s(a), &st);
            break;

        case W_CVTSD:
            ad = src_d(dev, f, data);
            put_s(dev, t, f64_to_f32(daz_d(ad), &st), is_nan_d(ad), &st);
            break;

        case W_CVTDS:
            a = src_s(dev, f, data);
            put_d(dev, t, f32_to_f64(daz_s(a), &st), is_nan_s(a), &st);
            break;

        case W_MACS:
            /* The multiplier's product goes to the ALU: rounded twice */
            a = src_s(dev, f, data);
            b = dev->r[t];
            {
                uint32_t p   = f32_mul(daz_s(b), daz_s(a), &st);
                int      nan = is_nan_s(a) || is_nan_s(b) || is_nan_s(dev->r[2]);

                put_s(dev, 2, f32_add(daz_s(dev->r[2]), p, &st), nan, &st);
            }
            break;

        case W_SQRTS:
            a = src_s(dev, f, data);
            put_s(dev, t, f32_sqrt(daz_s(a), &st), is_nan_s(a), &st);
            break;

        case W_MACDD:
            ad = src_d(dev, f, data);
            bd = get_d(dev, t);
            {
                uint64_t p   = f64_mul(daz_d(bd), daz_d(ad), &st);
                uint64_t acc = get_d(dev, 2);
                int      nan = is_nan_d(ad) || is_nan_d(bd) || is_nan_d(acc);

                put_d(dev, 2, f64_add(daz_d(acc), p, &st), nan, &st);
            }
            break;

        case W_MACDS:
            /* Single inputs, double accumulator: the product is exact */
            a = src_s(dev, f, data);
            b = dev->r[t];
            {
                uint64_t acc = get_d(dev, 2);
                int      nan = is_nan_s(a) || is_nan_s(b) || is_nan_d(acc);
                uint64_t p   = f64_mul(f32_to_f64(daz_s(b), &st), f32_to_f64(daz_s(a), &st), &st);

                put_d(dev, 2, f64_add(daz_d(acc), p, &st), nan, &st);
            }
            break;

        /* Double precision */
        case W_ADD | 0x20:
        case W_MUL | 0x20:
        case W_SUBR | 0x20:
        case W_DIV | 0x20:
        case W_MULN | 0x20:
        case W_AMUL | 0x20:
        case W_SUBD:
            ad = src_d(dev, f, data);
            bd = get_d(dev, t);
            {
                int      nan = is_nan_d(ad) || is_nan_d(bd);
                uint64_t r;

                switch (op) {
                    case W_ADD | 0x20:
                        r = f64_add(daz_d(bd), daz_d(ad), &st);
                        break;
                    case W_SUBR | 0x20:
                        r = f64_sub(daz_d(ad), daz_d(bd), &st);
                        break;
                    case W_SUBD:
                        r = f64_sub(daz_d(bd), daz_d(ad), &st);
                        break;
                    case W_DIV | 0x20:
                        r = f64_div(daz_d(ad), daz_d(bd), &st);
                        break;
                    default:
                        r = f64_mul(daz_d(bd), daz_d(ad), &st);
                        if (!(st.softfloat_exceptionFlags & softfloat_flag_invalid))
                            r = (op == (W_MULN | 0x20)) ? (r ^ 0x8000000000000000ULL) :
                                (op == (W_AMUL | 0x20)) ? (r & 0x7fffffffffffffffULL) : r;
                        break;
                }
                put_d(dev, t, r, nan, &st);
            }
            break;

        case W_LOAD | 0x20:
            /* LOAD on the double page moves the one word T, as the store
               there reads it: MetaWare High C (Weitek's 67FONGV) moves a
               double as two of them, even then odd register, a double
               constant as the low word to the odd register and then the high
               word to the even one, and a single with odd registers (ws31 =
               ws25, ws2 = ws29). */
            dev->r[t] = src_s(dev, f, data);
            break;

        case W_LOADDD:
            /* LOADD.D: a register pair, or the bus with ws1 as the low word
               for an even T ("wdt <- ereg & ws1", 67MACROS.EQU). 67TEST loads
               ws1-ws6 one word at a time with it and T = 1-6. */
            if (f)
                set_d(dev, t, get_d(dev, f));
            else {
                uint32_t lo = dev->r[1];

                dev->r[t] = data;
                if (!(t & 1))
                    dev->r[t | 1] = lo;
            }
            break;

        case W_FLOAT | 0x20:
            /* the integer is one 32-bit word: a register, or the bus alone */
            set_d(dev, t, i32_to_f64((int32_t) src_s(dev, f, data)));
            break;

        case W_CMPT | 0x20:
        case W_CMP | 0x20:
            ad  = src_d(dev, f, data);
            bd  = get_d(dev, t);
            rel = f64_compare_quiet(daz_d(ad), daz_d(bd), &st);
            dev->cc = weitek_compare(rel);
            if ((op == (W_CMPT | 0x20)) && (rel == softfloat_relation_unordered))
                weitek_raise(dev, AE_IE);
            break;

        case W_TSTT | 0x20:
        case W_TST | 0x20:
            ad  = src_d(dev, f, data);
            rel = f64_compare_quiet(daz_d(ad), 0, &st);
            dev->cc = weitek_compare(rel);
            if ((op == (W_TSTT | 0x20)) && (rel == softfloat_relation_unordered))
                weitek_raise(dev, AE_IE);
            break;

        case W_NEG | 0x20:
        case W_ABS | 0x20:
            ad = src_d(dev, f, data);
            if (is_nan_d(ad))
                put_d(dev, t, 0, 1, &st);
            else if (op == (W_NEG | 0x20))
                set_d(dev, t, f64_sub(0, daz_d(ad), &st));
            else
                set_d(dev, t, daz_d(ad) & 0x7fffffffffffffffULL);
            break;

        case W_FIX | 0x20:
            ad = src_d(dev, f, data);
            put_fix(dev, t, f64_to_i32(daz_d(ad), fix_rounding(dev, &st), true, &st), is_nan_d(ad), &st);
            break;

        case W_SQRTD:
            ad = src_d(dev, f, data);
            put_d(dev, t, f64_sqrt(daz_d(ad), &st), is_nan_d(ad), &st);
            break;

        case W_LDCTX:
            weitek_load_pcr(dev, src_s(dev, f, data));
            break;

        default:
            /* The stores on a write, and the unassigned opcodes. Only the
               4167 traps opcode 3Fh: Weitek's 67TEST and 67DIAG tell a 3167
               from a 4167 by UOE after a write to FC00h. */
            if ((op == 0x3f) && !dev->is_4167)
                break;
            weitek_log("Weitek: undefined opcode %02X on a write\n", op);
            weitek_raise(dev, AE_UOE);
            break;
    }
}

/* A bus read: only the stores are allowed ("Undefined Opcode Exception") */
static uint32_t
weitek_store(weitek_t *dev, uint16_t addr)
{
    int op = (addr >> 10) & 0x3f;
    int t  = (addr >> 2) & 0x1f;

    switch (op) {
        case W_STOR:
        case W_STOR | 0x20:
        case W_LOADDD: /* STORD.D */
            return dev->r[t];
        case W_STCTX:
            return weitek_pcr(dev) | ((dev->is_4167 && (t == 31)) ? PCR_ID_4167 : 0);
        default:
            weitek_log("Weitek: undefined opcode %02X on a read\n", op);
            weitek_raise(dev, AE_UOE);
            return 0xffffffff;
    }
}

/* The bus: the low two address bits are the first enabled byte, the data
   on its byte lanes. An access the 386 splits (a doubleword or word across
   a doubleword boundary) is two bus cycles, the lower address first -- two
   instructions to the 3167 (figure 39). */
static void
weitek_bus_write(weitek_t *dev, uint32_t addr, uint32_t val, int bytes)
{
    int lane = addr & 3;

    if ((lane + bytes) > 4) {
        int first = 4 - lane;

        weitek_execute(dev, addr & 0xffff, val << (lane << 3));
        weitek_execute(dev, (addr + first) & 0xfffc, val >> (first << 3));
    } else
        weitek_execute(dev, addr & 0xffff, val << (lane << 3));
}

static uint32_t
weitek_bus_read(weitek_t *dev, uint32_t addr, int bytes)
{
    int      lane = addr & 3;
    uint32_t val  = weitek_store(dev, addr & 0xffff) >> (lane << 3);

    if ((lane + bytes) > 4) {
        int first = 4 - lane;

        val |= weitek_store(dev, (addr + first) & 0xfffc) << (first << 3);
    }
    return val;
}

static uint8_t
weitek_readb(uint32_t addr, void *priv)
{
    return weitek_bus_read((weitek_t *) priv, addr, 1);
}

static uint16_t
weitek_readw(uint32_t addr, void *priv)
{
    return weitek_bus_read((weitek_t *) priv, addr, 2);
}

static uint32_t
weitek_readl(uint32_t addr, void *priv)
{
    return weitek_bus_read((weitek_t *) priv, addr, 4);
}

static void
weitek_writeb(uint32_t addr, uint8_t val, void *priv)
{
    weitek_bus_write((weitek_t *) priv, addr, val, 1);
}

static void
weitek_writew(uint32_t addr, uint16_t val, void *priv)
{
    weitek_bus_write((weitek_t *) priv, addr, val, 2);
}

static void
weitek_writel(uint32_t addr, uint32_t val, void *priv)
{
    weitek_bus_write((weitek_t *) priv, addr, val, 4);
}

static void
weitek_reset(void *priv)
{
    weitek_t *dev = (weitek_t *) priv;

    memset(dev->r, 0, sizeof(dev->r));
    dev->mosel = 0x0;
    dev->md    = 0x01;
    dev->em    = 0xff;
    dev->cc    = 0x00;
    dev->ae    = 0x00;
    weitek_update_intr(dev);
}

static void *
weitek_init(const device_t *info)
{
    weitek_t *dev = (weitek_t *) calloc(1, sizeof(weitek_t));

    if (dev == NULL)
        return NULL;

    dev->is_4167 = (info->local == 4167);

    weitek_reset(dev);

    /* A31-A25 select it: the whole 32 MB at C0000000 */
    mem_mapping_add(&dev->mapping, 0xc0000000, 0x02000000,
                    weitek_readb, weitek_readw, weitek_readl,
                    weitek_writeb, weitek_writew, weitek_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);

    return dev;
}

static void
weitek_close(void *priv)
{
    weitek_t *dev = (weitek_t *) priv;

    if (dev->intr && !fpu_ferr)
        picintc(1 << 13);
    free(dev);
}

const device_t weitek_3167_device = {
    .name          = "Weitek WTL 3167",
    .internal_name = "weitek_3167",
    .flags         = 0,
    .local         = 0,
    .init          = weitek_init,
    .close         = weitek_close,
    .reset         = weitek_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t weitek_4167_device = {
    .name          = "Weitek WTL 4167",
    .internal_name = "weitek_4167",
    .local         = 4167,
    .init          = weitek_init,
    .close         = weitek_close,
    .reset         = weitek_reset
};
