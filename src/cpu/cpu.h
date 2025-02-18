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
    CPU_PKG_8088_EUROPC      = (1 << 1),
    CPU_PKG_8086             = (1 << 2),
    CPU_PKG_8086_MAZOVIA     = (1 << 3),
    CPU_PKG_188              = (1 << 4),
    CPU_PKG_186              = (1 << 5),
    CPU_PKG_286              = (1 << 6),
    CPU_PKG_386SX            = (1 << 7),
    CPU_PKG_386DX            = (1 << 8),
    CPU_PKG_386DX_DESKPRO386 = (1 << 9),
    CPU_PKG_M6117            = (1 << 10),
    CPU_PKG_386SLC_IBM       = (1 << 11),
    CPU_PKG_486SLC           = (1 << 12),
    CPU_PKG_486SLC_IBM       = (1 << 13),
    CPU_PKG_486BL            = (1 << 14),
    CPU_PKG_486DLC           = (1 << 15),
    CPU_PKG_SOCKET1          = (1 << 16),
    CPU_PKG_SOCKET3          = (1 << 17),
    CPU_PKG_SOCKET3_PC330    = (1 << 18),
    CPU_PKG_STPC             = (1 << 19),
    CPU_PKG_SOCKET4          = (1 << 20),
    CPU_PKG_SOCKET5_7        = (1 << 21),
    CPU_PKG_SOCKET8          = (1 << 22),
    CPU_PKG_SLOT1            = (1 << 23),
    CPU_PKG_SLOT2            = (1 << 24),
    CPU_PKG_SOCKET370        = (1 << 25)
};

#define CPU_SUPPORTS_DYNAREC 1
#define CPU_REQUIRES_DYNAREC 2
#define CPU_ALTERNATE_XTAL   4
#define CPU_FIXED_MULTIPLIER 8

#if (defined __amd64__ || defined _M_X64)
#    define LOOKUP_INV -1LL
#else
#    define LOOKUP_INV -1
#endif

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

#define C_FLAG     0x0001
#define P_FLAG     0x0004
#define A_FLAG     0x0010
#define Z_FLAG     0x0040
#define N_FLAG     0x0080
#define T_FLAG     0x0100
#define I_FLAG     0x0200
#define D_FLAG     0x0400
#define V_FLAG     0x0800
#define NT_FLAG    0x4000
#define MD_FLAG    0x8000

#define RF_FLAG    0x0001 /* in EFLAGS */
#define VM_FLAG    0x0002 /* in EFLAGS */
#define VIF_FLAG   0x0008 /* in EFLAGS */
#define VIP_FLAG   0x0010 /* in EFLAGS */
#define VID_FLAG   0x0020 /* in EFLAGS */

#define EM_FLAG    0x00004 /* in CR0 */
#define WP_FLAG    0x10000 /* in CR0 */

#define CR4_VME    (1 << 0) /* Virtual 8086 Mode Extensions */
#define CR4_PVI    (1 << 1) /* Protected-mode Virtual Interrupts */
#define CR4_TSD    (1 << 2) /* Time Stamp Disable */
#define CR4_DE     (1 << 3) /* Debugging Extensions */
#define CR4_PSE    (1 << 4) /* Page Size Extension */
#define CR4_PAE    (1 << 5) /* Physical Address Extension */
#define CR4_MCE    (1 << 6) /* Machine Check Exception */
#define CR4_PGE    (1 << 7) /* Page Global Enabled */
#define CR4_PCE    (1 << 8) /* Performance-Monitoring Counter enable */
#define CR4_OSFXSR (1 << 9) /* Operating system support for FXSAVE and FXRSTOR instructions */

#define CPL        ((cpu_state.seg_cs.access >> 5) & 3)

#define IOPL       ((cpu_state.flags >> 12) & 3)

#define IOPLp      ((!(msw & 1)) || (CPL <= IOPL))

typedef union {
    uint32_t l;
    uint16_t w;
    struct {
        uint8_t l;
        uint8_t h;
    } b;
} x86reg;

typedef struct {
    uint32_t base;
    uint32_t limit;
    uint8_t  access;
    uint8_t  ar_high;
    uint16_t seg;
    uint32_t limit_low;
    uint32_t limit_high;
    int      checked; /*Non-zero if selector is known to be valid*/
} x86seg;

typedef union {
    uint64_t q;
    int64_t  sq;
    uint32_t l[2];
    int32_t  sl[2];
    uint16_t w[4];
    int16_t  sw[4];
    uint8_t  b[8];
    int8_t   sb[8];
    float    f[2];
} MMX_REG;

typedef struct {
    /* IBM 386SLC/486SLC/486BL MSRs */
    uint64_t ibm_por;  /* 0x00001000 - 386SLC and later */
    uint64_t ibm_crcr; /* 0x00001001 - 386SLC and later */
    uint64_t ibm_por2; /* 0x00001002 - 486SLC and later */
    uint64_t ibm_pcr;  /* 0x00001004 - 486BL3 */

    /* IDT WinChip C6/2/VIA Cyrix III MSRs */
    uint32_t fcr;      /* 0x00000107 (IDT), 0x00001107 (VIA) */
    uint64_t fcr2;     /* 0x00000108 (IDT), 0x00001108 (VIA) */
    uint64_t fcr3;     /* 0x00000108 (IDT), 0x00001108 (VIA) */
    uint64_t mcr[8];   /* 0x00000110 - 0x00000117 (IDT) */
    uint32_t mcr_ctrl; /* 0x00000120 (IDT) */

    /* AMD K5/K6 MSRs */
    uint64_t amd_aar;    /* 0x00000082 - all K5 */
    uint64_t amd_hwcr;   /* 0x00000083 - all K5 and all K6 */
    uint64_t amd_watmcr; /* 0x00000085 - K5 Model 1 and later */
    uint64_t amd_wapmrr; /* 0x00000086 - K5 Model 1 and later */

    uint64_t amd_efer;  /* 0xc0000080 - all K5 and all K6 */
    uint64_t amd_star;  /* 0xc0000081 - K6-2 and later */
    uint64_t amd_whcr;  /* 0xc0000082 - all K5 and all K6 */
    uint64_t amd_uwccr; /* 0xc0000085 - K6-2C and later */
    uint64_t amd_epmr;  /* 0xc0000086 - K6-III+/2+ only */
    uint64_t amd_psor;  /* 0xc0000087 - K6-2C and later */
    uint64_t amd_pfir;  /* 0xc0000088 - K6-2C and later */
    uint64_t amd_l2aar; /* 0xc0000089 - K6-III and later */

    /* Pentium/Pentium MMX MSRs */
    uint64_t mcar;         /* 0x00000000 - also on K5 and (R/W) K6 */
    uint64_t mctr;         /* 0x00000001 - also on K5 and (R/W) K6 */
    uint32_t tr1;          /* 0x00000002 - also on WinChip C6/2 */
    uint32_t tr2;          /* 0x00000004 - reserved on PMMX */
    uint32_t tr3;          /* 0x00000005 */
    uint32_t tr4;          /* 0x00000006 */
    uint32_t tr5;          /* 0x00000007 */
    uint32_t tr6;          /* 0x00000008 */
    uint32_t tr7;          /* 0x00000009 */
    uint32_t tr9;          /* 0x0000000b */
    uint32_t tr10;         /* 0x0000000c */
    uint32_t tr11;         /* 0x0000000d */
    uint32_t tr12;         /* 0x0000000e - also on WinChip C6/2 and K6 */
    uint32_t cesr;         /* 0x00000011 - also on WinChip C6/2 and Cx6x86MX */
    uint64_t pmc[2];       /* 0x00000012, 0x00000013 - also on WinChip C6/2 and Cx6x86MX */
    uint32_t fp_last_xcpt; /* 0x8000001b - undocumented */
    uint32_t probe_ctl;    /* 0x8000001d - undocumented */
    uint32_t ecx8000001e;  /* 0x8000001e - undocumented */
    uint32_t ecx8000001f;  /* 0x8000001f - undocumented */

    /* Pentium Pro/II MSRs */
    uint64_t apic_base; /* 0x0000001b */
    uint32_t test_ctl;  /* 0x00000033 */
    uint64_t bios_updt; /* 0x00000079 */

    uint64_t bbl_cr_dx[4]; /* 0x00000088 - 0x0000008b */
    uint64_t perfctr[2];  /* 0x000000c1, 0x000000c2 */
    uint64_t mtrr_cap;     /* 0x000000fe */

    uint64_t bbl_cr_addr; /* 0x00000116 */
    uint64_t bbl_cr_decc; /* 0x00000118 */
    uint64_t bbl_cr_ctl;  /* 0x00000119 */
    uint64_t bbl_cr_trig; /* 0x0000011a */
    uint64_t bbl_cr_busy; /* 0x0000011b */
    uint64_t bbl_cr_ctl3; /* 0x0000011e */

    uint16_t sysenter_cs;  /* 0x00000174 - Pentium II and later */
    uint32_t sysenter_esp; /* 0x00000175 - Pentium II and later */
    uint32_t sysenter_eip; /* 0x00000176 - Pentium II and later */

    uint64_t mcg_ctl;           /* 0x0000017b */
    uint64_t evntsel[2];        /* 0x00000186, 0x00000187 */

    uint32_t debug_ctl;         /* 0x000001d9 */
    uint32_t rob_cr_bkuptmpdr6; /* 0x000001e0 */

    /* MTTR-related MSRs also present on the VIA Cyrix III */
    uint64_t mtrr_physbase[8]; /* 0x00000200 - 0x0000020f (ECX & 0) */
    uint64_t mtrr_physmask[8]; /* 0x00000200 - 0x0000020f (ECX & 1) */
    uint64_t mtrr_fix64k_8000; /* 0x00000250 */
    uint64_t mtrr_fix16k_8000; /* 0x00000258 */
    uint64_t mtrr_fix16k_a000; /* 0x00000259 */
    uint64_t mtrr_fix4k[8];    /* 0x00000268 - 0x0000026f */
    uint64_t mtrr_deftype;     /* 0x000002ff */

    uint64_t pat;        /* 0x00000277 - Pentium II Deschutes and later */
    uint64_t mca_ctl[5]; /* 0x00000400, 0x00000404, 0x00000408, 0x0000040c, 0x00000410 */
    uint64_t ecx570;     /* 0x00000570 */

    /* Other/Unclassified MSRs */
    uint64_t ecx20; /* 0x00000020, really 0x40000020, but we filter out the top 18 bits
                       like a real Deschutes does. */
} msr_t;

typedef struct {
    x86reg regs[8];

    uint8_t tag[8];

    x86seg  *ea_seg;
    uint32_t eaaddr;

    int      flags_op;
    uint32_t flags_res;
    uint32_t flags_op1;
    uint32_t flags_op2;

    uint32_t pc;
    uint32_t oldpc;
    uint32_t op32;

    int TOP;

    union {
        struct {
            int8_t rm;
            int8_t mod;
            int8_t reg;
        } rm_mod_reg;
        int32_t rm_mod_reg_data;
    } rm_data;

    uint8_t ssegs;
    uint8_t ismmx;
    uint8_t abrt;
    uint8_t _smi_line;

    int _cycles;
#ifdef FPU_CYCLES
    int _fpu_cycles;
#endif
    int _in_smm;

    uint16_t npxs;
    uint16_t npxc;

    double ST[8];

    uint16_t MM_w4[8];

    MMX_REG MM[8];

#ifdef USE_NEW_DYNAREC
#    if defined(__APPLE__) && defined(__aarch64__)
    uint64_t old_fp_control;
    uint64_t new_fp_control;
#    else
    uint32_t old_fp_control;
    uint32_t new_fp_control;
#    endif
#    if defined i386 || defined __i386 || defined __i386__ || defined _X86_ || defined _M_IX86
    uint16_t old_fp_control2;
    uint16_t new_fp_control2;
#    endif
#    if defined i386 || defined __i386 || defined __i386__ || defined _X86_ || defined _M_IX86 || defined __amd64__ || defined _M_X64
    uint32_t trunc_fp_control;
#    endif
#else
    uint16_t old_npxc;
    uint16_t new_npxc;
#endif

    x86seg seg_cs;
    x86seg seg_ds;
    x86seg seg_es;
    x86seg seg_ss;
    x86seg seg_fs;
    x86seg seg_gs;

    union {
        uint32_t l;
        uint16_t w;
    } CR0;

    uint16_t flags;
    uint16_t eflags;

    uint32_t _smbase;
} cpu_state_t;

#define in_smm   cpu_state._in_smm
#define smi_line cpu_state._smi_line

#define smbase   cpu_state._smbase

/*The cpu_state.flags below must match in both cpu_cur_status and block->status for a block
  to be valid*/
#define CPU_STATUS_USE32   (1 << 0)
#define CPU_STATUS_STACK32 (1 << 1)
#define CPU_STATUS_PMODE   (1 << 2)
#define CPU_STATUS_V86     (1 << 3)
#define CPU_STATUS_SMM     (1 << 4)
#ifdef USE_NEW_DYNAREC
#    define CPU_STATUS_FLAGS 0xff
#else
#    define CPU_STATUS_FLAGS 0xffff
#endif

/*If the cpu_state.flags below are set in cpu_cur_status, they must be set in block->status.
  Otherwise they are ignored*/
#ifdef USE_NEW_DYNAREC
#    define CPU_STATUS_NOTFLATDS (1 << 8)
#    define CPU_STATUS_NOTFLATSS (1 << 9)
#    define CPU_STATUS_MASK      0xff00
#else
#    define CPU_STATUS_NOTFLATDS (1 << 16)
#    define CPU_STATUS_NOTFLATSS (1 << 17)
#    define CPU_STATUS_MASK      0xffff0000
#endif

#ifdef _MSC_VER
#    define COMPILE_TIME_ASSERT(expr) /*nada*/
#else
#    ifdef EXTREME_DEBUG
#        define COMPILE_TIME_ASSERT(expr) typedef char COMP_TIME_ASSERT[(expr) ? 1 : 0];
#    else
#        define COMPILE_TIME_ASSERT(expr) /*nada*/
#    endif
#endif

COMPILE_TIME_ASSERT(sizeof(cpu_state_t) <= 128)

#define cpu_state_offset(MEMBER) ((uint8_t) ((uintptr_t) &cpu_state.MEMBER - (uintptr_t) &cpu_state - 128))

#define EAX                      cpu_state.regs[0].l
#define AX                       cpu_state.regs[0].w
#define AL                       cpu_state.regs[0].b.l
#define AH                       cpu_state.regs[0].b.h
#define ECX                      cpu_state.regs[1].l
#define CX                       cpu_state.regs[1].w
#define CL                       cpu_state.regs[1].b.l
#define CH                       cpu_state.regs[1].b.h
#define EDX                      cpu_state.regs[2].l
#define DX                       cpu_state.regs[2].w
#define DL                       cpu_state.regs[2].b.l
#define DH                       cpu_state.regs[2].b.h
#define EBX                      cpu_state.regs[3].l
#define BX                       cpu_state.regs[3].w
#define BL                       cpu_state.regs[3].b.l
#define BH                       cpu_state.regs[3].b.h
#define ESP                      cpu_state.regs[4].l
#define EBP                      cpu_state.regs[5].l
#define ESI                      cpu_state.regs[6].l
#define EDI                      cpu_state.regs[7].l
#define SP                       cpu_state.regs[4].w
#define BP                       cpu_state.regs[5].w
#define SI                       cpu_state.regs[6].w
#define DI                       cpu_state.regs[7].w

#define cycles                   cpu_state._cycles
#ifdef FPU_CYCLES
#    define fpu_cycles cpu_state._fpu_cycles
#endif

#define cpu_rm  cpu_state.rm_data.rm_mod_reg.rm
#define cpu_mod cpu_state.rm_data.rm_mod_reg.mod
#define cpu_reg cpu_state.rm_data.rm_mod_reg.reg

/* Global variables. */
extern cpu_state_t cpu_state;

extern const cpu_family_t         cpu_families[];
extern cpu_family_t              *cpu_f;
extern CPU                       *cpu_s;
extern int                        cpu_override;

extern int    cpu_isintel;
extern int    cpu_iscyrix;
extern int    cpu_16bitbus;
extern int    cpu_64bitbus;
extern int    cpu_pci_speed;
extern int    cpu_multi;
extern double cpu_dmulti;
extern double fpu_multi;
extern double cpu_busspeed;
extern int    cpu_cyrix_alignment; /* Cyrix 5x86/6x86 only has data misalignment
                                      penalties when crossing 8-byte boundaries. */
extern int    cpu_cpurst_on_sr;    /* SiS 551x and 5571: Issue CPURST on soft reset. */

extern int is8086;
extern int is186;
extern int is286;
extern int is386;
extern int is6117;
extern int is486;
extern int is_am486;
extern int is_am486dxl;
extern int is_pentium;
extern int is_k5;
extern int is_k6;
extern int is_p6;
extern int is_cxsmm;
extern int hascache;
extern int isibm486;
extern int is_mazovia;
extern int is_nec;
extern int is_rapidcad;
extern int hasfpu;
#define CPU_FEATURE_RDTSC   (1 << 0)
#define CPU_FEATURE_MSR     (1 << 1)
#define CPU_FEATURE_MMX     (1 << 2)
#define CPU_FEATURE_CR4     (1 << 3)
#define CPU_FEATURE_VME     (1 << 4)
#define CPU_FEATURE_CX8     (1 << 5)
#define CPU_FEATURE_3DNOW   (1 << 6)
#define CPU_FEATURE_SYSCALL (1 << 7)
#define CPU_FEATURE_3DNOWE  (1 << 8)
#define CPU_FEATURE_PSE36   (1 << 9)

extern uint32_t cpu_features;

extern int smi_latched;
extern int smm_in_hlt;
extern int smi_block;

#ifdef USE_NEW_DYNAREC
extern uint16_t cpu_cur_status;
#else
extern uint32_t cpu_cur_status;
#endif
extern uint64_t cpu_CR4_mask;
extern uint64_t tsc;
extern msr_t    msr;
extern uint8_t  opcode;
extern int      cpl_override;
extern int      CPUID;
extern uint64_t xt_cpu_multi;
extern int      isa_cycles;
extern int      cpu_inited;
extern uint32_t oldds;
extern uint32_t oldss;
extern uint32_t olddslimit;
extern uint32_t oldsslimit;
extern uint32_t olddslimitw;
extern uint32_t oldsslimitw;
extern uint32_t pccache;
extern uint8_t *pccache2;

extern double   bus_timing;
extern double   isa_timing;
extern double   pci_timing;
extern double   agp_timing;
extern uint16_t temp_seg_data[4];
extern uint16_t cs_msr;
extern uint32_t esp_msr;
extern uint32_t eip_msr;

/* For the AMD K6. */
extern uint64_t amd_efer;
extern uint64_t star;

#define cr0                  cpu_state.CR0.l
#define msw                  cpu_state.CR0.w
extern uint32_t cr2;
extern uint32_t cr3;
extern uint32_t cr4;
extern uint32_t dr[8];
extern uint32_t _tr[8];
extern uint32_t cache_index;
extern uint8_t  _cache[2048];

/*Segments -
  _cs,_ds,_es,_ss are the segment structures
  CS,DS,ES,SS is the 16-bit data
  cs,ds,es,ss are defines to the bases*/
extern x86seg gdt;
extern x86seg ldt;
extern x86seg idt;
extern x86seg tr;
extern x86seg _oldds;
#define CS            cpu_state.seg_cs.seg
#define DS            cpu_state.seg_ds.seg
#define ES            cpu_state.seg_es.seg
#define SS            cpu_state.seg_ss.seg
#define FS            cpu_state.seg_fs.seg
#define GS            cpu_state.seg_gs.seg
#define cs            cpu_state.seg_cs.base
#define ds            cpu_state.seg_ds.base
#define es            cpu_state.seg_es.base
#define ss            cpu_state.seg_ss.base
#define fs_seg        cpu_state.seg_fs.base
#define gs            cpu_state.seg_gs.base

#define ISA_CYCLES(x) (x * isa_cycles)

extern int cpu_cycles_read;
extern int cpu_cycles_read_l;
extern int cpu_cycles_write;
extern int cpu_cycles_write_l;
extern int cpu_prefetch_cycles;
extern int cpu_prefetch_width;
extern int cpu_mem_prefetch_cycles;
extern int cpu_rom_prefetch_cycles;
extern int cpu_waitstates;
extern int cpu_flush_pending;
extern int cpu_old_paging;
extern int cpu_cache_int_enabled;
extern int cpu_cache_ext_enabled;
extern int cpu_isa_speed;
extern int cpu_pci_speed;
extern int cpu_agp_speed;

extern int timing_rr;
extern int timing_mr;
extern int timing_mrl;
extern int timing_rm;
extern int timing_rml;
extern int timing_mm;
extern int timing_mml;
extern int timing_bt;
extern int timing_bnt;
extern int timing_int;
extern int timing_int_rm;
extern int timing_int_v86;
extern int timing_int_pm;
extern int timing_int_pm_outer;
extern int timing_iret_rm;
extern int timing_iret_v86;
extern int timing_iret_pm;
extern int timing_iret_pm_outer;
extern int timing_call_rm;
extern int timing_call_pm;
extern int timing_call_pm_gate;
extern int timing_call_pm_gate_inner;
extern int timing_retf_rm;
extern int timing_retf_pm;
extern int timing_retf_pm_outer;
extern int timing_jmp_rm;
extern int timing_jmp_pm;
extern int timing_jmp_pm_gate;
extern int timing_misaligned;

extern int      in_sys;
extern int      unmask_a20_in_smm;
extern int      cycles_main;
extern uint32_t old_rammask;

#ifdef USE_ACYCS
extern int acycs;
#endif
extern int pic_pending;
extern int is_vpc;
extern int soft_reset_mask;
extern int alt_access;
extern int cpu_end_block_after_ins;

extern uint16_t cpu_fast_off_count;
extern uint16_t cpu_fast_off_val;
extern uint32_t cpu_fast_off_flags;

/* Functions. */
extern int cpu_has_feature(int feature);

extern char *cpu_current_pc(char *bufp);

extern void cpu_update_waitstates(void);
extern void cpu_set(void);
extern void cpu_close(void);
extern void cpu_set_isa_speed(int speed);
extern void cpu_set_pci_speed(int speed);
extern void cpu_set_isa_pci_div(int div);
extern void cpu_set_agp_speed(int speed);

extern void cpu_CPUID(void);
extern void cpu_RDMSR(void);
extern void cpu_WRMSR(void);

extern int  checkio(uint32_t port, int mask);
extern void codegen_block_end(void);
extern void codegen_reset(void);
extern void cpu_set_edx(void);
extern int  divl(uint32_t val);
extern void execx86(int32_t cycs);
extern void enter_smm(int in_hlt);
extern void enter_smm_check(int in_hlt);
extern void leave_smm(void);
extern void exec386_2386(int32_t cycs);
extern void exec386(int32_t cycs);
extern void exec386_dynarec(int32_t cycs);
extern int  idivl(int32_t val);
extern void resetmcr(void);
extern void resetx86(void);
extern void refreshread(void);
extern void resetreadlookup(void);
extern void softresetx86(void);
extern void hardresetx86(void);
extern void x86_int(int num);
extern void x86_int_sw(int num);
extern int  x86_int_sw_rm(int num);

#ifdef ENABLE_808X_LOG
extern void dumpregs(int __force);
extern void x87_dumpregs(void);
extern void x87_reset(void);
#endif

extern int  cpu_effective;
extern int  cpu_alt_reset;
extern void cpu_dynamic_switch(int new_cpu);

extern void cpu_ven_reset(void);
extern void update_tsc(void);

extern int sysenter(uint32_t fetchdat);
extern int sysexit(uint32_t fetchdat);
extern int syscall_op(uint32_t fetchdat);
extern int sysret(uint32_t fetchdat);

extern cpu_family_t *cpu_get_family(const char *internal_name);
extern uint8_t       cpu_is_eligible(const cpu_family_t *cpu_family, int cpu, int machine);
extern uint8_t       cpu_family_is_eligible(const cpu_family_t *cpu_family, int machine);
extern int           fpu_get_type(const cpu_family_t *cpu_family, int cpu, const char *internal_name);
extern const char   *fpu_get_internal_name(const cpu_family_t *cpu_family, int cpu, int type);
extern const char   *fpu_get_name_from_index(const cpu_family_t *cpu_family, int cpu, int c);
extern int           fpu_get_type_from_index(const cpu_family_t *cpu_family, int cpu, int c);

void cyrix_load_seg_descriptor(uint32_t addr, x86seg *seg);
void cyrix_write_seg_descriptor(uint32_t addr, x86seg *seg);

#define SMHR_VALID     (1 << 0)
#define SMHR_ADDR_MASK (0xfffffffc)

typedef union {
    uint32_t fd;
    uint8_t  b[4];
} fetch_dat_t;

typedef struct {
    struct {
        uint32_t base;
        uint64_t size;
    } arr[8];
    uint32_t smhr;
} cyrix_t;

extern uint32_t addr64;
extern uint32_t addr64_2;
extern uint32_t addr64a[8];
extern uint32_t addr64a_2[8];

extern int soft_reset_pci;

extern int reset_on_hlt;
extern int hlt_reset_pending;

extern cyrix_t cyrix;

extern int prefetch_prefixes;
extern int cpu_use_exec;

extern uint8_t  use_custom_nmi_vector;
extern uint32_t custom_nmi_vector;

extern void (*cpu_exec)(int32_t cycs);
extern uint8_t do_translate;
extern uint8_t do_translate2;

extern void SF_FPU_reset(void);

extern void reset_808x(int hard);
extern void interrupt_808x(uint16_t addr);

extern void cpu_register_fast_off_handler(void *timer);
extern void cpu_fast_off_advance(void);
extern void cpu_fast_off_period_set(uint16_t vla, double period);
extern void cpu_fast_off_reset(void);

extern void smi_raise(void);
extern void nmi_raise(void);

extern MMX_REG  *MMP[8];
extern uint16_t *MMEP[8];

extern int  cpu_block_end;
extern int  cpu_override_dynarec;

extern void mmx_init(void);
extern void prefetch_flush(void);

extern void prefetch_run(int instr_cycles, int bytes, int modrm, int reads, int reads_l, int writes, int writes_l, int ea32);

extern int lock_legal[256];
extern int lock_legal_0f[256];
extern int lock_legal_ba[8];
extern int lock_legal_80[8];
extern int lock_legal_f6[8];
extern int lock_legal_fe[8];

extern int new_ne;

extern int in_lock;
extern int cpu_override_interpreter;

extern int is_lock_legal(uint32_t fetchdat);

extern void     prefetch_queue_set_pos(int pos);
extern void     prefetch_queue_set_ip(uint16_t ip);
extern void     prefetch_queue_set_prefetching(int p);
extern int      prefetch_queue_get_pos(void);
extern uint16_t prefetch_queue_get_ip(void);
extern int      prefetch_queue_get_prefetching(void);
extern int      prefetch_queue_get_size(void);

#define prefetch_queue_set_suspended(s) prefetch_queue_set_prefetching(!s)
#define prefetch_queue_get_suspended !prefetch_queue_get_prefetching

#endif /*EMU_CPU_H*/
