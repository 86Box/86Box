/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- x86-64 span JIT backend.
 *
 *          For each draw state (rage128_draw_state_t) this file emits one
 *          function that rasterizes a whole scanline of a triangle. The
 *          function must write the same bytes as the interpreter's pixel
 *          loop in vid_ati_rage128_3d.c, so it repeats the loop's steps
 *          in the same order, with the same float and double widths. The
 *          interpreter is built with -ffp-contract=off, so its compiler
 *          never fuses a multiply and an add, and the emitted code uses
 *          no fused multiply-add either. The ARM64 backend
 *          (vid_ati_rage128_codegen_arm64.h) works to the same rules, and
 *          this file follows its structure. A state the generator cannot
 *          reproduce is refused and stays on the interpreter.
 *
 *          Both the Win64 and the System V calling conventions are
 *          supported. The choice is fixed at build time
 *          (R128_X64_ABI_WIN) and changes only the prologue, the epilogue
 *          and the call to the interpreter's texture stage.
 *
 *          The emitted code may use instructions up to SSE4.1 (PINSRD,
 *          PEXTRD, PMINUD, INSERTPS and PMOVZXBD in this file) and
 *          nothing newer: no AVX, FMA or BMI. rage128_jit_init leaves the
 *          JIT off on a host whose CPUID does not report SSE4.1.
 *
 *          Relevant literature:
 *
 *          [1] ATI Technologies, "RAGE 128 PRO Register Reference Guide",
 *              RRG-G04500-C Rev 1.01, January 2000. Cited below as
 *              "RRG: <register>, p. <printed page> / PDF <viewer page>".
 *
 *          [2] ATI Technologies, "RAGE 128 Software Development Guide",
 *              SDK-G04000 Rev 0.01, August 1999. Cited as "SDK: ...".
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#ifndef VIDEO_ATI_RAGE128_CODEGEN_X86_64_H
#define VIDEO_ATI_RAGE128_CODEGEN_X86_64_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <86box/vid_ati_rage128_codegen_rules.h>

#define R128_X64_BLOCK_SIZE 16384

/* The calling convention follows the host build: Win64 on Windows,
   System V elsewhere. Defining R128_X64_ABI_WIN before this header
   overrides that. The host JIT harness (tests/video/rage128/jit-harness)
   uses this to generate Win64 blocks on a System V host; such a block
   can only be run through a caller that uses the Win64 convention. */
#ifndef R128_X64_ABI_WIN
#    if defined(_WIN32)
#        define R128_X64_ABI_WIN 1
#    else
#        define R128_X64_ABI_WIN 0
#    endif
#endif

/* ------------------------------------------------------------------------
 * Register map of the emitted span function, after the prologue has
 * moved the arguments into place for either ABI:
 *
 *   rbx  tri (fixed)             r12d px
 *   rsi  e0 accumulator          r13d x1 (last column, inclusive)
 *   rdi  e1 accumulator          r14d drow
 *   rbp  e2 accumulator          r15d zrow
 *   r8   vram base               r9d  rx0 (-1 until a pixel is written)
 *   rax, rcx, rdx, r10, r11     scratch, except that r10 holds the
 *                                resolved Z/stencil cell from the Z
 *                                block to the Z write-back
 *
 *   xmm2 zline (double)          xmm5..xmm7  vca/vcb/vcc
 *   xmm3 dZdx (double)           xmm8..xmm10 w0/w1/w2
 *   xmm4 invs (float)            xmm11       col
 *   xmm0, xmm1, xmm12..xmm15    scratch
 *
 * The other per-triangle values (e0dxi..e2dxi, vram_mask, the staging
 * pointers, bases and limits) are read from tri, and zi and rx1 live
 * in the stack frame. The ARM64 backend keeps all of these in
 * registers. x86-64 has only 15 general registers besides rsp, but its
 * ALU instructions accept a memory operand, so most of these reads
 * fold into the instruction that uses the value. Float constants come
 * from a RIP-relative pool at the head of the block, not from
 * registers.
 * ---------------------------------------------------------------------- */

/* GPR encodings */
#define X64_RAX 0
#define X64_RCX 1
#define X64_RDX 2
#define X64_RBX 3
#define X64_RSP 4
#define X64_RBP 5
#define X64_RSI 6
#define X64_RDI 7
#define X64_R8  8
#define X64_R9  9
#define X64_R10 10
#define X64_R11 11
#define X64_R12 12
#define X64_R13 13
#define X64_R14 14
#define X64_R15 15

/* Condition codes: the low four bits of the Jcc (0F 80+cc), SETcc
   (0F 90+cc) and CMOVcc (0F 40+cc) opcodes. The ARM64 condition with
   the same meaning is in parentheses. */
#define X64_CC_O  0x0
#define X64_CC_NO 0x1
#define X64_CC_B  0x2 /* unsigned <   (LO) */
#define X64_CC_AE 0x3 /* unsigned >=  (HS) */
#define X64_CC_E  0x4 /* ==           (EQ) */
#define X64_CC_NE 0x5
#define X64_CC_BE 0x6 /* unsigned <=  (LS) */
#define X64_CC_A  0x7 /* unsigned >   (HI) */
#define X64_CC_S  0x8 /* sign         (MI) */
#define X64_CC_NS 0x9
#define X64_CC_P  0xA
#define X64_CC_L  0xC /* signed <     (LT) */
#define X64_CC_GE 0xD
#define X64_CC_LE 0xE
#define X64_CC_G  0xF /* signed >     (GT) */

typedef struct r128_x64_emit_t {
    uint8_t *base;
    int      pos; /* bytes */
    int      overflow;
    int      pool_pos; /* next free byte in the constant pool */
    /* offsets of the per-state pool entries: */
    int cp_texwh;    /* {texw0,texh0,texw1,texh1} floats           */
    int cp_lodm;     /* {lod_bias, -1000.0f} floats                 */
    int cp_tfx;      /* SoA: 4 x 4096.0f, then 4 x 2^-12            */
    int cp_lodv[2];  /* textured SoA with LOD, per stage: texw,
                        texh, lod_bias, -1000, (float)top and top,
                        each in all four lanes */
    int tex_sub[2];  /* per-stage offset of the S3TC/YUV decode
                        subroutine, or -1. A textured SoA block
                        emits it once, ahead of both loops; a
                        scalar-only block emits it in the stage */
    int soa_csub[2]; /* per-stage offset of the SoA coord/gather
                        subroutine (both trilinear passes call
                        it), or -1 */
    int soa_bay1;    /* frame slots of the row bayer add vectors: the
                        base pair, or the textured-SoA extension's */
    int soa_bay2;
    int soa_stq;     /* stencil packed-lane slot: the base one, or
                        the textured-SoA extension's */
    int cvt_i32_sub; /* offsets of the out-of-range fixups of the
                        scalar float to int conversions (see
                        r128_x64_emit_cvt_i32), or 0 until the
                        first conversion in the block emits them */
    int cvt_u32_sub;
    int cvt_i32v_sub; /* the same for the SoA conversions */
    int cvt_u32v_sub;
} r128_x64_emit_t;

static void
r128_x64_e8(r128_x64_emit_t *e, uint8_t b)
{
    if (e->pos + 1 > R128_X64_BLOCK_SIZE) {
        e->overflow = 1;
        return;
    }
    e->base[e->pos++] = b;
}

static void
r128_x64_e32(r128_x64_emit_t *e, uint32_t v)
{
    if (e->pos + 4 > R128_X64_BLOCK_SIZE) {
        e->overflow = 1;
        return;
    }
    memcpy(e->base + e->pos, &v, 4);
    e->pos += 4;
}

static void
r128_x64_e64(r128_x64_emit_t *e, uint64_t v)
{
    if (e->pos + 8 > R128_X64_BLOCK_SIZE) {
        e->overflow = 1;
        return;
    }
    memcpy(e->base + e->pos, &v, 8);
    e->pos += 8;
}

static int
r128_x64_here(const r128_x64_emit_t *e)
{
    return e->pos;
}

/* ---- Branches. Every branch uses the form with a 32-bit displacement
 * in its last four bytes. The emitter records the offset just past the
 * instruction, which is where the CPU measures the displacement from,
 * so r128_x64_patch32 fixes up a Jcc, a JMP or a CALL the same way once
 * the target is known. ---- */

static void
r128_x64_patch32(r128_x64_emit_t *e, int at, int target)
{
    int32_t rel = target - at;

    if (e->overflow)
        return;
    memcpy(e->base + at - 4, &rel, 4);
}

/* REX prefix, emitted only when it carries something: W for a 64-bit
   operand, or bit 3 of a register number (r8-r15) in R (reg), X
   (index) or B (rm or base). */
static void
r128_x64_rex(r128_x64_emit_t *e, int w, int reg, int idx, int rm)
{
    uint8_t rex = 0x40 | ((w & 1) << 3) | (((reg >> 3) & 1) << 2)
        | (((idx >> 3) & 1) << 1) | ((rm >> 3) & 1);

    if (rex != 0x40)
        r128_x64_e8(e, rex);
}

/* ModRM for [base + disp], no index. A base of rsp or r12 (low bits
 * 100) needs a SIB byte; 0x24 means no index, base rsp/r12. A base of
 * rbp or r13 (low bits 101) with mod 00 would mean RIP-relative, so
 * those bases always take a displacement, even 0. */
static void
r128_x64_modrm_mem(r128_x64_emit_t *e, int reg, int base, int32_t disp)
{
    int lo = base & 7;

    if (disp == 0 && lo != X64_RBP) { /* also excludes R13 via lo */
        r128_x64_e8(e, (uint8_t) (0x00 | ((reg & 7) << 3) | lo));
        if (lo == X64_RSP)
            r128_x64_e8(e, 0x24);
    } else if (disp >= -128 && disp <= 127) {
        r128_x64_e8(e, (uint8_t) (0x40 | ((reg & 7) << 3) | lo));
        if (lo == X64_RSP)
            r128_x64_e8(e, 0x24);
        r128_x64_e8(e, (uint8_t) disp);
    } else {
        r128_x64_e8(e, (uint8_t) (0x80 | ((reg & 7) << 3) | lo));
        if (lo == X64_RSP)
            r128_x64_e8(e, 0x24);
        r128_x64_e32(e, (uint32_t) disp);
    }
}

static void
r128_x64_modrm_reg(r128_x64_emit_t *e, int reg, int rm)
{
    r128_x64_e8(e, (uint8_t) (0xC0 | ((reg & 7) << 3) | (rm & 7)));
}

/* [base + index*scale + disp8/32]; scale is the shift count 0..3. As
   above, rbp or r13 as base forces a displacement. */
static void
r128_x64_modrm_sib(r128_x64_emit_t *e, int reg, int base, int idx, int scale,
                   int32_t disp)
{
    int lo   = base & 7;
    int mode = (disp == 0 && lo != X64_RBP) ? 0
        : (disp >= -128 && disp <= 127)     ? 1
                                            : 2;

    r128_x64_e8(e, (uint8_t) ((mode << 6) | ((reg & 7) << 3) | 4));
    r128_x64_e8(e, (uint8_t) ((scale << 6) | ((idx & 7) << 3) | lo));
    if (mode == 1)
        r128_x64_e8(e, (uint8_t) disp);
    else if (mode == 2)
        r128_x64_e32(e, (uint32_t) disp);
}

/* RIP-relative ModRM reaching `target`, an offset in the block (a
   constant-pool slot). The CPU measures the displacement from the end
   of the instruction, so this must be the last field of it; every
   caller here ends the instruction with it. */
static void
r128_x64_modrm_rip(r128_x64_emit_t *e, int reg, int target)
{
    r128_x64_e8(e, (uint8_t) (0x05 | ((reg & 7) << 3)));
    r128_x64_e32(e, 0);
    r128_x64_patch32(e, e->pos, target);
}

/* ---- GPR moves / loads / stores ---- */

static void
r128_x64_mov_r64_imm64(r128_x64_emit_t *e, int rd, uint64_t v)
{
    r128_x64_rex(e, 1, 0, 0, rd);
    r128_x64_e8(e, (uint8_t) (0xB8 | (rd & 7)));
    r128_x64_e64(e, v);
}

static void
r128_x64_mov_r32_imm32(r128_x64_emit_t *e, int rd, uint32_t v)
{
    r128_x64_rex(e, 0, 0, 0, rd);
    r128_x64_e8(e, (uint8_t) (0xB8 | (rd & 7)));
    r128_x64_e32(e, v);
}

static void
r128_x64_mov_r_r(r128_x64_emit_t *e, int w, int rd, int rs)
{
    r128_x64_rex(e, w, rs, 0, rd);
    r128_x64_e8(e, 0x89);
    r128_x64_modrm_reg(e, rs, rd);
}

static void
r128_x64_ld(r128_x64_emit_t *e, int w, int rd, int base, int32_t disp)
{
    r128_x64_rex(e, w, rd, 0, base);
    r128_x64_e8(e, 0x8B);
    r128_x64_modrm_mem(e, rd, base, disp);
}

static void
r128_x64_st(r128_x64_emit_t *e, int w, int rs, int base, int32_t disp)
{
    r128_x64_rex(e, w, rs, 0, base);
    r128_x64_e8(e, 0x89);
    r128_x64_modrm_mem(e, rs, base, disp);
}

static void
r128_x64_st16(r128_x64_emit_t *e, int rs, int base, int32_t disp)
{
    r128_x64_e8(e, 0x66);
    r128_x64_rex(e, 0, rs, 0, base);
    r128_x64_e8(e, 0x89);
    r128_x64_modrm_mem(e, rs, base, disp);
}

static void
r128_x64_ldzx16(r128_x64_emit_t *e, int rd, int base, int32_t disp)
{
    r128_x64_rex(e, 0, rd, 0, base);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0xB7);
    r128_x64_modrm_mem(e, rd, base, disp);
}

static void
r128_x64_ldzx8_sib(r128_x64_emit_t *e, int rd, int base, int idx)
{
    r128_x64_rex(e, 0, rd, idx, base);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0xB6);
    r128_x64_modrm_sib(e, rd, base, idx, 0, 0);
}

/* lea rd, [base + idx] (64-bit) */
static void
r128_x64_lea_add(r128_x64_emit_t *e, int rd, int base, int idx)
{
    r128_x64_rex(e, 1, rd, idx, base);
    r128_x64_e8(e, 0x8D);
    r128_x64_modrm_sib(e, rd, base, idx, 0, 0);
}

/* ---- ALU: op rd, rs / op rd, [base+disp] / op rd, imm ---- */

static void
r128_x64_alu_r_r(r128_x64_emit_t *e, uint8_t op, int w, int rd, int rs)
{
    /* op = 0x01 add, 0x09 or, 0x21 and, 0x29 sub, 0x31 xor, 0x39 cmp,
       0x85 test (all r/m, reg form) */
    r128_x64_rex(e, w, rs, 0, rd);
    r128_x64_e8(e, op);
    r128_x64_modrm_reg(e, rs, rd);
}

static void
r128_x64_alu_r_mem(r128_x64_emit_t *e, uint8_t op, int w, int rd, int base,
                   int32_t disp)
{
    /* op = 0x03 add, 0x0B or, 0x23 and, 0x2B sub, 0x33 xor, 0x3B cmp
       (reg, r/m form) */
    r128_x64_rex(e, w, rd, 0, base);
    r128_x64_e8(e, op);
    r128_x64_modrm_mem(e, rd, base, disp);
}

static void
r128_x64_alu_r_imm(r128_x64_emit_t *e, int ext, int w, int rd, int32_t imm)
{
    /* ext = /0 add, /1 or, /4 and, /5 sub, /7 cmp */
    r128_x64_rex(e, w, 0, 0, rd);
    if (imm >= -128 && imm <= 127) {
        r128_x64_e8(e, 0x83);
        r128_x64_modrm_reg(e, ext, rd);
        r128_x64_e8(e, (uint8_t) imm);
    } else {
        r128_x64_e8(e, 0x81);
        r128_x64_modrm_reg(e, ext, rd);
        r128_x64_e32(e, (uint32_t) imm);
    }
}

static void
r128_x64_test_r32_imm(r128_x64_emit_t *e, int rd, uint32_t imm)
{
    r128_x64_rex(e, 0, 0, 0, rd);
    r128_x64_e8(e, 0xF7);
    r128_x64_modrm_reg(e, 0, rd);
    r128_x64_e32(e, imm);
}

static void
r128_x64_shift_imm(r128_x64_emit_t *e, int ext, int w, int rd, int sh)
{
    /* ext = /4 shl, /5 shr, /7 sar */
    r128_x64_rex(e, w, 0, 0, rd);
    r128_x64_e8(e, 0xC1);
    r128_x64_modrm_reg(e, ext, rd);
    r128_x64_e8(e, (uint8_t) sh);
}

static void
r128_x64_shr_cl(r128_x64_emit_t *e, int w, int rd)
{
    r128_x64_rex(e, w, 0, 0, rd);
    r128_x64_e8(e, 0xD3);
    r128_x64_modrm_reg(e, 5, rd);
}

static void
r128_x64_not_r32(r128_x64_emit_t *e, int rd)
{
    r128_x64_rex(e, 0, 0, 0, rd);
    r128_x64_e8(e, 0xF7);
    r128_x64_modrm_reg(e, 2, rd);
}

static void
r128_x64_cmov(r128_x64_emit_t *e, int cc, int w, int rd, int rs)
{
    r128_x64_rex(e, w, rd, 0, rs);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, (uint8_t) (0x40 | cc));
    r128_x64_modrm_reg(e, rd, rs);
}

static void
r128_x64_setcc(r128_x64_emit_t *e, int cc, int rd)
{
    /* SETcc writes only the low byte of rd, so callers zero rd first
       with mov r32, 0, after the compare: mov leaves the flags alone,
       where xor would clear them. A REX prefix is always emitted so
       that registers 4-7 encode spl/bpl/sil/dil, not ah/ch/dh/bh. */
    r128_x64_e8(e, (uint8_t) (0x40 | ((rd >> 3) & 1)));
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, (uint8_t) (0x90 | cc));
    r128_x64_modrm_reg(e, 0, rd);
}

/* ---- branches ---- */

static int
r128_x64_jcc(r128_x64_emit_t *e, int cc)
{
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, (uint8_t) (0x80 | cc));
    r128_x64_e32(e, 0);
    return e->pos;
}

static int
r128_x64_jmp(r128_x64_emit_t *e)
{
    r128_x64_e8(e, 0xE9);
    r128_x64_e32(e, 0);
    return e->pos;
}

static void
r128_x64_jmp_to(r128_x64_emit_t *e, int target)
{
    int at = r128_x64_jmp(e);

    r128_x64_patch32(e, at, target);
}

static void
r128_x64_call_r(r128_x64_emit_t *e, int rn)
{
    r128_x64_rex(e, 0, 0, 0, rn);
    r128_x64_e8(e, 0xFF);
    r128_x64_modrm_reg(e, 2, rn);
}

static void
r128_x64_push(r128_x64_emit_t *e, int rd)
{
    r128_x64_rex(e, 0, 0, 0, rd);
    r128_x64_e8(e, (uint8_t) (0x50 | (rd & 7)));
}

static void
r128_x64_pop(r128_x64_emit_t *e, int rd)
{
    r128_x64_rex(e, 0, 0, 0, rd);
    r128_x64_e8(e, (uint8_t) (0x58 | (rd & 7)));
}

static void
r128_x64_ret(r128_x64_emit_t *e)
{
    r128_x64_e8(e, 0xC3);
}

/* ---- SSE register and memory forms. pfx is the mandatory prefix
   (0x66, 0xF2, 0xF3, or 0 for none), which must come before REX; xo is
   the opcode byte after 0F. ---- */

static void
r128_x64_sse_rr(r128_x64_emit_t *e, uint8_t pfx, uint8_t xo, int xd, int xs)
{
    if (pfx)
        r128_x64_e8(e, pfx);
    r128_x64_rex(e, 0, xd, 0, xs);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, xo);
    r128_x64_modrm_reg(e, xd, xs);
}

static void
r128_x64_sse_rm(r128_x64_emit_t *e, uint8_t pfx, uint8_t xo, int xd, int base,
                int32_t disp)
{
    if (pfx)
        r128_x64_e8(e, pfx);
    r128_x64_rex(e, 0, xd, 0, base);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, xo);
    r128_x64_modrm_mem(e, xd, base, disp);
}

static void
r128_x64_sse_rip(r128_x64_emit_t *e, uint8_t pfx, uint8_t xo, int xd,
                 int target)
{
    if (pfx)
        r128_x64_e8(e, pfx);
    r128_x64_rex(e, 0, xd, 0, 0);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, xo);
    r128_x64_modrm_rip(e, xd, target);
}

/* named forms of the SSE instructions the generator uses */
#define r128_x64_movss_rr(e, d, s)     r128_x64_sse_rr(e, 0xF3, 0x10, d, s)
#define r128_x64_movss_ld(e, d, b, o)  r128_x64_sse_rm(e, 0xF3, 0x10, d, b, o)
#define r128_x64_movsd_ld(e, d, b, o)  r128_x64_sse_rm(e, 0xF2, 0x10, d, b, o)
#define r128_x64_movsd_st(e, s, b, o)  r128_x64_sse_rm(e, 0xF2, 0x11, s, b, o)
#define r128_x64_movsd_rr(e, d, s)     r128_x64_sse_rr(e, 0xF2, 0x10, d, s)
#define r128_x64_movups_ld(e, d, b, o) r128_x64_sse_rm(e, 0, 0x10, d, b, o)
#define r128_x64_movaps_rr(e, d, s)    r128_x64_sse_rr(e, 0, 0x28, d, s)
#define r128_x64_movaps_ld(e, d, b, o) r128_x64_sse_rm(e, 0, 0x28, d, b, o)
#define r128_x64_movaps_st(e, s, b, o) r128_x64_sse_rm(e, 0, 0x29, s, b, o)
#define r128_x64_addss(e, d, s)        r128_x64_sse_rr(e, 0xF3, 0x58, d, s)
#define r128_x64_mulss(e, d, s)        r128_x64_sse_rr(e, 0xF3, 0x59, d, s)
#define r128_x64_subss(e, d, s)        r128_x64_sse_rr(e, 0xF3, 0x5C, d, s)
#define r128_x64_divss(e, d, s)        r128_x64_sse_rr(e, 0xF3, 0x5E, d, s)
#define r128_x64_addsd(e, d, s)        r128_x64_sse_rr(e, 0xF2, 0x58, d, s)
#define r128_x64_mulsd(e, d, s)        r128_x64_sse_rr(e, 0xF2, 0x59, d, s)
#define r128_x64_addps(e, d, s)        r128_x64_sse_rr(e, 0, 0x58, d, s)
#define r128_x64_mulps(e, d, s)        r128_x64_sse_rr(e, 0, 0x59, d, s)
#define r128_x64_subps(e, d, s)        r128_x64_sse_rr(e, 0, 0x5C, d, s)
#define r128_x64_xorps(e, d, s)        r128_x64_sse_rr(e, 0, 0x57, d, s)
#define r128_x64_comiss(e, a, b)       r128_x64_sse_rr(e, 0, 0x2F, a, b)
#define r128_x64_comisd(e, a, b)       r128_x64_sse_rr(e, 0x66, 0x2F, a, b)
#define r128_x64_cvtdq2ps(e, d, s)     r128_x64_sse_rr(e, 0, 0x5B, d, s)
#define r128_x64_cvtps2dq(e, d, s)     r128_x64_sse_rr(e, 0x66, 0x5B, d, s)
#define r128_x64_cvttps2dq(e, d, s)    r128_x64_sse_rr(e, 0xF3, 0x5B, d, s)
#define r128_x64_pand(e, d, s)         r128_x64_sse_rr(e, 0x66, 0xDB, d, s)
#define r128_x64_por(e, d, s)          r128_x64_sse_rr(e, 0x66, 0xEB, d, s)
#define r128_x64_paddd(e, d, s)        r128_x64_sse_rr(e, 0x66, 0xFE, d, s)
#define r128_x64_movd_x_r(e, x, r)     r128_x64_sse_gpr(e, 0x6E, x, r, 0)
#define r128_x64_movd_r_x(e, r, x)     r128_x64_sse_gpr(e, 0x7E, x, r, 0)

static void
r128_x64_sse_gpr(r128_x64_emit_t *e, uint8_t xo, int x, int r, int w)
{
    r128_x64_e8(e, 0x66);
    r128_x64_rex(e, w, x, 0, r);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, xo);
    r128_x64_modrm_reg(e, x, r);
}

/* cvtsi2ss xd, r64: int64 to float, rounded to nearest, like ARM64
   scvtf s, x */
static void
r128_x64_cvtsi2ss_r64(r128_x64_emit_t *e, int xd, int rs)
{
    r128_x64_e8(e, 0xF3);
    r128_x64_e8(e, (uint8_t) (0x48 | (((xd >> 3) & 1) << 2) | ((rs >> 3) & 1)));
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0x2A);
    r128_x64_modrm_reg(e, xd, rs);
}

/* cvttsd2si r64, xs: double to int64, truncating. Used for the depth
   value zi, in 0..0xffffffff, and for the table-fog split, which
   converts zc * 255 * 2^32, up to 255 * 2^32; the 64-bit form
   converts both exactly, where the 32-bit form fails above
   0x7fffffff. */
static void
r128_x64_cvttsd2si_r64(r128_x64_emit_t *e, int rd, int xs)
{
    r128_x64_e8(e, 0xF2);
    r128_x64_e8(e, (uint8_t) (0x48 | (((rd >> 3) & 1) << 2) | ((xs >> 3) & 1)));
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0x2C);
    r128_x64_modrm_reg(e, rd, xs);
}

/* cvttss2si r32, xs: float to int32, truncating. NaN and anything
   outside the int32 range give 0x80000000; the alpha test and the mip
   level pass values in 0..255, and r128_x64_emit_cvt_i32 passes any
   float and fixes that result up afterwards. */
static void
r128_x64_cvttss2si_r32(r128_x64_emit_t *e, int rd, int xs)
{
    r128_x64_e8(e, 0xF3);
    r128_x64_rex(e, 0, rd, 0, xs);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0x2C);
    r128_x64_modrm_reg(e, rd, xs);
}

static void
r128_x64_pshufd(r128_x64_emit_t *e, int xd, int xs, uint8_t sel)
{
    r128_x64_sse_rr(e, 0x66, 0x70, xd, xs);
    r128_x64_e8(e, sel);
}

/* copy lane 0 to all four lanes (ARM64 dup_4s_lane0) */
static void
r128_x64_splat0(r128_x64_emit_t *e, int xd, int xs)
{
    r128_x64_pshufd(e, xd, xs, 0x00);
}

/* SSE4.1 lane ops */
static void
r128_x64_pinsrd(r128_x64_emit_t *e, int xd, int rs, int lane)
{
    r128_x64_e8(e, 0x66);
    r128_x64_rex(e, 0, xd, 0, rs);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0x3A);
    r128_x64_e8(e, 0x22);
    r128_x64_modrm_reg(e, xd, rs);
    r128_x64_e8(e, (uint8_t) lane);
}

static void
r128_x64_pextrd(r128_x64_emit_t *e, int rd, int xs, int lane)
{
    r128_x64_e8(e, 0x66);
    r128_x64_rex(e, 0, xs, 0, rd);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0x3A);
    r128_x64_e8(e, 0x16);
    r128_x64_modrm_reg(e, xs, rd);
    r128_x64_e8(e, (uint8_t) lane);
}

static void
r128_x64_pminud(r128_x64_emit_t *e, int xd, int xs)
{
    r128_x64_e8(e, 0x66);
    r128_x64_rex(e, 0, xd, 0, xs);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0x38);
    r128_x64_e8(e, 0x3B);
    r128_x64_modrm_reg(e, xd, xs);
}

/* insertps: xd.lane[dl] = xs.lane[sl] (SSE4.1; ARM64 ins_elem_s) */
static void
r128_x64_insertps(r128_x64_emit_t *e, int xd, int xs, int dl, int sl)
{
    r128_x64_e8(e, 0x66);
    r128_x64_rex(e, 0, xd, 0, xs);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0x3A);
    r128_x64_e8(e, 0x21);
    r128_x64_modrm_reg(e, xd, xs);
    r128_x64_e8(e, (uint8_t) ((sl << 6) | (dl << 4)));
}

/* pmovzxbd xd, xs (SSE4.1, 66 0F 38 31): the low four bytes of xs,
   zero-extended to four dwords. Opcode 0x21 in the same map is
   pmovsxbd, which sign-extends: a byte of 0x80 or more would become a
   negative lane. */
static void
r128_x64_pmovzxbd(r128_x64_emit_t *e, int xd, int xs)
{
    r128_x64_e8(e, 0x66);
    r128_x64_rex(e, 0, xd, 0, xs);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0x38);
    r128_x64_e8(e, 0x31);
    r128_x64_modrm_reg(e, xd, xs);
}

/* cvtsi2ss xd, r32: signed int32 to float, rounded to nearest like the
   C's (float) cast. The fog-table and exponent uses pass values in
   -255..255, which convert exactly; the bilinear fraction passes the
   floored texture coordinate, which can be any int32 and rounds the
   same way as the interpreter's (float) u0. */
static void
r128_x64_cvtsi2ss_r32(r128_x64_emit_t *e, int xd, int rs)
{
    r128_x64_e8(e, 0xF3);
    r128_x64_rex(e, 0, xd, 0, rs);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0x2A);
    r128_x64_modrm_reg(e, xd, rs);
}

/* ------------------------------------------------------------------------
 * Constant pool. The block starts with a 5-byte jmp over the pool, and
 * the pool starts at offset 16. The fixed slots below hold the
 * constants any block may use; per-state values are appended from
 * R128_X64_CP_DYN on by r128_x64_pool_add. Code reads them
 * RIP-relative, so no register holds a constant. Each block is its own
 * page-aligned mapping, so a slot at a multiple of 16 is a 16-byte
 * aligned address, as movaps and the packed arithmetic instructions
 * with a memory operand require.
 * ---------------------------------------------------------------------- */
/* clang-format off */
#define R128_X64_POOL_BASE  16
#define R128_X64_CP_255F    (R128_X64_POOL_BASE + 0)   /* 4 x 255.0f  */
#define R128_X64_CP_HALFF   (R128_X64_POOL_BASE + 16)  /* 4 x 0.5f    */
#define R128_X64_CP_ONEF    (R128_X64_POOL_BASE + 32)  /* 4 x 1.0f    */
#define R128_X64_CP_255I    (R128_X64_POOL_BASE + 48)  /* 4 x 255     */
#define R128_X64_CP_ZERO    (R128_X64_POOL_BASE + 64)  /* 16 zero bytes */
#define R128_X64_CP_ONED    (R128_X64_POOL_BASE + 80)  /* 1.0d, 0.5d  */
#define R128_X64_CP_ZMAXD   (R128_X64_POOL_BASE + 96)  /* (double)zmax */
#define R128_X64_CP_ONED2   (R128_X64_POOL_BASE + 112) /* 2 x 1.0d    */
#define R128_X64_CP_HALFD2  (R128_X64_POOL_BASE + 128) /* 2 x 0.5d    */
#define R128_X64_CP_ZMAXD2  (R128_X64_POOL_BASE + 144) /* 2 x (double)zmax */
#define R128_X64_CP_LANEIDX (R128_X64_POOL_BASE + 160) /* {0,1,2,3}   */
#define R128_X64_CP_BIAS31  (R128_X64_POOL_BASE + 176) /* 4 x 1u<<31  */
#define R128_X64_CP_LERPM   (R128_X64_POOL_BASE + 192) /* 4 x 0x00ff00ff */
#define R128_X64_CP_LERPK   (R128_X64_POOL_BASE + 208) /* 4 x 0x00800080 */
#define R128_X64_CP_256I    (R128_X64_POOL_BASE + 224) /* 4 x 256     */
#define R128_X64_CP_POLY    (R128_X64_POOL_BASE + 240) /* log2 poly c0..c3 */
#define R128_X64_CP_TEXF    (R128_X64_POOL_BASE + 256) /* {4096, 2^-12,
                                                          256, 127}   */
#define R128_X64_CP_POLYV   (R128_X64_POOL_BASE + 272) /* 4 x {c0..c3} splats,
                                                          c0 first     */
#define R128_X64_CP_127F    (R128_X64_POOL_BASE + 336) /* 4 x 127.0f  */
#define R128_X64_CP_MANT    (R128_X64_POOL_BASE + 352) /* 4 x 0x007fffff */
#define R128_X64_CP_DYN     (R128_X64_POOL_BASE + 368)
#define R128_X64_POOL_END   (R128_X64_POOL_BASE + 672) /* code starts */
/* clang-format on */

static void
r128_x64_pool_init(r128_x64_emit_t *e, const rage128_draw_state_t *ds)
{
    float  f;
    double d;

    memset(e->base, 0, R128_X64_POOL_END);
    /* jmp rel32 over the pool */
    e->base[0] = 0xE9;
    {
        int32_t rel = R128_X64_POOL_END - 5;

        memcpy(e->base + 1, &rel, 4);
    }
    f = 255.0f;
    for (int i = 0; i < 4; i++)
        memcpy(e->base + R128_X64_CP_255F + i * 4, &f, 4);
    f = 0.5f;
    for (int i = 0; i < 4; i++)
        memcpy(e->base + R128_X64_CP_HALFF + i * 4, &f, 4);
    f = 1.0f;
    for (int i = 0; i < 4; i++)
        memcpy(e->base + R128_X64_CP_ONEF + i * 4, &f, 4);
    for (int i = 0; i < 4; i++) {
        uint32_t u = 255;

        memcpy(e->base + R128_X64_CP_255I + i * 4, &u, 4);
    }
    d = 1.0;
    memcpy(e->base + R128_X64_CP_ONED, &d, 8);
    memcpy(e->base + R128_X64_CP_ONED2, &d, 8);
    memcpy(e->base + R128_X64_CP_ONED2 + 8, &d, 8);
    d = 0.5;
    memcpy(e->base + R128_X64_CP_ONED + 8, &d, 8);
    memcpy(e->base + R128_X64_CP_HALFD2, &d, 8);
    memcpy(e->base + R128_X64_CP_HALFD2 + 8, &d, 8);
    d = (double) ds->zmax;
    memcpy(e->base + R128_X64_CP_ZMAXD, &d, 8);
    memcpy(e->base + R128_X64_CP_ZMAXD2, &d, 8);
    memcpy(e->base + R128_X64_CP_ZMAXD2 + 8, &d, 8);
    for (int i = 0; i < 4; i++) {
        uint32_t u = (uint32_t) i;

        memcpy(e->base + R128_X64_CP_LANEIDX + i * 4, &u, 4);
        u = 0x80000000u;
        memcpy(e->base + R128_X64_CP_BIAS31 + i * 4, &u, 4);
        u = 0x00ff00ffu;
        memcpy(e->base + R128_X64_CP_LERPM + i * 4, &u, 4);
        u = 0x00800080u;
        memcpy(e->base + R128_X64_CP_LERPK + i * 4, &u, 4);
        u = 256;
        memcpy(e->base + R128_X64_CP_256I + i * 4, &u, 4);
    }
    {
        /* Constants of the inline texture stage: the coefficients of
           the cubic in r3d_log2f_fast, and the r3d_texcoord_fx grid
           (4096, 2^-12) with 256 and 127. They stand in for the ARM64
           constant registers v11, v12 and v13. */
        static const float poly[4] = { -2.133847707f, 3.010783972f,
                                       -1.029521946f, 0.153918478f };
        static const float texf[4] = { 4096.0f, 1.0f / 4096.0f,
                                       256.0f, 127.0f };

        memcpy(e->base + R128_X64_CP_POLY, poly, 16);
        memcpy(e->base + R128_X64_CP_TEXF, texf, 16);
        /* The vector LOD wants each coefficient in all four lanes, and
           likewise 127.0f (the float exponent bias) and the mantissa
           mask. */
        uint32_t u = 0x007fffffu;

        for (int i = 0; i < 4; i++) {
            for (int k = 0; k < 4; k++)
                memcpy(e->base + R128_X64_CP_POLYV + i * 16 + k * 4,
                       &poly[i], 4);
            memcpy(e->base + R128_X64_CP_127F + i * 4, &texf[3], 4);
            memcpy(e->base + R128_X64_CP_MANT + i * 4, &u, 4);
        }
    }
    e->pool_pos = R128_X64_CP_DYN;
    e->pos      = R128_X64_POOL_END;
}

/* Reserve n pool bytes (align 4/8/16), fill them from p, and return
   the offset. Past the end of the pool it sets overflow, which fails
   the block like a code overflow, and returns the zero slot. */
static int
r128_x64_pool_add(r128_x64_emit_t *e, const void *p, int n, int align)
{
    int off = (e->pool_pos + align - 1) & ~(align - 1);

    if (off + n > R128_X64_POOL_END) {
        e->overflow = 1;
        return R128_X64_CP_ZERO;
    }
    memcpy(e->base + off, p, n);
    e->pool_pos = off + n;
    return off;
}

/* ------------------------------------------------------------------------
 * Compare function codes, shared by the alpha, Z and stencil tests: 0
 * never, 1 <, 2 <=, 3 ==, 4 >=, 5 >, 6 !=, 7 always (SDK: Setting 3D
 * Render States, p. 6-56 / PDF 168, Table 6-25; Tables 6-20 and 6-23
 * give the same codes for the alpha and Z tests). The interpreter's
 * r3d_cmp compares unsigned values, so these are the unsigned
 * conditions. cmp_fwd is the condition for a pass, cmp_inv the
 * condition for a fail, which is what a branch to pix_skip tests. The
 * scalar emitters in this file handle codes 0 and 7 without a compare;
 * the ARM64 backend has the same two tables.
 * ---------------------------------------------------------------------- */
static const int r128_x64_cmp_inv[8] = {
    /* 0 never  */ 0,
    /* 1 a<b    */ X64_CC_AE,
    /* 2 a<=b   */ X64_CC_A,
    /* 3 a==b   */ X64_CC_NE,
    /* 4 a>=b   */ X64_CC_B,
    /* 5 a>b    */ X64_CC_BE,
    /* 6 a!=b   */ X64_CC_E,
    /* 7 always */ 0
};

static const int r128_x64_cmp_fwd[8] = {
    /* 0 never  */ 0,
    /* 1 a<b    */ X64_CC_B,
    /* 2 a<=b   */ X64_CC_BE,
    /* 3 a==b   */ X64_CC_E,
    /* 4 a>=b   */ X64_CC_AE,
    /* 5 a>b    */ X64_CC_A,
    /* 6 a!=b   */ X64_CC_NE,
    /* 7 always */ 0
};

/* ------------------------------------------------------------------------
 * Stack frame, addressed from rsp after `sub rsp, frame`. With the
 * pushes, the frame size leaves rsp 16-byte aligned inside the block,
 * so movaps to the 16-byte slots works, and the call to
 * rage128_texstage_run meets the alignment both ABIs require at a
 * call.
 * ---------------------------------------------------------------------- */
#define R128_X64_SP_COL   0  /* 16B, aligned: col (texture call, SoA)   */
#define R128_X64_SP_RX1   16 /* dword: rx1                              */
#define R128_X64_SP_ZI    20 /* dword: zi                               */
#define R128_X64_SP_ZL    24 /* qword: zline / SoA 16-bit Z lanes       */
#define R128_X64_SP_DITH  32 /* dword: packed bayer row thresholds      */
#define R128_X64_SP_AUX   36 /* dword: aux rect y-active mask           */
#define R128_X64_SP_STEN  40 /* dword: packed {sbuf|sres<<8|zres<<9}    */
#define R128_X64_SP_KMASK 44 /* dword: SoA lane cover mask              */
#define R128_X64_SP_STIP  44
/* SoA vector-loop slots (16B each, 16-aligned) */
#define R128_X64_SP_SOA_ZIV  48 /* zi lanes                           */
#define R128_X64_SP_SOA_SRCR 64 /* float channels for blend/spec/fog  */
#define R128_X64_SP_SOA_SRCG 80
#define R128_X64_SP_SOA_SRCB 96
#define R128_X64_SP_SOA_SRCA 112
#define R128_X64_SP_SOA_TFA  128 /* raw zline lanes (table fog)        */
#define R128_X64_SP_SOA_TFB  144
/* Bayer adds stay live across groups in these slots. The first holds
   bay>>1 for five-bit RGB or bay for 4444; the second holds bay>>2 for
   565 green. Alpha in 1555 does not consume a Bayer add. */
#define R128_X64_SP_SOA_BAY1 160
#define R128_X64_SP_SOA_BAY2 176
#define R128_X64_SP_SOA_STQ  192 /* stencil packed lanes               */
#define R128_X64_SP_XMM      208 /* Win64 only: xmm6..xmm15 saves (160B)    */
#if R128_X64_ABI_WIN
#    define R128_X64_FRAME 376 /* 8 pushes: keeps rsp 16-aligned         */
#    define R128_X64_NPUSH 8   /* rbx rbp rsi rdi r12 r13 r14 r15        */
#else
#    define R128_X64_FRAME 216 /* 6 pushes: rbx rbp r12 r13 r14 r15      */
#    define R128_X64_NPUSH 6
#endif
/* Frame extension of a textured SoA block, above the base frame; the
   frame grows by R128_X64_SOAT_EXT, a multiple of 16. The base SoA
   area (offsets 48 to 207) is also where the scalar texture stage keeps
   its stash words (defined in vid_ati_rage128_codegen_x86_64_tex.h).
   During the vector loop the per-lane texel fetches use R128_X64_TS_LW,
   R128_X64_TS_LH, R128_X64_TS_BASE, R128_X64_TS_MASK, R128_X64_TS_TEXP
   and R128_X64_TS_PAL, and the S3TC and YUV decode subroutines use
   R128_X64_TS_DXT0 to R128_X64_TS_DXT2 as scratch, so every vector
   value that has to survive a gather lives up here. The SRC slots (the channels kept for blend, specular and fog)
   stay in the base area: they are written only after the group's last
   fetch, and the fetch words are written again for each group.

   R128_X64_FRAME is 8 mod 16 on both ABIs, which with the pushes makes
   rsp 16-byte aligned inside the block, so the 16-byte slots here start
   8 bytes above R128_X64_FRAME. A movaps to a misaligned address raises
   a general-protection fault. */
#define R128_X64_SOAT_EXT     496
#define R128_X64_SOAT_BASE    (R128_X64_FRAME + 8)       /* 16-aligned         */
#define R128_X64_SP_SOAT_C0   (R128_X64_SOAT_BASE + 0)   /* int color r,g,b,a  */
#define R128_X64_SP_SOAT_U0   (R128_X64_SOAT_BASE + 64)  /* wrapped lane u0    */
#define R128_X64_SP_SOAT_V0   (R128_X64_SOAT_BASE + 80)  /* wrapped lane v0    */
#define R128_X64_SP_SOAT_U1   (R128_X64_SOAT_BASE + 96)  /* bilinear u1/v1 and */
#define R128_X64_SP_SOAT_V1   (R128_X64_SOAT_BASE + 112) /* the 8.8 weights    */
#define R128_X64_SP_SOAT_WU   (R128_X64_SOAT_BASE + 128)
#define R128_X64_SP_SOAT_WV   (R128_X64_SOAT_BASE + 144)
#define R128_X64_SP_SOAT_BAY1 (R128_X64_SOAT_BASE + 160) /* row bayer adds     */
#define R128_X64_SP_SOAT_BAY2 (R128_X64_SOAT_BASE + 176)
#define R128_X64_SP_SOAT_DC   (R128_X64_SOAT_BASE + 192) /* r11 across gathers */
#define R128_X64_SP_SOAT_ZC   (R128_X64_SOAT_BASE + 200) /* r10 across gathers */
#define R128_X64_SP_SOAT_VLW  (R128_X64_SOAT_BASE + 208) /* per-lane level lw  */
#define R128_X64_SP_SOAT_VLH  (R128_X64_SOAT_BASE + 224) /* per-lane level lh  */
#define R128_X64_SP_SOAT_SL   (R128_X64_SOAT_BASE + 240) /* per-lane mip slot  */
#define R128_X64_SP_SOAT_RHW  (R128_X64_SOAT_BASE + 256) /* rhw lanes (LOD)    */
/* Two stages (sec_en): stage 0 leaves its combine output in P0 as
   float channels, because the interpreter hands stage 1 the float
   col[] without converting it. The perspective reciprocal is computed
   once for both stages and each reloads it from IR. Both slots sit
   above every per-stage slot. */
#define R128_X64_SP_SOAT_P0 (R128_X64_SOAT_BASE + 272) /* stage-0 out r,g,b,a */
#define R128_X64_SP_SOAT_IR (R128_X64_SOAT_BASE + 336) /* 1/rhw lanes (dual) */
/* Minification and magnification filters that differ
   (r128_jit_minb_split): s and t are kept in TS and TT across the
   minification pass, that pass's texel lanes wait in CA during the
   magnification rerun, and MSK marks the lanes with lod > 0, which keep
   the minification result. */
#define R128_X64_SP_SOAT_TS  (R128_X64_SOAT_BASE + 352) /* s stash           */
#define R128_X64_SP_SOAT_TT  (R128_X64_SOAT_BASE + 368) /* t stash           */
#define R128_X64_SP_SOAT_CA  (R128_X64_SOAT_BASE + 384) /* MIN-filter texels */
#define R128_X64_SP_SOAT_MSK (R128_X64_SOAT_BASE + 400) /* lod > 0 lanes     */
/* Trilinear: the second mip slot of each lane, the 8.8 level blend
   weight, and the saved return address of the per-stage coord/gather
   subroutine. That subroutine pops its return address into SLR on
   entry, so its body runs at the block's rsp: every frame offset stays
   valid, and the decode subroutines it calls find one return address
   on the stack, as their R128_X64_SUBF bias of 8 assumes. */
#define R128_X64_SP_SOAT_SLB (R128_X64_SOAT_BASE + 416) /* level-B mip slot  */
#define R128_X64_SP_SOAT_WM  (R128_X64_SOAT_BASE + 432) /* 8.8 level weight  */
#define R128_X64_SP_SOAT_SLR (R128_X64_SOAT_BASE + 448) /* qword: sub return */
/* Packed stencil lanes of a textured SoA block. The base slot
   R128_X64_SP_SOA_STQ covers R128_X64_TS_TRAW (offset 200), which the
   CI4/CI8 texel fetch writes during a gather. */
#define R128_X64_SP_SOAT_STQ (R128_X64_SOAT_BASE + 464)

/* Build the blend factor for an ALPHA_BLND_SRC or ALPHA_BLND_DST code
   in xd, from col (xmm11), the destination color dc (xmm12) and a 1.0f
   splat (xmm13); xmm0 and xmm1 are scratch. The factors are those of
   the interpreter's r3d_blend_factor, after SDK Tables 6-17 and 6-18
   (SDK: Setting 3D Render States, p. 6-49 / PDF 161), whose code
   numbers are listed in SDK Table F-18 (SDK: Type-0 CCE Packet, p. F-29
   / PDF 319):

     0 zero            4 src alpha        8 dst color
     1 one             5 1 - src alpha    9 1 - dst color
     2 src color       6 dst alpha       10 BLEND_SRCALPHASAT
     3 1 - src color   7 1 - dst alpha

   Codes 11 and 12 are handled by the caller when they are the source
   code; anywhere else they and the reserved codes 13 to 15 give one, as
   in r3d_blend_factor. The ARM64 version is
   r128_a64_emit_blend_factor. */
static void
r128_x64_emit_blend_factor(r128_x64_emit_t *e, int xd, uint32_t code)
{
    switch (code & 0xf) {
        case 0x0:
            r128_x64_xorps(e, xd, xd);
            break;
        case 0x2:
            r128_x64_movaps_rr(e, xd, 11);
            break;
        case 0x3:
            r128_x64_movaps_rr(e, xd, 13);
            r128_x64_subps(e, xd, 11);
            break;
        case 0x4:
            r128_x64_pshufd(e, xd, 11, 0xFF);
            break;
        case 0x5:
            r128_x64_pshufd(e, xd, 11, 0xFF);
            r128_x64_movaps_rr(e, 0, 13);
            r128_x64_subps(e, 0, xd);
            r128_x64_movaps_rr(e, xd, 0);
            break;
        case 0x6:
            r128_x64_pshufd(e, xd, 12, 0xFF);
            break;
        case 0x7:
            r128_x64_pshufd(e, xd, 12, 0xFF);
            r128_x64_movaps_rr(e, 0, 13);
            r128_x64_subps(e, 0, xd);
            r128_x64_movaps_rr(e, xd, 0);
            break;
        case 0x8:
            r128_x64_movaps_rr(e, xd, 12);
            break;
        case 0x9:
            r128_x64_movaps_rr(e, xd, 13);
            r128_x64_subps(e, xd, 12);
            break;
        case 0xa:
            {
                /* BLEND_SRCALPHASAT on either side, as r3d_blend_factor
                   computes it: f = sc[3] < 1 - dc[3] ? sc[3] : 1 - dc[3],
                   factor {f, f, f, 1}. For a NaN operand comiss reports
                   unordered and this picks sc[3] where the C picks
                   1 - dc[3]; the colors here are finite, so that case does
                   not arise. */
                int b1;

                r128_x64_pshufd(e, xd, 12, 0xFF);
                r128_x64_movaps_rr(e, 1, 13);
                r128_x64_subps(e, 1, xd);
                r128_x64_movaps_rr(e, xd, 1);    /* xd = 1-dc3 splat */
                r128_x64_pshufd(e, 0, 11, 0xFF); /* sc3 splat        */
                r128_x64_comiss(e, 0, xd);
                b1 = r128_x64_jcc(e, X64_CC_AE);
                r128_x64_movaps_rr(e, xd, 0);
                r128_x64_patch32(e, b1, r128_x64_here(e));
                r128_x64_splat0(e, xd, xd);
                r128_x64_insertps(e, xd, 13, 3, 0);
                break;
            }
        case 0x1:
        default:
            r128_x64_movaps_rr(e, xd, 13);
            break;
    }
}

/* Apply one stencil operation to the stencil byte in rs, result in rd.
   op is one of the Z_STEN_CNTL_C fields STEN_SFAIL_OP, STEN_ZPASS_OP or
   STEN_ZFAIL_OP. The SDK documents codes 0 to 5 (SDK: Setting 3D Render
   States, p. 6-57 / PDF 169, Table 6-26). Codes 6 and 7 are not
   documented; Mesa r128 r128_state.c r128DDStencilOpSeparate programs
   them for GL_INCR_WRAP and GL_DECR_WRAP, and the interpreter models
   them as a wrapping increment and decrement. Codes 3 and 4 saturate
   in the interpreter, matching the GL_INCR and GL_DECR that Mesa maps
   to them. As in the ARM64 emitter, the bits of rd above bit 7 are left
   as they fall: the write-mask merge that follows keeps only the low
   8. */
static void
r128_x64_emit_sten_op(r128_x64_emit_t *e, int rd, int rs, uint32_t op,
                      uint32_t sref)
{
    int b1;

    switch (op & 7) {
        case 0: /* KEEP    */
            r128_x64_mov_r_r(e, 0, rd, rs);
            break;
        case 1: /* ZERO    */
            r128_x64_mov_r32_imm32(e, rd, 0);
            break;
        case 2: /* REPLACE */
            r128_x64_mov_r32_imm32(e, rd, sref & 0xff);
            break;
        case 3: /* INCR sat: rs==0xff ? 0xff : rs+1 */
            r128_x64_mov_r_r(e, 0, rd, rs);
            r128_x64_alu_r_imm(e, 0, 0, rd, 1);
            r128_x64_alu_r_imm(e, 7, 0, rs, 0xff);
            r128_x64_cmov(e, X64_CC_E, 0, rd, rs);
            break;
        case 4: /* DECR sat: rs==0 ? 0 : rs-1 */
            r128_x64_mov_r_r(e, 0, rd, rs);
            r128_x64_alu_r_imm(e, 5, 0, rd, 1);
            r128_x64_alu_r_r(e, 0x85, 0, rs, rs);
            b1 = r128_x64_jcc(e, X64_CC_NE);
            r128_x64_mov_r32_imm32(e, rd, 0);
            r128_x64_patch32(e, b1, r128_x64_here(e));
            break;
        case 5: /* INVERT   */
            r128_x64_mov_r_r(e, 0, rd, rs);
            r128_x64_not_r32(e, rd);
            break;
        case 6: /* INCR wrap */
            r128_x64_mov_r_r(e, 0, rd, rs);
            r128_x64_alu_r_imm(e, 0, 0, rd, 1);
            break;
        default: /* 7 DECR wrap */
            r128_x64_mov_r_r(e, 0, rd, rs);
            r128_x64_alu_r_imm(e, 5, 0, rd, 1);
            break;
    }
}

/* The SoA loop: a vector loop that runs four pixels at a time ahead of
   the scalar loop. */
#include <86box/vid_ati_rage128_codegen_x86_64_soa.h>

/* The inline texture stage, its gate and its emitters, in a separate
   header as on ARM64. It uses the SoA header's encoder for the 0F 38
   opcode map (r128_x64_sse38_rr) and the frame slots above, so it comes
   after both. */
#include <86box/vid_ati_rage128_codegen_x86_64_tex.h>

/* The textured SoA group body. It fetches texels with the inline
   stage's texel emitter, so it is included after that header. */
#include <86box/vid_ati_rage128_codegen_x86_64_soa_tex.h>

/* Never-pass alpha or Z test (r128_jit_never_pass). */
static int
r128_x64_never_pass(const rage128_draw_state_t *ds)
{
    return r128_jit_never_pass(ds);
}

/* ------------------------------------------------------------------------
 * The states this backend compiles: exactly those r128_jit_state_can
 * accepts. For those the scalar loop emits shading, Z, dither, alpha
 * blend, both texture stages with every format, filter and mip mode
 * and chroma keys, alpha test, specular, vertex and table fog, the aux
 * scissors and stencil. Textures in the inline family are sampled in
 * the block; the others call the interpreter's rage128_texstage_run
 * for each pixel. Polygon stipple uses the scalar loop, reading its
 * pattern through the captured raster state. rage128_jit_get_block
 * refuses unstaged tiled textures before this gate is asked. The ARM64
 * gate adds a test that the 32-bit Z write mask fits an AND immediate;
 * x86-64 needs none, since mov r32, imm32 loads any mask.
 * ---------------------------------------------------------------------- */
static int
r128_jit_x64_can(const rage128_draw_state_t *ds)
{
    return r128_jit_state_can(ds);
}

/* ------------------------------------------------------------------------
 * Generator, following r128_jit_arm64_generate_1: a scalar loop over
 * the row, with the SoA vector loop ahead of it for the states its
 * gates accept (untextured states, and inline-family textured states
 * that the SoA texture gate accepts). Textured states outside the
 * inline family call rage128_texstage_run for each pixel, saving and
 * restoring the live state around the call as the ABI requires.
 * Returns the block length, -1 if the block overflowed, or -2 if the
 * state is refused. no_soa leaves out the vector loop;
 * r128_jit_x64_generate sets it to retry after an overflow. Each phase
 * below is one static function; r128_x64_gen_t carries the block state
 * between them, and r128_jit_x64_generate_1 calls them in order.
 * ---------------------------------------------------------------------- */
typedef struct {
    r128_x64_emit_t             e;
    const rage128_draw_state_t *ds;
    int                         bppsh, z_on, z_step, sten_on, dith_on, tex_on;
    int                         tex_inline, soa_on, soa_tex, frame;
    int                         aux_on, aux_add_mask, aux_row_out;
    int                         bails[12], nbail;
    int                         skips[24], nskip;
    int                         loop_head, b_done;
} r128_x64_gen_t;

static void
r128_x64_gen_setup(r128_x64_gen_t *g, uint8_t *code,
                   const rage128_draw_state_t *ds, int no_soa)
{
    int bppsh   = (ds->bpp == 4) ? 2 : 1;
    int z_on    = ds->z_en;
    int sten_on = ds->sten_on;
    int dith_on = ds->dither && ds->dst_dt != 6;
    int tex_on  = ds->tex_en || ds->sec_en;
    /* States in the inline family (vid_ati_rage128_codegen_x86_64_tex.h)
       sample the texture in the block; the others call
       rage128_texstage_run for each pixel. Table fog takes the call path
       even for inline-family states, as in the ARM64 generator, so both
       backends emit the same kind of block for a given state;
       rage128_jit_tex_inline in vid_ati_rage128_jit.c states the same
       rule. */
    int tex_inline = tex_on && r128_x64_texinline_can(ds)
        && !(ds->fog_en && ds->fog_table_en);
    int aux_on       = ds->aux_on;
    int aux_add_mask = 0;
    int soa_on, soa_tex, frame;

    if (aux_on)
        for (int i = 0; i < 3; i++)
            if ((ds->aux_cntl & (1u << (i * 2))) && !(ds->aux_cntl & (2u << (i * 2))))
                aux_add_mask |= 1 << i;

    /* soa_selftex is set when a texture stage's sample range overlaps
       the color or Z rows the draw writes. The vector loop fetches the
       texels of all four lanes before it stores any of them, while the
       interpreter stores pixel n before it fetches pixel n+1, so such a
       draw keeps the scalar block. */
    soa_tex = !no_soa && !ds->soa_selftex && tex_inline
        && r128_jit_x64_soa_tex_can(ds);
    soa_on = soa_tex || (!no_soa && !tex_on && r128_jit_x64_soa_can(ds));
    frame  = R128_X64_FRAME + (soa_tex ? R128_X64_SOAT_EXT : 0);

    g->e = (r128_x64_emit_t) {
        .base     = code,
        .tex_sub  = { -1, -1 },
        .soa_csub = { -1, -1 },
        .soa_bay1 = R128_X64_SP_SOA_BAY1,
        .soa_bay2 = R128_X64_SP_SOA_BAY2,
        .soa_stq  = R128_X64_SP_SOA_STQ
    };
    g->ds           = ds;
    g->bppsh        = bppsh;
    g->z_on         = z_on;
    /* Table fog reads interpolated depth even without the Z test, so
       its scalar loop loads and steps the same double DDA without
       enabling the Z test or its writes. */
    g->z_step       = z_on || (ds->fog_en && ds->fog_table_en);
    g->sten_on      = sten_on;
    g->dith_on      = dith_on;
    g->tex_on       = tex_on;
    g->tex_inline   = tex_inline;
    g->soa_on       = soa_on;
    g->soa_tex      = soa_tex;
    g->frame        = frame;
    g->aux_on       = aux_on;
    g->aux_add_mask = aux_add_mask;
    g->aux_row_out  = -1;
    g->nbail        = 0;
    g->nskip        = 0;
}

/* constant pool: the shared constants, then the per-state inline-texture
   and SoA-texture entries */
static void
r128_x64_gen_pool(r128_x64_gen_t *g)
{
    const rage128_draw_state_t *ds         = g->ds;
    int                         tex_inline = g->tex_inline, soa_tex = g->soa_tex;

    r128_x64_pool_init(&g->e, ds);
    if (tex_inline) {
        /* per-state inline-texture constants (ARM64 v10/v11 lanes) */
        float texwh[4] = { ds->texw0, ds->texh0, ds->texw1, ds->texh1 };
        float lodm[2]  = { ds->lod_bias, -1000.0f };

        g->e.cp_texwh = r128_x64_pool_add(&g->e, texwh, 16, 4);
        g->e.cp_lodm  = r128_x64_pool_add(&g->e, lodm, 8, 4);
    }
    if (soa_tex) {
        /* the r3d_texcoord_fx constants 4096 and 2^-12, each in all
           four lanes for the vector code; the scalar stage reads them
           as single lanes of R128_X64_CP_TEXF */
        float tfx[8] = { 4096.0f, 4096.0f, 4096.0f, 4096.0f,
                         1.0f / 4096.0f, 1.0f / 4096.0f,
                         1.0f / 4096.0f, 1.0f / 4096.0f };

        g->e.cp_tfx   = r128_x64_pool_add(&g->e, tfx, 32, 16);
        g->e.soa_bay1 = R128_X64_SP_SOAT_BAY1;
        g->e.soa_bay2 = R128_X64_SP_SOAT_BAY2;
        g->e.soa_stq  = R128_X64_SP_SOAT_STQ;
        for (int st = 0; st < (ds->sec_en ? 2 : 1); st++) {
            /* vector LOD + per-lane mip slot splats, per stage */
            float    lodv[24];
            float    topf = (float) ds->sh[st].top;
            uint32_t topi = (uint32_t) ds->sh[st].top;

            if (!(st ? ds->need_lod2 : ds->need_lod))
                continue;
            for (int k = 0; k < 4; k++) {
                lodv[k]      = st ? ds->texw1 : ds->texw0;
                lodv[4 + k]  = st ? ds->texh1 : ds->texh0;
                lodv[8 + k]  = ds->lod_bias;
                lodv[12 + k] = -1000.0f;
                lodv[16 + k] = topf;
                memcpy(&lodv[20 + k], &topi, 4);
            }
            g->e.cp_lodv[st] = r128_x64_pool_add(&g->e, lodv, 96, 16);
        }
    }
}

/* prologue: pushes, frame, Win64 xmm saves, argument moves, and the
   per-triangle values loaded from tri */
static void
r128_x64_gen_prologue(r128_x64_gen_t *g)
{
    int frame = g->frame;

    r128_x64_push(&g->e, X64_RBX);
    r128_x64_push(&g->e, X64_RBP);
#if R128_X64_ABI_WIN
    r128_x64_push(&g->e, X64_RSI);
    r128_x64_push(&g->e, X64_RDI);
#endif
    r128_x64_push(&g->e, X64_R12);
    r128_x64_push(&g->e, X64_R13);
    r128_x64_push(&g->e, X64_R14);
    r128_x64_push(&g->e, X64_R15);
    r128_x64_alu_r_imm(&g->e, 5, 1, X64_RSP, frame); /* sub rsp, n */
#if R128_X64_ABI_WIN
    for (int i = 0; i < 10; i++)
        r128_x64_movaps_st(&g->e, 6 + i, X64_RSP, R128_X64_SP_XMM + i * 16);
#endif

        /* Move the arguments into the register map. argb is the offset from
           rsp of the first stack byte above the return address. */
#if R128_X64_ABI_WIN
    /* Win64: rcx = tri, rdx = e0, r8 = e1, r9 = e2. The other four
       arguments are on the stack above the caller's 32-byte shadow
       space: zline, drow, zrow and py at 40, 48, 56 and 64 bytes above
       the rsp seen on entry, which is argb + 32 to argb + 56. */
    {
        int argb = frame + R128_X64_NPUSH * 8 + 8;

        r128_x64_mov_r_r(&g->e, 1, X64_RBX, X64_RCX);       /* tri            */
        r128_x64_mov_r_r(&g->e, 1, X64_RSI, X64_RDX);       /* e0             */
        r128_x64_mov_r_r(&g->e, 1, X64_RDI, X64_R8);        /* e1             */
        r128_x64_mov_r_r(&g->e, 1, X64_RBP, X64_R9);        /* e2             */
        r128_x64_movsd_ld(&g->e, 2, X64_RSP, argb + 32);    /* zline */
        r128_x64_ld(&g->e, 0, X64_R14, X64_RSP, argb + 40); /* drow  */
        r128_x64_ld(&g->e, 0, X64_R15, X64_RSP, argb + 48); /* zrow  */
        r128_x64_ld(&g->e, 0, X64_RCX, X64_RSP, argb + 56); /* py    */
    }
#else
    /* System V: rdi = tri, rsi = e0, rdx = e1, rcx = e2, r8d = drow,
       r9d = zrow, xmm0 = zline, and py, the seventh integer argument,
       on the stack 8 bytes above the rsp seen on entry (argb). The moves
       are ordered so that no register is overwritten before it is read:
       tri leaves rdi before e1 goes there, and e2 leaves rcx before py
       is loaded into it. */
    {
        int argb = frame + R128_X64_NPUSH * 8 + 8;

        r128_x64_mov_r_r(&g->e, 1, X64_RBX, X64_RDI);  /* tri            */
        r128_x64_mov_r_r(&g->e, 1, X64_RDI, X64_RDX);  /* e1             */
        r128_x64_mov_r_r(&g->e, 1, X64_R14, X64_R8);   /* drow           */
        r128_x64_mov_r_r(&g->e, 1, X64_R15, X64_R9);   /* zrow           */
        r128_x64_mov_r_r(&g->e, 1, X64_RBP, X64_RCX);  /* e2             */
        r128_x64_ld(&g->e, 0, X64_RCX, X64_RSP, argb); /* py             */
        r128_x64_movsd_rr(&g->e, 2, 0);                /* zline          */
    }
#endif

    /* per-triangle constants */
    r128_x64_ld(&g->e, 1, X64_R8, X64_RBX, (int) offsetof(r128_jit_tri_t, vram));
    r128_x64_ld(&g->e, 0, X64_R12, X64_RBX, (int) offsetof(r128_jit_tri_t, x0));
    r128_x64_ld(&g->e, 0, X64_R13, X64_RBX, (int) offsetof(r128_jit_tri_t, x1));
    r128_x64_movss_ld(&g->e, 4, X64_RBX, (int) offsetof(r128_jit_tri_t, invs));
    r128_x64_movups_ld(&g->e, 5, X64_RBX, (int) offsetof(r128_jit_tri_t, vca));
    r128_x64_movups_ld(&g->e, 6, X64_RBX, (int) offsetof(r128_jit_tri_t, vcb));
    r128_x64_movups_ld(&g->e, 7, X64_RBX, (int) offsetof(r128_jit_tri_t, vcc));
    if (g->z_step)
        r128_x64_movsd_ld(&g->e, 3, X64_RBX, (int) offsetof(r128_jit_tri_t, dZdx));
    r128_x64_mov_r32_imm32(&g->e, X64_R9, 0xffffffffu); /* rx0 = -1 */
    r128_x64_mov_r32_imm32(&g->e, X64_RAX, 0xffffffffu);
    r128_x64_st(&g->e, 0, X64_RAX, X64_RSP, R128_X64_SP_RX1);
}

/* Aux scissors, once per row. ds->aux_cntl holds the AUX_SC_CNTL enable
   and mode bit pairs of the enabled rectangles only (RRG: AUX_SC_CNTL,
   p. 3-156 / PDF 174): bit 2i enables rectangle i, and bit 2i+1 makes
   it subtractive when set, additive when clear. From py (still in ecx)
   this builds the mask of enabled rectangles whose rows include py and
   stores it in R128_X64_SP_AUX. The rectangle bounds are immediates
   taken from the draw state. */
static void
r128_x64_gen_row_aux(r128_x64_gen_t *g)
{
    const rage128_draw_state_t *ds           = g->ds;
    int                         aux_add_mask = g->aux_add_mask;

    r128_x64_mov_r32_imm32(&g->e, X64_RDX, 0);
    for (int i = 0; i < 3; i++) {
        int b1, b2;

        if (!(ds->aux_cntl & (1u << (i * 2))))
            continue;
        r128_x64_alu_r_imm(&g->e, 7, 0, X64_RCX, ds->aux_y0[i]);
        b1 = r128_x64_jcc(&g->e, X64_CC_L);
        r128_x64_alu_r_imm(&g->e, 7, 0, X64_RCX, ds->aux_y1[i]);
        b2 = r128_x64_jcc(&g->e, X64_CC_G);
        r128_x64_alu_r_imm(&g->e, 1, 0, X64_RDX, 1 << i);
        r128_x64_patch32(&g->e, b1, r128_x64_here(&g->e));
        r128_x64_patch32(&g->e, b2, r128_x64_here(&g->e));
    }
    if (aux_add_mask) {
        /* When additive rectangles are enabled, the interpreter
           (rage128_aux_sc_pass) passes a pixel only if one of them
           contains it. If none covers this row, no pixel can pass: exit
           with nothing written. */
        r128_x64_mov_r_r(&g->e, 0, X64_RAX, X64_RDX);
        r128_x64_test_r32_imm(&g->e, X64_RAX, (uint32_t) aux_add_mask);
        g->aux_row_out = r128_x64_jcc(&g->e, X64_CC_E);
    }
    r128_x64_st(&g->e, 0, X64_RDX, X64_RSP, R128_X64_SP_AUX);
}

/* The modeled pattern follows destination coordinates and stays outside
   the block key. Load its row through the captured raster state while
   ecx holds py. Stipple uses only the scalar loop, so its frame word
   shares the inactive vector coverage-mask slot across texture calls. */
static void
r128_x64_gen_row_stipple(r128_x64_gen_t *g)
{
    r128_x64_ld(&g->e, 1, X64_RAX, X64_RBX, (int) offsetof(r128_jit_tri_t, texctx));
    r128_x64_ld(&g->e, 1, X64_RAX, X64_RAX, (int) offsetof(r3d_texctx_t, rs));
    r128_x64_mov_r_r(&g->e, 0, X64_RDX, X64_RCX);
    r128_x64_alu_r_imm(&g->e, 4, 0, X64_RDX, 31);
    r128_x64_rex(&g->e, 0, X64_RAX, X64_RDX, X64_RAX);
    r128_x64_e8(&g->e, 0x8B);
    r128_x64_modrm_sib(&g->e, X64_RAX, X64_RAX, X64_RDX, 2,
                       (int) offsetof(rage128_raster_state_t, stipple));
    r128_x64_st(&g->e, 0, X64_RAX, X64_RSP, R128_X64_SP_STIP);
}

/* Dither thresholds for this row: row py & 3 of the interpreter's 4x4
   table r3d_bayer4, four bytes packed little-endian into the dword at
   R128_X64_SP_DITH. The two 64-bit immediates hold rows 0-1 and rows
   2-3; bit 1 of py (still in ecx) picks the pair and bit 0 the dword
   within it. */
static void
r128_x64_gen_row_dither(r128_x64_gen_t *g)
{
    r128_x64_mov_r64_imm64(&g->e, X64_RAX, 0x060E040C0A020800ull);
    r128_x64_mov_r64_imm64(&g->e, X64_RDX, 0x050D070F09010B03ull);
    r128_x64_test_r32_imm(&g->e, X64_RCX, 2);
    r128_x64_cmov(&g->e, X64_CC_NE, 1, X64_RAX, X64_RDX);
    r128_x64_mov_r_r(&g->e, 1, X64_RDX, X64_RAX);
    r128_x64_shift_imm(&g->e, 5, 1, X64_RDX, 32);
    r128_x64_test_r32_imm(&g->e, X64_RCX, 1);
    r128_x64_cmov(&g->e, X64_CC_NE, 0, X64_RAX, X64_RDX);
    r128_x64_st(&g->e, 0, X64_RAX, X64_RSP, R128_X64_SP_DITH);
}

/* The SoA vector loop, ahead of the scalar loop: first the per-stage
   decode and coord subroutines, each placed behind a jmp that skips
   it, then the SoA prologue and the loop itself. */
static void
r128_x64_gen_soa(r128_x64_gen_t *g)
{
    const rage128_draw_state_t *ds      = g->ds;
    int                         soa_tex = g->soa_tex, soa_on = g->soa_on;

    for (int st = 0; soa_tex && st < (ds->sec_en ? 2 : 1); st++) {
        /* one S3TC (dt 0) or YUV (dt 11, 12, 14) decode subroutine per
           stage, called from both the vector loop and the scalar
           loop's texture stage */
        const r3d_stage_hdr_t *h = &ds->sh[st];
        int                    b_over;

        if (!(h->dt == 0 || h->dt == 11 || h->dt == 12 || h->dt == 14))
            continue;
        b_over           = r128_x64_jmp(&g->e);
        g->e.tex_sub[st] = (h->dt == 0) ? r128_x64_emit_dxt_sub(&g->e, h)
                                        : r128_x64_emit_yuv_sub(&g->e, h);
        r128_x64_patch32(&g->e, b_over, r128_x64_here(&g->e));
    }
    for (int st = 0; soa_tex && st < (ds->sec_en ? 2 : 1); st++) {
        /* the coord/gather subroutine of each trilinear stage, called
           by both level passes; its body calls the decode subroutine
           above */
        const r3d_stage_hdr_t *h       = &ds->sh[st];
        int                    has_lod = st ? ds->need_lod2 : ds->need_lod;
        int                    b_over;

        if (!r128_jit_minb_tri(h->minb, h->mipdis, has_lod))
            continue;
        b_over            = r128_x64_jmp(&g->e);
        g->e.soa_csub[st] = r128_x64_emit_soa_coord_sub(&g->e, ds, st);
        r128_x64_patch32(&g->e, b_over, r128_x64_here(&g->e));
    }

    if (soa_on) {
        r128_x64_emit_soa_prologue(&g->e, ds);
        r128_x64_emit_soa_loop(&g->e, ds, g->bails, &g->nbail);
    }
}

/* ---- Scalar row loop. It runs the whole row, or in a block with a
   vector loop the pixels that loop leaves; the vector loop's bail
   branches land at the loop head. ---- */
/* loop head: exit when px > x1 */
static void
r128_x64_gen_loop_head(r128_x64_gen_t *g)
{
    g->loop_head = r128_x64_here(&g->e);
    for (int i = 0; i < g->nbail && !g->e.overflow; i++)
        r128_x64_patch32(&g->e, g->bails[i], g->loop_head);
    r128_x64_alu_r_r(&g->e, 0x39, 0, X64_R12, X64_R13); /* cmp r12d, r13d */
    g->b_done = r128_x64_jcc(&g->e, X64_CC_G);
}

/* Coverage, then the aux scissors. The caller passes e0..e2 with the
   triangle's winding folded in, so a pixel is inside when all three
   are >= 0; OR-ing them sets SF when any is negative. */
static void
r128_x64_gen_coverage(r128_x64_gen_t *g)
{
    const rage128_draw_state_t *ds     = g->ds;
    int                         aux_on = g->aux_on, aux_add_mask = g->aux_add_mask;

    r128_x64_mov_r_r(&g->e, 1, X64_RAX, X64_RSI);
    r128_x64_alu_r_r(&g->e, 0x09, 1, X64_RAX, X64_RDI); /* or rax, rdi   */
    r128_x64_alu_r_r(&g->e, 0x09, 1, X64_RAX, X64_RBP); /* or sets SF    */
    g->skips[g->nskip++] = r128_x64_jcc(&g->e, X64_CC_S);

    if (aux_on) {
        /* As in the interpreter, after coverage and before Z: an
           enabled subtractive rectangle that contains the pixel rejects
           it, and when additive rectangles are enabled the pixel must
           lie in one of them. */
        int okbr[3], nok = 0;

        r128_x64_ld(&g->e, 0, X64_RDX, X64_RSP, R128_X64_SP_AUX);
        for (int i = 0; i < 3; i++) {
            int b1, b2;

            if (!(ds->aux_cntl & (2u << (i * 2))))
                continue; /* enabled subtractive rects only */
            r128_x64_test_r32_imm(&g->e, X64_RDX, 1u << i);
            b1 = r128_x64_jcc(&g->e, X64_CC_E);
            r128_x64_alu_r_imm(&g->e, 7, 0, X64_R12, ds->aux_x0[i]);
            b2 = r128_x64_jcc(&g->e, X64_CC_L);
            r128_x64_alu_r_imm(&g->e, 7, 0, X64_R12, ds->aux_x1[i]);
            g->skips[g->nskip++] = r128_x64_jcc(&g->e, X64_CC_LE);
            r128_x64_patch32(&g->e, b1, r128_x64_here(&g->e));
            r128_x64_patch32(&g->e, b2, r128_x64_here(&g->e));
        }
        if (aux_add_mask) {
            for (int i = 0; i < 3; i++) {
                int b1, b2;

                if (!(aux_add_mask & (1 << i)))
                    continue;
                r128_x64_test_r32_imm(&g->e, X64_RDX, 1u << i);
                b1 = r128_x64_jcc(&g->e, X64_CC_E);
                r128_x64_alu_r_imm(&g->e, 7, 0, X64_R12, ds->aux_x0[i]);
                b2 = r128_x64_jcc(&g->e, X64_CC_L);
                r128_x64_alu_r_imm(&g->e, 7, 0, X64_R12, ds->aux_x1[i]);
                okbr[nok++] = r128_x64_jcc(&g->e, X64_CC_LE);
                r128_x64_patch32(&g->e, b1, r128_x64_here(&g->e));
                r128_x64_patch32(&g->e, b2, r128_x64_here(&g->e));
            }
            g->skips[g->nskip++] = r128_x64_jmp(&g->e); /* no additive rect hit */
            for (int k = 0; k < nok; k++)
                r128_x64_patch32(&g->e, okbr[k], r128_x64_here(&g->e));
        }
    }
    if (ds->stip_en) {
        /* A clear modeled pattern bit discards before Z and stencil.
           The 32-bit shift masks its count to five bits, so complementing
           px selects bit 31 - (px & 31) without changing live px. */
        r128_x64_ld(&g->e, 0, X64_RAX, X64_RSP, R128_X64_SP_STIP);
        r128_x64_mov_r_r(&g->e, 0, X64_RCX, X64_R12);
        r128_x64_not_r32(&g->e, X64_RCX);
        r128_x64_shr_cl(&g->e, 0, X64_RAX);
        r128_x64_test_r32_imm(&g->e, X64_RAX, 1);
        g->skips[g->nskip++] = r128_x64_jcc(&g->e, X64_CC_E);
    }
}

/* Z and stencil: clamp and quantize zline, resolve the tested cell into
   r10, then either the Z test, which branches to pix_skip on a fail, or
   the stencil block, which only records the results for later. */
static void
r128_x64_gen_zsten(r128_x64_gen_t *g)
{
    const rage128_draw_state_t *ds   = g->ds;
    int                         z_on = g->z_on, sten_on = g->sten_on;
    int                         b_zvram, b_zres;

    if (z_on) {
        /* zc = zline clamped to 0..1 as the interpreter does it:
           xmm15 = (zline > 0.0) ? zline : 0.0, then values above 1.0
           become 1.0. comisd reports a NaN as unordered, which takes
           the jbe, so a NaN zline becomes 0.0, as the C's
           !(zc > 0.0) test makes it. */
        r128_x64_xorps(&g->e, 15, 15); /* 0.0              */
        r128_x64_comisd(&g->e, 2, 15);
        {
            int b1 = r128_x64_jcc(&g->e, X64_CC_BE);

            r128_x64_movsd_rr(&g->e, 15, 2); /* zc = zline       */
            r128_x64_patch32(&g->e, b1, r128_x64_here(&g->e));
        }
        r128_x64_sse_rip(&g->e, 0xF2, 0x10, 14, R128_X64_CP_ONED);
        r128_x64_comisd(&g->e, 15, 14);
        {
            int b1 = r128_x64_jcc(&g->e, X64_CC_BE);

            r128_x64_movsd_rr(&g->e, 15, 14); /* zc = 1.0         */
            r128_x64_patch32(&g->e, b1, r128_x64_here(&g->e));
        }
        /* zq = zc*zmax + 0.5, clamp zmax; zi = (uint32)zq */
        r128_x64_sse_rip(&g->e, 0xF2, 0x10, 13, R128_X64_CP_ZMAXD);
        r128_x64_mulsd(&g->e, 15, 13);
        r128_x64_sse_rip(&g->e, 0xF2, 0x58, 15, R128_X64_CP_ONED + 8);
        r128_x64_comisd(&g->e, 15, 13);
        {
            int b1 = r128_x64_jcc(&g->e, X64_CC_BE);

            r128_x64_movsd_rr(&g->e, 15, 13); /* zq = zmax        */
            r128_x64_patch32(&g->e, b1, r128_x64_here(&g->e));
        }
        r128_x64_cvttsd2si_r64(&g->e, X64_RDX, 15);
        r128_x64_st(&g->e, 0, X64_RDX, X64_RSP, R128_X64_SP_ZI);
    }

    /* zaddr = zrow + px * zbpp -> eax (stencil implies zbpp == 4) */
    r128_x64_mov_r_r(&g->e, 0, X64_RAX, X64_R12);
    r128_x64_shift_imm(&g->e, 4, 0, X64_RAX, (ds->zbpp == 4) ? 2 : 1);
    r128_x64_alu_r_r(&g->e, 0x01, 0, X64_RAX, X64_R15); /* add eax, r15d */

    /* Resolve the Z cell into r10 as the interpreter does. With a
       staged Z surface (tri->zptr set) the cell is zptr + (zaddr -
       z_base), and a pixel whose cell would end past z_lim is skipped;
       otherwise it is vram + (zaddr & vram_mask). */
    r128_x64_ld(&g->e, 1, X64_R10, X64_RBX,
                (int) offsetof(r128_jit_tri_t, zptr));
    r128_x64_alu_r_r(&g->e, 0x85, 1, X64_R10, X64_R10);
    b_zvram = r128_x64_jcc(&g->e, X64_CC_E);
    r128_x64_alu_r_mem(&g->e, 0x2B, 0, X64_RAX, X64_RBX,
                       (int) offsetof(r128_jit_tri_t, z_base));
    /* ecx = off + zbpp; bounds: skip when ecx > z_lim */
    r128_x64_mov_r_r(&g->e, 0, X64_RCX, X64_RAX);
    r128_x64_alu_r_imm(&g->e, 0, 0, X64_RCX, ds->zbpp);
    r128_x64_alu_r_mem(&g->e, 0x3B, 0, X64_RCX, X64_RBX,
                       (int) offsetof(r128_jit_tri_t, z_lim));
    g->skips[g->nskip++] = r128_x64_jcc(&g->e, X64_CC_A);
    r128_x64_lea_add(&g->e, X64_R10, X64_R10, X64_RAX);
    b_zres = r128_x64_jmp(&g->e);
    r128_x64_patch32(&g->e, b_zvram, r128_x64_here(&g->e));
    r128_x64_alu_r_mem(&g->e, 0x23, 0, X64_RAX, X64_RBX,
                       (int) offsetof(r128_jit_tri_t, vram_mask));
    r128_x64_lea_add(&g->e, X64_R10, X64_R8, X64_RAX);
    r128_x64_patch32(&g->e, b_zres, r128_x64_here(&g->e));

    if (!sten_on) {
        /* z test (skip the load entirely on ALWAYS) */
        if (ds->zfn != 7) {
            if (ds->zbpp == 2)
                r128_x64_ldzx16(&g->e, X64_RCX, X64_R10, 0);
            else {
                r128_x64_ld(&g->e, 0, X64_RCX, X64_R10, 0);
                if (ds->zshift)
                    r128_x64_shift_imm(&g->e, 5, 0, X64_RCX, ds->zshift);
                if (ds->zmax != 0xffffffffu) {
                    r128_x64_mov_r32_imm32(&g->e, X64_RAX, ds->zmax);
                    r128_x64_alu_r_r(&g->e, 0x21, 0, X64_RCX, X64_RAX);
                }
            }
            r128_x64_alu_r_r(&g->e, 0x39, 0, X64_RDX, X64_RCX); /* zi,zbuf */
            g->skips[g->nskip++] = r128_x64_jcc(&g->e,
                                                r128_x64_cmp_inv[ds->zfn & 7]);
        }
    } else {
        /* Stencil on (bit 3 of TEX_CNTL_C, R128_STENCIL_ENABLE in
           xf86-video-r128 and Mesa r128). The Z cell is then a 32-bit
           word holding the depth and the stencil byte. Read it once and
           compute the Z and stencil results as values, without
           branching: as in the interpreter, a failed test discards the
           pixel only after the alpha test, where the stencil update is
           applied. The results go to R128_X64_SP_STEN as
           sbuf | sres << 8 | zres << 9 for r128_x64_gen_sten_update. */
        uint32_t sva = (ds->sref & ds->svmask) & 0xff;

        r128_x64_ld(&g->e, 0, X64_RAX, X64_R10, 0); /* raw word     */
        /* zres -> edx (depth off, or ALWAYS -> 1; NEVER -> 0) */
        if (!z_on || ds->zfn == 7)
            r128_x64_mov_r32_imm32(&g->e, X64_RDX, 1);
        else if (ds->zfn == 0)
            r128_x64_mov_r32_imm32(&g->e, X64_RDX, 0);
        else {
            r128_x64_mov_r_r(&g->e, 0, X64_RCX, X64_RAX);
            if (ds->zshift)
                r128_x64_shift_imm(&g->e, 5, 0, X64_RCX, ds->zshift);
            if (ds->zmax != 0xffffffffu)
                r128_x64_alu_r_imm(&g->e, 4, 0, X64_RCX,
                                   (int32_t) ds->zmax);
            r128_x64_ld(&g->e, 0, X64_R11, X64_RSP, R128_X64_SP_ZI);
            r128_x64_alu_r_r(&g->e, 0x39, 0, X64_R11, X64_RCX); /* zi,zb */
            r128_x64_mov_r32_imm32(&g->e, X64_RDX, 0);          /* flag-free    */
            r128_x64_setcc(&g->e, r128_x64_cmp_fwd[ds->zfn & 7], X64_RDX);
        }
        /* sbuf -> ecx = (raw >> sshift) & 0xff */
        r128_x64_mov_r_r(&g->e, 0, X64_RCX, X64_RAX);
        if (ds->sshift)
            r128_x64_shift_imm(&g->e, 5, 0, X64_RCX, ds->sshift);
        r128_x64_alu_r_imm(&g->e, 4, 0, X64_RCX, 0xff);
        /* sres -> r11d = r3d_cmp(sfn, sref&svmask, sbuf&svmask) */
        if (ds->sfn == 7)
            r128_x64_mov_r32_imm32(&g->e, X64_R11, 1);
        else if (ds->sfn == 0)
            r128_x64_mov_r32_imm32(&g->e, X64_R11, 0);
        else {
            r128_x64_mov_r_r(&g->e, 0, X64_RAX, X64_RCX);
            r128_x64_alu_r_imm(&g->e, 4, 0, X64_RAX,
                               (int32_t) ds->svmask);           /* b        */
            r128_x64_mov_r32_imm32(&g->e, X64_R11, sva);        /* a        */
            r128_x64_alu_r_r(&g->e, 0x39, 0, X64_R11, X64_RAX); /* a,b */
            r128_x64_mov_r32_imm32(&g->e, X64_R11, 0);
            r128_x64_setcc(&g->e, r128_x64_cmp_fwd[ds->sfn & 7], X64_R11);
        }
        /* pack ecx = sbuf | sres<<8 | zres<<9 -> frame */
        r128_x64_mov_r_r(&g->e, 0, X64_RAX, X64_R11);
        r128_x64_shift_imm(&g->e, 4, 0, X64_RAX, 8);
        r128_x64_alu_r_r(&g->e, 0x09, 0, X64_RCX, X64_RAX);
        r128_x64_mov_r_r(&g->e, 0, X64_RAX, X64_RDX);
        r128_x64_shift_imm(&g->e, 4, 0, X64_RAX, 9);
        r128_x64_alu_r_r(&g->e, 0x09, 0, X64_RCX, X64_RAX);
        r128_x64_st(&g->e, 0, X64_RCX, X64_RSP, R128_X64_SP_STEN);
    }
}

/* Barycentric weights w_i = (float)(e_i + b_i) * invs into xmm8-xmm10,
   with b_i the per-triangle fill-rule bias flag from tri. The bias
   decides coverage only, so the accumulators keep it and each weight
   adds it back in rax, a scratch register here, before the 64-bit
   convert; the integer add is exact, so the convert rounds the same
   value the interpreter converts. */
static void
r128_x64_gen_weights(r128_x64_gen_t *g)
{
    static const int eacc[3] = { X64_RSI, X64_RDI, X64_RBP };
    const int        boff[3] = { (int) offsetof(r128_jit_tri_t, e0b),
                                 (int) offsetof(r128_jit_tri_t, e1b),
                                 (int) offsetof(r128_jit_tri_t, e2b) };

    for (int k = 0; k < 3; k++) {
        r128_x64_mov_r_r(&g->e, 1, X64_RAX, eacc[k]);
        r128_x64_alu_r_mem(&g->e, 0x03, 1, X64_RAX, X64_RBX, boff[k]);
        r128_x64_cvtsi2ss_r64(&g->e, 8 + k, X64_RAX);
    }
    r128_x64_mulss(&g->e, 8, 4);
    r128_x64_mulss(&g->e, 9, 4);
    r128_x64_mulss(&g->e, 10, 4);
}

/* The barycentric weights, then the vertex color. The interpreter
   computes the weights before the Z test; computing them here gives
   the same values, since the Z code does not use them. */
static void
r128_x64_gen_color(r128_x64_gen_t *g)
{
    r128_x64_gen_weights(g);

    /* col = (w0*vca + w1*vcb) + w2*vcc, summed in the C's order */
    r128_x64_splat0(&g->e, 11, 8);
    r128_x64_splat0(&g->e, 12, 9);
    r128_x64_splat0(&g->e, 13, 10);
    r128_x64_mulps(&g->e, 11, 5);
    r128_x64_mulps(&g->e, 12, 6);
    r128_x64_addps(&g->e, 11, 12);
    r128_x64_mulps(&g->e, 13, 7);
    r128_x64_addps(&g->e, 11, 13);
}

/* Texture stage: sampled in the block for the inline family, otherwise
   a call to rage128_texstage_run for each pixel, with the live state
   saved and restored around it. */
static void
r128_x64_gen_texture(r128_x64_gen_t *g)
{
    const rage128_draw_state_t *ds         = g->ds;
    int                         tex_inline = g->tex_inline, tex_on = g->tex_on;
    int                         z_on = g->z_on, sten_on = g->sten_on;

    if (tex_inline) {
        /* Inline texture stage: the same steps as rage128_texstage_run
           for the inline family. There is no call, so the per-row
           registers and the weights (xmm8-xmm10) stay live, except r10,
           the tested Z/stencil cell, which the stage uses as scratch.
           ARM64 keeps that cell in the callee-saved x26; x86-64 has no
           register to spare for it, so it is saved to
           R128_X64_TS_ZCELL around the stage. A chroma-key reject
           branches to pix_skip, like a failed Z test. */
        if (z_on || sten_on)
            r128_x64_st(&g->e, 1, X64_R10, X64_RSP, R128_X64_TS_ZCELL);
        r128_x64_emit_texstage_inline(&g->e, ds, g->skips, &g->nskip);
        if (z_on || sten_on)
            r128_x64_ld(&g->e, 1, X64_R10, X64_RSP, R128_X64_TS_ZCELL);
    } else if (tex_on) {
        /* Call rage128_texstage_run(texctx, w0, w1, w2, &col), the
           interpreter's own texture code (formats, filters, mip
           levels, chroma keys, combine), through its address as an
           immediate. The emitted code only saves and restores the live
           state around the call. col goes through R128_X64_SP_COL; the
           call returns 0 when a chroma key discards the pixel. */
        r128_x64_movaps_st(&g->e, 11, X64_RSP, R128_X64_SP_COL);
        if (g->z_step)
            r128_x64_movsd_st(&g->e, 2, X64_RSP, R128_X64_SP_ZL);
        r128_x64_st(&g->e, 0, X64_R9, X64_RSP, R128_X64_TS_RX0);
        if (z_on || sten_on)
            r128_x64_st(&g->e, 1, X64_R10, X64_RSP, R128_X64_TS_ZCELL);
#if R128_X64_ABI_WIN
        /* Win64: rax, rcx, rdx, r8-r11 and xmm0-xmm5 are volatile
           across the call; rbx, rsi, rdi, rbp, r12-r15 and xmm6-xmm15
           (vcb, vcc, the weights, col) survive it. Arguments: rcx =
           texctx, xmm1-xmm3 = w0-w2, and the fifth, &col, on the stack
           above the callee's 32-byte shadow space. rsp is lowered by 48
           for the call, which keeps it 16-byte aligned and leaves every
           frame slot untouched; &col is therefore rsp +
           R128_X64_SP_COL + 48. */
        r128_x64_ld(&g->e, 1, X64_RCX, X64_RBX,
                    (int) offsetof(r128_jit_tri_t, texctx));
        r128_x64_movaps_rr(&g->e, 1, 8);
        r128_x64_movaps_rr(&g->e, 2, 9);
        r128_x64_movaps_rr(&g->e, 3, 10);
        r128_x64_alu_r_imm(&g->e, 5, 1, X64_RSP, 48);
        r128_x64_lea_disp(&g->e, X64_RAX, X64_RSP, 48 + R128_X64_SP_COL);
        r128_x64_st(&g->e, 1, X64_RAX, X64_RSP, 32);
        r128_x64_mov_r64_imm64(&g->e, X64_RAX,
                               (uint64_t) (uintptr_t) rage128_texstage_run);
        r128_x64_call_r(&g->e, X64_RAX);
        r128_x64_alu_r_imm(&g->e, 0, 1, X64_RSP, 48);
        r128_x64_movups_ld(&g->e, 5, X64_RBX,
                           (int) offsetof(r128_jit_tri_t, vca));
#else
        /* System V: every xmm register and rax, rcx, rdx, rsi, rdi and
           r8-r11 are volatile. e0 and e1 are saved next to rx0 and the
           Z cell; the rest is reloaded from tri or the frame after the
           call. Arguments: rdi = texctx, rsi = &col, xmm0-xmm2 = w0-w2,
           nothing on the stack. */
        r128_x64_st(&g->e, 1, X64_RSI, X64_RSP, R128_X64_TS_E0);
        r128_x64_st(&g->e, 1, X64_RDI, X64_RSP, R128_X64_TS_E1);
        r128_x64_ld(&g->e, 1, X64_RDI, X64_RBX,
                    (int) offsetof(r128_jit_tri_t, texctx));
        r128_x64_lea_disp(&g->e, X64_RSI, X64_RSP, R128_X64_SP_COL);
        r128_x64_movaps_rr(&g->e, 0, 8);
        r128_x64_movaps_rr(&g->e, 1, 9);
        r128_x64_movaps_rr(&g->e, 2, 10);
        r128_x64_mov_r64_imm64(&g->e, X64_RAX,
                               (uint64_t) (uintptr_t) rage128_texstage_run);
        r128_x64_call_r(&g->e, X64_RAX);
        r128_x64_ld(&g->e, 1, X64_RSI, X64_RSP, R128_X64_TS_E0);
        r128_x64_ld(&g->e, 1, X64_RDI, X64_RSP, R128_X64_TS_E1);
        r128_x64_movss_ld(&g->e, 4, X64_RBX,
                          (int) offsetof(r128_jit_tri_t, invs));
        r128_x64_movups_ld(&g->e, 5, X64_RBX,
                           (int) offsetof(r128_jit_tri_t, vca));
        r128_x64_movups_ld(&g->e, 6, X64_RBX,
                           (int) offsetof(r128_jit_tri_t, vcb));
        r128_x64_movups_ld(&g->e, 7, X64_RBX,
                           (int) offsetof(r128_jit_tri_t, vcc));
#endif
        r128_x64_ld(&g->e, 1, X64_R8, X64_RBX,
                    (int) offsetof(r128_jit_tri_t, vram));
        r128_x64_ld(&g->e, 0, X64_R9, X64_RSP, R128_X64_TS_RX0);
        if (z_on || sten_on)
            r128_x64_ld(&g->e, 1, X64_R10, X64_RSP, R128_X64_TS_ZCELL);
        if (g->z_step) {
            r128_x64_movsd_ld(&g->e, 2, X64_RSP, R128_X64_SP_ZL);
            r128_x64_movsd_ld(&g->e, 3, X64_RBX,
                              (int) offsetof(r128_jit_tri_t, dZdx));
        }
#if R128_X64_ABI_WIN
        r128_x64_movss_ld(&g->e, 4, X64_RBX,
                          (int) offsetof(r128_jit_tri_t, invs));
#endif
        r128_x64_movaps_ld(&g->e, 11, X64_RSP, R128_X64_SP_COL);
        r128_x64_alu_r_r(&g->e, 0x85, 0, X64_RAX, X64_RAX);
        g->skips[g->nskip++] = r128_x64_jcc(&g->e, X64_CC_E);
#if !R128_X64_ABI_WIN
        if (ds->spec_en || (ds->fog_en && !ds->fog_table_en)) {
            /* Specular and vertex fog read the weights, which the
               System V call destroyed. Recompute them from the restored
               e0..e2, the bias flags and invs with the same instructions
               as r128_x64_gen_color, so the values are bit-identical;
               rax is free again once the return value is tested.
               Win64 keeps xmm8-xmm10 across the call, and the inline
               stage keeps them on both ABIs. */
            r128_x64_gen_weights(g);
        }
#endif
    }
}

/* Specular, as the interpreter adds it: col[rgb] += (w0*spa + w1*spb)
   + w2*spc, then each channel above 1.0 is set to 1.0. All four lanes
   are computed and the original alpha is put back afterwards. minps
   with 1.0 matches the C's compare for every value but NaN. */
static void
r128_x64_gen_spec(r128_x64_gen_t *g)
{
    r128_x64_splat0(&g->e, 12, 8);
    r128_x64_splat0(&g->e, 13, 9);
    r128_x64_splat0(&g->e, 14, 10);
    r128_x64_movups_ld(&g->e, 15, X64_RBX,
                       (int) offsetof(r128_jit_tri_t, spa));
    r128_x64_mulps(&g->e, 15, 12);
    r128_x64_movups_ld(&g->e, 0, X64_RBX,
                       (int) offsetof(r128_jit_tri_t, spb));
    r128_x64_mulps(&g->e, 0, 13);
    r128_x64_addps(&g->e, 15, 0);
    r128_x64_movups_ld(&g->e, 0, X64_RBX,
                       (int) offsetof(r128_jit_tri_t, spc));
    r128_x64_mulps(&g->e, 0, 14);
    r128_x64_addps(&g->e, 15, 0);
    r128_x64_movaps_rr(&g->e, 1, 11); /* original col      */
    r128_x64_addps(&g->e, 11, 15);
    r128_x64_sse_rip(&g->e, 0, 0x5D, 11, R128_X64_CP_ONEF); /* minps */
    r128_x64_insertps(&g->e, 11, 1, 3, 3);
}

/* Fog, after specular as in the interpreter: col[rgb] = col * f +
   fog_color * (1 - f) (SDK: Setting 3D Render States, p. 6-51 / PDF
   163), with f computed into xmm15. Vertex fog takes f = w0*fga +
   w1*fgb + w2*fgc, clamped to 0..1 in the C's order. Each clamp
   branches around its move on an ordered comparison, and comiss
   reports NaN as unordered, so a NaN f skips both moves and stays NaN
   as in the C. */
static void
r128_x64_gen_fog(r128_x64_gen_t *g)
{
    const rage128_draw_state_t *ds = g->ds;

    if (!ds->fog_table_en) {
        int b1;

        r128_x64_movups_ld(&g->e, 12, X64_RBX,
                           (int) offsetof(r128_jit_tri_t, fog));
        r128_x64_movaps_rr(&g->e, 15, 8);
        r128_x64_mulss(&g->e, 15, 12); /* w0*fga           */
        r128_x64_pshufd(&g->e, 0, 12, 0x55);
        r128_x64_movaps_rr(&g->e, 1, 9);
        r128_x64_mulss(&g->e, 1, 0); /* w1*fgb           */
        r128_x64_addss(&g->e, 15, 1);
        r128_x64_pshufd(&g->e, 0, 12, 0xAA);
        r128_x64_movaps_rr(&g->e, 1, 10);
        r128_x64_mulss(&g->e, 1, 0); /* w2*fgc           */
        r128_x64_addss(&g->e, 15, 1);
        r128_x64_xorps(&g->e, 0, 0);
        r128_x64_comiss(&g->e, 0, 15);       /* 0 vs f           */
        b1 = r128_x64_jcc(&g->e, X64_CC_BE); /* not 0 > f, or NaN */
        r128_x64_movaps_rr(&g->e, 15, 0);    /* f<0 -> 0         */
        r128_x64_patch32(&g->e, b1, r128_x64_here(&g->e));
        r128_x64_sse_rip(&g->e, 0xF3, 0x10, 1, R128_X64_CP_ONEF);
        r128_x64_comiss(&g->e, 15, 1);
        b1 = r128_x64_jcc(&g->e, X64_CC_BE);
        r128_x64_movaps_rr(&g->e, 15, 1); /* f>1 -> 1         */
        r128_x64_patch32(&g->e, b1, r128_x64_here(&g->e));
    } else {
        /* Table fog, split between entries as the interpreter splits
           it. q = trunc(zc * 255 * 2^32), one cvttsd2si, gives the
           entry i in the high dword and a 32-bit fraction fr in the low
           dword. f = (T[i] + (T[i1] - T[i]) * t) / 255.0f, with
           t = (float)fr * 2^-32 and i1 = min(i + 1, 255). zc is
           recomputed from zline (xmm2) as in the Z block; table fog
           keeps the serial double DDA live even with the Z test off.
           The interpreter's factor clamp is not emitted here: the
           blend lies between two table bytes, so f is already in
           range. */
        int    b1;
        double d255  = 255.0;
        double d2p32 = 4294967296.0;
        float  f2m32 = 1.0f / 4294967296.0f;
        int    c255d = r128_x64_pool_add(&g->e, &d255, 8, 8);
        int    c2p32 = r128_x64_pool_add(&g->e, &d2p32, 8, 8);
        int    c2m32 = r128_x64_pool_add(&g->e, &f2m32, 4, 4);

        r128_x64_xorps(&g->e, 15, 15);
        r128_x64_comisd(&g->e, 2, 15);
        b1 = r128_x64_jcc(&g->e, X64_CC_BE);
        r128_x64_movsd_rr(&g->e, 15, 2); /* zc = zline       */
        r128_x64_patch32(&g->e, b1, r128_x64_here(&g->e));
        r128_x64_sse_rip(&g->e, 0xF2, 0x10, 14, R128_X64_CP_ONED);
        r128_x64_comisd(&g->e, 15, 14);
        b1 = r128_x64_jcc(&g->e, X64_CC_BE);
        r128_x64_movsd_rr(&g->e, 15, 14); /* zc = 1.0         */
        r128_x64_patch32(&g->e, b1, r128_x64_here(&g->e));
        r128_x64_sse_rip(&g->e, 0xF2, 0x59, 15, c255d); /* *255.0  */
        r128_x64_sse_rip(&g->e, 0xF2, 0x59, 15, c2p32); /* *2^32   */
        r128_x64_cvttsd2si_r64(&g->e, X64_RCX, 15);     /* q       */
        r128_x64_mov_r_r(&g->e, 0, X64_RAX, X64_RCX);   /* fr      */
        r128_x64_cvtsi2ss_r64(&g->e, 15, X64_RAX);      /* (float) */
        r128_x64_shift_imm(&g->e, 5, 1, X64_RCX, 32);   /* i       */
        r128_x64_ld(&g->e, 1, X64_RAX, X64_RBX,
                    (int) offsetof(r128_jit_tri_t, fog_table));
        r128_x64_mov_r_r(&g->e, 1, X64_RDX, X64_RCX);
        r128_x64_alu_r_imm(&g->e, 0, 1, X64_RDX, 1);
        r128_x64_alu_r_imm(&g->e, 7, 1, X64_RCX, 255);
        r128_x64_cmov(&g->e, X64_CC_AE, 1, X64_RDX, X64_RCX); /* i1  */
        /* r10 still holds the Z cell for the Z write-back, so the
           table bytes go through r11 and rcx */
        r128_x64_ldzx8_sib(&g->e, X64_R11, X64_RAX, X64_RDX); /* T[i1] */
        r128_x64_ldzx8_sib(&g->e, X64_RCX, X64_RAX, X64_RCX); /* T[i]  */
        r128_x64_alu_r_r(&g->e, 0x29, 0, X64_R11, X64_RCX);
        r128_x64_cvtsi2ss_r32(&g->e, 14, X64_R11);      /* fd      */
        r128_x64_cvtsi2ss_r32(&g->e, 13, X64_RCX);      /* fa      */
        r128_x64_sse_rip(&g->e, 0xF3, 0x59, 15, c2m32); /* t       */
        r128_x64_mulss(&g->e, 14, 15);                  /* fd*t    */
        r128_x64_addss(&g->e, 14, 13);                  /* +fa     */
        r128_x64_movaps_rr(&g->e, 15, 14);
        r128_x64_sse_rip(&g->e, 0xF3, 0x5E, 15, R128_X64_CP_255F);
    }
    /* the blend runs on all four lanes; the original alpha is put back */
    r128_x64_splat0(&g->e, 0, 15); /* f splat          */
    r128_x64_sse_rip(&g->e, 0, 0x28, 1, R128_X64_CP_ONEF);
    r128_x64_movaps_rr(&g->e, 14, 1);
    r128_x64_subps(&g->e, 14, 0); /* (1-f) splat      */
    {
        float fc[4] = { ds->fogr, ds->fogg, ds->fogb, 0.0f };
        int   cfog  = r128_x64_pool_add(&g->e, fc, 16, 16);

        r128_x64_sse_rip(&g->e, 0, 0x28, 13, cfog);
    }
    r128_x64_movaps_rr(&g->e, 12, 11); /* original col     */
    r128_x64_mulps(&g->e, 11, 0);      /* col*f            */
    r128_x64_mulps(&g->e, 13, 14);     /* fogc*(1-f)       */
    r128_x64_addps(&g->e, 11, 13);
    r128_x64_insertps(&g->e, 11, 12, 3, 3);
}

/* Alpha test as in the interpreter: r3d_cmp(atest_fn,
   (uint32)(col[3] * 255 + 0.5), atest_ref), with the ALPHA_TEST_OP
   codes of SDK Table 6-20 (SDK: Setting 3D Render States, p. 6-51 /
   PDF 163). A failing pixel branches to pix_skip. */
static void
r128_x64_gen_atest(r128_x64_gen_t *g)
{
    const rage128_draw_state_t *ds = g->ds;

    r128_x64_pshufd(&g->e, 0, 11, 0xFF);
    r128_x64_sse_rip(&g->e, 0xF3, 0x59, 0, R128_X64_CP_255F);
    r128_x64_sse_rip(&g->e, 0xF3, 0x58, 0, R128_X64_CP_HALFF);
    r128_x64_cvttss2si_r32(&g->e, X64_RAX, 0);
    r128_x64_alu_r_imm(&g->e, 7, 0, X64_RAX, (int) (ds->atest_ref & 0xff));
    g->skips[g->nskip++] = r128_x64_jcc(&g->e, r128_x64_cmp_inv[ds->atest_fn & 7]);
}

/* Stencil update and the deferred discard, after the alpha test and
   before the color cell is resolved, so r11 is free; r10 is the tested
   cell. The operation is chosen as in the interpreter: sfail_op if the
   stencil test failed, else zpass_op if the Z test passed, else
   zfail_op. All three results are computed and two cmovs pick one.
   The new byte is merged with the old one under the stencil write mask
   (STEN_REF_MASK_C:STEN_WRITE_MSK, SDK: Setting 3D Render States,
   p. 6-57 / PDF 169) and written back into the cell at sshift. Then
   the pixel is discarded unless both tests passed. The interpreter
   skips the write when the byte is unchanged; writing the same byte
   back leaves memory the same. */
static void
r128_x64_gen_sten_update(r128_x64_gen_t *g)
{
    const rage128_draw_state_t *ds   = g->ds;
    uint32_t                    keep = ~(0xffu << ds->sshift);

    r128_x64_ld(&g->e, 0, X64_RAX, X64_RSP, R128_X64_SP_STEN);
    r128_x64_mov_r_r(&g->e, 0, X64_RCX, X64_RAX);
    r128_x64_alu_r_imm(&g->e, 4, 0, X64_RCX, 0xff); /* sbuf     */
    r128_x64_emit_sten_op(&g->e, X64_RDX, X64_RCX, ds->zfail_op, ds->sref);
    r128_x64_emit_sten_op(&g->e, X64_R11, X64_RCX, ds->zpass_op, ds->sref);
    r128_x64_test_r32_imm(&g->e, X64_RAX, 1u << 9); /* zres     */
    r128_x64_cmov(&g->e, X64_CC_NE, 0, X64_RDX, X64_R11);
    r128_x64_emit_sten_op(&g->e, X64_R11, X64_RCX, ds->sfail_op, ds->sref);
    r128_x64_test_r32_imm(&g->e, X64_RAX, 1u << 8); /* sres     */
    r128_x64_cmov(&g->e, X64_CC_E, 0, X64_RDX, X64_R11);
    /* merged = (sbuf & ~swmask) | (snew & swmask) */
    r128_x64_alu_r_imm(&g->e, 4, 0, X64_RDX, (int32_t) (ds->swmask & 0xff));
    r128_x64_alu_r_imm(&g->e, 4, 0, X64_RCX,
                       (int32_t) ((~ds->swmask) & 0xff));
    r128_x64_alu_r_r(&g->e, 0x09, 0, X64_RDX, X64_RCX);
    /* RMW the stencil byte back into the cell */
    r128_x64_ld(&g->e, 0, X64_RCX, X64_R10, 0);
    r128_x64_mov_r32_imm32(&g->e, X64_R11, keep);
    r128_x64_alu_r_r(&g->e, 0x21, 0, X64_RCX, X64_R11);
    if (ds->sshift)
        r128_x64_shift_imm(&g->e, 4, 0, X64_RDX, ds->sshift);
    r128_x64_alu_r_r(&g->e, 0x09, 0, X64_RDX, X64_RCX);
    r128_x64_st(&g->e, 0, X64_RDX, X64_R10, 0);
    /* deferred discard: skip unless both tests passed */
    r128_x64_alu_r_imm(&g->e, 4, 0, X64_RAX, (int32_t) (3u << 8));
    r128_x64_alu_r_imm(&g->e, 7, 0, X64_RAX, (int32_t) (3u << 8));
    g->skips[g->nskip++] = r128_x64_jcc(&g->e, X64_CC_NE);
}

/* Color cell and alpha blend. daddr = drow + px * bpp, and the cell is
   resolved into r11 the same way as the Z cell: in the staged color
   arena with a c_lim check, or in VRAM under vram_mask. */
static void
r128_x64_gen_dcell_blend(r128_x64_gen_t *g)
{
    const rage128_draw_state_t *ds    = g->ds;
    int                         bppsh = g->bppsh;

    r128_x64_mov_r_r(&g->e, 0, X64_RAX, X64_R12);
    r128_x64_shift_imm(&g->e, 4, 0, X64_RAX, bppsh);
    r128_x64_alu_r_r(&g->e, 0x01, 0, X64_RAX, X64_R14); /* add eax, r14d */
    {
        int b_cvram, b_cres;

        r128_x64_ld(&g->e, 1, X64_R11, X64_RBX,
                    (int) offsetof(r128_jit_tri_t, cptr));
        r128_x64_alu_r_r(&g->e, 0x85, 1, X64_R11, X64_R11);
        b_cvram = r128_x64_jcc(&g->e, X64_CC_E);
        r128_x64_alu_r_mem(&g->e, 0x2B, 0, X64_RAX, X64_RBX,
                           (int) offsetof(r128_jit_tri_t, c_base));
        r128_x64_mov_r_r(&g->e, 0, X64_RCX, X64_RAX);
        r128_x64_alu_r_imm(&g->e, 0, 0, X64_RCX, ds->bpp);
        r128_x64_alu_r_mem(&g->e, 0x3B, 0, X64_RCX, X64_RBX,
                           (int) offsetof(r128_jit_tri_t, c_lim));
        g->skips[g->nskip++] = r128_x64_jcc(&g->e, X64_CC_A);
        r128_x64_lea_add(&g->e, X64_R11, X64_R11, X64_RAX);
        b_cres = r128_x64_jmp(&g->e);
        r128_x64_patch32(&g->e, b_cvram, r128_x64_here(&g->e));
        r128_x64_alu_r_mem(&g->e, 0x23, 0, X64_RAX, X64_RBX,
                           (int) offsetof(r128_jit_tri_t, vram_mask));
        r128_x64_lea_add(&g->e, X64_R11, X64_R8, X64_RAX);
        r128_x64_patch32(&g->e, b_cres, r128_x64_here(&g->e));
    }

    if (ds->alpha_en) {
        /* dc = the destination pixel as float lanes {r,g,b,a} / 255.0f,
           as r3d_dst_read and the division in the C give it. The
           destination format selects its channel widths before the
           common blend code uses the normalized values. */
        if (ds->dst_dt == 6) {
            r128_x64_sse_rm(&g->e, 0x66, 0x6E, 12, X64_R11, 0); /* movd  */
            r128_x64_pmovzxbd(&g->e, 12, 12);                   /* {b,g,r,a} dwords  */
            r128_x64_pshufd(&g->e, 12, 12, 0xC6);               /* -> {r,g,b,a}      */
        } else if (ds->dst_dt == 3 || ds->dst_dt == 15) {
            /* Modeled destination channels widen by shifts alone. The
               single alpha bit expands to 0 or 255 for blend factors;
               four-bit alpha expands to its high nibble, like RGB. */
            int bits = ds->dst_dt == 3 ? 5 : 4;

            r128_x64_ldzx16(&g->e, X64_RAX, X64_R11, 0);
            for (int ch = 0; ch < 4; ch++) {
                int shr = ch == 3 ? bits * 3 : (2 - ch) * bits;

                r128_x64_mov_r_r(&g->e, 0, X64_RCX, X64_RAX);
                if (shr)
                    r128_x64_shift_imm(&g->e, 5, 0, X64_RCX, shr);
                if (ch == 3 && ds->dst_dt == 3) {
                    r128_x64_mov_r_r(&g->e, 0, X64_RDX, X64_RCX);
                    r128_x64_shift_imm(&g->e, 4, 0, X64_RCX, 8);
                    r128_x64_alu_r_r(&g->e, 0x29, 0, X64_RCX, X64_RDX);
                } else {
                    r128_x64_alu_r_imm(&g->e, 4, 0, X64_RCX, (1 << bits) - 1);
                    r128_x64_shift_imm(&g->e, 4, 0, X64_RCX, 8 - bits);
                }
                if (ch == 0)
                    r128_x64_movd_x_r(&g->e, 12, X64_RCX);
                else
                    r128_x64_pinsrd(&g->e, 12, X64_RCX, ch);
            }
        } else {
            /* 565 -> ARGB field bytes exactly as r3d_dst_read */
            r128_x64_ldzx16(&g->e, X64_RAX, X64_R11, 0);
            r128_x64_mov_r_r(&g->e, 0, X64_RCX, X64_RAX);
            r128_x64_shift_imm(&g->e, 5, 0, X64_RCX, 11);
            r128_x64_alu_r_imm(&g->e, 4, 0, X64_RCX, 0x1f);
            r128_x64_shift_imm(&g->e, 4, 0, X64_RCX, 3);
            r128_x64_movd_x_r(&g->e, 12, X64_RCX);
            r128_x64_mov_r_r(&g->e, 0, X64_RCX, X64_RAX);
            r128_x64_shift_imm(&g->e, 5, 0, X64_RCX, 5);
            r128_x64_alu_r_imm(&g->e, 4, 0, X64_RCX, 0x3f);
            r128_x64_shift_imm(&g->e, 4, 0, X64_RCX, 2);
            r128_x64_pinsrd(&g->e, 12, X64_RCX, 1);
            r128_x64_mov_r_r(&g->e, 0, X64_RCX, X64_RAX);
            r128_x64_alu_r_imm(&g->e, 4, 0, X64_RCX, 0x1f);
            r128_x64_shift_imm(&g->e, 4, 0, X64_RCX, 3);
            r128_x64_pinsrd(&g->e, 12, X64_RCX, 2);
            r128_x64_mov_r32_imm32(&g->e, X64_RCX, 255);
            r128_x64_pinsrd(&g->e, 12, X64_RCX, 3);
        }
        r128_x64_cvtdq2ps(&g->e, 12, 12);
        r128_x64_sse_rip(&g->e, 0, 0x5E, 12, R128_X64_CP_255F); /* divps */

        r128_x64_sse_rip(&g->e, 0, 0x28, 13, R128_X64_CP_ONEF); /* 1.0f  */

        if (ds->bsrc == 0xb || ds->bsrc == 0xc) {
            /* Source codes 11 and 12, BLEND_BOTHSRCALPHA and
               BLEND_BOTHINVSRCALPHA (SDK Table 6-17), set both factors:
               fs = sa or 1 - sa in every lane, and fd = 1 - fs. */
            r128_x64_pshufd(&g->e, 14, 11, 0xFF);
            if (ds->bsrc == 0xc) {
                r128_x64_movaps_rr(&g->e, 0, 13);
                r128_x64_subps(&g->e, 0, 14);
                r128_x64_movaps_rr(&g->e, 14, 0);
            }
            r128_x64_movaps_rr(&g->e, 15, 13);
            r128_x64_subps(&g->e, 15, 14);
        } else {
            r128_x64_emit_blend_factor(&g->e, 14, ds->bsrc);
            r128_x64_emit_blend_factor(&g->e, 15, ds->bdst);
        }
        r128_x64_mulps(&g->e, 11, 14);
        r128_x64_mulps(&g->e, 12, 15);
        if (ds->bfcn & 2)
            r128_x64_subps(&g->e, 11, 12);
        else
            r128_x64_addps(&g->e, 11, 12);
        if (ds->bfcn & 1) {
            /* ALPHA_COMB_FCN (SDK: Setting 3D Render States, p. 6-50 /
               PDF 162, Table 6-19): bit 1, tested above, subtracts dst
               from src instead of adding, and bit 0 turns the clamp off.
               Without the clamp the interpreter keeps the low 8 bits,
               (float)(lrintf(v * 255) & 0xff) / 255. cvtps2dq rounds to
               nearest even under the default MXCSR, as lrintf does under
               the default rounding mode. */
            r128_x64_sse_rip(&g->e, 0, 0x59, 11, R128_X64_CP_255F);
            r128_x64_cvtps2dq(&g->e, 11, 11);
            r128_x64_sse_rip(&g->e, 0, 0x28, 12, R128_X64_CP_255I);
            r128_x64_pand(&g->e, 11, 12);
            r128_x64_cvtdq2ps(&g->e, 11, 11);
            r128_x64_sse_rip(&g->e, 0, 0x5E, 11, R128_X64_CP_255F);
        } else {
            /* Clamp to 0..1. maxps and minps differ from the C's
               compares only for NaN, which finite inputs do not
               produce, and for -0.0, which maxps turns into +0.0; both
               zeros pack to the same byte. */
            r128_x64_sse_rip(&g->e, 0, 0x5F, 11, R128_X64_CP_ZERO);
            r128_x64_sse_rip(&g->e, 0, 0x5D, 11, R128_X64_CP_ONEF);
        }
    }
}

/* Pack and store. out = (a << 24) | (r << 16) | (g << 8) | b, each
   channel (uint32)(col * 255 + 0.5), combined with 32-bit ORs as in the
   C, so a channel above 255 spills into the next field exactly as it
   does there. Then the dither, the store under the plane mask, and the
   rx0/rx1 update. */
static void
r128_x64_gen_pack_store(r128_x64_gen_t *g)
{
    const rage128_draw_state_t *ds      = g->ds;
    int                         dith_on = g->dith_on;

    r128_x64_sse_rip(&g->e, 0, 0x59, 11, R128_X64_CP_255F);  /* mulps */
    r128_x64_sse_rip(&g->e, 0, 0x58, 11, R128_X64_CP_HALFF); /* addps */
    r128_x64_cvttps2dq(&g->e, 11, 11);
    if (dith_on && ds->dst_dt == 4) {
        /* Dither, RGB 565 only (dith_on). r3d_dst_write adds bay >> 1
           to red and blue and bay >> 2 to green, saturating at 255
           (r3d_dq), with bay = r3d_bayer4[py & 3][px & 3]. Here the
           adds go on the integer lanes before the pack. The interpreter
           adds to the bytes it takes back out of the packed word, so
           the two agree as long as no channel exceeds 255 before the
           pack; the ARM64 emitter's dither note argues that no
           reachable color does. */
        r128_x64_mov_r_r(&g->e, 0, X64_RCX, X64_R12);
        r128_x64_alu_r_imm(&g->e, 4, 0, X64_RCX, 3);
        r128_x64_shift_imm(&g->e, 4, 0, X64_RCX, 3); /* (px&3)*8      */
        r128_x64_ld(&g->e, 0, X64_RAX, X64_RSP, R128_X64_SP_DITH);
        r128_x64_shr_cl(&g->e, 0, X64_RAX);
        r128_x64_alu_r_imm(&g->e, 4, 0, X64_RAX, 0xff); /* bay          */
        r128_x64_mov_r_r(&g->e, 0, X64_RDX, X64_RAX);
        r128_x64_shift_imm(&g->e, 5, 0, X64_RDX, 1); /* bay >> 1     */
        r128_x64_shift_imm(&g->e, 5, 0, X64_RAX, 2); /* bay >> 2     */
        r128_x64_movd_x_r(&g->e, 12, X64_RDX);
        r128_x64_pinsrd(&g->e, 12, X64_RAX, 1);
        r128_x64_pinsrd(&g->e, 12, X64_RDX, 2);
        r128_x64_mov_r32_imm32(&g->e, X64_RCX, 0);
        r128_x64_pinsrd(&g->e, 12, X64_RCX, 3); /* a untouched  */
        r128_x64_paddd(&g->e, 11, 12);
        r128_x64_sse_rip(&g->e, 0, 0x28, 12, R128_X64_CP_255I);
        r128_x64_pminud(&g->e, 11, 12);
    }
    r128_x64_pextrd(&g->e, X64_RAX, 11, 3); /* a */
    r128_x64_shift_imm(&g->e, 4, 0, X64_RAX, 24);
    r128_x64_pextrd(&g->e, X64_RCX, 11, 0); /* r */
    r128_x64_shift_imm(&g->e, 4, 0, X64_RCX, 16);
    r128_x64_alu_r_r(&g->e, 0x09, 0, X64_RAX, X64_RCX);
    r128_x64_pextrd(&g->e, X64_RCX, 11, 1); /* g */
    r128_x64_shift_imm(&g->e, 4, 0, X64_RCX, 8);
    r128_x64_alu_r_r(&g->e, 0x09, 0, X64_RAX, X64_RCX);
    r128_x64_pextrd(&g->e, X64_RCX, 11, 2); /* b */
    r128_x64_alu_r_r(&g->e, 0x09, 0, X64_RAX, X64_RCX);

    /* Store. When PLANE_3D_MASK_C is not all ones (RRG: PLANE_3D_MASK_C,
       p. 3-260 / PDF 278), the bits it clears keep the old pixel's
       value; the merge is on the packed pixel, as in r3d_dst_write. */
    if (ds->dst_dt == 6) {
        if (ds->wmask != 0xffffffffu) {
            r128_x64_ld(&g->e, 0, X64_RCX, X64_R11, 0);
            r128_x64_mov_r32_imm32(&g->e, X64_RDX, ds->wmask);
            r128_x64_alu_r_r(&g->e, 0x21, 0, X64_RAX, X64_RDX); /* out &  m */
            r128_x64_not_r32(&g->e, X64_RDX);
            r128_x64_alu_r_r(&g->e, 0x21, 0, X64_RCX, X64_RDX); /* dst & ~m */
            r128_x64_alu_r_r(&g->e, 0x09, 0, X64_RAX, X64_RCX);
        }
        r128_x64_st(&g->e, 0, X64_RAX, X64_R11, 0);
    } else if (ds->dst_dt == 3 || ds->dst_dt == 15) {
        /* Quantization uses the bytes of the packed ARGB word so any
           channel carry matches the interpreter before dithering.
           Alpha stays undithered for 1555 and uses the full Bayer
           threshold for 4444. The mask merges the final 16-bit pixel. */
        int bits = ds->dst_dt == 3 ? 5 : 4;

        r128_x64_movd_x_r(&g->e, 11, X64_RAX);
        r128_x64_pmovzxbd(&g->e, 11, 11);
        r128_x64_pshufd(&g->e, 11, 11, 0xc6);
        if (dith_on) {
            r128_x64_mov_r_r(&g->e, 0, X64_RCX, X64_R12);
            r128_x64_alu_r_imm(&g->e, 4, 0, X64_RCX, 3);
            r128_x64_shift_imm(&g->e, 4, 0, X64_RCX, 3);
            r128_x64_ld(&g->e, 0, X64_RAX, X64_RSP, R128_X64_SP_DITH);
            r128_x64_shr_cl(&g->e, 0, X64_RAX);
            r128_x64_alu_r_imm(&g->e, 4, 0, X64_RAX, 0xff);
            if (ds->dst_dt == 3)
                r128_x64_shift_imm(&g->e, 5, 0, X64_RAX, 1);
            r128_x64_movd_x_r(&g->e, 12, X64_RAX);
            r128_x64_pshufd(&g->e, 12, 12, 0);
            if (ds->dst_dt == 3) {
                r128_x64_mov_r32_imm32(&g->e, X64_RCX, 0);
                r128_x64_pinsrd(&g->e, 12, X64_RCX, 3);
            }
            r128_x64_paddd(&g->e, 11, 12);
            r128_x64_sse_rip(&g->e, 0, 0x28, 12, R128_X64_CP_255I);
            r128_x64_pminud(&g->e, 11, 12);
        }
        r128_x64_pextrd(&g->e, X64_RAX, 11, 3);
        r128_x64_shift_imm(&g->e, 5, 0, X64_RAX, ds->dst_dt == 3 ? 7 : 4);
        r128_x64_shift_imm(&g->e, 4, 0, X64_RAX, bits * 3);
        for (int ch = 0; ch < 3; ch++) {
            r128_x64_pextrd(&g->e, X64_RCX, 11, ch);
            r128_x64_shift_imm(&g->e, 5, 0, X64_RCX, 8 - bits);
            if (ch != 2)
                r128_x64_shift_imm(&g->e, 4, 0, X64_RCX, (2 - ch) * bits);
            r128_x64_alu_r_r(&g->e, 0x09, 0, X64_RAX, X64_RCX);
        }
        if ((ds->wmask & 0xffff) != 0xffff) {
            r128_x64_ldzx16(&g->e, X64_RCX, X64_R11, 0);
            r128_x64_mov_r32_imm32(&g->e, X64_RDX, ds->wmask & 0xffff);
            r128_x64_alu_r_r(&g->e, 0x21, 0, X64_RAX, X64_RDX);
            r128_x64_not_r32(&g->e, X64_RDX);
            r128_x64_alu_r_r(&g->e, 0x21, 0, X64_RCX, X64_RDX);
            r128_x64_alu_r_r(&g->e, 0x09, 0, X64_RAX, X64_RCX);
        }
        r128_x64_st16(&g->e, X64_RAX, X64_R11, 0);
    } else {
        /* 565: raw = out[23:19]<<11 | out[15:10]<<5 | out[7:3] */
        r128_x64_mov_r_r(&g->e, 0, X64_RCX, X64_RAX);
        r128_x64_shift_imm(&g->e, 5, 0, X64_RCX, 19);
        r128_x64_alu_r_imm(&g->e, 4, 0, X64_RCX, 0x1f);
        r128_x64_shift_imm(&g->e, 4, 0, X64_RCX, 11);
        r128_x64_mov_r_r(&g->e, 0, X64_RDX, X64_RAX);
        r128_x64_shift_imm(&g->e, 5, 0, X64_RDX, 10);
        r128_x64_alu_r_imm(&g->e, 4, 0, X64_RDX, 0x3f);
        r128_x64_shift_imm(&g->e, 4, 0, X64_RDX, 5);
        r128_x64_alu_r_r(&g->e, 0x09, 0, X64_RCX, X64_RDX);
        r128_x64_shift_imm(&g->e, 5, 0, X64_RAX, 3);
        r128_x64_alu_r_imm(&g->e, 4, 0, X64_RAX, 0x1f);
        r128_x64_alu_r_r(&g->e, 0x09, 0, X64_RCX, X64_RAX);
        if ((ds->wmask & 0xffff) != 0xffff) {
            r128_x64_ldzx16(&g->e, X64_RAX, X64_R11, 0);
            r128_x64_mov_r32_imm32(&g->e, X64_RDX, ds->wmask & 0xffff);
            r128_x64_alu_r_r(&g->e, 0x21, 0, X64_RCX, X64_RDX);
            r128_x64_not_r32(&g->e, X64_RDX);
            r128_x64_alu_r_r(&g->e, 0x21, 0, X64_RAX, X64_RDX);
            r128_x64_alu_r_r(&g->e, 0x09, 0, X64_RCX, X64_RAX);
        }
        r128_x64_st16(&g->e, X64_RCX, X64_R11, 0);
    }

    /* rx0/rx1 update (between color and Z write, like the C) */
    r128_x64_alu_r_imm(&g->e, 7, 0, X64_R9, -1);        /* cmp r9d, -1   */
    r128_x64_cmov(&g->e, X64_CC_E, 0, X64_R9, X64_R12); /* rx0 = px      */
    r128_x64_st(&g->e, 0, X64_R12, X64_RSP, R128_X64_SP_RX1);
}

/* Z write-back through the cell the test read (r10). A 32-bit cell
   keeps its bits outside zmax << zshift, where the stencil byte is, as
   the interpreter's merge does; with nothing to keep, zi is stored
   directly. */
static void
r128_x64_gen_zwrite(r128_x64_gen_t *g)
{
    const rage128_draw_state_t *ds = g->ds;

    r128_x64_ld(&g->e, 0, X64_RDX, X64_RSP, R128_X64_SP_ZI);
    if (ds->zbpp == 2)
        r128_x64_st16(&g->e, X64_RDX, X64_R10, 0);
    else {
        uint32_t keep = ~(ds->zmax << ds->zshift);

        if (keep == 0) {
            if (ds->zshift)
                r128_x64_shift_imm(&g->e, 4, 0, X64_RDX, ds->zshift);
            r128_x64_st(&g->e, 0, X64_RDX, X64_R10, 0);
        } else {
            if (ds->zshift)
                r128_x64_shift_imm(&g->e, 4, 0, X64_RDX, ds->zshift);
            r128_x64_ld(&g->e, 0, X64_RCX, X64_R10, 0);
            r128_x64_mov_r32_imm32(&g->e, X64_RAX, keep);
            r128_x64_alu_r_r(&g->e, 0x21, 0, X64_RCX, X64_RAX);
            r128_x64_alu_r_r(&g->e, 0x09, 0, X64_RDX, X64_RCX);
            r128_x64_st(&g->e, 0, X64_RDX, X64_R10, 0);
        }
    }
}

/* pix_skip, where every skip branch lands. Steps e0..e2, zline and px
   as the C loop's increment does, then jumps back to the loop head.
   The serial double depth DDA advances whenever the Z test or table
   fog reads it, including pixels rejected before shading. */
static void
r128_x64_gen_pixskip(r128_x64_gen_t *g)
{
    int fix = r128_x64_here(&g->e);

    for (int i = 0; i < g->nskip && !g->e.overflow; i++)
        r128_x64_patch32(&g->e, g->skips[i], fix);
    r128_x64_alu_r_mem(&g->e, 0x03, 1, X64_RSI, X64_RBX,
                       (int) offsetof(r128_jit_tri_t, e0dxi));
    r128_x64_alu_r_mem(&g->e, 0x03, 1, X64_RDI, X64_RBX,
                       (int) offsetof(r128_jit_tri_t, e1dxi));
    r128_x64_alu_r_mem(&g->e, 0x03, 1, X64_RBP, X64_RBX,
                       (int) offsetof(r128_jit_tri_t, e2dxi));
    if (g->z_step)
        r128_x64_addsd(&g->e, 2, 3);
    r128_x64_alu_r_imm(&g->e, 0, 0, X64_R12, 1);
    r128_x64_jmp_to(&g->e, g->loop_head);
}

/* Exit: return rax = (rx1 << 32) | rx0, the column range written, which
   the caller marks dirty; both halves are 0xffffffff when nothing was
   written. The aux row exit jumps here too. The Win64 xmm saves happen
   in the prologue, before that jump, so the one epilogue is right for
   both paths. */
static void
r128_x64_gen_epilogue(r128_x64_gen_t *g)
{
    int frame = g->frame;

    r128_x64_patch32(&g->e, g->b_done, r128_x64_here(&g->e));
    if (g->aux_row_out >= 0)
        r128_x64_patch32(&g->e, g->aux_row_out, r128_x64_here(&g->e));
    r128_x64_mov_r_r(&g->e, 0, X64_RAX, X64_R9); /* zero-extends  */
    r128_x64_ld(&g->e, 0, X64_RDX, X64_RSP, R128_X64_SP_RX1);
    r128_x64_shift_imm(&g->e, 4, 1, X64_RDX, 32);
    r128_x64_alu_r_r(&g->e, 0x09, 1, X64_RAX, X64_RDX);
#if R128_X64_ABI_WIN
    for (int i = 0; i < 10; i++)
        r128_x64_movaps_ld(&g->e, 6 + i, X64_RSP, R128_X64_SP_XMM + i * 16);
#endif
    r128_x64_alu_r_imm(&g->e, 0, 1, X64_RSP, frame); /* add rsp, n */
    r128_x64_pop(&g->e, X64_R15);
    r128_x64_pop(&g->e, X64_R14);
    r128_x64_pop(&g->e, X64_R13);
    r128_x64_pop(&g->e, X64_R12);
#if R128_X64_ABI_WIN
    r128_x64_pop(&g->e, X64_RDI);
    r128_x64_pop(&g->e, X64_RSI);
#endif
    r128_x64_pop(&g->e, X64_RBP);
    r128_x64_pop(&g->e, X64_RBX);
    r128_x64_ret(&g->e);
}

static int
r128_jit_x64_generate_1(uint8_t *code, const rage128_draw_state_t *ds,
                        int no_soa)
{
    r128_x64_gen_t g;

    if (!r128_jit_x64_can(ds))
        return -2; /* rejected state; -1 is reserved for overflow */

    r128_x64_gen_setup(&g, code, ds, no_soa);

    if (r128_x64_never_pass(ds)) {
        /* nothing can write: rx0 = rx1 = -1, no frame */
        r128_x64_rex(&g.e, 1, 0, 0, X64_RAX);
        r128_x64_e8(&g.e, 0xC7); /* mov rax, -1 (sign-extended imm32) */
        r128_x64_modrm_reg(&g.e, 0, X64_RAX);
        r128_x64_e32(&g.e, 0xffffffffu);
        r128_x64_ret(&g.e);
        return g.e.pos;
    }

    r128_x64_gen_pool(&g);
    r128_x64_gen_prologue(&g);
    if (g.aux_on)
        r128_x64_gen_row_aux(&g);
    if (ds->stip_en)
        r128_x64_gen_row_stipple(&g);
    if (g.dith_on)
        r128_x64_gen_row_dither(&g);
    r128_x64_gen_soa(&g);
    r128_x64_gen_loop_head(&g);
    r128_x64_gen_coverage(&g);
    if (g.z_on || g.sten_on)
        r128_x64_gen_zsten(&g);
    r128_x64_gen_color(&g);
    r128_x64_gen_texture(&g);
    if (ds->spec_en)
        r128_x64_gen_spec(&g);
    if (ds->fog_en)
        r128_x64_gen_fog(&g);
    if (ds->atest_en && (ds->atest_fn & 7) != 7)
        r128_x64_gen_atest(&g);
    if (g.sten_on)
        r128_x64_gen_sten_update(&g);
    r128_x64_gen_dcell_blend(&g);
    r128_x64_gen_pack_store(&g);
    if (g.z_on && ds->z_wr)
        r128_x64_gen_zwrite(&g);
    r128_x64_gen_pixskip(&g);
    r128_x64_gen_epilogue(&g);

    if (g.e.overflow)
        return -1;
    return g.e.pos;
}

/* Overflow fallback, as in the ARM64 wrapper. The SoA texture gates
   refuse the textured states known to overflow the 16 KB block
   (r128_x64_soa_single_gated and the stage-pair budget in
   r128_x64_soa_pair_weight_gated), but they are not proven to catch
   every such state. A block that still overflows (-1) is generated
   again with no_soa set, without the vector loop, rather than leaving
   the state to the interpreter. A block that fits the first time is
   returned as is, and a refused state (-2) is not retried. */
static int
r128_jit_x64_generate(uint8_t *code, const rage128_draw_state_t *ds)
{
    int len = r128_jit_x64_generate_1(code, ds, 0);

    if (len == -1)
        len = r128_jit_x64_generate_1(code, ds, 1);
    return len;
}

#endif /* VIDEO_ATI_RAGE128_CODEGEN_X86_64_H */
