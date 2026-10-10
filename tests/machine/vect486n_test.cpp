// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <numeric>
#include <gtest/gtest.h>
#include "../../src/machine/m_at_vect486n.h"

struct BiosRevision {
    const char *version;
    const char *date;
    size_t jump;
    uint16_t displacement;
};

class Vectra486nBiosTest : public testing::TestWithParam<BiosRevision> {
protected:
    std::array<uint8_t, 65536> bios { };

    void SetUp() override
    {
        for (size_t i = 0; i < bios.size(); ++i)
            bios[i] = (i * 37) & 0xff;
        const auto &revision = GetParam();
        const uint8_t entry[] = { 0xb0, 0x06, 0xe6, 0x80, 0xe9,
                                  uint8_t(revision.displacement), uint8_t(revision.displacement >> 8),
                                  0xb0, 0x07, 0xe6, 0x80 };
        memcpy(bios.data() + 0x93, revision.version, 7);
        memcpy(bios.data() + 0xfff5, revision.date, 8);
        memcpy(bios.data() + revision.jump - 4, entry, sizeof(entry));
    }
};

TEST_P(Vectra486nBiosTest, SkipsOnlyCacheDiagnosticAndPreservesChecksum)
{
    auto original = bios;
    const auto jump = GetParam().jump;
    ASSERT_EQ(vect486n_patch_cache_test(bios.data(), bios.size()), 1);
    EXPECT_EQ(bios[jump], 0xeb);                              // short jump
    EXPECT_EQ(jump + 2 + int8_t(bios[jump + 1]), jump + 3);    // POST 07 entry
    EXPECT_EQ(std::accumulate(bios.begin(), bios.end(), 0u) & 0xff,
              std::accumulate(original.begin(), original.end(), 0u) & 0xff);
    for (size_t i = 0; i < bios.size(); ++i) {
        if (i < jump || i > jump + 2) {
            ASSERT_EQ(bios[i], original[i]) << i;
        }
    }
}

TEST_P(Vectra486nBiosTest, RejectsUnknownRevisionDateAndInstructionSequence)
{
    auto original = bios;
    for (size_t offset : { size_t(0x93), size_t(0xfff5), GetParam().jump, GetParam().jump + 3 }) {
        bios = original;
        bios[offset] ^= 1;
        auto changed = bios;
        EXPECT_EQ(vect486n_patch_cache_test(bios.data(), bios.size()), 0);
        EXPECT_EQ(bios, changed);
    }
}

TEST_P(Vectra486nBiosTest, RejectsInvalidSizeAndAlreadyPatchedImagesWithoutWriting)
{
    auto original = bios;
    EXPECT_EQ(vect486n_patch_cache_test(nullptr, 65536), 0);
    EXPECT_EQ(vect486n_patch_cache_test(bios.data(), 8), 0);
    EXPECT_EQ(vect486n_patch_cache_test(bios.data(), 65537), 0);
    EXPECT_EQ(bios, original);
    ASSERT_EQ(vect486n_patch_cache_test(bios.data(), bios.size()), 1);
    auto patched = bios;
    EXPECT_EQ(vect486n_patch_cache_test(bios.data(), bios.size()), 0);
    EXPECT_EQ(bios, patched);
}

INSTANTIATE_TEST_SUITE_P(SupportedRevisions, Vectra486nBiosTest,
                        testing::Values(BiosRevision { "T.04.02", "08/27/92", 0x84ff, 0x7f8a },
                                        BiosRevision { "T.04.05", "10/11/94", 0x859d, 0x7eec }),
                        [](const testing::TestParamInfo<BiosRevision> &info) {
                            return info.index == 0 ? "T0402" : "T0405";
                        });
