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

/*do_mmut_r*() and do_mmut_w*() of an element inside a run (rep_run_room()):
  the segment bases are known not to be 0xffffffff, and pg is CR0.PG, which
  no element can change.*/
#define REP_MMUT(pg, lookup, lin, a64, num, write)                                                  \
    if (((pg) || cpu_dr_armed) && (((lookup)[(lin) >> 12] == (uintptr_t) LOOKUP_INV) || ((lin) & ((num) - 1)))) \
    do_mmutranslate(lin, a64, num, write)

/*One element of each REP MOVS/STOS form, written as the loop body of the
  interpreter's REP handlers in x86_ops_rep_dyn.h, for the loops below.
  Inside a run (run non-zero) the segment checks are known to pass and are
  left out. Returns non-zero if the element faulted.*/
#define REP_ELEM_OPS(size, SRC_REG, DEST_REG)                                                  \
    static __attribute__((always_inline)) __inline int                                          \
    rep_movsb_##size(x86seg *seg, int run, int pg)                                              \
    {                                                                                           \
        addr64 = addr64_2 = 0x00000000;                                                         \
        do {                                                                                    \
            uint8_t temp;                                                                       \
                                                                                                \
            if (!run) {                                                                         \
                SEG_CHECK_READ_REP(seg);                                                        \
                CHECK_READ_REP(seg, SRC_REG, SRC_REG);                                          \
            }                                                                                   \
            high_page = 0;                                                                      \
            if (run) {                                                                          \
                REP_MMUT(pg, readlookup2, seg->base + SRC_REG, &addr64, 1, 0);                  \
            } else {                                                                            \
                do_mmut_rb(seg->base, SRC_REG, &addr64);                                        \
            }                                                                                   \
            if (cpu_state.abrt)                                                                 \
                break;                                                                          \
                                                                                                \
            if (!run) {                                                                         \
                SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                         \
                CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG);                         \
            }                                                                                   \
            high_page = 0;                                                                      \
            if (run) {                                                                          \
                REP_MMUT(pg, writelookup2, es + DEST_REG, &addr64_2, 1, 1);                     \
            } else {                                                                            \
                do_mmut_wb(es, DEST_REG, &addr64_2);                                            \
            }                                                                                   \
            if (cpu_state.abrt)                                                                 \
                break;                                                                          \
                                                                                                \
            temp = readmemb_n(seg->base, SRC_REG, addr64);                                      \
            writememb_n(es, DEST_REG, addr64_2, temp);                                          \
        } while (0);                                                                            \
        return cpu_state.abrt;                                                                  \
    }                                                                                           \
    static __attribute__((always_inline)) __inline int                                          \
    rep_movsw_##size(x86seg *seg, int run, int pg)                                              \
    {                                                                                           \
        addr64a[0] = addr64a[1] = 0x00000000;                                                   \
        addr64a_2[0] = addr64a_2[1] = 0x00000000;                                               \
        do {                                                                                    \
            uint16_t temp;                                                                      \
                                                                                                \
            if (!run) {                                                                         \
                SEG_CHECK_READ_REP(seg);                                                        \
                CHECK_READ_REP(seg, SRC_REG, SRC_REG + 1UL);                                    \
            }                                                                                   \
            high_page = 0;                                                                      \
            if (run) {                                                                          \
                REP_MMUT(pg, readlookup2, seg->base + SRC_REG, addr64a, 2, 0);                  \
            } else {                                                                            \
                do_mmut_rw(seg->base, SRC_REG, addr64a);                                        \
            }                                                                                   \
            if (cpu_state.abrt)                                                                 \
                break;                                                                          \
                                                                                                \
            if (!run) {                                                                         \
                SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                         \
                CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG + 1UL);                   \
            }                                                                                   \
            high_page = 0;                                                                      \
            if (run) {                                                                          \
                REP_MMUT(pg, writelookup2, es + DEST_REG, addr64a_2, 2, 1);                     \
            } else {                                                                            \
                do_mmut_ww(es, DEST_REG, addr64a_2);                                            \
            }                                                                                   \
            if (cpu_state.abrt)                                                                 \
                break;                                                                          \
                                                                                                \
            temp = readmemw_n(seg->base, SRC_REG, addr64a);                                     \
            writememw_n(es, DEST_REG, addr64a_2, temp);                                         \
        } while (0);                                                                            \
        return cpu_state.abrt;                                                                  \
    }                                                                                           \
    static __attribute__((always_inline)) __inline int                                          \
    rep_movsl_##size(x86seg *seg, int run, int pg)                                              \
    {                                                                                           \
        addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;                        \
        addr64a_2[0] = addr64a_2[1] = addr64a_2[2] = addr64a_2[3] = 0x00000000;                \
        do {                                                                                    \
            uint32_t temp;                                                                      \
                                                                                                \
            if (!run) {                                                                         \
                SEG_CHECK_READ_REP(seg);                                                        \
                CHECK_READ_REP(seg, SRC_REG, SRC_REG + 3UL);                                    \
            }                                                                                   \
            high_page = 0;                                                                      \
            if (run) {                                                                          \
                REP_MMUT(pg, readlookup2, seg->base + SRC_REG, addr64a, 4, 0);                  \
            } else {                                                                            \
                do_mmut_rl(seg->base, SRC_REG, addr64a);                                        \
            }                                                                                   \
            if (cpu_state.abrt)                                                                 \
                break;                                                                          \
                                                                                                \
            if (!run) {                                                                         \
                SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                         \
                CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG + 3UL);                   \
            }                                                                                   \
            high_page = 0;                                                                      \
            if (run) {                                                                          \
                REP_MMUT(pg, writelookup2, es + DEST_REG, addr64a_2, 4, 1);                     \
            } else {                                                                            \
                do_mmut_wl(es, DEST_REG, addr64a_2);                                            \
            }                                                                                   \
            if (cpu_state.abrt)                                                                 \
                break;                                                                          \
                                                                                                \
            temp = readmeml_n(seg->base, SRC_REG, addr64a);                                     \
            writememl_n(es, DEST_REG, addr64a_2, temp);                                         \
        } while (0);                                                                            \
        return cpu_state.abrt;                                                                  \
    }                                                                                           \
                                                                                                \
    static __attribute__((always_inline)) __inline int                                          \
    rep_stosb_##size(UNUSED(x86seg *seg), int run, UNUSED(int pg))                              \
    {                                                                                           \
        do {                                                                                    \
            if (!run) {                                                                         \
                SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                         \
                CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG);                         \
            }                                                                                   \
            writememb(es, DEST_REG, AL);                                                        \
        } while (0);                                                                            \
        return cpu_state.abrt;                                                                  \
    }                                                                                           \
    static __attribute__((always_inline)) __inline int                                          \
    rep_stosw_##size(UNUSED(x86seg *seg), int run, UNUSED(int pg))                              \
    {                                                                                           \
        do {                                                                                    \
            if (!run) {                                                                         \
                SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                         \
                CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG + 1UL);                   \
            }                                                                                   \
            writememw(es, DEST_REG, AX);                                                        \
        } while (0);                                                                            \
        return cpu_state.abrt;                                                                  \
    }                                                                                           \
    static __attribute__((always_inline)) __inline int                                          \
    rep_stosl_##size(UNUSED(x86seg *seg), int run, UNUSED(int pg))                              \
    {                                                                                           \
        do {                                                                                    \
            if (!run) {                                                                         \
                SEG_CHECK_WRITE_REP(&cpu_state.seg_es);                                         \
                CHECK_WRITE_REP(&cpu_state.seg_es, DEST_REG, DEST_REG + 3UL);                   \
            }                                                                                   \
            writememl(es, DEST_REG, EAX);                                                       \
        } while (0);                                                                            \
        return cpu_state.abrt;                                                                  \
    }

REP_ELEM_OPS(a16, SI, DI)
REP_ELEM_OPS(a32, ESI, EDI)

/*Whether the routine would copy the element at the given offsets itself:
  aligned, with direct lookups for its pages*/
static __inline int
rep_direct(x86seg *seg, int is_stos, int shift, uint32_t src, uint32_t dst)
{
    uint32_t dst_lin = es + dst;

    if ((dst_lin & ((1 << shift) - 1)) || (writelookup2[dst_lin >> 12] == (uintptr_t) LOOKUP_INV))
        return 0;
    if (!is_stos) {
        uint32_t src_lin = seg->base + src;

        if ((src_lin & ((1 << shift) - 1)) || (readlookup2[src_lin >> 12] == (uintptr_t) LOOKUP_INV))
            return 0;
    }
    return 1;
}

/*How many elements, from the one at offset off of segment s and stepping
  down if down is set, pass the limit checks of CHECK_READ_REP and
  CHECK_WRITE_REP and neither cross a page nor wrap the offset past mask; 0
  if the first one does not. The sums are 64-bit, so the last byte of an
  element at the top of the offset space keeps its carry.*/
static __inline uint32_t
rep_room(const x86seg *s, uint32_t off, uint32_t mask, int shift, int down)
{
    const uint32_t size = 1 << shift;
    const uint64_t last = (uint64_t) off + size - 1;
    const uint32_t pos  = (s->base + off) & 0xfff;
    uint64_t       lim;
    uint64_t       page;

    if ((last > mask) || (off < s->limit_low) || (last > s->limit_high) || ((pos + size - 1) > 0xfff))
        return 0;
    if (down) {
        lim  = ((uint64_t) off - s->limit_low) >> shift;
        page = pos >> shift;
    } else {
        lim  = (((s->limit_high < mask) ? (uint64_t) s->limit_high : (uint64_t) mask) - last) >> shift;
        page = (0xfff - (pos + size - 1)) >> shift;
    }
    return (uint32_t) (((lim < page) ? lim : page) + 1);
}

/*The number of elements from the current ones that run with the checks of
  the element left out: those that stay inside both segment limits and one
  page of each side, without wrapping an offset, up to the count; 0 when a
  check that does not depend on the offset fails, a debug register is armed
  or the count is 0, for the element to run its checks itself. No element
  changes a segment, CR0 or EFLAGS.VM, so the checks hold for the run.*/
static __inline uint32_t
rep_run_room(x86seg *seg, int is_stos, int shift, uint32_t mask, uint32_t cnt, uint32_t src, uint32_t dst)
{
    const int    down = !!(cpu_state.flags & D_FLAG);
    const x86seg *s_es = &cpu_state.seg_es;
    const int    np    = (msw & 1) && !(cpu_state.eflags & VM_FLAG);
    uint32_t     n     = cnt;
    uint32_t     room;

    if (!n || cpu_dr_armed)
        return 0;
    if ((s_es->base == 0xffffffff) || SEG_NOT_WRITABLE(s_es) || (np && !(s_es->access & 0x80)))
        return 0;
    room = rep_room(s_es, dst, mask, shift, down);
    if (room < n)
        n = room;
    if (!is_stos) {
        if ((seg->base == 0xffffffff) || (np && !(seg->access & 0x80)))
            return 0;
        room = rep_room(seg, src, mask, shift, down);
        if (room < n)
            n = room;
    }
    return n;
}

/*Elements the compiled routine does not copy itself, each one as a pass of
  the interpreter's loop in x86_ops_rep_dyn.h: the element, then the steps of
  the registers, the count and the cycles. Carries on while the count is not
  zero, the cycle count is not below cycles_end, no SMI is pending and the
  next element is also one the routine would not copy, so that a string to a
  page without a direct lookup (video memory, MMIO) does not go back to the
  routine for each element. The elements go in runs (rep_run_room()) whose
  checks are made once; an element no run takes makes its own. Returns
  non-zero if an element faulted, with the registers left at that element.*/
#define REP_RUN_OP(name, size, CNT_REG, SRC_REG, DEST_REG, is_stos, shift, mask)                   \
    static int rep_run_##name##_##size(x86seg *seg, int32_t cycles_end)                            \
    {                                                                                              \
        const int     elem_cycles = (is_stos) ? (is486 ? 4 : 5) : (is486 ? 3 : 4);                 \
        const int32_t step        = (cpu_state.flags & D_FLAG) ? -(1 << (shift)) : (1 << (shift)); \
        const int     pg          = cr0 >> 31;                                                     \
                                                                                                   \
        for (;;) {                                                                                 \
            const uint32_t run  = rep_run_room(seg, is_stos, shift, mask, CNT_REG, SRC_REG, DEST_REG); \
            uint32_t       left = run ? run : 1;                                                   \
                                                                                                   \
            do {                                                                                   \
                if (rep_##name##_##size(seg, run != 0, pg))                                        \
                    return 1;                                                                      \
                if (!(is_stos))                                                                    \
                    SRC_REG += step;                                                               \
                DEST_REG += step;                                                                  \
                CNT_REG--;                                                                         \
                cycles -= elem_cycles;                                                             \
                if (!CNT_REG || (cycles < cycles_end) || smi_line                                  \
                    || rep_direct(seg, is_stos, shift, SRC_REG, DEST_REG))                         \
                    return 0;                                                                      \
            } while (--left);                                                                      \
        }                                                                                          \
    }

#define REP_RUN_OPS(size, CNT_REG, SRC_REG, DEST_REG, mask)         \
    REP_RUN_OP(movsb, size, CNT_REG, SRC_REG, DEST_REG, 0, 0, mask) \
    REP_RUN_OP(movsw, size, CNT_REG, SRC_REG, DEST_REG, 0, 1, mask) \
    REP_RUN_OP(movsl, size, CNT_REG, SRC_REG, DEST_REG, 0, 2, mask) \
    REP_RUN_OP(stosb, size, CNT_REG, SRC_REG, DEST_REG, 1, 0, mask) \
    REP_RUN_OP(stosw, size, CNT_REG, SRC_REG, DEST_REG, 1, 1, mask) \
    REP_RUN_OP(stosl, size, CNT_REG, SRC_REG, DEST_REG, 1, 2, mask)

REP_RUN_OPS(a16, CX, SI, DI, 0xffff)
REP_RUN_OPS(a32, ECX, ESI, EDI, 0xffffffff)

int (*const codegen_rep_run[2][3][2])(x86seg *seg, int32_t cycles_end) = {
    {
        { rep_run_movsb_a16, rep_run_movsb_a32 },
        { rep_run_movsw_a16, rep_run_movsw_a32 },
        { rep_run_movsl_a16, rep_run_movsl_a32 },
    },
    {
        { rep_run_stosb_a16, rep_run_stosb_a32 },
        { rep_run_stosw_a16, rep_run_stosw_a32 },
        { rep_run_stosl_a16, rep_run_stosl_a32 },
    },
};

#if defined __aarch64__ || defined _M_ARM64 || defined __amd64__ || defined _M_X64
/*The routine runs the whole instruction and leaves the guest registers and
  the cycle count in cpu_state, so they are reloaded after it. The cycle
  figures are the interpreter's, and do not change while compiled blocks
  exist. cpu_state.pc is the next instruction for an SMI raised by the last
  element, which leaves the block there.*/
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
    uop_MOV_IMM(ir, IREG_pc, op_pc);
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
