// SPDX-License-Identifier: GPL-2.0-or-later
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <gtest/gtest.h>

extern "C" {
#include <86box/86box.h>
#include "cpu.h"
#include "x87_sf.h"
#include "x87.h"
cpu_state_t cpu_state;
}

union x87_td { double d; uint64_t i; };
static unsigned raised, underflows;
#define ST(i) cpu_state.ST[(cpu_state.TOP + (i)) & 7]
#define X87_STACK_FAULTS 1
#define X87_ST_EMPTY2(a, b) (X87_ST_EMPTY(a) || X87_ST_EMPTY(b))
using std::isinf;
using std::isnan;
using std::signbit;

static void
x87_raise_es(unsigned flag)
{
    raised |= flag;
}

static void
x87_stack_underflow(int, int)
{
    underflows++;
}

#include "x87_ops_fprem.h"

static constexpr unsigned conditions = FPU_SW_C0 | FPU_SW_C1 | FPU_SW_C2 | FPU_SW_C3;

class FpremTest : public testing::Test {
protected:
    int rounding;
    void SetUp() override
    {
        rounding = std::fegetround();
        cpu_state = {};
        cpu_state.TOP = 3;
        cpu_state.npxc = 0x37f;
        cpu_state.npxs = conditions;
        cpu_state.tag[3] = cpu_state.tag[4] = TAG_VALID;
        raised = underflows = 0;
    }
    void TearDown() override { std::fesetround(rounding); }
};

TEST_F(FpremTest, PreservesSignedZeroAndClearsConditionBits)
{
    for (int nearest = 0; nearest <= 1; nearest++) {
        for (double zero : { 0.0, -0.0 }) {
            ST(0) = zero;
            ST(1) = 3.0;
            cpu_state.npxs = conditions | FPU_SW_Precision;
            EXPECT_EQ(x87_fprem_common(nearest), 0);
            EXPECT_EQ(ST(0), 0.0);
            EXPECT_EQ(std::signbit(ST(0)), std::signbit(zero));
            EXPECT_EQ(cpu_state.npxs, FPU_SW_Precision);
        }
    }
}

TEST_F(FpremTest, InfiniteDivisorPreservesFiniteDividend)
{
    for (int nearest = 0; nearest <= 1; nearest++) {
        for (double a : { -7.5, 7.5 }) {
            for (double b : { -INFINITY, INFINITY }) {
                ST(0) = a;
                ST(1) = b;
                cpu_state.npxs = conditions;
                EXPECT_EQ(x87_fprem_common(nearest), 0);
                EXPECT_EQ(ST(0), a);
                EXPECT_EQ(cpu_state.npxs, 0);
            }
        }
    }
}

TEST_F(FpremTest, CorrectsNearestQuotientForChoppedRemainder)
{
    for (int mode : { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO }) {
        ASSERT_EQ(std::fesetround(mode), 0);
        for (int nearest = 0; nearest <= 1; nearest++) {
            for (double a : { 5.0, -5.0, 7.0, -7.0, 11.0, -11.0, 17.0, -17.0 }) {
                for (double b : { 2.0, -2.0 }) {
                    int q;
                    double rn = std::remquo(a, b, &q);
                    double rt = std::fmod(a, b);
                    q = std::abs(q);
                    if (!nearest && rt != rn)
                        q = (q - 1) & 7;
                    unsigned flags = ((q & 4) ? FPU_SW_C0 : 0) |
                                     ((q & 2) ? FPU_SW_C3 : 0) |
                                     ((q & 1) ? FPU_SW_C1 : 0);
                    ST(0) = a;
                    ST(1) = b;
                    cpu_state.npxs = conditions;
                    EXPECT_EQ(x87_fprem_common(nearest), 0);
                    EXPECT_EQ(ST(0), nearest ? rn : rt);
                    EXPECT_EQ(cpu_state.npxs, flags);
                }
            }
        }
    }
}

TEST_F(FpremTest, InvalidOperandsAreCheckedBeforeZeroFastPath)
{
    // Quiet NaNs propagate without #IA (SDM, FPREM/FPREM1); the legacy
    // helper still mishandles them, so they are not invalid-operand oracles.
    for (int nearest = 0; nearest <= 1; nearest++) {
        for (auto operands : { std::pair{ 0.0, 0.0 },
                               std::pair{ double(INFINITY), 2.0 } }) {
            for (bool masked : { false, true }) {
                ST(0) = operands.first;
                ST(1) = operands.second;
                cpu_state.npxs = conditions;
                cpu_state.npxc = masked ? 0x37f : 0x37e;
                raised = 0;
                EXPECT_EQ(x87_fprem_common(nearest), 0);
                EXPECT_EQ(cpu_state.npxs, FPU_SW_Invalid);
                if (masked)
                    EXPECT_TRUE(std::isnan(ST(0)));
                else {
                    EXPECT_EQ(raised, FPU_SW_Invalid);
                    EXPECT_EQ(ST(0), operands.first);
                }
            }
        }
    }
}

TEST_F(FpremTest, EmptyStackDoesNotTakeZeroFastPath)
{
    ST(0) = -0.0;
    ST(1) = 3.0;
    cpu_state.tag[3] = TAG_EMPTY;
    EXPECT_EQ(x87_fprem_common(0), 1);
    EXPECT_EQ(underflows, 1u);
    EXPECT_EQ(cpu_state.npxs, conditions & ~FPU_SW_C2);
}

TEST_F(FpremTest, LargeExponentDifferenceStillNeedsPartialReduction)
{
    for (int nearest = 0; nearest <= 1; nearest++) {
        ST(0) = 0x1p1000;
        ST(1) = 3.0;
        cpu_state.npxs = conditions;
        unsigned steps = 0;
        do {
            ASSERT_EQ(x87_fprem_common(nearest), 0);
            ASSERT_LT(++steps, 100u);
        } while (cpu_state.npxs & FPU_SW_C2);
        EXPECT_GT(steps, 1u);
        EXPECT_EQ(ST(0), 1.0);
    }
}

TEST_F(FpremTest, PartialScalingMatchesLdexpAcrossNormalExponents)
{
    uint64_t random = UINT64_C(0xb467ac139285fd03);
    auto next = [&random]() {
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        return random;
    };
    for (int mode : { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO }) {
        ASSERT_EQ(std::fesetround(mode), 0);
        for (int i = 0; i < 2000; i++) {
            const int eb = 1 + next() % 1982;
            const int ea = eb + 64 + next() % (1983 - eb);
            uint64_t abits = (next() & UINT64_C(0x800fffffffffffff)) | (uint64_t(ea) << 52);
            uint64_t bbits = (next() & UINT64_C(0x800fffffffffffff)) | (uint64_t(eb) << 52);
            double a, b;
            std::memcpy(&a, &abits, sizeof(a));
            std::memcpy(&b, &bbits, sizeof(b));
            const int d = ea - eb;
            const int n = (d & 0x1f) | 0x20;
            const double expected = std::fmod(a, std::ldexp(b, d - n));
            uint64_t expected_bits;
            std::memcpy(&expected_bits, &expected, sizeof(expected_bits));
            for (int nearest = 0; nearest <= 1; nearest++) {
                ST(0) = a;
                ST(1) = b;
                cpu_state.npxs = conditions;
                ASSERT_EQ(x87_fprem_common(nearest), 0);
                uint64_t result_bits;
                std::memcpy(&result_bits, &ST(0), sizeof(result_bits));
                EXPECT_EQ(result_bits, expected_bits);
                EXPECT_EQ(cpu_state.npxs, FPU_SW_C2);
            }
        }
    }
}

TEST_F(FpremTest, SubnormalDivisorRetainsPartialScaling)
{
    for (int nearest = 0; nearest <= 1; nearest++) {
        ST(0) = 1.0;
        ST(1) = std::numeric_limits<double>::denorm_min();
        cpu_state.npxs = conditions;
        EXPECT_EQ(x87_fprem_common(nearest), 0);
        EXPECT_EQ(ST(0), 0.0);
        EXPECT_FALSE(std::signbit(ST(0)));
        EXPECT_EQ(cpu_state.npxs, FPU_SW_C2);
    }
}
