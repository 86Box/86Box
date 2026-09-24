#include <gtest/gtest.h>

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

class MkeTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        std::memset(cdrom, 0, sizeof(cdrom));
        read_lbas.clear();
        read_result             = 1;
        eject_calls             = 0;
        subq_attr               = 0x14;
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
    return 0x230;
}
void
io_sethandler(uint16_t, uint16_t, uint8_t (*rb)(uint16_t, void *),
              uint16_t (*)(uint16_t, void *), uint32_t (*)(uint16_t, void *),
              void (*wb)(uint16_t, uint8_t, void *),
              void (*)(uint16_t, uint16_t, void *),
              void (*)(uint16_t, uint32_t, void *), void *)
{
    read_port  = rb;
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
cdrom_readsector_raw(cdrom_t *, uint8_t *out, int lba, int, int, int flags, int *len, uint8_t)
{
    read_lbas.push_back(lba);
    *len = flags == 0x20 ? 4 : (flags == 0x78 ? 2340 : 2048);
    std::memset(out, lba & 255, *len);
    return read_result;
}
int
cdrom_read_toc(const cdrom_t *, uint8_t *out, int, uint8_t, int, int)
{
    out[2] = out[3] = 1;
    return 0;
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
