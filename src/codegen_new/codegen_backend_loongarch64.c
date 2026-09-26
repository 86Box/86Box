#if defined __loongarch_lp64

/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          LoongArch64 backend for the "new" dynamic recompiler -
 *          initialization (block-0 load/store stubs, gpf/exit), the block
 *          prologue/epilogue and rounding-mode control (plan sections
 *          8.1, 9 and 11).
 */

#    include <inttypes.h>
#    include <stdint.h>
#    include <stdlib.h>
#    include <86box/86box.h>
#    include "cpu.h"
#    include <86box/mem.h>
#    include <86box/plat.h>
#    include <86box/plat_unused.h>

#    include "codegen.h"
#    include "codegen_allocator.h"
#    include "codegen_backend.h"
#    include "codegen_backend_loongarch64_defs.h"
#    include "codegen_backend_loongarch64.h"
#    include "codegen_reg.h"
#    include "codegen_ir_defs.h"
#    include "x86.h"
#    include "x86seg_common.h"
#    include "x86seg.h"
#    include "x87_sf.h"
#    include "x87.h"

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
void *codegen_exit_rout;

host_reg_def_t codegen_host_reg_list[CODEGEN_HOST_REGS] = {
    { REG_S0, 0},
    { REG_S1, 0},
    { REG_S2, 0},
    { REG_S3, 0},
    { REG_S4, 0},
    { REG_S5, 0},
    { REG_S6, 0},
    { REG_S7, 0},
    { REG_S8, 0}
};

/* $fs0-$fs7 = f24-f31, callee-saved; saved/restored by the block
   prologue/epilogue (plan section 3: do not copy arm64's shortcut of
   leaving the FP set unsaved). */
host_reg_def_t codegen_host_fp_reg_list[CODEGEN_HOST_FP_REGS] = {
    { REG_F24, 0},
    { REG_F25, 0},
    { REG_F26, 0},
    { REG_F27, 0},
    { REG_F28, 0},
    { REG_F29, 0},
    { REG_F30, 0},
    { REG_F31, 0}
};

/*In - A0 = address
  Out - A0 = data, A1 = abrt

  SRLI.D  T0, A0, 12          ;page
  SLLI.D  T0, T0, 3
  MOV     T1, readlookup2
  LDX.D   T1, T1, T0          ;entry
  * (size > 1) ANDI T2, A0, size-1 ;BNE -> misaligned/slow
  ADDI.D  T2, T1, 1           ;entry == -1 ?
  BEQ     T2, zero, -> slow
  LDX.[BU/HU/WU] A0, T1, A0   ;or ADD.D + FLD for the float forms
  MOV     A1, 0
  RET
  slow:
  ADDI.D  sp, sp, -16
  ST.D    ra, sp, 0
  BL      readmembl / readmemwl / readmemll / readmemql (float forms reuse
          readmemll/writememll - the value travels as raw bits in A0/A1)
  [float: MOVGR2FR V_TEMP, A0]
  LDRBU   A1, cpu_state.abrt
  LD.D    ra, sp, 0
  ADDI.D  sp, sp, 16
  JIRL    zero, ra, 0
*/
static void
build_load_routine(codeblock_t *block, int size, int is_float)
{
    uint32_t *branch_offset;
    uint32_t *misaligned_offset = NULL;

    codegen_alloc(block, 140);
    host_loong64_SHR_D_IMM(block, REG_TEMP, REG_A0, 12);
    host_loong64_SHL_D_IMM(block, REG_TEMP, REG_TEMP, 3);
    host_loong64_mov_imm(block, REG_TEMP2, (uint64_t) (uintptr_t) readlookup2);
    host_loong64_LDRX_REG(block, REG_TEMP2, REG_TEMP2, REG_TEMP);
    if (size != 1) {
        host_loong64_ANDI(block, REG_TEMP3, REG_A0, size - 1);
        misaligned_offset = host_loong64_BNE_(block, REG_TEMP3, REG_ZERO);
    }
    host_loong64_ADDI_D(block, REG_TEMP3, REG_TEMP2, 1);
    branch_offset = host_loong64_BEQ_(block, REG_TEMP3, REG_ZERO);
    if (size == 1 && !is_float)
        host_loong64_LDRB_REG(block, REG_A0, REG_TEMP2, REG_A0);
    else if (size == 2 && !is_float)
        host_loong64_LDRH_REG(block, REG_A0, REG_TEMP2, REG_A0);
    else if (size == 4 && !is_float)
        host_loong64_LDR_REG(block, REG_A0, REG_TEMP2, REG_A0);
    else if ((size == 4 && is_float) || size == 8) {
        host_loong64_ADDX_REG(block, REG_TEMP3, REG_TEMP2, REG_A0);
        if (size == 4)
            host_loong64_FLD_S_IMM(block, REG_V_TEMP, REG_TEMP3, 0);
        else
            host_loong64_FLD_D_IMM(block, REG_V_TEMP, REG_TEMP3, 0);
    } else
        fatal("build_load_routine - unknown size %i\n", size);
    host_loong64_MOV_W(block, REG_A1, REG_ZERO);
    /*Fast-path exit: return here. Falling through re-runs the C mem op and
      overwrites A1 with cpu_state.abrt, so the block's abort check would
      test garbage instead of the stub's success flag. */
    host_loong64_RET(block);

    host_loong64_branch_set_offset(branch_offset, &block_write_data[block_pos]);
    if (size != 1)
        host_loong64_branch_set_offset(misaligned_offset, &block_write_data[block_pos]);

    host_loong64_ADDI_D(block, REG_R3, REG_R3, -16);
    host_loong64_STRX_IMM(block, REG_RA, REG_R3, 0);
    if (size == 1)
        host_loong64_call(block, (void *) readmembl);
    else if (size == 2)
        host_loong64_call(block, (void *) readmemwl);
    else if (size == 4)
        host_loong64_call(block, (void *) readmemll);
    else if (size == 8)
        host_loong64_call(block, (void *) readmemql);
    else
        fatal("build_load_routine - unknown size %i\n", size);
    if (size == 4 && is_float)
        host_loong64_MOVGR2FR_W(block, REG_V_TEMP, REG_A0);
    else if (size == 8)
        host_loong64_MOVGR2FR_D(block, REG_V_TEMP, REG_A0);
    codegen_direct_read_8(block, REG_A1, &cpu_state.abrt);
    host_loong64_LDRX_IMM(block, REG_RA, REG_R3, 0);
    host_loong64_ADDI_D(block, REG_R3, REG_R3, 16);
    host_loong64_RET(block);
}

static void
build_store_routine(codeblock_t *block, int size, int is_float)
{
    uint32_t *branch_offset;
    uint32_t *misaligned_offset = NULL;

    /*In - A0 = address, A1 = data (or V_TEMP for the float forms)
      Out - A1 = abrt*/
    codegen_alloc(block, 96);
    host_loong64_SHR_D_IMM(block, REG_TEMP, REG_A0, 12);
    host_loong64_SHL_D_IMM(block, REG_TEMP, REG_TEMP, 3);
    host_loong64_mov_imm(block, REG_TEMP2, (uint64_t) (uintptr_t) writelookup2);
    host_loong64_LDRX_REG(block, REG_TEMP2, REG_TEMP2, REG_TEMP);
    if (size != 1) {
        host_loong64_ANDI(block, REG_TEMP3, REG_A0, size - 1);
        misaligned_offset = host_loong64_BNE_(block, REG_TEMP3, REG_ZERO);
    }
    host_loong64_ADDI_D(block, REG_TEMP3, REG_TEMP2, 1);
    branch_offset = host_loong64_BEQ_(block, REG_TEMP3, REG_ZERO);
    if (size == 1 && !is_float)
        host_loong64_STRB_REG(block, REG_A1, REG_TEMP2, REG_A0);
    else if (size == 2 && !is_float)
        host_loong64_STRH_REG(block, REG_A1, REG_TEMP2, REG_A0);
    else if (size == 4 && !is_float)
        host_loong64_STR_REG(block, REG_A1, REG_TEMP2, REG_A0);
    else if ((size == 4 && is_float) || size == 8) {
        host_loong64_ADDX_REG(block, REG_TEMP3, REG_TEMP2, REG_A0);
        if (size == 4)
            host_loong64_FST_S_IMM(block, REG_V_TEMP, REG_TEMP3, 0);
        else
            host_loong64_FST_D_IMM(block, REG_V_TEMP, REG_TEMP3, 0);
    } else
        fatal("build_store_routine - unknown size %i\n", size);
    host_loong64_MOV_W(block, REG_A1, REG_ZERO);
    /*Fast-path exit: return here. Falling through re-runs the C mem op and
      overwrites A1 with cpu_state.abrt, so the block's abort check would
      test garbage instead of the stub's success flag. */
    host_loong64_RET(block);

    host_loong64_branch_set_offset(branch_offset, &block_write_data[block_pos]);
    if (size != 1)
        host_loong64_branch_set_offset(misaligned_offset, &block_write_data[block_pos]);

    host_loong64_ADDI_D(block, REG_R3, REG_R3, -16);
    host_loong64_STRX_IMM(block, REG_RA, REG_R3, 0);
    if (size == 4 && is_float)
        host_loong64_MOVFR2GR_S(block, REG_A1, REG_V_TEMP);
    else if (size == 8)
        host_loong64_MOVFR2GR_D(block, REG_A1, REG_V_TEMP);
    if (size == 1)
        host_loong64_call(block, (void *) writemembl);
    else if (size == 2)
        host_loong64_call(block, (void *) writememwl);
    else if (size == 4)
        host_loong64_call(block, (void *) writememll);
    else if (size == 8)
        host_loong64_call(block, (void *) writememql);
    else
        fatal("build_store_routine - unknown size %i\n", size);
    codegen_direct_read_8(block, REG_A1, &cpu_state.abrt);
    host_loong64_LDRX_IMM(block, REG_RA, REG_R3, 0);
    host_loong64_ADDI_D(block, REG_R3, REG_R3, 16);
    host_loong64_RET(block);
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

/*Emits the register restores shared by codegen_exit_rout and the block
  epilogue (plan section 8.1).*/
static void
codegen_backend_restore_saves(codeblock_t *block)
{
    host_loong64_LDRX_IMM(block, REG_RA, REG_R3, LOONG64_STACKOFF_RA);
    host_loong64_LDRX_IMM(block, REG_CPUSTATE, REG_R3, LOONG64_STACKOFF_CPUSTATE);
    for (int reg = REG_S0; reg <= REG_S8; reg++)
        host_loong64_LDRX_IMM(block, reg, REG_R3, LOONG64_STACKOFF_INT(reg));
    for (int freg = REG_F24; freg <= REG_F31; freg++)
        host_loong64_FLD_D_IMM(block, freg, REG_R3, LOONG64_STACKOFF_FP(freg));
}

void
codegen_backend_init(void)
{
    codeblock_t *block;
    uint8_t      large_block = 0;
    uint8_t      large_hash  = 0;
    uint64_t     fcsr0;

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

    /*codegen_fp_round / codegen_fp_round_quad are built in M3 (plan
       section 12); nothing calls them while FPU support is absent.*/

    codegen_alloc(block, 80);
    codegen_gpf_rout = &block_write_data[block_pos];
    host_loong64_mov_imm(block, REG_ARG0, 0);
    host_loong64_mov_imm(block, REG_ARG1, 0);
    host_loong64_call(block, (void *) x86gpf);

    codegen_exit_rout = &block_write_data[block_pos];
    codegen_backend_restore_saves(block);
    host_loong64_ADDI_D(block, REG_R3, REG_R3, LOONG64_PROLOGUE_FRAME);
    host_loong64_RET(block);

    block_write_data = NULL;

    codegen_allocator_clean_blocks(block->head_mem_block);

    /*Read FCSR0 into cpu_state.old_fp_control (plan section 9). The
      x87 mode is stored raw in new_fp_control by codegen_set_rounding_mode
      and decoded by the (M3) fp_round tables - no FCSR switching needed.*/
    asm volatile("movfcsr2gr %0, $fcsr0"
                 : "=r"(fcsr0));
    if (fcsr0 & 0x1f)
        fatal("codegen_backend_loongarch64_init: FCSR0 enables are not clear (%08" PRIx64 ") - FP exceptions would trap\n", fcsr0);
    cpu_state.old_fp_control = fcsr0;
}

void
codegen_set_rounding_mode(int mode)
{
    /*Store the raw x87 rounding mode (0=RN, 1=RD, 2=RU, 3=RZ); the
      JIT-built codegen_fp_round(_quad) tables decode it directly (they
      use the static ftintr* forms, so no FCSR switching is involved). */
    if (mode < 0 || mode > 3)
        fatal("codegen_set_rounding_mode - invalid mode\n");
    cpu_state.new_fp_control = mode;
}

/*Save set: $ra, REG_CPUSTATE ($r22), $s0-$s8 and $fs0-$fs7. SP+16..56 is
  left free for the generic IREG_temp / TOP-diff stack slots (plan section
  8.1; see LOONG64_PROLOGUE_FRAME in the backend header).*/
void
codegen_backend_prologue(codeblock_t *block)
{
    block_pos = BLOCK_START;

    host_loong64_ADDI_D(block, REG_R3, REG_R3, -LOONG64_PROLOGUE_FRAME);
    host_loong64_STRX_IMM(block, REG_RA, REG_R3, LOONG64_STACKOFF_RA);
    host_loong64_STRX_IMM(block, REG_CPUSTATE, REG_R3, LOONG64_STACKOFF_CPUSTATE);
    for (int reg = REG_S0; reg <= REG_S8; reg++)
        host_loong64_STRX_IMM(block, reg, REG_R3, LOONG64_STACKOFF_INT(reg));
    for (int freg = REG_F24; freg <= REG_F31; freg++)
        host_loong64_FST_D_IMM(block, freg, REG_R3, LOONG64_STACKOFF_FP(freg));

    host_loong64_mov_imm(block, REG_CPUSTATE, (uint64_t) (uintptr_t) &cpu_state);

    if (block->flags & CODEBLOCK_HAS_FPU) {
        host_loong64_LDR_W_IMM(block, REG_TEMP, REG_CPUSTATE, (uintptr_t) &cpu_state.TOP - (uintptr_t) &cpu_state);
        host_loong64_ADD_W_IMM(block, REG_TEMP, REG_TEMP, -(block->TOP));
        host_loong64_STR_W_IMM(block, REG_TEMP, REG_R3, IREG_TOP_diff_stack_offset);
    }
}

void
codegen_backend_epilogue(codeblock_t *block)
{
    codegen_backend_restore_saves(block);
    host_loong64_ADDI_D(block, REG_R3, REG_R3, LOONG64_PROLOGUE_FRAME);
    host_loong64_RET(block);

    codegen_allocator_clean_blocks(block->head_mem_block);
}

#endif