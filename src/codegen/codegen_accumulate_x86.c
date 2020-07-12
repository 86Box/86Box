#include <stdint.h>
#include <stdio.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

#include "codegen.h"
#include "codegen_accumulate.h"

static struct
{
        int count;
        uintptr_t dest_reg;
} acc_regs[] =
{
        [ACCREG_ins]    = {0, (uintptr_t) &(ins)},
        [ACCREG_cycles] = {0, (uintptr_t) &(cycles)}
};

void codegen_accumulate(int acc_reg, int delta)
{
        acc_regs[acc_reg].count += delta;

	if ((acc_reg == ACCREG_cycles) && (delta != 0)) {
		addbyte(0x81); /*ADD $acc_regs[c].count,acc_regs[c].dest*/
		addbyte(0x05);
		addlong((uint32_t) (uintptr_t) &(acycs));
		addlong((uintptr_t) -delta);
	}
}

void codegen_accumulate_flush(void)
{
        int c;
        
        for (c = 0; c < ACCREG_COUNT; c++)
        {
                if (acc_regs[c].count)
		{
                        addbyte(0x81); /*ADD $acc_regs[c].count,acc_regs[c].dest*/
                        addbyte(0x05);
                        addlong((uint32_t) acc_regs[c].dest_reg);
                        addlong(acc_regs[c].count);
                }

                acc_regs[c].count = 0;
        }
}

void codegen_accumulate_reset()
{
        int c;

        for (c = 0; c < ACCREG_COUNT; c++)
                acc_regs[c].count = 0;
}
