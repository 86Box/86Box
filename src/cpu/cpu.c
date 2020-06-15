/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		CPU type handler.
 *
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *		leilei,
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2018 Fred N. van Kempen.
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 leilei.
 *		Copyright 2016-2018 Miran Grca.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/io.h>
#include "x86_ops.h"
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/pci.h>
#ifdef USE_DYNAREC
# include "codegen.h"
#endif
#include "x87_timings.h"

/*#define ENABLE_CPU_LOG 1*/

static void	cpu_write(uint16_t addr, uint8_t val, void *priv);
static uint8_t	cpu_read(uint16_t addr, void *priv);


enum {
        CPUID_FPU = (1 << 0),
        CPUID_VME = (1 << 1),
        CPUID_PSE = (1 << 3),
        CPUID_TSC = (1 << 4),
        CPUID_MSR = (1 << 5),
        CPUID_PAE = (1 << 6),
        CPUID_CMPXCHG8B = (1 << 8),
	CPUID_AMDSEP = (1 << 10),
	CPUID_SEP = (1 << 11),
	CPUID_MTRR = (1 << 12),
        CPUID_CMOV = (1 << 15),
        CPUID_MMX = (1 << 23),
	CPUID_FXSR = (1 << 24)
};

/*Addition flags returned by CPUID function 0x80000001*/
enum
{
        CPUID_3DNOW = (1 << 31)
};


#ifdef USE_DYNAREC
const OpFn	*x86_dynarec_opcodes;
const OpFn	*x86_dynarec_opcodes_0f;
const OpFn	*x86_dynarec_opcodes_d8_a16;
const OpFn	*x86_dynarec_opcodes_d8_a32;
const OpFn	*x86_dynarec_opcodes_d9_a16;
const OpFn	*x86_dynarec_opcodes_d9_a32;
const OpFn	*x86_dynarec_opcodes_da_a16;
const OpFn	*x86_dynarec_opcodes_da_a32;
const OpFn	*x86_dynarec_opcodes_db_a16;
const OpFn	*x86_dynarec_opcodes_db_a32;
const OpFn	*x86_dynarec_opcodes_dc_a16;
const OpFn	*x86_dynarec_opcodes_dc_a32;
const OpFn	*x86_dynarec_opcodes_dd_a16;
const OpFn	*x86_dynarec_opcodes_dd_a32;
const OpFn	*x86_dynarec_opcodes_de_a16;
const OpFn	*x86_dynarec_opcodes_de_a32;
const OpFn	*x86_dynarec_opcodes_df_a16;
const OpFn	*x86_dynarec_opcodes_df_a32;
const OpFn	*x86_dynarec_opcodes_REPE;
const OpFn	*x86_dynarec_opcodes_REPNE;
const OpFn	*x86_dynarec_opcodes_3DNOW;
#endif

const OpFn	*x86_opcodes;
const OpFn	*x86_opcodes_0f;
const OpFn	*x86_opcodes_d8_a16;
const OpFn	*x86_opcodes_d8_a32;
const OpFn	*x86_opcodes_d9_a16;
const OpFn	*x86_opcodes_d9_a32;
const OpFn	*x86_opcodes_da_a16;
const OpFn	*x86_opcodes_da_a32;
const OpFn	*x86_opcodes_db_a16;
const OpFn	*x86_opcodes_db_a32;
const OpFn	*x86_opcodes_dc_a16;
const OpFn	*x86_opcodes_dc_a32;
const OpFn	*x86_opcodes_dd_a16;
const OpFn	*x86_opcodes_dd_a32;
const OpFn	*x86_opcodes_de_a16;
const OpFn	*x86_opcodes_de_a32;
const OpFn	*x86_opcodes_df_a16;
const OpFn	*x86_opcodes_df_a32;
const OpFn	*x86_opcodes_REPE;
const OpFn	*x86_opcodes_REPNE;
const OpFn	*x86_opcodes_3DNOW;

int in_smm = 0, smi_line = 0, smi_latched = 0, smm_in_hlt = 0;
uint32_t smbase = 0x30000;

CPU		*cpu_s;
int		cpu_effective;
int		cpu_multi;
double		cpu_dmulti;
int		cpu_16bitbus, cpu_64bitbus;
int		cpu_busspeed;
int		cpu_cyrix_alignment;
int		CPUID;
uint64_t	cpu_CR4_mask;
int		isa_cycles;
int		cpu_cycles_read, cpu_cycles_read_l,
		cpu_cycles_write, cpu_cycles_write_l;
int		cpu_prefetch_cycles, cpu_prefetch_width,
		cpu_mem_prefetch_cycles, cpu_rom_prefetch_cycles;
int		cpu_waitstates;
int		cpu_cache_int_enabled, cpu_cache_ext_enabled;
int		cpu_pci_speed, cpu_alt_reset;
uint16_t	cpu_fast_off_count, cpu_fast_off_val;
uint32_t	cpu_fast_off_flags;

uint32_t cpu_features;

int		is286,
		is386,
		is486 = 1,
		is486sx, is486dx, is486sx2, is486dx2, isdx4,
		cpu_iscyrix,
		hascache,
		isibm486,
		israpidcad,
		is_am486, is_pentium, is_k5, is_k6, is_p6;

int		hasfpu;
int 		fpu_type;

uint64_t	tsc = 0;
msr_t		msr;
cpu_state_t     cpu_state;
uint64_t	pmc[2] = {0, 0};

uint16_t	temp_seg_data[4] = {0, 0, 0, 0};

uint64_t	mtrr_cap_msr = 0x00000508;
uint64_t	mtrr_physbase_msr[8] = {0, 0, 0, 0, 0, 0, 0, 0};
uint64_t	mtrr_physmask_msr[8] = {0, 0, 0, 0, 0, 0, 0, 0};
uint64_t	mtrr_fix64k_8000_msr = 0;
uint64_t	mtrr_fix16k_8000_msr = 0;
uint64_t	mtrr_fix16k_a000_msr = 0;
uint64_t	mtrr_fix4k_msr[8] = {0, 0, 0, 0, 0, 0, 0, 0};
uint64_t	mtrr_deftype_msr = 0;

uint16_t	cs_msr = 0;
uint32_t	esp_msr = 0;
uint32_t	eip_msr = 0;
uint64_t	apic_base_msr = 0;
uint64_t	pat_msr = 0;
uint64_t	msr_ia32_pmc[8] = {0, 0, 0, 0, 0, 0, 0, 0};
uint64_t	ecx17_msr = 0;
uint64_t	ecx2a_msr = 0;
uint64_t	ecx79_msr = 0;
uint64_t	ecx8x_msr[4] = {0, 0, 0, 0};
uint64_t	ecx116_msr = 0;
uint64_t	ecx11x_msr[4] = {0, 0, 0, 0};
uint64_t	ecx11e_msr = 0;
uint64_t	ecx186_msr = 0;
uint64_t	ecx187_msr = 0;
uint64_t	ecx1e0_msr = 0;

/* Model Identification MSR's used by some Acer BIOSes*/
uint64_t	ecx404_msr = 0;
uint64_t	ecx408_msr = 0;
uint64_t	ecx40c_msr = 0;
uint64_t	ecx410_msr = 0;
uint64_t	ecx570_msr = 0;

uint64_t	ecx83_msr = 0;			/* AMD K5 and K6 MSR's. */

/* MSR used by some Intel AMI boards */
uint64_t        ecx1002ff_msr = 0;

/* Some weird long MSR's used by i686 AMI & some Phoenix BIOSes */
uint64_t	ecxf0f00250_msr = 0;	
uint64_t	ecxf0f00258_msr = 0;

uint64_t	star = 0;			/* AMD K6-2+. */

uint64_t	amd_efer = 0, amd_whcr = 0,
		amd_uwccr = 0, amd_epmr = 0,	/* AMD K6-2+ registers. */
		amd_psor = 0, amd_pfir = 0,
		amd_l2aar = 0;

int		timing_rr;
int		timing_mr, timing_mrl;
int		timing_rm, timing_rml;
int		timing_mm, timing_mml;
int		timing_bt, timing_bnt;
int		timing_int, timing_int_rm, timing_int_v86, timing_int_pm,
		timing_int_pm_outer;
int		timing_iret_rm, timing_iret_v86, timing_iret_pm,
		timing_iret_pm_outer;
int		timing_call_rm, timing_call_pm, timing_call_pm_gate,
		timing_call_pm_gate_inner;
int		timing_retf_rm, timing_retf_pm, timing_retf_pm_outer;
int		timing_jmp_rm, timing_jmp_pm, timing_jmp_pm_gate;
int		timing_misaligned;


static uint8_t	ccr0, ccr1, ccr2, ccr3, ccr4, ccr5, ccr6;


#ifdef ENABLE_CPU_LOG
int cpu_do_log = ENABLE_CPU_LOG;


void
cpu_log(const char *fmt, ...)
{
    va_list ap;

    if (cpu_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define cpu_log(fmt, ...)
#endif


int
cpu_has_feature(int feature)
{
		return cpu_features & feature;
}


void
cpu_dynamic_switch(int new_cpu)
{
        if (cpu_effective == new_cpu)
                return;

        int c = cpu;
        cpu = new_cpu;
        cpu_set();
        pc_speed_changed();
        cpu = c;
}


void
cpu_set_edx(void)
{
        EDX = machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].edx_reset;
}

int fpu_get_type(int machine, int cpu_manufacturer, int cpu, const char *internal_name)
{
        CPU *cpu_s = &machines[machine].cpu[cpu_manufacturer].cpus[cpu];
        const FPU *fpus = cpu_s->fpus;
        int fpu_type = fpus[0].type;
        int c = 0;
        
        while (fpus[c].internal_name)
        {
                if (!strcmp(internal_name, fpus[c].internal_name))
                        fpu_type = fpus[c].type;
                c++;
        }
        
        return fpu_type;
}

const char *fpu_get_internal_name(int machine, int cpu_manufacturer, int cpu, int type)
{
        CPU *cpu_s = &machines[machine].cpu[cpu_manufacturer].cpus[cpu];
        const FPU *fpus = cpu_s->fpus;
        int c = 0;

        while (fpus[c].internal_name)
        {
                if (fpus[c].type == type)
                        return fpus[c].internal_name;
                c++;
        }

        return fpus[0].internal_name;
}

const char *fpu_get_name_from_index(int machine, int cpu_manufacturer, int cpu, int c)
{
        CPU *cpu_s = &machines[machine].cpu[cpu_manufacturer].cpus[cpu];
        const FPU *fpus = cpu_s->fpus;
        
        return fpus[c].name;
}

int fpu_get_type_from_index(int machine, int cpu_manufacturer, int cpu, int c)
{
        CPU *cpu_s = &machines[machine].cpu[cpu_manufacturer].cpus[cpu];
        const FPU *fpus = cpu_s->fpus;

        return fpus[c].type;
}

void
cpu_set(void)
{
        if (!machines[machine].cpu[cpu_manufacturer].cpus)
        {
                /*CPU is invalid, set to default*/
                cpu_manufacturer = 0;
                cpu = 0;
        }

	cpu_effective = cpu;
        cpu_s = &machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective];

	cpu_alt_reset = 0;

        CPUID        = cpu_s->cpuid_model;
        is8086       = (cpu_s->cpu_type > CPU_8088);
        is286        = (cpu_s->cpu_type >= CPU_286);
        is386        = (cpu_s->cpu_type >= CPU_386SX);
	israpidcad   = (cpu_s->cpu_type == CPU_RAPIDCAD);
	isibm486     = (cpu_s->cpu_type == CPU_IBM486SLC) || (cpu_s->cpu_type == CPU_IBM486BL);
        is486        = (cpu_s->cpu_type >= CPU_i486SX) || (cpu_s->cpu_type == CPU_486SLC || cpu_s->cpu_type == CPU_486DLC || cpu_s->cpu_type == CPU_RAPIDCAD);
        is486sx      = (cpu_s->cpu_type >= CPU_i486SX) && (cpu_s->cpu_type < CPU_i486SX2);
        is486sx2     = (cpu_s->cpu_type >= CPU_i486SX2) && (cpu_s->cpu_type < CPU_i486DX);
        is486dx      = (cpu_s->cpu_type >= CPU_i486DX) && (cpu_s->cpu_type < CPU_i486DX2);
        is486dx2     = (cpu_s->cpu_type >= CPU_i486DX2) && (cpu_s->cpu_type < CPU_iDX4);	
        isdx4        = (cpu_s->cpu_type >= CPU_iDX4) && (cpu_s->cpu_type < CPU_WINCHIP);
	is_am486     = (cpu_s->cpu_type == CPU_Am486SX) || (cpu_s->cpu_type == CPU_Am486SX2) || (cpu_s->cpu_type == CPU_Am486DX) ||
		       (cpu_s->cpu_type == CPU_Am486DX2) || (cpu_s->cpu_type == CPU_Am486DX4) || (cpu_s->cpu_type == CPU_Am5x86);
        is_pentium   = (cpu_s->cpu_type == CPU_P24T) || (cpu_s->cpu_type == CPU_PENTIUM) || (cpu_s->cpu_type == CPU_PENTIUMMMX);
	/* Not Pentiums, but they share the same SMM save state table layout. */
	is_pentium  |= (cpu_s->cpu_type == CPU_i486DX2) || (cpu_s->cpu_type == CPU_iDX4);
	/* The WinChip datasheet claims these are Pentium-compatible. */
	is_pentium  |= (cpu_s->cpu_type == CPU_WINCHIP) || (cpu_s->cpu_type == CPU_WINCHIP2);
#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
        is_k5        = (cpu_s->cpu_type == CPU_K5) || (cpu_s->cpu_type == CPU_5K86);
#else
	is_k5        = 0;
#endif
	is_k6        = (cpu_s->cpu_type == CPU_K6) || (cpu_s->cpu_type == CPU_K6_2) ||
		       (cpu_s->cpu_type == CPU_K6_2C) || (cpu_s->cpu_type == CPU_K6_3) ||
		       (cpu_s->cpu_type == CPU_K6_2P) || (cpu_s->cpu_type == CPU_K6_3P);
        is_p6        = (cpu_s->cpu_type == CPU_PENTIUMPRO) || (cpu_s->cpu_type == CPU_PENTIUM2) ||
		       (cpu_s->cpu_type == CPU_PENTIUM2D);
	/* The Samuel 2 datasheet claims it's Celeron-compatible. */
	is_p6       |= (cpu_s->cpu_type == CPU_CYRIX3S);
        hasfpu       = (fpu_type != FPU_NONE);
	hascache     = (cpu_s->cpu_type >= CPU_486SLC) || (cpu_s->cpu_type == CPU_IBM386SLC || cpu_s->cpu_type == CPU_IBM486SLC || cpu_s->cpu_type == CPU_IBM486BL);
#if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
        cpu_iscyrix  = (cpu_s->cpu_type == CPU_486SLC || cpu_s->cpu_type == CPU_486DLC || cpu_s->cpu_type == CPU_Cx486S || cpu_s->cpu_type == CPU_Cx486DX || cpu_s->cpu_type == CPU_Cx5x86 || cpu_s->cpu_type == CPU_Cx6x86 || cpu_s->cpu_type == CPU_Cx6x86MX || cpu_s->cpu_type == CPU_Cx6x86L || cpu_s->cpu_type == CPU_CxGX1);
#else
        cpu_iscyrix  = (cpu_s->cpu_type == CPU_486SLC || cpu_s->cpu_type == CPU_486DLC || cpu_s->cpu_type == CPU_Cx486S || cpu_s->cpu_type == CPU_Cx486DX || cpu_s->cpu_type == CPU_Cx5x86);
#endif

        cpu_16bitbus = (cpu_s->cpu_type == CPU_286 || cpu_s->cpu_type == CPU_386SX || cpu_s->cpu_type == CPU_486SLC || cpu_s->cpu_type == CPU_IBM386SLC || cpu_s->cpu_type == CPU_IBM486SLC );
        cpu_64bitbus = (cpu_s->cpu_type >= CPU_WINCHIP);
	
        if (cpu_s->multi)
		cpu_busspeed = cpu_s->rspeed / cpu_s->multi;
	else
		cpu_busspeed = cpu_s->rspeed;
        cpu_multi = (int) ceil(cpu_s->multi);
        cpu_dmulti = cpu_s->multi;
        ccr0 = ccr1 = ccr2 = ccr3 = ccr4 = ccr5 = ccr6 = 0;



        cpu_update_waitstates();

	isa_cycles = cpu_s->atclk_div;

        if (cpu_s->rspeed <= 8000000)
                cpu_rom_prefetch_cycles = cpu_mem_prefetch_cycles;
        else
                cpu_rom_prefetch_cycles = cpu_s->rspeed / 1000000;

	if (cpu_busspeed < 42500000)
		cpu_pci_speed = cpu_busspeed;
	else if ((cpu_busspeed > 42500000) && (cpu_busspeed < 84000000))
		cpu_pci_speed = cpu_busspeed / 2;
	else
		cpu_pci_speed = cpu_busspeed / 3;

	pci_burst_time = cpu_s->rspeed / cpu_pci_speed;
	pci_nonburst_time = 4 * pci_burst_time;

        if (cpu_iscyrix)
		io_sethandler(0x0022, 0x0002, cpu_read, NULL, NULL, cpu_write, NULL, NULL, NULL);
        else
		io_removehandler(0x0022, 0x0002, cpu_read, NULL, NULL, cpu_write, NULL, NULL, NULL);

	if (hasfpu)
		io_sethandler(0x00f0, 0x000f, cpu_read, NULL, NULL, cpu_write, NULL, NULL, NULL);
	else
		io_removehandler(0x00f0, 0x000f, cpu_read, NULL, NULL, cpu_write, NULL, NULL, NULL);

#ifdef USE_DYNAREC
        x86_setopcodes(ops_386, ops_386_0f, dynarec_ops_386, dynarec_ops_386_0f);
#else
        x86_setopcodes(ops_386, ops_386_0f);
#endif
        x86_opcodes_REPE = ops_REPE;
        x86_opcodes_REPNE = ops_REPNE;
        x86_opcodes_3DNOW = ops_3DNOW;
#ifdef USE_DYNAREC
        x86_dynarec_opcodes_REPE = dynarec_ops_REPE;
        x86_dynarec_opcodes_REPNE = dynarec_ops_REPNE;
        x86_dynarec_opcodes_3DNOW = dynarec_ops_3DNOW;
#endif

#ifdef USE_DYNAREC
        if (hasfpu)
        {
                x86_dynarec_opcodes_d8_a16 = dynarec_ops_fpu_d8_a16;
                x86_dynarec_opcodes_d8_a32 = dynarec_ops_fpu_d8_a32;
                x86_dynarec_opcodes_d9_a16 = dynarec_ops_fpu_d9_a16;
                x86_dynarec_opcodes_d9_a32 = dynarec_ops_fpu_d9_a32;
                x86_dynarec_opcodes_da_a16 = dynarec_ops_fpu_da_a16;
                x86_dynarec_opcodes_da_a32 = dynarec_ops_fpu_da_a32;
                x86_dynarec_opcodes_db_a16 = dynarec_ops_fpu_db_a16;
                x86_dynarec_opcodes_db_a32 = dynarec_ops_fpu_db_a32;
                x86_dynarec_opcodes_dc_a16 = dynarec_ops_fpu_dc_a16;
                x86_dynarec_opcodes_dc_a32 = dynarec_ops_fpu_dc_a32;
                x86_dynarec_opcodes_dd_a16 = dynarec_ops_fpu_dd_a16;
                x86_dynarec_opcodes_dd_a32 = dynarec_ops_fpu_dd_a32;
                x86_dynarec_opcodes_de_a16 = dynarec_ops_fpu_de_a16;
                x86_dynarec_opcodes_de_a32 = dynarec_ops_fpu_de_a32;
                x86_dynarec_opcodes_df_a16 = dynarec_ops_fpu_df_a16;
                x86_dynarec_opcodes_df_a32 = dynarec_ops_fpu_df_a32;
        }
        else
        {
                x86_dynarec_opcodes_d8_a16 = dynarec_ops_nofpu_a16;
                x86_dynarec_opcodes_d8_a32 = dynarec_ops_nofpu_a32;
                x86_dynarec_opcodes_d9_a16 = dynarec_ops_nofpu_a16;
                x86_dynarec_opcodes_d9_a32 = dynarec_ops_nofpu_a32;
                x86_dynarec_opcodes_da_a16 = dynarec_ops_nofpu_a16;
                x86_dynarec_opcodes_da_a32 = dynarec_ops_nofpu_a32;
                x86_dynarec_opcodes_db_a16 = dynarec_ops_nofpu_a16;
                x86_dynarec_opcodes_db_a32 = dynarec_ops_nofpu_a32;
                x86_dynarec_opcodes_dc_a16 = dynarec_ops_nofpu_a16;
                x86_dynarec_opcodes_dc_a32 = dynarec_ops_nofpu_a32;
                x86_dynarec_opcodes_dd_a16 = dynarec_ops_nofpu_a16;
                x86_dynarec_opcodes_dd_a32 = dynarec_ops_nofpu_a32;
                x86_dynarec_opcodes_de_a16 = dynarec_ops_nofpu_a16;
                x86_dynarec_opcodes_de_a32 = dynarec_ops_nofpu_a32;
                x86_dynarec_opcodes_df_a16 = dynarec_ops_nofpu_a16;
                x86_dynarec_opcodes_df_a32 = dynarec_ops_nofpu_a32;
        }
        codegen_timing_set(&codegen_timing_486);
#endif

        if (hasfpu)
        {
                x86_opcodes_d8_a16 = ops_fpu_d8_a16;
                x86_opcodes_d8_a32 = ops_fpu_d8_a32;
                x86_opcodes_d9_a16 = ops_fpu_d9_a16;
                x86_opcodes_d9_a32 = ops_fpu_d9_a32;
                x86_opcodes_da_a16 = ops_fpu_da_a16;
                x86_opcodes_da_a32 = ops_fpu_da_a32;
                x86_opcodes_db_a16 = ops_fpu_db_a16;
                x86_opcodes_db_a32 = ops_fpu_db_a32;
                x86_opcodes_dc_a16 = ops_fpu_dc_a16;
                x86_opcodes_dc_a32 = ops_fpu_dc_a32;
                x86_opcodes_dd_a16 = ops_fpu_dd_a16;
                x86_opcodes_dd_a32 = ops_fpu_dd_a32;
                x86_opcodes_de_a16 = ops_fpu_de_a16;
                x86_opcodes_de_a32 = ops_fpu_de_a32;
                x86_opcodes_df_a16 = ops_fpu_df_a16;
                x86_opcodes_df_a32 = ops_fpu_df_a32;
        }
        else
        {
                x86_opcodes_d8_a16 = ops_nofpu_a16;
                x86_opcodes_d8_a32 = ops_nofpu_a32;
                x86_opcodes_d9_a16 = ops_nofpu_a16;
                x86_opcodes_d9_a32 = ops_nofpu_a32;
                x86_opcodes_da_a16 = ops_nofpu_a16;
                x86_opcodes_da_a32 = ops_nofpu_a32;
                x86_opcodes_db_a16 = ops_nofpu_a16;
                x86_opcodes_db_a32 = ops_nofpu_a32;
                x86_opcodes_dc_a16 = ops_nofpu_a16;
                x86_opcodes_dc_a32 = ops_nofpu_a32;
                x86_opcodes_dd_a16 = ops_nofpu_a16;
                x86_opcodes_dd_a32 = ops_nofpu_a32;
                x86_opcodes_de_a16 = ops_nofpu_a16;
                x86_opcodes_de_a32 = ops_nofpu_a32;
                x86_opcodes_df_a16 = ops_nofpu_a16;
                x86_opcodes_df_a32 = ops_nofpu_a32;
        }

        memset(&msr, 0, sizeof(msr));

        timing_misaligned = 0;
        cpu_cyrix_alignment = 0;

        switch (cpu_s->cpu_type)
        {
                case CPU_8088:
                case CPU_8086:
                break;
                
                case CPU_286:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_286, ops_286_0f, dynarec_ops_286, dynarec_ops_286_0f);
#else
                x86_setopcodes(ops_286, ops_286_0f);
#endif
		if (fpu_type == FPU_287)
		{
#ifdef USE_DYNAREC
                	x86_dynarec_opcodes_d9_a16 = dynarec_ops_fpu_287_d9_a16;
        	        x86_dynarec_opcodes_d9_a32 = dynarec_ops_fpu_287_d9_a32;
                	x86_dynarec_opcodes_da_a16 = dynarec_ops_fpu_287_da_a16;
        	        x86_dynarec_opcodes_da_a32 = dynarec_ops_fpu_287_da_a32;
	                x86_dynarec_opcodes_db_a16 = dynarec_ops_fpu_287_db_a16;
        	        x86_dynarec_opcodes_db_a32 = dynarec_ops_fpu_287_db_a32;
	                x86_dynarec_opcodes_dc_a16 = dynarec_ops_fpu_287_dc_a16;
        	        x86_dynarec_opcodes_dc_a32 = dynarec_ops_fpu_287_dc_a32;
	                x86_dynarec_opcodes_dd_a16 = dynarec_ops_fpu_287_dd_a16;
        	        x86_dynarec_opcodes_dd_a32 = dynarec_ops_fpu_287_dd_a32;
	                x86_dynarec_opcodes_de_a16 = dynarec_ops_fpu_287_de_a16;
        	        x86_dynarec_opcodes_de_a32 = dynarec_ops_fpu_287_de_a32;
	                x86_dynarec_opcodes_df_a16 = dynarec_ops_fpu_287_df_a16;
        	        x86_dynarec_opcodes_df_a32 = dynarec_ops_fpu_287_df_a32;
#endif
	                x86_opcodes_d9_a16 = ops_fpu_287_d9_a16;
	                x86_opcodes_d9_a32 = ops_fpu_287_d9_a32;
	                x86_opcodes_da_a16 = ops_fpu_287_da_a16;
	                x86_opcodes_da_a32 = ops_fpu_287_da_a32;
        	        x86_opcodes_db_a16 = ops_fpu_287_db_a16;
	                x86_opcodes_db_a32 = ops_fpu_287_db_a32;
        	        x86_opcodes_dc_a16 = ops_fpu_287_dc_a16;
	                x86_opcodes_dc_a32 = ops_fpu_287_dc_a32;
        	        x86_opcodes_dd_a16 = ops_fpu_287_dd_a16;
	                x86_opcodes_dd_a32 = ops_fpu_287_dd_a32;
        	        x86_opcodes_de_a16 = ops_fpu_287_de_a16;
	                x86_opcodes_de_a32 = ops_fpu_287_de_a32;
        	        x86_opcodes_df_a16 = ops_fpu_287_df_a16;
	                x86_opcodes_df_a32 = ops_fpu_287_df_a32;
		}
                timing_rr  = 2;   /*register dest - register src*/
                timing_rm  = 7;   /*register dest - memory src*/
                timing_mr  = 7;   /*memory dest   - register src*/
                timing_mm  = 7;   /*memory dest   - memory src*/
                timing_rml = 9;   /*register dest - memory src long*/
                timing_mrl = 11;  /*memory dest   - register src long*/
                timing_mml = 11;  /*memory dest   - memory src*/
                timing_bt  = 7-3; /*branch taken*/
                timing_bnt = 3;   /*branch not taken*/
                timing_int = 0;
                timing_int_rm       = 23;
                timing_int_v86      = 0;
                timing_int_pm       = 40;
                timing_int_pm_outer = 78;
                timing_iret_rm       = 17;
                timing_iret_v86      = 0;
                timing_iret_pm       = 31;
                timing_iret_pm_outer = 55;
                timing_call_rm            = 13;
                timing_call_pm            = 26;
                timing_call_pm_gate       = 52;
                timing_call_pm_gate_inner = 82;
                timing_retf_rm       = 15;
                timing_retf_pm       = 25;
                timing_retf_pm_outer = 55;
                timing_jmp_rm      = 11;
                timing_jmp_pm      = 23;
                timing_jmp_pm_gate = 38;
                break;
		
                case CPU_IBM486SLC:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_486_0f, dynarec_ops_386, dynarec_ops_486_0f);
#else
                x86_setopcodes(ops_386, ops_486_0f);
#endif
		case CPU_IBM386SLC:
                case CPU_386SX:
                timing_rr  = 2;   /*register dest - register src*/
                timing_rm  = 6;   /*register dest - memory src*/
                timing_mr  = 7;   /*memory dest   - register src*/
                timing_mm  = 6;   /*memory dest   - memory src*/
                timing_rml = 8;   /*register dest - memory src long*/
                timing_mrl = 11;  /*memory dest   - register src long*/
                timing_mml = 10;  /*memory dest   - memory src*/
                timing_bt  = 7-3; /*branch taken*/
                timing_bnt = 3;   /*branch not taken*/
                timing_int = 0;
                timing_int_rm       = 37;
                timing_int_v86      = 59;
                timing_int_pm       = 99;
                timing_int_pm_outer = 119;
                timing_iret_rm       = 22;
                timing_iret_v86      = 60;
                timing_iret_pm       = 38;
                timing_iret_pm_outer = 82;
                timing_call_rm            = 17;
                timing_call_pm            = 34;
                timing_call_pm_gate       = 52;
                timing_call_pm_gate_inner = 86;
                timing_retf_rm       = 18;
                timing_retf_pm       = 32;
                timing_retf_pm_outer = 68;
                timing_jmp_rm      = 12;
                timing_jmp_pm      = 27;
                timing_jmp_pm_gate = 45;
                break;
		
                case CPU_IBM486BL:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_486_0f, dynarec_ops_386, dynarec_ops_486_0f);
#else
                x86_setopcodes(ops_386, ops_486_0f);
#endif
                case CPU_386DX:
                if (fpu_type == FPU_287) /*In case we get Deskpro 386 emulation*/
                {
#ifdef USE_DYNAREC
                	x86_dynarec_opcodes_d9_a16 = dynarec_ops_fpu_287_d9_a16;
        	        x86_dynarec_opcodes_d9_a32 = dynarec_ops_fpu_287_d9_a32;
                	x86_dynarec_opcodes_da_a16 = dynarec_ops_fpu_287_da_a16;
        	        x86_dynarec_opcodes_da_a32 = dynarec_ops_fpu_287_da_a32;
	                x86_dynarec_opcodes_db_a16 = dynarec_ops_fpu_287_db_a16;
        	        x86_dynarec_opcodes_db_a32 = dynarec_ops_fpu_287_db_a32;
	                x86_dynarec_opcodes_dc_a16 = dynarec_ops_fpu_287_dc_a16;
        	        x86_dynarec_opcodes_dc_a32 = dynarec_ops_fpu_287_dc_a32;
	                x86_dynarec_opcodes_dd_a16 = dynarec_ops_fpu_287_dd_a16;
        	        x86_dynarec_opcodes_dd_a32 = dynarec_ops_fpu_287_dd_a32;
	                x86_dynarec_opcodes_de_a16 = dynarec_ops_fpu_287_de_a16;
        	        x86_dynarec_opcodes_de_a32 = dynarec_ops_fpu_287_de_a32;
	                x86_dynarec_opcodes_df_a16 = dynarec_ops_fpu_287_df_a16;
        	        x86_dynarec_opcodes_df_a32 = dynarec_ops_fpu_287_df_a32;
#endif
	                x86_opcodes_d9_a16 = ops_fpu_287_d9_a16;
	                x86_opcodes_d9_a32 = ops_fpu_287_d9_a32;
	                x86_opcodes_da_a16 = ops_fpu_287_da_a16;
	                x86_opcodes_da_a32 = ops_fpu_287_da_a32;
        	        x86_opcodes_db_a16 = ops_fpu_287_db_a16;
	                x86_opcodes_db_a32 = ops_fpu_287_db_a32;
        	        x86_opcodes_dc_a16 = ops_fpu_287_dc_a16;
	                x86_opcodes_dc_a32 = ops_fpu_287_dc_a32;
        	        x86_opcodes_dd_a16 = ops_fpu_287_dd_a16;
	                x86_opcodes_dd_a32 = ops_fpu_287_dd_a32;
        	        x86_opcodes_de_a16 = ops_fpu_287_de_a16;
	                x86_opcodes_de_a32 = ops_fpu_287_de_a32;
        	        x86_opcodes_df_a16 = ops_fpu_287_df_a16;
	                x86_opcodes_df_a32 = ops_fpu_287_df_a32;
		}		
                timing_rr  = 2; /*register dest - register src*/
                timing_rm  = 6; /*register dest - memory src*/
                timing_mr  = 7; /*memory dest   - register src*/
                timing_mm  = 6; /*memory dest   - memory src*/
                timing_rml = 6; /*register dest - memory src long*/
                timing_mrl = 7; /*memory dest   - register src long*/
                timing_mml = 6; /*memory dest   - memory src*/
                timing_bt  = 7-3; /*branch taken*/
                timing_bnt = 3; /*branch not taken*/
                timing_int = 0;
                timing_int_rm       = 37;
                timing_int_v86      = 59;
                timing_int_pm       = 99;
                timing_int_pm_outer = 119;
                timing_iret_rm       = 22;
                timing_iret_v86      = 60;
                timing_iret_pm       = 38;
                timing_iret_pm_outer = 82;
                timing_call_rm            = 17;
                timing_call_pm            = 34;
                timing_call_pm_gate       = 52;
                timing_call_pm_gate_inner = 86;
                timing_retf_rm       = 18;
                timing_retf_pm       = 32;
                timing_retf_pm_outer = 68;
                timing_jmp_rm      = 12;
                timing_jmp_pm      = 27;
                timing_jmp_pm_gate = 45;
                break;
                
		
                case CPU_RAPIDCAD:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_486_0f, dynarec_ops_386, dynarec_ops_486_0f);
#else
                x86_setopcodes(ops_386, ops_486_0f);
#endif
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 2; /*register dest - memory src*/
                timing_mr  = 3; /*memory dest   - register src*/
                timing_mm  = 3;
                timing_rml = 2; /*register dest - memory src long*/
                timing_mrl = 3; /*memory dest   - register src long*/
                timing_mml = 3;
                timing_bt  = 3-1; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                timing_int = 4;
                timing_int_rm       = 26;
                timing_int_v86      = 82;
                timing_int_pm       = 44;
                timing_int_pm_outer = 71;
                timing_iret_rm       = 15;
                timing_iret_v86      = 36; /*unknown*/
                timing_iret_pm       = 20;
                timing_iret_pm_outer = 36;
                timing_call_rm = 18;
                timing_call_pm = 20;
                timing_call_pm_gate = 35;
                timing_call_pm_gate_inner = 69;
                timing_retf_rm       = 13;
                timing_retf_pm       = 17;
                timing_retf_pm_outer = 35;
                timing_jmp_rm      = 17;
                timing_jmp_pm      = 19;
                timing_jmp_pm_gate = 32;
                timing_misaligned = 3;
                break;
                
                case CPU_486SLC:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_486_0f, dynarec_ops_386, dynarec_ops_486_0f);
#else
                x86_setopcodes(ops_386, ops_486_0f);
#endif
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 3; /*register dest - memory src*/
                timing_mr  = 5; /*memory dest   - register src*/
                timing_mm  = 3;
                timing_rml = 5; /*register dest - memory src long*/
                timing_mrl = 7; /*memory dest   - register src long*/
                timing_mml = 7;
                timing_bt  = 6-1; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                /*unknown*/
                timing_int = 4;
                timing_int_rm       = 14;
                timing_int_v86      = 82;
                timing_int_pm       = 49;
                timing_int_pm_outer = 77;
                timing_iret_rm       = 14;
                timing_iret_v86      = 66;
                timing_iret_pm       = 31;
                timing_iret_pm_outer = 66;
                timing_call_rm = 12;
                timing_call_pm = 30;
                timing_call_pm_gate = 41;
                timing_call_pm_gate_inner = 83;
                timing_retf_rm       = 13;
                timing_retf_pm       = 26;
                timing_retf_pm_outer = 61;
                timing_jmp_rm      = 9;
                timing_jmp_pm      = 26;
                timing_jmp_pm_gate = 37;
                timing_misaligned = 3;
                break;
                
                case CPU_486DLC:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_486_0f, dynarec_ops_386, dynarec_ops_486_0f);
#else
                x86_setopcodes(ops_386, ops_486_0f);
#endif
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 3; /*register dest - memory src*/
                timing_mr  = 3; /*memory dest   - register src*/
                timing_mm  = 3;
                timing_rml = 3; /*register dest - memory src long*/
                timing_mrl = 3; /*memory dest   - register src long*/
                timing_mml = 3;
                timing_bt  = 6-1; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                /*unknown*/
                timing_int = 4;
                timing_int_rm       = 14;
                timing_int_v86      = 82;
                timing_int_pm       = 49;
                timing_int_pm_outer = 77;
                timing_iret_rm       = 14;
                timing_iret_v86      = 66;
                timing_iret_pm       = 31;
                timing_iret_pm_outer = 66;
                timing_call_rm = 12;
                timing_call_pm = 30;
                timing_call_pm_gate = 41;
                timing_call_pm_gate_inner = 83;
                timing_retf_rm       = 13;
                timing_retf_pm       = 26;
                timing_retf_pm_outer = 61;
                timing_jmp_rm      = 9;
                timing_jmp_pm      = 26;
                timing_jmp_pm_gate = 37;
                timing_misaligned = 3;
                break;
                
		case CPU_iDX4:
		cpu_features = CPU_FEATURE_CR4 | CPU_FEATURE_VME;
		cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_VME;
		/*FALLTHROUGH*/
                case CPU_i486SX:
                case CPU_i486SX2:
                case CPU_i486DX:
                case CPU_i486DX2:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_486_0f, dynarec_ops_386, dynarec_ops_486_0f);
#else
                x86_setopcodes(ops_386, ops_486_0f);
#endif
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 2; /*register dest - memory src*/
                timing_mr  = 3; /*memory dest   - register src*/
                timing_mm  = 3;
                timing_rml = 2; /*register dest - memory src long*/
                timing_mrl = 3; /*memory dest   - register src long*/
                timing_mml = 3;
                timing_bt  = 3-1; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                timing_int = 4;
                timing_int_rm       = 26;
                timing_int_v86      = 82;
                timing_int_pm       = 44;
                timing_int_pm_outer = 71;
                timing_iret_rm       = 15;
                timing_iret_v86      = 36; /*unknown*/
                timing_iret_pm       = 20;
                timing_iret_pm_outer = 36;
                timing_call_rm = 18;
                timing_call_pm = 20;
                timing_call_pm_gate = 35;
                timing_call_pm_gate_inner = 69;
                timing_retf_rm       = 13;
                timing_retf_pm       = 17;
                timing_retf_pm_outer = 35;
                timing_jmp_rm      = 17;
                timing_jmp_pm      = 19;
                timing_jmp_pm_gate = 32;
                timing_misaligned = 3;
                break;

                case CPU_Am486SX:
                case CPU_Am486SX2:
                case CPU_Am486DX:
                case CPU_Am486DX2:
                case CPU_Am486DX4:
                case CPU_Am5x86:
                /*AMD timing identical to Intel*/
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_486_0f, dynarec_ops_386, dynarec_ops_486_0f);
#else
                x86_setopcodes(ops_386, ops_486_0f);
#endif
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 2; /*register dest - memory src*/
                timing_mr  = 3; /*memory dest   - register src*/
                timing_mm  = 3;
                timing_rml = 2; /*register dest - memory src long*/
                timing_mrl = 3; /*memory dest   - register src long*/
                timing_mml = 3;
                timing_bt  = 3-1; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                timing_int = 4;
                timing_int_rm       = 26;
                timing_int_v86      = 82;
                timing_int_pm       = 44;
                timing_int_pm_outer = 71;
                timing_iret_rm       = 15;
                timing_iret_v86      = 36; /*unknown*/
                timing_iret_pm       = 20;
                timing_iret_pm_outer = 36;
                timing_call_rm = 18;
                timing_call_pm = 20;
                timing_call_pm_gate = 35;
                timing_call_pm_gate_inner = 69;
                timing_retf_rm       = 13;
                timing_retf_pm       = 17;
                timing_retf_pm_outer = 35;
                timing_jmp_rm      = 17;
                timing_jmp_pm      = 19;
                timing_jmp_pm_gate = 32;
                timing_misaligned = 3;
                break;
                
                case CPU_Cx486S:
                case CPU_Cx486DX:
                case CPU_Cx486DX2:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_486_0f, dynarec_ops_386, dynarec_ops_486_0f);
#else
                x86_setopcodes(ops_386, ops_486_0f);
#endif
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 3; /*register dest - memory src*/
                timing_mr  = 3; /*memory dest   - register src*/
                timing_mm  = 3;
                timing_rml = 3; /*register dest - memory src long*/
                timing_mrl = 3; /*memory dest   - register src long*/
                timing_mml = 3;
                timing_bt  = 4-1; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                timing_int = 4;
                timing_int_rm       = 14;
                timing_int_v86      = 82;
                timing_int_pm       = 49;
                timing_int_pm_outer = 77;
                timing_iret_rm       = 14;
                timing_iret_v86      = 66; /*unknown*/
                timing_iret_pm       = 31;
                timing_iret_pm_outer = 66;
                timing_call_rm = 12;
                timing_call_pm = 30;
                timing_call_pm_gate = 41;
                timing_call_pm_gate_inner = 83;
                timing_retf_rm       = 13;
                timing_retf_pm       = 26;
                timing_retf_pm_outer = 61;
                timing_jmp_rm      = 9;
                timing_jmp_pm      = 26;
                timing_jmp_pm_gate = 37;
                timing_misaligned = 3;
                break;
                
                case CPU_Cx5x86:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_486_0f, dynarec_ops_386, dynarec_ops_486_0f);
#else
                x86_setopcodes(ops_386, ops_486_0f);
#endif
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 1; /*register dest - memory src*/
                timing_mr  = 2; /*memory dest   - register src*/
                timing_mm  = 2;
                timing_rml = 1; /*register dest - memory src long*/
                timing_mrl = 2; /*memory dest   - register src long*/
                timing_mml = 2;
                timing_bt  = 5-1; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                timing_int = 0;
                timing_int_rm       = 9;
                timing_int_v86      = 82; /*unknown*/
                timing_int_pm       = 21;
                timing_int_pm_outer = 32;
                timing_iret_rm       = 7;
                timing_iret_v86      = 26; /*unknown*/
                timing_iret_pm       = 10;
                timing_iret_pm_outer = 26;
                timing_call_rm = 4;
                timing_call_pm = 15;
                timing_call_pm_gate = 26;
                timing_call_pm_gate_inner = 35;
                timing_retf_rm       = 4;
                timing_retf_pm       = 7;
                timing_retf_pm_outer = 23;
                timing_jmp_rm      = 5;
                timing_jmp_pm      = 7;
                timing_jmp_pm_gate = 17;
                timing_misaligned = 2;
                cpu_cyrix_alignment = 1;
                break;

                case CPU_WINCHIP:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_winchip_0f, dynarec_ops_386, dynarec_ops_winchip_0f);
#else
                x86_setopcodes(ops_386, ops_winchip_0f);
#endif
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 2; /*register dest - memory src*/
                timing_mr  = 2; /*memory dest   - register src*/
                timing_mm  = 3;
                timing_rml = 2; /*register dest - memory src long*/
                timing_mrl = 2; /*memory dest   - register src long*/
                timing_mml = 3;
                timing_bt  = 3-1; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                cpu_features = CPU_FEATURE_RDTSC | CPU_FEATURE_MMX | CPU_FEATURE_MSR | CPU_FEATURE_CR4;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_CR4_mask = CR4_TSD | CR4_DE | CR4_MCE | CR4_PCE;
                /*unknown*/
                timing_int_rm       = 26;
                timing_int_v86      = 82;
                timing_int_pm       = 44;
                timing_int_pm_outer = 71;
                timing_iret_rm       = 7;
                timing_iret_v86      = 26;
                timing_iret_pm       = 10;
                timing_iret_pm_outer = 26;
                timing_call_rm = 4;
                timing_call_pm = 15;
                timing_call_pm_gate = 26;
                timing_call_pm_gate_inner = 35;
                timing_retf_rm       = 4;
                timing_retf_pm       = 7;
                timing_retf_pm_outer = 23;
                timing_jmp_rm      = 5;
                timing_jmp_pm      = 7;
                timing_jmp_pm_gate = 17;
#ifdef USE_DYNAREC
                codegen_timing_set(&codegen_timing_winchip);
#endif
                timing_misaligned = 2;
                cpu_cyrix_alignment = 1;
                break;

                case CPU_WINCHIP2:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_winchip2_0f, dynarec_ops_386, dynarec_ops_winchip2_0f);
#else
                x86_setopcodes(ops_386, ops_winchip2_0f);
#endif
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 2; /*register dest - memory src*/
                timing_mr  = 2; /*memory dest   - register src*/
                timing_mm  = 3;
                timing_rml = 2; /*register dest - memory src long*/
                timing_mrl = 2; /*memory dest   - register src long*/
                timing_mml = 3;
                timing_bt  = 3-1; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                cpu_features = CPU_FEATURE_RDTSC | CPU_FEATURE_MMX | CPU_FEATURE_MSR | CPU_FEATURE_CR4 | CPU_FEATURE_3DNOW;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 18) | (1 << 19) | (1 << 20) | (1 << 21);
                cpu_CR4_mask = CR4_TSD | CR4_DE | CR4_MCE | CR4_PCE;
                /*unknown*/
                timing_int_rm       = 26;
                timing_int_v86      = 82;
                timing_int_pm       = 44;
                timing_int_pm_outer = 71;
                timing_iret_rm       = 7;
                timing_iret_v86      = 26;
                timing_iret_pm       = 10;
                timing_iret_pm_outer = 26;
                timing_call_rm = 4;
                timing_call_pm = 15;
                timing_call_pm_gate = 26;
                timing_call_pm_gate_inner = 35;
                timing_retf_rm       = 4;
                timing_retf_pm       = 7;
                timing_retf_pm_outer = 23;
                timing_jmp_rm      = 5;
                timing_jmp_pm      = 7;
                timing_jmp_pm_gate = 17;
                timing_misaligned = 2;
                cpu_cyrix_alignment = 1;
#ifdef USE_DYNAREC		
                codegen_timing_set(&codegen_timing_winchip2);
#endif		
                break;

                case CPU_P24T:
                case CPU_PENTIUM:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_pentium_0f, dynarec_ops_386, dynarec_ops_pentium_0f);
#else
                x86_setopcodes(ops_386, ops_pentium_0f);
#endif
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 2; /*register dest - memory src*/
                timing_mr  = 3; /*memory dest   - register src*/
                timing_mm  = 3;
                timing_rml = 2; /*register dest - memory src long*/
                timing_mrl = 3; /*memory dest   - register src long*/
                timing_mml = 3;
                timing_bt  = 0; /*branch taken*/
                timing_bnt = 2; /*branch not taken*/
                timing_int = 6;
                timing_int_rm       = 11;
                timing_int_v86      = 54;
                timing_int_pm       = 25;
                timing_int_pm_outer = 42;
                timing_iret_rm       = 7;
                timing_iret_v86      = 27; /*unknown*/
                timing_iret_pm       = 10;
                timing_iret_pm_outer = 27;
                timing_call_rm = 4;
                timing_call_pm = 4;
                timing_call_pm_gate = 22;
                timing_call_pm_gate_inner = 44;
                timing_retf_rm       = 4;
                timing_retf_pm       = 4;
                timing_retf_pm_outer = 23;
                timing_jmp_rm      = 3;
                timing_jmp_pm      = 3;
                timing_jmp_pm_gate = 18;
                timing_misaligned = 3;
                cpu_features = CPU_FEATURE_RDTSC | CPU_FEATURE_MSR | CPU_FEATURE_CR4 | CPU_FEATURE_VME;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | CR4_PSE | CR4_MCE | CR4_PCE;
#ifdef USE_DYNAREC
                codegen_timing_set(&codegen_timing_pentium);
#endif
                break;

                case CPU_PENTIUMMMX:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_pentiummmx_0f, dynarec_ops_386, dynarec_ops_pentiummmx_0f);
#else
                x86_setopcodes(ops_386, ops_pentiummmx_0f);
#endif
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 2; /*register dest - memory src*/
                timing_mr  = 3; /*memory dest   - register src*/
                timing_mm  = 3;
                timing_rml = 2; /*register dest - memory src long*/
                timing_mrl = 3; /*memory dest   - register src long*/
                timing_mml = 3;
                timing_bt  = 0; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                timing_int = 6;
                timing_int_rm       = 11;
                timing_int_v86      = 54;
                timing_int_pm       = 25;
                timing_int_pm_outer = 42;
                timing_iret_rm       = 7;
                timing_iret_v86      = 27; /*unknown*/
                timing_iret_pm       = 10;
                timing_iret_pm_outer = 27;
                timing_call_rm = 4;
                timing_call_pm = 4;
                timing_call_pm_gate = 22;
                timing_call_pm_gate_inner = 44;
                timing_retf_rm       = 4;
                timing_retf_pm       = 4;
                timing_retf_pm_outer = 23;
                timing_jmp_rm      = 3;
                timing_jmp_pm      = 3;
                timing_jmp_pm_gate = 18;
                timing_misaligned = 3;
                cpu_features = CPU_FEATURE_RDTSC | CPU_FEATURE_MSR | CPU_FEATURE_CR4 | CPU_FEATURE_VME | CPU_FEATURE_MMX;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | CR4_PSE | CR4_MCE | CR4_PCE;
#ifdef USE_DYNAREC
                codegen_timing_set(&codegen_timing_pentium);
#endif
                break;

#if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
  		case CPU_Cx6x86:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_pentium_0f, dynarec_ops_386, dynarec_ops_pentium_0f);
#else
                x86_setopcodes(ops_386, ops_pentium_0f);
#endif
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 1; /*register dest - memory src*/
                timing_mr  = 2; /*memory dest   - register src*/
                timing_mm  = 2;
                timing_rml = 1; /*register dest - memory src long*/
                timing_mrl = 2; /*memory dest   - register src long*/
                timing_mml = 2;
                timing_bt  = 0; /*branch taken*/
                timing_bnt = 2; /*branch not taken*/
                timing_int_rm       = 9;
                timing_int_v86      = 46;
                timing_int_pm       = 21;
                timing_int_pm_outer = 32;
                timing_iret_rm       = 7;
                timing_iret_v86      = 26;
                timing_iret_pm       = 10;
                timing_iret_pm_outer = 26;
                timing_call_rm = 3;
                timing_call_pm = 4;
                timing_call_pm_gate = 15;
                timing_call_pm_gate_inner = 26;
                timing_retf_rm       = 4;
                timing_retf_pm       = 4;
                timing_retf_pm_outer = 23;
                timing_jmp_rm      = 1;
                timing_jmp_pm      = 4;
                timing_jmp_pm_gate = 14;
                timing_misaligned = 2;
                cpu_cyrix_alignment = 1;
                cpu_features = CPU_FEATURE_RDTSC;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
#ifdef USE_DYNAREC
  		codegen_timing_set(&codegen_timing_686);
#endif
  		CPUID = 0; /*Disabled on powerup by default*/
                break;

                case CPU_Cx6x86L:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_pentium_0f, dynarec_ops_386, dynarec_ops_pentium_0f);
#else
                x86_setopcodes(ops_386, ops_pentium_0f);
#endif
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 1; /*register dest - memory src*/
                timing_mr  = 2; /*memory dest   - register src*/
                timing_mm  = 2;
                timing_rml = 1; /*register dest - memory src long*/
                timing_mrl = 2; /*memory dest   - register src long*/
                timing_mml = 2;
                timing_bt  = 0; /*branch taken*/
                timing_bnt = 2; /*branch not taken*/
                timing_int_rm       = 9;
                timing_int_v86      = 46;
                timing_int_pm       = 21;
                timing_int_pm_outer = 32;
                timing_iret_rm       = 7;
                timing_iret_v86      = 26;
                timing_iret_pm       = 10;
                timing_iret_pm_outer = 26;
                timing_call_rm = 3;
                timing_call_pm = 4;
                timing_call_pm_gate = 15;
                timing_call_pm_gate_inner = 26;
                timing_retf_rm       = 4;
                timing_retf_pm       = 4;
                timing_retf_pm_outer = 23;
                timing_jmp_rm      = 1;
                timing_jmp_pm      = 4;
                timing_jmp_pm_gate = 14;
                timing_misaligned = 2;
                cpu_cyrix_alignment = 1;
                cpu_features = CPU_FEATURE_RDTSC;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
#ifdef USE_DYNAREC
         	codegen_timing_set(&codegen_timing_686);
#endif
         	ccr4 = 0x80;
                break;


                case CPU_CxGX1:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_pentium_0f, dynarec_ops_386, dynarec_ops_pentium_0f);
#else
                x86_setopcodes(ops_386, ops_pentium_0f);
#endif
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 1; /*register dest - memory src*/
                timing_mr  = 2; /*memory dest   - register src*/
                timing_mm  = 2;
                timing_rml = 1; /*register dest - memory src long*/
                timing_mrl = 2; /*memory dest   - register src long*/
                timing_mml = 2;
                timing_bt  = 5-1; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                timing_misaligned = 2;
                cpu_cyrix_alignment = 1;
                cpu_features = CPU_FEATURE_RDTSC | CPU_FEATURE_MSR | CPU_FEATURE_CR4;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_CR4_mask = CR4_TSD | CR4_DE | CR4_PCE;
#ifdef USE_DYNAREC
         	codegen_timing_set(&codegen_timing_686);
#endif
                break;

  
                case CPU_Cx6x86MX:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_c6x86mx_0f, dynarec_ops_386, dynarec_ops_c6x86mx_0f);
                x86_dynarec_opcodes_da_a16 = dynarec_ops_fpu_686_da_a16;
                x86_dynarec_opcodes_da_a32 = dynarec_ops_fpu_686_da_a32;
                x86_dynarec_opcodes_db_a16 = dynarec_ops_fpu_686_db_a16;
                x86_dynarec_opcodes_db_a32 = dynarec_ops_fpu_686_db_a32;
                x86_dynarec_opcodes_df_a16 = dynarec_ops_fpu_686_df_a16;
                x86_dynarec_opcodes_df_a32 = dynarec_ops_fpu_686_df_a32;
#else
                x86_setopcodes(ops_386, ops_c6x86mx_0f);
#endif
                x86_opcodes_da_a16 = ops_fpu_686_da_a16;
                x86_opcodes_da_a32 = ops_fpu_686_da_a32;
                x86_opcodes_db_a16 = ops_fpu_686_db_a16;
                x86_opcodes_db_a32 = ops_fpu_686_db_a32;
                x86_opcodes_df_a16 = ops_fpu_686_df_a16;
                x86_opcodes_df_a32 = ops_fpu_686_df_a32;
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 1; /*register dest - memory src*/
                timing_mr  = 2; /*memory dest   - register src*/
                timing_mm  = 2;
                timing_rml = 1; /*register dest - memory src long*/
                timing_mrl = 2; /*memory dest   - register src long*/
                timing_mml = 2;
                timing_bt  = 0; /*branch taken*/
                timing_bnt = 2; /*branch not taken*/
                timing_int_rm       = 9;
                timing_int_v86      = 46;
                timing_int_pm       = 21;
                timing_int_pm_outer = 32;
                timing_iret_rm       = 7;
                timing_iret_v86      = 26;
                timing_iret_pm       = 10;
                timing_iret_pm_outer = 26;
                timing_call_rm = 3;
                timing_call_pm = 4;
                timing_call_pm_gate = 15;
                timing_call_pm_gate_inner = 26;
                timing_retf_rm       = 4;
                timing_retf_pm       = 4;
                timing_retf_pm_outer = 23;
                timing_jmp_rm      = 1;
                timing_jmp_pm      = 4;
                timing_jmp_pm_gate = 14;
                timing_misaligned = 2;
                cpu_cyrix_alignment = 1;
                cpu_features = CPU_FEATURE_RDTSC | CPU_FEATURE_MSR | CPU_FEATURE_CR4 | CPU_FEATURE_MMX;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_CR4_mask = CR4_TSD | CR4_DE | CR4_PCE;
#ifdef USE_DYNAREC
         	codegen_timing_set(&codegen_timing_686);
#endif
         	ccr4 = 0x80;
                break;
#endif

#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
                case CPU_K5:
                case CPU_5K86:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_pentiummmx_0f, dynarec_ops_386, dynarec_ops_pentiummmx_0f);
#else
                x86_setopcodes(ops_386, ops_pentiummmx_0f);
#endif
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 2; /*register dest - memory src*/
                timing_mr  = 3; /*memory dest   - register src*/
                timing_mm  = 3;
                timing_rml = 2; /*register dest - memory src long*/
                timing_mrl = 3; /*memory dest   - register src long*/
                timing_mml = 3;
                timing_bt  = 0; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                timing_int = 6;
                timing_int_rm       = 11;
                timing_int_v86      = 54;
                timing_int_pm       = 25;
                timing_int_pm_outer = 42;
                timing_iret_rm       = 7;
                timing_iret_v86      = 27; /*unknown*/
                timing_iret_pm       = 10;
                timing_iret_pm_outer = 27;
                timing_call_rm = 4;
                timing_call_pm = 4;
                timing_call_pm_gate = 22;
                timing_call_pm_gate_inner = 44;
                timing_retf_rm       = 4;
                timing_retf_pm       = 4;
                timing_retf_pm_outer = 23;
                timing_jmp_rm      = 3;
                timing_jmp_pm      = 3;
                timing_jmp_pm_gate = 18;
                timing_misaligned = 3;
                cpu_features = CPU_FEATURE_RDTSC | CPU_FEATURE_MSR | CPU_FEATURE_CR4 | CPU_FEATURE_VME | CPU_FEATURE_MMX;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_CR4_mask = CR4_TSD | CR4_DE | CR4_MCE | CR4_PCE;
#ifdef USE_DYNAREC
                codegen_timing_set(&codegen_timing_k6);
#endif
                break;
#endif

                case CPU_K6:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_k6_0f, dynarec_ops_386, dynarec_ops_k6_0f);
#else
                x86_setopcodes(ops_386, ops_k6_0f);
#endif
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 2; /*register dest - memory src*/
                timing_mr  = 3; /*memory dest   - register src*/
                timing_mm  = 3;
                timing_rml = 2; /*register dest - memory src long*/
                timing_mrl = 3; /*memory dest   - register src long*/
                timing_mml = 3;
                timing_bt  = 0; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                timing_int = 6;
                timing_int_rm       = 11;
                timing_int_v86      = 54;
                timing_int_pm       = 25;
                timing_int_pm_outer = 42;
                timing_iret_rm       = 7;
                timing_iret_v86      = 27; /*unknown*/
                timing_iret_pm       = 10;
                timing_iret_pm_outer = 27;
                timing_call_rm = 4;
                timing_call_pm = 4;
                timing_call_pm_gate = 22;
                timing_call_pm_gate_inner = 44;
                timing_retf_rm       = 4;
                timing_retf_pm       = 4;
                timing_retf_pm_outer = 23;
                timing_jmp_rm      = 3;
                timing_jmp_pm      = 3;
                timing_jmp_pm_gate = 18;
                timing_misaligned = 3;
                cpu_features = CPU_FEATURE_RDTSC | CPU_FEATURE_MSR | CPU_FEATURE_CR4 | CPU_FEATURE_VME | CPU_FEATURE_MMX;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | CR4_PSE | CR4_MCE | CR4_PCE;
#ifdef USE_DYNAREC
                codegen_timing_set(&codegen_timing_k6);
#endif
                break;

                case CPU_K6_2:
                case CPU_K6_2C:
                case CPU_K6_3:
                case CPU_K6_2P:
                case CPU_K6_3P:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_k62_0f, dynarec_ops_386, dynarec_ops_k62_0f);
#else
                x86_setopcodes(ops_386, ops_k62_0f);
#endif
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 2; /*register dest - memory src*/
                timing_mr  = 3; /*memory dest   - register src*/
                timing_mm  = 3;
                timing_rml = 2; /*register dest - memory src long*/
                timing_mrl = 3; /*memory dest   - register src long*/
                timing_mml = 3;
                timing_bt  = 0; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                timing_int = 6;
                timing_int_rm       = 11;
                timing_int_v86      = 54;
                timing_int_pm       = 25;
                timing_int_pm_outer = 42;
                timing_iret_rm       = 7;
                timing_iret_v86      = 27; /*unknown*/
                timing_iret_pm       = 10;
                timing_iret_pm_outer = 27;
                timing_call_rm = 4;
                timing_call_pm = 4;
                timing_call_pm_gate = 22;
                timing_call_pm_gate_inner = 44;
                timing_retf_rm       = 4;
                timing_retf_pm       = 4;
                timing_retf_pm_outer = 23;
                timing_jmp_rm      = 3;
                timing_jmp_pm      = 3;
                timing_jmp_pm_gate = 18;
                timing_misaligned = 3;
                cpu_features = CPU_FEATURE_RDTSC | CPU_FEATURE_MSR | CPU_FEATURE_CR4 | CPU_FEATURE_VME | CPU_FEATURE_MMX | CPU_FEATURE_3DNOW;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | CR4_PSE | CR4_MCE;
#ifdef USE_DYNAREC
                codegen_timing_set(&codegen_timing_k6);
#endif
                break;

                case CPU_PENTIUMPRO:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_pentiumpro_0f, dynarec_ops_386, dynarec_ops_pentiumpro_0f);
                x86_dynarec_opcodes_da_a16 = dynarec_ops_fpu_686_da_a16;
                x86_dynarec_opcodes_da_a32 = dynarec_ops_fpu_686_da_a32;
                x86_dynarec_opcodes_db_a16 = dynarec_ops_fpu_686_db_a16;
                x86_dynarec_opcodes_db_a32 = dynarec_ops_fpu_686_db_a32;
                x86_dynarec_opcodes_df_a16 = dynarec_ops_fpu_686_df_a16;
                x86_dynarec_opcodes_df_a32 = dynarec_ops_fpu_686_df_a32;
#else
                x86_setopcodes(ops_386, ops_pentiumpro_0f);
#endif
                x86_opcodes_da_a16 = ops_fpu_686_da_a16;
                x86_opcodes_da_a32 = ops_fpu_686_da_a32;
                x86_opcodes_db_a16 = ops_fpu_686_db_a16;
                x86_opcodes_db_a32 = ops_fpu_686_db_a32;
                x86_opcodes_df_a16 = ops_fpu_686_df_a16;
                x86_opcodes_df_a32 = ops_fpu_686_df_a32;
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 2; /*register dest - memory src*/
                timing_mr  = 3; /*memory dest   - register src*/
                timing_mm  = 3;
                timing_rml = 2; /*register dest - memory src long*/
                timing_mrl = 3; /*memory dest   - register src long*/
                timing_mml = 3;
                timing_bt  = 0; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                timing_int = 6;
                timing_int_rm       = 11;
                timing_int_v86      = 54;
                timing_int_pm       = 25;
                timing_int_pm_outer = 42;
                timing_iret_rm       = 7;
                timing_iret_v86      = 27; /*unknown*/
                timing_iret_pm       = 10;
                timing_iret_pm_outer = 27;
                timing_call_rm = 4;
                timing_call_pm = 4;
                timing_call_pm_gate = 22;
                timing_call_pm_gate_inner = 44;
                timing_retf_rm       = 4;
                timing_retf_pm       = 4;
                timing_retf_pm_outer = 23;
                timing_jmp_rm      = 3;
                timing_jmp_pm      = 3;
                timing_jmp_pm_gate = 18;
                timing_misaligned = 3;
                cpu_features = CPU_FEATURE_RDTSC | CPU_FEATURE_MSR | CPU_FEATURE_CR4 | CPU_FEATURE_VME;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | CR4_PSE | CR4_PAE | CR4_MCE | CR4_PCE;
#ifdef USE_DYNAREC
         	codegen_timing_set(&codegen_timing_p6);
#endif
                break;

		case CPU_PENTIUM2:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_pentium2_0f, dynarec_ops_386, dynarec_ops_pentium2_0f);
                x86_dynarec_opcodes_da_a16 = dynarec_ops_fpu_686_da_a16;
                x86_dynarec_opcodes_da_a32 = dynarec_ops_fpu_686_da_a32;
                x86_dynarec_opcodes_db_a16 = dynarec_ops_fpu_686_db_a16;
                x86_dynarec_opcodes_db_a32 = dynarec_ops_fpu_686_db_a32;
                x86_dynarec_opcodes_df_a16 = dynarec_ops_fpu_686_df_a16;
                x86_dynarec_opcodes_df_a32 = dynarec_ops_fpu_686_df_a32;
#else
                x86_setopcodes(ops_386, ops_pentium2_0f);
#endif
                x86_opcodes_da_a16 = ops_fpu_686_da_a16;
                x86_opcodes_da_a32 = ops_fpu_686_da_a32;
                x86_opcodes_db_a16 = ops_fpu_686_db_a16;
                x86_opcodes_db_a32 = ops_fpu_686_db_a32;
                x86_opcodes_df_a16 = ops_fpu_686_df_a16;
                x86_opcodes_df_a32 = ops_fpu_686_df_a32;
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 2; /*register dest - memory src*/
                timing_mr  = 3; /*memory dest   - register src*/
                timing_mm  = 3;
                timing_rml = 2; /*register dest - memory src long*/
                timing_mrl = 3; /*memory dest   - register src long*/
                timing_mml = 3;
                timing_bt  = 0; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                timing_int = 6;
                timing_int_rm       = 11;
                timing_int_v86      = 54;
                timing_int_pm       = 25;
                timing_int_pm_outer = 42;
                timing_iret_rm       = 7;
                timing_iret_v86      = 27; /*unknown*/
                timing_iret_pm       = 10;
                timing_iret_pm_outer = 27;
                timing_call_rm = 4;
                timing_call_pm = 4;
                timing_call_pm_gate = 22;
                timing_call_pm_gate_inner = 44;
                timing_retf_rm       = 4;
                timing_retf_pm       = 4;
                timing_retf_pm_outer = 23;
                timing_jmp_rm      = 3;
                timing_jmp_pm      = 3;
                timing_jmp_pm_gate = 18;
                timing_misaligned = 3;
                cpu_features = CPU_FEATURE_RDTSC | CPU_FEATURE_MSR | CPU_FEATURE_CR4 | CPU_FEATURE_VME | CPU_FEATURE_MMX;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | CR4_PSE | CR4_PAE | CR4_MCE | CR4_PCE;
#ifdef USE_DYNAREC
         	codegen_timing_set(&codegen_timing_p6);
#endif
                break;

                case CPU_PENTIUM2D:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_pentium2d_0f, dynarec_ops_386, dynarec_ops_pentium2d_0f);
                x86_dynarec_opcodes_da_a16 = dynarec_ops_fpu_686_da_a16;
                x86_dynarec_opcodes_da_a32 = dynarec_ops_fpu_686_da_a32;
                x86_dynarec_opcodes_db_a16 = dynarec_ops_fpu_686_db_a16;
                x86_dynarec_opcodes_db_a32 = dynarec_ops_fpu_686_db_a32;
                x86_dynarec_opcodes_df_a16 = dynarec_ops_fpu_686_df_a16;
                x86_dynarec_opcodes_df_a32 = dynarec_ops_fpu_686_df_a32;
#else
                x86_setopcodes(ops_386, ops_pentium2d_0f);
#endif
                x86_opcodes_da_a16 = ops_fpu_686_da_a16;
                x86_opcodes_da_a32 = ops_fpu_686_da_a32;
                x86_opcodes_db_a16 = ops_fpu_686_db_a16;
                x86_opcodes_db_a32 = ops_fpu_686_db_a32;
                x86_opcodes_df_a16 = ops_fpu_686_df_a16;
                x86_opcodes_df_a32 = ops_fpu_686_df_a32;
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 2; /*register dest - memory src*/
                timing_mr  = 3; /*memory dest   - register src*/
                timing_mm  = 3;
                timing_rml = 2; /*register dest - memory src long*/
                timing_mrl = 3; /*memory dest   - register src long*/
                timing_mml = 3;
                timing_bt  = 0; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                timing_int = 6;
                timing_int_rm       = 11;
                timing_int_v86      = 54;
                timing_int_pm       = 25;
                timing_int_pm_outer = 42;
                timing_iret_rm       = 7;
                timing_iret_v86      = 27; /*unknown*/
                timing_iret_pm       = 10;
                timing_iret_pm_outer = 27;
                timing_call_rm = 4;
                timing_call_pm = 4;
                timing_call_pm_gate = 22;
                timing_call_pm_gate_inner = 44;
                timing_retf_rm       = 4;
                timing_retf_pm       = 4;
                timing_retf_pm_outer = 23;
                timing_jmp_rm      = 3;
                timing_jmp_pm      = 3;
                timing_jmp_pm_gate = 18;
                timing_misaligned = 3;
                cpu_features = CPU_FEATURE_RDTSC | CPU_FEATURE_MSR | CPU_FEATURE_CR4 | CPU_FEATURE_VME | CPU_FEATURE_MMX;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | CR4_PSE | CR4_MCE | CR4_PAE | CR4_PCE | CR4_OSFXSR;
#ifdef USE_DYNAREC
         	codegen_timing_set(&codegen_timing_p6);
#endif
                break;

                case CPU_CYRIX3S:
#ifdef USE_DYNAREC
                x86_setopcodes(ops_386, ops_winchip2_0f, dynarec_ops_386, dynarec_ops_winchip2_0f);
#else
                x86_setopcodes(ops_386, ops_winchip2_0f);
#endif
                timing_rr  = 1; /*register dest - register src*/
                timing_rm  = 2; /*register dest - memory src*/
                timing_mr  = 2; /*memory dest   - register src*/
                timing_mm  = 3;
                timing_rml = 2; /*register dest - memory src long*/
                timing_mrl = 2; /*memory dest   - register src long*/
                timing_mml = 3;
                timing_bt  = 3-1; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                cpu_features = CPU_FEATURE_RDTSC | CPU_FEATURE_MMX | CPU_FEATURE_MSR | CPU_FEATURE_CR4 | CPU_FEATURE_3DNOW;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 18) | (1 << 19) | (1 << 20) | (1 << 21);
                cpu_CR4_mask = CR4_TSD | CR4_DE | CR4_MCE | CR4_PCE;
                /*unknown*/
                timing_int_rm       = 26;
                timing_int_v86      = 82;
                timing_int_pm       = 44;
                timing_int_pm_outer = 71;
                timing_iret_rm       = 7;
                timing_iret_v86      = 26;
                timing_iret_pm       = 10;
                timing_iret_pm_outer = 26;
                timing_call_rm = 4;
                timing_call_pm = 15;
                timing_call_pm_gate = 26;
                timing_call_pm_gate_inner = 35;
                timing_retf_rm       = 4;
                timing_retf_pm       = 7;
                timing_retf_pm_outer = 23;
                timing_jmp_rm      = 5;
                timing_jmp_pm      = 7;
                timing_jmp_pm_gate = 17;
                timing_misaligned = 2;
                cpu_cyrix_alignment = 1;
#ifdef USE_DYNAREC
                codegen_timing_set(&codegen_timing_winchip);
#endif
                break;

                default:
                fatal("cpu_set : unknown CPU type %i\n", cpu_s->cpu_type);
        }

                
        switch (fpu_type)
        {
                case FPU_NONE:
                break;
		
                case FPU_8087:
                x87_timings = x87_timings_8087;
		break;
		
                case FPU_287:
                x87_timings = x87_timings_287;
                break;
		
                case FPU_287XL:
                case FPU_387:
                x87_timings = x87_timings_387;
	        break;
		
		default:
		x87_timings = x87_timings_486;
	}
}
char *
cpu_current_pc(char *bufp)
{
    static char buff[10];

    if (bufp == NULL)
	bufp = buff;

    sprintf(bufp, "%04X:%04X", CS, cpu_state.pc);

    return(bufp);
}


void
cpu_CPUID(void)
{
        switch (machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type)
        {
                case CPU_RAPIDCAD:
                case CPU_i486DX:
                case CPU_i486DX2:
                if (!EAX)
                {
                        EAX = 0x00000001;
                        EBX = 0x756e6547;
                        EDX = 0x49656e69;
                        ECX = 0x6c65746e;
                }
                else if (EAX == 1)
                {
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU; /*FPU*/
                }
                else
                   EAX = EBX = ECX = EDX = 0;
                break;

                case CPU_iDX4:
                if (!EAX)
                {
                        EAX = 0x00000001;
                        EBX = 0x756e6547;
                        EDX = 0x49656e69;
                        ECX = 0x6c65746e;
                }
                else if (EAX == 1)
                {
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU | CPUID_VME;
                }
                else
                   EAX = EBX = ECX = EDX = 0;
                break;

                case CPU_Am486SX:
                case CPU_Am486SX2:
                if (!EAX)
                {
                        EAX = 1;
                        EBX = 0x68747541;
                        ECX = 0x444D4163;
                        EDX = 0x69746E65;
                }
                else if (EAX == 1)
                {
                        EAX = CPUID;
                        EBX = ECX = EDX = 0; /*No FPU*/
                }
                else
                   EAX = EBX = ECX = EDX = 0;
                break;

                case CPU_Am486DX:
                case CPU_Am486DX2:
                case CPU_Am486DX4:
                case CPU_Am5x86:
                if (!EAX)
                {
                        EAX = 1;
                        EBX = 0x68747541;
                        ECX = 0x444D4163;
                        EDX = 0x69746E65;
                }
                else if (EAX == 1)
                {
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU; /*FPU*/
                }
                else
                   EAX = EBX = ECX = EDX = 0;
                break;
                
                case CPU_WINCHIP:
                if (!EAX)
                {
                        EAX = 1;
                        if (msr.fcr2 & (1 << 14))
                        {
                                EBX = msr.fcr3 >> 32;
                                ECX = msr.fcr3 & 0xffffffff;
                                EDX = msr.fcr2 >> 32;
                        }
                        else
                        {
                                EBX = 0x746e6543; /*CentaurHauls*/
                                ECX = 0x736c7561;                        
                                EDX = 0x48727561;
                        }
                }
                else if (EAX == 1)
                {
                        EAX = 0x540;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU | CPUID_TSC | CPUID_MSR;
                        if (cpu_has_feature(CPU_FEATURE_CX8))
                                EDX |= CPUID_CMPXCHG8B;
                        if (msr.fcr & (1 << 9))
                                EDX |= CPUID_MMX;
                }
                else
                   EAX = EBX = ECX = EDX = 0;
                break;

                case CPU_WINCHIP2:
                switch (EAX)
                {
                        case 0:
                        EAX = 1;
                        if (msr.fcr2 & (1 << 14))
                        {
                                EBX = msr.fcr3 >> 32;
                                ECX = msr.fcr3 & 0xffffffff;
                                EDX = msr.fcr2 >> 32;
                        }
                        else
                        {
                                EBX = 0x746e6543; /*CentaurHauls*/
                                ECX = 0x736c7561;                        
                                EDX = 0x48727561;
                        }
                        break;
                        case 1:
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU | CPUID_TSC | CPUID_MSR;
			if (cpu_has_feature(CPU_FEATURE_CX8))
				EDX |= CPUID_CMPXCHG8B;							
                        if (msr.fcr & (1 << 9))
                                EDX |= CPUID_MMX;
                        break;
                        case 0x80000000:
                        EAX = 0x80000005;
                        break;
                        case 0x80000001:
                        EAX = CPUID;
                        EDX = CPUID_FPU | CPUID_TSC | CPUID_MSR;
			if (cpu_has_feature(CPU_FEATURE_CX8))
				EDX |= CPUID_CMPXCHG8B;
                        if (msr.fcr & (1 << 9))
                                EDX |= CPUID_MMX;
			if (cpu_has_feature(CPU_FEATURE_3DNOW))
                                EDX |= CPUID_3DNOW;
                        break;
                                
                        case 0x80000002: /*Processor name string*/
                        EAX = 0x20544449; /*IDT WinChip 2-3D*/
                        EBX = 0x436e6957;
                        ECX = 0x20706968;
                        EDX = 0x44332d32;
                        break;
                        
                        case 0x80000005: /*Cache information*/
                        EBX = 0x08800880; /*TLBs*/
                        ECX = 0x20040120; /*L1 data cache*/
                        EDX = 0x20020120; /*L1 instruction cache*/
                        break;
                        
                        default:
                        EAX = EBX = ECX = EDX = 0;
                        break;
                }
                break;
		
                case CPU_P24T:
                case CPU_PENTIUM:
                if (!EAX)
                {
                        EAX = 0x00000001;
                        EBX = 0x756e6547;
                        EDX = 0x49656e69;
                        ECX = 0x6c65746e;
                }
                else if (EAX == 1)
                {
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B;
                }
                else
                        EAX = EBX = ECX = EDX = 0;
                break;

#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
                case CPU_K5:
                if (!EAX)
                {
                        EAX = 0x00000001;
                        EBX = 0x68747541;
                        EDX = 0x69746E65;
                        ECX = 0x444D4163;
                }
                else if (EAX == 1)
                {
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B;
                }
                else
                        EAX = EBX = ECX = EDX = 0;
                break;

                case CPU_5K86:
                if (!EAX)
                {
                        EAX = 0x00000001;
                        EBX = 0x68747541;
                        EDX = 0x69746E65;
                        ECX = 0x444D4163;
                }
                else if (EAX == 1)
                {
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B;
                }
                else if (EAX == 0x80000000)
                {
                        EAX = 0x80000005;
                        EBX = ECX = EDX = 0;
                }
                else if (EAX == 0x80000001)
                {
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B;
                }
		else if (EAX == 0x80000002)
		{
			EAX = 0x2D444D41;
			EBX = 0x7428354B;
			ECX = 0x5020296D;
			EDX = 0x65636F72;
		}
		else if (EAX == 0x80000003)
		{
			EAX = 0x726F7373;
			EBX = ECX = EDX = 0;
		}
		else if (EAX == 0x80000004)
		{
			EAX = EBX = ECX = EDX = 0;
		}
		else if (EAX == 0x80000005)
		{
			EAX = 0;
			EBX = 0x04800000;
			ECX = 0x08040120;
			EDX = 0x10040120;
		}
                else
                        EAX = EBX = ECX = EDX = 0;
                break;
#endif

                case CPU_K6:
                if (!EAX)
                {
                        EAX = 0x00000001;
                        EBX = 0x68747541;
                        EDX = 0x69746E65;
                        ECX = 0x444D4163;
                }
                else if (EAX == 1)
                {
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B | CPUID_MMX;
                }
                else if (EAX == 0x80000000)
                {
                        EAX = 0x80000005;
                        EBX = ECX = EDX = 0;
                }
                else if (EAX == 0x80000001)
                {
                        EAX = CPUID + 0x100;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B | CPUID_AMDSEP | CPUID_MMX;
                }
		else if (EAX == 0x80000002)
		{
			EAX = 0x2D444D41;
			EBX = 0x6D74364B;
			ECX = 0x202F7720;
			EDX = 0x746C756D;
		}
		else if (EAX == 0x80000003)
		{
			EAX = 0x64656D69;
			EBX = 0x65206169;
			ECX = 0x6E657478;
			EDX = 0x6E6F6973;
		}
		else if (EAX == 0x80000004)
		{
			EAX = 0x73;
			EBX = ECX = EDX = 0;
		}
		else if (EAX == 0x80000005)
		{
			EAX = 0;
			EBX = 0x02800140;
			ECX = 0x20020220;
			EDX = 0x20020220;
		}
		else if (EAX == 0x8FFFFFFF)
		{
			EAX = 0x4778654E;
			EBX = 0x72656E65;
			ECX = 0x6F697461;
			EDX = 0x444D416E;
		}
                else
                        EAX = EBX = ECX = EDX = 0;
                break;

                case CPU_K6_2:
                case CPU_K6_2C:
                switch (EAX)
                {
                        case 0:
                        EAX = 1;
                        EBX = 0x68747541; /*AuthenticAMD*/
                        ECX = 0x444d4163;
                        EDX = 0x69746e65;
                        break;
                        case 1:
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B | CPUID_MMX;
                        break;
                        case 0x80000000:
                        EAX = 0x80000005;
                        break;
                        case 0x80000001:
                        EAX = CPUID+0x100;
                        EDX = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B | CPUID_AMDSEP | CPUID_MMX | CPUID_3DNOW;
                        break;

                        case 0x80000002: /*Processor name string*/
                        EAX = 0x2d444d41; /*AMD-K6(tm) 3D pr*/
                        EBX = 0x7428364b;
                        ECX = 0x3320296d;
                        EDX = 0x72702044;
                        break;

                        case 0x80000003: /*Processor name string*/
                        EAX = 0x7365636f; /*ocessor*/
                        EBX = 0x00726f73;
                        ECX = 0x00000000;
                        EDX = 0x00000000;
                        break;

                        case 0x80000005: /*Cache information*/
                        EBX = 0x02800140; /*TLBs*/
                        ECX = 0x20020220; /*L1 data cache*/
                        EDX = 0x20020220; /*L1 instruction cache*/
                        break;

                        default:
                        EAX = EBX = ECX = EDX = 0;
                        break;
                }
                break;

                case CPU_K6_3:
                switch (EAX)
                {
                        case 0:
                        EAX = 1;
                        EBX = 0x68747541; /*AuthenticAMD*/
                        ECX = 0x444d4163;
                        EDX = 0x69746e65;
                        break;
                        case 1:
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B | CPUID_MMX;
                        break;
                        case 0x80000000:
                        EAX = 0x80000006;
                        break;
                        case 0x80000001:
                        EAX = CPUID+0x100;
                        EDX = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B | CPUID_AMDSEP | CPUID_MMX | CPUID_3DNOW;
                        break;

                        case 0x80000002: /*Processor name string*/
                        EAX = 0x2d444d41; /*AMD-K6(tm) 3D+ P*/
                        EBX = 0x7428364b;
                        ECX = 0x3320296d;
                        EDX = 0x50202b44;
                        break;

                        case 0x80000003: /*Processor name string*/
                        EAX = 0x65636f72; /*rocessor*/
                        EBX = 0x726f7373;
                        ECX = 0x00000000;
                        EDX = 0x00000000;
                        break;

                        case 0x80000005: /*Cache information*/
                        EBX = 0x02800140; /*TLBs*/
                        ECX = 0x20020220; /*L1 data cache*/
                        EDX = 0x20020220; /*L1 instruction cache*/
                        break;

                        case 0x80000006: /*L2 Cache information*/
                        ECX = 0x01004220;
                        break;
                        
                        default:
                        EAX = EBX = ECX = EDX = 0;
                        break;
                }
                break;

                case CPU_K6_2P:
                case CPU_K6_3P:
                switch (EAX)
                {
                        case 0:
                        EAX = 1;
                        EBX = 0x68747541; /*AuthenticAMD*/
                        ECX = 0x444d4163;
                        EDX = 0x69746e65;
                        break;
                        case 1:
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B | CPUID_MMX;
                        break;
                        case 0x80000000:
                        EAX = 0x80000007;
                        break;
                        case 0x80000001:
                        EAX = CPUID+0x100;
                        EDX = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B | CPUID_AMDSEP | CPUID_MMX | CPUID_3DNOW;
                        break;

                        case 0x80000002: /*Processor name string*/
                        EAX = 0x2d444d41; /*AMD-K6(tm)-III P*/
                        EBX = 0x7428364b;
                        ECX = 0x492d296d;
                        EDX = 0x50204949;
                        break;

                        case 0x80000003: /*Processor name string*/
                        EAX = 0x65636f72; /*rocessor*/
                        EBX = 0x726f7373;
                        ECX = 0x00000000;
                        EDX = 0x00000000;
                        break;

                        case 0x80000005: /*Cache information*/
                        EBX = 0x02800140; /*TLBs*/
                        ECX = 0x20020220; /*L1 data cache*/
                        EDX = 0x20020220; /*L1 instruction cache*/
                        break;

                        case 0x80000006: /*L2 Cache information*/
                        if (machines[machine].cpu[cpu_manufacturer].cpus[cpu].cpu_type == CPU_K6_3P)
                                ECX = 0x01004220;
                        else
                                ECX = 0x00804220;
                        break;

                        case 0x80000007: /*PowerNow information*/
                        EDX = 7;
                        break;

                        default:
                        EAX = EBX = ECX = EDX = 0;
                        break;
                }
                break;

                case CPU_PENTIUMMMX:
                if (!EAX)
                {
                        EAX = 0x00000001;
                        EBX = 0x756e6547;
                        EDX = 0x49656e69;
                        ECX = 0x6c65746e;
                }
                else if (EAX == 1)
                {
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B | CPUID_MMX;
                }
                else
                        EAX = EBX = ECX = EDX = 0;
                break;


#if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
                case CPU_Cx6x86:
                if (!EAX)
                {
                        EAX = 0x00000001;
                        EBX = 0x69727943;
                        EDX = 0x736e4978;
                        ECX = 0x64616574;
                }
                else if (EAX == 1)
                {
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU;
                }
                else
                        EAX = EBX = ECX = EDX = 0;
                break;


                case CPU_Cx6x86L:
                if (!EAX)
                {
                        EAX = 0x00000001;
                        EBX = 0x69727943;
                        EDX = 0x736e4978;
                        ECX = 0x64616574;
                }
                else if (EAX == 1)
                {
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU | CPUID_CMPXCHG8B;
                }
                else
                        EAX = EBX = ECX = EDX = 0;
                break;


                case CPU_CxGX1:
                if (!EAX)
                {
                        EAX = 0x00000001;
                        EBX = 0x69727943;
                        EDX = 0x736e4978;
                        ECX = 0x64616574;
                }
                else if (EAX == 1)
                {
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B;
                }
                else
                        EAX = EBX = ECX = EDX = 0;
                break;



                case CPU_Cx6x86MX:
                if (!EAX)
                {
                        EAX = 0x00000001;
                        EBX = 0x69727943;
                        EDX = 0x736e4978;
                        ECX = 0x64616574;
                }
                else if (EAX == 1)
                {
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B | CPUID_CMOV | CPUID_MMX;
                }
                else
                        EAX = EBX = ECX = EDX = 0;
                break;
#endif

                case CPU_PENTIUMPRO:
                if (!EAX)
                {
                        EAX = 0x00000002;
                        EBX = 0x756e6547;
                        EDX = 0x49656e69;
                        ECX = 0x6c65746e;
                }
                else if (EAX == 1)
                {
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_PAE | CPUID_CMPXCHG8B | CPUID_MTRR | CPUID_SEP | CPUID_CMOV;
                }
		else if (EAX == 2)
		{
			EAX = 0x00000001;
			EBX = ECX = 0;
			EDX = 0x00000000;
		}
                else
                        EAX = EBX = ECX = EDX = 0;
                break;

                case CPU_PENTIUM2:
                if (!EAX)
                {
                        EAX = 0x00000002;
                        EBX = 0x756e6547;
                        EDX = 0x49656e69;
                        ECX = 0x6c65746e;
                }
                else if (EAX == 1)
                {
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_PAE | CPUID_CMPXCHG8B | CPUID_MMX | CPUID_MTRR/* | CPUID_SEP*/ | CPUID_CMOV;
#ifdef USE_SEP
			EDX |= CPUID_SEP;
#endif
                }
		else if (EAX == 2)
		{
			EAX = 0x00000001;
			EBX = ECX = 0;
			EDX = 0x00000000;
		}
                else
                        EAX = EBX = ECX = EDX = 0;
                break;

                case CPU_PENTIUM2D:
                if (!EAX)
                {
                        EAX = 0x00000002;
                        EBX = 0x756e6547;
                        EDX = 0x49656e69;
                        ECX = 0x6c65746e;
                }
                else if (EAX == 1)
                {
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_PAE | CPUID_CMPXCHG8B | CPUID_MMX | CPUID_MTRR/* | CPUID_SEP*/ | CPUID_FXSR | CPUID_CMOV;
#ifdef USE_SEP
			EDX |= CPUID_SEP;
#endif
                }
		else if (EAX == 2)
		{
			EAX = 0x00000001;
			EBX = ECX = 0;
			EDX = 0x00000000;
		}
                else
                        EAX = EBX = ECX = EDX = 0;
                break;

                case CPU_CYRIX3S:
                switch (EAX)
                {
                        case 0:
                        EAX = 1;
                        if (msr.fcr2 & (1 << 14))
                        {
                                EBX = msr.fcr3 >> 32;
                                ECX = msr.fcr3 & 0xffffffff;
                                EDX = msr.fcr2 >> 32;
                        }
                        else
                        {
                                EBX = 0x746e6543; /*CentaurHauls*/
                                ECX = 0x736c7561;                        
                                EDX = 0x48727561;
                        }
                        break;
                        case 1:
                        EAX = CPUID;
                        EBX = ECX = 0;
                        EDX = CPUID_FPU | CPUID_TSC | CPUID_MSR | CPUID_MMX | CPUID_MTRR;
                        if (cpu_has_feature(CPU_FEATURE_CX8))
                                EDX |= CPUID_CMPXCHG8B;							
                        break;
                        case 0x80000000:
                        EAX = 0x80000005;
                        break;
                        case 0x80000001:
                        EAX = CPUID;
                        EDX = CPUID_FPU | CPUID_TSC | CPUID_MSR | CPUID_MMX | CPUID_MTRR | CPUID_3DNOW;
                        if (cpu_has_feature(CPU_FEATURE_CX8))
                                EDX |= CPUID_CMPXCHG8B;
                        break;                                
                        case 0x80000002: /*Processor name string*/
                        EAX = 0x20414956; /*VIA Samuel*/
                        EBX = 0x756d6153;
                        ECX = 0x00006c65;
                        EDX = 0x00000000;
                        break;
                       
                        case 0x80000005: /*Cache information*/
                        EBX = 0x08800880; /*TLBs*/
                        ECX = 0x40040120; /*L1 data cache*/
                        EDX = 0x40020120; /*L1 instruction cache*/
                        break;
                        
                        default:
                        EAX = EBX = ECX = EDX = 0;
                        break;
                }
                break;
        }
}

void cpu_ven_reset(void)
{
        switch (machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type)
        {
#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
                case CPU_K5:
                case CPU_5K86:
#endif
                case CPU_K6:
			amd_efer = amd_whcr = 0ULL;
			break;
		case CPU_K6_2:
			amd_efer = amd_whcr = 0ULL;
			star = 0ULL;
			break;
		case CPU_K6_2C:
			amd_efer = 2ULL;
			amd_whcr = star = 0ULL;
			amd_psor = 0x018cULL;
			amd_uwccr = 0ULL;
			break;
		case CPU_K6_3:
			amd_efer = 2ULL;
			amd_whcr = star = 0ULL;
			amd_psor = 0x008cULL;
			amd_uwccr = 0ULL;
			amd_pfir = amd_l2aar = 0ULL;
			break;
		case CPU_K6_2P:
		case CPU_K6_3P:
			amd_efer = 2ULL;
			amd_whcr = star = 0ULL;
			amd_psor = 0x008cULL;
			amd_uwccr = 0ULL;
			amd_pfir = amd_l2aar = 0ULL;
			amd_epmr = 0ULL;
			break;
	}
}

void cpu_RDMSR()
{
        switch (machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type)
        {
                case CPU_WINCHIP:
                case CPU_WINCHIP2:
                EAX = EDX = 0;
                switch (ECX)
                {
                        case 0x02:
                        EAX = msr.tr1;
                        break;
                        case 0x0e:
                        EAX = msr.tr12;
                        break;
                        case 0x10:
                        EAX = tsc & 0xffffffff;
                        EDX = tsc >> 32;
                        break;
                        case 0x11:
                        EAX = msr.cesr;
                        break;
                        case 0x107:
                        EAX = msr.fcr;
                        break;
                        case 0x108:
                        EAX = msr.fcr2 & 0xffffffff;
                        EDX = msr.fcr2 >> 32;
                        break;
                        case 0x10a:
                        EAX = cpu_multi & 3;
                        break;
                }
                break;
		
		case CPU_CYRIX3S:
                EAX = EDX = 0;
                switch (ECX)
                {
                        case 0x10:
                        EAX = tsc & 0xffffffff;
                        EDX = tsc >> 32;
                        break;
                        case 0x2A:
                        EAX = 0xC4000000;
                        EDX = 0;
                        if (cpu_dmulti == 3)
                        EAX |= ((0 << 25) | (0 << 24) | (0 << 23) | (1 << 22));
                        else if (cpu_dmulti == 3.5)
                        EAX |= ((0 << 25) | (1 << 24) | (0 << 23) | (1 << 22));
                        else if (cpu_dmulti == 4)
                        EAX |= ((0 << 25) | (0 << 24) | (1 << 23) | (0 << 22));
                        else if (cpu_dmulti == 4.5)
                        EAX |= ((0 << 25) | (1 << 24) | (1 << 23) | (0 << 22));
                        else if (cpu_dmulti == 5)
                        EAX |= 0;
                        else if (cpu_dmulti == 5.5)
                        EAX |= ((0 << 25) | (1 << 24) | (0 << 23) | (0 << 22));
                        else if (cpu_dmulti == 6)
                        EAX |= ((1 << 25) | (0 << 24) | (1 << 23) | (1 << 22));
                        else if (cpu_dmulti == 6.5)
                        EAX |= ((1 << 25) | (1 << 24) | (1 << 23) | (1 << 22));
                        else if (cpu_dmulti == 7)
                        EAX |= ((1 << 25) | (0 << 24) | (0 << 23) | (1 << 22));
                        else
                        EAX |= ((0 << 25) | (0 << 24) | (0 << 23) | (1 << 22));			
                        if (cpu_busspeed >= 84000000)
                        EAX |= (1 << 19);
                        break;
                        case 0x1107:
                        EAX = msr.fcr;
                        break;
                        case 0x1108:
                        EAX = msr.fcr2 & 0xffffffff;
                        EDX = msr.fcr2 >> 32;
                        break;
                        case 0x200: case 0x201: case 0x202: case 0x203: case 0x204: case 0x205: case 0x206: case 0x207:
                        case 0x208: case 0x209: case 0x20A: case 0x20B: case 0x20C: case 0x20D: case 0x20E: case 0x20F:
                        if (ECX & 1)
                        {
                                EAX = mtrr_physmask_msr[(ECX - 0x200) >> 1] & 0xffffffff;
                                EDX = mtrr_physmask_msr[(ECX - 0x200) >> 1] >> 32;
                        }
                        else
                        {
                                EAX = mtrr_physbase_msr[(ECX - 0x200) >> 1] & 0xffffffff;
                                EDX = mtrr_physbase_msr[(ECX - 0x200) >> 1] >> 32;
                        }
                        break;
                        case 0x250:
                        EAX = mtrr_fix64k_8000_msr & 0xffffffff;
                        EDX = mtrr_fix64k_8000_msr >> 32;
                        break;
                        case 0x258:
                        EAX = mtrr_fix16k_8000_msr & 0xffffffff;
                        EDX = mtrr_fix16k_8000_msr >> 32;
                        break;
                        case 0x259:
                        EAX = mtrr_fix16k_a000_msr & 0xffffffff;
                        EDX = mtrr_fix16k_a000_msr >> 32;
                        break;
                        case 0x268: case 0x269: case 0x26A: case 0x26B: case 0x26C: case 0x26D: case 0x26E: case 0x26F:
                        EAX = mtrr_fix4k_msr[ECX - 0x268] & 0xffffffff;
                        EDX = mtrr_fix4k_msr[ECX - 0x268] >> 32;
                        break;
                        case 0x2FF:
                        EAX = mtrr_deftype_msr & 0xffffffff;
                        EDX = mtrr_deftype_msr >> 32;
                        break;			
                }
                break;
		
#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
                case CPU_K5:
                case CPU_5K86:
#endif
                case CPU_K6:
                EAX = EDX = 0;
                switch (ECX)
                {
                        case 0x0000000e:
                        EAX = msr.tr12;
                        break;
                        case 0x00000010:
                        EAX = tsc & 0xffffffff;
                        EDX = tsc >> 32;
                        break;
                        case 0x00000083:
                        EAX = ecx83_msr & 0xffffffff;
                        EDX = ecx83_msr >> 32;
                        break;
                        case 0xC0000080:
                        EAX = amd_efer & 0xffffffff;
                        EDX = amd_efer >> 32;
                        break;
                        case 0xC0000082:
                        EAX = amd_whcr & 0xffffffff;
                        EDX = amd_whcr >> 32;
                        break;
			default:
			x86gpf(NULL, 0);
			break;
                }
                break;

                case CPU_K6_2:
                EAX = EDX = 0;
                switch (ECX)
                {
                        case 0x0000000e:
                        EAX = msr.tr12;
                        break;
                        case 0x00000010:
                        EAX = tsc & 0xffffffff;
                        EDX = tsc >> 32;
                        break;
                        case 0x00000083:
                        EAX = ecx83_msr & 0xffffffff;
                        EDX = ecx83_msr >> 32;
                        break;
                        case 0xC0000080:
                        EAX = amd_efer & 0xffffffff;
                        EDX = amd_efer >> 32;
                        break;
                        case 0xC0000081:
                        EAX = star & 0xffffffff;
                        EDX = star >> 32;
                        break;
                        case 0xC0000082:
                        EAX = amd_whcr & 0xffffffff;
                        EDX = amd_whcr >> 32;
                        break;
			default:
			x86gpf(NULL, 0);
			break;
                }
                break;

                case CPU_K6_2C:
                EAX = EDX = 0;
                switch (ECX)
                {
                        case 0x0000000e:
                        EAX = msr.tr12;
                        break;
                        case 0x00000010:
                        EAX = tsc & 0xffffffff;
                        EDX = tsc >> 32;
                        break;
                        case 0x00000083:
                        EAX = ecx83_msr & 0xffffffff;
                        EDX = ecx83_msr >> 32;
                        break;
                        case 0xC0000080:
                        EAX = amd_efer & 0xffffffff;
                        EDX = amd_efer >> 32;
                        break;
                        case 0xC0000081:
                        EAX = star & 0xffffffff;
                        EDX = star >> 32;
                        break;
                        case 0xC0000082:
                        EAX = amd_whcr & 0xffffffff;
                        EDX = amd_whcr >> 32;
                        break;
                        case 0xC0000085:
                        EAX = amd_uwccr & 0xffffffff;
                        EDX = amd_uwccr >> 32;
                        break;
                        case 0xC0000087:
                        EAX = amd_psor & 0xffffffff;
                        EDX = amd_psor >> 32;
                        break;
                        case 0xC0000088:
                        EAX = amd_pfir & 0xffffffff;
                        EDX = amd_pfir >> 32;
                        break;
			default:
			x86gpf(NULL, 0);
			break;
                }
                break;

                case CPU_K6_3:
                EAX = EDX = 0;
                switch (ECX)
                {
                        case 0x0000000e:
                        EAX = msr.tr12;
                        break;
                        case 0x00000010:
                        EAX = tsc & 0xffffffff;
                        EDX = tsc >> 32;
                        break;
                        case 0x00000083:
                        EAX = ecx83_msr & 0xffffffff;
                        EDX = ecx83_msr >> 32;
                        break;
                        case 0xC0000080:
                        EAX = amd_efer & 0xffffffff;
                        EDX = amd_efer >> 32;
                        break;
                        case 0xC0000081:
                        EAX = star & 0xffffffff;
                        EDX = star >> 32;
                        break;
                        case 0xC0000082:
                        EAX = amd_whcr & 0xffffffff;
                        EDX = amd_whcr >> 32;
                        break;
                        case 0xC0000085:
                        EAX = amd_uwccr & 0xffffffff;
                        EDX = amd_uwccr >> 32;
                        break;
                        case 0xC0000087:
                        EAX = amd_psor & 0xffffffff;
                        EDX = amd_psor >> 32;
                        break;
                        case 0xC0000088:
                        EAX = amd_pfir & 0xffffffff;
                        EDX = amd_pfir >> 32;
                        break;
                        case 0xC0000089:
                        EAX = amd_l2aar & 0xffffffff;
                        EDX = amd_l2aar >> 32;
                        break;
			default:
			x86gpf(NULL, 0);
			break;
                }
                break;

                case CPU_K6_2P:
                case CPU_K6_3P:
                EAX = EDX = 0;
                switch (ECX)
                {
                        case 0x0000000e:
                        EAX = msr.tr12;
                        break;
                        case 0x00000010:
                        EAX = tsc & 0xffffffff;
                        EDX = tsc >> 32;
                        break;
                        case 0x00000083:
                        EAX = ecx83_msr & 0xffffffff;
                        EDX = ecx83_msr >> 32;
                        break;
                        case 0xC0000080:
                        EAX = amd_efer & 0xffffffff;
                        EDX = amd_efer >> 32;
                        break;
                        case 0xC0000081:
                        EAX = star & 0xffffffff;
                        EDX = star >> 32;
                        break;
                        case 0xC0000082:
                        EAX = amd_whcr & 0xffffffff;
                        EDX = amd_whcr >> 32;
                        break;
                        case 0xC0000085:
                        EAX = amd_uwccr & 0xffffffff;
                        EDX = amd_uwccr >> 32;
                        break;
                        case 0xC0000086:
                        EAX = amd_epmr & 0xffffffff;
                        EDX = amd_epmr >> 32;
                        break;
                        case 0xC0000087:
                        EAX = amd_psor & 0xffffffff;
                        EDX = amd_psor >> 32;
                        break;
                        case 0xC0000088:
                        EAX = amd_pfir & 0xffffffff;
                        EDX = amd_pfir >> 32;
                        break;
                        case 0xC0000089:
                        EAX = amd_l2aar & 0xffffffff;
                        EDX = amd_l2aar >> 32;
                        break;
			default:
			x86gpf(NULL, 0);
			break;
                }
                break;

                case CPU_P24T:		
                case CPU_PENTIUM:
                case CPU_PENTIUMMMX:
                EAX = EDX = 0;
                switch (ECX)
                {
                        case 0x10:
				EAX = tsc & 0xffffffff;
				EDX = tsc >> 32;
				break;
                }
                break;
#if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
                case CPU_Cx6x86:
                case CPU_Cx6x86L:
                case CPU_CxGX1:
                case CPU_Cx6x86MX:
                switch (ECX)
                {
                        case 0x10:
                        EAX = tsc & 0xffffffff;
                        EDX = tsc >> 32;
                        break;
                }
 		break;
#endif

                case CPU_PENTIUMPRO:
                case CPU_PENTIUM2:
                case CPU_PENTIUM2D:
                EAX = EDX = 0;
                switch (ECX)
                {
                        case 0x10:
                        EAX = tsc & 0xffffffff;
                        EDX = tsc >> 32;
                        break;
                        case 0x17:
			if (machines[machine].cpu[cpu_manufacturer].cpus[cpu].cpu_type != CPU_PENTIUM2D)  goto i686_invalid_rdmsr;
                        EAX = ecx17_msr & 0xffffffff;
                        EDX = ecx17_msr >> 32;
                        break;
			case 0x1B:
                        EAX = apic_base_msr & 0xffffffff;
                        EDX = apic_base_msr >> 32;
			/* pclog("APIC_BASE read : %08X%08X\n", EDX, EAX); */
			break;
			case 0x2A:
                        EAX = 0xC4000000;
                        EDX = 0;
                        if (cpu_dmulti == 2.5)
                        EAX |= ((0 << 25) | (1 << 24) | (1 << 23) | (1 << 22));
                        else if (cpu_dmulti == 3)
                        EAX |= ((0 << 25) | (0 << 24) | (0 << 23) | (1 << 22));
                        else if (cpu_dmulti == 3.5)
                        EAX |= ((0 << 25) | (1 << 24) | (0 << 23) | (1 << 22));
                        else if (cpu_dmulti == 4)
                        EAX |= ((0 << 25) | (0 << 24) | (1 << 23) | (0 << 22));
                        else if (cpu_dmulti == 4.5)
                        EAX |= ((0 << 25) | (1 << 24) | (1 << 23) | (0 << 22));
                        else if (cpu_dmulti == 5)
                        EAX |= 0;
                        else if (cpu_dmulti == 5.5)
                        EAX |= ((0 << 25) | (1 << 24) | (0 << 23) | (0 << 22));
                        else if (cpu_dmulti == 6)
                        EAX |= ((1 << 25) | (0 << 24) | (1 << 23) | (1 << 22));
                        else if (cpu_dmulti == 6.5)
                        EAX |= ((1 << 25) | (1 << 24) | (1 << 23) | (1 << 22));
                        else if (cpu_dmulti == 7)
                        EAX |= ((1 << 25) | (0 << 24) | (0 << 23) | (1 << 22));
                        else if (cpu_dmulti == 7.5)
                        EAX |= ((1 << 25) | (1 << 24) | (0 << 23) | (1 << 22));	
                        else if (cpu_dmulti == 8)
                        EAX |= ((1 << 25) | (0 << 24) | (1 << 23) | (0 << 22));			
                        else
                        EAX |= ((0 << 25) | (1 << 24) | (1 << 23) | (1 << 22));		
                        if (machines[machine].cpu[cpu_manufacturer].cpus[cpu].cpu_type != CPU_PENTIUMPRO) {
                        if (cpu_busspeed >= 84000000)
                        EAX |= (1 << 19);
                        }
			break;
                        case 0x79:
                        EAX = ecx79_msr & 0xffffffff;
                        EDX = ecx79_msr >> 32;
                        break;
			case 0x88: case 0x89: case 0x8A: case 0x8B:
                        EAX = ecx8x_msr[ECX - 0x88] & 0xffffffff;
                        EDX = ecx8x_msr[ECX - 0x88] >> 32;
			break;
			case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: case 0xC6: case 0xC7: case 0xC8:
                        EAX = msr_ia32_pmc[ECX - 0xC1] & 0xffffffff;
                        EDX = msr_ia32_pmc[ECX - 0xC1] >> 32;
			break;
			case 0xFE:
                        EAX = mtrr_cap_msr & 0xffffffff;
                        EDX = mtrr_cap_msr >> 32;
                        break;
			case 0x116:
                        EAX = ecx116_msr & 0xffffffff;
                        EDX = ecx116_msr >> 32;
			break;
			case 0x118: case 0x119: case 0x11A: case 0x11B:
                        EAX = ecx11x_msr[ECX - 0x118] & 0xffffffff;
                        EDX = ecx11x_msr[ECX - 0x118] >> 32;
			break;
			case 0x11E:
                        EAX = ecx11e_msr & 0xffffffff;
                        EDX = ecx11e_msr >> 32;
			break;
			case 0x174:
			if (machines[machine].cpu[cpu_manufacturer].cpus[cpu].cpu_type == CPU_PENTIUMPRO)  goto i686_invalid_rdmsr;
			EAX &= 0xFFFF0000;
			EAX |= cs_msr;
			EDX = 0x00000000;
			break;
			case 0x175:
			if (machines[machine].cpu[cpu_manufacturer].cpus[cpu].cpu_type == CPU_PENTIUMPRO)  goto i686_invalid_rdmsr;
			EAX = esp_msr;
			EDX = 0x00000000;
			break;
			case 0x176:
			if (machines[machine].cpu[cpu_manufacturer].cpus[cpu].cpu_type == CPU_PENTIUMPRO)  goto i686_invalid_rdmsr;
			EAX = eip_msr;
			EDX = 0x00000000;
			break;
			case 0x179:
			EAX = EDX = 0x00000000;
			break;
			case 0x186:
                        EAX = ecx186_msr & 0xffffffff;
                        EDX = ecx186_msr >> 32;
			break;
			case 0x187:
                        EAX = ecx187_msr & 0xffffffff;
                        EDX = ecx187_msr >> 32;
			break;
			case 0x1E0:
                        EAX = ecx1e0_msr & 0xffffffff;
                        EDX = ecx1e0_msr >> 32;
			break;
			case 0x200: case 0x201: case 0x202: case 0x203: case 0x204: case 0x205: case 0x206: case 0x207:
			case 0x208: case 0x209: case 0x20A: case 0x20B: case 0x20C: case 0x20D: case 0x20E: case 0x20F:
			if (ECX & 1)
			{
                        	EAX = mtrr_physmask_msr[(ECX - 0x200) >> 1] & 0xffffffff;
	                        EDX = mtrr_physmask_msr[(ECX - 0x200) >> 1] >> 32;
			}
			else
			{
                        	EAX = mtrr_physbase_msr[(ECX - 0x200) >> 1] & 0xffffffff;
	                        EDX = mtrr_physbase_msr[(ECX - 0x200) >> 1] >> 32;
			}
			break;
			case 0x250:
                        EAX = mtrr_fix64k_8000_msr & 0xffffffff;
                        EDX = mtrr_fix64k_8000_msr >> 32;
			break;
			case 0x258:
                        EAX = mtrr_fix16k_8000_msr & 0xffffffff;
                        EDX = mtrr_fix16k_8000_msr >> 32;
			break;
			case 0x259:
                        EAX = mtrr_fix16k_a000_msr & 0xffffffff;
                        EDX = mtrr_fix16k_a000_msr >> 32;
			break;
			case 0x268: case 0x269: case 0x26A: case 0x26B: case 0x26C: case 0x26D: case 0x26E: case 0x26F:
                        EAX = mtrr_fix4k_msr[ECX - 0x268] & 0xffffffff;
                        EDX = mtrr_fix4k_msr[ECX - 0x268] >> 32;
			break;
			case 0x277:
                        EAX = pat_msr & 0xffffffff;
                        EDX = pat_msr >> 32;
			break;
			case 0x2FF:
                        EAX = mtrr_deftype_msr & 0xffffffff;
                        EDX = mtrr_deftype_msr >> 32;
			break;
			case 0x404:
                        EAX = ecx404_msr & 0xffffffff;
                        EDX = ecx404_msr >> 32;
			break;
			case 0x408:
                        EAX = ecx408_msr & 0xffffffff;
                        EDX = ecx408_msr >> 32;
			break;
			case 0x40c:
                        EAX = ecx40c_msr & 0xffffffff;
                        EDX = ecx40c_msr >> 32;
			break;
			case 0x410:
                        EAX = ecx410_msr & 0xffffffff;
                        EDX = ecx410_msr >> 32;
			break;
			case 0x570:
                        EAX = ecx570_msr & 0xffffffff;
                        EDX = ecx570_msr >> 32;
			break;
			case 0x1002ff:
                        EAX = ecx1002ff_msr & 0xffffffff;
                        EDX = ecx1002ff_msr >> 32;
			break;
			case 0xf0f00250:
                        EAX = ecxf0f00250_msr & 0xffffffff;
                        EDX = ecxf0f00250_msr >> 32;
			break;
			case 0xf0f00258:
                        EAX = ecxf0f00258_msr & 0xffffffff;
                        EDX = ecxf0f00258_msr >> 32;
			break;
			default:
i686_invalid_rdmsr:
			cpu_log("RDMSR: Invalid MSR: %08X\n", ECX);
			x86gpf(NULL, 0);
			break;
                }
                break;
        }

	cpu_log("RDMSR %08X %08X%08X\n", ECX, EDX, EAX);
}

void cpu_WRMSR()
{
	uint64_t temp;

	cpu_log("WRMSR %08X %08X%08X\n", ECX, EDX, EAX);
        switch (machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type)
        {
                case CPU_WINCHIP:
                case CPU_WINCHIP2:
                switch (ECX)
                {
                        case 0x02:
                        msr.tr1 = EAX & 2;
                        break;
                        case 0x0e:
                        msr.tr12 = EAX & 0x228;
                        break;
                        case 0x10:
                        tsc = EAX | ((uint64_t)EDX << 32);
                        break;
                        case 0x11:
                        msr.cesr = EAX & 0xff00ff;
                        break;
                        case 0x107:
                        msr.fcr = EAX;
                        if (EAX & (1 << 9))
                                cpu_features |= CPU_FEATURE_MMX;
                        else
                                cpu_features &= ~CPU_FEATURE_MMX;
			if (EAX & (1 << 1))
				cpu_features |= CPU_FEATURE_CX8;
			else
				cpu_features &= ~CPU_FEATURE_CX8;
			if ((EAX & (1 << 20)) && machines[machine].cpu[cpu_manufacturer].cpus[cpu].cpu_type >= CPU_WINCHIP2)
                                cpu_features |= CPU_FEATURE_3DNOW;
			else
                                cpu_features &= ~CPU_FEATURE_3DNOW;
                         if (EAX & (1 << 29))
                                 CPUID = 0;
                         else
                                 CPUID = machines[machine].cpu[cpu_manufacturer].cpus[cpu].cpuid_model;
                        break;
                        case 0x108:
                        msr.fcr2 = EAX | ((uint64_t)EDX << 32);
                        break;
                        case 0x109:
                        msr.fcr3 = EAX | ((uint64_t)EDX << 32);
                        break;
                }
                break;	
		case CPU_CYRIX3S:
                switch (ECX)
                {
                        case 0x10:
                        tsc = EAX | ((uint64_t)EDX << 32);
                        break;
                        case 0x1107:
                        msr.fcr = EAX;
                        if (EAX & (1 << 1))
                                cpu_features |= CPU_FEATURE_CX8;
                        else
                                cpu_features &= ~CPU_FEATURE_CX8;
                        break;
                        case 0x1108:
                        msr.fcr2 = EAX | ((uint64_t)EDX << 32);
                        break;
                        case 0x1109:
                        msr.fcr3 = EAX | ((uint64_t)EDX << 32);
                        break;
                        case 0x200: case 0x201: case 0x202: case 0x203: case 0x204: case 0x205: case 0x206: case 0x207:
                        case 0x208: case 0x209: case 0x20A: case 0x20B: case 0x20C: case 0x20D: case 0x20E: case 0x20F:
                        if (ECX & 1)
                                mtrr_physmask_msr[(ECX - 0x200) >> 1] = EAX | ((uint64_t)EDX << 32);
                        else
                                mtrr_physbase_msr[(ECX - 0x200) >> 1] = EAX | ((uint64_t)EDX << 32);
                        break;
                        case 0x250:
                        mtrr_fix64k_8000_msr = EAX | ((uint64_t)EDX << 32);
                        break;
                        case 0x258:
                        mtrr_fix16k_8000_msr = EAX | ((uint64_t)EDX << 32);
                        break;
                        case 0x259:
                        mtrr_fix16k_a000_msr = EAX | ((uint64_t)EDX << 32);
                        break;
                        case 0x268: case 0x269: case 0x26A: case 0x26B: case 0x26C: case 0x26D: case 0x26E: case 0x26F:
                        mtrr_fix4k_msr[ECX - 0x268] = EAX | ((uint64_t)EDX << 32);
                        break;
                        case 0x2FF:
                        mtrr_deftype_msr = EAX | ((uint64_t)EDX << 32);
                        break;			
                }
                break;
		
#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
                case CPU_K5:
                case CPU_5K86:
#endif
                case CPU_K6:
                switch (ECX)
                {
                        case 0x0e:
                        msr.tr12 = EAX & 0x228;
                        break;
                        case 0x10:
                        tsc = EAX | ((uint64_t)EDX << 32);
                        break;
			case 0x83:
			ecx83_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000080:
			temp = EAX | ((uint64_t)EDX << 32);
			if (temp & ~1ULL)
				x86gpf(NULL, 0);
			else
				amd_efer = temp;
			break;
			case 0xC0000082:
			amd_whcr = EAX | ((uint64_t)EDX << 32);
			break;
			default:
			x86gpf(NULL, 0);
			break;
                }
                break;

                case CPU_K6_2:
                switch (ECX)
                {
                        case 0x0e:
                        msr.tr12 = EAX & 0x228;
                        break;
                        case 0x10:
                        tsc = EAX | ((uint64_t)EDX << 32);
                        break;
			case 0x83:
			ecx83_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000080:
			temp = EAX | ((uint64_t)EDX << 32);
			if (temp & ~1ULL)
				x86gpf(NULL, 0);
			else
				amd_efer = temp;
			break;
			case 0xC0000081:
			star = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000082:
			amd_whcr = EAX | ((uint64_t)EDX << 32);
			break;
			default:
			x86gpf(NULL, 0);
			break;
                }
                break;

                case CPU_K6_2C:
                switch (ECX)
                {
                        case 0x0e:
                        msr.tr12 = EAX & 0x228;
                        break;
                        case 0x10:
                        tsc = EAX | ((uint64_t)EDX << 32);
                        break;
			case 0x83:
			ecx83_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000080:
			temp = EAX | ((uint64_t)EDX << 32);
			if (temp & ~0xfULL)
				x86gpf(NULL, 0);
			else
				amd_efer = temp;
			break;
			case 0xC0000081:
			star = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000082:
			amd_whcr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000085:
			amd_uwccr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000087:
			amd_psor = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000088:
			amd_pfir = EAX | ((uint64_t)EDX << 32);
			break;
			default:
			x86gpf(NULL, 0);
			break;
                }
                break;

                case CPU_K6_3:
                switch (ECX)
                {
                        case 0x0e:
                        msr.tr12 = EAX & 0x228;
                        break;
                        case 0x10:
                        tsc = EAX | ((uint64_t)EDX << 32);
                        break;
			case 0x83:
			ecx83_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000080:
			temp = EAX | ((uint64_t)EDX << 32);
			if (temp & ~0x1fULL)
				x86gpf(NULL, 0);
			else
				amd_efer = temp;
			break;
			case 0xC0000081:
			star = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000082:
			amd_whcr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000085:
			amd_uwccr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000087:
			amd_psor = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000088:
			amd_pfir = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000089:
			amd_l2aar = EAX | ((uint64_t)EDX << 32);
			break;
			default:
			x86gpf(NULL, 0);
			break;
                }
                break;

                case CPU_K6_2P:
                case CPU_K6_3P:
                switch (ECX)
                {
                        case 0x0e:
                        msr.tr12 = EAX & 0x228;
                        break;
                        case 0x10:
                        tsc = EAX | ((uint64_t)EDX << 32);
                        break;
			case 0x83:
			ecx83_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000080:
			temp = EAX | ((uint64_t)EDX << 32);
			if (temp & ~0x1fULL)
				x86gpf(NULL, 0);
			else
				amd_efer = temp;
			break;
			case 0xC0000081:
			star = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000082:
			amd_whcr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000085:
			amd_uwccr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000086:
			amd_epmr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000087:
			amd_psor = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000088:
			amd_pfir = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000089:
			amd_l2aar = EAX | ((uint64_t)EDX << 32);
			break;
			default:
			x86gpf(NULL, 0);
			break;
                }
                break;
		
                case CPU_P24T:
                case CPU_PENTIUM:
                case CPU_PENTIUMMMX:
                switch (ECX)
                {
                        case 0x10:
				tsc = EAX | ((uint64_t)EDX << 32);
				break;
                        case 0x8B:
				cpu_log("WRMSR: Invalid MSR: 0x8B\n");
				x86gpf(NULL, 0); /*Needed for Vista to correctly break on Pentium*/
				break;
                }
                break;
#if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
                case CPU_Cx6x86:
                case CPU_Cx6x86L:
                case CPU_CxGX1:
                case CPU_Cx6x86MX:
                switch (ECX)
                {
                        case 0x10:
                        tsc = EAX | ((uint64_t)EDX << 32);
                        break;
                }
                break;
#endif

                case CPU_PENTIUMPRO:
		case CPU_PENTIUM2:
		case CPU_PENTIUM2D:
                switch (ECX)
                {
                        case 0x10:
                        tsc = EAX | ((uint64_t)EDX << 32);
                        break;
			case 0x17:
			if (machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type != CPU_PENTIUM2D)  goto i686_invalid_wrmsr;
			ecx17_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x1B:
			/* pclog("APIC_BASE write: %08X%08X\n", EDX, EAX); */
			// apic_base_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x2A:
			ecx2a_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x79:
			ecx79_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x88: case 0x89: case 0x8A: case 0x8B:
			ecx8x_msr[ECX - 0x88] = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: case 0xC6: case 0xC7: case 0xC8:
			msr_ia32_pmc[ECX - 0xC1] = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xFE:
			mtrr_cap_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x116:
			ecx116_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x118: case 0x119: case 0x11A: case 0x11B:
			ecx11x_msr[ECX - 0x118] = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x11E:
			ecx11e_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x174:
			if (machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type == CPU_PENTIUMPRO)  goto i686_invalid_wrmsr;
			cs_msr = EAX & 0xFFFF;
			break;
			case 0x175:
			if (machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type == CPU_PENTIUMPRO)  goto i686_invalid_wrmsr;
			esp_msr = EAX;
			break;
			case 0x176:
			if (machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type == CPU_PENTIUMPRO)  goto i686_invalid_wrmsr;
			eip_msr = EAX;
			break;
			case 0x179:
			break;
			case 0x186:
			ecx186_msr = EAX | ((uint64_t)EDX << 32);
			break;			
			case 0x187:
			ecx187_msr = EAX | ((uint64_t)EDX << 32);
			break;			
			case 0x1E0:
			ecx1e0_msr = EAX | ((uint64_t)EDX << 32);
			break;			
			case 0x200: case 0x201: case 0x202: case 0x203: case 0x204: case 0x205: case 0x206: case 0x207:
			case 0x208: case 0x209: case 0x20A: case 0x20B: case 0x20C: case 0x20D: case 0x20E: case 0x20F:
			if (ECX & 1)
				mtrr_physmask_msr[(ECX - 0x200) >> 1] = EAX | ((uint64_t)EDX << 32);
			else
				mtrr_physbase_msr[(ECX - 0x200) >> 1] = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x250:
			mtrr_fix64k_8000_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x258:
			mtrr_fix16k_8000_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x259:
			mtrr_fix16k_a000_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x268: case 0x269: case 0x26A: case 0x26B: case 0x26C: case 0x26D: case 0x26E: case 0x26F:
			mtrr_fix4k_msr[ECX - 0x268] = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x277:
			pat_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x2FF:
			mtrr_deftype_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x404:
			ecx404_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x408:
			ecx408_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x40c:
			ecx40c_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x410:
			ecx410_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x570:
			ecx570_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x1002ff:
			ecx1002ff_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xf0f00250:
			ecxf0f00250_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xf0f00258:
			ecxf0f00258_msr = EAX | ((uint64_t)EDX << 32);
			break;
			default:
i686_invalid_wrmsr:
			cpu_log("WRMSR: Invalid MSR: %08X\n", ECX);
			x86gpf(NULL, 0);
			break;
                }
                break;
        }
}

static int cyrix_addr;

static void cpu_write(uint16_t addr, uint8_t val, void *priv)
{
	if (addr == 0xf0) {
		/* Writes to F0 clear FPU error and deassert the interrupt. */
		if (is286)
			picintc(1 << 13);
		else
			nmi = 0;
		return;
	} else if (addr >= 0xf1)
		return;		/* FPU stuff */

        if (!(addr & 1))
                cyrix_addr = val;
        else switch (cyrix_addr)
        {
                case 0xc0: /*CCR0*/
                ccr0 = val;
                break;
                case 0xc1: /*CCR1*/
                ccr1 = val;
                break;
                case 0xc2: /*CCR2*/
                ccr2 = val;
                break;
                case 0xc3: /*CCR3*/
                ccr3 = val;
                break;
                case 0xe8: /*CCR4*/
                if ((ccr3 & 0xf0) == 0x10)
                {
                        ccr4 = val;
#if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
                        if (machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type >= CPU_Cx6x86)
                        {
                                if (val & 0x80)
                                        CPUID = machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].cpuid_model;
                                else
                                        CPUID = 0;
                        }
#endif
                }
                break;
                case 0xe9: /*CCR5*/
                if ((ccr3 & 0xf0) == 0x10)
                        ccr5 = val;
                break;
                case 0xea: /*CCR6*/
                if ((ccr3 & 0xf0) == 0x10)
                        ccr6 = val;
                break;
        }
}

static uint8_t cpu_read(uint16_t addr, void *priv)
{
	if (addr >= 0xf0)
		return 0xff;		/* FPU stuff */

        if (addr & 1)
        {
                switch (cyrix_addr)
                {
                        case 0xc0: return ccr0;
                        case 0xc1: return ccr1;
                        case 0xc2: return ccr2;
                        case 0xc3: return ccr3;
                        case 0xe8: return ((ccr3 & 0xf0) == 0x10) ? ccr4 : 0xff;
                        case 0xe9: return ((ccr3 & 0xf0) == 0x10) ? ccr5 : 0xff;
                        case 0xea: return ((ccr3 & 0xf0) == 0x10) ? ccr6 : 0xff;
                        case 0xfe: return machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].cyrix_id & 0xff;
                        case 0xff: return machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].cyrix_id >> 8;
                }
                if ((cyrix_addr & 0xf0) == 0xc0) return 0xff;
                if (cyrix_addr == 0x20 && machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type == CPU_Cx5x86) return 0xff;
        }
        return 0xff;
}


void
#ifdef USE_DYNAREC
x86_setopcodes(const OpFn *opcodes, const OpFn *opcodes_0f,
	       const OpFn *dynarec_opcodes, const OpFn *dynarec_opcodes_0f)
{
        x86_opcodes = opcodes;
        x86_opcodes_0f = opcodes_0f;
        x86_dynarec_opcodes = dynarec_opcodes;
        x86_dynarec_opcodes_0f = dynarec_opcodes_0f;
}
#else
x86_setopcodes(const OpFn *opcodes, const OpFn *opcodes_0f)
{
        x86_opcodes = opcodes;
        x86_opcodes_0f = opcodes_0f;
}
#endif


void
cpu_update_waitstates(void)
{
        cpu_s = &machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective];

        if (is486)
                cpu_prefetch_width = 16;
        else
                cpu_prefetch_width = cpu_16bitbus ? 2 : 4;

        if (cpu_cache_int_enabled)
        {
                /* Disable prefetch emulation */
                cpu_prefetch_cycles = 0;
        }
        else if (cpu_waitstates && (cpu_s->cpu_type >= CPU_286 && cpu_s->cpu_type <= CPU_386DX))
        {
                /* Waitstates override */
                cpu_prefetch_cycles = cpu_waitstates+1;
                cpu_cycles_read = cpu_waitstates+1;
                cpu_cycles_read_l = (cpu_16bitbus ? 2 : 1) * (cpu_waitstates+1);
                cpu_cycles_write = cpu_waitstates+1;
                cpu_cycles_write_l = (cpu_16bitbus ? 2 : 1) * (cpu_waitstates+1);
        }
        else if (cpu_cache_ext_enabled)
        {
                /* Use cache timings */
                cpu_prefetch_cycles = cpu_s->cache_read_cycles;
                cpu_cycles_read = cpu_s->cache_read_cycles;
                cpu_cycles_read_l = (cpu_16bitbus ? 2 : 1) * cpu_s->cache_read_cycles;
                cpu_cycles_write = cpu_s->cache_write_cycles;
                cpu_cycles_write_l = (cpu_16bitbus ? 2 : 1) * cpu_s->cache_write_cycles;
        }
        else
        {
                /* Use memory timings */
                cpu_prefetch_cycles = cpu_s->mem_read_cycles;
                cpu_cycles_read = cpu_s->mem_read_cycles;
                cpu_cycles_read_l = (cpu_16bitbus ? 2 : 1) * cpu_s->mem_read_cycles;
                cpu_cycles_write = cpu_s->mem_write_cycles;
                cpu_cycles_write_l = (cpu_16bitbus ? 2 : 1) * cpu_s->mem_write_cycles;
        }
        if (is486)
                cpu_prefetch_cycles = (cpu_prefetch_cycles * 11) / 16;
        cpu_mem_prefetch_cycles = cpu_prefetch_cycles;
        if (cpu_s->rspeed <= 8000000)
                cpu_rom_prefetch_cycles = cpu_mem_prefetch_cycles;
}
