#include <gtest/gtest.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

extern "C" {
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/cartridge.h>
#include <86box/mem.h>
#include <86box/plat.h>
#include <86box/ui.h>
}

namespace {

// Only external cartridge mappings participate: no RAM, BIOS, paging, or CPU
// execution. Keep registration order and enable/set_addr semantics from mem.c;
// reads use the loader's real callback and instruction fetches its exec buffer.
std::vector<mem_mapping_t *> mappings;

mem_mapping_t *
mapping_at(uint32_t address)
{
    for (auto it = mappings.rbegin(); it != mappings.rend(); ++it) {
        auto *mapping = *it;
        if (mapping->enable && address >= mapping->base && address - mapping->base < mapping->size)
            return mapping;
    }
    return nullptr;
}

uint8_t
read_byte(uint32_t address)
{
    const auto *mapping = mapping_at(address);
    return mapping && mapping->read_b ? mapping->read_b(address, mapping->priv) : 0xff;
}

uint8_t
fetch_byte(uint32_t address)
{
    const auto *mapping = mapping_at(address);
    return mapping && mapping->exec ? mapping->exec[address - mapping->base] : 0xff;
}

std::vector<uint8_t>
payload(size_t size, uint8_t seed)
{
    std::vector<uint8_t> bytes(size);
    for (size_t i = 0; i < size; ++i)
        bytes[i] = static_cast<uint8_t>((seed + i * 37 + (i >> 8)) % 251);
    return bytes;
}

std::vector<uint8_t>
jrc_image(uint16_t segment, const std::vector<uint8_t> &bytes)
{
    std::vector<uint8_t> image(512, 0xa5);
    image[0x1ce] = static_cast<uint8_t>(segment);
    image[0x1cf] = static_cast<uint8_t>(segment >> 8);
    image.insert(image.end(), bytes.begin(), bytes.end());
    return image;
}

void
expect_rom(uint32_t base, const std::vector<uint8_t> &bytes)
{
    for (size_t i = 0; i < bytes.size(); ++i) {
        const auto address = base + static_cast<uint32_t>(i);
        ASSERT_EQ(read_byte(address), bytes[i]) << "Read address " << address;
        ASSERT_EQ(fetch_byte(address), bytes[i]) << "Fetch address " << address;
    }
}

void
expect_open_bus(uint32_t address)
{
    EXPECT_EQ(read_byte(address), 0xff) << "Read address " << address;
    EXPECT_EQ(fetch_byte(address), 0xff) << "Fetch address " << address;
}

class CartridgeTest : public ::testing::Test {
protected:
    std::filesystem::path directory;

    void SetUp() override
    {
        // Atomic directory creation isolates concurrent test processes while
        // filenames and image contents remain deterministic.
        std::error_code error;
        const auto      root = std::filesystem::temp_directory_path(error);
        ASSERT_FALSE(error) << error.message();
        for (unsigned i = 0; i < 1024; ++i) {
            const auto candidate = root / ("86box-cartridge-test-" + std::to_string(i));
            if (std::filesystem::create_directory(candidate, error)) {
                directory = candidate;
                break;
            }
            ASSERT_TRUE(!error || error == std::errc::file_exists) << error.message();
        }
        ASSERT_FALSE(directory.empty()) << "No available cartridge test directory";
        cart_fns[0][0] = cart_fns[1][0] = 0;
        mappings.clear(); // A machine hard reset discards mappings before cart_reset.
        cart_reset();
    }

    void TearDown() override
    {
        cart_close(1);
        cart_close(0);
        mappings.clear();
        if (!directory.empty()) {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
            EXPECT_FALSE(error) << error.message();
        }
    }

    void write_image(const char *name, const std::vector<uint8_t> &bytes)
    {
        std::ofstream file(directory / name, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        file.write(reinterpret_cast<const char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        file.close();
        ASSERT_TRUE(file.good()) << "Could not write " << name;
    }

    void load(int slot, const char *name)
    {
        auto filename = (directory / name).string();
        cart_load(slot, filename.data());
    }
};

TEST_F(CartridgeTest, RawSlotsRelocateWhenReplacedWith32KiBImages)
{
    const auto small = payload(0x2000, 17);
    const auto large = payload(0x8000, 93);
    ASSERT_NO_FATAL_FAILURE(write_image("small.bin", small));
    ASSERT_NO_FATAL_FAILURE(write_image("large.bin", large));
    load(0, "small.bin");
    load(1, "small.bin");
    ASSERT_NO_FATAL_FAILURE(expect_rom(0xd0000, small));
    ASSERT_NO_FATAL_FAILURE(expect_rom(0xe0000, small));
    expect_open_bus(0xcffff);
    expect_open_bus(0xd2000);
    expect_open_bus(0xdffff);
    expect_open_bus(0xe2000);

    load(0, "large.bin");
    load(1, "large.bin");
    ASSERT_NO_FATAL_FAILURE(expect_rom(0xd8000, large));
    ASSERT_NO_FATAL_FAILURE(expect_rom(0xe8000, large));
    expect_open_bus(0xd0000);
    expect_open_bus(0xd7fff);
    expect_open_bus(0xe0000);
    expect_open_bus(0xe7fff);
    expect_open_bus(0xf0000);
}

TEST_F(CartridgeTest, JrcUsesLittleEndianSegmentAndExcludesHeader)
{
    const auto bytes = payload(0x1000, 29);
    ASSERT_NO_FATAL_FAILURE(write_image("header.jrc", jrc_image(0xc100, bytes)));
    load(0, "header.jrc");
    ASSERT_NO_FATAL_FAILURE(expect_rom(0xc1000, bytes));
    expect_open_bus(0xc0fff);
    expect_open_bus(0xc2000);
    expect_open_bus(0xd0000);
}

TEST_F(CartridgeTest, MissingFileReplacementRemovesOldRom)
{
    const auto bytes = payload(0x2000, 41);
    ASSERT_NO_FATAL_FAILURE(write_image("original.bin", bytes));
    load(0, "original.bin");
    ASSERT_NO_FATAL_FAILURE(expect_rom(0xd0000, bytes));

    // Deliberately do not cart_close first: failed direct API replacement must
    // eject the previous image rather than leave an invisible cartridge active.
    load(0, "missing.bin");
    EXPECT_EQ(cart_fns[0][0], '\0');
    expect_open_bus(0xd0000);
    expect_open_bus(0xd1fff);
}

TEST_F(CartridgeTest, MisalignedPayloadIsRejectedWithoutDisturbingOtherSlot)
{
    const auto first     = payload(0x2000, 53);
    auto       malformed = jrc_image(0xc100, payload(0x1000, 67));
    malformed.push_back(0x42);
    ASSERT_NO_FATAL_FAILURE(write_image("first.bin", first));
    ASSERT_NO_FATAL_FAILURE(write_image("misaligned.jrc", malformed));
    load(0, "first.bin");
    load(1, "misaligned.jrc");
    EXPECT_EQ(cart_fns[1][0], '\0');
    expect_open_bus(0xc1000);
    expect_open_bus(0xc2000);
    ASSERT_NO_FATAL_FAILURE(expect_rom(0xd0000, first));
}

TEST_F(CartridgeTest, Exact20BitEndIsAcceptedButOverflowingReplacementIsRejected)
{
    const auto bytes = payload(0x1000, 79);
    ASSERT_NO_FATAL_FAILURE(write_image("last-page.jrc", jrc_image(0xff00, bytes)));
    ASSERT_NO_FATAL_FAILURE(write_image("overflow.jrc", jrc_image(0xff01, bytes)));
    load(0, "last-page.jrc");
    ASSERT_NO_FATAL_FAILURE(expect_rom(0xff000, bytes));
    expect_open_bus(0xfefff);
    expect_open_bus(0x100000);

    load(0, "overflow.jrc");
    EXPECT_EQ(cart_fns[0][0], '\0');
    expect_open_bus(0xff000);
    expect_open_bus(0xff010);
    expect_open_bus(0xfffff);
    expect_open_bus(0x100000);
}

TEST_F(CartridgeTest, RawPayloadCannotExtendPast20BitAddressSpace)
{
    ASSERT_NO_FATAL_FAILURE(write_image("oversized.bin", payload(0x40000, 101)));
    load(0, "oversized.bin");
    EXPECT_EQ(cart_fns[0][0], '\0');
    expect_open_bus(0xd0000);
    expect_open_bus(0xfffff);
    expect_open_bus(0x100000);
}

TEST_F(CartridgeTest, OverlappingSecondCartridgeIsRejectedButAdjacentOneIsAccepted)
{
    const auto first    = payload(0x2000, 113);
    const auto adjacent = payload(0x1000, 127);
    ASSERT_NO_FATAL_FAILURE(write_image("first.bin", first));
    ASSERT_NO_FATAL_FAILURE(write_image("overlap.jrc", jrc_image(0xcf00, payload(0x3000, 139))));
    ASSERT_NO_FATAL_FAILURE(write_image("adjacent.jrc", jrc_image(0xd200, adjacent)));
    load(0, "first.bin");
    load(1, "overlap.jrc");
    EXPECT_EQ(cart_fns[1][0], '\0');
    expect_open_bus(0xcf000);
    ASSERT_NO_FATAL_FAILURE(expect_rom(0xd0000, first));

    load(1, "adjacent.jrc");
    ASSERT_NO_FATAL_FAILURE(expect_rom(0xd0000, first));
    ASSERT_NO_FATAL_FAILURE(expect_rom(0xd2000, adjacent));
    expect_open_bus(0xd3000);
    cart_close(1);
    expect_open_bus(0xd2000);
    ASSERT_NO_FATAL_FAILURE(expect_rom(0xd0000, first));
}

TEST_F(CartridgeTest, HardResetReloadsBothSlotsAfterMappingsAreDiscarded)
{
    const auto first  = payload(0x2000, 151);
    const auto second = payload(0x8000, 163);
    ASSERT_NO_FATAL_FAILURE(write_image("first.bin", first));
    ASSERT_NO_FATAL_FAILURE(write_image("second.bin", second));
    load(0, "first.bin");
    load(1, "second.bin");
    mappings.clear();
    cart_reset();
    ASSERT_NO_FATAL_FAILURE(expect_rom(0xd0000, first));
    ASSERT_NO_FATAL_FAILURE(expect_rom(0xe8000, second));
    cart_close(0);
    expect_open_bus(0xd0000);
    ASSERT_NO_FATAL_FAILURE(expect_rom(0xe8000, second));
}

} // namespace

extern "C" {

int machine = 0;

int
machine_has_cartridge(int)
{
    return 1;
}

// These loader tests use PCjr mappings; the JX decoder has separate board tests.
int
machine_is_pcjx(int)
{
    return 0;
}

FILE *
plat_fopen(const char *path, const char *mode)
{
    return std::fopen(path, mode);
}

// CPU reset and status-bar notification have no host side effects here. Hard
// resets are exercised explicitly above, in the emulator's mem_reset order.
void
resetx86(void)
{
}
void
ui_sb_update_icon_state(int, int)
{
}

void
pclog_ex(const char *format, va_list args)
{
    std::vfprintf(stderr, format, args);
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

void
mem_mapping_add(mem_mapping_t *mapping, uint32_t base, uint32_t size,
                uint8_t (*read_b)(uint32_t, void *),
                uint16_t (*read_w)(uint32_t, void *),
                uint32_t (*read_l)(uint32_t, void *),
                void (*write_b)(uint32_t, uint8_t, void *),
                void (*write_w)(uint32_t, uint16_t, void *),
                void (*write_l)(uint32_t, uint32_t, void *),
                uint8_t *exec, uint32_t flags, void *priv)
{
    *mapping         = {};
    mapping->enable  = size != 0;
    mapping->base    = base;
    mapping->size    = size;
    mapping->read_b  = read_b;
    mapping->read_w  = read_w;
    mapping->read_l  = read_l;
    mapping->write_b = write_b;
    mapping->write_w = write_w;
    mapping->write_l = write_l;
    mapping->exec    = exec;
    mapping->flags   = flags;
    mapping->priv    = priv;
    mappings.push_back(mapping);
}

void
mem_mapping_set_addr(mem_mapping_t *mapping, uint32_t base, uint32_t size)
{
    mapping->base   = base;
    mapping->size   = size;
    mapping->enable = 1;
}

void
mem_mapping_set_exec(mem_mapping_t *mapping, uint8_t *exec)
{
    mapping->exec = exec;
}

void
mem_mapping_set_p(mem_mapping_t *mapping, void *priv)
{
    mapping->priv = priv;
}

void
mem_mapping_disable(mem_mapping_t *mapping)
{
    mapping->enable = 0;
}

} // extern "C"
