#if defined i386 || defined __i386 || defined __i386__ || defined _X86_ || defined _M_IX86

#    include <stdint.h>
#    include <86box/86box.h>
#    include "cpu.h"
#    include <86box/mem.h>

#    include "codegen.h"
#    include "codegen_allocator.h"
#    include "codegen_backend.h"
#    include "codegen_backend_x86_defs.h"
#    include "codegen_backend_x86_ops_fpu.h"
#    include "codegen_backend_x86_ops_helpers.h"

void
host_x87_FILDq_BASE(codeblock_t *block, int base_reg)
{
    if (base_reg == REG_ESP) {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0xdf, 0x2c, 0x24); /*FILDq [ESP]*/
    } else {
        codegen_alloc_bytes(block, 2);
        codegen_addbyte2(block, 0xdf, 0x28 | base_reg); /*FILDq [base_reg]*/
    }
}
void
host_x87_FISTPq_BASE(codeblock_t *block, int base_reg)
{
    if (base_reg == REG_ESP) {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0xdf, 0x3c, 0x24); /*FISTPq [ESP]*/
    } else {
        codegen_alloc_bytes(block, 2);
        codegen_addbyte2(block, 0xdf, 0x38 | base_reg); /*FISTPq [base_reg]*/
    }
}
void
host_x87_FLDCW(codeblock_t *block, void *p)
{
    int offset = (uintptr_t) p - (((uintptr_t) &cpu_state) + 128);

    if (offset >= -128 && offset < 127) {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0xd9, 0x68 | REG_EBP, offset); /*FLDCW offset[EBP]*/
    } else {
        codegen_alloc_bytes(block, 6);
        codegen_addbyte2(block, 0xd9, 0x2d); /*FLDCW [p]*/
        codegen_addlong(block, (uint32_t) p);
    }
}
void
host_x87_FLDd_BASE(codeblock_t *block, int base_reg)
{
    if (base_reg == REG_ESP) {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0xdd, 0x04, 0x24); /*FILDq [ESP]*/
    } else {
        codegen_alloc_bytes(block, 2);
        codegen_addbyte2(block, 0xdd, 0x08 | base_reg); /*FILDq [base_reg]*/
    }
}
void
host_x87_FSTPd_BASE(codeblock_t *block, int base_reg)
{
    if (base_reg == REG_ESP) {
        codegen_alloc_bytes(block, 3);
        codegen_addbyte3(block, 0xdd, 0x1c, 0x24); /*FILDq [ESP]*/
    } else {
        codegen_alloc_bytes(block, 2);
        codegen_addbyte2(block, 0xdd, 0x18 | base_reg); /*FILDq [base_reg]*/
    }
}

#endif
