// SPDX-License-Identifier: GPL-2.0-or-later
// The legacy FPU's FISTP m64 (x87_ops_loadstore.h, x87_ops_fist.h) and the
// FERR# its errors raise (x87_ferr.c): the production code, with the memory
// write and IRQ 13 observed.
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
extern "C" {
#include <86box/86box.h>
#include "cpu.h"
#include "x87_sf.h"
#include "x87.h"
cpu_state_t cpu_state; /* cr0 is its CR0 (cpu.h) */
fpu_state_t fpu_state;
int         is486 = 1, new_ne;
int         fpu_ignne, fpu_ferr, fpu_ferr_timing, fpu_type, fpu_softfloat;
}

static uint64_t destination;
static unsigned stores, pops, irqs;
static bool     memory_fault;
uint8_t         tempc;
union x87_td { double d; uint64_t i; };
union x87_ts { float s; uint32_t i; };
#define UNUSED(x)             x
#define ST(x)                 cpu_state.ST[(cpu_state.TOP + (x)) & 7]
#define FP_ENTER()            do { } while (0)
#define fetch_ea_16(x)        ((void) (x))
#define fetch_ea_32(x)        ((void) (x))
#define SEG_CHECK_READ(x)     ((void) 0)
#define SEG_CHECK_WRITE(x)    ((void) 0)
#define CHECK_WRITE_COMMON(s, l, h) ((void) 0)
#define CHECK_READ_EA(n)      ((void) 0)
#define CHECK_WRITE_EA(n)     ((void) 0)
#define CLOCK_CYCLES_FPU(x)   ((void) 0)
#define CONCURRENCY_CYCLES(x) ((void) 0)
#define FP_TAG_DEFAULT        ((void) 0)
#define FP_TAG_VALID          ((void) 0)
#define easeg                 0
using std::isnan;
using std::signbit;

extern "C" void
picint_common(uint16_t num, UNUSED(int level), int set, UNUSED(uint8_t *irq_state))
{
    if (set)
        irqs |= num;
}
void
seteaq(uint64_t value)
{
    if (memory_fault) {
        cpu_state.abrt = 1;
        return;
    }
    ++stores;
    destination = value;
}
void
x87_pop()
{
    ++pops;
    cpu_state.TOP = (cpu_state.TOP + 1) & 7;
}
// The rest of the header's handlers, which these cases never run: the
// linker drops them.
void     x87_push(double);
uint16_t geteaw();
uint32_t geteal();
uint64_t geteaq();
void     seteaw(uint16_t);
void     seteal(uint32_t);
uint64_t readmemq(uint32_t, uint32_t);
uint16_t readmemw(uint32_t, uint32_t);
void     writememb(uint32_t, uint32_t, uint8_t);
void     writememq(uint32_t, uint32_t, uint64_t);
void     writememw(uint32_t, uint32_t, uint16_t);
double   x87_ld80();
void     x87_st80(double);
#include "x87_ops_fist.h"
#include "x87_ops_loadstore.h"

static const uint64_t SENTINEL   = UINT64_C(0x123456789abcdef0);
static const uint64_t INDEFINITE = UINT64_C(0x8000000000000000);
static const uint16_t FISTP_M64  = 0x738; // DF /7

static void
set_empty()
{
    cpu_state.tag[cpu_state.TOP & 7] = TAG_EMPTY;
}
static void
set_valid(double value)
{
    cpu_state.tag[cpu_state.TOP & 7] = TAG_VALID;
    ST(0) = value;
}
static void
reset()
{
    cpu_state       = {};
    fpu_state       = {};
    cpu_state.TOP   = 3;
    cpu_state.npxc  = 0x37f;
    cpu_state.npxs  = FPU_SW_C1;
    x87_op          = FISTP_M64;
    set_empty();
    ST(0)           = 123.0; // stale data in an empty register must never be stored
    destination     = SENTINEL;
    stores = pops = irqs = 0;
    new_ne          = 0;
    memory_fault    = false;
    cr0             = 0;
    fpu_ignne       = 0;
    fpu_ferr        = 0;
    fpu_ferr_timing = FPU_FERR_486;
    fpu_type        = FPU_INTERNAL;
    fpu_softfloat   = 0;
}

class FistpTest : public testing::TestWithParam<bool> {
protected:
    void SetUp() override { reset(); }
    int  run() { return GetParam() ? FISTPiq_a32(0) : FISTPiq_a16(0); }
};

// A stack fault, masked: the integer indefinite stored and popped, IE and SF
// set, C1 clear for an underflow (SDM 1999, FISTP).
TEST_P(FistpTest, EmptyMaskedStoresIndefiniteAndPops)
{
    EXPECT_EQ(run(), 0);
    EXPECT_EQ(destination, INDEFINITE);
    EXPECT_EQ(cpu_state.npxs, 0x0041);
    EXPECT_EQ(stores, 1u);
    EXPECT_EQ(pops, 1u);
    EXPECT_EQ(irqs, 0u);
}
// Unmasked: nothing stored or popped, ES and B set. On the 486 a store's
// exception asserts FERR# at once: IRQ 13 now.
TEST_P(FistpTest, EmptyUnmaskedLeavesTheStackAndRaisesFerr)
{
    cpu_state.npxc &= ~FPU_SW_Invalid;
    EXPECT_EQ(run(), 0);
    EXPECT_EQ(cpu_state.npxs, 0x80c1);
    EXPECT_EQ(stores, 0u);
    EXPECT_EQ(pops, 0u);
    EXPECT_EQ(cpu_state.TOP, 3);
    EXPECT_EQ(destination, SENTINEL);
    EXPECT_EQ(irqs, 1u << 13);
    EXPECT_EQ(fpu_ferr, 1);
}
// With CR0.NE set the error is #MF at the next waiting instruction, not IRQ 13.
TEST_P(FistpTest, EmptyUnmaskedWithNeRaisesNoIrq)
{
    cpu_state.npxc &= ~FPU_SW_Invalid;
    cr0 = 0x20;
    EXPECT_EQ(run(), 0);
    EXPECT_EQ(cpu_state.npxs, 0x80c1);
    EXPECT_EQ(irqs, 0u);
    EXPECT_EQ(new_ne, 0);
}
// IGNNE# asserted: "the processor disregards the exception, does not assert FERR#".
TEST_P(FistpTest, EmptyUnmaskedUnderIgnneRaisesNoIrq)
{
    cpu_state.npxc &= ~FPU_SW_Invalid;
    fpu_ignne = 1;
    EXPECT_EQ(run(), 0);
    EXPECT_EQ(cpu_state.npxs, 0x80c1);
    EXPECT_EQ(irqs, 0u);
}
// The K5 and K6 assert FERR# only at the next floating-point instruction.
TEST_P(FistpTest, EmptyUnmaskedOnTheK6Waits)
{
    cpu_state.npxc &= ~FPU_SW_Invalid;
    fpu_ferr_timing = FPU_FERR_DEFERRED;
    EXPECT_EQ(run(), 0);
    EXPECT_EQ(cpu_state.npxs, 0x80c1);
    EXPECT_EQ(irqs, 0u);
}
// A faulting write changes nothing: "A program-state change does not
// normally accompany a page-fault exception" (SDM 1999 vol. 3).
TEST_P(FistpTest, MaskedStackFaultWriteFaultKeepsStatusAndStack)
{
    memory_fault = true;
    EXPECT_EQ(run(), 1);
    EXPECT_EQ(cpu_state.npxs, FPU_SW_C1);
    EXPECT_EQ(cpu_state.TOP, 3);
    EXPECT_EQ(stores, 0u);
    EXPECT_EQ(pops, 0u);
    EXPECT_EQ(irqs, 0u);
}
TEST_P(FistpTest, ValidWriteFaultKeepsStatusAndStack)
{
    set_valid(123.0);
    memory_fault = true;
    EXPECT_EQ(run(), 1);
    EXPECT_EQ(cpu_state.npxs, FPU_SW_C1);
    EXPECT_EQ(cpu_state.TOP, 3);
    EXPECT_EQ(pops, 0u);
}
// A valid conversion: stored, popped, C1 cleared ("Cleared to 0 otherwise").
TEST_P(FistpTest, ValidStoresPopsAndClearsC1)
{
    set_valid(123.0);
    EXPECT_EQ(run(), 0);
    EXPECT_EQ(destination, 123u);
    EXPECT_EQ(cpu_state.npxs, 0);
    EXPECT_EQ(stores, 1u);
    EXPECT_EQ(pops, 1u);
    EXPECT_EQ(irqs, 0u);
}
// A register FILD loaded keeps all 64 bits, even where the double rounds
// to 2^63, and raises nothing.
TEST_P(FistpTest, IntegerRegisterKeepsAllSixtyFourBits)
{
    for (uint64_t value : { UINT64_C(0x123456789abcdef0), UINT64_C(0x7fffffffffffffff) }) {
        reset();
        set_valid(9223372036854775808.0);
        cpu_state.tag[cpu_state.TOP & 7] |= TAG_UINT64;
        cpu_state.MM[cpu_state.TOP & 7].q = value;
        EXPECT_EQ(run(), 0);
        EXPECT_EQ(destination, value);
        EXPECT_EQ(cpu_state.npxs, 0);
        EXPECT_EQ(pops, 1u);
    }
}
// NaN, infinity and values out of range: masked, the indefinite with IE;
// unmasked, nothing stored, ES set and FERR# at once (a store, on the 486).
TEST_P(FistpTest, InvalidNumbers)
{
    for (double value : { double(INFINITY), -double(INFINITY), double(NAN), 9223372036854775808.0,
                          -18446744073709551616.0, std::nextafter(-9223372036854775808.0, -INFINITY) }) {
        for (bool masked : { false, true }) {
            reset();
            set_valid(value);
            if (!masked)
                cpu_state.npxc &= ~FPU_SW_Invalid;
            EXPECT_EQ(run(), 0);
            EXPECT_EQ(cpu_state.npxs, masked ? 0x0001 : 0x8081);
            EXPECT_EQ(stores, masked ? 1u : 0u);
            EXPECT_EQ(pops, masked ? 1u : 0u);
            EXPECT_EQ(irqs, masked ? 0u : 1u << 13);
            EXPECT_EQ(destination, masked ? INDEFINITE : SENTINEL);
        }
    }
}
// SF is not cleared by an invalid-operand condition.
TEST_P(FistpTest, StackFaultFlagStays)
{
    set_valid(double(NAN));
    cpu_state.npxs = FPU_SW_C1 | FPU_SW_Stack_Fault;
    EXPECT_EQ(run(), 0);
    EXPECT_EQ(cpu_state.npxs, 0x0041);
}
TEST_P(FistpTest, InRangeEdges)
{
    set_valid(-9223372036854775808.0);
    EXPECT_EQ(run(), 0);
    EXPECT_EQ(destination, INDEFINITE);
    EXPECT_EQ(cpu_state.npxs, 0);
    reset();
    set_valid(9223372036854774784.0);
    EXPECT_EQ(run(), 0);
    EXPECT_EQ(destination, UINT64_C(0x7ffffffffffffc00));
    EXPECT_EQ(cpu_state.npxs, 0);
}
// RC: nearest (ties to even), down, up, chop; negative zero stores as zero
// ("Negative zero is stored in the same encoding as positive zero", 387 PRM).
TEST_P(FistpTest, RoundingControl)
{
    const uint64_t expect[4] = { 2, 2, 3, 2 };
    for (int rc = 0; rc < 4; rc++) {
        reset();
        set_valid(2.5);
        cpu_state.npxc = (cpu_state.npxc & ~0xc00) | (rc << 10);
        EXPECT_EQ(run(), 0);
        EXPECT_EQ(destination, expect[rc]);
    }
    reset();
    set_valid(-2.5);
    EXPECT_EQ(run(), 0);
    EXPECT_EQ(destination, UINT64_C(0xfffffffffffffffe));
    reset();
    set_valid(-0.0);
    EXPECT_EQ(run(), 0);
    EXPECT_EQ(destination, 0u);
}
INSTANTIATE_TEST_SUITE_P(AddressSizes, FistpTest, testing::Bool());

// FERR# by instruction and exception, the 486's books against the Pentium's.
static unsigned
raised(int timing, uint16_t op, uint16_t unmasked)
{
    reset();
    fpu_ferr_timing = timing;
    x87_ferr_raised(0, unmasked, op);
    return irqs;
}
TEST(Ferr, FourEightySixAndPentiumLists)
{
    const unsigned irq = 1u << 13;
    // FSQRT: named on the 486, basic arithmetic on the Pentium
    EXPECT_EQ(raised(FPU_FERR_486, 0x1fa, FPU_SW_Invalid), irq);
    EXPECT_EQ(raised(FPU_FERR_P5, 0x1fa, FPU_SW_Invalid), 0u);
    // FRNDINT: named on the Pentium only
    EXPECT_EQ(raised(FPU_FERR_486, 0x1fc, FPU_SW_Invalid), 0u);
    EXPECT_EQ(raised(FPU_FERR_P5, 0x1fc, FPU_SW_Invalid), irq);
    // FSIN: stack fault, invalid and denormal at once; underflow waits
    EXPECT_EQ(raised(FPU_FERR_P5, 0x1fe, FPU_SW_Denormal_Op), irq);
    EXPECT_EQ(raised(FPU_FERR_P5, 0x1fe, FPU_SW_Underflow), 0u);
    // FDIV m32: basic arithmetic, deferred on both
    EXPECT_EQ(raised(FPU_FERR_486, 0x030, FPU_SW_Zero_Div), 0u);
    EXPECT_EQ(raised(FPU_FERR_P5, 0x030, FPU_SW_Zero_Div), 0u);
    // FIDIV m16: invalid at once on both, denormal only on the 486
    EXPECT_EQ(raised(FPU_FERR_P5, 0x630, FPU_SW_Invalid), irq);
    EXPECT_EQ(raised(FPU_FERR_486, 0x630, FPU_SW_Denormal_Op), irq);
    EXPECT_EQ(raised(FPU_FERR_P5, 0x630, FPU_SW_Denormal_Op), 0u);
    // FST m32: precision at once on the 486, deferred on the Pentium
    EXPECT_EQ(raised(FPU_FERR_486, 0x110, FPU_SW_Precision), irq);
    EXPECT_EQ(raised(FPU_FERR_P5, 0x110, FPU_SW_Precision), 0u);
    EXPECT_EQ(raised(FPU_FERR_P5, 0x110, FPU_SW_Overflow), irq);
    // FST ST(i), FLDENV and FRSTOR at once
    EXPECT_EQ(raised(FPU_FERR_P5, 0x5d1, FPU_SW_Invalid), irq);
    EXPECT_EQ(raised(FPU_FERR_486, 0x120, FPU_SW_Zero_Div), irq);
    EXPECT_EQ(raised(FPU_FERR_P5, 0x520, FPU_SW_Overflow), irq);
    // The 387 and P6 at the fault, the K6 never
    EXPECT_EQ(raised(FPU_FERR_AT_FAULT, 0x030, FPU_SW_Zero_Div), irq);
    EXPECT_EQ(raised(FPU_FERR_DEFERRED, 0x738, FPU_SW_Invalid), 0u);
}
// FERR# is a level: once up, a further exception in the same error raises
// no second interrupt; an error that sets ES anew does.
TEST(Ferr, OnceAnError)
{
    reset();
    fpu_ferr_timing = FPU_FERR_AT_FAULT;
    x87_ferr_raised(0, FPU_SW_Invalid, 0x738);
    irqs = 0;
    x87_ferr_raised(1, FPU_SW_Invalid, 0x738);
    EXPECT_EQ(irqs, 0u);
    x87_ferr_raised(0, FPU_SW_Invalid, 0x738);
    EXPECT_EQ(irqs, 1u << 13);
}
// A no-wait instruction asserts FERR# for an error not yet reported; the
// 486's list has no FNENI; #NM comes first.
TEST(Ferr, NoWaitInstructions)
{
    reset();
    fpu_ferr_timing = FPU_FERR_DEFERRED;
    cpu_state.npxs  = 0x8081;
    x87_ferr_nowait(0x7e0); // FNSTSW AX
    EXPECT_EQ(irqs, 1u << 13);
    reset();
    cpu_state.npxs = 0x8081;
    x87_ferr_nowait(0x3e0); // FNENI on the 486
    EXPECT_EQ(irqs, 0u);
    reset();
    cpu_state.npxs = 0x8081;
    cr0            = 0x8; // TS
    x87_ferr_nowait(0x7e0);
    EXPECT_EQ(irqs, 0u);
}
// An 8087's errors go out by its INT pin (NMI), not IRQ 13.
TEST(Ferr, NoIrq13ForThe8087)
{
    reset();
    fpu_ferr_timing = FPU_FERR_AT_FAULT;
    fpu_type        = FPU_8087;
    x87_ferr_raised(0, FPU_SW_Invalid, 0x738);
    EXPECT_EQ(irqs, 0u);
}
// FLDCW, FLDENV and FRSTOR: ES as the unmasked flags make it, whatever the
// image or the last control word held.
TEST(Ferr, StatusEsFollowsTheMasks)
{
    reset();
    cpu_state.npxs = FPU_SW_Invalid | 0x8080;
    cpu_state.npxc = 0x37f; // IE masked
    x87_legacy_status_es(1, 0x128);
    EXPECT_EQ(cpu_state.npxs, FPU_SW_Invalid);
    EXPECT_EQ(irqs, 0u);
    cpu_state.npxc = 0x37e; // IE unmasked
    x87_legacy_status_es(0, 0x128);
    EXPECT_EQ(cpu_state.npxs, FPU_SW_Invalid | 0x8080);
    EXPECT_EQ(irqs, 1u << 13);
}
