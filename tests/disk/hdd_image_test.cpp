#include <gtest/gtest.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#ifdef __linux__
#    include <sys/syscall.h>
#    include <unistd.h>
#endif

extern "C" {
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/hdd.h>
#include <86box/path.h>
#include <86box/plat.h>
}

namespace {

// Exercise the production image loader and real file I/O. Only platform
// services are adapted; the image sizing and writes are not mocked.
class HddImage : public ::testing::Test {
protected:
    static constexpr size_t cylinder_bytes = 4 * 17 * 512;
    std::filesystem::path directory;
    std::filesystem::path image_path;

    void SetUp() override
    {
        std::memset(hdd, 0, sizeof(hdd));
        hdd_image_init();
        std::error_code error;
        const auto root = std::filesystem::temp_directory_path(error);
        ASSERT_FALSE(error) << error.message();
        for (unsigned i = 0; i < 1024; ++i) {
            const auto candidate = root / ("86box-hdd-image-test-" + std::to_string(i));
            if (std::filesystem::create_directory(candidate, error)) {
                directory = candidate;
                break;
            }
            ASSERT_TRUE(!error || error == std::errc::file_exists) << error.message();
        }
        ASSERT_FALSE(directory.empty()) << "No available HDD image test directory";
        image_path = directory / "disk.img";
        const auto filename = image_path.string();
        ASSERT_LT(filename.size(), sizeof(hdd[0].fn));
        std::memcpy(hdd[0].fn, filename.c_str(), filename.size() + 1);
        hdd[0].spt = 17;
        hdd[0].hpc = 4;
        hdd[0].tracks = 4;
    }

    void TearDown() override
    {
        hdd_image_close(0);
        if (!directory.empty()) {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
            EXPECT_FALSE(error) << error.message();
        }
    }

    static std::vector<uint8_t> pattern(size_t size)
    {
        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; ++i)
            data[i] = static_cast<uint8_t>(1 + (i + i / 512) % 251);
        return data;
    }

    void write_image(const std::vector<uint8_t> &data)
    {
        std::ofstream file(image_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        file.write(reinterpret_cast<const char *>(data.data()),
                   static_cast<std::streamsize>(data.size()));
        file.close();
        ASSERT_TRUE(file.good());
    }

    std::vector<uint8_t> read_image()
    {
        std::ifstream file(image_path, std::ios::binary);
        if (!file.is_open()) {
            ADD_FAILURE() << "Cannot reopen backing image";
            return {};
        }
        std::vector<uint8_t> data { std::istreambuf_iterator<char>(file),
                                    std::istreambuf_iterator<char>() };
        EXPECT_FALSE(file.bad());
        return data;
    }
};

TEST_F(HddImage, NewRawImageHasConfiguredCapacityAndZeroFilledContents)
{
    ASSERT_EQ(hdd_image_load(0), 1);
    EXPECT_EQ(hdd_image_get_last_sector(0), 4 * 4 * 17 - 1u);
    hdd_image_close(0);

    const auto actual = read_image();
    ASSERT_EQ(actual.size(), 4 * cylinder_bytes);
    EXPECT_TRUE(std::all_of(actual.begin(), actual.end(), [](uint8_t b) { return b == 0; }));
}

TEST_F(HddImage, ExistingRawImageMatchingGeometryKeepsAllContents)
{
    const auto original = pattern(4 * cylinder_bytes);
    ASSERT_NO_FATAL_FAILURE(write_image(original));
    ASSERT_EQ(hdd_image_load(0), 1);
    hdd_image_close(0);

    EXPECT_EQ(read_image(), original);
}

TEST_F(HddImage, IncreasingRawImageGeometryPreservesDataAndZeroFillsAddedCapacity)
{
    // A user adds an existing raw image and increases Cylinders from 3 to 4
    // in the hard disk dialog. Loading it must extend the file without losing
    // the original partition/filesystem bytes, including the final sector.
    const auto original = pattern(3 * cylinder_bytes);
    ASSERT_NO_FATAL_FAILURE(write_image(original));
    ASSERT_EQ(hdd_image_load(0), 1);
    hdd_image_close(0);

    const auto actual = read_image();
    EXPECT_EQ(actual.size(), 4 * cylinder_bytes) << "Configured image capacity";
    ASSERT_GE(actual.size(), original.size()) << "Loading erased existing image bytes";
    EXPECT_TRUE(std::equal(original.begin(), original.end(), actual.begin()))
        << "Existing disk contents changed";
    EXPECT_TRUE(std::all_of(actual.begin() + original.size(), actual.end(),
                           [](uint8_t b) { return b == 0; }))
        << "Added capacity must contain zero-filled sectors";
}

#ifdef __linux__
// Guest writes to a file image are written by a thread of their own. The
// pwrite() below stands in for the host's: it can hold every write until the
// test lets it through, as a filesystem commit would, or fail one.
class HddImageWriteBehind : public HddImage {
protected:
    static constexpr uint32_t sectors = 4 * 4 * 17;

    void SetUp() override
    {
        HddImage::SetUp();
        host_writes_pass(true);
        host_write_fail_at(-1);
    }

    void TearDown() override
    {
        host_writes_pass(true);
        HddImage::TearDown();
    }

public:
    static void host_writes_pass(bool pass);
    static void host_write_fail_at(off_t offset);
    static int  host_write_calls();

protected:
    static std::vector<uint8_t> sector_data(uint32_t count, uint8_t seed)
    {
        std::vector<uint8_t> data(static_cast<size_t>(count) * 512);
        for (size_t i = 0; i < data.size(); ++i)
            data[i] = static_cast<uint8_t>(seed + i * 7 + i / 512);
        return data;
    }

    static void put(std::vector<uint8_t> &model, uint32_t sector, const std::vector<uint8_t> &data)
    {
        std::copy(data.begin(), data.end(), model.begin() + static_cast<size_t>(sector) * 512);
    }

    static std::vector<uint8_t> read_sectors(uint32_t sector, uint32_t count)
    {
        std::vector<uint8_t> data(static_cast<size_t>(count) * 512, 0xee);
        EXPECT_EQ(hdd_image_read(0, sector, count, data.data()), 0);
        return data;
    }
};

namespace host {
std::mutex              mutex;
std::condition_variable changed;
bool                    pass    = true;
off_t                   fail_at = -1;
int                     calls   = 0;
}

void
HddImageWriteBehind::host_writes_pass(bool pass)
{
    std::lock_guard<std::mutex> lock(host::mutex);
    host::pass = pass;
    host::changed.notify_all();
}

void
HddImageWriteBehind::host_write_fail_at(off_t offset)
{
    std::lock_guard<std::mutex> lock(host::mutex);
    host::fail_at = offset;
}

int
HddImageWriteBehind::host_write_calls()
{
    std::lock_guard<std::mutex> lock(host::mutex);
    return host::calls;
}

TEST_F(HddImageWriteBehind, ReadsSeeWritesStillQueuedBehindAStalledHostWrite)
{
    ASSERT_EQ(hdd_image_load(0), 1);
    std::vector<uint8_t> model(static_cast<size_t>(sectors) * 512, 0);

    host_writes_pass(false);
    const auto a = sector_data(10, 0x11);
    const auto b = sector_data(10, 0x22);
    const auto c = sector_data(5, 0x33);
    auto       a_copy = a;
    const int  calls  = host_write_calls();
    // Each returns at once although the host holds the first write
    ASSERT_EQ(hdd_image_write(0, 10, 10, a_copy.data()), 0);
    std::fill(a_copy.begin(), a_copy.end(), 0); // the queue keeps its own copy
    // The writer takes it up and the host holds it there
    while (host_write_calls() == calls)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_EQ(hdd_image_write(0, 15, 10, const_cast<uint8_t *>(b.data())), 0);
    ASSERT_EQ(hdd_image_write(0, 0, 5, const_cast<uint8_t *>(c.data())), 0);
    ASSERT_EQ(hdd_image_zero(0, 20, 2), 0);
    put(model, 10, a);
    put(model, 15, b);
    put(model, 0, c);
    put(model, 20, std::vector<uint8_t>(2 * 512, 0));

    // Overlapping the write being made, writes behind it, and none at all
    EXPECT_EQ(read_sectors(0, 30), std::vector<uint8_t>(model.begin(), model.begin() + 30 * 512));
    EXPECT_EQ(read_sectors(12, 2), std::vector<uint8_t>(model.begin() + 12 * 512, model.begin() + 14 * 512));
    EXPECT_EQ(read_sectors(100, 3), std::vector<uint8_t>(3 * 512, 0));
    EXPECT_EQ(hdd_image_get_pos(0), 103u);

    // Nothing has reached the file yet
    const auto before = read_image();
    EXPECT_TRUE(std::all_of(before.begin(), before.end(), [](uint8_t v) { return v == 0; }));

    host_writes_pass(true);
    EXPECT_EQ(hdd_image_flush(0), 0);
    EXPECT_EQ(read_image(), model);
    EXPECT_EQ(read_sectors(0, sectors), model);
}

TEST_F(HddImageWriteBehind, ARunOfSectorsWrittenOneAtATimeIsOneHostWrite)
{
    ASSERT_EQ(hdd_image_load(0), 1);
    std::vector<uint8_t> model(static_cast<size_t>(sectors) * 512, 0);

    // As an ATA WRITE MULTIPLE or PIO write hands them over
    host_writes_pass(false);
    const int calls = host_write_calls();
    for (uint32_t s = 20; s < 120; ++s) {
        auto data = sector_data(1, static_cast<uint8_t>(s));
        ASSERT_EQ(hdd_image_write(0, s, 1, data.data()), 0);
        put(model, s, data);
    }
    EXPECT_EQ(read_sectors(10, 120), std::vector<uint8_t>(model.begin() + 10 * 512, model.begin() + 130 * 512));
    host_writes_pass(true);
    EXPECT_EQ(hdd_image_flush(0), 0);

    EXPECT_LE(host_write_calls() - calls, 2);
    EXPECT_EQ(read_image(), model);
}

TEST_F(HddImageWriteBehind, AFailedHostWriteIsReportedByTheNextWriteOrFlush)
{
    ASSERT_EQ(hdd_image_load(0), 1);
    auto data = sector_data(4, 0x44);

    host_write_fail_at(50 * 512);
    EXPECT_EQ(hdd_image_write(0, 50, 4, data.data()), 0);
    EXPECT_EQ(hdd_image_flush(0), -1);
    EXPECT_EQ(hdd_image_flush(0), 0) << "Reported once";

    host_write_fail_at(60 * 512);
    EXPECT_EQ(hdd_image_write(0, 60, 4, data.data()), 0);
    // A write made once the host has refused that one reports it
    bool reported = false;
    for (int tries = 0; (tries < 5000) && !reported; ++tries) {
        reported = hdd_image_write(0, 70, 4, data.data()) == -1;
        if (!reported)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(reported);
    EXPECT_EQ(hdd_image_flush(0), 0);
}

TEST_F(HddImageWriteBehind, ClosingWritesEverythingQueued)
{
    ASSERT_EQ(hdd_image_load(0), 1);
    std::vector<uint8_t> model(static_cast<size_t>(sectors) * 512, 0);

    host_writes_pass(false);
    for (uint32_t s = 0; s < sectors; s += 16) {
        const uint32_t n    = std::min<uint32_t>(16, sectors - s);
        auto           data = sector_data(n, static_cast<uint8_t>(s));
        ASSERT_EQ(hdd_image_write(0, s, n, data.data()), 0);
        put(model, s, data);
    }
    std::thread release([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        HddImageWriteBehind::host_writes_pass(true);
    });
    hdd_image_close(0);
    release.join();

    EXPECT_EQ(read_image(), model);
}

TEST_F(HddImageWriteBehind, RandomWritesAndReadsMatchTheDisk)
{
    ASSERT_EQ(hdd_image_load(0), 1);
    std::vector<uint8_t> model(static_cast<size_t>(sectors) * 512, 0);
    uint32_t             seed = 12345;
    auto                 next = [&seed](uint32_t range) {
        seed = seed * 1103515245u + 12345u;
        return (seed >> 8) % range;
    };

    for (int i = 0; i < 4000; ++i) {
        // Held stretches stay far below the 16 MB the queue may hold
        if ((i % 256) == 255)
            host_writes_pass(true);
        else if (next(64) == 0)
            host_writes_pass(next(2) == 0);
        const uint32_t sector = next(sectors);
        const uint32_t count  = 1 + next(std::min<uint32_t>(24, sectors - sector));
        switch (next(5)) {
            case 0:
            case 1: {
                auto data = sector_data(count, static_cast<uint8_t>(i));
                ASSERT_EQ(hdd_image_write(0, sector, count, data.data()), 0);
                put(model, sector, data);
                break;
            }
            case 2:
                ASSERT_EQ(hdd_image_zero(0, sector, count), 0);
                put(model, sector, std::vector<uint8_t>(static_cast<size_t>(count) * 512, 0));
                break;
            default:
                ASSERT_EQ(read_sectors(sector, count),
                          std::vector<uint8_t>(model.begin() + static_cast<size_t>(sector) * 512,
                                               model.begin() + static_cast<size_t>(sector + count) * 512))
                    << "read " << sector << "+" << count << " at step " << i;
                break;
        }
    }
    host_writes_pass(true);
    hdd_image_close(0);

    EXPECT_EQ(read_image(), model);
}
#endif

} // namespace

#ifdef __linux__
// The host's pwrite() as the write-behind thread sees it
extern "C" ssize_t
pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    {
        std::unique_lock<std::mutex> lock(host::mutex);
        host::calls++;
        host::changed.wait(lock, [] { return host::pass; });
        if ((host::fail_at >= 0) && (offset == host::fail_at)) {
            host::fail_at = -1;
            errno         = EIO;
            return -1;
        }
    }
    return syscall(SYS_pwrite64, fd, buf, count, offset);
}
#endif

extern "C" {
hard_disk_t hdd[HDD_SLOTS];

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

void path_normalize(char *) {}
int plat_is_block_device(const char *) { return 0; }
int64_t plat_get_block_device_size(const char *) { return -1; }
plat_device_vol_locked_t *plat_lock_volumes(FILE *) { return nullptr; }
void plat_unlock_volumes(plat_device_vol_locked_t *) {}
void pclog(const char *, ...) {}
void pclog_toggle_suppr(void) {}

void
fatal(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::abort();
}
}
