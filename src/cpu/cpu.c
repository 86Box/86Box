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
#include <86box/gdbstub.h>
#ifdef USE_DYNAREC
#    include "codegen.h"
#endif
#include "x87_timings.h"

#define CCR1_USE_SMI  (1 << 1)
#define CCR1_SMAC     (1 << 2)
#define CCR1_SM3      (1 << 7)

#define CCR3_SMI_LOCK (1 << 0)
#define CCR3_NMI_EN   (1 << 1)

enum {
    CPUID_FPU       = (1 << 0),
    CPUID_VME       = (1 << 1),
    CPUID_PSE       = (1 << 3),
    CPUID_TSC       = (1 << 4),
    CPUID_MSR       = (1 << 5),
    CPUID_PAE       = (1 << 6),
    CPUID_MCE       = (1 << 7),
    CPUID_CMPXCHG8B = (1 << 8),
    CPUID_AMDSEP    = (1 << 10),
    CPUID_SEP       = (1 << 11),
    CPUID_MTRR      = (1 << 12),
    CPUID_PGE       = (1 << 13),
    CPUID_MCA       = (1 << 14),
    CPUID_CMOV      = (1 << 15),
    CPUID_MMX       = (1 << 23),
    CPUID_FXSR      = (1 << 24)
};

/*Addition flags returned by CPUID function 0x80000001*/
#define CPUID_3DNOW  (1UL << 31UL)
#define CPUID_3DNOWE (1UL << 30UL)

/* Make sure this is as low as possible. */
cpu_state_t cpu_state;
fpu_state_t fpu_state;

/* Place this immediately after. */
uint32_t abrt_error;

#ifdef USE_DYNAREC
const OpFn *x86_dynarec_opcodes, *x86_dynarec_opcodes_0f,
    *x86_dynarec_opcodes_d8_a16, *x86_dynarec_opcodes_d8_a32,
    *x86_dynarec_opcodes_d9_a16, *x86_dynarec_opcodes_d9_a32,
    *x86_dynarec_opcodes_da_a16, *x86_dynarec_opcodes_da_a32,
    *x86_dynarec_opcodes_db_a16, *x86_dynarec_opcodes_db_a32,
    *x86_dynarec_opcodes_dc_a16, *x86_dynarec_opcodes_dc_a32,
    *x86_dynarec_opcodes_dd_a16, *x86_dynarec_opcodes_dd_a32,
    *x86_dynarec_opcodes_de_a16, *x86_dynarec_opcodes_de_a32,
    *x86_dynarec_opcodes_df_a16, *x86_dynarec_opcodes_df_a32,
    *x86_dynarec_opcodes_REPE, *x86_dynarec_opcodes_REPNE,
    *x86_dynarec_opcodes_3DNOW;
#endif

const OpFn *x86_opcodes, *x86_opcodes_0f,
    *x86_opcodes_d8_a16, *x86_opcodes_d8_a32,
    *x86_opcodes_d9_a16, *x86_opcodes_d9_a32,
    *x86_opcodes_da_a16, *x86_opcodes_da_a32,
    *x86_opcodes_db_a16, *x86_opcodes_db_a32,
    *x86_opcodes_dc_a16, *x86_opcodes_dc_a32,
    *x86_opcodes_dd_a16, *x86_opcodes_dd_a32,
    *x86_opcodes_de_a16, *x86_opcodes_de_a32,
    *x86_opcodes_df_a16, *x86_opcodes_df_a32,
    *x86_opcodes_REPE, *x86_opcodes_REPNE,
    *x86_opcodes_3DNOW;

uint16_t cpu_fast_off_count, cpu_fast_off_val;
uint16_t temp_seg_data[4] = { 0, 0, 0, 0 };

int isa_cycles, cpu_inited,

    cpu_cycles_read, cpu_cycles_read_l, cpu_cycles_write, cpu_cycles_write_l,
    cpu_prefetch_cycles, cpu_prefetch_width, cpu_mem_prefetch_cycles, cpu_rom_prefetch_cycles,
    cpu_waitstates, cpu_cache_int_enabled, cpu_cache_ext_enabled,
    cpu_isa_speed, cpu_pci_speed, cpu_isa_pci_div, cpu_agp_speed, cpu_alt_reset,

    cpu_override, cpu_effective, cpu_multi, cpu_16bitbus, cpu_64bitbus,
    cpu_cyrix_alignment, CPUID,

    is186, is_nec,
    is286, is386, is6117, is486 = 1,
                          cpu_isintel, cpu_iscyrix, hascache, isibm486, israpidcad, is_vpc,
                          is_am486, is_am486dxl, is_pentium, is_k5, is_k6, is_p6, is_cxsmm, hasfpu,

                          timing_rr, timing_mr, timing_mrl, timing_rm, timing_rml,
                          timing_mm, timing_mml, timing_bt, timing_bnt,
                          timing_int, timing_int_rm, timing_int_v86, timing_int_pm,
                          timing_int_pm_outer, timing_iret_rm, timing_iret_v86, timing_iret_pm,
                          timing_iret_pm_outer, timing_call_rm, timing_call_pm, timing_call_pm_gate,
                          timing_call_pm_gate_inner, timing_retf_rm, timing_retf_pm, timing_retf_pm_outer,
                          timing_jmp_rm, timing_jmp_pm, timing_jmp_pm_gate, timing_misaligned;
uint32_t cpu_features, cpu_fast_off_flags;

uint32_t _tr[8]      = { 0, 0, 0, 0, 0, 0, 0, 0 };
uint32_t cache_index = 0;
uint8_t  _cache[2048];

uint64_t cpu_CR4_mask, tsc = 0;
uint64_t pmc[2] = { 0, 0 };

double cpu_dmulti, cpu_busspeed;

msr_t msr;

cyrix_t cyrix;

cpu_family_t *cpu_f;
CPU          *cpu_s;

uint8_t do_translate = 0, do_translate2 = 0;

void (*cpu_exec)(int cycs);

static uint8_t ccr0, ccr1, ccr2, ccr3, ccr4, ccr5, ccr6;

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
    uint32_t         packages, bus_speed;
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
        memset(fpu_state.st_space, 0, sizeof(floatx80)*8);
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
#endif

    soft_reset_pci = 0;

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
#endif
    x86_opcodes_REPE  = ops_REPE;
    x86_opcodes_REPNE = ops_REPNE;
    x86_opcodes_3DNOW = ops_3DNOW;
#ifdef USE_DYNAREC
    x86_dynarec_opcodes_REPE  = dynarec_ops_REPE;
    x86_dynarec_opcodes_REPNE = dynarec_ops_REPNE;
    x86_dynarec_opcodes_3DNOW = dynarec_ops_3DNOW;
#endif

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
#endif
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
#endif
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

#ifdef USE_DYNAREC
    codegen_timing_set(&codegen_timing_486);
#endif

    memset(&msr, 0, sizeof(msr));

    timing_misaligned   = 0;
    cpu_cyrix_alignment = 0;
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
#endif
            break;

        case CPU_286:
#ifdef USE_DYNAREC
            x86_setopcodes(ops_286, ops_286_0f, dynarec_ops_286, dynarec_ops_286_0f);
#else
            x86_setopcodes(ops_286, ops_286_0f);
#endif

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
#endif
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
#endif
            cpu_features = CPU_FEATURE_MSR;
            /* FALLTHROUGH */
        case CPU_386SX:
        case CPU_386DX:
            if (fpu_type == FPU_287) { /* In case we get Deskpro 386 emulation */
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
#endif
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
#endif

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
#endif

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
            /* FALLTHROUGH */
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
#endif

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
#endif

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
#endif

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
#endif

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
#endif
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
#endif

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
            msr.fcr      = (1 << 8) | (1 << 9) | (1 << 12) | (1 << 16) | (1 << 19) | (1 << 21);
            cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | CR4_PSE | CR4_MCE | CR4_PCE;
#ifdef USE_DYNAREC
            codegen_timing_set(&codegen_timing_pentium);
#endif
            break;

#if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
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
#    endif
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
                // x86_setopcodes(ops_386, ops_c6x86_0f, dynarec_ops_386, dynarec_ops_c6x86_0f);
#    else
            if (cpu_s->cpu_type == CPU_Cx6x86MX)
                x86_setopcodes(ops_386, ops_c6x86mx_0f);
            else if (cpu_s->cpu_type == CPU_Cx6x86L)
                x86_setopcodes(ops_386, ops_pentium_0f);
            else
                x86_setopcodes(ops_386, ops_c6x86mx_0f);
                // x86_setopcodes(ops_386, ops_c6x86_0f);
#    endif

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
            msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) | (1 << 16) | (1 << 19) | (1 << 21);
            if (cpu_s->cpu_type >= CPU_CxGX1)
                cpu_CR4_mask = CR4_TSD | CR4_DE | CR4_PCE;

#    ifdef USE_DYNAREC
            codegen_timing_set(&codegen_timing_686);
#    endif

            if ((cpu_s->cpu_type == CPU_Cx6x86L) || (cpu_s->cpu_type == CPU_Cx6x86MX))
                ccr4 = 0x80;
            else if (CPU_Cx6x86)
                CPUID = 0; /* Disabled on powerup by default */
            break;
#endif

#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
        case CPU_K5:
        case CPU_5K86:
#endif
        case CPU_K6:
        case CPU_K6_2:
        case CPU_K6_2C:
        case CPU_K6_3:
        case CPU_K6_2P:
        case CPU_K6_3P:
#ifdef USE_DYNAREC
            if (cpu_s->cpu_type >= CPU_K6_2)
                x86_setopcodes(ops_386, ops_k62_0f, dynarec_ops_386, dynarec_ops_k62_0f);
#    if defined(DEV_BRANCH) && defined(USE_AMD_K5)
            else if (cpu_s->cpu_type == CPU_K6)
                x86_setopcodes(ops_386, ops_k6_0f, dynarec_ops_386, dynarec_ops_k6_0f);
            else
                x86_setopcodes(ops_386, ops_pentiummmx_0f, dynarec_ops_386, dynarec_ops_pentiummmx_0f);
#    else
            else
                x86_setopcodes(ops_386, ops_k6_0f, dynarec_ops_386, dynarec_ops_k6_0f);
#    endif
#else
            if (cpu_s->cpu_type >= CPU_K6_2)
                x86_setopcodes(ops_386, ops_k62_0f);
#    if defined(DEV_BRANCH) && defined(USE_AMD_K5)
            else if (cpu_s->cpu_type == CPU_K6)
                x86_setopcodes(ops_386, ops_k6_0f);
            else
                x86_setopcodes(ops_386, ops_pentiummmx_0f);
#    else
            else
                x86_setopcodes(ops_386, ops_k6_0f);
#    endif
#endif

            if ((cpu_s->cpu_type == CPU_K6_2P) || (cpu_s->cpu_type == CPU_K6_3P)) {
                x86_opcodes_3DNOW = ops_3DNOWE;
#ifdef USE_DYNAREC
                x86_dynarec_opcodes_3DNOW = dynarec_ops_3DNOWE;
#endif
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
            msr.fcr = (1 << 8) | (1 << 9) | (1 << 12) | (1 << 16) | (1 << 19) | (1 << 21);
#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
            cpu_CR4_mask = CR4_TSD | CR4_DE | CR4_MCE;
            if (cpu_s->cpu_type >= CPU_K6) {
                cpu_CR4_mask |= (CR4_VME | CR4_PVI | CR4_PSE);
                if (cpu_s->cpu_type <= CPU_K6)
                    cpu_CR4_mask |= CR4_PCE;
            }
#else
            cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | CR4_PSE | CR4_MCE;
            if (cpu_s->cpu_type == CPU_K6)
                cpu_CR4_mask |= CR4_PCE;
#endif

#ifdef USE_DYNAREC
            codegen_timing_set(&codegen_timing_k6);
#endif
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
#endif
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
            msr.fcr      = (1 << 8) | (1 << 9) | (1 << 12) | (1 << 16) | (1 << 19) | (1 << 21);
            cpu_CR4_mask = CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | CR4_PSE | CR4_MCE | CR4_PAE | CR4_PCE | CR4_PGE;
            if (cpu_s->cpu_type == CPU_PENTIUM2D)
                cpu_CR4_mask |= CR4_OSFXSR;

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
            msr.fcr      = (1 << 8) | (1 << 9) | (1 << 12) | (1 << 16) | (1 << 18) | (1 << 19) | (1 << 20) | (1 << 21);
            cpu_CR4_mask = CR4_TSD | CR4_DE | CR4_MCE | CR4_PCE;

            cpu_cyrix_alignment = 1;

#ifdef USE_DYNAREC
            codegen_timing_set(&codegen_timing_winchip);
#endif
            break;

        default:
            fatal("cpu_set : unknown CPU type %i\n", cpu_s->cpu_type);
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

    if (is386) {
#if defined(USE_DYNAREC) && !defined(USE_GDBSTUB)
        if (cpu_use_dynarec)
            cpu_exec = exec386_dynarec;
        else
#endif
            cpu_exec = exec386;
    } else if (cpu_s->cpu_type >= CPU_286)
        cpu_exec = exec386;
    else
        cpu_exec = execx86;
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
        pc_speed_changed();
    } else if (cpu_busspeed >= 8000000)
        cpu_isa_speed = 8000000;
    else
        cpu_isa_speed = cpu_busspeed;

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

    return (bufp);
}

void
cpu_CPUID(void)
{
    switch (cpu_s->cpu_type) {
        case CPU_i486SX_SLENH:
            if (!EAX) {
                EAX = 0x00000001;
                EBX = 0x756e6547;
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
                EBX = 0x756e6547;
                EDX = 0x49656e69;
                ECX = 0x6c65746e;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_VME;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

        case CPU_ENH_Am486DX:
            if (!EAX) {
                EAX = 1;
                EBX = 0x68747541;
                ECX = 0x444D4163;
                EDX = 0x69746E65;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU; /*FPU*/
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

        case CPU_WINCHIP:
            if (!EAX) {
                EAX = 1;
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
                EAX = 0x540;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_TSC | CPUID_MSR;
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
                    EAX = 1;
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
                    EAX = CPUID;
                    EBX = ECX = 0;
                    EDX       = CPUID_FPU | CPUID_TSC | CPUID_MSR;
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
                EBX = 0x756e6547;
                EDX = 0x49656e69;
                ECX = 0x6c65746e;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
        case CPU_K5:
            if (!EAX) {
                EAX = 0x00000001;
                EBX = 0x68747541;
                EDX = 0x69746E65;
                ECX = 0x444D4163;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

        case CPU_5K86:
            if (!EAX) {
                EAX = 0x00000001;
                EBX = 0x68747541;
                EDX = 0x69746E65;
                ECX = 0x444D4163;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B;
            } else if (EAX == 0x80000000) {
                EAX = 0x80000005;
                EBX = ECX = EDX = 0;
            } else if (EAX == 0x80000001) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B;
            } else if (EAX == 0x80000002) {
                EAX = 0x2D444D41;
                EBX = 0x7428354B;
                ECX = 0x5020296D;
                EDX = 0x65636F72;
            } else if (EAX == 0x80000003) {
                EAX = 0x726F7373;
                EBX = ECX = EDX = 0;
            } else if (EAX == 0x80000004)
                EAX = EBX = ECX = EDX = 0;
            else if (EAX == 0x80000005) {
                EAX = 0;
                EBX = 0x04800000;
                ECX = 0x08040120;
                EDX = 0x10040120;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;
#endif

        case CPU_K6:
            if (!EAX) {
                EAX = 0x00000001;
                EBX = 0x68747541;
                EDX = 0x69746E65;
                ECX = 0x444D4163;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_MMX;
            } else if (EAX == 0x80000000) {
                EAX = 0x80000005;
                EBX = ECX = EDX = 0;
            } else if (EAX == 0x80000001) {
                EAX = CPUID + 0x100;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_AMDSEP | CPUID_MMX;
            } else if (EAX == 0x80000002) {
                EAX = 0x2D444D41;
                EBX = 0x6D74364B;
                ECX = 0x202F7720;
                EDX = 0x746C756D;
            } else if (EAX == 0x80000003) {
                EAX = 0x64656D69;
                EBX = 0x65206169;
                ECX = 0x6E657478;
                EDX = 0x6E6F6973;
            } else if (EAX == 0x80000004) {
                EAX = 0x73;
                EBX = ECX = EDX = 0;
            } else if (EAX == 0x80000005) {
                EAX = 0;
                EBX = 0x02800140;
                ECX = 0x20020220;
                EDX = 0x20020220;
            } else if (EAX == 0x8FFFFFFF) {
                EAX = 0x4778654E;
                EBX = 0x72656E65;
                ECX = 0x6F697461;
                EDX = 0x444D416E;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

        case CPU_K6_2:
        case CPU_K6_2C:
            switch (EAX) {
                case 0:
                    EAX = 1;
                    EBX = 0x68747541; /* AuthenticAMD */
                    ECX = 0x444d4163;
                    EDX = 0x69746e65;
                    break;
                case 1:
                    EAX = CPUID;
                    EBX = ECX = 0;
                    EDX       = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_MMX;
                    break;
                case 0x80000000:
                    EAX = 0x80000005;
                    EBX = ECX = EDX = 0;
                    break;
                case 0x80000001:
                    EAX = CPUID + 0x100;
                    EBX = ECX = 0;
                    EDX       = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_AMDSEP | CPUID_MMX | CPUID_3DNOW;
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
                case 0x80000005: /*Cache information*/
                    EAX = 0;
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
            switch (EAX) {
                case 0:
                    EAX = 1;
                    EBX = 0x68747541; /* AuthenticAMD */
                    ECX = 0x444d4163;
                    EDX = 0x69746e65;
                    break;
                case 1:
                    EAX = CPUID;
                    EBX = ECX = 0;
                    EDX       = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_MMX;
                    break;
                case 0x80000000:
                    EAX = 0x80000006;
                    EBX = ECX = EDX = 0;
                    break;
                case 0x80000001:
                    EAX = CPUID + 0x100;
                    EBX = ECX = 0;
                    EDX       = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_AMDSEP | CPUID_MMX | CPUID_3DNOW;
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
                    ECX = 0x20020220; /*L1 data cache*/
                    EDX = 0x20020220; /*L1 instruction cache*/
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
                    EAX = 1;
                    EBX = 0x68747541; /* AuthenticAMD */
                    ECX = 0x444d4163;
                    EDX = 0x69746e65;
                    break;
                case 1:
                    EAX = CPUID;
                    EBX = ECX = 0;
                    EDX       = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_MMX;
                    break;
                case 0x80000000:
                    EAX = 0x80000007;
                    EBX = ECX = EDX = 0;
                    break;
                case 0x80000001:
                    EAX = CPUID + 0x100;
                    EBX = ECX = 0;
                    EDX       = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_AMDSEP | CPUID_MMX | CPUID_3DNOW | CPUID_3DNOWE;
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
                EBX = 0x756e6547;
                EDX = 0x49656e69;
                ECX = 0x6c65746e;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_MMX;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

#if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
        case CPU_Cx6x86:
            if (!EAX) {
                EAX = 0x00000001;
                EBX = 0x69727943;
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
                EBX = 0x69727943;
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
                EBX = 0x69727943;
                EDX = 0x736e4978;
                ECX = 0x64616574;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

        case CPU_Cx6x86MX:
            if (!EAX) {
                EAX = 0x00000001;
                EBX = 0x69727943;
                EDX = 0x736e4978;
                ECX = 0x64616574;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_TSC | CPUID_MSR | CPUID_CMPXCHG8B | CPUID_CMOV | CPUID_MMX;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;
#endif

        case CPU_PENTIUMPRO:
            if (!EAX) {
                EAX = 0x00000002;
                EBX = 0x756e6547;
                EDX = 0x49656e69;
                ECX = 0x6c65746e;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_PAE | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_SEP | CPUID_CMOV;
            } else if (EAX == 2) {
                EAX = 0x00000001;
                EBX = ECX = 0;
                EDX       = 0x00000000;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

        case CPU_PENTIUM2:
            if (!EAX) {
                EAX = 0x00000002;
                EBX = 0x756e6547;
                EDX = 0x49656e69;
                ECX = 0x6c65746e;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_PAE | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_MMX | CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_SEP | CPUID_CMOV;
            } else if (EAX == 2) {
                EAX = 0x00000001;
                EBX = ECX = 0;
                EDX       = 0x00000000;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

        case CPU_PENTIUM2D:
            if (!EAX) {
                EAX = 0x00000002;
                EBX = 0x756e6547;
                EDX = 0x49656e69;
                ECX = 0x6c65746e;
            } else if (EAX == 1) {
                EAX = CPUID;
                EBX = ECX = 0;
                EDX       = CPUID_FPU | CPUID_VME | CPUID_PSE | CPUID_TSC | CPUID_MSR | CPUID_PAE | CPUID_MCE | CPUID_CMPXCHG8B | CPUID_MMX | CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_SEP | CPUID_FXSR | CPUID_CMOV;
            } else if (EAX == 2) {
                EAX = 0x00000001;
                EBX = ECX = 0;
                EDX       = 0x00000000;
            } else
                EAX = EBX = ECX = EDX = 0;
            break;

        case CPU_CYRIX3S:
            switch (EAX) {
                case 0:
                    EAX = 1;
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
                    EAX = CPUID;
                    EBX = ECX = 0;
                    EDX       = CPUID_FPU | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_MMX | CPUID_MTRR;
                    if (cpu_has_feature(CPU_FEATURE_CX8))
                        EDX |= CPUID_CMPXCHG8B;
                    break;
                case 0x80000000:
                    EAX = 0x80000005;
                    break;
                case 0x80000001:
                    EAX = CPUID;
                    EDX = CPUID_FPU | CPUID_TSC | CPUID_MSR | CPUID_MCE | CPUID_MMX | CPUID_MTRR | CPUID_3DNOW;
                    if (cpu_has_feature(CPU_FEATURE_CX8))
                        EDX |= CPUID_CMPXCHG8B;
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
        case CPU_K6_2P:
        case CPU_K6_3P:
        case CPU_K6_3:
        case CPU_K6_2C:
            msr.amd_psor = (cpu_s->cpu_type >= CPU_K6_3) ? 0x008cULL : 0x018cULL;
            /* FALLTHROUGH */
        case CPU_K6_2:
#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
        case CPU_K5:
        case CPU_5K86:
#endif
        case CPU_K6:
            msr.amd_efer = (cpu_s->cpu_type >= CPU_K6_2C) ? 2ULL : 0ULL;
            break;

        case CPU_PENTIUMPRO:
        case CPU_PENTIUM2:
        case CPU_PENTIUM2D:
            msr.mtrr_cap = 0x00000508ULL;
            /* FALLTHROUGH */
            break;
    }
}

void
cpu_RDMSR(void)
{
    switch (cpu_s->cpu_type) {
        case CPU_IBM386SLC:
        case CPU_IBM486SLC:
        case CPU_IBM486BL:
            EAX = EDX = 0;
            switch (ECX) {
                case 0x1000:
                    EAX = msr.ibm_por & ((cpu_s->cpu_type > CPU_IBM386SLC) ? 0xffeff : 0xfeff);
                    break;

                case 0x1001:
                    EAX = msr.ibm_crcr & 0xffffffffff;
                    break;

                case 0x1002:
                    if ((cpu_s->cpu_type > CPU_IBM386SLC) && cpu_s->multi)
                        EAX = msr.ibm_por2 & 0x3f000000;
                    break;
            }
            break;

        case CPU_WINCHIP:
        case CPU_WINCHIP2:
            EAX = EDX = 0;
            switch (ECX) {
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
            switch (ECX) {
                case 0x00:
                case 0x01:
                    break;
                case 0x10:
                    EAX = tsc & 0xffffffff;
                    EDX = tsc >> 32;
                    break;
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
                case 0x1107:
                    EAX = msr.fcr;
                    break;
                case 0x1108:
                    EAX = msr.fcr2 & 0xffffffff;
                    EDX = msr.fcr2 >> 32;
                    break;
                case 0x200:
                case 0x201:
                case 0x202:
                case 0x203:
                case 0x204:
                case 0x205:
                case 0x206:
                case 0x207:
                case 0x208:
                case 0x209:
                case 0x20a:
                case 0x20b:
                case 0x20c:
                case 0x20d:
                case 0x20e:
                case 0x20f:
                    if (ECX & 1) {
                        EAX = msr.mtrr_physmask[(ECX - 0x200) >> 1] & 0xffffffff;
                        EDX = msr.mtrr_physmask[(ECX - 0x200) >> 1] >> 32;
                    } else {
                        EAX = msr.mtrr_physbase[(ECX - 0x200) >> 1] & 0xffffffff;
                        EDX = msr.mtrr_physbase[(ECX - 0x200) >> 1] >> 32;
                    }
                    break;
                case 0x250:
                    EAX = msr.mtrr_fix64k_8000 & 0xffffffff;
                    EDX = msr.mtrr_fix64k_8000 >> 32;
                    break;
                case 0x258:
                    EAX = msr.mtrr_fix16k_8000 & 0xffffffff;
                    EDX = msr.mtrr_fix16k_8000 >> 32;
                    break;
                case 0x259:
                    EAX = msr.mtrr_fix16k_a000 & 0xffffffff;
                    EDX = msr.mtrr_fix16k_a000 >> 32;
                    break;
                case 0x268:
                case 0x269:
                case 0x26a:
                case 0x26b:
                case 0x26c:
                case 0x26d:
                case 0x26e:
                case 0x26f:
                    EAX = msr.mtrr_fix4k[ECX - 0x268] & 0xffffffff;
                    EDX = msr.mtrr_fix4k[ECX - 0x268] >> 32;
                    break;
                case 0x2ff:
                    EAX = msr.mtrr_deftype & 0xffffffff;
                    EDX = msr.mtrr_deftype >> 32;
                    break;
            }
            break;

#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
        case CPU_K5:
        case CPU_5K86:
#endif
        case CPU_K6:
        case CPU_K6_2:
        case CPU_K6_2C:
        case CPU_K6_3:
        case CPU_K6_2P:
        case CPU_K6_3P:
            EAX = EDX = 0;
            switch (ECX) {
                case 0x00000000:
                case 0x00000001:
                    break;
                case 0x0000000e:
                    EAX = msr.tr12;
                    break;
                case 0x00000010:
                    EAX = tsc & 0xffffffff;
                    EDX = tsc >> 32;
                    break;
                case 0x00000083:
                    EAX = msr.ecx83 & 0xffffffff;
                    EDX = msr.ecx83 >> 32;
                    break;
                case 0xc0000080:
                    EAX = msr.amd_efer & 0xffffffff;
                    EDX = msr.amd_efer >> 32;
                    break;
                case 0xc0000081:
                    if (cpu_s->cpu_type < CPU_K6_2)
                        goto amd_k_invalid_rdmsr;

                    EAX = msr.star & 0xffffffff;
                    EDX = msr.star >> 32;
                    break;
                case 0xc0000082:
                    EAX = msr.amd_whcr & 0xffffffff;
                    EDX = msr.amd_whcr >> 32;
                    break;
                case 0xc0000085:
                    if (cpu_s->cpu_type < CPU_K6_2C)
                        goto amd_k_invalid_rdmsr;

                    EAX = msr.amd_uwccr & 0xffffffff;
                    EDX = msr.amd_uwccr >> 32;
                    break;
                case 0xc0000086:
                    if (cpu_s->cpu_type < CPU_K6_2P)
                        goto amd_k_invalid_rdmsr;

                    EAX = msr.amd_epmr & 0xffffffff;
                    EDX = msr.amd_epmr >> 32;
                    break;
                case 0xc0000087:
                    if (cpu_s->cpu_type < CPU_K6_2C)
                        goto amd_k_invalid_rdmsr;

                    EAX = msr.amd_psor & 0xffffffff;
                    EDX = msr.amd_psor >> 32;
                    break;
                case 0xc0000088:
                    if (cpu_s->cpu_type < CPU_K6_2C)
                        goto amd_k_invalid_rdmsr;

                    EAX = msr.amd_pfir & 0xffffffff;
                    EDX = msr.amd_pfir >> 32;
                    break;
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
#if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
        case CPU_Cx6x86:
        case CPU_Cx6x86L:
        case CPU_CxGX1:
        case CPU_Cx6x86MX:
            if (cpu_s->cpu_type < CPU_Cx6x86)
#endif
                EAX = EDX = 0;
            switch (ECX) {
                case 0x00:
                case 0x01:
                    break;
                case 0x10:
                    EAX = tsc & 0xffffffff;
                    EDX = tsc >> 32;
                    break;
            }
            cpu_log("RDMSR: ECX = %08X, val = %08X%08X\n", ECX, EDX, EAX);
            break;

        case CPU_PENTIUMPRO:
        case CPU_PENTIUM2:
        case CPU_PENTIUM2D:
            EAX = EDX = 0;
            switch (ECX) {
                case 0x00:
                case 0x01:
                    break;
                case 0x10:
                    EAX = tsc & 0xffffffff;
                    EDX = tsc >> 32;
                    break;
                case 0x17:
                    if (cpu_s->cpu_type != CPU_PENTIUM2D)
                        goto i686_invalid_rdmsr;

                    if (cpu_f->package == CPU_PKG_SLOT2)
                        EDX |= 0x80000;
                    else if (cpu_f->package == CPU_PKG_SOCKET370)
                        EDX |= 0x100000;
                    break;
                case 0x1B:
                    EAX = msr.apic_base & 0xffffffff;
                    EDX = msr.apic_base >> 32;
                    cpu_log("APIC_BASE read : %08X%08X\n", EDX, EAX);
                    break;
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
                case 0x79:
                    EAX = msr.ecx79 & 0xffffffff;
                    EDX = msr.ecx79 >> 32;
                    break;
                case 0x88:
                case 0x89:
                case 0x8a:
                case 0x8b:
                    EAX = msr.ecx8x[ECX - 0x88] & 0xffffffff;
                    EDX = msr.ecx8x[ECX - 0x88] >> 32;
                    break;
                case 0xc1:
                case 0xc2:
                case 0xc3:
                case 0xc4:
                case 0xc5:
                case 0xc6:
                case 0xc7:
                case 0xc8:
                    EAX = msr.ia32_pmc[ECX - 0xC1] & 0xffffffff;
                    EDX = msr.ia32_pmc[ECX - 0xC1] >> 32;
                    break;
                case 0xfe:
                    EAX = msr.mtrr_cap & 0xffffffff;
                    EDX = msr.mtrr_cap >> 32;
                    break;
                case 0x116:
                    EAX = msr.ecx116 & 0xffffffff;
                    EDX = msr.ecx116 >> 32;
                    break;
                case 0x118:
                case 0x119:
                case 0x11a:
                case 0x11b:
                    EAX = msr.ecx11x[ECX - 0x118] & 0xffffffff;
                    EDX = msr.ecx11x[ECX - 0x118] >> 32;
                    break;
                case 0x11e:
                    EAX = msr.ecx11e & 0xffffffff;
                    EDX = msr.ecx11e >> 32;
                    break;
                case 0x174:
                    if (cpu_s->cpu_type == CPU_PENTIUMPRO)
                        goto i686_invalid_rdmsr;

                    EAX &= 0xffff0000;
                    EAX |= msr.sysenter_cs;
                    EDX = 0x00000000;
                    break;
                case 0x175:
                    if (cpu_s->cpu_type == CPU_PENTIUMPRO)
                        goto i686_invalid_rdmsr;

                    EAX = msr.sysenter_esp;
                    EDX = 0x00000000;
                    break;
                case 0x176:
                    if (cpu_s->cpu_type == CPU_PENTIUMPRO)
                        goto i686_invalid_rdmsr;

                    EAX = msr.sysenter_eip;
                    EDX = 0x00000000;
                    break;
                case 0x179:
                    EAX = 0x00000105;
                    EDX = 0x00000000;
                    break;
                case 0x17a:
                    break;
                case 0x17b:
                    EAX = msr.mcg_ctl & 0xffffffff;
                    EDX = msr.mcg_ctl >> 32;
                    break;
                case 0x186:
                    EAX = msr.ecx186 & 0xffffffff;
                    EDX = msr.ecx186 >> 32;
                    break;
                case 0x187:
                    EAX = msr.ecx187 & 0xffffffff;
                    EDX = msr.ecx187 >> 32;
                    break;
                case 0x1e0:
                    EAX = msr.ecx1e0 & 0xffffffff;
                    EDX = msr.ecx1e0 >> 32;
                    break;
                case 0x200:
                case 0x201:
                case 0x202:
                case 0x203:
                case 0x204:
                case 0x205:
                case 0x206:
                case 0x207:
                case 0x208:
                case 0x209:
                case 0x20a:
                case 0x20b:
                case 0x20c:
                case 0x20d:
                case 0x20e:
                case 0x20f:
                    if (ECX & 1) {
                        EAX = msr.mtrr_physmask[(ECX - 0x200) >> 1] & 0xffffffff;
                        EDX = msr.mtrr_physmask[(ECX - 0x200) >> 1] >> 32;
                    } else {
                        EAX = msr.mtrr_physbase[(ECX - 0x200) >> 1] & 0xffffffff;
                        EDX = msr.mtrr_physbase[(ECX - 0x200) >> 1] >> 32;
                    }
                    break;
                case 0x250:
                    EAX = msr.mtrr_fix64k_8000 & 0xffffffff;
                    EDX = msr.mtrr_fix64k_8000 >> 32;
                    break;
                case 0x258:
                    EAX = msr.mtrr_fix16k_8000 & 0xffffffff;
                    EDX = msr.mtrr_fix16k_8000 >> 32;
                    break;
                case 0x259:
                    EAX = msr.mtrr_fix16k_a000 & 0xffffffff;
                    EDX = msr.mtrr_fix16k_a000 >> 32;
                    break;
                case 0x268:
                case 0x269:
                case 0x26a:
                case 0x26b:
                case 0x26c:
                case 0x26d:
                case 0x26e:
                case 0x26f:
                    EAX = msr.mtrr_fix4k[ECX - 0x268] & 0xffffffff;
                    EDX = msr.mtrr_fix4k[ECX - 0x268] >> 32;
                    break;
                case 0x277:
                    EAX = msr.pat & 0xffffffff;
                    EDX = msr.pat >> 32;
                    break;
                case 0x2ff:
                    EAX = msr.mtrr_deftype & 0xffffffff;
                    EDX = msr.mtrr_deftype >> 32;
                    break;
                case 0x400:
                case 0x404:
                case 0x408:
                case 0x40c:
                case 0x410:
                    EAX = msr.mca_ctl[(ECX - 0x400) >> 2] & 0xffffffff;
                    EDX = msr.mca_ctl[(ECX - 0x400) >> 2] >> 32;
                    break;
                case 0x401:
                case 0x402:
                case 0x405:
                case 0x406:
                case 0x407:
                case 0x409:
                case 0x40d:
                case 0x40e:
                case 0x411:
                case 0x412:
                    break;
                case 0x570:
                    EAX = msr.ecx570 & 0xffffffff;
                    EDX = msr.ecx570 >> 32;
                    break;
                case 0x1002ff:
                    EAX = msr.ecx1002ff & 0xffffffff;
                    EDX = msr.ecx1002ff >> 32;
                    break;
                case 0xf0f00250:
                    EAX = msr.ecxf0f00250 & 0xffffffff;
                    EDX = msr.ecxf0f00250 >> 32;
                    break;
                case 0xf0f00258:
                    EAX = msr.ecxf0f00258 & 0xffffffff;
                    EDX = msr.ecxf0f00258 >> 32;
                    break;
                case 0xf0f00259:
                    EAX = msr.ecxf0f00259 & 0xffffffff;
                    EDX = msr.ecxf0f00259 >> 32;
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

    switch (cpu_s->cpu_type) {
        case CPU_IBM386SLC:
        case CPU_IBM486BL:
        case CPU_IBM486SLC:
            switch (ECX) {
                case 0x1000:
                    msr.ibm_por           = EAX & ((cpu_s->cpu_type > CPU_IBM386SLC) ? 0xffeff : 0xfeff);
                    cpu_cache_int_enabled = (EAX & (1 << 7));
                    break;
                case 0x1001:
                    msr.ibm_crcr = EAX & 0xffffffffff;
                    break;
                case 0x1002:
                    if ((cpu_s->cpu_type > CPU_IBM386SLC) && cpu_s->multi)
                        msr.ibm_por2 = EAX & 0x3f000000;
                    break;
            }
            break;

        case CPU_WINCHIP:
        case CPU_WINCHIP2:
            switch (ECX) {
                case 0x02:
                    msr.tr1 = EAX & 2;
                    break;
                case 0x0e:
                    msr.tr12 = EAX & 0x228;
                    break;
                case 0x10:
                    tsc = EAX | ((uint64_t) EDX << 32);
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
                    if ((EAX & (1 << 20)) && cpu_s->cpu_type >= CPU_WINCHIP2)
                        cpu_features |= CPU_FEATURE_3DNOW;
                    else
                        cpu_features &= ~CPU_FEATURE_3DNOW;
                    if (EAX & (1 << 29))
                        CPUID = 0;
                    else
                        CPUID = cpu_s->cpuid_model;
                    break;
                case 0x108:
                    msr.fcr2 = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x109:
                    msr.fcr3 = EAX | ((uint64_t) EDX << 32);
                    break;
            }
            break;

        case CPU_CYRIX3S:
            switch (ECX) {
                case 0x00:
                case 0x01:
                    break;
                case 0x10:
                    tsc = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x1107:
                    msr.fcr = EAX;
                    if (EAX & (1 << 1))
                        cpu_features |= CPU_FEATURE_CX8;
                    else
                        cpu_features &= ~CPU_FEATURE_CX8;
                    break;
                case 0x1108:
                    msr.fcr2 = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x1109:
                    msr.fcr3 = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x200:
                case 0x201:
                case 0x202:
                case 0x203:
                case 0x204:
                case 0x205:
                case 0x206:
                case 0x207:
                case 0x208:
                case 0x209:
                case 0x20a:
                case 0x20b:
                case 0x20c:
                case 0x20d:
                case 0x20e:
                case 0x20f:
                    if (ECX & 1)
                        msr.mtrr_physmask[(ECX - 0x200) >> 1] = EAX | ((uint64_t) EDX << 32);
                    else
                        msr.mtrr_physbase[(ECX - 0x200) >> 1] = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x250:
                    msr.mtrr_fix64k_8000 = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x258:
                    msr.mtrr_fix16k_8000 = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x259:
                    msr.mtrr_fix16k_a000 = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x268:
                case 0x269:
                case 0x26A:
                case 0x26B:
                case 0x26C:
                case 0x26D:
                case 0x26E:
                case 0x26F:
                    msr.mtrr_fix4k[ECX - 0x268] = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x2ff:
                    msr.mtrr_deftype = EAX | ((uint64_t) EDX << 32);
                    break;
            }
            break;

#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
        case CPU_K5:
        case CPU_5K86:
#endif
        case CPU_K6:
        case CPU_K6_2:
        case CPU_K6_2C:
        case CPU_K6_3:
        case CPU_K6_2P:
        case CPU_K6_3P:
            switch (ECX) {
                case 0x00:
                case 0x01:
                    break;
                case 0x0e:
                    msr.tr12 = EAX & 0x228;
                    break;
                case 0x10:
                    tsc = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x83:
                    msr.ecx83 = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0xc0000080:
                    temp = EAX | ((uint64_t) EDX << 32);
                    if (temp & ~1ULL)
                        x86gpf(NULL, 0);
                    else
                        msr.amd_efer = temp;
                    break;
                case 0xc0000081:
                    if (cpu_s->cpu_type < CPU_K6_2)
                        goto amd_k_invalid_wrmsr;

                    msr.star = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0xc0000082:
                    msr.amd_whcr = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0xc0000085:
                    if (cpu_s->cpu_type < CPU_K6_2C)
                        goto amd_k_invalid_wrmsr;

                    msr.amd_uwccr = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0xc0000086:
                    if (cpu_s->cpu_type < CPU_K6_2P)
                        goto amd_k_invalid_wrmsr;

                    msr.amd_epmr = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0xc0000087:
                    if (cpu_s->cpu_type < CPU_K6_2C)
                        goto amd_k_invalid_wrmsr;

                    msr.amd_psor = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0xc0000088:
                    if (cpu_s->cpu_type < CPU_K6_2C)
                        goto amd_k_invalid_wrmsr;

                    msr.amd_pfir = EAX | ((uint64_t) EDX << 32);
                    break;
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
#if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
        case CPU_Cx6x86:
        case CPU_Cx6x86L:
        case CPU_CxGX1:
        case CPU_Cx6x86MX:
#endif
            cpu_log("WRMSR: ECX = %08X, val = %08X%08X\n", ECX, EDX, EAX);
            switch (ECX) {
                case 0x00:
                case 0x01:
                    break;
                case 0x10:
                    tsc = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x8b:
#if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
                    if (cpu_s->cpu_type < CPU_Cx6x86) {
#endif
                        cpu_log("WRMSR: Invalid MSR: 0x8B\n");
                        x86gpf(NULL, 0); /* Needed for Vista to correctly break on Pentium */
#if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
                    }
#endif
                    break;
            }
            break;

        case CPU_PENTIUMPRO:
        case CPU_PENTIUM2:
        case CPU_PENTIUM2D:
            switch (ECX) {
                case 0x00:
                case 0x01:
                    if (EAX || EDX)
                        x86gpf(NULL, 0);
                    break;
                case 0x10:
                    tsc = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x1b:
                    cpu_log("APIC_BASE write: %08X%08X\n", EDX, EAX);
                    // msr.apic_base = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x2a:
                    break;
                case 0x79:
                    msr.ecx79 = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x88:
                case 0x89:
                case 0x8a:
                case 0x8b:
                    msr.ecx8x[ECX - 0x88] = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0xc1:
                case 0xc2:
                case 0xc3:
                case 0xc4:
                case 0xc5:
                case 0xc6:
                case 0xc7:
                case 0xc8:
                    msr.ia32_pmc[ECX - 0xC1] = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0xfe:
                    msr.mtrr_cap = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x116:
                    msr.ecx116 = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x118:
                case 0x119:
                case 0x11a:
                case 0x11b:
                    msr.ecx11x[ECX - 0x118] = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x11e:
                    msr.ecx11e = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x174:
                    if (cpu_s->cpu_type == CPU_PENTIUMPRO)
                        goto i686_invalid_wrmsr;

                    msr.sysenter_cs = EAX & 0xFFFF;
                    break;
                case 0x175:
                    if (cpu_s->cpu_type == CPU_PENTIUMPRO)
                        goto i686_invalid_wrmsr;

                    msr.sysenter_esp = EAX;
                    break;
                case 0x176:
                    if (cpu_s->cpu_type == CPU_PENTIUMPRO)
                        goto i686_invalid_wrmsr;

                    msr.sysenter_eip = EAX;
                    break;
                case 0x179:
                    break;
                case 0x17a:
                    if (EAX || EDX)
                        x86gpf(NULL, 0);
                    break;
                case 0x17b:
                    msr.mcg_ctl = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x186:
                    msr.ecx186 = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x187:
                    msr.ecx187 = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x1e0:
                    msr.ecx1e0 = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x200:
                case 0x201:
                case 0x202:
                case 0x203:
                case 0x204:
                case 0x205:
                case 0x206:
                case 0x207:
                case 0x208:
                case 0x209:
                case 0x20a:
                case 0x20b:
                case 0x20c:
                case 0x20d:
                case 0x20e:
                case 0x20f:
                    if (ECX & 1)
                        msr.mtrr_physmask[(ECX - 0x200) >> 1] = EAX | ((uint64_t) EDX << 32);
                    else
                        msr.mtrr_physbase[(ECX - 0x200) >> 1] = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x250:
                    msr.mtrr_fix64k_8000 = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x258:
                    msr.mtrr_fix16k_8000 = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x259:
                    msr.mtrr_fix16k_a000 = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x268:
                case 0x269:
                case 0x26a:
                case 0x26b:
                case 0x26c:
                case 0x26d:
                case 0x26e:
                case 0x26f:
                    msr.mtrr_fix4k[ECX - 0x268] = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x277:
                    msr.pat = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x2ff:
                    msr.mtrr_deftype = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x400:
                case 0x404:
                case 0x408:
                case 0x40c:
                case 0x410:
                    msr.mca_ctl[(ECX - 0x400) >> 2] = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x401:
                case 0x402:
                case 0x405:
                case 0x406:
                case 0x407:
                case 0x409:
                case 0x40d:
                case 0x40e:
                case 0x411:
                case 0x412:
                    if (EAX || EDX)
                        x86gpf(NULL, 0);
                    break;
                case 0x570:
                    msr.ecx570 = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0x1002ff:
                    msr.ecx1002ff = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0xf0f00250:
                    msr.ecxf0f00250 = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0xf0f00258:
                    msr.ecxf0f00258 = EAX | ((uint64_t) EDX << 32);
                    break;
                case 0xf0f00259:
                    msr.ecxf0f00259 = EAX | ((uint64_t) EDX << 32);
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
cpu_write(uint16_t addr, uint8_t val, void *priv)
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
                        cyrix.arr[3].size = 1ull << 32; /* 4 GB */
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
#if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
                    if (cpu_s->cpu_type >= CPU_Cx6x86) {
                        if (val & 0x80)
                            CPUID = cpu_s->cpuid_model;
                        else
                            CPUID = 0;
                    }
#endif
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
cpu_read(uint16_t addr, void *priv)
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
    x86_opcodes = opcodes;
    x86_opcodes_0f = opcodes_0f;
}
#endif

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
