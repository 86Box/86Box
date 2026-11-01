#include <gtest/gtest.h>
#include <cstdint>
#include <initializer_list>
extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/machine.h>
#include <86box/plat.h>
#include <86box/timer.h>
}

namespace {
uint8_t (*read_port)(uint16_t, void *);
void (*write_port)(uint16_t, uint8_t, void *);
int power_offs;
}

extern "C" {
uint64_t tsc;
uint64_t TIMER_USEC;

void
io_sethandler(uint16_t, uint16_t,
              uint8_t (*inb)(uint16_t, void *), uint16_t (*)(uint16_t, void *), uint32_t (*)(uint16_t, void *),
              void (*outb)(uint16_t, uint8_t, void *), void (*)(uint16_t, uint16_t, void *),
              void (*)(uint16_t, uint32_t, void *), void *)
{
    read_port  = inb;
    write_port = outb;
}

void
plat_power_off(void)
{
    power_offs++;
}
}

namespace {
class IbmPc700PowerTest : public ::testing::Test {
protected:
    void *dev = nullptr;

    void SetUp() override
    {
        power_offs = 0;
        TIMER_USEC = 100ULL << 32; /* 100 MHz */
        tsc        = 1000000;
        dev        = ibm_pc700_power_device.init(&ibm_pc700_power_device);
    }
    void TearDown() override
    {
        ibm_pc700_power_device.close(dev);
    }

    /* As the LQKT47A BIOS sends a command: an optional first nibble with bit 7
       set for Ch, Dh and Eh, then the two nibbles of the byte, each with bit 4. */
    void send(uint16_t cmd)
    {
        if (cmd & 0x8000)
            write_port(0x48, uint8_t(cmd >> 8) | 0x10, dev);
        write_port(0x48, uint8_t((cmd >> 4) & 0x0f) | 0x10, dev);
        write_port(0x48, uint8_t(cmd & 0x0f) | 0x10, dev);
    }
    int flag(uint8_t n)
    {
        send(0x80 | n);
        return read_port(0x48, dev) & 0x01;
    }
    int status()
    {
        return read_port(0x49, dev) & 0x01;
    }
};

TEST_F(IbmPc700PowerTest, CommandOEhSwitchesTheSupplyOff)
{
    send(0x000e);
    EXPECT_EQ(power_offs, 1);
}

TEST_F(IbmPc700PowerTest, DataByteOEhIsNotACommand)
{
    /* A wake-up count of 14 minutes, sent the way the BIOS does: D 03h, then
       three E writes, low byte first. */
    for (uint16_t cmd : { 0x8d03, 0x8e0e, 0x8e00, 0x8e00, 0x0044 })
        send(cmd);
    EXPECT_EQ(power_offs, 0);
}

TEST_F(IbmPc700PowerTest, CommandChTakesADataByte)
{
    /* The BIOS sends Ch 4Ah after POST; the next command must still decode. */
    send(0x8c4a);
    send(0x000e);
    EXPECT_EQ(power_offs, 1);
}

TEST_F(IbmPc700PowerTest, CommandChLeavesTheFlags)
{
    /* Ch 2Dh, sent while the BIOS relocates SMBASE, is not a flag clear. */
    send(0x006d);
    send(0x8c2d);
    EXPECT_EQ(flag(0x2d), 1);
}

TEST_F(IbmPc700PowerTest, LengthComesFromTheFirstNibble)
{
    /* Bit 7 does not reach the controller, so 1Eh also starts a data write. */
    for (uint8_t val : { 0x1e, 0x10, 0x1e })
        write_port(0x48, val, dev);
    EXPECT_EQ(power_offs, 0);
    send(0x000e);
    EXPECT_EQ(power_offs, 1);
}

TEST_F(IbmPc700PowerTest, WritesWithBit4ClearAreIgnored)
{
    for (uint8_t val : { 0x10, 0x00, 0x0e, 0x1e })
        write_port(0x48, val, dev);
    EXPECT_EQ(power_offs, 1);
}

TEST_F(IbmPc700PowerTest, ResetRestartsTheCommand)
{
    write_port(0x48, 0x10, dev);
    write_port(0x49, 0x01, dev);
    write_port(0x49, 0x00, dev);
    send(0x000e);
    EXPECT_EQ(power_offs, 1);
}

TEST_F(IbmPc700PowerTest, BiosFlagTestPasses)
{
    send(0x000a);
    EXPECT_EQ(flag(0x0a), 0);
    send(0x004a);
    EXPECT_EQ(flag(0x0a), 1);
}

TEST_F(IbmPc700PowerTest, PowerOnReportsTheSwitch)
{
    EXPECT_EQ(flag(0x30), 0);
    EXPECT_EQ(flag(0x31), 1);
    EXPECT_EQ(flag(0x32), 0);
    EXPECT_EQ(flag(0x33), 0);
    EXPECT_EQ(flag(0x0e), 1);
}

TEST_F(IbmPc700PowerTest, VariablesShareTheFlagRam)
{
    /* Flags 00h-07h are bits 0-7 of RAM byte 20h. */
    send(0x8d20);
    send(0x8e5a);
    for (uint8_t n = 0; n < 8; n++)
        EXPECT_EQ(flag(n), (0x5a >> n) & 1) << int(n);

    /* Eh moves to the next byte: flags 08h-0Fh. */
    send(0x8d20);
    send(0x8e00);
    send(0x8e81);
    EXPECT_EQ(flag(0x08), 1);
    EXPECT_EQ(flag(0x0f), 1);
    EXPECT_EQ(flag(0x0a), 0);
}

TEST_F(IbmPc700PowerTest, SetAndClearLeaveTheReply)
{
    send(0x004a);
    EXPECT_EQ(flag(0x0a), 1);
    send(0x0057);
    EXPECT_EQ(read_port(0x48, dev) & 0x01, 1);
}

TEST_F(IbmPc700PowerTest, StatusShowsBusyAndHeartbeat)
{
    /* 100 MHz: the heartbeat period is 5500 clocks with 150 of them low. */
    tsc = 5500 * 100 + 1000;
    write_port(0x48, 0x10, dev);
    EXPECT_EQ(status(), 0);
    tsc += 1000;
    EXPECT_EQ(status(), 1);
    tsc = 5500 * 200 + 10;
    EXPECT_EQ(status(), 0);
    tsc = 5500 * 200 + 160;
    EXPECT_EQ(status(), 1);
}
}
