// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <array>
#include <cstdint>
#include <gtest/gtest.h>
extern "C" {
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
}

class MemFlushWriteTest : public testing::Test {
protected:
    std::array<uint8_t, 8192> memory{};
    std::array<uint8_t, 4096> remapped{};
    std::array<page_t, 2> page_data{};
    uint8_t *saved_ram;
    page_t *saved_pages;

    void SetUp() override
    {
        saved_ram = ram;
        saved_pages = pages;
        ram = memory.data();
        pages = page_data.data();
        std::fill_n(writelookup, 256, -1);
        std::fill_n(writelookup2, 1024, uintptr_t(LOOKUP_INV));
        std::fill_n(page_lookup, 1024, nullptr);
    }

    void TearDown() override
    {
        ram = saved_ram;
        pages = saved_pages;
    }

    void install(unsigned slot, unsigned virt, uint8_t *backing, page_t *page = nullptr)
    {
        writelookup[slot] = int(virt);
        writelookup2[virt] = reinterpret_cast<uintptr_t>(backing) - (uintptr_t(virt) << 12);
        page_lookup[virt] = page;
    }
};

TEST_F(MemFlushWriteTest, EmptyCacheDoesNotTouchLookupTables)
{
    writelookup2[0] = 123;
    page_lookup[0] = &pages[1];
    mem_flush_write_page(0x1000, 0);
    EXPECT_EQ(writelookup2[0], 123u);
    EXPECT_EQ(page_lookup[0], &pages[1]);
}

TEST_F(MemFlushWriteTest, FindsEveryPositionAmongEmptyGroups)
{
    for (unsigned slot = 0; slot < 256; slot++) {
        install(slot, slot, ram + 4096);
        mem_flush_write_page(0x1000, 0);
        EXPECT_EQ(writelookup[slot], -1);
        EXPECT_EQ(writelookup2[slot], uintptr_t(LOOKUP_INV));
        EXPECT_EQ(page_lookup[slot], nullptr);
    }
}

TEST_F(MemFlushWriteTest, InvalidatesAllPhysicalAndRemappedAliases)
{
    pages[1].mem = remapped.data();
    install(0, 1, ram + 4096);
    install(127, 2, remapped.data());
    install(255, 3, ram, &pages[1]);
    install(254, 4, ram, &pages[0]);
    const uintptr_t unrelated = writelookup2[4];
    mem_flush_write_page(0x1007, 0x1234);
    for (unsigned slot : { 0u, 127u, 255u })
        EXPECT_EQ(writelookup[slot], -1);
    for (unsigned virt : { 1u, 2u, 3u }) {
        EXPECT_EQ(writelookup2[virt], uintptr_t(LOOKUP_INV));
        EXPECT_EQ(page_lookup[virt], nullptr);
    }
    EXPECT_EQ(writelookup[254], 4);
    EXPECT_EQ(writelookup2[4], unrelated);
    EXPECT_EQ(page_lookup[4], &pages[0]);
}

TEST_F(MemFlushWriteTest, MixedCacheMatchesBackingAndPageIdentity)
{
    uint32_t random = 0x917d34ab;
    for (unsigned iteration = 0; iteration < 128; iteration++) {
        std::fill_n(writelookup, 256, -1);
        pages[1].mem = iteration & 1 ? remapped.data() : page_ff;
        std::array<bool, 256> invalidates{};
        std::array<uintptr_t, 256> original{};
        for (unsigned slot = 0; slot < 256; slot++) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            if ((random & 7) == 0)
                continue;
            const unsigned kind = (random >> 8) & 3;
            uint8_t *backing = kind == 0 ? ram + 4096 : kind == 1 ? remapped.data() : ram;
            page_t *page = kind == 2 ? &pages[1] : &pages[0];
            install(slot, slot + 256, backing, page);
            original[slot] = writelookup2[slot + 256];
            invalidates[slot] = kind == 0 || kind == 2 || (kind == 1 && (iteration & 1));
        }
        mem_flush_write_page(0x1fff, 0);
        for (unsigned slot = 0; slot < 256; slot++) {
            if (!original[slot])
                continue;
            EXPECT_EQ(writelookup[slot], invalidates[slot] ? -1 : int(slot + 256));
            EXPECT_EQ(writelookup2[slot + 256],
                      invalidates[slot] ? uintptr_t(LOOKUP_INV) : original[slot]);
        }
    }
}
