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
 *          initialization, prologue/epilogue and rounding-mode control.
 *
 *          Skeleton (milestone M0): all entry points are present so the
 *          build links, but codegen_backend_init() fatal()s - the
 *          block-0 stubs and the real prologue/epilogue land in M1/M3.
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
#    include "codegen_reg.h"

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

/* $fs0-$fs7 = f24-f31, callee-saved (saved/restored by the block
   prologue/epilogue once implemented). */
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

void
codegen_backend_init(void)
{
    codeblock_t *block;
    uint8_t      large_block = 0;
    uint8_t      large_hash  = 0;

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

    /* TODO(LoongArch64 M1): build the block-0 load/store stubs, gpf/exit
       and fp_round(_quad) routines here and read FCSR0 into
       cpu_state.old_fp_control (asserting FCSR0.Enables == 0); see plan
       sections 9 and 11. Until then, refuse to run. */
    fatal("codegen_backend_loongarch64: backend not implemented yet\n");
}

void
codegen_set_rounding_mode(int mode)
{
    /* Store the raw x87 rounding mode (0=RN, 1=RD, 2=RU, 3=RZ); the
       JIT-built codegen_fp_round(_quad) tables decode it directly (they
       use the static ftintr* forms, so no FCSR switching is involved). */
    if (mode < 0 || mode > 3)
        fatal("codegen_set_rounding_mode - invalid mode\n");
    cpu_state.new_fp_control = mode;
}

/*Save set ($ra, $fp/r22 as REG_CPUSTATE, $s0-$s8 and $fs0-$fs7) is
  established in M1 - see plan section 8.1.*/
void
codegen_backend_prologue(UNUSED(codeblock_t *block))
{
    fatal("codegen_backend_loongarch64_prologue: backend not implemented yet\n");
}

void
codegen_backend_epilogue(UNUSED(codeblock_t *block))
{
    fatal("codegen_backend_loongarch64_epilogue: backend not implemented yet\n");
}

#endif
