#ifndef _CODEGEN_BACKEND_X86_DEFS_H_
#define _CODEGEN_BACKEND_X86_DEFS_H_

#define REG_EAX 0
#define REG_ECX 1
#define REG_EDX 2
#define REG_EBX 3
#define REG_ESP 4
#define REG_EBP 5
#define REG_ESI 6
#define REG_EDI 7

#define REG_XMM0 0
#define REG_XMM1 1
#define REG_XMM2 2
#define REG_XMM3 3
#define REG_XMM4 4
#define REG_XMM5 5
#define REG_XMM6 6
#define REG_XMM7 7

#define REG_XMM_TEMP  REG_XMM7
#define REG_XMM_TEMP2 REG_XMM6

#define CODEGEN_HOST_REGS 3
#define CODEGEN_HOST_FP_REGS 6

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

#define STACK_ARG0 (0)
#define STACK_ARG1 (4)
#define STACK_ARG2 (8)
#define STACK_ARG3 (12)

#endif
