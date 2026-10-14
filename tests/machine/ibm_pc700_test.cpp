// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdint>
#include <initializer_list>
#include <vector>
#include <gtest/gtest.h>
#include "../../src/machine/m_at_ibm_pc700.h"

class IbmPc700RapidTest : public testing::Test {
protected:
    uint8_t command = 0x00;
    uint8_t nibbles = 0;

    // Port 48h writes as E000:1831 in the LQKT47A BIOS makes them: an
    // optional first nibble from BH with bit 7 kept, then the two nibbles of
    // BL, each ORed with 10h.
    static std::vector<uint8_t> bios_writes(uint16_t bx)
    {
        std::vector<uint8_t> writes;
        if (bx & 0x8000)
            writes.push_back(uint8_t(bx >> 8) | 0x10);
        writes.push_back(uint8_t((bx >> 4) & 0x0f) | 0x10);
        writes.push_back(uint8_t(bx & 0x0f) | 0x10);
        return writes;
    }

    std::vector<int> send(std::initializer_list<uint16_t> commands)
    {
        std::vector<int> completed;
        for (uint16_t bx : commands)
            for (uint8_t val : bios_writes(bx)) {
                int ret = ibm_pc700_rapid_decode(&command, &nibbles, val);
                if (ret >= 0)
                    completed.push_back(ret);
            }
        return completed;
    }
};

TEST_F(IbmPc700RapidTest, PowerOffCompletesOnSecondNibble)
{
    EXPECT_EQ(ibm_pc700_rapid_decode(&command, &nibbles, 0x10), -1);
    EXPECT_EQ(ibm_pc700_rapid_decode(&command, &nibbles, 0x1e), 0x0e);
    EXPECT_EQ(nibbles, 0);
}

TEST_F(IbmPc700RapidTest, BiosTwoNibbleCommandsDecodeToThemselves)
{
    // Every BL value the BIOS sends without a BH nibble.
    const std::initializer_list<uint16_t> commands = {
        0x409b, 0x000e, 0x004d, 0x4057, 0x000a, 0x408a, 0x004a, 0x40b0, 0x0030,
        0x40b1, 0x40b3, 0x004e, 0x0015, 0x0055, 0x0044, 0x0004, 0x0041, 0x0043, 0x0042
    };
    std::vector<int> expected;
    for (uint16_t bx : commands)
        expected.push_back(bx & 0xff);
    EXPECT_EQ(send(commands), expected);
}

TEST_F(IbmPc700RapidTest, DataBytesAfterDAndEAreNotCommands)
{
    // Power-on setup at E000:17A4, then a 14-minute wake-up count (00000Eh)
    // the way E000:18E8 sends it: D 03h and three E writes, low byte first.
    EXPECT_EQ(send({ 0x004e, 0x8d02, 0x8eac }), std::vector<int>({ 0x4e }));
    EXPECT_EQ(send({ 0x8d03, 0x8e0e, 0x8e00, 0x8e00, 0x0044 }), std::vector<int>({ 0x44 }));
    EXPECT_EQ(send({ 0x8d13, 0x8e0e }), std::vector<int>());
    EXPECT_EQ(send({ 0x000e }), std::vector<int>({ 0x0e }));
}

TEST_F(IbmPc700RapidTest, LengthComesFromTheNibbleNotBit7)
{
    // Only SD0-SD4 reach the controller, so 1Eh starts a data write too.
    for (uint8_t val : { 0x1e, 0x10, 0x1e })
        EXPECT_EQ(ibm_pc700_rapid_decode(&command, &nibbles, val), -1);
    EXPECT_EQ(nibbles, 0);
}

TEST_F(IbmPc700RapidTest, WritesWithSd4LowAreIgnored)
{
    // F000:0AF9 writes 00h to port 48h.
    EXPECT_EQ(ibm_pc700_rapid_decode(&command, &nibbles, 0x10), -1);
    EXPECT_EQ(ibm_pc700_rapid_decode(&command, &nibbles, 0x00), -1);
    EXPECT_EQ(ibm_pc700_rapid_decode(&command, &nibbles, 0x0e), -1);
    EXPECT_EQ(ibm_pc700_rapid_decode(&command, &nibbles, 0x1e), 0x0e);
}
