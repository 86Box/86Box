#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

extern "C" {
#include <86box/random.h>
}

static std::vector<uint8_t>
draw(uint32_t seed, int n, bool call_rand_between = false)
{
    std::vector<uint8_t> out;

    random_seed(seed);
    for (int i = 0; i < n; i++) {
        if (call_rand_between)
            (void) rand();
        out.push_back(random_generate());
    }
    return out;
}

TEST(RandomTest, SameSeedSameBytes)
{
    EXPECT_EQ(draw(7, 1000), draw(7, 1000));
    EXPECT_EQ(draw(0, 1000), draw(0, 1000));
}

TEST(RandomTest, DifferentSeedsDiffer)
{
    EXPECT_NE(draw(7, 16), draw(8, 16));
}

TEST(RandomTest, OtherRandUsersDoNotShiftTheSequence)
{
    EXPECT_EQ(draw(0x1234, 1000), draw(0x1234, 1000, true));
}

TEST(RandomTest, SeededBytesUseTheWholeRange)
{
    std::vector<int> seen(256);

    for (uint8_t b : draw(1, 65536))
        seen[b]++;
    for (int i = 0; i < 256; i++)
        EXPECT_GT(seen[i], 128) << "byte " << i;
}
