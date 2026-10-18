#include "386_common.h"
#include "codegen_backend.h"

static inline int
LOAD_SP_WITH_OFFSET(ir_data_t *ir, int offset)
{
    if (stack32) {
        if (offset) {
            uop_ADD_IMM(ir, IREG_eaaddr, IREG_ESP, offset);
            return IREG_eaaddr;
        } else
            return IREG_ESP;
    } else {
        if (offset) {
            uop_ADD_IMM(ir, IREG_eaaddr_W, IREG_SP, offset);
            uop_MOVZX(ir, IREG_eaaddr, IREG_eaaddr_W);
            return IREG_eaaddr;
        } else {
            uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
            return IREG_eaaddr;
        }
    }
}

static inline int
LOAD_SP(ir_data_t *ir)
{
    return LOAD_SP_WITH_OFFSET(ir, 0);
}

static inline void
ADD_SP(ir_data_t *ir, int offset)
{
    if (stack32)
        uop_ADD_IMM(ir, IREG_ESP, IREG_ESP, offset);
    else
        uop_ADD_IMM(ir, IREG_SP, IREG_SP, offset);
}
static inline void
SUB_SP(ir_data_t *ir, int offset)
{
    if (stack32)
        uop_SUB_IMM(ir, IREG_ESP, IREG_ESP, offset);
    else
        uop_SUB_IMM(ir, IREG_SP, IREG_SP, offset);
}

static inline void
fpu_POP(codeblock_t *block, ir_data_t *ir)
{
    /* The legacy FPU stores TAG_EMPTY as zero. Clear the physical tag
       before advancing TOP, including for compare and arithmetic pops. */
    uop_MOV_IMM(ir, IREG_tag(0), 0);
    if (block->flags & CODEBLOCK_STATIC_TOP)
        uop_MOV_IMM(ir, IREG_FPU_TOP, cpu_state.TOP + 1);
    else
        uop_ADD_IMM(ir, IREG_FPU_TOP, IREG_FPU_TOP, 1);
}
static inline void
fpu_POP2(codeblock_t *block, ir_data_t *ir)
{
    uop_MOV_IMM(ir, IREG_tag(0), 0);
    uop_MOV_IMM(ir, IREG_tag(1), 0);
    if (block->flags & CODEBLOCK_STATIC_TOP)
        uop_MOV_IMM(ir, IREG_FPU_TOP, cpu_state.TOP + 2);
    else
        uop_ADD_IMM(ir, IREG_FPU_TOP, IREG_FPU_TOP, 2);
}
static inline void
fpu_PUSH(codeblock_t *block, ir_data_t *ir)
{
    if (block->flags & CODEBLOCK_STATIC_TOP)
        uop_MOV_IMM(ir, IREG_FPU_TOP, cpu_state.TOP - 1);
    else
        uop_SUB_IMM(ir, IREG_FPU_TOP, IREG_FPU_TOP, 1);
}

static inline void
CHECK_SEG_LIMITS(UNUSED(codeblock_t *block), ir_data_t *ir, x86seg *seg, int addr_reg, int end_offset)
{
    if ((seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS)) || (seg == &cpu_state.seg_es && codegen_flat_es && !(cpu_cur_status & CPU_STATUS_NOTFLATES)))
        return;

    /*#SS(0) through SS, #GP(0) through the others*/
    void *rout = (seg == &cpu_state.seg_ss) ? codegen_ss_rout : codegen_gpf_rout;

#if defined __aarch64__ || defined _M_ARM64 || defined __amd64__ || defined _M_X64
    uop_CMP_JNWITHIN(ir, addr_reg, ireg_seg_limit_low(seg), ireg_seg_limit_high(seg), end_offset, rout);
#else
    /*Only the arm64 and x86-64 backends implement UOP_CMP_JNWITHIN*/
    uop_CMP_JB(ir, addr_reg, ireg_seg_limit_low(seg), rout);
    if (end_offset) {
        uop_ADD_IMM(ir, IREG_temp3, addr_reg, end_offset);
        uop_CMP_JNBE(ir, IREG_temp3, ireg_seg_limit_high(seg), rout);
    } else
        uop_CMP_JNBE(ir, addr_reg, ireg_seg_limit_high(seg), rout);
#endif
}

/*An operand read from RAM at run time (CODEBLOCK_NO_IMMEDIATES) goes
  through a host pointer bound when the block is compiled, and only the
  block's first page has its mapping checked when the block is looked up.
  One that is not wholly on that page is compiled in and marked instead, as
  a Jcc displacement is (jcc_dynamic_start): the mark puts its page under
  the second-page checks, and a write to it recompiles the block.*/
static inline int
immediate_off_first_page(codeblock_t *block, uint32_t addr, int len)
{
    if (!(((addr + len - 1) ^ block->pc) & ~0xfff))
        return 0;
    codegen_mark_code_present(block, addr, len);
    return 1;
}

static inline void
LOAD_IMMEDIATE_FROM_RAM_8(codeblock_t *block, ir_data_t *ir, int dest_reg, uint32_t addr)
{
    if (immediate_off_first_page(block, addr, 1))
        uop_MOV_IMM(ir, dest_reg, fastreadb(addr));
    else
        uop_MOVZX_REG_PTR_8(ir, dest_reg, get_ram_ptr(addr));
}

void LOAD_IMMEDIATE_FROM_RAM_16_unaligned(codeblock_t *block, ir_data_t *ir, int dest_reg, uint32_t addr);
static inline void
LOAD_IMMEDIATE_FROM_RAM_16(codeblock_t *block, ir_data_t *ir, int dest_reg, uint32_t addr)
{
    if (immediate_off_first_page(block, addr, 2))
        uop_MOV_IMM(ir, dest_reg, fastreadw(addr));
    else if ((addr & 0xfff) == 0xfff)
        LOAD_IMMEDIATE_FROM_RAM_16_unaligned(block, ir, dest_reg, addr);
    else
        uop_MOVZX_REG_PTR_16(ir, dest_reg, get_ram_ptr(addr));
}

void LOAD_IMMEDIATE_FROM_RAM_32_unaligned(codeblock_t *block, ir_data_t *ir, int dest_reg, uint32_t addr);
static inline void
LOAD_IMMEDIATE_FROM_RAM_32(codeblock_t *block, ir_data_t *ir, int dest_reg, uint32_t addr)
{
    if (immediate_off_first_page(block, addr, 4))
        uop_MOV_IMM(ir, dest_reg, fastreadl(addr));
    else if ((addr & 0xfff) >= 0xffd)
        LOAD_IMMEDIATE_FROM_RAM_32_unaligned(block, ir, dest_reg, addr);
    else
        uop_MOV_REG_PTR(ir, dest_reg, get_ram_ptr(addr));
}

int codegen_flags_rebuild_known(ir_data_t *ir, int carry_only);

int codegen_can_unroll_full(codeblock_t *block, ir_data_t *ir, uint32_t next_pc, uint32_t dest_addr);
static inline int
codegen_can_unroll(codeblock_t *block, ir_data_t *ir, uint32_t next_pc, uint32_t dest_addr)
{
    if (block->flags & CODEBLOCK_BYTE_MASK)
        return 0;

    /*An unrolled loop's handler returns the loop's start, and a handler
      returning 0 means it compiled nothing: the instruction would run a
      second time through the interpreter. So never at offset 0.*/
    if (!dest_addr)
        return 0;

    /*Is dest within block?*/
    if (dest_addr > next_pc)
        return 0;
    if ((cs + dest_addr) < block->pc)
        return 0;

    return codegen_can_unroll_full(block, ir, next_pc, dest_addr);
}
