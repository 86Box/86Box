// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdint>
#include <random>
#include <set>
#include <vector>
#include <gtest/gtest.h>

extern "C" {
#include "../../src/codegen/codegen_free_set.h"
}

namespace {

struct FreeSet {
    explicit FreeSet(uint32_t nr)
        : bits((nr + 63) / 64)
        , summary(((nr + 63) / 64 + 63) / 64)
    {
    }

    void add(uint32_t c) { codegen_free_set_add(bits.data(), summary.data(), &count, c); }
    uint32_t take() { return codegen_free_set_take_lowest(bits.data(), summary.data(), &count); }

    std::vector<uint64_t> bits;
    std::vector<uint64_t> summary;
    uint32_t              count = 0;
};

} // namespace

TEST(CodegenFreeSet, HandsOutInAscendingOrder)
{
    FreeSet s(10000);
    for (uint32_t c = 0; c < 10000; c++)
        s.add(c);
    EXPECT_EQ(s.count, 10000u);
    for (uint32_t c = 0; c < 10000; c++)
        ASSERT_EQ(s.take(), c);
    EXPECT_EQ(s.count, 0u);
}

TEST(CodegenFreeSet, LowestFreedComesBackFirst)
{
    FreeSet s(262144);
    for (uint32_t c = 0; c < 262144; c++)
        s.add(c);
    for (uint32_t c = 0; c < 262144; c++)
        s.take();
    /*Freed high to low, across summary words: still lowest first*/
    const uint32_t freed[] = { 262143, 200000, 4096, 4095, 64, 63, 1, 0 };
    for (uint32_t c : freed)
        s.add(c);
    for (int i = 7; i >= 0; i--)
        EXPECT_EQ(s.take(), freed[i]);
    EXPECT_EQ(s.count, 0u);
}

TEST(CodegenFreeSet, SummaryClearedWhenWordEmpties)
{
    FreeSet s(8192);
    s.add(4100);
    s.add(70);
    EXPECT_EQ(s.take(), 70u);
    EXPECT_EQ(s.summary[0], 0u);
    EXPECT_EQ(s.take(), 4100u);
    EXPECT_EQ(s.summary[1], 0u);
    for (uint64_t w : s.bits)
        EXPECT_EQ(w, 0u);
}

TEST(CodegenFreeSet, MatchesOrderedSetModel)
{
    const uint32_t     nr = 131072 + 128;
    FreeSet            s(nr);
    std::set<uint32_t> model;
    std::mt19937       rng(12345);

    for (uint32_t c = 0; c < nr; c += 3) {
        s.add(c);
        model.insert(c);
    }
    for (int i = 0; i < 200000; i++) {
        if (!model.empty() && (rng() & 1)) {
            const uint32_t want = *model.begin();
            model.erase(model.begin());
            ASSERT_EQ(s.take(), want);
        } else {
            uint32_t c;
            do
                c = rng() % nr;
            while (model.count(c));
            s.add(c);
            model.insert(c);
        }
        ASSERT_EQ(s.count, model.size());
    }
    while (!model.empty()) {
        ASSERT_EQ(s.take(), *model.begin());
        model.erase(model.begin());
    }
}
