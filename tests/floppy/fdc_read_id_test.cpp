#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

extern "C" {
#include "../../src/floppy/fdc.c"
#define new new_track_size
#include "../../src/floppy/fdd.c"
#undef new
}

namespace {

// Exercise the real FDC -> FDD -> IMG/D86F path. Only host services are adapted;
// in particular, no adapter synthesizes a controller completion or media error.
class FdcReadId : public ::testing::Test {
protected:
    fdc_t       controller {};
    std::filesystem::path directory;
    std::string path;

    void SetUp() override
    {
        tsc = 0;
        std::memset(fdd, 0, sizeof(fdd));
        std::memset(drives, 0, sizeof(drives));
        std::memset(motoron, 0, sizeof(motoron));
        std::memset(fdd_seek_in_progress, 0, sizeof(fdd_seek_in_progress));
        std::memset(fdd_seek_timer, 0, sizeof(fdd_seek_timer));
        std::memset(fdd_pending, 0, sizeof(fdd_pending));
        std::memset(writeprot, 0, sizeof(writeprot));
        std::memset(ui_writeprot, 0, sizeof(ui_writeprot));
        fdd_notfound = 0;
        for (int drive = 0; drive < FDD_NUM; ++drive) {
            drives[drive].id = drive;
            timer_add(&fdd_poll_time[drive], fdd_poll, &drives[drive], 0);
        }

        controller.irq       = 6;
        controller.dma_ch    = 2;
        controller.rate      = 2;
        controller.drv2en    = 1;
        controller.max_track = 79;
        controller.fifo_p    = fifo16_init();
        timer_add(&controller.timer, fdc_callback, &controller, 0);
        timer_add(&controller.watchdog_timer, fdc_watchdog_poll, &controller, 0);
        fdd_set_fdc(&controller);
        img_set_fdc(&controller);
        d86f_set_fdc(&controller);

        std::error_code error;
        const auto      root = std::filesystem::temp_directory_path(error);
        ASSERT_FALSE(error) << error.message();
        for (unsigned i = 0; i < 1024; ++i) {
            const auto candidate = root / ("86box-fdc-read-id-test-" + std::to_string(i));
            if (std::filesystem::create_directory(candidate, error)) {
                directory = candidate;
                break;
            }
            ASSERT_TRUE(!error || error == std::errc::file_exists) << error.message();
        }
        ASSERT_FALSE(directory.empty()) << "No available FDC Read ID test directory";
        path       = (directory / "single-sided.img").string();
        FILE *file = std::fopen(path.c_str(), "wb");
        ASSERT_NE(file, nullptr);
        const std::array<uint8_t, 512> sector {};
        bool                           written = true;
        for (unsigned i = 0; i < 40 * 9; ++i)
            written &= std::fwrite(sector.data(), 1, sector.size(), file) == sector.size();
        const int close_result = std::fclose(file);
        ASSERT_TRUE(written);
        ASSERT_EQ(close_result, 0);

        // A two-headed physical drive containing a 180 KiB, one-headed image.
        fdd_set_type(0, fdd_get_from_internal_name(const_cast<char *>("35_2dd")));
        fdd_set_check_bpb(0, 0);
        fdd_set_turbo(0, 0);
        d86f_setup(0);
        img_load(0, const_cast<char *>(path.c_str()));
        ASSERT_NE(drives[0].seek, nullptr);
        fdd_do_seek(0, 0);
        fdc_write(0x3f2, 0x1c, &controller);
        for (unsigned remaining = 16; timer_is_enabled(&controller.timer); --remaining) {
            ASSERT_GT(remaining, 0u) << "Controller reset did not quiesce";
            tsc = controller.timer.ts_integer;
            timer_disable(&controller.timer);
            controller.timer.callback(controller.timer.priv);
        }
    }

    void TearDown() override
    {
        for (int drive = 0; drive < FDD_NUM; ++drive) {
            timer_disable(&fdd_poll_time[drive]);
            timer_disable(&fdd_seek_timer[drive]);
        }
        timer_disable(&controller.timer);
        timer_disable(&controller.watchdog_timer);
        img_close(0);
        d86f_destroy(0);
        fifo_close(controller.fifo_p);
        fdd_set_fdc(nullptr);
        img_set_fdc(nullptr);
        d86f_set_fdc(nullptr);
        if (!directory.empty()) {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
            EXPECT_FALSE(error) << error.message();
        }
    }

    void command(uint8_t opcode, std::initializer_list<uint8_t> params)
    {
        fdc_write(0x3f5, opcode, &controller);
        for (uint8_t value : params)
            fdc_write(0x3f5, value, &controller);
    }

    std::vector<uint8_t> result()
    {
        std::vector<uint8_t> bytes;
        for (unsigned i = 0; i < 7 && (fdc_read(0x3f4, &controller) & 0xf0) == 0xd0; ++i)
            bytes.push_back(fdc_read(0x3f5, &controller));
        return bytes;
    }
};

TEST_F(FdcReadId, MissingHeadPreservesImmediateErrorResult)
{
    // These are distinct branches: DMA selects MSR 0x50, non-DMA selects 0x70.
    // Neither may overwrite the synchronous D86F error's result-ready status.
    for (bool non_dma : { false, true }) {
        SCOPED_TRACE(non_dma ? "non-DMA" : "DMA");
        command(0x03, { 0xef, static_cast<uint8_t>(0x02 | non_dma) });
        command(0x4a, { 0x04 });

        // No polling: d86f_readaddress must reject the absent side immediately.
        ASSERT_EQ(fdc_read(0x3f4, &controller), 0xd0);
        const auto status = result();
        ASSERT_EQ(status.size(), 7u);
        EXPECT_EQ(status[0], 0x44); // Abnormal termination, head 1, drive 0.
        EXPECT_EQ(status[1], 0x01); // Missing ID address mark.
        EXPECT_EQ(status[2], 0x00);
        EXPECT_EQ(fdc_read(0x3f4, &controller), 0x80);
    }
}

TEST_F(FdcReadId, PresentHeadCompletesThroughRealBitstream)
{
    for (bool non_dma : { false, true }) {
        SCOPED_TRACE(non_dma ? "non-DMA" : "DMA");
        command(0x03, { 0xef, static_cast<uint8_t>(0x02 | non_dma) });
        command(0x4a, { 0x00 });
        EXPECT_EQ(fdc_read(0x3f4, &controller), non_dma ? 0x70 : 0x50);

        // Advance the real non-turbo engine until it decodes an ID field.
        for (unsigned remaining = 200000; remaining && fdc_read(0x3f4, &controller) != 0xd0; --remaining) {
            tsc += 2;
            d86f_poll(0);
        }
        ASSERT_EQ(fdc_read(0x3f4, &controller), 0xd0);
        const auto status = result();
        ASSERT_EQ(status.size(), 7u);
        EXPECT_EQ(status[0], 0x00);
        EXPECT_EQ(status[1], 0x00);
        EXPECT_EQ(status[2], 0x00);
        EXPECT_EQ(status[3], 0x00); // Cylinder 0.
        EXPECT_EQ(status[4], 0x00); // Head 0.
        EXPECT_GE(status[5], 1);    // Any of the nine physical IDs is valid.
        EXPECT_LE(status[5], 9);
        EXPECT_EQ(status[6], 0x02); // 512-byte sectors.
        EXPECT_EQ(fdc_read(0x3f4, &controller), 0x80);
    }
}

} // namespace

// Host adapters: deterministic clock, no PIC/DMA bus or UI/audio, ordinary files.
// Read ID transfers only result bytes, so no emulated DMA data transport is needed.
extern "C" {
uint64_t    TIMER_USEC = 1ULL << 32;
uint64_t    tsc;
uint64_t    timer_target;
cpu_state_t cpu_state;
int         isa_cycles = 1;
int         is486;
int         machine;

const char *
machine_getname(int)
{
    return "IBM PC";
}
int
machine_is_pcjx(int)
{
    return 0;
}
void
timer_enable(pc_timer_t *timer)
{
    timer->flags |= TIMER_ENABLED;
}
void
timer_disable(pc_timer_t *timer)
{
    timer->flags &= ~TIMER_ENABLED;
}
void
timer_add(pc_timer_t *timer, void (*callback)(void *), void *priv, int start)
{
    timer->callback = callback;
    timer->priv     = priv;
    timer->flags    = start ? TIMER_ENABLED : 0;
}
void
picint_common(uint16_t, int, int, uint8_t *)
{
}
void
dma_set_drq(int, int)
{
}
int
dma_get_drq(int)
{
    return 0;
}
int
dma_channel_read(int)
{
    return DMA_NODATA;
}
int
dma_channel_write(int, uint16_t)
{
    return DMA_NODATA;
}
int
dma_mode(int)
{
    return 0;
}
void
ui_sb_update_icon(int, int)
{
}
void
ui_sb_update_icon_write(int, int)
{
}
int
fdd_tape_present(int)
{
    return 0;
}
int
fdd_tape_track0(int)
{
    return 0;
}
int
fdd_tape_get_flags(int)
{
    return 0;
}
int
fdd_tape_step(int, int)
{
    return 0;
}
double
fdd_audio_get_seek_time(int, int, int)
{
    return 10000.0;
}
void
fdd_audio_set_motor_enable(int, int)
{
}
void
fdd_audio_play_multi_track_seek(int, int, int)
{
}
int
floppy_ioctl_read_sector(int, int, int, int, uint8_t *)
{
    return 0;
}
int
floppy_ioctl_write_sector(int, int, int, int, const uint8_t *)
{
    return 0;
}
void
floppy_ioctl_close(int)
{
}
FILE *
plat_fopen(const char *path, const char *mode)
{
    return std::fopen(path, mode);
}
char *
path_get_extension(char *path)
{
    char *dot = std::strrchr(path, '.');
    return dot ? dot + 1 : path + std::strlen(path);
}
void
pclog(const char *, ...)
{
}
void
pclog_ex(const char *, va_list)
{
}
void
fatal(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::abort();
}
uint8_t
random_generate(void)
{
    return 0;
}
void
io_sethandler(uint16_t, uint16_t, uint8_t (*)(uint16_t, void *),
              uint16_t (*)(uint16_t, void *), uint32_t (*)(uint16_t, void *),
              void (*)(uint16_t, uint8_t, void *), void (*)(uint16_t, uint16_t, void *),
              void (*)(uint16_t, uint32_t, void *), void *)
{
}
void
io_removehandler(uint16_t, uint16_t, uint8_t (*)(uint16_t, void *),
                 uint16_t (*)(uint16_t, void *), uint32_t (*)(uint16_t, void *),
                 void (*)(uint16_t, uint8_t, void *), void (*)(uint16_t, uint16_t, void *),
                 void (*)(uint16_t, uint32_t, void *), void *)
{
}
}
