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
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          leilei,
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2018 leilei.
 *          Copyright 2016-2020 Miran Grca.
 */
#ifndef EMU_CPU_H
#define EMU_CPU_H

enum {
    FPU_NONE,
    FPU_8087,
    FPU_80187,
    FPU_287,
    FPU_287XL,
    FPU_387,
    FPU_487SX,
    FPU_INTERNAL
};

enum {
    CPU_8088 = 1, /* 808x class CPUs */
    CPU_80C88,
    CPU_8086,
    CPU_8086_MAZOVIA,
    CPU_V20, /* NEC 808x class CPUs */
    CPU_V30,
    CPU_188, /* 18x class CPUs */
    CPU_186,
    CPU_286,   /* 286 class CPUs */
    CPU_386SX, /* 386 class CPUs */
    CPU_IBM386SLC,
    CPU_IBM486SLC,
    CPU_386DX,
    CPU_IBM486BL,
    CPU_RAPIDCAD,
    CPU_486SLC,
    CPU_486DLC,
    CPU_i486SX, /* 486 class CPUs */
    CPU_Am486SX,
    CPU_Cx486S,
    CPU_i486DX,
    CPU_Am486DX,
    CPU_Am486DXL,
    CPU_Cx486DX,
    CPU_STPC,
    CPU_i486SX_SLENH,
    CPU_i486DX_SLENH,
    CPU_ENH_Am486DX,
    CPU_Cx5x86,
    CPU_P24T,
    CPU_WINCHIP, /* 586 class CPUs */
    CPU_WINCHIP2,
    CPU_PENTIUM,
    CPU_PENTIUMMMX,
    CPU_Cx6x86,
    CPU_Cx6x86MX,
    CPU_Cx6x86L,
    CPU_CxGX1,
    CPU_K5,
    CPU_5K86,
    CPU_K6,
    CPU_K6_2,
    CPU_K6_2C,
    CPU_K6_3,
    CPU_K6_2P,
    CPU_K6_3P,
    CPU_CYRIX3S,
    CPU_PENTIUMPRO, /* 686 class CPUs */
    CPU_PENTIUM2,
    CPU_PENTIUM2D
};

enum {
    CPU_PKG_8088             = (1 << 0),
    CPU_PKG_80C88            = (1 << 28),
    CPU_PKG_8088_EUROPC      = (1 << 1),
    CPU_PKG_8088_VTECH       = (1 << 2),
    CPU_PKG_8086             = (1 << 3),
    CPU_PKG_8086_MAZOVIA     = (1 << 4),
    CPU_PKG_8086_VTECH       = (1 << 5),
    CPU_PKG_188              = (1 << 6),
    CPU_PKG_186              = (1 << 7),
    CPU_PKG_286              = (1 << 8),
    CPU_PKG_386SX            = (1 << 9),
    CPU_PKG_386DX            = (1 << 10),
    CPU_PKG_386DX_DESKPRO386 = (1 << 11),
    CPU_PKG_M6117            = (1 << 12),
    CPU_PKG_386SLC_IBM       = (1 << 13),
    CPU_PKG_486SLC           = (1 << 14),
    CPU_PKG_486SLC_IBM       = (1 << 15),
    CPU_PKG_486BL            = (1 << 16),
    CPU_PKG_486DLC           = (1 << 17),
    CPU_PKG_SOCKET1          = (1 << 18),
    CPU_PKG_SOCKET3          = (1 << 19),
    CPU_PKG_SOCKET3_PC330    = (1 << 20),
    CPU_PKG_STPC             = (1 << 21),
    CPU_PKG_SOCKET4          = (1 << 22),
    CPU_PKG_SOCKET5_7        = (1 << 23),
    CPU_PKG_SOCKET8          = (1 << 24),
    CPU_PKG_SLOT1            = (1 << 25),
    CPU_PKG_SLOT2            = (1 << 26),
    CPU_PKG_SOCKET370        = (1 << 27),
    CPU_PKG_GXM              = (1 << 29),
    CPU_PKG_386EX            = (1 << 30)
};

#define CPU_SUPPORTS_DYNAREC 1
#define CPU_REQUIRES_DYNAREC 2
#define CPU_ALTERNATE_XTAL   4
#define CPU_FIXED_MULTIPLIER 8

typedef struct fpu_t {
    const char *name;
    const char *internal_name;
    const int   type;
} FPU;

typedef struct cpu_t {
    const char *name;
    uint64_t    cpu_type;
    const FPU  *fpus;
    uint32_t    rspeed;
    double      multi;
    uint16_t    voltage;
    uint32_t    edx_reset;
    uint32_t    cpuid_model;
    uint16_t    cyrix_id;
    uint8_t     cpu_flags;
    int8_t      mem_read_cycles;
    int8_t      mem_write_cycles;
    int8_t      cache_read_cycles;
    int8_t      cache_write_cycles;
    int8_t      atclk_div;
} CPU;

typedef struct {
    const uint32_t package;
    const char    *manufacturer;
    const char    *name;
    const char    *internal_name;
    const CPU     *cpus;
} cpu_family_t;

/* Global variables. */
extern const cpu_family_t cpu_families[];
extern cpu_family_t *     cpu_f;
extern CPU *              cpu_s;
extern int                cpu_override;

extern int                cpu_waitstates;

/* Debug register operation (breakpoints, single-step DR6 status, RF) on the
   processors run by the 486 interpreter and the recompiler; the 386
   interpreter always has it. Set from the configuration, a change takes
   effect on a hard reset. */
extern int                cpu_debug_regs_486;

extern cpu_family_t *     cpu_get_family(const char *internal_name);
extern uint8_t            cpu_is_eligible(const cpu_family_t *cpu_family,
                                          int cpu, int machine);
extern uint8_t            cpu_family_is_eligible(const cpu_family_t *cpu_family,
                                                 int machine);
extern int                fpu_get_type(const cpu_family_t *cpu_family,
                                       int cpu, const char *internal_name);
extern const char *       fpu_get_internal_name(const cpu_family_t *cpu_family,
                                                int cpu, int type);
extern const char *       fpu_get_name_from_index(const cpu_family_t *cpu_family,
                                                  int cpu, int c);
extern int                fpu_get_type_from_index(const cpu_family_t *cpu_family,
                                                  int cpu, int c);

extern int                cpu_override_interpreter;

extern int                cpu_dyn_accurate_fpu_env;
extern int                cpu_dyn_block_link;

/* 1 if this build's recompiler can link blocks (codegen_block.c). */
extern const int          codegen_has_block_link;

#endif /*EMU_CPU_H*/
