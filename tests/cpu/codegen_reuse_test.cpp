// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>

extern "C" {
#include "cpu.h"
#include <86box/mem.h>
#include "../../src/codegen/codegen.h"
}

TEST(CodegenReuse, CountsEveryRunUpTo64)
{
    codeblock_t block = {};
    block.runs_left = CODEBLOCK_RUNS_NEW;
    for (unsigned run = 0; run < 64; run++) {
        for (unsigned n = 1; n <= 64; n++)
            ASSERT_EQ(codeblock_ran(block.runs_left, n), n <= run) << run << " " << n;
        block.runs_left >>= 1;
    }
    EXPECT_EQ(block.runs_left, 0u);
    for (unsigned run = 0; run < 256; run++) {
        block.runs_left >>= 1;
        EXPECT_TRUE(codeblock_ran(block.runs_left, 64));
    }
}

TEST(CodegenReuse, PaysBackAfterTheConfiguredNumberOfRuns)
{
    codeblock_t block = {};
    block.runs_left = CODEBLOCK_RUNS_NEW;
    for (unsigned run = 0; run < SMC_THRASH_MIN_RUNS; run++) {
        ASSERT_FALSE(codeblock_ran(block.runs_left, SMC_THRASH_MIN_RUNS));
        block.runs_left >>= 1;
    }
    EXPECT_TRUE(codeblock_ran(block.runs_left, SMC_THRASH_MIN_RUNS));
}

TEST(CodegenReuse, RestoredAndUnrolledBlocksKeepTheirExistingThresholds)
{
    codeblock_t restored = {};
    restored.runs_left = CODEBLOCK_RUNS_PAID;
    EXPECT_TRUE(codeblock_ran(restored.runs_left, SMC_THRASH_MIN_RUNS));
    EXPECT_FALSE(codeblock_ran(restored.runs_left, SMC_THRASH_MIN_RUNS + 1));

    codeblock_t unrolled = {};
    unrolled.runs_left = CODEBLOCK_RUNS_UNROLLED;
    EXPECT_FALSE(codeblock_ran(unrolled.runs_left, SMC_THRASH_MIN_RUNS));
    unrolled.runs_left >>= 1;
    EXPECT_TRUE(codeblock_ran(unrolled.runs_left, SMC_THRASH_MIN_RUNS));
}
