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
        [ACCREG_cycles] = {0, (uintptr_t) &(cycles)}
};

void codegen_accumulate(int acc_reg, int delta)
{
        acc_regs[acc_reg].count += delta;

#ifdef USE_ACYCS
	if ((acc_reg == ACCREG_cycles) && (delta != 0)) {
		if (delta == -1) {
			/* -delta = 1 */
			addbyte(0xff); /*inc dword ptr[&acycs]*/
			addbyte(0x05);
			addlong((uint32_t) (uintptr_t) &(acycs));
		} else if (delta == 1) {
			/* -delta = -1 */
			addbyte(0xff); /*dec dword ptr[&acycs]*/
			addbyte(0x0d);
			addlong((uint32_t) (uintptr_t) &(acycs));
		} else {
			addbyte(0x81); /*ADD $acc_regs[c].count,acc_regs[c].dest*/
			addbyte(0x05);
			addlong((uint32_t) (uintptr_t) &(acycs));
			addlong((uintptr_t) -delta);
		}
	}
#endif
}

void codegen_accumulate_flush(void)
{
	if (acc_regs[0].count) {
                /* To reduce the size of the generated code, we take advantage of
                   the fact that the target offset points to _cycles within cpu_state,
                   so we can just use our existing infrastracture for variables
                   relative to cpu_state. */
		addbyte(0x81); /*MOVL $acc_regs[0].count,(_cycles)*/
		addbyte(0x45);
		addbyte((uint8_t)cpu_state_offset(_cycles));
		addlong(acc_regs[0].count);
	}

	acc_regs[0].count = 0;
}

void codegen_accumulate_reset()
{
	acc_regs[0].count = 0;
}
