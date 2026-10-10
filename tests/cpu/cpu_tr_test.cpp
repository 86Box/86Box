// SPDX-License-Identifier: GPL-2.0-or-later
// Intel i486 datasheet 240440-002, section 8.3 (TR6/TR7 diagnostics).
#include <cstdint>
#include <gtest/gtest.h>

extern "C" {
void cpu_tr_reset(void);
void cpu_tr_write(int reg, uint32_t value);
uint32_t cpu_tr_read(int reg);
// The cache-as-RAM path is unrelated to these TLB tests.
void mem_writel_phys(uint32_t, uint32_t) {}
}

class CpuTlbTest : public testing::Test {
protected:
    void SetUp() override { cpu_tr_reset(); }
    static void write(unsigned way, uint32_t tag, uint32_t data) {
        cpu_tr_write(7, data | 0x10 | (way << 2));
        cpu_tr_write(6, tag);
    }
    static uint32_t lookup(uint32_t tag) {
        cpu_tr_write(6, tag | 1);
        return cpu_tr_read(7);
    }
};

TEST_F(CpuTlbTest, AllSetsAndWaysRetainIndependentTranslations) {
    for (unsigned set = 0; set < 8; ++set)
        for (unsigned way = 0; way < 4; ++way)
            write(way, 0x100000 + (way << 20) + (set << 12) + 0xb40,
                  0xaaaa0000 + (set << 12) + (way << 20) + 0xc00);
    for (unsigned set = 0; set < 8; ++set)
        for (unsigned way = 0; way < 4; ++way) {
            auto tag = 0x100000 + (way << 20) + (set << 12) + 0xb40;
            EXPECT_EQ(lookup(tag) & 0xfffffc1c,
                      (0xaaaa0000 + (set << 12) + (way << 20) + 0xc00) | 0x10 | (way << 2));
            EXPECT_EQ(cpu_tr_read(6), tag | 1);
        }
}

TEST_F(CpuTlbTest, ProtectionPairsMatchForceHitAndForceMiss) {
    // For each D, U and W bit, test both stored values and all four comparisons.
    for (unsigned bit : {6u, 8u, 10u})
        for (unsigned stored = 0; stored < 2; ++stored) {
            cpu_tr_reset();
            auto tag = 0x12345800u | (stored << bit);
            write(2, tag, 0xabcde000);
            for (unsigned pair = 0; pair < 4; ++pair) {
                auto query = (0x12345fe0u & ~(3u << (bit - 1))) | (pair << (bit - 1));
                bool hit = pair == 3 || pair == stored + 1;
                EXPECT_EQ(bool(lookup(query) & 0x10), hit) << bit << ':' << stored << ':' << pair;
            }
        }
}

TEST_F(CpuTlbTest, ValidBitAndLinearTagMustMatch) {
    write(1, 0x12345b40, 0xabcde000);
    EXPECT_EQ(lookup(0x12345340) & 0x10, 0u);
    EXPECT_EQ(lookup(0x22345b40) & 0x10, 0u);
    EXPECT_EQ(lookup(0x12345b40) & 0xfffff01c, 0xabcde014u);
    cpu_tr_reset();
    EXPECT_EQ(lookup(0x12345b40) & 0x10, 0u);
}

TEST_F(CpuTlbTest, DuplicateMatchesMiss) {
    write(0, 0x12345b40, 0xaaaa0000);
    write(1, 0x12345b40, 0xbbbb0000);
    EXPECT_EQ(lookup(0x12345b40) & 0x10, 0u);
}

TEST_F(CpuTlbTest, LookupReportsOldLruAndUpdatesReplacement) {
    for (unsigned way = 0; way < 4; ++way)
        write(way, 0x100b40 + (way << 20), 0xaaaa0000 + (way << 12));
    // Access order 0,1,2,3 leaves all three tree bits clear.
    EXPECT_EQ(lookup(0x100b40) & 0x380, 0u);
    // Accessing way 0 sets B0 and B1; next replacement is way 2.
    cpu_tr_write(7, 0xbbbb0000);
    cpu_tr_write(6, 0x500b40);
    EXPECT_EQ(lookup(0x500b40) & 0xfffff01c, 0xbbbb0018u);
    EXPECT_EQ(lookup(0x300b40) & 0x10, 0u);
    EXPECT_NE(lookup(0x100b40) & 0x10, 0u);
}

TEST_F(CpuTlbTest, VectraPostAlternatingPatterns) {
    // T.04.02 F000:0925 tests all 32 translations with alternating data.
    for (uint32_t pattern : {0xaaaaa010u, 0x55555010u, 0xaaaaa010u})
        for (unsigned set = 0; set < 8; ++set) {
            uint32_t data = pattern;
            for (unsigned way = 0; way < 4; ++way) {
                cpu_tr_write(7, data);
                cpu_tr_write(6, 0x100b40 + (set << 12) + (way << 20));
                data = (data + 4) ^ 0xfffff000;
            }
            data = pattern;
            for (unsigned way = 0; way < 4; ++way) {
                auto command = 0x100b41 + (set << 12) + (way << 20);
                cpu_tr_write(6, command);
                EXPECT_EQ(cpu_tr_read(7) & 0xfffff01f, data);
                EXPECT_EQ(cpu_tr_read(6), command);
                data = (data + 4) ^ 0xfffff000;
            }
        }
}
