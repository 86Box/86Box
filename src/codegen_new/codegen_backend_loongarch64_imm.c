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
 *          immediate materialisation (plan section 8.2).
 */

#    include <stdint.h>
#    include <86box/86box.h>
#    include "cpu.h"

#    include "codegen.h"
#    include "codegen_backend.h"
#    include "codegen_backend_loongarch64_defs.h"
#    include "codegen_backend_loongarch64.h"

void
host_loong64_MOVX_IMM(codeblock_t *block, int reg, uint64_t imm_data)
{
    /*Same loader as host_loong64_mov_imm (name kept for arm64 parity in
      the uop handlers).*/
    host_loong64_mov_imm(block, reg, imm_data);
}

void
host_loong64_mov_imm_w(codeblock_t *block, int reg, uint32_t imm_data)
{
    /*Canonical sign-extended 32-bit constant (QEMU tcg_out_movi_i32).*/
    uint32_t lo12 = imm_data & 0xfff;
    uint32_t hi20 = (imm_data >> 12) & 0xfffff;

    if (imm_data <= 0xfff) {
        /*ori rd, zero, imm*/
        host_loong64_ORI(block, reg, REG_ZERO, imm_data);
    } else if ((int32_t) imm_data >= -0x800 && (int32_t) imm_data <= 0x7ff) {
        /*addi.w rd, zero, imm - sign-extends the 32-bit write to 64 bits*/
        host_loong64_ADDI_W(block, reg, REG_ZERO, (int32_t) imm_data);
    } else {
        host_loong64_LU12I_W(block, reg, hi20);
        if (lo12)
            host_loong64_ORI(block, reg, reg, lo12);
    }
}

void
host_loong64_mov_imm(codeblock_t *block, int reg, uint64_t imm_data)
{
    /*64-bit constant (QEMU tcg_out_movi, PC-relative fast paths omitted -
       see plan section 8.2).*/
    uint32_t hi32_field;
    uint32_t hi52_field;
    uint32_t cur_hi32;
    uint32_t cur_hi52;

    if (imm_data == (uint64_t) (int64_t) (int32_t) (uint32_t) imm_data) {
        host_loong64_mov_imm_w(block, reg, (uint32_t) imm_data);
        return;
    }

    hi32_field = (uint32_t) ((imm_data >> 32) & 0xfffff);
    hi52_field = (uint32_t) ((imm_data >> 52) & 0xfff);

    if ((imm_data & 0xfffffffffffffull) == 0 && hi52_field) {
        /*Single lu52i.d when bits [51:0] are all zero.*/
        host_loong64_LU52I_D(block, reg, REG_ZERO, hi52_field);
        return;
    }

    /*Slow path: canonical 32-bit load, then splice in the high fields.
      mov_imm_w leaves rd[63:32] = sext(val[31:0]), ie field 0xfffff/0
      in [51:32] and its sign extension in [63:52].*/
    host_loong64_mov_imm_w(block, reg, (uint32_t) imm_data);

    cur_hi32 = ((int32_t) (uint32_t) imm_data < 0) ? 0xfffff : 0;
    if (hi32_field != cur_hi32)
        host_loong64_LU32I_D(block, reg, hi32_field);

    /*lu32i.d also sign-extends its field into bits [63:52]; when it is
      skipped the field left by the 32-bit load extends the same way
      (bit 19 of 0xfffff/0 is 1/0 respectively).*/
    cur_hi52 = ((hi32_field != cur_hi32) ? hi32_field : cur_hi32) & 0x80000 ? 0xfff : 0;
    if (hi52_field != cur_hi52)
        host_loong64_LU52I_D(block, reg, reg, hi52_field);
}

uint32_t
host_loong64_find_imm(uint32_t data)
{
    /*LA64 andi/ori/xori take a plain 12-bit unsigned immediate - no
      bitmask-encoding table like arm64's. Return the value itself when
      directly encodable so callers can branch on a zero result.*/
    return data <= 0xfff ? data : 0;
}

#endif
