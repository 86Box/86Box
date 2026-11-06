#include <gtest/gtest.h>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
extern "C" {
#include <86box/86box.h>
#include <86box/cdrom.h>
#include <86box/cdrom_image.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
}

namespace {
class McnTest : public ::testing::Test {
protected:
    cdrom_t dev{};
    std::filesystem::path dir;

    void SetUp() override
    {
        dir = std::filesystem::temp_directory_path() /
              ("86box-mcn-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        std::filesystem::create_directories(dir);
        std::ofstream data(dir / "track.bin", std::ios::binary);
        data.seekp(640 * 2048 - 1);
        data.put(0);
        dev.bus_type = CDROM_BUS_ATAPI;
    }

    void TearDown() override
    {
        close();
        std::filesystem::remove_all(dir);
    }

    void close()
    {
        if (dev.local)
            dev.ops->close(dev.local);
        dev.local = nullptr;
        dev.ops = nullptr;
    }

    void open(const std::string &catalog)
    {
        close();
        {
            std::ofstream cue(dir / "disc.cue");
            cue << catalog << "\nFILE \"track.bin\" BINARY\n"
                << "  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n";
        }
        dev.local = image_open(&dev, (dir / "disc.cue").string().c_str());
        // A cached position avoids unrelated sector decoding in these tests.
        dev.cached_sector = dev.subc_sector = dev.seek_pos = 0;
        dev.cached_subc.abs_f = 74;
    }

    std::array<uint8_t, 48> response(uint8_t format = 2)
    {
        std::array<uint8_t, 48> b;
        b.fill(0xa5);
        b[0] = format;
        cdrom_get_current_subchannel(&dev, b.data(), 1);
        return b;
    }
};

TEST_F(McnTest, CueCatalogPreservesLeadingZero)
{
    open("CATALOG 0123456789012");
    ASSERT_NE(dev.local, nullptr);
    const auto b = response();
    EXPECT_EQ(b[4], 0x80);
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(&b[5]), 13), "0123456789012");
    EXPECT_EQ(b[18], 0);
    EXPECT_EQ(b[19], 74);
    EXPECT_EQ(b[20], 0xa5);
}

TEST_F(McnTest, QuotedCatalogAndBcdDrive)
{
    open("CATALOG \"9876543210987\"");
    ASSERT_NE(dev.local, nullptr);
    dev.is_bcd = 1;
    const auto b = response();
    EXPECT_EQ(b[4], 0x80);
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(&b[5]), 13), "9876543210987");
    EXPECT_EQ(b[19], 0x74);
}

TEST_F(McnTest, MissingAndAllZeroCatalogAreNotValid)
{
    for (const auto &catalog : {"", "CATALOG 0000000000000"}) {
        open(catalog);
        ASSERT_NE(dev.local, nullptr);
        const auto b = response();
        EXPECT_EQ(b[4], 0);
        EXPECT_EQ(std::string(reinterpret_cast<const char *>(&b[5]), 13), "0000000000000");
    }
}

TEST_F(McnTest, MalformedCatalogIsRejected)
{
    for (const auto &catalog : {"CATALOG 123", "CATALOG 01234567890123",
                               "CATALOG 01234567890X2", "CATALOG \"0123456789012"}) {
        open(catalog);
        EXPECT_EQ(dev.local, nullptr) << catalog;
    }
}

TEST_F(McnTest, ReplacementDoesNotKeepPreviousCatalog)
{
    open("CATALOG 0123456789012");
    ASSERT_NE(dev.local, nullptr);
    EXPECT_EQ(response()[4], 0x80);
    open("");
    ASSERT_NE(dev.local, nullptr);
    EXPECT_EQ(response()[4], 0);
    close();
    dev.local = image_open(&dev, (dir / "track.bin").string().c_str());
    ASSERT_NE(dev.local, nullptr);
    EXPECT_EQ(response()[4], 0);
}

TEST_F(McnTest, ScsiFormatZeroContainsCatalog)
{
    open("CATALOG 0123456789012");
    ASSERT_NE(dev.local, nullptr);
    dev.bus_type = CDROM_BUS_SCSI;
    const auto b = response(0);
    EXPECT_EQ(b[12], 0x80);
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(&b[13]), 13), "0123456789012");
    EXPECT_EQ(b[26], 0);
    EXPECT_EQ(b[27], 74);
}

TEST_F(McnTest, BackendWithoutCatalogCallbackIsNotValid)
{
    cdrom_ops_t ops{};
    dev.ops = &ops;
    const auto b = response();
    EXPECT_EQ(b[4], 0);
    dev.ops = nullptr;
}
}

extern "C" {
void *log_open(const char *) { return nullptr; }
void log_close(void *) {}
void log_fatal(void *, const char *, ...) { std::abort(); }
void warning(const char *, ...) {}
void fatal(const char *, ...) { std::abort(); }
void pclog(const char *, ...) {}
char *plat_get_string(int) { static char text[] = "image error"; return text; }
char *nvr_path(char *path) { return path; }
FILE *plat_fopen(const char *path, const char *mode) { return std::fopen(path, mode); }
FILE *plat_fopen64(const char *path, const char *mode) { return std::fopen(path, mode); }
FILE *rom_fopen(const char *path, char *mode) { return std::fopen(path, mode); }
void *dynld_module(const char *, dllimp_t *) { return nullptr; }
void dynld_close(void *) {}
track_file_t *viso_init(uint8_t, const char *, int *error) { *error = 1; return nullptr; }
void plat_tempfile(char *, char *, char *) { std::abort(); }
void path_get_dirname(char *dest, const char *path)
{
    const auto dir = std::filesystem::path(path).parent_path().string();
    std::memcpy(dest, dir.c_str(), dir.size() + 1);
}
#ifndef _WIN32
int stricmp(const char *a, const char *b)
{
    while (*a && std::tolower(static_cast<unsigned char>(*a)) == std::tolower(static_cast<unsigned char>(*b))) {
        ++a;
        ++b;
    }
    return std::tolower(static_cast<unsigned char>(*a)) - std::tolower(static_cast<unsigned char>(*b));
}
#endif
int path_abs(char *path) { return std::filesystem::path(path).is_absolute(); }
void path_normalize(char *) {}
void path_append_filename(char *dest, const char *base, const char *file)
{
    const auto path = (std::filesystem::path(base) / file).string();
    std::memmove(dest, path.c_str(), path.size() + 1);
}
}
