#if defined __aarch64__ || defined _M_ARM64

#    include <inttypes.h>
#    include <stddef.h>
#    include <stdlib.h>
#    include <stdint.h>
#    include <86box/86box.h>
#    include "cpu.h"
#    include <86box/mem.h>
#    include <86box/plat.h>

#    include "codegen.h"
#    include "codegen_allocator.h"
#    include "codegen_backend.h"
#    include "codegen_backend_arm64_defs.h"
#    include "codegen_backend_arm64_ops.h"
#    include "codegen_reg.h"
#    include "x86.h"
#    include "x86seg_common.h"
#    include "x86seg.h"
#    include "x87_sf.h"
#    include "x87.h"

#    if defined(__linux__) || defined(__APPLE__)
#        include <sys/mman.h>
#        include <unistd.h>
#    endif
#    if defined WIN32 || defined _WIN32 || defined _WIN32
#        include <windows.h>
#    endif
#    include <string.h>

void *codegen_mem_load_byte;
void *codegen_mem_load_word;
void *codegen_mem_load_long;
void *codegen_mem_load_quad;
void *codegen_mem_load_single;
void *codegen_mem_load_double;

void *codegen_mem_store_byte;
void *codegen_mem_store_word;
void *codegen_mem_store_long;
void *codegen_mem_store_quad;
void *codegen_mem_store_single;
void *codegen_mem_store_double;

void *codegen_fp_round;
void *codegen_fp_round_quad;

void *codegen_gpf_rout;
void *codegen_ss_rout;
void *codegen_exit_rout;

host_reg_def_t codegen_host_reg_list[CODEGEN_HOST_REGS] = {
    { REG_X19, 0},
    { REG_X20, 0},
    { REG_X21, 0},
    { REG_X22, 0},
    { REG_X23, 0},
    { REG_X24, 0},
    { REG_X25, 0},
    { REG_X26, 0},
    { REG_X27, 0},
    { REG_X28, 0}
};

host_reg_def_t codegen_host_fp_reg_list[CODEGEN_HOST_FP_REGS] = {
    { REG_V8,  0},
    { REG_V9,  0},
    { REG_V10, 0},
    { REG_V11, 0},
    { REG_V12, 0},
    { REG_V13, 0},
    { REG_V14, 0},
    { REG_V15, 0}
};

static void
build_load_routine(codeblock_t *block, int size, int is_float)
{
    uint32_t *branch_offset;
    uint32_t *misaligned_offset;

    /*In - W0 = address
      Out - W0 = data, W1 = abrt*/
    /*MOV W1, W0, LSR #12
      MOV X2, #readlookup2
      LDR X1, [X2, X1, LSL #3]
      CMP X1, #-1
      BEQ +
      LDRB W0, [X1, X0]
      MOV W1, #0
      RET
    * STP X29, X30, [SP, #-16]
      BL readmembl
      LDRB R1, cpu_state.abrt
      LDP X29, X30, [SP, #-16]
      RET
    */
    codegen_alloc(block, 80);
    host_arm64_MOV_REG_LSR(block, REG_W1, REG_W0, 12);
    host_arm64_MOVX_IMM(block, REG_X2, (uint64_t) readlookup2);
    host_arm64_LDRX_REG_LSL3(block, REG_X1, REG_X2, REG_X1);
    if (size != 1) {
        host_arm64_TST_IMM(block, REG_W0, size - 1);
        misaligned_offset = host_arm64_BNE_(block);
    }
    host_arm64_CMPX_IMM(block, REG_X1, -1);
    branch_offset = host_arm64_BEQ_(block);
    if (size == 1 && !is_float)
        host_arm64_LDRB_REG(block, REG_W0, REG_W1, REG_W0);
    else if (size == 2 && !is_float)
        host_arm64_LDRH_REG(block, REG_W0, REG_W1, REG_W0);
    else if (size == 4 && !is_float)
        host_arm64_LDR_REG(block, REG_W0, REG_W1, REG_W0);
    else if (size == 4 && is_float)
        host_arm64_LDR_REG_F32(block, REG_V_TEMP, REG_W1, REG_W0);
    else if (size == 8)
        host_arm64_LDR_REG_F64(block, REG_V_TEMP, REG_W1, REG_W0);
    host_arm64_MOVZ_IMM(block, REG_W1, 0);
    host_arm64_RET(block, REG_X30);

    host_arm64_branch_set_offset(branch_offset, &block_write_data[block_pos]);
    if (size != 1)
        host_arm64_branch_set_offset(misaligned_offset, &block_write_data[block_pos]);
    host_arm64_STP_PREIDX_X(block, REG_X29, REG_X30, REG_XSP, -16);
    if (size == 1)
        host_arm64_call(block, (void *) readmembl);
    else if (size == 2)
        host_arm64_call(block, (void *) readmemwl);
    else if (size == 4)
        host_arm64_call(block, (void *) readmemll);
    else if (size == 8)
        host_arm64_call(block, (void *) readmemql);
    else
        fatal("build_load_routine - unknown size %i\n", size);
    codegen_direct_read_8(block, REG_W1, &cpu_state.abrt);
    if (size == 4 && is_float)
        host_arm64_FMOV_S_W(block, REG_V_TEMP, REG_W0);
    else if (size == 8)
        host_arm64_FMOV_D_Q(block, REG_V_TEMP, REG_X0);
    host_arm64_LDP_POSTIDX_X(block, REG_X29, REG_X30, REG_XSP, 16);
    host_arm64_RET(block, REG_X30);
}

static void
build_store_routine(codeblock_t *block, int size, int is_float)
{
    uint32_t *branch_offset;
    uint32_t *misaligned_offset;

    /*In - R0 = address, R1 = data
      Out - R1 = abrt*/
    /*MOV W2, W0, LSR #12
      MOV X3, #writelookup2
      LDR X2, [X3, X2, LSL #3]
      CMP X2, #-1
      BEQ +
      STRB W1, [X2, X0]
      MOV W1, #0
      RET
    * STP X29, X30, [SP, #-16]
      BL writemembl
      LDRB R1, cpu_state.abrt
      LDP X29, X30, [SP, #-16]
      RET
    */
    codegen_alloc(block, 80);
    host_arm64_MOV_REG_LSR(block, REG_W2, REG_W0, 12);
    host_arm64_MOVX_IMM(block, REG_X3, (uint64_t) writelookup2);
    host_arm64_LDRX_REG_LSL3(block, REG_X2, REG_X3, REG_X2);
    if (size != 1) {
        host_arm64_TST_IMM(block, REG_W0, size - 1);
        misaligned_offset = host_arm64_BNE_(block);
    }
    host_arm64_CMPX_IMM(block, REG_X2, -1);
    branch_offset = host_arm64_BEQ_(block);
    if (size == 1 && !is_float)
        host_arm64_STRB_REG(block, REG_X1, REG_X2, REG_X0);
    else if (size == 2 && !is_float)
        host_arm64_STRH_REG(block, REG_X1, REG_X2, REG_X0);
    else if (size == 4 && !is_float)
        host_arm64_STR_REG(block, REG_X1, REG_X2, REG_X0);
    else if (size == 4 && is_float)
        host_arm64_STR_REG_F32(block, REG_V_TEMP, REG_X2, REG_X0);
    else if (size == 8)
        host_arm64_STR_REG_F64(block, REG_V_TEMP, REG_X2, REG_X0);
    host_arm64_MOVZ_IMM(block, REG_X1, 0);
    host_arm64_RET(block, REG_X30);

    host_arm64_branch_set_offset(branch_offset, &block_write_data[block_pos]);
    if (size != 1)
        host_arm64_branch_set_offset(misaligned_offset, &block_write_data[block_pos]);
    host_arm64_STP_PREIDX_X(block, REG_X29, REG_X30, REG_XSP, -16);
    if (size == 4 && is_float)
        host_arm64_FMOV_W_S(block, REG_W1, REG_V_TEMP);
    else if (size == 8)
        host_arm64_FMOV_Q_D(block, REG_X1, REG_V_TEMP);
    if (size == 1)
        host_arm64_call(block, (void *) writemembl);
    else if (size == 2)
        host_arm64_call(block, (void *) writememwl);
    else if (size == 4)
        host_arm64_call(block, (void *) writememll);
    else if (size == 8)
        host_arm64_call(block, (void *) writememql);
    else
        fatal("build_store_routine - unknown size %i\n", size);
    codegen_direct_read_8(block, REG_W1, &cpu_state.abrt);
    host_arm64_LDP_POSTIDX_X(block, REG_X29, REG_X30, REG_XSP, 16);
    host_arm64_RET(block, REG_X30);
}

static void
build_loadstore_routines(codeblock_t *block)
{
    codegen_mem_load_byte = &block_write_data[block_pos];
    build_load_routine(block, 1, 0);
    codegen_mem_load_word = &block_write_data[block_pos];
    build_load_routine(block, 2, 0);
    codegen_mem_load_long = &block_write_data[block_pos];
    build_load_routine(block, 4, 0);
    codegen_mem_load_quad = &block_write_data[block_pos];
    build_load_routine(block, 8, 0);
    codegen_mem_load_single = &block_write_data[block_pos];
    build_load_routine(block, 4, 1);
    codegen_mem_load_double = &block_write_data[block_pos];
    build_load_routine(block, 8, 1);

    codegen_mem_store_byte = &block_write_data[block_pos];
    build_store_routine(block, 1, 0);
    codegen_mem_store_word = &block_write_data[block_pos];
    build_store_routine(block, 2, 0);
    codegen_mem_store_long = &block_write_data[block_pos];
    build_store_routine(block, 4, 0);
    codegen_mem_store_quad = &block_write_data[block_pos];
    build_store_routine(block, 8, 0);
    codegen_mem_store_single = &block_write_data[block_pos];
    build_store_routine(block, 4, 1);
    codegen_mem_store_double = &block_write_data[block_pos];
    build_store_routine(block, 8, 1);
}

static void
build_fp_round_routine(codeblock_t *block, int is_quad)
{
    uint64_t *jump_table;

    codegen_alloc(block, 80);
    host_arm64_LDR_IMM_W(block, REG_TEMP, REG_CPUSTATE, (uintptr_t) &cpu_state.new_fp_control - (uintptr_t) &cpu_state);
    host_arm64_ADR(block, REG_TEMP2, 12);
    host_arm64_LDR_REG_X(block, REG_TEMP2, REG_TEMP2, REG_TEMP);
    host_arm64_BR(block, REG_TEMP2);

    jump_table = (uint64_t *) &block_write_data[block_pos];
    block_pos += 4 * 8;

    jump_table[X87_ROUNDING_NEAREST] = (uint64_t) (uintptr_t) &block_write_data[block_pos]; // tie even
    if (is_quad)
        host_arm64_FCVTNS_X_D(block, REG_TEMP, REG_V_TEMP);
    else
        host_arm64_FCVTNS_W_D(block, REG_TEMP, REG_V_TEMP);
    host_arm64_RET(block, REG_X30);

    jump_table[X87_ROUNDING_UP] = (uint64_t) (uintptr_t) &block_write_data[block_pos]; // pos inf
    if (is_quad)
        host_arm64_FCVTPS_X_D(block, REG_TEMP, REG_V_TEMP);
    else
        host_arm64_FCVTPS_W_D(block, REG_TEMP, REG_V_TEMP);
    host_arm64_RET(block, REG_X30);

    jump_table[X87_ROUNDING_DOWN] = (uint64_t) (uintptr_t) &block_write_data[block_pos]; // neg inf
    if (is_quad)
        host_arm64_FCVTMS_X_D(block, REG_TEMP, REG_V_TEMP);
    else
        host_arm64_FCVTMS_W_D(block, REG_TEMP, REG_V_TEMP);
    host_arm64_RET(block, REG_X30);

    jump_table[X87_ROUNDING_CHOP] = (uint64_t) (uintptr_t) &block_write_data[block_pos]; // zero
    if (is_quad)
        host_arm64_FCVTZS_X_D(block, REG_TEMP, REG_V_TEMP);
    else
        host_arm64_FCVTZS_W_D(block, REG_TEMP, REG_V_TEMP);
    host_arm64_RET(block, REG_X30);
}

void
codegen_backend_init(void)
{
    codeblock_t *block;
    uint8_t      large_block = 0;
    uint8_t      large_hash = 0;

    codeblock      = plat_mmap(BLOCK_SIZE * sizeof(codeblock_t), 0, &large_block);
    codeblock_hash = plat_mmap(HASH_SIZE * sizeof(codeblock_t *), 0, &large_hash);

    if (large_block)
        pclog("Allocated %" PRIu64 " bytes of large pages for codeblock pointers\n", (uint64_t) (BLOCK_SIZE * sizeof(codeblock_t)));
    if (large_hash)
        pclog("Allocated %" PRIu64 " bytes of large pages for codeblock hashes\n", (uint64_t) (HASH_SIZE * sizeof(codeblock_t *)));

    for (int c = 0; c < BLOCK_SIZE; c++) {
        codeblock[c].valid = 0;
    }

    block_current         = 0;
    block_pos             = 0;
    block                 = &codeblock[block_current];
    block->head_mem_block = codegen_allocator_allocate(NULL, block_current);
    block->data           = codeblock_allocator_get_ptr(block->head_mem_block);
    block_write_data      = block->data;
    build_loadstore_routines(block);

    codegen_fp_round = &block_write_data[block_pos];
    build_fp_round_routine(block, 0);
    codegen_fp_round_quad = &block_write_data[block_pos];
    build_fp_round_routine(block, 1);

    codegen_alloc(block, 80);
    codegen_gpf_rout = &block_write_data[block_pos];
    host_arm64_mov_imm(block, REG_ARG0, 0);
    host_arm64_mov_imm(block, REG_ARG1, 0);
    host_arm64_call(block, (void *) x86gpf);

    codegen_exit_rout = &block_write_data[block_pos];
    host_arm64_LDP_POSTIDX_X(block, REG_X19, REG_X20, REG_XSP, 64);
    host_arm64_LDP_POSTIDX_X(block, REG_X21, REG_X22, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X23, REG_X24, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X25, REG_X26, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X27, REG_X28, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X29, REG_X30, REG_XSP, 16);
    host_arm64_RET(block, REG_X30);

    /*#SS(0): an operand through SS outside its limit (CHECK_SEG_LIMITS)*/
    codegen_alloc(block, 40);
    codegen_ss_rout = &block_write_data[block_pos];
    host_arm64_mov_imm(block, REG_ARG0, 0);
    host_arm64_mov_imm(block, REG_ARG1, 0);
    host_arm64_call(block, (void *) x86ss);
    host_arm64_B(block, codegen_exit_rout);

    block_write_data = NULL;

    codegen_allocator_clean_blocks(block->head_mem_block);

    asm("mrs %0, fpcr\n"
        : "=r"(cpu_state.old_fp_control));
}

void
codegen_set_rounding_mode(int mode)
{
    if (mode < 0 || mode > 3)
        fatal("codegen_set_rounding_mode - invalid mode\n");
    cpu_state.new_fp_control = mode << 3;
}

/*R10 - cpu_state*/
void
codegen_backend_prologue(codeblock_t *block)
{
    block_pos = BLOCK_START;

    /*Entry code*/

    host_arm64_STP_PREIDX_X(block, REG_X29, REG_X30, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X27, REG_X28, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X25, REG_X26, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X23, REG_X24, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X21, REG_X22, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X19, REG_X20, REG_XSP, -64);

    host_arm64_MOVX_IMM(block, REG_CPUSTATE, (uint64_t) &cpu_state);

    /* A chain enters here: the frame and X29 are every block's; what
       follows is this block's own. */
    block->chain_entry = block_pos;
    if (block->flags & CODEBLOCK_HAS_FPU) {
        host_arm64_LDR_IMM_W(block, REG_TEMP, REG_CPUSTATE, (uintptr_t) &cpu_state.TOP - (uintptr_t) &cpu_state);
        host_arm64_SUB_IMM(block, REG_TEMP, REG_TEMP, block->TOP);
        host_arm64_STR_IMM_W(block, REG_TEMP, REG_XSP, IREG_TOP_diff_stack_offset);
#    ifdef CODEGEN_CHAIN
        /* Compiled for one top of stack, which the dispatcher checked; a
           chain arriving with another leaves for the dispatcher, which
           compiles the block again for a moving top. */
        if (block->flags & CODEBLOCK_STATIC_TOP) {
            uint32_t *jmp;

            host_arm64_TST_IMM(block, REG_TEMP, 7);
            jmp = host_arm64_BNE_(block);
            host_arm64_branch_set_offset(jmp, codegen_exit_rout);
        }
#    endif
    }
}

#    ifdef CODEGEN_CHAIN
/* Block chaining on ARM64, as on x86-64. The stubs are emitted as fixed
   sequences of raw instructions, so their fields keep their offsets: what
   a link is made under (the target's generation pointer and value, its
   group word and value, the key) is data in a literal pool after the
   stub's code, read with LDR (literal), and the only instruction ever
   written after the block is compiled is one aligned B -- a single-copy
   atomic 32-bit store, followed by the instruction-cache maintenance it
   needs. Every stub is patched by the dispatcher, on the thread that runs
   the blocks, between two of them (exec386_dynarec_dyn), never while it
   could be executing it.

   Scratch registers: X0-X8, X16, X17 (the block has stored everything
   back at its end; X18 is the platform's and is never touched). X29
   holds &cpu_state in every block. */
#        if defined(__APPLE__)
#            include <pthread.h>
#            include <libkern/OSCacheControl.h>
#        endif

#        define A64_X(n)                     (n)
#        define A64_IMM19(off)               (((((uint32_t) (int32_t) (off)) >> 2) & 0x7ffff) << 5)
#        define A64_LDR_X_IMM(rt, rn, off)   (0xf9400000u | ((uint32_t) ((off) >> 3) << 10) | ((rn) << 5) | (rt))
#        define A64_LDR_W_IMM(rt, rn, off)   (0xb9400000u | ((uint32_t) ((off) >> 2) << 10) | ((rn) << 5) | (rt))
#        define A64_LDRH_IMM(rt, rn, off)    (0x79400000u | ((uint32_t) ((off) >> 1) << 10) | ((rn) << 5) | (rt))
#        define A64_LDRB_IMM(rt, rn, off)    (0x39400000u | ((uint32_t) (off) << 10) | ((rn) << 5) | (rt))
#        define A64_LDR_X_LIT(rt, off)       (0x58000000u | A64_IMM19(off) | (rt))
#        define A64_LDR_W_LIT(rt, off)       (0x18000000u | A64_IMM19(off) | (rt))
#        define A64_CBNZ_W(rt, off)          (0x35000000u | A64_IMM19(off) | (rt))
#        define A64_BCOND(cond, off)         (0x54000000u | A64_IMM19(off) | (cond))
#        define A64_TBNZ_W(rt, bit, off)     (0x37000000u | ((uint32_t) (bit) << 19) | (((((uint32_t) (int32_t) (off)) >> 2) & 0x3fff) << 5) | (rt))
#        define A64_B(off)                   (0x14000000u | ((((uint32_t) (int32_t) (off)) >> 2) & 0x3ffffff))
#        define A64_CMP_W(rn, rm)            (0x6b00001fu | ((rm) << 16) | ((rn) << 5))
#        define A64_CMP_X(rn, rm)            (0xeb00001fu | ((rm) << 16) | ((rn) << 5))
#        define A64_ADR(rd, off)             (0x10000000u | ((((uint32_t) (int32_t) (off)) & 3) << 29) | A64_IMM19(off) | (rd))
#        define A64_STR_X(rt, rn)            (0xf9000000u | ((rn) << 5) | (rt))
#        define A64_STR_W(rt, rn)            (0xb9000000u | ((rn) << 5) | (rt))
#        define A64_MOVZ_W(rd, imm)          (0x52800000u | ((uint32_t) (imm) << 5) | (rd))
#        define A64_UBFX_W(rd, rn, lsb, w)   (0x53000000u | ((uint32_t) (lsb) << 16) | ((uint32_t) ((lsb) + (w) - 1) << 10) | ((rn) << 5) | (rd))
#        define A64_ORR_W_LSL(rd, rn, rm, s) (0x2a000000u | ((rm) << 16) | ((uint32_t) (s) << 10) | ((rn) << 5) | (rd))
#        define A64_ADD_W(rd, rn, rm)        (0x0b000000u | ((rm) << 16) | ((rn) << 5) | (rd))
#        define A64_EOR_W_LSR(rd, rn, rm, s) (0x4a400000u | ((rm) << 16) | ((uint32_t) (s) << 10) | ((rn) << 5) | (rd))
#        define A64_ADD_X_LSL(rd, rn, rm, s) (0x8b000000u | ((rm) << 16) | ((uint32_t) (s) << 10) | ((rn) << 5) | (rd))
#        define A64_LDP_W(rt, rt2, rn, off)  (0x29400000u | ((((uint32_t) (off) >> 2) & 0x7f) << 15) | ((rt2) << 10) | ((rn) << 5) | (rt))
#        define A64_LDP_X(rt, rt2, rn, off)  (0xa9400000u | ((((uint32_t) (off) >> 3) & 0x7f) << 15) | ((rt2) << 10) | ((rn) << 5) | (rt))
#        define A64_BLR(rn)                  (0xd63f0000u | ((rn) << 5))
#        define A64_BR(rn)                   (0xd61f0000u | ((rn) << 5))
#        define A64_NOP                      0xd503201fu
#        define A64_EQ                       0x0
#        define A64_NE                       0x1
#        define A64_LE                       0xd

/* The offsets of the cpu_state fields a stub reads through X29, checked
   against the forms that read them. */
static int
a64_cpu_off(const void *field, int scale)
{
    intptr_t off = (intptr_t) field - (intptr_t) &cpu_state;

    if ((off < 0) || (off & (scale - 1)) || ((off / scale) >= 4096))
        fatal("codegen chain: cpu_state field at %i not reachable with scale %i\n", (int) off, scale);
    return (int) off;
}

/* Generated code is written from this thread only between these two
   (MAP_JIT memory: write and execute are a per-thread state on Apple). */
static inline void
a64_jit_write_begin(void)
{
#        if defined(__APPLE__)
    if (__builtin_available(macOS 11.0, *))
        pthread_jit_write_protect_np(0);
#        endif
}

static inline void
a64_jit_write_end(void)
{
#        if defined(__APPLE__)
    if (__builtin_available(macOS 11.0, *))
        pthread_jit_write_protect_np(1);
#        endif
}

static inline void
a64_icache_sync(void *start, size_t len)
{
#        if defined(__APPLE__)
    sys_icache_invalidate(start, len);
#        else
    __builtin___clear_cache((char *) start, (char *) start + len);
#        endif
}

/* A link stub's literal pool: 8-byte aligned after its instructions. */
typedef struct a64_link_lits_t {
    uint64_t genptr;    /* &target->link_gen, or &codegen_link_unlinked */
    uint64_t gen;       /* its value when linked */
    uint64_t gword;     /* the target's group word (codegen_link_group_word) */
    uint64_t gval;      /* its value when linked */
    uint64_t status;    /* &cpu_cur_status */
    uint64_t last_exit; /* &codegen_chain_last_exit */
    uint64_t service;   /* codegen_chain_service */
    uint32_t key;       /* CODEGEN_CHAIN_KEY() when linked; never matches until then */
    uint32_t pad;
} a64_link_lits_t;

#        define LINK_INS  35         /* instructions before the pool */
#        define LINK_BACK 3          /* past the signal test: where a serviced chain goes on */
#        define LINK_JMP  26         /* the B patched to the target */
#        define LINK_MISS 27
#        define LINK_EXIT 30
#        define LINK_SERV 31
#        define LINK_SIZE ((LINK_INS * 4) + 4 + (int) sizeof(a64_link_lits_t))

static inline a64_link_lits_t *
a64_link_lits(uint8_t *stub)
{
    return (a64_link_lits_t *) (((uintptr_t) stub + (LINK_INS * 4) + 7) & ~(uintptr_t) 7);
}

/* PC-relative offset from instruction i of the stub at p to dest. */
#        define A64_REL(p, i, dest) ((intptr_t) (dest) - (intptr_t) &(p)[i])

static void
a64_exit_branch(uint32_t *at)
{
    intptr_t off = (intptr_t) codegen_exit_rout - (intptr_t) at;

    if (!codegen_allocator_can_branch_imm26((uint8_t *) at, codegen_exit_rout))
        fatal("codegen chain: exit routine out of B range (%" PRIi64 ")\n", (int64_t) off);
    *at = A64_B(off);
}

/* A link stub, at a block exit whose PC is already stored: it goes
   straight to the next block while nothing needs the dispatcher. */
void
codegen_backend_link_stub(codeblock_t *block)
{
    const int        sig = a64_cpu_off(&cpu_state.chain_signal, 8);
    const int        cyc = a64_cpu_off(&cpu_state._cycles, 4);
    const int        flr = a64_cpu_off(&cpu_state.chain_floor, 4);
    const int        flg = a64_cpu_off((uint8_t *) &cpu_state.flags + 1, 1);
    const int        acc = a64_cpu_off(&cpu_state.seg_cs.access, 1);
    uint32_t        *p;
    uint8_t         *stub;
    a64_link_lits_t *lit;
    int              n;

    /* One piece, so the fields keep their offsets. */
    codegen_alloc(block, LINK_SIZE + 4);
    stub = &block_write_data[block_pos];
    p    = (uint32_t *) stub;
    lit  = a64_link_lits(stub);

    p[0]  = A64_LDR_X_IMM(16, 29, sig);                      /* X16 = the signal word */
    p[1]  = A64_LDR_W_IMM(17, 16, 0);
    p[2]  = A64_CBNZ_W(17, A64_REL(p, 2, &p[LINK_SERV]));    /* something needs the dispatcher */
    p[3]  = A64_LDR_W_IMM(16, 29, cyc);                      /* back: */
    p[4]  = A64_LDR_W_IMM(17, 29, flr);
    p[5]  = A64_CMP_W(16, 17);
    p[6]  = A64_BCOND(A64_LE, A64_REL(p, 6, &p[LINK_EXIT])); /* out of cycles, or a timer is due */
    p[7]  = A64_LDRB_IMM(16, 29, flg);
    p[8]  = A64_TBNZ_W(16, 0, A64_REL(p, 8, &p[LINK_EXIT])); /* TF */
    /* The pool's base once, and its fields in the pairs they sit in: on
       Apple cores a hop costs what its loads do. */
    p[9]  = A64_ADR(2, A64_REL(p, 9, lit));
    /* The target's link generation (codegen_block.c). */
    p[10] = A64_LDP_X(16, 17, 2, offsetof(a64_link_lits_t, genptr)); /* genptr, gen */
    p[11] = A64_LDR_X_IMM(16, 16, 0);
    p[12] = A64_CMP_X(16, 17);
    p[13] = A64_BCOND(A64_NE, A64_REL(p, 13, &p[LINK_MISS]));
    /* Its link group's word: held while its pages translate elsewhere. */
    p[14] = A64_LDP_X(16, 17, 2, offsetof(a64_link_lits_t, gword)); /* gword, gval */
    p[15] = A64_LDR_X_IMM(16, 16, 0);
    p[16] = A64_CMP_X(16, 17);
    p[17] = A64_BCOND(A64_NE, A64_REL(p, 17, &p[LINK_MISS]));
    /* The key: the status word, and the CPL in bits 17:16 (CODEGEN_CHAIN_KEY). */
    p[18] = A64_LDR_X_IMM(16, 2, offsetof(a64_link_lits_t, status));
    p[19] = A64_LDRH_IMM(16, 16, 0);
    p[20] = A64_LDRB_IMM(17, 29, acc);
    p[21] = A64_UBFX_W(17, 17, 5, 2);                        /* DPL = CPL */
    p[22] = A64_ORR_W_LSL(16, 16, 17, 16);
    p[23] = A64_LDR_W_IMM(17, 2, offsetof(a64_link_lits_t, key));
    p[24] = A64_CMP_W(16, 17);
    p[25] = A64_BCOND(A64_NE, A64_REL(p, 25, &p[LINK_MISS]));
    p[LINK_JMP] = A64_B(A64_REL(p, LINK_JMP, &p[LINK_MISS])); /* patched: B target */
    /* miss: the dispatcher links this stub if it can */
    p[27] = A64_ADR(16, A64_REL(p, 27, stub));
    p[28] = A64_LDR_X_LIT(17, A64_REL(p, 28, &lit->last_exit));
    p[29] = A64_STR_X(16, 17);
    a64_exit_branch(&p[LINK_EXIT]); /* exit: */
    /* service: the signal word is set -- codegen_chain_service looks at it
       (386_dynarec.c) and the chain goes on unless the dispatcher must look */
    p[31] = A64_LDR_X_LIT(16, A64_REL(p, 31, &lit->service));
    p[32] = A64_BLR(16);
    p[33] = A64_CBNZ_W(0, A64_REL(p, 33, &p[LINK_EXIT]));
    p[34] = A64_B(A64_REL(p, 34, &p[LINK_BACK]));
    for (n = LINK_INS; (uint8_t *) &p[n] < (uint8_t *) lit; n++)
        p[n] = A64_NOP;

    lit->genptr    = (uint64_t) (uintptr_t) &codegen_link_unlinked;
    lit->gen       = 0;
    lit->gword     = (uint64_t) (uintptr_t) &codegen_lg_solo_word;
    lit->gval      = 0;
    lit->status    = (uint64_t) (uintptr_t) &cpu_cur_status;
    lit->last_exit = (uint64_t) (uintptr_t) &codegen_chain_last_exit;
    lit->service   = (uint64_t) (uintptr_t) codegen_chain_service;
    lit->key       = 0xffffffff;
    lit->pad       = 0;

    block_pos += (int) (((uint8_t *) (lit + 1)) - stub);
    if ((((uint8_t *) (lit + 1)) - stub) > LINK_SIZE)
        fatal("codegen_backend_link_stub: %i bytes\n", (int) (((uint8_t *) (lit + 1)) - stub));
}

/* An indirect exit's literal pool. */
typedef struct a64_ind_lits_t {
    uint64_t status;  /* &cpu_cur_status */
    uint64_t cache;   /* codegen_ind_cache */
    uint64_t miss;    /* &codegen_ind_miss */
    uint64_t service; /* codegen_chain_service */
} a64_ind_lits_t;

#        define IND_INS  47
#        define IND_BACK 3
#        define IND_MISS 39
#        define IND_EXIT 42
#        define IND_SERV 43
#        define IND_SIZE ((IND_INS * 4) + 4 + (int) sizeof(a64_ind_lits_t))

/* An indirect exit: a near RET, CALL or indirect JMP has stored a PC known
   only now. The same checks as a link, then the indirect link table
   looked up by the linear PC (the 64-byte codegen_ind_t entries, indexed
   with LSL 6); a hit that holds for the CS base, key, generation and group
   goes straight to its block, a miss leaves for the dispatcher, which fills
   the entry (codegen_ind_fill). */
void
codegen_backend_indirect_stub(codeblock_t *block)
{
    const int       sig  = a64_cpu_off(&cpu_state.chain_signal, 8);
    const int       cyc  = a64_cpu_off(&cpu_state._cycles, 4);
    const int       flr  = a64_cpu_off(&cpu_state.chain_floor, 4);
    const int       flg  = a64_cpu_off((uint8_t *) &cpu_state.flags + 1, 1);
    const int       acc  = a64_cpu_off(&cpu_state.seg_cs.access, 1);
    const int       pcof = a64_cpu_off(&cpu_state.pc, 4);
    const int       csof = a64_cpu_off(&cpu_state.seg_cs.base, 4);
    const int       bits = __builtin_ctz(CODEGEN_IND_SIZE);
    uint32_t       *p;
    uint8_t        *stub;
    a64_ind_lits_t *lit;
    int             n;

    _Static_assert((CODEGEN_IND_SIZE & (CODEGEN_IND_SIZE - 1)) == 0, "the indirect table's size is a power of two");
    _Static_assert((offsetof(codegen_ind_t, pc) == 0) && (offsetof(codegen_ind_t, cs_base) == 4) &&
                       (offsetof(codegen_ind_t, status) == 8),
                   "the indirect stub reads these fields at fixed offsets");

    codegen_alloc(block, IND_SIZE + 4);
    stub = &block_write_data[block_pos];
    p    = (uint32_t *) stub;
    lit  = (a64_ind_lits_t *) (((uintptr_t) stub + (IND_INS * 4) + 7) & ~(uintptr_t) 7);

    p[0]  = A64_LDR_X_IMM(16, 29, sig);
    p[1]  = A64_LDR_W_IMM(17, 16, 0);
    p[2]  = A64_CBNZ_W(17, A64_REL(p, 2, &p[IND_SERV]));
    p[3]  = A64_LDR_W_IMM(16, 29, cyc); /* back: */
    p[4]  = A64_LDR_W_IMM(17, 29, flr);
    p[5]  = A64_CMP_W(16, 17);
    p[6]  = A64_BCOND(A64_LE, A64_REL(p, 6, &p[IND_EXIT]));
    p[7]  = A64_LDRB_IMM(16, 29, flg);
    p[8]  = A64_TBNZ_W(16, 0, A64_REL(p, 8, &p[IND_EXIT]));
    p[9]  = A64_LDR_W_IMM(0, 29, pcof);
    p[10] = A64_LDR_W_IMM(1, 29, csof);
    p[11] = A64_ADD_W(0, 0, 1);              /* W0 = the linear PC */
    p[12] = A64_EOR_W_LSR(2, 0, 0, 12);      /* (pc ^ (pc >> 12)) */
    p[13] = A64_UBFX_W(2, 2, 0, bits);       /* & (SIZE - 1) */
    p[14] = A64_LDR_X_LIT(3, A64_REL(p, 14, &lit->cache));
    p[15] = A64_ADD_X_LSL(3, 3, 2, 6);       /* X3 = the entry */
    p[16] = A64_LDP_W(4, 5, 3, 0);           /* pc, cs_base */
    p[17] = A64_CMP_W(4, 0);
    p[18] = A64_BCOND(A64_NE, A64_REL(p, 18, &p[IND_MISS]));
    p[19] = A64_CMP_W(5, 1);
    p[20] = A64_BCOND(A64_NE, A64_REL(p, 20, &p[IND_MISS]));
    p[21] = A64_LDR_X_LIT(16, A64_REL(p, 21, &lit->status));
    p[22] = A64_LDRH_IMM(16, 16, 0);
    p[23] = A64_LDRB_IMM(17, 29, acc);
    p[24] = A64_UBFX_W(17, 17, 5, 2);
    p[25] = A64_ORR_W_LSL(16, 16, 17, 16);   /* CODEGEN_CHAIN_KEY */
    p[26] = A64_LDR_W_IMM(4, 3, 8);          /* key */
    p[27] = A64_CMP_W(16, 4);
    p[28] = A64_BCOND(A64_NE, A64_REL(p, 28, &p[IND_MISS]));
    /* The block's link generation, and its group's word, as a link's. */
    p[29] = A64_LDP_X(4, 5, 3, 16);          /* gen, target */
    p[30] = A64_LDP_X(6, 7, 3, 32);          /* &link_gen, group word */
    p[31] = A64_LDR_X_IMM(6, 6, 0);
    p[32] = A64_CMP_X(6, 4);
    p[33] = A64_BCOND(A64_NE, A64_REL(p, 33, &p[IND_MISS]));
    p[34] = A64_LDR_X_IMM(8, 3, 48);         /* group value */
    p[35] = A64_LDR_X_IMM(7, 7, 0);
    p[36] = A64_CMP_X(7, 8);
    p[37] = A64_BCOND(A64_NE, A64_REL(p, 37, &p[IND_MISS]));
    p[38] = A64_BR(5);
    /* miss: have the dispatcher fill the entry */
    p[39] = A64_LDR_X_LIT(16, A64_REL(p, 39, &lit->miss));
    p[40] = A64_MOVZ_W(17, 1);
    p[41] = A64_STR_W(17, 16);
    a64_exit_branch(&p[IND_EXIT]); /* exit: */
    p[43] = A64_LDR_X_LIT(16, A64_REL(p, 43, &lit->service)); /* service: as a link's */
    p[44] = A64_BLR(16);
    p[45] = A64_CBNZ_W(0, A64_REL(p, 45, &p[IND_EXIT]));
    p[46] = A64_B(A64_REL(p, 46, &p[IND_BACK]));
    for (n = IND_INS; (uint8_t *) &p[n] < (uint8_t *) lit; n++)
        p[n] = A64_NOP;

    lit->status  = (uint64_t) (uintptr_t) &cpu_cur_status;
    lit->cache   = (uint64_t) (uintptr_t) codegen_ind_cache;
    lit->miss    = (uint64_t) (uintptr_t) &codegen_ind_miss;
    lit->service = (uint64_t) (uintptr_t) codegen_chain_service;

    block_pos += (int) (((uint8_t *) (lit + 1)) - stub);
    if ((((uint8_t *) (lit + 1)) - stub) > IND_SIZE)
        fatal("codegen_backend_indirect_stub: %i bytes\n", (int) (((uint8_t *) (lit + 1)) - stub));
}

/* The dispatcher found `target` valid where a chain left through `stub`
   for want of a link: the stub goes straight there from now on, for as
   long as the target's link generation, group word and the key it saw
   hold. On the thread that runs the blocks, between two of them: the pool is
   written first, then the one B, then the instruction cache is made to
   see it. */
int
codegen_chain_patch(uint8_t *stub, codeblock_t *target, uint64_t gen)
{
    uint8_t         *dest = &target->data[target->chain_entry];
    uint32_t        *jmp  = (uint32_t *) (stub + (LINK_JMP * 4));
    a64_link_lits_t *lit  = a64_link_lits(stub);
    uint64_t         gval;
    uint64_t        *gword;

    /* The code pool is smaller than B's reach (codegen_allocator.c), so
       this holds for every pair of blocks; checked all the same. */
    if (!codegen_allocator_can_branch_imm26((uint8_t *) jmp, dest))
        return 0;

    gword = codegen_link_group_word(target, &gval);

    a64_jit_write_begin();
    __atomic_store_n(&lit->genptr, (uint64_t) (uintptr_t) &target->link_gen, __ATOMIC_RELAXED);
    __atomic_store_n(&lit->gen, gen, __ATOMIC_RELAXED);
    __atomic_store_n(&lit->gword, (uint64_t) (uintptr_t) gword, __ATOMIC_RELAXED);
    __atomic_store_n(&lit->gval, gval, __ATOMIC_RELAXED);
    __atomic_store_n(&lit->key, CODEGEN_CHAIN_KEY(), __ATOMIC_RELAXED);
    __atomic_store_n(jmp, A64_B((intptr_t) dest - (intptr_t) jmp), __ATOMIC_RELEASE);
    a64_jit_write_end();
    a64_icache_sync(jmp, 4);
    return 1;
}
#    endif /* CODEGEN_CHAIN */

void
codegen_backend_epilogue(codeblock_t *block)
{
    host_arm64_LDP_POSTIDX_X(block, REG_X19, REG_X20, REG_XSP, 64);
    host_arm64_LDP_POSTIDX_X(block, REG_X21, REG_X22, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X23, REG_X24, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X25, REG_X26, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X27, REG_X28, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X29, REG_X30, REG_XSP, 16);
    host_arm64_RET(block, REG_X30);

    codegen_allocator_clean_blocks(block->head_mem_block);
}

#endif
