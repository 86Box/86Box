#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

#include "x86.h"
#include "386_common.h"
#include "x86_flags.h"
#include "codegen.h"
#include "codegen_backend.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_helpers.h"
#include "codegen_ops_mov.h"

static int NF_SET_01()
{
        return NF_SET() ? 1 : 0;
}
static int VF_SET_01()
{
        return VF_SET() ? 1 : 0;
}

static int ropJO_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
        int jump_uop;

        switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN)
        {
                case FLAGS_ZN8: case FLAGS_ZN16: case FLAGS_ZN32:
                /*Overflow is always zero*/
                return 0;

                case FLAGS_SUB8: case FLAGS_DEC8:
                jump_uop = uop_CMP_JNO_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
                break;

                case FLAGS_SUB16: case FLAGS_DEC16:
                jump_uop = uop_CMP_JNO_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
                break;

                case FLAGS_SUB32: case FLAGS_DEC32:
                jump_uop = uop_CMP_JNO_DEST(ir, IREG_flags_op1, IREG_flags_op2);
                break;

                case FLAGS_UNKNOWN:
                default:
                uop_CALL_FUNC_RESULT(ir, IREG_temp0, VF_SET);
                jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
                break;
        }
        uop_MOV_IMM(ir, IREG_pc, dest_addr);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);
        return 0;
}
static int ropJNO_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
        int jump_uop;

        switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN)
        {
                case FLAGS_ZN8: case FLAGS_ZN16: case FLAGS_ZN32:
                /*Overflow is always zero*/
                uop_MOV_IMM(ir, IREG_pc, dest_addr);
                uop_JMP(ir, codegen_exit_rout);
                return 0;

                case FLAGS_SUB8: case FLAGS_DEC8:
                jump_uop = uop_CMP_JO_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
                break;

                case FLAGS_SUB16: case FLAGS_DEC16:
                jump_uop = uop_CMP_JO_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
                break;

                case FLAGS_SUB32: case FLAGS_DEC32:
                jump_uop = uop_CMP_JO_DEST(ir, IREG_flags_op1, IREG_flags_op2);
                break;

                case FLAGS_UNKNOWN:
                default:
                uop_CALL_FUNC_RESULT(ir, IREG_temp0, VF_SET);
                jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                break;
        }
        uop_MOV_IMM(ir, IREG_pc, dest_addr);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);
        return 0;
}

static int ropJB_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
        int jump_uop;
        int do_unroll = (CF_SET() && codegen_can_unroll(block, ir, next_pc, dest_addr));

        switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN)
        {
                case FLAGS_ZN8: case FLAGS_ZN16: case FLAGS_ZN32:
                /*Carry is always zero*/
                return 0;

                case FLAGS_SUB8:
                if (do_unroll)
                        jump_uop = uop_CMP_JB_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
                else
                        jump_uop = uop_CMP_JNB_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
                break;

                case FLAGS_SUB16:
                if (do_unroll)
                        jump_uop = uop_CMP_JB_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
                else
                        jump_uop = uop_CMP_JNB_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
                break;

                case FLAGS_SUB32:
                if (do_unroll)
                        jump_uop = uop_CMP_JB_DEST(ir, IREG_flags_op1, IREG_flags_op2);
                else
                        jump_uop = uop_CMP_JNB_DEST(ir, IREG_flags_op1, IREG_flags_op2);
                break;

                case FLAGS_UNKNOWN:
                default:
                uop_CALL_FUNC_RESULT(ir, IREG_temp0, CF_SET);
                if (do_unroll)
                        jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                else
                        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
                break;
        }
        uop_MOV_IMM(ir, IREG_pc, do_unroll ? next_pc : dest_addr);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);
        return do_unroll ? 1 : 0;
}
static int ropJNB_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
        int jump_uop;
        int do_unroll = (!CF_SET() && codegen_can_unroll(block, ir, next_pc, dest_addr));

        switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN)
        {
                case FLAGS_ZN8: case FLAGS_ZN16: case FLAGS_ZN32:
                /*Carry is always zero*/
                uop_MOV_IMM(ir, IREG_pc, dest_addr);
                uop_JMP(ir, codegen_exit_rout);
                return 0;

                case FLAGS_SUB8:
                if (do_unroll)
                        jump_uop = uop_CMP_JNB_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
                else
                        jump_uop = uop_CMP_JB_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
                break;

                case FLAGS_SUB16:
                if (do_unroll)
                        jump_uop = uop_CMP_JNB_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
                else
                        jump_uop = uop_CMP_JB_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
                break;

                case FLAGS_SUB32:
                if (do_unroll)
                        jump_uop = uop_CMP_JNB_DEST(ir, IREG_flags_op1, IREG_flags_op2);
                else
                        jump_uop = uop_CMP_JB_DEST(ir, IREG_flags_op1, IREG_flags_op2);
                break;

                case FLAGS_UNKNOWN:
                default:
                uop_CALL_FUNC_RESULT(ir, IREG_temp0, CF_SET);
                if (do_unroll)
                        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
                else
                        jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                break;
        }
        uop_MOV_IMM(ir, IREG_pc, do_unroll ? next_pc : dest_addr);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);
        return do_unroll ? 1 : 0;
}

static int ropJE_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
        int jump_uop;

        if (ZF_SET() && codegen_can_unroll(block, ir, next_pc, dest_addr))
        {
                if (!codegen_flags_changed || !flags_res_valid())
                {
                        uop_CALL_FUNC_RESULT(ir, IREG_temp0, ZF_SET);
                        jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                }
                else
                {
                        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_flags_res, 0);
                }
                uop_MOV_IMM(ir, IREG_pc, next_pc);
                uop_JMP(ir, codegen_exit_rout);
                uop_set_jump_dest(ir, jump_uop);
                return 1;
        }
        else
        {
                if (!codegen_flags_changed || !flags_res_valid())
                {
                        uop_CALL_FUNC_RESULT(ir, IREG_temp0, ZF_SET);
                        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
                }
                else
                {
                        jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_flags_res, 0);
                }
                uop_MOV_IMM(ir, IREG_pc, dest_addr);
                uop_JMP(ir, codegen_exit_rout);
                uop_set_jump_dest(ir, jump_uop);
        }
        return 0;
}
int ropJNE_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
        int jump_uop;

        if (!ZF_SET() && codegen_can_unroll(block, ir, next_pc, dest_addr))
        {
                if (!codegen_flags_changed || !flags_res_valid())
                {
                        uop_CALL_FUNC_RESULT(ir, IREG_temp0, ZF_SET);
                        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
                }
                else
                {
                        jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_flags_res, 0);
                }
                uop_MOV_IMM(ir, IREG_pc, next_pc);
                uop_JMP(ir, codegen_exit_rout);
                uop_set_jump_dest(ir, jump_uop);
                return 1;
        }
        else
        {
                if (!codegen_flags_changed || !flags_res_valid())
                {
                        uop_CALL_FUNC_RESULT(ir, IREG_temp0, ZF_SET);
                        jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                }
                else
                {
                        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_flags_res, 0);
                }
                uop_MOV_IMM(ir, IREG_pc, dest_addr);
                uop_JMP(ir, codegen_exit_rout);
                uop_set_jump_dest(ir, jump_uop);
        }
        return 0;
}

static int ropJBE_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
        int jump_uop, jump_uop2 = -1;
        int do_unroll = ((CF_SET() || ZF_SET()) && codegen_can_unroll(block, ir, next_pc, dest_addr));

        switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN)
        {
                case FLAGS_ZN8: case FLAGS_ZN16: case FLAGS_ZN32:
                /*Carry is always zero, so test zero only*/
                if (do_unroll)
                        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_flags_res, 0);
                else
                        jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_flags_res, 0);
                break;

                case FLAGS_SUB8:
                if (do_unroll)
                        jump_uop = uop_CMP_JBE_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
                else
                        jump_uop = uop_CMP_JNBE_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
                break;
                case FLAGS_SUB16:
                if (do_unroll)
                        jump_uop = uop_CMP_JBE_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
                else
                        jump_uop = uop_CMP_JNBE_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
                break;
                case FLAGS_SUB32:
                if (do_unroll)
                        jump_uop = uop_CMP_JBE_DEST(ir, IREG_flags_op1, IREG_flags_op2);
                else
                        jump_uop = uop_CMP_JNBE_DEST(ir, IREG_flags_op1, IREG_flags_op2);
                break;

                case FLAGS_UNKNOWN:
                default:
                if (do_unroll)
                {
                        uop_CALL_FUNC_RESULT(ir, IREG_temp0, CF_SET);
                        jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                        uop_CALL_FUNC_RESULT(ir, IREG_temp0, ZF_SET);
                        jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                }
                else
                {
                        uop_CALL_FUNC_RESULT(ir, IREG_temp0, CF_SET);
                        jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                        uop_CALL_FUNC_RESULT(ir, IREG_temp0, ZF_SET);
                        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
                }
                break;
        }
        if (do_unroll)
        {
                uop_MOV_IMM(ir, IREG_pc, next_pc);
                uop_JMP(ir, codegen_exit_rout);
                uop_set_jump_dest(ir, jump_uop);
                if (jump_uop2 != -1)
                        uop_set_jump_dest(ir, jump_uop2);
                return 1;
        }
        else
        {
                if (jump_uop2 != -1)
                        uop_set_jump_dest(ir, jump_uop2);
                uop_MOV_IMM(ir, IREG_pc, dest_addr);
                uop_JMP(ir, codegen_exit_rout);
                uop_set_jump_dest(ir, jump_uop);
                return 0;
        }
}
static int ropJNBE_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
        int jump_uop, jump_uop2 = -1;
        int do_unroll = ((!CF_SET() && !ZF_SET()) && codegen_can_unroll(block, ir, next_pc, dest_addr));

        switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN)
        {
                case FLAGS_ZN8: case FLAGS_ZN16: case FLAGS_ZN32:
                /*Carry is always zero, so test zero only*/
                if (do_unroll)
                        jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_flags_res, 0);
                else
                        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_flags_res, 0);
                break;

                case FLAGS_SUB8:
                if (do_unroll)
                        jump_uop = uop_CMP_JNBE_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
                else
                        jump_uop = uop_CMP_JBE_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
                break;
                case FLAGS_SUB16:
                if (do_unroll)
                        jump_uop = uop_CMP_JNBE_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
                else
                        jump_uop = uop_CMP_JBE_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
                break;
                case FLAGS_SUB32:
                if (do_unroll)
                        jump_uop = uop_CMP_JNBE_DEST(ir, IREG_flags_op1, IREG_flags_op2);
                else
                        jump_uop = uop_CMP_JBE_DEST(ir, IREG_flags_op1, IREG_flags_op2);
                break;

                case FLAGS_UNKNOWN:
                default:
                if (do_unroll)
                {
                        uop_CALL_FUNC_RESULT(ir, IREG_temp0, CF_SET);
                        jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                        uop_CALL_FUNC_RESULT(ir, IREG_temp0, ZF_SET);
                        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
                }
                else
                {
                        uop_CALL_FUNC_RESULT(ir, IREG_temp0, CF_SET);
                        jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                        uop_CALL_FUNC_RESULT(ir, IREG_temp0, ZF_SET);
                        jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                }
                break;
        }
        if (do_unroll)
        {
                if (jump_uop2 != -1)
                        uop_set_jump_dest(ir, jump_uop2);
                uop_MOV_IMM(ir, IREG_pc, next_pc);
                uop_JMP(ir, codegen_exit_rout);
                uop_set_jump_dest(ir, jump_uop);
                return 1;
        }
        else
        {
                uop_MOV_IMM(ir, IREG_pc, dest_addr);
                uop_JMP(ir, codegen_exit_rout);
                uop_set_jump_dest(ir, jump_uop);
                if (jump_uop2 != -1)
                        uop_set_jump_dest(ir, jump_uop2);
                return 0;
        }
}

static int ropJS_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
        int jump_uop;
        int do_unroll = (NF_SET() && codegen_can_unroll(block, ir, next_pc, dest_addr));

        switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN)
        {
                case FLAGS_ZN8:
                case FLAGS_ADD8:
                case FLAGS_SUB8:
                case FLAGS_SHL8:
                case FLAGS_SHR8:
                case FLAGS_SAR8:
                case FLAGS_INC8:
                case FLAGS_DEC8:
                if (do_unroll)
                        jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res_B);
                else
                        jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res_B);
                break;

                case FLAGS_ZN16:
                case FLAGS_ADD16:
                case FLAGS_SUB16:
                case FLAGS_SHL16:
                case FLAGS_SHR16:
                case FLAGS_SAR16:
                case FLAGS_INC16:
                case FLAGS_DEC16:
                if (do_unroll)
                        jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res_W);
                else
                        jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res_W);
                break;

                case FLAGS_ZN32:
                case FLAGS_ADD32:
                case FLAGS_SUB32:
                case FLAGS_SHL32:
                case FLAGS_SHR32:
                case FLAGS_SAR32:
                case FLAGS_INC32:
                case FLAGS_DEC32:
                if (do_unroll)
                        jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res);
                else
                        jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res);
                break;

                case FLAGS_UNKNOWN:
                default:
                uop_CALL_FUNC_RESULT(ir, IREG_temp0, NF_SET);
                if (do_unroll)
                        jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                else
                        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
                break;
        }
        uop_MOV_IMM(ir, IREG_pc, do_unroll ? next_pc : dest_addr);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);
        return do_unroll ? 1 : 0;
}
static int ropJNS_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
        int jump_uop;
        int do_unroll = (!NF_SET() && codegen_can_unroll(block, ir, next_pc, dest_addr));

        switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN)
        {
                case FLAGS_ZN8:
                case FLAGS_ADD8:
                case FLAGS_SUB8:
                case FLAGS_SHL8:
                case FLAGS_SHR8:
                case FLAGS_SAR8:
                case FLAGS_INC8:
                case FLAGS_DEC8:
                if (do_unroll)
                        jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res_B);
                else
                        jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res_B);
                break;

                case FLAGS_ZN16:
                case FLAGS_ADD16:
                case FLAGS_SUB16:
                case FLAGS_SHL16:
                case FLAGS_SHR16:
                case FLAGS_SAR16:
                case FLAGS_INC16:
                case FLAGS_DEC16:
                if (do_unroll)
                        jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res_W);
                else
                        jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res_W);
                break;

                case FLAGS_ZN32:
                case FLAGS_ADD32:
                case FLAGS_SUB32:
                case FLAGS_SHL32:
                case FLAGS_SHR32:
                case FLAGS_SAR32:
                case FLAGS_INC32:
                case FLAGS_DEC32:
                if (do_unroll)
                        jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res);
                else
                        jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res);
                break;

                case FLAGS_UNKNOWN:
                default:
                uop_CALL_FUNC_RESULT(ir, IREG_temp0, NF_SET);
                if (do_unroll)
                        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
                else
                        jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                break;
        }
        uop_MOV_IMM(ir, IREG_pc, do_unroll ? next_pc : dest_addr);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);
        return do_unroll ? 1 : 0;
}

static int ropJP_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
        int jump_uop;

        uop_CALL_FUNC_RESULT(ir, IREG_temp0, PF_SET);
        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
        uop_MOV_IMM(ir, IREG_pc, dest_addr);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);
        return 0;
}
static int ropJNP_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
        int jump_uop;

        uop_CALL_FUNC_RESULT(ir, IREG_temp0, PF_SET);
        jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
        uop_MOV_IMM(ir, IREG_pc, dest_addr);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);
        return 0;
}

static int ropJL_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
        int jump_uop;
        int do_unroll = ((NF_SET() ? 1 : 0) != (VF_SET() ? 1 : 0) && codegen_can_unroll(block, ir, next_pc, dest_addr));

        switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN)
        {
                case FLAGS_ZN8:
                /*V flag is always clear. Condition is true if N is set*/
                if (do_unroll)
                        jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res_B);
                else
                        jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res_B);
                break;
                case FLAGS_ZN16:
                if (do_unroll)
                        jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res_W);
                else
                        jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res_W);
                break;
                case FLAGS_ZN32:
                if (do_unroll)
                        jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res);
                else
                        jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res);
                break;

                case FLAGS_SUB8: case FLAGS_DEC8:
                if (do_unroll)
                        jump_uop = uop_CMP_JL_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
                else
                        jump_uop = uop_CMP_JNL_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
                break;
                case FLAGS_SUB16: case FLAGS_DEC16:
                if (do_unroll)
                        jump_uop = uop_CMP_JL_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
                else
                        jump_uop = uop_CMP_JNL_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
                break;
                case FLAGS_SUB32: case FLAGS_DEC32:
                if (do_unroll)
                        jump_uop = uop_CMP_JL_DEST(ir, IREG_flags_op1, IREG_flags_op2);
                else
                        jump_uop = uop_CMP_JNL_DEST(ir, IREG_flags_op1, IREG_flags_op2);
                break;

                case FLAGS_UNKNOWN:
                default:
                uop_CALL_FUNC_RESULT(ir, IREG_temp0, NF_SET_01);
                uop_CALL_FUNC_RESULT(ir, IREG_temp1, VF_SET_01);
                if (do_unroll)
                        jump_uop = uop_CMP_JNZ_DEST(ir, IREG_temp0, IREG_temp1);
                else
                        jump_uop = uop_CMP_JZ_DEST(ir, IREG_temp0, IREG_temp1);
                break;
        }
        if (do_unroll)
                uop_MOV_IMM(ir, IREG_pc, next_pc);
        else
                uop_MOV_IMM(ir, IREG_pc, dest_addr);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);
        return do_unroll ? 1 : 0;
}
static int ropJNL_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
        int jump_uop;
        int do_unroll = ((NF_SET() ? 1 : 0) == (VF_SET() ? 1 : 0) && codegen_can_unroll(block, ir, next_pc, dest_addr));

        switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN)
        {
                case FLAGS_ZN8:
                /*V flag is always clear. Condition is true if N is set*/
                if (do_unroll)
                        jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res_B);
                else
                        jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res_B);
                break;
                case FLAGS_ZN16:
                if (do_unroll)
                        jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res_W);
                else
                        jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res_W);
                break;
                case FLAGS_ZN32:
                if (do_unroll)
                        jump_uop = uop_TEST_JNS_DEST(ir, IREG_flags_res);
                else
                        jump_uop = uop_TEST_JS_DEST(ir, IREG_flags_res);
                break;

                case FLAGS_SUB8: case FLAGS_DEC8:
                if (do_unroll)
                        jump_uop = uop_CMP_JNL_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
                else
                        jump_uop = uop_CMP_JL_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
                break;
                case FLAGS_SUB16: case FLAGS_DEC16:
                if (do_unroll)
                        jump_uop = uop_CMP_JNL_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
                else
                        jump_uop = uop_CMP_JL_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
                break;
                case FLAGS_SUB32: case FLAGS_DEC32:
                if (do_unroll)
                        jump_uop = uop_CMP_JNL_DEST(ir, IREG_flags_op1, IREG_flags_op2);
                else
                        jump_uop = uop_CMP_JL_DEST(ir, IREG_flags_op1, IREG_flags_op2);
                break;

                case FLAGS_UNKNOWN:
                default:
                uop_CALL_FUNC_RESULT(ir, IREG_temp0, NF_SET_01);
                uop_CALL_FUNC_RESULT(ir, IREG_temp1, VF_SET_01);
                if (do_unroll)
                        jump_uop = uop_CMP_JZ_DEST(ir, IREG_temp0, IREG_temp1);
                else
                        jump_uop = uop_CMP_JNZ_DEST(ir, IREG_temp0, IREG_temp1);
                break;
        }
        if (do_unroll)
                uop_MOV_IMM(ir, IREG_pc, next_pc);
        else
                uop_MOV_IMM(ir, IREG_pc, dest_addr);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);
        return do_unroll ? 1 : 0;
}

static int ropJLE_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
        int jump_uop, jump_uop2 = -1;
        int do_unroll = (((NF_SET() ? 1 : 0) != (VF_SET() ? 1 : 0) || ZF_SET()) && codegen_can_unroll(block, ir, next_pc, dest_addr));

        switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN)
        {
                case FLAGS_SUB8: case FLAGS_DEC8:
                if (do_unroll)
                        jump_uop = uop_CMP_JLE_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
                else
                        jump_uop = uop_CMP_JNLE_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
                break;
                case FLAGS_SUB16: case FLAGS_DEC16:
                if (do_unroll)
                        jump_uop = uop_CMP_JLE_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
                else
                        jump_uop = uop_CMP_JNLE_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
                break;
                case FLAGS_SUB32: case FLAGS_DEC32:
                if (do_unroll)
                        jump_uop = uop_CMP_JLE_DEST(ir, IREG_flags_op1, IREG_flags_op2);
                else
                        jump_uop = uop_CMP_JNLE_DEST(ir, IREG_flags_op1, IREG_flags_op2);
                break;

                case FLAGS_UNKNOWN:
                default:
                if (do_unroll)
                {
                        uop_CALL_FUNC_RESULT(ir, IREG_temp0, ZF_SET);
                        jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                        uop_CALL_FUNC_RESULT(ir, IREG_temp0, NF_SET_01);
                        uop_CALL_FUNC_RESULT(ir, IREG_temp1, VF_SET_01);
                        jump_uop = uop_CMP_JNZ_DEST(ir, IREG_temp0, IREG_temp1);
                }
                else
                {
                        uop_CALL_FUNC_RESULT(ir, IREG_temp0, ZF_SET);
                        jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                        uop_CALL_FUNC_RESULT(ir, IREG_temp0, NF_SET_01);
                        uop_CALL_FUNC_RESULT(ir, IREG_temp1, VF_SET_01);
                        jump_uop = uop_CMP_JZ_DEST(ir, IREG_temp0, IREG_temp1);
                }
                break;
        }
        if (do_unroll)
        {
                uop_MOV_IMM(ir, IREG_pc, next_pc);
                uop_JMP(ir, codegen_exit_rout);
                uop_set_jump_dest(ir, jump_uop);
                if (jump_uop2 != -1)
                        uop_set_jump_dest(ir, jump_uop2);
                return 1;
        }
        else
        {
                if (jump_uop2 != -1)
                        uop_set_jump_dest(ir, jump_uop2);
                uop_MOV_IMM(ir, IREG_pc, dest_addr);
                uop_JMP(ir, codegen_exit_rout);
                uop_set_jump_dest(ir, jump_uop);
                return 0;
        }
}
static int ropJNLE_common(codeblock_t *block, ir_data_t *ir, uint32_t dest_addr, uint32_t next_pc)
{
        int jump_uop, jump_uop2 = -1;
        int do_unroll = ((NF_SET() ? 1 : 0) == (VF_SET() ? 1 : 0) && !ZF_SET() && codegen_can_unroll(block, ir, next_pc, dest_addr));

        switch (codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN)
        {
                case FLAGS_SUB8: case FLAGS_DEC8:
                if (do_unroll)
                        jump_uop = uop_CMP_JNLE_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
                else
                        jump_uop = uop_CMP_JLE_DEST(ir, IREG_flags_op1_B, IREG_flags_op2_B);
                break;
                case FLAGS_SUB16: case FLAGS_DEC16:
                if (do_unroll)
                        jump_uop = uop_CMP_JNLE_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
                else
                        jump_uop = uop_CMP_JLE_DEST(ir, IREG_flags_op1_W, IREG_flags_op2_W);
                break;
                case FLAGS_SUB32: case FLAGS_DEC32:
                if (do_unroll)
                        jump_uop = uop_CMP_JNLE_DEST(ir, IREG_flags_op1, IREG_flags_op2);
                else
                        jump_uop = uop_CMP_JLE_DEST(ir, IREG_flags_op1, IREG_flags_op2);
                break;

                case FLAGS_UNKNOWN:
                default:
                if (do_unroll)
                {
                        uop_CALL_FUNC_RESULT(ir, IREG_temp0, ZF_SET);
                        jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                        uop_CALL_FUNC_RESULT(ir, IREG_temp0, NF_SET_01);
                        uop_CALL_FUNC_RESULT(ir, IREG_temp1, VF_SET_01);
                        jump_uop = uop_CMP_JZ_DEST(ir, IREG_temp0, IREG_temp1);
                }
                else
                {
                        uop_CALL_FUNC_RESULT(ir, IREG_temp0, ZF_SET);
                        jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
                        uop_CALL_FUNC_RESULT(ir, IREG_temp0, NF_SET_01);
                        uop_CALL_FUNC_RESULT(ir, IREG_temp1, VF_SET_01);
                        jump_uop = uop_CMP_JNZ_DEST(ir, IREG_temp0, IREG_temp1);
                }
                break;
        }
        if (do_unroll)
        {
                if (jump_uop2 != -1)
                        uop_set_jump_dest(ir, jump_uop2);
                uop_MOV_IMM(ir, IREG_pc, next_pc);
                uop_JMP(ir, codegen_exit_rout);
                uop_set_jump_dest(ir, jump_uop);
                return 1;
        }
        else
        {
                uop_MOV_IMM(ir, IREG_pc, dest_addr);
                uop_JMP(ir, codegen_exit_rout);
                uop_set_jump_dest(ir, jump_uop);
                if (jump_uop2 != -1)
                        uop_set_jump_dest(ir, jump_uop2);
                return 0;
        }
}

#define ropJ(cond)                                                                                                                      \
uint32_t ropJ ## cond ## _8(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)       \
{                                                                                                                                       \
	uint32_t offset = (int32_t)(int8_t)fastreadb(cs + op_pc);                                                                       \
	uint32_t dest_addr = op_pc + 1 + offset;                                                                                        \
	int ret;                                                                                                                        \
                                                                                                                                        \
	if (!(op_32 & 0x100))                                                                                                           \
                dest_addr &= 0xffff;                                                                                                    \
	ret = ropJ ## cond ## _common(block, ir, dest_addr, op_pc+1);                                                                   \
                                                                                                                                        \
        codegen_mark_code_present(block, cs+op_pc, 1);                                                                                  \
	return ret ? dest_addr : (op_pc+1);                                                                                             \
}                                                                                                                                       \
uint32_t ropJ ## cond ## _16(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)      \
{                                                                                                                                       \
	uint32_t offset = (int32_t)(int16_t)fastreadw(cs + op_pc);                                                                      \
	uint32_t dest_addr = (op_pc + 2 + offset) & 0xffff;                                                                             \
	int ret;                                                                                                                        \
                                                                                                                                        \
        ret = ropJ ## cond ## _common(block, ir, dest_addr, op_pc+2);                                                                   \
                                                                                                                                        \
        codegen_mark_code_present(block, cs+op_pc, 2);                                                                                  \
	return ret ? dest_addr : (op_pc+2);                                                                                             \
}                                                                                                                                       \
uint32_t ropJ ## cond ## _32(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)      \
{                                                                                                                                       \
	uint32_t offset = fastreadl(cs + op_pc);                                                                                        \
	uint32_t dest_addr = op_pc + 4 + offset;                                                                                        \
	int ret;                                                                                                                        \
                                                                                                                                        \
        ret = ropJ ## cond ## _common(block, ir, dest_addr, op_pc+4);                                                                   \
                                                                                                                                        \
        codegen_mark_code_present(block, cs+op_pc, 4);                                                                                  \
	return ret ? dest_addr : (op_pc+4);                                                                                             \
}

ropJ(O)
ropJ(NO)
ropJ(B)
ropJ(NB)
ropJ(E)
ropJ(NE)
ropJ(BE)
ropJ(NBE)
ropJ(S)
ropJ(NS)
ropJ(P)
ropJ(NP)
ropJ(L)
ropJ(NL)
ropJ(LE)
ropJ(NLE)


uint32_t ropJCXZ(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uint32_t offset = (int32_t)(int8_t)fastreadb(cs + op_pc);
        uint32_t dest_addr = op_pc + 1 + offset;
        int jump_uop;

	if (!(op_32 & 0x100))
                dest_addr &= 0xffff;

        if (op_32 & 0x200)
                jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_ECX, 0);
        else
                jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_CX, 0);
        uop_MOV_IMM(ir, IREG_pc, dest_addr);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);

        codegen_mark_code_present(block, cs+op_pc, 1);
        return op_pc+1;
}

uint32_t ropLOOP(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uint32_t offset = (int32_t)(int8_t)fastreadb(cs + op_pc);
        uint32_t dest_addr = op_pc + 1 + offset;
        uint32_t ret_addr;
        int jump_uop;

	if (!(op_32 & 0x100))
                dest_addr &= 0xffff;

        if (((op_32 & 0x200) ? ECX : CX) != 1 && codegen_can_unroll(block, ir, op_pc+1, dest_addr))
        {
                if (op_32 & 0x200)
                {
                        uop_SUB_IMM(ir, IREG_ECX, IREG_ECX, 1);
                        jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_ECX, 0);
                }
                else
                {
                        uop_SUB_IMM(ir, IREG_CX, IREG_CX, 1);
                        jump_uop = uop_CMP_IMM_JNZ_DEST(ir, IREG_CX, 0);
                }
                uop_MOV_IMM(ir, IREG_pc, op_pc+1);
                ret_addr = dest_addr;
                CPU_BLOCK_END();
        }
        else
        {
                if (op_32 & 0x200)
                {
                        uop_SUB_IMM(ir, IREG_ECX, IREG_ECX, 1);
                        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_ECX, 0);
                }
                else
                {
                        uop_SUB_IMM(ir, IREG_CX, IREG_CX, 1);
                        jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_CX, 0);
                }
                uop_MOV_IMM(ir, IREG_pc, dest_addr);
                ret_addr = op_pc+1;
        }
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump_uop);

        codegen_mark_code_present(block, cs+op_pc, 1);
        return ret_addr;
}

uint32_t ropLOOPE(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uint32_t offset = (int32_t)(int8_t)fastreadb(cs + op_pc);
        uint32_t dest_addr = op_pc + 1 + offset;
        int jump_uop, jump_uop2;

	if (!(op_32 & 0x100))
                dest_addr &= 0xffff;

        if (op_32 & 0x200)
        {
                uop_SUB_IMM(ir, IREG_ECX, IREG_ECX, 1);
                jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_ECX, 0);
        }
        else
        {
                uop_SUB_IMM(ir, IREG_CX, IREG_CX, 1);
                jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_CX, 0);
        }
        if (!codegen_flags_changed || !flags_res_valid())
        {
                uop_CALL_FUNC_RESULT(ir, IREG_temp0, ZF_SET);
                jump_uop2 = uop_CMP_IMM_JZ_DEST(ir, IREG_temp0, 0);
        }
        else
        {
                jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_flags_res, 0);
        }
        uop_MOV_IMM(ir, IREG_pc, dest_addr);
        uop_JMP(ir, codegen_exit_rout);
        uop_NOP_BARRIER(ir);
        uop_set_jump_dest(ir, jump_uop);
        uop_set_jump_dest(ir, jump_uop2);

        codegen_mark_code_present(block, cs+op_pc, 1);
        return op_pc+1;
}
uint32_t ropLOOPNE(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
        uint32_t offset = (int32_t)(int8_t)fastreadb(cs + op_pc);
        uint32_t dest_addr = op_pc + 1 + offset;
        int jump_uop, jump_uop2;

	if (!(op_32 & 0x100))
                dest_addr &= 0xffff;

        if (op_32 & 0x200)
        {
                uop_SUB_IMM(ir, IREG_ECX, IREG_ECX, 1);
                jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_ECX, 0);
        }
        else
        {
                uop_SUB_IMM(ir, IREG_CX, IREG_CX, 1);
                jump_uop = uop_CMP_IMM_JZ_DEST(ir, IREG_CX, 0);
        }
        if (!codegen_flags_changed || !flags_res_valid())
        {
                uop_CALL_FUNC_RESULT(ir, IREG_temp0, ZF_SET);
                jump_uop2 = uop_CMP_IMM_JNZ_DEST(ir, IREG_temp0, 0);
        }
        else
        {
                jump_uop2 = uop_CMP_IMM_JZ_DEST(ir, IREG_flags_res, 0);
        }
        uop_MOV_IMM(ir, IREG_pc, dest_addr);
        uop_JMP(ir, codegen_exit_rout);
        uop_NOP_BARRIER(ir);
        uop_set_jump_dest(ir, jump_uop);
        uop_set_jump_dest(ir, jump_uop2);

        codegen_mark_code_present(block, cs+op_pc, 1);
        return op_pc+1;
}
