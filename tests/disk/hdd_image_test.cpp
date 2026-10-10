#include <gtest/gtest.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

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

} // namespace

extern "C" {
hard_disk_t hdd[HDD_NUM];

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
