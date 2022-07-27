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
        [ACCREG_cycles] = {0, (uintptr_t) &(cycles)},
};

void codegen_accumulate(int acc_reg, int delta)
{
        acc_regs[acc_reg].count += delta;

#ifdef USE_ACYCS
	if ((acc_reg == ACCREG_cycles) && (delta != 0)) {
		if (delta == -1) {
			/* -delta = 1 */
			addbyte(0xff); /*inc dword ptr[&acycs]*/
			addbyte(0x04);
			addbyte(0x25);
			addlong((uint32_t) (uintptr_t) &(acycs));
		} else if (delta == 1) {
			/* -delta = -1 */
			addbyte(0xff); /*dec dword ptr[&acycs]*/
			addbyte(0x0c);
			addbyte(0x25);
			addlong((uint32_t) (uintptr_t) &(acycs));
		} else {
			addbyte(0x81); /*ADD $acc_regs[c].count,acc_regs[c].dest*/
			addbyte(0x04);
			addbyte(0x25);
			addlong((uint32_t) (uintptr_t) &(acycs));
			addlong(-delta);
		}
	}
#endif
}

void codegen_accumulate_flush(void)
{
	if (acc_regs[0].count) {
		addbyte(0x55); /*push rbp*/
		addbyte(0x48); /*mov rbp,val*/
		addbyte(0xbd);
		addlong((uint32_t) (acc_regs[0].dest_reg & 0xffffffffULL));
		addlong((uint32_t) (acc_regs[0].dest_reg >> 32ULL));
		addbyte(0x81); /* add d,[rbp][0],val */
		addbyte(0x45);
		addbyte(0x00);
		addlong(acc_regs[0].count);
		addbyte(0x5d); /*pop rbp*/
	}

	acc_regs[0].count = 0;
}

void codegen_accumulate_reset()
{
	acc_regs[0].count = 0;
}
