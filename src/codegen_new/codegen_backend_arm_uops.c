#if defined __ARM_EABI__ || defined _ARM_ || defined _M_ARM

#include <math.h>
#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

#include "x86.h"
#include "x87.h"
#include "386_common.h"
#include "codegen.h"
#include "codegen_allocator.h"
#include "codegen_backend.h"
#include "codegen_backend_arm_defs.h"
#include "codegen_backend_arm_ops.h"
#include "codegen_ir_defs.h"

static inline int get_arm_imm(uint32_t imm_data, uint32_t *arm_imm)
{
	int shift = 0;
	if (!(imm_data & 0xffff))
	{
		shift += 16;
		imm_data >>= 16;
	}
	if (!(imm_data & 0xff))
	{
		shift += 8;
		imm_data >>= 8;
	}
	if (!(imm_data & 0xf))
	{
		shift += 4;
		imm_data >>= 4;
	}
	if (!(imm_data & 0x3))
	{
		shift += 2;
		imm_data >>= 2;
	}
	if (imm_data > 0xff) /*Note - should handle rotation round the word*/
		return 0;
	*arm_imm = imm_data | ((((32 - shift) >> 1) & 15) << 8);
	return 1;
}

static inline int in_range(void *addr, void *base)
{
	int diff = (uintptr_t)addr - (uintptr_t)base;

	if (diff < -4095 || diff > 4095)
		return 0;
	return 1;
}

static inline int in_range_h(void *addr, void *base)
{
	int diff = (uintptr_t)addr - (uintptr_t)base;

	if (diff < 0 || diff > 255)
		return 0;
	return 1;
}

void host_arm_call(codeblock_t *block, void *dst_addr)
{
	host_arm_MOV_IMM(block, REG_R3, (uintptr_t)dst_addr);
	host_arm_BLX(block, REG_R3);
}

void host_arm_nop(codeblock_t *block)
{
	host_arm_MOV_REG_LSL(block, REG_R0, REG_R0, 0);
}

#define HOST_REG_GET(reg) (IREG_GET_REG(reg) & 0xf)

#define REG_IS_L(size) (size == IREG_SIZE_L)
#define REG_IS_W(size) (size == IREG_SIZE_W)
#define REG_IS_B(size) (size == IREG_SIZE_B)
#define REG_IS_BH(size) (size == IREG_SIZE_BH)
#define REG_IS_D(size) (size == IREG_SIZE_D)
#define REG_IS_Q(size) (size == IREG_SIZE_Q)

static int codegen_ADD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

	if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
	{
		host_arm_ADD_REG_LSL(block, dest_reg, src_reg_a, src_reg_b, 0);
	}
	else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
	{
		host_arm_ADD_REG(block, REG_TEMP, src_reg_a, src_reg_b);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
	}
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
	{
		host_arm_ADD_REG(block, REG_TEMP, src_reg_a, src_reg_b);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
	}
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_BH(src_size_b) && dest_reg == src_reg_a)
	{
		host_arm_UXTB(block, REG_TEMP, src_reg_b, 8);
		host_arm_UADD8(block, dest_reg, src_reg_a, REG_TEMP);
	}
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_BH(src_size_b))
	{
		host_arm_ADD_REG_LSR(block, REG_TEMP, src_reg_a, src_reg_b, 8);
		host_arm_BFI(block, dest_reg, REG_TEMP, 8, 8);
	}
	else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size_a) && REG_IS_B(src_size_b) && dest_reg == src_reg_a)
	{
		host_arm_UXTB(block, REG_TEMP, src_reg_b, 0);
		host_arm_MOV_REG_LSL(block, REG_TEMP, REG_TEMP, 8);
		host_arm_UADD8(block, dest_reg, src_reg_a, REG_TEMP);
	}
	else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size_a) && REG_IS_BH(src_size_b) && dest_reg == src_reg_a)
	{
		host_arm_AND_IMM(block, REG_TEMP, src_reg_b, 0x0000ff00);
		host_arm_UADD8(block, dest_reg, src_reg_a, REG_TEMP);
	}
	else
                fatal("ADD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_ADD_IMM(codeblock_t *block, uop_t *uop)
{
//	host_arm_ADD_IMM(block, uop->dest_reg_a_real, uop->src_reg_a_real, uop->imm_data);
//        return 0;

        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

	if (REG_IS_L(dest_size) && REG_IS_L(src_size))
	{
		host_arm_ADD_IMM(block, dest_reg, src_reg, uop->imm_data);
	}
	else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
	{
		host_arm_ADD_IMM(block, REG_TEMP, src_reg, uop->imm_data);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
	}
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
	{
		host_arm_ADD_IMM(block, REG_TEMP, src_reg, uop->imm_data);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
	}
	else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size) && src_reg == dest_reg)
	{
		host_arm_MOV_IMM(block, REG_TEMP, uop->imm_data << 8);
		host_arm_UADD8(block, dest_reg, src_reg, REG_TEMP);
	}
	else
                fatal("ADD_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;

}
static int codegen_ADD_LSHIFT(codeblock_t *block, uop_t *uop)
{
	host_arm_ADD_REG_LSL(block, uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real, uop->imm_data);
        return 0;
}

static int codegen_AND(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

	if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
	{
		host_arm_VAND_D(block, dest_reg, src_reg_a, src_reg_b);
	}
	else if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
	{
		host_arm_AND_REG_LSL(block, dest_reg, src_reg_a, src_reg_b, 0);
	}
	else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b) && dest_reg == src_reg_a)
	{
		host_arm_MVN_REG_LSL(block, REG_TEMP, src_reg_b, 16);
		host_arm_BIC_REG_LSR(block, dest_reg, src_reg_a, REG_TEMP, 16);
	}
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b) && dest_reg == src_reg_a)
	{
		host_arm_MVN_REG_LSL(block, REG_TEMP, src_reg_b, 24);
		host_arm_BIC_REG_LSR(block, dest_reg, src_reg_a, REG_TEMP, 24);
	}
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_BH(src_size_b) && dest_reg == src_reg_a)
	{
		host_arm_MVN_REG_LSL(block, REG_TEMP, src_reg_b, 16);
		host_arm_BIC_REG_LSR(block, dest_reg, src_reg_a, REG_TEMP, 24);
	}
	else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size_a) && REG_IS_B(src_size_b) && dest_reg == src_reg_a)
	{
		host_arm_MVN_REG_LSL(block, REG_TEMP, src_reg_b, 8);
		host_arm_AND_IMM(block, REG_TEMP, REG_TEMP, 0x0000ff00);
		host_arm_BIC_REG_LSL(block, dest_reg, src_reg_a, REG_TEMP, 0);
	}
	else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size_a) && REG_IS_BH(src_size_b) && dest_reg == src_reg_a)
	{
		host_arm_MVN_REG_LSL(block, REG_TEMP, src_reg_b, 0);
		host_arm_AND_IMM(block, REG_TEMP, REG_TEMP, 0x0000ff00);
		host_arm_BIC_REG_LSL(block, dest_reg, src_reg_a, REG_TEMP, 0);
	}
	else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
	{
		host_arm_AND_REG_LSL(block, REG_TEMP, src_reg_a, src_reg_b, 0);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
	}
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
	{
		host_arm_AND_REG_LSL(block, REG_TEMP, src_reg_a, src_reg_b, 0);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
	}
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_BH(src_size_b))
	{
		host_arm_AND_REG_LSR(block, REG_TEMP, src_reg_a, src_reg_b, 8);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
	}
	else if (REG_IS_B(dest_size) && REG_IS_BH(src_size_a) && REG_IS_B(src_size_b))
	{
		host_arm_AND_REG_LSR(block, REG_TEMP, src_reg_b, src_reg_a, 8);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
	}
	else if (REG_IS_B(dest_size) && REG_IS_BH(src_size_a) && REG_IS_BH(src_size_b))
	{
		host_arm_AND_REG_LSL(block, REG_TEMP, src_reg_a, src_reg_b, 0);
		host_arm_MOV_REG_LSR(block, REG_TEMP, REG_TEMP, 8);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
	}
	else
                fatal("AND %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_AND_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

	if (REG_IS_L(dest_size) && REG_IS_L(src_size))
	{
		host_arm_AND_IMM(block, dest_reg, src_reg, uop->imm_data);
	}
	else if (REG_IS_W(dest_size) && REG_IS_W(src_size) && dest_reg == src_reg)
	{
		host_arm_AND_IMM(block, dest_reg, src_reg, uop->imm_data | 0xffff0000);
	}
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size) && dest_reg == src_reg)
	{
		host_arm_AND_IMM(block, dest_reg, src_reg, uop->imm_data | 0xffffff00);
	}
	else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size) && dest_reg == src_reg)
	{
		host_arm_AND_IMM(block, dest_reg, src_reg, (uop->imm_data << 8) | 0xffff00ff);
	}
	else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
	{
		host_arm_AND_IMM(block, REG_TEMP, src_reg, uop->imm_data);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
	}
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
	{
		host_arm_AND_IMM(block, REG_TEMP, src_reg, uop->imm_data);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
	}
	else if (REG_IS_B(dest_size) && REG_IS_BH(src_size))
	{
		host_arm_MOV_REG_LSR(block, REG_TEMP, src_reg, 8);
		host_arm_AND_IMM(block, REG_TEMP, REG_TEMP, uop->imm_data);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
	}
	else
                fatal("AND_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_ANDN(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

	if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
	{
		host_arm_VBIC_D(block, dest_reg, src_reg_b, src_reg_a);
	}
	else
                fatal("ANDN %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}

static int codegen_CALL_FUNC(codeblock_t *block, uop_t *uop)
{
        host_arm_call(block, uop->p);

        return 0;
}

static int codegen_CALL_FUNC_RESULT(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

        if (!REG_IS_L(dest_size))
                fatal("CALL_FUNC_RESULT %02x\n", uop->dest_reg_a_real);
        host_arm_call(block, uop->p);
        host_arm_MOV_REG(block, dest_reg, REG_R0);

        return 0;
}

static int codegen_CALL_INSTRUCTION_FUNC(codeblock_t *block, uop_t *uop)
{
	host_arm_call(block, uop->p);
	host_arm_TST_REG(block, REG_R0, REG_R0);
	host_arm_BNE(block, (uintptr_t)codegen_exit_rout);

        return 0;
}

static int codegen_CMP_IMM_JZ(codeblock_t *block, uop_t *uop)
{
        int src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(src_size))
        {
                host_arm_CMP_IMM(block, src_reg, uop->imm_data);
        }
        else
                fatal("CMP_IMM_JZ %02x\n", uop->src_reg_a_real);
        host_arm_BEQ(block, (uintptr_t)uop->p);

        return 0;
}

static int codegen_CMP_IMM_JNZ_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(src_size))
        {
                host_arm_CMP_IMM(block, src_reg, uop->imm_data);
        }
        else if (REG_IS_W(src_size))
        {
		host_arm_UXTH(block, REG_TEMP, src_reg, 0);
                host_arm_CMP_IMM(block, REG_TEMP, uop->imm_data);
        }
        else
                fatal("CMP_IMM_JNZ_DEST %02x\n", uop->src_reg_a_real);

        uop->p = host_arm_BNE_(block);

        return 0;
}
static int codegen_CMP_IMM_JZ_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(src_size))
        {
                host_arm_CMP_IMM(block, src_reg, uop->imm_data);
        }
        else if (REG_IS_W(src_size))
        {
		host_arm_UXTH(block, REG_TEMP, src_reg, 0);
                host_arm_CMP_IMM(block, REG_TEMP, uop->imm_data);
        }
        else
                fatal("CMP_IMM_JZ_DEST %02x\n", uop->src_reg_a_real);

        uop->p = host_arm_BEQ_(block);

        return 0;
}

static int codegen_CMP_JB(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);
        uint32_t *jump_p;

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
		host_arm_CMP_REG(block, src_reg_a, src_reg_b);
        }
        else
                fatal("CMP_JB %02x\n", uop->src_reg_a_real);

        jump_p = host_arm_BCC_(block);
        *jump_p |= ((((uintptr_t)uop->p - (uintptr_t)jump_p) - 8) & 0x3fffffc) >> 2;

        return 0;
}
static int codegen_CMP_JNBE(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);
        uint32_t *jump_p;

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
		host_arm_CMP_REG(block, src_reg_a, src_reg_b);
        }
        else
                fatal("CMP_JNBE %02x\n", uop->src_reg_a_real);

        jump_p = host_arm_BHI_(block);
        *jump_p |= ((((uintptr_t)uop->p - (uintptr_t)jump_p) - 8) & 0x3fffffc) >> 2;
        
        return 0;
}

static int codegen_CMP_JNB_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
		host_arm_CMP_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 16);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 16);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 24);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 24);
        }
        else
                fatal("CMP_JNB_DEST %02x\n", uop->src_reg_a_real);

        uop->p = host_arm_BCS_(block);

        return 0;
}
static int codegen_CMP_JNBE_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
		host_arm_CMP_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 16);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 16);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 24);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 24);
        }
        else
                fatal("CMP_JNBE_DEST %02x\n", uop->src_reg_a_real);

        uop->p = host_arm_BHI_(block);

        return 0;
}
static int codegen_CMP_JNL_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
		host_arm_CMP_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 16);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 16);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 24);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 24);
        }
        else
                fatal("CMP_JNL_DEST %02x\n", uop->src_reg_a_real);

        uop->p = host_arm_BGE_(block);

        return 0;
}
static int codegen_CMP_JNLE_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
		host_arm_CMP_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 16);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 16);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 24);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 24);
        }
        else
                fatal("CMP_JNLE_DEST %02x\n", uop->src_reg_a_real);

        uop->p = host_arm_BGT_(block);

        return 0;
}
static int codegen_CMP_JNO_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
		host_arm_CMP_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 16);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 16);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 24);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 24);
        }
        else
                fatal("CMP_JNO_DEST %02x\n", uop->src_reg_a_real);

        uop->p = host_arm_BVC_(block);

        return 0;
}
static int codegen_CMP_JNZ_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
		host_arm_CMP_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 16);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 16);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 24);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 24);
        }
        else
                fatal("CMP_JNZ_DEST %02x\n", uop->src_reg_a_real);

        uop->p = host_arm_BNE_(block);

        return 0;
}
static int codegen_CMP_JB_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
		host_arm_CMP_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 16);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 16);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 24);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 24);
        }
        else
                fatal("CMP_JB_DEST %02x\n", uop->src_reg_a_real);

        uop->p = host_arm_BCC_(block);

        return 0;
}
static int codegen_CMP_JBE_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
		host_arm_CMP_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 16);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 16);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 24);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 24);
        }
        else
                fatal("CMP_JBE_DEST %02x\n", uop->src_reg_a_real);

        uop->p = host_arm_BLS_(block);

        return 0;
}
static int codegen_CMP_JL_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
		host_arm_CMP_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 16);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 16);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 24);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 24);
        }
        else
                fatal("CMP_JL_DEST %02x\n", uop->src_reg_a_real);

        uop->p = host_arm_BLT_(block);

        return 0;
}
static int codegen_CMP_JLE_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
		host_arm_CMP_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 16);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 16);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 24);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 24);
        }
        else
                fatal("CMP_JLE_DEST %02x\n", uop->src_reg_a_real);

        uop->p = host_arm_BLE_(block);

        return 0;
}
static int codegen_CMP_JO_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
		host_arm_CMP_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 16);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 16);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 24);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 24);
        }
        else
                fatal("CMP_JO_DEST %02x\n", uop->src_reg_a_real);

        uop->p = host_arm_BVS_(block);

        return 0;
}
static int codegen_CMP_JZ_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
        {
		host_arm_CMP_REG(block, src_reg_a, src_reg_b);
        }
        else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 16);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 16);
        }
        else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg_a, 24);
                host_arm_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 24);
        }
        else
                fatal("CMP_JZ_DEST %02x\n", uop->src_reg_a_real);

        uop->p = host_arm_BEQ_(block);

        return 0;
}

static int codegen_FABS(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_D(dest_size) && REG_IS_D(src_size_a))
        {
                host_arm_VABS_D(block, dest_reg, src_reg_a);
        }
        else
                fatal("codegen_FABS %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_FCHS(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_D(dest_size) && REG_IS_D(src_size_a))
        {
                host_arm_VNEG_D(block, dest_reg, src_reg_a);
        }
        else
                fatal("codegen_FCHS %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_FSQRT(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_D(dest_size) && REG_IS_D(src_size_a))
        {
                host_arm_VSQRT_D(block, dest_reg, src_reg_a);
        }
        else
                fatal("codegen_FSQRT %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_FTST(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_W(dest_size) && REG_IS_D(src_size_a))
        {
		host_arm_VSUB_D(block, REG_D_TEMP, REG_D_TEMP, REG_D_TEMP);
		host_arm_VCMP_D(block, src_reg_a, REG_D_TEMP);
		host_arm_MOV_IMM(block, dest_reg, 0);
		host_arm_VMRS_APSR(block);
		host_arm_ORREQ_IMM(block, dest_reg, dest_reg, C3);
		host_arm_ORRCC_IMM(block, dest_reg, dest_reg, C0);
		host_arm_ORRVS_IMM(block, dest_reg, dest_reg, C0|C2|C3);
        }
        else
                fatal("codegen_FTST %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}

static int codegen_FADD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b))
        {
                host_arm_VADD_D(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("codegen_FADD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_FCOM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_W(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b))
        {
		host_arm_VCMP_D(block, src_reg_a, src_reg_b);
		host_arm_MOV_IMM(block, dest_reg, 0);
		host_arm_VMRS_APSR(block);
		host_arm_ORREQ_IMM(block, dest_reg, dest_reg, C3);
		host_arm_ORRCC_IMM(block, dest_reg, dest_reg, C0);
		host_arm_ORRVS_IMM(block, dest_reg, dest_reg, C0|C2|C3);
        }
        else
                fatal("codegen_FCOM %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_FDIV(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b))
        {
                host_arm_VDIV_D(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("codegen_FDIV %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_FMUL(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b))
        {
                host_arm_VMUL_D(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("codegen_FMUL %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_FSUB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b))
        {
                host_arm_VSUB_D(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("codegen_FSUB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}

static int codegen_FP_ENTER(codeblock_t *block, uop_t *uop)
{
        uint32_t *branch_ptr;

	if (!in_range(&cr0, &cpu_state))
		fatal("codegen_FP_ENTER - out of range\n");

        host_arm_LDR_IMM(block, REG_TEMP, REG_CPUSTATE, (uintptr_t)&cr0 - (uintptr_t)&cpu_state);
        host_arm_TST_IMM(block, REG_TEMP, 0xc);
        branch_ptr = host_arm_BEQ_(block);

	host_arm_MOV_IMM(block, REG_TEMP, uop->imm_data);
	host_arm_STR_IMM(block, REG_TEMP, REG_CPUSTATE, (uintptr_t)&cpu_state.oldpc - (uintptr_t)&cpu_state);
        host_arm_MOV_IMM(block, REG_ARG0, 7);
	host_arm_call(block, x86_int);
        host_arm_B(block, (uintptr_t)codegen_exit_rout);

	*branch_ptr |= ((((uintptr_t)&block_write_data[block_pos] - (uintptr_t)branch_ptr) - 8) & 0x3fffffc) >> 2;

        return 0;
}
static int codegen_MMX_ENTER(codeblock_t *block, uop_t *uop)
{
        uint32_t *branch_ptr;

	if (!in_range(&cr0, &cpu_state))
		fatal("codegen_MMX_ENTER - out of range\n");

        host_arm_LDR_IMM(block, REG_TEMP, REG_CPUSTATE, (uintptr_t)&cr0 - (uintptr_t)&cpu_state);
        host_arm_TST_IMM(block, REG_TEMP, 0xc);
        branch_ptr = host_arm_BEQ_(block);

	host_arm_MOV_IMM(block, REG_TEMP, uop->imm_data);
	host_arm_STR_IMM(block, REG_TEMP, REG_CPUSTATE, (uintptr_t)&cpu_state.oldpc - (uintptr_t)&cpu_state);
        host_arm_MOV_IMM(block, REG_ARG0, 7);
	host_arm_call(block, x86_int);
        host_arm_B(block, (uintptr_t)codegen_exit_rout);

	*branch_ptr |= ((((uintptr_t)&block_write_data[block_pos] - (uintptr_t)branch_ptr) - 8) & 0x3fffffc) >> 2;

        host_arm_MOV_IMM(block, REG_TEMP, 0x01010101);
	host_arm_STR_IMM(block, REG_TEMP, REG_CPUSTATE, (uintptr_t)&cpu_state.tag[0] - (uintptr_t)&cpu_state);
	host_arm_STR_IMM(block, REG_TEMP, REG_CPUSTATE, (uintptr_t)&cpu_state.tag[4] - (uintptr_t)&cpu_state);
        host_arm_MOV_IMM(block, REG_TEMP, 0);
	host_arm_STR_IMM(block, REG_TEMP, REG_CPUSTATE, (uintptr_t)&cpu_state.TOP - (uintptr_t)&cpu_state);
	host_arm_STRB_IMM(block, REG_TEMP, REG_CPUSTATE, (uintptr_t)&cpu_state.ismmx - (uintptr_t)&cpu_state);

        return 0;
}

static int codegen_JMP(codeblock_t *block, uop_t *uop)
{
        host_arm_B(block, (uintptr_t)uop->p);

        return 0;
}

static int codegen_LOAD_FUNC_ARG0(codeblock_t *block, uop_t *uop)
{
        int src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_W(src_size))
        {
		host_arm_UXTH(block, REG_ARG0, src_reg, 0);
        }
        else
                fatal("codegen_LOAD_FUNC_ARG0 %02x\n", uop->src_reg_a_real);

        return 0;
}
static int codegen_LOAD_FUNC_ARG1(codeblock_t *block, uop_t *uop)
{
        fatal("codegen_LOAD_FUNC_ARG1 %02x\n", uop->src_reg_a_real);
        return 0;
}
static int codegen_LOAD_FUNC_ARG2(codeblock_t *block, uop_t *uop)
{
        fatal("codegen_LOAD_FUNC_ARG2 %02x\n", uop->src_reg_a_real);
        return 0;
}
static int codegen_LOAD_FUNC_ARG3(codeblock_t *block, uop_t *uop)
{
        fatal("codegen_LOAD_FUNC_ARG3 %02x\n", uop->src_reg_a_real);
        return 0;
}

static int codegen_LOAD_FUNC_ARG0_IMM(codeblock_t *block, uop_t *uop)
{
	host_arm_MOV_IMM(block, REG_ARG0, uop->imm_data);
        return 0;
}
static int codegen_LOAD_FUNC_ARG1_IMM(codeblock_t *block, uop_t *uop)
{
	host_arm_MOV_IMM(block, REG_ARG1, uop->imm_data);
        return 0;
}
static int codegen_LOAD_FUNC_ARG2_IMM(codeblock_t *block, uop_t *uop)
{
	host_arm_MOV_IMM(block, REG_ARG2, uop->imm_data);
        return 0;
}
static int codegen_LOAD_FUNC_ARG3_IMM(codeblock_t *block, uop_t *uop)
{
	host_arm_MOV_IMM(block, REG_ARG3, uop->imm_data);
        return 0;
}

static int codegen_LOAD_SEG(codeblock_t *block, uop_t *uop)
{
        int src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (!REG_IS_W(src_size))
                fatal("LOAD_SEG %02x %p\n", uop->src_reg_a_real, uop->p);
	host_arm_UXTH(block, REG_ARG0, src_reg, 0);
        host_arm_MOV_IMM(block, REG_ARG1, (uint32_t)uop->p);
	host_arm_call(block, loadseg);
	host_arm_TST_REG(block, REG_R0, REG_R0);
	host_arm_BNE(block, (uintptr_t)codegen_exit_rout);

        return 0;
}

static int codegen_MEM_LOAD_ABS(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), seg_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

	host_arm_ADD_IMM(block, REG_R0, seg_reg, uop->imm_data);
        if (REG_IS_B(dest_size) || REG_IS_BH(dest_size))
        {
                host_arm_BL(block, (uintptr_t)codegen_mem_load_byte);
        }
        else if (REG_IS_W(dest_size))
        {
                host_arm_BL(block, (uintptr_t)codegen_mem_load_word);
        }
        else if (REG_IS_L(dest_size))
        {
                host_arm_BL(block, (uintptr_t)codegen_mem_load_long);
        }
        else
                fatal("MEM_LOAD_ABS - %02x\n", uop->dest_reg_a_real);
        host_arm_TST_REG(block, REG_R1, REG_R1);
        host_arm_BNE(block, (uintptr_t)codegen_exit_rout);
        if (REG_IS_B(dest_size))
        {
		host_arm_BFI(block, dest_reg, REG_R0, 0, 8);
        }
	else if (REG_IS_BH(dest_size))
        {
		host_arm_BFI(block, dest_reg, REG_R0, 8, 8);
        }
        else if (REG_IS_W(dest_size))
        {
		host_arm_BFI(block, dest_reg, REG_R0, 0, 16);
        }
        else if (REG_IS_L(dest_size))
        {
                host_arm_MOV_REG(block, dest_reg, REG_R0);
        }

        return 0;
}

static int codegen_MEM_LOAD_REG(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), seg_reg = HOST_REG_GET(uop->src_reg_a_real), addr_reg = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

	host_arm_ADD_REG(block, REG_R0, seg_reg, addr_reg);
        if (uop->imm_data)
                host_arm_ADD_IMM(block, REG_R0, REG_R0, uop->imm_data);
        if (REG_IS_B(dest_size) || REG_IS_BH(dest_size))
        {
                host_arm_BL(block, (uintptr_t)codegen_mem_load_byte);
        }
        else if (REG_IS_W(dest_size))
        {
                host_arm_BL(block, (uintptr_t)codegen_mem_load_word);
        }
        else if (REG_IS_L(dest_size))
        {
                host_arm_BL(block, (uintptr_t)codegen_mem_load_long);
        }
        else if (REG_IS_Q(dest_size))
        {
                host_arm_BL(block, (uintptr_t)codegen_mem_load_quad);
        }
        else
                fatal("MEM_LOAD_REG - %02x\n", uop->dest_reg_a_real);
        host_arm_TST_REG(block, REG_R1, REG_R1);
        host_arm_BNE(block, (uintptr_t)codegen_exit_rout);
        if (REG_IS_B(dest_size))
        {
		host_arm_BFI(block, dest_reg, REG_R0, 0, 8);
        }
	else if (REG_IS_BH(dest_size))
        {
		host_arm_BFI(block, dest_reg, REG_R0, 8, 8);
        }
        else if (REG_IS_W(dest_size))
        {
		host_arm_BFI(block, dest_reg, REG_R0, 0, 16);
        }
        else if (REG_IS_L(dest_size))
        {
                host_arm_MOV_REG(block, dest_reg, REG_R0);
        }
        else if (REG_IS_Q(dest_size))
        {
                host_arm_VMOV_D_D(block, dest_reg, REG_D_TEMP);
        }

        return 0;
}
static int codegen_MEM_LOAD_SINGLE(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), seg_reg = HOST_REG_GET(uop->src_reg_a_real), addr_reg = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

	if (!REG_IS_D(dest_size))
                fatal("MEM_LOAD_SINGLE - %02x\n", uop->dest_reg_a_real);

	host_arm_ADD_REG(block, REG_R0, seg_reg, addr_reg);
        if (uop->imm_data)
                host_arm_ADD_IMM(block, REG_R0, REG_R0, uop->imm_data);
        host_arm_BL(block, (uintptr_t)codegen_mem_load_single);
        host_arm_TST_REG(block, REG_R1, REG_R1);
        host_arm_BNE(block, (uintptr_t)codegen_exit_rout);
	host_arm_VCVT_D_S(block, dest_reg, REG_D_TEMP);

        return 0;
}
static int codegen_MEM_LOAD_DOUBLE(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), seg_reg = HOST_REG_GET(uop->src_reg_a_real), addr_reg = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

	if (!REG_IS_D(dest_size))
                fatal("MEM_LOAD_DOUBLE - %02x\n", uop->dest_reg_a_real);

	host_arm_ADD_REG(block, REG_R0, seg_reg, addr_reg);
        if (uop->imm_data)
                host_arm_ADD_IMM(block, REG_R0, REG_R0, uop->imm_data);
        host_arm_BL(block, (uintptr_t)codegen_mem_load_double);
        host_arm_TST_REG(block, REG_R1, REG_R1);
        host_arm_BNE(block, (uintptr_t)codegen_exit_rout);
	host_arm_VMOV_D_D(block, dest_reg, REG_D_TEMP);

        return 0;
}

static int codegen_MEM_STORE_ABS(codeblock_t *block, uop_t *uop)
{
        int seg_reg = HOST_REG_GET(uop->src_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_b_real);
        int src_size = IREG_GET_SIZE(uop->src_reg_b_real);

	host_arm_ADD_IMM(block, REG_R0, seg_reg, uop->imm_data);
        if (REG_IS_B(src_size))
        {
                host_arm_MOV_REG(block, REG_R1, src_reg);
                host_arm_BL(block, (uintptr_t)codegen_mem_store_byte);
        }
        else if (REG_IS_W(src_size))
        {
                host_arm_MOV_REG(block, REG_R1, src_reg);
                host_arm_BL(block, (uintptr_t)codegen_mem_store_word);
        }
        else if (REG_IS_L(src_size))
        {
                host_arm_MOV_REG(block, REG_R1, src_reg);
                host_arm_BL(block, (uintptr_t)codegen_mem_store_long);
        }
        else
                fatal("MEM_STORE_ABS - %02x\n", uop->src_reg_b_real);
        host_arm_TST_REG(block, REG_R1, REG_R1);
        host_arm_BNE(block, (uintptr_t)codegen_exit_rout);

        return 0;
}

static int codegen_MEM_STORE_REG(codeblock_t *block, uop_t *uop)
{
        int seg_reg = HOST_REG_GET(uop->src_reg_a_real), addr_reg = HOST_REG_GET(uop->src_reg_b_real), src_reg = HOST_REG_GET(uop->src_reg_c_real);
        int src_size = IREG_GET_SIZE(uop->src_reg_c_real);

	host_arm_ADD_REG(block, REG_R0, seg_reg, addr_reg);
        if (uop->imm_data)
                host_arm_ADD_IMM(block, REG_R0, REG_R0, uop->imm_data);
        if (REG_IS_B(src_size))
        {
                host_arm_MOV_REG(block, REG_R1, src_reg);
                host_arm_BL(block, (uintptr_t)codegen_mem_store_byte);
        }
        else if (REG_IS_BH(src_size))
        {
                host_arm_MOV_REG_LSR(block, REG_R1, src_reg, 8);
                host_arm_BL(block, (uintptr_t)codegen_mem_store_byte);
        }
        else if (REG_IS_W(src_size))
        {
                host_arm_MOV_REG(block, REG_R1, src_reg);
                host_arm_BL(block, (uintptr_t)codegen_mem_store_word);
        }
        else if (REG_IS_L(src_size))
        {
                host_arm_MOV_REG(block, REG_R1, src_reg);
                host_arm_BL(block, (uintptr_t)codegen_mem_store_long);
        }
        else if (REG_IS_Q(src_size))
        {
                host_arm_VMOV_D_D(block, REG_D_TEMP, src_reg);
                host_arm_BL(block, (uintptr_t)codegen_mem_store_quad);
        }
        else
                fatal("MEM_STORE_REG - %02x\n", uop->src_reg_c_real);
        host_arm_TST_REG(block, REG_R1, REG_R1);
        host_arm_BNE(block, (uintptr_t)codegen_exit_rout);

        return 0;
}

static int codegen_MEM_STORE_IMM_8(codeblock_t *block, uop_t *uop)
{
        int seg_reg = HOST_REG_GET(uop->src_reg_a_real), addr_reg = HOST_REG_GET(uop->src_reg_b_real);

	host_arm_ADD_REG(block, REG_R0, seg_reg, addr_reg);
        host_arm_MOV_IMM(block, REG_R1, uop->imm_data);
        host_arm_BL(block, (uintptr_t)codegen_mem_store_byte);
        host_arm_TST_REG(block, REG_R1, REG_R1);
        host_arm_BNE(block, (uintptr_t)codegen_exit_rout);

        return 0;
}
static int codegen_MEM_STORE_IMM_16(codeblock_t *block, uop_t *uop)
{
        int seg_reg = HOST_REG_GET(uop->src_reg_a_real), addr_reg = HOST_REG_GET(uop->src_reg_b_real);

	host_arm_ADD_REG(block, REG_R0, seg_reg, addr_reg);
        host_arm_MOV_IMM(block, REG_R1, uop->imm_data);
        host_arm_BL(block, (uintptr_t)codegen_mem_store_word);
        host_arm_TST_REG(block, REG_R1, REG_R1);
        host_arm_BNE(block, (uintptr_t)codegen_exit_rout);

        return 0;
}
static int codegen_MEM_STORE_IMM_32(codeblock_t *block, uop_t *uop)
{
        int seg_reg = HOST_REG_GET(uop->src_reg_a_real), addr_reg = HOST_REG_GET(uop->src_reg_b_real);

	host_arm_ADD_REG(block, REG_R0, seg_reg, addr_reg);
        host_arm_MOV_IMM(block, REG_R1, uop->imm_data);
        host_arm_BL(block, (uintptr_t)codegen_mem_store_long);
        host_arm_TST_REG(block, REG_R1, REG_R1);
        host_arm_BNE(block, (uintptr_t)codegen_exit_rout);

        return 0;
}
static int codegen_MEM_STORE_SINGLE(codeblock_t *block, uop_t *uop)
{
        int seg_reg = HOST_REG_GET(uop->src_reg_a_real), addr_reg = HOST_REG_GET(uop->src_reg_b_real), src_reg = HOST_REG_GET(uop->src_reg_c_real);
        int src_size = IREG_GET_SIZE(uop->src_reg_c_real);

	if (!REG_IS_D(src_size))
                fatal("MEM_STORE_REG - %02x\n", uop->dest_reg_a_real);

	host_arm_ADD_REG(block, REG_R0, seg_reg, addr_reg);
        if (uop->imm_data)
                host_arm_ADD_IMM(block, REG_R0, REG_R0, uop->imm_data);
	host_arm_VCVT_S_D(block, REG_D_TEMP, src_reg);
        host_arm_BL(block, (uintptr_t)codegen_mem_store_single);
        host_arm_TST_REG(block, REG_R1, REG_R1);
        host_arm_BNE(block, (uintptr_t)codegen_exit_rout);

        return 0;
}
static int codegen_MEM_STORE_DOUBLE(codeblock_t *block, uop_t *uop)
{
        int seg_reg = HOST_REG_GET(uop->src_reg_a_real), addr_reg = HOST_REG_GET(uop->src_reg_b_real), src_reg = HOST_REG_GET(uop->src_reg_c_real);
        int src_size = IREG_GET_SIZE(uop->src_reg_c_real);

	if (!REG_IS_D(src_size))
                fatal("MEM_STORE_REG - %02x\n", uop->dest_reg_a_real);

	host_arm_ADD_REG(block, REG_R0, seg_reg, addr_reg);
        if (uop->imm_data)
                host_arm_ADD_IMM(block, REG_R0, REG_R0, uop->imm_data);
	host_arm_VMOV_D_D(block, REG_D_TEMP, src_reg);
        host_arm_BL(block, (uintptr_t)codegen_mem_store_double);
        host_arm_TST_REG(block, REG_R1, REG_R1);
        host_arm_BNE(block, (uintptr_t)codegen_exit_rout);

        return 0;
}

static int codegen_MOV(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

	if (REG_IS_L(dest_size) && REG_IS_L(src_size))
	{
		host_arm_MOV_REG_LSL(block, dest_reg, src_reg, 0);
	}
	else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
	{
		host_arm_BFI(block, dest_reg, src_reg, 0, 16);
	}
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
	{
		host_arm_BFI(block, dest_reg, src_reg, 0, 8);
	}
	else if (REG_IS_BH(dest_size) && REG_IS_B(src_size))
	{
		host_arm_BFI(block, dest_reg, src_reg, 8, 8);
	}
	else if (REG_IS_B(dest_size) && REG_IS_BH(src_size))
	{
		host_arm_MOV_REG_LSR(block, REG_TEMP, src_reg, 8);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
	}
	else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size))
	{
		host_arm_MOV_REG_LSR(block, REG_TEMP, src_reg, 8);
		host_arm_BFI(block, dest_reg, REG_TEMP, 8, 8);
	}
	else if (REG_IS_D(dest_size) && REG_IS_D(src_size))
	{
		host_arm_VMOV_D_D(block, dest_reg, src_reg);
	}
	else if (REG_IS_Q(dest_size) && REG_IS_Q(src_size))
	{
		host_arm_VMOV_D_D(block, dest_reg, src_reg);
	}
	else
                fatal("MOV %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}

static int codegen_MOV_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

	if (REG_IS_L(dest_size))
	{
		host_arm_MOV_IMM(block, dest_reg, uop->imm_data);
	}
	else if (REG_IS_W(dest_size))
	{
		host_arm_MOVW_IMM(block, REG_TEMP, uop->imm_data);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
	}
	else if (REG_IS_B(dest_size))
	{
		host_arm_AND_IMM(block, dest_reg, dest_reg, ~0x000000ff);
		host_arm_ORR_IMM(block, dest_reg, dest_reg, uop->imm_data);
	}
	else if (REG_IS_BH(dest_size))
	{
		host_arm_AND_IMM(block, dest_reg, dest_reg, ~0x0000ff00);
		host_arm_ORR_IMM(block, dest_reg, dest_reg, uop->imm_data << 8);
	}
	else
                fatal("MOV_IMM %02x\n", uop->dest_reg_a_real);

        return 0;
}
static int codegen_MOV_PTR(codeblock_t *block, uop_t *uop)
{
	host_arm_MOV_IMM(block, uop->dest_reg_a_real, (uintptr_t)uop->p);

        return 0;
}

static int codegen_MOVSX(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

	if (REG_IS_L(dest_size) && REG_IS_B(src_size))
	{
		host_arm_SXTB(block, dest_reg, src_reg, 0);
	}
	else if (REG_IS_L(dest_size) && REG_IS_BH(src_size))
	{
		host_arm_SXTB(block, dest_reg, src_reg, 8);
	}
	else if (REG_IS_L(dest_size) && REG_IS_W(src_size))
	{
		host_arm_SXTH(block, dest_reg, src_reg, 0);
	}
	else if (REG_IS_W(dest_size) && REG_IS_B(src_size))
	{
		host_arm_SXTB(block, REG_TEMP, src_reg, 0);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
	}
	else if (REG_IS_W(dest_size) && REG_IS_BH(src_size))
	{
		host_arm_SXTB(block, REG_TEMP, src_reg, 8);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
	}
	else
		fatal("MOVSX %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_MOVZX(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

	if (REG_IS_Q(dest_size) && REG_IS_L(src_size))
	{
                host_arm_MOV_IMM(block, REG_TEMP, 0);
                host_arm_VMOV_D_64(block, dest_reg, src_reg, REG_TEMP);
	}
	else if (REG_IS_L(dest_size) && REG_IS_Q(src_size))
	{
                host_arm_VMOV_32_S(block, dest_reg, src_reg);
	}
	else if (REG_IS_L(dest_size) && REG_IS_B(src_size))
	{
		host_arm_UXTB(block, dest_reg, src_reg, 0);
	}
	else if (REG_IS_L(dest_size) && REG_IS_BH(src_size))
	{
		host_arm_UXTB(block, dest_reg, src_reg, 8);
	}
	else if (REG_IS_L(dest_size) && REG_IS_W(src_size))
	{
		host_arm_UXTH(block, dest_reg, src_reg, 0);
	}
	else if (REG_IS_W(dest_size) && REG_IS_B(src_size))
	{
		if (src_reg == dest_reg)
			host_arm_BIC_IMM(block, dest_reg, dest_reg, 0xff00);
		else
		{
			host_arm_UXTB(block, REG_TEMP, src_reg, 0);
			host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
		}
	}
	else if (REG_IS_W(dest_size) && REG_IS_BH(src_size))
	{
		host_arm_MOV_REG_LSR(block, REG_TEMP, src_reg, 8);
		host_arm_BIC_IMM(block, dest_reg, dest_reg, 0xff00);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
	}
	else
		fatal("MOVZX %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static double int64_to_double(int64_t a)
{
	return (double)a;
}
static int codegen_MOV_DOUBLE_INT(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_D(dest_size) && REG_IS_L(src_size))
        {
		host_arm_VMOV_S_32(block, REG_D_TEMP, src_reg);
		host_arm_VCVT_D_IS(block, dest_reg, REG_D_TEMP);
        }
        else if (REG_IS_D(dest_size) && REG_IS_W(src_size))
        {
		host_arm_SXTH(block, REG_TEMP, src_reg, 0);
		host_arm_VMOV_S_32(block, REG_D_TEMP, REG_TEMP);
		host_arm_VCVT_D_IS(block, dest_reg, REG_D_TEMP);
        }
        else if (REG_IS_D(dest_size) && REG_IS_Q(src_size))
        {
		/*ARMv7 has no instructions to convert a 64-bit integer to a double.
		  For simplicity, call a C function and let the compiler do it.*/
		host_arm_VMOV_64_D(block, REG_R0, REG_R1, src_reg);
		host_arm_BL(block, (uintptr_t)int64_to_double); /*Input - R0/R1, Output - D0*/
		host_arm_VMOV_D_D(block, dest_reg, REG_D0);
	}
        else
                fatal("MOV_DOUBLE_INT %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_MOV_INT_DOUBLE(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_D(src_size))
        {
		host_arm_VMOV_D_D(block, REG_D_TEMP, src_reg);
	        host_arm_BL(block, (uintptr_t)codegen_fp_round);
		host_arm_VMOV_32_S(block, dest_reg, REG_D_TEMP);
        }
        else if (REG_IS_W(dest_size) && REG_IS_D(src_size))
        {
		host_arm_VMOV_D_D(block, REG_D_TEMP, src_reg);
	        host_arm_BL(block, (uintptr_t)codegen_fp_round);
		host_arm_VMOV_32_S(block, REG_TEMP, REG_D_TEMP);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
        }
        else
                fatal("MOV_INT_DOUBLE %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int64_t x87_fround64(double b)
{
        int64_t a, c;
        
        switch ((cpu_state.npxc >> 10) & 3)
        {
                case 0: /*Nearest*/
                a = (int64_t)floor(b);
                c = (int64_t)floor(b + 1.0);
                if ((b - a) < (c - b))
                        return a;
                else if ((b - a) > (c - b))
                        return c;
                else
                        return (a & 1) ? c : a;
                case 1: /*Down*/
                return (int64_t)floor(b);
                case 2: /*Up*/
                return (int64_t)ceil(b);
                case 3: /*Chop*/
                return (int64_t)b;
        }
        
        return 0;
}
static int codegen_MOV_INT_DOUBLE_64(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real), src_64_reg = HOST_REG_GET(uop->src_reg_b_real), tag_reg = HOST_REG_GET(uop->src_reg_c_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real), src_64_size = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_D(src_size) && REG_IS_Q(src_64_size))
        {
                uint32_t *branch_offset;

                /*If TAG_UINT64 is set then the source is MM[]. Otherwise it is a double in ST()*/
                host_arm_VMOV_D_D(block, dest_reg, src_64_reg);
		host_arm_TST_IMM(block, tag_reg, TAG_UINT64);
                branch_offset = host_arm_BNE_(block);

		/*VFP/NEON has no instructions to convert a float to 64-bit integer,
		  so call out to C.*/
		host_arm_VMOV_D_D(block, REG_D0, src_reg);
	        host_arm_call(block, x87_fround64);
                host_arm_VMOV_D_64(block, REG_D_TEMP, REG_R0, REG_R1);
                
		*branch_offset |= ((((uintptr_t)&block_write_data[block_pos] - (uintptr_t)branch_offset) - 8) & 0x3fffffc) >> 2;
        }
        else
                fatal("MOV_INT_DOUBLE_64 %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_MOV_REG_PTR(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

	host_arm_MOV_IMM(block, REG_TEMP, (uintptr_t)uop->p);
        if (REG_IS_L(dest_size))
        {
                host_arm_LDR_IMM(block, dest_reg, REG_TEMP, 0);
        }
        else
                fatal("MOV_REG_PTR %02x\n", uop->dest_reg_a_real);

        return 0;
}
static int codegen_MOVZX_REG_PTR_8(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

	host_arm_MOV_IMM(block, REG_TEMP, (uintptr_t)uop->p);
        if (REG_IS_L(dest_size))
        {
                host_arm_LDRB_IMM(block, dest_reg, REG_TEMP, 0);
        }
        else if (REG_IS_W(dest_size))
        {
                host_arm_LDRB_IMM(block, REG_TEMP, REG_TEMP, 0);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
        }
        else if (REG_IS_B(dest_size))
        {
                host_arm_LDRB_IMM(block, REG_TEMP, REG_TEMP, 0);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
        }
        else
                fatal("MOVZX_REG_PTR_8 %02x\n", uop->dest_reg_a_real);

        return 0;
}
static int codegen_MOVZX_REG_PTR_16(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

	host_arm_MOV_IMM(block, REG_TEMP, (uintptr_t)uop->p);
        if (REG_IS_L(dest_size))
        {
                host_arm_LDRH_IMM(block, dest_reg, REG_TEMP, 0);
        }
        else if (REG_IS_W(dest_size))
        {
                host_arm_LDRH_IMM(block, REG_TEMP, REG_TEMP, 0);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
        }
        else
                fatal("MOVZX_REG_PTR_16 %02x\n", uop->dest_reg_a_real);

        return 0;
}

static int codegen_NOP(codeblock_t *block, uop_t *uop)
{
	return 0;
}

static int codegen_OR(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

	if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
	{
		host_arm_VORR_D(block, dest_reg, src_reg_a, src_reg_b);
	}
	else if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
	{
		host_arm_ORR_REG_LSL(block, dest_reg, src_reg_a, src_reg_b, 0);
	}
	else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
	{
		host_arm_ORR_REG_LSL(block, REG_TEMP, src_reg_a, src_reg_b, 0);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
	}
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b) && dest_reg == src_reg_a)
	{
		host_arm_UXTB(block, REG_TEMP, src_reg_b, 0);
		host_arm_ORR_REG_LSL(block, dest_reg, src_reg_a, REG_TEMP, 0);
	}
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_BH(src_size_b) && dest_reg == src_reg_a)
	{
		host_arm_UXTB(block, REG_TEMP, src_reg_b, 8);
		host_arm_ORR_REG_LSL(block, dest_reg, src_reg_a, REG_TEMP, 0);
	}
	else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size_a) && REG_IS_B(src_size_b) && dest_reg == src_reg_a)
	{
		host_arm_UXTB(block, REG_TEMP, src_reg_b, 0);
		host_arm_ORR_REG_LSL(block, dest_reg, src_reg_a, REG_TEMP, 8);
	}
	else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size_a) && REG_IS_BH(src_size_b) && dest_reg == src_reg_a)
	{
		host_arm_UXTB(block, REG_TEMP, src_reg_b, 8);
		host_arm_ORR_REG_LSL(block, dest_reg, src_reg_a, REG_TEMP, 8);
	}
	else
                fatal("OR %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_OR_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

	if (REG_IS_L(dest_size) && REG_IS_L(src_size))
	{
		host_arm_ORR_IMM(block, dest_reg, src_reg, uop->imm_data);
	}
	else if (REG_IS_W(dest_size) && REG_IS_W(src_size) && dest_reg == src_reg)
	{
		host_arm_ORR_IMM(block, dest_reg, src_reg, uop->imm_data);
	}
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size) && dest_reg == src_reg)
	{
		host_arm_ORR_IMM(block, dest_reg, src_reg, uop->imm_data);
	}
	else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size) && dest_reg == src_reg)
	{
		host_arm_ORR_IMM(block, dest_reg, src_reg, uop->imm_data << 8);
	}
	else
                fatal("OR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}

static int codegen_PACKSSWB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
		host_arm_VMOV_D_D(block, REG_Q_TEMP, src_reg_a);
		host_arm_VMOV_D_D(block, REG_Q_TEMP_2, src_reg_b);
                host_arm_VQMOVN_S16(block, dest_reg, REG_Q_TEMP);
                host_arm_VQMOVN_S16(block, REG_D_TEMP, REG_Q_TEMP_2);
                host_arm_VZIP_D32(block, dest_reg, REG_D_TEMP);
        }
        else
                fatal("PACKSSWB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PACKSSDW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
		host_arm_VMOV_D_D(block, REG_Q_TEMP, src_reg_a);
		host_arm_VMOV_D_D(block, REG_Q_TEMP_2, src_reg_b);
                host_arm_VQMOVN_S32(block, dest_reg, REG_Q_TEMP);
                host_arm_VQMOVN_S32(block, REG_D_TEMP, REG_Q_TEMP_2);
                host_arm_VZIP_D32(block, dest_reg, REG_D_TEMP);
        }
        else
                fatal("PACKSSDW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PACKUSWB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
		host_arm_VMOV_D_D(block, REG_Q_TEMP, src_reg_a);
		host_arm_VMOV_D_D(block, REG_Q_TEMP_2, src_reg_b);
                host_arm_VQMOVN_U16(block, dest_reg, REG_Q_TEMP);
                host_arm_VQMOVN_U16(block, REG_D_TEMP, REG_Q_TEMP_2);
                host_arm_VZIP_D32(block, dest_reg, REG_D_TEMP);
        }
        else
                fatal("PACKUSWB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}

static int codegen_PADDB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VADD_I8(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PADDB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PADDW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VADD_I16(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PADDW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PADDD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VADD_I32(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PADDD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PADDSB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VQADD_S8(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PADDSB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PADDSW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VQADD_S16(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PADDSW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PADDUSB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VQADD_U8(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PADDUSB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PADDUSW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VQADD_U16(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PADDUSW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}

static int codegen_PCMPEQB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VCEQ_I8(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PCMPEQB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PCMPEQW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VCEQ_I16(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PCMPEQW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PCMPEQD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VCEQ_I32(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PCMPEQD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PCMPGTB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VCGT_S8(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PCMPGTB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PCMPGTW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VCGT_S16(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PCMPGTW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PCMPGTD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VCGT_S32(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PCMPGTD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}

static int codegen_PF2ID(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a))
        {
                host_arm_VCVT_S32_F32(block, dest_reg, src_reg_a);
        }
        else
                fatal("PF2ID %02x %02x\n", uop->dest_reg_a_real);

        return 0;
}
static int codegen_PFADD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VADD_F32(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PFADD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PFCMPEQ(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VCEQ_F32(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PFCMPEQ %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PFCMPGE(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VCGE_F32(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PFCMPGE %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PFCMPGT(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VCGT_F32(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PFCMPGT %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PFMAX(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VMAX_F32(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PFMAX %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PFMIN(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VMIN_F32(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PFMIN %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PFMUL(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VMUL_F32(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PFMUL %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PFRCP(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a))
        {
                /*TODO: This could be improved (use VRECPE/VRECPS)*/
                host_arm_VMOV_F32_ONE(block, REG_D_TEMP);
                host_arm_VDIV_S(block, dest_reg, REG_D_TEMP, src_reg_a);
                host_arm_VDUP_32(block, dest_reg, dest_reg, 0);
        }
        else
                fatal("PFRCP %02x %02x\n", uop->dest_reg_a_real);

        return 0;
}
static int codegen_PFRSQRT(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a))
        {
                /*TODO: This could be improved (use VRSQRTE/VRSQRTS)*/
                host_arm_VSQRT_S(block, REG_D_TEMP, src_reg_a);
                host_arm_VMOV_F32_ONE(block, REG_D_TEMP);
                host_arm_VDIV_S(block, dest_reg, dest_reg, REG_D_TEMP);
                host_arm_VDUP_32(block, dest_reg, dest_reg, 0);
        }
        else
                fatal("PFRSQRT %02x %02x\n", uop->dest_reg_a_real);

        return 0;
}
static int codegen_PFSUB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VSUB_F32(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PFSUB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PI2FD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a))
        {
                host_arm_VCVT_F32_S32(block, dest_reg, src_reg_a);
        }
        else
                fatal("PI2FD %02x %02x\n", uop->dest_reg_a_real);

        return 0;
}

static int codegen_PMADDWD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VMULL_S16(block, REG_Q_TEMP, src_reg_a, src_reg_b);
                host_arm_VPADDL_Q_S32(block, REG_Q_TEMP, REG_Q_TEMP);
		host_arm_VMOVN_I64(block, dest_reg, REG_Q_TEMP);
        }
        else
                fatal("PMULHW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PMULHW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VMULL_S16(block, REG_Q_TEMP, src_reg_a, src_reg_b);
                host_arm_VSHRN_32(block, dest_reg, REG_Q_TEMP, 16);
        }
        else
                fatal("PMULHW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PMULLW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VMUL_S16(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PMULLW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}

static int codegen_PSLLW_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size))
        {
                if (uop->imm_data == 0)
			host_arm_VMOV_D_D(block, dest_reg, src_reg);
		else if (uop->imm_data > 15)
                        host_arm_VEOR_D(block, dest_reg, dest_reg, dest_reg);
                else
                        host_arm_VSHL_D_IMM_16(block, dest_reg, src_reg, uop->imm_data);
        }
        else
                fatal("PSLLW_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_PSLLD_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size))
        {
                if (uop->imm_data == 0)
			host_arm_VMOV_D_D(block, dest_reg, src_reg);
		else if (uop->imm_data > 31)
                        host_arm_VEOR_D(block, dest_reg, dest_reg, dest_reg);
                else
                        host_arm_VSHL_D_IMM_32(block, dest_reg, src_reg, uop->imm_data);
        }
        else
                fatal("PSLLD_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_PSLLQ_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size))
        {
                if (uop->imm_data == 0)
			host_arm_VMOV_D_D(block, dest_reg, src_reg);
		else if (uop->imm_data > 63)
                        host_arm_VEOR_D(block, dest_reg, dest_reg, dest_reg);
                else
                        host_arm_VSHL_D_IMM_64(block, dest_reg, src_reg, uop->imm_data);
        }
        else
                fatal("PSLLQ_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_PSRAW_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size))
        {
		if (uop->imm_data == 0)
			host_arm_VMOV_D_D(block, dest_reg, src_reg);
		else if (uop->imm_data > 15)
                        host_arm_VSHR_D_S16(block, dest_reg, src_reg, 15);
                else
                        host_arm_VSHR_D_S16(block, dest_reg, src_reg, uop->imm_data);
        }
        else
                fatal("PSRAW_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_PSRAD_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size))
        {
                if (uop->imm_data == 0)
			host_arm_VMOV_D_D(block, dest_reg, src_reg);
		else if (uop->imm_data > 31)
                        host_arm_VSHR_D_S32(block, dest_reg, src_reg, 31);
                else
                        host_arm_VSHR_D_S32(block, dest_reg, src_reg, uop->imm_data);
        }
        else
                fatal("PSRAD_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_PSRAQ_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size))
        {
                if (uop->imm_data == 0)
			host_arm_VMOV_D_D(block, dest_reg, src_reg);
		else if (uop->imm_data > 63)
                        host_arm_VSHR_D_S64(block, dest_reg, src_reg, 63);
                else
                        host_arm_VSHR_D_S64(block, dest_reg, src_reg, uop->imm_data);
        }
        else
                fatal("PSRAQ_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_PSRLW_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size))
        {
                if (uop->imm_data == 0)
			host_arm_VMOV_D_D(block, dest_reg, src_reg);
		else if (uop->imm_data > 15)
                        host_arm_VEOR_D(block, dest_reg, dest_reg, dest_reg);
                else
                        host_arm_VSHR_D_U16(block, dest_reg, src_reg, uop->imm_data);
        }
        else
                fatal("PSRLW_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_PSRLD_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size))
        {
                if (uop->imm_data == 0)
			host_arm_VMOV_D_D(block, dest_reg, src_reg);
		else if (uop->imm_data > 31)
                        host_arm_VEOR_D(block, dest_reg, dest_reg, dest_reg);
                else
                        host_arm_VSHR_D_U32(block, dest_reg, src_reg, uop->imm_data);
        }
        else
                fatal("PSRLD_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_PSRLQ_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size))
        {
                if (uop->imm_data == 0)
			host_arm_VMOV_D_D(block, dest_reg, src_reg);
		else if (uop->imm_data > 63)
                        host_arm_VEOR_D(block, dest_reg, dest_reg, dest_reg);
                else
                        host_arm_VSHR_D_U64(block, dest_reg, src_reg, uop->imm_data);
        }
        else
                fatal("PSRLQ_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}

static int codegen_PSUBB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VSUB_I8(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PSUBB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PSUBW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VSUB_I16(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PSUBW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PSUBD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VSUB_I32(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PSUBD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PSUBSB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VQSUB_S8(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PSUBSB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PSUBSW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VQSUB_S16(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PSUBSW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PSUBUSB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VQSUB_U8(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PSUBUSB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PSUBUSW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VQSUB_U16(block, dest_reg, src_reg_a, src_reg_b);
        }
        else
                fatal("PSUBUSW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}

static int codegen_PUNPCKHBW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VMOV_D_D(block, REG_D_TEMP, src_reg_b);
                if (dest_reg != src_reg_a)
                        host_arm_VMOV_D_D(block, dest_reg, src_reg_a);
                host_arm_VZIP_D8(block, dest_reg, REG_D_TEMP);
                host_arm_VMOV_D_D(block, dest_reg, REG_D_TEMP);
        }
        else
                fatal("PUNPCKHBW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PUNPCKHWD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VMOV_D_D(block, REG_D_TEMP, src_reg_b);
                if (dest_reg != src_reg_a)
                        host_arm_VMOV_D_D(block, dest_reg, src_reg_a);
                host_arm_VZIP_D16(block, dest_reg, REG_D_TEMP);
                host_arm_VMOV_D_D(block, dest_reg, REG_D_TEMP);
        }
        else
                fatal("PUNPCKHWD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PUNPCKHDQ(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VMOV_D_D(block, REG_D_TEMP, src_reg_b);
                if (dest_reg != src_reg_a)
                        host_arm_VMOV_D_D(block, dest_reg, src_reg_a);
                host_arm_VZIP_D32(block, dest_reg, REG_D_TEMP);
                host_arm_VMOV_D_D(block, dest_reg, REG_D_TEMP);
        }
        else
                fatal("PUNPCKHDQ %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PUNPCKLBW(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VMOV_D_D(block, REG_D_TEMP, src_reg_b);
                if (dest_reg != src_reg_a)
                        host_arm_VMOV_D_D(block, dest_reg, src_reg_a);
                host_arm_VZIP_D8(block, dest_reg, REG_D_TEMP);
        }
        else
                fatal("PUNPCKLBW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PUNPCKLWD(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VMOV_D_D(block, REG_D_TEMP, src_reg_b);
                if (dest_reg != src_reg_a)
                        host_arm_VMOV_D_D(block, dest_reg, src_reg_a);
                host_arm_VZIP_D16(block, dest_reg, REG_D_TEMP);
        }
        else
                fatal("PUNPCKLWD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_PUNPCKLDQ(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

        if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
        {
                host_arm_VMOV_D_D(block, REG_D_TEMP, src_reg_b);
                if (dest_reg != src_reg_a)
                        host_arm_VMOV_D_D(block, dest_reg, src_reg_a);
                host_arm_VZIP_D32(block, dest_reg, REG_D_TEMP);
        }
        else
                fatal("PUNPCKLDQ %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}

static int codegen_ROL(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real), shift_reg = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

	if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
		host_arm_RSB_IMM(block, REG_TEMP2, shift_reg, 32);
		host_arm_MOV_REG_ROR_REG(block, dest_reg, src_reg, REG_TEMP2);
        }
	else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
		host_arm_UXTH(block, REG_TEMP, src_reg, 0);
		host_arm_RSB_IMM(block, REG_TEMP2, shift_reg, 16);
		host_arm_ORR_REG_LSL(block, REG_TEMP, REG_TEMP, REG_TEMP, 16);
		host_arm_MOV_REG_ROR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP2);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
        }
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
		host_arm_RSB_IMM(block, REG_TEMP2, shift_reg, 8);
		host_arm_UXTB(block, REG_TEMP, src_reg, 0);
		host_arm_AND_IMM(block, REG_TEMP2, REG_TEMP2, 7);
		host_arm_ORR_REG_LSL(block, REG_TEMP, REG_TEMP, REG_TEMP, 8);
		host_arm_MOV_REG_LSR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP2);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
        }
	else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size))
        {
		host_arm_RSB_IMM(block, REG_TEMP2, shift_reg, 8);
		host_arm_UXTB(block, REG_TEMP, src_reg, 8);
		host_arm_AND_IMM(block, REG_TEMP2, REG_TEMP2, 7);
		host_arm_ORR_REG_LSL(block, REG_TEMP, REG_TEMP, REG_TEMP, 8);
		host_arm_MOV_REG_LSR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP2);
		host_arm_BFI(block, dest_reg, REG_TEMP, 8, 8);
        }
	else
                fatal("ROL %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_ROL_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
		if (!(uop->imm_data & 31))
		{
			if (src_reg != dest_reg)
				host_arm_MOV_REG(block, dest_reg, src_reg);
		}
		else
		{
			host_arm_MOV_REG_ROR(block, dest_reg, src_reg, 32 - (uop->imm_data & 31));
		}
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
		if ((uop->imm_data & 15) == 0)
		{
			if (src_reg != dest_reg)
				host_arm_BFI(block, dest_reg, src_reg, 0, 16);
		}
		else
		{
			host_arm_UXTH(block, REG_TEMP, src_reg, 0);
			host_arm_ORR_REG_LSL(block, REG_TEMP, REG_TEMP, REG_TEMP, 16);
			host_arm_MOV_REG_LSR(block, REG_TEMP, REG_TEMP, 16-(uop->imm_data & 15));
			host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
		}
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
		if ((uop->imm_data & 7) == 0)
		{
			if (src_reg != dest_reg)
				host_arm_BFI(block, dest_reg, src_reg, 0, 8);
		}
		else
		{
			host_arm_UXTB(block, REG_TEMP, src_reg, 0);
			host_arm_ORR_REG_LSL(block, REG_TEMP, REG_TEMP, REG_TEMP, 8);
			host_arm_MOV_REG_LSR(block, REG_TEMP, REG_TEMP, 8-(uop->imm_data & 7));
			host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
		}
        }
	else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size))
        {
		if ((uop->imm_data & 7) == 0)
		{
			if (src_reg != dest_reg)
		                fatal("ROL_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
		}
		else
		{
			host_arm_UXTB(block, REG_TEMP, src_reg, 8);
			host_arm_ORR_REG_LSL(block, REG_TEMP, REG_TEMP, REG_TEMP, 8);
			host_arm_MOV_REG_LSR(block, REG_TEMP, REG_TEMP, 8-(uop->imm_data & 7));
			host_arm_BFI(block, dest_reg, REG_TEMP, 8, 8);
		}
        }
	else
                fatal("ROL_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_ROR(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real), shift_reg = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

	if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
		host_arm_MOV_REG_ROR_REG(block, dest_reg, src_reg, shift_reg);
        }
	else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
		host_arm_UXTH(block, REG_TEMP, src_reg, 0);
		host_arm_AND_IMM(block, REG_TEMP2, shift_reg, 15);
		host_arm_ORR_REG_LSL(block, REG_TEMP, REG_TEMP, REG_TEMP, 16);
		host_arm_MOV_REG_LSR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP2);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
        }
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
		host_arm_UXTB(block, REG_TEMP, src_reg, 0);
		host_arm_AND_IMM(block, REG_TEMP2, shift_reg, 7);
		host_arm_ORR_REG_LSL(block, REG_TEMP, REG_TEMP, REG_TEMP, 8);
		host_arm_MOV_REG_LSR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP2);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
        }
	else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size))
        {
		host_arm_UXTB(block, REG_TEMP, src_reg, 8);
		host_arm_AND_IMM(block, REG_TEMP2, shift_reg, 7);
		host_arm_ORR_REG_LSL(block, REG_TEMP, REG_TEMP, REG_TEMP, 8);
		host_arm_MOV_REG_LSR_REG(block, REG_TEMP, REG_TEMP, REG_TEMP2);
		host_arm_BFI(block, dest_reg, REG_TEMP, 8, 8);
        }
	else
                fatal("ROR %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_ROR_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
		if (!(uop->imm_data & 31))
		{
			if (src_reg != dest_reg)
				host_arm_MOV_REG(block, dest_reg, src_reg);
		}
		else
		{
			host_arm_MOV_REG_ROR(block, dest_reg, src_reg, uop->imm_data & 31);
		}
        }
	else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
		if ((uop->imm_data & 15) == 0)
		{
			if (src_reg != dest_reg)
		                fatal("ROR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
		}
		else
		{
			host_arm_UXTH(block, REG_TEMP, src_reg, 0);
			host_arm_ORR_REG_LSL(block, REG_TEMP, REG_TEMP, REG_TEMP, 16);
			host_arm_MOV_REG_LSR(block, REG_TEMP, REG_TEMP, uop->imm_data & 15);
			host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
		}
        }
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
		if ((uop->imm_data & 7) == 0)
		{
			if (src_reg != dest_reg)
		                fatal("ROR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
		}
		else
		{
			host_arm_UXTB(block, REG_TEMP, src_reg, 0);
			host_arm_ORR_REG_LSL(block, REG_TEMP, REG_TEMP, REG_TEMP, 8);
			host_arm_MOV_REG_LSR(block, REG_TEMP, REG_TEMP, uop->imm_data & 7);
			host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
		}
        }
	else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size))
        {
		if ((uop->imm_data & 7) == 0)
		{
			if (src_reg != dest_reg)
		                fatal("ROR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
		}
		else
		{
			host_arm_UXTB(block, REG_TEMP, src_reg, 8);
			host_arm_ORR_REG_LSL(block, REG_TEMP, REG_TEMP, REG_TEMP, 8);
			host_arm_MOV_REG_LSR(block, REG_TEMP, REG_TEMP, uop->imm_data & 7);
			host_arm_BFI(block, dest_reg, REG_TEMP, 8, 8);
		}
        }
	else
                fatal("ROR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}

static int codegen_SAR(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real), shift_reg = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                host_arm_MOV_REG_ASR_REG(block, dest_reg, src_reg, shift_reg);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg, 16);
                host_arm_MOV_REG_ASR_REG(block, REG_TEMP, REG_TEMP, shift_reg);
		host_arm_UXTH(block, REG_TEMP, REG_TEMP, 16);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg, 24);
                host_arm_MOV_REG_ASR_REG(block, REG_TEMP, REG_TEMP, shift_reg);
		host_arm_UXTB(block, REG_TEMP, REG_TEMP, 24);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
        }
        else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg, 16);
                host_arm_MOV_REG_ASR_REG(block, REG_TEMP, REG_TEMP, shift_reg);
		host_arm_UXTB(block, REG_TEMP, REG_TEMP, 24);
		host_arm_BFI(block, dest_reg, REG_TEMP, 8, 8);
        }
        else
                fatal("SAR %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_SAR_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                host_arm_MOV_REG_ASR(block, dest_reg, src_reg, uop->imm_data);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg, 16);
                host_arm_MOV_REG_ASR(block, REG_TEMP, REG_TEMP, uop->imm_data);
		host_arm_UXTH(block, REG_TEMP, REG_TEMP, 16);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg, 24);
                host_arm_MOV_REG_ASR(block, REG_TEMP, REG_TEMP, uop->imm_data);
		host_arm_UXTB(block, REG_TEMP, REG_TEMP, 24);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
        }
        else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size))
        {
		host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg, 16);
                host_arm_MOV_REG_ASR(block, REG_TEMP, REG_TEMP, uop->imm_data);
		host_arm_UXTB(block, REG_TEMP, REG_TEMP, 24);
		host_arm_BFI(block, dest_reg, REG_TEMP, 8, 8);
        }
        else
                fatal("SAR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_SHL(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real), shift_reg = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                host_arm_MOV_REG_LSL_REG(block, dest_reg, src_reg, shift_reg);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
                host_arm_MOV_REG_LSL_REG(block, REG_TEMP, src_reg, shift_reg);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
                host_arm_MOV_REG_LSL_REG(block, REG_TEMP, src_reg, shift_reg);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
        }
        else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size))
        {
		host_arm_UXTB(block, REG_TEMP, src_reg, 8);
                host_arm_MOV_REG_LSL_REG(block, REG_TEMP, REG_TEMP, shift_reg);
		host_arm_BFI(block, dest_reg, REG_TEMP, 8, 8);
        }
        else
                fatal("SHL %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_SHL_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                host_arm_MOV_REG_LSL(block, dest_reg, src_reg, uop->imm_data);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
                host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg, uop->imm_data);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
                host_arm_MOV_REG_LSL(block, REG_TEMP, src_reg, uop->imm_data);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
        }
        else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size))
        {
		host_arm_UXTB(block, REG_TEMP, src_reg, 8);
                host_arm_MOV_REG_LSL(block, REG_TEMP, REG_TEMP, uop->imm_data);
		host_arm_BFI(block, dest_reg, REG_TEMP, 8, 8);
        }
        else
                fatal("SHL_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_SHR(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real), shift_reg = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                host_arm_MOV_REG_LSR_REG(block, dest_reg, src_reg, shift_reg);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
		host_arm_UXTH(block, REG_TEMP, src_reg, 0);
		host_arm_MOV_REG_LSR_REG(block, REG_TEMP, REG_TEMP, shift_reg);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
		host_arm_UXTB(block, REG_TEMP, src_reg, 0);
		host_arm_MOV_REG_LSR_REG(block, REG_TEMP, REG_TEMP, shift_reg);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
        }
        else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size))
        {
		host_arm_UXTB(block, REG_TEMP, src_reg, 8);
		host_arm_MOV_REG_LSR_REG(block, REG_TEMP, REG_TEMP, shift_reg);
		host_arm_BFI(block, dest_reg, REG_TEMP, 8, 8);
        }
        else
                fatal("SHR %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}
static int codegen_SHR_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(dest_size) && REG_IS_L(src_size))
        {
                host_arm_MOV_REG_LSR(block, dest_reg, src_reg, uop->imm_data);
        }
        else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
        {
		host_arm_UXTH(block, REG_TEMP, src_reg, 0);
		host_arm_MOV_REG_LSR(block, REG_TEMP, REG_TEMP, uop->imm_data);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
        }
        else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
        {
		host_arm_UXTB(block, REG_TEMP, src_reg, 0);
		host_arm_MOV_REG_LSR(block, REG_TEMP, REG_TEMP, uop->imm_data);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
        }
        else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size))
        {
		host_arm_UXTB(block, REG_TEMP, src_reg, 8);
		host_arm_MOV_REG_LSR(block, REG_TEMP, REG_TEMP, uop->imm_data);
		host_arm_BFI(block, dest_reg, REG_TEMP, 8, 8);
        }
        else
                fatal("SHR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}

static int codegen_STORE_PTR_IMM(codeblock_t *block, uop_t *uop)
{
	host_arm_MOV_IMM(block, REG_R0, uop->imm_data);

	if (in_range(uop->p, &cpu_state))
		host_arm_STR_IMM(block, REG_R0, REG_CPUSTATE, (uintptr_t)uop->p - (uintptr_t)&cpu_state);
	else
		fatal("codegen_STORE_PTR_IMM - not in range\n");

        return 0;
}
static int codegen_STORE_PTR_IMM_8(codeblock_t *block, uop_t *uop)
{
	host_arm_MOV_IMM(block, REG_R0, uop->imm_data);
	if (in_range(uop->p, &cpu_state))
		host_arm_STRB_IMM(block, REG_R0, REG_CPUSTATE, (uintptr_t)uop->p - (uintptr_t)&cpu_state);
	else
		fatal("codegen_STORE_PTR_IMM - not in range\n");

        return 0;
}

static int codegen_SUB(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

	if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
	{
		host_arm_SUB_REG_LSL(block, dest_reg, src_reg_a, src_reg_b, 0);
	}
	else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b))
	{
		host_arm_SUB_REG_LSL(block, REG_TEMP, src_reg_a, src_reg_b, 0);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
	}
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b))
	{
		host_arm_SUB_REG_LSL(block, REG_TEMP, src_reg_a, src_reg_b, 0);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
	}
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_BH(src_size_b))
	{
		host_arm_SUB_REG_LSR(block, REG_TEMP, src_reg_a, src_reg_b, 8);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
	}
	else if (REG_IS_B(dest_size) && REG_IS_BH(src_size_a) && REG_IS_B(src_size_b))
	{
		host_arm_RSB_REG_LSR(block, REG_TEMP, src_reg_b, src_reg_a, 8);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
	}
	else if (REG_IS_B(dest_size) && REG_IS_BH(src_size_a) && REG_IS_BH(src_size_b))
	{
		host_arm_SUB_REG_LSL(block, REG_TEMP, src_reg_a, src_reg_b, 0);
		host_arm_MOV_REG_LSR(block, REG_TEMP, REG_TEMP, 8);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
	}
	else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size_a) && REG_IS_B(src_size_b))
	{
		host_arm_RSB_REG_LSR(block, REG_TEMP, src_reg_b, src_reg_a, 8);
		host_arm_BFI(block, dest_reg, REG_TEMP, 8, 8);
	}
	else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size_a) && REG_IS_BH(src_size_b))
	{
		host_arm_MOV_REG_LSR(block, REG_TEMP, src_reg_a, 8);
		host_arm_SUB_REG_LSR(block, REG_TEMP, REG_TEMP, src_reg_b, 8);
		host_arm_BFI(block, dest_reg, REG_TEMP, 8, 8);
	}
	else
                fatal("SUB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;

//	host_arm_SUB_REG_LSL(block, uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real, 0);
//        return 0;
}
static int codegen_SUB_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

	if (REG_IS_L(dest_size) && REG_IS_L(src_size))
	{
		host_arm_SUB_IMM(block, dest_reg, src_reg, uop->imm_data);
	}
	else if (REG_IS_W(dest_size) && REG_IS_W(src_size))
	{
		host_arm_SUB_IMM(block, REG_TEMP, src_reg, uop->imm_data);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 16);
	}
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size))
	{
		host_arm_SUB_IMM(block, REG_TEMP, src_reg, uop->imm_data);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
	}
	else if (REG_IS_B(dest_size) && REG_IS_BH(src_size))
	{
		host_arm_SUB_IMM(block, REG_TEMP, src_reg, uop->imm_data << 8);
		host_arm_MOV_REG_LSR(block, REG_TEMP, REG_TEMP, 8);
		host_arm_BFI(block, dest_reg, REG_TEMP, 0, 8);
	}
	else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size))
	{
		host_arm_SUB_IMM(block, REG_TEMP, src_reg, uop->imm_data << 8);
		host_arm_MOV_REG_LSR(block, REG_TEMP, REG_TEMP, 8);
		host_arm_BFI(block, dest_reg, REG_TEMP, 8, 8);
	}
	else
                fatal("SUB_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}

static int codegen_TEST_JNS_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(src_size))
        {
                host_arm_TST_IMM(block, src_reg, 1 << 31);
        }
        else if (REG_IS_W(src_size))
        {
		host_arm_TST_IMM(block, src_reg, 1 << 15);
        }
        else if (REG_IS_B(src_size))
        {
		host_arm_TST_IMM(block, src_reg, 1 << 7);
        }
        else
                fatal("TEST_JNS_DEST %02x\n", uop->src_reg_a_real);

        uop->p = host_arm_BEQ_(block);

        return 0;
}
static int codegen_TEST_JS_DEST(codeblock_t *block, uop_t *uop)
{
        int src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

        if (REG_IS_L(src_size))
        {
                host_arm_TST_IMM(block, src_reg, 1 << 31);
        }
        else if (REG_IS_W(src_size))
        {
		host_arm_TST_IMM(block, src_reg, 1 << 15);
        }
        else if (REG_IS_B(src_size))
        {
		host_arm_TST_IMM(block, src_reg, 1 << 7);
        }
        else
                fatal("TEST_JS_DEST %02x\n", uop->src_reg_a_real);

        uop->p = host_arm_BNE_(block);

        return 0;
}

static int codegen_XOR(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg_a = HOST_REG_GET(uop->src_reg_a_real), src_reg_b = HOST_REG_GET(uop->src_reg_b_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size_a = IREG_GET_SIZE(uop->src_reg_a_real), src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

	if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b))
	{
		host_arm_VEOR_D(block, dest_reg, src_reg_a, src_reg_b);
	}
	else if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b))
	{
		host_arm_EOR_REG_LSL(block, dest_reg, src_reg_a, src_reg_b, 0);
	}
	else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b) && dest_reg == src_reg_a)
	{
		host_arm_UXTH(block, REG_TEMP, src_reg_b, 0);
		host_arm_EOR_REG_LSL(block, dest_reg, src_reg_a, REG_TEMP, 0);
	}
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b) && dest_reg == src_reg_a)
	{
		host_arm_UXTB(block, REG_TEMP, src_reg_b, 0);
		host_arm_EOR_REG_LSL(block, dest_reg, src_reg_a, REG_TEMP, 0);
	}
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_BH(src_size_b) && dest_reg == src_reg_a)
	{
		host_arm_UXTB(block, REG_TEMP, src_reg_b, 8);
		host_arm_EOR_REG_LSL(block, dest_reg, src_reg_a, REG_TEMP, 0);
	}
	else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size_a) && REG_IS_B(src_size_b) && dest_reg == src_reg_a)
	{
		host_arm_UXTB(block, REG_TEMP, src_reg_b, 0);
		host_arm_EOR_REG_LSL(block, dest_reg, src_reg_a, REG_TEMP, 8);
	}
	else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size_a) && REG_IS_BH(src_size_b) && dest_reg == src_reg_a)
	{
		host_arm_UXTB(block, REG_TEMP, src_reg_b, 8);
		host_arm_EOR_REG_LSL(block, dest_reg, src_reg_a, REG_TEMP, 8);
	}
	else
                fatal("XOR %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

        return 0;
}
static int codegen_XOR_IMM(codeblock_t *block, uop_t *uop)
{
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real), src_reg = HOST_REG_GET(uop->src_reg_a_real);
        int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real), src_size = IREG_GET_SIZE(uop->src_reg_a_real);

	if (REG_IS_L(dest_size) && REG_IS_L(src_size))
	{
		host_arm_EOR_IMM(block, dest_reg, src_reg, uop->imm_data);
	}
	else if (REG_IS_W(dest_size) && REG_IS_W(src_size) && dest_reg == src_reg)
	{
		host_arm_EOR_IMM(block, dest_reg, src_reg, uop->imm_data);
	}
	else if (REG_IS_B(dest_size) && REG_IS_B(src_size) && dest_reg == src_reg)
	{
		host_arm_EOR_IMM(block, dest_reg, src_reg, uop->imm_data);
	}
	else if (REG_IS_BH(dest_size) && REG_IS_BH(src_size) && dest_reg == src_reg)
	{
		host_arm_EOR_IMM(block, dest_reg, src_reg, uop->imm_data << 8);
	}
	else
                fatal("XOR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

        return 0;
}

const uOpFn uop_handlers[UOP_MAX] =
{
        [UOP_CALL_FUNC & UOP_MASK] = codegen_CALL_FUNC,
        [UOP_CALL_FUNC_RESULT & UOP_MASK] = codegen_CALL_FUNC_RESULT,
        [UOP_CALL_INSTRUCTION_FUNC & UOP_MASK] = codegen_CALL_INSTRUCTION_FUNC,

        [UOP_JMP & UOP_MASK] = codegen_JMP,

        [UOP_LOAD_SEG & UOP_MASK] = codegen_LOAD_SEG,

        [UOP_LOAD_FUNC_ARG_0 & UOP_MASK] = codegen_LOAD_FUNC_ARG0,
        [UOP_LOAD_FUNC_ARG_1 & UOP_MASK] = codegen_LOAD_FUNC_ARG1,
        [UOP_LOAD_FUNC_ARG_2 & UOP_MASK] = codegen_LOAD_FUNC_ARG2,
        [UOP_LOAD_FUNC_ARG_3 & UOP_MASK] = codegen_LOAD_FUNC_ARG3,

        [UOP_LOAD_FUNC_ARG_0_IMM & UOP_MASK] = codegen_LOAD_FUNC_ARG0_IMM,
        [UOP_LOAD_FUNC_ARG_1_IMM & UOP_MASK] = codegen_LOAD_FUNC_ARG1_IMM,
        [UOP_LOAD_FUNC_ARG_2_IMM & UOP_MASK] = codegen_LOAD_FUNC_ARG2_IMM,
        [UOP_LOAD_FUNC_ARG_3_IMM & UOP_MASK] = codegen_LOAD_FUNC_ARG3_IMM,

        [UOP_STORE_P_IMM & UOP_MASK] = codegen_STORE_PTR_IMM,
        [UOP_STORE_P_IMM_8 & UOP_MASK] = codegen_STORE_PTR_IMM_8,

        [UOP_MEM_LOAD_ABS & UOP_MASK] = codegen_MEM_LOAD_ABS,
        [UOP_MEM_LOAD_REG & UOP_MASK] = codegen_MEM_LOAD_REG,
        [UOP_MEM_LOAD_SINGLE & UOP_MASK] = codegen_MEM_LOAD_SINGLE,
        [UOP_MEM_LOAD_DOUBLE & UOP_MASK] = codegen_MEM_LOAD_DOUBLE,

        [UOP_MEM_STORE_ABS & UOP_MASK] = codegen_MEM_STORE_ABS,
        [UOP_MEM_STORE_REG & UOP_MASK] = codegen_MEM_STORE_REG,
        [UOP_MEM_STORE_IMM_8 & UOP_MASK] = codegen_MEM_STORE_IMM_8,
        [UOP_MEM_STORE_IMM_16 & UOP_MASK] = codegen_MEM_STORE_IMM_16,
        [UOP_MEM_STORE_IMM_32 & UOP_MASK] = codegen_MEM_STORE_IMM_32,
        [UOP_MEM_STORE_SINGLE & UOP_MASK] = codegen_MEM_STORE_SINGLE,
        [UOP_MEM_STORE_DOUBLE & UOP_MASK] = codegen_MEM_STORE_DOUBLE,

        [UOP_MOV     & UOP_MASK] = codegen_MOV,
        [UOP_MOV_PTR & UOP_MASK] = codegen_MOV_PTR,
        [UOP_MOV_IMM & UOP_MASK] = codegen_MOV_IMM,
        [UOP_MOVSX   & UOP_MASK] = codegen_MOVSX,
        [UOP_MOVZX   & UOP_MASK] = codegen_MOVZX,
        [UOP_MOV_DOUBLE_INT & UOP_MASK] = codegen_MOV_DOUBLE_INT,
        [UOP_MOV_INT_DOUBLE    & UOP_MASK] = codegen_MOV_INT_DOUBLE,
        [UOP_MOV_INT_DOUBLE_64 & UOP_MASK] = codegen_MOV_INT_DOUBLE_64,
        [UOP_MOV_REG_PTR       & UOP_MASK] = codegen_MOV_REG_PTR,
        [UOP_MOVZX_REG_PTR_8   & UOP_MASK] = codegen_MOVZX_REG_PTR_8,
        [UOP_MOVZX_REG_PTR_16   & UOP_MASK] = codegen_MOVZX_REG_PTR_16,

        [UOP_ADD     & UOP_MASK] = codegen_ADD,
        [UOP_ADD_IMM & UOP_MASK] = codegen_ADD_IMM,
        [UOP_ADD_LSHIFT & UOP_MASK] = codegen_ADD_LSHIFT,
        [UOP_AND     & UOP_MASK] = codegen_AND,
        [UOP_AND_IMM & UOP_MASK] = codegen_AND_IMM,
        [UOP_ANDN    & UOP_MASK] = codegen_ANDN,
        [UOP_OR      & UOP_MASK] = codegen_OR,
        [UOP_OR_IMM  & UOP_MASK] = codegen_OR_IMM,
        [UOP_SUB     & UOP_MASK] = codegen_SUB,
        [UOP_SUB_IMM & UOP_MASK] = codegen_SUB_IMM,
        [UOP_XOR     & UOP_MASK] = codegen_XOR,
        [UOP_XOR_IMM & UOP_MASK] = codegen_XOR_IMM,

        [UOP_SAR     & UOP_MASK] = codegen_SAR,
        [UOP_SAR_IMM & UOP_MASK] = codegen_SAR_IMM,
        [UOP_SHL     & UOP_MASK] = codegen_SHL,
        [UOP_SHL_IMM & UOP_MASK] = codegen_SHL_IMM,
        [UOP_SHR     & UOP_MASK] = codegen_SHR,
        [UOP_SHR_IMM & UOP_MASK] = codegen_SHR_IMM,
        [UOP_ROL     & UOP_MASK] = codegen_ROL,
        [UOP_ROL_IMM & UOP_MASK] = codegen_ROL_IMM,
        [UOP_ROR     & UOP_MASK] = codegen_ROR,
        [UOP_ROR_IMM & UOP_MASK] = codegen_ROR_IMM,

        [UOP_CMP_IMM_JZ & UOP_MASK] = codegen_CMP_IMM_JZ,

        [UOP_CMP_JB        & UOP_MASK] = codegen_CMP_JB,
        [UOP_CMP_JNBE      & UOP_MASK] = codegen_CMP_JNBE,

        [UOP_CMP_JNB_DEST  & UOP_MASK] = codegen_CMP_JNB_DEST,
        [UOP_CMP_JNBE_DEST & UOP_MASK] = codegen_CMP_JNBE_DEST,
        [UOP_CMP_JNL_DEST  & UOP_MASK] = codegen_CMP_JNL_DEST,
        [UOP_CMP_JNLE_DEST & UOP_MASK] = codegen_CMP_JNLE_DEST,
        [UOP_CMP_JNO_DEST  & UOP_MASK] = codegen_CMP_JNO_DEST,
        [UOP_CMP_JNZ_DEST  & UOP_MASK] = codegen_CMP_JNZ_DEST,
        [UOP_CMP_JB_DEST   & UOP_MASK] = codegen_CMP_JB_DEST,
        [UOP_CMP_JBE_DEST  & UOP_MASK] = codegen_CMP_JBE_DEST,
        [UOP_CMP_JL_DEST   & UOP_MASK] = codegen_CMP_JL_DEST,
        [UOP_CMP_JLE_DEST  & UOP_MASK] = codegen_CMP_JLE_DEST,
        [UOP_CMP_JO_DEST   & UOP_MASK] = codegen_CMP_JO_DEST,
        [UOP_CMP_JZ_DEST   & UOP_MASK] = codegen_CMP_JZ_DEST,

        [UOP_CMP_IMM_JNZ_DEST & UOP_MASK] = codegen_CMP_IMM_JNZ_DEST,
        [UOP_CMP_IMM_JZ_DEST  & UOP_MASK] = codegen_CMP_IMM_JZ_DEST,

        [UOP_TEST_JNS_DEST & UOP_MASK] = codegen_TEST_JNS_DEST,
        [UOP_TEST_JS_DEST & UOP_MASK] = codegen_TEST_JS_DEST,

        [UOP_FP_ENTER & UOP_MASK] = codegen_FP_ENTER,
        [UOP_MMX_ENTER & UOP_MASK] = codegen_MMX_ENTER,

        [UOP_FADD & UOP_MASK] = codegen_FADD,
        [UOP_FCOM & UOP_MASK] = codegen_FCOM,
        [UOP_FDIV & UOP_MASK] = codegen_FDIV,
        [UOP_FMUL & UOP_MASK] = codegen_FMUL,
        [UOP_FSUB & UOP_MASK] = codegen_FSUB,

        [UOP_FABS & UOP_MASK] = codegen_FABS,
        [UOP_FCHS & UOP_MASK] = codegen_FCHS,
        [UOP_FSQRT & UOP_MASK] = codegen_FSQRT,
        [UOP_FTST & UOP_MASK] = codegen_FTST,

        [UOP_PACKSSWB & UOP_MASK] = codegen_PACKSSWB,
        [UOP_PACKSSDW & UOP_MASK] = codegen_PACKSSDW,
        [UOP_PACKUSWB & UOP_MASK] = codegen_PACKUSWB,

        [UOP_PADDB & UOP_MASK]   = codegen_PADDB,
        [UOP_PADDW & UOP_MASK]   = codegen_PADDW,
        [UOP_PADDD & UOP_MASK]   = codegen_PADDD,
        [UOP_PADDSB & UOP_MASK]  = codegen_PADDSB,
        [UOP_PADDSW & UOP_MASK]  = codegen_PADDSW,
        [UOP_PADDUSB & UOP_MASK] = codegen_PADDUSB,
        [UOP_PADDUSW & UOP_MASK] = codegen_PADDUSW,

        [UOP_PCMPEQB & UOP_MASK] = codegen_PCMPEQB,
        [UOP_PCMPEQW & UOP_MASK] = codegen_PCMPEQW,
        [UOP_PCMPEQD & UOP_MASK] = codegen_PCMPEQD,
        [UOP_PCMPGTB & UOP_MASK] = codegen_PCMPGTB,
        [UOP_PCMPGTW & UOP_MASK] = codegen_PCMPGTW,
        [UOP_PCMPGTD & UOP_MASK] = codegen_PCMPGTD,

        [UOP_PF2ID & UOP_MASK]   = codegen_PF2ID,
        [UOP_PFADD & UOP_MASK]   = codegen_PFADD,
        [UOP_PFCMPEQ & UOP_MASK] = codegen_PFCMPEQ,
        [UOP_PFCMPGE & UOP_MASK] = codegen_PFCMPGE,
        [UOP_PFCMPGT & UOP_MASK] = codegen_PFCMPGT,
        [UOP_PFMAX & UOP_MASK]   = codegen_PFMAX,
        [UOP_PFMIN & UOP_MASK]   = codegen_PFMIN,
        [UOP_PFMUL & UOP_MASK]   = codegen_PFMUL,
        [UOP_PFRCP & UOP_MASK]   = codegen_PFRCP,
        [UOP_PFRSQRT & UOP_MASK] = codegen_PFRSQRT,
        [UOP_PFSUB & UOP_MASK]   = codegen_PFSUB,
        [UOP_PI2FD & UOP_MASK]   = codegen_PI2FD,

        [UOP_PMADDWD & UOP_MASK] = codegen_PMADDWD,
        [UOP_PMULHW & UOP_MASK]  = codegen_PMULHW,
        [UOP_PMULLW & UOP_MASK]  = codegen_PMULLW,

        [UOP_PSLLW_IMM & UOP_MASK] = codegen_PSLLW_IMM,
        [UOP_PSLLD_IMM & UOP_MASK] = codegen_PSLLD_IMM,
        [UOP_PSLLQ_IMM & UOP_MASK] = codegen_PSLLQ_IMM,
        [UOP_PSRAW_IMM & UOP_MASK] = codegen_PSRAW_IMM,
        [UOP_PSRAD_IMM & UOP_MASK] = codegen_PSRAD_IMM,
        [UOP_PSRAQ_IMM & UOP_MASK] = codegen_PSRAQ_IMM,
        [UOP_PSRLW_IMM & UOP_MASK] = codegen_PSRLW_IMM,
        [UOP_PSRLD_IMM & UOP_MASK] = codegen_PSRLD_IMM,
        [UOP_PSRLQ_IMM & UOP_MASK] = codegen_PSRLQ_IMM,

        [UOP_PSUBB & UOP_MASK]   = codegen_PSUBB,
        [UOP_PSUBW & UOP_MASK]   = codegen_PSUBW,
        [UOP_PSUBD & UOP_MASK]   = codegen_PSUBD,
        [UOP_PSUBSB & UOP_MASK]  = codegen_PSUBSB,
        [UOP_PSUBSW & UOP_MASK]  = codegen_PSUBSW,
        [UOP_PSUBUSB & UOP_MASK] = codegen_PSUBUSB,
        [UOP_PSUBUSW & UOP_MASK] = codegen_PSUBUSW,

        [UOP_PUNPCKHBW & UOP_MASK] = codegen_PUNPCKHBW,
        [UOP_PUNPCKHWD & UOP_MASK] = codegen_PUNPCKHWD,
        [UOP_PUNPCKHDQ & UOP_MASK] = codegen_PUNPCKHDQ,
        [UOP_PUNPCKLBW & UOP_MASK] = codegen_PUNPCKLBW,
        [UOP_PUNPCKLWD & UOP_MASK] = codegen_PUNPCKLWD,
        [UOP_PUNPCKLDQ & UOP_MASK] = codegen_PUNPCKLDQ,

	[UOP_NOP_BARRIER & UOP_MASK] = codegen_NOP
};

void codegen_direct_read_8(codeblock_t *block, int host_reg, void *p)
{
	if (in_range_h(p, &cpu_state))
		host_arm_LDRB_IMM(block, host_reg, REG_CPUSTATE, (uintptr_t)p - (uintptr_t)&cpu_state);
	else
		fatal("codegen_direct_read_8 - not in range\n");
}
void codegen_direct_read_16(codeblock_t *block, int host_reg, void *p)
{
	if (in_range_h(p, &cpu_state))
		host_arm_LDRH_IMM(block, host_reg, REG_CPUSTATE, (uintptr_t)p - (uintptr_t)&cpu_state);
	else
	{
		host_arm_MOV_IMM(block, REG_R3, (uintptr_t)p - (uintptr_t)&cpu_state);
		host_arm_LDRH_REG(block, host_reg, REG_CPUSTATE, REG_R3);
	}
}
void codegen_direct_read_32(codeblock_t *block, int host_reg, void *p)
{
	if (in_range(p, &cpu_state))
		host_arm_LDR_IMM(block, host_reg, REG_CPUSTATE, (uintptr_t)p - (uintptr_t)&cpu_state);
	else
		fatal("codegen_direct_read_32 - not in range\n");
}
void codegen_direct_read_pointer(codeblock_t *block, int host_reg, void *p)
{
	codegen_direct_read_32(block, host_reg, p);
}
void codegen_direct_read_64(codeblock_t *block, int host_reg, void *p)
{
	host_arm_VLDR_D(block, host_reg, REG_CPUSTATE, (uintptr_t)p - (uintptr_t)&cpu_state);
}
void codegen_direct_read_double(codeblock_t *block, int host_reg, void *p)
{
	host_arm_VLDR_D(block, host_reg, REG_CPUSTATE, (uintptr_t)p - (uintptr_t)&cpu_state);
}
void codegen_direct_read_st_8(codeblock_t *block, int host_reg, void *base, int reg_idx)
{
        host_arm_LDR_IMM(block, REG_TEMP, REG_HOST_SP, IREG_TOP_diff_stack_offset);
        host_arm_ADD_IMM(block, REG_TEMP, REG_TEMP, reg_idx);
        host_arm_AND_IMM(block, REG_TEMP, REG_TEMP, 7);
	host_arm_ADD_REG_LSL(block, REG_TEMP, REG_CPUSTATE, REG_TEMP, 3);
	host_arm_LDRB_IMM(block, host_reg, REG_TEMP, (uintptr_t)base - (uintptr_t)&cpu_state);
}
void codegen_direct_read_st_64(codeblock_t *block, int host_reg, void *base, int reg_idx)
{
        host_arm_LDR_IMM(block, REG_TEMP, REG_HOST_SP, IREG_TOP_diff_stack_offset);
        host_arm_ADD_IMM(block, REG_TEMP, REG_TEMP, reg_idx);
        host_arm_AND_IMM(block, REG_TEMP, REG_TEMP, 7);
	host_arm_ADD_REG_LSL(block, REG_TEMP, REG_CPUSTATE, REG_TEMP, 3);
	host_arm_VLDR_D(block, host_reg, REG_TEMP, (uintptr_t)base - (uintptr_t)&cpu_state);
}
void codegen_direct_read_st_double(codeblock_t *block, int host_reg, void *base, int reg_idx)
{
        host_arm_LDR_IMM(block, REG_TEMP, REG_HOST_SP, IREG_TOP_diff_stack_offset);
        host_arm_ADD_IMM(block, REG_TEMP, REG_TEMP, reg_idx);
        host_arm_AND_IMM(block, REG_TEMP, REG_TEMP, 7);
	host_arm_ADD_REG_LSL(block, REG_TEMP, REG_CPUSTATE, REG_TEMP, 3);
	host_arm_VLDR_D(block, host_reg, REG_TEMP, (uintptr_t)base - (uintptr_t)&cpu_state);
}

void codegen_direct_write_8(codeblock_t *block, void *p, int host_reg)
{
	if (in_range(p, &cpu_state))
		host_arm_STRB_IMM(block, host_reg, REG_CPUSTATE, (uintptr_t)p - (uintptr_t)&cpu_state);
	else
		fatal("codegen_direct_write_8 - not in range\n");
}
void codegen_direct_write_16(codeblock_t *block, void *p, int host_reg)
{
	if (in_range_h(p, &cpu_state))
		host_arm_STRH_IMM(block, host_reg, REG_CPUSTATE, (uintptr_t)p - (uintptr_t)&cpu_state);
	else
	{
		host_arm_MOV_IMM(block, REG_R3, (uintptr_t)p - (uintptr_t)&cpu_state);
		host_arm_STRH_REG(block, host_reg, REG_CPUSTATE, REG_R3);
	}
}
void codegen_direct_write_32(codeblock_t *block, void *p, int host_reg)
{
	if (in_range(p, &cpu_state))
		host_arm_STR_IMM(block, host_reg, REG_CPUSTATE, (uintptr_t)p - (uintptr_t)&cpu_state);
	else
		fatal("codegen_direct_write_32 - not in range\n");
}
void codegen_direct_write_64(codeblock_t *block, void *p, int host_reg)
{
	host_arm_VSTR_D(block, host_reg, REG_CPUSTATE, (uintptr_t)p - (uintptr_t)&cpu_state);
}
void codegen_direct_write_double(codeblock_t *block, void *p, int host_reg)
{
	host_arm_VSTR_D(block, host_reg, REG_CPUSTATE, (uintptr_t)p - (uintptr_t)&cpu_state);
}
void codegen_direct_write_st_8(codeblock_t *block, void *base, int reg_idx, int host_reg)
{
        host_arm_LDR_IMM(block, REG_TEMP, REG_HOST_SP, IREG_TOP_diff_stack_offset);
        host_arm_ADD_IMM(block, REG_TEMP, REG_TEMP, reg_idx);
        host_arm_AND_IMM(block, REG_TEMP, REG_TEMP, 7);
	host_arm_ADD_REG_LSL(block, REG_TEMP, REG_CPUSTATE, REG_TEMP, 3);
	host_arm_STRB_IMM(block, host_reg, REG_TEMP, (uintptr_t)base - (uintptr_t)&cpu_state);
}
void codegen_direct_write_st_64(codeblock_t *block, void *base, int reg_idx, int host_reg)
{
        host_arm_LDR_IMM(block, REG_TEMP, REG_HOST_SP, IREG_TOP_diff_stack_offset);
        host_arm_ADD_IMM(block, REG_TEMP, REG_TEMP, reg_idx);
        host_arm_AND_IMM(block, REG_TEMP, REG_TEMP, 7);
	host_arm_ADD_REG_LSL(block, REG_TEMP, REG_CPUSTATE, REG_TEMP, 3);
	host_arm_VSTR_D(block, host_reg, REG_TEMP, (uintptr_t)base - (uintptr_t)&cpu_state);
}
void codegen_direct_write_st_double(codeblock_t *block, void *base, int reg_idx, int host_reg)
{
        host_arm_LDR_IMM(block, REG_TEMP, REG_HOST_SP, IREG_TOP_diff_stack_offset);
        host_arm_ADD_IMM(block, REG_TEMP, REG_TEMP, reg_idx);
        host_arm_AND_IMM(block, REG_TEMP, REG_TEMP, 7);
	host_arm_ADD_REG_LSL(block, REG_TEMP, REG_CPUSTATE, REG_TEMP, 3);
	host_arm_VSTR_D(block, host_reg, REG_TEMP, (uintptr_t)base - (uintptr_t)&cpu_state);
}

void codegen_direct_write_ptr(codeblock_t *block, void *p, int host_reg)
{
	if (in_range(p, &cpu_state))
		host_arm_STR_IMM(block, host_reg, REG_CPUSTATE, (uintptr_t)p - (uintptr_t)&cpu_state);
	else
		fatal("codegen_direct_write_ptr - not in range\n");
}

void codegen_direct_read_16_stack(codeblock_t *block, int host_reg, int stack_offset)
{
	if (stack_offset >= 0 && stack_offset < 256)
		host_arm_LDRH_IMM(block, host_reg, REG_HOST_SP, stack_offset);
	else
		fatal("codegen_direct_read_32 - not in range\n");
}
void codegen_direct_read_32_stack(codeblock_t *block, int host_reg, int stack_offset)
{
	if (stack_offset >= 0 && stack_offset < 4096)
		host_arm_LDR_IMM(block, host_reg, REG_HOST_SP, stack_offset);
	else
		fatal("codegen_direct_read_32 - not in range\n");
}
void codegen_direct_read_pointer_stack(codeblock_t *block, int host_reg, int stack_offset)
{
	codegen_direct_read_32_stack(block, host_reg, stack_offset);
}
void codegen_direct_read_64_stack(codeblock_t *block, int host_reg, int stack_offset)
{
	host_arm_VLDR_D(block, host_reg, REG_HOST_SP, stack_offset);
}
void codegen_direct_read_double_stack(codeblock_t *block, int host_reg, int stack_offset)
{
        host_arm_VLDR_D(block, host_reg, REG_HOST_SP, stack_offset);
}

void codegen_direct_write_32_stack(codeblock_t *block, int stack_offset, int host_reg)
{
	if (stack_offset >= 0 && stack_offset < 4096)
		host_arm_STR_IMM(block, host_reg, REG_HOST_SP, stack_offset);
	else
		fatal("codegen_direct_write_32 - not in range\n");
}
void codegen_direct_write_64_stack(codeblock_t *block, int stack_offset, int host_reg)
{
	host_arm_VSTR_D(block, host_reg, REG_HOST_SP, stack_offset);
}
void codegen_direct_write_double_stack(codeblock_t *block, int stack_offset, int host_reg)
{
        host_arm_VSTR_D(block, host_reg, REG_HOST_SP, stack_offset);
}

void codegen_set_jump_dest(codeblock_t *block, void *p)
{
	*(uint32_t *)p |= ((((uintptr_t)&block_write_data[block_pos] - (uintptr_t)p) - 8) & 0x3fffffc) >> 2;
}
#endif
