#if defined i386 || defined __i386 || defined __i386__ || defined _X86_ || defined _M_IX86

#    include <stdint.h>
#    include <86box/86box.h>
#    include "cpu.h"
#    include <86box/mem.h>

#    include "codegen.h"
#    include "codegen_allocator.h"
#    include "codegen_backend.h"
#    include "codegen_backend_x86_defs.h"
#    include "codegen_backend_x86_ops_sse.h"
#    include "codegen_backend_x86_ops_helpers.h"

void
host_x86_ADDPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x0f, 0x58, 0xc0 | src_reg | (dst_reg << 3)); /*ADDPS dst_reg, src_reg*/
}
void
host_x86_ADDSD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0xf2, 0x0f, 0x58, 0xc0 | src_reg | (dst_reg << 3));
}

void
host_x86_CMPPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg, int type)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x0f, 0xc2, 0xc0 | src_reg | (dst_reg << 3), type); /*CMPPS dst_reg, src_reg, type*/
}

void
host_x86_COMISD_XREG_XREG(codeblock_t *block, int src_reg_a, int src_reg_b)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0x2e, 0xc0 | src_reg_b | (src_reg_a << 3));
}

void
host_x86_CVTDQ2PS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x0f, 0x5b, 0xc0 | src_reg | (dst_reg << 3)); /*CVTDQ2PS dst_reg, src_reg*/
}
void
host_x86_CVTPS2DQ_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0x5b, 0xc0 | src_reg | (dst_reg << 3)); /*CVTPS2DQ dst_reg, src_reg*/
}

void
host_x86_CVTSD2SI_REG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0xf2, 0x0f, 0x2d, 0xc0 | src_reg | (dst_reg << 3)); /*CVTSD2SI dst_reg, src_reg*/
}
void
host_x86_CVTSD2SS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0xf2, 0x0f, 0x5a, 0xc0 | src_reg | (dst_reg << 3)); /*CVTSD2SS dst_reg, src_reg*/
}

void
host_x86_CVTSI2SD_XREG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0xf2, 0x0f, 0x2a, 0xc0 | src_reg | (dst_reg << 3)); /*CVTSI2SD dst_reg, src_reg*/
}
void
host_x86_CVTSI2SS_XREG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0xf3, 0x0f, 0x2a, 0xc0 | src_reg | (dst_reg << 3)); /*CVTSI2SD dst_reg, src_reg*/
}

void
host_x86_CVTSS2SD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0xf3, 0x0f, 0x5a, 0xc0 | src_reg | (dst_reg << 3));
}
void
host_x86_CVTSS2SD_XREG_BASE_INDEX(codeblock_t *block, int dst_reg, int base_reg, int idx_reg)
{
    codegen_alloc_bytes(block, 5);
    codegen_addbyte4(block, 0xf3, 0x0f, 0x5a, 0x04 | (dst_reg << 3)); /*CVTSS2SD XMMx, [base_reg + idx_reg]*/
    codegen_addbyte(block, base_reg | (idx_reg << 3));
}

void
host_x86_DIVSD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0xf2, 0x0f, 0x5e, 0xc0 | src_reg | (dst_reg << 3)); /*DIVSD dst_reg, src_reg*/
}
void
host_x86_DIVSS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0xf3, 0x0f, 0x5e, 0xc0 | src_reg | (dst_reg << 3)); /*DIVSS dst_reg, src_reg*/
}

void
host_x86_LDMXCSR(codeblock_t *block, void *p)
{
    int offset = (uintptr_t) p - (((uintptr_t) &cpu_state) + 128);

    if (offset >= -128 && offset < 127) {
        codegen_alloc_bytes(block, 4);
        codegen_addbyte4(block, 0x0f, 0xae, 0x50 | REG_EBP, offset); /*LDMXCSR offset[EBP]*/
    } else {
        codegen_alloc_bytes(block, 7);
        codegen_addbyte3(block, 0x0f, 0xae, 0x15); /*LDMXCSR [p]*/
        codegen_addlong(block, (uint32_t) p);
    }
}

void
host_x86_MAXSD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0xf2, 0x0f, 0x5f, 0xc0 | src_reg | (dst_reg << 3)); /*MAXSD dst_reg, src_reg*/
}

void
host_x86_MOVD_BASE_INDEX_XREG(codeblock_t *block, int base_reg, int idx_reg, int src_reg)
{
    codegen_alloc_bytes(block, 5);
    codegen_addbyte4(block, 0x66, 0x0f, 0x7e, 0x04 | (src_reg << 3)); /*MOVD XMMx, [base_reg + idx_reg]*/
    codegen_addbyte(block, base_reg | (idx_reg << 3));
}
void
host_x86_MOVD_REG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0x7e, 0xc0 | dst_reg | (src_reg << 3));
}
void
host_x86_MOVD_XREG_BASE_INDEX(codeblock_t *block, int dst_reg, int base_reg, int idx_reg)
{
    codegen_alloc_bytes(block, 5);
    codegen_addbyte4(block, 0x66, 0x0f, 0x6e, 0x04 | (dst_reg << 3)); /*MOVD XMMx, [base_reg + idx_reg]*/
    codegen_addbyte(block, base_reg | (idx_reg << 3));
}
void
host_x86_MOVD_XREG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0x6e, 0xc0 | src_reg | (dst_reg << 3));
}

void
host_x86_MOVQ_ABS_XREG(codeblock_t *block, void *p, int src_reg)
{
    int offset = (uintptr_t) p - (((uintptr_t) &cpu_state) + 128);

    if (offset >= -128 && offset < 127) {
        codegen_alloc_bytes(block, 5);
        codegen_addbyte4(block, 0x66, 0x0f, 0xd6, 0x45 | (src_reg << 3)); /*MOVQ offset[EBP], src_reg*/
        codegen_addbyte(block, offset);
    } else {
        codegen_alloc_bytes(block, 8);
        codegen_addbyte4(block, 0x66, 0x0f, 0xd6, 0x05 | (src_reg << 3)); /*MOVQ [p], src_reg*/
        codegen_addlong(block, (uint32_t) p);
    }
}
void
host_x86_MOVQ_ABS_REG_REG_SHIFT_XREG(codeblock_t *block, uint32_t addr, int src_reg_a, int src_reg_b, int shift, int src_reg)
{
    if (addr < 0x80 || addr >= 0xffffff80) {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte3(block, 0x66, 0x0f, 0xd6); /*MOVQ addr[src_reg_a + src_reg_b << shift], XMMx*/
        codegen_addbyte3(block, 0x44 | (src_reg << 3), src_reg_a | (src_reg_b << 3) | (shift << 6), addr & 0xff);
    } else {
        codegen_alloc_bytes(block, 9);
        codegen_addbyte3(block, 0x66, 0x0f, 0xd6); /*MOVQ addr[src_reg_a + src_reg_b << shift], XMMx*/
        codegen_addbyte2(block, 0x84 | (src_reg << 3), src_reg_a | (src_reg_b << 3) | (shift << 6));
        codegen_addlong(block, addr);
    }
}
void
host_x86_MOVQ_BASE_INDEX_XREG(codeblock_t *block, int base_reg, int idx_reg, int src_reg)
{
    codegen_alloc_bytes(block, 5);
    codegen_addbyte4(block, 0x66, 0x0f, 0xd6, 0x04 | (src_reg << 3)); /*MOVQ XMMx, [base_reg + idx_reg]*/
    codegen_addbyte(block, base_reg | (idx_reg << 3));
}
void
host_x86_MOVQ_BASE_OFFSET_XREG(codeblock_t *block, int base_reg, int offset, int src_reg)
{
    if (offset >= -128 && offset < 127) {
        if (base_reg == REG_ESP) {
            codegen_alloc_bytes(block, 6);
            codegen_addbyte4(block, 0x66, 0x0f, 0xd6, 0x44 | (src_reg << 3)); /*MOVQ [ESP + offset], XMMx*/
            codegen_addbyte2(block, 0x24, offset);
        } else {
            codegen_alloc_bytes(block, 5);
            codegen_addbyte4(block, 0x66, 0x0f, 0xd6, 0x40 | base_reg | (src_reg << 3)); /*MOVQ [base_reg + offset], XMMx*/
            codegen_addbyte(block, offset);
        }
    } else
        fatal("MOVQ_BASE_OFFSET_XREG - offset %i\n", offset);
}
void
host_x86_MOVQ_STACK_OFFSET_XREG(codeblock_t *block, int offset, int src_reg)
{
    if (!offset) {
        codegen_alloc_bytes(block, 5);
        codegen_addbyte4(block, 0x66, 0x0f, 0xd6, 0x04 | (src_reg << 3)); /*MOVQ [ESP], src_reg*/
        codegen_addbyte(block, 0x24);
    } else if (offset >= -0x80 && offset < 0x80) {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte4(block, 0x66, 0x0f, 0xd6, 0x44 | (src_reg << 3)); /*MOVQ offset[ESP], src_reg*/
        codegen_addbyte2(block, 0x24, offset & 0xff);
    } else {
        codegen_alloc_bytes(block, 9);
        codegen_addbyte4(block, 0x66, 0x0f, 0xd6, 0x84 | (src_reg << 3)); /*MOVQ offset[ESP], src_reg*/
        codegen_addbyte(block, 0x24);
        codegen_addlong(block, offset);
    }
}

void
host_x86_MOVQ_XREG_ABS(codeblock_t *block, int dst_reg, void *p)
{
    int offset = (uintptr_t) p - (((uintptr_t) &cpu_state) + 128);

    if (offset >= -128 && offset < 127) {
        codegen_alloc_bytes(block, 5);
        codegen_addbyte4(block, 0xf3, 0x0f, 0x7e, 0x45 | (dst_reg << 3)); /*MOVQ offset[EBP], src_reg*/
        codegen_addbyte(block, offset);
    } else {
        codegen_alloc_bytes(block, 8);
        codegen_addbyte4(block, 0xf3, 0x0f, 0x7e, 0x05 | (dst_reg << 3)); /*MOVQ [p], src_reg*/
        codegen_addlong(block, (uint32_t) p);
    }
}
void
host_x86_MOVQ_XREG_ABS_REG_REG_SHIFT(codeblock_t *block, int dst_reg, uint32_t addr, int src_reg_a, int src_reg_b, int shift)
{
    if (addr < 0x80 || addr >= 0xffffff80) {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte3(block, 0xf3, 0x0f, 0x7e); /*MOVQ XMMx, addr[src_reg_a + src_reg_b << shift]*/
        codegen_addbyte3(block, 0x44 | (dst_reg << 3), src_reg_a | (src_reg_b << 3) | (shift << 6), addr & 0xff);
    } else {
        codegen_alloc_bytes(block, 9);
        codegen_addbyte3(block, 0xf3, 0x0f, 0x7e); /*MOVQ XMMx, addr[src_reg_a + src_reg_b << shift]*/
        codegen_addbyte2(block, 0x84 | (dst_reg << 3), src_reg_a | (src_reg_b << 3) | (shift << 6));
        codegen_addlong(block, addr);
    }
}
void
host_x86_MOVQ_XREG_BASE_INDEX(codeblock_t *block, int dst_reg, int base_reg, int idx_reg)
{
    codegen_alloc_bytes(block, 5);
    codegen_addbyte4(block, 0xf3, 0x0f, 0x7e, 0x04 | (dst_reg << 3)); /*MOVQ XMMx, [base_reg + idx_reg]*/
    codegen_addbyte(block, base_reg | (idx_reg << 3));
}
void
host_x86_MOVQ_XREG_BASE_OFFSET(codeblock_t *block, int dst_reg, int base_reg, int offset)
{
    if (offset >= -128 && offset < 127) {
        if (base_reg == REG_ESP) {
            codegen_alloc_bytes(block, 6);
            codegen_addbyte4(block, 0xf3, 0x0f, 0x7e, 0x44 | (dst_reg << 3)); /*MOVQ XMMx, [ESP + offset]*/
            codegen_addbyte2(block, 0x24, offset);
        } else {
            codegen_alloc_bytes(block, 5);
            codegen_addbyte4(block, 0xf3, 0x0f, 0x7e, 0x40 | base_reg | (dst_reg << 3)); /*MOVQ XMMx, [base_reg + offset]*/
            codegen_addbyte(block, offset);
        }
    } else
        fatal("MOVQ_REG_BASE_OFFSET - offset %i\n", offset);
}
void
host_x86_MOVQ_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0xf3, 0x0f, 0x7e, 0xc0 | src_reg | (dst_reg << 3)); /*MOVQ dst_reg, src_reg*/
}

void
host_x86_MAXPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x0f, 0x5f, 0xc0 | src_reg | (dst_reg << 3)); /*MAXPS dst_reg, src_reg*/
}
void
host_x86_MINPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x0f, 0x5d, 0xc0 | src_reg | (dst_reg << 3)); /*MINPS dst_reg, src_reg*/
}

void
host_x86_MULPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x0f, 0x59, 0xc0 | src_reg | (dst_reg << 3)); /*MULPS dst_reg, src_reg*/
}
void
host_x86_MULSD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0xf2, 0x0f, 0x59, 0xc0 | src_reg | (dst_reg << 3));
}

void
host_x86_PACKSSWB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 9);
    codegen_addbyte4(block, 0x66, 0x0f, 0x63, 0xc0 | src_reg | (dst_reg << 3)); /*PACKSSWB dst_reg, src_reg*/
    codegen_addbyte4(block, 0x66, 0x0f, 0x70, 0xc0 | dst_reg | (dst_reg << 3)); /*PSHUFD dst_reg, dst_reg, 0x88 (move bits 64-95 to 32-63)*/
    codegen_addbyte(block, 0x88);
}
void
host_x86_PACKSSDW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 9);
    codegen_addbyte4(block, 0x66, 0x0f, 0x6b, 0xc0 | src_reg | (dst_reg << 3)); /*PACKSSDW dst_reg, src_reg*/
    codegen_addbyte4(block, 0x66, 0x0f, 0x70, 0xc0 | dst_reg | (dst_reg << 3)); /*PSHUFD dst_reg, dst_reg, 0x88 (move bits 64-95 to 32-63)*/
    codegen_addbyte(block, 0x88);
}
void
host_x86_PACKUSWB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 9);
    codegen_addbyte4(block, 0x66, 0x0f, 0x67, 0xc0 | src_reg | (dst_reg << 3)); /*PACKUSWB dst_reg, src_reg*/
    codegen_addbyte4(block, 0x66, 0x0f, 0x70, 0xc0 | dst_reg | (dst_reg << 3)); /*PSHUFD dst_reg, dst_reg, 0x88 (move bits 64-95 to 32-63)*/
    codegen_addbyte(block, 0x88);
}

void
host_x86_PADDB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xfc, 0xc0 | src_reg | (dst_reg << 3)); /*PADDB dst_reg, src_reg*/
}
void
host_x86_PADDW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xfd, 0xc0 | src_reg | (dst_reg << 3)); /*PADDW dst_reg, src_reg*/
}
void
host_x86_PADDD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xfe, 0xc0 | src_reg | (dst_reg << 3)); /*PADDD dst_reg, src_reg*/
}
void
host_x86_PADDSB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xec, 0xc0 | src_reg | (dst_reg << 3)); /*PADDSB dst_reg, src_reg*/
}
void
host_x86_PADDSW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xed, 0xc0 | src_reg | (dst_reg << 3)); /*PADDSW dst_reg, src_reg*/
}
void
host_x86_PADDUSB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xdc, 0xc0 | src_reg | (dst_reg << 3)); /*PADDUSB dst_reg, src_reg*/
}
void
host_x86_PADDUSW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xdd, 0xc0 | src_reg | (dst_reg << 3)); /*PADDUSW dst_reg, src_reg*/
}

void
host_x86_PAND_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xdb, 0xc0 | src_reg | (dst_reg << 3)); /*PAND dst_reg, src_reg*/
}
void
host_x86_PANDN_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xdf, 0xc0 | src_reg | (dst_reg << 3)); /*PANDN dst_reg, src_reg*/
}
void
host_x86_POR_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xeb, 0xc0 | src_reg | (dst_reg << 3)); /*POR dst_reg, src_reg*/
}
void
host_x86_PXOR_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xef, 0xc0 | src_reg | (dst_reg << 3)); /*PXOR dst_reg, src_reg*/
}

void
host_x86_PCMPEQB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0x74, 0xc0 | src_reg | (dst_reg << 3)); /*PCMPEQB dst_reg, src_reg*/
}
void
host_x86_PCMPEQW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0x75, 0xc0 | src_reg | (dst_reg << 3)); /*PCMPEQW dst_reg, src_reg*/
}
void
host_x86_PCMPEQD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0x76, 0xc0 | src_reg | (dst_reg << 3)); /*PCMPEQD dst_reg, src_reg*/
}
void
host_x86_PCMPGTB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0x64, 0xc0 | src_reg | (dst_reg << 3)); /*PCMPGTB dst_reg, src_reg*/
}
void
host_x86_PCMPGTW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0x65, 0xc0 | src_reg | (dst_reg << 3)); /*PCMPGTW dst_reg, src_reg*/
}
void
host_x86_PCMPGTD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0x66, 0xc0 | src_reg | (dst_reg << 3)); /*PCMPGTD dst_reg, src_reg*/
}

void
host_x86_PMADDWD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xf5, 0xc0 | src_reg | (dst_reg << 3)); /*PMULLW dst_reg, src_reg*/
}
void
host_x86_PMULHW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xe5, 0xc0 | src_reg | (dst_reg << 3)); /*PMULLW dst_reg, src_reg*/
}
void
host_x86_PMULLW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xd5, 0xc0 | src_reg | (dst_reg << 3)); /*PMULLW dst_reg, src_reg*/
}

void
host_x86_PSHUFD_XREG_XREG_IMM(codeblock_t *block, int dst_reg, int src_reg, uint8_t shuffle)
{
    codegen_alloc_bytes(block, 5);
    codegen_addbyte4(block, 0x66, 0x0f, 0x70, 0xc0 | src_reg | (dst_reg << 3)); /*PSHUFD dst_reg, dst_reg, 0xee (move top 64-bits to low 64-bits)*/
    codegen_addbyte(block, shuffle);
}

void
host_x86_PSLLW_XREG_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 5);
    codegen_addbyte4(block, 0x66, 0x0f, 0x71, 0xc0 | 0x30 | dst_reg); /*PSLLW dst_reg, imm*/
    codegen_addbyte(block, shift);
}
void
host_x86_PSLLD_XREG_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 5);
    codegen_addbyte4(block, 0x66, 0x0f, 0x72, 0xc0 | 0x30 | dst_reg); /*PSLLD dst_reg, imm*/
    codegen_addbyte(block, shift);
}
void
host_x86_PSLLQ_XREG_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 5);
    codegen_addbyte4(block, 0x66, 0x0f, 0x73, 0xc0 | 0x30 | dst_reg); /*PSLLD dst_reg, imm*/
    codegen_addbyte(block, shift);
}
void
host_x86_PSRAW_XREG_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 5);
    codegen_addbyte4(block, 0x66, 0x0f, 0x71, 0xc0 | 0x20 | dst_reg); /*PSRAW dst_reg, imm*/
    codegen_addbyte(block, shift);
}
void
host_x86_PSRAD_XREG_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 5);
    codegen_addbyte4(block, 0x66, 0x0f, 0x72, 0xc0 | 0x20 | dst_reg); /*PSRAD dst_reg, imm*/
    codegen_addbyte(block, shift);
}
void
host_x86_PSRAQ_XREG_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 5);
    codegen_addbyte4(block, 0x66, 0x0f, 0x73, 0xc0 | 0x20 | dst_reg); /*PSRAD dst_reg, imm*/
    codegen_addbyte(block, shift);
}
void
host_x86_PSRLW_XREG_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 5);
    codegen_addbyte4(block, 0x66, 0x0f, 0x71, 0xc0 | 0x10 | dst_reg); /*PSRLW dst_reg, imm*/
    codegen_addbyte(block, shift);
}
void
host_x86_PSRLD_XREG_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 5);
    codegen_addbyte4(block, 0x66, 0x0f, 0x72, 0xc0 | 0x10 | dst_reg); /*PSRLD dst_reg, imm*/
    codegen_addbyte(block, shift);
}
void
host_x86_PSRLQ_XREG_IMM(codeblock_t *block, int dst_reg, int shift)
{
    codegen_alloc_bytes(block, 5);
    codegen_addbyte4(block, 0x66, 0x0f, 0x73, 0xc0 | 0x10 | dst_reg); /*PSRLD dst_reg, imm*/
    codegen_addbyte(block, shift);
}

void
host_x86_PSUBB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xf8, 0xc0 | src_reg | (dst_reg << 3)); /*PADDB dst_reg, src_reg*/
}
void
host_x86_PSUBW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xf9, 0xc0 | src_reg | (dst_reg << 3)); /*PADDW dst_reg, src_reg*/
}
void
host_x86_PSUBD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xfa, 0xc0 | src_reg | (dst_reg << 3)); /*PADDD dst_reg, src_reg*/
}
void
host_x86_PSUBSB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xe8, 0xc0 | src_reg | (dst_reg << 3)); /*PSUBSB dst_reg, src_reg*/
}
void
host_x86_PSUBSW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xe9, 0xc0 | src_reg | (dst_reg << 3)); /*PSUBSW dst_reg, src_reg*/
}
void
host_x86_PSUBUSB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xd8, 0xc0 | src_reg | (dst_reg << 3)); /*PSUBUSB dst_reg, src_reg*/
}
void
host_x86_PSUBUSW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0xd9, 0xc0 | src_reg | (dst_reg << 3)); /*PSUBUSW dst_reg, src_reg*/
}

void
host_x86_PUNPCKLBW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0x60, 0xc0 | src_reg | (dst_reg << 3)); /*PUNPCKLBW dst_reg, src_reg*/
}
void
host_x86_PUNPCKLWD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0x61, 0xc0 | src_reg | (dst_reg << 3)); /*PUNPCKLWD dst_reg, src_reg*/
}
void
host_x86_PUNPCKLDQ_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0x66, 0x0f, 0x62, 0xc0 | src_reg | (dst_reg << 3)); /*PUNPCKLDQ dst_reg, src_reg*/
}

void
host_x86_SQRTSD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0xf2, 0x0f, 0x51, 0xc0 | src_reg | (dst_reg << 3)); /*SQRTSD dst_reg, src_reg*/
}
void
host_x86_SQRTSS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0xf3, 0x0f, 0x51, 0xc0 | src_reg | (dst_reg << 3)); /*SQRTSS dst_reg, src_reg*/
}

void
host_x86_SUBPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x0f, 0x5c, 0xc0 | src_reg | (dst_reg << 3)); /*SUBPS dst_reg, src_reg*/
}
void
host_x86_SUBSD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 4);
    codegen_addbyte4(block, 0xf2, 0x0f, 0x5c, 0xc0 | src_reg | (dst_reg << 3));
}

void
host_x86_UNPCKLPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block, 0x0f, 0x14, 0xc0 | src_reg | (dst_reg << 3));
}

#endif
