/*Basic P6 timing model by plant/nerd73. Based on the K6 timing model*/
/*Some cycle timings come from https://www.agner.org/optimize/instruction_tables.pdf*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/machine.h>

#include "x86.h"
#include "x86_ops.h"
#include "x87.h"
#include "386_common.h"
#include "codegen.h"
#include "codegen_ops.h"
#include "codegen_timing_common.h"

typedef enum uop_type_t
{
        UOP_ALU = 0,   /*Executes in Port 0 or 1 ALU units*/
        UOP_ALUP0,     /*Executes in Port 0 ALU unit*/
        UOP_LOAD,      /*Executes in Load unit*/
        UOP_STORED,    /*Executes in Data Store unit*/
	UOP_STOREA,    /*Executes in Address Store unit*/
        UOP_FLOAD,     /*Executes in Load unit*/
        UOP_FSTORED,   /*Executes in Data Store unit*/
	UOP_FSTOREA,   /*Executes in Address Store unit*/
        UOP_MLOAD,     /*Executes in Load unit*/
        UOP_MSTORED,   /*Executes in Data Store unit*/
        UOP_MSTOREA,   /*Executes in Address Store unit*/
        UOP_FLOAT,     /*Executes in Floating Point unit*/
        UOP_MMX,       /*Executes in Port 0 or 1 ALU units as MMX*/
        UOP_MMX_SHIFT, /*Executes in Port 1 ALU unit. Uses MMX shifter*/
        UOP_MMX_MUL,   /*Executes in Port 0 ALU unit. Uses MMX multiplier*/
        UOP_BRANCH,    /*Executes in Branch unit*/
        UOP_FXCH       /*Does not require an execution unit*/
} uop_type_t;

typedef enum decode_type_t
{
        DECODE_SIMPLE,
        DECODE_COMPLEX,
} decode_type_t;

#define MAX_UOPS 10

typedef struct p6_uop_t
{
        uop_type_t type;
        int latency;
} p6_uop_t;

typedef struct macro_op_t
{
        int nr_uops;
        decode_type_t decode_type;
        p6_uop_t uop[MAX_UOPS];
} macro_op_t;

static const macro_op_t alu_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SIMPLE,
        .uop[0] = {.type = UOP_ALU, .latency = 1}
};
static const macro_op_t alup0_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SIMPLE,
        .uop[0] = {.type = UOP_ALUP0, .latency = 1}
};
static const macro_op_t load_alu_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD, .latency = 1},
        .uop[1] = {.type = UOP_ALU,  .latency = 1}
};
static const macro_op_t load_alup0_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,  .latency = 1},
        .uop[1] = {.type = UOP_ALUP0, .latency = 1}
};
static const macro_op_t alu_store_op =
{
        .nr_uops = 4,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,    .latency = 1},
        .uop[1] = {.type = UOP_ALU,     .latency = 1},
        .uop[2] = {.type = UOP_STORED,  .latency = 1},
        .uop[3] = {.type = UOP_STOREA,  .latency = 1}
	};
static const macro_op_t alup0_store_op =
{
        .nr_uops = 4,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,    .latency = 1},
        .uop[1] = {.type = UOP_ALUP0,   .latency = 1},
        .uop[2] = {.type = UOP_STORED,  .latency = 1},
        .uop[3] = {.type = UOP_STOREA,  .latency = 1}
};

static const macro_op_t branch_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_BRANCH, .latency = 2}
};

static const macro_op_t fxch_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SIMPLE,
        .uop[0] = {.type = UOP_FXCH, .latency = 1}
};

static const macro_op_t load_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SIMPLE,
        .uop[0] = {.type = UOP_LOAD, .latency = 1}
};

static const macro_op_t store_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_STORED,  .latency = 1},
        .uop[1] = {.type = UOP_STOREA,  .latency = 1}
};


static const macro_op_t bswap_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU,   .latency = 1},
        .uop[1] = {.type = UOP_ALU,   .latency = 1},
};
static const macro_op_t leave_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD, .latency = 1},
        .uop[1] = {.type = UOP_ALU,  .latency = 1},
        .uop[2] = {.type = UOP_ALU,  .latency = 1}
};
static const macro_op_t lods_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,   .latency = 1},
        .uop[1] = {.type = UOP_ALU,    .latency = 1}
};
static const macro_op_t loop_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU,    .latency = 1},
        .uop[1] = {.type = UOP_BRANCH, .latency = 2}
};
static const macro_op_t mov_reg_seg_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,   .latency = 1},
};
static const macro_op_t movs_op =
{
        .nr_uops = 4,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,   .latency = 1},
        .uop[1] = {.type = UOP_STORED, .latency = 1},
        .uop[2] = {.type = UOP_STOREA, .latency = 1},
        .uop[3] = {.type = UOP_ALU,    .latency = 1}
};
static const macro_op_t pop_reg_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD, .latency = 1},
        .uop[1] = {.type = UOP_ALU,  .latency = 1}
};
static const macro_op_t pop_mem_op =
{
        .nr_uops = 4,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,   .latency = 1},
        .uop[1] = {.type = UOP_STORED, .latency = 1},
        .uop[2] = {.type = UOP_STOREA, .latency = 1},
        .uop[3] = {.type = UOP_ALU,    .latency = 1}
};
static const macro_op_t push_imm_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_STORED,  .latency = 1},
        .uop[1] = {.type = UOP_STOREA,  .latency = 1},
};
static const macro_op_t push_mem_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,   .latency = 1},
        .uop[1] = {.type = UOP_STORED, .latency = 1},
        .uop[2] = {.type = UOP_STOREA, .latency = 1}
};
static const macro_op_t push_seg_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,   .latency = 1},
        .uop[1] = {.type = UOP_STORED, .latency = 1},
        .uop[2] = {.type = UOP_STOREA, .latency = 1},
	.uop[3] = {.type = UOP_ALU,    .latency = 1}
};
static const macro_op_t stos_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[1] = {.type = UOP_STORED, .latency = 1},
        .uop[2] = {.type = UOP_STOREA, .latency = 1},
        .uop[3] = {.type = UOP_ALU,    .latency = 1}
};
static const macro_op_t test_reg_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU,    .latency = 1}
};
static const macro_op_t test_reg_b_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALUP0,   .latency = 1}
};
static const macro_op_t test_mem_imm_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD, .latency = 1},
        .uop[1] = {.type = UOP_ALU,  .latency = 1}
};
static const macro_op_t test_mem_imm_b_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,  .latency = 1},
        .uop[1] = {.type = UOP_ALUP0, .latency = 1}
};
static const macro_op_t xchg_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU, .latency = 1},
        .uop[1] = {.type = UOP_ALU, .latency = 1},
        .uop[2] = {.type = UOP_ALU, .latency = 1}
};


static const macro_op_t mmx_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SIMPLE,
        .uop[0] = {.type = UOP_MMX, .latency = 1}
};
static const macro_op_t mmx_mul_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SIMPLE,
        .uop[0] = {.type = UOP_MMX_MUL, .latency = 1}
};
static const macro_op_t mmx_shift_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SIMPLE,
        .uop[0] = {.type = UOP_MMX_SHIFT, .latency = 1}
};
static const macro_op_t load_mmx_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD, .latency =   2},
        .uop[1] = {.type = UOP_MMX,  .latency =   2}
};
static const macro_op_t load_mmx_mul_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,    .latency =   2},
        .uop[1] = {.type = UOP_MMX_MUL, .latency =   2}
};
static const macro_op_t load_mmx_shift_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,      .latency =   2},
        .uop[1] = {.type = UOP_MMX_SHIFT, .latency =   2}
};
static const macro_op_t mload_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_MLOAD, .latency = 1},
};

static const macro_op_t mstore_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_MSTORED, .latency = 1},
        .uop[1] = {.type = UOP_MSTOREA, .latency = 1}
};
static const macro_op_t pmul_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SIMPLE,
        .uop[0] = {.type = UOP_MMX_MUL, .latency = 1}
};
static const macro_op_t pmul_mem_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,    .latency =   2},
        .uop[1] = {.type = UOP_MMX_MUL, .latency =   2}
};
static const macro_op_t float_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SIMPLE,
        .uop[0] = {.type = UOP_FLOAT, .latency = 1}
};
static const macro_op_t fadd_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SIMPLE,
        .uop[0] = {.type = UOP_FLOAT, .latency = 2}
};
static const macro_op_t fmul_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SIMPLE,
        .uop[0] = {.type = UOP_ALUP0, .latency = 3}
};
static const macro_op_t float2_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_FLOAT, .latency = 1},
        .uop[1] = {.type = UOP_FLOAT, .latency = 1}
};
static const macro_op_t fchs_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_FLOAT, .latency = 2},
        .uop[1] = {.type = UOP_FLOAT, .latency = 2},
        .uop[2] = {.type = UOP_FLOAT, .latency = 2}
};
static const macro_op_t load_float_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_FLOAD, .latency = 1},
        .uop[1] = {.type = UOP_FLOAT, .latency = 1}
};
static const macro_op_t load_fadd_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_FLOAD, .latency = 1},
        .uop[1] = {.type = UOP_FLOAT, .latency = 2}
};
static const macro_op_t load_fmul_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD, .latency = 1},
        .uop[1] = {.type = UOP_ALU,  .latency = 4}
};
static const macro_op_t fstore_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_FSTORED, .latency = 1},
        .uop[1] = {.type = UOP_FSTOREA, .latency = 1},
};
static const macro_op_t load_fiadd_op =
{
        .nr_uops = 7,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_FLOAD, .latency = 1},
        .uop[1] = {.type = UOP_FLOAT, .latency = 1},
        .uop[2] = {.type = UOP_FLOAT, .latency = 1},
        .uop[3] = {.type = UOP_FLOAT, .latency = 1},
        .uop[4] = {.type = UOP_FLOAT, .latency = 1},
        .uop[5] = {.type = UOP_FLOAT, .latency = 1},
        .uop[6] = {.type = UOP_FLOAT, .latency = 1}
};
static const macro_op_t fdiv_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SIMPLE,
        .uop[0] = {.type = UOP_FLOAT, .latency = 37}
};
static const macro_op_t fdiv_mem_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_FLOAD, .latency = 1},
        .uop[1] = {.type = UOP_FLOAT, .latency = 37}
};
static const macro_op_t fsin_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SIMPLE,
        .uop[0] = {.type = UOP_FLOAT, .latency = 62}
};
static const macro_op_t fsqrt_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SIMPLE,
        .uop[0] = {.type = UOP_FLOAT, .latency = 69}
};

static const macro_op_t fldcw_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SIMPLE,
        .uop[0] = {.type = UOP_FLOAT, .latency = 10}
};
static const macro_op_t complex_float_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_FLOAT, .latency = 1}
};
static const macro_op_t complex_float_l_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_FLOAT, .latency = 50}
};
static const macro_op_t flde_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_FLOAD, .latency = 1},
        .uop[1] = {.type = UOP_FLOAD, .latency = 1},
        .uop[2] = {.type = UOP_FLOAT, .latency = 2}
};
static const macro_op_t fste_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_FLOAT,   .latency = 2},
        .uop[1] = {.type = UOP_FSTORED, .latency = 1},
        .uop[2] = {.type = UOP_FSTOREA, .latency = 1}
};

static const macro_op_t complex_alu1_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU, .latency = 1}
};
static const macro_op_t alu2_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU, .latency = 1},
        .uop[1] = {.type = UOP_ALU, .latency = 1}
};
static const macro_op_t alu3_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU, .latency = 1},
        .uop[1] = {.type = UOP_ALU, .latency = 1},
        .uop[2] = {.type = UOP_ALU, .latency = 1}
};
static const macro_op_t alu6_op =
{
        .nr_uops = 6,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU, .latency = 1},
        .uop[1] = {.type = UOP_ALU, .latency = 1},
        .uop[2] = {.type = UOP_ALU, .latency = 1},
        .uop[3] = {.type = UOP_ALU, .latency = 1},
        .uop[4] = {.type = UOP_ALU, .latency = 1},
        .uop[5] = {.type = UOP_ALU, .latency = 1}
};
static const macro_op_t complex_alup0_1_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALUP0, .latency = 1}
};
static const macro_op_t alup0_3_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALUP0, .latency = 1},
        .uop[1] = {.type = UOP_ALUP0, .latency = 1},
        .uop[2] = {.type = UOP_ALUP0, .latency = 1}
};
static const macro_op_t alup0_6_op =
{
        .nr_uops = 6,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALUP0, .latency = 1},
        .uop[1] = {.type = UOP_ALUP0, .latency = 1},
        .uop[2] = {.type = UOP_ALUP0, .latency = 1},
        .uop[3] = {.type = UOP_ALUP0, .latency = 1},
        .uop[4] = {.type = UOP_ALUP0, .latency = 1},
        .uop[5] = {.type = UOP_ALUP0, .latency = 1}
};
static const macro_op_t arpl_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU,   .latency = 3},
        .uop[1] = {.type = UOP_ALU,   .latency = 3}
};
static const macro_op_t bound_op =
{
        .nr_uops = 4,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD, .latency = 1},
        .uop[1] = {.type = UOP_LOAD, .latency = 1},
        .uop[2] = {.type = UOP_ALU,  .latency = 1},
        .uop[3] = {.type = UOP_ALU,  .latency = 1}
};
static const macro_op_t bsx_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU, .latency = 10}
};
static const macro_op_t call_far_op =
{
        .nr_uops = 4,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU,     .latency = 3},
        .uop[1] = {.type = UOP_STORED,  .latency = 1},
        .uop[2] = {.type = UOP_STOREA,  .latency = 1},
        .uop[3] = {.type = UOP_BRANCH,  .latency = 1}
};
static const macro_op_t cli_sti_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU, .latency = 7}
};
static const macro_op_t cmps_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,   .latency = 1},
        .uop[1] = {.type = UOP_ALU,    .latency = 1},
        .uop[2] = {.type = UOP_ALU,    .latency = 1}
};
static const macro_op_t cmpsb_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,    .latency = 1},
        .uop[1] = {.type = UOP_ALUP0,   .latency = 1},
        .uop[2] = {.type = UOP_ALU,     .latency = 1}
};
static const macro_op_t cmpxchg_op =
{
        .nr_uops = 4,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,   .latency = 1},
        .uop[1] = {.type = UOP_ALU,    .latency = 1},
        .uop[2] = {.type = UOP_STORED, .latency = 1},
        .uop[3] = {.type = UOP_STOREA, .latency = 1}
};
static const macro_op_t cmpxchg_b_op =
{
        .nr_uops = 4,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,    .latency = 1},
        .uop[1] = {.type = UOP_ALUP0,   .latency = 1},
        .uop[2] = {.type = UOP_STORED,  .latency = 1},
        .uop[3] = {.type = UOP_STOREA,  .latency = 1}
};
static const macro_op_t complex_push_mem_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_STORED, .latency = 1},
        .uop[1] = {.type = UOP_STOREA, .latency = 1}
};

static const macro_op_t cpuid_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU, .latency = 23}
};
static const macro_op_t div16_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALUP0, .latency = 21}
};
static const macro_op_t div16_mem_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,    .latency =  1},
        .uop[1] = {.type = UOP_ALUP0,   .latency = 21}
};
static const macro_op_t div32_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALUP0, .latency = 37}
};
static const macro_op_t div32_mem_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,   .latency = 1},
        .uop[1] = {.type = UOP_ALUP0,  .latency = 37}
};
static const macro_op_t emms_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU, .latency = 50}
};
static const macro_op_t enter_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_STORED, .latency =  1},
        .uop[1] = {.type = UOP_STOREA, .latency =  1},
        .uop[2] = {.type = UOP_ALU,    .latency = 10}
};
static const macro_op_t femms_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU, .latency = 6}
};
static const macro_op_t in_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD, .latency = 18}
};
static const macro_op_t ins_op =
{
        .nr_uops = 4,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,   .latency = 18},
        .uop[1] = {.type = UOP_STORED, .latency =  1},
        .uop[2] = {.type = UOP_STOREA, .latency =  1},
        .uop[3] = {.type = UOP_ALU,    .latency =  1}
};
static const macro_op_t int_op =
{
        .nr_uops = 8,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU,     .latency = 20},
        .uop[1] = {.type = UOP_STORED,  .latency =  1},
        .uop[2] = {.type = UOP_STOREA,  .latency =  1},
        .uop[3] = {.type = UOP_STORED,  .latency =  1},
        .uop[4] = {.type = UOP_STOREA,  .latency =  1},
        .uop[5] = {.type = UOP_STORED,  .latency =  1},
        .uop[6] = {.type = UOP_STOREA,  .latency =  1},
        .uop[7] = {.type = UOP_BRANCH,  .latency =  1}
};
static const macro_op_t iret_op =
{
        .nr_uops = 5,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,   .latency =  3},
        .uop[1] = {.type = UOP_LOAD,   .latency =  3},
        .uop[2] = {.type = UOP_LOAD,   .latency =  3},
        .uop[3] = {.type = UOP_ALU,    .latency = 20},
        .uop[4] = {.type = UOP_BRANCH, .latency =  1}
};
static const macro_op_t invd_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU, .latency = 500}
};
static const macro_op_t jmp_far_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU,    .latency = 3},
        .uop[1] = {.type = UOP_BRANCH, .latency = 1}
};
static const macro_op_t lss_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,   .latency = 1},
        .uop[1] = {.type = UOP_LOAD,   .latency = 1},
        .uop[2] = {.type = UOP_ALU,    .latency = 3}
};
static const macro_op_t mov_mem_seg_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,   .latency = 1},
        .uop[1] = {.type = UOP_STORED, .latency = 1},
        .uop[2] = {.type = UOP_STOREA, .latency = 1},
};
static const macro_op_t mov_seg_mem_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,   .latency = 1},
        .uop[1] = {.type = UOP_ALU,    .latency = 3}
};
static const macro_op_t mov_seg_reg_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU,   .latency = 3}
};
static const macro_op_t mul_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SIMPLE,
        .uop[0] = {.type = UOP_ALUP0, .latency = 1}
};
static const macro_op_t mul_mem_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,    .latency = 1},
        .uop[1] = {.type = UOP_ALUP0,   .latency = 1}
};
static const macro_op_t mul64_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALUP0, .latency = 1},
        .uop[1] = {.type = UOP_ALUP0, .latency = 1},
        .uop[2] = {.type = UOP_ALUP0, .latency = 1}
};
static const macro_op_t mul64_mem_op =
{
        .nr_uops = 4,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,    .latency = 1},
        .uop[1] = {.type = UOP_ALUP0,   .latency = 1},
        .uop[2] = {.type = UOP_ALUP0,   .latency = 1},
        .uop[3] = {.type = UOP_ALUP0,   .latency = 1}
};
static const macro_op_t out_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU,    .latency = 18}
};
static const macro_op_t outs_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,   .latency =  1},
        .uop[1] = {.type = UOP_ALU,    .latency = 18}
};
static const macro_op_t pusha_op =
{
        .nr_uops = 8,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_STORED, .latency = 2},
        .uop[1] = {.type = UOP_STOREA, .latency = 2},
        .uop[2] = {.type = UOP_STORED, .latency = 2},
        .uop[3] = {.type = UOP_STOREA, .latency = 2},
        .uop[4] = {.type = UOP_STORED, .latency = 2},
        .uop[5] = {.type = UOP_STOREA, .latency = 2},
        .uop[6] = {.type = UOP_STORED, .latency = 2},
        .uop[7] = {.type = UOP_STOREA, .latency = 2}
};
static const macro_op_t popa_op =
{
        .nr_uops = 8,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD, .latency = 1},
        .uop[1] = {.type = UOP_LOAD, .latency = 1},
        .uop[2] = {.type = UOP_LOAD, .latency = 1},
        .uop[3] = {.type = UOP_LOAD, .latency = 1},
        .uop[4] = {.type = UOP_LOAD, .latency = 1},
        .uop[5] = {.type = UOP_LOAD, .latency = 1},
        .uop[6] = {.type = UOP_LOAD, .latency = 1},
        .uop[7] = {.type = UOP_LOAD, .latency = 1}
};
static const macro_op_t popf_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,  .latency =  1},
        .uop[1] = {.type = UOP_ALU,   .latency =  6},
        .uop[2] = {.type = UOP_ALUP0, .latency = 10}
};
static const macro_op_t pushf_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALUP0,   .latency = 1},
        .uop[1] = {.type = UOP_STORED,  .latency = 1},
        .uop[2] = {.type = UOP_STOREA,  .latency = 1}
};
static const macro_op_t ret_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,   .latency = 1},
        .uop[1] = {.type = UOP_BRANCH, .latency = 1}
};
static const macro_op_t retf_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,   .latency = 1},
        .uop[1] = {.type = UOP_ALU,    .latency = 3},
        .uop[2] = {.type = UOP_BRANCH, .latency = 1}
};
static const macro_op_t scas_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,   .latency = 1},
        .uop[1] = {.type = UOP_ALU,    .latency = 1}
};
static const macro_op_t scasb_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,   .latency = 1},
        .uop[1] = {.type = UOP_ALU,    .latency = 1}
};
static const macro_op_t setcc_mem_op =
{
        .nr_uops = 4,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALUP0,    .latency = 1},
        .uop[1] = {.type = UOP_ALUP0,    .latency = 1},
        .uop[2] = {.type = UOP_FSTORED,  .latency = 1},
        .uop[3] = {.type = UOP_FSTOREA,  .latency = 1}
};
static const macro_op_t setcc_reg_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALUP0,  .latency = 1},
        .uop[1] = {.type = UOP_ALUP0,  .latency = 1},
        .uop[2] = {.type = UOP_ALU,    .latency = 1}
};
static const macro_op_t test_mem_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD, .latency = 1},
        .uop[1] = {.type = UOP_ALU,  .latency = 1}
};
static const macro_op_t test_mem_b_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,  .latency = 1},
        .uop[1] = {.type = UOP_ALUP0, .latency = 1}
};
static const macro_op_t xchg_mem_op =
{
        .nr_uops = 4,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_LOAD,   .latency = 1},
        .uop[1] = {.type = UOP_STORED, .latency = 1},
        .uop[2] = {.type = UOP_STOREA, .latency = 1},
        .uop[3] = {.type = UOP_ALU,    .latency = 1}
};
static const macro_op_t xlat_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU,    .latency = 1},
        .uop[1] = {.type = UOP_LOAD,   .latency = 1}
};
static const macro_op_t wbinvd_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_COMPLEX,
        .uop[0] = {.type = UOP_ALU, .latency = 10000}
};
#define INVALID NULL

static const macro_op_t *opcode_timings[256] =
{
/*      ADD                    ADD                    ADD                   ADD*/
/*00*/  &alup0_store_op,       &alu_store_op,         &load_alup0_op,       &load_alu_op,
/*      ADD                    ADD                    PUSH ES               POP ES*/
        &alup0_op,             &alu_op,               &push_seg_op,         &mov_seg_mem_op,
/*      OR                     OR                     OR                    OR*/
        &alup0_store_op,       &alu_store_op,         &load_alup0_op,       &load_alu_op,
/*      OR                     OR                     PUSH CS               */
        &alup0_op,             &alu_op,               &push_seg_op,         INVALID,

/*      ADC                    ADC                    ADC                   ADC*/
/*10*/  &alup0_store_op,       &alu_store_op,         &load_alup0_op,       &load_alu_op,
/*      ADC                    ADC                    PUSH SS               POP SS*/
        &complex_alup0_1_op,   &complex_alu1_op,      &push_seg_op,         &mov_seg_mem_op,
/*      SBB                    SBB                    SBB                   SBB*/
/*10*/  &alup0_store_op,       &alu_store_op,         &load_alup0_op,       &load_alu_op,
/*      SBB                    SBB                    PUSH DS               POP DS*/
        &complex_alup0_1_op,   &complex_alu1_op,      &push_seg_op,         &mov_seg_mem_op,

/*      AND                    AND                    AND                   AND*/
/*20*/  &alup0_store_op,       &alu_store_op,         &load_alup0_op,       &load_alu_op,
/*      AND                    AND                                          DAA*/
        &alup0_op,             &alu_op,               INVALID,              &complex_alup0_1_op,
/*      SUB                    SUB                    SUB                   SUB*/
        &alup0_store_op,       &alu_store_op,         &load_alup0_op,       &load_alu_op,
/*      SUB                    SUB                                          DAS*/
        &alup0_op,             &alu_op,               INVALID,              &complex_alup0_1_op,

/*      XOR                    XOR                    XOR                   XOR*/
/*30*/  &alup0_store_op,       &alu_store_op,         &load_alup0_op,       &load_alu_op,
/*      XOR                    XOR                                          AAA*/
        &alup0_op,             &alu_op,               INVALID,              &alup0_6_op,
/*      CMP                    CMP                    CMP                   CMP*/
        &load_alup0_op,        &load_alu_op,          &load_alup0_op,       &load_alu_op,
/*      CMP                    CMP                                          AAS*/
        &alup0_op,             &alu_op,               INVALID,              &alup0_6_op,

/*      INC EAX                INC ECX                INC EDX                INC EBX*/
/*40*/  &alu_op,               &alu_op,               &alu_op,               &alu_op,
/*      INC ESP                INC EBP                INC ESI                INC EDI*/
        &alu_op,               &alu_op,               &alu_op,               &alu_op,
/*      DEC EAX                DEC ECX                DEC EDX                DEC EBX*/
        &alu_op,               &alu_op,               &alu_op,               &alu_op,
/*      DEC ESP                DEC EBP                DEC ESI                DEC EDI*/
        &alu_op,               &alu_op,               &alu_op,               &alu_op,

/*      PUSH EAX               PUSH ECX               PUSH EDX               PUSH EBX*/
/*50*/  &store_op,             &store_op,             &store_op,             &store_op,
/*      PUSH ESP               PUSH EBP               PUSH ESI               PUSH EDI*/
        &store_op,             &store_op,             &store_op,             &store_op,
/*      POP EAX                POP ECX                POP EDX                POP EBX*/
        &pop_reg_op,           &pop_reg_op,           &pop_reg_op,           &pop_reg_op,
/*      POP ESP                POP EBP                POP ESI                POP EDI*/
        &pop_reg_op,           &pop_reg_op,           &pop_reg_op,           &pop_reg_op,

/*      PUSHA                  POPA                   BOUND                  ARPL*/
/*60*/  &pusha_op,             &popa_op,              &bound_op,             &arpl_op,
        INVALID,               INVALID,               INVALID,               INVALID,
/*      PUSH imm               IMUL                   PUSH imm               IMUL*/
        &push_imm_op,          &mul_op,               &push_imm_op,          &mul_op,
/*      INSB                   INSW                   OUTSB                  OUTSW*/
        &ins_op,               &ins_op,               &outs_op,              &outs_op,

/*      Jxx*/
/*70*/  &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,

/*80*/  INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      TEST                       TEST                       XCHG                       XCHG*/
        &test_mem_b_op,            &test_mem_op,              &xchg_mem_op,              &xchg_mem_op,
/*      MOV                        MOV                        MOV                        MOV*/
        &store_op,                 &store_op,                 &load_op,                  &load_op,
/*      MOV from seg               LEA                        MOV to seg                 POP*/
        &mov_mem_seg_op,           &store_op,                 &mov_seg_mem_op,           &pop_mem_op,

/*      NOP                        XCHG                       XCHG                       XCHG*/
/*90*/  &fxch_op,                  &xchg_op,                  &xchg_op,                  &xchg_op,
/*      XCHG                       XCHG                       XCHG                       XCHG*/
        &xchg_op,                  &xchg_op,                  &xchg_op,                  &xchg_op,
/*      CBW                        CWD                        CALL far                   WAIT*/
        &complex_alu1_op,          &complex_alu1_op,          &call_far_op,              &fxch_op,
/*      PUSHF                      POPF                       SAHF                       LAHF*/
        &pushf_op,                 &popf_op,                  &complex_alup0_1_op,       &complex_alup0_1_op,

/*      MOV                        MOV                        MOV                        MOV*/
/*a0*/  &load_op,                  &load_op,                  &store_op,                 &store_op,
/*      MOVSB                      MOVSW                      CMPSB                      CMPSW*/
        &movs_op,                  &movs_op,                  &cmpsb_op,                 &cmps_op,
/*      TEST                       TEST                       STOSB                      STOSW*/
        &test_reg_b_op,            &test_reg_op,              &stos_op,                  &stos_op,
/*      LODSB                      LODSW                      SCASB                      SCASW*/
        &lods_op,                  &lods_op,                  &scasb_op,                 &scas_op,

/*      MOV*/
/*b0*/  &alu_op,       &alu_op,       &alu_op,       &alu_op,
        &alu_op,       &alu_op,       &alu_op,       &alu_op,
        &alu_op,       &alu_op,       &alu_op,       &alu_op,
        &alu_op,       &alu_op,       &alu_op,       &alu_op,

/*                                                            RET imm                    RET*/
/*c0*/  INVALID,                   INVALID,                   &ret_op,                   &ret_op,
/*      LES                        LDS                        MOV                        MOV*/
        &lss_op,                   &lss_op,                   &store_op,                 &store_op,
/*      ENTER                      LEAVE                      RETF                       RETF*/
        &enter_op,                 &leave_op,                 &retf_op,                  &retf_op,
/*      INT3                       INT                        INTO                       IRET*/
        &int_op,                   &int_op,                   &int_op,                   &iret_op,


/*d0*/  INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      AAM                        AAD                        SETALC                     XLAT*/
        &alup0_6_op,               &alup0_3_op,               &complex_alup0_1_op,       &xlat_op,
        INVALID,                   INVALID,                   INVALID,                   INVALID,
        INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      LOOPNE                     LOOPE                      LOOP                       JCXZ*/
/*e0*/  &loop_op,                  &loop_op,                  &loop_op,                  &loop_op,
/*      IN AL                      IN AX                      OUT_AL                     OUT_AX*/
        &in_op,                    &in_op,                    &out_op,                   &out_op,
/*      CALL                       JMP                        JMP                        JMP*/
        &store_op,                 &branch_op,                &jmp_far_op,               &branch_op,
/*      IN AL                      IN AX                      OUT_AL                     OUT_AX*/
        &in_op,                    &in_op,                    &out_op,                   &out_op,

/*                                                            REPNE                      REPE*/
/*f0*/  INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      HLT                        CMC*/
        &complex_alup0_1_op,       &alu2_op,                  INVALID,                   INVALID,
/*      CLC                        STC                        CLI                        STI*/
        &complex_alu1_op,          &complex_alu1_op,          &cli_sti_op,               &cli_sti_op,
/*      CLD                        STD                        INCDEC*/
        &complex_alu1_op,          &complex_alu1_op,          &alup0_store_op,           INVALID
};

static const macro_op_t *opcode_timings_mod3[256] =
{
/*      ADD                       ADD                       ADD                       ADD*/
/*00*/  &alup0_op,                &alu_op,                  &alup0_op,                &alu_op,
/*      ADD                       ADD                       PUSH ES                   POP ES*/
        &alup0_op,                &alu_op,                  &push_seg_op,             &mov_seg_mem_op,
/*      OR                        OR                        OR                        OR*/
        &alup0_op,                &alu_op,                  &alup0_op,                &alu_op,
/*      OR                        OR                        PUSH CS                   */
        &alup0_op,                &alu_op,                  &push_seg_op,             INVALID,

/*      ADC                       ADC                       ADC                       ADC*/
/*10*/  &complex_alup0_1_op,      &complex_alu1_op,         &complex_alup0_1_op,      &complex_alu1_op,
/*      ADC                       ADC                       PUSH SS                   POP SS*/
        &complex_alup0_1_op,      &complex_alu1_op,         &push_seg_op,             &mov_seg_mem_op,
/*      SBB                       SBB                       SBB                       SBB*/
        &complex_alup0_1_op,      &complex_alu1_op,         &complex_alup0_1_op,      &complex_alu1_op,
/*      SBB                       SBB                       PUSH DS                   POP DS*/
        &complex_alup0_1_op,      &complex_alu1_op,         &push_seg_op,             &mov_seg_mem_op,

/*      AND                       AND                       AND                       AND*/
/*20*/  &alup0_op,                &alu_op,                  &alup0_op,                &alu_op,
/*      AND                       AND                                                 DAA*/
        &alup0_op,                &alu_op,                  INVALID,                  &complex_alup0_1_op,
/*      SUB                       SUB                       SUB                       SUB*/
        &alup0_op,                &alu_op,                  &alup0_op,                &alu_op,
/*      SUB                       SUB                                                 DAS*/
        &alup0_op,                &alu_op,                  INVALID,                  &complex_alup0_1_op,

/*      XOR                       XOR                       XOR                       XOR*/
/*30*/  &alup0_op,                &alu_op,                  &alup0_op,                &alu_op,
/*      XOR                       XOR                                                 AAA*/
        &alup0_op,                &alu_op,                  INVALID,                  &alup0_6_op,
/*      CMP                       CMP                       CMP                       CMP*/
        &alup0_op,                &alu_op,                  &alup0_op,                &alu_op,
/*      CMP                       CMP                                                 AAS*/
        &alup0_op,                &alu_op,                  INVALID,                  &alup0_6_op,

/*      INC EAX                INC ECX                INC EDX                INC EBX*/
/*40*/  &alu_op,               &alu_op,               &alu_op,               &alu_op,
/*      INC ESP                INC EBP                INC ESI                INC EDI*/
        &alu_op,               &alu_op,               &alu_op,               &alu_op,
/*      DEC EAX                DEC ECX                DEC EDX                DEC EBX*/
        &alu_op,               &alu_op,               &alu_op,               &alu_op,
/*      DEC ESP                DEC EBP                DEC ESI                DEC EDI*/
        &alu_op,               &alu_op,               &alu_op,               &alu_op,

/*      PUSH EAX               PUSH ECX               PUSH EDX               PUSH EBX*/
/*50*/  &store_op,             &store_op,             &store_op,             &store_op,
/*      PUSH ESP               PUSH EBP               PUSH ESI               PUSH EDI*/
        &store_op,             &store_op,             &store_op,             &store_op,
/*      POP EAX                POP ECX                POP EDX                POP EBX*/
        &pop_reg_op,           &pop_reg_op,           &pop_reg_op,           &pop_reg_op,
/*      POP ESP                POP EBP                POP ESI                POP EDI*/
        &pop_reg_op,           &pop_reg_op,           &pop_reg_op,           &pop_reg_op,

/*      PUSHA                  POPA                   BOUND                  ARPL*/
/*60*/  &pusha_op,             &popa_op,              &bound_op,             &arpl_op,
        INVALID,               INVALID,               INVALID,               INVALID,
/*      PUSH imm               IMUL                   PUSH imm               IMUL*/
        &push_imm_op,          &mul_op,               &push_imm_op,          &mul_op,
/*      INSB                   INSW                   OUTSB                  OUTSW*/
        &ins_op,               &ins_op,               &outs_op,              &outs_op,

/*      Jxx*/
/*70*/  &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,

/*80*/  INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      TEST                       TEST                       XCHG                       XCHG*/
        &complex_alu1_op,          &complex_alu1_op,          &alu3_op,                  &alu3_op,
/*      MOV                        MOV                        MOV                        MOV*/
        &store_op,                 &store_op,                 &load_op,                  &load_op,
/*      MOV from seg               LEA                        MOV to seg                 POP*/
        &mov_reg_seg_op,           &store_op,                 &mov_seg_reg_op,           &pop_reg_op,

/*      NOP                        XCHG                       XCHG                       XCHG*/
/*90*/  &fxch_op,                  &xchg_op,                  &xchg_op,                  &xchg_op,
/*      XCHG                       XCHG                       XCHG                       XCHG*/
        &xchg_op,                  &xchg_op,                  &xchg_op,                  &xchg_op,
/*      CBW                        CWD                        CALL far                   WAIT*/
        &complex_alu1_op,          &complex_alu1_op,          &call_far_op,              &fxch_op,
/*      PUSHF                      POPF                       SAHF                       LAHF*/
        &pushf_op,                 &popf_op,                  &complex_alup0_1_op,       &complex_alup0_1_op,

/*      MOV                        MOV                        MOV                        MOV*/
/*a0*/  &load_op,                  &load_op,                  &store_op,                 &store_op,
/*      MOVSB                      MOVSW                      CMPSB                      CMPSW*/
        &movs_op,                  &movs_op,                  &cmpsb_op,                 &cmps_op,
/*      TEST                       TEST                       STOSB                      STOSW*/
        &test_reg_b_op,            &test_reg_op,              &stos_op,                  &stos_op,
/*      LODSB                      LODSW                      SCASB                      SCASW*/
        &lods_op,                  &lods_op,                  &scasb_op,                 &scas_op,

/*      MOV*/
/*b0*/  &alu_op,       &alu_op,       &alu_op,       &alu_op,
        &alu_op,       &alu_op,       &alu_op,       &alu_op,
        &alu_op,       &alu_op,       &alu_op,       &alu_op,
        &alu_op,       &alu_op,       &alu_op,       &alu_op,

/*                                                            RET imm                    RET*/
/*c0*/  INVALID,                   INVALID,                   &ret_op,                   &ret_op,
/*      LES                        LDS                        MOV                        MOV*/
        &lss_op,                   &lss_op,                   &store_op,                 &store_op,
/*      ENTER                      LEAVE                      RETF                       RETF*/
        &enter_op,                 &leave_op,                 &retf_op,                  &retf_op,
/*      INT3                       INT                        INTO                       IRET*/
        &int_op,                   &int_op,                   &int_op,                   &iret_op,


/*d0*/  INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      AAM                        AAD                        SETALC                     XLAT*/
        &alup0_6_op,               &alup0_3_op,               &complex_alup0_1_op,       &xlat_op,
        INVALID,                   INVALID,                   INVALID,                   INVALID,
        INVALID,                   INVALID,                   INVALID,                   INVALID,

/*      LOOPNE                     LOOPE                      LOOP                       JCXZ*/
/*e0*/  &loop_op,                  &loop_op,                  &loop_op,                  &loop_op,
/*      IN AL                      IN AX                      OUT_AL                     OUT_AX*/
        &in_op,                    &in_op,                    &out_op,                   &out_op,
/*      CALL                       JMP                        JMP                        JMP*/
        &store_op,                 &branch_op,                &jmp_far_op,               &branch_op,
/*      IN AL                      IN AX                      OUT_AL                     OUT_AX*/
        &in_op,                    &in_op,                    &out_op,                   &out_op,

/*                                                            REPNE                      REPE*/
/*f0*/  INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      HLT                        CMC*/
        &complex_alup0_1_op,       &alu2_op,                  INVALID,                   INVALID,
/*      CLC                        STC                        CLI                        STI*/
        &complex_alu1_op,          &complex_alu1_op,          &cli_sti_op,               &cli_sti_op,
/*      CLD                        STD                        INCDEC*/
        &complex_alu1_op,          &complex_alu1_op,          &complex_alup0_1_op,       INVALID
};

static const macro_op_t *opcode_timings_0f[256] =
{
/*00*/  &alu6_op,               &alu6_op,               &alu6_op,               &alu6_op,
        INVALID,                &alu6_op,               &alu6_op,               INVALID,
        &invd_op,               &wbinvd_op,             INVALID,                INVALID,
        INVALID,                &load_op,               &femms_op,              INVALID,

/*10*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*20*/  &alu6_op,               &alu6_op,               &alu6_op,               &alu6_op,
        &alu6_op,               &alu6_op,               INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*30*/  &alu6_op,               &alu6_op,               &alu6_op,               INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*40*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*50*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*60*/  &load_mmx_op,           &load_mmx_op,           &load_mmx_op,           &load_mmx_op,
        &load_mmx_op,           &load_mmx_op,           &load_mmx_op,           &load_mmx_op,
        &load_mmx_op,           &load_mmx_op,           &load_mmx_op,           &load_mmx_op,
        INVALID,                INVALID,                &mload_op,              &mload_op,

/*70*/  INVALID,                &load_mmx_shift_op,     &load_mmx_shift_op,     &load_mmx_shift_op,
        &load_mmx_op,           &load_mmx_op,           &load_mmx_op,           &emms_op,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                &mstore_op,             &mstore_op,

/*80*/  &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,

/*90*/  &setcc_reg_op,           &setcc_reg_op,          &setcc_reg_op,          &setcc_reg_op,
        &setcc_reg_op,           &setcc_reg_op,          &setcc_reg_op,          &setcc_reg_op,
        &setcc_reg_op,           &setcc_reg_op,          &setcc_reg_op,          &setcc_reg_op,
        &setcc_reg_op,           &setcc_reg_op,          &setcc_reg_op,          &setcc_reg_op,

/*a0*/  &push_seg_op,            &mov_seg_mem_op,        &cpuid_op,              &load_alu_op,
        &alu_store_op,           &alu_store_op,          INVALID,                INVALID,
        &push_seg_op,            &mov_seg_mem_op,        INVALID,                &load_alu_op,
        &alu_store_op,           &alu_store_op,          INVALID,                &mul_op,

/*b0*/  &cmpxchg_b_op,           &cmpxchg_op,            &lss_op,                &load_alu_op,
        &lss_op,                 &lss_op,                &load_alup0_op,         &load_alu_op,
        INVALID,                 INVALID,                &load_alu_op,           &load_alu_op,
        &bsx_op,                 &bsx_op,                &load_alup0_op,         &load_alu_op,

/*c0*/  &alup0_store_op,         &alu_store_op,          INVALID,                INVALID,
        INVALID,                 INVALID,                INVALID,                &cmpxchg_op,
        &bswap_op,               &bswap_op,              &bswap_op,              &bswap_op,
        &bswap_op,               &bswap_op,              &bswap_op,              &bswap_op,

/*d0*/  INVALID,                 &load_mmx_shift_op,     &load_mmx_shift_op,     &load_mmx_shift_op,
        INVALID,                 &load_mmx_mul_op,       INVALID,                INVALID,
        &load_mmx_op,            &load_mmx_op,           INVALID,                &load_mmx_op,
        &load_mmx_op,            &load_mmx_op,           INVALID,                &load_mmx_op,

/*e0*/  &load_mmx_op,            &load_mmx_shift_op,     &load_mmx_shift_op,     INVALID,
        INVALID,                 &pmul_mem_op,           INVALID,                INVALID,
        &load_mmx_op,            &load_mmx_op,           INVALID,                &load_mmx_op,
        &load_mmx_op,            &load_mmx_op,           INVALID,                &load_mmx_op,

/*f0*/  INVALID,                 &load_mmx_shift_op,     &load_mmx_shift_op,     &load_mmx_shift_op,
        INVALID,                 &pmul_mem_op,           INVALID,                INVALID,
        &load_mmx_op,            &load_mmx_op,           &load_mmx_op,           INVALID,
        &load_mmx_op,            &load_mmx_op,           &load_mmx_op,           INVALID,
};
static const macro_op_t *opcode_timings_0f_mod3[256] =
{
/*00*/  &alu6_op,                &alu6_op,               &alu6_op,               &alu6_op,
        INVALID,                 &alu6_op,               &alu6_op,               INVALID,
        &invd_op,                &wbinvd_op,             INVALID,                INVALID,
        INVALID,                 INVALID,                &femms_op,              INVALID,

/*10*/  INVALID,                 INVALID,                INVALID,                INVALID,
        INVALID,                 INVALID,                INVALID,                INVALID,
        INVALID,                 INVALID,                INVALID,                INVALID,
        INVALID,                 INVALID,                INVALID,                INVALID,

/*20*/  &alu6_op,                &alu6_op,               &alu6_op,               &alu6_op,
        &alu6_op,                &alu6_op,               INVALID,                INVALID,
        INVALID,                 INVALID,                INVALID,                INVALID,
        INVALID,                 INVALID,                INVALID,                INVALID,

/*30*/  &alu6_op,                &alu6_op,               &alu6_op,               INVALID,
        INVALID,                 INVALID,                INVALID,                INVALID,
        INVALID,                 INVALID,                INVALID,                INVALID,
        INVALID,                 INVALID,                INVALID,                INVALID,

/*40*/  INVALID,                 INVALID,                INVALID,                INVALID,
        INVALID,                 INVALID,                INVALID,                INVALID,
        INVALID,                 INVALID,                INVALID,                INVALID,
        INVALID,                 INVALID,                INVALID,                INVALID,

/*50*/  INVALID,                 INVALID,                INVALID,                INVALID,
        INVALID,                 INVALID,                INVALID,                INVALID,
        INVALID,                 INVALID,                INVALID,                INVALID,
        INVALID,                 INVALID,                INVALID,                INVALID,

/*60*/  &mmx_op,                 &mmx_op,                &mmx_op,                &mmx_op,
        &mmx_op,                 &mmx_op,                &mmx_op,                &mmx_op,
        &mmx_op,                 &mmx_op,                &mmx_op,                &mmx_op,
        INVALID,                 INVALID,                &mmx_op,                &mmx_op,

/*70*/  INVALID,                 &mmx_shift_op,          &mmx_shift_op,          &mmx_shift_op,
        &mmx_op,                 &mmx_op,                &mmx_op,                &emms_op,
        INVALID,                 INVALID,                INVALID,                INVALID,
        INVALID,                 INVALID,                &mmx_op,                &mmx_op,

/*80*/  &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,

/*90*/  &setcc_mem_op,           &setcc_mem_op,          &setcc_mem_op,          &setcc_mem_op,
        &setcc_mem_op,           &setcc_mem_op,          &setcc_mem_op,          &setcc_mem_op,
        &setcc_mem_op,           &setcc_mem_op,          &setcc_mem_op,          &setcc_mem_op,
        &setcc_mem_op,           &setcc_mem_op,          &setcc_mem_op,          &setcc_mem_op,

/*a0*/  &push_seg_op,            &mov_seg_mem_op,        &cpuid_op,              &complex_alu1_op,
        &complex_alu1_op,        &complex_alu1_op,       INVALID,                INVALID,
        &push_seg_op,            &mov_seg_mem_op,        INVALID,                &complex_alu1_op,
        &complex_alu1_op,        &complex_alu1_op,       INVALID,                &mul_op,

/*b0*/  &cmpxchg_b_op,           &cmpxchg_op,            &lss_op,                &complex_alu1_op,
        &lss_op,                 &lss_op,                &alup0_op,              &alu_op,
        INVALID,                 INVALID,                &complex_alu1_op,       &complex_alu1_op,
        &bsx_op,                 &bsx_op,                &alup0_op,              &alu_op,

/*c0*/  &complex_alup0_1_op,     &complex_alu1_op,       INVALID,                INVALID,
        INVALID,                 INVALID,                INVALID,                INVALID,
        &bswap_op,               &bswap_op,              &bswap_op,              &bswap_op,
        &bswap_op,               &bswap_op,              &bswap_op,              &bswap_op,

/*d0*/  INVALID,                 &mmx_shift_op,          &mmx_shift_op,          &mmx_shift_op,
        INVALID,                 &mmx_mul_op,            INVALID,                INVALID,
        &mmx_op,                 &mmx_op,                INVALID,                &mmx_op,
        &mmx_op,                 &mmx_op,                INVALID,                &mmx_op,

/*e0*/  &mmx_op,                 &mmx_shift_op,          &mmx_shift_op,          INVALID,
        INVALID,                 &pmul_op,               INVALID,                INVALID,
        &mmx_op,                 &mmx_op,                INVALID,                &mmx_op,
        &mmx_op,                 &mmx_op,                INVALID,                &mmx_op,

/*f0*/  INVALID,                 &mmx_shift_op,          &mmx_shift_op,          &mmx_shift_op,
        INVALID,                 &pmul_op,               INVALID,                INVALID,
        &mmx_op,                 &mmx_op,                &mmx_op,                INVALID,
        &mmx_op,                 &mmx_op,                &mmx_op,                INVALID,
};

static const macro_op_t *opcode_timings_shift[8] =
{
        &alu_store_op,   &alu_store_op,   &alu_store_op,   &alu_store_op,
        &alu_store_op,   &alu_store_op,   &alu_store_op,   &alu_store_op
};
static const macro_op_t *opcode_timings_shift_b[8] =
{
        &alup0_store_op,  &alup0_store_op,  &alup0_store_op,  &alup0_store_op,
        &alup0_store_op,  &alup0_store_op,  &alup0_store_op,  &alup0_store_op
};
static const macro_op_t *opcode_timings_shift_mod3[8] =
{
        &complex_alu1_op,   &complex_alu1_op,   &complex_alu1_op,   &complex_alu1_op,
        &alu_op,            &alu_op,            &alu_op,            &alu_op
};
static const macro_op_t *opcode_timings_shift_b_mod3[8] =
{
        &complex_alup0_1_op,  &complex_alup0_1_op,  &complex_alup0_1_op,  &complex_alup0_1_op,
        &alup0_op,            &alup0_op,            &alup0_op,            &alup0_op
};

static const macro_op_t *opcode_timings_80[8] =
{
        &alup0_store_op, &alup0_store_op, &alup0_store_op,  &alup0_store_op,
        &alup0_store_op, &alup0_store_op, &alup0_store_op,  &alup0_store_op,
};
static const macro_op_t *opcode_timings_80_mod3[8] =
{
        &alup0_op,       &alup0_op,       &alup0_store_op,  &alup0_store_op,
        &alup0_op,       &alup0_op,       &alup0_op,        &alup0_op,
};
static const macro_op_t *opcode_timings_8x[8] =
{
        &alu_store_op,  &alu_store_op,    &alu_store_op,    &alu_store_op,
        &alu_store_op,  &alu_store_op,    &alu_store_op,    &alu_store_op,
};
static const macro_op_t *opcode_timings_8x_mod3[8] =
{
        &alu_op,        &alu_op,          &alu_store_op,    &alu_store_op,
        &alu_op,        &alu_op,          &alu_op,          &alu_op,
};

static const macro_op_t *opcode_timings_f6[8] =
{
/*      TST                                             NOT                     NEG*/
        &test_mem_imm_b_op,     INVALID,                &alup0_store_op,        &alup0_store_op,
/*      MUL                     IMUL                    DIV                     IDIV*/
        &mul_mem_op,            &mul_mem_op,            &div16_mem_op,          &div16_mem_op,
};
static const macro_op_t *opcode_timings_f6_mod3[8] =
{
/*      TST                                             NOT                     NEG*/
        &test_reg_b_op,         INVALID,                &alup0_op,              &alup0_op,
/*      MUL                     IMUL                    DIV                     IDIV*/
        &mul_op,                &mul_op,                &div16_op,              &div16_op,
};
static const macro_op_t *opcode_timings_f7[8] =
{
/*      TST                                             NOT                     NEG*/
        &test_mem_imm_op,       INVALID,                &alu_store_op,          &alu_store_op,
/*      MUL                     IMUL                    DIV                     IDIV*/
        &mul64_mem_op,          &mul64_mem_op,          &div32_mem_op,          &div32_mem_op,
};
static const macro_op_t *opcode_timings_f7_mod3[8] =
{
/*      TST                                             NOT                     NEG*/
        &test_reg_op,           INVALID,                &alu_op,                &alu_op,
/*      MUL                     IMUL                    DIV                     IDIV*/
        &mul64_op,              &mul64_op,              &div32_op,              &div32_op,
};
static const macro_op_t *opcode_timings_ff[8] =
{
/*      INC                     DEC                     CALL                    CALL far*/
        &alu_store_op,          &alu_store_op,          &store_op,              &call_far_op,
/*      JMP                     JMP far                 PUSH*/
        &branch_op,             &jmp_far_op,            &push_mem_op,           INVALID
};
static const macro_op_t *opcode_timings_ff_mod3[8] =
{
/*      INC                     DEC                     CALL                    CALL far*/
        &complex_alu1_op,       &complex_alu1_op,       &store_op,              &call_far_op,
/*      JMP                     JMP far                 PUSH*/
        &branch_op,             &jmp_far_op,            &complex_push_mem_op,   INVALID
};

static const macro_op_t *opcode_timings_d8[8] =
{
/*      FADDs            FMULs            FCOMs            FCOMPs*/
        &load_fadd_op,   &load_fmul_op,   &load_float_op,  &load_float_op,
/*      FSUBs            FSUBRs           FDIVs            FDIVRs*/
        &load_float_op,  &load_float_op,  &fdiv_mem_op,    &fdiv_mem_op,
};
static const macro_op_t *opcode_timings_d8_mod3[8] =
{
/*      FADD             FMUL             FCOM             FCOMP*/
        &fadd_op,        &fmul_op,        &float_op,       &float_op,
/*      FSUB             FSUBR            FDIV             FDIVR*/
        &float_op,       &float_op,       &fdiv_op,        &fdiv_op,
};

static const macro_op_t *opcode_timings_d9[8] =
{
/*      FLDs                                    FSTs                 FSTPs*/
        &load_float_op,      INVALID,           &fstore_op,          &fstore_op,
/*      FLDENV               FLDCW              FSTENV               FSTCW*/
        &complex_float_l_op, &fldcw_op,         &complex_float_l_op, &complex_float_op
};
static const macro_op_t *opcode_timings_d9_mod3[64] =
{
        /*FLD*/
        &float_op,    &float_op,    &float_op,    &float_op,
        &float_op,    &float_op,    &float_op,    &float_op,
        /*FXCH*/
        &fxch_op,     &fxch_op,     &fxch_op,     &fxch_op,
        &fxch_op,     &fxch_op,     &fxch_op,     &fxch_op,
        /*FNOP*/
        &float_op,    INVALID,      INVALID,      INVALID,
        INVALID,      INVALID,      INVALID,      INVALID,
        /*FSTP*/
        &float2_op,   &float2_op,   &float2_op,   &float2_op,
        &float2_op,   &float2_op,   &float2_op,   &float2_op,
/*      opFCHS        opFABS*/
        &fchs_op,     &float_op,    INVALID,      INVALID,
/*      opFTST        opFXAM*/
        &float_op,    &float_op,    INVALID,      INVALID,
/*      opFLD1        opFLDL2T      opFLDL2E      opFLDPI*/
        &float_op,    &float_op,    &float_op,    &float_op,
/*      opFLDEG2      opFLDLN2      opFLDZ*/
        &float_op,    &float_op,    &float_op,    INVALID,
/*      opF2XM1       opFYL2X       opFPTAN       opFPATAN*/
        &fsin_op,     &fsin_op,     &fsin_op,     &fsin_op,
/*                                  opFDECSTP     opFINCSTP,*/
        INVALID,      INVALID,      &float_op,    &float_op,
/*      opFPREM                     opFSQRT       opFSINCOS*/
        &fdiv_op,     INVALID,      &fsqrt_op,    &fsin_op,
/*      opFRNDINT     opFSCALE      opFSIN        opFCOS*/
        &float_op,    &fdiv_op,     &fsin_op,     &fsin_op
};

static const macro_op_t *opcode_timings_da[8] =
{
/*      FIADDl            FIMULl            FICOMl            FICOMPl*/
        &load_fadd_op,    &load_fmul_op,    &load_float_op,   &load_float_op,
/*      FISUBl            FISUBRl           FIDIVl            FIDIVRl*/
        &load_float_op,   &load_float_op,   &fdiv_mem_op,     &fdiv_mem_op,
};
static const macro_op_t *opcode_timings_da_mod3[8] =
{
        INVALID,          INVALID,          INVALID,          INVALID,
/*                        FCOMPP*/
        INVALID,          &float_op,        INVALID,          INVALID
};

static const macro_op_t *opcode_timings_db[8] =
{
/*      FLDil                               FSTil         FSTPil*/
        &load_float_op,   INVALID,          &fstore_op,   &fstore_op,
/*                        FLDe                            FSTPe*/
        INVALID,          &flde_op,         INVALID,      &fste_op
};
static const macro_op_t *opcode_timings_db_mod3[64] =
{
        INVALID,          INVALID,          INVALID,      INVALID,
        INVALID,          INVALID,          INVALID,      INVALID,

        INVALID,          INVALID,          INVALID,      INVALID,
        INVALID,          INVALID,          INVALID,      INVALID,

        INVALID,          INVALID,          INVALID,      INVALID,
        INVALID,          INVALID,          INVALID,      INVALID,

        INVALID,          INVALID,          INVALID,      INVALID,
        INVALID,          INVALID,          INVALID,      INVALID,

/*                        opFNOP            opFCLEX       opFINIT*/
        INVALID,          &float_op,        &float_op,    &float_op,
/*      opFNOP            opFNOP*/
        &float_op,        &float_op,        INVALID,      INVALID,

        INVALID,          INVALID,          INVALID,      INVALID,
        INVALID,          INVALID,          INVALID,      INVALID,

        INVALID,          INVALID,          INVALID,      INVALID,
        INVALID,          INVALID,          INVALID,      INVALID,

        INVALID,          INVALID,          INVALID,      INVALID,
        INVALID,          INVALID,          INVALID,      INVALID,
};

static const macro_op_t *opcode_timings_dc[8] =
{
/*      FADDd             FMULd             FCOMd             FCOMPd*/
        &load_fadd_op,    &load_fmul_op,    &load_float_op,   &load_float_op,
/*      FSUBd             FSUBRd            FDIVd             FDIVRd*/
        &load_float_op,   &load_float_op,   &fdiv_mem_op,     &fdiv_mem_op,
};
static const macro_op_t *opcode_timings_dc_mod3[8] =
{
/*      opFADDr           opFMULr*/
        &fadd_op,         &fmul_op,         INVALID,          INVALID,
/*      opFSUBRr          opFSUBr           opFDIVRr          opFDIVr*/
        &float_op,        &float_op,        &fdiv_op,         &fdiv_op
};

static const macro_op_t *opcode_timings_dd[8] =
{
/*      FLDd                                    FSTd                 FSTPd*/
        &load_float_op,      INVALID,           &fstore_op,          &fstore_op,
/*      FRSTOR                                  FSAVE                FSTSW*/
        &complex_float_l_op, INVALID,           &complex_float_l_op, &complex_float_l_op
};
static const macro_op_t *opcode_timings_dd_mod3[8] =
{
/*      FFFREE                            FST                FSTP*/
        &float_op,       INVALID,         &float_op,         &float_op,
/*      FUCOM            FUCOMP*/
        &float_op,       &float_op,       INVALID,           INVALID
};

static const macro_op_t *opcode_timings_de[8] =
{
/*      FIADDw            FIMULw            FICOMw            FICOMPw*/
        &load_fiadd_op,   &load_fiadd_op,   &load_fiadd_op,   &load_fiadd_op,
/*      FISUBw            FISUBRw           FIDIVw            FIDIVRw*/
        &load_fiadd_op,   &load_fiadd_op,   &load_fiadd_op,   &load_fiadd_op,
};
static const macro_op_t *opcode_timings_de_mod3[8] =
{
/*      FADDP            FMULP                          FCOMPP*/
        &fadd_op,        &fmul_op,        INVALID,      &float_op,
/*      FSUBP            FSUBRP           FDIVP         FDIVRP*/
        &float_op,       &float_op,       &fdiv_op,     &fdiv_op,
};

static const macro_op_t *opcode_timings_df[8] =
{
/*      FILDiw                              FISTiw               FISTPiw*/
        &load_float_op,   INVALID,          &fstore_op,          &fstore_op,
/*                        FILDiq            FBSTP                FISTPiq*/
        INVALID,          &load_float_op,   &complex_float_l_op, &fstore_op,
};
static const macro_op_t *opcode_timings_df_mod3[8] =
{
        INVALID,      INVALID,      INVALID,      INVALID,
/*      FSTSW AX*/
        &float_op,    INVALID,      INVALID,      INVALID
};


static uint8_t last_prefix;
static int prefixes;

static int decode_timestamp;
static int last_complete_timestamp;

typedef struct p6_unit_t
{
        uint32_t uop_mask;
        double first_available_cycle;
} p6_unit_t;

static int nr_units;
static p6_unit_t *units;

/*Pentium Pro has no MMX*/
static p6_unit_t ppro_units[] =
{
        {.uop_mask = (1 << UOP_ALU) | (1 << UOP_ALUP0) | (1 << UOP_FLOAT)},     /*Port 0*/
        {.uop_mask = (1 << UOP_ALU) | (1 << UOP_BRANCH)},            		/*Port 1*/
        {.uop_mask = (1 << UOP_LOAD)  | (1 << UOP_FLOAD)},          		/*Port 2*/
        {.uop_mask = (1 << UOP_STORED) | (1 << UOP_FSTORED)},         		/*Port 3*/
	{.uop_mask = (1 << UOP_STOREA) | (1 << UOP_FSTOREA)},       		/*Port 4*/
};
#define NR_PPRO_UNITS (sizeof(ppro_units) / sizeof(p6_unit_t))

/*Pentium II/Celeron assigns the multiplier to port 0, the shifter to port 1, and shares the MMX ALU*/
static p6_unit_t p2_units[] =
{
        {.uop_mask = (1 << UOP_ALU) | (1 << UOP_ALUP0) | (1 << UOP_FLOAT) |             /*Port 0*/
	                 (1 << UOP_MMX) | (1 << UOP_MMX_MUL)},
        {.uop_mask = (1 << UOP_ALU) | (1 << UOP_BRANCH) |                               /*Port 1*/
	                 (1 << UOP_MMX) | (1 << UOP_MMX_SHIFT)},
        {.uop_mask = (1 << UOP_LOAD)  | (1 << UOP_FLOAD)  | (1 << UOP_MLOAD)},  	/*Port 2*/
        {.uop_mask = (1 << UOP_STORED) | (1 << UOP_FSTORED) | (1 << UOP_MSTORED)},      /*Port 3*/
	{.uop_mask = (1 << UOP_STOREA) | (1 << UOP_FSTOREA) | (1 << UOP_MSTOREA)}, 	/*Port 4*/
};
#define NR_P2_UNITS (sizeof(p2_units) / sizeof(p6_unit_t))

static int uop_run(const p6_uop_t *uop, int decode_time)
{
        int c;
        p6_unit_t *best_unit = NULL;
        int best_start_cycle = 99999;

        /*UOP_FXCH does not require execution*/
        if (uop->type == UOP_FXCH)
               return decode_time;

        /*Find execution unit for this uOP*/
        for (c = 0; c < nr_units; c++)
        {
                if (units[c].uop_mask & (1 << uop->type))
                {
                        if (units[c].first_available_cycle < best_start_cycle)
                        {
                                best_unit = &units[c];
                                best_start_cycle = units[c].first_available_cycle;
                        }
                }
        }
        if (!best_unit)
                  fatal("uop_run: can not find execution unit\n");

        if (best_start_cycle < decode_time)
                best_start_cycle = decode_time;
        best_unit->first_available_cycle = best_start_cycle + uop->latency;



        return best_start_cycle + uop->latency;
}

/*The P6 decoders can decode, per clock :
  - 1 to 3 'simple' instructions, each up to 1 uOP and 7 bytes long
  - 1 'complex' instruction, up to 4 uOPs or 3 per cycle for instructions longer than 4 uOPs
*/
static struct
{
        int nr_uops;
        const p6_uop_t *uops[6];
        /*Earliest time a uop can start. If the timestamp is -1, then the uop is
          part of a dependency chain and the start time is the completion time of
          the previous uop*/
        int earliest_start[6];
} decode_buffer;

#define NR_OPSEQS 3
/*Timestamps of when the last three op sequences completed. Technically this is incorrect,
as the actual size of the opseq buffer is 20 bytes and not 18, but I'm restricted to multiples of 6*/
static int opseq_completion_timestamp[NR_OPSEQS];
static int next_opseq = 0;

#define NR_REGS 8
/*Timestamp of when last operation on an integer register completed*/
static int reg_available_timestamp[NR_REGS];
/*Timestamp of when last operation on an FPU register completed*/
static int fpu_st_timestamp[8];
/*Completion time of the last uop to be processed. Used to calculate timing of
  dependent uop chains*/
static int last_uop_timestamp = 0;

void decode_flush_p6()
{
        int c;
        int start_timestamp, uop_timestamp = 0;

        /*Decoded opseq can not be submitted if there are no free spaces in the
          opseq buffer*/
        if (decode_timestamp < opseq_completion_timestamp[next_opseq])
                decode_timestamp = opseq_completion_timestamp[next_opseq];

        /*Ensure that uops can not be submitted before they have been decoded*/
        if (decode_timestamp > last_uop_timestamp)
                last_uop_timestamp = decode_timestamp;

        /*Submit uops to execution units, and determine the latest completion time*/
        for (c = 0; c < (decode_buffer.nr_uops); c++)
        {
                if (decode_buffer.earliest_start[c] == -1)
                        start_timestamp = last_uop_timestamp;
                else
                        start_timestamp = decode_buffer.earliest_start[c];

                last_uop_timestamp = uop_run(decode_buffer.uops[c], start_timestamp);
                if (last_uop_timestamp > uop_timestamp)
                        uop_timestamp = last_uop_timestamp;
        }

        /*Calculate opseq completion time. Since opseqs complete in order, it
          must be after the last completion.*/
        if (uop_timestamp <= last_complete_timestamp)
                last_complete_timestamp = last_complete_timestamp + 1;
        else
                last_complete_timestamp = uop_timestamp;

        /*Advance to next opseq in buffer*/
        opseq_completion_timestamp[next_opseq] = last_complete_timestamp;
        next_opseq++;
        if (next_opseq == NR_OPSEQS)
                next_opseq = 0;

        decode_timestamp++;
        decode_buffer.nr_uops = 0;
}

/*The instruction is only of interest here if it's longer than 7 bytes, as that's the
  limit on P6 simple decoding*/
static int codegen_timing_instr_length(uint64_t deps, uint32_t fetchdat, int op_32)
{
        int len = prefixes + 1; /*Opcode*/
        if (deps & MODRM)
        {
                len++; /*ModR/M*/
                if (deps & HAS_IMM8)
                        len++;
                if (deps & HAS_IMM1632)
                        len += (op_32 & 0x100) ? 4 : 2;

                if (op_32 & 0x200)
                {
                        if ((fetchdat & 7) == 4 && (fetchdat & 0xc0) != 0xc0)
                        {
                                /* Has SIB*/
                                len++;
                                if ((fetchdat & 0xc0) == 0x40)
                                        len++;
                                else if ((fetchdat & 0xc0) == 0x80)
                                        len += 4;
                                else if ((fetchdat & 0x700) == 0x500)
                                        len += 4;
                        }
                        else
                        {
                                if ((fetchdat & 0xc0) == 0x40)
                                        len++;
                                else if ((fetchdat & 0xc0) == 0x80)
                                        len += 4;
                                else if ((fetchdat & 0xc7) == 0x05)
                                        len += 4;
                        }
                }
                else
                {
                        if ((fetchdat & 0xc0) == 0x40)
                                len++;
                        else if ((fetchdat & 0xc0) == 0x80)
                                len += 2;
                        else if ((fetchdat & 0xc7) == 0x06)
                                len += 2;
                }
        }

        return len;
}

static void decode_instruction(const macro_op_t *ins, uint64_t deps, uint32_t fetchdat, int op_32, int bit8)
{
        uint32_t regmask_required;
        uint32_t regmask_modified;
        int c;
	int d = 0; /*Complex decoder uOPs*/
        int earliest_start = 0;
        decode_type_t decode_type = ins->decode_type;
        int instr_length = codegen_timing_instr_length(deps, fetchdat, op_32);

        /*Generate input register mask, and determine the earliest time this
          instruction can start. This is not accurate, as this is calculated per
          x86 instruction when it should be handled per uop*/
        regmask_required = get_dstdep_mask(deps, fetchdat, bit8);
        regmask_required |= get_addr_regmask(deps, fetchdat, op_32);
        for (c = 0; c < 8; c++)
        {
                if (regmask_required & (1 << c))
                {
                        if (reg_available_timestamp[c] > decode_timestamp)
                                earliest_start = reg_available_timestamp[c];
                }
        }
        if ((deps & FPU_RW_ST0) && fpu_st_timestamp[0] > decode_timestamp)
                earliest_start = fpu_st_timestamp[0];
        if ((deps & FPU_RW_ST1) && fpu_st_timestamp[1] > decode_timestamp)
                earliest_start = fpu_st_timestamp[1];
        if ((deps & FPU_RW_STREG))
        {
                int reg = fetchdat & 7;

                if (fpu_st_timestamp[reg] > decode_timestamp)
                        earliest_start = fpu_st_timestamp[reg];
        }

        /*Simple decoders are limited to 7 bytes & 1 uOP*/
        if ((decode_type == DECODE_SIMPLE && instr_length > 7) || (decode_type == DECODE_SIMPLE && ins->nr_uops > 1))
                decode_type = DECODE_COMPLEX;

        switch (decode_type)
        {
                case DECODE_SIMPLE:
                if (decode_buffer.nr_uops - d == 2)
                {
                        decode_buffer.uops[decode_buffer.nr_uops] = &ins->uop[0];
                        decode_buffer.earliest_start[decode_buffer.nr_uops] = earliest_start;
                        decode_buffer.nr_uops = 3;
                        decode_flush_p6();
                }
                else if (decode_buffer.nr_uops - d == 1)
                {
                        decode_buffer.uops[decode_buffer.nr_uops] = &ins->uop[0];
                        decode_buffer.earliest_start[decode_buffer.nr_uops] = earliest_start;
                        decode_buffer.nr_uops = 2+d;
			if (d)
                        decode_flush_p6();
                }
                else if (decode_buffer.nr_uops)
                {
                        decode_buffer.uops[decode_buffer.nr_uops] = &ins->uop[0];
                        decode_buffer.earliest_start[decode_buffer.nr_uops] = earliest_start;
                        decode_buffer.nr_uops = 1+d;
                }
                else
                {
                        decode_buffer.nr_uops = 1;
                        decode_buffer.uops[0] = &ins->uop[0];
                        decode_buffer.earliest_start[0] = earliest_start;
                }
                break;

                case DECODE_COMPLEX:
                if (decode_buffer.nr_uops)
                        decode_flush_p6(); /*The 4-1-1 arrangement implies that a complex ins. can't be decoded after a simple one*/

                d = 0;

                for (c = 0; c < ins->nr_uops; c++)
                {
                        decode_buffer.uops[d] = &ins->uop[c];
                        if (c == 0)
                                decode_buffer.earliest_start[d] = earliest_start;
                        else
                                decode_buffer.earliest_start[d] = -1;
			d++;

                        if ((d == 3) && (ins->nr_uops > 4)) /*Ins. with >4 uOPs require the use of special units only present on 3 translate PLAs*/
                        {
                                d = 0;
                                decode_buffer.nr_uops = 3;
                                decode_flush_p6(); /*The other two decoders are halted to preserve in-order issue*/
                        }
                }
		if (d)
		{
			decode_buffer.nr_uops = d;
		}
                break;
        }

        /*Update write timestamps for any output registers*/
        regmask_modified = get_dstdep_mask(deps, fetchdat, bit8);
        for (c = 0; c < 8; c++)
        {
                if (regmask_modified & (1 << c))
                        reg_available_timestamp[c] = last_complete_timestamp;
        }
        if (deps & FPU_POP)
        {
                for (c = 0; c < 7; c++)
                        fpu_st_timestamp[c] = fpu_st_timestamp[c+1];
                fpu_st_timestamp[7] = 0;
        }
        if (deps & FPU_POP2)
        {
                for (c = 0; c < 6; c++)
                        fpu_st_timestamp[c] = fpu_st_timestamp[c+2];
                fpu_st_timestamp[6] = fpu_st_timestamp[7] = 0;
        }
        if (deps & FPU_PUSH)
        {
                for (c = 0; c < 7; c++)
                        fpu_st_timestamp[c+1] = fpu_st_timestamp[c];
                fpu_st_timestamp[0] = 0;
        }
        if (deps & FPU_WRITE_ST0)
                fpu_st_timestamp[0] = last_complete_timestamp;
        if (deps & FPU_WRITE_ST1)
                fpu_st_timestamp[1] = last_complete_timestamp;
        if (deps & FPU_WRITE_STREG)
        {
                int reg = fetchdat & 7;
                if (deps & FPU_POP)
                        reg--;
                if (reg >= 0 &&
                        !(reg == 0 && (deps & FPU_WRITE_ST0)) &&
                        !(reg == 1 && (deps & FPU_WRITE_ST1)))
                        fpu_st_timestamp[reg] = last_complete_timestamp;
        }
}

void codegen_timing_p6_block_start()
{
        int c;

        for (c = 0; c < nr_units; c++)
                units[c].first_available_cycle = 0;

        decode_timestamp = 0;
        last_complete_timestamp = 0;

        for (c = 0; c < NR_OPSEQS; c++)
                opseq_completion_timestamp[c] = 0;
        next_opseq = 0;

        for (c = 0; c < NR_REGS; c++)
                reg_available_timestamp[c] = 0;
        for (c = 0; c < 8; c++)
                fpu_st_timestamp[c] = 0;
}

void codegen_timing_p6_start()
{
        if (cpu_s->cpu_type == CPU_PENTIUMPRO)
        {
                units = ppro_units;
                nr_units = NR_PPRO_UNITS;
        }
        else
        {
                units = p2_units;
                nr_units = NR_P2_UNITS;
        }
        last_prefix = 0;
        prefixes = 0;
}

void codegen_timing_p6_prefix(uint8_t prefix, uint32_t fetchdat)
{
        if (prefix != 0x0f)
                decode_timestamp++;

        last_prefix = prefix;
        prefixes++;
}

void codegen_timing_p6_opcode(uint8_t opcode, uint32_t fetchdat, int op_32, uint32_t op_pc)
{
        const macro_op_t **ins_table;
        uint64_t *deps;
        int mod3 = ((fetchdat & 0xc0) == 0xc0);
        int old_last_complete_timestamp = last_complete_timestamp;
        int bit8 = !(opcode & 1);

        switch (last_prefix)
        {
                case 0x0f:
		ins_table = mod3 ? opcode_timings_0f_mod3 : opcode_timings_0f;
		deps = mod3 ? opcode_deps_0f_mod3 : opcode_deps_0f;
                break;

                case 0xd8:
                ins_table = mod3 ? opcode_timings_d8_mod3 : opcode_timings_d8;
                deps = mod3 ? opcode_deps_d8_mod3 : opcode_deps_d8;
                opcode = (opcode >> 3) & 7;
                break;
                case 0xd9:
                ins_table = mod3 ? opcode_timings_d9_mod3 : opcode_timings_d9;
                deps = mod3 ? opcode_deps_d9_mod3 : opcode_deps_d9;
                opcode = mod3 ? opcode & 0x3f : (opcode >> 3) & 7;
                break;
                case 0xda:
                ins_table = mod3 ? opcode_timings_da_mod3 : opcode_timings_da;
                deps = mod3 ? opcode_deps_da_mod3 : opcode_deps_da;
                opcode = (opcode >> 3) & 7;
                break;
                case 0xdb:
                ins_table = mod3 ? opcode_timings_db_mod3 : opcode_timings_db;
                deps = mod3 ? opcode_deps_db_mod3 : opcode_deps_db;
                opcode = mod3 ? opcode & 0x3f : (opcode >> 3) & 7;
                break;
                case 0xdc:
                ins_table = mod3 ? opcode_timings_dc_mod3 : opcode_timings_dc;
                deps = mod3 ? opcode_deps_dc_mod3 : opcode_deps_dc;
                opcode = (opcode >> 3) & 7;
                break;
                case 0xdd:
                ins_table = mod3 ? opcode_timings_dd_mod3 : opcode_timings_dd;
                deps = mod3 ? opcode_deps_dd_mod3 : opcode_deps_dd;
                opcode = (opcode >> 3) & 7;
                break;
                case 0xde:
                ins_table = mod3 ? opcode_timings_de_mod3 : opcode_timings_de;
                deps = mod3 ? opcode_deps_de_mod3 : opcode_deps_de;
                opcode = (opcode >> 3) & 7;
                break;
                case 0xdf:
                ins_table = mod3 ? opcode_timings_df_mod3 : opcode_timings_df;
                deps = mod3 ? opcode_deps_df_mod3 : opcode_deps_df;
                opcode = (opcode >> 3) & 7;
                break;

                default:
                switch (opcode)
                {
                        case 0x80: case 0x82:
                        ins_table = mod3 ? opcode_timings_80_mod3 : opcode_timings_80;
                        deps = mod3 ? opcode_deps_8x_mod3 : opcode_deps_8x;
                        opcode = (fetchdat >> 3) & 7;
                        break;
                        case 0x81: case 0x83:
                        ins_table = mod3 ? opcode_timings_8x_mod3 : opcode_timings_8x;
                        deps = mod3 ? opcode_deps_8x_mod3 : opcode_deps_8x;
                        opcode = (fetchdat >> 3) & 7;
                        break;

                        case 0xc0: case 0xd0: case 0xd2:
                        ins_table = mod3 ? opcode_timings_shift_b_mod3 : opcode_timings_shift_b;
                        deps = mod3 ? opcode_deps_shift_mod3 : opcode_deps_shift;
                        opcode = (fetchdat >> 3) & 7;
                        break;

                        case 0xc1: case 0xd1: case 0xd3:
                        ins_table = mod3 ? opcode_timings_shift_mod3 : opcode_timings_shift;
                        deps = mod3 ? opcode_deps_shift_mod3 : opcode_deps_shift;
                        opcode = (fetchdat >> 3) & 7;
                        break;

                        case 0xf6:
                        ins_table = mod3 ? opcode_timings_f6_mod3 : opcode_timings_f6;
                        deps = mod3 ? opcode_deps_f6_mod3 : opcode_deps_f6;
                        opcode = (fetchdat >> 3) & 7;
                        break;
                        case 0xf7:
                        ins_table = mod3 ? opcode_timings_f7_mod3 : opcode_timings_f7;
                        deps = mod3 ? opcode_deps_f7_mod3 : opcode_deps_f7;
                        opcode = (fetchdat >> 3) & 7;
                        break;
                        case 0xff:
                        ins_table = mod3 ? opcode_timings_ff_mod3 : opcode_timings_ff;
                        deps = mod3 ? opcode_deps_ff_mod3 : opcode_deps_ff;
                        opcode = (fetchdat >> 3) & 7;
                        break;

                        default:
                        ins_table = mod3 ? opcode_timings_mod3 : opcode_timings;
                        deps = mod3 ? opcode_deps_mod3 : opcode_deps;
                        break;
                }
        }

        if (ins_table[opcode])
                decode_instruction(ins_table[opcode], deps[opcode], fetchdat, op_32, bit8);
        else
                decode_instruction(&complex_alu1_op, 0, fetchdat, op_32, bit8);
        codegen_block_cycles += (last_complete_timestamp - old_last_complete_timestamp);
}

void codegen_timing_p6_block_end()
{
        if (decode_buffer.nr_uops)
        {
                int old_last_complete_timestamp = last_complete_timestamp;
                decode_flush_p6();
                codegen_block_cycles += (last_complete_timestamp - old_last_complete_timestamp);
        }
}

int codegen_timing_p6_jump_cycles()
{
        if (decode_buffer.nr_uops)
                return 1;
        return 0;
}

codegen_timing_t codegen_timing_p6 =
{
        codegen_timing_p6_start,
        codegen_timing_p6_prefix,
        codegen_timing_p6_opcode,
        codegen_timing_p6_block_start,
        codegen_timing_p6_block_end,
        codegen_timing_p6_jump_cycles
};
