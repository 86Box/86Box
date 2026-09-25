#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/cdrom.h>
#include <86box/cdrom_philips.h>
#include <86box/timer.h>
}

namespace {
std::vector<pc_timer_t *> timers;
std::vector<int>          read_lbas;
uint8_t (*read_port)(uint16_t, void *);
void (*write_port)(uint16_t, uint8_t, void *);
cdrom_ops_t ops { };
int         read_result, read_length, removed, irq;
double      delay;
uint32_t    sectors;

class PhilipsTest : public ::testing::Test {
protected:
    void *dev { };
    void  SetUp() override
    {
        std::memset(cdrom, 0, sizeof(cdrom));
        timers.clear();
        read_lbas.clear();
        read_result = 1;
        read_length = 2352;
        removed = irq      = 0;
        sectors            = 10000;
        ops                = { };
        ops.get_track_info = [](const void *, uint32_t track, int, track_info_t *t) {
            if (track != 1 && track != 0xa2)
                return 0;
            unsigned f = track == 0xa2 ? sectors + 150 : 150;
            t->number  = track;
            t->attr    = 0x14;
            t->m       = f / 4500;
            t->s       = (f / 75) % 60;
            t->f       = f % 75;
            return 1;
        };
        cdrom[0].bus_type  = CDROM_BUS_PHILIPS;
        cdrom[0].ops       = &ops;
        cdrom[0].cd_status = CD_STATUS_DATA_ONLY;
        dev                = philips_cm250_device.init(&philips_cm250_device);
    }
    void TearDown() override
    {
        if (dev)
            philips_cm250_device.close(dev);
    }
    void    out(int reg, uint8_t value) { write_port(0x340 + reg, value, dev); }
    uint8_t in(int reg) { return read_port(0x340 + reg, dev); }
    void    tick(unsigned index)
    {
        auto *t = timers.at(index);
        ASSERT_TRUE(t->flags & TIMER_ENABLED);
        timer_disable(t);
        t->callback(t->priv);
    }
    uint8_t byte(uint8_t b)
    {
        out(5, b);
        EXPECT_EQ(in(3) & 3, 0);
        tick(0);
        EXPECT_NE(irq, 0);
        EXPECT_EQ(in(3) & 3, 3);
        return in(1);
    }
    void command(std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes)
            EXPECT_EQ(byte(b), b);
    }
};

// Reset and identify sequence used by the unmodified CM250.MSC 1.00.
TEST_F(PhilipsTest, OriginalDriverResetAndIdentify)
{
    out(4, 0x61);
    EXPECT_EQ(in(0), 0);
    EXPECT_EQ(in(3) & 0xef, 0x41);
    out(4, 0x21);
    out(4, 0xa1);
    out(4, 0x21);
    EXPECT_EQ(in(3) & 0x10, 0x10);
    command({ 0x3a });
    std::array<uint8_t, 8> status;
    for (auto &v : status)
        v = byte(0x9c);
    EXPECT_EQ(status[6], 0x0e);
    command({ 0x4e });
    EXPECT_EQ(in(3) & 0x10, 0);
    command({ 0x2d });
    EXPECT_EQ(byte(0x9c), 5);
    EXPECT_EQ(byte(0x9c), 4);
}

TEST_F(PhilipsTest, ReadPacingAndUnconsumedSector)
{
    command({ 0xa6, 0x16, 2, 0, 2, 0, 0 });
    EXPECT_NEAR(delay, 50, 0.001); // Last command echo uses its own timer.
    tick(1);
    EXPECT_EQ(in(0) & 0x10, 0x10);
    EXPECT_EQ(read_lbas, (std::vector<int> { 16 }));
    EXPECT_FALSE(timers[1]->flags & TIMER_ENABLED);
    for (int i = 0; i < 2352; i++)
        ASSERT_EQ(in(2), 16);
    EXPECT_NEAR(delay, 1000000.0 / 75, 0.001);
    tick(1);
    EXPECT_EQ(read_lbas, (std::vector<int> { 16, 17 }));
    for (int i = 0; i < 2352; i++)
        ASSERT_EQ(in(2), 17);
    EXPECT_FALSE(timers[1]->flags & TIMER_ENABLED);
    EXPECT_EQ(in(3) & 0x40, 0x40);
    EXPECT_EQ(in(2), 0xff);
}

TEST_F(PhilipsTest, InvalidAddressAndReadFailureExposeNoSector)
{
    for (auto msf : {
             std::array<uint8_t, 3> { 0,    0,    0 },
             { 0x75, 2,    0 },
             { 0,    0x60, 0 },
             { 0xfa, 2,    0 }
    }) {
        command({ 0x4e });
        command({ 0xa6, msf[0], msf[1], msf[2], 1, 0, 0 });
        EXPECT_NE(in(3) & 0x10, 0);
        EXPECT_FALSE(timers[1]->flags & TIMER_ENABLED);
    }
    EXPECT_TRUE(read_lbas.empty());
    command({ 0x4e });
    command({ 0xa6, 0, 2, 0, 1, 0, 0 });
    read_result = 0;
    tick(1);
    EXPECT_EQ(in(2), 0xff);
    EXPECT_NE(in(3) & 0x10, 0);
}

TEST_F(PhilipsTest, TocUsesDataFifoAndTrackReadyInterrupt)
{
    command({ 0xe5, 1 });
    out(7, 0x80);
    out(5, 1);
    tick(0);
    EXPECT_NE(irq, 0);
    EXPECT_EQ(in(3) & 0xc0, 0x80);
    EXPECT_EQ(in(3) & 0x80, 0);
    for (auto v : { 0, 1, 0, 2, 0, 0x40, 0 })
        EXPECT_EQ(in(2), v);
    EXPECT_EQ(in(3) & 0x40, 0x40);
}

TEST_F(PhilipsTest, ExtendReadPreservesUnreadSectorAndCompletedStream)
{
    command({ 0xa7, 1, 0, 0 });
    EXPECT_NE(in(3) & 0x10, 0);
    EXPECT_FALSE(timers[1]->flags & TIMER_ENABLED);
    command({ 0x4e });
    command({ 0xa6, 0x16, 2, 0, 1, 0, 0 });
    tick(1);
    command({ 0xa7, 1, 0, 0 });
    EXPECT_FALSE(timers[1]->flags & TIMER_ENABLED);
    for (int i = 0; i < 2352; i++)
        ASSERT_EQ(in(2), 16);
    tick(1);
    for (int i = 0; i < 2352; i++)
        ASSERT_EQ(in(2), 17);
    EXPECT_EQ(in(3) & 0x40, 0x40);
    command({ 0xa7, 1, 0, 0 });
    tick(1);
    for (int i = 0; i < 2352; i++)
        ASSERT_EQ(in(2), 18);
    EXPECT_EQ(read_lbas, (std::vector<int> { 16, 17, 18 }));
}

TEST_F(PhilipsTest, MixedModeTocReturnsBinaryPositionsAndAudioControl)
{
    ops.get_raw_track_info = [](const void *, int *num, uint8_t *buffer) {
        raw_track_info_t tracks[3] = { };
        for (auto &t : tracks)
            t.session = 1;
        tracks[0].point = 1;
        tracks[0].ps    = 2;
        tracks[1].point = 2;
        tracks[1].pm    = 19;
        tracks[1].ps    = 26;
        tracks[1].pf    = 55;
        tracks[2].point = 0xa2;
        tracks[2].pm    = 37;
        *num            = 3;
        std::memcpy(buffer, tracks, sizeof(tracks));
    };
    ops.get_track_info = [](const void *, uint32_t track, int, track_info_t *t) {
        if (track != 2)
            return 0;
        t->number = 2;
        t->attr   = 0x10;
        t->m      = 19;
        t->s      = 26;
        t->f      = 55;
        return 1;
    };
    command({ 0xe5, 2 });
    out(5, 2);
    tick(0);
    EXPECT_EQ(in(3) & 0x80, 0x80);
    for (auto v : { 0, 2, 55, 26, 19, 0, 0 })
        EXPECT_EQ(in(2), v);
    EXPECT_EQ(in(3) & 0x40, 0x40);
}

TEST_F(PhilipsTest, OriginalDriveExposesOnlyFirstSession)
{
    ops.get_raw_track_info = [](const void *, int *num, uint8_t *buffer) {
        raw_track_info_t tracks[4] = { };
        tracks[0].session = tracks[1].session = 1;
        tracks[2].session = tracks[3].session = 2;
        tracks[0].point                       = 1;
        tracks[0].ps                          = 2;
        tracks[1].point                       = 0xa2;
        tracks[1].ps                          = 3; // First session ends at LBA 75.
        tracks[2].point                       = 2;
        tracks[2].ps                          = 4;
        tracks[3].point                       = 0xa2;
        tracks[3].ps                          = 5;
        *num                                  = 4;
        std::memcpy(buffer, tracks, sizeof(tracks));
    };
    command({ 0x3a });
    std::array<uint8_t, 13> status;
    for (auto &v : status)
        v = byte(0x9c);
    EXPECT_EQ(status[8], 0x74);
    EXPECT_EQ(status[9], 2);
    EXPECT_EQ(status[10], 0);
    EXPECT_EQ(status[11], 1);
    EXPECT_EQ(status[12], 1);
    command({ 0xe5, 2, 2 });
    EXPECT_NE(in(3) & 0x10, 0);
    EXPECT_EQ(in(2), 0xff);
    command({ 0x4e });
    command({ 0xa6, 0, 3, 0, 1, 0, 0 });
    EXPECT_NE(in(3) & 0x10, 0);
    EXPECT_FALSE(timers[1]->flags & TIMER_ENABLED);
    EXPECT_TRUE(read_lbas.empty());
}

TEST_F(PhilipsTest, MediaChangeCancelsPartialCommandAndClearsLatchedStatus)
{
    command({ 0xa6 });
    out(5, 0);
    EXPECT_TRUE(timers[0]->flags & TIMER_ENABLED);
    cdrom[0].insert(cdrom[0].priv);
    EXPECT_FALSE(timers[0]->flags & TIMER_ENABLED);
    EXPECT_EQ(irq, 0);
    command({ 0x3a });
    for (int i = 0; i < 5; i++)
        byte(0x9c);
    EXPECT_EQ(byte(0x9c), 0x10);
    command({ 0x4e });
    command({ 0x3a });
    for (int i = 0; i < 5; i++)
        byte(0x9c);
    EXPECT_EQ(byte(0x9c), 0);
}

TEST_F(PhilipsTest, ResetCancelsPendingTimersAndShortReadFails)
{
    command({ 0xa6, 0, 2, 0, 1, 0, 0 });
    out(5, 0x3a);
    out(4, 0x61);
    EXPECT_FALSE(timers[0]->flags & TIMER_ENABLED);
    EXPECT_FALSE(timers[1]->flags & TIMER_ENABLED);
    EXPECT_EQ(in(2), 0xff);
    command({ 0xa6, 0, 2, 0, 1, 0, 0 });
    read_length = 2048;
    tick(1);
    EXPECT_EQ(in(2), 0xff);
    EXPECT_NE(in(3) & 0x10, 0);
}

TEST_F(PhilipsTest, UnsupportedAudioPacketsPreserveCommandBoundaries)
{
    command({ 0xc5 });
    EXPECT_EQ(in(3) & 0x10, 0);
    command({ 0x2d });
    EXPECT_NE(in(3) & 0x10, 0);
    EXPECT_EQ(byte(0x9c), 0); // The routing parameter was not an identify command.
    command({ 0x4e });
    command({ 0xb1, 0xa6, 0, 2, 0, 1, 0 });
    EXPECT_EQ(in(3) & 0x10, 0);
    command({ 0 });
    EXPECT_NE(in(3) & 0x10, 0);
    EXPECT_FALSE(timers[1]->flags & TIMER_ENABLED);
    command({ 0x4e });
    command({ 0x2d });
    EXPECT_EQ(byte(0x9c), 5);
}

TEST_F(PhilipsTest, MediaRemovalStopsReadAndCloseDetaches)
{
    command({ 0xa6, 0, 2, 0, 1, 0, 0 });
    cdrom[0].cd_status = CD_STATUS_EMPTY;
    cdrom[0].insert(cdrom[0].priv);
    EXPECT_FALSE(timers[1]->flags & TIMER_ENABLED);
    EXPECT_EQ(in(2), 0xff);
    command({ 0xa6, 0, 2, 0, 1, 0, 0 });
    EXPECT_NE(in(3) & 0x10, 0);
    philips_cm250_device.close(dev);
    dev = nullptr;
    EXPECT_EQ(removed, 1);
    EXPECT_EQ(cdrom[0].priv, nullptr);
    EXPECT_EQ(cdrom[0].insert, nullptr);
}
}
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
    timers.push_back(t);
    t->callback = callback;
    t->priv     = priv;
    t->flags    = start ? TIMER_ENABLED : 0;
}
void
timer_on_auto(pc_timer_t *t, double period)
{
    delay = period;
    timer_enable(t);
}
int
device_get_config_hex16(const char *)
{
    return 0x340;
}
void
pclog(const char *, ...)
{
}
void
io_sethandler(uint16_t base, uint16_t size, uint8_t (*rb)(uint16_t, void *),
              uint16_t (*)(uint16_t, void *), uint32_t (*)(uint16_t, void *),
              void (*wb)(uint16_t, uint8_t, void *),
              void (*)(uint16_t, uint16_t, void *),
              void (*)(uint16_t, uint32_t, void *), void *)
{
    EXPECT_EQ(base, 0x340);
    EXPECT_EQ(size, 8);
    read_port  = rb;
    write_port = wb;
}
void
io_removehandler(uint16_t, uint16_t, uint8_t (*)(uint16_t, void *),
                 uint16_t (*)(uint16_t, void *), uint32_t (*)(uint16_t, void *),
                 void (*)(uint16_t, uint8_t, void *),
                 void (*)(uint16_t, uint16_t, void *),
                 void (*)(uint16_t, uint32_t, void *), void *)
{
    ++removed;
}
int
cdrom_get_q(cdrom_t *, uint8_t *out, int cursor, uint8_t mode)
{
    EXPECT_EQ(mode, 1);
    std::memset(out, cursor, 10);
    return cursor + 1;
}
void
cdrom_stop(cdrom_t *)
{
}
void
cdrom_seek(cdrom_t *, uint32_t, uint8_t)
{
}
int
cdrom_readsector_raw(cdrom_t *, uint8_t *out, int lba, int is_msf, int type, int flags, int *len, uint8_t)
{
    EXPECT_EQ(is_msf, 0);
    EXPECT_EQ(type, 2);
    EXPECT_EQ(flags, 0xf8);
    read_lbas.push_back(lba);
    *len = read_length;
    std::memset(out, lba & 255, read_length);
    return read_result;
}
}
extern "C" int
device_get_config_int(const char *)
{
    return 5;
}
extern "C" void
picint_common(uint16_t num, int level, int set, uint8_t *)
{
    EXPECT_EQ(num, 1 << 5);
    EXPECT_EQ(level, 0);
    irq = set;
}
