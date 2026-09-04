#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
#define fallthrough [[fallthrough]]
#define calloc(count, size) (mcd_t *) calloc(count, size)
#include "../src/cdrom/cdrom_mitsumi.c"
#undef calloc
#undef fallthrough
}

namespace {

struct MockState {
    uint32_t irq_asserted{};
    uint32_t irq_cleared{};
    std::array<int, 8> drq{};
    std::vector<uint16_t> dma_values;
    int dma_result{};
    int stop_calls{};
    int reload_calls{};
    int eject_calls{};
    int read_result{1};
    int read_length{COOKED_SECTOR_SIZE};
    uint32_t last_seek{};
    uint8_t track_type{CD_TRACK_UNK_DATA};
} mock;

cdrom_ops_t ops{};

void reset_mocks()
{
    mock = MockState{};
    mock.read_result = 1;
    mock.read_length = COOKED_SECTOR_SIZE;
    mock.track_type = CD_TRACK_UNK_DATA;
    std::memset(dma, 0, sizeof(dma));
    dma_m = dma_e = 0;
}

class MitsumiTest : public ::testing::Test {
protected:
    cdrom_t cd{};
    mcd_t dev{};

    void SetUp() override
    {
        reset_mocks();
        ops.get_track_type = [](const void *, uint32_t) { return mock.track_type; };
        cd.ops = &ops;
        cd.cd_status = CD_STATUS_DATA_ONLY;
        cd.cdrom_capacity = 10000;
        dev.cdrom_dev = &cd;
        dev.irq = 10;
        dev.dma = 5;
        mitsumi_cdrom_reset(&dev);
        mock.irq_cleared = 0;
        mock.stop_calls = 0;
    }

    void command(uint8_t cmd, std::initializer_list<uint8_t> args = {})
    {
        mitsumi_cdrom_out(0, cmd, &dev);
        for (uint8_t arg : args)
            mitsumi_cdrom_out(0, arg, &dev);
    }

    std::vector<uint8_t> response()
    {
        std::vector<uint8_t> out;
        while (!(mitsumi_cdrom_get_flags(&dev) & FLAG_NOSTAT))
            out.push_back(mitsumi_cdrom_in(0, &dev));
        return out;
    }
};

TEST(MitsumiConversion, ConvertsValidBcdMsfAndRejectsInvalidAddresses)
{
    uint32_t lba = 0xdeadbeef;
    EXPECT_TRUE(mitsumi_msf_to_lba(0x000200, &lba));
    EXPECT_EQ(lba, 0u);
    EXPECT_TRUE(mitsumi_msf_to_lba(0x010000, &lba));
    EXPECT_EQ(lba, 4350u);

    for (uint32_t invalid : { 0x000199u, 0x000159u, 0x000175u, 0x006000u,
                              0x00017au, 0x0a0200u, 0x000000u }) {
        lba = 1234;
        EXPECT_FALSE(mitsumi_msf_to_lba(invalid, &lba)) << std::hex << invalid;
        EXPECT_EQ(lba, 1234u);
    }
}

TEST(MitsumiConversion, DecodesBiasedDmaCount)
{
    mcd_t dev{};
    dev.dmalen = 0;
    EXPECT_EQ(mitsumi_dma_length(&dev), 0u);
    dev.dmalen = 7;
    EXPECT_EQ(mitsumi_dma_length(&dev), 0u);
    dev.dmalen = 0x0807;
    EXPECT_EQ(mitsumi_dma_length(&dev), 2048u);
    dev.dmalen = 0x0937;
    EXPECT_EQ(mitsumi_dma_length(&dev), 2352u);
}

TEST_F(MitsumiTest, StatusReflectsMediaTrayChangeAndPlayback)
{
    EXPECT_EQ(mitsumi_status(&dev), STAT_READY | STAT_SERVO | STAT_CHANGE);
    cd.cd_status = CD_STATUS_PLAYING;
    EXPECT_EQ(mitsumi_status(&dev), STAT_READY | STAT_SERVO | STAT_DISK_CDDA |
                                    STAT_PLAY_CDDA | STAT_CHANGE);
    cd.cd_status = CD_STATUS_PLAYING;
    EXPECT_TRUE(mitsumi_status(&dev) & STAT_PLAY_CDDA);
    dev.tray_open = 1;
    cd.cd_status = CD_STATUS_EMPTY;
    EXPECT_EQ(mitsumi_status(&dev), STAT_OPEN | STAT_CHANGE);
    EXPECT_EQ(mitsumi_error_status(&dev, 2), STAT_OPEN | STAT_CHANGE | STAT_ERROR | STAT_CMD_CHECK);
    EXPECT_EQ(mitsumi_error_status(&dev, 3), STAT_OPEN | STAT_CHANGE | STAT_ERROR);
}

TEST_F(MitsumiTest, ResetRestoresDocumentedDefaultsAndStopsActivity)
{
    dev.enable_dma = dev.enable_irq = 0xff;
    dev.readcount = 10;
    dev.buf_count = 10;
    dev.locked = 1;
    dev.cdrom_vols = { 1, 2, 3, 4 };
    mitsumi_cdrom_reset(&dev);

    EXPECT_EQ(dev.dmalen, 2055);
    EXPECT_EQ(dev.smode, 1);
    EXPECT_EQ(dev.cur_control, 0x0c);
    EXPECT_EQ(dev.change, 1);
    EXPECT_EQ(dev.enable_dma, 0);
    EXPECT_EQ(dev.enable_irq, 0);
    EXPECT_EQ(dev.readcount, 0u);
    EXPECT_EQ(dev.buf_count, 0);
    EXPECT_EQ(dev.drvmode, DRV_MODE_STOP);
    EXPECT_EQ(dev.cdrom_vols.att0, 255);
    EXPECT_EQ(dev.cdrom_vols.att1, 0);
    EXPECT_EQ(dev.cdrom_vols.att2, 255);
    EXPECT_EQ(dev.cdrom_vols.att3, 0);
    EXPECT_EQ(mock.stop_calls, 1);
}

TEST_F(MitsumiTest, FlagsArbitrateDataAndStatusAndExposeTray)
{
    dev.change = 0;
    dev.cmdbuf_count = 0;
    dev.buf_count = 0;
    EXPECT_EQ(mitsumi_cdrom_get_flags(&dev), FLAG_NODATA | FLAG_NOSTAT | FLAG_UNK | 1);
    dev.cmdbuf_count = 1;
    EXPECT_EQ(mitsumi_cdrom_get_flags(&dev), FLAG_NODATA | FLAG_UNK | 1);
    dev.data = 1;
    dev.buf_count = 1;
    EXPECT_EQ(mitsumi_cdrom_get_flags(&dev), FLAG_NOSTAT | FLAG_UNK | 1);
    dev.early_status = 1;
    EXPECT_EQ(mitsumi_cdrom_get_flags(&dev), FLAG_NODATA | FLAG_UNK | 1);
    dev.tray_open = 1;
    EXPECT_TRUE(mitsumi_cdrom_get_flags(&dev) & FLAG_OPEN);
}

TEST_F(MitsumiTest, VersionUnknownStatusAndSenseCommandsReturnExpectedBytes)
{
    dev.change = 0;
    command(CMD_GET_VER);
    EXPECT_EQ(response(), (std::vector<uint8_t>{ STAT_READY | STAT_SERVO, 'D', 0x10 }));

    command(0x12);
    EXPECT_EQ(response(), (std::vector<uint8_t>{ STAT_READY | STAT_SERVO | STAT_CMD_CHECK }));

    dev.cur_sense = 3;
    command(CMD_REQ_SENSE);
    EXPECT_EQ(response(), (std::vector<uint8_t>{ STAT_READY | STAT_SERVO, 3 }));
    EXPECT_EQ(dev.cur_sense, 0);

    dev.change = 1;
    command(CMD_GET_STAT);
    EXPECT_EQ(response(), (std::vector<uint8_t>{ STAT_READY | STAT_SERVO }));
    EXPECT_EQ(dev.change, 0);
}

TEST_F(MitsumiTest, ModeVolumeLockAndControlRegistersAreProgrammable)
{
    command(CMD_SET_MODE, { 0xa4 });
    EXPECT_EQ(dev.mode, 0xa4);
    EXPECT_EQ(response(), (std::vector<uint8_t>{ STAT_READY | STAT_SERVO | STAT_CHANGE, 0 }));

    command(CMD_SET_VOL, { 10, 20, 30, 40 });
    command(CMD_GET_VOL);
    EXPECT_EQ(response(), (std::vector<uint8_t>{ dev.stat, 10, 20, 30, 40 }));
    EXPECT_EQ(mitsumi_get_volume(&dev, 0), 10u);
    EXPECT_EQ(mitsumi_get_volume(&dev, 1), 30u);
    EXPECT_EQ(mitsumi_get_volume(&dev, 2), 20u);
    EXPECT_EQ(mitsumi_get_volume(&dev, 3), 40u);
    EXPECT_EQ(mitsumi_get_channel(&dev, 0), 3u);
    EXPECT_EQ(mitsumi_get_channel(&dev, 1), 3u);

    command(CMD_LOCK, { 1 });
    EXPECT_EQ(dev.locked, 1);
    mitsumi_cdrom_out(2, 0x5a, &dev);
    EXPECT_EQ(dev.cur_control, 0x5a);
}

TEST_F(MitsumiTest, ConfigurationProgramsDmaLengthEnableAndIrqMask)
{
    command(CMD_CONFIG, { 1, 0x08, 0x07 });
    EXPECT_EQ(dev.dmalen, 0x0807);
    EXPECT_EQ(mitsumi_dma_length(&dev), 2048u);

    command(CMD_CONFIG, { 2, 1 });
    EXPECT_EQ(dev.enable_dma, 1);
    command(CMD_CONFIG, { 0x10, IRQ_DATAREADY | IRQ_ERROR });
    EXPECT_EQ(dev.enable_irq, IRQ_DATAREADY | IRQ_ERROR);
}

TEST_F(MitsumiTest, LockedEjectReportsSenseWhileUnlockedEjectOpensTray)
{
    dev.enable_irq = IRQ_ERROR;
    dev.locked = 1;
    command(CMD_EJECT);
    EXPECT_EQ(dev.cur_sense, 4);
    EXPECT_TRUE(response().front() & STAT_ERROR);
    EXPECT_EQ(mock.eject_calls, 0);
    EXPECT_EQ(mock.irq_asserted, 1u << dev.irq);

    dev.locked = 0;
    command(CMD_EJECT);
    EXPECT_EQ(mock.eject_calls, 1);
    EXPECT_EQ(dev.tray_open, 1);
    EXPECT_EQ(dev.change, 1);
}

TEST_F(MitsumiTest, CookedPioReadFetchesSectorAndAdvancesMsf)
{
    dev.change = 0;
    command(CMD_READ2X, { 0x00, 0x02, 0x00, 0x00, 0x00, 0x01 });
    ASSERT_EQ(dev.buf_count, COOKED_SECTOR_SIZE);
    EXPECT_EQ(dev.readcount, 0u);
    EXPECT_EQ(mock.last_seek, 0u);
    EXPECT_EQ(dev.readmsf, 0x000201u);
    EXPECT_EQ(mitsumi_cdrom_in(0, &dev), 0u);
    EXPECT_EQ(dev.buf_count, COOKED_SECTOR_SIZE - 1);
}

TEST_F(MitsumiTest, InvalidReadAddressReturnsCommandErrorAndSenseTwo)
{
    dev.enable_irq = IRQ_ERROR;
    command(CMD_READ2X, { 0x00, 0x01, 0x99, 0x00, 0x00, 0x01 });
    EXPECT_EQ(dev.cur_sense, 2);
    const auto bytes = response();
    ASSERT_FALSE(bytes.empty());
    EXPECT_TRUE(bytes.front() & STAT_CMD_CHECK);
    EXPECT_EQ(mock.irq_asserted, 1u << dev.irq);
}

TEST_F(MitsumiTest, MediaCommandsFailCleanlyWhenDriveIsEmpty)
{
    cd.cd_status = CD_STATUS_EMPTY;
    dev.tray_open = 1;

    for (uint8_t cmd : { CMD_GET_INFO, CMD_DISC_INFO, CMD_GET_Q, CMD_READ2X }) {
        command(cmd);
        const auto bytes = response();
        ASSERT_EQ(bytes.size(), 1u);
        EXPECT_TRUE(bytes.front() & STAT_CMD_CHECK);
    }
}

TEST_F(MitsumiTest, DmaWithoutDataOrAProgrammedCountWaitsWithoutMutatingBuffer)
{
    dev.buf[0] = 0x5a;
    dev.buf_count = 1;
    dev.buf_idx = 0;
    dma[5].cc = -1;
    EXPECT_EQ(mitsumi_dma_transfer(&dev), 1);
    EXPECT_EQ(dev.buf_count, 1);
    EXPECT_TRUE(mock.dma_values.empty());

    dma[5].cc = 0;
    mock.dma_result = DMA_NODATA;
    EXPECT_EQ(mitsumi_dma_transfer(&dev), 1);
    EXPECT_EQ(dev.buf_count, 1);
    EXPECT_EQ(dev.buf_idx, 0);
}

TEST_F(MitsumiTest, DmaTransferSupportsWordChannelsAndTerminalCount)
{
    dev.enable_irq = IRQ_DATACOMP;
    dev.enable_dma = 1;
    dev.drvmode = DRV_MODE_READ;
    std::memset(dev.buf, 0, sizeof(dev.buf));
    dev.buf[0] = 0x34;
    dev.buf[1] = 0x12;
    dev.buf_count = 2;
    dma[5].cc = 0;
    mock.dma_result = DMA_OVER;

    EXPECT_EQ(mitsumi_dma_transfer(&dev), 0);
    ASSERT_EQ(mock.dma_values.size(), 1u);
    EXPECT_EQ(mock.dma_values[0], 0x1234);
    EXPECT_EQ(dev.buf_count, 0);
    EXPECT_EQ(dev.drvmode, DRV_MODE_STOP);
    EXPECT_EQ(mock.drq[5], 0);
    EXPECT_EQ(mock.irq_asserted, 1u << dev.irq);
}

TEST_F(MitsumiTest, InsertAbortsReadUpdatesTrayAndSignalsChange)
{
    dev.readcount = 4;
    dev.buf_count = 100;
    mitsumi_cdrom_insert(&dev);
    EXPECT_EQ(dev.readcount, 0u);
    EXPECT_EQ(dev.buf_count, 0);
    EXPECT_EQ(dev.change, 1);
    EXPECT_EQ(dev.tray_open, 0);
    EXPECT_EQ(mock.stop_calls, 1);
    EXPECT_EQ(mock.irq_cleared, 1u << dev.irq);
}

} // namespace

extern "C" {

dma_t dma[8];
uint8_t dma_e;
uint8_t dma_m;
uint64_t TIMER_USEC = 1ULL << 32;
uint64_t tsc;
uint64_t timer_target;
volatile int cpu_thread_run = 1;
volatile int is_quit;
int hard_reset_pending;

void picint_common(uint16_t mask, int, int set, uint8_t *)
{
    if (set)
        mock.irq_asserted |= mask;
    else
        mock.irq_cleared |= mask;
}
void dma_set_drq(int channel, int set) { mock.drq.at(channel) = set; }
void dma_set_service_handler(int, void (*)(void *), void *) {}
int dma_channel_write(int, uint16_t value)
{
    mock.dma_values.push_back(value);
    return mock.dma_result;
}
int dma_channel_writable(int) { return 1; }
void timer_enable(pc_timer_t *timer) { timer->flags |= TIMER_ENABLED; }
void timer_disable(pc_timer_t *timer) { timer->flags &= ~TIMER_ENABLED; }
void timer_add(pc_timer_t *timer, void (*callback)(void *), void *priv, int start)
{
    timer->callback = callback;
    timer->priv = priv;
    timer->flags = start ? TIMER_ENABLED : 0;
}
void cdrom_stop(cdrom_t *) { ++mock.stop_calls; }
int cdrom_read_toc(const cdrom_t *, uint8_t *buffer, int, uint8_t, int, int)
{
    buffer[2] = 1;
    buffer[3] = 1;
    return 0;
}
void cdrom_get_track_buffer(cdrom_t *, uint8_t *buffer)
{
    std::memset(buffer, 0, 34);
    buffer[2] = 0;
    buffer[3] = 2;
    buffer[4] = 0;
}
int cdrom_get_q(cdrom_t *, uint8_t *buffer, int current, uint8_t)
{
    std::memset(buffer, 0, 10);
    return current + 1;
}
void cdrom_seek(cdrom_t *dev, uint32_t pos, uint8_t)
{
    mock.last_seek = pos;
    dev->seek_pos = pos;
}
int cdrom_readsector_raw(cdrom_t *, uint8_t *buffer, int, int, int, int,
                         int *len, uint8_t)
{
    for (int i = 0; i < mock.read_length; ++i)
        buffer[i] = static_cast<uint8_t>(i);
    *len = mock.read_length;
    return mock.read_result;
}
uint8_t cdrom_audio_play(cdrom_t *, uint32_t, uint32_t, int) { return 1; }
int cdrom_lba_to_msf_accurate(int lba)
{
    const uint32_t absolute = lba + 150;
    return ((absolute / (60 * 75)) << 16) | (((absolute / 75) % 60) << 8) | (absolute % 75);
}
void cdrom_reload(uint8_t) { ++mock.reload_calls; }
void cdrom_eject(uint8_t) { ++mock.eject_calls; }

}
