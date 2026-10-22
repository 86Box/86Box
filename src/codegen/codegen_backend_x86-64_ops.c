#if defined __amd64__ || defined _M_X64

#    include <stdint.h>
#    include <inttypes.h>
#    include <86box/86box.h>
#    include "cpu.h"
#    include <86box/mem.h>
#    include <86box/plat_unused.h>

#    include "codegen.h"
#    include "codegen_allocator.h"
#    include "codegen_backend.h"
#    include "codegen_backend_x86-64_defs.h"
#    include "codegen_backend_x86-64_ops.h"
#    include "codegen_backend_x86-64_ops_helpers.h"

#    define RM_OP_ADD 0x00
#    define RM_OP_OR  0x08
#    define RM_OP_AND 0x20
#    define RM_OP_SUB 0x28
#    define RM_OP_XOR 0x30
#    define RM_OP_CMP 0x38

#    define RM_OP_ROL 0x00
#    define RM_OP_ROR 0x08
#    define RM_OP_SHL 0x20
#    define RM_OP_SHR 0x28
#    define RM_OP_SAR 0x38

/*Move to a new code chunk now if size bytes might not fit in this one, so
  code emitted next, up to size bytes, stays in one chunk.*/
void
codegen_alloc(codeblock_t *block, int size)
{
    codegen_alloc_bytes(block, size);
}

static inline void
call(codeblock_t *block, uintptr_t func)
{
    intptr_t diff;

    codegen_alloc_bytes(block, 5);
    diff = (intptr_t) (func - (uintptr_t) &block_write_data[block_pos + 5]);

    if (diff >= -0x80000000LL && diff < 0x7fffffffLL) {
        codegen_addbyte(block, 0xE8); /*CALL*/
        codegen_addlong(block, (uint32_t) diff);
    } else {
        codegen_alloc_bytes(block, 13);
        codegen_addbyte2(block, 0x49, 0xb9); /*MOV R9, func*/
        codegen_addquad(block, func);
        codegen_addbyte3(block, 0x41, 0xff, 0xd1); /*CALL R9*/
    }
}

static inline void
jmp(codeblock_t *block, uintptr_t func)
{
    intptr_t diff;

    codegen_alloc_bytes(block, 5);
    diff = (intptr_t) (func - (uintptr_t) &block_write_data[block_pos + 5]);

    if (diff >= -0x80000000LL && diff < 0x7fffffffLL) {
        codegen_addbyte(block, 0xe9); /*JMP*/
        codegen_addlong(block, (uint32_t) diff);
    } else {
        codegen_alloc_bytes(block, 13);
        codegen_addbyte2(block, 0x49, 0xb9); /*MOV R9, func*/
        codegen_addquad(block, func);
        codegen_addbyte3(block, 0x41, 0xff, 0xe1); /*JMP R9*/
    }
}

/*ALU operation with an immediate: the accumulator short form for EAX,
  the sign-extended imm8 form where it fits, else the full immediate.*/
static void
emit_alu8_imm(codeblock_t *block, uint8_t eax_op, int ext, int dst_reg, uint8_t imm_data)
{
    if (dst_reg == REG_EAX) {
        emit_head(block, 0, 0, 1, eax_op, 1);
        codegen_addbyte(block, imm_data);
    } else
        emit_ext(block, 0, 0, 1, 1, 0x80, ext, dst_reg, 1, imm_data);
}
static void
emit_alu16_imm(codeblock_t *block, uint8_t eax_op, int ext, int dst_reg, uint16_t imm_data)
{
    if (is_imm8(imm_data))
        emit_ext(block, 0x66, 0, 0, 1, 0x83, ext, dst_reg, 1, imm_data & 0xff);
    else if (dst_reg == REG_EAX) {
        emit_head(block, 0x66, 0, 1, eax_op, 2);
        codegen_addword(block, imm_data);
    } else
        emit_ext(block, 0x66, 0, 0, 1, 0x81, ext, dst_reg, 2, imm_data);
}
static void
emit_alu32_imm(codeblock_t *block, uint8_t eax_op, int ext, int dst_reg, uint32_t imm_data)
{
    if (is_imm8(imm_data))
        emit_ext(block, 0, 0, 0, 1, 0x83, ext, dst_reg, 1, imm_data & 0xff);
    else if (dst_reg == REG_EAX) {
        emit_head(block, 0, 0, 1, eax_op, 4);
        codegen_addlong(block, imm_data);
    } else
        emit_ext(block, 0, 0, 0, 1, 0x81, ext, dst_reg, 4, imm_data);
}

/*[RBP + offset] addresses cpu_state; see codegen_backend_prologue(). Other
  globals of the executable are also in reach of a signed 32-bit offset.*/
static inline int64_t
rbp_offset(void *p)
{
    return (uintptr_t) p - (((uintptr_t) &cpu_state) + 128);
}

static inline int
fits_disp32(int64_t offset)
{
    return (offset >= -2147483648LL) && (offset <= 2147483647LL);
}

void
host_x86_ADD8_REG_IMM(codeblock_t *block, int dst_reg, uint8_t imm_data)
{
    emit_alu8_imm(block, 0x04, RM_OP_ADD, dst_reg, imm_data); /*ADD dst_reg, imm_data*/
}
void
host_x86_ADD16_REG_IMM(codeblock_t *block, int dst_reg, uint16_t imm_data)
{
    emit_alu16_imm(block, 0x05, RM_OP_ADD, dst_reg, imm_data); /*ADD dst_reg, imm_data*/
}
void
host_x86_ADD32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data)
{
    emit_alu32_imm(block, 0x05, RM_OP_ADD, dst_reg, imm_data); /*ADD dst_reg, imm_data*/
}
void
host_x86_ADD64_REG_IMM(codeblock_t *block, int dst_reg, uint64_t imm_data)
{
    if (is_imm8(imm_data))
        emit_ext(block, 0, 1, 0, 1, 0x83, RM_OP_ADD, dst_reg, 1, imm_data & 0xff); /*ADD dst_reg, imm_data*/
    else
        fatal("ADD64_REG_IMM !is_imm8 %016" PRIx64 "\n", imm_data);
}
void
host_x86_ADD8_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 0, 1, 1, 0x00, src_reg, dst_reg); /*ADD dst_reg, src_reg*/
}
void
host_x86_ADD16_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0x66, 0, 0, 1, 0x01, src_reg, dst_reg); /*ADD dst_reg, src_reg*/
}
void
host_x86_ADD32_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 0, 0, 1, 0x01, src_reg, dst_reg); /*ADD dst_reg, src_reg*/
}

void
host_x86_AND8_REG_IMM(codeblock_t *block, int dst_reg, uint8_t imm_data)
{
    emit_alu8_imm(block, 0x24, RM_OP_AND, dst_reg, imm_data); /*AND dst_reg, imm_data*/
}
void
host_x86_AND16_REG_IMM(codeblock_t *block, int dst_reg, uint16_t imm_data)
{
    emit_alu16_imm(block, 0x25, RM_OP_AND, dst_reg, imm_data); /*AND dst_reg, imm_data*/
}
void
host_x86_AND32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data)
{
    emit_alu32_imm(block, 0x25, RM_OP_AND, dst_reg, imm_data); /*AND dst_reg, imm_data*/
}
void
host_x86_AND8_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 0, 1, 1, 0x20, src_reg, dst_reg); /*AND dst_reg, src_reg*/
}
void
host_x86_AND16_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0x66, 0, 0, 1, 0x21, src_reg, dst_reg); /*AND dst_reg, src_reg*/
}
void
host_x86_AND32_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 0, 0, 1, 0x21, src_reg, dst_reg); /*AND dst_reg, src_reg*/
}

void
host_x86_CALL(codeblock_t *block, void *p)
{
    call(block, (uintptr_t) p);
}

void
host_x86_CMP16_REG_IMM(codeblock_t *block, int dst_reg, uint16_t imm_data)
{
    emit_alu16_imm(block, 0x3d, RM_OP_CMP, dst_reg, imm_data); /*CMP dst_reg, imm_data*/
}
void
host_x86_CMP32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data)
{
    emit_alu32_imm(block, 0x3d, RM_OP_CMP, dst_reg, imm_data); /*CMP dst_reg, imm_data*/
}
void
host_x86_CMP64_REG_IMM(codeblock_t *block, int dst_reg, uint64_t imm_data)
{
    if (is_imm8(imm_data))
        emit_ext(block, 0, 1, 0, 1, 0x83, RM_OP_CMP, dst_reg, 1, imm_data & 0xff); /*CMP dst_reg, imm_data*/
    else
        fatal("CMP64_REG_IMM not 8-bit imm\n");
}

void
host_x86_CMP8_REG_REG(codeblock_t *block, int src_reg_a, int src_reg_b)
{
    emit_rr(block, 0, 0, 1, 1, 0x38, src_reg_b, src_reg_a); /*CMP src_reg_a, src_reg_b*/
}
void
host_x86_CMP16_REG_REG(codeblock_t *block, int src_reg_a, int src_reg_b)
{
    emit_rr(block, 0x66, 0, 0, 1, 0x39, src_reg_b, src_reg_a); /*CMP src_reg_a, src_reg_b*/
}
void
host_x86_CMP32_REG_REG(codeblock_t *block, int src_reg_a, int src_reg_b)
{
    emit_rr(block, 0, 0, 0, 1, 0x39, src_reg_b, src_reg_a); /*CMP src_reg_a, src_reg_b*/
}
void
host_x86_CMP64_REG_REG(codeblock_t *block, int src_reg_a, int src_reg_b)
{
    emit_rr(block, 0, 1, 0, 1, 0x39, src_reg_b, src_reg_a); /*CMP src_reg_a, src_reg_b (64-bit)*/
}

void
host_x86_JMP(codeblock_t *block, void *p)
{
    jmp(block, (uintptr_t) p);
}

void
host_x86_JNZ(codeblock_t *block, void *p)
{
    codegen_alloc_bytes(block, 6);
    codegen_addbyte2(block, 0x0f, 0x85); /*JNZ*/
    codegen_addlong(block, (uintptr_t) p - (uintptr_t) &block_write_data[block_pos + 4]);
}
void
host_x86_JZ(codeblock_t *block, void *p)
{
    codegen_alloc_bytes(block, 6);
    codegen_addbyte2(block, 0x0f, 0x84); /*JZ*/
    codegen_addlong(block, (uintptr_t) p - (uintptr_t) &block_write_data[block_pos + 4]);
}

uint8_t *
host_x86_JNBE_short(codeblock_t *block)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0x77, 0); /*JNBE*/
    return &block_write_data[block_pos - 1];
}
uint8_t *
host_x86_JNZ_short(codeblock_t *block)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0x75, 0); /*JNZ*/
    return &block_write_data[block_pos - 1];
}
uint8_t *
host_x86_JS_short(codeblock_t *block)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0x78, 0); /*JS*/
    return &block_write_data[block_pos - 1];
}
uint8_t *
host_x86_JZ_short(codeblock_t *block)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0x74, 0); /*JZ*/
    return &block_write_data[block_pos - 1];
}

uint32_t *
host_x86_JMP_long(codeblock_t *block)
{
    codegen_alloc_bytes(block, 5);
    codegen_addbyte(block, 0xe9); /*JMP*/
    codegen_addlong(block, 0);
    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_x86_JNB_long(codeblock_t *block)
{
    codegen_alloc_bytes(block, 6);
    codegen_addbyte2(block, 0x0f, 0x83); /*JNB*/
    codegen_addlong(block, 0);
    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_x86_JNBE_long(codeblock_t *block)
{
    codegen_alloc_bytes(block, 6);
    codegen_addbyte2(block, 0x0f, 0x87); /*JNBE*/
    codegen_addlong(block, 0);
    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_x86_JNL_long(codeblock_t *block)
{
    codegen_alloc_bytes(block, 6);
    codegen_addbyte2(block, 0x0f, 0x8d); /*JNL*/
    codegen_addlong(block, 0);
    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_x86_JNLE_long(codeblock_t *block)
{
    codegen_alloc_bytes(block, 6);
    codegen_addbyte2(block, 0x0f, 0x8f); /*JNLE*/
    codegen_addlong(block, 0);
    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_x86_JNO_long(codeblock_t *block)
{
    codegen_alloc_bytes(block, 6);
    codegen_addbyte2(block, 0x0f, 0x81); /*JNO*/
    codegen_addlong(block, 0);
    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_x86_JNS_long(codeblock_t *block)
{
    codegen_alloc_bytes(block, 6);
    codegen_addbyte2(block, 0x0f, 0x89); /*JNS*/
    codegen_addlong(block, 0);
    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_x86_JNZ_long(codeblock_t *block)
{
    codegen_alloc_bytes(block, 6);
    codegen_addbyte2(block, 0x0f, 0x85); /*JNZ*/
    codegen_addlong(block, 0);
    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_x86_JB_long(codeblock_t *block)
{
    codegen_alloc_bytes(block, 6);
    codegen_addbyte2(block, 0x0f, 0x82); /*JB*/
    codegen_addlong(block, 0);
    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_x86_JBE_long(codeblock_t *block)
{
    codegen_alloc_bytes(block, 6);
    codegen_addbyte2(block, 0x0f, 0x86); /*JBE*/
    codegen_addlong(block, 0);
    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_x86_JL_long(codeblock_t *block)
{
    codegen_alloc_bytes(block, 6);
    codegen_addbyte2(block, 0x0f, 0x8c); /*JL*/
    codegen_addlong(block, 0);
    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_x86_JLE_long(codeblock_t *block)
{
    codegen_alloc_bytes(block, 6);
    codegen_addbyte2(block, 0x0f, 0x8e); /*JLE*/
    codegen_addlong(block, 0);
    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_x86_JO_long(codeblock_t *block)
{
    codegen_alloc_bytes(block, 6);
    codegen_addbyte2(block, 0x0f, 0x80); /*JO*/
    codegen_addlong(block, 0);
    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_x86_JS_long(codeblock_t *block)
{
    codegen_alloc_bytes(block, 6);
    codegen_addbyte2(block, 0x0f, 0x88); /*JS*/
    codegen_addlong(block, 0);
    return (uint32_t *) &block_write_data[block_pos - 4];
}
uint32_t *
host_x86_JZ_long(codeblock_t *block)
{
    codegen_alloc_bytes(block, 6);
    codegen_addbyte2(block, 0x0f, 0x84); /*JZ*/
    codegen_addlong(block, 0);
    return (uint32_t *) &block_write_data[block_pos - 4];
}

void
host_x86_LAHF(codeblock_t *block)
{
    codegen_alloc_bytes(block, 1);
    codegen_addbyte(block, 0x9f); /*LAHF*/
}

void
host_x86_LEA_REG_IMM(codeblock_t *block, int dst_reg, int src_reg, uint32_t offset)
{
    /*LEA dst_reg, [offset+src_reg]*/
    emit_mem(block, 0, 0, 0, 1, 0x8d, dst_reg, src_reg, NO_INDEX, 0, offset, offset ? DISP_32 : DISP_AUTO, 0, 0);
}
void
host_x86_LEA_REG_REG(codeblock_t *block, int dst_reg, int src_reg_a, int src_reg_b)
{
    emit_mem(block, 0, 0, 0, 1, 0x8d, dst_reg, src_reg_a, src_reg_b, 0, 0, DISP_AUTO, 0, 0); /*LEA dst_reg, [Rsrc_reg_a + Rsrc_reg_b]*/
}
void
host_x86_LEA_REG_REG_SHIFT(codeblock_t *block, int dst_reg, int src_reg_a, int src_reg_b, int shift)
{
    emit_mem(block, 0, 0, 0, 1, 0x8d, dst_reg, src_reg_a, src_reg_b, shift, 0, DISP_AUTO, 0, 0); /*LEA dst_reg, [Rsrc_reg_a + Rsrc_reg_b * (1 << shift)]*/
}

void
host_x86_MOV8_ABS_IMM(codeblock_t *block, void *p, uint32_t imm_data)
{
    int64_t offset = rbp_offset(p);

    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0, 0, 0, 1, 0xc6, 0, REG_RBP, NO_INDEX, 0, offset, DISP_8, 1, imm_data); /*MOVB offset[RBP], imm_data*/
    else if (fits_disp32(offset))
        emit_mem(block, 0, 0, 0, 1, 0xc6, 0, REG_RBP, NO_INDEX, 0, offset, DISP_32, 1, imm_data); /*MOVB offset[RBP], imm_data*/
    else {
        if ((uintptr_t) p >> 32)
            fatal("host_x86_MOV8_ABS_IMM - out of range %p\n", p);
        emit_abs32(block, 0, 0, 0, 1, 0xc6, 0, (uint32_t) (uintptr_t) p, 1, imm_data); /*MOVB p, imm_data*/
    }
}
void
host_x86_MOV16_ABS_IMM(codeblock_t *block, void *p, uint16_t imm_data)
{
    int64_t offset = rbp_offset(p);

    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0x66, 0, 0, 1, 0xc7, 0, REG_RBP, NO_INDEX, 0, offset, DISP_8, 2, imm_data); /*MOV offset[RBP], imm_data*/
    else if (fits_disp32(offset))
        emit_mem(block, 0x66, 0, 0, 1, 0xc7, 0, REG_RBP, NO_INDEX, 0, offset, DISP_32, 2, imm_data); /*MOV offset[RBP], imm_data*/
    else {
        if ((uintptr_t) p >> 32)
            fatal("host_x86_MOV32_ABS_IMM - out of range %p\n", p);
        emit_abs32(block, 0x66, 0, 0, 1, 0xc7, 0, (uint32_t) (uintptr_t) p, 2, imm_data); /*MOV p, imm_data*/
    }
}
void
host_x86_MOV32_ABS_IMM(codeblock_t *block, void *p, uint32_t imm_data)
{
    int64_t offset = rbp_offset(p);

    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0, 0, 0, 1, 0xc7, 0, REG_RBP, NO_INDEX, 0, offset, DISP_8, 4, imm_data); /*MOV offset[RBP], imm_data*/
    else if (fits_disp32(offset))
        emit_mem(block, 0, 0, 0, 1, 0xc7, 0, REG_RBP, NO_INDEX, 0, offset, DISP_32, 4, imm_data); /*MOV offset[RBP], imm_data*/
    else {
        if ((uintptr_t) p >> 32)
            fatal("host_x86_MOV32_ABS_IMM - out of range %p\n", p);
        emit_abs32(block, 0, 0, 0, 1, 0xc7, 0, (uint32_t) (uintptr_t) p, 4, imm_data); /*MOV p, imm_data*/
    }
}

void
host_x86_MOV8_ABS_REG(codeblock_t *block, void *p, int src_reg)
{
    int64_t offset = rbp_offset(p);

    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0, 0, 1, 1, 0x88, src_reg, REG_RBP, NO_INDEX, 0, offset, DISP_8, 0, 0); /*MOVB offset[RBP], src_reg*/
    else if (fits_disp32(offset))
        emit_mem(block, 0, 0, 1, 1, 0x88, src_reg, REG_RBP, NO_INDEX, 0, offset, DISP_32, 0, 0); /*MOVB offset[RBP], src_reg*/
    else {
        if ((uintptr_t) p >> 32)
            fatal("host_x86_MOV8_ABS_REG - out of range %p\n", p);
        emit_abs32(block, 0, 0, 1, 1, 0x88, src_reg, (uint32_t) (uintptr_t) p, 0, 0); /*MOVB [p], src_reg*/
    }
}
void
host_x86_MOV16_ABS_REG(codeblock_t *block, void *p, int src_reg)
{
    int64_t offset = rbp_offset(p);

    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0x66, 0, 0, 1, 0x89, src_reg, REG_RBP, NO_INDEX, 0, offset, DISP_8, 0, 0); /*MOV offset[RBP], src_reg*/
    else if (fits_disp32(offset))
        emit_mem(block, 0x66, 0, 0, 1, 0x89, src_reg, REG_RBP, NO_INDEX, 0, offset, DISP_32, 0, 0); /*MOV offset[RBP], src_reg*/
    else {
        if ((uintptr_t) p >> 32)
            fatal("host_x86_MOV16_ABS_REG - out of range %p\n", p);
        emit_abs32(block, 0x66, 0, 0, 1, 0x89, src_reg, (uint32_t) (uintptr_t) p, 0, 0);
    }
}
void
host_x86_MOV32_ABS_REG(codeblock_t *block, void *p, int src_reg)
{
    int64_t offset = rbp_offset(p);

    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0, 0, 0, 1, 0x89, src_reg, REG_RBP, NO_INDEX, 0, offset, DISP_8, 0, 0); /*MOV offset[RBP], src_reg*/
    else if (fits_disp32(offset))
        emit_mem(block, 0, 0, 0, 1, 0x89, src_reg, REG_RBP, NO_INDEX, 0, offset, DISP_32, 0, 0); /*MOV offset[RBP], src_reg*/
    else {
        if ((uintptr_t) p >> 32)
            fatal("host_x86_MOV32_ABS_REG - out of range %p\n", p);
        emit_abs32(block, 0, 0, 0, 1, 0x89, src_reg, (uint32_t) (uintptr_t) p, 0, 0); /*MOV [p], src_reg*/
    }
}
void
host_x86_MOV64_ABS_REG(codeblock_t *block, void *p, int src_reg)
{
    int64_t offset = rbp_offset(p);

    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0, 1, 0, 1, 0x89, src_reg, REG_RBP, NO_INDEX, 0, offset, DISP_8, 0, 0); /*MOV offset[RBP], src_reg*/
    else if (fits_disp32(offset))
        emit_mem(block, 0, 1, 0, 1, 0x89, src_reg, REG_RBP, NO_INDEX, 0, offset, DISP_32, 0, 0); /*MOV offset[RBP], src_reg*/
    else {
        if ((uintptr_t) p >> 32)
            fatal("host_x86_MOV64_ABS_REG - out of range %p\n", p);
        emit_abs32(block, 0, 1, 0, 1, 0x89, src_reg, (uint32_t) (uintptr_t) p, 0, 0); /*MOV [p], src_reg*/
    }
}

void
host_x86_MOV8_ABS_REG_REG_SHIFT_REG(codeblock_t *block, uint32_t addr, int base_reg, int index_reg, int shift, int src_reg)
{
    /*MOV addr[base_reg + idx_reg << shift], src_reg*/
    emit_mem(block, 0, 0, 1, 1, 0x88, src_reg, base_reg, index_reg, shift, addr, (addr < 0x80 || addr >= 0xffffff80) ? DISP_8 : DISP_32, 0, 0);
}

void
host_x86_MOV8_BASE_INDEX_REG(codeblock_t *block, int base_reg, int index_reg, int src_reg)
{
    emit_mem(block, 0, 0, 1, 1, 0x88, src_reg, base_reg, index_reg, 0, 0, DISP_AUTO, 0, 0); /*MOV B[base_reg + index_reg], src_reg*/
}
void
host_x86_MOV16_BASE_INDEX_REG(codeblock_t *block, int base_reg, int index_reg, int src_reg)
{
    emit_mem(block, 0x66, 0, 0, 1, 0x89, src_reg, base_reg, index_reg, 0, 0, DISP_AUTO, 0, 0); /*MOV W[base_reg + index_reg], src_reg*/
}
void
host_x86_MOV32_BASE_INDEX_REG(codeblock_t *block, int base_reg, int index_reg, int src_reg)
{
    emit_mem(block, 0, 0, 0, 1, 0x89, src_reg, base_reg, index_reg, 0, 0, DISP_AUTO, 0, 0); /*MOV L[base_reg + index_reg], src_reg*/
}

void
host_x86_MOV8_REG_ABS(codeblock_t *block, int dst_reg, void *p)
{
    int64_t offset     = rbp_offset(p);
    int64_t ram_offset = (uintptr_t) p - (((uintptr_t) ram) + 2147483648ULL);

    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0, 0, 1, 1, 0x8a, dst_reg, REG_RBP, NO_INDEX, 0, offset, DISP_8, 0, 0); /*MOV dst_reg, offset[RBP]*/
    else if (fits_disp32(offset))
        emit_mem(block, 0, 0, 1, 1, 0x8a, dst_reg, REG_RBP, NO_INDEX, 0, offset, DISP_32, 0, 0); /*MOV dst_reg, offset[RBP]*/
    else if ((ram_offset >= -2147483648LL) && (ram_offset <= 2147483647LL) && (block->flags & CODEBLOCK_NO_IMMEDIATES))
        emit_mem(block, 0, 0, 1, 1, 0x8a, dst_reg, REG_R12, NO_INDEX, 0, ram_offset, DISP_32, 0, 0); /*MOV dst_reg, ram_offset[R12]*/
    else {
        host_x86_MOV64_REG_IMM(block, REG_R9, (uintptr_t) p);
        emit_mem(block, 0, 0, 1, 1, 0x8a, dst_reg, REG_R9, NO_INDEX, 0, 0, DISP_AUTO, 0, 0); /*MOV dst_reg, [R9]*/
    }
}
void
host_x86_MOV16_REG_ABS(codeblock_t *block, int dst_reg, void *p)
{
    int64_t offset     = rbp_offset(p);
    int64_t ram_offset = (uintptr_t) p - (((uintptr_t) ram) + 2147483648ULL);

    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0x66, 0, 0, 1, 0x8b, dst_reg, REG_RBP, NO_INDEX, 0, offset, DISP_8, 0, 0); /*MOV dst_reg, offset[RBP]*/
    else if (fits_disp32(offset))
        emit_mem(block, 0x66, 0, 0, 1, 0x8b, dst_reg, REG_RBP, NO_INDEX, 0, offset, DISP_32, 0, 0); /*MOV dst_reg, offset[RBP]*/
    else if ((ram_offset >= -2147483648LL) && (ram_offset <= 2147483647LL) && (block->flags & CODEBLOCK_NO_IMMEDIATES))
        emit_mem(block, 0x66, 0, 0, 1, 0x8b, dst_reg, REG_R12, NO_INDEX, 0, ram_offset, DISP_32, 0, 0); /*MOV dst_reg, ram_offset[R12]*/
    else {
        host_x86_MOV64_REG_IMM(block, REG_R9, (uintptr_t) p);
        emit_mem(block, 0x66, 0, 0, 1, 0x8b, dst_reg, REG_R9, NO_INDEX, 0, 0, DISP_AUTO, 0, 0);
    }
}
void
host_x86_MOV32_REG_ABS(codeblock_t *block, int dst_reg, void *p)
{
    int64_t offset     = rbp_offset(p);
    int64_t ram_offset = (uintptr_t) p - (((uintptr_t) ram) + 2147483648ULL);

    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0, 0, 0, 1, 0x8b, dst_reg, REG_RBP, NO_INDEX, 0, offset, DISP_8, 0, 0); /*MOV dst_reg, offset[RBP]*/
    else if (fits_disp32(offset))
        emit_mem(block, 0, 0, 0, 1, 0x8b, dst_reg, REG_RBP, NO_INDEX, 0, offset, DISP_32, 0, 0); /*MOV dst_reg, offset[RBP]*/
    else if ((ram_offset >= -2147483648LL) && (ram_offset <= 2147483647LL) && (block->flags & CODEBLOCK_NO_IMMEDIATES))
        emit_mem(block, 0, 0, 0, 1, 0x8b, dst_reg, REG_R12, NO_INDEX, 0, ram_offset, DISP_32, 0, 0); /*MOV dst_reg, ram_offset[R12]*/
    else {
        host_x86_MOV64_REG_IMM(block, REG_R9, (uintptr_t) p);
        emit_mem(block, 0, 0, 0, 1, 0x8b, dst_reg, REG_R9, NO_INDEX, 0, 0, DISP_AUTO, 0, 0); /*MOV dst_reg, [R9]*/
    }
}
void
host_x86_MOV64_REG_ABS(codeblock_t *block, int dst_reg, void *p)
{
    int64_t offset = rbp_offset(p);

    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0, 1, 0, 1, 0x8b, dst_reg, REG_RBP, NO_INDEX, 0, offset, DISP_8, 0, 0); /*MOV dst_reg, offset[RBP]*/
    else if (fits_disp32(offset))
        emit_mem(block, 0, 1, 0, 1, 0x8b, dst_reg, REG_RBP, NO_INDEX, 0, offset, DISP_32, 0, 0); /*MOV dst_reg, offset[RBP]*/
    else {
        host_x86_MOV64_REG_IMM(block, dst_reg, (uintptr_t) p);
        host_x86_MOV64_REG_DEREF(block, dst_reg, dst_reg); /*MOV dst_reg, [p]*/
    }
}

void
host_x86_MOV8_REG_ABS_REG_REG_SHIFT(codeblock_t *block, int dst_reg, uint32_t addr, int base_reg, int index_reg, int shift)
{
    /*MOV dst_reg, addr[base_reg + idx_reg << shift]*/
    emit_mem(block, 0, 0, 1, 1, 0x8a, dst_reg, base_reg, index_reg, shift, addr, (addr < 0x80 || addr >= 0xffffff80) ? DISP_8 : DISP_32, 0, 0);
}

void
host_x86_MOV8_REG_BASE_INDEX(codeblock_t *block, int dst_reg, int base_reg, int index_reg)
{
    emit_mem(block, 0, 0, 1, 1, 0x8a, dst_reg, base_reg, index_reg, 0, 0, DISP_AUTO, 0, 0); /*MOV dst_reg, B[base_reg + index_reg]*/
}
void
host_x86_MOV16_REG_BASE_INDEX(codeblock_t *block, int dst_reg, int base_reg, int index_reg)
{
    emit_mem(block, 0x66, 0, 0, 1, 0x8b, dst_reg, base_reg, index_reg, 0, 0, DISP_AUTO, 0, 0); /*MOV dst_reg, W[base_reg + index_reg]*/
}
void
host_x86_MOV32_REG_BASE_INDEX(codeblock_t *block, int dst_reg, int base_reg, int index_reg)
{
    emit_mem(block, 0, 0, 0, 1, 0x8b, dst_reg, base_reg, index_reg, 0, 0, DISP_AUTO, 0, 0); /*MOV dst_reg, L[base_reg + index_reg]*/
}

void
host_x86_MOV64_REG_BASE_INDEX_SHIFT(codeblock_t *block, int dst_reg, int base_reg, int index_reg, int scale)
{
    emit_mem(block, 0, 1, 0, 1, 0x8b, dst_reg, base_reg, index_reg, scale, 0, DISP_AUTO, 0, 0); /*MOV dst_reg, Q[base_reg + index_reg << scale]*/
}

void
host_x86_MOV16_REG_BASE_OFFSET(codeblock_t *block, int dst_reg, int base_reg, int offset)
{
    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0x66, 0, 0, 1, 0x8b, dst_reg, base_reg, NO_INDEX, 0, offset, DISP_8, 0, 0); /* MOV dst_reg, [base_reg + offset] */
    else
        fatal("MOV16_REG_BASE_OFFSET - offset %i\n", offset);
}
void
host_x86_MOV32_REG_BASE_OFFSET(codeblock_t *block, int dst_reg, int base_reg, int offset)
{
    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0, 0, 0, 1, 0x8b, dst_reg, base_reg, NO_INDEX, 0, offset, DISP_8, 0, 0); /* MOV dst_reg, [base_reg + offset] */
    else
        fatal("MOV32_REG_BASE_OFFSET - offset %i\n", offset);
}
/* MOV dst_reg, [base_reg]: any registers, REX as needed. */
void
host_x86_MOV64_REG_DEREF(codeblock_t *block, int dst_reg, int base_reg)
{
    emit_mem(block, 0, 1, 0, 1, 0x8b, dst_reg, base_reg, NO_INDEX, 0, 0, DISP_AUTO, 0, 0);
}

void
host_x86_MOV64_REG_BASE_OFFSET(codeblock_t *block, int dst_reg, int base_reg, int offset)
{
    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0, 1, 0, 1, 0x8b, dst_reg, base_reg, NO_INDEX, 0, offset, DISP_8, 0, 0); /* MOV dst_reg, [base_reg + offset] */
    else
        fatal("MOV32_REG_BASE_OFFSET - offset %i\n", offset);
}

void
host_x86_MOV32_BASE_OFFSET_REG(codeblock_t *block, int base_reg, int offset, int src_reg)
{
    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0, 0, 0, 1, 0x89, src_reg, base_reg, NO_INDEX, 0, offset, DISP_8, 0, 0); /* MOV [base_reg + offset], src_reg*/
    else
        fatal("MOV32_BASE_OFFSET_REG - offset %i\n", offset);
}
void
host_x86_MOV64_BASE_OFFSET_REG(codeblock_t *block, int base_reg, int offset, int src_reg)
{
    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0, 1, 0, 1, 0x89, src_reg, base_reg, NO_INDEX, 0, offset, DISP_8, 0, 0); /* MOV [base_reg + offset], src_reg*/
    else
        fatal("MOV64_BASE_OFFSET_REG - offset %i\n", offset);
}

void
host_x86_MOV32_BASE_OFFSET_IMM(codeblock_t *block, int base_reg, int offset, uint32_t imm_data)
{
    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0, 0, 0, 1, 0xc7, 0, base_reg, NO_INDEX, 0, offset, DISP_8, 4, imm_data); /* MOV [base_reg + offset], imm_data*/
    else
        fatal("MOV32_BASE_OFFSET_IMM - offset %i\n", offset);
}

void
host_x86_MOV8_REG_IMM(codeblock_t *block, int reg, uint16_t imm_data)
{
    emit_opreg(block, 0, 0, 1, 0xb0, reg, 1, imm_data); /*MOV reg, imm_data*/
}
void
host_x86_MOV16_REG_IMM(codeblock_t *block, int reg, uint16_t imm_data)
{
    emit_opreg(block, 0x66, 0, 0, 0xb8, reg, 2, imm_data); /*MOV reg, imm_data*/
}
void
host_x86_MOV32_REG_IMM(codeblock_t *block, int reg, uint32_t imm_data)
{
    emit_opreg(block, 0, 0, 0, 0xb8, reg, 4, imm_data); /*MOV reg, imm_data*/
}

void
host_x86_MOV64_REG_IMM(codeblock_t *block, int reg, uint64_t imm_data)
{
    emit_head(block, 0, rex_prefix(1, 0, 0, reg), 1, 0xb8 | (reg & 7), 8); /*MOVQ reg, imm_data*/
    codegen_addquad(block, imm_data);
}

void
host_x86_MOV8_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 0, 1, 1, 0x88, src_reg, dst_reg); /*MOV dst_reg, src_reg*/
}
void
host_x86_MOV16_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0x66, 0, 0, 1, 0x89, src_reg, dst_reg); /*MOV dst_reg, src_reg*/
}
void
host_x86_MOV32_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 0, 0, 1, 0x89, src_reg, dst_reg); /*MOV dst_reg, src_reg*/
}
void
host_x86_MOV64_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 1, 0, 1, 0x89, src_reg, dst_reg); /*MOV dst_reg, src_reg (64-bit)*/
}

void
host_x86_MOV32_STACK_IMM(codeblock_t *block, int32_t offset, uint32_t imm_data)
{
    if (!offset) {
        codegen_alloc_bytes(block, 7);
        codegen_addbyte3(block, 0xc7, 0x04, 0x24); /*MOV [ESP], imm_data*/
        codegen_addlong(block, imm_data);
    } else if (offset >= -0x80 && offset < 0x80) {
        codegen_alloc_bytes(block, 8);
        codegen_addbyte4(block, 0xc7, 0x44, 0x24, offset & 0xff); /*MOV offset[ESP], imm_data*/
        codegen_addlong(block, imm_data);
    } else {
        codegen_alloc_bytes(block, 11);
        codegen_addbyte3(block, 0xc7, 0x84, 0x24); /*MOV offset[ESP], imm_data*/
        codegen_addlong(block, offset);
        codegen_addlong(block, imm_data);
    }
}

/*The 8-bit sources of MOVSX/MOVZX are byte registers, so they are checked
  against the REX prefix like any other byte operand.*/
static void
emit_extend8(codeblock_t *block, uint8_t prefix, uint8_t op, int dst_reg, int src_reg)
{
    uint8_t rex = rex_prefix(0, dst_reg, 0, src_reg);

    check_byte_reg(rex, src_reg);
    emit_head(block, prefix, rex, 2, 0x0f00 | op, 1);
    codegen_addbyte(block, 0xc0 | ((dst_reg & 7) << 3) | (src_reg & 7));
}

void
host_x86_MOVSX_REG_16_8(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_extend8(block, 0x66, 0xbe, dst_reg, src_reg); /*MOVSX dst_reg, src_reg*/
}
void
host_x86_MOVSX_REG_32_8(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_extend8(block, 0, 0xbe, dst_reg, src_reg); /*MOVSX dst_reg, src_reg*/
}
void
host_x86_MOVSX_REG_32_16(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 0, 0, 2, 0x0fbf, dst_reg, src_reg); /*MOVSX dst_reg, src_reg*/
}

void
host_x86_MOVZX_BASE_INDEX_32_8(codeblock_t *block, int dst_reg, int base_reg, int index_reg)
{
    emit_mem(block, 0, 0, 0, 2, 0x0fb6, dst_reg, base_reg, index_reg, 0, 0, DISP_AUTO, 0, 0); /*MOVZX dst_reg, B[base_reg + index_reg]*/
}
void
host_x86_MOVZX_BASE_INDEX_32_16(codeblock_t *block, int dst_reg, int base_reg, int index_reg)
{
    emit_mem(block, 0, 0, 0, 2, 0x0fb7, dst_reg, base_reg, index_reg, 0, 0, DISP_AUTO, 0, 0); /*MOVZX dst_reg, W[base_reg + index_reg]*/
}

void
host_x86_MOVZX_REG_16_8(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_extend8(block, 0x66, 0xb6, dst_reg, src_reg); /*MOVZX dst_reg, src_reg*/
}
/*SETcc into the low byte of a register. SPL-DIL and R8B-R15B need a REX
  prefix, which also keeps 4-7 from meaning AH-BH*/
void
host_x86_SETcc_REG(codeblock_t *block, int cc, int dst_reg)
{
    uint8_t rex = (dst_reg >= 4) ? (0x40 | ((dst_reg & 8) ? 0x01 : 0)) : 0;

    emit_head(block, 0, rex, 2, 0x0f90 | (cc & 0xf), 1);
    codegen_addbyte(block, 0xc0 | (dst_reg & 7)); /*SETcc dst_reg*/
}
void
host_x86_IMUL32_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 0, 0, 2, 0x0faf, dst_reg, src_reg); /*IMUL dst_reg, src_reg*/
}
void
host_x86_IMUL64_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 1, 0, 2, 0x0faf, dst_reg, src_reg); /*IMUL dst_reg, src_reg (64-bit)*/
}
void
host_x86_MOVSXD_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 1, 0, 1, 0x63, dst_reg, src_reg); /*MOVSXD dst_reg, src_reg*/
}
void
host_x86_SHR64_IMM(codeblock_t *block, int dst_reg, int shift)
{
    emit_ext(block, 0, 1, 0, 1, 0xc1, RM_OP_SHR, dst_reg, 1, shift); /*SHR dst_reg, shift (64-bit)*/
}
void
host_x86_BSF32_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 0, 0, 2, 0x0fbc, dst_reg, src_reg); /*BSF dst_reg, src_reg*/
}
void
host_x86_BSR32_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 0, 0, 2, 0x0fbd, dst_reg, src_reg); /*BSR dst_reg, src_reg*/
}
void
host_x86_CMOVNZ32_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 0, 0, 2, 0x0f45, dst_reg, src_reg); /*CMOVNZ dst_reg, src_reg*/
}
void
host_x86_BSWAP32_REG(codeblock_t *block, int reg)
{
    emit_head(block, 0, (reg & 8) ? 0x41 : 0, 2, 0x0fc8 | (reg & 7), 0); /*BSWAP reg*/
}

void
host_x86_MOVZX_REG_32_8(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_extend8(block, 0, 0xb6, dst_reg, src_reg); /*MOVZX dst_reg, src_reg*/
}
void
host_x86_MOVZX_REG_32_16(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 0, 0, 2, 0x0fb7, dst_reg, src_reg); /*MOVZX dst_reg, src_reg*/
}

void
host_x86_MOVZX_REG_ABS_16_8(codeblock_t *block, int dst_reg, void *p)
{
    int64_t offset     = rbp_offset(p);
    int64_t ram_offset = (uintptr_t) p - (((uintptr_t) ram) + 2147483648ULL);

    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0x66, 0, 0, 2, 0x0fb6, dst_reg, REG_RBP, NO_INDEX, 0, offset, DISP_8, 0, 0); /*MOVZX dst_reg, offset[RBP]*/
    else if (fits_disp32(offset))
        emit_mem(block, 0x66, 0, 0, 2, 0x0fb6, dst_reg, REG_RBP, NO_INDEX, 0, offset, DISP_32, 0, 0); /*MOVZX dst_reg, offset[RBP]*/
    else if ((ram_offset >= -2147483648LL) && (ram_offset <= 2147483647LL) && (block->flags & CODEBLOCK_NO_IMMEDIATES))
        emit_mem(block, 0x66, 0, 0, 2, 0x0fb6, dst_reg, REG_R12, NO_INDEX, 0, ram_offset, DISP_32, 0, 0); /*MOVZX dst_reg, ram_offset[R12]*/
    else {
        host_x86_MOV64_REG_IMM(block, REG_R9, (uintptr_t) p);
        emit_mem(block, 0x66, 0, 0, 2, 0x0fb6, dst_reg, REG_R9, NO_INDEX, 0, 0, DISP_AUTO, 0, 0); /*MOVZX dst_reg, [r9]*/
    }
}
void
host_x86_MOVZX_REG_ABS_32_8(codeblock_t *block, int dst_reg, void *p)
{
    int64_t offset     = rbp_offset(p);
    int64_t ram_offset = (uintptr_t) p - (((uintptr_t) ram) + 2147483648ULL);

    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0, 0, 0, 2, 0x0fb6, dst_reg, REG_RBP, NO_INDEX, 0, offset, DISP_8, 0, 0); /*MOVZX dst_reg, offset[RBP]*/
    else if (fits_disp32(offset))
        emit_mem(block, 0, 0, 0, 2, 0x0fb6, dst_reg, REG_RBP, NO_INDEX, 0, offset, DISP_32, 0, 0); /*MOVZX dst_reg, offset[RBP]*/
    else if ((ram_offset >= -2147483648LL) && (ram_offset <= 2147483647LL) && (block->flags & CODEBLOCK_NO_IMMEDIATES))
        emit_mem(block, 0, 0, 0, 2, 0x0fb6, dst_reg, REG_R12, NO_INDEX, 0, ram_offset, DISP_32, 0, 0); /*MOVZX dst_reg, ram_offset[R12]*/
    else {
        host_x86_MOV64_REG_IMM(block, REG_R9, (uintptr_t) p);
        emit_mem(block, 0, 0, 0, 2, 0x0fb6, dst_reg, REG_R9, NO_INDEX, 0, 0, DISP_AUTO, 0, 0); /*MOVZX dst_reg, [r9]*/
    }
}
void
host_x86_MOVZX_REG_ABS_32_16(codeblock_t *block, int dst_reg, void *p)
{
    int64_t offset     = rbp_offset(p);
    int64_t ram_offset = (uintptr_t) p - (((uintptr_t) ram) + 2147483648ULL);

    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0, 0, 0, 2, 0x0fb7, dst_reg, REG_RBP, NO_INDEX, 0, offset, DISP_8, 0, 0); /*MOVZX dst_reg, offset[RBP]*/
    else if (fits_disp32(offset))
        emit_mem(block, 0, 0, 0, 2, 0x0fb7, dst_reg, REG_RBP, NO_INDEX, 0, offset, DISP_32, 0, 0); /*MOVZX dst_reg, offset[RBP]*/
    else if ((ram_offset >= -2147483648LL) && (ram_offset <= 2147483647LL) && (block->flags & CODEBLOCK_NO_IMMEDIATES))
        emit_mem(block, 0, 0, 0, 2, 0x0fb7, dst_reg, REG_R12, NO_INDEX, 0, ram_offset, DISP_32, 0, 0); /*MOVZX dst_reg, ram_offset[R12]*/
    else {
        host_x86_MOV64_REG_IMM(block, REG_R9, (uintptr_t) p);
        emit_mem(block, 0, 0, 0, 2, 0x0fb7, dst_reg, REG_R9, NO_INDEX, 0, 0, DISP_AUTO, 0, 0); /*MOVZX dst_reg, [r9]*/
    }
}

void
host_x86_NOP(codeblock_t *block)
{
    codegen_alloc_bytes(block, 1);
    codegen_addbyte(block, 0x90); /*NOP*/
}

void
host_x86_OR8_REG_IMM(codeblock_t *block, int dst_reg, uint8_t imm_data)
{
    emit_alu8_imm(block, 0x0c, RM_OP_OR, dst_reg, imm_data); /*OR dst_reg, imm_data*/
}
void
host_x86_OR16_REG_IMM(codeblock_t *block, int dst_reg, uint16_t imm_data)
{
    emit_alu16_imm(block, 0x0d, RM_OP_OR, dst_reg, imm_data); /*OR dst_reg, imm_data*/
}
void
host_x86_OR32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data)
{
    emit_alu32_imm(block, 0x0d, RM_OP_OR, dst_reg, imm_data); /*OR dst_reg, imm_data*/
}
void
host_x86_OR8_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 0, 1, 1, 0x08, src_reg, dst_reg); /*OR dst_reg, src_reg*/
}
void
host_x86_OR16_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0x66, 0, 0, 1, 0x09, src_reg, dst_reg); /*OR dst_reg, src_reg*/
}
void
host_x86_OR32_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 0, 0, 1, 0x09, src_reg, dst_reg); /*OR dst_reg, src_reg*/
}

void
host_x86_OR64_BASE_OFFSET_REG(codeblock_t *block, int base_reg, int offset, int src_reg)
{
    if (offset >= -128 && offset <= 127)
        emit_mem(block, 0, 1, 0, 1, 0x09, src_reg, base_reg, NO_INDEX, 0, offset, DISP_8, 0, 0); /*OR Q[base_reg + offset], src_reg*/
    else
        fatal("OR64_BASE_OFFSET_REG - offset %i\n", offset);
}
void
host_x86_OR64_BASE_INDEX_SHIFT_REG(codeblock_t *block, int base_reg, int index_reg, int scale, int src_reg)
{
    emit_mem(block, 0, 1, 0, 1, 0x09, src_reg, base_reg, index_reg, scale, 0, DISP_AUTO, 0, 0); /*OR Q[base_reg + index_reg << scale], src_reg*/
}

void
host_x86_POP(codeblock_t *block, int dst_reg)
{
    emit_opreg(block, 0, 0, 0, 0x58, dst_reg, 0, 0); /*POP reg*/
}

void
host_x86_PUSH(codeblock_t *block, int src_reg)
{
    emit_opreg(block, 0, 0, 0, 0x50, src_reg, 0, 0); /*PUSH reg*/
}

void
host_x86_RET(codeblock_t *block)
{
    codegen_alloc_bytes(block, 1);
    codegen_addbyte(block, 0xc3); /*RET*/
}

/*Shifts and rotates: by CL, or by an immediate.*/
static void
emit_shift_cl(codeblock_t *block, int size, int ext, int dst_reg)
{
    if (size == 8)
        emit_ext(block, 0, 0, 1, 1, 0xd2, ext, dst_reg, 0, 0);
    else
        emit_ext(block, (size == 16) ? 0x66 : 0, 0, 0, 1, 0xd3, ext, dst_reg, 0, 0);
}
static void
emit_shift_imm(codeblock_t *block, int size, int ext, int dst_reg, int shift)
{
    if (size == 8)
        emit_ext(block, 0, 0, 1, 1, 0xc0, ext, dst_reg, 1, shift);
    else
        emit_ext(block, (size == 16) ? 0x66 : 0, 0, 0, 1, 0xc1, ext, dst_reg, 1, shift);
}

void
host_x86_ROL8_CL(codeblock_t *block, int dst_reg)
{
    emit_shift_cl(block, 8, RM_OP_ROL, dst_reg); /*ROL dst_reg, CL*/
}
void
host_x86_ROL16_CL(codeblock_t *block, int dst_reg)
{
    emit_shift_cl(block, 16, RM_OP_ROL, dst_reg); /*ROL dst_reg, CL*/
}
void
host_x86_ROL32_CL(codeblock_t *block, int dst_reg)
{
    emit_shift_cl(block, 32, RM_OP_ROL, dst_reg); /*ROL dst_reg, CL*/
}

void
host_x86_ROL8_IMM(codeblock_t *block, int dst_reg, int shift)
{
    emit_shift_imm(block, 8, RM_OP_ROL, dst_reg, shift); /*ROL dst_reg, shift*/
}
void
host_x86_ROL16_IMM(codeblock_t *block, int dst_reg, int shift)
{
    emit_shift_imm(block, 16, RM_OP_ROL, dst_reg, shift); /*ROL dst_reg, shift*/
}
void
host_x86_ROL32_IMM(codeblock_t *block, int dst_reg, int shift)
{
    emit_shift_imm(block, 32, RM_OP_ROL, dst_reg, shift); /*ROL dst_reg, shift*/
}

void
host_x86_ROR8_CL(codeblock_t *block, int dst_reg)
{
    emit_shift_cl(block, 8, RM_OP_ROR, dst_reg); /*ROR dst_reg, CL*/
}
void
host_x86_ROR16_CL(codeblock_t *block, int dst_reg)
{
    emit_shift_cl(block, 16, RM_OP_ROR, dst_reg); /*ROR dst_reg, CL*/
}
void
host_x86_ROR32_CL(codeblock_t *block, int dst_reg)
{
    emit_shift_cl(block, 32, RM_OP_ROR, dst_reg); /*ROR dst_reg, CL*/
}

void
host_x86_ROR8_IMM(codeblock_t *block, int dst_reg, int shift)
{
    emit_shift_imm(block, 8, RM_OP_ROR, dst_reg, shift); /*ROR dst_reg, shift*/
}
void
host_x86_ROR16_IMM(codeblock_t *block, int dst_reg, int shift)
{
    emit_shift_imm(block, 16, RM_OP_ROR, dst_reg, shift); /*ROR dst_reg, shift*/
}
void
host_x86_ROR32_IMM(codeblock_t *block, int dst_reg, int shift)
{
    emit_shift_imm(block, 32, RM_OP_ROR, dst_reg, shift); /*ROR dst_reg, shift*/
}

void
host_x86_SAR8_CL(codeblock_t *block, int dst_reg)
{
    emit_shift_cl(block, 8, RM_OP_SAR, dst_reg); /*SAR dst_reg, CL*/
}
void
host_x86_SAR16_CL(codeblock_t *block, int dst_reg)
{
    emit_shift_cl(block, 16, RM_OP_SAR, dst_reg); /*SAR dst_reg, CL*/
}
void
host_x86_SAR32_CL(codeblock_t *block, int dst_reg)
{
    emit_shift_cl(block, 32, RM_OP_SAR, dst_reg); /*SAR dst_reg, CL*/
}

void
host_x86_SAR8_IMM(codeblock_t *block, int dst_reg, int shift)
{
    emit_shift_imm(block, 8, RM_OP_SAR, dst_reg, shift); /*SAR dst_reg, shift*/
}
void
host_x86_SAR16_IMM(codeblock_t *block, int dst_reg, int shift)
{
    emit_shift_imm(block, 16, RM_OP_SAR, dst_reg, shift); /*SAR dst_reg, shift*/
}
void
host_x86_SAR32_IMM(codeblock_t *block, int dst_reg, int shift)
{
    emit_shift_imm(block, 32, RM_OP_SAR, dst_reg, shift); /*SAR dst_reg, shift*/
}

void
host_x86_SHL8_CL(codeblock_t *block, int dst_reg)
{
    emit_shift_cl(block, 8, RM_OP_SHL, dst_reg); /*SHL dst_reg, CL*/
}
void
host_x86_SHL16_CL(codeblock_t *block, int dst_reg)
{
    emit_shift_cl(block, 16, RM_OP_SHL, dst_reg); /*SHL dst_reg, CL*/
}
void
host_x86_SHL32_CL(codeblock_t *block, int dst_reg)
{
    emit_shift_cl(block, 32, RM_OP_SHL, dst_reg); /*SHL dst_reg, CL*/
}
void
host_x86_SHL64_CL(codeblock_t *block, int dst_reg)
{
    emit_ext(block, 0, 1, 0, 1, 0xd3, RM_OP_SHL, dst_reg, 0, 0); /*SHL dst_reg, CL (64-bit)*/
}

void
host_x86_SHL8_IMM(codeblock_t *block, int dst_reg, int shift)
{
    emit_shift_imm(block, 8, RM_OP_SHL, dst_reg, shift); /*SHL dst_reg, shift*/
}
void
host_x86_SHL16_IMM(codeblock_t *block, int dst_reg, int shift)
{
    emit_shift_imm(block, 16, RM_OP_SHL, dst_reg, shift); /*SHL dst_reg, shift*/
}
void
host_x86_SHL32_IMM(codeblock_t *block, int dst_reg, int shift)
{
    emit_shift_imm(block, 32, RM_OP_SHL, dst_reg, shift); /*SHL dst_reg, shift*/
}

void
host_x86_SHR8_CL(codeblock_t *block, int dst_reg)
{
    emit_shift_cl(block, 8, RM_OP_SHR, dst_reg); /*SHR dst_reg, CL*/
}
void
host_x86_SHR16_CL(codeblock_t *block, int dst_reg)
{
    emit_shift_cl(block, 16, RM_OP_SHR, dst_reg); /*SHR dst_reg, CL*/
}
void
host_x86_SHR32_CL(codeblock_t *block, int dst_reg)
{
    emit_shift_cl(block, 32, RM_OP_SHR, dst_reg); /*SHR dst_reg, CL*/
}

void
host_x86_SHR8_IMM(codeblock_t *block, int dst_reg, int shift)
{
    emit_shift_imm(block, 8, RM_OP_SHR, dst_reg, shift); /*SHR dst_reg, shift*/
}
void
host_x86_SHR16_IMM(codeblock_t *block, int dst_reg, int shift)
{
    emit_shift_imm(block, 16, RM_OP_SHR, dst_reg, shift); /*SHR dst_reg, shift*/
}
void
host_x86_SHR32_IMM(codeblock_t *block, int dst_reg, int shift)
{
    emit_shift_imm(block, 32, RM_OP_SHR, dst_reg, shift); /*SHR dst_reg, shift*/
}

void
host_x86_SUB8_REG_IMM(codeblock_t *block, int dst_reg, uint8_t imm_data)
{
    emit_alu8_imm(block, 0x2c, RM_OP_SUB, dst_reg, imm_data); /*SUB dst_reg, imm_data*/
}
void
host_x86_SUB16_REG_IMM(codeblock_t *block, int dst_reg, uint16_t imm_data)
{
    emit_alu16_imm(block, 0x2d, RM_OP_SUB, dst_reg, imm_data); /*SUB dst_reg, imm_data*/
}
void
host_x86_SUB32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data)
{
    emit_alu32_imm(block, 0x2d, RM_OP_SUB, dst_reg, imm_data); /*SUB dst_reg, imm_data*/
}
void
host_x86_SUB64_REG_IMM(codeblock_t *block, int dst_reg, uint64_t imm_data)
{
    if (is_imm8(imm_data))
        emit_ext(block, 0, 1, 0, 1, 0x83, RM_OP_SUB, dst_reg, 1, imm_data & 0xff); /*SUB dst_reg, imm_data*/
    else
        fatal("SUB64_REG_IMM !is_imm8 %016" PRIx64 "\n", imm_data);
}
void
host_x86_SUB8_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 0, 1, 1, 0x28, src_reg, dst_reg); /*SUB dst_reg, src_reg*/
}
void
host_x86_SUB16_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0x66, 0, 0, 1, 0x29, src_reg, dst_reg); /*SUB dst_reg, src_reg*/
}
void
host_x86_SUB32_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 0, 0, 1, 0x29, src_reg, dst_reg); /*SUB dst_reg, src_reg*/
}

void
host_x86_TEST8_REG(codeblock_t *block, int src_host_reg, int dst_host_reg)
{
    emit_rr(block, 0, 0, 1, 1, 0x84, dst_host_reg, src_host_reg); /*TEST dst_host_reg, src_host_reg*/
}
void
host_x86_TEST16_REG(codeblock_t *block, int src_host_reg, int dst_host_reg)
{
    emit_rr(block, 0x66, 0, 0, 1, 0x85, dst_host_reg, src_host_reg); /*TEST dst_host_reg, src_host_reg*/
}
void
host_x86_TEST32_REG(codeblock_t *block, int src_reg, int dst_reg)
{
    emit_rr(block, 0, 0, 0, 1, 0x85, dst_reg, src_reg); /*TEST dst_host_reg, src_host_reg*/
}
void
host_x86_TEST64_REG(codeblock_t *block, int src_reg, int dst_reg)
{
    emit_rr(block, 0, 1, 0, 1, 0x85, dst_reg, src_reg); /*TEST dst_reg, src_reg (64-bit)*/
}
void
host_x86_TEST32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data)
{
    if (dst_reg == REG_EAX) {
        emit_head(block, 0, 0, 1, 0xa9, 4); /*TEST EAX, imm_data*/
        codegen_addlong(block, imm_data);
    } else
        emit_ext(block, 0, 0, 0, 1, 0xf7, 0, dst_reg, 4, imm_data); /*TEST dst_reg, imm_data*/
}

void
host_x86_XOR8_REG_IMM(codeblock_t *block, int dst_reg, uint8_t imm_data)
{
    emit_alu8_imm(block, 0x34, RM_OP_XOR, dst_reg, imm_data); /*XOR dst_reg, imm_data*/
}
void
host_x86_XOR16_REG_IMM(codeblock_t *block, int dst_reg, uint16_t imm_data)
{
    emit_alu16_imm(block, 0x35, RM_OP_XOR, dst_reg, imm_data); /*XOR dst_reg, imm_data*/
}
void
host_x86_XOR32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data)
{
    emit_alu32_imm(block, 0x35, RM_OP_XOR, dst_reg, imm_data); /*XOR dst_reg, imm_data*/
}
void
host_x86_XOR8_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 0, 1, 1, 0x30, src_reg, dst_reg); /*XOR dst_reg, src_reg*/
}
void
host_x86_XOR16_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0x66, 0, 0, 1, 0x31, src_reg, dst_reg); /*XOR dst_reg, src_reg*/
}
void
host_x86_XOR32_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    emit_rr(block, 0, 0, 0, 1, 0x31, src_reg, dst_reg); /*XOR dst_reg, src_reg*/
}

#endif
