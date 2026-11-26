/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          x86 CPU segment emulation header.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2017 Miran Grca.
 */
#ifndef EMU_X86SEG_H
#define EMU_X86SEG_H

extern int call_far_rm_fault(uint32_t new_pc, int is32);

extern int      call_far_a_pending;
extern uint32_t call_far_a_addr;
extern uint16_t call_far_a_word;
extern int      call_far_set_accessed(void);
extern int      call_far_set_accessed_2386(void);

#ifdef OPS_286_386

extern void x86_doabrt_2386(int x86_abrt);
extern void x86_fault_in_delivery_2386(int first, int second);
extern int  loadseg_2386(uint16_t seg, x86seg *s);
extern void loadcs_2386(uint16_t seg);
extern void loadcsjmp_2386(uint16_t seg, uint32_t old_pc);
extern void loadcscall_2386(uint16_t seg, uint32_t old_pc, int is32);
extern void pmoderetf_2386(int is32, uint16_t off);
extern void pmodeint_2386(int num, int soft);
extern void pmodeiret_2386(int is32);
extern void taskswitch286_2386(uint16_t seg, uint16_t *segdat, int is32);

/* #define's to avoid long #ifdef blocks in x86_ops_*.h. */
#define op_doabrt         x86_doabrt_2386
#define op_loadseg        loadseg_2386
#define op_loadcs         loadcs_2386
#define op_loadcsjmp      loadcsjmp_2386
#define op_loadcscall     loadcscall_2386
#define op_call_far_set_accessed call_far_set_accessed_2386
#define op_pmoderetf      pmoderetf_2386
#define op_pmodeint       pmodeint_2386
#define op_pmodeiret      pmodeiret_2386
#define op_taskswitch     taskswitch_2386
#define op_taskswitch286  taskswitch286_2386

#else

extern void x86_doabrt(int x86_abrt);
extern void x86_fault_in_delivery(int first, int second);
extern int  loadseg(uint16_t seg, x86seg *s);
#ifdef USE_DYNAREC
extern int  loadseg_pm_fast(uint16_t seg, x86seg *s);
extern int  call_far_pm_fast(uint16_t seg, uint32_t new_pc, uint32_t ret_pc, int is32);
extern void pmoderetf_pm_fast(int is32, uint16_t off);
extern void loadcsjmp_pm_fast(uint16_t seg, uint32_t old_pc);

/* A DS, ES, FS, GS or SS load that loadseg_pm_fast() completed, which the
   recompiler's load routines repeat without the call while every input of
   that load is unchanged: the selector and CPL (tag), the GDTR or LDTR
   base and limit, a valid read lookup for the descriptor's page and the
   eight bytes read through it */
#    define SEG_LOAD_CACHE_SIZE  256
#    define SEG_LOAD_CACHE_VALID 0x80000000

typedef struct seg_load_cache_t {
    uint32_t      tag;       /* selector | CPL << 16 | SEG_LOAD_CACHE_VALID */
    uint32_t      addr;      /* linear address of the descriptor */
    const x86seg *dt;        /* &gdt or &ldt */
    uint32_t      dt_base;   /* its base and limit at the load, compared */
    uint32_t      dt_limit;  /* as one 64-bit word with its first two */
    uintptr_t    *lookup;    /* &readlookup2[addr >> 12] */
    uint64_t      desc;      /* the descriptor read */
    uint32_t      image[5];  /* the first 20 bytes of the x86seg loaded */
    uint16_t      status_ds; /* the NOTFLAT, NOTWIDE and NOT64K bits */
    uint16_t      status_es; /* seg_load_status() gives for DS and ES */
} seg_load_cache_t;

extern seg_load_cache_t seg_load_cache[SEG_LOAD_CACHE_SIZE];

extern void seg_load_cache_flush(void);

/* A CS load that the far CALL, RETF or far JMP short path completed, which
   the recompiler's far transfer routines repeat while every input of the
   descriptor's checks is unchanged: the same tag, table, lookup and
   descriptor tests as seg_load_cache. The image is CS as
   far_pm_fast_load_cs() loads it, its limit the one the target offset is
   tested against */
#    define CS_LOAD_CACHE_SIZE 256

typedef struct cs_load_cache_t {
    uint32_t      tag;      /* selector | CPL << 16 | SEG_LOAD_CACHE_VALID */
    uint32_t      addr;     /* linear address of the descriptor */
    const x86seg *dt;       /* &gdt or &ldt */
    uint32_t      dt_base;  /* its base and limit at the load, compared */
    uint32_t      dt_limit; /* as one 64-bit word with its first two */
    uintptr_t    *lookup;   /* &readlookup2[addr >> 12] */
    uint64_t      desc;     /* the descriptor read */
    uint32_t      image[5]; /* the first 20 bytes of cpu_state.seg_cs */
    uint16_t      status;   /* its NOT64K and USE32 bits of cpu_cur_status */
    uint16_t      use32;    /* use32 after the load */
} cs_load_cache_t;

extern cs_load_cache_t cs_load_cache[CS_LOAD_CACHE_SIZE];

extern void cs_load_cache_flush(void);
#endif
/* The prototype of loadcs_2386() is needed here for reset. */
extern void loadcs_2386(uint16_t seg);
extern void loadcs(uint16_t seg);
extern void loadcsjmp(uint16_t seg, uint32_t old_pc);
extern void loadcscall(uint16_t seg, uint32_t old_pc, int is32);
extern void pmoderetf(int is32, uint16_t off);
/* The prototype of pmodeint_2386() is needed here for 386_common.c interrupts. */
extern void pmodeint_2386(int num, int soft);
extern void pmodeint(int num, int soft);
extern void pmodeiret(int is32);
extern void taskswitch286(uint16_t seg, uint16_t *segdat, int is32);

/* #define's to avoid long #ifdef blocks in x86_ops_*.h. */
#define op_doabrt         x86_doabrt
#define op_loadseg        loadseg
#define op_loadcs         loadcs
#define op_loadcsjmp      loadcsjmp
#define op_loadcscall     loadcscall
#define op_call_far_set_accessed call_far_set_accessed
#define op_pmoderetf      pmoderetf
#define op_pmodeint       pmodeint
#define op_pmodeiret      pmodeiret
#define op_taskswitch286  taskswitch286

#endif

extern void cyrix_write_seg_descriptor_2386(uint32_t addr, x86seg *seg);
extern void cyrix_load_seg_descriptor_2386(uint32_t addr, x86seg *seg);

extern void cyrix_write_seg_descriptor(uint32_t addr, x86seg *seg);
extern void cyrix_load_seg_descriptor(uint32_t addr, x86seg *seg);

#endif /*EMU_X86SEG_H*/
