// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>

extern "C" {
#include "cpu.h"
#include "x86.h"
#include <86box/mem.h>
#include <86box/debugger.h>

cpu_state_t cpu_state;
int         is286, is386, is_nec, cpu_16bitbus, hascache;
int         cpu_cache_int_enabled, cpu_flush_pending, stack32;
uint32_t    use32, cr2, cr3, cr4, dr[8], cpu_features;
uint16_t    cpu_cur_status;
uint64_t    cpu_CR4_mask, cpu_CR4_ignore, pae_pdpte[4];
x86seg      gdt, idt, ldt, tr;
uint8_t    *ram;

static int flushes, dr_updates, flag_extracts, prefetch_flushes, pae_invalid;
static int stopped, partial, emulation8080;
void
debugger_attach(int)
{
    stopped = 0;
}
void
debugger_pause(void)
{
    stopped = 1;
}
void
debugger_continue(void)
{
    stopped = 0;
}
int
debugger_stopped(void)
{
    return stopped;
}
int
debugger_stopped_partial(void)
{
    return partial;
}
uint16_t
debugger_flags(void)
{
    return cpu_state.flags;
}
void
debugger_cpu_info(debugger_cpu_info_t *info)
{
    info->ip = cpu_state.pc;
}
void
debugger_mem_read(int, uint32_t address, uint32_t length, uint8_t *data, uint8_t *cls)
{
    for (uint32_t i = 0; i < length; ++i) {
        data[i] = address + i < 4096 ? ram[address + i] : 0xff;
        cls[i]  = address + i < 4096 ? DEBUGGER_MEM_RAM : DEBUGGER_MEM_UNMAPPED;
    }
}
void
flushmmucache(void)
{
    ++flushes;
}
void
cpu_dr7_update(void)
{
    ++dr_updates;
}
void
cpu_386_flags_extract(void)
{
    ++flag_extracts;
    cpu_state.flags_op = 0;
}
void
cpu_update_waitstates(void)
{
}
void
cpu_car_update(void)
{
}
int
cpu_has_feature(int feature)
{
    return !!(cpu_features & feature);
}
int
pae_check_load(uint32_t)
{
    return pae_invalid;
}
int
vx0_i8080_state(uint16_t *, uint16_t *, uint8_t *, uint8_t *)
{
    return emulation8080;
}
void
set_ip_808x(uint16_t value)
{
    cpu_state.pc = value;
    ++prefetch_flushes;
}
void
flush_prefetch_808x(void)
{
    ++prefetch_flushes;
}
}

class DebuggerRegisters : public testing::Test {
protected:
    uint8_t memory[4096] { };
    void    SetUp() override
    {
        std::memset(&cpu_state, 0, sizeof(cpu_state));
        is286 = is386 = 1;
        is_nec = cpu_16bitbus = hascache = 0;
        cr2 = cr3 = cr4 = use32 = cpu_cur_status = 0;
        stack32 = cpu_flush_pending = 0;
        cpu_features                = CPU_FEATURE_CR4;
        cpu_CR4_mask                = 0x3ff;
        cpu_CR4_ignore              = 0;
        flushes = dr_updates = flag_extracts = prefetch_flushes = pae_invalid = 0;
        partial = emulation8080 = 0;
        gdt = idt = ldt = tr = { };
        ram                  = memory;
        debugger_attach(1);
        debugger_pause();
    }
    void     TearDown() override { debugger_attach(0); }
    uint32_t read(const char *name)
    {
        uint32_t value = 0;
        EXPECT_EQ(debugger_register(name, &value, nullptr), 1) << name;
        return value;
    }
};

TEST_F(DebuggerRegisters, AliasesPreserveUnaffectedBitsAndRejectOverflow)
{
    ASSERT_TRUE(debugger_register_write("eax", 0x12345678));
    ASSERT_TRUE(debugger_register_write("ah", 0xab));
    EXPECT_EQ(read("eax"), 0x1234ab78u);
    ASSERT_TRUE(debugger_register_write("al", 0xcd));
    EXPECT_EQ(read("ax"), 0xabcdu);
    ASSERT_TRUE(debugger_register_write("ax", 0x9876));
    EXPECT_EQ(read("eax"), 0x12349876u);
    EXPECT_FALSE(debugger_register_write("al", 0x100));
    EXPECT_FALSE(debugger_register_write("ax", 0x10000));
    EXPECT_EQ(read("eax"), 0x12349876u);
}

TEST_F(DebuggerRegisters, WritesRequirePauseAndExistingRegisters)
{
    debugger_continue();
    EXPECT_FALSE(debugger_register_write("eax", 1));
    debugger_pause();
    EXPECT_TRUE(debugger_register_write("eax", 1));
    partial = 1;
    EXPECT_FALSE(debugger_register_write("eax", 2));
    partial       = 0;
    emulation8080 = 1;
    EXPECT_FALSE(debugger_register_write("eax", 2));
    emulation8080 = 0;
    EXPECT_FALSE(debugger_register_write("unknown", 2));
    is386 = 0;
    EXPECT_FALSE(debugger_register_write("eax", 2));
    EXPECT_FALSE(debugger_register_write("fs", 2));
    EXPECT_TRUE(debugger_register_write("ax", 2));
}

TEST_F(DebuggerRegisters, InstructionPointerAndFlagsUpdateExecutionState)
{
    ASSERT_TRUE(debugger_register_write("eip", 0x12345678));
    ASSERT_TRUE(debugger_register_write("ip", 0xabcd));
    EXPECT_EQ(read("eip"), 0x1234abcdu);
    ASSERT_TRUE(debugger_register_write("eflags", 0x20201));
    EXPECT_EQ(read("eflags"), 0x20203u);
    EXPECT_EQ(flag_extracts, 1);
    EXPECT_NE(cpu_cur_status & CPU_STATUS_V86, 0);
    is286 = is386 = 0;
    ASSERT_TRUE(debugger_register_write("ip", 0x4321));
    EXPECT_EQ(read("ip"), 0x4321u);
    EXPECT_EQ(prefetch_flushes, 1);
}

TEST_F(DebuggerRegisters, HiddenCachesUpdateDecodeAndStackSizes)
{
    ASSERT_TRUE(debugger_register_write("cs.flags", 0x40));
    EXPECT_EQ(use32, 0x300u);
    ASSERT_TRUE(debugger_register_write("ss.flags", 0x40));
    EXPECT_EQ(stack32, 1);
    ASSERT_TRUE(debugger_register_write("ds.limit", UINT32_MAX));
    EXPECT_EQ(cpu_cur_status & CPU_STATUS_NOTFLATDS, 0);
    ASSERT_TRUE(debugger_register_write("ds.base", 0x1000));
    EXPECT_NE(cpu_cur_status & CPU_STATUS_NOTFLATDS, 0);
    EXPECT_EQ(cpu_state.seg_ds.limit_high, UINT32_MAX);
    EXPECT_FALSE(debugger_register_write("gdtr.limit", 0x10000));
    EXPECT_FALSE(debugger_register_write("cs.access", 0x100));
}

TEST_F(DebuggerRegisters, SelectorLoadsDescriptorAndRejectsInvalidWithoutMutation)
{
    cr0       = 1;
    gdt.limit = 15;
    // Flat 32-bit code descriptor at GDT:8.
    const uint8_t descriptor[] = { 0xff, 0xff, 0, 0, 0, 0x9b, 0xcf, 0 };
    std::memcpy(memory + 8, descriptor, 8);
    ASSERT_TRUE(debugger_register_write("cs", 8));
    EXPECT_EQ(read("cs"), 8u);
    EXPECT_EQ(read("cs.limit"), UINT32_MAX);
    EXPECT_EQ(use32, 0x300u);
    EXPECT_FALSE(debugger_register_write("ss", 8));  // code cannot be a stack
    EXPECT_FALSE(debugger_register_write("cs", 16)); // outside GDT
    EXPECT_FALSE(debugger_register_write("cs", 0));
    EXPECT_EQ(read("cs"), 8u);
    EXPECT_EQ(cpu_state.abrt, 0);
}

TEST_F(DebuggerRegisters, ControlAndDebugRegistersUpdateDerivedState)
{
    EXPECT_FALSE(debugger_register_write("cr0", 0x80000000));
    EXPECT_EQ(cr0, 0u);
    ASSERT_TRUE(debugger_register_write("cr0", 1));
    EXPECT_NE(cpu_cur_status & CPU_STATUS_PMODE, 0);
    ASSERT_TRUE(debugger_register_write("cr3", 0x1000));
    EXPECT_EQ(read("cr3"), 0x1000u);
    ASSERT_TRUE(debugger_register_write("dr7", 0x401));
    EXPECT_EQ(dr_updates, 1);
    EXPECT_EQ(read("dr7"), 0x401u);
    EXPECT_FALSE(debugger_register_write("cr4", 0x80000000));
    cr0         = 0x80000001;
    cr4         = CR4_PAE;
    pae_invalid = 1;
    EXPECT_FALSE(debugger_register_write("cr3", 0x2000));
    EXPECT_EQ(cr3, 0x1000u);
    EXPECT_GT(flushes, 0);
}
