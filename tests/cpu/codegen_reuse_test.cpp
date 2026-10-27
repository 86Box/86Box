// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>

extern "C" {
#include "cpu.h"
#include <86box/mem.h>
#include "../../src/codegen/codegen.h"
}

TEST(CodegenReuse, RequiresExactlyTheConfiguredNumberOfEntries)
{
    codeblock_t block = {};
    block.runs_left = SMC_THRASH_RUN_MASK;
    for (unsigned entry = 0; entry < SMC_THRASH_MIN_RUNS; entry++) {
        ASSERT_NE(block.runs_left, 0);
        block.runs_left >>= 1;
    }
    EXPECT_EQ(block.runs_left, 0);
    for (unsigned entry = 0; entry < 256; entry++) {
        block.runs_left >>= 1;
        EXPECT_EQ(block.runs_left, 0);
    }
}

TEST(CodegenReuse, RestoredAndUnrolledBlocksKeepTheirExistingThresholds)
{
    codeblock_t restored = {};
    restored.runs_left >>= 1;
    EXPECT_EQ(restored.runs_left, 0);

    codeblock_t unrolled = {};
    unrolled.runs_left = 1;
    unrolled.runs_left >>= 1;
    EXPECT_EQ(unrolled.runs_left, 0);
}
