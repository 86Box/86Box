#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "386_common.h"
#include "codegen.h"
#include "codegen_accumulate.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_helpers.h"
#include "codegen_ops_rep.h"

/*One element of each REP MOVS/STOS form, written as the loop body of the
  interpreter's REP handlers in x86_ops_rep_dyn.h. The compiled REP routines
  call these for any element they do not copy directly, and step the
  registers and charge the cycles themselves. Returns non-zero if the element
  faulted.*/
#define REP_ELEM_OPS(size, SRC_REG, DEST_REG)                                                  \
    static int rep_movsb_##size(x86seg *seg)                                                    \
    {                                                                                           \
        addr64 = addr64_2 = 0x00000000;                                                         \
        do {                                                                                    \
            uint8_t temp;                                                                       \
                                                                                                \
            SEG_CHECK_READ_REP(seg);                                                            \
            CHECK_READ_REP(seg, SRC_REG, SRC_REG);                                              \
            high_page = 0;                                                                      \
            do_mmut_rb(seg->base, SRC_REG, &addr64);                                            \
            if (cpu_state.abrt)                                                                 \
                break;                                                                          \
                                                                                                \
            SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                             \
            CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG);                             \
            high_page = 0;                                                                      \
            do_mmut_wb(es, DEST_REG, &addr64_2);                                                \
            if (cpu_state.abrt)                                                                 \
                break;                                                                          \
                                                                                                \
            temp = readmemb_n(seg->base, SRC_REG, addr64);                                      \
            writememb_n(es, DEST_REG, addr64_2, temp);                                          \
        } while (0);                                                                            \
        return cpu_state.abrt;                                                                  \
    }                                                                                           \
    static int rep_movsw_##size(x86seg *seg)                                                    \
    {                                                                                           \
        addr64a[0] = addr64a[1] = 0x00000000;                                                   \
        addr64a_2[0] = addr64a_2[1] = 0x00000000;                                               \
        do {                                                                                    \
            uint16_t temp;                                                                      \
                                                                                                \
            SEG_CHECK_READ_REP(seg);                                                            \
            CHECK_READ_REP(seg, SRC_REG, SRC_REG + 1UL);                                        \
            high_page = 0;                                                                      \
            do_mmut_rw(seg->base, SRC_REG, addr64a);                                            \
            if (cpu_state.abrt)                                                                 \
                break;                                                                          \
                                                                                                \
            SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                             \
            CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG + 1UL);                       \
            high_page = 0;                                                                      \
            do_mmut_ww(es, DEST_REG, addr64a_2);                                                \
            if (cpu_state.abrt)                                                                 \
                break;                                                                          \
                                                                                                \
            temp = readmemw_n(seg->base, SRC_REG, addr64a);                                     \
            writememw_n(es, DEST_REG, addr64a_2, temp);                                         \
        } while (0);                                                                            \
        return cpu_state.abrt;                                                                  \
    }                                                                                           \
    static int rep_movsl_##size(x86seg *seg)                                                    \
    {                                                                                           \
        addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;                        \
        addr64a_2[0] = addr64a_2[1] = addr64a_2[2] = addr64a_2[3] = 0x00000000;                \
        do {                                                                                    \
            uint32_t temp;                                                                      \
                                                                                                \
            SEG_CHECK_READ_REP(seg);                                                            \
            CHECK_READ_REP(seg, SRC_REG, SRC_REG + 3UL);                                        \
            high_page = 0;                                                                      \
            do_mmut_rl(seg->base, SRC_REG, addr64a);                                            \
            if (cpu_state.abrt)                                                                 \
                break;                                                                          \
                                                                                                \
            SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                             \
            CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG + 3UL);                       \
            high_page = 0;                                                                      \
            do_mmut_wl(es, DEST_REG, addr64a_2);                                                \
            if (cpu_state.abrt)                                                                 \
                break;                                                                          \
                                                                                                \
            temp = readmeml_n(seg->base, SRC_REG, addr64a);                                     \
            writememl_n(es, DEST_REG, addr64a_2, temp);                                         \
        } while (0);                                                                            \
        return cpu_state.abrt;                                                                  \
    }                                                                                           \
                                                                                                \
    static int rep_stosb_##size(UNUSED(x86seg *seg))                                            \
    {                                                                                           \
        do {                                                                                    \
            SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                             \
            CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG);                             \
            writememb(es, DEST_REG, AL);                                                        \
        } while (0);                                                                            \
        return cpu_state.abrt;                                                                  \
    }                                                                                           \
    static int rep_stosw_##size(UNUSED(x86seg *seg))                                            \
    {                                                                                           \
        do {                                                                                    \
            SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                             \
            CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG + 1UL);                       \
            writememw(es, DEST_REG, AX);                                                        \
        } while (0);                                                                            \
        return cpu_state.abrt;                                                                  \
    }                                                                                           \
    static int rep_stosl_##size(UNUSED(x86seg *seg))                                            \
    {                                                                                           \
        do {                                                                                    \
            SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                             \
            CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG + 3UL);                       \
            writememl(es, DEST_REG, EAX);                                                       \
        } while (0);                                                                            \
        return cpu_state.abrt;                                                                  \
    }

REP_ELEM_OPS(a16, SI, DI)
REP_ELEM_OPS(a32, ESI, EDI)

int (*const codegen_rep_elem[2][3][2])(x86seg *seg) = {
    {
        { rep_movsb_a16, rep_movsb_a32 },
        { rep_movsw_a16, rep_movsw_a32 },
        { rep_movsl_a16, rep_movsl_a32 },
    },
    {
        { rep_stosb_a16, rep_stosb_a32 },
        { rep_stosw_a16, rep_stosw_a32 },
        { rep_stosl_a16, rep_stosl_a32 },
    },
};

#if defined __aarch64__ || defined _M_ARM64 || defined __amd64__ || defined _M_X64
/*The routine runs the whole instruction and leaves the guest registers and
  the cycle count in cpu_state, so they are reloaded after it. The cycle
  figures are the interpreter's, and do not change while compiled blocks
  exist.*/
static uint32_t
rep_string(ir_data_t *ir, int is_stos, int shift, uint32_t op_32, uint32_t op_pc)
{
    int      a32         = (op_32 & 0x200) ? 1 : 0;
    uint32_t seg_offset  = 0;
    uint32_t cycle_limit = (is386 && cpu_use_dynarec) ? 1000 : 100;
    uint32_t elem_cycles = is_stos ? (is486 ? 4 : 5) : (is486 ? 3 : 4);

    if (!is_stos) {
        seg_offset = (uint32_t) ((uintptr_t) op_ea_seg - (uintptr_t) &cpu_state);
        if (seg_offset > 0xfff)
            return 0;
    }

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_accumulate_flush(ir);
    uop_CALL_REP_STRING(ir, codegen_rep_rout[is_stos][shift][a32], seg_offset | (cycle_limit << 12) | (elem_cycles << 24));
    uop_MOV_REG_PTR(ir, IREG_ECX, &ECX);
    if (!is_stos)
        uop_MOV_REG_PTR(ir, IREG_ESI, &ESI);
    uop_MOV_REG_PTR(ir, IREG_EDI, &EDI);
    uop_MOV_REG_PTR(ir, IREG_cycles, &cpu_state._cycles);

    return op_pc;
}
#else
static uint32_t
rep_string(UNUSED(ir_data_t *ir), UNUSED(int is_stos), UNUSED(int shift), UNUSED(uint32_t op_32), UNUSED(uint32_t op_pc))
{
    return 0;
}
#endif

uint32_t
ropREP_MOVSB(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return rep_string(ir, 0, 0, op_32, op_pc);
}
uint32_t
ropREP_MOVSW(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return rep_string(ir, 0, 1, op_32, op_pc);
}
uint32_t
ropREP_MOVSL(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return rep_string(ir, 0, 2, op_32, op_pc);
}
uint32_t
ropREP_STOSB(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return rep_string(ir, 1, 0, op_32, op_pc);
}
uint32_t
ropREP_STOSW(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return rep_string(ir, 1, 1, op_32, op_pc);
}
uint32_t
ropREP_STOSL(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return rep_string(ir, 1, 2, op_32, op_pc);
}
