// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdint>
#include <gtest/gtest.h>
#include "../../src/device/kbc_at_vectra.h"

TEST(VectraKbcTest, ExtendedSubcommandsHaveNoTrailingPayload)
{
    for (uint8_t data : { 0x10, 0x50, 0x05, 0x06, 0x87, 0x88, 0x92 })
        EXPECT_EQ(kbc_vectra_next_phase(1, data), 0);
}

TEST(VectraKbcTest, Subcommand93ConsumesEightBytes)
{
    uint8_t phase = kbc_vectra_next_phase(1, 0x93);
    for (unsigned i = 0; i < 8; ++i) {
        ASSERT_NE(phase, 0);
        // Command-like values inside the payload must not start a new command.
        phase = kbc_vectra_next_phase(phase, i & 1 ? 0xde : 0x94);
    }
    EXPECT_EQ(phase, 0);
}

TEST(VectraKbcTest, Subcommand94ConsumesOneByte)
{
    const uint8_t phase = kbc_vectra_next_phase(1, 0x94);
    ASSERT_NE(phase, 0);
    EXPECT_EQ(kbc_vectra_next_phase(phase, 0x93), 0);
}
