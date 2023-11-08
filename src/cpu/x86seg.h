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

#ifdef OPS_286_386

extern void x86_doabrt_2386(int x86_abrt);
#ifdef USE_NEW_DYNAREC
extern int  loadseg_2386(uint16_t seg, x86seg *s);
#else
extern void loadseg_2386(uint16_t seg, x86seg *s);
#endif
extern void loadcs_2386(uint16_t seg);
extern void loadcsjmp_2386(uint16_t seg, uint32_t old_pc);
#ifdef USE_NEW_DYNAREC
extern void loadcscall_2386(uint16_t seg, uint32_t old_pc);
#else
extern void loadcscall_2386(uint16_t seg);
#endif
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
#define op_pmoderetf      pmoderetf_2386
#define op_pmodeint       pmodeint_2386
#define op_pmodeiret      pmodeiret_2386
#define op_taskswitch     taskswitch_2386
#define op_taskswitch286  taskswitch286_2386

#else

extern void x86_doabrt(int x86_abrt);
#ifdef USE_NEW_DYNAREC
extern int  loadseg(uint16_t seg, x86seg *s);
#else
extern void loadseg(uint16_t seg, x86seg *s);
#endif
/* The prototype of loadcs_2386() is needed here for reset. */
extern void loadcs_2386(uint16_t seg);
extern void loadcs(uint16_t seg);
extern void loadcsjmp(uint16_t seg, uint32_t old_pc);
#ifdef USE_NEW_DYNAREC
extern void loadcscall(uint16_t seg, uint32_t old_pc);
#else
extern void loadcscall(uint16_t seg);
#endif
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
