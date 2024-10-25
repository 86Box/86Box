/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU type handler.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          leilei,
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2018 leilei.
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2018-2021 Fred N. van Kempen.
 */
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include "x87_sf.h"
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/io.h>
#include "x86_ops.h"
#include "x86seg_common.h"
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/pci.h>
#include <86box/timer.h>
#include <86box/gdbstub.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>

#ifdef USE_DYNAREC
#    include "codegen.h"
#endif /* USE_DYNAREC */
#include "x87_timings.h"

#define CCR1_USE_SMI  (1 << 1)
#define CCR1_SMAC     (1 << 2)
#define CCR1_SM3      (1 << 7)

#define CCR3_SMI_LOCK (1 << 0)
#define CCR3_NMI_EN   (1 << 1)

enum {
    CPUID_FPU       = (1 << 0),  /* On-chip Floating Point Unit */
    CPUID_VME       = (1 << 1),  /* Virtual 8086 mode extensions */
    CPUID_DE        = (1 << 2),  /* Debugging extensions */
    CPUID_PSE       = (1 << 3),  /* Page Size Extension */
    CPUID_TSC       = (1 << 4),  /* Time Stamp Counter */
    CPUID_MSR       = (1 << 5),  /* Model-specific registers */
    CPUID_PAE       = (1 << 6),  /* Physical Address Extension */
    CPUID_MCE       = (1 << 7),  /* Machine Check Exception */
    CPUID_CMPXCHG8B = (1 << 8),  /* CMPXCHG8B instruction */
    CPUID_APIC      = (1 << 9),  /* On-chip APIC */
    CPUID_AMDPGE    = (1 << 9),  /* Global Page Enable (AMD K5 Model 0 only) */
    CPUID_AMDSEP    = (1 << 10), /* SYSCALL and SYSRET instructions (AMD K6 only) */
    CPUID_SEP       = (1 << 11), /* SYSENTER and SYSEXIT instructions (SYSCALL and SYSRET if EAX=80000001h) */
    CPUID_MTRR      = (1 << 12), /* Memory type range registers */
    CPUID_PGE       = (1 << 13), /* Page Global Enable */
    CPUID_MCA       = (1 << 14), /* Machine Check Architecture */
    CPUID_CMOV      = (1 << 15), /* Conditional move instructions */
    CPUID_PAT       = (1 << 16), /* Page Attribute Table */
    CPUID_MMX       = (1 << 23), /* MMX technology */
    CPUID_FXSR      = (1 << 24)  /* FXSAVE and FXRSTOR instructions */
};

/* Additional flags returned by CPUID function 0x80000001 */
#define CPUID_3DNOWE (1UL << 30UL) /* Extended 3DNow! instructions */
#define CPUID_3DNOW  (1UL << 31UL) /* 3DNow! instructions */

/* Remove the Debugging Extensions CPUID flag if not compiled
   with debug register support for 486 and later CPUs. */
#ifndef USE_DEBUG_REGS_486
#    define CPUID_DE 0
#endif

/* Make sure this is as low as possible. */
cpu_state_t cpu_state;
fpu_state_t fpu_state;

/* Place this immediately after. */
uint32_t abrt_error;

#ifdef USE_DYNAREC
const OpFn *x86_dynarec_opcodes;
const OpFn *x86_dynarec_opcodes_0f;
const OpFn *x86_dynarec_opcodes_d8_a16;
const OpFn *x86_dynarec_opcodes_d8_a32;
const OpFn *x86_dynarec_opcodes_d9_a16;
const OpFn *x86_dynarec_opcodes_d9_a32;
const OpFn *x86_dynarec_opcodes_da_a16;
const OpFn *x86_dynarec_opcodes_da_a32;
const OpFn *x86_dynarec_opcodes_db_a16;
const OpFn *x86_dynarec_opcodes_db_a32;
const OpFn *x86_dynarec_opcodes_dc_a16;
const OpFn *x86_dynarec_opcodes_dc_a32;
const OpFn *x86_dynarec_opcodes_dd_a16;
const OpFn *x86_dynarec_opcodes_dd_a32;
const OpFn *x86_dynarec_opcodes_de_a16;
const OpFn *x86_dynarec_opcodes_de_a32;
const OpFn *x86_dynarec_opcodes_df_a16;
const OpFn *x86_dynarec_opcodes_df_a32;
const OpFn *x86_dynarec_opcodes_REPE;
const OpFn *x86_dynarec_opcodes_REPNE;
const OpFn *x86_dynarec_opcodes_3DNOW;
#endif /* USE_DYNAREC */

const OpFn *x86_opcodes;
const OpFn *x86_opcodes_0f;
const OpFn *x86_opcodes_d8_a16;
const OpFn *x86_opcodes_d8_a32;
const OpFn *x86_opcodes_d9_a16;
const OpFn *x86_opcodes_d9_a32;
const OpFn *x86_opcodes_da_a16;
const OpFn *x86_opcodes_da_a32;
const OpFn *x86_opcodes_db_a16;
const OpFn *x86_opcodes_db_a32;
const OpFn *x86_opcodes_dc_a16;
const OpFn *x86_opcodes_dc_a32;
const OpFn *x86_opcodes_dd_a16;
const OpFn *x86_opcodes_dd_a32;
const OpFn *x86_opcodes_de_a16;
const OpFn *x86_opcodes_de_a32;
const OpFn *x86_opcodes_df_a16;
const OpFn *x86_opcodes_df_a32;
const OpFn *x86_opcodes_REPE;
const OpFn *x86_opcodes_REPNE;
const OpFn *x86_opcodes_3DNOW;

const OpFn *x86_2386_opcodes;
const OpFn *x86_2386_opcodes_0f;
const OpFn *x86_2386_opcodes_d8_a16;
const OpFn *x86_2386_opcodes_d8_a32;
const OpFn *x86_2386_opcodes_d9_a16;
const OpFn *x86_2386_opcodes_d9_a32;
const OpFn *x86_2386_opcodes_da_a16;
const OpFn *x86_2386_opcodes_da_a32;
const OpFn *x86_2386_opcodes_db_a16;
const OpFn *x86_2386_opcodes_db_a32;
const OpFn *x86_2386_opcodes_dc_a16;
const OpFn *x86_2386_opcodes_dc_a32;
const OpFn *x86_2386_opcodes_dd_a16;
const OpFn *x86_2386_opcodes_dd_a32;
const OpFn *x86_2386_opcodes_de_a16;
const OpFn *x86_2386_opcodes_de_a32;
const OpFn *x86_2386_opcodes_df_a16;
const OpFn *x86_2386_opcodes_df_a32;
const OpFn *x86_2386_opcodes_REPE;
const OpFn *x86_2386_opcodes_REPNE;

uint16_t cpu_fast_off_count;
uint16_t cpu_fast_off_val;
uint16_t temp_seg_data[4] = { 0, 0, 0, 0 };

int isa_cycles;
int cpu_inited;

int cpu_cycles_read;
int cpu_cycles_read_l;
int cpu_cycles_write;
int cpu_cycles_write_l;
int cpu_prefetch_cycles;
int cpu_prefetch_width;
int cpu_mem_prefetch_cycles;
int cpu_rom_prefetch_cycles;
int cpu_waitstates;
int cpu_cache_int_enabled;
int cpu_cache_ext_enabled;
int cpu_flush_pending;
int cpu_old_paging;
int cpu_isa_speed;
int cpu_pci_speed;
int cpu_isa_pci_div;
int cpu_agp_speed;
int cpu_alt_reset;

int cpu_override;
int cpu_effective;
int cpu_multi;
int cpu_16bitbus;
int cpu_64bitbus;
int cpu_cyrix_alignment;
int cpu_cpurst_on_sr;
int cpu_use_exec = 0;
int cpu_override_interpreter;
int CPUID;

int is186;
int is_nec;
int is286;
int is386;
int is6117;
int is486 = 1;
int cpu_isintel;
int cpu_iscyrix;
int hascache;
int isibm486;
int israpidcad;
int is_vpc;
int is_am486;
int is_am486dxl;
int is_pentium;
int is_k5;
int is_k6;
int is_p6;
int is_cxsmm;
int hasfpu;

int timing_rr;
int timing_mr;
int timing_mrl;
int timing_rm;
int timing_rml;
int timing_mm;
int timing_mml;
int timing_bt;
int timing_bnt;
int timing_int;
int timing_int_rm;
int timing_int_v86;
int timing_int_pm;
int timing_int_pm_outer;
int timing_iret_rm;
int timing_iret_v86;
int timing_iret_pm;
int timing_iret_pm_outer;
int timing_call_rm;
int timing_call_pm;
int timing_call_pm_gate;
int timing_call_pm_gate_inner;
int timing_retf_rm;
int timing_retf_pm;
int timing_retf_pm_outer;
int timing_jmp_rm;
int timing_jmp_pm;
int timing_jmp_pm_gate;
int timing_misaligned;

uint32_t cpu_features;
uint32_t cpu_fast_off_flags;

uint32_t _tr[8]      = { 0, 0, 0, 0, 0, 0, 0, 0 };
uint32_t cache_index = 0;
uint8_t  _cache[2048];

uint64_t cpu_CR4_mask;
uint64_t tsc = 0;

double cpu_dmulti;
double cpu_busspeed;

msr_t msr;

cyrix_t cyrix;

cpu_family_t *cpu_f;
CPU          *cpu_s;

uint8_t do_translate  = 0;
uint8_t do_translate2 = 0;

void (*cpu_exec)(int32_t cycs);

static uint8_t ccr0;
static uint8_t ccr1;
static uint8_t ccr2;
static uint8_t ccr3;
static uint8_t ccr4;
static uint8_t ccr5;
static uint8_t ccr6;

static int cyrix_addr;

static void    cpu_write(uint16_t addr, uint8_t val, void *priv);
static uint8_t cpu_read(uint16_t addr, void *priv);

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
#    define cpu_log(fmt, ...)
#endif

int
cpu_has_feature(int feature)
{
    return cpu_features & feature;
}

void
cpu_dynamic_switch(int new_cpu)
{
    int c;

    if (cpu_effective == new_cpu)
        return;

    c   = cpu;
    cpu = new_cpu;
    cpu_set();
    pc_speed_changed();
    cpu = c;
}

void
cpu_set_edx(void)
{
    EDX = cpu_s->edx_reset;
    if (fpu_softfloat)
        SF_FPU_reset();
}

cpu_family_t *
cpu_get_family(const char *internal_name)
{
    int c = 0;

    while (cpu_families[c].package) {
        if (!strcmp(internal_name, cpu_families[c].internal_name))
            return (cpu_family_t *) &cpu_families[c];
        c++;
    }

    return NULL;
}

uint8_t
cpu_is_eligible(const cpu_family_t *cpu_family, int cpu, int machine)
{
    const machine_t *machine_s = &machines[machine];
    const CPU       *cpu_s     = &cpu_family->cpus[cpu];
    uint32_t         packages;
    uint32_t         bus_speed;
    uint8_t          i;
    double           multi;

    /* Full override. */
    if (cpu_override > 1)
        return 1;

    /* Add implicit CPU package compatibility. */
    packages = machine_s->cpu.package;
    if (packages & CPU_PKG_SOCKET3)
        packages |= CPU_PKG_SOCKET1;
    else if (packages & CPU_PKG_SLOT1)
        packages |= CPU_PKG_SOCKET370 | CPU_PKG_SOCKET8;

    /* Package type. */
    if (!(cpu_family->package & packages))
        return 0;

    /* Partial override. */
    if (cpu_override)
        return 1;

    /* Check CPU blocklist. */
    if (machine_s->cpu.block) {
        i = 0;

        while (machine_s->cpu.block[i]) {
            if (machine_s->cpu.block[i++] == cpu_s->cpu_type)
                return 0;
        }
    }

    bus_speed = cpu_s->rspeed / cpu_s->multi;

    /* Minimum bus speed with ~0.84 MHz (for 8086) tolerance. */
    if (machine_s->cpu.min_bus && (bus_speed < (machine_s->cpu.min_bus - 840907)))
        return 0;

    /* Maximum bus speed with ~0.84 MHz (for 8086) tolerance. */
    if (machine_s->cpu.max_bus && (bus_speed > (machine_s->cpu.max_bus + 840907)))
        return 0;

    /* Minimum voltage with 0.1V tolerance. */
    if (machine_s->cpu.min_voltage && (cpu_s->voltage < (machine_s->cpu.min_voltage - 100)))
        return 0;

    /* Maximum voltage with 0.1V tolerance. */
    if (machine_s->cpu.max_voltage && (cpu_s->voltage > (machine_s->cpu.max_voltage + 100)))
        return 0;

    /* Account for CPUs which use a different internal multiplier than specified by jumpers. */
    multi = cpu_s->multi;

    /* Don't care about multiplier compatibility on fixed multiplier CPUs. */
    if (cpu_s->cpu_flags & CPU_FIXED_MULTIPLIER)
        return 1;
    else if (cpu_family->package & CPU_PKG_SOCKET5_7) {
        if ((multi == 1.5) && (cpu_s->cpu_type == CPU_5K86) && (machine_s->cpu.min_multi > 1.5)) /* K5 5k86 */
            multi = 2.0;
        else if (multi == 1.75) /* K5 5k86 */
            multi = 2.5;
        else if (multi == 2.0) {
            if (cpu_s->cpu_type == CPU_5K86) /* K5 5k86 */
                multi = 3.0;
            /* K6-2+ / K6-3+ */
            else if ((cpu_s->cpu_type == CPU_K6_2P) || (cpu_s->cpu_type == CPU_K6_3P))
                multi = 2.5;
            else if (((cpu_s->cpu_type == CPU_WINCHIP) || (cpu_s->cpu_type == CPU_WINCHIP2)) && (machine_s->cpu.min_multi > 2.0)) /* WinChip (2) */
                multi = 2.5;
        } else if (multi == (7.0 / 3.0)) /* WinChip 2A - 2.33x */
            multi = 5.0;
        else if (multi == (8.0 / 3.0)) /* WinChip 2A - 2.66x */
            multi = 5.5;
        else if ((multi == 3.0) && (cpu_s->cpu_type == CPU_Cx6x86 || cpu_s->cpu_type == CPU_Cx6x86L)) /* 6x86(L) */
            multi = 1.5;
        else if (multi == (10.0 / 3.0)) /* WinChip 2A - 3.33x */
            multi = 2.0;
        else if (multi == 3.5) /* standard set by the Pentium MMX */
            multi = 1.5;
        else if (multi == 4.0) {
            /* WinChip (2) */
            if ((cpu_s->cpu_type == CPU_WINCHIP) || (cpu_s->cpu_type == CPU_WINCHIP2)) {
                if (machine_s->cpu.min_multi >= 1.5)
                    multi = 1.5;
                else if (machine_s->cpu.min_multi >= 3.5)
                    multi = 3.5;
                else if (machine_s->cpu.min_multi >= 4.5)
                    multi = 4.5;
            } else if ((cpu_s->cpu_type == CPU_Cx6x86) || (cpu_s->cpu_type == CPU_Cx6x86L)) /* 6x86(L) */
                multi = 3.0;
        } else if ((multi == 5.0) && ((cpu_s->cpu_type == CPU_WINCHIP) || (cpu_s->cpu_type == CPU_WINCHIP2)) && (machine_s->cpu.min_multi > 5.0)) /* WinChip (2) */
            multi = 5.5;
        else if (multi == 6.0) /* K6-2(+) / K6-3(+) */
            multi = 2.0;
    }

    /* Minimum multiplier, */
    if (multi < machine_s->cpu.min_multi)
        return 0;

    /* Maximum multiplier. */
    if (machine_s->cpu.max_multi && (multi > machine_s->cpu.max_multi))
        return 0;

    return 1;
}

uint8_t
cpu_family_is_eligible(const cpu_family_t *cpu_family, int machine)
{
    int c = 0;

    while (cpu_family->cpus[c].cpu_type) {
        if (cpu_is_eligible(cpu_family, c, machine))
            return 1;
        c++;
    }

    return 0;
}

void
SF_FPU_reset(void)
{
    if (fpu_type != FPU_NONE) {
        fpu_state.cwd = 0x0040;
        fpu_state.swd = 0;
        fpu_state.tos = 0;
        fpu_state.tag = 0x5555;
        fpu_state.foo = 0;
        fpu_state.fip = 0;
        fpu_state.fcs = 0;
        fpu_state.fds = 0;
        fpu_state.fdp = 0;
        memset(fpu_state.st_space, 0, sizeof(floatx80) * 8);
    }
}

void
cpu_set(void)
{
    cpu_inited = 1;

    cpu_effective = cpu;
    cpu_s         = (CPU *) &cpu_f->cpus[cpu_effective];

#ifdef USE_ACYCS
    acycs = 0;
#endif /* USE_ACYCS */

    soft_reset_pci    = 0;
    cpu_init          = 0;

    cpu_alt_reset     = 0;
    unmask_a20_in_smm = 0;

    CPUID       = cpu_s->cpuid_model;
    is8086      = (cpu_s->cpu_type > CPU_8088) && (cpu_s->cpu_type != CPU_V20) && (cpu_s->cpu_type != CPU_188);
    is_nec      = (cpu_s->cpu_type == CPU_V20) || (cpu_s->cpu_type == CPU_V30);
    is186       = (cpu_s->cpu_type == CPU_186) || (cpu_s->cpu_type == CPU_188) || (cpu_s->cpu_type == CPU_V20) || (cpu_s->cpu_type == CPU_V30);
    is286       = (cpu_s->cpu_type >= CPU_286);
    is386       = (cpu_s->cpu_type >= CPU_386SX);
    israpidcad  = (cpu_s->cpu_type == CPU_RAPIDCAD);
    isibm486    = (cpu_s->cpu_type == CPU_IBM386SLC) || (cpu_s->cpu_type == CPU_IBM486SLC) || (cpu_s->cpu_type == CPU_IBM486BL);
    is486       = (cpu_s->cpu_type >= CPU_RAPIDCAD);
    is_am486    = (cpu_s->cpu_type == CPU_ENH_Am486DX);
    is_am486dxl = (cpu_s->cpu_type == CPU_Am486DXL);

    is6117 = !strcmp(cpu_f->manufacturer, "ALi");

    cpu_isintel = !strcmp(cpu_f->manufacturer, "Intel");
    cpu_iscyrix = !strcmp(cpu_f->manufacturer, "Cyrix") || !strcmp(cpu_f->manufacturer, "ST");

    /* SL-Enhanced Intel 486s have the same SMM save state table layout as Pentiums,
       and the WinChip datasheet claims those are Pentium-compatible as well. AMD Am486DXL/DXL2 also has compatible SMM, or would if not for it's different SMBase*/
    is_pentium = (cpu_isintel && (cpu_s->cpu_type >= CPU_i486SX_SLENH) && (cpu_s->cpu_type < CPU_PENTIUMPRO)) || !strcmp(cpu_f->manufacturer, "IDT") || (cpu_s->cpu_type == CPU_Am486DXL);
    is_k5      = !strcmp(cpu_f->manufacturer, "AMD") && (cpu_s->cpu_type > CPU_ENH_Am486DX) && (cpu_s->cpu_type < CPU_K6);
    is_k6      = (cpu_s->cpu_type >= CPU_K6) && !strcmp(cpu_f->manufacturer, "AMD");
    /* The Samuel 2 datasheet claims it's Celeron-compatible. */
    is_p6    = (cpu_isintel && (cpu_s->cpu_type >= CPU_PENTIUMPRO)) || !strcmp(cpu_f->manufacturer, "VIA");
    is_cxsmm = (!strcmp(cpu_f->manufacturer, "Cyrix") || !strcmp(cpu_f->manufacturer, "ST")) && (cpu_s->cpu_type >= CPU_Cx486S);

    cpu_isintel = cpu_isintel || !strcmp(cpu_f->manufacturer, "AMD");

    hasfpu   = (fpu_type != FPU_NONE);
    hascache = (cpu_s->cpu_type >= CPU_486SLC) || (cpu_s->cpu_type == CPU_IBM386SLC) || (cpu_s->cpu_type == CPU_IBM486SLC) || (cpu_s->cpu_type == CPU_IBM486BL);

    cpu_16bitbus = (cpu_s->cpu_type == CPU_286) || (cpu_s->cpu_type == CPU_386SX) || (cpu_s->cpu_type == CPU_486SLC) || (cpu_s->cpu_type == CPU_IBM386SLC) || (cpu_s->cpu_type == CPU_IBM486SLC);
    cpu_64bitbus = (cpu_s->cpu_type >= CPU_WINCHIP);

    if (cpu_s->multi)
        cpu_busspeed = cpu_s->rspeed / cpu_s->multi;
    else
        cpu_busspeed = cpu_s->rspeed;
    cpu_multi  = (int) ceil(cpu_s->multi);
    cpu_dmulti = cpu_s->multi;
    ccr0 = ccr1 = ccr2 = ccr3 = ccr4 = ccr5 = ccr6 = 0;

    cpu_update_waitstates();

    isa_cycles = cpu_s->atclk_div;

    if (cpu_s->rspeed <= 8000000)
        cpu_rom_prefetch_cycles = cpu_mem_prefetch_cycles;
    else
        cpu_rom_prefetch_cycles = cpu_s->rspeed / 1000000;

    cpu_set_isa_pci_div(0);
    cpu_set_pci_speed(0);
    cpu_set_agp_speed(0);

    io_handler(cpu_iscyrix, 0x0022, 0x0002, cpu_read, NULL, NULL, cpu_write, NULL, NULL, NULL);

    io_handler(hasfpu, 0x00f0, 0x000f, cpu_read, NULL, NULL, cpu_write, NULL, NULL, NULL);
    io_handler(hasfpu, 0xf007, 0x0001, cpu_read, NULL, NULL, cpu_write, NULL, NULL, NULL);

#ifdef USE_DYNAREC
    x86_setopcodes(ops_386, ops_386_0f, dynarec_ops_386, dynarec_ops_386_0f);
#else
    x86_setopcodes(ops_386, ops_386_0f);
#endif /* USE_DYNAREC */
    x86_setopcodes_2386(ops_2386_386, ops_2386_386_0f);
    x86_opcodes_REPE       = ops_REPE;
    x86_opcodes_REPNE      = ops_REPNE;
    x86_2386_opcodes_REPE  = ops_2386_REPE;
    x86_2386_opcodes_REPNE = ops_2386_REPNE;
    x86_opcodes_3DNOW      = ops_3DNOW;
#ifdef USE_DYNAREC
    x86_dynarec_opcodes_REPE  = dynarec_ops_REPE;
    x86_dynarec_opcodes_REPNE = dynarec_ops_REPNE;
    x86_dynarec_opcodes_3DNOW = dynarec_ops_3DNOW;
#endif /* USE_DYNAREC */

    if (hasfpu) {
#ifdef USE_DYNAREC
        if (fpu_softfloat) {
            x86_dynarec_opcodes_d8_a16 = dynarec_ops_sf_fpu_d8_a16;
            x86_dynarec_opcodes_d8_a32 = dynarec_ops_sf_fpu_d8_a32;
            x86_dynarec_opcodes_d9_a16 = dynarec_ops_sf_fpu_d9_a16;
            x86_dynarec_opcodes_d9_a32 = dynarec_ops_sf_fpu_d9_a32;
            x86_dynarec_opcodes_da_a16 = dynarec_ops_sf_fpu_da_a16;
            x86_dynarec_opcodes_da_a32 = dynarec_ops_sf_fpu_da_a32;
            x86_dynarec_opcodes_db_a16 = dynarec_ops_sf_fpu_db_a16;
            x86_dynarec_opcodes_db_a32 = dynarec_ops_sf_fpu_db_a32;
            x86_dynarec_opcodes_dc_a16 = dynarec_ops_sf_fpu_dc_a16;
            x86_dynarec_opcodes_dc_a32 = dynarec_ops_sf_fpu_dc_a32;
            x86_dynarec_opcodes_dd_a16 = dynarec_ops_sf_fpu_dd_a16;
            x86_dynarec_opcodes_dd_a32 = dynarec_ops_sf_fpu_dd_a32;
            x86_dynarec_opcodes_de_a16 = dynarec_ops_sf_fpu_de_a16;
            x86_dynarec_opcodes_de_a32 = dynarec_ops_sf_fpu_de_a32;
            x86_dynarec_opcodes_df_a16 = dynarec_ops_sf_fpu_df_a16;
            x86_dynarec_opcodes_df_a32 = dynarec_ops_sf_fpu_df_a32;
        } else {
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
#endif /* USE_DYNAREC */
        if (fpu_softfloat) {
            x86_opcodes_d8_a16 = ops_sf_fpu_d8_a16;
            x86_opcodes_d8_a32 = ops_sf_fpu_d8_a32;
            x86_opcodes_d9_a16 = ops_sf_fpu_d9_a16;
            x86_opcodes_d9_a32 = ops_sf_fpu_d9_a32;
            x86_opcodes_da_a16 = ops_sf_fpu_da_a16;
            x86_opcodes_da_a32 = ops_sf_fpu_da_a32;
            x86_opcodes_db_a16 = ops_sf_fpu_db_a16;
            x86_opcodes_db_a32 = ops_sf_fpu_db_a32;
            x86_opcodes_dc_a16 = ops_sf_fpu_dc_a16;
            x86_opcodes_dc_a32 = ops_sf_fpu_dc_a32;
            x86_opcodes_dd_a16 = ops_sf_fpu_dd_a16;
            x86_opcodes_dd_a32 = ops_sf_fpu_dd_a32;
            x86_opcodes_de_a16 = ops_sf_fpu_de_a16;
            x86_opcodes_de_a32 = ops_sf_fpu_de_a32;
            x86_opcodes_df_a16 = ops_sf_fpu_df_a16;
            x86_opcodes_df_a32 = ops_sf_fpu_df_a32;

            x86_2386_opcodes_d8_a16 = ops_2386_sf_fpu_d8_a16;
            x86_2386_opcodes_d8_a32 = ops_2386_sf_fpu_d8_a32;
            x86_2386_opcodes_d9_a16 = ops_2386_sf_fpu_d9_a16;
            x86_2386_opcodes_d9_a32 = ops_2386_sf_fpu_d9_a32;
            x86_2386_opcodes_da_a16 = ops_2386_sf_fpu_da_a16;
            x86_2386_opcodes_da_a32 = ops_2386_sf_fpu_da_a32;
            x86_2386_opcodes_db_a16 = ops_2386_sf_fpu_db_a16;
            x86_2386_opcodes_db_a32 = ops_2386_sf_fpu_db_a32;
            x86_2386_opcodes_dc_a16 = ops_2386_sf_fpu_dc_a16;
            x86_2386_opcodes_dc_a32 = ops_2386_sf_fpu_dc_a32;
            x86_2386_opcodes_dd_a16 = ops_2386_sf_fpu_dd_a16;
            x86_2386_opcodes_dd_a32 = ops_2386_sf_fpu_dd_a32;
            x86_2386_opcodes_de_a16 = ops_2386_sf_fpu_de_a16;
            x86_2386_opcodes_de_a32 = ops_2386_sf_fpu_de_a32;
            x86_2386_opcodes_df_a16 = ops_2386_sf_fpu_df_a16;
            x86_2386_opcodes_df_a32 = ops_2386_sf_fpu_df_a32;
        } else {
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

            x86_2386_opcodes_d8_a16 = ops_2386_fpu_d8_a16;
            x86_2386_opcodes_d8_a32 = ops_2386_fpu_d8_a32;
            x86_2386_opcodes_d9_a16 = ops_2386_fpu_d9_a16;
            x86_2386_opcodes_d9_a32 = ops_2386_fpu_d9_a32;
            x86_2386_opcodes_da_a16 = ops_2386_fpu_da_a16;
            x86_2386_opcodes_da_a32 = ops_2386_fpu_da_a32;
            x86_2386_opcodes_db_a16 = ops_2386_fpu_db_a16;
            x86_2386_opcodes_db_a32 = ops_2386_fpu_db_a32;
            x86_2386_opcodes_dc_a16 = ops_2386_fpu_dc_a16;
            x86_2386_opcodes_dc_a32 = ops_2386_fpu_dc_a32;
            x86_2386_opcodes_dd_a16 = ops_2386_fpu_dd_a16;
            x86_2386_opcodes_dd_a32 = ops_2386_fpu_dd_a32;
            x86_2386_opcodes_de_a16 = ops_2386_fpu_de_a16;
            x86_2386_opcodes_de_a32 = ops_2386_fpu_de_a32;
            x86_2386_opcodes_df_a16 = ops_2386_fpu_df_a16;
            x86_2386_opcodes_df_a32 = ops_2386_fpu_df_a32;
        }
    } else {
#ifdef USE_DYNAREC
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
#endif /* USE_DYNAREC */
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

        x86_2386_opcodes_d8_a16 = ops_2386_nofpu_a16;
        x86_2386_opcodes_d8_a32 = ops_2386_nofpu_a32;
        x86_2386_opcodes_d9_a16 = ops_2386_nofpu_a16;
        x86_2386_opcodes_d9_a32 = ops_2386_nofpu_a32;
        x86_2386_opcodes_da_a16 = ops_2386_nofpu_a16;
        x86_2386_opcodes_da_a32 = ops_2386_nofpu_a32;
        x86_2386_opcodes_db_a16 = ops_2386_nofpu_a16;
        x86_2386_opcodes_db_a32 = ops_2386_nofpu_a32;
        x86_2386_opcodes_dc_a16 = ops_2386_nofpu_a16;
        x86_2386_opcodes_dc_a32 = ops_2386_nofpu_a32;
        x86_2386_opcodes_dd_a16 = ops_2386_nofpu_a16;
        x86_2386_opcodes_dd_a32 = ops_2386_nofpu_a32;
        x86_2386_opcodes_de_a16 = ops_2386_nofpu_a16;
        x86_2386_opcodes_de_a32 = ops_2386_nofpu_a32;
        x86_2386_opcodes_df_a16 = ops_2386_nofpu_a16;
        x86_2386_opcodes_df_a32 = ops_2386_nofpu_a32;
    }

#ifdef USE_DYNAREC
    codegen_timing_set(&codegen_timing_486);
#endif /* USE_DYNAREC */

    memset(&msr, 0, sizeof(msr));

    timing_misaligned   = 0;
    cpu_cyrix_alignment = 0;
    cpu_cpurst_on_sr    = 0;
    cpu_CR4_mask        = 0;

    switch (cpu_s->cpu_type) {
        case CPU_8088:
        case CPU_8086:
            break;

        case CPU_V20:
        case CPU_V30:
        case CPU_186:
        case CPU_188:
#ifdef USE_DYNAREC
            x86_setopcodes(ops_186, ops_186_0f, dynarec_ops_186, dynarec_ops_186_0f);
#else
            x86_setopcodes(ops_186, ops_186_0f);
#endif /* USE_DYNAREC */
            x86_setopcodes_2386(ops_2386_186, ops_2386_186_0f);
            break;

        case CPU_286:
#ifdef USE_DYNAREC
            x86_setopcodes(ops_286, ops_286_0f, dynarec_ops_286, dynarec_ops_286_0f);
#else
            x86_setopcodes(ops_286, ops_286_0f);
#endif /* USE_DYNAREC */
            x86_setopcodes_2386(ops_2386_286, ops_2386_286_0f);

            if (fpu_type == FPU_287) {
#ifdef USE_DYNAREC
                if (fpu_softfloat) {
                    x86_dynarec_opcodes_d9_a16 = dynarec_ops_sf_fpu_287_d9_a16;
                    x86_dynarec_opcodes_d9_a32 = dynarec_ops_sf_fpu_287_d9_a32;
                    x86_dynarec_opcodes_da_a16 = dynarec_ops_sf_fpu_287_da_a16;
                    x86_dynarec_opcodes_da_a32 = dynarec_ops_sf_fpu_287_da_a32;
                    x86_dynarec_opcodes_db_a16 = dynarec_ops_sf_fpu_287_db_a16;
                    x86_dynarec_opcodes_db_a32 = dynarec_ops_sf_fpu_287_db_a32;
                    x86_dynarec_opcodes_dc_a16 = dynarec_ops_sf_fpu_287_dc_a16;
                    x86_dynarec_opcodes_dc_a32 = dynarec_ops_sf_fpu_287_dc_a32;
                    x86_dynarec_opcodes_dd_a16 = dynarec_ops_sf_fpu_287_dd_a16;
                    x86_dynarec_opcodes_dd_a32 = dynarec_ops_sf_fpu_287_dd_a32;
                    x86_dynarec_opcodes_de_a16 = dynarec_ops_sf_fpu_287_de_a16;
                    x86_dynarec_opcodes_de_a32 = dynarec_ops_sf_fpu_287_de_a32;
                    x86_dynarec_opcodes_df_a16 = dynarec_ops_sf_fpu_287_df_a16;
                    x86_dynarec_opcodes_df_a32 = dynarec_ops_sf_fpu_287_df_a32;
                } else {
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
                }
#endif /* USE_DYNAREC */
                if (fpu_softfloat) {
                    x86_opcodes_d9_a16 = ops_sf_fpu_287_d9_a16;
                    x86_opcodes_d9_a32 = ops_sf_fpu_287_d9_a32;
                    x86_opcodes_da_a16 = ops_sf_fpu_287_da_a16;
                    x86_opcodes_da_a32 = ops_sf_fpu_287_da_a32;
                    x86_opcodes_db_a16 = ops_sf_fpu_287_db_a16;
                    x86_opcodes_db_a32 = ops_sf_fpu_287_db_a32;
                    x86_opcodes_dc_a16 = ops_sf_fpu_287_dc_a16;
                    x86_opcodes_dc_a32 = ops_sf_fpu_287_dc_a32;
                    x86_opcodes_dd_a16 = ops_sf_fpu_287_dd_a16;
                    x86_opcodes_dd_a32 = ops_sf_fpu_287_dd_a32;
                    x86_opcodes_de_a16 = ops_sf_fpu_287_de_a16;
                    x86_opcodes_de_a32 = ops_sf_fpu_287_de_a32;
                    x86_opcodes_df_a16 = ops_sf_fpu_287_df_a16;
                    x86_opcodes_df_a32 = ops_sf_fpu_287_df_a32;

                    x86_2386_opcodes_d9_a16 = ops_2386_sf_fpu_287_d9_a16;
                    x86_2386_opcodes_d9_a32 = ops_2386_sf_fpu_287_d9_a32;
                    x86_2386_opcodes_da_a16 = ops_2386_sf_fpu_287_da_a16;
                    x86_2386_opcodes_da_a32 = ops_2386_sf_fpu_287_da_a32;
                    x86_2386_opcodes_db_a16 = ops_2386_sf_fpu_287_db_a16;
                    x86_2386_opcodes_db_a32 = ops_2386_sf_fpu_287_db_a32;
                    x86_2386_opcodes_dc_a16 = ops_2386_sf_fpu_287_dc_a16;
                    x86_2386_opcodes_dc_a32 = ops_2386_sf_fpu_287_dc_a32;
                    x86_2386_opcodes_dd_a16 = ops_2386_sf_fpu_287_dd_a16;
                    x86_2386_opcodes_dd_a32 = ops_2386_sf_fpu_287_dd_a32;
                    x86_2386_opcodes_de_a16 = ops_2386_sf_fpu_287_de_a16;
                    x86_2386_opcodes_de_a32 = ops_2386_sf_fpu_287_de_a32;
                    x86_2386_opcodes_df_a16 = ops_2386_sf_fpu_287_df_a16;
                    x86_2386_opcodes_df_a32 = ops_2386_sf_fpu_287_df_a32;
                } else {
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

                    x86_2386_opcodes_d9_a16 = ops_2386_fpu_287_d9_a16;
                    x86_2386_opcodes_d9_a32 = ops_2386_fpu_287_d9_a32;
                    x86_2386_opcodes_da_a16 = ops_2386_fpu_287_da_a16;
                    x86_2386_opcodes_da_a32 = ops_2386_fpu_287_da_a32;
                    x86_2386_opcodes_db_a16 = ops_2386_fpu_287_db_a16;
                    x86_2386_opcodes_db_a32 = ops_2386_fpu_287_db_a32;
                    x86_2386_opcodes_dc_a16 = ops_2386_fpu_287_dc_a16;
                    x86_2386_opcodes_dc_a32 = ops_2386_fpu_287_dc_a32;
                    x86_2386_opcodes_dd_a16 = ops_2386_fpu_287_dd_a16;
                    x86_2386_opcodes_dd_a32 = ops_2386_fpu_287_dd_a32;
                    x86_2386_opcodes_de_a16 = ops_2386_fpu_287_de_a16;
                    x86_2386_opcodes_de_a32 = ops_2386_fpu_287_de_a32;
                    x86_2386_opcodes_df_a16 = ops_2386_fpu_287_df_a16;
                    x86_2386_opcodes_df_a32 = ops_2386_fpu_287_df_a32;
                }
            }

            timing_rr  = 2;  /* register dest - register src */
            timing_rm  = 7;  /* register dest - memory src */
            timing_mr  = 7;  /* memory dest   - register src */
            timing_mm  = 7;  /* memory dest   - memory src */
            timing_rml = 9;  /* register dest - memory src long */
            timing_mrl = 11; /* memory dest   - register src long */
            timing_mml = 11; /* memory dest   - memory src */
            timing_bt  = 4;  /* branch taken */
            timing_bnt = 3;  /* branch not taken */

            timing_int                = 0;
            timing_int_rm             = 23;
            timing_int_v86            = 0;
            timing_int_pm             = 40;
            timing_int_pm_outer       = 78;
            timing_iret_rm            = 17;
            timing_iret_v86           = 0;
            timing_iret_pm            = 31;
            timing_iret_pm_outer      = 55;
            timing_call_rm            = 13;
            timing_call_pm            = 26;
            timing_call_pm_gate       = 52;
            timing_call_pm_gate_inner = 82;
            timing_retf_rm            = 15;
            timing_retf_pm            = 25;
            timing_retf_pm_outer      = 55;
            timing_jmp_rm             = 11;
            timing_jmp_pm             = 23;
            timing_jmp_pm_gate        = 38;
            break;

        case CPU_IBM486SLC:
        case CPU_IBM386SLC:
        case CPU_IBM486BL:
#ifdef USE_DYNAREC
            x86_setopcodes(ops_386, ops_ibm486_0f, dynarec_ops_386, dynarec_ops_ibm486_0f);
#else
            x86_setopcodes(ops_386, ops_ibm486_0f);
#endif  /* USE_DYNAREC */
            x86_setopcodes_2386(ops_2386_386, ops_2386_ibm486_0f);
            cpu_features = CPU_FEATURE_MSR;
            fallthrough;
        case CPU_386SX:
        case CPU_386DX:
            /* In case we get Deskpro 386 emulation */
            if (fpu_type == FPU_287) {
#ifdef USE_DYNAREC
                if (fpu_softfloat) {
                    x86_dynarec_opcodes_d9_a16 = dynarec_ops_sf_fpu_287_d9_a16;
                    x86_dynarec_opcodes_d9_a32 = dynarec_ops_sf_fpu_287_d9_a32;
                    x86_dynarec_opcodes_da_a16 = dynarec_ops_sf_fpu_287_da_a16;
                    x86_dynarec_opcodes_da_a32 = dynarec_ops_sf_fpu_287_da_a32;
                    x86_dynarec_opcodes_db_a16 = dynarec_ops_sf_fpu_287_db_a16;
                    x86_dynarec_opcodes_db_a32 = dynarec_ops_sf_fpu_287_db_a32;
                    x86_dynarec_opcodes_dc_a16 = dynarec_ops_sf_fpu_287_dc_a16;
                    x86_dynarec_opcodes_dc_a32 = dynarec_ops_sf_fpu_287_dc_a32;
                    x86_dynarec_opcodes_dd_a16 = dynarec_ops_sf_fpu_287_dd_a16;
                    x86_dynarec_opcodes_dd_a32 = dynarec_ops_sf_fpu_287_dd_a32;
                    x86_dynarec_opcodes_de_a16 = dynarec_ops_sf_fpu_287_de_a16;
                    x86_dynarec_opcodes_de_a32 = dynarec_ops_sf_fpu_287_de_a32;
                    x86_dynarec_opcodes_df_a16 = dynarec_ops_sf_fpu_287_df_a16;
                    x86_dynarec_opcodes_df_a32 = dynarec_ops_sf_fpu_287_df_a32;
                } else {
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
                }
#endif /* USE_DYNAREC */
                if (fpu_softfloat) {
                    x86_opcodes_d9_a16 = ops_sf_fpu_287_d9_a16;
                    x86_opcodes_d9_a32 = ops_sf_fpu_287_d9_a32;
                    x86_opcodes_da_a16 = ops_sf_fpu_287_da_a16;
                    x86_opcodes_da_a32 = ops_sf_fpu_287_da_a32;
                    x86_opcodes_db_a16 = ops_sf_fpu_287_db_a16;
                    x86_opcodes_db_a32 = ops_sf_fpu_287_db_a32;
                    x86_opcodes_dc_a16 = ops_sf_fpu_287_dc_a16;
                    x86_opcodes_dc_a32 = ops_sf_fpu_287_dc_a32;
                    x86_opcodes_dd_a16 = ops_sf_fpu_287_dd_a16;
                    x86_opcodes_dd_a32 = ops_sf_fpu_287_dd_a32;
                    x86_opcodes_de_a16 = ops_sf_fpu_287_de_a16;
                    x86_opcodes_de_a32 = ops_sf_fpu_287_de_a32;
                    x86_opcodes_df_a16 = ops_sf_fpu_287_df_a16;
                    x86_opcodes_df_a32 = ops_sf_fpu_287_df_a32;

                    x86_2386_opcodes_d9_a16 = ops_2386_sf_fpu_287_d9_a16;
                    x86_2386_opcodes_d9_a32 = ops_2386_sf_fpu_287_d9_a32;
                    x86_2386_opcodes_da_a16 = ops_2386_sf_fpu_287_da_a16;
                    x86_2386_opcodes_da_a32 = ops_2386_sf_fpu_287_da_a32;
                    x86_2386_opcodes_db_a16 = ops_2386_sf_fpu_287_db_a16;
                    x86_2386_opcodes_db_a32 = ops_2386_sf_fpu_287_db_a32;
                    x86_2386_opcodes_dc_a16 = ops_2386_sf_fpu_287_dc_a16;
                    x86_2386_opcodes_dc_a32 = ops_2386_sf_fpu_287_dc_a32;
                    x86_2386_opcodes_dd_a16 = ops_2386_sf_fpu_287_dd_a16;
                    x86_2386_opcodes_dd_a32 = ops_2386_sf_fpu_287_dd_a32;
                    x86_2386_opcodes_de_a16 = ops_2386_sf_fpu_287_de_a16;
                    x86_2386_opcodes_de_a32 = ops_2386_sf_fpu_287_de_a32;
                    x86_2386_opcodes_df_a16 = ops_2386_sf_fpu_287_df_a16;
                    x86_2386_opcodes_df_a32 = ops_2386_sf_fpu_287_df_a32;
                } else {
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

                    x86_2386_opcodes_d9_a16 = ops_2386_fpu_287_d9_a16;
                    x86_2386_opcodes_d9_a32 = ops_2386_fpu_287_d9_a32;
                    x86_2386_opcodes_da_a16 = ops_2386_fpu_287_da_a16;
                    x86_2386_opcodes_da_a32 = ops_2386_fpu_287_da_a32;
                    x86_2386_opcodes_db_a16 = ops_2386_fpu_287_db_a16;
                    x86_2386_opcodes_db_a32 = ops_2386_fpu_287_db_a32;
                    x86_2386_opcodes_dc_a16 = ops_2386_fpu_287_dc_a16;
                    x86_2386_opcodes_dc_a32 = ops_2386_fpu_287_dc_a32;
                    x86_2386_opcodes_dd_a16 = ops_2386_fpu_287_dd_a16;
                    x86_2386_opcodes_dd_a32 = ops_2386_fpu_287_dd_a32;
                    x86_2386_opcodes_de_a16 = ops_2386_fpu_287_de_a16;
                    x86_2386_opcodes_de_a32 = ops_2386_fpu_287_de_a32;
                    x86_2386_opcodes_df_a16 = ops_2386_fpu_287_df_a16;
                    x86_2386_opcodes_df_a32 = ops_2386_fpu_287_df_a32;
                }
            }

            timing_rr = 2; /* register dest - register src */
            timing_rm = 6; /* register dest - memory src */
            timing_mr = 7; /* memory dest   - register src */
            timing_mm = 6; /* memory dest   - memory src */
            if (cpu_s->cpu_type >= CPU_386DX) {
                timing_rml = 6; /* register dest - memory src long */
                timing_mrl = 7; /* memory dest   - register src long */
                timing_mml = 6; /* memory dest   - memory src */
            } else {
                timing_rml = 8;  /* register dest - memory src long */
                timing_mrl = 11; /* memory dest   - register src long */
                timing_mml = 10; /* memory dest   - memory src */
            }
            timing_bt  = 4; /* branch taken */
            timing_bnt = 3; /* branch not taken */

            timing_int                = 0;
            timing_int_rm             = 37;
            timing_int_v86            = 59;
            timing_int_pm             = 99;
            timing_int_pm_outer       = 119;
            timing_iret_rm            = 22;
            timing_iret_v86           = 60;
            timing_iret_pm            = 38;
            timing_iret_pm_outer      = 82;
            timing_call_rm            = 17;
            timing_call_pm            = 34;
            timing_call_pm_gate       = 52;
            timing_call_pm_gate_inner = 86;
            timing_retf_rm            = 18;
            timing_retf_pm            = 32;
            timing_retf_pm_outer      = 68;
            timing_jmp_rm             = 12;
            timing_jmp_pm             = 27;
            timing_jmp_pm_gate        = 45;
            break;

        case CPU_486SLC:
#ifdef USE_DYNAREC
            x86_setopcodes(ops_386, ops_486_0f, dynarec_ops_386, dynarec_ops_486_0f);
#else
            x86_setopcodes(ops_386, ops_486_0f);
#endif /* USE_DYNAREC */
            x86_setopcodes_2386(ops_2386_386, ops_2386_486_0f);

            timing_rr  = 1; /* register dest - register src */
            timing_rm  = 3; /* register dest - memory src */
            timing_mr  = 5; /* memory dest   - register src */
            timing_mm  = 3;
            timing_rml = 5; /* register dest - memory src long */
            timing_mrl = 7; /* memory dest   - register src long */
            timing_mml = 7;
            timing_bt  = 5; /* branch taken */
            timing_bnt = 1; /* branch not taken */

            timing_int                = 4; /* unknown */
            timing_int_rm             = 14;
            timing_int_v86            = 82;
            timing_int_pm             = 49;
            timing_int_pm_outer       = 77;
            timing_iret_rm            = 14;
            timing_iret_v86           = 66;
            timing_iret_pm            = 31;
            timing_iret_pm_outer      = 66;
            timing_call_rm            = 12;
            timing_call_pm            = 30;
            timing_call_pm_gate       = 41;
            timing_call_pm_gate_inner = 83;
            timing_retf_rm            = 13;
            timing_retf_pm            = 26;
            timing_retf_pm_outer      = 61;
            timing_jmp_rm             = 9;
            timing_jmp_pm             = 26;
            timing_jmp_pm_gate        = 37;
            timing_misaligned         = 3;
            break;

        case CPU_486DLC:
#ifdef USE_DYNAREC
            x86_setopcodes(ops_386, ops_486_0f, dynarec_ops_386, dynarec_ops_486_0f);
#else
            x86_setopcodes(ops_386, ops_486_0f);
#endif /* USE_DYNAREC */
            x86_setopcodes_2386(ops_2386_386, ops_2386_486_0f);

            timing_rr  = 1; /* register dest - register src */
            timing_rm  = 3; /* register dest - memory src */
            timing_mr  = 3; /* memory dest   - register src */
            timing_mm  = 3;
            timing_rml = 3; /* register dest - memory src long */
            timing_mrl = 3; /* memory dest   - register src long */
            timing_mml = 3;
            timing_bt  = 5; /* branch taken */
            timing_bnt = 1; /* branch not taken */

            timing_int                = 4; /* unknown */
            timing_int_rm             = 14;
            timing_int_v86            = 82;
            timing_int_pm             = 49;
            timing_int_pm_outer       = 77;
            timing_iret_rm            = 14;
            timing_iret_v86           = 66;
            timing_iret_pm            = 31;
            timing_iret_pm_outer      = 66;
            timing_call_rm            = 12;
            timing_call_pm            = 30;
            timing_call_pm_gate       = 41;
            timing_call_pm_gate_inner = 83;
            timing_retf_rm            = 13;
            timing_retf_pm            = 26;
            timing_retf_pm_outer      = 61;
            timing_jmp_rm             = 9;
            timing_jmp_pm             = 26;
            timing_jmp_pm_gate        = 37;

            timing_misaligned = 3;
            break;

        case CPU_i486SX_SLENH:
        case CPU_i486DX_SLENH:
            cpu_features = CPU_FEATURE_CR4 | CPU_FEATURE_VME;
            cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_VME;
            fallthrough;
        case CPU_RAPIDCAD:
        case CPU_i486SX:
        case CPU_i486DX:
        case CPU_Am486SX:
        case CPU_Am486DX:
        case CPU_Am486DXL:
        case CPU_ENH_Am486DX:
            /*AMD timing identical to Intel*/
#ifdef USE_DYNAREC
            x86_setopcodes(ops_386, ops_486_0f, dynarec_ops_386, dynarec_ops_486_0f);
#else
            x86_setopcodes(ops_386, ops_486_0f);
#endif /* USE_DYNAREC */
            x86_setopcodes_2386(ops_2386_386, ops_2386_486_0f);

            timing_rr  = 1; /* register dest - register src */
            timing_rm  = 2; /* register dest - memory src */
            timing_mr  = 3; /* memory dest   - register src */
            timing_mm  = 3;
            timing_rml = 2; /* register dest - memory src long */
            timing_mrl = 3; /* memory dest   - register src long */
            timing_mml = 3;
            timing_bt  = 2; /* branch taken */
            timing_bnt = 1; /* branch not taken */

            timing_int                = 4;
            timing_int_rm             = 26;
            timing_int_v86            = 82;
            timing_int_pm             = 44;
            timing_int_pm_outer       = 71;
            timing_iret_rm            = 15;
            timing_iret_v86           = 36; /* unknown */
            timing_iret_pm            = 20;
            timing_iret_pm_outer      = 36;
            timing_call_rm            = 18;
            timing_call_pm            = 20;
            timing_call_pm_gate       = 35;
            timing_call_pm_gate_inner = 69;
            timing_retf_rm            = 13;
            timing_retf_pm            = 17;
            timing_retf_pm_outer      = 35;
            timing_jmp_rm             = 17;
            timing_jmp_pm             = 19;
            timing_jmp_pm_gate        = 32;

            timing_misaligned = 3;
            break;

        case CPU_Cx486S:
        case CPU_Cx486DX:
        case CPU_STPC:
#ifdef USE_DYNAREC
            if (cpu_s->cpu_type == CPU_STPC)
                x86_setopcodes(ops_386, ops_stpc_0f, dynarec_ops_386, dynarec_ops_stpc_0f);
            else
                x86_setopcodes(ops_386, ops_c486_0f, dynarec_ops_386, dynarec_ops_c486_0f);
#else
            if (cpu_s->cpu_type == CPU_STPC)
                x86_setopcodes(ops_386, ops_stpc_0f);
            else
                x86_setopcodes(ops_386, ops_c486_0f);
#endif /* USE_DYNAREC */

            timing_rr  = 1; /* register dest - register src */
            timing_rm  = 3; /* register dest - memory src */
            timing_mr  = 3; /* memory dest   - register src */
            timing_mm  = 3;
            timing_rml = 3; /* register dest - memory src long */
            timing_mrl = 3; /* memory dest   - register src long */
            timing_mml = 3;
            timing_bt  = 3; /* branch taken */
            timing_bnt = 1; /* branch not taken */

            timing_int                = 4;
            timing_int_rm             = 14;
            timing_int_v86            = 82;
            timing_int_pm             = 49;
            timing_int_pm_outer       = 77;
            timing_iret_rm            = 14;
            timing_iret_v86           = 66; /* unknown */
            timing_iret_pm            = 31;
            timing_iret_pm_outer      = 66;
            timing_call_rm            = 12;
            timing_call_pm            = 30;
            timing_call_pm_gate       = 41;
            timing_call_pm_gate_inner = 83;
            timing_retf_rm            = 13;
            timing_retf_pm            = 26;
            timing_retf_pm_outer      = 61;
            timing_jmp_rm             = 9;
            timing_jmp_pm             = 26;
            timing_jmp_pm_gate        = 37;

            timing_misaligned = 3;

            if (cpu_s->cpu_type == CPU_STPC)
                cpu_features = CPU_FEATURE_RDTSC;
            break;

        case CPU_Cx5x86:
#ifdef USE_DYNAREC
            x86_setopcodes(ops_386, ops_c486_0f, dynarec_ops_386, dynarec_ops_c486_0f);
#else
            x86_setopcodes(ops_386, ops_c486_0f);
#endif /* USE_DYNAREC */

            timing_rr  = 1; /* register dest - register src */
            timing_rm  = 1; /* register dest - memory src */
            timing_mr  = 2; /* memory dest   - register src */
            timing_mm  = 2;
            timing_rml = 1; /* register dest - memory src long */
            timing_mrl = 2; /* memory dest   - register src long */
            timing_mml = 2;
            timing_bt  = 4; /* branch taken */
            timing_bnt = 1; /* branch not taken */

            timing_int                = 0;
            timing_int_rm             = 9;
            timing_int_v86            = 82; /* unknown */
            timing_int_pm             = 21;
            timing_int_pm_outer       = 32;
            timing_iret_rm            = 7;
            timing_iret_v86           = 26; /* unknown */
            timing_iret_pm            = 10;
            timing_iret_pm_outer      = 26;
            timing_call_rm            = 4;
            timing_call_pm            = 15;
            timing_call_pm_gate       = 26;
            timing_call_pm_gate_inner = 35;
            timing_retf_rm            = 4;
            timing_retf_pm            = 7;
            timing_retf_pm_outer      = 23;
            timing_jmp_rm             = 5;
            timing_jmp_pm             = 7;
            timing_jmp_pm_gate        = 17;

            timing_misaligned = 2;

            cpu_cyrix_alignment = 1;
            break;

        case CPU_WINCHIP:
        case CPU_WINCHIP2:
#ifdef USE_DYNAREC
            if (cpu_s->cpu_type == CPU_WINCHIP2)
                x86_setopcodes(ops_386, ops_winchip2_0f, dynarec_ops_386, dynarec_ops_winchip2_0f);
            else
                x86_setopcodes(ops_386, ops_winchip_0f, dynarec_ops_386, dynarec_ops_winchip_0f);
#else
            if (cpu_s->cpu_type == CPU_WINCHIP2)
                x86_setopcodes(ops_386, ops_winchip2_0f);
            else
                x86_setopcodes(ops_386, ops_winchip_0f);
#endif /* USE_DYNAREC */

            timing_rr  = 1; /* register dest - register src */
            timing_rm  = 2; /* register dest - memory src */
            timing_mr  = 2; /* memory dest   - register src */
            timing_mm  = 3;
            timing_rml = 2; /* register dest - memory src long */
            timing_mrl = 2; /* memory dest   - register src long */
            timing_mml = 3;
            timing_bt  = 2; /* branch taken */
            timing_bnt = 1; /* branch not taken */

            /*unknown*/
            timing_int_rm             = 26;
            timing_int_v86            = 82;
            timing_int_pm             = 44;
            timing_int_pm_outer       = 71;
            timing_iret_rm            = 7;
            timing_iret_v86           = 26;
            timing_iret_pm            = 10;
            timing_iret_pm_outer      = 26;
            timing_call_rm            = 4;
            timing_call_pm            = 15;
            timing_call_pm_gate       = 26;
            timing_call_pm_gate_inner = 35;
            timing_retf_rm            = 4;
            timing_retf_pm            = 7;
            timing_retf_pm_outer      = 23;
            timing_jmp_rm             = 5;
            timing_jmp_pm             = 7;
            timing_jmp_pm_gate        = 17;

            timing_misaligned = 2;

            cpu_cyrix_alignment = 1;

            cpu_features = CPU_FEATURE_RDTSC | CPU_FEATURE_MMX | CPU_FEATURE_MSR | CPU_FEATURE_CR4;
            if (cpu_s->cpu_type == CPU_WINCHIP2)
                cpu_features |= CPU_FEATURE_3DNOW;
            msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) | (1 << 16) | (1 << 19) | (1 << 21);
            if (cpu_s->cpu_type == CPU_WINCHIP2)
                msr.fcr |= (1 << 18) | (1 << 20);
            cpu_CR4_mask = CR4_TSD | CR4_DE | CR4_MCE | CR4_PCE;

#ifdef USE_DYNAREC
            if (cpu_s->cpu_type == CPU_WINCHIP2)
                codegen_timing_set(&codegen_timing_winchip2);
            else
                codegen_timing_set(&codegen_timing_winchip);
#endif /* USE_DYNAREC */
            break;

        case CPU_P24T:
        case CPU_PENTIUM:
        case CPU_PENTIUMMMX:
#ifdef USE_DYNAREC
            if (cpu_s->cpu_type == CPU_PENTIUMMMX)
                x86_setopcodes(ops_386, ops_pentiummmx_0f, dynarec_ops_386, dynarec_ops_pentiummmx_0f);
            else
                x86_setopcodes(ops_386, ops_pentium_0f, dynarec_ops_386, dynarec_ops_pentium_0f);
#else
            if (cpu_s->cpu_type == CPU_PENTIUMMMX)
                x86_setopcodes(ops_386, ops_pentiummmx_0f);
            else
                x86_setopcodes(ops_386, ops_pentium_0f);
#endif /* USE_DYNAREC */

            timing_rr  = 1; /* register dest - register src */
            timing_rm  = 2; /* register dest - memory src */
            timing_mr  = 3; /* memory dest   - register src */
            timing_mm  = 3;
            timing_rml = 2; /* register dest - memory src long */
            timing_mrl = 3; /* memory dest   - register src long */
            timing_mml = 3;
            timing_bt  = 0; /* branch taken */
            if (cpu_s->cpu_type == CPU_PENTIUMMMX)
                timing_bnt = 1; /* branch not taken */
            else
                timing_bnt = 2; /* branch not taken */

            timing_int                = 6;
            timing_int_rm             = 11;
            timing_int_v86            = 54;
            timing_int_pm             = 25;
            timing_int_pm_outer       = 42;
            timing_iret_rm            = 7;
            timing_iret_v86           = 27; /* unknown */
            timing_iret_pm            = 10;
            timing_iret_pm_outer      = 27;
            timing_call_rm            = 4;
            timing_call_pm            = 4;
            timing_call_pm_gate       = 22;
            timing_call_pm_gate_inner = 44;
            timing_retf_rm            = 4;
            timing_retf_pm            = 4;
            timing_retf_pm_outer      = 23;
            timing_jmp_rm             = 3;
            timing_jmp_pm             = 3;
            timing_jmp_pm_gate        = 18;

            timing_misaligned = 3;

            cpu_features = CPU_FEATURE_RDTSC | CPU_FEATURE_MSR | CPU_FEATURE_CR4 | CPU_FEATURE_VME;
            if (cpu_s->cpu_type == CPU_PENTIUMMMX)
                cpu_features |= CPU_FEATURE_MMX;
            cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | CR4_PSE | CR4_MCE | CR4_PCE;
#ifdef USE_DYNAREC
            codegen_timing_set(&codegen_timing_pentium);
#endif /* USE_DYNAREC */
            break;

#ifdef USE_CYRIX_6X86
        case CPU_Cx6x86:
        case CPU_Cx6x86L:
        case CPU_CxGX1:
        case CPU_Cx6x86MX:
            if (cpu_s->cpu_type == CPU_Cx6x86MX) {
#    ifdef USE_DYNAREC
                if (fpu_softfloat) {
                    x86_dynarec_opcodes_da_a16 = dynarec_ops_sf_fpu_686_da_a16;
                    x86_dynarec_opcodes_da_a32 = dynarec_ops_sf_fpu_686_da_a32;
                    x86_dynarec_opcodes_db_a16 = dynarec_ops_sf_fpu_686_db_a16;
                    x86_dynarec_opcodes_db_a32 = dynarec_ops_sf_fpu_686_db_a32;
                    x86_dynarec_opcodes_df_a16 = dynarec_ops_sf_fpu_686_df_a16;
                    x86_dynarec_opcodes_df_a32 = dynarec_ops_sf_fpu_686_df_a32;
                } else {
                    x86_dynarec_opcodes_da_a16 = dynarec_ops_fpu_686_da_a16;
                    x86_dynarec_opcodes_da_a32 = dynarec_ops_fpu_686_da_a32;
                    x86_dynarec_opcodes_db_a16 = dynarec_ops_fpu_686_db_a16;
                    x86_dynarec_opcodes_db_a32 = dynarec_ops_fpu_686_db_a32;
                    x86_dynarec_opcodes_df_a16 = dynarec_ops_fpu_686_df_a16;
                    x86_dynarec_opcodes_df_a32 = dynarec_ops_fpu_686_df_a32;
                }
#    endif /* USE_DYNAREC */
                if (fpu_softfloat) {
                    x86_opcodes_da_a16 = ops_sf_fpu_686_da_a16;
                    x86_opcodes_da_a32 = ops_sf_fpu_686_da_a32;
                    x86_opcodes_db_a16 = ops_sf_fpu_686_db_a16;
                    x86_opcodes_db_a32 = ops_sf_fpu_686_db_a32;
                    x86_opcodes_df_a16 = ops_sf_fpu_686_df_a16;
                    x86_opcodes_df_a32 = ops_sf_fpu_686_df_a32;
                } else {
                    x86_opcodes_da_a16 = ops_fpu_686_da_a16;
                    x86_opcodes_da_a32 = ops_fpu_686_da_a32;
                    x86_opcodes_db_a16 = ops_fpu_686_db_a16;
                    x86_opcodes_db_a32 = ops_fpu_686_db_a32;
                    x86_opcodes_df_a16 = ops_fpu_686_df_a16;
                    x86_opcodes_df_a32 = ops_fpu_686_df_a32;
                }
            }

#    ifdef USE_DYNAREC
            if (cpu_s->cpu_type == CPU_Cx6x86MX)
                x86_setopcodes(ops_386, ops_c6x86mx_0f, dynarec_ops_386, dynarec_ops_c6x86mx_0f);
            else if (cpu_s->cpu_type == CPU_Cx6x86L)
                x86_setopcodes(ops_386, ops_pentium_0f, dynarec_ops_386, dynarec_ops_pentium_0f);
            else
                x86_setopcodes(ops_386, ops_c6x86mx_0f, dynarec_ops_386, dynarec_ops_c6x86mx_0f);
#        if 0
                x86_setopcodes(ops_386, ops_c6x86_0f, dynarec_ops_386, dynarec_ops_c6x86_0f);
#        endif
#    else
            if (cpu_s->cpu_type == CPU_Cx6x86MX)
                x86_setopcodes(ops_386, ops_c6x86mx_0f);
            else if (cpu_s->cpu_type == CPU_Cx6x86L)
                x86_setopcodes(ops_386, ops_pentium_0f);
            else
                x86_setopcodes(ops_386, ops_c6x86mx_0f);
#        if 0
                x86_setopcodes(ops_386, ops_c6x86_0f);
#        endif
#    endif /* USE_DYNAREC */

            timing_rr  = 1; /* register dest - register src */
            timing_rm  = 1; /* register dest - memory src */
            timing_mr  = 2; /* memory dest   - register src */
            timing_mm  = 2;
            timing_rml = 1; /* register dest - memory src long */
            timing_mrl = 2; /* memory dest   - register src long */
            timing_mml = 2;
            if (cpu_s->cpu_type == CPU_CxGX1) {
                timing_bt  = 4; /* branch taken */
                timing_bnt = 1; /* branch not taken */
            } else {
                timing_bt  = 0; /* branch taken */
                timing_bnt = 2; /* branch not taken */
            }

            /* Make the CxGX1 share the timings with most other Cyrix C6x86's due to the real
               ones still being unknown. */
            timing_int_rm             = 9;
            timing_int_v86            = 46;
            timing_int_pm             = 21;
            timing_int_pm_outer       = 32;
            timing_iret_rm            = 7;
            timing_iret_v86           = 26;
            timing_iret_pm            = 10;
            timing_iret_pm_outer      = 26;
            timing_call_rm            = 3;
            timing_call_pm            = 4;
            timing_call_pm_gate       = 15;
            timing_call_pm_gate_inner = 26;
            timing_retf_rm            = 4;
            timing_retf_pm            = 4;
            timing_retf_pm_outer      = 23;
            timing_jmp_rm             = 1;
            timing_jmp_pm             = 4;
            timing_jmp_pm_gate        = 14;

            timing_misaligned = 2;

            cpu_cyrix_alignment = 1;

            cpu_features = CPU_FEATURE_RDTSC;
            if (cpu_s->cpu_type >= CPU_CxGX1)
                cpu_features |= CPU_FEATURE_MSR | CPU_FEATURE_CR4;
            if (cpu_s->cpu_type == CPU_Cx6x86MX)
                cpu_features |= CPU_FEATURE_MMX;
            if (cpu_s->cpu_type >= CPU_CxGX1)
                cpu_CR4_mask = CR4_TSD | CR4_DE | CR4_PCE;

#    ifdef USE_DYNAREC
            codegen_timing_set(&codegen_timing_686);
#    endif /* USE_DYNAREC */

            if ((cpu_s->cpu_type == CPU_Cx6x86L) || (cpu_s->cpu_type == CPU_Cx6x86MX))
                ccr4 = 0x80;
            else if (CPU_Cx6x86)
                CPUID = 0; /* Disabled on powerup by default */
            break;
#endif /* USE_CYRIX_6X86 */

#ifdef USE_AMD_K5
        case CPU_K5:
        case CPU_5K86:
#ifdef USE_DYNAREC
            x86_setopcodes(ops_386, ops_pentiummmx_0f, dynarec_ops_386, dynarec_ops_pentiummmx_0f);
#else
            x86_setopcodes(ops_386, ops_pentiummmx_0f);
#endif /* USE_DYNAREC */

            timing_rr  = 1; /* register dest - register src */
            timing_rm  = 2; /* register dest - memory src */
            timing_mr  = 3; /* memory dest   - register src */
            timing_mm  = 3;
            timing_rml = 2; /* register dest - memory src long */
            timing_mrl = 3; /* memory dest   - register src long */
            timing_mml = 3;
            timing_bt  = 0; /* branch taken */
            timing_bnt = 1; /* branch not taken */

            timing_int                = 6;
            timing_int_rm             = 11;
            timing_int_v86            = 54;
            timing_int_pm             = 25;
            timing_int_pm_outer       = 42;
            timing_iret_rm            = 7;
            timing_iret_v86           = 27; /* unknown */
            timing_iret_pm            = 10;
            timing_iret_pm_outer      = 27;
            timing_call_rm            = 4;
            timing_call_pm            = 4;
            timing_call_pm_gate       = 22;
            timing_call_pm_gate_inner = 44;
            timing_retf_rm            = 4;
            timing_retf_pm            = 4;
            timing_retf_pm_outer      = 23;
            timing_jmp_rm             = 3;
            timing_jmp_pm             = 3;
            timing_jmp_pm_gate        = 18;

            timing_misaligned = 3;

            cpu_features = CPU_FEATURE_RDTSC | CPU_FEATURE_MSR | CPU_FEATURE_CR4 | CPU_FEATURE_VME | CPU_FEATURE_MMX;
            cpu_CR4_mask = CR4_TSD | CR4_DE | CR4_MCE | CR4_PGE;

#ifdef USE_DYNAREC
            codegen_timing_set(&codegen_timing_k5);
#endif /* USE_DYNAREC */
            break;

#endif /* USE_AMD_K5 */
        case CPU_K6:
        case CPU_K6_2:
        case CPU_K6_2C:
        case CPU_K6_3:
        case CPU_K6_2P:
        case CPU_K6_3P:
#ifdef USE_DYNAREC
            if (cpu_s->cpu_type >= CPU_K6_2)
                x86_setopcodes(ops_386, ops_k62_0f, dynarec_ops_386, dynarec_ops_k62_0f);
            else
                x86_setopcodes(ops_386, ops_k6_0f, dynarec_ops_386, dynarec_ops_k6_0f);
#else
            if (cpu_s->cpu_type >= CPU_K6_2)
                x86_setopcodes(ops_386, ops_k62_0f);
            else
                x86_setopcodes(ops_386, ops_k6_0f);
#endif /* USE_DYNAREC */

            if ((cpu_s->cpu_type == CPU_K6_2P) || (cpu_s->cpu_type == CPU_K6_3P)) {
                x86_opcodes_3DNOW = ops_3DNOWE;
#ifdef USE_DYNAREC
                x86_dynarec_opcodes_3DNOW = dynarec_ops_3DNOWE;
#endif /* USE_DYNAREC */
            }

            timing_rr  = 1; /* register dest - register src */
            timing_rm  = 2; /* register dest - memory src */
            timing_mr  = 3; /* memory dest   - register src */
            timing_mm  = 3;
            timing_rml = 2; /* register dest - memory src long */
            timing_mrl = 3; /* memory dest   - register src long */
            timing_mml = 3;
            timing_bt  = 0; /* branch taken */
            timing_bnt = 1; /* branch not taken */

            timing_int                = 6;
            timing_int_rm             = 11;
            timing_int_v86            = 54;
            timing_int_pm             = 25;
            timing_int_pm_outer       = 42;
            timing_iret_rm            = 7;
            timing_iret_v86           = 27; /* unknown */
            timing_iret_pm            = 10;
            timing_iret_pm_outer      = 27;
            timing_call_rm            = 4;
            timing_call_pm            = 4;
            timing_call_pm_gate       = 22;
            timing_call_pm_gate_inner = 44;
            timing_retf_rm            = 4;
            timing_retf_pm            = 4;
            timing_retf_pm_outer      = 23;
            timing_jmp_rm             = 3;
            timing_jmp_pm             = 3;
            timing_jmp_pm_gate        = 18;

            timing_misaligned = 3;

            cpu_features = CPU_FEATURE_RDTSC | CPU_FEATURE_MSR | CPU_FEATURE_CR4 | CPU_FEATURE_VME | CPU_FEATURE_MMX;
            if (cpu_s->cpu_type >= CPU_K6_2)
                cpu_features |= CPU_FEATURE_3DNOW;
            if ((cpu_s->cpu_type == CPU_K6_2P) || (cpu_s->cpu_type == CPU_K6_3P))
                cpu_features |= CPU_FEATURE_3DNOWE;
            cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | CR4_PSE | CR4_MCE;
            if (cpu_s->cpu_type == CPU_K6)
                cpu_CR4_mask |= CR4_PCE;
            else if (cpu_s->cpu_type >= CPU_K6_2C)
                cpu_CR4_mask |= CR4_PGE;

#ifdef USE_DYNAREC
            codegen_timing_set(&codegen_timing_k6);
#endif /* USE_DYNAREC */
            break;

        case CPU_PENTIUMPRO:
        case CPU_PENTIUM2:
        case CPU_PENTIUM2D:
#ifdef USE_DYNAREC
            /* TODO: Perhaps merge the three opcode tables with some instructions UD#'ing depending on
                     CPU type. */
            if (cpu_s->cpu_type == CPU_PENTIUM2D)
                x86_setopcodes(ops_386, ops_pentium2d_0f, dynarec_ops_386, dynarec_ops_pentium2d_0f);
            else if (cpu_s->cpu_type == CPU_PENTIUM2)
                x86_setopcodes(ops_386, ops_pentium2_0f, dynarec_ops_386, dynarec_ops_pentium2_0f);
            else
                x86_setopcodes(ops_386, ops_pentiumpro_0f, dynarec_ops_386, dynarec_ops_pentiumpro_0f);
            if (fpu_softfloat) {
                x86_dynarec_opcodes_da_a16 = dynarec_ops_sf_fpu_686_da_a16;
                x86_dynarec_opcodes_da_a32 = dynarec_ops_sf_fpu_686_da_a32;
                x86_dynarec_opcodes_db_a16 = dynarec_ops_sf_fpu_686_db_a16;
                x86_dynarec_opcodes_db_a32 = dynarec_ops_sf_fpu_686_db_a32;
                x86_dynarec_opcodes_df_a16 = dynarec_ops_sf_fpu_686_df_a16;
                x86_dynarec_opcodes_df_a32 = dynarec_ops_sf_fpu_686_df_a32;
            } else {
                x86_dynarec_opcodes_da_a16 = dynarec_ops_fpu_686_da_a16;
                x86_dynarec_opcodes_da_a32 = dynarec_ops_fpu_686_da_a32;
                x86_dynarec_opcodes_db_a16 = dynarec_ops_fpu_686_db_a16;
                x86_dynarec_opcodes_db_a32 = dynarec_ops_fpu_686_db_a32;
                x86_dynarec_opcodes_df_a16 = dynarec_ops_fpu_686_df_a16;
                x86_dynarec_opcodes_df_a32 = dynarec_ops_fpu_686_df_a32;
            }
#else
            if (cpu_s->cpu_type == CPU_PENTIUM2D)
                x86_setopcodes(ops_386, ops_pentium2d_0f);
            else
                x86_setopcodes(ops_386, ops_pentium2_0f);
#endif /* USE_DYNAREC */
            if (fpu_softfloat) {
                x86_opcodes_da_a16 = ops_sf_fpu_686_da_a16;
                x86_opcodes_da_a32 = ops_sf_fpu_686_da_a32;
                x86_opcodes_db_a16 = ops_sf_fpu_686_db_a16;
                x86_opcodes_db_a32 = ops_sf_fpu_686_db_a32;
                x86_opcodes_df_a16 = ops_sf_fpu_686_df_a16;
                x86_opcodes_df_a32 = ops_sf_fpu_686_df_a32;
            } else {
                x86_opcodes_da_a16 = ops_fpu_686_da_a16;
                x86_opcodes_da_a32 = ops_fpu_686_da_a32;
                x86_opcodes_db_a16 = ops_fpu_686_db_a16;
                x86_opcodes_db_a32 = ops_fpu_686_db_a32;
                x86_opcodes_df_a16 = ops_fpu_686_df_a16;
                x86_opcodes_df_a32 = ops_fpu_686_df_a32;
            }

            timing_rr  = 1; /* register dest - register src */
            timing_rm  = 2; /* register dest - memory src */
            timing_mr  = 3; /* memory dest   - register src */
            timing_mm  = 3;
            timing_rml = 2; /* register dest - memory src long */
            timing_mrl = 3; /* memory dest   - register src long */
            timing_mml = 3;
            timing_bt  = 0; /* branch taken */
            timing_bnt = 1; /* branch not taken */

            timing_int                = 6;
            timing_int_rm             = 11;
            timing_int_v86            = 54;
            timing_int_pm             = 25;
            timing_int_pm_outer       = 42;
            timing_iret_rm            = 7;
            timing_iret_v86           = 27; /* unknown */
            timing_iret_pm            = 10;
            timing_iret_pm_outer      = 27;
            timing_call_rm            = 4;
            timing_call_pm            = 4;
            timing_call_pm_gate       = 22;
            timing_call_pm_gate_inner = 44;
            timing_retf_rm            = 4;
            timing_retf_pm            = 4;
            timing_retf_pm_outer      = 23;
            timing_jmp_rm             = 3;
            timing_jmp_pm             = 3;
            timing_jmp_pm_gate        = 18;

            timing_misaligned = 3;

            cpu_features = CPU_FEATURE_RDTSC | CPU_FEATURE_MSR | CPU_FEATURE_CR4 | CPU_FEATURE_VME;
            if (cpu_s->cpu_type >= CPU_PENTIUM2)
                cpu_features |= CPU_FEATURE_MMX;
            cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | CR4_PSE | CR4_MCE | CR4_PAE | CR4_PCE | CR4_PGE;
            if (cpu_s->cpu_type == CPU_PENTIUM2D)
                cpu_CR4_mask |= CR4_OSFXSR;

#ifdef USE_DYNAREC
            codegen_timing_set(&codegen_timing_p6);
#endif /* USE_DYNAREC */
            break;

        case CPU_CYRIX3S:
#ifdef USE_DYNAREC
            x86_setopcodes(ops_386, ops_winchip2_0f, dynarec_ops_386, dynarec_ops_winchip2_0f);
#else
            x86_setopcodes(ops_386, ops_winchip2_0f);
#endif /* USE_DYNAREC */
            timing_rr  = 1; /* register dest - register src */
            timing_rm  = 2; /* register dest - memory src */
            timing_mr  = 2; /* memory dest   - register src */
            timing_mm  = 3;
            timing_rml = 2; /* register dest - memory src long */
            timing_mrl = 2; /* memory dest   - register src long */
            timing_mml = 3;
            timing_bt  = 2; /* branch taken */
            timing_bnt = 1; /* branch not taken */

            timing_int_rm             = 26; /* unknown */
            timing_int_v86            = 82;
            timing_int_pm             = 44;
            timing_int_pm_outer       = 71;
            timing_iret_rm            = 7;
            timing_iret_v86           = 26;
            timing_iret_pm            = 10;
            timing_iret_pm_outer      = 26;
            timing_call_rm            = 4;
            timing_call_pm            = 15;
            timing_call_pm_gate       = 26;
            timing_call_pm_gate_inner = 35;
            timing_retf_rm            = 4;
            timing_retf_pm            = 7;
            timing_retf_pm_outer      = 23;
            timing_jmp_rm             = 5;
            timing_jmp_pm             = 7;
            timing_jmp_pm_gate        = 17;

            timing_misaligned = 2;

            cpu_features = CPU_FEATURE_RDTSC | CPU_FEATURE_MMX | CPU_FEATURE_MSR | CPU_FEATURE_CR4 | CPU_FEATURE_3DNOW;
            msr.fcr      = (1 << 7) | (1 << 8) | (1 << 9) | (1 << 12) | (1 << 16) | (1 << 18) | (1 << 19) | (1 << 20) | (1 << 21);
            cpu_CR4_mask = CR4_TSD | CR4_DE | CR4_MCE | CR4_PCE | CR4_PGE;

            cpu_cyrix_alignment = 1;

#ifdef USE_DYNAREC
            codegen_timing_set(&codegen_timing_winchip);
#endif /* USE_DYNAREC */
            break;

        default:
            fatal("cpu_set : unknown CPU type %" PRIu64 "\n", cpu_s->cpu_type);
    }

    switch (fpu_type) {
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

        case FPU_487SX:
        default:
            x87_timings     = x87_timings_486;
            x87_concurrency = x87_concurrency_486;
    }

    cpu_use_exec = 0;

    if (is386) {
#if defined(USE_DYNAREC) && !defined(USE_GDBSTUB)
        if (cpu_use_dynarec) {
            cpu_exec = exec386_dynarec;
            cpu_use_exec = 1;
        } else
#endif /* defined(USE_DYNAREC) && !defined(USE_GDBSTUB) */
            /* Use exec386 for CPU_IBM486SLC because it can reach 100 MHz. */
            if ((cpu_s->cpu_type == CPU_IBM486SLC) || (cpu_s->cpu_type == CPU_IBM486BL) ||
                cpu_iscyrix || (cpu_s->cpu_type > CPU_486DLC) || cpu_override_interpreter) {
                cpu_exec = exec386;
                cpu_use_exec = 1;
            } else
                cpu_exec = exec386_2386;
    } else if (cpu_s->cpu_type >= CPU_286)
        cpu_exec = exec386_2386;
    else
        cpu_exec = execx86;
    mmx_init();
    gdbstub_cpu_init();
}

void
cpu_close(void)
{
    cpu_inited = 0;
}

void
cpu_set_isa_speed(int speed)
{
    if (speed) {
        cpu_isa_speed = speed;
    } else if (cpu_busspeed >= 8000000)
        cpu_isa_speed = 8000000;
    else
        cpu_isa_speed = cpu_busspeed;

    pc_speed_changed();

    cpu_log("cpu_set_isa_speed(%d) = %d\n", speed, cpu_isa_speed);
}

void
cpu_set_pci_speed(int speed)
{
    if (speed)
        cpu_pci_speed = speed;
    else if (cpu_busspeed < 42500000)
        cpu_pci_speed = cpu_busspeed;
    else if (cpu_busspeed < 84000000)
        cpu_pci_speed = cpu_busspeed / 2;
    else if (cpu_busspeed < 120000000)
        cpu_pci_speed = cpu_busspeed / 3;
    else
        cpu_pci_speed = cpu_busspeed / 4;

    if (cpu_isa_pci_div)
        cpu_set_isa_pci_div(cpu_isa_pci_div);
    else if (speed)
        pc_speed_changed();

    pci_burst_time    = cpu_s->rspeed / cpu_pci_speed;
    pci_nonburst_time = 4 * pci_burst_time;

    cpu_log("cpu_set_pci_speed(%d) = %d\n", speed, cpu_pci_speed);
}

void
cpu_set_isa_pci_div(int div)
{
    cpu_isa_pci_div = div;

    cpu_log("cpu_set_isa_pci_div(%d)\n", cpu_isa_pci_div);

    if (cpu_isa_pci_div)
        cpu_set_isa_speed(cpu_pci_speed / cpu_isa_pci_div);
    else
        cpu_set_isa_speed(0);
}

void
cpu_set_agp_speed(int speed)
{
    if (speed) {
        cpu_agp_speed = speed;
        pc_speed_changed();
    } else if (cpu_busspeed < 84000000)
        cpu_agp_speed = cpu_busspeed;
    else if (cpu_busspeed < 120000000)
        cpu_agp_speed = cpu_busspeed / 1.5;
    else
        cpu_agp_speed = cpu_busspeed / 2;

    agp_burst_time    = cpu_s->rspeed / cpu_agp_speed;
    agp_nonburst_time = 4 * agp_burst_time;

    cpu_log("cpu_set_agp_speed(%d) = %d\n", speed, cpu_agp_speed);
}

char *
cpu_current_pc(char *bufp)
{
    static char buff[10];

    if (bufp == NULL)
        bufp = buff;

    sprintf(bufp, "%04X:%04X", CS, cpu_state.pc);

    return bufp;
}

void
cpu_CPUID(void)
{
    switch (cpu_s->cpu_type) {
        case CPU_i486SX_SLENH:
            if (!EAX) {
                EAX = 0x00000001;
                EBX = 0x756e6547; /* GenuineIntel */
                EDX = 0x49656e69;
                ECX = 0x6c65746e;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_VME;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

        case CPU_i486DX_SLENH:
            if (!EAX) {
                EAX = 0x00000001;
                EBX = 0x756e6547; /* GenuineIntel */
                EDX = 0x49656e69;
                ECX = 0x6c65746e;
            } else if (EAX == 1) {
                if ((CPUID == 0x0436) && (cr0 & (1 << 29)))
                    EAX = 0x0470;
                else
                    EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_VME;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

        case CPU_ENH_Am486DX:
            if (!EAX) {
                EAX = 0x00000001;
                EBX = 0x68747541;/* AuthenticAMD */
                ECX = 0x444D4163;
                EDX = 0x69746E65;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

        case CPU_WINCHIP:
            if (!EAX) {
                EAX = 0x00000001;
                if (msr.fcr2 & (1 << 14)) {
                    EBX = msr.fcr3 >> 32;
                    ECX = msr.fcr3 & 0xffffffff;
                    EDX = msr.fcr2 >> 32;
                } else {
                    EBX = 0x746e6543; /* CentaurHauls */
                    ECX = 0x736c7561;
                    EDX = 0x48727561;
                }
            } else if (EAX == 1) {
                EAX = ((msr.fcr2 & 0x0ff0) ? ((msr.fcr2 & 0x0ff0) | (CPUID & 0xf00f)) : CPUID);
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_DE | CPUID_TSC | CPUID_MSR;
                if (cpu_has_feature(CPU_FEATURE_CX8))
                    EDX |= CPUID_CMPXCHG8B;
                if (msr.fcr & (1 << 9))
                    EDX |= CPUID_MMX;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

        case CPU_WINCHIP2:
            switch (EAX) {
                case 0:
                    EAX = 0x00000001;
                    if (msr.fcr2 & (1 << 14)) {
                        EBX = msr.fcr3 >> 32;
                        ECX = msr.fcr3 & 0xffffffff;
                        EDX = msr.fcr2 >> 32;
                    } else {
                        EBX = 0x746e6543; /* CentaurHauls */
                        ECX = 0x736c7561;
                        EDX = 0x48727561;
                    }
                    break;
                case 1:
                    EAX = ((msr.fcr2 & 0x0ff0) ? ((msr.fcr2 & 0x0ff0) | (CPUID & 0xf00f)) : CPUID);
                    EBX = ECX = 0;
                    EDX       = CPUID_FPU | CPUID_DE | CPUID_TSC | CPUID_MSR;
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
                    EDX = CPUID_FPU | CPUID_DE | CPUID_TSC | CPUID_MSR;
                    if (cpu_has_feature(CPU_FEATURE_CX8))
                        EDX |= CPUID_CMPXCHG8B;
                    if (msr.fcr & (1 << 9))
                        EDX |= CPUID_MMX;
                    if (cpu_has_feature(CPU_FEATURE_3DNOW))
                        EDX |= CPUID_3DNOW;
                    break;

                case 0x80000002:      /* Processor name string */
                    EAX = 0x20544449; /* IDT WinChip 2-3D */
                    EBX = 0x436e6957;
                    ECX = 0x20706968;
                    EDX = 0x44332d32;
                    break;

                case 0x80000005:      /*Cache information*/
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
            if (!EAX) {
                EAX = 0x00000001;
                EBX = 0x756e6547; /* GenuineIntel */
                EDX = 0x49656e69;
                ECX = 0x6c65746e;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_VME | CPUID_DE | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B;
                if (cpu_s->cpu_type != CPU_P24T)
                    EDX |= CPUID_MCE;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

#ifdef USE_AMD_K5
        case CPU_K5:
            if (!EAX) {
                EAX = 0x00000001;
                EBX = 0x68747541; /* AuthenticAMD */
                EDX = 0x69746E65;
                ECX = 0x444D4163;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_DE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_AMDPGE;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

        case CPU_5K86:
            switch (EAX) {
                case 0:
                    EAX = 0x00000001;
                    EBX = 0x68747541; /* AuthenticAMD */
                    EDX = 0x69746E65;
                    ECX = 0x444D4163;
                    break;
                case 1:
                    EAX = CPUID;
                    EBX = ECX = 0;
                    EDX       = CPUID_FPU | CPUID_DE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_PGE;
                    break;
                case 0x80000000:
                    EAX = 0x80000005;
                    EBX = ECX = EDX = 0;
                    break;
                case 0x80000001:
                    EAX = CPUID;
                    EBX = ECX = 0;
                    EDX       = CPUID_FPU | CPUID_DE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_PGE;
                    break;
                case 0x80000002:      /* Processor name string */
                    EAX = 0x2D444D41; /* AMD-K5(tm) Proce */
                    EBX = 0x7428354B;
                    ECX = 0x5020296D;
                    EDX = 0x65636F72;
                    break;
                case 0x80000003:      /* Processor name string */
                    EAX = 0x726F7373; /* ssor */
                    EBX = ECX = EDX = 0;
                    break;
                case 0x80000005:      /* Cache information */
                    EAX = 0;
                    EBX = 0x04800000; /* TLBs */
                    ECX = 0x08040120; /* L1 data cache */
                    EDX = 0x10040120; /* L1 instruction cache */
                    break;
                default:
                    EAX = EBX = ECX = EDX = 0;
                    break;
            }
            break;
#endif /* USE_AMD_K5 */

        case CPU_K6:
            switch (EAX) {
                case 0:
                    EAX = 0x00000001;
                    EBX = 0x68747541; /* AuthenticAMD */
                    EDX = 0x69746E65;
                    ECX = 0x444D4163;
                    break;
                case 1:
                    EAX = CPUID;
                    EBX = ECX = 0;
                    EDX       = CPUID_FPU | CPUID_VME | CPUID_DE | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_MMX;
                    break;
                case 0x80000000:
                    EAX = 0x80000005;
                    EBX = ECX = EDX = 0;
                    break;
                case 0x80000001:
                    EAX = CPUID + 0x100;
                    EBX = ECX = 0;
                    EDX       = CPUID_FPU | CPUID_VME | CPUID_DE | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_AMDSEP | CPUID_MMX;
                    break;
                case 0x80000002:      /* Processor name string */
                    EAX = 0x2D444D41; /* AMD-K6tm w/ mult */
                    EBX = 0x6D74364B;
                    ECX = 0x202F7720;
                    EDX = 0x746C756D;
                    break;
                case 0x80000003:      /* Processor name string */
                    EAX = 0x64656D69; /* imedia extension */
                    EBX = 0x65206169;
                    ECX = 0x6E657478;
                    EDX = 0x6E6F6973;
                    break;
                case 0x80000004:      /* Processor name string */
                    EAX = 0x73;       /* s */
                    EBX = ECX = EDX = 0;
                    break;
                case 0x80000005:      /* Cache information */
                    EAX = 0;
                    EBX = 0x02800140; /* TLBs */
                    ECX = 0x20020220; /* L1 data cache */
                    EDX = 0x20020220; /* L1 instruction cache */
                    break;
                case 0x8FFFFFFF:      /* Easter egg */
                    EAX = 0x4778654E; /* NexGenerationAMD */
                    EBX = 0x72656E65;
                    ECX = 0x6F697461;
                    EDX = 0x444D416E;
                    break;
                default:
                    EAX = EBX = ECX = EDX = 0;
                    break;
            }
            break;

        case CPU_K6_2:
        case CPU_K6_2C:
            switch (EAX) {
                case 0:
                    EAX = 0x00000001;
                    EBX = 0x68747541; /* AuthenticAMD */
                    ECX = 0x444d4163;
                    EDX = 0x69746e65;
                    break;
                case 1:
                    EAX = CPUID;
                    EBX = ECX = 0;
                    EDX       = CPUID_FPU | CPUID_VME | CPUID_DE | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_MMX;
                    if (cpu_s->cpu_type == CPU_K6_2C)
                        EDX |= CPUID_PGE;
                    break;
                case 0x80000000:
                    EAX = 0x80000005;
                    EBX = ECX = EDX = 0;
                    break;
                case 0x80000001:
                    EAX = CPUID + 0x100;
                    EBX = ECX = 0;
                    EDX       = CPUID_FPU | CPUID_VME | CPUID_DE | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_SEP | CPUID_MMX | CPUID_3DNOW;
                    if (cpu_s->cpu_type == CPU_K6_2C)
                        EDX |= CPUID_PGE;
                    break;
                case 0x80000002:      /* Processor name string */
                    EAX = 0x2d444d41; /* AMD-K6(tm) 3D pr */
                    EBX = 0x7428364b;
                    ECX = 0x3320296d;
                    EDX = 0x72702044;
                    break;
                case 0x80000003:      /* Processor name string */
                    EAX = 0x7365636f; /* ocessor */
                    EBX = 0x00726f73;
                    ECX = 0x00000000;
                    EDX = 0x00000000;
                    break;
                case 0x80000005: /* Cache information */
                    EAX = 0;
                    EBX = 0x02800140; /* TLBs */
                    ECX = 0x20020220; /* L1 data cache */
                    EDX = 0x20020220; /* L1 instruction cache */
                    break;
                default:
                    EAX = EBX = ECX = EDX = 0;
                    break;
            }
            break;

        case CPU_K6_3:
            switch (EAX) {
                case 0:
                    EAX = 0x00000001;
                    EBX = 0x68747541; /* AuthenticAMD */
                    ECX = 0x444d4163;
                    EDX = 0x69746e65;
                    break;
                case 1:
                    EAX = CPUID;
                    EBX = ECX = 0;
                    EDX       = CPUID_FPU | CPUID_VME | CPUID_DE | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_PGE | CPUID_MMX;
                    break;
                case 0x80000000:
                    EAX = 0x80000006;
                    EBX = ECX = EDX = 0;
                    break;
                case 0x80000001:
                    EAX = CPUID + 0x100;
                    EBX = ECX = 0;
                    EDX       = CPUID_FPU | CPUID_VME | CPUID_DE | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_SEP | CPUID_PGE | CPUID_MMX | CPUID_3DNOW;
                    break;
                case 0x80000002:      /* Processor name string */
                    EAX = 0x2d444d41; /* AMD-K6(tm) 3D+ P */
                    EBX = 0x7428364b;
                    ECX = 0x3320296d;
                    EDX = 0x50202b44;
                    break;
                case 0x80000003:      /* Processor name string */
                    EAX = 0x65636f72; /* rocessor */
                    EBX = 0x726f7373;
                    ECX = 0x00000000;
                    EDX = 0x00000000;
                    break;
                case 0x80000005: /* Cache information */
                    EAX = 0;
                    EBX = 0x02800140; /* TLBs */
                    ECX = 0x20020220; /* L1 data cache */
                    EDX = 0x20020220; /* L1 instruction cache */
                    break;
                case 0x80000006: /* L2 Cache information */
                    EAX = EBX = EDX = 0;
                    ECX             = 0x01004220;
                    break;
                default:
                    EAX = EBX = ECX = EDX = 0;
                    break;
            }
            break;

        case CPU_K6_2P:
        case CPU_K6_3P:
            switch (EAX) {
                case 0:
                    EAX = 0x00000001;
                    EBX = 0x68747541; /* AuthenticAMD */
                    ECX = 0x444d4163;
                    EDX = 0x69746e65;
                    break;
                case 1:
                    EAX = CPUID;
                    EBX = ECX = 0;
                    EDX       = CPUID_FPU | CPUID_VME | CPUID_DE | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_PGE | CPUID_MMX;
                    break;
                case 0x80000000:
                    EAX = 0x80000007;
                    EBX = ECX = EDX = 0;
                    break;
                case 0x80000001:
                    EAX = CPUID + 0x100;
                    EBX = ECX = 0;
                    EDX       = CPUID_FPU | CPUID_VME | CPUID_DE | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_SEP | CPUID_MMX | CPUID_PGE | CPUID_3DNOW | CPUID_3DNOWE;
                    break;
                case 0x80000002:      /* Processor name string */
                    EAX = 0x2d444d41; /* AMD-K6(tm)-III P */
                    EBX = 0x7428364b;
                    ECX = 0x492d296d;
                    EDX = 0x50204949;
                    break;
                case 0x80000003:      /* Processor name string */
                    EAX = 0x65636f72; /* rocessor */
                    EBX = 0x726f7373;
                    ECX = 0x00000000;
                    EDX = 0x00000000;
                    break;
                case 0x80000005: /* Cache information */
                    EAX = 0;
                    EBX = 0x02800140; /* TLBs */
                    ECX = 0x20020220; /* L1 data cache */
                    EDX = 0x20020220; /* L1 instruction cache */
                    break;
                case 0x80000006: /* L2 Cache information */
                    EAX = EBX = EDX = 0;
                    if (cpu_s->cpu_type == CPU_K6_3P)
                        ECX = 0x01004220;
                    else
                        ECX = 0x00804220;
                    break;
                case 0x80000007: /* PowerNow information */
                    EAX = EBX = ECX = 0;
                    EDX             = 7;
                    break;
                default:
                    EAX = EBX = ECX = EDX = 0;
                    break;
            }
            break;

        case CPU_PENTIUMMMX:
            if (!EAX) {
                EAX = 0x00000001;
                EBX = 0x756e6547; /* GenuineIntel */
                EDX = 0x49656e69;
                ECX = 0x6c65746e;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_VME | CPUID_DE | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_MMX;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

#ifdef USE_CYRIX_6X86
        case CPU_Cx6x86:
            if (!EAX) {
                EAX = 0x00000001;
                EBX = 0x69727943; /* CyrixInstead */
                EDX = 0x736e4978;
                ECX = 0x64616574;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

        case CPU_Cx6x86L:
            if (!EAX) {
                EAX = 0x00000001;
                EBX = 0x69727943; /* CyrixInstead */
                EDX = 0x736e4978;
                ECX = 0x64616574;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_CMPXCHG8B;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

        case CPU_CxGX1:
            if (!EAX) {
                EAX = 0x00000001;
                EBX = 0x69727943; /* CyrixInstead */
                EDX = 0x736e4978;
                ECX = 0x64616574;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_DE | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

        case CPU_Cx6x86MX:
            if (!EAX) {
                EAX = 0x00000001;
                EBX = 0x69727943; /* CyrixInstead */
                EDX = 0x736e4978;
                ECX = 0x64616574;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_DE | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B | CPUID_CMOV | CPUID_MMX;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;
#endif /* USE_CYRIX_6X86 */

        case CPU_PENTIUMPRO:
            if (!EAX) {
                EAX = 0x00000002;
                EBX = 0x756e6547; /* GenuineIntel */
                EDX = 0x49656e69;
                ECX = 0x6c65746e;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_VME | CPUID_DE | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_PAE | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_SEP | CPUID_CMOV;
            } else if (EAX == 2) {
                EAX = 0x03020101; /* Instruction TLB: 4 KB pages, 4-way set associative, 32 entries
                                     Instruction TLB: 4 MB pages, fully associative, 2 entries
                                     Data TLB: 4 KB pages, 4-way set associative, 64 entries */
                EBX = ECX = 0;
                EDX       = 0x06040a42; /* 2nd-level cache: 256 KB, 4-way set associative, 32-byte line size
                                           1st-level data cache: 8 KB, 2-way set associative, 32-byte line size
                                           Data TLB: 4 MB pages, 4-way set associative, 8 entries
                                           1st-level instruction cache: 8 KB, 4-way set associative, 32-byte line size */
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

        case CPU_PENTIUM2:
            if (!EAX) {
                EAX = 0x00000002;
                EBX = 0x756e6547; /* GenuineIntel */
                EDX = 0x49656e69;
                ECX = 0x6c65746e;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_VME | CPUID_DE | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_PAE | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_MMX | CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_SEP | CPUID_CMOV;
            } else if (EAX == 2) {
                EAX = 0x03020101; /* Instruction TLB: 4 KB pages, 4-way set associative, 32 entries
                                     Instruction TLB: 4 MB pages, fully associative, 2 entries
                                     Data TLB: 4 KB pages, 4-way set associative, 64 entries */
                EBX = ECX = 0;
                EDX       = 0x0c040843; /* 2nd-level cache: 512 KB, 4-way set associative, 32-byte line size
                                           1st-level data cache: 16 KB, 4-way set associative, 32-byte line size
                                           Data TLB: 4 MB pages, 4-way set associative, 8 entries
                                           1st-level instruction cache: 16 KB, 4-way set associative, 32-byte line size */
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

        case CPU_PENTIUM2D:
            if (!EAX) {
                EAX = 0x00000002;
                EBX = 0x756e6547; /* GenuineIntel */
                EDX = 0x49656e69;
                ECX = 0x6c65746e;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_VME | CPUID_DE | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_PAE | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_MMX | CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_SEP | CPUID_FXSR | CPUID_CMOV;
            } else if (EAX == 2) {
                EAX = 0x03020101; /* Instruction TLB: 4 KB pages, 4-way set associative, 32 entries
                                     Instruction TLB: 4 MB pages, fully associative, 2 entries
                                     Data TLB: 4 KB pages, 4-way set associative, 64 entries */
                EBX = ECX = 0;
                if (cpu_f->package == CPU_PKG_SLOT2) /* Pentium II Xeon Drake */
                    EDX = 0x0c040844; /* 2nd-level cache: 1 MB, 4-way set associative, 32-byte line size
                                         1st-level data cache: 16 KB, 4-way set associative, 32-byte line size
                                         Data TLB: 4 MB pages, 4-way set associative, 8 entries
                                         1st-level instruction cache: 16 KB, 4-way set associative, 32-byte line size */
                else if (!strncmp(cpu_f->internal_name, "celeron", 7)) { /* Celeron */
                    if (CPUID >= 0x660) /* Mendocino */
                        EDX = 0x0c040841; /* 2nd-level cache: 128 KB, 4-way set associative, 32-byte line size */
                    else /* Covington */
                        EDX = 0x0c040840; /* No 2nd-level cache */
                } else /* Pentium II Deschutes and OverDrive */
                    EDX = 0x0c040843; /* 2nd-level cache: 512 KB, 4-way set associative, 32-byte line size */
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

        case CPU_CYRIX3S:
            switch (EAX) {
                case 0:
                    EAX = 0x00000001;
                    if (msr.fcr2 & (1 << 14)) {
                        EBX = msr.fcr3 >> 32;
                        ECX = msr.fcr3 & 0xffffffff;
                        EDX = msr.fcr2 >> 32;
                    } else {
                        EBX = 0x746e6543; /* CentaurHauls */
                        ECX = 0x736c7561;
                        EDX = 0x48727561;
                    }
                    break;
                case 1:
                    EAX = ((msr.fcr2 & 0x0ff0) ? ((msr.fcr2 & 0x0ff0) | (CPUID & 0xf00f)) : CPUID);
                    EBX = ECX = 0;
                    EDX       = CPUID_FPU | CPUID_DE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_MMX | CPUID_MTRR;
                    if (cpu_has_feature(CPU_FEATURE_CX8))
                        EDX |= CPUID_CMPXCHG8B;
                    if (msr.fcr & (1 << 7))
                        EDX |= CPUID_PGE;
                    break;
                case 0x80000000:
                    EAX = 0x80000005;
                    break;
                case 0x80000001:
                    EAX = CPUID;
                    EDX = CPUID_FPU | CPUID_DE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_MMX | CPUID_MTRR | CPUID_3DNOW;
                    if (cpu_has_feature(CPU_FEATURE_CX8))
                        EDX |= CPUID_CMPXCHG8B;
                    if (msr.fcr & (1 << 7))
                        EDX |= CPUID_PGE;
                    break;
                case 0x80000002:      /* Processor name string */
                    EAX = 0x20414956; /* VIA Samuel */
                    EBX = 0x756d6153;
                    ECX = 0x00006c65;
                    EDX = 0x00000000;
                    break;
                case 0x80000005:      /* Cache information */
                    EBX = 0x08800880; /* TLBs */
                    ECX = 0x40040120; /* L1 data cache */
                    EDX = 0x40020120; /* L1 instruction cache */
                    break;
                default:
                    EAX = EBX = ECX = EDX = 0;
                    break;
            }
            break;
    }
}

void
cpu_ven_reset(void)
{
    memset(&msr, 0, sizeof(msr));

    switch (cpu_s->cpu_type) {
        case CPU_WINCHIP:
        case CPU_WINCHIP2:
            msr.fcr      = (1 << 8) | (1 << 9) | (1 << 12) | (1 << 16) | (1 << 19) | (1 << 21);
            msr.mcr_ctrl = 0xf8000000;
            if (cpu_s->cpu_type == CPU_WINCHIP2) {
                msr.fcr      |= (1 << 18) | (1 << 20);
                msr.mcr_ctrl |= (1 << 17);
            }
            break;

        case CPU_K6_2P:
        case CPU_K6_3P:
        case CPU_K6_3:
        case CPU_K6_2C:
            msr.amd_psor = (cpu_s->cpu_type >= CPU_K6_3) ? 0x008cULL : 0x018cULL;
            fallthrough;
        case CPU_K6_2:
#ifdef USE_AMD_K5
        case CPU_K5:
        case CPU_5K86:
#endif /* USE_AMD_K5 */
        case CPU_K6:
            msr.amd_efer = (cpu_s->cpu_type >= CPU_K6_2C) ? 2ULL : 0ULL;
            break;

        case CPU_PENTIUMPRO:
        case CPU_PENTIUM2:
        case CPU_PENTIUM2D:
            msr.mtrr_cap = 0x00000508ULL;
            break;

        case CPU_CYRIX3S:
            msr.fcr = (1 << 7) | (1 << 8) | (1 << 9) | (1 << 12) | (1 << 16) | (1 << 18) | (1 << 19) |
                      (1 << 20) | (1 << 21);
            break;
    }
}

void
cpu_RDMSR(void)
{
    if (CPL)
        x86gpf(NULL, 0);
    else  switch (cpu_s->cpu_type) {
        case CPU_IBM386SLC:
        case CPU_IBM486SLC:
        case CPU_IBM486BL:
            EAX = EDX = 0;
            switch (ECX) {
                /* Processor Operation Register */
                case 0x1000:
                    EAX = msr.ibm_por & ((cpu_s->cpu_type > CPU_IBM386SLC) ? 0xffeff : 0xfeff);
                    break;

                /* Cache Region Control Register */
                case 0x1001:
                    EAX = msr.ibm_crcr & 0xffffffff;
                    EDX = (msr.ibm_crcr >> 32) & 0x0000ffff;
                    break;

                /* Processor Operation Register */
                case 0x1002:
                    if ((cpu_s->cpu_type > CPU_IBM386SLC) && cpu_s->multi)
                        EAX = msr.ibm_por2 & 0x3f000000;
                    break;

                /* Processor Control Register */
                case 0x1004:
                    if (cpu_s->cpu_type > CPU_IBM486SLC)
                        EAX = msr.ibm_pcr & 0x00d6001a;
                    break;
            }
            break;

        case CPU_WINCHIP:
        case CPU_WINCHIP2:
            EAX = EDX = 0;
            switch (ECX) {
                /* Pentium Processor Parity Reversal Register */
                case 0x02:
                    EAX = msr.tr1;
                    break;
                /* Pentium Processor New Feature Control */
                case 0x0e:
                    EAX = msr.tr12;
                    break;
                /* Time Stamp Counter */
                case 0x10:
                    EAX = tsc & 0xffffffff;
                    EDX = tsc >> 32;
                    break;
                /* Performance Monitor - Control and Event Select */
                case 0x11:
                    EAX = msr.cesr;
                    break;
                /* Performance Monitor - Event Counter 0 */
                case 0x12:
                    EAX = msr.pmc[0] & 0xffffffff;
                    EDX = msr.pmc[0] >> 32;
                    break;
                /* Performance Monitor - Event Counter 1 */
                case 0x13:
                    EAX = msr.pmc[1] & 0xffffffff;
                    EDX = msr.pmc[1] >> 32;
                    break;
                /* Feature Control Register */
                case 0x107:
                    EAX = msr.fcr;
                    break;
                /* Feature Control Register 2 */
                case 0x108:
                    EAX = msr.fcr2 & 0xffffffff;
                    EDX = msr.fcr2 >> 32;
                    break;
                /* Feature Control Register 4 */
                case 0x10a:
                    EAX = cpu_multi & 3;
                    break;
                /* Memory Configuration Register Control */
                case 0x120:
                    EAX = msr.mcr_ctrl;
                    break;
                /* Unknown */
                case 0x131:
                case 0x142 ... 0x145:
                case 0x147:
                case 0x150:
                case 0x151:
                    break;
            }
            break;

        case CPU_CYRIX3S:
            EAX = EDX = 0;
            switch (ECX) {
                /* Machine Check Exception Address */
                case 0x00:
                /* Machine Check Exception Type */
                case 0x01:
                    break;
                /* Time Stamp Counter */
                case 0x10:
                    EAX = tsc & 0xffffffff;
                    EDX = tsc >> 32;
                    break;
                /* EBL_CR_POWERON - Processor Hard Power-On Configuration */
                case 0x2a:
                    EAX = 0xc4000000;
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
                /* PERFCTR0 - Performance Counter Register 0 - aliased to TSC */
                case 0xc1:
                    EAX = tsc & 0xffffffff;
                    EDX = (tsc >> 32) & 0xff;
                    break;
                /* PERFCTR1 - Performance Counter Register 1 */
                case 0xc2:
                    EAX = msr.perfctr[1] & 0xffffffff;
                    EDX = msr.perfctr[1] >> 32;
                    break;
                /* BBL_CR_CTL3 - L2 Cache Control Register 3 */
                case 0x11e:
                    EAX = 0x800000; /* L2 cache disabled */
                    break;
                /* EVNTSEL0 - Performance Counter Event Select 0 - hardcoded */
                case 0x186:
                    EAX = 0x470079;
                    break;
                /* EVNTSEL1 - Performance Counter Event Select 1 */
                case 0x187:
                    EAX = msr.evntsel[1] & 0xffffffff;
                    EDX = msr.evntsel[1] >> 32;
                    break;
                /* Feature Control Register */
                case 0x1107:
                    EAX = msr.fcr;
                    break;
                /* Feature Control Register 2 */
                case 0x1108:
                    EAX = msr.fcr2 & 0xffffffff;
                    EDX = msr.fcr2 >> 32;
                    break;
                /* ECX & 0: MTRRphysBase0 ... MTRRphysBase7
                   ECX & 1: MTRRphysMask0 ... MTRRphysMask7 */
                case 0x200 ... 0x20f:
                    if (ECX & 1) {
                        EAX = msr.mtrr_physmask[(ECX - 0x200) >> 1] & 0xffffffff;
                        EDX = msr.mtrr_physmask[(ECX - 0x200) >> 1] >> 32;
                    } else {
                        EAX = msr.mtrr_physbase[(ECX - 0x200) >> 1] & 0xffffffff;
                        EDX = msr.mtrr_physbase[(ECX - 0x200) >> 1] >> 32;
                    }
                    break;
                /* MTRRfix64K_00000 */
                case 0x250:
                    EAX = msr.mtrr_fix64k_8000 & 0xffffffff;
                    EDX = msr.mtrr_fix64k_8000 >> 32;
                    break;
                /* MTRRfix16K_80000 */
                case 0x258:
                    EAX = msr.mtrr_fix16k_8000 & 0xffffffff;
                    EDX = msr.mtrr_fix16k_8000 >> 32;
                    break;
                /* MTRRfix16K_A0000 */
                case 0x259:
                    EAX = msr.mtrr_fix16k_a000 & 0xffffffff;
                    EDX = msr.mtrr_fix16k_a000 >> 32;
                    break;
                /* MTRRfix4K_C0000 ... MTRRfix4K_F8000 */
                case 0x268 ... 0x26f:
                    EAX = msr.mtrr_fix4k[ECX - 0x268] & 0xffffffff;
                    EDX = msr.mtrr_fix4k[ECX - 0x268] >> 32;
                    break;
                /* MTRRdefType */
                case 0x2ff:
                    EAX = msr.mtrr_deftype & 0xffffffff;
                    EDX = msr.mtrr_deftype >> 32;
                    break;
            }
            break;

#ifdef USE_AMD_K5
        case CPU_K5:
        case CPU_5K86:
#endif /* USE_AMD_K5 */
        case CPU_K6:
        case CPU_K6_2:
        case CPU_K6_2C:
        case CPU_K6_3:
        case CPU_K6_2P:
        case CPU_K6_3P:
            EAX = 0;
            /* EDX is left unchanged when reading this MSR! */
            if (ECX != 0x82)
                EDX = 0;
            switch (ECX) {
                /* Machine Check Address Register */
                case 0x00000000:
                    EAX = msr.mcar & 0xffffffff;
                    EDX = msr.mcar >> 32;
                    break;
                /* Machine Check Type Register */
                case 0x00000001:
                    EAX = msr.mctr & 0xffffffff;
                    EDX = msr.mctr >> 32;
                    break;
                /* Test Register 12 */
                case 0x0000000e:
                    EAX = msr.tr12;
                    break;
                /* Time Stamp Counter */
                case 0x00000010:
                    EAX = tsc & 0xffffffff;
                    EDX = tsc >> 32;
                    break;
                /* Array Access Register */
                case 0x00000082:
                    if (cpu_s->cpu_type > CPU_5K86)
                        goto amd_k_invalid_rdmsr;
                    EAX = msr.amd_aar & 0xffffffff;
                    /* EDX is left unchanged! */
                    break;
                /* Hardware Configuration Register */
                case 0x00000083:
                    EAX = msr.amd_hwcr & 0xffffffff;
                    EDX = msr.amd_hwcr >> 32;
                    break;
                /* Write Allocate Top-of-Memory and Control Register */
                case 0x00000085:
                    if (cpu_s->cpu_type != CPU_5K86)
                        goto amd_k_invalid_rdmsr;
                    EAX = msr.amd_watmcr & 0xffffffff;
                    EDX = msr.amd_watmcr >> 32;
                    break;
                /* Write Allocate Programmable Memory Range Register */
                case 0x00000086:
                    if (cpu_s->cpu_type != CPU_5K86)
                        goto amd_k_invalid_rdmsr;
                    EAX = msr.amd_wapmrr & 0xffffffff;
                    EDX = msr.amd_wapmrr >> 32;
                    break;
                /* Extended Feature Enable Register */
                case 0xc0000080:
                    EAX = msr.amd_efer & 0xffffffff;
                    EDX = msr.amd_efer >> 32;
                    break;
                /* SYSCALL Target Address Register */
                case 0xc0000081:
                    if (cpu_s->cpu_type < CPU_K6_2)
                        goto amd_k_invalid_rdmsr;

                    EAX = msr.amd_star & 0xffffffff;
                    EDX = msr.amd_star >> 32;
                    break;
                /* Write-Handling Control Register */
                case 0xc0000082:
                    EAX = msr.amd_whcr & 0xffffffff;
                    EDX = msr.amd_whcr >> 32;
                    break;
                /* UC/WC Cacheability Control Register */
                case 0xc0000085:
                    if (cpu_s->cpu_type < CPU_K6_2C)
                        goto amd_k_invalid_rdmsr;

                    EAX = msr.amd_uwccr & 0xffffffff;
                    EDX = msr.amd_uwccr >> 32;
                    break;
                /* Enhanced Power Management Register */
                case 0xc0000086:
                    if (cpu_s->cpu_type < CPU_K6_2P)
                        goto amd_k_invalid_rdmsr;

                    EAX = msr.amd_epmr & 0xffffffff;
                    EDX = msr.amd_epmr >> 32;
                    break;
                /* Processor State Observability Register */
                case 0xc0000087:
                    if (cpu_s->cpu_type < CPU_K6_2C)
                        goto amd_k_invalid_rdmsr;

                    EAX = msr.amd_psor & 0xffffffff;
                    EDX = msr.amd_psor >> 32;
                    break;
                /* Page Flush/Invalidate Register */
                case 0xc0000088:
                    if (cpu_s->cpu_type < CPU_K6_2C)
                        goto amd_k_invalid_rdmsr;

                    EAX = msr.amd_pfir & 0xffffffff;
                    EDX = msr.amd_pfir >> 32;
                    break;
                /* Level-2 Cache Array Access Register */
                case 0xc0000089:
                    if (cpu_s->cpu_type < CPU_K6_3)
                        goto amd_k_invalid_rdmsr;

                    EAX = msr.amd_l2aar & 0xffffffff;
                    EDX = msr.amd_l2aar >> 32;
                    break;
                default:
amd_k_invalid_rdmsr:
                    x86gpf(NULL, 0);
                    break;
            }
            break;

        case CPU_P24T:
        case CPU_PENTIUM:
        case CPU_PENTIUMMMX:
            EAX = EDX = 0;
            /* Filter out the upper 27 bits when ECX value is over 0x80000000, as per:
               Ralf Brown, Pentium Model-Specific Registers and What They Reveal.
               https://www.cs.cmu.edu/~ralf/papers/highmsr.html
               But leave the bit 31 intact to be able to handle both low and high
               MSRs in a single switch block. */
            switch (ECX & (ECX > 0x7fffffff ? 0x8000001f : 0x7fffffff)) {
                /* Machine Check Exception Address */
                case 0x00000000:
                case 0x80000000:
                    EAX = msr.mcar & 0xffffffff;
                    EDX = msr.mcar >> 32;
                    break;
                /* Machine Check Exception Type */
                case 0x00000001:
                case 0x80000001:
                    EAX = msr.mctr & 0xffffffff;
                    EDX = msr.mctr >> 32;
                    msr.mctr &= ~0x1; /* clear the machine check pending bit */
                    break;
                /* TR1 - Parity Reversal Test Register */
                case 0x00000002:
                case 0x80000002:
                    EAX = msr.tr1;
                    break;
                /* TR2 - Instruction Cache End Bit */
                case 0x00000004:
                case 0x80000004:
                    if (cpu_s->cpu_type == CPU_PENTIUMMMX)
                        goto pentium_invalid_rdmsr;
                    EAX = msr.tr2;
                    break;
                /* TR3 - Cache Test Data */
                case 0x00000005:
                case 0x80000005:
                    EAX = msr.tr3;
                    break;
                /* TR4 - Cache Test Tag */
                case 0x00000006:
                case 0x80000006:
                    EAX = msr.tr4;
                    break;
                /* TR5 - Cache Test Control */
                case 0x00000007:
                case 0x80000007:
                    EAX = msr.tr5;
                    break;
                /* TR6 - TLB Test Command */
                case 0x00000008:
                case 0x80000008:
                    EAX = msr.tr6;
                    break;
                /* TR7 - TLB Test Data */
                case 0x00000009:
                case 0x80000009:
                    EAX = msr.tr7;
                    break;
                /* TR9 - Branch Target Buffer Tag */
                case 0x0000000b:
                case 0x8000000b:
                    EAX = msr.tr9;
                    break;
                /* TR10 - Branch Target Buffer Target */
                case 0x0000000c:
                case 0x8000000c:
                    EAX = msr.tr10;
                    break;
                /* TR11 - Branch Target Buffer Control */
                case 0x0000000d:
                case 0x8000000d:
                    EAX = msr.tr11;
                    break;
                /* TR12 - New Feature Control */
                case 0x0000000e:
                case 0x8000000e:
                    EAX = msr.tr12;
                    break;
                /* Time Stamp Counter */
                case 0x00000010:
                case 0x80000010:
                    EAX = tsc & 0xffffffff;
                    EDX = tsc >> 32;
                    break;
                /* Performance Monitor - Control and Event Select */
                case 0x00000011:
                case 0x80000011:
                    EAX = msr.cesr;
                    break;
                /* Performance Monitor - Event Counter 0 */
                case 0x00000012:
                case 0x80000012:
                    EAX = msr.pmc[0] & 0xffffffff;
                    EDX = msr.pmc[0] >> 32;
                    break;
                /* Performance Monitor - Event Counter 1 */
                case 0x00000013:
                case 0x80000013:
                    EAX = msr.pmc[1] & 0xffffffff;
                    EDX = msr.pmc[1] >> 32;
                    break;
                /* Unknown */
                case 0x00000014:
                case 0x80000014:
                    if ((CPUID & 0xfff) <= 0x520)
                        goto pentium_invalid_rdmsr;
                    break;
                /* Unknown, possibly paging-related; initial value is 0004h,
                   becomes 0008h once paging is enabled */
                case 0x80000018:
                        EAX = ((cr0 & (1 << 31)) ? 0x00000008 : 0x00000004);
                    break;
                /* Floating point - last prefetched opcode
                   bits 10-8: low three bits of first byte of FP instruction
                   bits 7-0: second byte of floating-point instruction */
                case 0x80000019:
                    EAX = 0;
                    break;
                /* Floating point - last executed non-control opcode */
                case 0x8000001a:
                    EAX = 0;
                    break;
                /* Floating point - last non-control exception opcode - part
                   of FSTENV/FSAVE'd environment */
                case 0x8000001b:
                    EAX = msr.fp_last_xcpt;
                    break;
                /* Unknown */
                case 0x8000001c:
                    EAX = 0x00000004;
                    break;
                /* Probe Mode Control */
                case 0x8000001d:
                    EAX = msr.probe_ctl;
                    break;
                /* Unknown, possibly scratchpad register */
                case 0x8000001e:
                    EAX = msr.ecx8000001e;
                    break;
                /* Unknown, possibly scratchpad register */
                case 0x8000001f:
                    EAX = msr.ecx8000001f;
                    break;
                /* Reserved/Unimplemented */
                case 0x80000003:
                case 0x8000000a:
                case 0x8000000f:
                case 0x80000015 ... 0x80000017:
                    EAX = (ECX & 0x1f) * 2;
                    break;
                default:
pentium_invalid_rdmsr:
                    cpu_log("RDMSR: Invalid MSR: %08X\n", ECX);
                    x86gpf(NULL, 0);
                    break;
            }
            cpu_log("RDMSR: ECX = %08X, val = %08X%08X\n", ECX, EDX, EAX);
            break;

#ifdef USE_CYRIX_6X86
        case CPU_Cx6x86:
        case CPU_Cx6x86L:
        case CPU_CxGX1:
        case CPU_Cx6x86MX:
            switch (ECX) {
                /* Test Data */
                case 0x03:
                    EAX = msr.tr3;
                    break;
                /* Test Address */
                case 0x04:
                    EAX = msr.tr4;
                    break;
                /* Test Command/Status */
                case 0x05:
                    EAX = msr.tr5;
                    break;
                /* Time Stamp Counter */
                case 0x10:
                    EAX = tsc & 0xffffffff;
                    EDX = tsc >> 32;
                    break;
                /* Performance Monitor - Control and Event Select */
                case 0x11:
                    EAX = msr.cesr;
                    break;
                /* Performance Monitor - Event Counter 0 */
                case 0x12:
                    EAX = msr.pmc[0] & 0xffffffff;
                    EDX = msr.pmc[0] >> 32;
                    break;
                /* Performance Monitor - Event Counter 1 */
                case 0x13:
                    EAX = msr.pmc[1] & 0xffffffff;
                    EDX = msr.pmc[1] >> 32;
                    break;
            }
            cpu_log("RDMSR: ECX = %08X, val = %08X%08X\n", ECX, EDX, EAX);
            break;
#endif /* USE_CYRIX_6X86 */

        case CPU_PENTIUMPRO:
        case CPU_PENTIUM2:
        case CPU_PENTIUM2D:
            EAX = EDX = 0;
            /* Per RichardG's probing of a real Deschutes using my RDMSR tool,
               we have discovered that the top 18 bits are filtered out. */
            switch (ECX & 0x00003fff) {
                /* Machine Check Exception Address */
                case 0x00:
                /* Machine Check Exception Type */
                case 0x01:
                    break;
                /* Time Stamp Counter */
                case 0x10:
                    EAX = tsc & 0xffffffff;
                    EDX = tsc >> 32;
                    break;
                /* IA32_PLATFORM_ID - Platform ID */
                case 0x17:
                    if (cpu_s->cpu_type < CPU_PENTIUM2D)
                        goto i686_invalid_rdmsr;

                    if (cpu_f->package == CPU_PKG_SLOT2)
                        EDX |= (1 << 19);
                    else if (cpu_f->package == CPU_PKG_SOCKET370)
                        EDX |= (1 << 20);
                    break;
                /* Unknown */
                case 0x18:
                    break;
                /* IA32_APIC_BASE - APIC Base Address */
                case 0x1B:
                    EAX = msr.apic_base & 0xffffffff;
                    EDX = msr.apic_base >> 32;
                    cpu_log("APIC_BASE read : %08X%08X\n", EDX, EAX);
                    break;
                /* Unknown (undocumented?) MSR used by the Hyper-V BIOS */
                case 0x20:
                    EAX = msr.ecx20 & 0xffffffff;
                    EDX = msr.ecx20 >> 32;
                    break;
                /* Unknown */
                case 0x21:
                    if (cpu_s->cpu_type == CPU_PENTIUMPRO)
                        goto i686_invalid_rdmsr;
                    break;
                /* EBL_CR_POWERON - Processor Hard Power-On Configuration */
                case 0x2a:
                    EAX = 0xc4000000;
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
                    if (cpu_s->cpu_type != CPU_PENTIUMPRO) {
                        if (cpu_busspeed >= 84000000)
                            EAX |= (1 << 19);
                    }
                    break;
                /* Unknown */
                case 0x32:
                    if (cpu_s->cpu_type == CPU_PENTIUMPRO)
                        goto i686_invalid_rdmsr;
                    break;
                /* TEST_CTL - Test Control Register */
                case 0x33:
                    EAX = msr.test_ctl;
                    break;
                /* Unknown */
                case 0x34:
                case 0x3a:
                case 0x3b:
                case 0x50 ... 0x54:
                    break;
                /* BIOS_UPDT_TRIG - BIOS Update Trigger */
                case 0x79:
                    EAX = msr.bios_updt & 0xffffffff;
                    EDX = msr.bios_updt >> 32;
                    break;
                /* BBL_CR_D0 ... BBL_CR_D3 - Chunk 0..3 Data Register
                   8Bh: BIOS_SIGN - BIOS Update Signature */
                case 0x88 ... 0x8b:
                    EAX = msr.bbl_cr_dx[ECX - 0x88] & 0xffffffff;
                    EDX = msr.bbl_cr_dx[ECX - 0x88] >> 32;
                    break;
                /* Unknown */
                case 0xae:
                    break;
                /* PERFCTR0 - Performance Counter Register 0 */
                case 0xc1:
                /* PERFCTR1 - Performance Counter Register 1 */
                case 0xc2:
                    EAX = msr.perfctr[ECX - 0xC1] & 0xffffffff;
                    EDX = msr.perfctr[ECX - 0xC1] >> 32;
                    break;
                /* MTRRcap */
                case 0xfe:
                    EAX = msr.mtrr_cap & 0xffffffff;
                    EDX = msr.mtrr_cap >> 32;
                    break;
                /* BBL_CR_ADDR - L2 Cache Address Register */
                case 0x116:
                    EAX = msr.bbl_cr_addr & 0xffffffff;
                    EDX = msr.bbl_cr_addr >> 32;
                    break;
                /* BBL_CR_DECC - L2 Cache Date ECC Register */
                case 0x118:
                    EAX = msr.bbl_cr_decc & 0xffffffff;
                    EDX = msr.bbl_cr_decc >> 32;
                    break;
                /* BBL_CR_CTL - L2 Cache Control Register */
                case 0x119:
                    EAX = msr.bbl_cr_ctl & 0xffffffff;
                    EDX = msr.bbl_cr_ctl >> 32;
                    break;
                /* BBL_CR_TRIG - L2 Cache Trigger Register */
                case 0x11a:
                    EAX = msr.bbl_cr_trig & 0xffffffff;
                    EDX = msr.bbl_cr_trig >> 32;
                    break;
                /* BBL_CR_BUSY - L2 Cache Busy Register */
                case 0x11b:
                    EAX = msr.bbl_cr_busy & 0xffffffff;
                    EDX = msr.bbl_cr_busy >> 32;
                    break;
                /* BBL_CR_CTL3 - L2 Cache Control Register 3 */
                case 0x11e:
                    EAX = msr.bbl_cr_ctl3 & 0xffffffff;
                    EDX = msr.bbl_cr_ctl3 >> 32;
                    break;
                /* Unknown */
                case 0x131:
                case 0x14e ... 0x151:
                case 0x154:
                case 0x15b:
                case 0x15f:
                    break;
                /* SYSENTER_CS - SYSENTER target CS */
                case 0x174:
                    EAX &= 0xffff0000;
                    EAX |= msr.sysenter_cs;
                    EDX = 0x00000000;
                    break;
                /* SYSENTER_ESP - SYSENTER target ESP */
                case 0x175:
                    EAX = msr.sysenter_esp;
                    EDX = 0x00000000;
                    break;
                /* SYSENTER_EIP - SYSENTER target EIP */
                case 0x176:
                    EAX = msr.sysenter_eip;
                    EDX = 0x00000000;
                    break;
                /* MCG_CAP - Machine Check Global Capability */
                case 0x179:
                    EAX = 0x00000105;
                    EDX = 0x00000000;
                    break;
                /* MCG_STATUS - Machine Check Global Status */
                case 0x17a:
                    break;
                /* MCG_CTL - Machine Check Global Control */
                case 0x17b:
                    EAX = msr.mcg_ctl & 0xffffffff;
                    EDX = msr.mcg_ctl >> 32;
                    break;
                /* EVNTSEL0 - Performance Counter Event Select 0 */
                case 0x186:
                /* EVNTSEL1 - Performance Counter Event Select 1 */
                case 0x187:
                    EAX = msr.evntsel[ECX - 0x186] & 0xffffffff;
                    EDX = msr.evntsel[ECX - 0x186] >> 32;
                    break;
                /* Unknown */
                case 0x1d3:
                    break;
                /* DEBUGCTLMSR - Debugging Control Register */
                case 0x1d9:
                    EAX = msr.debug_ctl;
                    break;
                /* LASTBRANCHFROMIP - address from which a branch was last taken */
                case 0x1db:
                /* LASTBRANCHTOIP - destination address of the last taken branch instruction */
                case 0x1dc:
                /* LASTINTFROMIP - address at which an interrupt last occurred */
                case 0x1dd:
                /* LASTINTTOIP - address to which the last interrupt caused a branch */
                case 0x1de:
                    break;
                /* ROB_CR_BKUPTMPDR6 */
                case 0x1e0:
                    EAX = msr.rob_cr_bkuptmpdr6;
                    break;
                /* ECX & 0: MTRRphysBase0 ... MTRRphysBase7
                   ECX & 1: MTRRphysMask0 ... MTRRphysMask7 */
                case 0x200 ... 0x20f:
                    if (ECX & 1) {
                        EAX = msr.mtrr_physmask[(ECX - 0x200) >> 1] & 0xffffffff;
                        EDX = msr.mtrr_physmask[(ECX - 0x200) >> 1] >> 32;
                    } else {
                        EAX = msr.mtrr_physbase[(ECX - 0x200) >> 1] & 0xffffffff;
                        EDX = msr.mtrr_physbase[(ECX - 0x200) >> 1] >> 32;
                    }
                    break;
                /* MTRRfix64K_00000 */
                case 0x250:
                    EAX = msr.mtrr_fix64k_8000 & 0xffffffff;
                    EDX = msr.mtrr_fix64k_8000 >> 32;
                    break;
                /* MTRRfix16K_80000 */
                case 0x258:
                    EAX = msr.mtrr_fix16k_8000 & 0xffffffff;
                    EDX = msr.mtrr_fix16k_8000 >> 32;
                    break;
                /* MTRRfix16K_A0000 */
                case 0x259:
                    EAX = msr.mtrr_fix16k_a000 & 0xffffffff;
                    EDX = msr.mtrr_fix16k_a000 >> 32;
                    break;
                /* MTRRfix4K_C0000 ... MTRRfix4K_F8000 */
                case 0x268 ... 0x26f:
                    EAX = msr.mtrr_fix4k[ECX - 0x268] & 0xffffffff;
                    EDX = msr.mtrr_fix4k[ECX - 0x268] >> 32;
                    break;
                /* Page Attribute Table */
                case 0x277:
                    if (cpu_s->cpu_type < CPU_PENTIUM2D)
                        goto i686_invalid_rdmsr;
                    EAX = msr.pat & 0xffffffff;
                    EDX = msr.pat >> 32;
                    break;
                /* Unknown */
                case 0x280:
                    if (cpu_s->cpu_type == CPU_PENTIUMPRO)
                        goto i686_invalid_rdmsr;
                    break;
                /* MTRRdefType */
                case 0x2ff:
                    EAX = msr.mtrr_deftype & 0xffffffff;
                    EDX = msr.mtrr_deftype >> 32;
                    break;
                /* MC0_CTL - Machine Check 0 Control */
                case 0x400:
                /* MC1_CTL - Machine Check 1 Control */
                case 0x404:
                /* MC2_CTL - Machine Check 2 Control */
                case 0x408:
                /* MC4_CTL - Machine Check 4 Control */
                case 0x40c:
                /* MC3_CTL - Machine Check 3 Control */
                case 0x410:
                    EAX = msr.mca_ctl[(ECX - 0x400) >> 2] & 0xffffffff;
                    EDX = msr.mca_ctl[(ECX - 0x400) >> 2] >> 32;
                    break;
                /* MC0_STATUS - Machine Check 0 Status */
                case 0x401:
                /* MC0_ADDR - Machine Check 0 Address */
                case 0x402:
                /* MC1_STATUS - Machine Check 1 Status */
                case 0x405:
                /* MC1_ADDR - Machine Check 1 Address */
                case 0x406:
                /* MC2_STATUS - Machine Check 2 Status */
                case 0x409:
                /* MC2_ADDR - Machine Check 2 Address */
                case 0x40a:
                /* MC4_STATUS - Machine Check 4 Status */
                case 0x40d:
                /* MC4_ADDR - Machine Check 4 Address */
                case 0x40e:
                /* MC3_STATUS - Machine Check 3 Status */
                case 0x411:
                /* MC3_ADDR - Machine Check 3 Address */
                case 0x412:
                    break;
                /* Unknown */
                case 0x570:
                    EAX = msr.ecx570 & 0xffffffff;
                    EDX = msr.ecx570 >> 32;
                    break;
                /* Unknown, possibly debug registers? */
                case 0x1000 ... 0x1007:
                /* Unknown, possibly control registers? */
                case 0x2000:
                case 0x2002 ... 0x2004:
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

void
cpu_WRMSR(void)
{
    uint64_t temp;

    cpu_log("WRMSR %08X %08X%08X\n", ECX, EDX, EAX);

    if (CPL)
        x86gpf(NULL, 0);
    else  switch (cpu_s->cpu_type) {
        case CPU_IBM386SLC:
        case CPU_IBM486SLC:
        case CPU_IBM486BL:
            switch (ECX) {
                /* Processor Operation Register */
                case 0x1000:
                    msr.ibm_por           = EAX & ((cpu_s->cpu_type > CPU_IBM386SLC) ? 0xffeff : 0xfeff);
                    cpu_cache_int_enabled = (EAX & (1 << 7));
                    break;
                /* Cache Region Control Register */
                case 0x1001:
                    msr.ibm_crcr = EAX | ((uint64_t) (EDX & 0x0000ffff) << 32);
                    break;
                /* Processor Operation Register */
                case 0x1002:
                    if ((cpu_s->cpu_type > CPU_IBM386SLC) && cpu_s->multi)
                        msr.ibm_por2 = EAX & 0x3f000000;
                    break;
                /* Processor Control Register */
                case 0x1004:
                    if (cpu_s->cpu_type > CPU_IBM486SLC)
                        msr.ibm_pcr = EAX & 0x00d6001a;
                    break;
            }
            break;

        case CPU_WINCHIP:
        case CPU_WINCHIP2:
            switch (ECX) {
                /* Pentium Processor Parity Reversal Register */
                case 0x02:
                    msr.tr1 = EAX & 2;
                    break;
                /* Pentium Processor New Feature Control */
                case 0x0e:
                    msr.tr12 = EAX & 0x248;
                    break;
                /* Time Stamp Counter */
                case 0x10:
                    timer_set_new_tsc(EAX | ((uint64_t) EDX << 32));
                    break;
                /* Performance Monitor - Control and Event Select */
                case 0x11:
                    msr.cesr = EAX & 0xff00ff;
                    break;
                /* Performance Monitor - Event Counter 0 */
                case 0x12:
                    msr.pmc[0] = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Performance Monitor - Event Counter 1 */
                case 0x13:
                    msr.pmc[1] = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Feature Control Register */
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
                    if ((EAX & (1 << 20)) && cpu_s->cpu_type >= CPU_WINCHIP2)
                        cpu_features |= CPU_FEATURE_3DNOW;
                    else
                        cpu_features &= ~CPU_FEATURE_3DNOW;
                    if (EAX & (1 << 29))
                        CPUID = 0;
                    else
                        CPUID = cpu_s->cpuid_model;
                    break;
                /* Feature Control Register 2 */
                case 0x108:
                    msr.fcr2 = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Feature Control Register 3 */
                case 0x109:
                    msr.fcr3 = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Memory Configuration Register 0..7 */
                case 0x110 ... 0x117:
                    temp = ECX - 0x110;
                    if (cpu_s->cpu_type == CPU_WINCHIP2) {
                        if (EAX & 0x1f)
                            msr.mcr_ctrl |= (1 << (temp + 9));
                        else
                            msr.mcr_ctrl &= ~(1 << (temp + 9));
                    }
                    msr.mcr[temp] = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Memory Configuration Register Control */
                case 0x120:
                    msr.mcr_ctrl = EAX & ((cpu_s->cpu_type == CPU_WINCHIP2) ? 0x1df : 0x1f);
                    break;
                /* Unknown */
                case 0x131:
                case 0x142 ... 0x145:
                case 0x147:
                case 0x150:
                case 0x151:
                    break;
            }
            break;

        case CPU_CYRIX3S:
            switch (ECX) {
                /* Machine Check Exception Address */
                case 0x00:
                /* Machine Check Exception Type */
                case 0x01:
                    break;
                /* Time Stamp Counter */
                case 0x10:
                    timer_set_new_tsc(EAX | ((uint64_t) EDX << 32));
                    break;
                /* PERFCTR0 - Performance Counter Register 0 - aliased to TSC */
                case 0xc1:
                    break;
                /* PERFCTR0 - Performance Counter Register 1 */
                case 0xc2:
                    msr.perfctr[1] = EAX | ((uint64_t) EDX << 32);
                    break;
                /* BBL_CR_CTL3 - L2 Cache Control Register 3 */
                case 0x11e:
                /* EVNTSEL0 - Performance Counter Event Select 0 - hardcoded */
                case 0x186:
                    break;
                /* EVNTSEL1 - Performance Counter Event Select 1 */
                case 0x187:
                    msr.evntsel[1] = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Feature Control Register */
                case 0x1107:
                    msr.fcr = EAX;
                    if (EAX & (1 << 1))
                        cpu_features |= CPU_FEATURE_CX8;
                    else
                        cpu_features &= ~CPU_FEATURE_CX8;
                    if (EAX & (1 << 7))
                        cpu_CR4_mask |= CR4_PGE;
                    else
                        cpu_CR4_mask &= ~CR4_PGE;
                    break;
                /* Feature Control Register 2 */
                case 0x1108:
                    msr.fcr2 = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Feature Control Register 3 */
                case 0x1109:
                    msr.fcr3 = EAX | ((uint64_t) EDX << 32);
                    break;
                /* ECX & 0: MTRRphysBase0 ... MTRRphysBase7
                   ECX & 1: MTRRphysMask0 ... MTRRphysMask7 */
                case 0x200 ... 0x20f:
                    if (ECX & 1)
                        msr.mtrr_physmask[(ECX - 0x200) >> 1] = EAX | ((uint64_t) EDX << 32);
                    else
                        msr.mtrr_physbase[(ECX - 0x200) >> 1] = EAX | ((uint64_t) EDX << 32);
                    break;
                /* MTRRfix64K_00000 */
                case 0x250:
                    msr.mtrr_fix64k_8000 = EAX | ((uint64_t) EDX << 32);
                    break;
                /* MTRRfix16K_80000 */
                case 0x258:
                    msr.mtrr_fix16k_8000 = EAX | ((uint64_t) EDX << 32);
                    break;
                /* MTRRfix16K_A0000 */
                case 0x259:
                    msr.mtrr_fix16k_a000 = EAX | ((uint64_t) EDX << 32);
                    break;
                /* MTRRfix4K_C0000 ... MTRRfix4K_F8000 */
                case 0x268 ... 0x26f:
                    msr.mtrr_fix4k[ECX - 0x268] = EAX | ((uint64_t) EDX << 32);
                    break;
                /* MTRRdefType */
                case 0x2ff:
                    msr.mtrr_deftype = EAX | ((uint64_t) EDX << 32);
                    break;
            }
            break;

#ifdef USE_AMD_K5
        case CPU_K5:
        case CPU_5K86:
#endif /* USE_AMD_K5 */
        case CPU_K6:
        case CPU_K6_2:
        case CPU_K6_2C:
        case CPU_K6_3:
        case CPU_K6_2P:
        case CPU_K6_3P:
            switch (ECX) {
                /* Machine Check Address Register */
                case 0x00000000:
                    if (cpu_s->cpu_type > CPU_5K86)
                        msr.mcar = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Machine Check Type Register */
                case 0x00000001:
                    if (cpu_s->cpu_type > CPU_5K86)
                        msr.mctr = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Test Register 12 */
                case 0x0000000e:
                    msr.tr12 = EAX & 0x8;
                    break;
                /* Time Stamp Counter */
                case 0x00000010:
                    timer_set_new_tsc(EAX | ((uint64_t) EDX << 32));
                    break;
                /* Array Access Register */
                case 0x00000082:
                    if (cpu_s->cpu_type > CPU_5K86)
                        goto amd_k_invalid_wrmsr;
                    msr.amd_aar = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Hardware Configuration Register */
                case 0x00000083:
                    msr.amd_hwcr = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Write Allocate Top-of-Memory and Control Register */
                case 0x00000085:
                    if (cpu_s->cpu_type != CPU_5K86)
                        goto amd_k_invalid_wrmsr;
                    msr.amd_watmcr = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Write Allocate Programmable Memory Range Register */
                case 0x00000086:
                    if (cpu_s->cpu_type != CPU_5K86)
                        goto amd_k_invalid_wrmsr;
                    msr.amd_wapmrr = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Extended Feature Enable Register */
                case 0xc0000080:
                    temp = EAX | ((uint64_t) EDX << 32);
                    if (temp & ~0x1fULL)
                        x86gpf(NULL, 0);
                    else
                        msr.amd_efer = temp;
                    break;
                /* SYSCALL Target Address Register */
                case 0xc0000081:
                    if (cpu_s->cpu_type < CPU_K6_2)
                        goto amd_k_invalid_wrmsr;

                    msr.amd_star = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Write-Handling Control Register */
                case 0xc0000082:
                    msr.amd_whcr = EAX | ((uint64_t) EDX << 32);
                    break;
                /* UC/WC Cacheability Control Register */
                case 0xc0000085:
                    if (cpu_s->cpu_type < CPU_K6_2C)
                        goto amd_k_invalid_wrmsr;

                    msr.amd_uwccr = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Enhanced Power Management Register */
                case 0xc0000086:
                    if (cpu_s->cpu_type < CPU_K6_2P)
                        goto amd_k_invalid_wrmsr;

                    msr.amd_epmr = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Processor State Observability Register */
                case 0xc0000087:
                    if (cpu_s->cpu_type < CPU_K6_2C)
                        goto amd_k_invalid_wrmsr;

                    msr.amd_psor = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Page Flush/Invalidate Register */
                case 0xc0000088:
                    if (cpu_s->cpu_type < CPU_K6_2C)
                        goto amd_k_invalid_wrmsr;

                    msr.amd_pfir = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Level-2 Cache Array Access Register */
                case 0xc0000089:
                    if (cpu_s->cpu_type < CPU_K6_3)
                        goto amd_k_invalid_wrmsr;

                    msr.amd_l2aar = EAX | ((uint64_t) EDX << 32);
                    break;
                default:
amd_k_invalid_wrmsr:
                    x86gpf(NULL, 0);
                    break;
            }
            break;

        case CPU_P24T:
        case CPU_PENTIUM:
        case CPU_PENTIUMMMX:
            cpu_log("WRMSR: ECX = %08X, val = %08X%08X\n", ECX, EDX, EAX);
            /* Filter out the upper 27 bits when ECX value is over 0x80000000, as per:
               Ralf Brown, Pentium Model-Specific Registers and What They Reveal.
               https://www.cs.cmu.edu/~ralf/papers/highmsr.html
               But leave the bit 31 intact to be able to handle both low and high
               MSRs in a single switch block. */
            switch (ECX & (ECX > 0x7fffffff ? 0x8000001f : 0x7fffffff)) {
                /* Machine Check Exception Address */
                case 0x00000000:
                case 0x80000000:
                /* Machine Check Exception Type */
                case 0x00000001:
                case 0x80000001:
                    break;
                /* TR1 - Parity Reversal Test Register */
                case 0x00000002:
                case 0x80000002:
                    msr.tr1 = EAX & 0x3fff;
                    break;
                /* TR2 - Instruction Cache End Bit */
                case 0x00000004:
                case 0x80000004:
                    if (cpu_s->cpu_type == CPU_PENTIUMMMX)
                        goto pentium_invalid_wrmsr;
                    msr.tr2 = EAX & 0xf;
                    break;
                /* TR3 - Cache Test Data */
                case 0x00000005:
                case 0x80000005:
                    msr.tr3 = EAX;
                    break;
                /* TR4 - Cache Test Tag */
                case 0x00000006:
                case 0x80000006:
                    msr.tr4 = EAX & ((cpu_s->cpu_type == CPU_PENTIUMMMX) ? 0xffffff1f : 0xffffff07);
                    break;
                /* TR5 - Cache Test Control */
                case 0x00000007:
                case 0x80000007:
                    msr.tr5 = EAX & ((cpu_s->cpu_type == CPU_PENTIUMMMX) ? 0x87fff : 0x7fff);
                    break;
                /* TR6 - TLB Test Command */
                case 0x00000008:
                case 0x80000008:
                    msr.tr6 = EAX & 0xffffff07;
                    break;
                /* TR7 - TLB Test Data */
                case 0x00000009:
                case 0x80000009:
                    msr.tr7 = EAX & ((cpu_s->cpu_type == CPU_PENTIUMMMX) ? 0xfffffc7f : 0xffffff9c);
                    break;
                /* TR9 - Branch Target Buffer Tag */
                case 0x0000000b:
                case 0x8000000b:
                    msr.tr9 = EAX & ((cpu_s->cpu_type == CPU_PENTIUMMMX) ? 0xffffffff : 0xffffffc3);
                    break;
                /* TR10 - Branch Target Buffer Target */
                case 0x0000000c:
                case 0x8000000c:
                    msr.tr10 = EAX;
                    break;
                /* TR11 - Branch Target Buffer Control */
                case 0x0000000d:
                case 0x8000000d:
                    msr.tr11 = EAX & ((cpu_s->cpu_type >= CPU_PENTIUMMMX) ? 0x3001fcf : 0xfcf);
                    break;
                /* TR12 - New Feature Control */
                case 0x0000000e:
                case 0x8000000e:
                    if (cpu_s->cpu_type == CPU_PENTIUMMMX)
                        temp = EAX & 0x38034f;
                    else if ((CPUID & 0xfff) >= 0x52b)
                        temp = EAX & 0x20435f;
                    else if ((CPUID & 0xfff) >= 0x520)
                        temp = EAX & 0x20035f;
                    else
                        temp = EAX & 0x20030f;
                    msr.tr12 = temp;
                    break;
                /* Time Stamp Counter */
                case 0x00000010:
                case 0x80000010:
                    timer_set_new_tsc(EAX | ((uint64_t) EDX << 32));
                    break;
                /* Performance Monitor - Control and Event Select */
                case 0x00000011:
                case 0x80000011:
                    msr.cesr = EAX & 0x3ff03ff;
                    break;
                /* Performance Monitor - Event Counter 0 */
                case 0x00000012:
                case 0x80000012:
                    msr.pmc[0] = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Performance Monitor - Event Counter 1 */
                case 0x00000013:
                case 0x80000013:
                    msr.pmc[1] = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Unknown */
                case 0x00000014:
                case 0x80000014:
                    if ((CPUID & 0xfff) <= 0x520)
                        goto pentium_invalid_wrmsr;
                    break;
                /* Unknown, possibly paging-related; initial value is 0004h,
                   becomes 0008h once paging is enabled */
                case 0x80000018:
                /* Floating point - last prefetched opcode
                   bits 10-8: low three bits of first byte of FP instruction
                   bits 7-0: second byte of floating-point instruction */
                case 0x80000019:
                /* Floating point - last executed non-control opcode */
                case 0x8000001a:
                    break;
                /* Floating point - last non-control exception opcode - part
                   of FSTENV/FSAVE'd environment */
                case 0x8000001b:
                    EAX = msr.fp_last_xcpt & 0x7ff;
                    break;
                /* Unknown */
                case 0x8000001c:
                    break;
                /* Probe Mode Control */
                case 0x8000001d:
                    EAX = msr.probe_ctl & 0x7;
                    break;
                /* Unknown, possibly scratchpad register */
                case 0x8000001e:
                    msr.ecx8000001e = EAX;
                    break;
                /* Unknown, possibly scratchpad register */
                case 0x8000001f:
                    msr.ecx8000001f = EAX;
                    break;
                /* Reserved/Unimplemented */
                case 0x80000003:
                case 0x8000000a:
                case 0x8000000f:
                case 0x80000015 ... 0x80000017:
                    break;
                default:
pentium_invalid_wrmsr:
                    cpu_log("WRMSR: Invalid MSR: %08X\n", ECX);
                    x86gpf(NULL, 0);
                    break;
            }
            break;

#ifdef USE_CYRIX_6X86
        case CPU_Cx6x86:
        case CPU_Cx6x86L:
        case CPU_CxGX1:
        case CPU_Cx6x86MX:
            cpu_log("WRMSR: ECX = %08X, val = %08X%08X\n", ECX, EDX, EAX);
            switch (ECX) {
                /* Test Data */
                case 0x03:
                    msr.tr3 = EAX;
                /* Test Address */
                case 0x04:
                    msr.tr4 = EAX;
                /* Test Command/Status */
                case 0x05:
                    msr.tr5 = EAX & 0x008f0f3b;
                /* Time Stamp Counter */
                case 0x10:
                    timer_set_new_tsc(EAX | ((uint64_t) EDX << 32));
                    break;
                /* Performance Monitor - Control and Event Select */
                case 0x11:
                    msr.cesr = EAX & 0x7ff07ff;
                    break;
                /* Performance Monitor - Event Counter 0 */
                case 0x12:
                    msr.pmc[0] = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Performance Monitor - Event Counter 1 */
                case 0x13:
                    msr.pmc[1] = EAX | ((uint64_t) EDX << 32);
                    break;
            }
            break;
#endif /* USE_CYRIX_6X86 */

        case CPU_PENTIUMPRO:
        case CPU_PENTIUM2:
        case CPU_PENTIUM2D:
            /* Per RichardG's probing of a real Deschutes using my RDMSR tool,
               we have discovered that the top 18 bits are filtered out. */
            switch (ECX & 0x00003fff) {
                /* Machine Check Exception Address */
                case 0x00:
                /* Machine Check Exception Type */
                case 0x01:
                    if (EAX || EDX)
                        x86gpf(NULL, 0);
                    break;
                /* Time Stamp Counter */
                case 0x10:
                    timer_set_new_tsc(EAX | ((uint64_t) EDX << 32));
                    break;
                /* Unknown */
                case 0x18:
                    break;
                /* IA32_APIC_BASE - APIC Base Address */
                case 0x1b:
                    cpu_log("APIC_BASE write: %08X%08X\n", EDX, EAX);
#if 0
                    msr.apic_base = EAX | ((uint64_t) EDX << 32);
#endif
                    break;
                /* Unknown (undocumented?) MSR used by the Hyper-V BIOS */
                case 0x20:
                    msr.ecx20 = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Unknown */
                case 0x21:
                    if (cpu_s->cpu_type == CPU_PENTIUMPRO)
                        goto i686_invalid_wrmsr;
                    break;
                /* EBL_CR_POWERON - Processor Hard Power-On Configuration */
                case 0x2a:
                    break;
                /* Unknown */
                case 0x32:
                    if (cpu_s->cpu_type == CPU_PENTIUMPRO)
                        goto i686_invalid_wrmsr;
                    break;
                /* TEST_CTL - Test Control Register */
                case 0x33:
                    msr.test_ctl = EAX;
                    break;
                /* Unknown */
                case 0x34:
                case 0x3a:
                case 0x3b:
                case 0x50 ... 0x54:
                    break;
                /* BIOS_UPDT_TRIG - BIOS Update Trigger */
                case 0x79:
                    msr.bios_updt = EAX | ((uint64_t) EDX << 32);
                    break;
                /* BBL_CR_D0 ... BBL_CR_D3 - Chunk 0..3 Data Register
                   8Bh: BIOS_SIGN - BIOS Update Signature */
                case 0x88 ... 0x8b:
                    msr.bbl_cr_dx[ECX - 0x88] = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Unknown */
                case 0xae:
                    break;
                /* PERFCTR0 - Performance Counter Register 0 */
                case 0xc1:
                /* PERFCTR1 - Performance Counter Register 1 */
                case 0xc2:
                    msr.perfctr[ECX - 0xC1] = EAX | ((uint64_t) EDX << 32);
                    break;
                /* MTRRcap */
                case 0xfe:
                    msr.mtrr_cap = EAX | ((uint64_t) EDX << 32);
                    break;
                /* BBL_CR_ADDR - L2 Cache Address Register */
                case 0x116:
                    msr.bbl_cr_addr = EAX | ((uint64_t) EDX << 32);
                    break;
                /* BBL_CR_DECC - L2 Cache Date ECC Register */
                case 0x118:
                    msr.bbl_cr_decc = EAX | ((uint64_t) EDX << 32);
                    break;
                /* BBL_CR_CTL - L2 Cache Control Register */
                case 0x119:
                    msr.bbl_cr_ctl = EAX | ((uint64_t) EDX << 32);
                    break;
                /* BBL_CR_TRIG - L2 Cache Trigger Register */
                case 0x11a:
                    msr.bbl_cr_trig = EAX | ((uint64_t) EDX << 32);
                    break;
                /* BBL_CR_BUSY - L2 Cache Busy Register */
                case 0x11b:
                    msr.bbl_cr_busy = EAX | ((uint64_t) EDX << 32);
                    break;
                /* BBL_CR_CTL3 - L2 Cache Control Register 3 */
                case 0x11e:
                    msr.bbl_cr_ctl3 = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Unknown */
                case 0x131:
                case 0x14e ... 0x151:
                case 0x154:
                case 0x15b:
                case 0x15f:
                    break;
                /* SYSENTER_CS - SYSENTER target CS */
                case 0x174:
                    msr.sysenter_cs = EAX & 0xFFFF;
                    break;
                /* SYSENTER_ESP - SYSENTER target ESP */
                case 0x175:
                    msr.sysenter_esp = EAX;
                    break;
                /* SYSENTER_EIP - SYSENTER target EIP */
                case 0x176:
                    msr.sysenter_eip = EAX;
                    break;
                /* MCG_CAP - Machine Check Global Capability */
                case 0x179:
                    break;
                /* MCG_STATUS - Machine Check Global Status */
                case 0x17a:
                    if (EAX || EDX)
                        x86gpf(NULL, 0);
                    break;
                /* MCG_CTL - Machine Check Global Control */
                case 0x17b:
                    msr.mcg_ctl = EAX | ((uint64_t) EDX << 32);
                    break;
                /* EVNTSEL0 - Performance Counter Event Select 0 */
                case 0x186:
                /* EVNTSEL1 - Performance Counter Event Select 1 */
                case 0x187:
                    msr.evntsel[ECX - 0x186] = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x1d3:
                    break;
                /* DEBUGCTLMSR - Debugging Control Register */
                case 0x1d9:
                    msr.debug_ctl = EAX;
                    break;
                /* ROB_CR_BKUPTMPDR6 */
                case 0x1e0:
                    msr.rob_cr_bkuptmpdr6 = EAX;
                    break;
                /* ECX & 0: MTRRphysBase0 ... MTRRphysBase7
                   ECX & 1: MTRRphysMask0 ... MTRRphysMask7 */
                case 0x200 ... 0x20f:
                    if (ECX & 1)
                        msr.mtrr_physmask[(ECX - 0x200) >> 1] = EAX | ((uint64_t) EDX << 32);
                    else
                        msr.mtrr_physbase[(ECX - 0x200) >> 1] = EAX | ((uint64_t) EDX << 32);
                    break;
                /* MTRRfix64K_00000 */
                case 0x250:
                    msr.mtrr_fix64k_8000 = EAX | ((uint64_t) EDX << 32);
                    break;
                /* MTRRfix16K_80000 */
                case 0x258:
                    msr.mtrr_fix16k_8000 = EAX | ((uint64_t) EDX << 32);
                    break;
                /* MTRRfix16K_A0000 */
                case 0x259:
                    msr.mtrr_fix16k_a000 = EAX | ((uint64_t) EDX << 32);
                    break;
                /* MTRRfix4K_C0000 ... MTRRfix4K_F8000 */
                case 0x268 ... 0x26f:
                    msr.mtrr_fix4k[ECX - 0x268] = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Page Attribute Table */
                case 0x277:
                    if (cpu_s->cpu_type < CPU_PENTIUM2D)
                        goto i686_invalid_wrmsr;
                    msr.pat = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Unknown */
                case 0x280:
                    if (cpu_s->cpu_type == CPU_PENTIUMPRO)
                        goto i686_invalid_wrmsr;
                    break;
                /* MTRRdefType */
                case 0x2ff:
                    msr.mtrr_deftype = EAX | ((uint64_t) EDX << 32);
                    break;
                /* MC0_CTL - Machine Check 0 Control */
                case 0x400:
                /* MC1_CTL - Machine Check 1 Control */
                case 0x404:
                /* MC2_CTL - Machine Check 2 Control */
                case 0x408:
                /* MC4_CTL - Machine Check 4 Control */
                case 0x40c:
                /* MC3_CTL - Machine Check 3 Control */
                case 0x410:
                    msr.mca_ctl[(ECX - 0x400) >> 2] = EAX | ((uint64_t) EDX << 32);
                    break;
                /* MC0_STATUS - Machine Check 0 Status */
                case 0x401:
                /* MC0_ADDR - Machine Check 0 Address */
                case 0x402:
                /* MC1_STATUS - Machine Check 1 Status */
                case 0x405:
                /* MC1_ADDR - Machine Check 1 Address */
                case 0x406:
                /* MC2_STATUS - Machine Check 2 Status */
                case 0x409:
                /* MC2_ADDR - Machine Check 2 Address */
                case 0x40a:
                /* MC4_STATUS - Machine Check 4 Status */
                case 0x40d:
                /* MC4_ADDR - Machine Check 4 Address */
                case 0x40e:
                /* MC3_STATUS - Machine Check 3 Status */
                case 0x411:
                /* MC3_ADDR - Machine Check 3 Address */
                case 0x412:
                    if (EAX || EDX)
                        x86gpf(NULL, 0);
                    break;
                /* Unknown */
                case 0x570:
                    msr.ecx570 = EAX | ((uint64_t) EDX << 32);
                    break;
                /* Unknown, possibly debug registers? */
                case 0x1000 ... 0x1007:
                /* Unknown, possibly control registers? */
                case 0x2000:
                case 0x2002 ... 0x2004:
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

static void
cpu_write(uint16_t addr, uint8_t val, UNUSED(void *priv))
{
    if (addr == 0xf0) {
        /* Writes to F0 clear FPU error and deassert the interrupt. */
        if (is286)
            picintc(1 << 13);
        else
            nmi = 0;
        return;
    } else if (addr >= 0xf1)
        return; /* FPU stuff */

    if (!(addr & 1))
        cyrix_addr = val;
    else
        switch (cyrix_addr) {
            case 0xc0: /* CCR0 */
                ccr0 = val;
                break;
            case 0xc1: /* CCR1 */
                if ((ccr3 & CCR3_SMI_LOCK) && !in_smm)
                    val = (val & ~(CCR1_USE_SMI | CCR1_SMAC | CCR1_SM3)) | (ccr1 & (CCR1_USE_SMI | CCR1_SMAC | CCR1_SM3));
                ccr1 = val;
                break;
            case 0xc2: /* CCR2 */
                ccr2 = val;
                break;
            case 0xc3: /* CCR3 */
                if ((ccr3 & CCR3_SMI_LOCK) && !in_smm)
                    val = (val & ~(CCR3_NMI_EN)) | (ccr3 & CCR3_NMI_EN) | CCR3_SMI_LOCK;
                ccr3 = val;
                break;
            case 0xcd:
                if (!(ccr3 & CCR3_SMI_LOCK) || in_smm) {
                    cyrix.arr[3].base = (cyrix.arr[3].base & ~0xff000000) | (val << 24);
                    cyrix.smhr &= ~SMHR_VALID;
                }
                break;
            case 0xce:
                if (!(ccr3 & CCR3_SMI_LOCK) || in_smm) {
                    cyrix.arr[3].base = (cyrix.arr[3].base & ~0x00ff0000) | (val << 16);
                    cyrix.smhr &= ~SMHR_VALID;
                }
                break;
            case 0xcf:
                if (!(ccr3 & CCR3_SMI_LOCK) || in_smm) {
                    cyrix.arr[3].base = (cyrix.arr[3].base & ~0x0000f000) | ((val & 0xf0) << 8);
                    if ((val & 0xf) == 0xf)
                        cyrix.arr[3].size = 1ULL << 32; /* 4 GB */
                    else if (val & 0xf)
                        cyrix.arr[3].size = 2048 << (val & 0xf);
                    else
                        cyrix.arr[3].size = 0; /* Disabled */
                    cyrix.smhr &= ~SMHR_VALID;
                }
                break;

            case 0xe8: /* CCR4 */
                if ((ccr3 & 0xf0) == 0x10) {
                    ccr4 = val;
#ifdef USE_CYRIX_6X86
                    if (cpu_s->cpu_type >= CPU_Cx6x86) {
                        if (val & 0x80)
                            CPUID = cpu_s->cpuid_model;
                        else
                            CPUID = 0;
                    }
#endif /* USE_CYRIX_6X86 */
                }
                break;
            case 0xe9: /* CCR5 */
                if ((ccr3 & 0xf0) == 0x10)
                    ccr5 = val;
                break;
            case 0xea: /* CCR6 */
                if ((ccr3 & 0xf0) == 0x10)
                    ccr6 = val;
                break;
        }
}

static uint8_t
cpu_read(uint16_t addr, UNUSED(void *priv))
{
    if (addr == 0xf007)
        return 0x7f;

    if (addr >= 0xf0)
        return 0xff; /* FPU stuff */

    if (addr & 1) {
        switch (cyrix_addr) {
            case 0xc0:
                return ccr0;
            case 0xc1:
                return ccr1;
            case 0xc2:
                return ccr2;
            case 0xc3:
                return ccr3;
            case 0xe8:
                return ((ccr3 & 0xf0) == 0x10) ? ccr4 : 0xff;
            case 0xe9:
                return ((ccr3 & 0xf0) == 0x10) ? ccr5 : 0xff;
            case 0xea:
                return ((ccr3 & 0xf0) == 0x10) ? ccr6 : 0xff;
            case 0xfe:
                return cpu_s->cyrix_id & 0xff;
            case 0xff:
                return cpu_s->cyrix_id >> 8;

            default:
                break;
        }

        if ((cyrix_addr & 0xf0) == 0xc0)
            return 0xff;

        if (cyrix_addr == 0x20 && (cpu_s->cpu_type == CPU_Cx5x86))
            return 0xff;
    }

    return 0xff;
}

void
#ifdef USE_DYNAREC
x86_setopcodes(const OpFn *opcodes, const OpFn *opcodes_0f,
               const OpFn *dynarec_opcodes, const OpFn *dynarec_opcodes_0f)
{
    x86_opcodes            = opcodes;
    x86_opcodes_0f         = opcodes_0f;
    x86_dynarec_opcodes    = dynarec_opcodes;
    x86_dynarec_opcodes_0f = dynarec_opcodes_0f;
}
#else
x86_setopcodes(const OpFn *opcodes, const OpFn *opcodes_0f)
{
    x86_opcodes    = opcodes;
    x86_opcodes_0f = opcodes_0f;
}
#endif /* USE_DYNAREC */

void
x86_setopcodes_2386(const OpFn *opcodes, const OpFn *opcodes_0f)
{
    x86_2386_opcodes    = opcodes;
    x86_2386_opcodes_0f = opcodes_0f;
}

void
cpu_update_waitstates(void)
{
    cpu_s = (CPU *) &cpu_f->cpus[cpu_effective];

    if (is486)
        cpu_prefetch_width = 16;
    else
        cpu_prefetch_width = cpu_16bitbus ? 2 : 4;

    if (cpu_cache_int_enabled) {
        /* Disable prefetch emulation */
        cpu_prefetch_cycles = 0;
    } else if (cpu_waitstates && (cpu_s->cpu_type >= CPU_286 && cpu_s->cpu_type <= CPU_386DX)) {
        /* Waitstates override */
        cpu_prefetch_cycles = cpu_waitstates + 1;
        cpu_cycles_read     = cpu_waitstates + 1;
        cpu_cycles_read_l   = (cpu_16bitbus ? 2 : 1) * (cpu_waitstates + 1);
        cpu_cycles_write    = cpu_waitstates + 1;
        cpu_cycles_write_l  = (cpu_16bitbus ? 2 : 1) * (cpu_waitstates + 1);
    } else if (cpu_cache_ext_enabled) {
        /* Use cache timings */
        cpu_prefetch_cycles = cpu_s->cache_read_cycles;
        cpu_cycles_read     = cpu_s->cache_read_cycles;
        cpu_cycles_read_l   = (cpu_16bitbus ? 2 : 1) * cpu_s->cache_read_cycles;
        cpu_cycles_write    = cpu_s->cache_write_cycles;
        cpu_cycles_write_l  = (cpu_16bitbus ? 2 : 1) * cpu_s->cache_write_cycles;
    } else {
        /* Use memory timings */
        cpu_prefetch_cycles = cpu_s->mem_read_cycles;
        cpu_cycles_read     = cpu_s->mem_read_cycles;
        cpu_cycles_read_l   = (cpu_16bitbus ? 2 : 1) * cpu_s->mem_read_cycles;
        cpu_cycles_write    = cpu_s->mem_write_cycles;
        cpu_cycles_write_l  = (cpu_16bitbus ? 2 : 1) * cpu_s->mem_write_cycles;
    }

    if (is486)
        cpu_prefetch_cycles = (cpu_prefetch_cycles * 11) / 16;

    cpu_mem_prefetch_cycles = cpu_prefetch_cycles;

    if (cpu_s->rspeed <= 8000000)
        cpu_rom_prefetch_cycles = cpu_mem_prefetch_cycles;
}
