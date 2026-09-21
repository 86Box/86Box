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

// Keep infrastructure isolated from the board tests while using the real floppy engines.
#define TIMER_USEC pcjx_floppy_TIMER_USEC
#define tsc pcjx_floppy_tsc
#define timer_target pcjx_floppy_timer_target
#define timer_enable pcjx_floppy_timer_enable
#define timer_disable pcjx_floppy_timer_disable
#define timer_add pcjx_floppy_timer_add
#define cpu_state pcjx_floppy_cpu_state
#define isa_cycles pcjx_floppy_isa_cycles
#define is486 pcjx_floppy_is486
#define machine pcjx_floppy_machine
#define machine_is_pcjx pcjx_floppy_machine_is_pcjx
#define machine_getname pcjx_floppy_machine_getname
#define picint_common pcjx_floppy_picint_common
#define dma_set_drq pcjx_floppy_dma_set_drq
#define dma_get_drq pcjx_floppy_dma_get_drq
#define dma_channel_read pcjx_floppy_dma_channel_read
#define dma_channel_write pcjx_floppy_dma_channel_write
#define dma_mode pcjx_floppy_dma_mode
#define ui_sb_update_icon pcjx_floppy_ui_sb_update_icon
#define ui_sb_update_icon_write pcjx_floppy_ui_sb_update_icon_write
#define io_sethandler pcjx_floppy_io_sethandler
#define io_removehandler pcjx_floppy_io_removehandler
#define fdd_tape_present pcjx_floppy_fdd_tape_present
#define fdd_tape_track0 pcjx_floppy_fdd_tape_track0
#define fdd_tape_get_flags pcjx_floppy_fdd_tape_get_flags
#define fdd_tape_step pcjx_floppy_fdd_tape_step
#define fdd_audio_get_seek_time pcjx_floppy_fdd_audio_get_seek_time
#define fdd_audio_set_motor_enable pcjx_floppy_fdd_audio_set_motor_enable
#define fdd_audio_play_multi_track_seek pcjx_floppy_fdd_audio_play_multi_track_seek
#define floppy_ioctl_read_sector pcjx_floppy_floppy_ioctl_read_sector
#define floppy_ioctl_write_sector pcjx_floppy_floppy_ioctl_write_sector
#define floppy_ioctl_close pcjx_floppy_floppy_ioctl_close
#define plat_fopen pcjx_floppy_plat_fopen
#define path_get_extension pcjx_floppy_path_get_extension
#define pclog pcjx_floppy_pclog
#define pclog_ex pcjx_floppy_pclog_ex
#define fatal pcjx_floppy_fatal
#define random_generate pcjx_floppy_random_generate

extern "C" {
#include "../../src/floppy/fdc.c"
#define new new_track_size
#include "../../src/floppy/fdd.c"
#undef new
}

namespace {

bool jx_machine = true;
uint16_t irq_lines;
unsigned dma_accesses;

struct IoHandler {
    uint16_t base;
    uint16_t size;
    uint8_t (*read)(uint16_t, void *);
    void (*write)(uint16_t, uint8_t, void *);
    void *priv;
};
std::vector<IoHandler> io_handlers;

uint8_t read_port(uint16_t port)
{
    uint8_t value = 0xff;
    for (const auto &handler : io_handlers)
        if ((port >= handler.base) && (port - handler.base < handler.size) && handler.read)
            value &= handler.read(port, handler.priv);
    return value;
}

class PcjxFloppy : public ::testing::Test {
protected:
    fdc_t controller{};
    std::filesystem::path directory;
    std::vector<std::string> paths;
    std::array<bool, FDD_NUM> engine_created{};

    void SetUp() override
    {
        jx_machine = true;
        irq_lines = 0;
        dma_accesses = 0;
        tsc = 0;
        io_handlers.clear();
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
            const auto candidate = root / ("86box-pcjx-floppy-test-" + std::to_string(i));
            if (std::filesystem::create_directory(candidate, error)) {
                directory = candidate;
                break;
            }
            ASSERT_TRUE(!error || error == std::errc::file_exists) << error.message();
        }
        ASSERT_FALSE(directory.empty()) << "No available PC JX floppy test directory";
        controller.flags = FDC_FLAG_PCJX;
        controller.irq = 6;
        controller.rate = 2;
        controller.densel_polarity = 1;
        controller.drv2en = 1;
        controller.max_track = 79;
        controller.fifo_p = fifo16_init();
        timer_add(&controller.timer, fdc_callback, &controller, 0);
        timer_add(&controller.watchdog_timer, fdc_watchdog_poll, &controller, 0);
        fdd_set_fdc(&controller);
        img_set_fdc(&controller);
        d86f_set_fdc(&controller);
        fdc_pcjx_dor(&controller, 0x80);
        run_motion_timers();
        command(0x03, { 0xef, 0x03 });
    }

    void TearDown() override
    {
        for (int drive = 0; drive < FDD_NUM; ++drive) {
            timer_disable(&fdd_poll_time[drive]);
            timer_disable(&fdd_seek_timer[drive]);
            img_close(drive);
            if (engine_created[drive])
                d86f_destroy(drive);
        }
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
        io_handlers.clear();
    }

    static uint8_t pattern(int cylinder, int side, int sector, int seed)
    {
        return static_cast<uint8_t>(cylinder * 3 + side * 67 + sector + seed);
    }

    void mount(int drive, int cylinders = 40, int sectors = 9, int seed = 0, bool turbo = true)
    {
        const std::string path = (directory / ("disk" + std::to_string(paths.size()) + ".img")).string();
        paths.push_back(path);
        FILE *file = std::fopen(path.c_str(), "wb");
        ASSERT_NE(file, nullptr);
        std::array<uint8_t, 512> data;
        for (int c = 0; c < cylinders; ++c)
            for (int h = 0; h < 2; ++h)
                for (int r = 1; r <= sectors; ++r) {
                    data.fill(pattern(c, h, r, seed));
                    ASSERT_EQ(std::fwrite(data.data(), 1, data.size(), file), data.size());
                }
        ASSERT_EQ(std::fclose(file), 0);
        fdd_set_type(drive, fdd_get_from_internal_name(const_cast<char *>("35_2dd")));
        fdd_set_check_bpb(drive, 0);
        fdd_set_turbo(drive, turbo);
        if (!engine_created[drive]) {
            d86f_setup(drive);
            engine_created[drive] = true;
        }
        img_load(drive, const_cast<char *>(path.c_str()));
        ASSERT_NE(drives[drive].seek, nullptr);
        fdd_do_seek(drive, 0);
        fdc_write(0xf2, controller.dor | (1 << drive), &controller);
    }

    void command(uint8_t opcode, std::initializer_list<uint8_t> params = {})
    {
        fdc_write(0xf5, opcode, &controller);
        for (uint8_t value : params)
            fdc_write(0xf5, value, &controller);
    }

    void run_motion_timers()
    {
        for (unsigned remaining = 32; remaining; --remaining) {
            pc_timer_t *next = timer_is_enabled(&controller.timer) ? &controller.timer : nullptr;
            for (auto &timer : fdd_seek_timer)
                if (timer_is_enabled(&timer) && (!next || timer.ts_integer < next->ts_integer))
                    next = &timer;
            if (!next)
                return;
            tsc = next->ts_integer;
            timer_disable(next);
            next->callback(next->priv);
        }
        FAIL() << "Seek/controller timers failed to quiesce";
    }

    std::vector<uint8_t> result()
    {
        std::vector<uint8_t> bytes;
        for (unsigned i = 0; i < 7 && (fdc_read(0xf4, &controller) & 0xf0) == 0xd0; ++i)
            bytes.push_back(fdc_read(0xf5, &controller));
        return bytes;
    }

    void seek(int drive, int physical)
    {
        command(0x0f, { static_cast<uint8_t>(drive), static_cast<uint8_t>(physical) });
        run_motion_timers();
        command(0x08);
        const auto status = result();
        ASSERT_EQ(status.size(), 2u);
        ASSERT_EQ(status[0] & 0x73, 0x20 | drive);
        ASSERT_EQ(status[1], physical);
    }

    std::vector<uint8_t> transfer(int drive, const std::vector<uint8_t> &output = {})
    {
        std::vector<uint8_t> input;
        size_t output_pos = 0;
        for (unsigned remaining = 1000000; remaining; --remaining) {
            const uint8_t status = fdc_read(0xf4, &controller) & 0xf0;
            if (status == 0xd0)
                return input;
            if (status == 0xf0)
                input.push_back(fdc_read(0xf5, &controller));
            if (status == 0xb0) {
                if (output_pos >= output.size()) {
                    ADD_FAILURE() << "Controller requested excess write/format bytes";
                    return input;
                }
                fdc_write(0xf5, output[output_pos++], &controller);
            }
            tsc += 2;
            if (timer_is_enabled(&controller.timer) && controller.timer.ts_integer <= tsc) {
                timer_disable(&controller.timer);
                controller.timer.callback(controller.timer.priv);
            }
            d86f_poll(drive);
        }
        ADD_FAILURE() << "Controller never entered result phase";
        return input;
    }

    std::vector<uint8_t> read_id(int drive, int side)
    {
        command(0x4a, { static_cast<uint8_t>(drive | (side << 2)) });
        EXPECT_TRUE(transfer(drive).empty());
        return result();
    }

    std::vector<uint8_t> read_sector(int drive, int c, int h = 0, int r = 1)
    {
        command(0x46, { static_cast<uint8_t>(drive | (h << 2)), static_cast<uint8_t>(c),
                        static_cast<uint8_t>(h), static_cast<uint8_t>(r), 2,
                        static_cast<uint8_t>(r), 0x2a, 0xff });
        return transfer(drive);
    }

    void expect_success()
    {
        const auto status = result();
        ASSERT_EQ(status.size(), 7u);
        EXPECT_EQ(status[0] & 0xc0, 0);
        EXPECT_EQ(status[1], 0);
        EXPECT_EQ(status[2], 0);
        EXPECT_EQ(fdc_read(0xf4, &controller), 0x80);
    }
};

TEST_F(PcjxFloppy, SkippedTracksHaveLogicalIdsWithoutOddTrackAliases)
{
    mount(0);
    for (bool turbo : { false, true }) {
        fdd_set_turbo(0, turbo);
        seek(0, 2);
        auto id = read_id(0, 0);
        ASSERT_EQ(id.size(), 7u);
        EXPECT_EQ(id[1], 0);
        EXPECT_EQ(id[3], 1);
        EXPECT_EQ(id[4], 0);
        EXPECT_EQ(read_sector(0, 1), std::vector<uint8_t>(512, pattern(1, 0, 1, 0)));
        expect_success();
        EXPECT_TRUE(read_sector(0, 2).empty());
        auto wrong = result();
        ASSERT_EQ(wrong.size(), 7u);
        EXPECT_NE(wrong[0] & 0x40, 0);
        EXPECT_TRUE((wrong[1] & 0x05) || (wrong[2] & 0x10));
        seek(0, 1);
        id = read_id(0, 0);
        ASSERT_EQ(id.size(), 7u);
        EXPECT_NE(id[1] & 1, 0);
        EXPECT_TRUE(read_sector(0, 0).empty());
        auto odd = result();
        ASSERT_EQ(odd.size(), 7u);
        EXPECT_NE(odd[0] & 0x40, 0);
        seek(0, 78);
        EXPECT_EQ(read_sector(0, 39, 1, 9), std::vector<uint8_t>(512, pattern(39, 1, 9, 0)));
        expect_success();
        seek(0, 80);
        id = read_id(0, 1);
        ASSERT_EQ(id.size(), 7u);
        EXPECT_NE(id[1] & 1, 0);
    }
    EXPECT_EQ(dma_accesses, 0u);
    EXPECT_EQ(irq_lines, 0);
}

TEST_F(PcjxFloppy, FullSizeMediaKeepsOddAndLastCylinders)
{
    mount(0, 80);
    seek(0, 1);
    auto id = read_id(0, 1);
    ASSERT_EQ(id.size(), 7u);
    EXPECT_EQ(id[3], 1);
    EXPECT_EQ(id[4], 1);
    EXPECT_EQ(read_sector(0, 1), std::vector<uint8_t>(512, pattern(1, 0, 1, 0)));
    expect_success();
    seek(0, 79);
    EXPECT_EQ(read_sector(0, 79, 1, 9), std::vector<uint8_t>(512, pattern(79, 1, 9, 0)));
    expect_success();
}

TEST_F(PcjxFloppy, PlacementIsScopedToMachineAndEligiblePhysicalDrive)
{
    mount(0);
    for (const char *type : { "35_2dd", "35_2hd" }) {
        fdd_set_type(0, fdd_get_from_internal_name(const_cast<char *>(type)));
        for (bool turbo : { false, true }) {
            SCOPED_TRACE(::testing::Message() << type << ", turbo=" << turbo);
            fdd_set_turbo(0, turbo);
            seek(0, 0);
            jx_machine = false;
            EXPECT_FALSE(fdd_is_pcjx_360(0));
            seek(0, 1);
            EXPECT_EQ(read_sector(0, 1), std::vector<uint8_t>(512, pattern(1, 0, 1, 0)));
            expect_success();
            seek(0, 2);
            EXPECT_EQ(read_sector(0, 2), std::vector<uint8_t>(512, pattern(2, 0, 1, 0)));
            expect_success();

            seek(0, 0);
            jx_machine = true;
            EXPECT_TRUE(fdd_is_pcjx_360(0));
            seek(0, 2);
            EXPECT_EQ(read_sector(0, 1), std::vector<uint8_t>(512, pattern(1, 0, 1, 0)));
            expect_success();
            seek(0, 1);
            EXPECT_TRUE(read_sector(0, 0).empty());
            const auto status = result();
            ASSERT_EQ(status.size(), 7u);
            EXPECT_NE(status[0] & 0x40, 0);
        }
    }
    fdd_set_type(0, fdd_get_from_internal_name(const_cast<char *>("525_2hd")));
    EXPECT_FALSE(fdd_is_pcjx_360(0));
    fdd_set_type(0, fdd_get_from_internal_name(const_cast<char *>("35_1dd")));
    EXPECT_FALSE(fdd_is_pcjx_360(0));
    fdd_set_type(0, fdd_get_from_internal_name(const_cast<char *>("35_2hd")));
    EXPECT_TRUE(fdd_is_pcjx_360(0));
}

TEST_F(PcjxFloppy, F2MotorsAndBinaryUnitsAreIndependent)
{
    mount(0, 40, 9, 0);
    mount(1, 40, 9, 31);
    mount(2, 40, 9, 59);
    fdc_write(0xf2, 0x87, &controller);
    for (int unit = 0; unit < 3; ++unit) {
        seek(unit, 2);
        EXPECT_EQ(read_sector(unit, 1), std::vector<uint8_t>(512, pattern(1, 0, 1, unit == 0 ? 0 : unit == 1 ? 31 : 59)));
        expect_success();
    }
    fdc_write(0xf2, 0x85, &controller);
    EXPECT_TRUE(read_sector(1, 1).empty());
    auto disabled = result();
    ASSERT_EQ(disabled.size(), 7u);
    EXPECT_NE(disabled[0] & 0x40, 0);
    fdd_set_type(3, fdd_get_from_internal_name(const_cast<char *>("35_2dd")));
    fdc_write(0xf2, 0x9f, &controller);
    command(0x0f, { 3, 2 });
    command(0x08);
    auto absent = result();
    ASSERT_EQ(absent.size(), 2u);
    EXPECT_EQ(absent[0] & 0x73, 0x73);
    EXPECT_EQ(fdd_current_track(3), 0);
}

TEST_F(PcjxFloppy, WatchdogIsTheOnlySystemInterruptAndResetAbortsSeek)
{
    mount(0, 40, 9, 0, false);
    command(0x0f, { 0, 20 });
    fdc_write(0xf2, 0x01, &controller);
    EXPECT_EQ(fdc_read(0xf4, &controller), 0);
    fdc_write(0xf2, 0x81, &controller);
    run_motion_timers();
    EXPECT_EQ(fdd_current_track(0), 20);
    EXPECT_FALSE(timer_is_enabled(&fdd_seek_timer[0]));
    fdc_write(0xf2, 0xe1, &controller);
    fdc_write(0xf2, 0xa1, &controller);
    for (int ms = 0; ms < 999; ++ms) {
        tsc = controller.watchdog_timer.ts_integer;
        timer_disable(&controller.watchdog_timer);
        controller.watchdog_timer.callback(controller.watchdog_timer.priv);
    }
    EXPECT_EQ(irq_lines, 0);
    tsc = controller.watchdog_timer.ts_integer;
    timer_disable(&controller.watchdog_timer);
    controller.watchdog_timer.callback(controller.watchdog_timer.priv);
    EXPECT_EQ(irq_lines, 1 << 6);
    fdc_write(0xf2, 0xe1, &controller);
    fdc_write(0xf2, 0xa1, &controller);
    EXPECT_EQ(irq_lines, 0);
    fdc_write(0xf2, 0x81, &controller);
    EXPECT_FALSE(timer_is_enabled(&controller.watchdog_timer));
}

TEST_F(PcjxFloppy, EightByteDecodeCanMoveAndDoesNotExposeAtRegisters)
{
    fdc_set_base(&controller, 0xf0);
    EXPECT_EQ(read_port(0xf4), 0x80);
    EXPECT_EQ(read_port(0xfc), 0xff);
    EXPECT_EQ(read_port(0xf7), 0xff);
    EXPECT_EQ(read_port(0xf3), 0xff);
    fdc_remove(&controller);
    EXPECT_EQ(read_port(0xf4), 0xff);
    fdc_set_base(&controller, 0);
    EXPECT_EQ(read_port(4), 0x80);
    fdc_remove(&controller);
    EXPECT_EQ(read_port(4), 0xff);
}

TEST_F(PcjxFloppy, EvenTrackWritesPersistAndOddWritesCannotCorruptPreviousTrack)
{
    mount(0);
    seek(0, 2);
    command(0x45, { 0, 1, 0, 1, 2, 1, 0x2a, 0xff });
    EXPECT_TRUE(transfer(0, std::vector<uint8_t>(512, 0xa5)).empty());
    expect_success();
    seek(0, 1);
    command(0x45, { 0, 0, 0, 1, 2, 1, 0x2a, 0xff });
    transfer(0, std::vector<uint8_t>(512, 0x5a));
    auto failed = result();
    ASSERT_EQ(failed.size(), 7u);
    EXPECT_NE(failed[0] & 0x40, 0);
    img_close(0);
    img_load(0, const_cast<char *>(paths[0].c_str()));
    seek(0, 0);
    EXPECT_EQ(read_sector(0, 0), std::vector<uint8_t>(512, pattern(0, 0, 1, 0)));
    expect_success();
    seek(0, 2);
    EXPECT_EQ(read_sector(0, 1), std::vector<uint8_t>(512, 0xa5));
    expect_success();
    writeprot[0] = 1;
    command(0x45, { 0, 1, 0, 1, 2, 1, 0x2a, 0xff });
    transfer(0, std::vector<uint8_t>(512, 0x11));
    auto protected_result = result();
    ASSERT_EQ(protected_result.size(), 7u);
    EXPECT_NE(protected_result[1] & 2, 0);
}

TEST_F(PcjxFloppy, FormatUsesPhysicalTrackOnceAndRejectsBlankOddTrack)
{
    mount(0, 40, 8);
    seek(0, 4);
    std::vector<uint8_t> ids;
    for (uint8_t sector = 1; sector <= 8; ++sector)
        ids.insert(ids.end(), { 2, 0, sector, 2 });
    command(0x4d, { 0, 2, 8, 0x50, 0xe5 });
    transfer(0, ids);
    expect_success();
    EXPECT_EQ(read_sector(0, 2), std::vector<uint8_t>(512, 0xe5));
    expect_success();
    seek(0, 3);
    command(0x4d, { 0, 2, 8, 0x50, 0x11 });
    transfer(0, ids);
    auto failed = result();
    ASSERT_EQ(failed.size(), 7u);
    EXPECT_NE(failed[0] & 0x40, 0);
    seek(0, 2);
    EXPECT_EQ(read_sector(0, 1), std::vector<uint8_t>(512, pattern(1, 0, 1, 0)));
    expect_success();
}

} // namespace

extern "C" {
uint64_t TIMER_USEC = 1ULL << 32;
uint64_t tsc;
uint64_t timer_target;
cpu_state_t cpu_state;
int isa_cycles = 1;
int is486;
int machine;

int machine_is_pcjx(int) { return jx_machine; }
const char *machine_getname(int) { return jx_machine ? "IBM PC JX" : "IBM PC"; }
void timer_enable(pc_timer_t *timer) { timer->flags |= TIMER_ENABLED; }
void timer_disable(pc_timer_t *timer) { timer->flags &= ~TIMER_ENABLED; }
void timer_add(pc_timer_t *timer, void (*callback)(void *), void *priv, int start)
{
    timer->callback = callback;
    timer->priv = priv;
    timer->flags = start ? TIMER_ENABLED : 0;
}
void picint_common(uint16_t mask, int, int set, uint8_t *)
{
    if (set)
        irq_lines |= mask;
    else
        irq_lines &= ~mask;
}
void dma_set_drq(int, int) { ++dma_accesses; }
int dma_get_drq(int) { ++dma_accesses; return 0; }
int dma_channel_read(int) { ++dma_accesses; return DMA_NODATA; }
int dma_channel_write(int, uint16_t) { ++dma_accesses; return DMA_NODATA; }
int dma_mode(int) { ++dma_accesses; return 0; }
void ui_sb_update_icon(int, int) {}
void ui_sb_update_icon_write(int, int) {}
int fdd_tape_present(int) { return 0; }
int fdd_tape_track0(int) { return 0; }
int fdd_tape_get_flags(int) { return 0; }
int fdd_tape_step(int, int) { return 0; }
double fdd_audio_get_seek_time(int, int, int) { return 10000.0; }
void fdd_audio_set_motor_enable(int, int) {}
void fdd_audio_play_multi_track_seek(int, int, int) {}
int floppy_ioctl_read_sector(int, int, int, int, uint8_t *) { return 0; }
int floppy_ioctl_write_sector(int, int, int, int, const uint8_t *) { return 0; }
void floppy_ioctl_close(int) {}
FILE *plat_fopen(const char *path, const char *mode) { return std::fopen(path, mode); }
char *path_get_extension(char *path)
{
    char *dot = std::strrchr(path, '.');
    return dot ? dot + 1 : path + std::strlen(path);
}
void pclog(const char *, ...) {}
void pclog_ex(const char *, va_list) {}
void fatal(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::abort();
}
uint8_t random_generate(void) { return 0; }

void io_sethandler(uint16_t base, uint16_t size, uint8_t (*inb)(uint16_t, void *),
                   uint16_t (*)(uint16_t, void *), uint32_t (*)(uint16_t, void *),
                   void (*outb)(uint16_t, uint8_t, void *), void (*)(uint16_t, uint16_t, void *),
                   void (*)(uint16_t, uint32_t, void *), void *priv)
{
    io_handlers.push_back({ base, size, inb, outb, priv });
}
void io_removehandler(uint16_t base, uint16_t size, uint8_t (*)(uint16_t, void *),
                      uint16_t (*)(uint16_t, void *), uint32_t (*)(uint16_t, void *),
                      void (*)(uint16_t, uint8_t, void *), void (*)(uint16_t, uint16_t, void *),
                      void (*)(uint16_t, uint32_t, void *), void *priv)
{
    io_handlers.erase(std::remove_if(io_handlers.begin(), io_handlers.end(),
        [=](const IoHandler &handler) { return handler.base == base && handler.size == size && handler.priv == priv; }),
        io_handlers.end());
}
}
