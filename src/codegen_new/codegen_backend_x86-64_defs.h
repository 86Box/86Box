/*RBP = cpu_state + 128
  R12 = ram (if block->flags & CODEBLOCK_NO_IMMEDIATES)*/
#define REG_AX 0
#define REG_CX 1
#define REG_DX 2
#define REG_BX 3
#define REG_SP 4
#define REG_BP 5
#define REG_SI 6
#define REG_DI 7

#define REG_EAX 0
#define REG_ECX 1
#define REG_EDX 2
#define REG_EBX 3
#define REG_ESP 4
#define REG_EBP 5
#define REG_ESI 6
#define REG_EDI 7

#define REG_RAX 0
#define REG_RCX 1
#define REG_RDX 2
#define REG_RBX 3
#define REG_RSP 4
#define REG_RBP 5
#define REG_RSI 6
#define REG_RDI 7
#define REG_R8  8
#define REG_R9  9
#define REG_R10 10
#define REG_R11 11
#define REG_R12 12
#define REG_R13 13
#define REG_R14 14
#define REG_R15 15

#define REG_XMM0 0
#define REG_XMM1 1
#define REG_XMM2 2
#define REG_XMM3 3
#define REG_XMM4 4
#define REG_XMM5 5
#define REG_XMM6 6
#define REG_XMM7 7

#define REG_XMM_TEMP REG_XMM0

#define CODEGEN_HOST_REGS 3
#define CODEGEN_HOST_FP_REGS 7

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

extern void *codegen_gpf_rout;
extern void *codegen_exit_rout;
