#define REG_R0   0
#define REG_R1   1
#define REG_R2   2
#define REG_R3   3
#define REG_R4   4
#define REG_R5   5
#define REG_R6   6
#define REG_R7   7
#define REG_R8   8
#define REG_R9   9
#define REG_R10 10
#define REG_R11 11
#define REG_R12 12
#define REG_HOST_SP  13
#define REG_LR  14
#define REG_PC  15

#define REG_ARG0 REG_R0
#define REG_ARG1 REG_R1
#define REG_ARG2 REG_R2
#define REG_ARG3 REG_R3

#define REG_CPUSTATE REG_R10

#define REG_TEMP  REG_R3
#define REG_TEMP2 REG_R2

#define REG_D0   0
#define REG_D1   1
#define REG_D2   2
#define REG_D3   3
#define REG_D4   4
#define REG_D5   5
#define REG_D6   6
#define REG_D7   7
#define REG_D8   8
#define REG_D9   9
#define REG_D10 10
#define REG_D11 11
#define REG_D12 12
#define REG_D13 13
#define REG_D14 14
#define REG_D15 15

#define REG_D_TEMP REG_D0
#define REG_Q_TEMP   REG_D0
#define REG_Q_TEMP_2 REG_D2

#define REG_MASK_R0  (1 << REG_R0)
#define REG_MASK_R1  (1 << REG_R1)
#define REG_MASK_R2  (1 << REG_R2)
#define REG_MASK_R3  (1 << REG_R3)
#define REG_MASK_R4  (1 << REG_R4)
#define REG_MASK_R5  (1 << REG_R5)
#define REG_MASK_R6  (1 << REG_R6)
#define REG_MASK_R7  (1 << REG_R7)
#define REG_MASK_R8  (1 << REG_R8)
#define REG_MASK_R9  (1 << REG_R9)
#define REG_MASK_R10 (1 << REG_R10)
#define REG_MASK_R11 (1 << REG_R11)
#define REG_MASK_R12 (1 << REG_R12)
#define REG_MASK_SP  (1 << REG_HOST_SP)
#define REG_MASK_LR  (1 << REG_LR)
#define REG_MASK_PC  (1 << REG_PC)

#define REG_MASK_LOCAL (REG_MASK_R4 | REG_MASK_R5 | REG_MASK_R6 | REG_MASK_R7 | \
			REG_MASK_R8 | REG_MASK_R9 | REG_MASK_R10 | REG_MASK_R11)

#define CODEGEN_HOST_REGS 7
#define CODEGEN_HOST_FP_REGS 8

extern void *codegen_mem_load_byte;
extern void *codegen_mem_load_word;
extern void *codegen_mem_load_long;
extern void *codegen_mem_load_quad;
extern void *codegen_mem_load_single;
extern void *codegen_mem_load_double;

extern void *codegen_mem_store_byte;
extern void *codegen_mem_store_word;
extern void *codegen_mem_store_long;
extern void *codegen_mem_store_quad;
extern void *codegen_mem_store_single;
extern void *codegen_mem_store_double;

extern void *codegen_fp_round;

extern void *codegen_gpf_rout;
extern void *codegen_exit_rout;
