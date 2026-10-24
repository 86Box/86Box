#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

extern "C" {
#include <86box/pc_block.h>
}

namespace {

std::vector<int32_t>
blocks(pc_block_t *b, uint32_t speed, uint32_t per_s, int n)
{
    std::vector<int32_t> out;
    for (int i = 0; i < n; i++)
        out.push_back(pc_block_cycles(b, speed, per_s));
    return out;
}

int64_t
sum(const std::vector<int32_t> &v)
{
    int64_t s = 0;
    for (int32_t c : v)
        s += c;
    return s;
}

} // namespace

/* 66666666 Hz: 66666.666 cycles a millisecond. A thousand blocks run exactly
   a second of the CPU's cycles, not 666 short. */
TEST(PcBlock, BlocksAverageExactlyAMillisecond)
{
    pc_block_t b {};
    auto       v = blocks(&b, 66666666, 1000, 1000);

    EXPECT_EQ(sum(v), 66666666);
    for (int32_t c : v)
        EXPECT_TRUE(c == 66666 || c == 66667) << c;
}

/* The ten-millisecond blocks of force_10ms, likewise. */
TEST(PcBlock, TenMillisecondBlocks)
{
    pc_block_t b {};

    EXPECT_EQ(sum(blocks(&b, 33333333, 100, 100)), 33333333);
}

/* A clock that is a multiple of 1 kHz runs every block the same, as before. */
TEST(PcBlock, WholeClockIsUnchanged)
{
    pc_block_t b {};

    for (int32_t c : blocks(&b, 25000000, 1000, 50))
        EXPECT_EQ(c, 25000);
}

/* After a reset, blocks fall exactly as after a fresh start, whatever ran
   before: the pattern of long and short blocks doesn't depend on history. */
TEST(PcBlock, ResetStartsAfresh)
{
    pc_block_t fresh {};
    auto       want = blocks(&fresh, 66666666, 1000, 50);

    pc_block_t used {};
    blocks(&used, 66666666, 1000, 2001); /* leaves a remainder of 666 */
    ASSERT_NE(used.remainder, 0u);
    pc_block_reset(&used);

    EXPECT_EQ(blocks(&used, 66666666, 1000, 50), want);
}

/* Switching between 1 ms and 10 ms blocks starts the carry afresh, so the
   remainder never exceeds a block's worth and no block gets more than one
   extra cycle. */
TEST(PcBlock, BlockLengthChangeStartsAfresh)
{
    pc_block_t b {};

    blocks(&b, 66666666, 1000, 2001);
    auto v = blocks(&b, 66666666, 100, 100);
    for (int32_t c : v)
        EXPECT_TRUE(c == 666666 || c == 666667) << c;
    EXPECT_EQ(sum(v), 66666666);
    EXPECT_LT(b.remainder, 100u);
}
