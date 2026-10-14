// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdint>
#include <initializer_list>
#include <vector>
#include <gtest/gtest.h>
#include "../../src/machine/m_at_ibm_pc700.h"

class IbmPc700RapidTest : public testing::Test {
protected:
    ibm_pc700_rapid_t rr {};
    int               power_offs = 0;

    void SetUp() override
    {
        ibm_pc700_rapid_power_on(&rr);
    }

    // Port 48h writes as 21831h in the LQKT47A BIOS makes them: an optional
    // first nibble from BH with bit 7 kept, then the two nibbles of BL, each
    // ORed with 10h. BH bit 6 asks for a reply, read back from SD0.
    static std::vector<uint8_t> bios_writes(uint16_t bx)
    {
        std::vector<uint8_t> writes;
        if (bx & 0x8000)
            writes.push_back(uint8_t(bx >> 8) | 0x10);
        writes.push_back(uint8_t((bx >> 4) & 0x0f) | 0x10);
        writes.push_back(uint8_t(bx & 0x0f) | 0x10);
        return writes;
    }

    // Returns the decoded commands; counts power-offs.
    std::vector<int> send(std::initializer_list<uint16_t> commands)
    {
        std::vector<int> completed;
        for (uint16_t bx : commands)
            for (uint8_t val : bios_writes(bx)) {
                int cmd = ibm_pc700_rapid_decode(&rr, val);
                if (cmd >= 0) {
                    completed.push_back(cmd);
                    power_offs += ibm_pc700_rapid_execute(&rr, cmd);
                }
            }
        return completed;
    }

    int query(uint16_t bx)
    {
        send({ bx });
        return rr.reply;
    }
};

TEST_F(IbmPc700RapidTest, PowerOffCompletesOnSecondNibble)
{
    EXPECT_EQ(ibm_pc700_rapid_decode(&rr, 0x10), -1);
    EXPECT_EQ(ibm_pc700_rapid_decode(&rr, 0x1e), 0x0e);
    EXPECT_EQ(ibm_pc700_rapid_execute(&rr, 0x0e), 1);
    EXPECT_EQ(ibm_pc700_rapid_flag(&rr, 0x0e), 0);
}

TEST_F(IbmPc700RapidTest, BiosTwoNibbleCommandsDecodeToThemselves)
{
    // Every BL value the BIOS sends without a BH nibble, except 0Eh.
    const std::initializer_list<uint16_t> commands = {
        0x409b, 0x004d, 0x4057, 0x000a, 0x408a, 0x004a, 0x40b0, 0x0030, 0x40b1,
        0x40b3, 0x004e, 0x0015, 0x0055, 0x0044, 0x0004, 0x0041, 0x0043, 0x0042
    };
    std::vector<int> expected;
    for (uint16_t bx : commands)
        expected.push_back(bx & 0xff);
    EXPECT_EQ(send(commands), expected);
    EXPECT_EQ(power_offs, 0);
}

TEST_F(IbmPc700RapidTest, DataBytesAfterDAndEAreNotCommands)
{
    // Power-on setup at 217A4h, then a 14-minute wake-up count (00000Eh) the
    // way 218E8h sends it: D 03h and three E writes, low byte first.
    EXPECT_EQ(send({ 0x004e, 0x8d02, 0x8eac }), std::vector<int>({ 0x4e, 0xd02, 0xeac }));
    EXPECT_EQ(send({ 0x8d03, 0x8e0e, 0x8e00, 0x8e00, 0x0044 }),
              std::vector<int>({ 0xd03, 0xe0e, 0xe00, 0xe00, 0x44 }));
    EXPECT_EQ(power_offs, 0);
    EXPECT_EQ(send({ 0x000e }), std::vector<int>({ 0x0e }));
    EXPECT_EQ(power_offs, 1);
}

TEST_F(IbmPc700RapidTest, LengthComesFromTheNibbleNotBit7)
{
    // Only SD0-SD4 reach the controller, so 1Eh starts a data write too.
    for (uint8_t val : { 0x1e, 0x10 })
        EXPECT_EQ(ibm_pc700_rapid_decode(&rr, val), -1);
    EXPECT_EQ(ibm_pc700_rapid_decode(&rr, 0x1e), 0xe0e);
    EXPECT_EQ(rr.nibbles, 0);
}

TEST_F(IbmPc700RapidTest, WritesWithSd4LowAreIgnored)
{
    // 10AF9h writes 00h to port 48h.
    EXPECT_EQ(ibm_pc700_rapid_decode(&rr, 0x10), -1);
    EXPECT_EQ(ibm_pc700_rapid_decode(&rr, 0x00), -1);
    EXPECT_EQ(ibm_pc700_rapid_decode(&rr, 0x0e), -1);
    EXPECT_EQ(ibm_pc700_rapid_decode(&rr, 0x1e), 0x0e);
}

TEST_F(IbmPc700RapidTest, BiosSelfTestPasses)
{
    // 216ACh: clear flag 0Ah, expect 0; set it, expect 1.
    send({ 0x000a });
    EXPECT_EQ(query(0x408a), 0);
    send({ 0x004a });
    EXPECT_EQ(query(0x408a), 1);
}

TEST_F(IbmPc700RapidTest, PowerOnReportsTheSwitch)
{
    // 2171Dh-21772h: 30h would lead to the stay-off check, 31h boots.
    EXPECT_EQ(query(0x40b0), 0);
    EXPECT_EQ(query(0x40b1), 1);
    EXPECT_EQ(query(0x40b2), 0);
    EXPECT_EQ(query(0x40b3), 0);
    EXPECT_EQ(ibm_pc700_rapid_flag(&rr, IBM_PC700_RAPID_POWER), 1);
}

TEST_F(IbmPc700RapidTest, FlagsAreBitAddressableRam)
{
    send({ 0x0055, 0x0044, 0x0041 });
    EXPECT_EQ(rr.ram[0x22], 0x20); // flag 15h
    EXPECT_EQ(rr.ram[0x20], 0x12); // flags 01h and 04h
    send({ 0x0004 });
    EXPECT_EQ(rr.ram[0x20], 0x02);
}

TEST_F(IbmPc700RapidTest, VariablesAreWrittenInSequence)
{
    // Seconds to the next minute at 216DEh (D 13h) and a wake-up count of
    // 012345h minutes at 218E8h (D 03h, low byte first).
    send({ 0x8d13, 0x8e20, 0x8d03, 0x8e45, 0x8e23, 0x8e01 });
    EXPECT_EQ(rr.ram[0x13], 0x20);
    EXPECT_EQ(rr.ram[0x03], 0x45);
    EXPECT_EQ(rr.ram[0x04], 0x23);
    EXPECT_EQ(rr.ram[0x05], 0x01);
    EXPECT_EQ(rr.pointer, 0x06);
    EXPECT_EQ(power_offs, 0);
}

TEST_F(IbmPc700RapidTest, SetAndClearDoNotChangeTheReply)
{
    // 21A1Eh and 214C9h read SD0 after 57h, a set command.
    send({ 0x004a });
    EXPECT_EQ(query(0x408a), 1);
    EXPECT_EQ(query(0x4057), 1);
    send({ 0x000a });
    EXPECT_EQ(query(0x4057), 1);
    EXPECT_EQ(ibm_pc700_rapid_flag(&rr, 0x17), 1);
}
