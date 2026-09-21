#include <gtest/gtest.h>

#include <algorithm>
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

// Use the production controller and drive; IMG, D86F, FIFO and CRC compile as C.
// Only host services (timers, DMA, UI, audio and file access) are adapted below.
extern "C" {
#include "../../src/floppy/fdc.c"
#define new new_track_size
#include "../../src/floppy/fdd.c"
#undef new
}

namespace {

std::vector<uint8_t>        dma_input;
const std::vector<uint8_t> *dma_output;
size_t                      dma_output_pos;

class ImgTrack : public ::testing::Test {
protected:
    fdc_t       controller {};
    std::filesystem::path directory;
    std::string path;
    bool        engine_created = false;

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
        std::error_code error;
        const auto      root = std::filesystem::temp_directory_path(error);
        ASSERT_FALSE(error) << error.message();
        for (unsigned i = 0; i < 1024; ++i) {
            const auto candidate = root / ("86box-img-track-test-" + std::to_string(i));
            if (std::filesystem::create_directory(candidate, error)) {
                directory = candidate;
                break;
            }
            ASSERT_TRUE(!error || error == std::errc::file_exists) << error.message();
        }
        ASSERT_FALSE(directory.empty()) << "No available IMG track test directory";
        path                       = (directory / "disk.img").string();
        controller.flags           = FDC_FLAG_AT;
        controller.irq             = 6;
        controller.dor             = 0x0c;
        controller.rate            = 2;
        controller.densel_polarity = 1;
        controller.drv2en          = 1;
        controller.max_track       = 85;
        controller.fifo_p          = fifo16_init();
        timer_add(&controller.timer, fdc_callback, &controller, 0);
        timer_add(&controller.watchdog_timer, fdc_watchdog_poll, &controller, 0);
        fdd_set_fdc(&controller);
        img_set_fdc(&controller);
        d86f_set_fdc(&controller);
        fdc_ctrl_reset(&controller);
        command(0x03, { 0xef, 0x02 }); // Standard AT DMA transfers.
    }

    void TearDown() override
    {
        for (int drive = 0; drive < FDD_NUM; ++drive) {
            timer_disable(&fdd_poll_time[drive]);
            timer_disable(&fdd_seek_timer[drive]);
        }
        img_close(0);
        if (engine_created)
            d86f_destroy(0);
        timer_disable(&controller.timer);
        timer_disable(&controller.watchdog_timer);
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

    static uint8_t pattern(int cylinder, int side, int sector)
    {
        return static_cast<uint8_t>(cylinder * 3 + side * 67 + sector);
    }

    void mount(const char *type, bool turbo = true)
    {
        FILE *file = std::fopen(path.c_str(), "wb");
        ASSERT_NE(file, nullptr);
        std::array<uint8_t, 512> data;
        for (int c = 0; c < 40; ++c)
            for (int h = 0; h < 2; ++h)
                for (int r = 1; r <= 9; ++r) {
                    data.fill(pattern(c, h, r));
                    ASSERT_EQ(std::fwrite(data.data(), 1, data.size(), file), data.size());
                }
        ASSERT_EQ(std::fclose(file), 0);
        fdd_set_type(0, fdd_get_from_internal_name(const_cast<char *>(type)));
        fdd_set_check_bpb(0, 0);
        fdd_set_turbo(0, turbo);
        d86f_setup(0);
        engine_created = true;
        img_load(0, const_cast<char *>(path.c_str()));
        ASSERT_NE(drives[0].seek, nullptr);
        fdd_do_seek(0, 0);
        fdc_write(0x3f2, 0x1c, &controller);
        // A single-speed 1.2M drive rotates DD media at 360 RPM.
        fdc_write(0x3f7, fdd_is_525(0) ? 1 : 2, &controller);
    }

    void command(uint8_t opcode, std::initializer_list<uint8_t> params = {})
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

    std::vector<uint8_t> transfer(const std::vector<uint8_t> &output = {})
    {
        dma_input.clear();
        dma_output     = &output;
        dma_output_pos = 0;
        for (unsigned remaining = 1000000; remaining; --remaining) {
            const uint8_t status = fdc_read(0x3f4, &controller) & 0xf0;
            if (status == 0xd0)
                return dma_input;
            tsc += 2;
            if (timer_is_enabled(&controller.timer) && controller.timer.ts_integer <= tsc) {
                timer_disable(&controller.timer);
                controller.timer.callback(controller.timer.priv);
            }
            d86f_poll(0);
        }
        ADD_FAILURE() << "Controller never entered result phase";
        return dma_input;
    }

    std::vector<uint8_t> read_sector(int cylinder, int side = 0, int sector = 1)
    {
        command(0x46, { static_cast<uint8_t>(side << 2), static_cast<uint8_t>(cylinder), static_cast<uint8_t>(side), static_cast<uint8_t>(sector), 2, static_cast<uint8_t>(sector), 0x2a, 0xff });
        return transfer();
    }

    void expect_success()
    {
        const auto status = result();
        ASSERT_EQ(status.size(), 7u);
        EXPECT_EQ(status[0] & 0xc0, 0);
        EXPECT_EQ(status[1], 0);
        EXPECT_EQ(status[2], 0);
    }

    void expect_failure()
    {
        const auto status = result();
        ASSERT_EQ(status.size(), 7u);
        EXPECT_NE(status[0] & 0x40, 0);
        EXPECT_EQ(fdc_read(0x3f4, &controller), 0x80);
    }

    std::vector<uint8_t> file_contents()
    {
        FILE *file = std::fopen(path.c_str(), "rb");
        if (!file) {
            ADD_FAILURE() << "Cannot reopen backing image";
            return {};
        }
        std::vector<uint8_t>      contents;
        std::array<uint8_t, 4096> buffer;
        size_t                    count;
        while ((count = std::fread(buffer.data(), 1, buffer.size(), file)))
            contents.insert(contents.end(), buffer.begin(), buffer.begin() + count);
        EXPECT_EQ(std::ferror(file), 0);
        EXPECT_EQ(std::fclose(file), 0);
        return contents;
    }
};

TEST_F(ImgTrack, OrdinaryThreeInchDrivesDoNotDoubleStepFortyCylinderImages)
{
    mount("35_2dd");
    for (const char *type : { "35_2dd", "35_2hd" }) {
        SCOPED_TRACE(type);
        fdd_set_type(0, fdd_get_from_internal_name(const_cast<char *>(type)));
        for (bool turbo : { false, true }) {
            SCOPED_TRACE(turbo);
            fdd_set_turbo(0, turbo);
            fdd_do_seek(0, 1);
            EXPECT_EQ(read_sector(1), std::vector<uint8_t>(512, pattern(1, 0, 1)));
            expect_success();
        }
    }
}

TEST_F(ImgTrack, FiveInchHighDensityDriveRetainsDoubleStepping)
{
    mount("525_2hd");
    for (bool turbo : { false, true }) {
        SCOPED_TRACE(turbo);
        fdd_set_turbo(0, turbo);
        fdd_do_seek(0, 2);
        EXPECT_EQ(read_sector(1), std::vector<uint8_t>(512, pattern(1, 0, 1)));
        expect_success();
    }
}

TEST_F(ImgTrack, DoubleStepFormatReseeksPhysicalCylinderAndPreservesNeighbors)
{
    mount("525_2hd");
    auto expected = file_contents();
    ASSERT_EQ(expected.size(), 40u * 2 * 9 * 512);
    for (bool turbo : { false, true }) {
        SCOPED_TRACE(turbo);
        fdd_set_turbo(0, turbo);
        fdd_do_seek(0, 4);
        const uint8_t        fill = turbo ? 0xe5 : 0xa5;
        std::vector<uint8_t> ids;
        for (uint8_t sector = 1; sector <= 9; ++sector)
            ids.insert(ids.end(), { 2, 0, sector, 2 });
        command(0x4d, { 0, 2, 9, 0x50, fill });
        transfer(ids);
        expect_success();
        // No explicit seek: FORMAT must leave this physical cylinder selected.
        EXPECT_EQ(read_sector(2), std::vector<uint8_t>(512, fill));
        expect_success();
        std::fill_n(expected.begin() + 2 * 2 * 9 * 512, 9 * 512, fill);
        EXPECT_EQ(file_contents(), expected);
        fdd_do_seek(0, 2);
        EXPECT_EQ(read_sector(1), std::vector<uint8_t>(512, pattern(1, 0, 1)));
        expect_success();
    }
}

TEST_F(ImgTrack, InvalidSeekRetiresFluxTurboAndSelectedSectorViews)
{
    mount("525_2hd");
    const auto original = file_contents();
    for (bool turbo : { false, true }) {
        SCOPED_TRACE(turbo);
        fdd_set_turbo(0, turbo);
        for (int physical : { 80, 82, -1 }) {
            SCOPED_TRACE(physical);
            fdd_do_seek(0, 2);
            EXPECT_EQ(read_sector(1), std::vector<uint8_t>(512, pattern(1, 0, 1)));
            expect_success();
            auto &backend = d86f_handler[0];
            backend.set_sector(0, 0, 1, 0, 1, 2);
            fdd_do_seek(0, physical);
            EXPECT_EQ(backend.read_data(0, 0, 0), 0xff);
            backend.write_data(0, 0, 0, 0x5a);
            backend.writeback(0);
            EXPECT_TRUE(read_sector(physical >= 0 ? physical / 2 : 0).empty());
            expect_failure();
            EXPECT_TRUE(read_sector(1).empty());
            expect_failure();
            command(0x4d, { 0, 2, 9, 0x50, 0x11 });
            transfer(std::vector<uint8_t>(9 * 4, 0));
            expect_failure();
            // HD-COPY's three-sector probe must not bypass track validity.
            d86f_format_id_t ids[3] = {};
            EXPECT_EQ(backend.format_track(0, 0, ids, 3, 0x11), 0);
            EXPECT_EQ(file_contents(), original);
        }
    }
}

TEST_F(ImgTrack, MissingSectorCannotAliasFirstSectorOrModifyBackingImage)
{
    mount("35_2dd");
    fdd_do_seek(0, 1);
    const auto original = file_contents();
    auto      &backend  = d86f_handler[0];
    backend.set_sector(0, 0, 1, 0, 1, 2);
    EXPECT_EQ(backend.read_data(0, 0, 0), pattern(1, 0, 1));
    // Exercise absent R and absent H through the public backend callbacks.
    for (const auto id : {
             std::array<uint8_t, 2> { 0, 0 },
               { 2, 1 }
    }) {
        backend.set_sector(0, 0, 1, id[0], id[1], 2);
        EXPECT_EQ(backend.read_data(0, 0, 0), 0xff);
        backend.write_data(0, 0, 0, 0x5a);
        backend.writeback(0);
    }
    EXPECT_EQ(file_contents(), original);
    EXPECT_EQ(read_sector(1), std::vector<uint8_t>(512, pattern(1, 0, 1)));
    expect_success();
}

} // namespace

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
    if (dma_output_pos >= dma_output->size())
        return DMA_NODATA;
    const int value = (*dma_output)[dma_output_pos++];
    return value | (dma_output_pos == dma_output->size() ? DMA_OVER : 0);
}
int
dma_channel_write(int, uint16_t value)
{
    dma_input.push_back(static_cast<uint8_t>(value));
    return dma_input.size() == 512 ? DMA_OVER : 0;
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
}
