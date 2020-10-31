/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Common functions for hardware monitoring chips.
 *
 *
 *
 * Author:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/hwm.h>


/* Refer to specific hardware monitor implementations for the meaning of hwm_values. */
hwm_values_t	hwm_values;


uint16_t
hwm_get_vcore()
{
    /* Determine Vcore for the active CPU. */
    CPU *cpu = &machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective];
    switch (cpu->cpu_type) {
	case CPU_WINCHIP:
	case CPU_WINCHIP2:
#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
	case CPU_K5:
	case CPU_5K86:
#endif
#if (defined(USE_NEW_DYNAREC) || (defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)))
	case CPU_Cx6x86:
#endif
		return 3520;

	case CPU_PENTIUMMMX:
		return ((cpu->cpuid_model & 0xf000) == 0x1000) ? 3300 : 2800;

#if (defined(USE_NEW_DYNAREC) || (defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)))
	case CPU_Cx6x86MX:
		return (cpu->rspeed == 208333333) ? 2700 : 2900;

	case CPU_Cx6x86L:
#endif
	case CPU_PENTIUM2:
		return 2800;

	case CPU_K6_2C:
		if (cpu->multi == 5.0)
			return 2400;
		else if (cpu->rspeed >= 550000000)
			return 2300;
		else
			return 2200;

	case CPU_K6:
		if ((cpu->cpuid_model & 0x0f0) == 0x070)
			return 2200;
		else if (cpu->multi <= 3.0)
			return 2900;
		else
			return 3200;

	case CPU_K6_2:
	case CPU_K6_3:
		return 2200;

	case CPU_PENTIUM2D:
	case CPU_CYRIX3S:
		return 2050;

	case CPU_K6_2P:
	case CPU_K6_3P:
		return 2000;

	default:
		return 3300;
    }
}
