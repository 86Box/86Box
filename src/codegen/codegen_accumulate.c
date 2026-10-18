#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "codegen.h"
#include "codegen_accumulate.h"
#include "codegen_ir.h"

static struct {
    int count;
    int dest_reg;
} acc_regs[] = {
    [ACCREG_cycles] = {0, IREG_cycles}
};

void
codegen_accumulate(UNUSED(ir_data_t *ir), int acc_reg, int delta)
{
    acc_regs[acc_reg].count += delta;

#ifdef USE_ACYCS
    if ((acc_reg == ACCREG_cycles) && (delta != 0)) {
        uop_ADD_IMM(ir, IREG_acycs, IREG_acycs, -delta);
    }
#endif
}

void
codegen_accumulate_flush(ir_data_t *ir)
{
    if (acc_regs[0].count) {
        uop_ADD_IMM(ir, acc_regs[0].dest_reg, acc_regs[0].dest_reg, acc_regs[0].count);
    }

    acc_regs[0].count = 0;
}

void
codegen_accumulate_reset(void)
{
    acc_regs[0].count = 0;
}
