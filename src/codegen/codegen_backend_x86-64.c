#if defined __amd64__ || defined _M_X64

#    include <stddef.h>
#    include <stdlib.h>
#    include <stdint.h>
#    include <86box/86box.h>
#    include "cpu.h"
#    include <86box/mem.h>
#    include <86box/plat.h>

#    include "codegen.h"
#    include "codegen_allocator.h"
#    include "codegen_backend.h"
#    include "codegen_backend_x86-64_defs.h"
#    include "codegen_backend_x86-64_ops.h"
#    include "codegen_backend_x86-64_ops_helpers.h"
#    include "codegen_backend_x86-64_ops_sse.h"
#    include "codegen_ir_defs.h"
#    include "codegen_ops_rep.h"
#    include "codegen_reg.h"
#    include "x86.h"
#    include "x86seg_common.h"
#    include "x86seg.h"

#    if defined(__linux__) || defined(__APPLE__)
#        include <sys/mman.h>
#        include <unistd.h>
#    endif
#    if defined WIN32 || defined _WIN32 || defined _WIN32
#        include <windows.h>
#    endif
#    include <string.h>

void *codegen_mem_load_byte;
void *codegen_mem_load_word;
void *codegen_mem_load_long;
void *codegen_mem_load_quad;
void *codegen_mem_load_single;
void *codegen_mem_load_double;

void *codegen_mem_store_byte;
void *codegen_mem_store_word;
void *codegen_mem_store_long;
void *codegen_mem_load_word_ea;
void *codegen_mem_load_long_ea;

void *codegen_mem_store_quad;
void *codegen_mem_store_single;
void *codegen_mem_store_double;

void *codegen_gpf_rout;
void *codegen_ss_rout;
void *codegen_exit_rout;
void *codegen_rep_rout[2][3][2];

host_reg_def_t codegen_host_reg_list[CODEGEN_HOST_REGS] = {
  /*R13-R15 are callee-saved and already saved by the block prologue. They
  come first so that registers never accessed by byte settle there, leaving
  the three with byte forms to the rest*/
    {REG_R13,  HOST_REG_FLAG_NO_BYTE},
    { REG_R14, HOST_REG_FLAG_NO_BYTE},
    { REG_R15, HOST_REG_FLAG_NO_BYTE},
  /*Note: while EAX and EDX are normally volatile registers under x86
  calling conventions, the recompiler will explicitly save and restore
  them across funcion calls*/
    { REG_EAX, 0},
    { REG_EBX, 0},
    { REG_EDX, 0}
};

/* The Windows x64 frame: the 32 bytes of home space a called function may
   store its arguments in, the block temporaries above them (codegen_reg.c,
   ending at 0x50), then XMM6 and XMM7 as the caller left them. 0x78 keeps
   RSP 16-byte aligned after the eight pushes and the return address. */
#define CODEGEN_WIN64_FRAME 0x78
#define CODEGEN_XMM6_SAVE   0x50
#define CODEGEN_XMM7_SAVE   0x60

host_reg_def_t codegen_host_fp_reg_list[CODEGEN_HOST_FP_REGS] = {
#    if _WIN64
  /*Windows x86-64 calling convention preserves XMM6-XMM15*/
    {REG_XMM6,  0                     },
    { REG_XMM7, 0                     },
#    else
    /*System V AMD64 calling convention does not preserve any XMM registers*/
    { REG_XMM6, HOST_REG_FLAG_VOLATILE },
    { REG_XMM7, HOST_REG_FLAG_VOLATILE },
#    endif
    { REG_XMM1, HOST_REG_FLAG_VOLATILE},
    { REG_XMM2, HOST_REG_FLAG_VOLATILE},
    { REG_XMM3, HOST_REG_FLAG_VOLATILE},
    { REG_XMM4, HOST_REG_FLAG_VOLATILE},
    { REG_XMM5, HOST_REG_FLAG_VOLATILE}
};

/*ea_rule: take the direct load whenever the address is at most 0xffc into
  its page, aligned or not, as the interpreter's eal_r path does; readmemwl
  and readmemll charge timing_misaligned only on the slow path.*/
static void
build_load_routine(codeblock_t *block, int size, int is_float, int ea_rule)
{
    uint8_t *branch_offset;
    uint8_t *misaligned_offset = NULL;

    /*In - ESI = address
      Out - ECX = data, ESI = abrt*/
    /*MOV ECX, ESI
      SHR ESI, 12
      MOV RSI, [readlookup2+ESI*4]
      CMP ESI, -1
      JNZ +
      MOVZX ECX, B[RSI+RCX]
      XOR ESI,ESI
      RET
    * PUSH EAX
      PUSH EDX
      PUSH ECX
      CALL readmembl
      POP ECX
      POP EDX
      POP EAX
      MOVZX ECX, AL
      RET
    */
    host_x86_MOV32_REG_REG(block, REG_ECX, REG_ESI);
    host_x86_SHR32_IMM(block, REG_ESI, 12);
    host_x86_MOV64_REG_IMM(block, REG_RDI, (uint64_t) (uintptr_t) readlookup2);
    host_x86_MOV64_REG_BASE_INDEX_SHIFT(block, REG_RSI, REG_RDI, REG_RSI, 3);
    if (ea_rule) {
        host_x86_MOV32_REG_REG(block, REG_EDI, REG_ECX);
        host_x86_AND32_REG_IMM(block, REG_EDI, 0xfff);
        host_x86_CMP32_REG_IMM(block, REG_EDI, 0xffc);
        misaligned_offset = host_x86_JNBE_short(block);
    } else if (size != 1) {
        host_x86_TEST32_REG_IMM(block, REG_ECX, size - 1);
        misaligned_offset = host_x86_JNZ_short(block);
    }
    host_x86_CMP64_REG_IMM(block, REG_RSI, (uint32_t) -1);
    branch_offset = host_x86_JZ_short(block);
    if (size == 1 && !is_float)
        host_x86_MOVZX_BASE_INDEX_32_8(block, REG_ECX, REG_RSI, REG_RCX);
    else if (size == 2 && !is_float)
        host_x86_MOVZX_BASE_INDEX_32_16(block, REG_ECX, REG_RSI, REG_RCX);
    else if (size == 4 && !is_float)
        host_x86_MOV32_REG_BASE_INDEX(block, REG_ECX, REG_RSI, REG_RCX);
    else if (size == 4 && is_float)
        host_x86_CVTSS2SD_XREG_BASE_INDEX(block, REG_XMM_TEMP, REG_RSI, REG_RCX);
    else if (size == 8)
        host_x86_MOVQ_XREG_BASE_INDEX(block, REG_XMM_TEMP, REG_RSI, REG_RCX);
    else
        fatal("build_load_routine: size=%i\n", size);
    host_x86_XOR32_REG_REG(block, REG_ESI, REG_ESI);
    host_x86_RET(block);

    *branch_offset = (uint8_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) branch_offset) - 1;
    if (ea_rule || size != 1)
        *misaligned_offset = (uint8_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) misaligned_offset) - 1;
    host_x86_PUSH(block, REG_RAX);
    host_x86_PUSH(block, REG_RDX);
#    if _WIN64
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x28);
    // host_x86_MOV32_REG_REG(block, REG_ECX, uop->imm_data);
#    else
    /* Align RSP to 16: entry RSP%16=8 (after CALL from JIT block), two PUSHes
       leave it at 8; subtract 8 more to satisfy the SysV ABI before calling C. */
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x8);
    host_x86_MOV32_REG_REG(block, REG_EDI, REG_ECX);
#    endif
    if (size == 1 && !is_float) {
        host_x86_CALL(block, (void *) readmembl);
        host_x86_MOVZX_REG_32_8(block, REG_ECX, REG_EAX);
    } else if (size == 2 && !is_float) {
        host_x86_CALL(block, (void *) readmemwl);
        host_x86_MOVZX_REG_32_16(block, REG_ECX, REG_EAX);
    } else if (size == 4 && !is_float) {
        host_x86_CALL(block, (void *) readmemll);
        host_x86_MOV32_REG_REG(block, REG_ECX, REG_EAX);
    } else if (size == 4 && is_float) {
        host_x86_CALL(block, (void *) readmemll);
        host_x86_MOVD_XREG_REG(block, REG_XMM_TEMP, REG_EAX);
        host_x86_CVTSS2SD_XREG_XREG(block, REG_XMM_TEMP, REG_XMM_TEMP);
    } else if (size == 8) {
        host_x86_CALL(block, (void *) readmemql);
        host_x86_MOVQ_XREG_REG(block, REG_XMM_TEMP, REG_RAX);
    }
#    if _WIN64
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x28);
#    else
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x8);
#    endif
    host_x86_POP(block, REG_RDX);
    host_x86_POP(block, REG_RAX);
    host_x86_MOVZX_REG_ABS_32_8(block, REG_ESI, &cpu_state.abrt);
    host_x86_RET(block);
}

static void
build_store_routine(codeblock_t *block, int size, int is_float)
{
    uint8_t *branch_offset;
    uint8_t *misaligned_offset = NULL;

    /*In - ECX = data, ESI = address
      Out - ESI = abrt
      Corrupts EDI*/
    /*MOV EDI, ESI
      SHR ESI, 12
      MOV ESI, [writelookup2+ESI*4]
      CMP ESI, -1
      JNZ +
      MOV [RSI+RDI], ECX
      XOR ESI,ESI
      RET
    * PUSH EAX
      PUSH EDX
      PUSH ECX
      CALL writemembl
      POP ECX
      POP EDX
      POP EAX
      MOVZX ECX, AL
      RET
    */
    host_x86_MOV32_REG_REG(block, REG_EDI, REG_ESI);
    host_x86_SHR32_IMM(block, REG_ESI, 12);
    host_x86_MOV64_REG_IMM(block, REG_R8, (uint64_t) (uintptr_t) writelookup2);
    host_x86_MOV64_REG_BASE_INDEX_SHIFT(block, REG_RSI, REG_R8, REG_RSI, 3);
    if (size != 1) {
        host_x86_TEST32_REG_IMM(block, REG_EDI, size - 1);
        misaligned_offset = host_x86_JNZ_short(block);
    }
    host_x86_CMP64_REG_IMM(block, REG_RSI, (uint32_t) -1);
    branch_offset = host_x86_JZ_short(block);
    if (size == 1 && !is_float)
        host_x86_MOV8_BASE_INDEX_REG(block, REG_RSI, REG_RDI, REG_ECX);
    else if (size == 2 && !is_float)
        host_x86_MOV16_BASE_INDEX_REG(block, REG_RSI, REG_RDI, REG_ECX);
    else if (size == 4 && !is_float)
        host_x86_MOV32_BASE_INDEX_REG(block, REG_RSI, REG_RDI, REG_ECX);
    else if (size == 4 && is_float)
        host_x86_MOVD_BASE_INDEX_XREG(block, REG_RSI, REG_RDI, REG_XMM_TEMP);
    else if (size == 8)
        host_x86_MOVQ_BASE_INDEX_XREG(block, REG_RSI, REG_RDI, REG_XMM_TEMP);
    else
        fatal("build_store_routine: size=%i\n", size);
    host_x86_XOR32_REG_REG(block, REG_ESI, REG_ESI);
    host_x86_RET(block);

    *branch_offset = (uint8_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) branch_offset) - 1;
    if (size != 1)
        *misaligned_offset = (uint8_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) misaligned_offset) - 1;
    host_x86_PUSH(block, REG_RAX);
    host_x86_PUSH(block, REG_RDX);
#    if _WIN64
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x28);
    if (size == 4 && is_float)
        host_x86_MOVD_REG_XREG(block, REG_EDX, REG_XMM_TEMP); // data
    else if (size == 8)
        host_x86_MOVQ_REG_XREG(block, REG_RDX, REG_XMM_TEMP); // data
    else
        host_x86_MOV32_REG_REG(block, REG_EDX, REG_ECX); // data
    host_x86_MOV32_REG_REG(block, REG_ECX, REG_EDI);     // address
#    else
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x8);
    // host_x86_MOV32_REG_REG(block, REG_EDI, REG_ECX);  //address
    if (size == 4 && is_float)
        host_x86_MOVD_REG_XREG(block, REG_ESI, REG_XMM_TEMP); // data
    else if (size == 8)
        host_x86_MOVQ_REG_XREG(block, REG_RSI, REG_XMM_TEMP); // data
    else
        host_x86_MOV32_REG_REG(block, REG_ESI, REG_ECX); // data
#    endif
    if (size == 1)
        host_x86_CALL(block, (void *) writemembl);
    else if (size == 2)
        host_x86_CALL(block, (void *) writememwl);
    else if (size == 4)
        host_x86_CALL(block, (void *) writememll);
    else if (size == 8)
        host_x86_CALL(block, (void *) writememql);
#    if _WIN64
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x28);
#    else
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x8);
#    endif
    host_x86_POP(block, REG_RDX);
    host_x86_POP(block, REG_RAX);
    host_x86_MOVZX_REG_ABS_32_8(block, REG_ESI, &cpu_state.abrt);
    host_x86_RET(block);
}

/*The routines span more than one code chunk. Each routine starts in a chunk
  with room for all of it, since its short branches cannot cross the jump
  that links two chunks, and its address is taken from the chunk being
  written, not from the first one.*/
static void *
routine_start(codeblock_t *block)
{
    codegen_alloc(block, 0x80);
    return &block_write_data[block_pos];
}

static void
build_loadstore_routines(codeblock_t *block)
{

    codegen_mem_load_byte = routine_start(block);
    build_load_routine(block, 1, 0, 0);
    codegen_mem_load_word = routine_start(block);
    build_load_routine(block, 2, 0, 0);
    codegen_mem_load_long = routine_start(block);
    build_load_routine(block, 4, 0, 0);
    codegen_mem_load_quad = routine_start(block);
    build_load_routine(block, 8, 0, 0);
    codegen_mem_load_single = routine_start(block);
    build_load_routine(block, 4, 1, 0);
    codegen_mem_load_double = routine_start(block);
    build_load_routine(block, 8, 1, 0);
    codegen_mem_load_word_ea = routine_start(block);
    build_load_routine(block, 2, 0, 1);
    codegen_mem_load_long_ea = routine_start(block);
    build_load_routine(block, 4, 0, 1);
    codegen_mem_store_byte = routine_start(block);
    build_store_routine(block, 1, 0);
    codegen_mem_store_word = routine_start(block);
    build_store_routine(block, 2, 0);
    codegen_mem_store_long = routine_start(block);
    build_store_routine(block, 4, 0);
    codegen_mem_store_quad = routine_start(block);
    build_store_routine(block, 8, 0);
    codegen_mem_store_single = routine_start(block);
    build_store_routine(block, 4, 1);
    codegen_mem_store_double = routine_start(block);
    build_store_routine(block, 8, 1);
}

/*The REP routines need most of the host registers, R8-R15 included, which
  the general emitters do not take, so they use these encoders. Every memory
  operand is [base + disp32], and every branch has a 32-bit displacement, so
  a routine may run across code chunks.*/
static void
rx_bytes(codeblock_t *block, const uint8_t *b, int n)
{
    codegen_alloc_bytes(block, n);
    for (int c = 0; c < n; c++)
        codegen_addbyte(block, b[c]);
}

/*Operand-size prefix, REX and opcode for an instruction with ModRM reg field
  reg and rm register or base rm. op is one byte, or 0x0fxx for two.*/
static int
rx_head(uint8_t *b, int p66, int w, int reg, int rm, uint16_t op)
{
    int     n   = 0;
    uint8_t rex = 0x40 | (w ? 8 : 0) | ((reg & 8) ? 4 : 0) | ((rm & 8) ? 1 : 0);

    if (p66)
        b[n++] = 0x66;
    if (rex != 0x40)
        b[n++] = rex;
    if (op > 0xff)
        b[n++] = op >> 8;
    b[n++] = op & 0xff;
    return n;
}

/*op reg, rm with a register rm*/
static void
rx_rr(codeblock_t *block, int p66, int w, uint16_t op, int reg, int rm)
{
    uint8_t b[8];
    int     n = rx_head(b, p66, w, reg, rm, op);

    b[n++] = 0xc0 | ((reg & 7) << 3) | (rm & 7);
    rx_bytes(block, b, n);
}

/*op reg, [base + disp]*/
static void
rx_rm(codeblock_t *block, int p66, int w, uint16_t op, int reg, int base, int32_t disp)
{
    uint8_t b[16];
    int     n = rx_head(b, p66, w, reg, base, op);

    b[n++] = 0x80 | ((reg & 7) << 3) | (base & 7);
    if ((base & 7) == 4)
        b[n++] = 0x24; /*SIB with no index, for RSP and R12*/
    memcpy(&b[n], &disp, 4);
    rx_bytes(block, b, n + 4);
}

/*Group-1 ALU op on a register with a 32-bit immediate: ext 0 ADD, 1 OR,
  4 AND, 5 SUB, 7 CMP*/
static void
rx_alu_ri(codeblock_t *block, int w, int ext, int rm, uint32_t imm)
{
    uint8_t b[12];
    int     n = rx_head(b, 0, w, 0, rm, 0x81);

    b[n++] = 0xc0 | (ext << 3) | (rm & 7);
    memcpy(&b[n], &imm, 4);
    rx_bytes(block, b, n + 4);
}

/*Group-1 ALU op on a 32-bit memory operand with a 32-bit immediate*/
static void
rx_alu_mi(codeblock_t *block, int ext, int base, int32_t disp, uint32_t imm)
{
    uint8_t b[16];
    int     n = rx_head(b, 0, 0, 0, base, 0x81);

    b[n++] = 0x80 | (ext << 3) | (base & 7);
    if ((base & 7) == 4)
        b[n++] = 0x24;
    memcpy(&b[n], &disp, 4);
    memcpy(&b[n + 4], &imm, 4);
    rx_bytes(block, b, n + 8);
}

/*TEST byte [base + disp], imm*/
static void
rx_testb_mi(codeblock_t *block, int base, int32_t disp, uint8_t imm)
{
    uint8_t b[16];
    int     n = rx_head(b, 0, 0, 0, base, 0xf6);

    b[n++] = 0x80 | (base & 7);
    if ((base & 7) == 4)
        b[n++] = 0x24;
    memcpy(&b[n], &disp, 4);
    b[n + 4] = imm;
    rx_bytes(block, b, n + 5);
}

/*TEST reg32, imm32*/
static void
rx_test_ri(codeblock_t *block, int rm, uint32_t imm)
{
    uint8_t b[12];
    int     n = rx_head(b, 0, 0, 0, rm, 0xf7);

    b[n++] = 0xc0 | (rm & 7);
    memcpy(&b[n], &imm, 4);
    rx_bytes(block, b, n + 4);
}

/*Shift reg32 by an immediate: ext 4 SHL, 5 SHR*/
static void
rx_shift_ri(codeblock_t *block, int ext, int rm, uint8_t imm)
{
    uint8_t b[8];
    int     n = rx_head(b, 0, 0, 0, rm, 0xc1);

    b[n++] = 0xc0 | (ext << 3) | (rm & 7);
    b[n++] = imm;
    rx_bytes(block, b, n);
}

/*MOV reg32, imm32*/
static void
rx_mov_ri(codeblock_t *block, int rm, uint32_t imm)
{
    uint8_t b[8];
    int     n = 0;

    if (rm & 8)
        b[n++] = 0x41;
    b[n++] = 0xb8 | (rm & 7);
    memcpy(&b[n], &imm, 4);
    rx_bytes(block, b, n + 4);
}

/*MOV reg64, imm64*/
static void
rx_mov_ri64(codeblock_t *block, int rm, uint64_t imm)
{
    uint8_t b[12];

    b[0] = 0x48 | ((rm & 8) ? 1 : 0);
    b[1] = 0xb8 | (rm & 7);
    memcpy(&b[2], &imm, 8);
    rx_bytes(block, b, 10);
}

static void
rx_push(codeblock_t *block, int reg)
{
    uint8_t b[2] = { 0x41, 0x50 | (reg & 7) };

    if (reg & 8)
        rx_bytes(block, b, 2);
    else
        rx_bytes(block, &b[1], 1);
}
static void
rx_pop(codeblock_t *block, int reg)
{
    uint8_t b[2] = { 0x41, 0x58 | (reg & 7) };

    if (reg & 8)
        rx_bytes(block, b, 2);
    else
        rx_bytes(block, &b[1], 1);
}

#    define RX_CC_B  0x2
#    define RX_CC_Z  0x4
#    define RX_CC_NZ 0x5
#    define RX_CC_A  0x7
#    define RX_CC_S  0x8
#    define RX_CC_NS 0x9
#    define RX_CC_L  0xc

/*Jcc rel32 to a target set later with rx_patch*/
static int32_t *
rx_jcc(codeblock_t *block, int cc)
{
    uint8_t b[6] = { 0x0f, 0x80 | cc, 0, 0, 0, 0 };

    rx_bytes(block, b, 6);
    return (int32_t *) &block_write_data[block_pos - 4];
}
/*JMP rel32 to a target set later with rx_patch*/
static int32_t *
rx_jmp(codeblock_t *block)
{
    uint8_t b[5] = { 0xe9, 0, 0, 0, 0 };

    rx_bytes(block, b, 5);
    return (int32_t *) &block_write_data[block_pos - 4];
}
static void
rx_patch(int32_t *p, void *dest)
{
    *p = (int32_t) ((intptr_t) dest - ((intptr_t) p + 4));
}
static void *
rx_here(void)
{
    return &block_write_data[block_pos];
}

#    define RX_CNT     REG_R8
#    define RX_SRC     REG_R9
#    define RX_DST     REG_R10
#    define RX_CYCLES  REG_R11
#    define RX_END     REG_R12
#    define RX_STEP    REG_R13
#    define RX_ELEMCYC REG_R14
#    define RX_SEG     REG_R15
#    define RX_SLOW    REG_RBX

#    define RX_MAX_PATCH 24

/*Displacement of a cpu_state field from RBP, which points 128 bytes in*/
static int32_t
rx_state(void *p)
{
    return (int32_t) ((uintptr_t) p - (uintptr_t) &cpu_state) - 128;
}

static void
rx_load_regs(codeblock_t *block, int is_stos, int a32)
{
    uint16_t op = a32 ? 0x8b : 0x0fb7; /*MOV or MOVZX from a word*/

    rx_rm(block, 0, 0, op, RX_CNT, REG_RBP, rx_state(a32 ? (void *) &ECX : (void *) &CX));
    if (!is_stos)
        rx_rm(block, 0, 0, op, RX_SRC, REG_RBP, rx_state(a32 ? (void *) &ESI : (void *) &SI));
    rx_rm(block, 0, 0, op, RX_DST, REG_RBP, rx_state(a32 ? (void *) &EDI : (void *) &DI));
    rx_rm(block, 0, 0, 0x8b, RX_CYCLES, REG_RBP, rx_state(&cpu_state._cycles));
}

static void
rx_writeback(codeblock_t *block, int is_stos, int a32)
{
    int p66 = !a32;

    rx_rm(block, p66, 0, 0x89, RX_CNT, REG_RBP, rx_state(a32 ? (void *) &ECX : (void *) &CX));
    if (!is_stos)
        rx_rm(block, p66, 0, 0x89, RX_SRC, REG_RBP, rx_state(a32 ? (void *) &ESI : (void *) &SI));
    rx_rm(block, p66, 0, 0x89, RX_DST, REG_RBP, rx_state(a32 ? (void *) &EDI : (void *) &DI));
    rx_rm(block, 0, 0, 0x89, RX_CYCLES, REG_RBP, rx_state(&cpu_state._cycles));
}

/*EAX = min(EAX, ECX), unsigned*/
static void
rx_min(codeblock_t *block)
{
    rx_rr(block, 0, 0, 0x39, REG_RCX, REG_RAX);   /*CMP EAX, ECX*/
    rx_rr(block, 0, 0, 0x0f47, REG_RAX, REG_RCX); /*CMOVA EAX, ECX*/
}

/*Sets RX_SLOW if the segment at [base + disp] would fail the interpreter's
  null selector or not-present check, using EDX = (msw & 1) && !(eflags & VM)*/
static void
rx_seg_check(codeblock_t *block, int base, int32_t disp)
{
    int32_t *skip;

    rx_alu_mi(block, 7, base, disp + (int32_t) offsetof(x86seg, base), 0xffffffff);
    skip = rx_jcc(block, RX_CC_NZ);
    rx_alu_ri(block, 0, 1, RX_SLOW, 1);
    rx_patch(skip, rx_here());
    rx_testb_mi(block, base, disp + (int32_t) offsetof(x86seg, access), 0x80);
    skip = rx_jcc(block, RX_CC_NZ);
    rx_rr(block, 0, 0, 0x09, REG_RDX, RX_SLOW); /*OR EBX, EDX*/
    rx_patch(skip, rx_here());
}

/*Forward run: clamp EAX to the elements left in the page at lin_reg*/
static void
rx_page_room_fwd(codeblock_t *block, int lin_reg, int shift)
{
    rx_rr(block, 0, 0, 0x89, lin_reg, REG_RCX);
    rx_rr(block, 0, 0, 0xf7, 2, REG_RCX); /*NOT ECX*/
    rx_alu_ri(block, 0, 4, REG_RCX, 0xfff);
    rx_alu_ri(block, 0, 0, REG_RCX, 1);
    if (shift)
        rx_shift_ri(block, 5, REG_RCX, shift);
    rx_min(block);
}

/*Backward run: clamp EAX to the elements down to the start of the page at
  lin_reg*/
static void
rx_page_room_bwd(codeblock_t *block, int lin_reg, int shift)
{
    rx_rr(block, 0, 0, 0x89, lin_reg, REG_RCX);
    rx_alu_ri(block, 0, 4, REG_RCX, 0xfff);
    if (shift)
        rx_shift_ri(block, 5, REG_RCX, shift);
    rx_alu_ri(block, 0, 0, REG_RCX, 1);
    rx_min(block);
}

/*Forward run: clamp EAX to the elements before the offset in off_reg wraps.
  Zero if the first element already straddles the wrap.*/
static void
rx_offset_room_fwd(codeblock_t *block, int off_reg, int shift, int a32)
{
    if (a32) {
        int32_t *nonzero;

        rx_rr(block, 0, 0, 0x89, off_reg, REG_RCX);
        rx_rr(block, 0, 0, 0xf7, 3, REG_RCX); /*NEG ECX*/
        if (shift)
            rx_shift_ri(block, 5, REG_RCX, shift);
        /*Offset 0 has the whole 4 GB ahead of it*/
        rx_rr(block, 0, 0, 0x85, off_reg, off_reg);
        nonzero = rx_jcc(block, RX_CC_NZ);
        rx_rr(block, 0, 0, 0x89, RX_CNT, REG_RCX);
        rx_patch(nonzero, rx_here());
    } else {
        rx_mov_ri(block, REG_RCX, 0x10000);
        rx_rr(block, 0, 0, 0x29, off_reg, REG_RCX); /*SUB ECX, off_reg*/
        if (shift)
            rx_shift_ri(block, 5, REG_RCX, shift);
    }
    rx_min(block);
}

/*Backward run: clamp EAX to the elements down to offset 0, and branch to the
  slow path if the first element straddles the top of the offset range*/
static void
rx_offset_room_bwd(codeblock_t *block, int off_reg, int shift, int a32, int32_t **slow, int *nr_slow)
{
    int size = 1 << shift;

    rx_rr(block, 0, 0, 0x89, off_reg, REG_RCX);
    if (shift)
        rx_shift_ri(block, 5, REG_RCX, shift);
    rx_alu_ri(block, 0, 0, REG_RCX, 1);
    rx_min(block);
    if (size > 1) {
        rx_rr(block, 0, 0, 0x89, off_reg, REG_RCX);
        rx_alu_ri(block, 0, 0, REG_RCX, size - 1);
        if (a32)
            slow[(*nr_slow)++] = rx_jcc(block, RX_CC_B); /*carry out of bit 31*/
        else {
            rx_shift_ri(block, 5, REG_RCX, 16);
            slow[(*nr_slow)++] = rx_jcc(block, RX_CC_NZ);
        }
    }
}

/*Branches to the slow path unless every element of the run lies inside the
  segment's limits. EDX = run length in bytes.*/
static void
rx_limit_check(codeblock_t *block, int off_reg, int base, int32_t disp, int size, int32_t **slow, int *nr_slow)
{
    int32_t  limit_low  = disp + (int32_t) offsetof(x86seg, limit_low);
    int32_t  limit_high = disp + (int32_t) offsetof(x86seg, limit_high);
    int32_t *bwd;
    int32_t *done;

    rx_rr(block, 0, 0, 0x85, RX_STEP, RX_STEP);
    bwd = rx_jcc(block, RX_CC_S);
    rx_rm(block, 0, 0, 0x3b, off_reg, base, limit_low); /*CMP off_reg, limit_low*/
    slow[(*nr_slow)++] = rx_jcc(block, RX_CC_B);
    rx_rr(block, 0, 0, 0x89, off_reg, REG_RCX);
    rx_rr(block, 0, 0, 0x01, REG_RDX, REG_RCX);
    rx_alu_ri(block, 0, 5, REG_RCX, 1);
    rx_rm(block, 0, 0, 0x3b, REG_RCX, base, limit_high);
    slow[(*nr_slow)++] = rx_jcc(block, RX_CC_A);
    done = rx_jmp(block);

    rx_patch(bwd, rx_here());
    rx_rr(block, 0, 0, 0x89, off_reg, REG_RCX);
    rx_rr(block, 0, 0, 0x29, REG_RDX, REG_RCX);
    rx_alu_ri(block, 0, 0, REG_RCX, size);
    rx_rm(block, 0, 0, 0x3b, REG_RCX, base, limit_low);
    slow[(*nr_slow)++] = rx_jcc(block, RX_CC_B);
    rx_rr(block, 0, 0, 0x89, off_reg, REG_RCX);
    rx_alu_ri(block, 0, 0, REG_RCX, size - 1);
    rx_rm(block, 0, 0, 0x3b, REG_RCX, base, limit_high);
    slow[(*nr_slow)++] = rx_jcc(block, RX_CC_A);

    rx_patch(done, rx_here());
}

/*off_reg += delta_reg, kept to 16 bits with 16-bit addressing*/
static void
rx_step_offset(codeblock_t *block, int off_reg, int delta_reg, int a32)
{
    rx_rr(block, 0, 0, 0x01, delta_reg, off_reg);
    if (!a32)
        rx_rr(block, 0, 0, 0x0fb7, off_reg, off_reg); /*MOVZX off_reg, off_reg16*/
}

/*host = lookup[lin >> 12] + lin for the linear address in lin_reg (RSI or
  RDI), branching to the slow path when the entry is -1. Uses ECX and RDX.*/
static void
rx_lookup(codeblock_t *block, int lin_reg, uintptr_t *lookup, int32_t **slow, int *nr_slow)
{
    static const uint8_t load[4] = { 0x48, 0x8b, 0x14, 0xca }; /*MOV RDX, [RDX + RCX*8]*/

    rx_rr(block, 0, 0, 0x89, lin_reg, REG_RCX);
    rx_shift_ri(block, 5, REG_RCX, 12);
    rx_mov_ri64(block, REG_RDX, (uint64_t) (uintptr_t) lookup);
    rx_bytes(block, load, 4);
    rx_alu_ri(block, 1, 7, REG_RDX, 0xffffffff);
    slow[(*nr_slow)++] = rx_jcc(block, RX_CC_Z);
    rx_rr(block, 0, 1, 0x01, REG_RDX, lin_reg); /*ADD lin_reg, RDX*/
}

static void
rx_epilogue(codeblock_t *block)
{
    static const uint8_t ret = 0xc3;

    rx_alu_ri(block, 1, 0, REG_RSP, 0x20);
    rx_pop(block, REG_R15);
    rx_pop(block, REG_R14);
    rx_pop(block, REG_R13);
    rx_pop(block, REG_R12);
    rx_pop(block, REG_RBX);
    rx_bytes(block, &ret, 1);
}

/*REP MOVS and REP STOS.
  In  - ECX (Windows) or EDI (System V) = source segment offset in cpu_state
        (bits 0-11), cycle limit (bits 12-23), cycles per element (bits 24-31)
  Out - EAX = 0 to carry on with the block, non-zero to leave it with
        cpu_state.pc set back to the instruction
  Preserves RBX, RBP and R12-R15; the caller saves RAX and RDX.

  Does what the interpreter's REP handlers in x86_ops_rep_dyn.h do: the same
  cycles per element, and the block ends at the same element when the string
  outlasts the cycle limit or the trap flag is set. Elements are copied in
  runs that stay inside one source page and one destination page, both with
  direct lookups, inside both segment limits and inside the cycle budget, so a
  run does exactly what the interpreter's fast path does for each element. A
  run is copied with the host's REP MOVS or REP STOS, whose result is that of
  copying one element at a time, overlaps included. Any other element goes
  through elem_func, which is the interpreter's loop body for one element.*/
static void
build_rep_routine(codeblock_t *block, int is_stos, int shift, int a32, int (*elem_func)(x86seg *seg))
{
    int      size      = 1 << shift;
    int32_t  es_disp   = rx_state(&cpu_state.seg_es);
    int32_t *slow[RX_MAX_PATCH];
    int      nr_slow   = 0;
    int32_t *to_done;
    int32_t *to_break[2];
    int32_t *to_abort;
    int32_t *to_set_pc;
    int32_t *branch;
    int32_t *branch2;
    void    *loop;
    void    *done_ret;

    /*RSP is 8 off 16-byte alignment on entry; five pushes and the 32-byte
      home area align it for the call to elem_func*/
    rx_push(block, REG_RBX);
    rx_push(block, REG_R12);
    rx_push(block, REG_R13);
    rx_push(block, REG_R14);
    rx_push(block, REG_R15);
    rx_alu_ri(block, 1, 5, REG_RSP, 0x20);

    /*Unpack the parameters, ECX = cycle limit*/
#    if _WIN64
    rx_rr(block, 0, 0, 0x89, REG_RCX, REG_RAX);
#    else
    rx_rr(block, 0, 0, 0x89, REG_RDI, REG_RAX);
#    endif
    rx_rr(block, 0, 0, 0x89, REG_RAX, REG_RCX);
    rx_shift_ri(block, 5, REG_RCX, 12);
    rx_alu_ri(block, 0, 4, REG_RCX, 0xfff);
    rx_rr(block, 0, 0, 0x89, REG_RAX, RX_ELEMCYC);
    rx_shift_ri(block, 5, RX_ELEMCYC, 24);
    if (!is_stos) {
        rx_alu_ri(block, 0, 4, REG_RAX, 0xfff);
        rx_rr(block, 0, 1, 0x89, REG_RBP, RX_SEG);
        rx_rr(block, 0, 1, 0x01, REG_RAX, RX_SEG);
        rx_alu_ri(block, 1, 5, RX_SEG, 128);
    }

    /*Guest registers*/
    rx_load_regs(block, is_stos, a32);

    /*Cycle count at which the loop stops: one element only with the trap flag set*/
    rx_rr(block, 0, 0, 0x89, RX_CYCLES, RX_END);
    rx_rr(block, 0, 0, 0x29, REG_RCX, RX_END);
    rx_mov_ri64(block, REG_RAX, (uint64_t) (uintptr_t) &trap);
    rx_rm(block, 0, 0, 0x8b, REG_RAX, REG_RAX, 0);
    rx_rr(block, 0, 0, 0x85, REG_RAX, REG_RAX);
    branch = rx_jcc(block, RX_CC_Z);
    rx_rr(block, 0, 0, 0x89, RX_CYCLES, RX_END);
    rx_alu_ri(block, 0, 0, RX_END, 1);
    rx_patch(branch, rx_here());

    /*Direction*/
    rx_mov_ri(block, RX_STEP, size);
    rx_rm(block, 0, 0, 0x0fb7, REG_RAX, REG_RBP, rx_state(&cpu_state.flags));
    rx_test_ri(block, REG_RAX, D_FLAG);
    branch = rx_jcc(block, RX_CC_Z);
    rx_mov_ri(block, RX_STEP, -size);
    rx_patch(branch, rx_here());

    /*Conditions that send every element down the slow path: debug
      breakpoints, which the interpreter's translate check looks at, and a
      segment that would fault*/
    rx_mov_ri64(block, REG_RAX, (uint64_t) (uintptr_t) &dr[7]);
    rx_rm(block, 0, 0, 0x8b, RX_SLOW, REG_RAX, 0);
    rx_alu_ri(block, 0, 4, RX_SLOW, 0xff);
    rx_rm(block, 0, 0, 0x0fb7, REG_RDX, REG_RBP, rx_state(&cpu_state.CR0.w));
    rx_alu_ri(block, 0, 4, REG_RDX, 1);
    rx_rm(block, 0, 0, 0x0fb7, REG_RAX, REG_RBP, rx_state(&cpu_state.eflags));
    rx_test_ri(block, REG_RAX, VM_FLAG);
    branch = rx_jcc(block, RX_CC_Z);
    rx_rr(block, 0, 0, 0x31, REG_RDX, REG_RDX);
    rx_patch(branch, rx_here());
    rx_seg_check(block, REG_RBP, es_disp);
    if (!is_stos)
        rx_seg_check(block, RX_SEG, 0);

    /*Top of the loop*/
    loop = rx_here();
    rx_rr(block, 0, 0, 0x85, RX_CNT, RX_CNT);
    to_done = rx_jcc(block, RX_CC_Z);
    rx_rr(block, 0, 0, 0x85, RX_SLOW, RX_SLOW);
    slow[nr_slow++] = rx_jcc(block, RX_CC_NZ);

    /*EAX = run length, first limited to the cycle budget: the element that
      takes the count below the end is the last one*/
    rx_rr(block, 0, 0, 0x89, RX_CYCLES, REG_RAX);
    rx_rr(block, 0, 0, 0x29, RX_END, REG_RAX);
    branch = rx_jcc(block, RX_CC_S);
    rx_rr(block, 0, 0, 0x31, REG_RDX, REG_RDX);
    rx_rr(block, 0, 0, 0xf7, 6, RX_ELEMCYC); /*DIV ELEMCYC*/
    rx_alu_ri(block, 0, 0, REG_RAX, 1);
    branch2 = rx_jmp(block);
    rx_patch(branch, rx_here());
    rx_mov_ri(block, REG_RAX, 1);
    rx_patch(branch2, rx_here());
    rx_rr(block, 0, 0, 0x89, RX_CNT, REG_RCX);
    rx_min(block);

    /*Linear addresses, ESI = source, EDI = destination*/
    if (!is_stos) {
        rx_rm(block, 0, 0, 0x8b, REG_RSI, RX_SEG, offsetof(x86seg, base));
        rx_rr(block, 0, 0, 0x01, RX_SRC, REG_RSI);
    }
    rx_rm(block, 0, 0, 0x8b, REG_RDI, REG_RBP, es_disp + (int32_t) offsetof(x86seg, base));
    rx_rr(block, 0, 0, 0x01, RX_DST, REG_RDI);
    if (size > 1) {
        /*Misaligned elements take the interpreter's slow path*/
        rx_rr(block, 0, 0, 0x89, REG_RDI, REG_RCX);
        if (!is_stos)
            rx_rr(block, 0, 0, 0x09, REG_RSI, REG_RCX);
        rx_test_ri(block, REG_RCX, size - 1);
        slow[nr_slow++] = rx_jcc(block, RX_CC_NZ);
    }

    /*Limit the run to what is left of each page*/
    rx_rr(block, 0, 0, 0x85, RX_STEP, RX_STEP);
    branch = rx_jcc(block, RX_CC_S);
    rx_page_room_fwd(block, REG_RDI, shift);
    if (!is_stos)
        rx_page_room_fwd(block, REG_RSI, shift);
    branch2 = rx_jmp(block);
    rx_patch(branch, rx_here());
    rx_page_room_bwd(block, REG_RDI, shift);
    if (!is_stos)
        rx_page_room_bwd(block, REG_RSI, shift);
    rx_patch(branch2, rx_here());

    /*Direct lookups for both pages, RSI and RDI become host pointers*/
    if (!is_stos)
        rx_lookup(block, REG_RSI, readlookup2, slow, &nr_slow);
    rx_lookup(block, REG_RDI, writelookup2, slow, &nr_slow);

    /*Limit the run to offsets that do not wrap*/
    rx_rr(block, 0, 0, 0x85, RX_STEP, RX_STEP);
    branch = rx_jcc(block, RX_CC_S);
    rx_offset_room_fwd(block, RX_DST, shift, a32);
    if (!is_stos)
        rx_offset_room_fwd(block, RX_SRC, shift, a32);
    branch2 = rx_jmp(block);
    rx_patch(branch, rx_here());
    rx_offset_room_bwd(block, RX_DST, shift, a32, slow, &nr_slow);
    if (!is_stos)
        rx_offset_room_bwd(block, RX_SRC, shift, a32, slow, &nr_slow);
    rx_patch(branch2, rx_here());
    rx_rr(block, 0, 0, 0x85, REG_RAX, REG_RAX);
    slow[nr_slow++] = rx_jcc(block, RX_CC_Z);

    /*Segment limits for the whole run, EDX = run length in bytes*/
    rx_rr(block, 0, 0, 0x89, REG_RAX, REG_RDX);
    if (shift)
        rx_shift_ri(block, 4, REG_RDX, shift);
    rx_limit_check(block, RX_DST, REG_RBP, es_disp, size, slow, &nr_slow);
    if (!is_stos)
        rx_limit_check(block, RX_SRC, RX_SEG, 0, size, slow, &nr_slow);

    /*Nothing in the run can fault now, so step the registers past it*/
    rx_rr(block, 0, 0, 0x29, REG_RAX, RX_CNT);
    rx_rr(block, 0, 0, 0x89, REG_RAX, REG_RCX);
    rx_rr(block, 0, 0, 0x0faf, REG_RCX, RX_STEP); /*IMUL ECX, STEP*/
    rx_step_offset(block, RX_DST, REG_RCX, a32);
    if (!is_stos)
        rx_step_offset(block, RX_SRC, REG_RCX, a32);
    rx_rr(block, 0, 0, 0x89, REG_RAX, REG_RCX);
    rx_rr(block, 0, 0, 0x0faf, REG_RCX, RX_ELEMCYC);
    rx_rr(block, 0, 0, 0x29, REG_RCX, RX_CYCLES);

    /*Copy ECX elements, downwards with the direction flag set*/
    rx_rr(block, 0, 0, 0x89, REG_RAX, REG_RCX);
    if (is_stos) {
        if (size == 1)
            rx_rm(block, 0, 0, 0x0fb6, REG_RAX, REG_RBP, rx_state(&AL));
        else if (size == 2)
            rx_rm(block, 0, 0, 0x0fb7, REG_RAX, REG_RBP, rx_state(&AX));
        else
            rx_rm(block, 0, 0, 0x8b, REG_RAX, REG_RBP, rx_state(&EAX));
    }
    rx_rr(block, 0, 0, 0x85, RX_STEP, RX_STEP);
    branch = rx_jcc(block, RX_CC_NS);
    {
        static const uint8_t std = 0xfd;

        rx_bytes(block, &std, 1);
    }
    rx_patch(branch, rx_here());
    {
        static const uint8_t movs[3][3] = { { 0xf3, 0xa4 }, { 0x66, 0xf3, 0xa5 }, { 0xf3, 0xa5 } };
        static const uint8_t stos[3][3] = { { 0xf3, 0xaa }, { 0x66, 0xf3, 0xab }, { 0xf3, 0xab } };
        static const uint8_t cld        = 0xfc;

        rx_bytes(block, is_stos ? stos[shift] : movs[shift], (shift == 1) ? 3 : 2);
        rx_bytes(block, &cld, 1);
    }
    rx_rr(block, 0, 0, 0x39, RX_END, RX_CYCLES); /*CMP CYCLES, END*/
    to_break[0] = rx_jcc(block, RX_CC_L);
    rx_patch(rx_jmp(block), loop);

    /*One element through the interpreter's loop body*/
    for (int c = 0; c < nr_slow; c++)
        rx_patch(slow[c], rx_here());
    rx_writeback(block, is_stos, a32);
#    if _WIN64
    rx_rr(block, 0, 1, 0x89, RX_SEG, REG_RCX);
#    else
    rx_rr(block, 0, 1, 0x89, RX_SEG, REG_RDI);
#    endif
    rx_mov_ri64(block, REG_RAX, (uint64_t) (uintptr_t) elem_func);
    rx_rr(block, 0, 0, 0xff, 2, REG_RAX); /*CALL RAX*/
    /*The call does not keep R8-R11, and a misaligned access charges extra
      cycles, so the guest registers and the cycle count are read back*/
    rx_load_regs(block, is_stos, a32);
    rx_rr(block, 0, 0, 0x85, REG_RAX, REG_RAX);
    to_abort = rx_jcc(block, RX_CC_NZ);
    if (!is_stos)
        rx_step_offset(block, RX_SRC, RX_STEP, a32);
    rx_step_offset(block, RX_DST, RX_STEP, a32);
    rx_alu_ri(block, 0, 5, RX_CNT, 1);
    rx_rr(block, 0, 0, 0x29, RX_ELEMCYC, RX_CYCLES);
    rx_rr(block, 0, 0, 0x39, RX_END, RX_CYCLES);
    to_break[1] = rx_jcc(block, RX_CC_L);
    rx_patch(rx_jmp(block), loop);

    /*Count ran out*/
    rx_patch(to_done, rx_here());
    rx_writeback(block, is_stos, a32);
    done_ret = rx_here();
    rx_rr(block, 0, 0, 0x31, REG_RAX, REG_RAX);
    rx_epilogue(block);

    /*Out of cycles: carry on if the count ran out on the same element,
      otherwise leave the block to restart the instruction*/
    rx_patch(to_break[0], rx_here());
    rx_patch(to_break[1], rx_here());
    rx_writeback(block, is_stos, a32);
    rx_rr(block, 0, 0, 0x85, RX_CNT, RX_CNT);
    rx_patch(rx_jcc(block, RX_CC_Z), done_ret);
    to_set_pc = rx_jmp(block);

    /*Faulting element*/
    rx_patch(to_abort, rx_here());
    rx_writeback(block, is_stos, a32);

    rx_patch(to_set_pc, rx_here());
    rx_rm(block, 0, 0, 0x8b, REG_RCX, REG_RBP, rx_state(&cpu_state.oldpc));
    rx_rm(block, 0, 0, 0x89, REG_RCX, REG_RBP, rx_state(&cpu_state.pc));
    rx_mov_ri(block, REG_RAX, 1);
    rx_epilogue(block);

    if (nr_slow > RX_MAX_PATCH)
        fatal("build_rep_routine - too many slow path branches\n");
}

void
codegen_backend_init(void)
{
    codeblock_t *block;
    int          c;
    uint8_t      large_block = 0;
    uint8_t      large_hash = 0;

    codeblock      = plat_mmap(BLOCK_SIZE * sizeof(codeblock_t), 0, &large_block);
    codeblock_hash = plat_mmap(HASH_SIZE * sizeof(codeblock_t *), 0, &large_hash);

    if (large_block)
        pclog("Allocated %llu bytes of large pages for codeblock pointers\n", (unsigned long long) (BLOCK_SIZE * sizeof(codeblock_t)));
    if (large_hash)
        pclog("Allocated %llu bytes of large pages for codeblock hashes\n", (unsigned long long) (HASH_SIZE * sizeof(codeblock_t *)));

    for (c = 0; c < BLOCK_SIZE; c++)
        codeblock[c].valid = 0;

    block_current                           = 0;
    block_pos                               = 0;
    block                                   = &codeblock[block_current];
    codeblock[block_current].head_mem_block = codegen_allocator_allocate(NULL, block_current);
    codeblock[block_current].data           = codeblock_allocator_get_ptr(codeblock[block_current].head_mem_block);
    block_write_data                        = codeblock[block_current].data;
    build_loadstore_routines(&codeblock[block_current]);

    codegen_gpf_rout = routine_start(block);
#    if _WIN64
    host_x86_XOR32_REG_REG(block, REG_ECX, REG_ECX);
    host_x86_XOR32_REG_REG(block, REG_EDX, REG_EDX);
#    else
    host_x86_XOR32_REG_REG(block, REG_EDI, REG_EDI);
    host_x86_XOR32_REG_REG(block, REG_ESI, REG_ESI);
#    endif
    host_x86_CALL(block, (void *) x86gpf);
    codegen_exit_rout = &block_write_data[block_pos];
#ifdef _WIN64
    /* XMM6 and XMM7 hold guest FPU/MMX values in blocks, and the Windows
       x64 ABI makes them the caller's: put back what the caller had. */
    host_x86_MOVDQU_XREG_BASE_OFFSET(block, REG_XMM6, REG_RSP, CODEGEN_XMM6_SAVE);
    host_x86_MOVDQU_XREG_BASE_OFFSET(block, REG_XMM7, REG_RSP, CODEGEN_XMM7_SAVE);
    host_x86_ADD64_REG_IMM(block, REG_RSP, CODEGEN_WIN64_FRAME);
#else
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x48);
#endif
    host_x86_POP(block, REG_R15);
    host_x86_POP(block, REG_R14);
    host_x86_POP(block, REG_R13);
    host_x86_POP(block, REG_R12);
#ifdef _WIN64
    host_x86_POP(block, REG_RDI);
    host_x86_POP(block, REG_RSI);
#endif
    host_x86_POP(block, REG_RBP);
    host_x86_POP(block, REG_RBX);
    host_x86_RET(block);

    /*#SS(0): an operand through SS outside its limit (CHECK_SEG_LIMITS)*/
    codegen_ss_rout = routine_start(block);
#    if _WIN64
    host_x86_XOR32_REG_REG(block, REG_ECX, REG_ECX);
    host_x86_XOR32_REG_REG(block, REG_EDX, REG_EDX);
#    else
    host_x86_XOR32_REG_REG(block, REG_EDI, REG_EDI);
    host_x86_XOR32_REG_REG(block, REG_ESI, REG_ESI);
#    endif
    host_x86_CALL(block, (void *) x86ss);
    host_x86_JMP(block, codegen_exit_rout);

    for (int is_stos = 0; is_stos < 2; is_stos++) {
        for (int shift = 0; shift < 3; shift++) {
            for (int a32 = 0; a32 < 2; a32++) {
                codegen_rep_rout[is_stos][shift][a32] = routine_start(block);
                build_rep_routine(block, is_stos, shift, a32, codegen_rep_elem[is_stos][shift][a32]);
            }
        }
    }

    block_write_data = NULL;

    asm(
        "stmxcsr %0\n"
        : "=m"(cpu_state.old_fp_control));
    cpu_state.trunc_fp_control = cpu_state.old_fp_control | 0x6000;
}

void
codegen_set_rounding_mode(int mode)
{
    cpu_state.new_fp_control = (cpu_state.old_fp_control & ~0x6000) | (mode << 13);
}

void
codegen_backend_prologue(codeblock_t *block)
{
    block_pos = BLOCK_START; /*Entry code*/
    host_x86_PUSH(block, REG_RBX);
    host_x86_PUSH(block, REG_RBP);
#ifdef _WIN64
    host_x86_PUSH(block, REG_RSI);
    host_x86_PUSH(block, REG_RDI);
#endif
    host_x86_PUSH(block, REG_R12);
    host_x86_PUSH(block, REG_R13);
    host_x86_PUSH(block, REG_R14);
    host_x86_PUSH(block, REG_R15);
#ifdef _WIN64
    host_x86_SUB64_REG_IMM(block, REG_RSP, CODEGEN_WIN64_FRAME);
    host_x86_MOVDQU_BASE_OFFSET_XREG(block, REG_RSP, CODEGEN_XMM6_SAVE, REG_XMM6);
    host_x86_MOVDQU_BASE_OFFSET_XREG(block, REG_RSP, CODEGEN_XMM7_SAVE, REG_XMM7);
#else
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x48);
#endif
    host_x86_MOV64_REG_IMM(block, REG_RBP, ((uintptr_t) &cpu_state) + 128);
    if (block->flags & CODEBLOCK_HAS_FPU) {
        host_x86_MOV32_REG_ABS(block, REG_EAX, &cpu_state.TOP);
        host_x86_SUB32_REG_IMM(block, REG_EAX, block->TOP);
        host_x86_MOV32_BASE_OFFSET_REG(block, REG_RSP, IREG_TOP_diff_stack_offset, REG_EAX);
    }
    if (block->flags & CODEBLOCK_NO_IMMEDIATES)
        host_x86_MOV64_REG_IMM(block, REG_R12, ((uintptr_t) ram) + 2147483648ULL);
}

void
codegen_backend_epilogue(codeblock_t *block)
{
#ifdef _WIN64
    /* XMM6 and XMM7 hold guest FPU/MMX values in blocks, and the Windows
       x64 ABI makes them the caller's: put back what the caller had. */
    host_x86_MOVDQU_XREG_BASE_OFFSET(block, REG_XMM6, REG_RSP, CODEGEN_XMM6_SAVE);
    host_x86_MOVDQU_XREG_BASE_OFFSET(block, REG_XMM7, REG_RSP, CODEGEN_XMM7_SAVE);
    host_x86_ADD64_REG_IMM(block, REG_RSP, CODEGEN_WIN64_FRAME);
#else
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x48);
#endif
    host_x86_POP(block, REG_R15);
    host_x86_POP(block, REG_R14);
    host_x86_POP(block, REG_R13);
    host_x86_POP(block, REG_R12);
#ifdef _WIN64
    host_x86_POP(block, REG_RDI);
    host_x86_POP(block, REG_RSI);
#endif
    host_x86_POP(block, REG_RBP);
    host_x86_POP(block, REG_RBX);
    host_x86_RET(block);
}
#endif
