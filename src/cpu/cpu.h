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
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		leilei,
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2018 leilei.
 *		Copyright 2016-2020 Miran Grca.
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
    CPU_V20,   /* NEC 808x class CPUs */
    CPU_V30,
    CPU_188,   /* 18x class CPUs */
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
    CPU_PKG_8088          = (1 << 0),
    CPU_PKG_8088_EUROPC   = (1 << 1),
    CPU_PKG_8086          = (1 << 2),
    CPU_PKG_188           = (1 << 3),
    CPU_PKG_186           = (1 << 4),
    CPU_PKG_286           = (1 << 5),
    CPU_PKG_386SX         = (1 << 6),
    CPU_PKG_386DX         = (1 << 7),
    CPU_PKG_M6117         = (1 << 8),
    CPU_PKG_386SLC_IBM    = (1 << 9),
    CPU_PKG_486SLC        = (1 << 10),
    CPU_PKG_486SLC_IBM    = (1 << 11),
    CPU_PKG_486BL         = (1 << 12),
    CPU_PKG_486DLC        = (1 << 13),
    CPU_PKG_SOCKET1       = (1 << 14),
    CPU_PKG_SOCKET3       = (1 << 15),
    CPU_PKG_SOCKET3_PC330 = (1 << 16),
    CPU_PKG_STPC          = (1 << 17),
    CPU_PKG_SOCKET4       = (1 << 18),
    CPU_PKG_SOCKET5_7     = (1 << 19),
    CPU_PKG_SOCKET8       = (1 << 20),
    CPU_PKG_SLOT1         = (1 << 21),
    CPU_PKG_SLOT2         = (1 << 22),
    CPU_PKG_SLOTA         = (1 << 23),
    CPU_PKG_SOCKET370     = (1 << 24),
    CPU_PKG_SOCKETA       = (1 << 25),
    CPU_PKG_EBGA368       = (1 << 26)
};

#define MANU_INTEL           0
#define MANU_AMD             1
#define MANU_CYRIX           2
#define MANU_IDT             3
#define MANU_NEC             4
#define MANU_IBM             5

#define CPU_SUPPORTS_DYNAREC 1
#define CPU_REQUIRES_DYNAREC 2
#define CPU_ALTERNATE_XTAL   4
#define CPU_FIXED_MULTIPLIER 8

#if (defined __amd64__ || defined _M_X64)
#    define LOOKUP_INV -1LL
#else
#    define LOOKUP_INV -1
#endif

typedef struct {
    const char *name;
    const char *internal_name;
    const int   type;
} FPU;

typedef struct {
    const char *name;
    uint64_t    cpu_type;
    const FPU  *fpus;
    int         rspeed;
    double      multi;
    uint16_t    voltage;
    uint32_t    edx_reset;
    uint32_t    cpuid_model;
    uint16_t    cyrix_id;
    uint8_t     cpu_flags;
    int8_t      mem_read_cycles, mem_write_cycles;
    int8_t      cache_read_cycles, cache_write_cycles;
    int8_t      atclk_div;
} CPU;

typedef struct {
    const uint32_t package;
    const char    *manufacturer;
    const char    *name;
    const char    *internal_name;
    const CPU     *cpus;
} cpu_family_t;

typedef struct {
    const char  *family;
    const int    rspeed;
    const double multi;
} cpu_legacy_table_t;

typedef struct {
    const char                *machine;
    const cpu_legacy_table_t **tables;
} cpu_legacy_machine_t;

#define C_FLAG   0x0001
#define P_FLAG   0x0004
#define A_FLAG   0x0010
#define Z_FLAG   0x0040
#define N_FLAG   0x0080
#define T_FLAG   0x0100
#define I_FLAG   0x0200
#define D_FLAG   0x0400
#define V_FLAG   0x0800
#define NT_FLAG  0x4000
#define MD_FLAG  0x8000

#define RF_FLAG  0x0001 /* in EFLAGS */
#define VM_FLAG  0x0002 /* in EFLAGS */
#define VIF_FLAG 0x0008 /* in EFLAGS */
#define VIP_FLAG 0x0010 /* in EFLAGS */
#define VID_FLAG 0x0020 /* in EFLAGS */

#define WP_FLAG  0x10000 /* in CR0 */
#define CR4_VME  (1 << 0)
#define CR4_PVI  (1 << 1)
#define CR4_PSE  (1 << 4)
#define CR4_PAE  (1 << 5)

#define CPL      ((cpu_state.seg_cs.access >> 5) & 3)

#define IOPL     ((cpu_state.flags >> 12) & 3)

#define IOPLp    ((!(msw & 1)) || (CPL <= IOPL))

typedef union {
    uint32_t l;
    uint16_t w;
    struct {
        uint8_t l,
            h;
    } b;
} x86reg;

typedef struct {
    uint32_t base;
    uint32_t limit;
    uint8_t  access, ar_high;
    uint16_t seg;
    uint32_t limit_low, limit_high;
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
    /* IDT WinChip and WinChip 2 MSR's */
    uint32_t tr1, tr12; /* 0x00000002, 0x0000000e */
    uint32_t cesr;      /* 0x00000011 */

    /* Pentium Pro, Pentium II Klamath, and Pentium II Deschutes MSR's */
    uint64_t apic_base; /* 0x0000001b - Should the Pentium not also have this? */
    uint64_t ecx79;     /* 0x00000079 */

    /* AMD K5, 5k86, K6, K6-2, K6-2C, K6-3, K6-2P, and K6-3P MSR's */
    uint64_t ecx83; /* 0x00000083 - AMD K5 and K6 MSR's. */

    /* Pentium Pro, Pentium II Klamath, and Pentium II Deschutes MSR's */
    uint64_t ecx8x[4];    /* 0x00000088 - 0x0000008b */
    uint64_t ia32_pmc[8]; /* 0x000000c1 - 0x000000c8 */
    uint64_t mtrr_cap;    /* 0x000000fe */

    /* IDT WinChip and WinChip 2 MSR's that are also on the VIA Cyrix III */
    uint32_t fcr;        /* 0x00000107 (IDT), 0x00001107 (VIA) */
    uint64_t fcr2, fcr3; /* 0x00000108 (IDT), 0x00001108 (VIA) */

    /* Pentium Pro, Pentium II Klamath, and Pentium II Deschutes MSR's */
    uint64_t ecx116;    /* 0x00000116 */
    uint64_t ecx11x[4]; /* 0x00000118 - 0x0000011b */
    uint64_t ecx11e;    /* 0x0000011e */

    /* Pentium II Klamath and Pentium II Deschutes MSR's */
    uint16_t sysenter_cs;  /* 0x00000174 - SYSENTER/SYSEXIT MSR's */
    uint32_t sysenter_esp; /* 0x00000175 - SYSENTER/SYSEXIT MSR's */
    uint32_t sysenter_eip; /* 0x00000176 - SYSENTER/SYSEXIT MSR's */

    /* Pentium Pro, Pentium II Klamath, and Pentium II Deschutes MSR's */
    uint64_t mcg_ctl; /* 0x0000017b - Machine Check Architecture */

    /* Pentium Pro, Pentium II Klamath, and Pentium II Deschutes MSR's */
    uint64_t ecx186, ecx187; /* 0x00000186, 0x00000187 */
    uint64_t ecx1e0;         /* 0x000001e0 */

    /* Pentium Pro, Pentium II Klamath, and Pentium II Deschutes MSR's that are also
       on the VIA Cyrix III */
    uint64_t mtrr_physbase[8]; /* 0x00000200 - 0x0000020f */
    uint64_t mtrr_physmask[8]; /* 0x00000200 - 0x0000020f (ECX & 1) */
    uint64_t mtrr_fix64k_8000; /* 0x00000250 */
    uint64_t mtrr_fix16k_8000; /* 0x00000258 */
    uint64_t mtrr_fix16k_a000; /* 0x00000259 */
    uint64_t mtrr_fix4k[8];    /* 0x00000268 - 0x0000026f */

    /* Pentium Pro, Pentium II Klamath, and Pentium II Deschutes MSR's */
    uint64_t pat; /* 0x00000277 */

    /* Pentium Pro, Pentium II Klamath, and Pentium II Deschutes MSR's that are also
       on the VIA Cyrix III */
    uint64_t mtrr_deftype; /* 0x000002ff */

    /* Pentium Pro, Pentium II Klamath, and Pentium II Deschutes MSR's */
    uint64_t mca_ctl[5]; /* 0x00000400, 0x00000404, 0x00000408, 0x0000040c, 0x00000410 - Machine Check Architecture */
    uint64_t ecx570;     /* 0x00000570 */

    /* IBM 386SLC, 486SLC, and 486BL MSR's */
    uint64_t ibm_por;  /* 0x00001000 - Processor Operation Register */
    uint64_t ibm_crcr; /* 0x00001001 - Cache Region Control Register */

    /* IBM 486SLC and 486BL MSR's */
    uint64_t ibm_por2; /* 0x00001002 - Processor Operation Register */

    /* Pentium Pro, Pentium II Klamath, and Pentium II Deschutes MSR's */
    uint64_t ecx1002ff; /* 0x001002ff - MSR used by some Intel AMI boards */

    /* AMD K5, 5k86, K6, K6-2, K6-2C, K6-3, K6-2P, and K6-3P MSR's */
    uint64_t amd_efer; /* 0xc0000080 */

    /* AMD K6-2, K6-2C, K6-3, K6-2P, and K6-3P MSR's */
    uint64_t star; /* 0xc0000081 */

    /* AMD K5, 5k86, K6, K6-2, K6-2C, K6-3, K6-2P, and K6-3P MSR's */
    uint64_t amd_whcr; /* 0xc0000082 */

    /* AMD K6-2C, K6-3, K6-2P, and K6-3P MSR's */
    uint64_t amd_uwccr; /* 0xc0000085 */

    /* AMD K6-2P and K6-3P MSR's */
    uint64_t amd_epmr; /* 0xc0000086 */

    /* AMD K6-2C, K6-3, K6-2P, and K6-3P MSR's */
    uint64_t amd_psor, amd_pfir; /* 0xc0000087, 0xc0000088 */

    /* K6-3, K6-2P, and K6-3P MSR's */
    uint64_t amd_l2aar; /* 0xc0000089 */

    /* Pentium Pro, Pentium II Klamath, and Pentium II Deschutes MSR's */
    uint64_t ecxf0f00250; /* 0xf0f00250 - Some weird long MSR's used by i686 AMI & some Phoenix BIOSes */
    uint64_t ecxf0f00258; /* 0xf0f00258 */
    uint64_t ecxf0f00259; /* 0xf0f00259 */
} msr_t;

typedef struct {
    x86reg regs[8];

    uint8_t tag[8];

    x86seg  *ea_seg;
    uint32_t eaaddr;

    int      flags_op;
    uint32_t flags_res,
        flags_op1, flags_op2;

    uint32_t pc,
        oldpc, op32;

    int TOP;

    union {
        struct {
            int8_t rm,
                mod,
                reg;
        } rm_mod_reg;
        int32_t rm_mod_reg_data;
    } rm_data;

    uint8_t ssegs, ismmx,
        abrt, _smi_line;

#ifdef FPU_CYCLES
    int _cycles, _fpu_cycles, _in_smm;
#else
    int      _cycles, _in_smm;
#endif

    uint16_t npxs, npxc;

    double ST[8];

    uint16_t MM_w4[8];

    MMX_REG MM[8];

#ifdef USE_NEW_DYNAREC
    uint32_t    old_fp_control, new_fp_control;
#    if defined i386 || defined __i386 || defined __i386__ || defined _X86_ || defined _M_IX86
    uint16_t                                                                           old_fp_control2, new_fp_control2;
#    endif
#    if defined i386 || defined __i386 || defined __i386__ || defined _X86_ || defined _M_IX86 || defined __amd64__ || defined _M_X64
    uint32_t                                                                                                                   trunc_fp_control;
#    endif
#else
    uint16_t old_npxc, new_npxc;
#endif

    x86seg seg_cs, seg_ds, seg_es, seg_ss,
        seg_fs, seg_gs;

    union {
        uint32_t l;
        uint16_t w;
    } CR0;

    uint16_t flags, eflags;

    uint32_t _smbase;

    uint8_t inside_emulation_mode;
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
#define CPU_STATUS_FLAGS   0xffff

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

#define cpu_rm     cpu_state.rm_data.rm_mod_reg.rm
#define cpu_mod    cpu_state.rm_data.rm_mod_reg.mod
#define cpu_reg    cpu_state.rm_data.rm_mod_reg.reg

#define CR4_TSD    (1 << 2)
#define CR4_DE     (1 << 3)
#define CR4_MCE    (1 << 6)
#define CR4_PCE    (1 << 8)
#define CR4_OSFXSR (1 << 9)

/* Global variables. */
extern cpu_state_t cpu_state;

extern const cpu_family_t         cpu_families[];
extern const cpu_legacy_machine_t cpu_legacy_table[];
extern cpu_family_t              *cpu_f;
extern CPU                       *cpu_s;
extern int                        cpu_override;

extern int    cpu_isintel;
extern int    cpu_iscyrix;
extern int    cpu_16bitbus, cpu_64bitbus;
extern int    cpu_busspeed, cpu_pci_speed;
extern int    cpu_multi;
extern double cpu_dmulti;
extern double fpu_multi;
extern int    cpu_cyrix_alignment; /*Cyrix 5x86/6x86 only has data misalignment
                                     penalties when crossing 8-byte boundaries*/

extern int is8086, is186, is286, is386, is6117, is486;
extern int is_am486, is_am486dxl, is_pentium, is_k5, is_k6, is_p6, is_cxsmm;
extern int hascache;
extern int isibm486;
extern int is_nec;
extern int is_rapidcad;
extern int hasfpu;
#define CPU_FEATURE_RDTSC (1 << 0)
#define CPU_FEATURE_MSR   (1 << 1)
#define CPU_FEATURE_MMX   (1 << 2)
#define CPU_FEATURE_CR4   (1 << 3)
#define CPU_FEATURE_VME   (1 << 4)
#define CPU_FEATURE_CX8   (1 << 5)
#define CPU_FEATURE_3DNOW (1 << 6)
#define CPU_FEATURE_SYSCALL (1 << 7)

extern uint32_t cpu_features;

extern int smi_latched, smm_in_hlt;
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
extern int      cgate16;
extern int      cpl_override;
extern int      CPUID;
extern uint64_t xt_cpu_multi;
extern int      isa_cycles, cpu_inited;
extern uint32_t oldds, oldss, olddslimit, oldsslimit, olddslimitw, oldsslimitw;
extern uint32_t pccache;
extern uint8_t *pccache2;

extern double   bus_timing, isa_timing, pci_timing, agp_timing;
extern uint64_t pmc[2];
extern uint16_t temp_seg_data[4];
extern uint16_t cs_msr;
extern uint32_t esp_msr;
extern uint32_t eip_msr;

/* For the AMD K6. */
extern uint64_t amd_efer, star;

#define FPU_CW_Reserved_Bits (0xe0c0)

#define cr0                  cpu_state.CR0.l
#define msw                  cpu_state.CR0.w
extern uint32_t cr2, cr3, cr4;
extern uint32_t dr[8];
extern uint32_t _tr[8];
extern uint32_t cache_index;
extern uint8_t  _cache[2048];

/*Segments -
  _cs,_ds,_es,_ss are the segment structures
  CS,DS,ES,SS is the 16-bit data
  cs,ds,es,ss are defines to the bases*/
extern x86seg gdt, ldt, idt, tr;
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

extern int cpu_cycles_read, cpu_cycles_read_l, cpu_cycles_write, cpu_cycles_write_l;
extern int cpu_prefetch_cycles, cpu_prefetch_width, cpu_mem_prefetch_cycles, cpu_rom_prefetch_cycles;
extern int cpu_waitstates;
extern int cpu_cache_int_enabled, cpu_cache_ext_enabled;
extern int cpu_isa_speed, cpu_pci_speed, cpu_agp_speed;

extern int timing_rr;
extern int timing_mr, timing_mrl;
extern int timing_rm, timing_rml;
extern int timing_mm, timing_mml;
extern int timing_bt, timing_bnt;
extern int timing_int, timing_int_rm, timing_int_v86, timing_int_pm;
extern int timing_int_pm_outer, timing_iret_rm, timing_iret_v86, timing_iret_pm;
extern int timing_iret_pm_outer, timing_call_rm, timing_call_pm;
extern int timing_call_pm_gate, timing_call_pm_gate_inner;
extern int timing_retf_rm, timing_retf_pm, timing_retf_pm_outer;
extern int timing_jmp_rm, timing_jmp_pm, timing_jmp_pm_gate;
extern int timing_misaligned;

extern int      in_sys, unmask_a20_in_smm;
extern int      cycles_main;
extern uint32_t old_rammask;

#ifdef USE_ACYCS
extern int acycs;
#endif
extern int pic_pending, is_vpc;
extern int soft_reset_mask, alt_access;
extern int cpu_end_block_after_ins;

extern uint16_t cpu_fast_off_count, cpu_fast_off_val;
extern uint32_t cpu_fast_off_flags;

/* Functions. */
extern int cpu_has_feature(int feature);

#ifdef USE_NEW_DYNAREC
extern void loadseg_dynarec(uint16_t seg, x86seg *s);
extern int  loadseg(uint16_t seg, x86seg *s);
extern void loadcs(uint16_t seg);
#else
extern void loadseg(uint16_t seg, x86seg *s);
extern void loadcs(uint16_t seg);
#endif

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

extern int  checkio(uint32_t port);
extern void codegen_block_end(void);
extern void codegen_reset(void);
extern void cpu_set_edx(void);
extern int  divl(uint32_t val);
extern void execx86(int cycs);
extern void enter_smm(int in_hlt);
extern void enter_smm_check(int in_hlt);
extern void leave_smm(void);
extern void exec386(int cycs);
extern void exec386_dynarec(int cycs);
extern int  idivl(int32_t val);
#ifdef USE_NEW_DYNAREC
extern void loadcscall(uint16_t seg, uint32_t old_pc);
extern void loadcsjmp(uint16_t seg, uint32_t old_pc);
extern void pmodeint(int num, int soft);
extern void pmoderetf(int is32, uint16_t off);
extern void pmodeiret(int is32);
#else
extern void loadcscall(uint16_t seg);
extern void loadcsjmp(uint16_t seg, uint32_t old_pc);
extern void pmodeint(int num, int soft);
extern void pmoderetf(int is32, uint16_t off);
extern void pmodeiret(int is32);
#endif
extern void resetmcr(void);
extern void resetx86(void);
extern void refreshread(void);
extern void resetreadlookup(void);
extern void softresetx86(void);
extern void hardresetx86(void);
extern void x86_int(int num);
extern void x86_int_sw(int num);
extern int  x86_int_sw_rm(int num);
extern void x86de(char *s, uint16_t error);
extern void x86gpf(char *s, uint16_t error);
extern void x86np(char *s, uint16_t error);
extern void x86ss(char *s, uint16_t error);
extern void x86ts(char *s, uint16_t error);

#ifdef ENABLE_808X_LOG
extern void dumpregs(int __force);
extern void x87_dumpregs(void);
extern void x87_reset(void);
#endif

extern int  cpu_effective, cpu_alt_reset;
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

typedef struct
{
    struct
    {
        uint32_t base;
        uint64_t size;
    } arr[8];
    uint32_t smhr;
} cyrix_t;

extern uint32_t addr64, addr64_2;
extern uint32_t addr64a[8], addr64a_2[8];

extern int soft_reset_pci;

extern int reset_on_hlt, hlt_reset_pending;

extern cyrix_t cyrix;

extern uint8_t  use_custom_nmi_vector;
extern uint32_t custom_nmi_vector;

extern void (*cpu_exec)(int cycs);
extern uint8_t do_translate, do_translate2;

extern void reset_808x(int hard);
extern void interrupt_808x(uint16_t addr);

extern void cpu_register_fast_off_handler(void *timer);
extern void cpu_fast_off_advance(void);
extern void cpu_fast_off_period_set(uint16_t vla, double period);
extern void cpu_fast_off_reset(void);

extern void smi_raise();
extern void nmi_raise();

#endif /*EMU_CPU_H*/
