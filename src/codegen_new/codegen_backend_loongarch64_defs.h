#ifndef _CODEGEN_BACKEND_LOONGARCH64_DEFS_H_
#define _CODEGEN_BACKEND_LOONGARCH64_DEFS_H_

/* LSX is a hard platform requirement for the new dynarec on LoongArch64
   (plan section 2) - hosts without it (LA264/LA364E-class) are unsupported.
   LSX instructions are emitted unconditionally, so gate at compile time. */
#if !defined(__loongarch_sx)
#    error "LoongArch64 new dynarec requires LSX; build with -march=la464 (or newer) -mlsx"
#endif

/*
 * LoongArch64 (LP64D ABI, little-endian) host register model for the
 * "new" dynamic recompiler.
 *
 * Integer register file (psABI):
 *   r0 $zero, r1 $ra, r2 $tp, r3 $sp, r4-r11 $a0-$a7, r12-r20 $t0-$t8,
 *   r21 reserved (non-allocatable), r22 $fp/$s9, r23-r31 $s0-$s8.
 *   r21 must never be touched; $fp (r22) is dedicated to REG_CPUSTATE,
 *   r23-r31 are the allocatable set.
 *
 * Floating point / vector:
 *   f0-f31 alias the low 64 bits of the LSX vector registers v0-v31 and use
 *   the same register numbers. f0-f23 are caller-saved; f24-f31 ($fs0-$fs7)
 *   are callee-saved and form the allocatable FP set (saved in the block
 *   prologue).
 */

/* Integer registers. */
#define REG_R0   0
#define REG_R1   1
#define REG_R2   2
#define REG_R3   3
#define REG_R4   4
#define REG_R5   5
#define REG_R6   6
#define REG_R7   7
#define REG_R8   8
#define REG_R9   9
#define REG_R10  10
#define REG_R11  11
#define REG_R12  12
#define REG_R13  13
#define REG_R14  14
#define REG_R15  15
#define REG_R16  16
#define REG_R17  17
#define REG_R18  18
#define REG_R19  19
#define REG_R20  20
#define REG_R21  21
#define REG_R22  22
#define REG_R23  23
#define REG_R24  24
#define REG_R25  25
#define REG_R26  26
#define REG_R27  27
#define REG_R28  28
#define REG_R29  29
#define REG_R30  30
#define REG_R31  31

#define REG_ZERO REG_R0
#define REG_RA   REG_R1
#define REG_TP   REG_R2
#define REG_SP   REG_R3

#define REG_A0   REG_R4
#define REG_A1   REG_R5
#define REG_A2   REG_R6
#define REG_A3   REG_R7
#define REG_A4   REG_R8
#define REG_A5   REG_R9
#define REG_A6   REG_R10
#define REG_A7   REG_R11

#define REG_T0   REG_R12
#define REG_T1   REG_R13
#define REG_T2   REG_R14
#define REG_T3   REG_R15
#define REG_T4   REG_R16
#define REG_T5   REG_R17
#define REG_T6   REG_R18
#define REG_T7   REG_R19
#define REG_T8   REG_R20

/* psABI: $r21 is "Reserved (Non-allocatable)" - do not use. */
#define REG_RESERVED REG_R21

/* NOTE: no REG_FP alias is defined for r22 ($fp) because the generic
   register allocator (codegen_reg.c) uses REG_FP as a register-class
   enum constant; use REG_R22 or REG_CPUSTATE. */
#define REG_S0   REG_R23
#define REG_S1   REG_R24
#define REG_S2   REG_R25
#define REG_S3   REG_R26
#define REG_S4   REG_R27
#define REG_S5   REG_R28
#define REG_S6   REG_R29
#define REG_S7   REG_R30
#define REG_S8   REG_R31

/* Floating point registers (aliases of the low 64 bits of v0-v31). */
#define REG_F0   0
#define REG_F1   1
#define REG_F2   2
#define REG_F3   3
#define REG_F4   4
#define REG_F5   5
#define REG_F6   6
#define REG_F7   7
#define REG_F8   8
#define REG_F9   9
#define REG_F10  10
#define REG_F11  11
#define REG_F12  12
#define REG_F13  13
#define REG_F14  14
#define REG_F15  15
#define REG_F16  16
#define REG_F17  17
#define REG_F18  18
#define REG_F19  19
#define REG_F20  20
#define REG_F21  21
#define REG_F22  22
#define REG_F23  23
#define REG_F24  24
#define REG_F25  25
#define REG_F26  26
#define REG_F27  27
#define REG_F28  28
#define REG_F29  29
#define REG_F30  30
#define REG_F31  31

#define REG_ARG0 REG_A0
#define REG_ARG1 REG_A1
#define REG_ARG2 REG_A2
#define REG_ARG3 REG_A3

#define REG_CPUSTATE REG_R22

#define REG_TEMP   REG_T0
#define REG_TEMP2  REG_T1

#define REG_V_TEMP REG_F0

#define CODEGEN_HOST_REGS    9
#define CODEGEN_HOST_FP_REGS 8

extern void *codegen_mem_load_byte;
extern void *codegen_mem_load_word;
extern void *codegen_mem_load_long;
extern void *codegen_mem_load_quad;
extern void *codegen_mem_load_single;
extern void *codegen_mem_load_double;

extern void *codegen_mem_store_byte;
extern void *codegen_mem_store_word;
extern void *codegen_mem_store_long;
extern void *codegen_mem_store_quad;
extern void *codegen_mem_store_single;
extern void *codegen_mem_store_double;

extern void *codegen_fp_round;
extern void *codegen_fp_round_quad;

extern void *codegen_gpf_rout;
extern void *codegen_exit_rout;

#endif
