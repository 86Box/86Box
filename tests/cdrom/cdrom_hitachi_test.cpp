#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/cdrom.h>
#include <86box/cdrom_hitachi.h>
#include <86box/timer.h>
}

namespace {
std::vector<pc_timer_t *> timers;
std::vector<int>          read_lbas;
uint8_t (*read_port)(uint16_t, void *);
void (*write_port)(uint16_t, uint8_t, void *);
cdrom_ops_t             ops { };
int                     read_result, read_length, removed;
double                  delay;
std::array<uint32_t, 2> sector_counts;

class HitachiTest : public ::testing::Test {
protected:
    void   *dev { };
    uint8_t select { };
    void    SetUp() override
    {
        std::memset(cdrom, 0, sizeof(cdrom));
        timers.clear();
        read_lbas.clear();
        read_result = 1;
        read_length = 2340;
        removed     = 0;
        delay       = 0;
        ops         = { };
        sector_counts.fill(10000);
        ops.get_track_info = [](const void *local, uint32_t track, int, track_info_t *ti) {
            EXPECT_EQ(track, 0xa2u);
            auto frames = *static_cast<const uint32_t *>(local) + 150;
            ti->number  = 0xa2;
            ti->m       = frames / 4500;
            ti->s       = (frames / 75) % 60;
            ti->f       = frames % 75;
            return 1;
        };
        for (unsigned i = 0; i < 2; ++i) {
            cdrom[i].bus_type        = CDROM_BUS_HITACHI;
            cdrom[i].hitachi_channel = i;
            cdrom[i].ops             = &ops;
            cdrom[i].local           = &sector_counts[i];
            cdrom[i].cd_status       = CD_STATUS_DATA_ONLY;
            cdrom[i].cdrom_capacity  = 10149; // Inclusive cache includes the pregap.
        }
        dev = hitachi_cdrom_device.init(&hitachi_cdrom_device);
    }
    void TearDown() override
    {
        if (dev)
            hitachi_cdrom_device.close(dev);
    }
    void    out(int reg, uint8_t v) { write_port(0x300 + reg, v, dev); }
    uint8_t in(int reg) { return read_port(0x300 + reg, dev); }
    void    command(std::initializer_list<uint8_t> bytes)
    {
        out(3, 0x82);
        for (auto b : bytes) {
            out(2, 0xa0 | select);
            out(0, b);
            out(2, 0xa1 | select);
            EXPECT_EQ(in(1) & 2, 2);
            out(2, 0xa0 | select);
            EXPECT_EQ(in(1) & 2, 0);
        }
        out(3, 0x92);
        out(2, 0xa2 | select);
    }
    uint8_t reply()
    {
        out(2, 0xa3 | select);
        auto v = in(0);
        out(2, 0xa2 | select);
        return v;
    }
    uint8_t status()
    {
        command({ 0x70 });
        return reply();
    }
    void tick(unsigned drive = 0)
    {
        auto *t = timers.at(drive);
        ASSERT_TRUE(t->flags & TIMER_ENABLED);
        timer_disable(t);
        t->callback(t->priv);
    }
    void ack()
    {
        out(2, 0xa6 | select);
        EXPECT_EQ(in(1) & 1, 0);
        out(2, 0xa2 | select);
    }
    void read(unsigned n, uint8_t expected)
    {
        for (unsigned i = 0; i < n; ++i)
            ASSERT_EQ(in(4), expected) << i;
    }
};

TEST_F(HitachiTest, StatusHandshakeAndEmptyMedia)
{
    command({ 0x60 });
    EXPECT_EQ(reply(), 4);
    EXPECT_EQ(status(), 1);
    cdrom[0].cd_status = CD_STATUS_EMPTY;
    command({ 0x60 });
    EXPECT_EQ(reply(), 0x80);
    command({ 0xff, 0x22, 0, 2, 0 });
    EXPECT_EQ(status(), 2);
    EXPECT_FALSE(timers[0]->flags & TIMER_ENABLED);
}

TEST_F(HitachiTest, CookedStreamWaitsForAcknowledgement)
{
    command({ 0xff, 0x22, 0, 2, 0x16 });
    EXPECT_NEAR(delay, 1000000.0 / 75, 0.001);
    EXPECT_EQ(status(), 0);
    EXPECT_EQ(in(1) & 1, 0);
    tick();
    EXPECT_EQ(status(), 4);
    EXPECT_EQ(in(1) & 1, 1);
    ack(); // The driver's pre-read pulse must not skip a sector.
    EXPECT_EQ(in(1) & 1, 1);
    EXPECT_EQ(read_lbas, (std::vector<int> { 16 }));
    read(2052, 16); // Four header bytes followed by 2048 cooked bytes.
    EXPECT_FALSE(timers[0]->flags & TIMER_ENABLED);
    ack();
    EXPECT_EQ(in(1) & 1, 0);
    tick();
    read(2052, 17);
    ack();
    tick();
    EXPECT_EQ(read_lbas, (std::vector<int> { 16, 17, 18 }));
    command({ 0xff, 0x18 });
    EXPECT_EQ(status(), 1);
    EXPECT_FALSE(timers[0]->flags & TIMER_ENABLED);
    EXPECT_EQ(in(4), 0xff);
}

TEST_F(HitachiTest, RawStreamAndEndOfDisc)
{
    sector_counts[0] = 17;
    command({ 0xff, 0x22, 0, 2, 0x16 });
    tick();
    read(2340, 16);
    EXPECT_EQ(in(4), 0xff);
    ack();
    tick();
    EXPECT_EQ(status(), 2);
    EXPECT_EQ(read_lbas, (std::vector<int> { 16 }));
    EXPECT_FALSE(timers[0]->flags & TIMER_ENABLED);
}

TEST_F(HitachiTest, InvalidPositionsNeverReadBackend)
{
    for (const auto &msf : std::array<std::array<uint8_t, 3>, 5> {
             { { 0, 0, 0 }, { 0, 0x60, 0 }, { 0, 2, 0x75 }, { 0, 2, 0xfa }, { 0x99, 0, 0 } }
    }) {
        command({ 0xff, 0x22, msf[0], msf[1], msf[2] });
        EXPECT_EQ(status(), 2);
        EXPECT_FALSE(timers[0]->flags & TIMER_ENABLED);
    }
    EXPECT_TRUE(read_lbas.empty());
}

TEST_F(HitachiTest, FailedAndShortReadsExposeNoData)
{
    for (int length : { 2340, 2048 }) {
        read_result = length == 2340 ? 0 : 1;
        read_length = length;
        command({ 0xff, 0x22, 0, 2, 0 });
        tick();
        EXPECT_EQ(status(), 2);
        EXPECT_EQ(in(1) & 1, 0);
        EXPECT_EQ(in(4), 0xff);
    }
}

TEST_F(HitachiTest, MediaChangeCancelsPendingReadAndLatchesOnce)
{
    command({ 0xff, 0x22, 0, 2, 0 });
    cdrom[0].insert(cdrom[0].priv);
    EXPECT_FALSE(timers[0]->flags & TIMER_ENABLED);
    command({ 0x60 });
    EXPECT_EQ(reply(), 0x24);
    command({ 0x60 });
    EXPECT_EQ(reply(), 4);
    EXPECT_EQ(status(), 1);
}

TEST_F(HitachiTest, DriveSelectionIsolatesStreams)
{
    command({ 0xff, 0x22, 0, 2, 0x16 });
    tick();
    select = 8;
    command({ 0xff, 0x22, 0, 2, 0x32 });
    tick(1);
    read(2052, 32);
    ack();
    tick(1);
    select = 0;
    out(2, 0xa2);
    read(2052, 16);
    EXPECT_EQ(read_lbas, (std::vector<int> { 16, 32, 33 }));
    select = 16;
    out(2, 0xa2 | select);
    EXPECT_EQ(in(1), 0xff);
}

TEST_F(HitachiTest, ResetDropsPartialCommandAndReadState)
{
    command({ 0xff, 0x22, 0, 2, 0 });
    command({ 0xff, 0x30 });
    hitachi_cdrom_device.reset(dev);
    EXPECT_FALSE(timers[0]->flags & TIMER_ENABLED);
    command({ 0x60 });
    EXPECT_EQ(reply(), 4);
    EXPECT_EQ(status(), 1);
}

TEST_F(HitachiTest, VolumeLengthAndUnsupportedCommandFraming)
{
    command({ 0xff, 0x30, 0x90, 0x90, 0x82 });
    std::array<uint8_t, 192> data;
    for (auto &v : data)
        v = reply();
    EXPECT_EQ((data[15] * 256u + data[16]) * 75 + data[17], 10000u);
    command({ 0xff, 0xe0, 0, 2, 0, 0, 3, 0 });
    EXPECT_EQ(status(), 0x40);
    command({ 0xff, 0x18 });
    EXPECT_EQ(status(), 1);
}

TEST_F(HitachiTest, LeadInScanRestartsOnMediaChange)
{
    ops.get_raw_track_info = [](const void *, int *, uint8_t *) { };
    command({ 0xff, 0xc0 });
    for (uint8_t i = 0; i < 3; ++i) {
        command({ 0x50 });
        for (unsigned j = 0; j < 10; ++j)
            EXPECT_EQ(reply(), i);
    }
    cdrom[0].insert(cdrom[0].priv);
    command({ 0x50 });
    for (unsigned j = 0; j < 10; ++j)
        EXPECT_EQ(reply(), 0);
}

TEST_F(HitachiTest, CloseRemovesIoAndCallbacks)
{
    command({ 0xff, 0x22, 0, 2, 0 });
    hitachi_cdrom_device.close(dev);
    dev = nullptr;
    EXPECT_EQ(removed, 1);
    EXPECT_EQ(cdrom[0].insert, nullptr);
    EXPECT_EQ(cdrom[0].priv, nullptr);
    EXPECT_EQ(cdrom[1].insert, nullptr);
    EXPECT_EQ(cdrom[1].priv, nullptr);
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
    return 0x300;
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
    EXPECT_EQ(base, 0x300);
    EXPECT_EQ(size, 16);
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
    EXPECT_EQ(flags, 0x78);
    read_lbas.push_back(lba);
    *len = read_length;
    std::memset(out, lba & 255, read_length);
    return read_result;
}
}
