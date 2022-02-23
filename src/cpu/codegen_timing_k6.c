/*Most of the vector instructions here are a total guess.
  Some of the timings are based on http://users.atw.hu/instlatx64/AuthenticAMD0000562_K6_InstLatX86.txt*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/mem.h>
#include "cpu.h"
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
        UOP_ALU = 0,   /*Executes in Integer X or Y units*/
        UOP_ALUX,      /*Executes in Integer X unit*/
        UOP_LOAD,      /*Executes in Load unit*/
        UOP_STORE,     /*Executes in Store unit*/
        UOP_FLOAD,     /*Executes in Load unit*/
        UOP_FSTORE,    /*Executes in Store unit*/
        UOP_MLOAD,     /*Executes in Load unit*/
        UOP_MSTORE,    /*Executes in Store unit*/
        UOP_FLOAT,     /*Executes in Floating Point unit*/
        UOP_MEU,       /*Executes in Multimedia unit*/
        UOP_MEU_SHIFT, /*Executes in Multimedia unit or ALU X/Y. Uses MMX shifter*/
        UOP_MEU_MUL,   /*Executes in Multimedia unit or ALU X/Y. Uses MMX/3DNow multiplier*/
        UOP_MEU_3DN,   /*Executes in Multimedia unit or ALU X/Y. Uses 3DNow ALU*/
        UOP_BRANCH,    /*Executes in Branch unit*/
        UOP_LIMM       /*Does not require an execution unit*/
} uop_type_t;

typedef enum decode_type_t
{
        DECODE_SHORT,
        DECODE_LONG,
        DECODE_VECTOR
} decode_type_t;

#define MAX_UOPS 10

typedef struct risc86_uop_t
{
        uop_type_t type;
        int throughput;
        int latency;
} risc86_uop_t;

typedef struct risc86_instruction_t
{
        int nr_uops;
        decode_type_t decode_type;
        risc86_uop_t uop[MAX_UOPS];
} risc86_instruction_t;

static const risc86_instruction_t alu_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_ALU, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t alux_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_ALUX, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t load_alu_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_LOAD, .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU,  .throughput = 1, .latency = 1}
};
static const risc86_instruction_t load_alux_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_LOAD, .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALUX, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t alu_store_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_LONG,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU,   .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STORE, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t alux_store_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_LONG,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALUX,  .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STORE, .throughput = 1, .latency = 1}
};

static const risc86_instruction_t branch_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_BRANCH, .throughput = 1, .latency = 1}
};

static const risc86_instruction_t limm_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_LIMM, .throughput = 1, .latency = 1}
};

static const risc86_instruction_t load_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_LOAD, .throughput = 1, .latency = 2}
};

static const risc86_instruction_t store_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_STORE, .throughput = 1, .latency = 1}
};


static const risc86_instruction_t bswap_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_LONG,
        .uop[0] = {.type = UOP_ALU,   .throughput = 1, .latency = 1}
};
static const risc86_instruction_t leave_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_LONG,
        .uop[0] = {.type = UOP_LOAD, .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU,  .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU,  .throughput = 1, .latency = 1}
};
static const risc86_instruction_t lods_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_LONG,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU,   .throughput = 1, .latency = 1}
};
static const risc86_instruction_t loop_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_ALU,  .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_BRANCH, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t mov_reg_seg_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_LONG,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
};
static const risc86_instruction_t movs_op =
{
        .nr_uops = 4,
        .decode_type = DECODE_LONG,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_STORE, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU,   .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_ALU,   .throughput = 1, .latency = 1}
};
static const risc86_instruction_t pop_reg_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_LOAD, .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU,  .throughput = 1, .latency = 1}
};
static const risc86_instruction_t pop_mem_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_LONG,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_STORE, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU,   .throughput = 1, .latency = 1}
};
static const risc86_instruction_t push_imm_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_LONG,
        .uop[0] = {.type = UOP_STORE,  .throughput = 1, .latency = 2},
};
static const risc86_instruction_t push_mem_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_LONG,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_STORE, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t push_seg_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_LONG,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_STORE, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t stos_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_LONG,
        .uop[1] = {.type = UOP_STORE, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_ALU,   .throughput = 1, .latency = 1}
};
static const risc86_instruction_t test_reg_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_LONG,
        .uop[0] = {.type = UOP_ALU, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t test_reg_b_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_LONG,
        .uop[0] = {.type = UOP_ALUX, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t test_mem_imm_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_LONG,
        .uop[0] = {.type = UOP_LOAD, .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU,  .throughput = 1, .latency = 1}
};
static const risc86_instruction_t test_mem_imm_b_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_LONG,
        .uop[0] = {.type = UOP_LOAD, .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALUX, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t xchg_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_LONG,
        .uop[0] = {.type = UOP_ALU, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALU, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU, .throughput = 1, .latency = 1}
};

static const risc86_instruction_t m3dn_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_MEU_3DN, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t mmx_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_MEU, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t mmx_mul_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_MEU_MUL, .throughput = 1, .latency = 2}
};
static const risc86_instruction_t mmx_shift_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_MEU_SHIFT, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t load_3dn_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_LOAD,     .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_MEU_3DN,  .throughput = 1, .latency = 1}
};
static const risc86_instruction_t load_mmx_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_LOAD, .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_MEU,  .throughput = 1, .latency = 1}
};
static const risc86_instruction_t load_mmx_mul_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_LOAD,    .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_MEU_MUL, .throughput = 1, .latency = 2}
};
static const risc86_instruction_t load_mmx_shift_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_LOAD,      .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_MEU_SHIFT, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t mload_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_MLOAD, .throughput = 1, .latency = 2}
};

static const risc86_instruction_t mstore_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_MSTORE, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t pmul_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_MEU_MUL, .throughput = 1, .latency = 2}
};
static const risc86_instruction_t pmul_mem_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_LOAD,    .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_MEU_MUL, .throughput = 1, .latency = 2}
};

static const risc86_instruction_t float_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_FLOAT, .throughput = 2, .latency = 2}
};
static const risc86_instruction_t load_float_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_FLOAD, .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_FLOAT, .throughput = 2, .latency = 2}
};
static const risc86_instruction_t fstore_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_FSTORE, .throughput = 1, .latency = 1}
};

static const risc86_instruction_t fdiv_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_FLOAT, .throughput = 40, .latency = 40}
};
static const risc86_instruction_t fdiv_mem_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_FLOAD, .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_FLOAT, .throughput = 40, .latency = 40}
};
static const risc86_instruction_t fsin_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_FLOAT, .throughput = 62, .latency = 62}
};
static const risc86_instruction_t fsqrt_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_SHORT,
        .uop[0] = {.type = UOP_FLOAT, .throughput = 41, .latency = 41}
};

static const risc86_instruction_t vector_fldcw_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_FLOAT, .throughput = 8, .latency = 8}
};
static const risc86_instruction_t vector_float_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_FLOAT, .throughput = 2, .latency = 2}
};
static const risc86_instruction_t vector_float_l_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_FLOAT, .throughput = 50, .latency = 50}
};
static const risc86_instruction_t vector_flde_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_FLOAD, .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_FLOAD, .throughput = 1, .latency = 2},
        .uop[2] = {.type = UOP_FLOAT, .throughput = 2, .latency = 2}
};
static const risc86_instruction_t vector_fste_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_FLOAT, .throughput = 2, .latency = 2},
        .uop[1] = {.type = UOP_FSTORE, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_FSTORE, .throughput = 1, .latency = 1}
};

static const risc86_instruction_t vector_alu1_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALU, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_alu2_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALU, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALU, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_alu3_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALU, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALU, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_alu6_op =
{
        .nr_uops = 6,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALU, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALU, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_ALU, .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_ALU, .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_ALU, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_alux1_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALUX, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_alux3_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALUX, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALUX, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALUX, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_alux6_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALUX, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALUX, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALUX, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_ALUX, .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_ALUX, .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_ALUX, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_alu_store_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU,   .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STORE, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_alux_store_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALUX,  .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STORE, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_arpl_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALU,   .throughput = 3, .latency = 3},
        .uop[1] = {.type = UOP_ALU,   .throughput = 3, .latency = 3}
};
static const risc86_instruction_t vector_bound_op =
{
        .nr_uops = 4,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD, .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_LOAD, .throughput = 1, .latency = 2},
        .uop[2] = {.type = UOP_ALU, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_ALU, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_bsx_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALU, .throughput = 10, .latency = 10}
};
static const risc86_instruction_t vector_call_far_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALU,    .throughput = 3, .latency = 3},
        .uop[1] = {.type = UOP_STORE,  .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_BRANCH, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_cli_sti_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALU, .throughput = 7, .latency = 7}
};
static const risc86_instruction_t vector_cmps_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU,   .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU,   .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_cmpsb_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALUX,  .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU,   .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_cmpxchg_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU,   .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STORE, .throughput = 1, .latency = 1},
};
static const risc86_instruction_t vector_cmpxchg_b_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALUX,  .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STORE, .throughput = 1, .latency = 1},
};
static const risc86_instruction_t vector_cpuid_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALU, .throughput = 22, .latency = 22}
};
static const risc86_instruction_t vector_div16_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALUX, .throughput = 10, .latency = 10}
};
static const risc86_instruction_t vector_div16_mem_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALUX, .throughput = 10, .latency = 10}
};
static const risc86_instruction_t vector_div32_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALUX, .throughput = 18, .latency = 18}
};
static const risc86_instruction_t vector_div32_mem_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALUX, .throughput = 18, .latency = 18}
};
static const risc86_instruction_t vector_emms_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALU, .throughput = 25, .latency = 25}
};
static const risc86_instruction_t vector_enter_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_STORE,  .throughput =  1, .latency =  2},
        .uop[1] = {.type = UOP_ALU,    .throughput = 10, .latency = 10}
};
static const risc86_instruction_t vector_femms_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALU, .throughput = 6, .latency = 6}
};
static const risc86_instruction_t vector_in_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD, .throughput = 10, .latency = 11}
};
static const risc86_instruction_t vector_ins_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 10, .latency = 11},
        .uop[1] = {.type = UOP_STORE, .throughput =  1, .latency =  1},
        .uop[2] = {.type = UOP_ALU,   .throughput =  1, .latency =  1}
};
static const risc86_instruction_t vector_int_op =
{
        .nr_uops = 5,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALU,    .throughput = 20, .latency = 20},
        .uop[1] = {.type = UOP_STORE,  .throughput =  1, .latency =  1},
        .uop[2] = {.type = UOP_STORE,  .throughput =  1, .latency =  1},
        .uop[3] = {.type = UOP_STORE,  .throughput =  1, .latency =  1},
        .uop[4] = {.type = UOP_BRANCH, .throughput =  1, .latency =  1}
};
static const risc86_instruction_t vector_iret_op =
{
        .nr_uops = 5,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD,   .throughput =  1, .latency =  2},
        .uop[1] = {.type = UOP_LOAD,   .throughput =  1, .latency =  2},
        .uop[2] = {.type = UOP_LOAD,   .throughput =  1, .latency =  2},
        .uop[3] = {.type = UOP_ALU,    .throughput = 20, .latency = 20},
        .uop[4] = {.type = UOP_BRANCH, .throughput =  1, .latency =  1}
};
static const risc86_instruction_t vector_invd_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALU, .throughput = 1000, .latency = 1000}
};
static const risc86_instruction_t vector_jmp_far_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALU,    .throughput = 3, .latency = 3},
        .uop[1] = {.type = UOP_BRANCH, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_load_alu_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD, .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU,  .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_load_alux_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD, .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALUX, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_loop_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALU,  .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_BRANCH, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_lss_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[2] = {.type = UOP_ALU,   .throughput = 3, .latency = 3}
};
static const risc86_instruction_t vector_mov_mem_seg_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_STORE, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_mov_seg_mem_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU,   .throughput = 3, .latency = 3}
};
static const risc86_instruction_t vector_mov_seg_reg_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALU,   .throughput = 3, .latency = 3}
};
static const risc86_instruction_t vector_mul_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALUX, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALUX, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_mul_mem_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALUX, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALUX, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_mul64_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALUX, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALUX, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALUX, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_mul64_mem_op =
{
        .nr_uops = 4,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALUX, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALUX, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_ALUX, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_out_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_STORE, .throughput = 10, .latency = 10}
};
static const risc86_instruction_t vector_outs_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD,  .throughput =  1, .latency =  1},
        .uop[1] = {.type = UOP_STORE, .throughput = 10, .latency = 10},
        .uop[2] = {.type = UOP_ALU,   .throughput =  1, .latency =  1}
};
static const risc86_instruction_t vector_pusha_op =
{
        .nr_uops = 8,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_STORE, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_STORE, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_STORE, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_STORE, .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_STORE, .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_STORE, .throughput = 1, .latency = 1},
        .uop[6] = {.type = UOP_STORE, .throughput = 1, .latency = 1},
        .uop[7] = {.type = UOP_STORE, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_popa_op =
{
        .nr_uops = 8,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_LOAD, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_LOAD, .throughput = 1, .latency = 1},
        .uop[3] = {.type = UOP_LOAD, .throughput = 1, .latency = 1},
        .uop[4] = {.type = UOP_LOAD, .throughput = 1, .latency = 1},
        .uop[5] = {.type = UOP_LOAD, .throughput = 1, .latency = 1},
        .uop[6] = {.type = UOP_LOAD, .throughput = 1, .latency = 1},
        .uop[7] = {.type = UOP_LOAD, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_popf_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD, .throughput =  1, .latency = 2},
        .uop[1] = {.type = UOP_ALUX, .throughput = 17, .latency = 17}
};
static const risc86_instruction_t vector_push_mem_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_STORE, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_pushf_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALUX,  .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_STORE, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_ret_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD,   .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_BRANCH, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_retf_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD,   .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU,    .throughput = 3, .latency = 3},
        .uop[2] = {.type = UOP_BRANCH, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_scas_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU,   .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_scasb_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU,   .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_setcc_mem_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALUX,   .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALUX,   .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_FSTORE, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_setcc_reg_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALUX,  .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_ALUX,  .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU,   .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_test_mem_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD, .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALU,  .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_test_mem_b_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD, .throughput = 1, .latency = 2},
        .uop[1] = {.type = UOP_ALUX, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_xchg_mem_op =
{
        .nr_uops = 3,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_LOAD, .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_STORE, .throughput = 1, .latency = 1},
        .uop[2] = {.type = UOP_ALU, .throughput = 1, .latency = 1}
};
static const risc86_instruction_t vector_xlat_op =
{
        .nr_uops = 2,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALU,   .throughput = 1, .latency = 1},
        .uop[1] = {.type = UOP_LOAD,  .throughput = 1, .latency = 2}
};
static const risc86_instruction_t vector_wbinvd_op =
{
        .nr_uops = 1,
        .decode_type = DECODE_VECTOR,
        .uop[0] = {.type = UOP_ALU, .throughput = 10000, .latency = 10000}
};

#define INVALID NULL

static const risc86_instruction_t *opcode_timings[256] =
{
/*      ADD                    ADD                    ADD                   ADD*/
/*00*/  &alux_store_op,        &alu_store_op,         &load_alux_op,        &load_alu_op,
/*      ADD                    ADD                    PUSH ES               POP ES*/
        &alux_op,              &alu_op,               &push_seg_op,         &vector_mov_seg_mem_op,
/*      OR                     OR                     OR                    OR*/
        &alux_store_op,        &alu_store_op,         &load_alux_op,        &load_alu_op,
/*      OR                     OR                     PUSH CS               */
        &alux_op,              &alu_op,               &push_seg_op,         INVALID,

/*      ADC                    ADC                    ADC                   ADC*/
/*10*/  &vector_alux_store_op, &vector_alu_store_op,  &vector_load_alux_op, &vector_load_alu_op,
/*      ADC                    ADC                    PUSH SS               POP SS*/
        &vector_alux1_op,      &vector_alu1_op,       &push_seg_op,         &vector_mov_seg_mem_op,
/*      SBB                    SBB                    SBB                   SBB*/
/*10*/  &vector_alux_store_op, &vector_alu_store_op,  &vector_load_alux_op, &vector_load_alu_op,
/*      SBB                    SBB                    PUSH DS               POP DS*/
        &vector_alux1_op,      &vector_alu1_op,       &push_seg_op,         &vector_mov_seg_mem_op,

/*      AND                    AND                    AND                   AND*/
/*20*/  &alux_store_op,        &alu_store_op,         &load_alux_op,        &load_alu_op,
/*      AND                    AND                                          DAA*/
        &alux_op,              &alu_op,               INVALID,              &vector_alux1_op,
/*      SUB                    SUB                    SUB                   SUB*/
        &alux_store_op,        &alu_store_op,         &load_alux_op,        &load_alu_op,
/*      SUB                    SUB                                          DAS*/
        &alux_op,              &alu_op,               INVALID,              &vector_alux1_op,

/*      XOR                    XOR                    XOR                   XOR*/
/*30*/  &alux_store_op,        &alu_store_op,         &load_alux_op,        &load_alu_op,
/*      XOR                    XOR                                          AAA*/
        &alux_op,              &alu_op,               INVALID,              &vector_alux6_op,
/*      CMP                    CMP                    CMP                   CMP*/
        &load_alux_op,         &load_alu_op,          &load_alux_op,        &load_alu_op,
/*      CMP                    CMP                                          AAS*/
        &alux_op,              &alu_op,               INVALID,              &vector_alux6_op,

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
/*60*/  &vector_pusha_op,      &vector_popa_op,       &vector_bound_op,      &vector_arpl_op,
        INVALID,               INVALID,               INVALID,               INVALID,
/*      PUSH imm               IMUL                   PUSH imm               IMUL*/
        &push_imm_op,          &vector_mul_op,        &push_imm_op,          &vector_mul_op,
/*      INSB                   INSW                   OUTSB                  OUTSW*/
        &vector_ins_op,        &vector_ins_op,        &vector_outs_op,       &vector_outs_op,

/*      Jxx*/
/*70*/  &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,

/*80*/  INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      TEST                       TEST                       XCHG                       XCHG*/
        &vector_test_mem_b_op,     &vector_test_mem_op,       &vector_xchg_mem_op,       &vector_xchg_mem_op,
/*      MOV                        MOV                        MOV                        MOV*/
        &store_op,                 &store_op,                 &load_op,                  &load_op,
/*      MOV from seg               LEA                        MOV to seg                 POP*/
        &vector_mov_mem_seg_op,    &store_op,                 &vector_mov_seg_mem_op,    &pop_mem_op,

/*      NOP                        XCHG                       XCHG                       XCHG*/
/*90*/  &limm_op,                  &xchg_op,                  &xchg_op,                  &xchg_op,
/*      XCHG                       XCHG                       XCHG                       XCHG*/
        &xchg_op,                  &xchg_op,                  &xchg_op,                  &xchg_op,
/*      CBW                        CWD                        CALL far                   WAIT*/
        &vector_alu1_op,           &vector_alu1_op,           &vector_call_far_op,       &limm_op,
/*      PUSHF                      POPF                       SAHF                       LAHF*/
        &vector_pushf_op,          &vector_popf_op,           &vector_alux1_op,          &vector_alux1_op,

/*      MOV                        MOV                        MOV                        MOV*/
/*a0*/  &load_op,                  &load_op,                  &store_op,                 &store_op,
/*      MOVSB                      MOVSW                      CMPSB                      CMPSW*/
        &movs_op,                  &movs_op,                  &vector_cmpsb_op,          &vector_cmps_op,
/*      TEST                       TEST                       STOSB                      STOSW*/
        &test_reg_b_op,            &test_reg_op,              &stos_op,                  &stos_op,
/*      LODSB                      LODSW                      SCASB                      SCASW*/
        &lods_op,                  &lods_op,                  &vector_scasb_op,          &vector_scas_op,

/*      MOV*/
/*b0*/  &limm_op,       &limm_op,       &limm_op,       &limm_op,
        &limm_op,       &limm_op,       &limm_op,       &limm_op,
        &limm_op,       &limm_op,       &limm_op,       &limm_op,
        &limm_op,       &limm_op,       &limm_op,       &limm_op,

/*                                                            RET imm                    RET*/
/*c0*/  INVALID,                   INVALID,                   &vector_ret_op,            &vector_ret_op,
/*      LES                        LDS                        MOV                        MOV*/
        &vector_lss_op,            &vector_lss_op,            &store_op,                 &store_op,
/*      ENTER                      LEAVE                      RETF                       RETF*/
        &vector_enter_op,          &leave_op,                 &vector_retf_op,           &vector_retf_op,
/*      INT3                       INT                        INTO                       IRET*/
        &vector_int_op,            &vector_int_op,            &vector_int_op,            &vector_iret_op,


/*d0*/  INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      AAM                        AAD                        SETALC                     XLAT*/
        &vector_alux6_op,          &vector_alux3_op,          &vector_alux1_op,          &vector_xlat_op,
        INVALID,                   INVALID,                   INVALID,                   INVALID,
        INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      LOOPNE                     LOOPE                      LOOP                       JCXZ*/
/*e0*/  &vector_loop_op,           &vector_loop_op,           &loop_op,                  &vector_loop_op,
/*      IN AL                      IN AX                      OUT_AL                     OUT_AX*/
        &vector_in_op,             &vector_in_op,             &vector_out_op,            &vector_out_op,
/*      CALL                       JMP                        JMP                        JMP*/
        &store_op,                 &branch_op,                &vector_jmp_far_op,           &branch_op,
/*      IN AL                      IN AX                      OUT_AL                     OUT_AX*/
        &vector_in_op,             &vector_in_op,             &vector_out_op,            &vector_out_op,

/*                                                            REPNE                      REPE*/
/*f0*/  INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      HLT                        CMC*/
        &vector_alux1_op,          &vector_alu2_op,           INVALID,                   INVALID,
/*      CLC                        STC                        CLI                        STI*/
        &vector_alu1_op,           &vector_alu1_op,           &vector_cli_sti_op,        &vector_cli_sti_op,
/*      CLD                        STD                        INCDEC*/
        &vector_alu1_op,           &vector_alu1_op,           &alux_store_op,            INVALID
};

static const risc86_instruction_t *opcode_timings_mod3[256] =
{
/*      ADD                       ADD                       ADD                       ADD*/
/*00*/  &alux_op,                 &alu_op,                  &alux_op,                 &alu_op,
/*      ADD                       ADD                       PUSH ES                   POP ES*/
        &alux_op,                 &alu_op,                  &push_seg_op,             &vector_mov_seg_mem_op,
/*      OR                        OR                        OR                        OR*/
        &alux_op,                 &alu_op,                  &alux_op,                 &alu_op,
/*      OR                        OR                        PUSH CS                   */
        &alux_op,                 &alu_op,                  &push_seg_op,             INVALID,

/*      ADC                       ADC                       ADC                       ADC*/
/*10*/  &vector_alux1_op,         &vector_alu1_op,          &vector_alux1_op,         &vector_alu1_op,
/*      ADC                       ADC                       PUSH SS                   POP SS*/
        &vector_alux1_op,         &vector_alu1_op,          &push_seg_op,             &vector_mov_seg_mem_op,
/*      SBB                       SBB                       SBB                       SBB*/
        &vector_alux1_op,         &vector_alu1_op,          &vector_alux1_op,         &vector_alu1_op,
/*      SBB                       SBB                       PUSH DS                   POP DS*/
        &vector_alux1_op,         &vector_alu1_op,          &push_seg_op,             &vector_mov_seg_mem_op,

/*      AND                       AND                       AND                       AND*/
/*20*/  &alux_op,                 &alu_op,                  &alux_op,                 &alu_op,
/*      AND                       AND                                                 DAA*/
        &alux_op,                 &alu_op,                  INVALID,                  &vector_alux1_op,
/*      SUB                       SUB                       SUB                       SUB*/
        &alux_op,                 &alu_op,                  &alux_op,                 &alu_op,
/*      SUB                       SUB                                                 DAS*/
        &alux_op,                 &alu_op,                  INVALID,                  &vector_alux1_op,

/*      XOR                       XOR                       XOR                       XOR*/
/*30*/  &alux_op,                 &alu_op,                  &alux_op,                 &alu_op,
/*      XOR                       XOR                                                 AAA*/
        &alux_op,                 &alu_op,                  INVALID,                  &vector_alux6_op,
/*      CMP                       CMP                       CMP                       CMP*/
        &alux_op,                 &alu_op,                  &alux_op,                 &alu_op,
/*      CMP                       CMP                                                 AAS*/
        &alux_op,                 &alu_op,                  INVALID,                  &vector_alux6_op,

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
/*60*/  &vector_pusha_op,      &vector_popa_op,       &vector_bound_op,      &vector_arpl_op,
        INVALID,               INVALID,               INVALID,               INVALID,
/*      PUSH imm               IMUL                   PUSH imm               IMUL*/
        &push_imm_op,          &vector_mul_op,        &push_imm_op,          &vector_mul_op,
/*      INSB                   INSW                   OUTSB                  OUTSW*/
        &vector_ins_op,        &vector_ins_op,        &vector_outs_op,       &vector_outs_op,

/*      Jxx*/
/*70*/  &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,

/*80*/  INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      TEST                       TEST                       XCHG                       XCHG*/
        &vector_alu1_op,           &vector_alu1_op,           &vector_alu3_op,           &vector_alu3_op,
/*      MOV                        MOV                        MOV                        MOV*/
        &store_op,                 &store_op,                 &load_op,                  &load_op,
/*      MOV from seg               LEA                        MOV to seg                 POP*/
        &mov_reg_seg_op,           &store_op,                 &vector_mov_seg_reg_op,    &pop_reg_op,

/*      NOP                        XCHG                       XCHG                       XCHG*/
/*90*/  &limm_op,                  &xchg_op,                  &xchg_op,                  &xchg_op,
/*      XCHG                       XCHG                       XCHG                       XCHG*/
        &xchg_op,                  &xchg_op,                  &xchg_op,                  &xchg_op,
/*      CBW                        CWD                        CALL far                   WAIT*/
        &vector_alu1_op,           &vector_alu1_op,           &vector_call_far_op,       &limm_op,
/*      PUSHF                      POPF                       SAHF                       LAHF*/
        &vector_pushf_op,          &vector_popf_op,           &vector_alux1_op,          &vector_alux1_op,

/*      MOV                        MOV                        MOV                        MOV*/
/*a0*/  &load_op,                  &load_op,                  &store_op,                 &store_op,
/*      MOVSB                      MOVSW                      CMPSB                      CMPSW*/
        &movs_op,                  &movs_op,                  &vector_cmpsb_op,          &vector_cmps_op,
/*      TEST                       TEST                       STOSB                      STOSW*/
        &test_reg_b_op,            &test_reg_op,              &stos_op,                  &stos_op,
/*      LODSB                      LODSW                      SCASB                      SCASW*/
        &lods_op,                  &lods_op,                  &vector_scasb_op,          &vector_scas_op,

/*      MOV*/
/*b0*/  &limm_op,       &limm_op,       &limm_op,       &limm_op,
        &limm_op,       &limm_op,       &limm_op,       &limm_op,
        &limm_op,       &limm_op,       &limm_op,       &limm_op,
        &limm_op,       &limm_op,       &limm_op,       &limm_op,

/*                                                            RET imm                    RET*/
/*c0*/  INVALID,                   INVALID,                   &vector_ret_op,            &vector_ret_op,
/*      LES                        LDS                        MOV                        MOV*/
        &vector_lss_op,            &vector_lss_op,            &store_op,                 &store_op,
/*      ENTER                      LEAVE                      RETF                       RETF*/
        &vector_enter_op,          &leave_op,                 &vector_retf_op,           &vector_retf_op,
/*      INT3                       INT                        INTO                       IRET*/
        &vector_int_op,            &vector_int_op,            &vector_int_op,            &vector_iret_op,


/*d0*/  INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      AAM                        AAD                        SETALC                     XLAT*/
        &vector_alux6_op,          &vector_alux3_op,          &vector_alux1_op,          &vector_xlat_op,
        INVALID,                   INVALID,                   INVALID,                   INVALID,
        INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      LOOPNE                     LOOPE                      LOOP                       JCXZ*/
/*e0*/  &vector_loop_op,           &vector_loop_op,           &loop_op,                  &vector_loop_op,
/*      IN AL                      IN AX                      OUT_AL                     OUT_AX*/
        &vector_in_op,             &vector_in_op,             &vector_out_op,            &vector_out_op,
/*      CALL                       JMP                        JMP                        JMP*/
        &store_op,                 &branch_op,                &vector_jmp_far_op,        &branch_op,
/*      IN AL                      IN AX                      OUT_AL                     OUT_AX*/
        &vector_in_op,             &vector_in_op,             &vector_out_op,            &vector_out_op,

/*                                                            REPNE                      REPE*/
/*f0*/  INVALID,                   INVALID,                   INVALID,                   INVALID,
/*      HLT                        CMC*/
        &vector_alux1_op,          &vector_alu2_op,           INVALID,                   INVALID,
/*      CLC                        STC                        CLI                        STI*/
        &vector_alu1_op,           &vector_alu1_op,           &vector_cli_sti_op,        &vector_cli_sti_op,
/*      CLD                        STD                        INCDEC*/
        &vector_alu1_op,           &vector_alu1_op,           &vector_alux1_op,          INVALID
};

static const risc86_instruction_t *opcode_timings_0f[256] =
{
/*00*/  &vector_alu6_op,        &vector_alu6_op,        &vector_alu6_op,        &vector_alu6_op,
        INVALID,                &vector_alu6_op,        &vector_alu6_op,        INVALID,
        &vector_invd_op,        &vector_wbinvd_op,      INVALID,                INVALID,
        INVALID,                &load_op,               &vector_femms_op,       &load_3dn_op,

/*10*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*20*/  &vector_alu6_op,        &vector_alu6_op,        &vector_alu6_op,        &vector_alu6_op,
        &vector_alu6_op,        &vector_alu6_op,        INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*30*/  &vector_alu6_op,        &vector_alu6_op,        &vector_alu6_op,        INVALID,
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
        &load_mmx_op,           &load_mmx_op,           &load_mmx_op,           &vector_emms_op,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                &mstore_op,             &mstore_op,

/*80*/  &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,

/*90*/  &vector_setcc_reg_op, &vector_setcc_reg_op, &vector_setcc_reg_op, &vector_setcc_reg_op,
        &vector_setcc_reg_op, &vector_setcc_reg_op, &vector_setcc_reg_op, &vector_setcc_reg_op,
        &vector_setcc_reg_op, &vector_setcc_reg_op, &vector_setcc_reg_op, &vector_setcc_reg_op,
        &vector_setcc_reg_op, &vector_setcc_reg_op, &vector_setcc_reg_op, &vector_setcc_reg_op,

/*a0*/  &push_seg_op,         &vector_mov_seg_mem_op,   &vector_cpuid_op,       &vector_load_alu_op,
        &vector_alu_store_op, &vector_alu_store_op,     INVALID,                INVALID,
        &push_seg_op,         &vector_mov_seg_mem_op,   INVALID,                &vector_load_alu_op,
        &vector_alu_store_op, &vector_alu_store_op,     INVALID,                &vector_mul_op,

/*b0*/  &vector_cmpxchg_b_op,   &vector_cmpxchg_op,     &vector_lss_op,         &vector_load_alu_op,
        &vector_lss_op,         &vector_lss_op,         &load_alux_op,          &load_alu_op,
        INVALID,                INVALID,                &vector_load_alu_op,    &vector_load_alu_op,
        &vector_bsx_op,         &vector_bsx_op,         &load_alux_op,          &load_alu_op,

/*c0*/  &vector_alux_store_op,  &vector_alu_store_op,   INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                &vector_cmpxchg_op,
        &bswap_op,              &bswap_op,              &bswap_op,              &bswap_op,
        &bswap_op,              &bswap_op,              &bswap_op,              &bswap_op,

/*d0*/  INVALID,                &load_mmx_shift_op,     &load_mmx_shift_op,     &load_mmx_shift_op,
        INVALID,                &load_mmx_mul_op,       INVALID,                INVALID,
        &load_mmx_op,           &load_mmx_op,           INVALID,                &load_mmx_op,
        &load_mmx_op,           &load_mmx_op,           INVALID,                &load_mmx_op,

/*e0*/  &load_mmx_op,           &load_mmx_shift_op,     &load_mmx_shift_op,     INVALID,
        INVALID,                &pmul_mem_op,           INVALID,                INVALID,
        &load_mmx_op,           &load_mmx_op,           INVALID,                &load_mmx_op,
        &load_mmx_op,           &load_mmx_op,           INVALID,                &load_mmx_op,

/*f0*/  INVALID,                &load_mmx_shift_op,     &load_mmx_shift_op,     &load_mmx_shift_op,
        INVALID,                &pmul_mem_op,           INVALID,                INVALID,
        &load_mmx_op,           &load_mmx_op,           &load_mmx_op,           INVALID,
        &load_mmx_op,           &load_mmx_op,           &load_mmx_op,           INVALID,
};
static const risc86_instruction_t *opcode_timings_0f_mod3[256] =
{
/*00*/  &vector_alu6_op,        &vector_alu6_op,        &vector_alu6_op,        &vector_alu6_op,
        INVALID,                &vector_alu6_op,        &vector_alu6_op,        INVALID,
        &vector_invd_op,        &vector_wbinvd_op,      INVALID,                INVALID,
        INVALID,                INVALID,                &vector_femms_op,       &m3dn_op,

/*10*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*20*/  &vector_alu6_op,        &vector_alu6_op,        &vector_alu6_op,        &vector_alu6_op,
        &vector_alu6_op,        &vector_alu6_op,        INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*30*/  &vector_alu6_op,        &vector_alu6_op,        &vector_alu6_op,        INVALID,
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
        &mmx_op,                &mmx_op,                &mmx_op,                &vector_emms_op,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                &mmx_op,                &mmx_op,

/*80*/  &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,
        &branch_op,     &branch_op,     &branch_op,     &branch_op,

/*90*/  &vector_setcc_mem_op, &vector_setcc_mem_op, &vector_setcc_mem_op, &vector_setcc_mem_op,
        &vector_setcc_mem_op, &vector_setcc_mem_op, &vector_setcc_mem_op, &vector_setcc_mem_op,
        &vector_setcc_mem_op, &vector_setcc_mem_op, &vector_setcc_mem_op, &vector_setcc_mem_op,
        &vector_setcc_mem_op, &vector_setcc_mem_op, &vector_setcc_mem_op, &vector_setcc_mem_op,

/*a0*/  &push_seg_op,         &vector_mov_seg_mem_op,   &vector_cpuid_op,       &vector_alu1_op,
        &vector_alu1_op,      &vector_alu1_op,          INVALID,                INVALID,
        &push_seg_op,         &vector_mov_seg_mem_op,   INVALID,                &vector_alu1_op,
        &vector_alu1_op,      &vector_alu1_op,          INVALID,                &vector_mul_op,

/*b0*/  &vector_cmpxchg_b_op,   &vector_cmpxchg_op,     &vector_lss_op,         &vector_alu1_op,
        &vector_lss_op,         &vector_lss_op,         &alux_op,               &alu_op,
        INVALID,                INVALID,                &vector_alu1_op,        &vector_alu1_op,
        &vector_bsx_op,         &vector_bsx_op,         &alux_op,               &alu_op,

/*c0*/  &vector_alux1_op,       &vector_alu1_op,        INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        &bswap_op,              &bswap_op,              &bswap_op,              &bswap_op,
        &bswap_op,              &bswap_op,              &bswap_op,              &bswap_op,

/*d0*/  INVALID,                &mmx_shift_op,          &mmx_shift_op,          &mmx_shift_op,
        INVALID,                &mmx_mul_op,            INVALID,                INVALID,
        &mmx_op,                &mmx_op,                INVALID,                &mmx_op,
        &mmx_op,                &mmx_op,                INVALID,                &mmx_op,

/*e0*/  &mmx_op,                &mmx_shift_op,          &mmx_shift_op,          INVALID,
        INVALID,                &pmul_op,               INVALID,                INVALID,
        &mmx_op,                &mmx_op,                INVALID,                &mmx_op,
        &mmx_op,                &mmx_op,                INVALID,                &mmx_op,

/*f0*/  INVALID,                &mmx_shift_op,          &mmx_shift_op,          &mmx_shift_op,
        INVALID,                &pmul_op,               INVALID,                INVALID,
        &mmx_op,                &mmx_op,                &mmx_op,                INVALID,
        &mmx_op,                &mmx_op,                &mmx_op,                INVALID,
};

static const risc86_instruction_t *opcode_timings_0f0f[256] =
{
/*00*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                &load_3dn_op,           INVALID,                INVALID,

/*10*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                &load_3dn_op,           INVALID,                INVALID,

/*20*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*30*/  INVALID,                INVALID,                INVALID,                INVALID,
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

/*60*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*70*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*80*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*90*/  &load_3dn_op,           INVALID,                INVALID,                INVALID,
        &load_3dn_op,           INVALID,                &load_3dn_op,           &load_3dn_op,
        INVALID,                INVALID,                &load_3dn_op,           INVALID,
        INVALID,                INVALID,                &load_3dn_op,           INVALID,

/*a0*/  &load_3dn_op,           INVALID,                INVALID,                INVALID,
        &load_3dn_op,           INVALID,                &load_mmx_mul_op,       &load_mmx_mul_op,
        INVALID,                INVALID,                &load_3dn_op,           INVALID,
        INVALID,                INVALID,                &load_3dn_op,           INVALID,

/*b0*/  &load_3dn_op,           INVALID,                INVALID,                INVALID,
        &load_mmx_mul_op,       INVALID,                &load_mmx_mul_op,       &load_mmx_mul_op,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                &load_mmx_op,

/*c0*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*d0*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*e0*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*f0*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

};
static const risc86_instruction_t *opcode_timings_0f0f_mod3[256] =
{
/*00*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                &m3dn_op,               INVALID,                INVALID,

/*10*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                &m3dn_op,               INVALID,                INVALID,

/*20*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*30*/  INVALID,                INVALID,                INVALID,                INVALID,
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

/*60*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*70*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*80*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*90*/  &m3dn_op,               INVALID,                INVALID,                INVALID,
        &m3dn_op,               INVALID,                &m3dn_op,               &m3dn_op,
        INVALID,                INVALID,                &m3dn_op,               INVALID,
        INVALID,                INVALID,                &m3dn_op,               INVALID,

/*a0*/  &m3dn_op,               INVALID,                INVALID,                INVALID,
        &m3dn_op,               INVALID,                &mmx_mul_op,            &mmx_mul_op,
        INVALID,                INVALID,                &m3dn_op,               INVALID,
        INVALID,                INVALID,                &m3dn_op,               INVALID,

/*b0*/  &m3dn_op,               INVALID,                INVALID,                INVALID,
        &mmx_mul_op,            INVALID,                &mmx_mul_op,            &mmx_mul_op,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                &mmx_op,

/*c0*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*d0*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*e0*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

/*f0*/  INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,

};

static const risc86_instruction_t *opcode_timings_shift[8] =
{
        &vector_alu_store_op,   &vector_alu_store_op,   &vector_alu_store_op,   &vector_alu_store_op,
        &vector_alu_store_op,   &vector_alu_store_op,   &vector_alu_store_op,   &vector_alu_store_op
};
static const risc86_instruction_t *opcode_timings_shift_b[8] =
{
        &vector_alux_store_op,  &vector_alux_store_op,  &vector_alux_store_op,  &vector_alux_store_op,
        &vector_alux_store_op,  &vector_alux_store_op,  &vector_alux_store_op,  &vector_alux_store_op
};
static const risc86_instruction_t *opcode_timings_shift_mod3[8] =
{
        &vector_alu1_op,   &vector_alu1_op,   &vector_alu1_op,   &vector_alu1_op,
        &alu_op,           &alu_op,           &alu_op,           &alu_op
};
static const risc86_instruction_t *opcode_timings_shift_b_mod3[8] =
{
        &vector_alux1_op,  &vector_alux1_op,  &vector_alux1_op,  &vector_alux1_op,
        &alux_op,          &alux_op,          &alux_op,          &alux_op
};

static const risc86_instruction_t *opcode_timings_80[8] =
{
        &alux_store_op, &alux_store_op, &vector_alux_store_op,  &vector_alux_store_op,
        &alux_store_op, &alux_store_op, &alux_store_op,         &alux_store_op,
};
static const risc86_instruction_t *opcode_timings_80_mod3[8] =
{
        &alux_op,       &alux_op,       &alux_store_op,         &alux_store_op,
        &alux_op,       &alux_op,       &alux_op,               &alux_op,
};
static const risc86_instruction_t *opcode_timings_8x[8] =
{
        &alu_store_op,  &alu_store_op,  &vector_alu_store_op,   &vector_alu_store_op,
        &alu_store_op,  &alu_store_op,  &alu_store_op,          &alu_store_op,
};
static const risc86_instruction_t *opcode_timings_8x_mod3[8] =
{
        &alu_op,        &alu_op,        &alu_store_op,          &alu_store_op,
        &alu_op,        &alu_op,        &alu_op,                &alu_op,
};

static const risc86_instruction_t *opcode_timings_f6[8] =
{
/*      TST                                             NOT                     NEG*/
        &test_mem_imm_b_op,     INVALID,                &vector_alux_store_op,  &vector_alux_store_op,
/*      MUL                     IMUL                    DIV                     IDIV*/
        &vector_mul_mem_op,     &vector_mul_mem_op,     &vector_div16_mem_op,   &vector_div16_mem_op,
};
static const risc86_instruction_t *opcode_timings_f6_mod3[8] =
{
/*      TST                                             NOT                     NEG*/
        &test_reg_b_op,         INVALID,                &alux_op,               &alux_op,
/*      MUL                     IMUL                    DIV                     IDIV*/
        &vector_mul_op,         &vector_mul_op,         &vector_div16_op,       &vector_div16_op,
};
static const risc86_instruction_t *opcode_timings_f7[8] =
{
/*      TST                                             NOT                     NEG*/
        &test_mem_imm_op,       INVALID,                &vector_alu_store_op,   &vector_alu_store_op,
/*      MUL                     IMUL                    DIV                     IDIV*/
        &vector_mul64_mem_op,   &vector_mul64_mem_op,   &vector_div32_mem_op,   &vector_div32_mem_op,
};
static const risc86_instruction_t *opcode_timings_f7_mod3[8] =
{
/*      TST                                             NOT                     NEG*/
        &test_reg_op,           INVALID,                &alu_op,                &alu_op,
/*      MUL                     IMUL                    DIV                     IDIV*/
        &vector_mul64_op,       &vector_mul64_op,       &vector_div32_op,       &vector_div32_op,
};
static const risc86_instruction_t *opcode_timings_ff[8] =
{
/*      INC                     DEC                     CALL                    CALL far*/
        &alu_store_op,          &alu_store_op,          &store_op,              &vector_call_far_op,
/*      JMP                     JMP far                 PUSH*/
        &branch_op,             &vector_jmp_far_op,     &push_mem_op,           INVALID
};
static const risc86_instruction_t *opcode_timings_ff_mod3[8] =
{
/*      INC                     DEC                     CALL                    CALL far*/
        &vector_alu1_op,        &vector_alu1_op,        &store_op,              &vector_call_far_op,
/*      JMP                     JMP far                 PUSH*/
        &branch_op,             &vector_jmp_far_op,     &vector_push_mem_op,    INVALID
};

static const risc86_instruction_t *opcode_timings_d8[8] =
{
/*      FADDs            FMULs            FCOMs            FCOMPs*/
        &load_float_op,  &load_float_op,  &load_float_op,  &load_float_op,
/*      FSUBs            FSUBRs           FDIVs            FDIVRs*/
        &load_float_op,  &load_float_op,  &fdiv_mem_op,    &fdiv_mem_op,
};
static const risc86_instruction_t *opcode_timings_d8_mod3[8] =
{
/*      FADD             FMUL             FCOM             FCOMP*/
        &float_op,       &float_op,       &float_op,       &float_op,
/*      FSUB             FSUBR            FDIV             FDIVR*/
        &float_op,       &float_op,       &fdiv_op,        &fdiv_op,
};

static const risc86_instruction_t *opcode_timings_d9[8] =
{
/*      FLDs                                    FSTs                 FSTPs*/
        &load_float_op,      INVALID,           &fstore_op,          &fstore_op,
/*      FLDENV               FLDCW              FSTENV               FSTCW*/
        &vector_float_l_op,  &vector_fldcw_op,  &vector_float_l_op,  &vector_float_op
};
static const risc86_instruction_t *opcode_timings_d9_mod3[64] =
{
        /*FLD*/
        &float_op,    &float_op,    &float_op,    &float_op,
        &float_op,    &float_op,    &float_op,    &float_op,
        /*FXCH*/
        &float_op,    &float_op,    &float_op,    &float_op,
        &float_op,    &float_op,    &float_op,    &float_op,
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

static const risc86_instruction_t *opcode_timings_da[8] =
{
/*      FIADDl            FIMULl            FICOMl            FICOMPl*/
        &load_float_op,   &load_float_op,   &load_float_op,   &load_float_op,
/*      FISUBl            FISUBRl           FIDIVl            FIDIVRl*/
        &load_float_op,   &load_float_op,   &fdiv_mem_op,     &fdiv_mem_op,
};
static const risc86_instruction_t *opcode_timings_da_mod3[8] =
{
        INVALID,          INVALID,          INVALID,          INVALID,
/*                        FCOMPP*/
        INVALID,          &float_op,        INVALID,          INVALID
};

static const risc86_instruction_t *opcode_timings_db[8] =
{
/*      FLDil                               FSTil         FSTPil*/
        &load_float_op,   INVALID,          &fstore_op,   &fstore_op,
/*                        FLDe                            FSTPe*/
        INVALID,          &vector_flde_op,  INVALID,      &vector_fste_op
};
static const risc86_instruction_t *opcode_timings_db_mod3[64] =
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

static const risc86_instruction_t *opcode_timings_dc[8] =
{
/*      FADDd             FMULd             FCOMd             FCOMPd*/
        &load_float_op,   &load_float_op,   &load_float_op,   &load_float_op,
/*      FSUBd             FSUBRd            FDIVd             FDIVRd*/
        &load_float_op,   &load_float_op,   &fdiv_mem_op,     &fdiv_mem_op,
};
static const risc86_instruction_t *opcode_timings_dc_mod3[8] =
{
/*      opFADDr           opFMULr*/
        &float_op,        &float_op,        INVALID,          INVALID,
/*      opFSUBRr          opFSUBr           opFDIVRr          opFDIVr*/
        &float_op,        &float_op,        &fdiv_op,         &fdiv_op
};

static const risc86_instruction_t *opcode_timings_dd[8] =
{
/*      FLDd                                    FSTd                 FSTPd*/
        &load_float_op,     INVALID,            &fstore_op,          &fstore_op,
/*      FRSTOR                                  FSAVE                FSTSW*/
        &vector_float_l_op, INVALID,            &vector_float_l_op,  &vector_float_l_op
};
static const risc86_instruction_t *opcode_timings_dd_mod3[8] =
{
/*      FFFREE                            FST                FSTP*/
        &float_op,       INVALID,         &float_op,         &float_op,
/*      FUCOM            FUCOMP*/
        &float_op,       &float_op,       INVALID,           INVALID
};

static const risc86_instruction_t *opcode_timings_de[8] =
{
/*      FIADDw            FIMULw            FICOMw            FICOMPw*/
        &load_float_op,   &load_float_op,   &load_float_op,   &load_float_op,
/*      FISUBw            FISUBRw           FIDIVw            FIDIVRw*/
        &load_float_op,   &load_float_op,   &fdiv_mem_op,     &fdiv_mem_op,
};
static const risc86_instruction_t *opcode_timings_de_mod3[8] =
{
/*      FADDP            FMULP                          FCOMPP*/
        &float_op,       &float_op,       INVALID,      &float_op,
/*      FSUBP            FSUBRP           FDIVP         FDIVRP*/
        &float_op,       &float_op,       &fdiv_op,     &fdiv_op,
};

static const risc86_instruction_t *opcode_timings_df[8] =
{
/*      FILDiw                              FISTiw               FISTPiw*/
        &load_float_op,   INVALID,          &fstore_op,          &fstore_op,
/*                        FILDiq            FBSTP                FISTPiq*/
        INVALID,          &load_float_op,   &vector_float_l_op,  &fstore_op,
};
static const risc86_instruction_t *opcode_timings_df_mod3[8] =
{
        INVALID,      INVALID,      INVALID,      INVALID,
/*      FSTSW AX*/
        &float_op,    INVALID,      INVALID,      INVALID
};


static uint8_t last_prefix;
static int prefixes;

static int decode_timestamp;
static int last_complete_timestamp;

typedef struct k6_unit_t
{
        uint32_t uop_mask;
        int first_available_cycle;
} k6_unit_t;

static int nr_units;
static k6_unit_t *units;

/*K6 has dedicated MMX unit*/
static k6_unit_t k6_units[] =
{
        {.uop_mask = (1 << UOP_ALU) | (1 << UOP_ALUX)},                                 /*Integer X*/
        {.uop_mask = (1 << UOP_ALU)},                                                   /*Integer Y*/
        {.uop_mask = (1 << UOP_MEU) | (1 << UOP_MEU_SHIFT) | (1 << UOP_MEU_MUL)},       /*Multimedia*/
        {.uop_mask = (1 << UOP_FLOAT)},                                                 /*Floating point*/
        {.uop_mask = (1 << UOP_LOAD)  | (1 << UOP_FLOAD)  | (1 << UOP_MLOAD)},          /*Load*/
        {.uop_mask = (1 << UOP_STORE) | (1 << UOP_FSTORE) | (1 << UOP_MSTORE)},         /*Store*/
        {.uop_mask = (1 << UOP_BRANCH)}                                                 /*Branch*/
};
#define NR_K6_UNITS (sizeof(k6_units) / sizeof(k6_unit_t))

/*K6-2 and later integrate MMX into ALU X & Y, sharing multiplier, shifter and
  3DNow ALU between two execution units*/
static k6_unit_t k6_2_units[] =
{
        {.uop_mask = (1 << UOP_ALU) | (1 << UOP_ALUX) | (1 << UOP_MEU) |        /*Integer X*/
                        (1 << UOP_MEU_SHIFT) | (1 << UOP_MEU_MUL) | (1 << UOP_MEU_3DN)},
        {.uop_mask = (1 << UOP_ALU) | (1 << UOP_MEU) |                          /*Integer Y*/
                        (1 << UOP_MEU_SHIFT) | (1 << UOP_MEU_MUL) | (1 << UOP_MEU_3DN)},
        {.uop_mask = (1 << UOP_FLOAT)},                                         /*Floating point*/
        {.uop_mask = (1 << UOP_LOAD)  | (1 << UOP_FLOAD)  | (1 << UOP_MLOAD)},  /*Load*/
        {.uop_mask = (1 << UOP_STORE) | (1 << UOP_FSTORE) | (1 << UOP_MSTORE)}, /*Store*/
        {.uop_mask = (1 << UOP_BRANCH)}                                         /*Branch*/
};
#define NR_K6_2_UNITS (sizeof(k6_2_units) / sizeof(k6_unit_t))

/*First available cycles of shared execution units. Each of these can be submitted
  to by ALU X and Y*/
static int mul_first_available_cycle;
static int shift_first_available_cycle;
static int m3dnow_first_available_cycle;

static int uop_run(const risc86_uop_t *uop, int decode_time)
{
        int c;
        k6_unit_t *best_unit = NULL;
        int best_start_cycle = 99999;

        /*UOP_LIMM does not require execution*/
        if (uop->type == UOP_LIMM)
                return decode_time;

        /*Handle shared units on K6-2 and later*/
        if (units == k6_2_units)
        {
                if (uop->type == UOP_MEU_MUL && decode_time < mul_first_available_cycle)
                        decode_time = mul_first_available_cycle;
                else if (uop->type == UOP_MEU_SHIFT && decode_time < mul_first_available_cycle)
                        decode_time = shift_first_available_cycle;
                else if (uop->type == UOP_MEU_3DN && decode_time < mul_first_available_cycle)
                        decode_time = m3dnow_first_available_cycle;
        }

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
        best_unit->first_available_cycle = best_start_cycle + uop->throughput;

        if (units == k6_2_units)
        {
                if (uop->type == UOP_MEU_MUL)
                        mul_first_available_cycle = best_start_cycle + uop->throughput;
                else if (uop->type == UOP_MEU_SHIFT)
                        shift_first_available_cycle = best_start_cycle + uop->throughput;
                else if (uop->type == UOP_MEU_3DN)
                        m3dnow_first_available_cycle = best_start_cycle + uop->throughput;
        }

        return best_start_cycle + uop->throughput;
}

/*The K6 decoder can decode, per clock :
  - 1 or 2 'short' instructions, each up to 2 uOPs and 7 bytes long
  - 1 'long' instruction, up to 4 uOPs
  - 1 'vector' instruction, up to 4 uOPs per cycle, plus (I think) 1 cycle startup delay)
*/
static struct
{
        int nr_uops;
        const risc86_uop_t *uops[4];
        /*Earliest time a uop can start. If the timestamp is -1, then the uop is
          part of a dependency chain and the start time is the completion time of
          the previous uop*/
        int earliest_start[4];
} decode_buffer;

#define NR_OPQUADS 6
/*Timestamps of when the last six opquads completed. The K6 scheduler retires
  opquads in order, so this is needed to determine when the next can be scheduled*/
static int opquad_completion_timestamp[NR_OPQUADS];
static int next_opquad = 0;

#define NR_REGS 8
/*Timestamp of when last operation on an integer register completed*/
static int reg_available_timestamp[NR_REGS];
/*Timestamp of when last operation on an FPU register completed*/
static int fpu_st_timestamp[8];
/*Completion time of the last uop to be processed. Used to calculate timing of
  dependent uop chains*/
static int last_uop_timestamp = 0;

void decode_flush()
{
        int c;
        int uop_timestamp = 0;

        /*Decoded opquad can not be submitted if there are no free spaces in the
          opquad buffer*/
        if (decode_timestamp < opquad_completion_timestamp[next_opquad])
                decode_timestamp = opquad_completion_timestamp[next_opquad];

        /*Ensure that uops can not be submitted before they have been decoded*/
        if (decode_timestamp > last_uop_timestamp)
                last_uop_timestamp = decode_timestamp;

        /*Submit uops to execution units, and determine the latest completion time*/
        for (c = 0; c < decode_buffer.nr_uops; c++)
        {
                int start_timestamp;

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

        /*Advance to next opquad in buffer*/
        opquad_completion_timestamp[next_opquad] = last_complete_timestamp;
        next_opquad++;
        if (next_opquad == NR_OPQUADS)
                next_opquad = 0;

        decode_timestamp++;
        decode_buffer.nr_uops = 0;
}

/*The instruction is only of interest here if it's longer than 7 bytes, as that's the
  limit on K6 short decoding*/
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

static void decode_instruction(const risc86_instruction_t *ins, uint64_t deps, uint32_t fetchdat, int op_32, int bit8)
{
        uint32_t regmask_required;
        uint32_t regmask_modified;
        int c, d;
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

        /*Short decoders are limited to 7 bytes*/
        if (decode_type == DECODE_SHORT && instr_length > 7)
                decode_type = DECODE_LONG;
        /*Long decoder is limited to 11 bytes*/
        else if (instr_length > 11)
                decode_type = DECODE_VECTOR;

        switch (decode_type)
        {
                case DECODE_SHORT:
                if (decode_buffer.nr_uops)
                {
                        decode_buffer.uops[decode_buffer.nr_uops] = &ins->uop[0];
                        decode_buffer.earliest_start[decode_buffer.nr_uops] = earliest_start;
                        if (ins->nr_uops > 1)
                        {
                                decode_buffer.uops[decode_buffer.nr_uops+1] = &ins->uop[1];
                                decode_buffer.earliest_start[decode_buffer.nr_uops+1] = -1;
                        }
                        decode_buffer.nr_uops += ins->nr_uops;

                        decode_flush();
                }
                else
                {
                        decode_buffer.nr_uops = ins->nr_uops;
                        decode_buffer.uops[0] = &ins->uop[0];
                        decode_buffer.earliest_start[0] = earliest_start;
                        if (ins->nr_uops > 1)
                        {
                                decode_buffer.uops[1] = &ins->uop[1];
                                decode_buffer.earliest_start[1] = -1;
                        }
                }
                break;

                case DECODE_LONG:
                if (decode_buffer.nr_uops)
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
                decode_flush();
                break;

                case DECODE_VECTOR:
                if (decode_buffer.nr_uops)
                        decode_flush();

                decode_timestamp++;
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

void codegen_timing_k6_block_start()
{
        int c;

        for (c = 0; c < nr_units; c++)
                units[c].first_available_cycle = 0;

        mul_first_available_cycle = 0;
        shift_first_available_cycle = 0;
        m3dnow_first_available_cycle = 0;

        decode_timestamp = 0;
        last_complete_timestamp = 0;

        for (c = 0; c < NR_OPQUADS; c++)
                opquad_completion_timestamp[c] = 0;
        next_opquad = 0;

        for (c = 0; c < NR_REGS; c++)
                reg_available_timestamp[c] = 0;
        for (c = 0; c < 8; c++)
                fpu_st_timestamp[c] = 0;
}

void codegen_timing_k6_start()
{
        if (cpu_s->cpu_type == CPU_K6)
        {
                units = k6_units;
                nr_units = NR_K6_UNITS;
        }
        else
        {
                units = k6_2_units;
                nr_units = NR_K6_2_UNITS;
        }
        last_prefix = 0;
        prefixes = 0;
}

void codegen_timing_k6_prefix(uint8_t prefix, uint32_t fetchdat)
{
        if (prefix != 0x0f)
                decode_timestamp++;

        last_prefix = prefix;
        prefixes++;
}

void codegen_timing_k6_opcode(uint8_t opcode, uint32_t fetchdat, int op_32, uint32_t op_pc)
{
        const risc86_instruction_t **ins_table;
        uint64_t *deps;
        int mod3 = ((fetchdat & 0xc0) == 0xc0);
        int old_last_complete_timestamp = last_complete_timestamp;
        int bit8 = !(opcode & 1);

        switch (last_prefix)
        {
                case 0x0f:
                if (opcode == 0x0f)
                {
                        /*3DNow has the actual opcode after ModR/M, SIB and any offset*/
                        uint32_t opcode_pc = op_pc + 1; /*Byte after ModR/M*/
                        uint8_t modrm = fetchdat & 0xff;
                        uint8_t sib = (fetchdat >> 8) & 0xff;

                        if ((modrm & 0xc0) != 0xc0)
                        {
                                if (op_32 & 0x200)
                                {
                                        if ((modrm & 7) == 4)
                                        {
                                                /* Has SIB*/
                                                opcode_pc++;
                                                if ((modrm & 0xc0) == 0x40)
                                                        opcode_pc++;
                                                else if ((modrm & 0xc0) == 0x80)
                                                        opcode_pc += 4;
                                                else if ((sib & 0x07) == 0x05)
                                                        opcode_pc += 4;
                                        }
                                        else
                                        {
                                                if ((modrm & 0xc0) == 0x40)
                                                        opcode_pc++;
                                                else if ((modrm & 0xc0) == 0x80)
                                                        opcode_pc += 4;
                                                else if ((modrm & 0xc7) == 0x05)
                                                        opcode_pc += 4;
                                        }
                                }
                                else
                                {
                                        if ((modrm & 0xc0) == 0x40)
                                                opcode_pc++;
                                        else if ((modrm & 0xc0) == 0x80)
                                                opcode_pc += 2;
                                        else if ((modrm & 0xc7) == 0x06)
                                                opcode_pc += 2;
                                }
                        }

                        opcode = fastreadb(cs + opcode_pc);

                        ins_table = mod3 ? opcode_timings_0f0f_mod3 : opcode_timings_0f0f;
                        deps = mod3 ? opcode_deps_0f0f_mod3 : opcode_deps_0f0f;
                }
                else
                {
                        ins_table = mod3 ? opcode_timings_0f_mod3 : opcode_timings_0f;
                        deps = mod3 ? opcode_deps_0f_mod3 : opcode_deps_0f;
                }
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
                decode_instruction(&vector_alu1_op, 0, fetchdat, op_32, bit8);
        codegen_block_cycles += (last_complete_timestamp - old_last_complete_timestamp);
}

void codegen_timing_k6_block_end()
{
        if (decode_buffer.nr_uops)
        {
                int old_last_complete_timestamp = last_complete_timestamp;
                decode_flush();
                codegen_block_cycles += (last_complete_timestamp - old_last_complete_timestamp);
        }
}

int codegen_timing_k6_jump_cycles()
{
        if (decode_buffer.nr_uops)
                return 1;
        return 0;
}

codegen_timing_t codegen_timing_k6 =
{
        codegen_timing_k6_start,
        codegen_timing_k6_prefix,
        codegen_timing_k6_opcode,
        codegen_timing_k6_block_start,
        codegen_timing_k6_block_end,
        codegen_timing_k6_jump_cycles
};
