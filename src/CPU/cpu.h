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
 * Version:	@(#)cpu.h	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		leilei,
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 leilei.
 *		Copyright 2016-2017 Miran Grca.
 */

#ifndef _CPU_H_
#define _CPU_H_

extern int cpu, cpu_manufacturer;

/*808x class CPUs*/
#define CPU_8088 0
#define CPU_8086 1

/*286 class CPUs*/
#define CPU_286 2

/*386 class CPUs*/
#define CPU_386SX  3
#define CPU_386DX  4
#define CPU_RAPIDCAD	5
#define CPU_486SLC 6
#define CPU_486DLC 7

/*486 class CPUs*/
#define CPU_i486SX  8
#define CPU_Am486SX 9
#define CPU_Cx486S  10
#define CPU_i486DX  11
#define CPU_Am486DX 12
#define CPU_Cx486DX 13
#define CPU_Cx5x86  14

/*586 class CPUs*/
#define CPU_WINCHIP 15
#define CPU_PENTIUM 16
#define CPU_PENTIUMMMX 17
#define CPU_Cx6x86 	18
#define CPU_Cx6x86MX 	19
#define CPU_Cx6x86L 	20
#define CPU_CxGX1 	21
#define CPU_K5 22
#define CPU_5K86 23
#define CPU_K6 24

/*686 class CPUs*/
#define CPU_PENTIUMPRO 25
/*
#define CPU_PENTIUM2 26
#define CPU_PENTIUM2D 27 */
#define CPU_PENTIUM2D 26

#define MANU_INTEL 0
#define MANU_AMD   1
#define MANU_CYRIX 2
#define MANU_IDT   3

extern int timing_rr;
extern int timing_mr, timing_mrl;
extern int timing_rm, timing_rml;
extern int timing_mm, timing_mml;
extern int timing_bt, timing_bnt;

extern int timing_int, timing_int_rm, timing_int_v86, timing_int_pm, timing_int_pm_outer;
extern int timing_iret_rm, timing_iret_v86, timing_iret_pm, timing_iret_pm_outer;
extern int timing_call_rm, timing_call_pm, timing_call_pm_gate, timing_call_pm_gate_inner;
extern int timing_retf_rm, timing_retf_pm, timing_retf_pm_outer;
extern int timing_jmp_rm, timing_jmp_pm, timing_jmp_pm_gate;


typedef struct
{
        char name[32];
        int cpu_type;
        int speed;
        int rspeed;
        int multi;
        int pci_speed;
        uint32_t edx_reset;
        uint32_t cpuid_model;
        uint16_t cyrix_id;
        int cpu_flags;
        int mem_read_cycles, mem_write_cycles;
        int cache_read_cycles, cache_write_cycles;
} CPU;

extern CPU cpus_8088[];
extern CPU cpus_8086[];
extern CPU cpus_286[];
extern CPU cpus_i386SX[];
extern CPU cpus_i386DX[];
extern CPU cpus_Am386SX[];
extern CPU cpus_Am386DX[];
extern CPU cpus_486SLC[];
extern CPU cpus_486DLC[];
extern CPU cpus_i486[];
extern CPU cpus_Am486[];
extern CPU cpus_Cx486[];
extern CPU cpus_WinChip[];
extern CPU cpus_Pentium5V[];
extern CPU cpus_Pentium5V50[];
extern CPU cpus_PentiumS5[];
extern CPU cpus_K5[];
extern CPU cpus_K56[];
extern CPU cpus_Pentium[];
extern CPU cpus_6x86[];
extern CPU cpus_PentiumPro[];
extern CPU cpus_Pentium2[];
extern CPU cpus_Pentium2D[];

extern CPU cpus_pcjr[];
extern CPU cpus_pc1512[];
extern CPU cpus_ibmat[];
extern CPU cpus_ps1_m2011[];
extern CPU cpus_ps2_m30_286[];
extern CPU cpus_acer[];

extern int cpu_iscyrix;
extern int cpu_16bitbus;
extern int cpu_busspeed;
extern int cpu_multi;

extern int cpu_hasrdtsc;
extern int cpu_hasMSR;
extern int cpu_hasMMX;
extern int cpu_hasCR4;

#define CR4_TSD  (1 << 2)
#define CR4_DE   (1 << 3)
#define CR4_MCE  (1 << 6)
#define CR4_PCE  (1 << 8)
#define CR4_OSFXSR  (1 << 9)

extern uint64_t cpu_CR4_mask;

#define CPU_SUPPORTS_DYNAREC 1
#define CPU_REQUIRES_DYNAREC 2

extern int cpu_cycles_read, cpu_cycles_read_l, cpu_cycles_write, cpu_cycles_write_l;
extern int cpu_prefetch_cycles, cpu_prefetch_width;
extern int cpu_waitstates;
extern int cpu_cache_int_enabled, cpu_cache_ext_enabled;
extern int cpu_pci_speed;

extern uint64_t tsc;

void cyrix_write(uint16_t addr, uint8_t val, void *priv);
uint8_t cyrix_read(uint16_t addr, void *priv);

extern int is8086;

void cpu_CPUID();

void cpu_RDMSR();
void cpu_WRMSR();

extern int cpu_use_dynarec;

extern int xt_cpu_multi;

#define ISA_CYCLES_SHIFT 6
extern int isa_cycles;
#define ISA_CYCLES(x) ((x * isa_cycles) >> ISA_CYCLES_SHIFT)

void cpu_update_waitstates();
void cpu_set();

#endif
