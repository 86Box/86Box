// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <cstdint>
#include <gtest/gtest.h>

extern "C" {
#include <86box/86box.h>
#include "cpu.h"
#include "x86seg_common.h"
#include <86box/mem.h>

cpu_state_t cpu_state;
uint32_t cr2, cr3, cr4, abrt_error, cpu_features;
int cpl_override, is486, isibm486;
}

class MmuTranslationTest : public testing::TestWithParam<bool> {
protected:
    alignas(4096) std::array<uint8_t, 0x10000> memory{};
    static constexpr uint32_t linear = 0x1234;

    void SetUp() override
    {
        cpu_state = {};
        cr2 = cr4 = abrt_error = cpu_features = 0;
        cpl_override = isibm486 = 0;
        is486 = 1;
        for (size_t offset = 0; offset < memory.size(); offset += MEM_GRANULARITY_SIZE)
            _mem_exec[offset >> MEM_GRANULARITY_BITS] = memory.data() + offset;
        cr3 = GetParam() ? 0x1000 : 0x2000;
        if (GetParam()) {
            cr4 = CR4_PAE;
            entry64(0x1000) = 0x2001;
            pae_load_pdptes();
        }
        directory(0x3007);
        table(0x5007);
    }

    void TearDown() override
    {
        for (size_t offset = 0; offset < memory.size(); offset += MEM_GRANULARITY_SIZE)
            _mem_exec[offset >> MEM_GRANULARITY_BITS] = nullptr;
    }

    uint32_t &entry32(uint32_t address)
    {
        return *reinterpret_cast<uint32_t *>(memory.data() + address);
    }

    uint64_t &entry64(uint32_t address)
    {
        return *reinterpret_cast<uint64_t *>(memory.data() + address);
    }

    uint64_t directory() { return GetParam() ? entry64(0x2000) : entry32(0x2000); }
    uint64_t table() { return GetParam() ? entry64(0x3008) : entry32(0x3004); }

    void directory(uint32_t value)
    {
        if (GetParam())
            entry64(0x2000) = value;
        else
            entry32(0x2000) = value;
    }

    void table(uint32_t value)
    {
        if (GetParam())
            entry64(0x3008) = value;
        else
            entry32(0x3004) = value;
    }
};

TEST_P(MmuTranslationTest, ReadThenWriteUpdatesAccessedAndDirtyBits)
{
    EXPECT_EQ(mmutranslatereal(linear, 0), 0x5234u);
    EXPECT_EQ(directory(), 0x3027u);
    EXPECT_EQ(table(), 0x5027u);
    EXPECT_EQ(cpu_state.abrt, ABRT_NONE);
    EXPECT_EQ(mmutranslatereal(linear, 1), 0x5234u);
    EXPECT_EQ(directory(), 0x3027u);
    EXPECT_EQ(table(), 0x5067u);
    if (GetParam())
        EXPECT_EQ(entry64(0x1000), 0x2001u);
}

TEST_P(MmuTranslationTest, MissingEntryReportsWriteFault)
{
    table(0x5006);
    EXPECT_EQ(mmutranslatereal(linear, 1), UINT64_MAX);
    EXPECT_EQ(cpu_state.abrt, ABRT_PF);
    EXPECT_EQ(cr2, linear);
    EXPECT_EQ(abrt_error, 2u);
    EXPECT_EQ(table(), 0x5006u);
}

TEST_P(MmuTranslationTest, SupervisorWriteProtectionReportsProtectionFault)
{
    cr0 = WP_FLAG;
    table(0x5005);
    EXPECT_EQ(mmutranslatereal(linear, 1), UINT64_MAX);
    EXPECT_EQ(cpu_state.abrt, ABRT_PF);
    EXPECT_EQ(cr2, linear);
    EXPECT_EQ(abrt_error, 3u);
    EXPECT_EQ(table(), 0x5005u);
}

TEST_P(MmuTranslationTest, UserAccessRequiresUserDirectoryPermission)
{
    cpu_state.seg_cs.access = 0x60;
    directory(0x3003);
    EXPECT_EQ(mmutranslatereal(linear, 0), UINT64_MAX);
    EXPECT_EQ(cpu_state.abrt, ABRT_PF);
    EXPECT_EQ(cr2, linear);
    EXPECT_EQ(abrt_error, 5u);
    EXPECT_EQ(table(), 0x5007u);
}

TEST_P(MmuTranslationTest, LargePageReadAndWriteUpdateDirectory)
{
    cr4 |= CR4_PSE;
    directory(0x400087);
    EXPECT_EQ(mmutranslatereal(linear, 0), 0x401234u);
    EXPECT_EQ(directory(), 0x4000a7u);
    EXPECT_EQ(mmutranslatereal(linear, 1), 0x401234u);
    EXPECT_EQ(directory(), 0x4000e7u);
    EXPECT_EQ(cpu_state.abrt, ABRT_NONE);
}

TEST_P(MmuTranslationTest, PriorAbortDoesNotWalkOrOverwriteFaultState)
{
    cpu_state.abrt = ABRT_GPF;
    cr2 = 0xabcdef;
    abrt_error = 7;
    EXPECT_EQ(mmutranslatereal(linear, 1), UINT64_MAX);
    EXPECT_EQ(cpu_state.abrt, ABRT_GPF);
    EXPECT_EQ(cr2, 0xabcdefu);
    EXPECT_EQ(abrt_error, 7u);
    EXPECT_EQ(directory(), 0x3007u);
    EXPECT_EQ(table(), 0x5007u);
}

INSTANTIATE_TEST_SUITE_P(PagingModes, MmuTranslationTest, testing::Bool());
