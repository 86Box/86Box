#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/cdrom.h>
#include <86box/cdrom_sony.h>
#include <86box/dma.h>
#include <86box/timer.h>
}

namespace {
std::vector<pc_timer_t *> timers;
std::vector<int>          lbas;
std::vector<uint8_t>      dma_bytes;
uint8_t (*read_port)(uint16_t, void *);
void (*write_port)(uint16_t, uint8_t, void *);
cdrom_ops_t ops;
uint16_t    base;
int         irq, drq, dma_channel, removed, read_ok, short_read, track_count;
int         config_irq, config_base, irq_line;
size_t      dma_limit;
bool        double_speed, dma_masked, audio_track;

class SonyTest : public ::testing::Test {
protected:
    void *dev = nullptr;
    void  SetUp() override
    {
        std::memset(cdrom, 0, sizeof(cdrom));
        timers.clear();
        lbas.clear();
        dma_bytes.clear();
        irq = drq = removed = short_read = 0;
        config_irq                       = 5;
        config_base                      = 0x340;
        irq_line                         = 0;
        dma_limit                        = 2048;
        dma_channel                      = -1;
        read_ok = track_count = 1;
        double_speed = dma_masked = audio_track = false;
        ops                                     = { };
        ops.get_track_info                      = [](const void *, uint32_t n, int, track_info_t *t) {
            if (n != 0xa2 && (n < 1 || n > (unsigned) track_count))
                return 0;
            unsigned f = n == 0xa2 ? 10150 : 150 + (n - 1) * 75;
            *t         = { };
            t->number  = n;
            t->attr    = audio_track ? 0x10 : 0x14;
            t->m       = f / 4500;
            t->s       = f / 75 % 60;
            t->f       = f % 75;
            return 1;
        };
        ops.get_track_type = [](const void *, uint32_t) -> uint8_t {
            return audio_track ? CD_TRACK_AUDIO : CD_TRACK_NORMAL;
        };
        cdrom[0].bus_type  = CDROM_BUS_SONY;
        cdrom[0].ops       = &ops;
        cdrom[0].cd_status = CD_STATUS_DATA_ONLY;
        init();
    }
    void TearDown() override
    {
        if (dev)
            sony_cdu31a_device.close(dev);
    }
    void init(const device_t *device = &sony_cdu31a_device)
    {
        if (dev)
            sony_cdu31a_device.close(dev);
        timers.clear();
        dev = device->init(device);
        out(3, 1);
        EXPECT_EQ(in(1), 0x80);
    }
    void    out(int reg, uint8_t value) { write_port(base + reg, value, dev); }
    uint8_t in(int reg) { return read_port(base + reg, dev); }
    void    tick(unsigned n)
    {
        auto *t = timers.at(n);
        ASSERT_TRUE(t->flags & TIMER_ENABLED);
        timer_disable(t);
        t->callback(t->priv);
    }
    void command(uint8_t cmd, std::initializer_list<uint8_t> params = { })
    {
        out(3, 0x42);
        for (uint8_t p : params)
            out(1, p);
        out(0, cmd);
        EXPECT_NE(in(0) & 0x80, 0);
        tick(0);
    }
    std::vector<uint8_t> result()
    {
        EXPECT_NE(in(0) & 2, 0);
        out(3, 2);
        std::vector<uint8_t> r { in(1) };
        if ((r[0] & 0xf0) == 0x50)
            return r;
        r.push_back(in(1));
        if ((r[0] & 0xf0) == 0x20)
            return r;
        unsigned len = ((r[0] & 15) << 8) | r[1];
        for (unsigned i = 0; i < len; ++i) {
            if ((i + 2) % 10 == 0) {
                EXPECT_NE(in(0) & 2, 0);
                out(3, 2);
            }
            r.push_back(in(1));
        }
        return r;
    }
    void ok() { EXPECT_EQ(result(), (std::vector<uint8_t> { 0, 0 })); }
};

TEST_F(SonyTest, IdentifyBothModelsAcrossTenByteResultBatches)
{
    for (int model = 0; model < 2; ++model) {
        double_speed = model;
        init();
        command(0);
        auto r = result();
        ASSERT_EQ(r.size(), 36U);
        EXPECT_EQ(std::string(r.begin() + 2, r.begin() + 6), "SONY");
        EXPECT_EQ(std::string(r.begin() + 10, r.begin() + 23), model ? "CD-ROM CDU33A" : "CD-ROM CDU31A");
        EXPECT_EQ(r[34] & 0x10, model ? 0x10 : 0);
        EXPECT_EQ(in(0) & 2, 0);
        EXPECT_EQ(in(1), 0xff);
        command(0x10, { 5, 7 });
        ok();
        EXPECT_EQ(cdrom[0].cur_speed, model ? 2 : 1);
    }
}

TEST_F(SonyTest, SlcdDetectionFillsExactlyTenParameterBytes)
{
    EXPECT_EQ(in(3), 0xf3);
    out(1, 0x55);
    EXPECT_EQ(in(3), 0xf1);
    for (int i = 1; i < 10; ++i)
        out(1, 0x55);
    EXPECT_EQ(in(3), 0xf0);
    out(3, 0x40);
    EXPECT_EQ(in(3), 0xf3);
}

TEST_F(SonyTest, Nt31ProbeReadsResetAttentionBeforeAcknowledging)
{
    out(3, 0x80);
    EXPECT_EQ(in(0) & 1, 1);
    EXPECT_EQ(in(1), 0x80);
    EXPECT_EQ(in(0) & 1, 1);
    out(3, 1);
    EXPECT_EQ(in(0) & 1, 0);
    EXPECT_EQ(in(3), 0xf3);
    out(1, 0x55);
    EXPECT_EQ(in(3), 0xf1);
    for (int i = 0; i < 9; ++i)
        out(1, 0x55);
    EXPECT_EQ(in(3), 0xf0);
    out(3, 0x80);
    EXPECT_EQ(in(1), 0x80);
    out(3, 1);
    command(0);
    EXPECT_EQ(result().size(), 36U);
}

TEST_F(SonyTest, QueuedAttentionsAcceptEitherAcknowledgmentOrder)
{
    for (bool read_first : { false, true }) {
        cdrom[0].cd_status = CD_STATUS_DATA_ONLY;
        out(3, 0x80);
        cdrom[0].cd_status = CD_STATUS_EMPTY;
        cdrom[0].insert(dev);
        out(3, 8);
        for (uint8_t expected : { 0x80, 0x28 }) {
            EXPECT_EQ(irq, 1);
            EXPECT_EQ(in(3) & 4, 4);
            if (read_first) {
                EXPECT_EQ(in(1), expected);
                EXPECT_EQ(in(3) & 4, 0);
                EXPECT_EQ(irq, 1);
                out(3, 9);
            } else {
                out(3, 9);
                EXPECT_EQ(irq, 0);
                EXPECT_EQ(in(1), expected);
            }
        }
        EXPECT_EQ(irq, 0);
        EXPECT_EQ(in(0) & 1, 0);
        EXPECT_EQ(in(3), 0xf3);
        EXPECT_EQ(in(1), 0xff);
    }
}

TEST_F(SonyTest, CreativeHasSelectableAddressAndNoInterruptOrDma)
{
    const auto *config = sony_creative_device.config;
    ASSERT_NE(config, nullptr);
    EXPECT_STREQ(config[0].name, "base");
    EXPECT_EQ(config[0].type, CONFIG_HEX16);
    EXPECT_EQ(config[0].default_int, 0x230);
    EXPECT_EQ(config[1].type, CONFIG_END);
    dma_channel = 3;
    const std::array<int, 4> bases{ 0x230, 0x250, 0x270, 0x290 };
    for (unsigned i = 0; i < bases.size(); ++i) {
        EXPECT_EQ(config[0].selection[i].value, bases[i]);
        config_base = bases[i];
        init(&sony_creative_device);
        EXPECT_EQ(base, bases[i]);
        out(3, 0x38);
        out(0, 0);
        tick(0);
        EXPECT_EQ(irq, 0);
        result();
        command(0x10, { 1, 1 });
        ok();
        command(0x32, { 0, 2, 0, 0, 0, 1 });
        tick(1);
        EXPECT_EQ(drq, 0);
        EXPECT_FALSE(timers[2]->flags & TIMER_ENABLED);
        for (unsigned n = 0; n < 2048; ++n)
            EXPECT_EQ(in(2), n & 255);
        ok();
    }
    EXPECT_EQ(config[0].selection[4].description, nullptr);
}

TEST_F(SonyTest, ResultInterruptCanBeEnabledAndAcknowledgedWithoutDiscardingData)
{
    out(3, 0x50);
    out(0, 0);
    tick(0);
    EXPECT_EQ(irq, 1);
    EXPECT_EQ(in(0) & 0x12, 0x12);
    out(3, 0x12);
    EXPECT_EQ(irq, 0);
    EXPECT_EQ(in(1), 0);
    EXPECT_EQ(in(1), 34);
    for (int i = 0; i < 8; ++i)
        in(1);
    EXPECT_EQ(irq, 1);
    out(3, 2);
    EXPECT_EQ(irq, 0);
}

TEST_F(SonyTest, AdapterResourcesAndAttentionAndDataInterrupts)
{
    const std::array<int, 4> bases { 0x320, 0x330, 0x340, 0x360 };
    for (int i = 0; i < 4; ++i) {
        config_base = bases[i];
        config_irq  = i + 3;
        init();
        EXPECT_EQ(base, bases[i]);
        cdrom[0].insert(dev);
        out(3, 8);
        EXPECT_EQ(irq, 1);
        EXPECT_EQ(irq_line, 1 << config_irq);
        out(3, 9);
        EXPECT_EQ(irq, 0);
        EXPECT_EQ(in(1), 0x80);
        command(0x32, { 0, 2, 0, 0, 0, 1 });
        out(3, 0x20);
        tick(1);
        EXPECT_EQ(irq, 1);
        EXPECT_EQ(irq_line, 1 << config_irq);
        out(3, 0x24);
        EXPECT_EQ(irq, 0);
        EXPECT_EQ(in(0) & 0x40, 0x40);
        out(3, 0x80);
        EXPECT_FALSE(timers[1]->flags & TIMER_ENABLED);
    }
}

TEST_F(SonyTest, Nt31InterruptPollingPreservesEnablesBeforeDataIsReady)
{
    out(3, 0x38);
    EXPECT_EQ(irq, 0);
    EXPECT_EQ(in(0) & 0x38, 0x38);
    command(0x32, { 0, 2, 0, 0, 0, 1 });
    out(3, 0x38);
    /* Setup polls the ISR before the sector timer fires. It saves the
       enables from status, disables interrupts, then restores the enables. */
    uint8_t enables = in(0) & 0x38;
    out(3, 0);
    EXPECT_EQ(in(0) & 0x38, 0);
    out(3, enables);
    EXPECT_EQ(irq, 0);
    tick(1);
    EXPECT_EQ(irq, 1);
    EXPECT_EQ(in(0) & 0x3c, 0x3c);
    out(3, 0x3c);
    EXPECT_EQ(irq, 0);
    EXPECT_EQ(in(0) & 0x38, 0x38);
}

TEST_F(SonyTest, DmaTerminalCountInMiddleOfSectorPreservesRemainingBytes)
{
    for (int channel = 1; channel <= 3; ++channel) {
        dma_channel = channel;
        dma_bytes.clear();
        dma_masked = false;
        dma_limit  = 513;
        init();
        command(0x10, { 1, 1 });
        ok();
        command(0x32, { 0, 2, 0, 0, 0, 1 });
        tick(1);
        while (!dma_masked)
            tick(2);
        EXPECT_EQ(dma_bytes.size(), 513U);
        tick(2);
        EXPECT_EQ(dma_bytes.size(), 513U);
        EXPECT_EQ(drq, 1);
        EXPECT_EQ(in(0) & 2, 0);
        dma_limit  = 2048;
        dma_masked = false;
        while (timers[2]->flags & TIMER_ENABLED)
            tick(2);
        ASSERT_EQ(dma_bytes.size(), 2048U);
        for (unsigned i = 0; i < 2048; ++i)
            EXPECT_EQ(dma_bytes[i], i & 255);
        EXPECT_EQ(drq, 0);
        ok();
    }
}

TEST_F(SonyTest, TocContainsBcdTracksAndLeadoutAndSupportsLongReplies)
{
    track_count = 60;
    command(0x20);
    EXPECT_EQ(result(), (std::vector<uint8_t> { 0x20, 0x60 }));
    command(0x30);
    ok();
    command(0x24, { 1 });
    auto r = result();
    ASSERT_EQ(r.size(), 318U);
    EXPECT_EQ(r[0], 1);
    EXPECT_EQ(r[2], 1);
    EXPECT_EQ(r[3], 0x41);
    EXPECT_EQ(r[5], 1);
    EXPECT_EQ(r[10], 0x60);
    EXPECT_EQ(r[14], 0xa2);
    EXPECT_EQ(r[15], 2);
    EXPECT_EQ(r[16], 0x15);
    EXPECT_EQ(r[17], 0x25);
    EXPECT_EQ(r[314], 0x60);
    command(0x24, { 2 });
    EXPECT_EQ(result(), (std::vector<uint8_t> { 0x20, 0x11 }));
}

TEST_F(SonyTest, MultisectorPioWaitsForConsumptionAndReturnsBlockThenFinalStatus)
{
    command(0x34, { 0, 2, 0, 0, 0, 2 });
    tick(1);
    EXPECT_EQ(lbas, (std::vector<int> { 0 }));
    EXPECT_EQ(in(0) & 0x44, 0x44);
    out(3, 4);
    EXPECT_EQ(in(0) & 0x44, 0x40);
    for (unsigned i = 0; i < 2048; ++i)
        EXPECT_EQ(in(2), i & 255);
    EXPECT_EQ(result(), (std::vector<uint8_t> { 0x54 }));
    tick(1);
    for (unsigned i = 0; i < 2048; ++i)
        EXPECT_EQ(in(2), (i + 17) & 255);
    EXPECT_EQ(result(), (std::vector<uint8_t> { 0x54 }));
    ok();
    EXPECT_EQ(in(2), 0xff);
    EXPECT_EQ(lbas, (std::vector<int> { 0, 1 }));
    EXPECT_FALSE(timers[1]->flags & TIMER_ENABLED);
}

TEST_F(SonyTest, DmaPreservesMaskedDataAndCompletesSector)
{
    dma_channel = 3;
    init();
    command(0x10, { 1, 1 });
    ok();
    command(0x32, { 0, 2, 0, 0, 0, 1 });
    tick(1);
    EXPECT_EQ(drq, 1);
    dma_masked = true;
    tick(2);
    EXPECT_TRUE(dma_bytes.empty());
    dma_masked = false;
    for (int i = 0; i < 16; ++i)
        tick(2);
    ASSERT_EQ(dma_bytes.size(), 2048U);
    for (unsigned i = 0; i < dma_bytes.size(); ++i)
        EXPECT_EQ(dma_bytes[i], i & 255);
    EXPECT_EQ(drq, 0);
    ok();
}

TEST_F(SonyTest, InvalidRangesOverflowAndShortReadsDoNotExposeOldData)
{
    command(0x32, { 0, 0, 0, 0, 0, 1 });
    EXPECT_EQ(result(), (std::vector<uint8_t> { 0x20, 0x11 }));
    command(0x32, { 0, 2, 0, 0xff, 0xff, 0xff });
    EXPECT_EQ(result(), (std::vector<uint8_t> { 0x20, 0x46 }));
    command(0x32, { 0, 2, 0, 0, 0, 1 });
    short_read = 1;
    tick(1);
    EXPECT_EQ(result(), (std::vector<uint8_t> { 0x20, 0x57 }));
    EXPECT_EQ(in(2), 0xff);
    out(3, 0x40);
    for (int i = 0; i < 17; ++i)
        out(1, i);
    out(0, 0x10);
    tick(0);
    EXPECT_EQ(result(), (std::vector<uint8_t> { 0x20, 0x11 }));
}

TEST_F(SonyTest, RawDataOmitsSyncAndAudioIncludesAll2352Bytes)
{
    command(0x10, { 0, 7 });
    ok();
    command(0x34, { 0, 2, 0, 0, 0, 1 });
    tick(1);
    for (unsigned i = 12; i < 2352; ++i)
        EXPECT_EQ(in(2), i & 255);
    EXPECT_EQ(result(), (std::vector<uint8_t> { 0x54 }));
    ok();
    audio_track = true;
    command(0x34, { 0, 2, 0, 0, 0, 1 });
    tick(1);
    for (unsigned i = 0; i < 2352; ++i)
        EXPECT_EQ(in(2), i & 255);
    EXPECT_EQ(result(), (std::vector<uint8_t> { 0x50 }));
    ok();
}

TEST_F(SonyTest, ResetAndMediaRemovalCancelTimersAndLatchAttention)
{
    command(0x32, { 0, 2, 0, 0, 0, 2 });
    tick(1);
    in(2);
    out(3, 0x80);
    EXPECT_EQ(in(2), 0xff);
    for (auto *t : timers)
        EXPECT_FALSE(t->flags & TIMER_ENABLED);
    out(3, 1);
    EXPECT_EQ(in(1), 0x80);
    command(0x32, { 0, 2, 0, 0, 0, 1 });
    cdrom[0].cd_status = CD_STATUS_EMPTY;
    cdrom[0].insert(dev);
    EXPECT_EQ(in(0) & 0xc6, 0);
    out(3, 1);
    EXPECT_EQ(in(1), 0x28);
    command(0x51);
    EXPECT_EQ(result(), (std::vector<uint8_t> { 0x20, 0x21 }));
    sony_cdu31a_device.close(dev);
    dev = nullptr;
    EXPECT_EQ(cdrom[0].priv, nullptr);
    EXPECT_EQ(cdrom[0].insert, nullptr);
}

TEST_F(SonyTest, MechanicalStatusReportsClosedSpinningDiscAndToc)
{
    command(0x03);
    EXPECT_EQ(result(), (std::vector<uint8_t> { 0, 3, 3, 0, 0 }));
    command(0x10, { 5, 3 });
    ok();
    command(0x51);
    ok();
    command(0x03);
    EXPECT_EQ(result(), (std::vector<uint8_t> { 0, 3, 0x1b, 0, 0 }));
    EXPECT_EQ(in(3), 0xf3);
    command(0x50);
    EXPECT_NE(in(0) & 1, 0);
    out(3, 1);
    EXPECT_EQ(in(1), 0x28);
    ok();
    command(0x03);
    EXPECT_EQ(result(), (std::vector<uint8_t> { 0, 3, 0, 0, 0 }));
}

TEST_F(SonyTest, MediaInsertionHonorsAutoSpinUpAndReportsCompletion)
{
    for (bool model : { false, true }) {
        for (bool read_first : { false, true }) {
            double_speed = model;
            init();
            command(0x10, { 5, 7 });
            ok();
            cdrom[0].cd_status = CD_STATUS_EMPTY;
            cdrom[0].insert(dev);
            out(3, 1);
            EXPECT_EQ(in(1), 0x28);

            out(3, 8);
            cdrom[0].cd_status = CD_STATUS_DATA_ONLY;
            cdrom[0].insert(dev);
            for (uint8_t expected : { 0x80, 0x24, 0x62 }) {
                EXPECT_EQ(irq, 1);
                EXPECT_EQ(in(0) & 1, 1);
                if (read_first) {
                    EXPECT_EQ(in(1), expected);
                    out(3, 9);
                } else {
                    out(3, 9);
                    EXPECT_EQ(in(1), expected);
                }
            }
            EXPECT_EQ(irq, 0);
            EXPECT_EQ(in(0) & 1, 0);
            EXPECT_EQ(cdrom[0].cur_speed, model ? 2 : 1);
            command(0x03);
            EXPECT_EQ(result(), (std::vector<uint8_t> { 0, 3, 0x1b, 0, 0 }));
            command(0x20);
            EXPECT_EQ(result().size(), 22U);
        }
    }
}

TEST_F(SonyTest, MediaInsertionWithoutAutoSpinUpWaitsForExplicitCommands)
{
    /* Auto-eject and double-speed must not imply automatic spin-up. */
    for (uint8_t mechanical : { 0, 2, 4, 6 }) {
        command(0x10, { 5, mechanical });
        ok();
        command(0x30);
        ok();
        cdrom[0].insert(dev);
        out(3, 1);
        EXPECT_EQ(in(1), 0x80);
        EXPECT_EQ(in(0) & 1, 0);
        command(0x03);
        EXPECT_EQ(result(), (std::vector<uint8_t> { 0, 3, 3, 0, 0 }));
        command(0x20);
        EXPECT_EQ(result(), (std::vector<uint8_t> { 0x20, 0x60 }));
        command(0x30);
        ok();
        command(0x20);
        EXPECT_EQ(result().size(), 22U);
    }
}

TEST_F(SonyTest, AudioPositionPauseResumeCompletionAndMixerRouting)
{
    audio_track = true;
    command(0x40, { 3, 0, 2, 0, 0, 4, 0 });
    ok();
    EXPECT_EQ(cdrom[0].cd_end, 151U);
    command(0x03);
    EXPECT_EQ(result(), (std::vector<uint8_t> { 0, 3, 0x0b, 0x10, 0 }));
    cdrom[0].seek_pos = 30;
    command(0x21);
    EXPECT_EQ(result(), (std::vector<uint8_t> { 0, 10, 1, 1, 1, 0, 0, 0x30, 0, 0, 2, 0x30 }));
    command(0x41);
    ok();
    EXPECT_EQ(cdrom[0].cd_status, CD_STATUS_PAUSED);
    EXPECT_EQ(cdrom[0].seek_pos, 30U);
    command(0x03);
    EXPECT_EQ(result(), (std::vector<uint8_t> { 0, 3, 0x0b, 0, 0 }));
    command(0x40, { 3, 0, 2, 0x30, 0, 4, 0 });
    ok();
    EXPECT_EQ(cdrom[0].seek_pos, 30U);
    command(0x10, { 3, 6 });
    ok();
    command(0x10, { 4, 64, 192 });
    ok();
    EXPECT_EQ(cdrom[0].get_channel(dev, 0), 2U);
    EXPECT_EQ(cdrom[0].get_channel(dev, 1), 1U);
    EXPECT_EQ(cdrom[0].get_volume(dev, 0), 64U);
    EXPECT_EQ(cdrom[0].get_volume(dev, 1), 192U);
    cdrom[0].cd_status = CD_STATUS_PLAYING_COMPLETED;
    EXPECT_EQ(in(0) & 1, 1);
    out(3, 1);
    EXPECT_EQ(in(1), 0x90);
    command(0x03);
    EXPECT_EQ(result(), (std::vector<uint8_t> { 0, 3, 0x0b, 0, 0 }));
}

TEST_F(SonyTest, AudioRangeIncludesLastFrameAndExcludesLeadout)
{
    audio_track = true;
    command(0x40, { 3, 0, 2, 0x10, 0, 2, 0x10 });
    ok();
    EXPECT_EQ(cdrom[0].seek_pos, 10U);
    EXPECT_EQ(cdrom[0].cd_end, 11U);
    command(0x40, { 3, 0, 2, 0x10, 0, 2, 9 });
    EXPECT_EQ(result(), (std::vector<uint8_t> { 0x20, 0x11 }));
    command(0x40, { 3, 2, 0x15, 0x24, 2, 0x15, 0x24 });
    ok();
    EXPECT_EQ(cdrom[0].seek_pos, 9999U);
    EXPECT_EQ(cdrom[0].cd_end, 10000U);
    command(0x40, { 3, 2, 0x15, 0x24, 2, 0x15, 0x25 });
    EXPECT_EQ(result(), (std::vector<uint8_t> { 0x20, 0x11 }));
}

TEST_F(SonyTest, InterfaceWithoutDriveNeverRespondsToProbe)
{
    sony_cdu31a_device.close(dev);
    dev               = nullptr;
    cdrom[0].bus_type = CDROM_BUS_DISABLED;
    dev               = sony_cdu31a_device.init(&sony_cdu31a_device);
    out(3, 0x80);
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(in(i), 0xff);
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
timer_add(pc_timer_t *t, void (*cb)(void *), void *priv, int start)
{
    timers.push_back(t);
    t->callback = cb;
    t->priv     = priv;
    t->flags    = start ? TIMER_ENABLED : 0;
}
int
device_get_config_hex16(const char *)
{
    return config_base;
}
int
device_get_config_int(const char *name)
{
    return !strcmp(name, "irq") ? config_irq : dma_channel;
}
char *
cdrom_get_internal_name(int)
{
    static char single[] = "sony_cdu31a", dual[] = "sony_cdu33a";
    return double_speed ? dual : single;
}
void
pclog(const char *, ...)
{
}
void
ui_sb_update_icon(int, int)
{
}
void
io_sethandler(uint16_t b, uint16_t size, uint8_t (*rb)(uint16_t, void *),
              uint16_t (*)(uint16_t, void *), uint32_t (*)(uint16_t, void *),
              void (*wb)(uint16_t, uint8_t, void *), void (*)(uint16_t, uint16_t, void *),
              void (*)(uint16_t, uint32_t, void *), void *)
{
    base = b;
    EXPECT_EQ(size, 8);
    read_port  = rb;
    write_port = wb;
}
void
io_removehandler(uint16_t, uint16_t, uint8_t (*)(uint16_t, void *),
                 uint16_t (*)(uint16_t, void *), uint32_t (*)(uint16_t, void *),
                 void (*)(uint16_t, uint8_t, void *), void (*)(uint16_t, uint16_t, void *),
                 void (*)(uint16_t, uint32_t, void *), void *)
{
    ++removed;
}
void
picint_common(uint16_t num, int level, int set, uint8_t *)
{
    EXPECT_TRUE(num == (1 << 3) || num == (1 << 4) || num == (1 << 5) || num == (1 << 6));
    EXPECT_EQ(level, 0);
    irq_line = num;
    irq      = set;
}
void
dma_set_drq(int channel, int set)
{
    EXPECT_GE(channel, 1);
    EXPECT_LE(channel, 3);
    drq = set;
}
int
dma_channel_write(int channel, uint16_t value)
{
    EXPECT_EQ(channel, dma_channel);
    if (dma_masked)
        return DMA_NODATA;
    dma_bytes.push_back(value);
    if (dma_bytes.size() == dma_limit) {
        dma_masked = true;
        return DMA_OVER;
    }
    return 0;
}
void
cdrom_stop(cdrom_t *d)
{
    if (d->cd_status > CD_STATUS_DVD)
        d->cd_status = CD_STATUS_STOPPED;
}
void
cdrom_eject(uint8_t id)
{
    cdrom[id].cd_status = CD_STATUS_EMPTY;
    cdrom[id].insert(cdrom[id].priv);
}
uint8_t
cdrom_audio_play(cdrom_t *d, uint32_t pos, uint32_t len, int)
{
    if (!audio_track)
        return 0;
    d->seek_pos  = pos;
    d->cd_end    = pos + len;
    d->cd_status = CD_STATUS_PLAYING;
    return 1;
}
void
cdrom_audio_pause_resume(cdrom_t *d, uint8_t resume)
{
    d->cd_status = resume ? CD_STATUS_PLAYING : CD_STATUS_PAUSED;
}
int
cdrom_readsector_raw(cdrom_t *, uint8_t *b, int lba, int msf, int type, int flags, int *len, uint8_t)
{
    EXPECT_EQ(msf, 0);
    EXPECT_EQ(type, 0);
    lbas.push_back(lba);
    *len = (flags == 0xf8 ? 2352 : 2048) - short_read;
    for (int i = 0; i < *len; ++i)
        b[i] = (i + 17 * lba) & 255;
    return read_ok;
}
}
