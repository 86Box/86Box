/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		CPU type handler.
 *
 * Version:	@(#)cpu.c	1.0.11	2018/02/18
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		leilei,
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 leilei.
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "cpu.h"
#include "../device.h"
#include "../machine/machine.h"
#include "../io.h"
#include "x86_ops.h"
#include "../mem.h"
#include "../pci.h"
#ifdef USE_DYNAREC
#include "codegen.h"
#endif

int isa_cycles;
static uint8_t ccr0, ccr1, ccr2, ccr3, ccr4, ccr5, ccr6;

#ifdef USE_DYNAREC
OpFn *x86_dynarec_opcodes;
OpFn *x86_dynarec_opcodes_0f;
OpFn *x86_dynarec_opcodes_d8_a16;
OpFn *x86_dynarec_opcodes_d8_a32;
OpFn *x86_dynarec_opcodes_d9_a16;
OpFn *x86_dynarec_opcodes_d9_a32;
OpFn *x86_dynarec_opcodes_da_a16;
OpFn *x86_dynarec_opcodes_da_a32;
OpFn *x86_dynarec_opcodes_db_a16;
OpFn *x86_dynarec_opcodes_db_a32;
OpFn *x86_dynarec_opcodes_dc_a16;
OpFn *x86_dynarec_opcodes_dc_a32;
OpFn *x86_dynarec_opcodes_dd_a16;
OpFn *x86_dynarec_opcodes_dd_a32;
OpFn *x86_dynarec_opcodes_de_a16;
OpFn *x86_dynarec_opcodes_de_a32;
OpFn *x86_dynarec_opcodes_df_a16;
OpFn *x86_dynarec_opcodes_df_a32;
OpFn *x86_dynarec_opcodes_REPE;
OpFn *x86_dynarec_opcodes_REPNE;
#endif

OpFn *x86_opcodes;
OpFn *x86_opcodes_0f;
OpFn *x86_opcodes_d8_a16;
OpFn *x86_opcodes_d8_a32;
OpFn *x86_opcodes_d9_a16;
OpFn *x86_opcodes_d9_a32;
OpFn *x86_opcodes_da_a16;
OpFn *x86_opcodes_da_a32;
OpFn *x86_opcodes_db_a16;
OpFn *x86_opcodes_db_a32;
OpFn *x86_opcodes_dc_a16;
OpFn *x86_opcodes_dc_a32;
OpFn *x86_opcodes_dd_a16;
OpFn *x86_opcodes_dd_a32;
OpFn *x86_opcodes_de_a16;
OpFn *x86_opcodes_de_a32;
OpFn *x86_opcodes_df_a16;
OpFn *x86_opcodes_df_a32;
OpFn *x86_opcodes_REPE;
OpFn *x86_opcodes_REPNE;

enum
{
        CPUID_FPU = (1 << 0),
        CPUID_VME = (1 << 1),
        CPUID_PSE = (1 << 3),
        CPUID_TSC = (1 << 4),
        CPUID_MSR = (1 << 5),
        CPUID_CMPXCHG8B = (1 << 8),
	CPUID_AMDSEP = (1 << 10),
	CPUID_SEP = (1 << 11),
        CPUID_CMOV = (1 << 15),
        CPUID_MMX = (1 << 23),
	CPUID_FXSR = (1 << 24)
};

CPU *cpu_s;
int cpu_multi;
int cpu_iscyrix;
int cpu_16bitbus;
int cpu_busspeed;
int cpu_hasrdtsc;
int cpu_hasMMX, cpu_hasMSR;
int cpu_hasCR4;
int cpu_hasVME;
int cpu_cyrix_alignment;
int hasfpu;
int cpuspeed;

uint64_t cpu_CR4_mask;

int cpu_cycles_read, cpu_cycles_read_l, cpu_cycles_write, cpu_cycles_write_l;
int cpu_prefetch_cycles, cpu_prefetch_width;
int cpu_waitstates;
int cpu_cache_int_enabled, cpu_cache_ext_enabled;
int cpu_pci_speed;

int is286, is386, is486;
int israpidcad, is_pentium;

uint64_t tsc = 0;

cr0_t CR0;
uint64_t pmc[2] = {0, 0};

uint16_t temp_seg_data[4] = {0, 0, 0, 0};

#ifdef DEV_BRANCH
#ifdef USE_I686
uint16_t cs_msr = 0;
uint32_t esp_msr = 0;
uint32_t eip_msr = 0;
uint64_t apic_base_msr = 0;
uint64_t mtrr_cap_msr = 0;
uint64_t mtrr_physbase_msr[8] = {0, 0, 0, 0, 0, 0, 0, 0};
uint64_t mtrr_physmask_msr[8] = {0, 0, 0, 0, 0, 0, 0, 0};
uint64_t mtrr_fix64k_8000_msr = 0;
uint64_t mtrr_fix16k_8000_msr = 0;
uint64_t mtrr_fix16k_a000_msr = 0;
uint64_t mtrr_fix4k_msr[8] = {0, 0, 0, 0, 0, 0, 0, 0};
uint64_t pat_msr = 0;
uint64_t mtrr_deftype_msr = 0;
uint64_t msr_ia32_pmc[8] = {0, 0, 0, 0, 0, 0, 0, 0};
uint64_t ecx17_msr = 0;
uint64_t ecx79_msr = 0;
uint64_t ecx8x_msr[4] = {0, 0, 0, 0};
uint64_t ecx116_msr = 0;
uint64_t ecx11x_msr[4] = {0, 0, 0, 0};
uint64_t ecx11e_msr = 0;
uint64_t ecx186_msr = 0;
uint64_t ecx187_msr = 0;
uint64_t ecx1e0_msr = 0;
uint64_t ecx570_msr = 0;
#endif
#endif

#ifdef DEV_BRANCH
#ifdef USE_AMD_K
/* AMD K5 and K6 MSR's. */
uint64_t ecx83_msr = 0;
/* These are K6-only. */
uint64_t star = 0;
uint64_t sfmask = 0;
#endif
#endif

int timing_rr;
int timing_mr, timing_mrl;
int timing_rm, timing_rml;
int timing_mm, timing_mml;
int timing_bt, timing_bnt;
int timing_int, timing_int_rm, timing_int_v86, timing_int_pm, timing_int_pm_outer;
int timing_iret_rm, timing_iret_v86, timing_iret_pm, timing_iret_pm_outer;
int timing_call_rm, timing_call_pm, timing_call_pm_gate, timing_call_pm_gate_inner;
int timing_retf_rm, timing_retf_pm, timing_retf_pm_outer;
int timing_jmp_rm, timing_jmp_pm, timing_jmp_pm_gate;
int timing_misaligned;

msr_t msr;


void cpu_set_edx()
{
        EDX = machines[machine].cpu[cpu_manufacturer].cpus[cpu].edx_reset;
}


void cpu_set()
{
        CPU *cpu_s;
        
        if (!machines[machine].cpu[cpu_manufacturer].cpus)
        {
                /*CPU is invalid, set to default*/
                cpu_manufacturer = 0;
                cpu = 0;
        }
        
        cpu_s = &machines[machine].cpu[cpu_manufacturer].cpus[cpu];

        CPUID    = cpu_s->cpuid_model;
        cpuspeed = cpu_s->speed;
        is8086   = (cpu_s->cpu_type > CPU_8088);
        is286   = (cpu_s->cpu_type >= CPU_286);
        is386    = (cpu_s->cpu_type >= CPU_386SX);
	israpidcad = (cpu_s->cpu_type == CPU_RAPIDCAD);
        is486    = (cpu_s->cpu_type >= CPU_i486SX) || (cpu_s->cpu_type == CPU_486SLC || cpu_s->cpu_type == CPU_486DLC || cpu_s->cpu_type == CPU_RAPIDCAD);
        is_pentium= (cpu_s->cpu_type >= CPU_WINCHIP);
        hasfpu   = (cpu_s->cpu_type >= CPU_i486DX) || (cpu_s->cpu_type == CPU_RAPIDCAD);
        cpu_iscyrix = (cpu_s->cpu_type == CPU_486SLC || cpu_s->cpu_type == CPU_486DLC || cpu_s->cpu_type == CPU_Cx486S || cpu_s->cpu_type == CPU_Cx486DX || cpu_s->cpu_type == CPU_Cx5x86 || cpu_s->cpu_type == CPU_Cx6x86 || cpu_s->cpu_type == CPU_Cx6x86MX || cpu_s->cpu_type == CPU_Cx6x86L || cpu_s->cpu_type == CPU_CxGX1);
        cpu_16bitbus = (cpu_s->cpu_type == CPU_286 || cpu_s->cpu_type == CPU_386SX || cpu_s->cpu_type == CPU_486SLC);
        if (cpu_s->multi) 
           cpu_busspeed = cpu_s->rspeed / cpu_s->multi;
        cpu_multi = cpu_s->multi;
        cpu_hasrdtsc = 0;
        cpu_hasMMX = 0;
        cpu_hasMSR = 0;
        cpu_hasCR4 = 0;
        ccr0 = ccr1 = ccr2 = ccr3 = ccr4 = ccr5 = ccr6 = 0;

	if ((cpu_s->cpu_type == CPU_286) || (cpu_s->cpu_type == CPU_386SX) || (cpu_s->cpu_type == CPU_386DX) || (cpu_s->cpu_type == CPU_i486SX))
	{
		if (enable_external_fpu)
		{
			hasfpu = 1;
		}
	}

        cpu_update_waitstates();

	isa_cycles = cpu_s->atclk_div;

        if (cpu_s->pci_speed)
        {
                pci_nonburst_time = 4*cpu_s->rspeed / cpu_s->pci_speed;
                pci_burst_time = cpu_s->rspeed / cpu_s->pci_speed;
        }
        else
        {
                pci_nonburst_time = 4;
                pci_burst_time = 1;
        }
        pclog("PCI burst=%i nonburst=%i\n", pci_burst_time, pci_nonburst_time);

        if (cpu_iscyrix)
           io_sethandler(0x0022, 0x0002, cyrix_read, NULL, NULL, cyrix_write, NULL, NULL, NULL);
        else
           io_removehandler(0x0022, 0x0002, cyrix_read, NULL, NULL, cyrix_write, NULL, NULL, NULL);
        
        pclog("hasfpu - %i\n",hasfpu);
        pclog("is486 - %i  %i\n",is486,cpu_s->cpu_type);

#ifdef USE_DYNAREC
        x86_setopcodes(ops_386, ops_386_0f, dynarec_ops_386, dynarec_ops_386_0f);
#else
        x86_setopcodes(ops_386, ops_386_0f);
#endif
        x86_opcodes_REPE = ops_REPE;
        x86_opcodes_REPNE = ops_REPNE;
#ifdef USE_DYNAREC
        x86_dynarec_opcodes_REPE = dynarec_ops_REPE;
        x86_dynarec_opcodes_REPNE = dynarec_ops_REPNE;
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
		if (enable_external_fpu)
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

                case CPU_386DX:
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
		cpu_hasCR4 = 1;
		cpu_hasVME = 1;
		cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_VME;
                case CPU_i486SX:
                case CPU_i486DX:
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
                case CPU_Am486DX:
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
                cpu_hasrdtsc = 1;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_hasMMX = cpu_hasMSR = 1;
                cpu_hasCR4 = 1;
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
                cpu_hasrdtsc = 1;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_hasMMX = 0;
                cpu_hasMSR = 1;
                cpu_hasCR4 = 1;
		cpu_hasVME = 1;
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
                cpu_hasrdtsc = 1;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_hasMMX = 1;
                cpu_hasMSR = 1;
                cpu_hasCR4 = 1;
		cpu_hasVME = 1;
                cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | CR4_PSE | CR4_MCE | CR4_PCE;
#ifdef USE_DYNAREC
                codegen_timing_set(&codegen_timing_pentium);
#endif
                break;

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
                cpu_hasrdtsc = 1;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_hasMMX = 0;
                cpu_hasMSR = 0;
                cpu_hasCR4 = 0;
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
                cpu_hasrdtsc = 1;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_hasMMX = 0;
                cpu_hasMSR = 0;
                cpu_hasCR4 = 0;
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
                cpu_hasrdtsc = 1;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_hasMMX = 0;
                cpu_hasMSR = 1;
                cpu_hasCR4 = 1;
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
                cpu_hasrdtsc = 1;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_hasMMX = 1;
                cpu_hasMSR = 1;
                cpu_hasCR4 = 1;
                cpu_CR4_mask = CR4_TSD | CR4_DE | CR4_PCE;
#ifdef USE_DYNAREC
         	codegen_timing_set(&codegen_timing_686);
#endif
         	ccr4 = 0x80;
                break;

#ifdef DEV_BRANCH
#ifdef USE_AMD_K
                case CPU_K5:
                case CPU_5K86:
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
                timing_misaligned = 3;
                cpu_hasrdtsc = 1;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_hasMMX = 1;
                cpu_hasMSR = 1;
                cpu_hasCR4 = 1;
                cpu_CR4_mask = CR4_TSD | CR4_DE | CR4_MCE | CR4_PCE;
                break;

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
                timing_misaligned = 3;
                cpu_hasrdtsc = 1;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_hasMMX = 1;
                cpu_hasMSR = 1;
                cpu_hasCR4 = 1;
		cpu_hasVME = 1;
                cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | CR4_PSE | CR4_MCE | CR4_PCE;
#ifdef USE_DYNAREC
                codegen_timing_set(&codegen_timing_pentium);
#endif
                break;
#endif
#endif

#ifdef DEV_BRANCH
#ifdef USE_I686
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
                timing_rm  = 1; /*register dest - memory src*/
                timing_mr  = 1; /*memory dest   - register src*/
                timing_mm  = 1;
                timing_rml = 1; /*register dest - memory src long*/
                timing_mrl = 1; /*memory dest   - register src long*/
                timing_mml = 1;
                timing_bt  = 0; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                timing_misaligned = 3;
                cpu_hasrdtsc = 1;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_hasMMX = 0;
                cpu_hasMSR = 1;
                cpu_hasCR4 = 1;
		cpu_hasVME = 1;
                cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | CR4_PSE | CR4_MCE | CR4_PCE;
#ifdef USE_DYNAREC
         	codegen_timing_set(&codegen_timing_686);
#endif
                break;

#if 0                
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
                timing_rm  = 1; /*register dest - memory src*/
                timing_mr  = 1; /*memory dest   - register src*/
                timing_mm  = 1;
                timing_rml = 1; /*register dest - memory src long*/
                timing_mrl = 1; /*memory dest   - register src long*/
                timing_mml = 1;
                timing_bt  = 0; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                timing_misaligned = 3;
                cpu_hasrdtsc = 1;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_hasMMX = 1;
                cpu_hasMSR = 1;
                cpu_hasCR4 = 1;
		cpu_hasVME = 1;
                cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | CR4_PSE | CR4_MCE | CR4_PCE;
#ifdef USE_DYNAREC
         	codegen_timing_set(&codegen_timing_686);
#endif
                break;
#endif

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
                timing_rm  = 1; /*register dest - memory src*/
                timing_mr  = 1; /*memory dest   - register src*/
                timing_mm  = 1;
                timing_rml = 1; /*register dest - memory src long*/
                timing_mrl = 1; /*memory dest   - register src long*/
                timing_mml = 1;
                timing_bt  = 0; /*branch taken*/
                timing_bnt = 1; /*branch not taken*/
                timing_misaligned = 3;
                cpu_hasrdtsc = 1;
                msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) |  (1 << 16) | (1 << 19) | (1 << 21);
                cpu_hasMMX = 1;
                cpu_hasMSR = 1;
                cpu_hasCR4 = 1;
		cpu_hasVME = 1;
                cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | CR4_PSE | CR4_MCE | CR4_PCE | CR4_OSFXSR;
#ifdef USE_DYNAREC
         	codegen_timing_set(&codegen_timing_686);
#endif
                break;
#endif
#endif

                default:
                fatal("cpu_set : unknown CPU type %i\n", cpu_s->cpu_type);
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


void cpu_CPUID()
{
        switch (machines[machine].cpu[cpu_manufacturer].cpus[cpu].cpu_type)
        {
                case CPU_i486DX:
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
                   EAX = 0;
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
                   EAX = 0;
                break;

                case CPU_Am486SX:
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
                   EAX = 0;
                break;

                case CPU_Am486DX:
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
                   EAX = 0;
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
                        if (msr.fcr & (1 << 1))
                                EDX |= CPUID_CMPXCHG8B;
                        if (msr.fcr & (1 << 9))
                                EDX |= CPUID_MMX;
                }
                else
                   EAX = 0;
                break;

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
                        EAX = 0;
                break;

#ifdef DEV_BRANCH
#ifdef USE_AMD_K
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
                        EAX = 0;
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
                        EAX = 0;
                break;

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
                        EDX = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B;
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
                        EDX = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B | CPUID_AMDSEP;
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
                        EAX = 0;
                break;
#endif
#endif

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
                        EAX = 0;
                break;


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
                        EAX = 0;
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
                        EAX = 0;
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
                        EAX = 0;
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
                        EAX = 0;
                break;

#ifdef DEV_BRANCH
#ifdef USE_I686
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
                        EDX = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B | CPUID_SEP | CPUID_CMOV;
                }
		else if (EAX == 2)
		{
		}
                else
                        EAX = 0;
                break;

                /* case CPU_PENTIUM2:
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
                        EDX = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B | CPUID_MMX | CPUID_SEP | CPUID_CMOV;
                }
		else if (EAX == 2)
		{
			EAX = 0x03020101;
			EBX = ECX = 0;
			EDX = 0x0C040843;
		}
                else
                        EAX = 0;
                break; */

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
                        EDX = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B | CPUID_MMX | CPUID_SEP | CPUID_FXSR | CPUID_CMOV;
                }
		else if (EAX == 2)
		{
			EAX = 0x03020101;
			EBX = ECX = 0;
			EDX = 0x0C040844;
		}
                else
                        EAX = 0;
                break;
#endif
#endif

        }
}

void cpu_RDMSR()
{
        switch (machines[machine].cpu[cpu_manufacturer].cpus[cpu].cpu_type)
        {
                case CPU_WINCHIP:
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

#ifdef DEV_BRANCH
#ifdef USE_AMD_K
                case CPU_K5:
                case CPU_5K86:
                case CPU_K6:
                EAX = EDX = 0;
                switch (ECX)
                {
                        case 0x0e:
                        EAX = msr.tr12;
                        break;
                        case 0x10:
                        EAX = tsc & 0xffffffff;
                        EDX = tsc >> 32;
                        break;
                        case 0x83:
                        EAX = ecx83_msr & 0xffffffff;
                        EDX = ecx83_msr >> 32;
                        break;
                        case 0xC0000081:
                        EAX = star & 0xffffffff;
                        EDX = star >> 32;
                        break;
                        case 0xC0000084:
                        EAX = sfmask & 0xffffffff;
                        EDX = sfmask >> 32;
                        break;
			default:
#ifndef RELEASE_BUILD
			pclog("Invalid MSR: %08X\n", ECX);
#endif
			x86gpf(NULL, 0);
			break;
                }
                break;
#endif
#endif

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

#ifdef DEV_BRANCH
#ifdef USE_I686
                case CPU_PENTIUMPRO:
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
			break;
			case 0x2A:
			EAX = 0xC5800000;
			EDX = 0;
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
			break;
			case 0x175:
			if (machines[machine].cpu[cpu_manufacturer].cpus[cpu].cpu_type == CPU_PENTIUMPRO)  goto i686_invalid_rdmsr;
			EAX = esp_msr;
			break;
			case 0x176:
			if (machines[machine].cpu[cpu_manufacturer].cpus[cpu].cpu_type == CPU_PENTIUMPRO)  goto i686_invalid_rdmsr;
			EAX = eip_msr;
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
			case 0x570:
                        EAX = ecx570_msr & 0xffffffff;
                        EDX = ecx570_msr >> 32;
			break;
			default:
i686_invalid_rdmsr:
#ifndef RELEASE_BUILD
			pclog("Invalid MSR: %08X\n", ECX);
#endif
			x86gpf(NULL, 0);
			break;
                }
                break;
#endif
#endif
        }
}

void cpu_WRMSR()
{
        switch (machines[machine].cpu[cpu_manufacturer].cpus[cpu].cpu_type)
        {
                case CPU_WINCHIP:
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
                        cpu_hasMMX = EAX & (1 << 9);
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
#ifdef DEV_BRANCH
#ifdef USE_AMD_K
                case CPU_K5:
                case CPU_5K86:
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
			case 0xC0000081:
			star = EAX | ((uint64_t)EDX << 32);
			break;
			case 0xC0000084:
			sfmask = EAX | ((uint64_t)EDX << 32);
			break;
                }
                break;
#endif
#endif

                case CPU_PENTIUM:
                case CPU_PENTIUMMMX:
                switch (ECX)
                {
                        case 0x10:
				tsc = EAX | ((uint64_t)EDX << 32);
				break;
                }
                break;
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

#ifdef DEV_BRANCH
#ifdef USE_I686
                case CPU_PENTIUMPRO:
		case CPU_PENTIUM2D:
                switch (ECX)
                {
                        case 0x10:
                        tsc = EAX | ((uint64_t)EDX << 32);
                        break;
			case 0x17:
			if (machines[machine].cpu[cpu_manufacturer].cpus[cpu].cpu_type != CPU_PENTIUM2D)  goto i686_invalid_wrmsr;
			ecx17_msr = EAX | ((uint64_t)EDX << 32);
			break;
			case 0x1B:
			apic_base_msr = EAX | ((uint64_t)EDX << 32);
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
			if (machines[machine].cpu[cpu_manufacturer].cpus[cpu].cpu_type == CPU_PENTIUMPRO)  goto i686_invalid_wrmsr;
			cs_msr = EAX & 0xFFFF;
			break;
			case 0x175:
			if (machines[machine].cpu[cpu_manufacturer].cpus[cpu].cpu_type == CPU_PENTIUMPRO)  goto i686_invalid_wrmsr;
			esp_msr = EAX;
			break;
			case 0x176:
			if (machines[machine].cpu[cpu_manufacturer].cpus[cpu].cpu_type == CPU_PENTIUMPRO)  goto i686_invalid_wrmsr;
			eip_msr = EAX;
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
			case 0x570:
			ecx570_msr = EAX | ((uint64_t)EDX << 32);
			break;			
			default:
i686_invalid_wrmsr:
#ifndef RELEASE_BUILD
			pclog("Invalid MSR: %08X\n", ECX);
#endif
			x86gpf(NULL, 0);
			break;
                }
                break;
#endif
#endif
        }
}

static int cyrix_addr;

void cyrix_write(uint16_t addr, uint8_t val, void *priv)
{
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
                        if (machines[machine].cpu[cpu_manufacturer].cpus[cpu].cpu_type >= CPU_Cx6x86)
                        {
                                if (val & 0x80)
                                        CPUID = machines[machine].cpu[cpu_manufacturer].cpus[cpu].cpuid_model;
                                else
                                        CPUID = 0;
                        }
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

uint8_t cyrix_read(uint16_t addr, void *priv)
{
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
                        case 0xfe: return machines[machine].cpu[cpu_manufacturer].cpus[cpu].cyrix_id & 0xff;
                        case 0xff: return machines[machine].cpu[cpu_manufacturer].cpus[cpu].cyrix_id >> 8;
                }
                if ((cyrix_addr & 0xf0) == 0xc0) return 0xff;
                if (cyrix_addr == 0x20 && machines[machine].cpu[cpu_manufacturer].cpus[cpu].cpu_type == CPU_Cx5x86) return 0xff;
        }
        return 0xff;
}

#ifdef USE_DYNAREC
void x86_setopcodes(OpFn *opcodes, OpFn *opcodes_0f, OpFn *dynarec_opcodes, OpFn *dynarec_opcodes_0f)
{
        x86_opcodes = opcodes;
        x86_opcodes_0f = opcodes_0f;
        x86_dynarec_opcodes = dynarec_opcodes;
        x86_dynarec_opcodes_0f = dynarec_opcodes_0f;
}
#else
void x86_setopcodes(OpFn *opcodes, OpFn *opcodes_0f)
{
        x86_opcodes = opcodes;
        x86_opcodes_0f = opcodes_0f;
}
#endif

void cpu_update_waitstates()
{
        cpu_s = &machines[machine].cpu[cpu_manufacturer].cpus[cpu];
        
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
}
