// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>

extern "C" {
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>
#include "../../src/codegen/codegen.h"
#include "../../src/codegen/codegen_ir.h"
#include "../../src/codegen/codegen_ops_shift.h"

cpu_state_t cpu_state;
uint16_t cpu_cur_status;
int cpu_block_end;
int codegen_flags_changed;
int codegen_flat_ds, codegen_flat_es, codegen_flat_ss;
x86seg *op_ea_seg;
int op_ssegs;
void *codegen_exit_rout, *codegen_gpf_rout, *codegen_ss_rout;
uint32_t pccache;
uint8_t *pccache2;
uint8_t znptable8[256];
uint8_t reg_last_version[IREG_COUNT];
uint64_t dirty_ir_regs[2];
uint64_t ireg_permanent_mask[2];
reg_version_t reg_version[IREG_COUNT][256];
uint16_t reg_dead_list;
ir_reg_t invalid_ir_reg = {IREG_INVALID, 0};
int max_version_refcount;

void fatal(const char *, ...) { std::abort(); }
int reg_is_native_size(ir_reg_t reg) { return IREG_GET_SIZE(reg.reg) == IREG_SIZE_L; }
int codegen_flags_rebuild_known(ir_data_t *, int) { return 0; }
void codegen_mark_code_present_multibyte(codeblock_t *, uint32_t, int) { }
x86seg *codegen_generate_ea(ir_data_t *, x86seg *, uint32_t, int, uint32_t *, uint32_t, int)
{
    std::abort();
}
void codegen_check_seg_write(codeblock_t *, ir_data_t *, x86seg *) { std::abort(); }
uint8_t *getpccache(uint32_t) { std::abort(); }
uint8_t readmembl(uint32_t) { std::abort(); }
}

class CodegenShiftCl : public ::testing::Test {
protected:
    static ir_data_t ir;
    codeblock_t block = {};

    void SetUp() override
    {
        block = {};
        std::memset(&cpu_state, 0, sizeof(cpu_state));
        std::memset(&ir, 0, sizeof(ir));
        std::memset(reg_last_version, 0, sizeof(reg_last_version));
        std::memset(reg_version, 0, sizeof(reg_version));
        std::memset(dirty_ir_regs, 0, sizeof(dirty_ir_regs));
        std::memset(ireg_permanent_mask, 0, sizeof(ireg_permanent_mask));
        reg_dead_list = 0xffff;
        cpu_block_end = max_version_refcount = codegen_flags_changed = 0;
    }

    void CheckTranslation(int size, unsigned operation, unsigned destination,
                          unsigned count, unsigned prior_instructions)
    {
        cpu_state.regs[1].b.l = count;
        block.ins = prior_instructions;
        auto translate = size == 8 ? ropD2 : size == 16 ? ropD3_w : ropD3_l;
        ASSERT_EQ(translate(&block, &ir, 0, 0xc0 | (operation << 3) | destination,
                            0x300, 0x101), 0x102u);
        ASSERT_GT(ir.wr_pos, 2);
        EXPECT_EQ(ir.uops[0].type & UOP_MASK, UOP_AND_IMM & UOP_MASK);
        EXPECT_EQ(ir.uops[0].src_reg_a.reg, IREG_ECX);
        EXPECT_EQ(ir.uops[0].imm_data, 31u);
        const auto &branch = ir.uops[1];
        EXPECT_EQ(branch.type & UOP_MASK, UOP_CMP_IMM_JZ_DEST & UOP_MASK);
        EXPECT_EQ(branch.imm_data, 0u);
        EXPECT_EQ(branch.jump_dest_uop, ir.wr_pos);
        EXPECT_EQ(ir.uops[ir.wr_pos - 1].type & UOP_MASK, UOP_NOP_BARRIER & UOP_MASK);
        EXPECT_EQ(codegen_flags_changed, 0);
        EXPECT_NE(block.page_mask, 0u);
        for (int i = 0; i < ir.wr_pos; i++) {
            EXPECT_NE(ir.uops[i].type & UOP_MASK, UOP_CALL_INSTRUCTION_FUNC & UOP_MASK);
            EXPECT_NE(ir.uops[i].type & UOP_MASK, UOP_CMP_IMM_JZ & UOP_MASK);
        }
    }
};

ir_data_t CodegenShiftCl::ir;

TEST_F(CodegenShiftCl, ZeroCountAtBlockEntryDoesNotRequireInterpreter)
{
    CheckTranslation(32, 5, 2, 0, 0);
}

TEST_F(CodegenShiftCl, TranslatesEveryRegisterAndCountIncludingBlockEntry)
{
    for (int size : {8, 16, 32})
        for (unsigned operation : {0, 1, 4, 5, 6, 7})
            for (unsigned destination = 0; destination < 8; destination++)
                for (unsigned count : {0, 1, 8, 16, 31, 32, 64, 255})
                    for (unsigned prior : {0, 1}) {
                        SetUp();
                        SCOPED_TRACE(::testing::Message() << size << ':' << operation << ':'
                                     << destination << ':' << count << ':' << prior);
                        CheckTranslation(size, operation, destination, count, prior);
                    }
}

TEST_F(CodegenShiftCl, MemoryOperandsKeepTheirExistingFallbackGuards)
{
    for (auto translate : {ropD2, ropD3_w, ropD3_l}) {
        cpu_state.regs[1].b.l = 0;
        block.ins = 1;
        EXPECT_EQ(translate(&block, &ir, 0, 0x20, 0x300, 0x101), 0u);
        cpu_state.regs[1].b.l = 1;
        block.ins = 0;
        EXPECT_EQ(translate(&block, &ir, 0, 0x20, 0x300, 0x101), 0u);
        EXPECT_EQ(ir.wr_pos, 0);
    }
}

TEST_F(CodegenShiftCl, CarryRotatesKeepTheirExistingFallback)
{
    for (auto translate : {ropD2, ropD3_w, ropD3_l})
        for (unsigned operation : {2, 3}) {
            EXPECT_EQ(translate(&block, &ir, 0, 0xc0 | (operation << 3), 0x300, 0x101), 0u);
            EXPECT_EQ(ir.wr_pos, 0);
        }
}
