#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/cdrom.h>
#include <86box/cdrom_mke.h>
#include <86box/timer.h>
}

namespace {
uint8_t (*read_port)(uint16_t, void *);
uint16_t (*read_word)(uint16_t, void *);
void (*write_port)(uint16_t, uint8_t, void *);
void            *interface;
pc_timer_t      *read_timer;
cdrom_ops_t      ops { };
std::vector<int> read_lbas;
int              read_result, eject_calls;
uint32_t         seek_lba, audio_start, audio_end;
int              audio_mode;
double           read_delay;
uint8_t          subq_attr;
uint16_t         config_base, mapped_base, mapped_size;

class MkeTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        std::memset(cdrom, 0, sizeof(cdrom));
        read_lbas.clear();
        read_result             = 1;
        eject_calls             = 0;
        subq_attr               = 0x14;
        config_base             = 0x230;
        cdrom[0].bus_type       = CDROM_BUS_MKE;
        cdrom[0].type           = type("cr521b");
        cdrom[0].cd_status      = CD_STATUS_DATA_ONLY;
        cdrom[0].cdrom_capacity = 100000;
        cdrom[0].ops            = &ops;
        ops.get_raw_track_info  = [](const void *, int *num, uint8_t *buffer) {
            auto *out = reinterpret_cast<raw_track_info_t *>(buffer);
            *num      = 1;
            std::memset(out, 0, sizeof(*out));
            out->point   = 1;
            out->adr_ctl = 0x14;
            out->ps      = 2;
        };
        interface = mke_cdrom_device.init(&mke_cdrom_device);
    }
    void       TearDown() override { mke_cdrom_device.close(interface); }
    static int type(const char *name)
    {
        for (int i = 0; cdrom_drive_types[i].bus_type != BUS_TYPE_NONE; ++i)
            if (!std::strcmp(cdrom_drive_types[i].internal_name, name))
                return i;
        return 0;
    }
    void command(std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes)
            write_port(0x230, b, interface);
    }
    std::vector<uint8_t> response()
    {
        std::vector<uint8_t> out;
        write_port(0x231, 0, interface);
        while (!(read_port(0x231, interface) & 4) && out.size() < 128)
            out.push_back(read_port(0x230, interface));
        return out;
    }
    uint8_t status()
    {
        command({ 0x81 });
        auto out = response();
        EXPECT_EQ(out.size(), 1u);
        return out.empty() ? 0 : out[0];
    }
    std::vector<uint8_t> data(unsigned count = 2048)
    {
        std::vector<uint8_t> out;
        write_port(0x231, 1, interface);
        while (count--)
            out.push_back(read_port(0x230, interface));
        write_port(0x231, 0, interface);
        return out;
    }
    void tick()
    {
        ASSERT_TRUE(read_timer->flags & TIMER_ENABLED);
        timer_disable(read_timer);
        read_timer->callback(read_timer->priv);
    }
};

TEST_F(MkeTest, OriginalDriverDetectionAndStatusFraming)
{
    command({ 0, 0, 0, 0, 0, 0, 0 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0xaa, 0x55 }));
    command({ 0x83, 0, 0, 0, 0, 0, 0 });
    const auto version = response();
    EXPECT_EQ(std::string(version.begin(), version.end()), "MATSHITA2.11");
    EXPECT_EQ(status(), 0xc9);
    EXPECT_EQ(status(), 0xc9);
    command({ 0x05, 0, 0, 0, 0, 0, 0 });
    EXPECT_TRUE(response().empty());
    EXPECT_EQ(status(), 0xe9);
}

TEST_F(MkeTest, CapacityModeAndTocUseFamilyZeroLayouts)
{
    command({ 0x88, 0, 0, 0, 0, 0, 0 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 1, 0x86, 0xa0, 8, 0 }));
    command({ 0x84, 0, 9, 0x24, 0, 0, 0 });
    EXPECT_TRUE(response().empty());
    command({ 0x85, 0, 0, 0, 0, 0, 0 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 9, 0x24 }));
    command({ 0x8c, 2, 1, 0, 0, 0, 0 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0, 0x14, 1, 0, 0, 0, 2, 0 }));
    command({ 0x8c, 0, 1, 0, 0, 0, 0 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0, 0x14, 1, 0, 0, 0, 0, 0 }));
    command({ 0x8b, 0, 0, 0, 0, 0, 0 });
    EXPECT_EQ(response().size(), 6u);
}

TEST_F(MkeTest, StreamsSixteenBitReadCountsWithoutImplicitStatus)
{
    command({ 2, 0, 0, 16, 4, 1, 0 }); // 1025 sectors exceeds the shared data buffer.
    EXPECT_NE(status() & 4, 0);
    for (unsigned sector = 0; sector < 1025; ++sector) {
        tick();
        EXPECT_EQ(read_port(0x231, interface) & 2, 0);
        EXPECT_EQ(read_delay, 1000000.0 / 75.0);
        auto bytes = data();
        EXPECT_EQ(bytes[0], uint8_t(16 + sector));
        EXPECT_TRUE(response().empty());
    }
    EXPECT_EQ(read_lbas.size(), 1025u);
    EXPECT_EQ(read_lbas.back(), 1040);
    EXPECT_EQ(status(), 0xe9);
    EXPECT_FALSE(read_timer->flags & TIMER_ENABLED);
}

TEST_F(MkeTest, BcdReadAndBinarySeekHaveDifferentAddressFormats)
{
    command({ 2, 0x00, 0x12, 0, 0, 1, 2 }); // BCD: 12 seconds, LBA 750.
    tick();
    data();
    EXPECT_EQ(read_lbas.back(), 750);
    command({ 1, 2, 0, 12, 0, 0, 0 }); // Binary: also 12 seconds.
    EXPECT_EQ(seek_lba, 750u);
    EXPECT_EQ(status() & 0x10, 0);
    for (uint8_t bad : { 0x6a, 0x75, 0x99 }) {
        command({ 2, 0, 2, bad, 0, 1, 2 });
        EXPECT_NE(status() & 0x10, 0);
        EXPECT_FALSE(read_timer->flags & TIMER_ENABLED);
    }
}

TEST_F(MkeTest, ReadErrorsEmptyMediaAndInvalidRangesAreRecoverable)
{
    command({ 2, 1, 0x86, 0x9f, 0, 2, 0 }); // Read past the end.
    EXPECT_NE(status() & 0x10, 0);
    command({ 0x82, 0, 0, 0, 0, 0, 0 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0, 6, 0, 0, 0, 0 }));
    EXPECT_EQ(status() & 0x10, 0);
    read_result = -1;
    command({ 2, 0, 0, 0, 0, 1, 0 });
    tick();
    EXPECT_NE(status() & 0x10, 0);
    EXPECT_NE(read_port(0x231, interface) & 1, 0);
    cdrom[0].cd_status = CD_STATUS_EMPTY;
    command({ 0x88, 0, 0, 0, 0, 0, 0 });
    EXPECT_EQ(response(), std::vector<uint8_t>(5));
    EXPECT_EQ(status() & 0x41, 0);
    EXPECT_NE(status() & 0x10, 0);
}

TEST_F(MkeTest, ResetCancelsPartialCommandReadAndError)
{
    command({ 2, 0, 0, 0, 0, 2, 0 });
    tick();
    command({ 0x84, 0, 9 });
    write_port(0x232, 0, interface);
    EXPECT_FALSE(read_timer->flags & TIMER_ENABLED);
    EXPECT_EQ(read_port(0x231, interface) & 6, 6);
    EXPECT_EQ(status(), 0xc9);
    command({ 0x85, 0, 0, 0, 0, 0, 0 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 8, 0 }));
}

TEST_F(MkeTest, RemovalCancelsReadAndMediaChangeIsReportedOnce)
{
    command({ 2, 0, 0, 0, 0, 2, 0 });
    tick();
    cdrom[0].ops = nullptr;
    cdrom[0].insert(cdrom[0].priv);
    EXPECT_EQ(read_port(0x231, interface) & 6, 6);
    EXPECT_FALSE(read_timer->flags & TIMER_ENABLED);
    cdrom[0].ops       = &ops;
    cdrom[0].cd_status = CD_STATUS_DATA_ONLY | CD_STATUS_TRANSITION;
    cdrom[0].insert(cdrom[0].priv);
    command({ 2, 0, 0, 0, 0, 1, 0 });
    command({ 0x82, 0, 0, 0, 0, 0, 0 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0, 0x11, 0, 0, 0, 0 }));
    command({ 2, 0, 0, 0, 0, 1, 0 });
    tick();
    EXPECT_EQ(data().size(), 2048u);
}

TEST_F(MkeTest, AudioControlsAndSpinDownDoNotEjectCaddy)
{
    command({ 0x84, 0x83, 0, 0, 0x90, 0xff, 0 });
    EXPECT_EQ(cdrom[0].get_volume(cdrom[0].priv, 0), 0u);
    EXPECT_EQ(cdrom[0].get_volume(cdrom[0].priv, 1), 255u);
    EXPECT_EQ(cdrom[0].get_channel(cdrom[0].priv, 1), 1u);
    command({ 0x85, 3, 0, 0, 0, 0, 0 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0x90, 0xff }));
    command({ 0x0b, 0, 2, 0, 0, 5, 0 });
    EXPECT_EQ(audio_start, 0x200u);
    EXPECT_EQ(audio_end, 0x500u);
    EXPECT_EQ(audio_mode, 1);
    EXPECT_NE(status() & 4, 0);
    command({ 0x8d, 0, 0, 0, 0, 0, 0 });
    EXPECT_EQ(cdrom[0].cd_status, CD_STATUS_PAUSED);
    EXPECT_EQ(status() & 0x0c, 0x0c);
    command({ 0x8d, 0x80, 0, 0, 0, 0, 0 });
    EXPECT_EQ(cdrom[0].cd_status, CD_STATUS_PLAYING);
    command({ 6, 0, 0, 0, 0, 0, 0 });
    EXPECT_EQ(eject_calls, 0);
    EXPECT_EQ(status() & 0x20, 0);
}

TEST_F(MkeTest, HeaderPacketsAndUnsupportedCommandsKeepFraming)
{
    command({ 4, 0, 0, 0, 16, 0, 0 });
    EXPECT_TRUE(response().empty());
    command({ 0x8e, 4, 0, 0, 0, 0, 0 });
    EXPECT_EQ(response(), std::vector<uint8_t>(4, 16));
    command({ 0x8e, 255, 0, 0, 0, 0, 0 });
    EXPECT_TRUE(response().empty());
    EXPECT_NE(status() & 0x10, 0);
    command({ 8 }); // This is not the one-byte family 1 ABORT command.
    EXPECT_TRUE(response().empty());
    command({ 0, 0, 0, 0, 0, 0 });
    EXPECT_TRUE(response().empty());
    EXPECT_NE(status() & 0x10, 0);
    command({ 0x82, 0, 0, 0, 0, 0, 0 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0, 0x0e, 0, 0, 0, 0 }));
}

TEST_F(MkeTest, StandardInterfaceUsesSeparateDataPortAndIsolatesDriveSelection)
{
    mke_cdrom_device.close(interface);
    interface = mke_cdrom_noncreative_device.init(&mke_cdrom_noncreative_device);
    write_port(0x233, 1, interface);
    command({ 0x83, 0, 0, 0, 0, 0, 0 });
    EXPECT_EQ(read_port(0x233, interface), 0xff);
    write_port(0x233, 0, interface);
    command({ 2, 0, 0, 7, 0, 1, 0 });
    tick();
    for (unsigned i = 0; i < 2048; ++i)
        EXPECT_EQ(read_port(0x232, interface), 7);
    EXPECT_EQ(status(), 0xe9);
}

TEST_F(MkeTest, RawReadAndSubchannelResponseLengths)
{
    command({ 3, 0, 0, 0, 0, 1, 0 });
    tick();
    EXPECT_EQ(data(2340).size(), 2340u);
    EXPECT_EQ(read_port(0x231, interface) & 2, 2);
    command({ 0x89, 2, 0, 0, 0, 0, 0 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0x15, 0x14, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0 }));
    command({ 2, 0, 0, 0, 0, 0, 0 });
    EXPECT_FALSE(read_timer->flags & TIMER_ENABLED);
    EXPECT_EQ(status(), 0xe9);
}

TEST_F(MkeTest, SubchannelReportsAudioStateWithoutChangingPlayback)
{
    for (const auto &[state, code] : std::array<std::pair<uint8_t, uint8_t>, 4> {
             { { CD_STATUS_PLAYING, 0x11 }, { CD_STATUS_PAUSED, 0x12 }, { CD_STATUS_PLAYING_COMPLETED, 0x13 }, { CD_STATUS_DATA_ONLY, 0x15 } }
    }) {
        cdrom[0].cd_status = state;
        subq_attr = (state == CD_STATUS_DATA_ONLY) ? 0x14 : 0x10;
        command({ 0x89, 2, 0, 0, 0, 0, 0 });
        const auto reply = response();
        ASSERT_EQ(reply.size(), 13u);
        EXPECT_EQ(reply[0], code);
        EXPECT_EQ(reply[1], subq_attr);
        EXPECT_EQ(cdrom[0].cd_status, state);
    }
}

TEST_F(MkeTest, ExistingFamilyOneCommandsRetainTheirFraming)
{
    mke_cdrom_device.close(interface);
    cdrom[0].type = type("cr562");
    interface     = mke_cdrom_device.init(&mke_cdrom_device);
    command({ 0x83, 0, 0, 0, 0, 0, 0 });
    auto version = response();
    ASSERT_EQ(version.size(), 11u);
    EXPECT_EQ(std::string(version.begin(), version.begin() + 10), "CR-5620.75");
    command({ 5, 0, 0, 0, 0, 0, 0 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0xe3 }));
    command({ 0x10, 0, 2, 0, 0, 0, 1 });
    tick();
    EXPECT_EQ(data()[0], 0);
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0xe3 }));
    command({ 8 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0xe3 }));
}
class TeacTest : public MkeTest {
protected:
    void SetUp() override
    {
        MkeTest::SetUp();
        select(teac_cdrom_device);
    }
    void select(const device_t &card)
    {
        mke_cdrom_device.close(interface);
        cdrom[0].type = type("teac_cd55a");
        interface = card.init(&card);
    }
    void send(std::initializer_list<uint8_t> bytes)
    {
        std::array<uint8_t, 10> packet {};
        std::copy(bytes.begin(), bytes.end(), packet.begin());
        for (auto b : packet)
            write_port(0x230, b, interface);
    }
};

TEST_F(TeacTest, ResetSignatureTenByteFramingAndInquiry)
{
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0x55 }));
    command({ 0x12, 0, 0, 0, 12, 0, 0 });
    EXPECT_TRUE(response().empty());
    command({ 0, 0, 0 });
    auto reply = response();
    ASSERT_EQ(reply.size(), 12u);
    EXPECT_EQ(reply[0], 0);
    EXPECT_EQ(std::string(reply.begin() + 1, reply.begin() + 7), "CD-55A");
    command({ 0x28, 0, 0 });
    write_port(0x232, 0, interface);
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0x55 }));
    send({ 0 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0 }));
}

TEST_F(TeacTest, ModesSpeedAndChannelMute)
{
    response();
    send({ 0x5a, 0, 0, 0, 10 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0, 2, 8, 0, 0, 0, 0, 1, 0, 0 }));
    for (const auto &[speed, multiplier] : std::array<std::pair<uint8_t, int>, 3> {
             { { 0x80, 1 }, { 0x81, 2 }, { 0x82, 4 } } }) {
        send({ 0x55, speed, 8, 0, 0, 0, 0, 0x11 });
        EXPECT_EQ(response(), (std::vector<uint8_t> { 0 }));
        EXPECT_EQ(cdrom[0].get_volume(cdrom[0].priv, 0), 0u);
        EXPECT_EQ(cdrom[0].get_volume(cdrom[0].priv, 1), 255u);
        send({ 0x28, 0, 0, 0, 0, 0, 0, 0, 1 });
        EXPECT_DOUBLE_EQ(read_delay, 1000000.0 / (75 * multiplier));
        tick();
        data();
        EXPECT_EQ(response(), (std::vector<uint8_t> { 0 }));
    }
}

TEST_F(TeacTest, WordReadsConsumeConsecutiveDataBytesAndFinishAfterLastSector)
{
    select(teac_cdrom_16bit_device);
    response();
    ASSERT_NE(read_word, nullptr);
    EXPECT_EQ(read_port(0x231, interface) & 0x80, 0);
    send({ 0x28, 0, 0, 0, 0, 16, 0, 0, 2 });
    for (int sector = 16; sector < 18; ++sector) {
        EXPECT_TRUE(response().empty());
        tick();
        write_port(0x231, 1, interface);
        for (int i = 0; i < 2048; i += 2)
            EXPECT_EQ(read_word(0x230, interface),
                      ((i * 37 + sector) & 255) | ((((i + 1) * 37 + sector) & 255) << 8));
    }
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0 }));
    EXPECT_EQ(read_lbas, (std::vector<int> { 16, 17 }));
    EXPECT_FALSE(read_timer->flags & TIMER_ENABLED);
}

TEST_F(TeacTest, BothEightBitPortLayoutsAndAbsentDrives)
{
    for (const device_t *card : { &teac_cdrom_device, &mke_cdrom_device, &mke_cdrom_noncreative_device }) {
        select(*card);
        response();
        EXPECT_EQ(read_port(0x231, interface) & 0x80, 0x80);
        EXPECT_EQ(read_word, nullptr);
        send({ 0x28, 0, 0, 0, 0, 7, 0, 0, 1 });
        tick();
        write_port(0x231, 1, interface);
        for (int i = 0; i < 2048; ++i)
            EXPECT_EQ(read_port(card->local ? 0x230 : 0x232, interface), (i * 37 + 7) & 255);
        EXPECT_EQ(response(), (std::vector<uint8_t> { 0 }));
        write_port(0x233, 3, interface);
        EXPECT_EQ(read_port(0x231, interface), 0xff);
        send({ 0xc0 });
        EXPECT_EQ(read_port(0x231, interface), 0xff);
    }
}

TEST_F(TeacTest, AddressSwitchesAndJumpersDecodeAllDocumentedSettings)
{
    for (const device_t *card : { &teac_cdrom_device, &teac_cdrom_16bit_device }) {
        const bool word = card == &teac_cdrom_16bit_device;
        for (unsigned address = word ? 0x2c0 : 0; address <= (word ? 0x3a0 : 0x3fc); address += word ? 0x20 : 4) {
            SCOPED_TRACE(address);
            config_base = address;
            select(*card);
            EXPECT_EQ(mapped_base, address);
            EXPECT_EQ(mapped_size, 4);
            EXPECT_EQ(read_port(address, interface), 0x55);
            const std::array<uint8_t, 10> inquiry { 0x12, 0, 0, 0, 12 };
            for (auto byte : inquiry)
                write_port(address, byte, interface);
            EXPECT_EQ(read_port(address + 1, interface) & 4, 0);
            EXPECT_EQ(read_port(address, interface), 0);
            std::string model;
            for (int i = 0; i < 6; ++i)
                model += char(read_port(address, interface));
            EXPECT_EQ(model, "CD-55A");
        }
    }
}

TEST_F(TeacTest, TocUsesDataFifoWithSeparateCompletionAndAllocationLimit)
{
    response();
    send({ 0x43, 2, 0, 0, 0, 0, 1, 0, 12 });
    EXPECT_TRUE(response().empty());
    EXPECT_EQ(data(12), (std::vector<uint8_t> { 0, 18, 1, 1, 0, 0x14, 1, 0, 0, 0, 2, 0 }));
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0 }));
    EXPECT_EQ(read_port(0x231, interface) & 6, 6);
}

TEST_F(TeacTest, LargeReadsRawSectorsAndAbort)
{
    response();
    send({ 0x28, 0, 0, 0, 0, 0, 0, 4, 1 }); // 1025 sectors, beyond the FIFO size.
    for (unsigned i = 0; i < 1025; ++i) {
        tick();
        auto bytes = data();
        EXPECT_EQ(bytes.front(), i & 255);
        EXPECT_EQ(bytes.back(), (2047 * 37 + i) & 255);
    }
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0 }));
    EXPECT_EQ(read_lbas.size(), 1025u);
    for (unsigned size : { 2340, 2352 }) {
        send({ 0x55, 2, uint8_t(size >> 8), uint8_t(size) });
        EXPECT_EQ(response(), (std::vector<uint8_t> { 0 }));
        send({ 0x28, 0, 0, 0, 0, 5, 0, 0, 2 });
        tick();
        EXPECT_EQ(data(size).back(), ((size - 1) * 37 + 5) & 255);
        send({ 8 });
        EXPECT_EQ(response(), (std::vector<uint8_t> { 0 }));
        EXPECT_FALSE(read_timer->flags & TIMER_ENABLED);
        EXPECT_EQ(read_port(0x231, interface) & 6, 6);
    }
}

TEST_F(TeacTest, DriveSelectionIsolatesModesAndSharedResetReachesBothDrives)
{
    cdrom[1] = cdrom[0];
    cdrom[1].id = 1;
    cdrom[1].mke_channel = 1;
    select(teac_cdrom_device);
    response();
    send({ 0x55, 0x80, 8, 0 });
    response();
    write_port(0x233, 2, interface); // Creative swaps ID selection bits 0 and 1.
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0x55 }));
    send({ 0x5a, 0, 0, 0, 10 });
    auto mode = response();
    ASSERT_EQ(mode.size(), 10u);
    EXPECT_EQ(mode[1], 2);
    write_port(0x233, 0, interface);
    send({ 0x5a, 0, 0, 0, 10 });
    mode = response();
    ASSERT_EQ(mode.size(), 10u);
    EXPECT_EQ(mode[1], 0x80);
    write_port(0x232, 0, interface);
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0x55 }));
    write_port(0x233, 2, interface);
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0x55 }));
}

TEST_F(TeacTest, ErrorsMediaChangeAndResetCancelTransfers)
{
    response();
    send({ 0x28, 0, 0, 1, 0x86, 0x9f, 0, 0, 2 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 2 }));
    send({ 3 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0, 0, 5, 0x21, 0 }));
    send({ 0x28, 0, 0, 0, 0, 0, 0, 0, 1 });
    read_result = -1;
    tick();
    EXPECT_EQ(response(), (std::vector<uint8_t> { 2 }));
    send({ 3 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0, 0, 3, 0x11, 0 }));
    read_result = 1;
    send({ 0x28, 0, 0, 0, 0, 0, 0, 0, 2 });
    tick();
    cdrom[0].ops = nullptr;
    cdrom[0].insert(cdrom[0].priv);
    EXPECT_EQ(read_port(0x231, interface) & 6, 6);
    EXPECT_FALSE(read_timer->flags & TIMER_ENABLED);
    send({ 0 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 2 }));
    send({ 3 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0, 0, 2, 0x3a, 0 }));
    cdrom[0].ops = &ops;
    cdrom[0].cd_status = CD_STATUS_DATA_ONLY | CD_STATUS_TRANSITION;
    send({ 0 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 2 }));
    send({ 3 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0, 0, 6, 0x28, 0 }));
    send({ 0 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0 }));
    send({ 0x28, 0, 0, 0, 0, 0, 0, 0, 2 });
    send({ 0xc0 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0x55 }));
    EXPECT_FALSE(read_timer->flags & TIMER_ENABLED);
}

TEST_F(TeacTest, AudioPauseSenseAndDoorLock)
{
    response();
    send({ 0x47, 0, 0, 0, 2, 0, 0, 5, 0 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0 }));
    EXPECT_EQ(audio_start, 0u);
    EXPECT_EQ(audio_end, 226u);
    EXPECT_EQ(audio_mode, 0);
    send({ 3 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0, 0, 0, 0, 0x11 }));
    send({ 0x4b });
    response();
    send({ 3 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0, 0, 0, 0, 0x12 }));
    send({ 0x1e, 0, 0, 0, 1 });
    response();
    send({ 0x1b, 0, 0, 0, 2 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 2 }));
    EXPECT_EQ(eject_calls, 0);
    send({ 0x1e });
    response();
    send({ 0x1b, 0, 0, 0, 2 });
    EXPECT_EQ(response(), (std::vector<uint8_t> { 0 }));
    EXPECT_EQ(eject_calls, 1);
}
} // namespace

extern "C" {
cdrom_t  cdrom[CDROM_NUM];
uint64_t TIMER_USEC = 1ULL << 32;
uint64_t tsc, timer_target;
void
timer_enable(pc_timer_t *t)
{
    t->flags |= TIMER_ENABLED;
}
void
timer_disable(pc_timer_t *t)
{
    t->flags &= ~TIMER_ENABLED;
}
void
timer_add(pc_timer_t *t, void (*callback)(void *), void *priv, int start)
{
    read_timer  = t;
    t->callback = callback;
    t->priv     = priv;
    t->flags    = start ? TIMER_ENABLED : 0;
}
void
timer_on_auto(pc_timer_t *t, double period)
{
    read_delay = period;
    timer_enable(t);
}
void
ui_sb_update_icon(int, int)
{
}
int
device_get_config_hex16(const char *)
{
    return config_base;
}
void
io_sethandler(uint16_t base, uint16_t size, uint8_t (*rb)(uint16_t, void *),
              uint16_t (*rw)(uint16_t, void *), uint32_t (*)(uint16_t, void *),
              void (*wb)(uint16_t, uint8_t, void *),
              void (*)(uint16_t, uint16_t, void *),
              void (*)(uint16_t, uint32_t, void *), void *)
{
    mapped_base = base;
    mapped_size = size;
    read_port  = rb;
    read_word  = rw;
    write_port = wb;
}
char *
cdrom_get_internal_name(int type)
{
    return const_cast<char *>(cdrom_drive_types[type].internal_name);
}
void
cdrom_generate_name_mke(int type, char *name)
{
    std::sprintf(name, "%s%s", cdrom_drive_types[type].model, cdrom_drive_types[type].revision);
}
void
cdrom_stop(cdrom_t *dev)
{
    if (dev->cd_status == CD_STATUS_PLAYING || dev->cd_status == CD_STATUS_PAUSED)
        dev->cd_status = CD_STATUS_STOPPED;
}
double
cdrom_seek_time(const cdrom_t *)
{
    return 0;
}
void
cdrom_seek(cdrom_t *, uint32_t pos, uint8_t)
{
    seek_lba = pos;
}
void
cdrom_eject(uint8_t)
{
    ++eject_calls;
}
void
cdrom_reload(uint8_t)
{
}
int
cdrom_readsector_raw(cdrom_t *dev, uint8_t *out, int lba, int, int sector_type, int flags, int *len, uint8_t)
{
    // The real backend divides by this multiplier for logical-block requests.
    if ((sector_type & 15) >= 8) {
        EXPECT_NE(sector_type >> 4, 0);
    }
    read_lbas.push_back(lba);
    *len = flags == 0x20 ? 4 : (flags == 0x78 ? 2340 : (flags == 0xf8 ? 2352 : 2048));
    std::memset(out, lba & 255, *len);
    if (!std::strcmp(cdrom_get_internal_name(dev->type), "teac_cd55a"))
        for (int i = 0; i < *len; ++i)
            out[i] = (i * 37 + lba) & 255;
    return read_result;
}
int
cdrom_read_toc(const cdrom_t *, uint8_t *out, int kind, uint8_t track, int, int)
{
    std::memset(out, 0, 20);
    out[1] = 18;
    out[2] = out[3] = 1;
    out[5] = 0x14;
    out[6] = track == 0xaa ? 0xaa : 1;
    out[10] = 2;
    out[13] = 0x14;
    out[14] = 0xaa;
    out[17] = 22;
    out[18] = 15;
    out[19] = 25;
    return (kind == CD_TOC_SESSION || track == 0xaa) ? 12 : 20;
}
void
cdrom_read_disc_information(const cdrom_t *, uint8_t *out)
{
    std::memset(out, 0, 34);
}
uint8_t
cdrom_get_current_status(const cdrom_t *dev)
{
    switch (dev->cd_status) {
        case CD_STATUS_PLAYING:
            return 0x11;
        case CD_STATUS_PAUSED:
            return 0x12;
        case CD_STATUS_PLAYING_COMPLETED:
            return 0x13;
        default:
            return 0x15;
    }
}
void
cdrom_get_current_subchannel_sony(cdrom_t *, uint8_t *out, int)
{
    std::memset(out, 0, 9);
    out[0] = subq_attr;
    out[7] = 2;
}
uint8_t
cdrom_audio_play(cdrom_t *dev, uint32_t pos, uint32_t len, int mode)
{
    audio_start    = pos;
    audio_end      = len;
    audio_mode     = mode;
    dev->cd_status = CD_STATUS_PLAYING;
    return 1;
}
void
cdrom_audio_pause_resume(cdrom_t *dev, uint8_t resume)
{
    dev->cd_status = resume ? CD_STATUS_PLAYING : CD_STATUS_PAUSED;
}
}
