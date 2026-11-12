// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <cstdint>
#include <cstring>
#include <sys/mman.h>
#include <gtest/gtest.h>

extern "C" size_t codegen_rx_memory(uint8_t *, int, int, uint16_t, int, int, int32_t);

TEST(CodegenRx, UsesShortestDisplacementForEveryRegisterAndPrefix)
{
    constexpr std::array<int32_t, 9> offsets = {
        INT32_MIN, -129, -128, -1, 0, 1, 127, 128, INT32_MAX
    };
    for (int base = 0; base < 16; base++) {
        for (int reg = 0; reg < 16; reg++) {
            for (int p66 = 0; p66 < 2; p66++) {
                for (int w = 0; w < 2; w++) {
                    for (uint16_t op : { 0x8b, 0x0fb7 }) {
                        for (int32_t offset : offsets) {
                            std::array<uint8_t, 16> bytes{};
                            const size_t size = codegen_rx_memory(
                                bytes.data(), p66, w, op, reg, base, offset);
                            SCOPED_TRACE(testing::Message() << base << "," << reg << ","
                                         << p66 << "," << w << "," << op << "," << offset);
                            size_t cursor = 0;
                            if (p66)
                                ASSERT_EQ(bytes[cursor++], 0x66);
                            const uint8_t rex = 0x40 | (w << 3) | ((reg >> 3) << 2) | (base >> 3);
                            if (rex != 0x40)
                                ASSERT_EQ(bytes[cursor++], rex);
                            if (op > 0xff)
                                ASSERT_EQ(bytes[cursor++], 0x0f);
                            ASSERT_EQ(bytes[cursor++], op & 0xff);
                            const uint8_t modrm = bytes[cursor++];
                            ASSERT_EQ((modrm >> 3) & 7, reg & 7);
                            ASSERT_EQ(modrm & 7, base & 7);
                            if ((base & 7) == 4)
                                ASSERT_EQ(bytes[cursor++], 0x24);
                            const unsigned mod = modrm >> 6;
                            if (offset < -128 || offset > 127) {
                                ASSERT_EQ(mod, 2u);
                                int32_t actual;
                                std::memcpy(&actual, bytes.data() + cursor, sizeof(actual));
                                cursor += sizeof(actual);
                                ASSERT_EQ(actual, offset);
                            } else if (offset || (base & 7) == 5) {
                                ASSERT_EQ(mod, 1u);
                                ASSERT_EQ(int8_t(bytes[cursor++]), offset);
                            } else
                                ASSERT_EQ(mod, 0u);
                            ASSERT_EQ(cursor, size);
                        }
                    }
                }
            }
        }
    }
}

TEST(CodegenRx, NativeLoadsUseTheRequestedBaseAndSignedDisplacement)
{
    constexpr std::array<int32_t, 7> offsets = { -129, -128, -1, 0, 1, 127, 128 };
    std::array<uint8_t, 1024> data;
    for (size_t i = 0; i < data.size(); i++)
        data[i] = uint8_t(i * 13 + 19);
    void *mapping = mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_NE(mapping, MAP_FAILED);
    uint8_t *executable = static_cast<uint8_t *>(mapping);
    unsigned functions = 0;
    for (int base = 0; base < 16; base++) {
        if (base == 4)
            continue; // RSP must remain a valid host stack during execution.
        const bool saved = base == 3 || base == 5 || base >= 12;
        for (int32_t offset : offsets) {
            uint8_t *bytes = executable + functions++ * 32;
            size_t cursor = 0;
            if (saved) {
                if (base >= 8)
                    bytes[cursor++] = 0x41;
                bytes[cursor++] = 0x50 | (base & 7);
            }
            bytes[cursor++] = 0x48 | (base >> 3);
            bytes[cursor++] = 0x89;
            bytes[cursor++] = 0xf8 | (base & 7); // MOV base, RDI (the pointer argument).
            cursor += codegen_rx_memory(bytes + cursor, 0, 1, 0x8b, 0, base, offset);
            if (saved) {
                if (base >= 8)
                    bytes[cursor++] = 0x41;
                bytes[cursor++] = 0x58 | (base & 7);
            }
            bytes[cursor++] = 0xc3;
            EXPECT_LE(cursor, 32u);
        }
    }
    EXPECT_LE(functions * 32, 4096u);
    if (mprotect(executable, 4096, PROT_READ | PROT_EXEC)) {
        munmap(executable, 4096);
        FAIL() << "mprotect failed";
    }
    unsigned function = 0;
    for (int base = 0; base < 16; base++) {
        if (base == 4)
            continue;
        for (int32_t offset : offsets) {
            uint64_t expected;
            std::memcpy(&expected, data.data() + 512 + offset, sizeof(expected));
            auto load = reinterpret_cast<uint64_t (*)(const void *)>(executable + function++ * 32);
            EXPECT_EQ(load(data.data() + 512), expected) << base << "," << offset;
        }
    }
    EXPECT_EQ(munmap(executable, 4096), 0);
}
