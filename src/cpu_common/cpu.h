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
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 leilei.
 *		Copyright 2016,2018 Miran Grca.
 */
#ifndef EMU_CPU_H
# define EMU_CPU_H
enum {
    CPU_8088,		/* 808x class CPUs */
    CPU_8086,
#ifdef USE_NEC_808X
    CPU_V20,		/* NEC 808x class CPUs - future proofing */
    CPU_V30,
#endif
    CPU_286,		/* 286 class CPUs */
    CPU_386SX,		/* 386 class CPUs */
    CPU_386DX,
    CPU_IBM386SLC,
    CPU_IBM486SLC,
    CPU_IBM486BL,
    CPU_RAPIDCAD,
    CPU_486SLC,
    CPU_486DLC,
    CPU_i486SX,		/* 486 class CPUs */
    CPU_Am486SX,
    CPU_Cx486S,
    CPU_i486SX2,
    CPU_Am486SX2,
    CPU_i486DX,
    CPU_i486DX2,
    CPU_Am486DX,
    CPU_Am486DX2,
    CPU_Cx486DX,
    CPU_Cx486DX2,
    CPU_iDX4,
    CPU_Am486DX4,
    CPU_Cx486DX4,
    CPU_Am5x86,
    CPU_Cx5x86,
    CPU_WINCHIP,	/* 586 class CPUs */
    CPU_WINCHIP2,
    CPU_PENTIUM,
    CPU_PENTIUMMMX,
#if (defined(USE_NEW_DYNAREC) || (defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)))
    CPU_Cx6x86,
    CPU_Cx6x86MX,
    CPU_Cx6x86L,
    CPU_CxGX1,
#endif
#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
    CPU_K5,
    CPU_5K86,
#endif
    CPU_K6,
    CPU_K6_2,
    CPU_K6_2C,
    CPU_K6_3,
    CPU_K6_2P,
    CPU_K6_3P,
    CPU_CYRIX3S,
#if defined(DEV_BRANCH) && defined(USE_I686)
    CPU_PENTIUMPRO,	/* 686 class CPUs */
    CPU_PENTIUM2,
    CPU_PENTIUM2D,
#endif
    CPU_MAX		/* Only really needed to close the enum in a way independent of the #ifdef's. */
};


#define MANU_INTEL	0
#define MANU_AMD	1
#define MANU_CYRIX	2
#define MANU_IDT	3
#define MANU_NEC	4

#define CPU_SUPPORTS_DYNAREC 1
#define CPU_REQUIRES_DYNAREC 2
#define CPU_ALTERNATE_XTAL   4


typedef struct {
    const char *name;
    int        cpu_type;
    int        rspeed;
    double     multi;
    uint32_t   edx_reset;
    uint32_t   cpuid_model;
    uint16_t   cyrix_id;
    uint8_t    cpu_flags;
    int8_t     mem_read_cycles, mem_write_cycles;
    int8_t     cache_read_cycles, cache_write_cycles;
    int8_t     atclk_div;
} CPU;


extern CPU	cpus_8088[];
extern CPU	cpus_8086[];
extern CPU	cpus_286[];
extern CPU	cpus_i386SX[];
extern CPU	cpus_i386DX[];
extern CPU	cpus_Am386SX[];
extern CPU	cpus_Am386DX[];
extern CPU	cpus_486SLC[];
extern CPU	cpus_486DLC[];
extern CPU  cpus_IBM386SLC[];
extern CPU  cpus_IBM486SLC[];
extern CPU  cpus_IBM486BL[];
extern CPU  cpus_i486S1[];
extern CPU	cpus_Am486S1[];
extern CPU	cpus_Cx486S1[];
extern CPU	cpus_i486[];
extern CPU	cpus_Am486[];
extern CPU	cpus_Cx486[];
extern CPU	cpus_WinChip[];
extern CPU	cpus_WinChip_SS7[];
extern CPU	cpus_Pentium5V[];
extern CPU	cpus_Pentium5V50[];
extern CPU	cpus_PentiumS5[];
extern CPU	cpus_Pentium3V[];
extern CPU	cpus_Pentium[];
#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
extern CPU	cpus_K5[];
#endif
extern CPU	cpus_K56[];
extern CPU	cpus_K56_SS7[];
#if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
extern CPU	cpus_6x863V[];
extern CPU	cpus_6x86[];
#ifdef USE_NEW_DYNAREC
extern CPU	cpus_6x86SS7[];
#endif
#endif
extern CPU  cpus_Cyrix3[];
#if defined(DEV_BRANCH) && defined(USE_I686)
extern CPU	cpus_PentiumPro[];
extern CPU	cpus_PentiumII[];
extern CPU	cpus_Celeron[];
#endif


#define C_FLAG		0x0001
#define P_FLAG		0x0004
#define A_FLAG		0x0010
#define Z_FLAG		0x0040
#define N_FLAG		0x0080
#define T_FLAG		0x0100
#define I_FLAG		0x0200
#define D_FLAG		0x0400
#define V_FLAG		0x0800
#define NT_FLAG		0x4000

#define RF_FLAG		0x0001			/* in EFLAGS */
#define VM_FLAG		0x0002			/* in EFLAGS */
#define VIF_FLAG	0x0008			/* in EFLAGS */
#define VIP_FLAG	0x0010			/* in EFLAGS */
#define VID_FLAG	0x0020			/* in EFLAGS */

#define WP_FLAG		0x10000			/* in CR0 */
#define CR4_VME		(1 << 0)
#define CR4_PVI		(1 << 1)
#define CR4_PSE		(1 << 4)
#define CR4_PAE		(1 << 5)

#define CPL ((cpu_state.seg_cs.access>>5)&3)

#define IOPL ((cpu_state.flags>>12)&3)

#define IOPLp ((!(msw&1)) || (CPL<=IOPL))


typedef union {
    uint32_t	l;
    uint16_t	w;
    struct {
	uint8_t	l,
		h;
    }		b;
} x86reg;

typedef struct {
    uint32_t	base;
    uint32_t	limit;
    uint8_t	access;
    uint8_t	ar_high;
    uint16_t	seg;
    uint32_t	limit_low,
		limit_high;
    int		checked; /*Non-zero if selector is known to be valid*/
} x86seg;

typedef union {
    uint64_t	q;
    int64_t	sq;
    uint32_t	l[2];
    int32_t	sl[2];
    uint16_t	w[4];
    int16_t	sw[4];
    uint8_t	b[8];
    int8_t	sb[8];
    float	f[2];
} MMX_REG;

typedef struct {
    uint32_t	tr1, tr12;
    uint32_t	cesr;
    uint32_t	fcr;
    uint64_t	fcr2, fcr3;
} msr_t;

typedef union {
    uint32_t l;
    uint16_t w;
} cr0_t;


typedef struct {
    x86reg	regs[8];

    uint8_t	tag[8];

    x86seg	*ea_seg;
    uint32_t	eaaddr;

    int		flags_op;
    uint32_t	flags_res;
    uint32_t	flags_op1,
		flags_op2;

    uint32_t	pc;
    uint32_t	oldpc;
    uint32_t	op32;  

    int		TOP;

    union {
	struct {
	    int8_t	rm,
			mod,
			reg;
	} rm_mod_reg;
	int32_t		rm_mod_reg_data;
    }		rm_data;

    int8_t	ssegs;
    int8_t	ismmx;
    int8_t	abrt;

    int		_cycles;
    int		cpu_recomp_ins;

    uint16_t	npxs,
		npxc;

    double	ST[8];

    uint16_t	MM_w4[8];

    MMX_REG	MM[8];

    uint16_t	old_npxc,
		new_npxc;
    uint32_t	last_ea;

#ifdef USE_NEW_DYNAREC
    uint32_t	old_fp_control, new_fp_control;
#if defined i386 || defined __i386 || defined __i386__ || defined _X86_
    uint16_t	old_fp_control2, new_fp_control2;
#endif
#if defined i386 || defined __i386 || defined __i386__ || defined _X86_ || defined __amd64__
    uint32_t	trunc_fp_control;
#endif
#endif

    x86seg	seg_cs,
		seg_ds,
		seg_es,
		seg_ss,
		seg_fs,
		seg_gs;

    uint16_t flags, eflags;

    cr0_t CR0;
} cpu_state_t;

/*The cpu_state.flags below must match in both cpu_cur_status and block->status for a block
  to be valid*/
#define CPU_STATUS_USE32   (1 << 0)
#define CPU_STATUS_STACK32 (1 << 1)
#define CPU_STATUS_PMODE   (1 << 2)
#define CPU_STATUS_V86     (1 << 3)
#define CPU_STATUS_FLAGS 0xffff

/*If the cpu_state.flags below are set in cpu_cur_status, they must be set in block->status.
  Otherwise they are ignored*/
#ifdef USE_NEW_DYNAREC
#define CPU_STATUS_NOTFLATDS  (1 << 8)
#define CPU_STATUS_NOTFLATSS  (1 << 9)
#define CPU_STATUS_MASK 0xff00
#else
#define CPU_STATUS_NOTFLATDS  (1 << 16)
#define CPU_STATUS_NOTFLATSS  (1 << 17)
#define CPU_STATUS_MASK 0xffff0000
#endif

#ifdef _MSC_VER
# define COMPILE_TIME_ASSERT(expr)	/*nada*/
#else
# ifdef EXTREME_DEBUG
#  define COMPILE_TIME_ASSERT(expr) typedef char COMP_TIME_ASSERT[(expr) ? 1 : 0];
# else
#  define COMPILE_TIME_ASSERT(expr)	/*nada*/
# endif
#endif

COMPILE_TIME_ASSERT(sizeof(cpu_state) <= 128)

#define cpu_state_offset(MEMBER) ((uint8_t)((uintptr_t)&cpu_state.MEMBER - (uintptr_t)&cpu_state - 128))

#define EAX	cpu_state.regs[0].l
#define AX	cpu_state.regs[0].w
#define AL	cpu_state.regs[0].b.l
#define AH	cpu_state.regs[0].b.h
#define ECX	cpu_state.regs[1].l
#define CX	cpu_state.regs[1].w
#define CL	cpu_state.regs[1].b.l
#define CH	cpu_state.regs[1].b.h
#define EDX	cpu_state.regs[2].l
#define DX	cpu_state.regs[2].w
#define DL	cpu_state.regs[2].b.l
#define DH	cpu_state.regs[2].b.h
#define EBX	cpu_state.regs[3].l
#define BX	cpu_state.regs[3].w
#define BL	cpu_state.regs[3].b.l
#define BH	cpu_state.regs[3].b.h
#define ESP	cpu_state.regs[4].l
#define EBP	cpu_state.regs[5].l
#define ESI	cpu_state.regs[6].l
#define EDI	cpu_state.regs[7].l
#define SP	cpu_state.regs[4].w
#define BP	cpu_state.regs[5].w
#define SI	cpu_state.regs[6].w
#define DI	cpu_state.regs[7].w

#define cycles	cpu_state._cycles

#define cpu_rm	cpu_state.rm_data.rm_mod_reg.rm
#define cpu_mod	cpu_state.rm_data.rm_mod_reg.mod
#define cpu_reg	cpu_state.rm_data.rm_mod_reg.reg

#define CR4_TSD  (1 << 2)
#define CR4_DE   (1 << 3)
#define CR4_MCE  (1 << 6)
#define CR4_PCE  (1 << 8)
#define CR4_OSFXSR  (1 << 9)


/* Global variables. */
extern int	cpu_iscyrix;
extern int	cpu_16bitbus;
extern int	cpu_busspeed, cpu_pci_speed;
extern int	cpu_multi;
extern double	cpu_dmulti;
extern int	cpu_cyrix_alignment;	/*Cyrix 5x86/6x86 only has data misalignment
					  penalties when crossing 8-byte boundaries*/

extern int	is8086,	is286, is386, is486, is486sx, is486dx, is486sx2, is486dx2, isdx4;
extern int	is_pentium, is_k5, is_k6, is_p6;
extern int      hascache;
extern int      isibm486;
extern int	is_rapidcad;
extern int	hasfpu;
#define CPU_FEATURE_RDTSC (1 << 0)
#define CPU_FEATURE_MSR   (1 << 1)
#define CPU_FEATURE_MMX   (1 << 2)
#define CPU_FEATURE_CR4   (1 << 3)
#define CPU_FEATURE_VME   (1 << 4)
#define CPU_FEATURE_CX8   (1 << 5)
#define CPU_FEATURE_3DNOW (1 << 6)

extern uint32_t	cpu_features;

extern int	in_smm, smi_line, smi_latched, smm_in_hlt;
extern uint32_t	smbase;

#ifdef USE_NEW_DYNAREC
extern uint16_t		cpu_cur_status;
#else
extern uint32_t		cpu_cur_status;
#endif
extern uint64_t		cpu_CR4_mask;
extern uint64_t		tsc;
extern msr_t		msr;
extern cpu_state_t  cpu_state;
extern uint8_t		opcode;
extern int		insc;
extern int		fpucount;
extern float		mips,flops;
extern int		clockrate;
extern int		cgate16;
extern int		cpl_override;
extern int		CPUID;
extern uint64_t xt_cpu_multi;
extern int		isa_cycles;
extern uint32_t		oldds,oldss,olddslimit,oldsslimit,olddslimitw,oldsslimitw;
extern int		ins,output;
extern uint32_t		pccache;
extern uint8_t		*pccache2;

extern double		bus_timing, pci_timing;
extern uint64_t		pmc[2];
extern uint16_t		temp_seg_data[4];
extern uint16_t		cs_msr;
extern uint32_t		esp_msr;
extern uint32_t		eip_msr;

/* For the AMD K6. */
extern uint64_t		star;

#define FPU_CW_Reserved_Bits (0xe0c0)

#define cr0		cpu_state.CR0.l
#define msw		cpu_state.CR0.w
extern uint32_t		cr2, cr3, cr4;
extern uint32_t		dr[8];


/*Segments -
  _cs,_ds,_es,_ss are the segment structures
  CS,DS,ES,SS is the 16-bit data
  cs,ds,es,ss are defines to the bases*/
extern x86seg	gdt,ldt,idt,tr;
extern x86seg	_oldds;
#define CS	cpu_state.seg_cs.seg
#define DS	cpu_state.seg_ds.seg
#define ES	cpu_state.seg_es.seg
#define SS	cpu_state.seg_ss.seg
#define FS	cpu_state.seg_fs.seg
#define GS	cpu_state.seg_gs.seg
#define cs	cpu_state.seg_cs.base
#define ds	cpu_state.seg_ds.base
#define es	cpu_state.seg_es.base
#define ss	cpu_state.seg_ss.base
#define fs_seg	cpu_state.seg_fs.base
#define gs	cpu_state.seg_gs.base


#define ISA_CYCLES(x)    (x * isa_cycles)

extern int	cpu_cycles_read, cpu_cycles_read_l, cpu_cycles_write, cpu_cycles_write_l;
extern int	cpu_prefetch_cycles, cpu_prefetch_width, cpu_mem_prefetch_cycles, cpu_rom_prefetch_cycles;
extern int	cpu_waitstates;
extern int	cpu_cache_int_enabled, cpu_cache_ext_enabled;
extern int	cpu_pci_speed;

extern int	timing_rr;
extern int	timing_mr, timing_mrl;
extern int	timing_rm, timing_rml;
extern int	timing_mm, timing_mml;
extern int	timing_bt, timing_bnt;
extern int	timing_int, timing_int_rm, timing_int_v86, timing_int_pm;
extern int	timing_int_pm_outer, timing_iret_rm, timing_iret_v86, timing_iret_pm;
extern int	timing_iret_pm_outer, timing_call_rm, timing_call_pm;
extern int	timing_call_pm_gate, timing_call_pm_gate_inner;
extern int	timing_retf_rm, timing_retf_pm, timing_retf_pm_outer;
extern int	timing_jmp_rm, timing_jmp_pm, timing_jmp_pm_gate;
extern int	timing_misaligned;

extern int	in_sys;

extern CPU	cpus_pcjr[];		// FIXME: should be in machine file!
extern CPU	cpus_europc[];		// FIXME: should be in machine file!
extern CPU	cpus_pc1512[];		// FIXME: should be in machine file!
extern CPU	cpus_ibmat[];		// FIXME: should be in machine file!
extern CPU	cpus_ibmxt286[];	// FIXME: should be in machine file!
extern CPU	cpus_ps1_m2011[];	// FIXME: should be in machine file!
extern CPU	cpus_ps2_m30_286[];	// FIXME: should be in machine file!
#if 0
extern CPU	cpus_acer[];		// FIXME: should be in machine file!
#endif


/* Functions. */
extern int cpu_has_feature(int feature);

#ifdef USE_NEW_DYNAREC
extern void	loadseg_dynarec(uint16_t seg, x86seg *s);
extern int	loadseg(uint16_t seg, x86seg *s);
extern void	loadcs(uint16_t seg);
#else
extern void	loadseg(uint16_t seg, x86seg *s);
extern void	loadcs(uint16_t seg);
#endif

extern char	*cpu_current_pc(char *bufp);

extern void	cpu_update_waitstates(void);
extern void	cpu_set(void);

extern void	cpu_CPUID(void);
extern void	cpu_RDMSR(void);
extern void	cpu_WRMSR(void);

extern int      checkio(int port);
extern void	codegen_block_end(void);
extern void	codegen_reset(void);
extern void	cpu_set_edx(void);
extern int	divl(uint32_t val);
extern void	execx86(int cycs);
extern void	enter_smm(int in_hlt);
extern void	enter_smm_check(int in_hlt);
extern void	leave_smm(void);
extern void	exec386(int cycs);
extern void	exec386_dynarec(int cycs);
extern int	idivl(int32_t val);
#ifdef USE_NEW_DYNAREC
extern void	loadcscall(uint16_t seg, uint32_t old_pc);
extern void	loadcsjmp(uint16_t seg, uint32_t old_pc);
extern void	pmodeint(int num, int soft);
extern void	pmoderetf(int is32, uint16_t off);
extern void	pmodeiret(int is32);
#else
extern void	loadcscall(uint16_t seg);
extern void	loadcsjmp(uint16_t seg, uint32_t old_pc);
extern void	pmodeint(int num, int soft);
extern void	pmoderetf(int is32, uint16_t off);
extern void	pmodeiret(int is32);
#endif
extern void	resetmcr(void);
extern void	resetx86(void);
extern void	refreshread(void);
extern void	resetreadlookup(void);
extern void	softresetx86(void);
extern void x86_int(int num);
extern void	x86_int_sw(int num);
extern int	x86_int_sw_rm(int num);
extern void	x86gpf(char *s, uint16_t error);
extern void	x86np(char *s, uint16_t error);
extern void	x86ss(char *s, uint16_t error);
extern void	x86ts(char *s, uint16_t error);

#ifdef ENABLE_808X_LOG
extern void	dumpregs(int __force);
extern void	x87_dumpregs(void);
extern void	x87_reset(void);
#endif

extern int	cpu_effective, cpu_alt_reset;
extern void	cpu_dynamic_switch(int new_cpu);

extern void	cpu_ven_reset(void);

extern int	sysenter(uint32_t fetchdat);
extern int	sysexit(uint32_t fetchdat);
extern int	syscall(uint32_t fetchdat);
extern int	sysret(uint32_t fetchdat);


#endif	/*EMU_CPU_H*/
