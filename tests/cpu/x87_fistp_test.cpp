// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the production FISTP m64 handlers with controlled memory/IRQ hooks.
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
// Test both tag formats regardless of which recompiler the main build uses.
#undef USE_NEW_DYNAREC
#ifdef X87_TEST_NEW_TAGS
#    define USE_NEW_DYNAREC
#endif
extern "C" {
#include <86box/86box.h>
#include "cpu.h"
#include "x87_sf.h"
#include "x87.h"
cpu_state_t cpu_state;
int is486 = 1, new_ne;
}

uint64_t destination;
unsigned stores, pops, irqs;
bool memory_fault;
uint8_t tempc;
union x87_td { double d; uint64_t i; };
union x87_ts { float s; uint32_t i; };
#define UNUSED(x) x
#define ST(x) cpu_state.ST[(cpu_state.TOP + (x)) & 7]
#define FP_ENTER() do { } while (0)
#define fetch_ea_16(x) ((void) (x))
#define fetch_ea_32(x) ((void) (x))
#define SEG_CHECK_READ(x) ((void) 0)
#define SEG_CHECK_WRITE(x) ((void) 0)
#define CLOCK_CYCLES_FPU(x) ((void) 0)
#define CONCURRENCY_CYCLES(x) ((void) 0)
#define FP_TAG_DEFAULT ((void) 0)
#define easeg 0
void picint(unsigned mask) { irqs |= mask; }
void seteaq(uint64_t value)
{
    if (memory_fault) {
        cpu_state.abrt = 1;
        return;
    }
    ++stores;
    destination = value;
}
void x87_pop() { ++pops; cpu_state.TOP = (cpu_state.TOP + 1) & 7; }
int64_t x87_fround(double value) { return static_cast<int64_t>(value); }
// Unused instructions in the included header are discarded by the linker.
void x87_push(double);
int16_t x87_fround16(double);
int32_t x87_fround32(double);
uint16_t geteaw();
uint32_t geteal();
uint64_t geteaq();
void seteaw(uint16_t);
void seteal(uint32_t);
uint64_t readmemq(uint32_t, uint32_t);
uint16_t readmemw(uint32_t, uint32_t);
void writememb(uint32_t, uint32_t, uint8_t);
double x87_ld80();
void x87_st80(double);
#include "x87_ops_loadstore.h"

class FastFistpTest : public testing::TestWithParam<bool> {
protected:
    void SetUp() override
    {
        cpu_state = {};
        cpu_state.TOP = 3;
        cpu_state.npxc = 0x37f;
        cpu_state.npxs = FPU_SW_C1;
#ifdef USE_NEW_DYNAREC
        cpu_state.tag[3] = TAG_EMPTY;
#else
        cpu_state.tag[3] = X87_TAG_EMPTY;
#endif
        ST(0) = 123.0; // stale data in an empty register must never be stored
        destination = UINT64_C(0x123456789abcdef0);
        stores = pops = irqs = 0;
        new_ne = 0;
        memory_fault = false;
        cr0 = 0;
    }
    int run() { return GetParam() ? FISTPiq_a32(0) : FISTPiq_a16(0); }
};

TEST_P(FastFistpTest, UnmaskedUnderflowRaisesIrqWithoutStoreOrPop)
{
    cpu_state.npxc &= ~1;
    EXPECT_EQ(run(), 0);
    EXPECT_EQ(cpu_state.npxs, 0x80c1);
    EXPECT_EQ(irqs, 1u << 13);
    EXPECT_EQ(new_ne, 0);
    EXPECT_EQ(stores, 0u);
    EXPECT_EQ(pops, 0u);
    EXPECT_EQ(cpu_state.TOP, 3);
    EXPECT_EQ(destination, UINT64_C(0x123456789abcdef0));
}
TEST_P(FastFistpTest, NativeExceptionUsesNeInsteadOfIrq)
{
    cpu_state.npxc &= ~1;
    cr0 = 0x20;
    EXPECT_EQ(run(), 0);
    EXPECT_EQ(new_ne, 1);
    EXPECT_EQ(irqs, 0u);
    EXPECT_EQ(stores, 0u);
    EXPECT_EQ(pops, 0u);
}
TEST_P(FastFistpTest, MaskedUnderflowStoresIntegerIndefiniteAndPops)
{
    EXPECT_EQ(run(), 0);
    EXPECT_EQ(destination, UINT64_C(0x8000000000000000));
    EXPECT_EQ(cpu_state.npxs, 0x41);
    EXPECT_EQ(stores, 1u);
    EXPECT_EQ(pops, 1u);
    EXPECT_EQ(irqs, 0u);
}
TEST_P(FastFistpTest, MemoryFaultPreservesStatusAndStack)
{
    memory_fault = true;
    EXPECT_EQ(run(), 1);
    EXPECT_EQ(cpu_state.npxs, FPU_SW_C1);
    EXPECT_EQ(cpu_state.TOP, 3);
    EXPECT_EQ(stores, 0u);
    EXPECT_EQ(pops, 0u);
    EXPECT_EQ(irqs, 0u);
}
TEST_P(FastFistpTest, NonemptyStoreStillConvertsAndPops)
{
#ifdef USE_NEW_DYNAREC
    cpu_state.tag[3] = TAG_VALID;
#else
    cpu_state.tag[3] = X87_TAG_VALID;
#endif
    EXPECT_EQ(run(), 0);
    EXPECT_EQ(destination, 123u);
    EXPECT_EQ(cpu_state.npxs, 0);
    EXPECT_EQ(stores, 1u);
    EXPECT_EQ(pops, 1u);
    EXPECT_EQ(irqs, 0u);
}
TEST_P(FastFistpTest, IntegerRegisterRetainsAllSixtyFourBits)
{
    cpu_state.tag[3] = TAG_UINT64;
    cpu_state.MM[3].q = UINT64_C(0x123456789abcdef0);
    EXPECT_EQ(run(), 0);
    EXPECT_EQ(destination, UINT64_C(0x123456789abcdef0));
    EXPECT_EQ(stores, 1u);
    EXPECT_EQ(pops, 1u);
    EXPECT_EQ(irqs, 0u);
}
TEST_P(FastFistpTest, InvalidNumbersDoNotReachHostIntegerConversion)
{
    for (double value : { double(INFINITY), -double(INFINITY), double(NAN), 9223372036854775808.0, -18446744073709551616.0 }) {
        for (bool masked : { false, true }) {
            SetUp();
#ifdef USE_NEW_DYNAREC
            cpu_state.tag[3] = TAG_VALID;
#else
            cpu_state.tag[3] = X87_TAG_VALID;
#endif
            ST(0) = value;
            if (!masked)
                cpu_state.npxc &= ~1;
            EXPECT_EQ(run(), 0);
            EXPECT_EQ(cpu_state.npxs, masked ? 0x0001 : 0x8081);
            EXPECT_EQ(stores, masked ? 1u : 0u);
            EXPECT_EQ(pops, masked ? 1u : 0u);
            EXPECT_EQ(irqs, masked ? 0u : 1u << 13);
            EXPECT_EQ(destination, masked ? UINT64_C(0x8000000000000000) : UINT64_C(0x123456789abcdef0));
        }
    }
}
INSTANTIATE_TEST_SUITE_P(AddressSizes, FastFistpTest, testing::Bool());
