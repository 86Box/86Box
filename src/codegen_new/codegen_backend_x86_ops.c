#if defined i386 || defined __i386 || defined __i386__ || defined _X86_ || defined _M_IX86

#    include <stdint.h>
#    include <86box/86box.h>
#    include "cpu.h"
#    include <86box/mem.h>

#    include "codegen.h"
#    include "codegen_allocator.h"
#    include "codegen_backend.h"
#    include "codegen_backend_x86_defs.h"
#    include "codegen_backend_x86_ops.h"
#    include "codegen_backend_x86_ops_helpers.h"

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

void
host_x86_ADD32_REG_ABS(codeblock_t *block, int dst_reg, void *p)
{
    int offset = (uintptr_t) p - (((uintptr_t) &cpu_state) + 128);

    if (offset >= -128 && offset < 127) {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0x03, 0x45 | (dst_reg << 3), offset); /*MOV offset[EBP], src_reg*/
    } else {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte(block, 0x03); /*MOV [p], src_reg*/
        codegen_addbyte(block, 0x05 | (dst_reg << 3));
        codegen_addlong(block, (uint32_t) p);
    }
}

void
host_x86_ADD8_REG_IMM(codeblock_t *block, int dst_reg, uint8_t imm_data)
{
    if (dst_reg == REG_EAX) {
        codegen_alloc_bytes(block, 2);
        codegen_addbyte2(block, 0x04, imm_data); /*ADD AL, imm_data*/
    } else {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0x80, 0xc0 | RM_OP_ADD | dst_reg, imm_data); /*ADD dst_reg, imm_data*/
    }
}
void
host_x86_ADD16_REG_IMM(codeblock_t *block, int dst_reg, uint16_t imm_data)
{
    if (is_imm8(imm_data)) {
        codegen_alloc_bytes(block, 4);
        codegen_addbyte4(block, 0x66, 0x83, 0xc0 | RM_OP_ADD | dst_reg, imm_data & 0xff); /*ADD dst_reg, imm_data*/
    } else if (dst_reg == REG_EAX) {
        codegen_alloc_bytes(block, 4);
        codegen_addbyte2(block, 0x66, 0x05); /*ADD AX, imm_data*/
        codegen_addword(block, imm_data);
    } else {
        codegen_alloc_bytes(block, 5);
        codegen_addbyte3(block, 0x66, 0x81, 0xc0 | RM_OP_ADD | dst_reg); /*ADD dst_reg, imm_data*/
        codegen_addword(block, imm_data);
    }
}
void
host_x86_ADD32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data)
{
    if (is_imm8(imm_data)) {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0x83, 0xc0 | RM_OP_ADD | dst_reg, imm_data & 0xff); /*ADD dst_reg, imm_data*/
    } else if (dst_reg == REG_EAX) {
        codegen_alloc_bytes(block, 5);
        codegen_addbyte(block, 0x05); /*ADD EAX, imm_data*/
        codegen_addlong(block, imm_data);
    } else {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte2(block, 0x81, 0xc0 | RM_OP_ADD | dst_reg); /*ADD dst_reg, imm_data*/
        codegen_addlong(block, imm_data);
    }
}

void
host_x86_ADD8_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0x00, 0xc0 | dst_reg | (src_reg << 3)); /*ADD dst_reg, src_reg*/
}
void
host_x86_ADD16_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x66, 0x01, 0xc0 | dst_reg | (src_reg << 3)); /*ADD dst_reg, src_reg*/
}
void
host_x86_ADD32_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte2(block, 0x01, 0xc0 | dst_reg | (src_reg << 3)); /*ADD dst_reg, src_reg*/
}

void
host_x86_AND8_REG_IMM(codeblock_t *block, int dst_reg, uint8_t imm_data)
{
    if (dst_reg == REG_EAX) {
        codegen_alloc_bytes(block, 2);
        codegen_addbyte2(block, 0x24, imm_data); /*AND AL, imm_data*/
    } else {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0x80, 0xc0 | RM_OP_AND | dst_reg, imm_data); /*AND dst_reg, imm_data*/
    }
}
void
host_x86_AND16_REG_IMM(codeblock_t *block, int dst_reg, uint16_t imm_data)
{
    if (is_imm8(imm_data)) {
        codegen_alloc_bytes(block, 4);
        codegen_addbyte4(block, 0x66, 0x83, 0xc0 | RM_OP_AND | dst_reg, imm_data & 0xff); /*AND dst_reg, imm_data*/
    } else if (dst_reg == REG_EAX) {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte2(block, 0x66, 0x25); /*AND AX, imm_data*/
        codegen_addword(block, imm_data);
    } else {
        codegen_alloc_bytes(block, 5);
        codegen_addbyte3(block, 0x66, 0x81, 0xc0 | RM_OP_AND | dst_reg); /*AND dst_reg, imm_data*/
        codegen_addword(block, imm_data);
    }
}
void
host_x86_AND32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data)
{
    if (is_imm8(imm_data)) {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0x83, 0xc0 | RM_OP_AND | dst_reg, imm_data & 0xff); /*AND dst_reg, imm_data*/
    } else if (dst_reg == REG_EAX) {
        codegen_alloc_bytes(block, 5);
        codegen_addbyte(block, 0x25); /*AND EAX, imm_data*/
        codegen_addlong(block, imm_data);
    } else {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte2(block, 0x81, 0xc0 | RM_OP_AND | dst_reg); /*AND dst_reg, imm_data*/
        codegen_addlong(block, imm_data);
    }
}

void
host_x86_AND8_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0x20, 0xc0 | dst_reg | (src_reg << 3)); /*AND dst_reg, src_reg_b*/
}
void
host_x86_AND16_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x66, 0x21, 0xc0 | dst_reg | (src_reg << 3)); /*AND dst_reg, src_reg_b*/
}
void
host_x86_AND32_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0x21, 0xc0 | dst_reg | (src_reg << 3)); /*AND dst_reg, src_reg_b*/
}

void
host_x86_CALL(codeblock_t *block, void *p)
{
    codegen_alloc_bytes(block, 5);
    codegen_addbyte(block, 0xe8); /*CALL*/
    codegen_addlong(block, (uintptr_t) p - (uintptr_t) &block_write_data[block_pos + 4]);
}

void
host_x86_CMP16_REG_IMM(codeblock_t *block, int dst_reg, uint16_t imm_data)
{
    if (is_imm8(imm_data)) {
        codegen_alloc_bytes(block, 4);
        codegen_addbyte4(block, 0x66, 0x83, 0xc0 | RM_OP_CMP | dst_reg, imm_data & 0xff); /*CMP dst_reg, imm_data*/
    } else if (dst_reg == REG_EAX) {
        codegen_alloc_bytes(block, 4);
        codegen_addbyte2(block, 0x66, 0x3d); /*CMP AX, imm_data*/
        codegen_addword(block, imm_data);
    } else {
        codegen_alloc_bytes(block, 5);
        codegen_addbyte3(block, 0x66, 0x81, 0xc0 | RM_OP_CMP | dst_reg); /*CMP dst_reg, imm_data*/
        codegen_addword(block, imm_data);
    }
}
void
host_x86_CMP32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data)
{
    if (is_imm8(imm_data)) {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0x83, 0xc0 | RM_OP_CMP | dst_reg, imm_data & 0xff); /*CMP dst_reg, imm_data*/
    } else if (dst_reg == REG_EAX) {
        codegen_alloc_bytes(block, 5);
        codegen_addbyte(block, 0x3d); /*CMP EAX, imm_data*/
        codegen_addlong(block, imm_data);
    } else {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte2(block, 0x81, 0xc0 | RM_OP_CMP | dst_reg); /*CMP dst_reg, imm_data*/
        codegen_addlong(block, imm_data);
    }
}

void
host_x86_CMP8_REG_REG(codeblock_t *block, int src_reg_a, int src_reg_b)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0x38, 0xc0 | src_reg_a | (src_reg_b << 3)); /*CMP src_reg_a, src_reg_b*/
}
void
host_x86_CMP16_REG_REG(codeblock_t *block, int src_reg_a, int src_reg_b)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x66, 0x39, 0xc0 | src_reg_a | (src_reg_b << 3)); /*CMP src_reg_a, src_reg_b*/
}
void
host_x86_CMP32_REG_REG(codeblock_t *block, int src_reg_a, int src_reg_b)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0x39, 0xc0 | src_reg_a | (src_reg_b << 3)); /*CMP src_reg_a, src_reg_b*/
}

void
host_x86_INC32_ABS(codeblock_t *block, void *p)
{
    codegen_alloc_bytes(block, 6);
    codegen_addbyte2(block, 0xff, 0x05); /*INC p*/
    codegen_addlong(block, (uint32_t) p);
}

void
host_x86_JMP(codeblock_t *block, void *p)
{
    codegen_alloc_bytes(block, 5);
    codegen_addbyte(block, 0xe9); /*JMP*/
    codegen_addlong(block, (uintptr_t) p - (uintptr_t) &block_write_data[block_pos + 4]);
}
uint32_t *
host_x86_JMP_long(codeblock_t *block)
{
    codegen_alloc_bytes(block, 5);
    codegen_addbyte(block, 0xe9); /*JMP*/
    codegen_addlong(block, 0);
    return (uint32_t *) &block_write_data[block_pos - 4];
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
    if (offset) {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte2(block, 0x8d, 0x80 | (dst_reg << 3) | src_reg); /*LEA dst_reg, [offset+src_reg]*/
        codegen_addlong(block, offset);
    } else {
        codegen_alloc_bytes(block, 2);
        codegen_addbyte2(block, 0x8d, 0x00 | (dst_reg << 3) | src_reg); /*LEA dst_reg, [src_reg]*/
    }
}

void
host_x86_LEA_REG_REG(codeblock_t *block, int dst_reg, int src_reg_a, int src_reg_b)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x8d, 0x04 | (dst_reg << 3), (src_reg_b << 3) | src_reg_a); /*LEA dst_reg, [src_reg_a + src_reg_b]*/
}
void
host_x86_LEA_REG_REG_SHIFT(codeblock_t *block, int dst_reg, int src_reg_a, int src_reg_b, int shift)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x8d, 0x04 | (dst_reg << 3), (shift << 6) | (src_reg_b << 3) | src_reg_a); /*LEA dst_reg, [src_reg_a + src_reg_b * (1 << shift)]*/
}

void
host_x86_MOV8_ABS_IMM(codeblock_t *block, void *p, uint32_t imm_data)
{
    int offset = (uintptr_t) p - (((uintptr_t) &cpu_state) + 128);

    if (offset >= -128 && offset < 127) {
        codegen_alloc_bytes(block, 4);
        codegen_addbyte3(block, 0xc6, 0x45, offset); /*MOVB offset[EBP], imm_data*/
        codegen_addbyte(block, imm_data);
    } else {
        codegen_alloc_bytes(block, 7);
        codegen_addbyte2(block, 0xc6, 0x05); /*MOVB p, imm_data*/
        codegen_addlong(block, (uint32_t) p);
        codegen_addbyte(block, imm_data);
    }
}
void
host_x86_MOV16_ABS_IMM(codeblock_t *block, void *p, uint16_t imm_data)
{
    int offset = (uintptr_t) p - (((uintptr_t) &cpu_state) + 128);

    if (offset >= -128 && offset < 127) {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte4(block, 0x66, 0xc7, 0x45, offset); /*MOV offset[EBP], imm_data*/
        codegen_addword(block, imm_data);
    } else {
        codegen_alloc_bytes(block, 9);
        codegen_addbyte3(block, 0x66, 0xc7, 0x05); /*MOV p, imm_data*/
        codegen_addlong(block, (uint32_t) p);
        codegen_addword(block, imm_data);
    }
}
void
host_x86_MOV32_ABS_IMM(codeblock_t *block, void *p, uint32_t imm_data)
{
    int offset = (uintptr_t) p - (((uintptr_t) &cpu_state) + 128);

    if (offset >= -128 && offset < 127) {
        codegen_alloc_bytes(block, 7);
        codegen_addbyte3(block, 0xc7, 0x45, offset); /*MOV offset[EBP], imm_data*/
        codegen_addlong(block, imm_data);
    } else {
        codegen_alloc_bytes(block, 10);
        codegen_addbyte2(block, 0xc7, 0x05); /*MOV p, imm_data*/
        codegen_addlong(block, (uint32_t) p);
        codegen_addlong(block, imm_data);
    }
}

void
host_x86_MOV8_ABS_REG(codeblock_t *block, void *p, int src_reg)
{
    int offset = (uintptr_t) p - (((uintptr_t) &cpu_state) + 128);

    if (offset >= -128 && offset < 127) {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0x88, 0x45 | (src_reg << 3), offset); /*MOVB offset[EBP], src_reg*/
    } else {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte(block, 0x88); /*MOVB [p], src_reg*/
        codegen_addbyte(block, 0x05 | (src_reg << 3));
        codegen_addlong(block, (uint32_t) p);
    }
}
void
host_x86_MOV16_ABS_REG(codeblock_t *block, void *p, int src_reg)
{
    int offset = (uintptr_t) p - (((uintptr_t) &cpu_state) + 128);

    if (offset >= -128 && offset < 127) {
        codegen_alloc_bytes(block, 4);
        codegen_addbyte4(block, 0x66, 0x89, 0x45 | (src_reg << 3), offset); /*MOV offset[EBP], src_reg*/
    } else {
        codegen_alloc_bytes(block, 7);
        codegen_addbyte3(block, 0x66, 0x89, 0x05 | (src_reg << 3)); /*MOV [p], src_reg*/
        codegen_addlong(block, (uint32_t) p);
    }
}
void
host_x86_MOV32_ABS_REG(codeblock_t *block, void *p, int src_reg)
{
    int offset = (uintptr_t) p - (((uintptr_t) &cpu_state) + 128);

    if (offset >= -128 && offset < 127) {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0x89, 0x45 | (src_reg << 3), offset); /*MOV offset[EBP], src_reg*/
    } else {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte(block, 0x89); /*MOV [p], src_reg*/
        codegen_addbyte(block, 0x05 | (src_reg << 3));
        codegen_addlong(block, (uint32_t) p);
    }
}

void
host_x86_MOV8_ABS_REG_REG_SHIFT_REG(codeblock_t *block, uint32_t addr, int base_reg, int idx_reg, int shift, int src_reg)
{
    if (addr < 0x80 || addr >= 0xffffff80) {
        codegen_alloc_bytes(block, 4);
        codegen_addbyte4(block, 0x88, 0x44 | (src_reg << 3), base_reg | (idx_reg << 3) | (shift << 6), addr & 0xff); /*MOV addr[base_reg + idx_reg << shift], src_reg*/
    } else {
        codegen_alloc_bytes(block, 7);
        codegen_addbyte3(block, 0x88, 0x84 | (src_reg << 3), base_reg | (idx_reg << 3) | (shift << 6)); /*MOV addr[base_reg + idx_reg << shift], src_reg*/
        codegen_addlong(block, addr);
    }
}

void
host_x86_MOV8_BASE_INDEX_REG(codeblock_t *block, int base_reg, int idx_reg, int src_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x88, 0x04 | (src_reg << 3), base_reg | (idx_reg << 3)); /*MOV B[base_reg + idx_reg], src_reg*/
}
void
host_x86_MOV16_BASE_INDEX_REG(codeblock_t *block, int base_reg, int idx_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x89, 0x04 | (src_reg << 3), base_reg | (idx_reg << 3)); /*MOV W[base_reg + idx_reg], src_reg*/
}
void
host_x86_MOV32_BASE_INDEX_REG(codeblock_t *block, int base_reg, int idx_reg, int src_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x89, 0x04 | (src_reg << 3), base_reg | (idx_reg << 3)); /*MOV L[base_reg + idx_reg], src_reg*/
}

void
host_x86_MOV8_REG_ABS(codeblock_t *block, int dst_reg, void *p)
{
    int offset = (uintptr_t) p - (((uintptr_t) &cpu_state) + 128);

    if (offset >= -128 && offset < 127) {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0x8a, 0x45 | (dst_reg << 3), offset); /*MOV offset[EBP], src_reg*/
    } else {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte(block, 0x8a); /*MOV [p], src_reg*/
        codegen_addbyte(block, 0x05 | (dst_reg << 3));
        codegen_addlong(block, (uint32_t) p);
    }
}
void
host_x86_MOV16_REG_ABS(codeblock_t *block, int dst_reg, void *p)
{
    int offset = (uintptr_t) p - (((uintptr_t) &cpu_state) + 128);

    if (offset >= -128 && offset < 127) {
        codegen_alloc_bytes(block, 4);
        codegen_addbyte4(block, 0x66, 0x8b, 0x45 | (dst_reg << 3), offset); /*MOV offset[EBP], src_reg*/
    } else {
        codegen_alloc_bytes(block, 7);
        codegen_addbyte3(block, 0x66, 0x8b, 0x05 | (dst_reg << 3)); /*MOV [p], src_reg*/
        codegen_addlong(block, (uint32_t) p);
    }
}
void
host_x86_MOV32_REG_ABS(codeblock_t *block, int dst_reg, void *p)
{
    int offset = (uintptr_t) p - (((uintptr_t) &cpu_state) + 128);

    if (offset >= -128 && offset < 127) {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0x8b, 0x45 | (dst_reg << 3), offset); /*MOV offset[EBP], src_reg*/
    } else {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte(block, 0x8b); /*MOV [p], src_reg*/
        codegen_addbyte(block, 0x05 | (dst_reg << 3));
        codegen_addlong(block, (uint32_t) p);
    }
}

void
host_x86_MOV32_REG_ABS_INDEX_SHIFT(codeblock_t *block, int dst_reg, void *p, int idx_reg, int shift)
{
    codegen_alloc_bytes(block, 7);
    codegen_addbyte3(block, 0x8b, 0x04 | (dst_reg << 3), (shift << 6) | (idx_reg << 3) | 0x05); /*MOV dst_reg, [p + idx_reg << shift]*/
    codegen_addlong(block, (uint32_t) p);
}

void
host_x86_MOV8_REG_ABS_REG_REG_SHIFT(codeblock_t *block, int dst_reg, uint32_t addr, int base_reg, int idx_reg, int shift)
{
    if (addr < 0x80 || addr >= 0xffffff80) {
        codegen_alloc_bytes(block, 4);
        codegen_addbyte4(block, 0x8a, 0x44 | (dst_reg << 3), base_reg | (idx_reg << 3) | (shift << 6), addr & 0xff); /*MOV addr[base_reg + idx_reg << shift], src_reg*/
    } else {
        codegen_alloc_bytes(block, 7);
        codegen_addbyte3(block, 0x8a, 0x84 | (dst_reg << 3), base_reg | (idx_reg << 3) | (shift << 6)); /*MOV addr[base_reg + idx_reg << shift], src_reg*/
        codegen_addlong(block, addr);
    }
}

void
host_x86_MOV32_REG_BASE_INDEX(codeblock_t *block, int dst_reg, int base_reg, int idx_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x8b, 0x04 | (dst_reg << 3), base_reg | (idx_reg << 3)); /*MOV dst_reg, L[base_reg + idx_reg]*/
}

void
host_x86_MOV16_REG_BASE_OFFSET(codeblock_t *block, int dst_reg, int base_reg, int offset)
{
    if (offset >= -128 && offset < 127) {
        if (base_reg == REG_ESP) {
            codegen_alloc_bytes(block, 5);
            codegen_addbyte(block, 0x66);
            codegen_addbyte4(block, 0x8b, 0x40 | base_reg | (dst_reg << 3), 0x24, offset);
        } else {
            codegen_alloc_bytes(block, 4);
            codegen_addbyte4(block, 0x66, 0x8b, 0x40 | base_reg | (dst_reg << 3), offset);
        }
    } else
        fatal("MOV16_REG_BASE_OFFSET - offset %i\n", offset);
}
void
host_x86_MOV32_REG_BASE_OFFSET(codeblock_t *block, int dst_reg, int base_reg, int offset)
{
    if (offset >= -128 && offset < 127) {
        if (base_reg == REG_ESP) {
            codegen_alloc_bytes(block, 4);
            codegen_addbyte4(block, 0x8b, 0x40 | base_reg | (dst_reg << 3), 0x24, offset);
        } else {
            codegen_alloc_bytes(block, 3);
            codegen_addbyte3(block, 0x8b, 0x40 | base_reg | (dst_reg << 3), offset);
        }
    } else
        fatal("MOV32_REG_BASE_OFFSET - offset %i\n", offset);
}

void
host_x86_MOV16_BASE_OFFSET_REG(codeblock_t *block, int base_reg, int offset, int src_reg)
{
    if (offset >= -128 && offset < 127) {
        if (base_reg == REG_ESP) {
            codegen_alloc_bytes(block, 5);
            codegen_addbyte(block, 0x66);
            codegen_addbyte4(block, 0x89, 0x40 | base_reg | (src_reg << 3), 0x24, offset);
        } else {
            codegen_alloc_bytes(block, 4);
            codegen_addbyte4(block, 0x66, 0x89, 0x40 | base_reg | (src_reg << 3), offset);
        }
    } else
        fatal("MOV16_BASE_OFFSET_REG - offset %i\n", offset);
}
void
host_x86_MOV32_BASE_OFFSET_REG(codeblock_t *block, int base_reg, int offset, int src_reg)
{
    if (offset >= -128 && offset < 127) {
        if (base_reg == REG_ESP) {
            codegen_alloc_bytes(block, 4);
            codegen_addbyte4(block, 0x89, 0x40 | base_reg | (src_reg << 3), 0x24, offset);
        } else {
            codegen_alloc_bytes(block, 3);
            codegen_addbyte3(block, 0x89, 0x40 | base_reg | (src_reg << 3), offset);
        }
    } else
        fatal("MOV32_BASE_OFFSET_REG - offset %i\n", offset);
}

void
host_x86_MOV32_BASE_OFFSET_IMM(codeblock_t *block, int base_reg, int offset, uint32_t imm_data)
{
    if (offset >= -128 && offset < 127) {
        if (base_reg == REG_ESP) {
            codegen_alloc_bytes(block, 8);
            codegen_addbyte4(block, 0xc7, 0x40 | base_reg, 0x24, offset);
            codegen_addlong(block, imm_data);
        } else {
            codegen_alloc_bytes(block, 7);
            codegen_addbyte3(block, 0xc7, 0x40 | base_reg, offset);
            codegen_addlong(block, imm_data);
        }
    } else
        fatal("MOV32_BASE_OFFSET_IMM - offset %i\n", offset);
}

void
host_x86_MOV8_REG_IMM(codeblock_t *block, int dst_reg, uint8_t imm_data)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0xb0 + dst_reg, imm_data); /*MOV reg, imm_data*/
}
void
host_x86_MOV16_REG_IMM(codeblock_t *block, int dst_reg, uint16_t imm_data)
{
    if (!imm_data) {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0x66, 0x31, 0xc0 | dst_reg | (dst_reg << 3)); /*XOR dst_reg, dst_reg*/
    } else {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte2(block, 0x66, 0xb8 + dst_reg); /*MOV reg, imm_data*/
        codegen_addword(block, imm_data);
    }
}
void
host_x86_MOV32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data)
{
    if (!imm_data) {
        codegen_alloc_bytes(block, 2);
        codegen_addbyte2(block, 0x31, 0xc0 | dst_reg | (dst_reg << 3)); /*XOR dst_reg, dst_reg*/
    } else {
        codegen_alloc_bytes(block, 5);
        codegen_addbyte(block, 0xb8 + dst_reg); /*MOV reg, imm_data*/
        codegen_addlong(block, imm_data);
    }
}

void
host_x86_MOV8_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0x88, 0xc0 | dst_reg | (src_reg << 3));
}
void
host_x86_MOV16_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x66, 0x89, 0xc0 | dst_reg | (src_reg << 3));
}
void
host_x86_MOV32_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0x89, 0xc0 | dst_reg | (src_reg << 3));
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

void
host_x86_MOVSX_REG_16_8(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xbe, 0xc0 | (dst_reg << 3) | src_reg); /*MOVSX dst_reg, src_reg*/
}
void
host_x86_MOVSX_REG_32_8(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x0f, 0xbe, 0xc0 | (dst_reg << 3) | src_reg); /*MOVSX dst_reg, src_reg*/
}
void
host_x86_MOVSX_REG_32_16(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x0f, 0xbf, 0xc0 | (dst_reg << 3) | src_reg); /*MOVSX dst_reg, src_reg*/
}

void
host_x86_MOVZX_REG_ABS_16_8(codeblock_t *block, int dst_reg, void *p)
{
    int offset = (uintptr_t) p - (((uintptr_t) &cpu_state) + 128);

    if (offset >= -128 && offset < 127) {
        codegen_alloc_bytes(block, 5);
        codegen_addbyte(block, 0x66);
        codegen_addbyte4(block, 0x0f, 0xb6, 0x45 | (dst_reg << 3), offset); /*MOV dest_reg, [EBP+offset]*/
    } else {
        codegen_alloc_bytes(block, 8);
        codegen_addbyte4(block, 0x66, 0x0f, 0xb6, 0x05 | (dst_reg << 3)); /*MOVZX dst_reg, [p]*/
        codegen_addlong(block, (uint32_t) p);
    }
}
void
host_x86_MOVZX_REG_ABS_32_8(codeblock_t *block, int dst_reg, void *p)
{
    int offset = (uintptr_t) p - (((uintptr_t) &cpu_state) + 128);

    if (offset >= -128 && offset < 127) {
        codegen_alloc_bytes(block, 4);
        codegen_addbyte4(block, 0x0f, 0xb6, 0x45 | (dst_reg << 3), offset); /*MOV dest_reg, [EBP+offset]*/
    } else {
        codegen_alloc_bytes(block, 7);
        codegen_addbyte3(block, 0x0f, 0xb6, 0x05 | (dst_reg << 3)); /*MOVZX dst_reg, [p]*/
        codegen_addlong(block, (uint32_t) p);
    }
}
void
host_x86_MOVZX_REG_ABS_32_16(codeblock_t *block, int dst_reg, void *p)
{
    int offset = (uintptr_t) p - (((uintptr_t) &cpu_state) + 128);

    if (offset >= -128 && offset < 127) {
        codegen_alloc_bytes(block, 4);
        codegen_addbyte4(block, 0x0f, 0xb7, 0x45 | (dst_reg << 3), offset); /*MOV dest_reg, [EBP+offset]*/
    } else {
        codegen_alloc_bytes(block, 7);
        codegen_addbyte3(block, 0x0f, 0xb7, 0x05 | (dst_reg << 3)); /*MOVZX dst_reg, [p]*/
        codegen_addlong(block, (uint32_t) p);
    }
}

void
host_x86_MOVZX_REG_16_8(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xb6, 0xc0 | (dst_reg << 3) | src_reg); /*MOVZX dst_reg, src_reg*/
}
void
host_x86_MOVZX_REG_32_8(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x0f, 0xb6, 0xc0 | (dst_reg << 3) | src_reg); /*MOVZX dst_reg, src_reg*/
}
void
host_x86_MOVZX_REG_32_16(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x0f, 0xb7, 0xc0 | (dst_reg << 3) | src_reg); /*MOVZX dst_reg, src_reg*/
}

void
host_x86_MOVZX_BASE_INDEX_32_8(codeblock_t *block, int dst_reg, int base_reg, int idx_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x0f, 0xb6, 0x04 | (dst_reg << 3), base_reg | (idx_reg << 3)); /*MOVZX dst_reg, B[base_reg + idx_reg]*/
}
void
host_x86_MOVZX_BASE_INDEX_32_16(codeblock_t *block, int dst_reg, int base_reg, int idx_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x0f, 0xb7, 0x04 | (dst_reg << 3), base_reg | (idx_reg << 3)); /*MOVZX dst_reg, W[base_reg + idx_reg]*/
}

void
host_x86_OR8_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0x08, 0xc0 | dst_reg | (src_reg << 3)); /*OR dst_reg, src_reg_b*/
}
void
host_x86_OR16_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x66, 0x09, 0xc0 | dst_reg | (src_reg << 3)); /*OR dst_reg, src_reg_b*/
}
void
host_x86_OR32_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0x09, 0xc0 | dst_reg | (src_reg << 3)); /*OR dst_reg, src_reg_b*/
}

void
host_x86_OR8_REG_IMM(codeblock_t *block, int dst_reg, uint8_t imm_data)
{
    if (dst_reg == REG_EAX) {
        codegen_alloc_bytes(block, 2);
        codegen_addbyte2(block, 0x0c, imm_data); /*OR AL, imm_data*/
    } else {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0x80, 0xc0 | RM_OP_OR | dst_reg, imm_data); /*OR dst_reg, imm_data*/
    }
}
void
host_x86_OR16_REG_IMM(codeblock_t *block, int dst_reg, uint16_t imm_data)
{
    if (is_imm8(imm_data)) {
        codegen_alloc_bytes(block, 4);
        codegen_addbyte4(block, 0x66, 0x83, 0xc0 | RM_OP_OR | dst_reg, imm_data & 0xff); /*OR dst_reg, imm_data*/
    } else if (dst_reg == REG_EAX) {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte2(block, 0x66, 0x0d); /*OR AX, imm_data*/
        codegen_addword(block, imm_data);
    } else {
        codegen_alloc_bytes(block, 7);
        codegen_addbyte3(block, 0x66, 0x81, 0xc0 | RM_OP_OR | dst_reg); /*OR dst_reg, imm_data*/
        codegen_addword(block, imm_data);
    }
}
void
host_x86_OR32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data)
{
    if (is_imm8(imm_data)) {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0x83, 0xc0 | RM_OP_OR | dst_reg, imm_data & 0xff); /*OR dst_reg, imm_data*/
    } else if (dst_reg == REG_EAX) {
        codegen_alloc_bytes(block, 5);
        codegen_addbyte(block, 0x0d); /*OR EAX, imm_data*/
        codegen_addlong(block, imm_data);
    } else {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte2(block, 0x81, 0xc0 | RM_OP_OR | dst_reg); /*OR dst_reg, imm_data*/
        codegen_addlong(block, imm_data);
    }
}

void
host_x86_POP(codeblock_t *block, int src_reg)
{
    codegen_alloc_bytes(block, 1);
    codegen_addbyte(block, 0x58 | src_reg); /*POP reg*/
}

void
host_x86_PUSH(codeblock_t *block, int src_reg)
{
    codegen_alloc_bytes(block, 1);
    codegen_addbyte(block, 0x50 | src_reg); /*PUSH reg*/
}

void
host_x86_RET(codeblock_t *block)
{
    codegen_alloc_bytes(block, 1);
    codegen_addbyte(block, 0xc3); /*RET*/
}

void
host_x86_ROL8_CL(codeblock_t *block, int dst_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0xd2, 0xc0 | RM_OP_ROL | dst_reg); /*SHL dst_reg, CL*/
}
void
host_x86_ROL16_CL(codeblock_t *block, int dst_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x66, 0xd3, 0xc0 | RM_OP_ROL | dst_reg); /*SHL dst_reg, CL*/
}
void
host_x86_ROL32_CL(codeblock_t *block, int dst_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0xd3, 0xc0 | RM_OP_ROL | dst_reg); /*SHL dst_reg, CL*/
}

void
host_x86_ROL8_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0xc0, 0xc0 | RM_OP_ROL | dst_reg, shift); /*SHL dst_reg, shift*/
}
void
host_x86_ROL16_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0xc1, 0xc0 | RM_OP_ROL | dst_reg, shift); /*SHL dst_reg, shift*/
}
void
host_x86_ROL32_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0xc1, 0xc0 | RM_OP_ROL | dst_reg, shift); /*SHL dst_reg, shift*/
}

void
host_x86_ROR8_CL(codeblock_t *block, int dst_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0xd2, 0xc0 | RM_OP_ROR | dst_reg); /*SHR dst_reg, CL*/
}
void
host_x86_ROR16_CL(codeblock_t *block, int dst_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x66, 0xd3, 0xc0 | RM_OP_ROR | dst_reg); /*SHR dst_reg, CL*/
}
void
host_x86_ROR32_CL(codeblock_t *block, int dst_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0xd3, 0xc0 | RM_OP_ROR | dst_reg); /*SHR dst_reg, CL*/
}

void
host_x86_ROR8_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0xc0, 0xc0 | RM_OP_ROR | dst_reg, shift); /*SHR dst_reg, shift*/
}
void
host_x86_ROR16_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0xc1, 0xc0 | RM_OP_ROR | dst_reg, shift); /*SHR dst_reg, shift*/
}
void
host_x86_ROR32_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0xc1, 0xc0 | RM_OP_ROR | dst_reg, shift); /*SHR dst_reg, shift*/
}

#    define MODRM_MOD_REG(rm, reg) (0xc0 | reg | (rm << 3))

void
host_x86_SAR8_CL(codeblock_t *block, int dst_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0xd2, 0xc0 | RM_OP_SAR | dst_reg); /*SAR dst_reg, CL*/
}
void
host_x86_SAR16_CL(codeblock_t *block, int dst_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x66, 0xd3, 0xc0 | RM_OP_SAR | dst_reg); /*SAR dst_reg, CL*/
}
void
host_x86_SAR32_CL(codeblock_t *block, int dst_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0xd3, 0xc0 | RM_OP_SAR | dst_reg); /*SAR dst_reg, CL*/
}

void
host_x86_SAR8_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0xc0, 0xc0 | RM_OP_SAR | dst_reg, shift); /*SAR dst_reg, shift*/
}
void
host_x86_SAR16_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0xc1, 0xc0 | RM_OP_SAR | dst_reg, shift); /*SAR dst_reg, shift*/
}
void
host_x86_SAR32_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0xc1, 0xc0 | RM_OP_SAR | dst_reg, shift); /*SAR dst_reg, shift*/
}

void
host_x86_SHL8_CL(codeblock_t *block, int dst_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0xd2, 0xc0 | RM_OP_SHL | dst_reg); /*SHL dst_reg, CL*/
}
void
host_x86_SHL16_CL(codeblock_t *block, int dst_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x66, 0xd3, 0xc0 | RM_OP_SHL | dst_reg); /*SHL dst_reg, CL*/
}
void
host_x86_SHL32_CL(codeblock_t *block, int dst_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0xd3, 0xc0 | RM_OP_SHL | dst_reg); /*SHL dst_reg, CL*/
}

void
host_x86_SHL8_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0xc0, 0xc0 | RM_OP_SHL | dst_reg, shift); /*SHL dst_reg, shift*/
}
void
host_x86_SHL16_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0xc1, 0xc0 | RM_OP_SHL | dst_reg, shift); /*SHL dst_reg, shift*/
}
void
host_x86_SHL32_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0xc1, 0xc0 | RM_OP_SHL | dst_reg, shift); /*SHL dst_reg, shift*/
}

void
host_x86_SHR8_CL(codeblock_t *block, int dst_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0xd2, 0xc0 | RM_OP_SHR | dst_reg); /*SHR dst_reg, CL*/
}
void
host_x86_SHR16_CL(codeblock_t *block, int dst_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x66, 0xd3, 0xc0 | RM_OP_SHR | dst_reg); /*SHR dst_reg, CL*/
}
void
host_x86_SHR32_CL(codeblock_t *block, int dst_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0xd3, 0xc0 | RM_OP_SHR | dst_reg); /*SHR dst_reg, CL*/
}

void
host_x86_SHR8_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0xc0, 0xc0 | RM_OP_SHR | dst_reg, shift); /*SHR dst_reg, shift*/
}
void
host_x86_SHR16_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0xc1, 0xc0 | RM_OP_SHR | dst_reg, shift); /*SHR dst_reg, shift*/
}
void
host_x86_SHR32_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0xc1, 0xc0 | RM_OP_SHR | dst_reg, shift); /*SHR dst_reg, shift*/
}

void
host_x86_SUB8_REG_IMM(codeblock_t *block, int dst_reg, uint8_t imm_data)
{
    if (dst_reg == REG_EAX) {
        codegen_alloc_bytes(block, 2);
        codegen_addbyte2(block, 0x2c, imm_data); /*SUB AL, imm_data*/
    } else {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0x80, 0xc0 | RM_OP_SUB | dst_reg, imm_data); /*SUB dst_reg, imm_data*/
    }
}
void
host_x86_SUB16_REG_IMM(codeblock_t *block, int dst_reg, uint16_t imm_data)
{
    if (is_imm8(imm_data)) {
        codegen_alloc_bytes(block, 4);
        codegen_addbyte4(block, 0x66, 0x83, 0xc0 | RM_OP_SUB | dst_reg, imm_data & 0xff); /*SUB dst_reg, imm_data*/
    } else if (dst_reg == REG_EAX) {
        codegen_alloc_bytes(block, 4);
        codegen_addbyte2(block, 0x66, 0x2d); /*SUB AX, imm_data*/
        codegen_addword(block, imm_data);
    } else {
        codegen_alloc_bytes(block, 5);
        codegen_addbyte3(block, 0x66, 0x81, 0xc0 | RM_OP_SUB | dst_reg); /*SUB dst_reg, imm_data*/
        codegen_addword(block, imm_data);
    }
}
void
host_x86_SUB32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data)
{
    if (is_imm8(imm_data)) {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0x83, 0xc0 | RM_OP_SUB | dst_reg, imm_data & 0xff); /*SUB dst_reg, imm_data*/
    } else if (dst_reg == REG_EAX) {
        codegen_alloc_bytes(block, 5);
        codegen_addbyte(block, 0x2d); /*SUB EAX, imm_data*/
        codegen_addlong(block, imm_data);
    } else {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte2(block, 0x81, 0xc0 | RM_OP_SUB | dst_reg); /*SUB dst_reg, imm_data*/
        codegen_addlong(block, imm_data);
    }
}

void
host_x86_SUB8_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0x28, 0xc0 | dst_reg | (src_reg << 3)); /*SUB dst_reg, src_reg*/
}
void
host_x86_SUB16_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x66, 0x29, 0xc0 | dst_reg | (src_reg << 3)); /*SUB dst_reg, src_reg*/
}
void
host_x86_SUB32_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0x29, 0xc0 | dst_reg | (src_reg << 3)); /*SUB dst_reg, src_reg*/
}

void
host_x86_TEST8_REG(codeblock_t *block, int src_host_reg, int dst_host_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0x84, MODRM_MOD_REG(dst_host_reg, src_host_reg)); /*TEST dst_host_reg, src_host_reg*/
}
void
host_x86_TEST16_REG(codeblock_t *block, int src_host_reg, int dst_host_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x66, 0x85, MODRM_MOD_REG(dst_host_reg, src_host_reg)); /*TEST dst_host_reg, src_host_reg*/
}
void
host_x86_TEST32_REG(codeblock_t *block, int src_host_reg, int dst_host_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0x85, MODRM_MOD_REG(dst_host_reg, src_host_reg)); /*TEST dst_host_reg, src_host_reg*/
}
void
host_x86_TEST32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data)
{
    if (dst_reg == REG_EAX) {
        codegen_alloc_bytes(block, 5);
        codegen_addbyte(block, 0xa9); /*TEST EAX, imm_data*/
        codegen_addlong(block, imm_data);
    } else {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte2(block, 0xf7, 0xc0 | dst_reg); /*TEST dst_reg, imm_data*/
        codegen_addlong(block, imm_data);
    }
}

void
host_x86_XOR8_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0x30, 0xc0 | dst_reg | (src_reg << 3)); /*XOR dst_reg, src_reg*/
}
void
host_x86_XOR16_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x66, 0x31, 0xc0 | dst_reg | (src_reg << 3)); /*XOR dst_reg, src_reg*/
}
void
host_x86_XOR32_REG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 2);
    codegen_addbyte2(block, 0x31, 0xc0 | dst_reg | (src_reg << 3)); /*XOR dst_reg, src_reg*/
}

void
host_x86_XOR8_REG_IMM(codeblock_t *block, int dst_reg, uint8_t imm_data)
{
    if (dst_reg == REG_EAX) {
        codegen_alloc_bytes(block, 2);
        codegen_addbyte2(block, 0x34, imm_data); /*XOR AL, imm_data*/
    } else {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0x80, 0xc0 | RM_OP_XOR | dst_reg, imm_data); /*XOR dst_reg, imm_data*/
    }
}
void
host_x86_XOR16_REG_IMM(codeblock_t *block, int dst_reg, uint16_t imm_data)
{
    if (is_imm8(imm_data)) {
        codegen_alloc_bytes(block, 4);
        codegen_addbyte4(block, 0x66, 0x83, 0xc0 | RM_OP_XOR | dst_reg, imm_data & 0xff); /*XOR dst_reg, imm_data*/
    } else if (dst_reg == REG_EAX) {
        codegen_alloc_bytes(block, 4);
        codegen_addbyte2(block, 0x66, 0x35); /*XOR AX, imm_data*/
        codegen_addword(block, imm_data);
    } else {
        codegen_alloc_bytes(block, 5);
        codegen_addbyte3(block, 0x66, 0x81, 0xc0 | RM_OP_XOR | dst_reg); /*XOR dst_reg, imm_data*/
        codegen_addword(block, imm_data);
    }
}
void
host_x86_XOR32_REG_IMM(codeblock_t *block, int dst_reg, uint32_t imm_data)
{
    if (is_imm8(imm_data)) {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0x83, 0xc0 | RM_OP_XOR | dst_reg, imm_data & 0xff); /*XOR dst_reg, imm_data*/
    } else if (dst_reg == REG_EAX) {
        codegen_alloc_bytes(block, 5);
        codegen_addbyte(block, 0x35); /*XOR EAX, imm_data*/
        codegen_addlong(block, imm_data);
    } else {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte2(block, 0x81, 0xc0 | RM_OP_XOR | dst_reg); /*XOR dst_reg, imm_data*/
        codegen_addlong(block, imm_data);
    }
}

#endif
