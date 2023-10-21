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
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86_ops.h"
#include "x86seg_common.h"
#include "x87.h"
#include "386_common.h"
#include "codegen.h"
#include "codegen_ops.h"
#include "codegen_timing_common.h"

typedef enum uop_type_t
{
        UOP_ALU0 = 0,  /*Executes in port 0 ALU*/
        UOP_ALU1,      /*Executes in port 1 ALU*/
        UOP_ALU01,     /*Executes in either port 0 or 1 ALU*/
        UOP_BRANCH,    /*Executes in port 1 branch*/
        UOP_LOAD,      /*Executes in port 2 Load unit*/
        UOP_STOREADDR, /*Executes in port 3 Store Address unit*/
        UOP_STOREDATA, /*Executes in port 4 Store Data unit*/
        UOP_FLOAT,     /*Executes in port 0 Floating Point unit*/
        UOP_ALU0_SEG   /*Executes in port 0 ALU, loads segment, causing pipeline flush on PPro*/
} uop_type_t;

#define MAX_UOPS 300

typedef struct p6_uop_t
{
        uop_type_t type;
        int throughput;
        int latency;
} p6_uop_t;

typedef struct p6_instruction_t
{
        int nr_uops;
        p6_uop_t uop[MAX_UOPS];
} p6_instruction_t;

static const p6_instruction_t alu_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_ALU01, .throughput = 1, .latency = 1}
};
static const p6_instruction_t alu0_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_ALU0, .throughput = 1, .latency = 1}
};
static const p6_instruction_t alu1_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_ALU1, .throughput = 1, .latency = 1}
};
static const p6_instruction_t alu01_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_ALU0, .throughput = 1, .latency = 1}
};
static const p6_instruction_t alu4_op =
{
        .nr_uops = 4,
        .uop[0] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_ALU01, .throughput = 1, .latency = 1}
};
static const p6_instruction_t alu6_op =
{
        .nr_uops = 6,
        .uop[0] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_ALU01, .throughput = 1, .latency = 1}
};
static const p6_instruction_t aluc_op =
{
        .nr_uops = 2,
        .uop[0] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[0] = {.type = UOP_ALU01, .throughput = 1, .latency = 1}
};
static const p6_instruction_t load_alu_op =
{
        .nr_uops = 2,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU01, .throughput = 1, .latency = 1}
};
static const p6_instruction_t load_aluc_op =
{
        .nr_uops = 3,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU01, .throughput = 1, .latency = 1}
};
static const p6_instruction_t alu_store_op =
{
        .nr_uops = 4,
        .uop[0] = {.type = UOP_LOAD,      .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1}
};
static const p6_instruction_t alu0_store_op =
{
        .nr_uops = 4,
        .uop[0] = {.type = UOP_LOAD,      .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU0,      .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1}
};
static const p6_instruction_t aluc_store_op =
{
        .nr_uops = 6,
        .uop[0] = {.type = UOP_LOAD,      .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1}
};

static const p6_instruction_t branch_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_BRANCH, .throughput = 1, .latency = 1}
};

static const p6_instruction_t load_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_LOAD, .throughput = 1, .latency = 2}
};

static const p6_instruction_t store_op =
{
        .nr_uops = 2,
        .uop[0] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1}
};


static const p6_instruction_t bswap_op =
{
        .nr_uops = 2,
        .uop[0] = {.type = UOP_ALU0,  .throughput = 1, .latency = 1},
        .uop[0] = {.type = UOP_ALU01, .throughput = 1, .latency = 1}
};
static const p6_instruction_t leave_op =
{
        .nr_uops = 3,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU01, .throughput = 1, .latency = 1}
};
static const p6_instruction_t lods_op =
{
        .nr_uops = 2,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_LOAD,  .throughput = 1, .latency = 1}
};
static const p6_instruction_t loop_op =
{
        .nr_uops = 11,
        .uop[0] = {.type = UOP_ALU0,  .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALU0,  .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU1,  .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[6] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[7] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[8] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[9] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[10] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
};
static const p6_instruction_t movs_op =
{
        .nr_uops = 6,
        .uop[0] = {.type = UOP_LOAD,      .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_LOAD,      .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_LOAD,      .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
};
static const p6_instruction_t pop_reg_op =
{
        .nr_uops = 2,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU01, .throughput = 1, .latency = 1}
};
static const p6_instruction_t pop_mem_op =
{
        .nr_uops = 8,
        .uop[0] = {.type = UOP_LOAD,      .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[6] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[7] = {.type = UOP_ALU01,     .throughput = 1, .latency = 1}
};
static const p6_instruction_t push_imm_op =
{
        .nr_uops = 3,
        .uop[0] = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1}
};
static const p6_instruction_t push_mem_op =
{
        .nr_uops = 4,
        .uop[0] = {.type = UOP_LOAD,      .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
};
static const p6_instruction_t push_seg_op =
{
        .nr_uops = 4,
        .uop[0] = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1}
};
static const p6_instruction_t rcx_op =
{
        .nr_uops = 8,
        .uop[0] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALU0,  .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_ALU0,  .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_ALU0,  .throughput = 1, .latency = 1},
        .uop[6] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[7] = {.type = UOP_ALU0,  .throughput = 1, .latency = 1},
};
static const p6_instruction_t rcx_store_op =
{
        .nr_uops = 11,
        .uop[0]  = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1]  = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[2]  = {.type = UOP_ALU0,  .throughput = 1, .latency = 1},
        .uop[3]  = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[4]  = {.type = UOP_ALU0,  .throughput = 1, .latency = 1},
        .uop[5]  = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[6]  = {.type = UOP_ALU0,  .throughput = 1, .latency = 1},
        .uop[7]  = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[8]  = {.type = UOP_ALU0,  .throughput = 1, .latency = 1},
        .uop[9]  = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[10] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1}
};
static const p6_instruction_t stos_op =
{
        .nr_uops = 3,
        .uop[0] = {.type = UOP_LOAD,      .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1}
};
static const p6_instruction_t xchg_op =
{
        .nr_uops = 3,
        .uop[0] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU01, .throughput = 1, .latency = 1}
};

static const p6_instruction_t mmx_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_ALU01, .throughput = 1, .latency = 1}
};
static const p6_instruction_t mmx_mul_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_ALU0, .throughput = 1, .latency = 3}
};
static const p6_instruction_t mmx_shift_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_ALU1, .throughput = 1, .latency = 1}
};
static const p6_instruction_t load_mmx_op =
{
        .nr_uops = 2,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU01, .throughput = 1, .latency = 1}
};
static const p6_instruction_t load_mmx_mul_op =
{
        .nr_uops = 2,
        .uop[0] = {.type = UOP_LOAD,    .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU0,    .throughput = 1, .latency = 1}
};
static const p6_instruction_t load_mmx_shift_op =
{
        .nr_uops = 2,
        .uop[0] = {.type = UOP_LOAD,    .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU1,    .throughput = 1, .latency = 1}
};

static const p6_instruction_t faddsub_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_FLOAT, .throughput = 1, .latency = 3}
};
static const p6_instruction_t load_faddsub_op =
{
        .nr_uops = 2,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_FLOAT, .throughput = 1, .latency = 3}
};
static const p6_instruction_t fbstp_op =
{
        .nr_uops = 5,
        .uop[0] = {.type = UOP_FLOAT,     .throughput = 165, .latency = 165},
        .uop[1] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1}
};
static const p6_instruction_t fcom_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_FLOAT, .throughput = 1, .latency = 1}
};
static const p6_instruction_t load_fcom_op =
{
        .nr_uops = 2,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_FLOAT, .throughput = 1, .latency = 1}
};
static const p6_instruction_t fcompp_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_FLOAT, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALU01, .throughput = 1, .latency = 1}
};
static const p6_instruction_t fild_op =
{
        .nr_uops = 4,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_FLOAT, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_FLOAT, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_FLOAT, .throughput = 1, .latency = 1}
};
static const p6_instruction_t load_fi_op =
{
        .nr_uops = 7,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_FLOAT, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_FLOAT, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_FLOAT, .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_FLOAT, .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_FLOAT, .throughput = 1, .latency = 1},
        .uop[6] = {.type = UOP_FLOAT, .throughput = 1, .latency = 1}
};
static const p6_instruction_t fmul_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_FLOAT, .throughput = 1, .latency = 5}
};
static const p6_instruction_t load_fmul_op =
{
        .nr_uops = 2,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_FLOAT, .throughput = 1, .latency = 5}
};

static const p6_instruction_t fxch_op =
{
        .nr_uops = 0
};

static const p6_instruction_t fist_op =
{
        .nr_uops = 4,
        .uop[0] = {.type = UOP_FLOAT,     .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_FLOAT,     .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1}
};

static const p6_instruction_t fdiv_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_FLOAT, .throughput = 37, .latency = 38}
};
static const p6_instruction_t fdiv_mem_op =
{
        .nr_uops = 2,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_FLOAT, .throughput = 37, .latency = 38}
};
static const p6_instruction_t fsin_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_FLOAT, .throughput = 62, .latency = 62}
};
static const p6_instruction_t fsqrt_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_FLOAT, .throughput = 69, .latency = 69}
};

static const p6_instruction_t float_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_FLOAT, .throughput = 1, .latency = 1}
};

static const p6_instruction_t fldcw_op =
{
        .nr_uops = 3,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_FLOAT, .throughput = 7, .latency = 7},
};
static const p6_instruction_t flde_op =
{
        .nr_uops = 4,
        .uop[0] = {.type = UOP_LOAD, .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_LOAD, .throughput = 1, .latency = 2},
        .uop[2] = {.type = UOP_FLOAT, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_FLOAT, .throughput = 1, .latency = 1}
};
static const p6_instruction_t frstor_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_FLOAT,     .throughput = 72, .latency = 72}
};
static const p6_instruction_t fsave_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_FLOAT,     .throughput = 141, .latency = 141}
};
static const p6_instruction_t fste_op =
{
        .nr_uops = 6,
        .uop[0] = {.type = UOP_FLOAT,     .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_FLOAT,     .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1}
};
static const p6_instruction_t fstsw_ax_op =
{
        .nr_uops = 3,
        .uop[0] = {.type = UOP_FLOAT, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALU0,  .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU0,  .throughput = 1, .latency = 1}
};
static const p6_instruction_t fstsw_mem_op =
{
        .nr_uops = 3,
        .uop[0] = {.type = UOP_FLOAT,     .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1}
};

static const p6_instruction_t aad_op =
{
        .nr_uops = 3,
        .uop[0] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALU0,  .throughput = 1, .latency = 4},
        .uop[2] = {.type = UOP_ALU01, .throughput = 1, .latency = 1}
};
static const p6_instruction_t aam_op =
{
        .nr_uops = 3,
        .uop[0] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALU0,  .throughput = 1, .latency = 4},
        .uop[2] = {.type = UOP_ALU1,  .throughput = 1, .latency = 9},
        .uop[3] = {.type = UOP_ALU01, .throughput = 1, .latency = 1}
};
static const p6_instruction_t arpl_op =
{
        .nr_uops = 2,
        .uop[0] = {.type = UOP_ALU01, .throughput = 3, .latency = 3},
        .uop[1] = {.type = UOP_ALU01, .throughput = 3, .latency = 3}
};
static const p6_instruction_t bound_op =
{
        .nr_uops = 4,
        .uop[0] = {.type = UOP_LOAD, .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_LOAD, .throughput = 1, .latency = 2},
        .uop[2] = {.type = UOP_ALU0, .throughput = 7, .latency = 7},
        .uop[3] = {.type = UOP_ALU01, .throughput = 6, .latency = 6}
};
static const p6_instruction_t bsx_op =
{
        .nr_uops = 2,
        .uop[0] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALU1, .throughput = 1, .latency = 1}
};
static const p6_instruction_t load_bsx_op =
{
        .nr_uops = 3,
        .uop[0] = {.type = UOP_LOAD, .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU1, .throughput = 1, .latency = 1}
};
static const p6_instruction_t call_far_op =
{
        .nr_uops = 6,
        .uop[0] = {.type = UOP_LOAD,      .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_ALU0_SEG,  .throughput = 28, .latency = 28}
};
static const p6_instruction_t load_call_far_op =
{
        .nr_uops = 7,
        .uop[0] = {.type = UOP_LOAD,      .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_LOAD,      .throughput = 1, .latency = 2},
        .uop[2] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[6] = {.type = UOP_ALU0_SEG,  .throughput = 28, .latency = 28}
};
static const p6_instruction_t cli_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_ALU01, .throughput = 9, .latency = 9}
};
static const p6_instruction_t sti_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_ALU01, .throughput = 17, .latency = 17}
};
static const p6_instruction_t cmps_op =
{
        .nr_uops = 6,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[2] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_ALU01, .throughput = 1, .latency = 1}
};
static const p6_instruction_t cmpxchg_op =
{
        .nr_uops = 4,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1}
};
static const p6_instruction_t cpuid_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_ALU0, .throughput = 48, .latency = 48}
};
/*static const p6_instruction_t div8_op =
{
        .nr_uops = 3,
        .uop[0] = {.type = UOP_ALU0,  .throughput = 12, .latency = 17},
        .uop[1] = {.type = UOP_ALU0,  .throughput = 1,  .latency = 1},
        .uop[2] = {.type = UOP_ALU01, .throughput = 1,  .latency = 1}
};*/
/*static const p6_instruction_t div8_mem_op =
{
        .nr_uops = 4,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[0] = {.type = UOP_ALU0,  .throughput = 12, .latency = 17},
        .uop[1] = {.type = UOP_ALU0,  .throughput = 1,  .latency = 1},
        .uop[2] = {.type = UOP_ALU01, .throughput = 1,  .latency = 1}
};*/
static const p6_instruction_t div16_op =
{
        .nr_uops = 3,
        .uop[0] = {.type = UOP_ALU0,  .throughput = 21, .latency = 21},
        .uop[1] = {.type = UOP_ALU0,  .throughput = 1,  .latency = 1},
        .uop[2] = {.type = UOP_ALU01, .throughput = 1,  .latency = 1}
};
static const p6_instruction_t div16_mem_op =
{
        .nr_uops = 4,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[0] = {.type = UOP_ALU0,  .throughput = 21, .latency = 21},
        .uop[1] = {.type = UOP_ALU0,  .throughput = 1,  .latency = 1},
        .uop[2] = {.type = UOP_ALU01, .throughput = 1,  .latency = 1}
};
static const p6_instruction_t div32_op =
{
        .nr_uops = 3,
        .uop[0] = {.type = UOP_ALU0,  .throughput = 35, .latency = 35},
        .uop[1] = {.type = UOP_ALU0,  .throughput = 1,  .latency = 1},
        .uop[2] = {.type = UOP_ALU01, .throughput = 1,  .latency = 1}
};
static const p6_instruction_t div32_mem_op =
{
        .nr_uops = 4,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[0] = {.type = UOP_ALU0,  .throughput = 35, .latency = 35},
        .uop[1] = {.type = UOP_ALU0,  .throughput = 1,  .latency = 1},
        .uop[2] = {.type = UOP_ALU01, .throughput = 1,  .latency = 1}
};
static const p6_instruction_t emms_op =
{
        .nr_uops = 11,
        .uop[0] = {.type = UOP_ALU0, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALU0, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU0, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_ALU0, .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_ALU0, .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_ALU0, .throughput = 1, .latency = 1},
        .uop[6] = {.type = UOP_ALU0, .throughput = 1, .latency = 1},
        .uop[7] = {.type = UOP_ALU0, .throughput = 1, .latency = 1},
        .uop[8] = {.type = UOP_ALU0, .throughput = 1, .latency = 1},
        .uop[9] = {.type = UOP_ALU0, .throughput = 1, .latency = 1},
        .uop[10] = {.type = UOP_ALU0, .throughput = 1, .latency = 1}
};
static const p6_instruction_t enter_op =
{
        .nr_uops = 14,
        .uop[0] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[6] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[7] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[8] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[9] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[10] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[11] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[12] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[13] = {.type = UOP_ALU01, .throughput = 1, .latency = 1}
};
static const p6_instruction_t io_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_LOAD, .throughput = 18, .latency = 18}
};
static const p6_instruction_t ins_op =
{
        .nr_uops = 4,
        .uop[0] = {.type = UOP_ALU0,  .throughput = 18, .latency = 18},
        .uop[1] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU01, .throughput =  1, .latency =  1}
};
static const p6_instruction_t int_op =
{
        .nr_uops = 7,
        .uop[0] = {.type = UOP_ALU0,    .throughput = 20, .latency = 20},
        .uop[1] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[6] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
};
static const p6_instruction_t iret_op =
{
        .nr_uops = 4,
        .uop[0] = {.type = UOP_LOAD,   .throughput =  1, .latency =  2},
        .uop[1] = {.type = UOP_LOAD,   .throughput =  1, .latency =  2},
        .uop[2] = {.type = UOP_LOAD,   .throughput =  1, .latency =  2},
        .uop[3] = {.type = UOP_ALU01,  .throughput = 20, .latency = 20},
};
static const p6_instruction_t invd_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_ALU01, .throughput = 1000, .latency = 1000}
};
static const p6_instruction_t jmp_far_op =
{
        .nr_uops = 2,
        .uop[0] = {.type = UOP_LOAD,     .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU0_SEG, .throughput = 21, .latency = 21}
};
static const p6_instruction_t load_jmp_far_op =
{
        .nr_uops = 3,
        .uop[0] = {.type = UOP_LOAD,     .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_LOAD,     .throughput = 1, .latency = 2},
        .uop[2] = {.type = UOP_ALU0_SEG, .throughput = 21, .latency = 21}
};
static const p6_instruction_t lss_op =
{
        .nr_uops = 11,
        .uop[0] = {.type = UOP_LOAD,   .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_LOAD,   .throughput = 1, .latency = 2},
        .uop[2] = {.type = UOP_LOAD,   .throughput = 1, .latency = 2},
        .uop[3] = {.type = UOP_ALU0_SEG,.throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_ALU01,  .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_ALU01,  .throughput = 1, .latency = 1},
        .uop[6] = {.type = UOP_ALU01,  .throughput = 1, .latency = 1},
        .uop[7] = {.type = UOP_ALU01,  .throughput = 1, .latency = 1},
        .uop[8] = {.type = UOP_ALU01,  .throughput = 1, .latency = 1},
        .uop[9] = {.type = UOP_ALU01,  .throughput = 1, .latency = 1},
        .uop[10] = {.type = UOP_ALU01, .throughput = 1, .latency = 1}
};
/*static const p6_instruction_t mov_mem_seg_op =
{
        .nr_uops = 3,
        .uop[0] = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
};*/
static const p6_instruction_t mov_seg_mem_op =
{
        .nr_uops = 9,
        .uop[0] = {.type = UOP_LOAD,   .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU0_SEG,.throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU0,   .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_ALU0,   .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_ALU0,   .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_ALU0,   .throughput = 1, .latency = 1},
        .uop[6] = {.type = UOP_ALU0,   .throughput = 1, .latency = 1},
        .uop[7] = {.type = UOP_ALU0,   .throughput = 1, .latency = 1},
        .uop[8] = {.type = UOP_ALU0,   .throughput = 1, .latency = 1}
};
static const p6_instruction_t mov_seg_reg_op =
{
        .nr_uops = 8,
        .uop[0] = {.type = UOP_ALU0,   .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALU0_SEG,.throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU0,   .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_ALU0,   .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_ALU0,   .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_ALU0,   .throughput = 1, .latency = 1},
        .uop[6] = {.type = UOP_ALU0,   .throughput = 1, .latency = 1},
        .uop[7] = {.type = UOP_ALU0,   .throughput = 1, .latency = 1}
};
static const p6_instruction_t mul_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_ALU0, .throughput = 1, .latency = 4}
};
static const p6_instruction_t mul_mem_op =
{
        .nr_uops = 2,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU0, .throughput = 1, .latency = 4}
};
static const p6_instruction_t outs_op =
{
        .nr_uops = 3,
        .uop[0] = {.type = UOP_LOAD,  .throughput =  1, .latency =  1},
        .uop[1] = {.type = UOP_ALU0,  .throughput = 18, .latency = 18},
        .uop[2] = {.type = UOP_ALU01, .throughput =  1, .latency =  1}
};
static const p6_instruction_t pusha_op =
{
        .nr_uops = 18,
        .uop[0]  = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[1]  = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[2]  = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[3]  = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[4]  = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[5]  = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[6]  = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[7]  = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[8]  = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[9]  = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[10] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[11] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[12] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[13] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[14] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[15] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[16] = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
        .uop[17] = {.type = UOP_ALU01,     .throughput = 1, .latency = 1}
};
static const p6_instruction_t popa_op =
{
        .nr_uops = 10,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_LOAD,  .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_LOAD,  .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_LOAD,  .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_LOAD,  .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_LOAD,  .throughput = 1, .latency = 1},
        .uop[6] = {.type = UOP_LOAD,  .throughput = 1, .latency = 1},
        .uop[7] = {.type = UOP_LOAD,  .throughput = 1, .latency = 1},
        .uop[8] = {.type = UOP_ALU01, .throughput =  1, .latency =  1},
        .uop[9] = {.type = UOP_ALU01, .throughput =  1, .latency =  1}
};
static const p6_instruction_t popf_op =
{
        .nr_uops = 17,
        .uop[0]  = {.type = UOP_LOAD,  .throughput =  1, .latency = 2},
        .uop[1]  = {.type = UOP_ALU0,  .throughput =  1, .latency = 1},
        .uop[2]  = {.type = UOP_ALU01, .throughput =  1, .latency = 1},
        .uop[3]  = {.type = UOP_ALU0,  .throughput =  1, .latency = 1},
        .uop[4]  = {.type = UOP_ALU01, .throughput =  1, .latency = 1},
        .uop[5]  = {.type = UOP_ALU0,  .throughput =  1, .latency = 1},
        .uop[6]  = {.type = UOP_ALU01, .throughput =  1, .latency = 1},
        .uop[7]  = {.type = UOP_ALU0,  .throughput =  1, .latency = 1},
        .uop[8]  = {.type = UOP_ALU01, .throughput =  1, .latency = 1},
        .uop[9]  = {.type = UOP_ALU0,  .throughput =  1, .latency = 1},
        .uop[10] = {.type = UOP_ALU01, .throughput =  1, .latency = 1},
        .uop[11] = {.type = UOP_ALU0,  .throughput =  1, .latency = 1},
        .uop[12] = {.type = UOP_ALU01, .throughput =  1, .latency = 1},
        .uop[13] = {.type = UOP_ALU0,  .throughput =  1, .latency = 1},
        .uop[14] = {.type = UOP_ALU0,  .throughput =  1, .latency = 1},
        .uop[15] = {.type = UOP_ALU0,  .throughput =  1, .latency = 1},
        .uop[16] = {.type = UOP_ALU0,  .throughput =  1, .latency = 1},
};
static const p6_instruction_t pushf_op =
{
        .nr_uops = 16,
        .uop[0]  = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
        .uop[1]  = {.type = UOP_ALU0,      .throughput = 1, .latency = 1},
        .uop[2]  = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
        .uop[3]  = {.type = UOP_ALU0,      .throughput = 1, .latency = 1},
        .uop[4]  = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
        .uop[5]  = {.type = UOP_ALU0,      .throughput = 1, .latency = 1},
        .uop[6]  = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
        .uop[7]  = {.type = UOP_ALU0,      .throughput = 1, .latency = 1},
        .uop[8]  = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
        .uop[9]  = {.type = UOP_ALU0,      .throughput = 1, .latency = 1},
        .uop[10] = {.type = UOP_ALU01,     .throughput = 1, .latency = 1},
        .uop[11] = {.type = UOP_ALU0,      .throughput = 1, .latency = 1},
        .uop[12] = {.type = UOP_ALU0,      .throughput = 1, .latency = 1},
        .uop[13] = {.type = UOP_ALU0,      .throughput = 1, .latency = 1},
        .uop[14] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[15] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1}
};
static const p6_instruction_t ret_op =
{
        .nr_uops = 4,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_ALU1,  .throughput = 1, .latency = 1}
};
static const p6_instruction_t reti_op =
{
        .nr_uops = 5,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_ALU1,  .throughput = 1, .latency = 1}
};
static const p6_instruction_t retf_op =
{
        .nr_uops = 4,
        .uop[0] = {.type = UOP_LOAD,   .throughput = 1,  .latency = 2},
        .uop[1] = {.type = UOP_LOAD,   .throughput = 1,  .latency = 2},
        .uop[2] = {.type = UOP_LOAD,   .throughput = 1,  .latency = 2},
        .uop[3] = {.type = UOP_ALU0,   .throughput = 23, .latency = 23},
};
static const p6_instruction_t scas_op =
{
        .nr_uops = 3,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[2] = {.type = UOP_ALU01, .throughput = 1, .latency = 1}
};
static const p6_instruction_t xchg_mem_op =
{
        .nr_uops = 7,
        .uop[0] = {.type = UOP_LOAD, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALU01,  .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU01,  .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_ALU01,  .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_STOREADDR, .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_STOREDATA, .throughput = 1, .latency = 1},
        .uop[6] = {.type = UOP_ALU01, .throughput = 1, .latency = 1}
};
static const p6_instruction_t xlat_op =
{
        .nr_uops = 2,
        .uop[0] = {.type = UOP_ALU01, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2}
};
static const p6_instruction_t wbinvd_op =
{
        .nr_uops = 1,
        .uop[0] = {.type = UOP_ALU0, .throughput = 10000, .latency = 10000}
};

#define INVALID NULL

static const p6_instruction_t *opcode_timings[256] =
{
/*      ADD                    ADD                    ADD                   ADD*/
/*00*/  &alu_store_op,         &alu_store_op,         &load_alu_op,         &load_alu_op,
/*      ADD                    ADD                    PUSH ES               POP ES*/
        &alu_op,               &alu_op,               &push_seg_op,         &mov_seg_mem_op,
/*      OR                     OR                     OR                    OR*/
        &alu_store_op,         &alu_store_op,         &load_alu_op,         &load_alu_op,
/*      OR                     OR                     PUSH CS               */
        &alu_op,               &alu_op,               &push_seg_op,         INVALID,

/*      ADC                    ADC                    ADC                   ADC*/
/*10*/  &aluc_store_op,        &aluc_store_op,        &load_aluc_op,        &load_aluc_op,
/*      ADC                    ADC                    PUSH SS               POP SS*/
        &aluc_op,              &aluc_op,              &push_seg_op,         &mov_seg_mem_op,
/*      SBB                    SBB                    SBB                   SBB*/
/*10*/  &aluc_store_op,        &aluc_store_op,        &load_aluc_op,        &load_aluc_op,
/*      SBB                    SBB                    PUSH DS               POP DS*/
        &aluc_op,              &aluc_op,              &push_seg_op,         &mov_seg_mem_op,

/*      AND                    AND                    AND                   AND*/
/*20*/  &alu_store_op,         &alu_store_op,         &load_alu_op,         &load_alu_op,
/*      AND                    AND                                          DAA*/
        &alu_op,               &alu_op,               INVALID,              &alu1_op,
/*      SUB                    SUB                    SUB                   SUB*/
        &alu_store_op,         &alu_store_op,         &load_alu_op,         &load_alu_op,
/*      SUB                    SUB                                          DAS*/
        &alu_op,               &alu_op,               INVALID,              &alu1_op,

/*      XOR                    XOR                    XOR                   XOR*/
/*30*/  &alu_store_op,         &alu_store_op,         &load_alu_op,         &load_alu_op,
/*      XOR                    XOR                                          AAA*/
        &alu_op,               &alu_op,               INVALID,              &alu1_op,
/*      CMP                    CMP                    CMP                   CMP*/
        &load_alu_op,          &load_alu_op,          &load_alu_op,         &load_alu_op,
/*      CMP                    CMP                                          AAS*/
        &alu_op,               &alu_op,               INVALID,              &alu1_op,

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
        &load_alu_op,              &load_alu_op,              &xchg_mem_op,              &xchg_mem_op,
/*      MOV                        MOV                        MOV                        MOV*/
        &store_op,                 &store_op,                 &load_op,                  &load_op,
/*      MOV from seg               LEA                        MOV to seg                 POP*/
        &store_op,                 &alu0_op,                  &mov_seg_mem_op,           &pop_mem_op,

/*      NOP                        XCHG                       XCHG                       XCHG*/
/*90*/  &alu_op,                   &xchg_op,                  &xchg_op,                  &xchg_op,
/*      XCHG                       XCHG                       XCHG                       XCHG*/
        &xchg_op,                  &xchg_op,                  &xchg_op,                  &xchg_op,
/*      CBW                        CWD                        CALL far                   WAIT*/
        &alu_op,                   &alu0_op,                  &call_far_op,              &alu_op,
/*      PUSHF                      POPF                       SAHF                       LAHF*/
        &pushf_op,                 &popf_op,                  &alu_op,                   &alu_op,

/*      MOV                        MOV                        MOV                        MOV*/
/*a0*/  &load_op,                  &load_op,                  &store_op,                 &store_op,
/*      MOVSB                      MOVSW                      CMPSB                      CMPSW*/
        &movs_op,                  &movs_op,                  &cmps_op,                  &cmps_op,
/*      TEST                       TEST                       STOSB                      STOSW*/
        &load_alu_op,              &load_alu_op,              &stos_op,                  &stos_op,
/*      LODSB                      LODSW                      SCASB                      SCASW*/
        &lods_op,                  &lods_op,                  &scas_op,                  &scas_op,

/*      MOV*/
/*b0*/  &alu_op,        &alu_op,        &alu_op,        &alu_op,
        &alu_op,        &alu_op,        &alu_op,        &alu_op,
        &alu_op,        &alu_op,        &alu_op,        &alu_op,
        &alu_op,        &alu_op,        &alu_op,        &alu_op,

/*                                                            RET imm                    RET*/
/*c0*/  INVALID,                   INVALID,                   &reti_op,                  &ret_op,
/*      LES                        LDS                        MOV                        MOV*/
        &lss_op,                   &lss_op,                   &store_op,                 &store_op,
/*      ENTER                      LEAVE                      RETF                       RETF*/
        &enter_op,                 &leave_op,                 &retf_op,                  &retf_op,
/*      INT3                       INT                        INTO                       IRET*/
        &int_op,                   &int_op,                   &int_op,                   &iret_op,


/*d0*/  INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      AAM                        AAD                        SETALC                     XLAT*/
        &aam_op,                   &aad_op,                   &alu_op,                   &xlat_op,
        INVALID,                   INVALID,                   INVALID,                   INVALID,
        INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      LOOPNE                     LOOPE                      LOOP                       JCXZ*/
/*e0*/  &loop_op,                  &loop_op,                  &loop_op,                  &loop_op,
/*      IN AL                      IN AX                      OUT_AL                     OUT_AX*/
        &io_op,                    &io_op,                    &io_op,                    &io_op,
/*      CALL                       JMP                        JMP                        JMP*/
        &store_op,                 &branch_op,                &jmp_far_op,               &branch_op,
/*      IN AL                      IN AX                      OUT_AL                     OUT_AX*/
        &io_op,                    &io_op,                    &io_op,                    &io_op,

/*                                                            REPNE                      REPE*/
/*f0*/  INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      HLT                        CMC*/
        &alu4_op,                  &alu_op,                   INVALID,                   INVALID,
/*      CLC                        STC                        CLI                        STI*/
        &alu_op,                   &alu_op,                   &cli_op,                   &sti_op,
/*      CLD                        STD                        INCDEC*/
        &alu4_op,                  &alu4_op,                  &store_op,                 INVALID
};

static const p6_instruction_t *opcode_timings_mod3[256] =
{
/*      ADD                       ADD                       ADD                       ADD*/
/*00*/  &alu_op,                  &alu_op,                  &alu_op,                  &alu_op,
/*      ADD                       ADD                       PUSH ES                   POP ES*/
        &alu_op,                  &alu_op,                  &push_seg_op,             &mov_seg_mem_op,
/*      OR                        OR                        OR                        OR*/
        &alu_op,                  &alu_op,                  &alu_op,                  &alu_op,
/*      OR                        OR                        PUSH CS                   */
        &alu_op,                  &alu_op,                  &push_seg_op,             INVALID,

/*      ADC                       ADC                       ADC                       ADC*/
/*10*/  &aluc_op,                 &aluc_op,                 &aluc_op,                 &aluc_op,
/*      ADC                       ADC                       PUSH SS                   POP SS*/
        &aluc_op,                 &aluc_op,                 &push_seg_op,             &mov_seg_mem_op,
/*      SBB                       SBB                       SBB                       SBB*/
        &aluc_op,                 &aluc_op,                 &aluc_op,                 &aluc_op,
/*      SBB                       SBB                       PUSH DS                   POP DS*/
        &aluc_op,                 &aluc_op,                 &push_seg_op,             &mov_seg_mem_op,

/*      AND                       AND                       AND                       AND*/
/*20*/  &alu_op,                  &alu_op,                  &alu_op,                  &alu_op,
/*      AND                       AND                                                 DAA*/
        &alu_op,                  &alu_op,                  INVALID,                  &alu1_op,
/*      SUB                       SUB                       SUB                       SUB*/
        &alu_op,                  &alu_op,                  &alu_op,                  &alu_op,
/*      SUB                       SUB                                                 DAS*/
        &alu_op,                  &alu_op,                  INVALID,                  &alu1_op,

/*      XOR                       XOR                       XOR                       XOR*/
/*30*/  &alu_op,                  &alu_op,                  &alu_op,                  &alu_op,
/*      XOR                       XOR                                                 AAA*/
        &alu_op,                  &alu_op,                  INVALID,                  &alu1_op,
/*      CMP                       CMP                       CMP                       CMP*/
        &alu_op,                  &alu_op,                  &alu_op,                  &alu_op,
/*      CMP                       CMP                                                 AAS*/
        &alu_op,                  &alu_op,                  INVALID,                  &alu1_op,

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
        &alu_op,                   &alu_op,                   &xchg_op,                  &xchg_op,
/*      MOV                        MOV                        MOV                        MOV*/
        &store_op,                 &store_op,                 &load_op,                  &load_op,
/*      MOV from seg               LEA                        MOV to seg                 POP*/
        &alu_op,                   &alu0_op,                  &mov_seg_reg_op,           &pop_reg_op,

/*      NOP                        XCHG                       XCHG                       XCHG*/
/*90*/  &alu_op,                   &xchg_op,                  &xchg_op,                  &xchg_op,
/*      XCHG                       XCHG                       XCHG                       XCHG*/
        &xchg_op,                  &xchg_op,                  &xchg_op,                  &xchg_op,
/*      CBW                        CWD                        CALL far                   WAIT*/
        &alu_op,                   &alu0_op,                  &call_far_op,              &alu_op,
/*      PUSHF                      POPF                       SAHF                       LAHF*/
        &pushf_op,                 &popf_op,                  &alu_op,                   &alu_op,

/*      MOV                        MOV                        MOV                        MOV*/
/*a0*/  &load_op,                  &load_op,                  &store_op,                 &store_op,
/*      MOVSB                      MOVSW                      CMPSB                      CMPSW*/
        &movs_op,                  &movs_op,                  &cmps_op,                  &cmps_op,
/*      TEST                       TEST                       STOSB                      STOSW*/
        &alu_op,                   &alu_op,                   &stos_op,                  &stos_op,
/*      LODSB                      LODSW                      SCASB                      SCASW*/
        &lods_op,                  &lods_op,                  &scas_op,                  &scas_op,

/*      MOV*/
/*b0*/  &alu_op,        &alu_op,        &alu_op,        &alu_op,
        &alu_op,        &alu_op,        &alu_op,        &alu_op,
        &alu_op,        &alu_op,        &alu_op,        &alu_op,
        &alu_op,        &alu_op,        &alu_op,        &alu_op,

/*                                                            RET imm                    RET*/
/*c0*/  INVALID,                   INVALID,                   &reti_op,                  &ret_op,
/*      LES                        LDS                        MOV                        MOV*/
        &lss_op,                   &lss_op,                   &store_op,                 &store_op,
/*      ENTER                      LEAVE                      RETF                       RETF*/
        &enter_op,                 &leave_op,                 &retf_op,                  &retf_op,
/*      INT3                       INT                        INTO                       IRET*/
        &int_op,                   &int_op,                   &int_op,                   &iret_op,


/*d0*/  INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      AAM                        AAD                        SETALC                     XLAT*/
        &aam_op,                   &aad_op,                   &alu_op,                   &xlat_op,
        INVALID,                   INVALID,                   INVALID,                   INVALID,
        INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      LOOPNE                     LOOPE                      LOOP                       JCXZ*/
/*e0*/  &loop_op,                  &loop_op,                  &loop_op,                  &loop_op,
/*      IN AL                      IN AX                      OUT_AL                     OUT_AX*/
        &io_op,                    &io_op,                    &io_op,                    &io_op,
/*      CALL                       JMP                        JMP                        JMP*/
        &store_op,                 &branch_op,                &jmp_far_op,               &branch_op,
/*      IN AL                      IN AX                      OUT_AL                     OUT_AX*/
        &io_op,                    &io_op,                    &io_op,                    &io_op,

/*                                                            REPNE                      REPE*/
/*f0*/  INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      HLT                        CMC*/
        &alu4_op,                  &alu_op,                   INVALID,                   INVALID,
/*      CLC                        STC                        CLI                        STI*/
        &alu_op,                   &alu_op,                   &cli_op,                   &sti_op,
/*      CLD                        STD                        INCDEC*/
        &alu4_op,                  &alu4_op,                  &alu_op,                   INVALID
};

static const p6_instruction_t *opcode_timings_0f[256] =
{
/*00*/  &alu6_op,               &alu6_op,               &alu6_op,               &alu6_op,
        INVALID,                &alu6_op,               &alu6_op,               INVALID,
        &invd_op,               &wbinvd_op,             INVALID,                INVALID,
        INVALID,                &load_op,               INVALID,                INVALID,

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
        INVALID,                INVALID,                &load_op,               &load_op,

/*70*/  INVALID,                &load_mmx_shift_op,     &load_mmx_shift_op,     &load_mmx_shift_op,
        &load_mmx_op,           &load_mmx_op,           &load_mmx_op,           &emms_op,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                &store_op,              &store_op,

/*80*/  &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,

/*90*/  &alu_op,                &alu_op,                &alu_op,                &alu_op,
        &alu_op,                &alu_op,                &alu_op,                &alu_op,
        &alu_op,                &alu_op,                &alu_op,                &alu_op,
        &alu_op,                &alu_op,                &alu_op,                &alu_op,

/*a0*/  &push_seg_op,           &mov_seg_mem_op,        &cpuid_op,              &load_alu_op,
        &alu_store_op,          &alu_store_op,          INVALID,                INVALID,
        &push_seg_op,           &mov_seg_mem_op,        INVALID,                &load_alu_op,
        &alu_store_op,          &alu_store_op,          INVALID,                &mul_op,

/*b0*/  &cmpxchg_op,            &cmpxchg_op,            &lss_op,                &load_alu_op,
        &lss_op,                &lss_op,                &load_alu_op,           &load_alu_op,
        INVALID,                INVALID,                &load_alu_op,           &load_alu_op,
        &load_bsx_op,           &load_bsx_op,           &load_alu_op,           &load_alu_op,

/*c0*/  &alu_store_op,          &alu_store_op,          INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                &cmpxchg_op,
        &bswap_op,              &bswap_op,              &bswap_op,              &bswap_op,
        &bswap_op,              &bswap_op,              &bswap_op,              &bswap_op,

/*d0*/  INVALID,                &load_mmx_shift_op,     &load_mmx_shift_op,     &load_mmx_shift_op,
        INVALID,                &load_mmx_mul_op,       INVALID,                INVALID,
        &load_mmx_op,           &load_mmx_op,           INVALID,                &load_mmx_op,
        &load_mmx_op,           &load_mmx_op,           INVALID,                &load_mmx_op,

/*e0*/  &load_mmx_op,           &load_mmx_shift_op,     &load_mmx_shift_op,     INVALID,
        INVALID,                &load_mmx_mul_op,       INVALID,                INVALID,
        &load_mmx_op,           &load_mmx_op,           INVALID,                &load_mmx_op,
        &load_mmx_op,           &load_mmx_op,           INVALID,                &load_mmx_op,

/*f0*/  INVALID,                &load_mmx_shift_op,     &load_mmx_shift_op,     &load_mmx_shift_op,
        INVALID,                &load_mmx_mul_op,       INVALID,                INVALID,
        &load_mmx_op,           &load_mmx_op,           &load_mmx_op,           INVALID,
        &load_mmx_op,           &load_mmx_op,           &load_mmx_op,           INVALID,
};
static const p6_instruction_t *opcode_timings_0f_mod3[256] =
{
/*00*/  &alu6_op,               &alu6_op,               &alu6_op,               &alu6_op,
        INVALID,                &alu6_op,               &alu6_op,               INVALID,
        &invd_op,               &wbinvd_op,             INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

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

/*60*/  &mmx_op,                &mmx_op,                &mmx_op,                &mmx_op,
        &mmx_op,                &mmx_op,                &mmx_op,                &mmx_op,
        &mmx_op,                &mmx_op,                &mmx_op,                &mmx_op,
        INVALID,                INVALID,                &mmx_op,                &mmx_op,

/*70*/  INVALID,                &mmx_shift_op,          &mmx_shift_op,          &mmx_shift_op,
        &mmx_op,                &mmx_op,                &mmx_op,                &emms_op,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                &mmx_op,                &mmx_op,

/*80*/  &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,

/*90*/  &alu_op,                &alu_op,                &alu_op,                &alu_op,
        &alu_op,                &alu_op,                &alu_op,                &alu_op,
        &alu_op,                &alu_op,                &alu_op,                &alu_op,
        &alu_op,                &alu_op,                &alu_op,                &alu_op,

/*a0*/  &push_seg_op,           &mov_seg_mem_op,        &cpuid_op,              &alu_op,
        &alu_op,                &alu_op,                INVALID,                INVALID,
        &push_seg_op,           &mov_seg_mem_op,        INVALID,                &alu_op,
        &alu_op,                &alu_op,                INVALID,                &mul_op,

/*b0*/  &cmpxchg_op,            &cmpxchg_op,            &lss_op,                &alu_op,
        &lss_op,                &lss_op,                &alu_op,                &alu_op,
        INVALID,                INVALID,                &alu_op,                &alu_op,
        &bsx_op,                &bsx_op,                &alu_op,                &alu_op,

/*c0*/  &alu_op,                &alu_op,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        &bswap_op,              &bswap_op,              &bswap_op,              &bswap_op,
        &bswap_op,              &bswap_op,              &bswap_op,              &bswap_op,

/*d0*/  INVALID,                &mmx_shift_op,          &mmx_shift_op,          &mmx_shift_op,
        INVALID,                &mmx_mul_op,            INVALID,                INVALID,
        &mmx_op,                &mmx_op,                INVALID,                &mmx_op,
        &mmx_op,                &mmx_op,                INVALID,                &mmx_op,

/*e0*/  &mmx_op,                &mmx_shift_op,          &mmx_shift_op,          INVALID,
        INVALID,                &mmx_mul_op,            INVALID,                INVALID,
        &mmx_op,                &mmx_op,                INVALID,                &mmx_op,
        &mmx_op,                &mmx_op,                INVALID,                &mmx_op,

/*f0*/  INVALID,                &mmx_shift_op,          &mmx_shift_op,          &mmx_shift_op,
        INVALID,                &mmx_mul_op,            INVALID,                INVALID,
        &mmx_op,                &mmx_op,                &mmx_op,                INVALID,
        &mmx_op,                &mmx_op,                &mmx_op,                INVALID,
};

static const p6_instruction_t *opcode_timings_shift[8] =
{
        &alu0_store_op,    &alu0_store_op,    &rcx_store_op,     &rcx_store_op,
        &alu0_store_op,    &alu0_store_op,    &alu0_store_op,    &alu0_store_op
};
static const p6_instruction_t *opcode_timings_shift_mod3[8] =
{
        &alu0_op,          &alu0_op,          &rcx_op,           &rcx_op,
        &alu0_op,          &alu0_op,          &alu0_op,          &alu0_op
};

static const p6_instruction_t *opcode_timings_8x[8] =
{
        &alu_store_op,  &alu_store_op,  &aluc_store_op,         &aluc_store_op,
        &alu_store_op,  &alu_store_op,  &alu_store_op,          &alu_store_op,
};
static const p6_instruction_t *opcode_timings_8x_mod3[8] =
{
        &alu_op,        &alu_op,        &aluc_op,               &aluc_op,
        &alu_op,        &alu_op,        &alu_op,                &alu_op,
};

static const p6_instruction_t *opcode_timings_f6[8] =
{
/*      TST                                             NOT                     NEG*/
        &load_alu_op,           INVALID,                &alu_store_op,  &alu_store_op,
/*      MUL                     IMUL                    DIV                     IDIV*/
        &mul_mem_op,            &mul_mem_op,            &div16_mem_op,          &div16_mem_op,
};
static const p6_instruction_t *opcode_timings_f6_mod3[8] =
{
/*      TST                                             NOT                     NEG*/
        &alu_op,                INVALID,                &alu_op,                &alu_op,
/*      MUL                     IMUL                    DIV                     IDIV*/
        &mul_op,                &mul_op,                &div16_op,              &div16_op,
};
static const p6_instruction_t *opcode_timings_f7[8] =
{
/*      TST                                             NOT                     NEG*/
        &load_alu_op,           INVALID,                &alu_store_op,          &alu_store_op,
/*      MUL                     IMUL                    DIV                     IDIV*/
        &mul_mem_op,            &mul_mem_op,            &div32_mem_op,          &div32_mem_op,
};
static const p6_instruction_t *opcode_timings_f7_mod3[8] =
{
/*      TST                                             NOT                     NEG*/
        &alu_op,                INVALID,                &alu_op,                &alu_op,
/*      MUL                     IMUL                    DIV                     IDIV*/
        &mul_op,                &mul_op,                &div32_op,              &div32_op,
};
static const p6_instruction_t *opcode_timings_ff[8] =
{
/*      INC                     DEC                     CALL                    CALL far*/
        &alu_store_op,          &alu_store_op,          &store_op,              &load_call_far_op,
/*      JMP                     JMP far                 PUSH*/
        &branch_op,             &load_jmp_far_op,       &push_mem_op,           INVALID
};
static const p6_instruction_t *opcode_timings_ff_mod3[8] =
{
/*      INC                     DEC                     CALL                    CALL far*/
        &alu_op,                &alu_op,                &store_op,              &call_far_op,
/*      JMP                     JMP far                 PUSH*/
        &branch_op,             &load_jmp_far_op,       &push_mem_op,           INVALID
};

static const p6_instruction_t *opcode_timings_d8[8] =
{
/*      FADDs                FMULs              FCOMs            FCOMPs*/
        &load_faddsub_op,    &load_fmul_op,     &fcom_op,        &load_fcom_op,
/*      FSUBs                FSUBRs             FDIVs            FDIVRs*/
        &load_faddsub_op,    &load_faddsub_op,  &fdiv_mem_op,    &fdiv_mem_op,
};
static const p6_instruction_t *opcode_timings_d8_mod3[8] =
{
/*      FADD             FMUL             FCOM             FCOMP*/
        &faddsub_op,     &fmul_op,        &fcom_op,        &fcom_op,
/*      FSUB             FSUBR            FDIV             FDIVR*/
        &faddsub_op,     &faddsub_op,     &fdiv_op,        &fdiv_op,
};

static const p6_instruction_t *opcode_timings_d9[8] =
{
/*      FLDs                                    FSTs                 FSTPs*/
        &load_op,            INVALID,           &store_op,           &store_op,
/*      FLDENV               FLDCW              FSTENV               FSTCW*/
        &flde_op,            &fldcw_op,         &fste_op,            &fstsw_mem_op
};
static const p6_instruction_t *opcode_timings_d9_mod3[64] =
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
        &float_op,    &float_op,    &float_op,    &float_op,
        &float_op,    &float_op,    &float_op,    &float_op,
/*      opFCHS        opFABS*/
        &float_op,    &float_op,    INVALID,      INVALID,
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

static const p6_instruction_t *opcode_timings_da[8] =
{
/*      FIADDl            FIMULl            FICOMl            FICOMPl*/
        &load_fi_op,      &load_fi_op,      &load_fcom_op,    &load_fcom_op,
/*      FISUBl            FISUBRl           FIDIVl            FIDIVRl*/
        &load_fi_op,      &load_fi_op,      &load_fi_op,      &load_fi_op,
};
static const p6_instruction_t *opcode_timings_da_mod3[8] =
{
        INVALID,          INVALID,          INVALID,          INVALID,
/*                        FCOMPP*/
        INVALID,          &fcompp_op,       INVALID,          INVALID
};

static const p6_instruction_t *opcode_timings_db[8] =
{
/*      FLDil                               FSTil         FSTPil*/
        &fild_op,         INVALID,          &fist_op,     &fist_op,
/*                        FLDe                            FSTPe*/
        INVALID,          &flde_op,         INVALID,      &fste_op
};
static const p6_instruction_t *opcode_timings_db_mod3[64] =
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

static const p6_instruction_t *opcode_timings_dc[8] =
{
/*      FADDd             FMULd             FCOMd             FCOMPd*/
        &load_faddsub_op, &load_fmul_op,    &load_fcom_op,    &load_fcom_op,
/*      FSUBd             FSUBRd            FDIVd             FDIVRd*/
        &load_faddsub_op, &load_faddsub_op, &fdiv_mem_op,     &fdiv_mem_op,
};
static const p6_instruction_t *opcode_timings_dc_mod3[8] =
{
/*      opFADDr           opFMULr*/
        &faddsub_op,      &fmul_op,         INVALID,          INVALID,
/*      opFSUBRr          opFSUBr           opFDIVRr          opFDIVr*/
        &faddsub_op,      &faddsub_op,      &fdiv_op,         &fdiv_op
};

static const p6_instruction_t *opcode_timings_dd[8] =
{
/*      FLDd                                    FSTd                 FSTPd*/
        &load_op,           INVALID,            &store_op,           &store_op,
/*      FRSTOR                                  FSAVE                FSTSW*/
        &frstor_op,         INVALID,            &fsave_op,           &fstsw_mem_op
};
static const p6_instruction_t *opcode_timings_dd_mod3[8] =
{
/*      FFFREE                            FST                FSTP*/
        &float_op,       INVALID,         &float_op,         &float_op,
/*      FUCOM            FUCOMP*/
        &float_op,       &float_op,       INVALID,           INVALID
};

static const p6_instruction_t *opcode_timings_de[8] =
{
/*      FIADDw            FIMULw            FICOMw            FICOMPw*/
        &load_fi_op,      &load_fi_op,      &load_fcom_op,    &load_fcom_op,
/*      FISUBw            FISUBRw           FIDIVw            FIDIVRw*/
        &load_fi_op,      &load_fi_op,      &load_fi_op,      &load_fi_op,
};
static const p6_instruction_t *opcode_timings_de_mod3[8] =
{
/*      FADDP            FMULP                          FCOMPP*/
        &faddsub_op,     &fmul_op,        INVALID,      &fcompp_op,
/*      FSUBP            FSUBRP           FDIVP         FDIVRP*/
        &faddsub_op,     &faddsub_op,     &fdiv_op,     &fdiv_op,
};

static const p6_instruction_t *opcode_timings_df[8] =
{
/*      FILDiw                              FISTiw               FISTPiw*/
        &fild_op,         INVALID,          &fist_op,            &fist_op,
/*                        FILDiq            FBSTP                FISTPiq*/
        INVALID,          &fild_op,         &fbstp_op,           &fist_op,
};
static const p6_instruction_t *opcode_timings_df_mod3[8] =
{
        INVALID,      INVALID,      INVALID,      INVALID,
/*      FSTSW AX*/
        &fstsw_ax_op, INVALID,      INVALID,      INVALID
};


static uint8_t last_prefix;
static int prefixes;

static int decode_timestamp;
static int last_complete_timestamp;

typedef struct p6_unit_t
{
        const uint32_t uop_mask;
        const int port;
        int first_available_cycle;
} p6_unit_t;

enum
{
        UNIT_INT0 = 0,
        UNIT_INT1,
        UNIT_FPU,
        UNIT_MMX_ADD,
        UNIT_MMX_MUL,
        UNIT_JUMP,
        UNIT_LOAD,
        UNIT_STORE_ADDR,
        UNIT_STORE_DATA
};

/*Each port can only be used once per clock*/
typedef struct p6_port_t
{
        uint32_t unit_mask;
} p6_port_t;

static int port_last_timestamps[5];

static const p6_port_t ports[] =
{
        {UNIT_INT0 | UNIT_FPU | UNIT_MMX_MUL},
        {UNIT_INT1 | UNIT_MMX_ADD | UNIT_JUMP},
        {UNIT_LOAD},
        {UNIT_STORE_ADDR},
        {UNIT_STORE_DATA}
};
const int nr_ports = (sizeof(ports) / sizeof(p6_port_t));

static p6_unit_t units[] =
{
        [UNIT_INT0]       = {.uop_mask = (1 << UOP_ALU0) | (1 << UOP_ALU0_SEG) | (1 << UOP_ALU01), .port = 0},                 /*Integer 0*/
        [UNIT_INT1]       = {.uop_mask = (1 << UOP_ALU1) | (1 << UOP_ALU01), .port = 1},                 /*Integer 1*/
        [UNIT_FPU]        = {.uop_mask = 1 << UOP_FLOAT,                     .port = 0},
        [UNIT_MMX_ADD]    = {.uop_mask = 0,                                  .port = 1},
        [UNIT_MMX_MUL]    = {.uop_mask = 0,                                  .port = 0},
        [UNIT_JUMP]       = {.uop_mask = 1 << UOP_BRANCH,                    .port = 1},
        [UNIT_LOAD]       = {.uop_mask = 1 << UOP_LOAD,                      .port = 2},
        [UNIT_STORE_ADDR] = {.uop_mask = 1 << UOP_STOREADDR,                 .port = 3},
        [UNIT_STORE_DATA] = {.uop_mask = 1 << UOP_STOREDATA,                 .port = 4}
};
const int nr_units = (sizeof(units) / sizeof(p6_unit_t));

static int rat_timestamp = 0;
static int rat_uops = 0;

static int uop_run(const p6_uop_t *uop, int decode_time)
{
        int c;
        p6_unit_t *best_unit = NULL;
        int best_start_cycle = 99999;

        /*Peak of 3 uOPs from decode to RAT per cycle*/
        if (decode_time < rat_timestamp)
                decode_time = rat_timestamp;
        else if (decode_time > rat_timestamp)
        {
                rat_timestamp = decode_time;
                rat_uops = 0;
        }

        rat_uops++;
        if (rat_uops == 3)
        {
                rat_timestamp++;
                rat_uops = 0;
        }


        /*Find execution unit for this uOP*/
        for (c = 0; c < nr_units; c++)
        {
                p6_unit_t *unit = &units[c];
                if (unit->uop_mask & (1 << uop->type))
                {
                        int start_cycle = MAX(unit->first_available_cycle,
                                              port_last_timestamps[units[c].port]);

                        if (start_cycle < best_start_cycle)
                        {
                                best_unit = unit;
                                best_start_cycle = start_cycle;
                        }
                }
        }
        if (!best_unit)
                fatal("uop_run: can not find execution unit  %x\n", uop->type);

        if (cpu_s->cpu_type == CPU_PENTIUMPRO && uop->type == UOP_ALU0_SEG)
        {
                /*Pentium Pro will flush pipeline on a segment load. Find last
                  execution unit to complete, then set earliest start times on
                  _all_ execution units to after this point*/
                int last_start_cycle = 0;

                for (c = 0; c < nr_units; c++)
                {
                        p6_unit_t *unit = &units[c];
                        int start_cycle = unit->first_available_cycle;

                        if (start_cycle > last_start_cycle)
                                last_start_cycle = start_cycle;
                }
                for (c = 0; c < nr_units; c++)
                        units[c].first_available_cycle = last_start_cycle+1;
        }
        if (best_start_cycle < decode_time)
                best_start_cycle = decode_time;
        best_unit->first_available_cycle = best_start_cycle + uop->throughput;
        port_last_timestamps[best_unit->port] = best_start_cycle + 1;

        return best_start_cycle + uop->throughput;
//        return decode_time+1;
}

/*The P6 decoder can decode up to three instructions per clock
  First can be up to 4 uOPs
  Second and third must be 1 uOP each
  Up to 16 bytes per cycle
  1 cycle per prefix beyond 1 prefix
  0x66 causes delays if instruction has immediate
  0x67 causes delays if modr/m present (and mod != 3)
*/
static struct
{
        int nr_ins;
        int nr_uops;
        const p6_uop_t *uops[6];
        /*Earliest time a uop can start. If the timestamp is -1, then the uop is
          part of a dependency chain and the start time is the completion time of
          the previous uop*/
        int earliest_start[6];
} decode_buffer;

#define NR_REGS 8
/*Timestamp of when last operation on an integer register completed*/
static int reg_available_timestamp[NR_REGS];
/*Timestamp of when last operation on an FPU register completed*/
static int fpu_st_timestamp[8];
/*Completion time of the last uop to be processed. Used to calculate timing of
  dependent uop chains*/
static int last_uop_timestamp = 0;

static int ifetch_length = 0;
static int has_66_prefix, has_67_prefix;

static void decode_flush(void)
{
        int c;
        int uop_timestamp = 0;

        /*Ensure that uops can not be submitted before they have been decoded*/
        if (decode_timestamp > last_uop_timestamp)
                last_uop_timestamp = decode_timestamp;

        /*Submit uops to execution units, and determine the latest completion time*/
        for (c = 0; c < decode_buffer.nr_uops; c++)
        {
                int start_timestamp;
                
//                pclog("uOP %i, %p\n", c, decode_buffer.uops[c]);
                
                if (decode_buffer.earliest_start[c] == -1)
                        start_timestamp = last_uop_timestamp;
                else
                        start_timestamp = decode_buffer.earliest_start[c];
                
                last_uop_timestamp = uop_run(decode_buffer.uops[c], start_timestamp);
                if (last_uop_timestamp > uop_timestamp)
                        uop_timestamp = last_uop_timestamp;
        }

        /*Calculate opquad completion time. Since opquads complete in order, it
          must be after the last completion.*/
        if (uop_timestamp <= last_complete_timestamp)
                last_complete_timestamp = last_complete_timestamp + 1;
        else
                last_complete_timestamp = uop_timestamp;

        decode_timestamp++;
        decode_buffer.nr_uops = 0;
        decode_buffer.nr_ins = 0;
        ifetch_length = 0;
}

/*The instruction is only of interest here if it's longer than 8 bytes, as that's the
  limit on P6 short decoding*/
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

static void decode_instruction(const p6_instruction_t *ins, uint64_t deps, uint32_t fetchdat, int op_32, uint32_t op_pc, int bit8)
{
        uint32_t regmask_required;
        uint32_t regmask_modified;
        int c, d;
        int earliest_start;
        int instr_length = codegen_timing_instr_length(deps, fetchdat, op_32);

        if (has_66_prefix && (deps & HAS_IMM1632))
                decode_timestamp += 2;
        if (has_67_prefix && (deps & MODRM))
                decode_timestamp += 4;

        earliest_start = decode_timestamp;

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
        
//        pclog("ins->nr_uops=%i decode_buffer.nr_uops=%i decode_buffer.nr_ins=%i instr_length=%i\n", ins->nr_uops, decode_buffer.nr_uops, decode_buffer.nr_ins, instr_length);

        /*Can only decode 16 bytes per cycle*/
        if ((ifetch_length + instr_length) > 16)
                decode_flush();
        else
                ifetch_length += instr_length;
                
        if (ins->nr_uops > 4)
        {
//                pclog("Long decode\n");
                /*Multi-cycle decode (> 4 uOPs) instruction*/
                if (decode_buffer.nr_ins)
                        decode_flush();

                d = 0;

                for (c = 0; c < ins->nr_uops; c++)
                {
                        decode_buffer.uops[d] = &ins->uop[c];
                        if (c == 0)
                                decode_buffer.earliest_start[d] = earliest_start;
                        else
                                decode_buffer.earliest_start[d] = -1;
                        d++;

                        if (d == 4)
                        {
                                d = 0;
                                decode_buffer.nr_uops = 4;
                                decode_flush();
                        }
                }
                if (d)
                {
                        decode_buffer.nr_uops = d;
                        decode_flush();
                }
        }
        else if (ins->nr_uops > 1 || instr_length > 8)
        {
//                pclog("Mid decode\n");
                /*Long (> 1 uOPs) instruction*/
                if (decode_buffer.nr_ins)
                        decode_flush();

                decode_buffer.nr_uops = ins->nr_uops;
                
                for (c = 0; c < ins->nr_uops; c++)
                {
                        decode_buffer.uops[c] = &ins->uop[c];
                        if (c == 0)
                                decode_buffer.earliest_start[c] = earliest_start;
                        else
                                decode_buffer.earliest_start[c] = -1;
                }
                decode_buffer.nr_ins++;
        }
        else if (ins->nr_uops == 1)
        {
//                pclog("Short decode\n");
                /*Short (1 uOP) instruction*/
                decode_buffer.uops[decode_buffer.nr_uops] = &ins->uop[0];
                decode_buffer.earliest_start[decode_buffer.nr_uops] = earliest_start;
                decode_buffer.nr_ins++;
                decode_buffer.nr_uops++;

                if (decode_buffer.nr_ins == 3)
                        decode_flush();
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

void codegen_timing_p6_block_start(void)
{
        int c;

        for (c = 0; c < nr_units; c++)
                units[c].first_available_cycle = 0;

        decode_timestamp = 0;
        last_complete_timestamp = 0;

        rat_timestamp = 0;
        rat_uops = 0;
        
        for (c = 0; c < 5; c++)
                port_last_timestamps[c] = 0;

        for (c = 0; c < NR_REGS; c++)
                reg_available_timestamp[c] = 0;
        for (c = 0; c < 8; c++)
                fpu_st_timestamp[c] = 0;

        ifetch_length = 0;
}

void codegen_timing_p6_start(void)
{
//        units = p6_units;
//        nr_units = NR_P6_UNITS;
        last_prefix = 0;
        prefixes = 0;
        has_66_prefix = 0;
        has_67_prefix = 0;
}

/*Prefixes :
  - first is free
  - operand size prefix has a penalty of 'a few clocks' if instruction has 16 or
    32 bit immediate
  - address size prefix has penalty if instruction has explicit memory operand*/
void codegen_timing_p6_prefix(uint8_t prefix, uint32_t fetchdat)
{
        if (last_prefix) /*First prefix is free*/
                decode_timestamp++;
        if (prefix == 0x66)
                has_66_prefix = 1;
        if (prefix == 0x67)
                has_67_prefix = 1;

        last_prefix = prefix;
        prefixes++;
}

void codegen_timing_p6_opcode(uint8_t opcode, uint32_t fetchdat, int op_32, uint32_t op_pc)
{
        const p6_instruction_t **ins_table;
        uint64_t *deps;
        int mod3 = ((fetchdat & 0xc0) == 0xc0);
        int old_last_complete_timestamp = last_complete_timestamp;
        int bit8 = !(opcode & 1);

//        pclog("timing_opcode %02x\n", opcode);
        switch (last_prefix)
        {
                case 0x0f:
                ins_table = mod3 ? opcode_timings_0f_mod3 : opcode_timings_0f;
                deps = mod3 ? opcode_deps_0f_mod3 : opcode_deps_0f;
//                pclog("timings 0f\n");
                break;

                case 0xd8:
                ins_table = mod3 ? opcode_timings_d8_mod3 : opcode_timings_d8;
                deps = mod3 ? opcode_deps_d8_mod3 : opcode_deps_d8;
                opcode = (opcode >> 3) & 7;
//                pclog("timings d8\n");
                break;
                case 0xd9:
                ins_table = mod3 ? opcode_timings_d9_mod3 : opcode_timings_d9;
                deps = mod3 ? opcode_deps_d9_mod3 : opcode_deps_d9;
                opcode = mod3 ? opcode & 0x3f : (opcode >> 3) & 7;
//                pclog("timings d9\n");
                break;
                case 0xda:
                ins_table = mod3 ? opcode_timings_da_mod3 : opcode_timings_da;
                deps = mod3 ? opcode_deps_da_mod3 : opcode_deps_da;
                opcode = (opcode >> 3) & 7;
//                pclog("timings da\n");
                break;
                case 0xdb:
                ins_table = mod3 ? opcode_timings_db_mod3 : opcode_timings_db;
                deps = mod3 ? opcode_deps_db_mod3 : opcode_deps_db;
                opcode = mod3 ? opcode & 0x3f : (opcode >> 3) & 7;
//                pclog("timings db\n");
                break;
                case 0xdc:
                ins_table = mod3 ? opcode_timings_dc_mod3 : opcode_timings_dc;
                deps = mod3 ? opcode_deps_dc_mod3 : opcode_deps_dc;
                opcode = (opcode >> 3) & 7;
//                pclog("timings dc\n");
                break;
                case 0xdd:
                ins_table = mod3 ? opcode_timings_dd_mod3 : opcode_timings_dd;
                deps = mod3 ? opcode_deps_dd_mod3 : opcode_deps_dd;
                opcode = (opcode >> 3) & 7;
//                pclog("timings dd\n");
                break;
                case 0xde:
                ins_table = mod3 ? opcode_timings_de_mod3 : opcode_timings_de;
                deps = mod3 ? opcode_deps_de_mod3 : opcode_deps_de;
                opcode = (opcode >> 3) & 7;
//                pclog("timings de\n");
                break;
                case 0xdf:
                ins_table = mod3 ? opcode_timings_df_mod3 : opcode_timings_df;
                deps = mod3 ? opcode_deps_df_mod3 : opcode_deps_df;
                opcode = (opcode >> 3) & 7;
//                pclog("timings df\n");
                break;

                default:
                switch (opcode)
                {
                        case 0x80: case 0x81: case 0x82: case 0x83:
                        ins_table = mod3 ? opcode_timings_8x_mod3 : opcode_timings_8x;
                        deps = mod3 ? opcode_deps_8x_mod3 : opcode_deps_8x;
                        opcode = (fetchdat >> 3) & 7;
//                        pclog("timings 80 %p %p %p\n", (void *)timings, (void *)opcode_timings_mod3, (void *)opcode_timings_8x);
                        break;

                        case 0xc0: case 0xd0: case 0xd2:
                        case 0xc1: case 0xd1: case 0xd3:
                        ins_table = mod3 ? opcode_timings_shift_mod3 : opcode_timings_shift;
                        deps = mod3 ? opcode_deps_shift_mod3 : opcode_deps_shift;
                        opcode = (fetchdat >> 3) & 7;
//                        pclog("timings c1\n");
                        break;

                        case 0xf6:
                        ins_table = mod3 ? opcode_timings_f6_mod3 : opcode_timings_f6;
                        deps = mod3 ? opcode_deps_f6_mod3 : opcode_deps_f6;
                        opcode = (fetchdat >> 3) & 7;
//                        pclog("timings f6\n");
                        break;
                        case 0xf7:
                        ins_table = mod3 ? opcode_timings_f7_mod3 : opcode_timings_f7;
                        deps = mod3 ? opcode_deps_f7_mod3 : opcode_deps_f7;
                        opcode = (fetchdat >> 3) & 7;
//                        pclog("timings f7\n");
                        break;
                        case 0xff:
                        ins_table = mod3 ? opcode_timings_ff_mod3 : opcode_timings_ff;
                        deps = mod3 ? opcode_deps_ff_mod3 : opcode_deps_ff;
                        opcode = (fetchdat >> 3) & 7;
//                        pclog("timings ff\n");
                        break;

                        default:
                        ins_table = mod3 ? opcode_timings_mod3 : opcode_timings;
                        deps = mod3 ? opcode_deps_mod3 : opcode_deps;
//                        pclog("timings normal\n");
                        break;
                }
        }

        if (ins_table[opcode])
                decode_instruction(ins_table[opcode], deps[opcode], fetchdat, op_32, op_pc, bit8);
        else
                decode_instruction(&alu01_op, 0, fetchdat, op_32, op_pc, bit8);
        codegen_block_cycles += (last_complete_timestamp - old_last_complete_timestamp);
}

void codegen_timing_p6_block_end(void)
{
        if (decode_buffer.nr_ins)
        {
                int old_last_complete_timestamp = last_complete_timestamp;
                decode_flush();
                codegen_block_cycles += (last_complete_timestamp - old_last_complete_timestamp);
        }
//        pclog("codegen_block_cycles=%i\n", codegen_block_cycles);
}

int codegen_timing_p6_jump_cycles(void)
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
