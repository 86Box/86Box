/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- x86-64 span JIT, SoA multi-pixel row loop.
 *
 *          For the states the sub-gates admit (the untextured states
 *          r128_jit_x64_soa_can accepts, and the textured states
 *          r128_jit_x64_soa_tex_can accepts), the span function starts
 *          with a vector loop that shades four pixels per iteration in
 *          SoA form: each per-pixel value becomes one lane of an XMM
 *          register. The ordinary scalar loop follows and finishes the
 *          rest of the row. The per-pixel skips (coverage, aux
 *          scissors, Z test, alpha test, stencil) become per-lane mask
 *          bits, and the stores write only the lanes whose bit is set.
 *
 *          Every bail jumps to the scalar loop head. At that point rsi,
 *          rdi and rbp (e0..e2), xmm2 (zline), r12d (px), r9d (rx0) and
 *          the frame word R128_X64_SP_RX1 (rx1) hold exactly the values
 *          the scalar loop would hold after the same pixels, so any
 *          group boundary is a valid entry point into the scalar loop,
 *          both for a bail and for the row tail.
 *
 *          Every lane must match the interpreter's pixel loop (r3d_raster
 *          in vid_ati_rage128_3d.c) bit for bit: the same float
 *          operations in the same order, and no fused multiply-add
 *          (SSE4.1 has none). The loop follows the ARM64 vector loop in
 *          vid_ati_rage128_codegen_arm64_soa.h lane for lane. The x86-64
 *          forms differ in these places:
 *           - The int64 edge values stay in general registers. CVTSI2SS
 *             from a 64-bit register rounds the exact int64 to float
 *             once; ARM64 converts to double (exact) and then to float
 *             (one rounding), which gives the same result.
 *           - The lane mask is four bits, one per lane, in the frame
 *             word R128_X64_SP_KMASK. Vector compare results reach it
 *             through MOVMSKPS. ARM64 keeps 16 bits per lane in a
 *             general register.
 *           - SSE4.1 has no unsigned dword compare. Unsigned compares
 *             flip bit 31 of both operands and then use the signed
 *             PCMPGTD (r128_x64_soa_ucmp).
 *           - Float to integer conversions use the signed truncating
 *             CVTTPS2DQ and CVTTSD2SI. The interpreter's (uint32_t)
 *             casts compile on x86-64 to a signed 64-bit truncating
 *             convert. Depth and the fog-table split use the 64-bit
 *             CVTTSD2SI, which covers the whole uint32 range; color
 *             values stay near 0..255, far inside the 32-bit range of
 *             CVTTPS2DQ. The block is checked against the interpreter
 *             built for the same host, so these are the conversions it
 *             must match.
 *
 *          Included only by vid_ati_rage128_codegen_x86_64.h.
 *
 *          Target: SSE4.1. No AVX, FMA or BMI instructions.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#ifndef VIDEO_ATI_RAGE128_CODEGEN_X86_64_SOA_H
#define VIDEO_ATI_RAGE128_CODEGEN_X86_64_SOA_H

/* ------------------------------------------------------------------------
 * Vector-loop register map (four lanes). The general registers keep the
 * roles they have in the scalar loop (see the map in
 * vid_ati_rage128_codegen_x86_64.h): rsi/rdi/rbp hold e0..e2 at the
 * group base, r12d holds px, r9d rx0 and R128_X64_SP_RX1 rx1, so a bail
 * or the row tail enters the scalar loop with nothing to fix up.
 *
 * XMM registers live across the whole vector loop:
 *   xmm4      invs in all four lanes (lane 0 is the scalar loop's invs)
 *   xmm5/6/7  vca/vcb/vcc, {r,g,b,a} each, loaded whole by the prologue
 *   xmm2/xmm3 zline accumulator / dZdx (shared with the scalar loop)
 * Per group:
 *   xmm8/9/10 w0/w1/w2 weight lanes; the blend stage reuses these
 *             registers after the last read of the weights
 *   xmm11     lane x values in the aux scissor stage, then the zi lanes
 *             from the Z quantize through the stencil Z block (also
 *             saved to R128_X64_SP_SOA_ZIV for the Z write), then a
 *             working register of the color stages
 *   r10/r11   Z / color group cell base, the same roles as the scalar
 *             loop's resolved cells
 * Scratch inside one group: xmm0, xmm1, xmm12..xmm15; rax, rcx, rdx; and
 * r10/r11 until the cells resolve. The lane mask is the frame word
 * R128_X64_SP_KMASK, bit k for pixel px+k. The "SRC slots" below are the
 * frame slots R128_X64_SP_SOA_SRCR, SRCG, SRCB and SRCA, which hold the
 * four float color channels between the color stages.
 * ---------------------------------------------------------------------- */

/* ---- additional instruction emitters (SoA only) ----
   Each emitter writes one instruction with register operands unless its
   comment says otherwise. xd/xs are XMM register numbers 0-15, rd/rs
   general registers (X64_*). The bytes are the mandatory prefix (66, F2
   or F3) when the instruction has one, a REX prefix only when an operand
   is r8-r15 or xmm8-xmm15 or the operand size is 64 bits, the opcode
   bytes, and a ModRM byte. */

/* SSE4.1 three-byte opcodes 66 0F 38 xx /r, register form: PMAXUD (3F),
   PACKUSDW (2B) and PMOVZXWD (33) below. */
static void
r128_x64_sse38_rr(r128_x64_emit_t *e, uint8_t xo, int xd, int xs)
{
    r128_x64_e8(e, 0x66);
    r128_x64_rex(e, 0, xd, 0, xs);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0x38);
    r128_x64_e8(e, xo);
    r128_x64_modrm_reg(e, xd, xs);
}

#define r128_x64_pmaxud(e, d, s)   r128_x64_sse38_rr(e, 0x3F, d, s)
#define r128_x64_packusdw(e, d, s) r128_x64_sse38_rr(e, 0x2B, d, s)
/* PMOVZXWD: the low four words zero-extended to four dwords. Opcode 33;
   23 is PMOVSXWD, which sign-extends, so one wrong byte turns 16-bit
   values of 0x8000 and up into negative lanes (the same pair as
   PMOVZXBD 31 / PMOVSXBD 21). */
#define r128_x64_pmovzxwd(e, d, s) r128_x64_sse38_rr(e, 0x33, d, s)

#define r128_x64_pxor(e, d, s)     r128_x64_sse_rr(e, 0x66, 0xEF, d, s)
#define r128_x64_pandn(e, d, s)    r128_x64_sse_rr(e, 0x66, 0xDF, d, s)
#define r128_x64_psubd(e, d, s)    r128_x64_sse_rr(e, 0x66, 0xFA, d, s)
#define r128_x64_pcmpeqd(e, d, s)  r128_x64_sse_rr(e, 0x66, 0x76, d, s)
/* PCMPGTD is a signed compare; unsigned operands first get bit 31 flipped
   with R128_X64_CP_BIAS31 (see r128_x64_soa_ucmp). */
#define r128_x64_pcmpgtd(e, d, s)    r128_x64_sse_rr(e, 0x66, 0x66, d, s)
#define r128_x64_unpcklpd(e, d, s)   r128_x64_sse_rr(e, 0x66, 0x14, d, s)
#define r128_x64_unpckhpd(e, d, s)   r128_x64_sse_rr(e, 0x66, 0x15, d, s)
#define r128_x64_movq_ld(e, d, b, o) r128_x64_sse_rm(e, 0xF3, 0x7E, d, b, o)
#define r128_x64_movq_st(e, s, b, o) r128_x64_sse_rm(e, 0x66, 0xD6, s, b, o)

/* PSLLD / PSRLD xmm, imm8: 66 0F 72 with /6 (left) or /2 (right) in the
   ModRM reg field, then the shift count. */
static void
r128_x64_pshift_imm(r128_x64_emit_t *e, int ext, int xd, int imm)
{
    r128_x64_e8(e, 0x66);
    r128_x64_rex(e, 0, 0, 0, xd);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0x72);
    r128_x64_modrm_reg(e, ext, xd);
    r128_x64_e8(e, (uint8_t) imm);
}

#define r128_x64_pslld(e, d, n) r128_x64_pshift_imm(e, 6, d, n)
#define r128_x64_psrld(e, d, n) r128_x64_pshift_imm(e, 2, d, n)

/* CMPPS / CMPPD xd, xs, imm8: each lane of xd becomes all ones where
   the predicate holds and zero elsewhere. Only predicate 1 (LT) is
   used. It is false when either operand is NaN, so "a > b" is emitted
   as "b < a" on a copy of b and is false for NaN, like the C ">" and
   the ARM64 FCMGT. */
static void
r128_x64_cmpp_imm(r128_x64_emit_t *e, uint8_t pfx, int xd, int xs, int pred)
{
    r128_x64_sse_rr(e, pfx, 0xC2, xd, xs);
    r128_x64_e8(e, (uint8_t) pred);
}

#define r128_x64_cmpltps(e, d, s) r128_x64_cmpp_imm(e, 0, d, s, 1)
#define r128_x64_cmpltpd(e, d, s) r128_x64_cmpp_imm(e, 0x66, d, s, 1)

/* MOVMSKPS r32, xs: the sign bit of lane k goes to bit k, bits 4-31 are
   cleared. Applied to a compare mask, it gives the four-bit lane mask. */
static void
r128_x64_movmskps(r128_x64_emit_t *e, int rd, int xs)
{
    r128_x64_rex(e, 0, rd, 0, xs);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0x50);
    r128_x64_modrm_reg(e, rd, xs);
}

/* BSF / BSR r32, r32 (0F BC / 0F BD): index of the lowest / highest set
   bit. The result is undefined for a zero source, so callers pass a
   nonzero mask. */
static void
r128_x64_bsf(r128_x64_emit_t *e, int rd, int rs)
{
    r128_x64_rex(e, 0, rd, 0, rs);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0xBC);
    r128_x64_modrm_reg(e, rd, rs);
}

static void
r128_x64_bsr(r128_x64_emit_t *e, int rd, int rs)
{
    r128_x64_rex(e, 0, rd, 0, rs);
    r128_x64_e8(e, 0x0F);
    r128_x64_e8(e, 0xBD);
    r128_x64_modrm_reg(e, rd, rs);
}

/* NEG r32 (F7 /3). */
static void
r128_x64_neg_r32(r128_x64_emit_t *e, int rd)
{
    r128_x64_rex(e, 0, 0, 0, rd);
    r128_x64_e8(e, 0xF7);
    r128_x64_modrm_reg(e, 3, rd);
}

/* op dword or qword [base+disp], rs: the ALU form with a memory
   destination. op is the opcode byte: 01 ADD, 09 OR, 21 AND, 29 SUB.
   w = 1 selects 64-bit operands. */
static void
r128_x64_alu_mem_r(r128_x64_emit_t *e, uint8_t op, int w, int rs, int base,
                   int32_t disp)
{
    r128_x64_rex(e, w, rs, 0, base);
    r128_x64_e8(e, op);
    r128_x64_modrm_mem(e, rs, base, disp);
}

/* LEA rd, [base + idx * 2^scale] with 64-bit operands (REX.W 8D and a
   SIB byte). */
static void
r128_x64_lea_scaled(r128_x64_emit_t *e, int rd, int base, int idx, int scale)
{
    r128_x64_rex(e, 1, rd, idx, base);
    r128_x64_e8(e, 0x8D);
    r128_x64_modrm_sib(e, rd, base, idx, scale, 0);
}

/* Put imm32 in all four dword lanes of xd: MOV gpr, imm32, MOVD xd, gpr,
   PSHUFD xd, xd, 0. gpr is overwritten. r128_x64_splat_fimm does the
   same with the bit pattern of a float. */
static void
r128_x64_splat_imm(r128_x64_emit_t *e, int xd, uint32_t imm, int gpr)
{
    r128_x64_mov_r32_imm32(e, gpr, imm);
    r128_x64_movd_x_r(e, xd, gpr);
    r128_x64_pshufd(e, xd, xd, 0x00);
}

static void
r128_x64_splat_fimm(r128_x64_emit_t *e, int xd, float f, int gpr)
{
    uint32_t u;

    memcpy(&u, &f, 4);
    r128_x64_splat_imm(e, xd, u, gpr);
}

/* ------------------------------------------------------------------------
 * Sub-gate for the untextured vector loop: the shared rule
 * r128_jit_soa_can. The textured sub-gate, r128_jit_x64_soa_tex_can, is
 * in vid_ati_rage128_codegen_x86_64_soa_tex.h next to its stage. Both are
 * asked only about states r128_jit_x64_can has accepted.
 * ---------------------------------------------------------------------- */
static int
r128_jit_x64_soa_can(const rage128_draw_state_t *ds)
{
    return r128_jit_soa_can(ds);
}

/* Whether the vector loop emits an alpha test: the shared rule
   r128_jit_soa_atest_on (test on, function neither 0 nor 7). */
static int
r128_x64_soa_atest_on(const rage128_draw_state_t *ds)
{
    return r128_jit_soa_atest_on(ds);
}

/* ------------------------------------------------------------------------
 * Unsigned dword compare on four lanes: the mask of lanes where
 * r3d_cmp(fn, a, b) holds, with a in xa and b in xb. Both are
 * overwritten; xt is scratch. Returns the register that holds the mask.
 * When *inv is set, that register holds the complement and the caller
 * inverts it: XOR 0xf after MOVMSKPS, or PANDN in place of PAND when it
 * folds the mask into a vector.
 *
 * Equal and not-equal use PCMPEQD. The ordered codes flip bit 31 of
 * both operands, which turns unsigned order into signed order, and use
 * PCMPGTD; a <= b and a >= b are the complements of a > b and a < b.
 * Codes 0 (never) and 7 (always) do not reach here: the callers handle
 * them, or r128_jit_never_pass and r128_jit_soa_atest_on keep the test
 * from being emitted.
 * ---------------------------------------------------------------------- */
static int
r128_x64_soa_ucmp(r128_x64_emit_t *e, uint32_t fn, int xa, int xb, int xt,
                  int *inv)
{
    switch (fn & 7) {
        case 3:
            r128_x64_pcmpeqd(e, xa, xb);
            *inv = 0;
            return xa;
        case 6:
            r128_x64_pcmpeqd(e, xa, xb);
            *inv = 1;
            return xa;
        default:
            break;
    }
    r128_x64_sse_rip(e, 0, 0x28, xt, R128_X64_CP_BIAS31); /* movaps */
    r128_x64_pxor(e, xa, xt);
    r128_x64_pxor(e, xb, xt);
    switch (fn & 7) {
        case 1: /* a < b  */
            r128_x64_pcmpgtd(e, xb, xa);
            *inv = 0;
            return xb;
        case 2: /* a <= b */
            r128_x64_pcmpgtd(e, xa, xb);
            *inv = 1;
            return xa;
        case 4: /* a >= b */
            r128_x64_pcmpgtd(e, xb, xa);
            *inv = 1;
            return xb;
        default: /* 5: a > b */
            r128_x64_pcmpgtd(e, xa, xb);
            *inv = 0;
            return xa;
    }
}

/* ------------------------------------------------------------------------
 * Additions to the row prologue, emitted after the scalar prologue has
 * stored the row's aux mask (R128_X64_SP_AUX) and Bayer thresholds
 * (R128_X64_SP_DITH). invs is copied to all four lanes of xmm4. With
 * dithering on for a 16-bit destination, the row's Bayer add vectors are
 * built: R128_X64_SP_DITH holds the four thresholds of row py&3, the
 * one for column c in byte c, and lane k takes the byte for column
 * (px+k)&3. r12d holds the first px here and px advances by 4 per
 * group, so each lane keeps its column for the whole row. The vectors
 * hold bay>>1 (red and blue) and bay>>2 (green) for 565, bay>>1
 * for 1555 RGB, or bay for every 4444 channel, matching the modeled
 * quantization. The frame slots e->soa_bay1 and e->soa_bay2 keep them
 * live across groups; 1555 alpha does not consume a Bayer add.
 * ---------------------------------------------------------------------- */
static void
r128_x64_emit_soa_prologue(r128_x64_emit_t *e, const rage128_draw_state_t *ds)
{
    int dith_on = ds->dither && ds->dst_dt != 6;

    r128_x64_pshufd(e, 4, 4, 0x00); /* invs splat, lane 0 unchanged */

    if (dith_on) {
        r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_SP_DITH);
        for (int k = 0; k < 4; k++) {
            r128_x64_mov_r_r(e, 0, X64_RCX, X64_R12);
            if (k)
                r128_x64_alu_r_imm(e, 0, 0, X64_RCX, k);
            r128_x64_alu_r_imm(e, 4, 0, X64_RCX, 3);
            r128_x64_shift_imm(e, 4, 0, X64_RCX, 3); /* ((px+k)&3)*8 */
            r128_x64_mov_r_r(e, 0, X64_RDX, X64_RAX);
            r128_x64_shr_cl(e, 0, X64_RDX);
            r128_x64_alu_r_imm(e, 4, 0, X64_RDX, 0xff);
            r128_x64_pinsrd(e, 0, X64_RDX, k);
        }
        r128_x64_movaps_rr(e, 1, 0);
        if (ds->dst_dt != 15)
            r128_x64_psrld(e, 1, 1);
        r128_x64_movaps_st(e, 1, X64_RSP, e->soa_bay1);
        if (ds->dst_dt != 15)
            r128_x64_psrld(e, 0, ds->dst_dt == 3 ? 1 : 2);
        r128_x64_movaps_st(e, 0, X64_RSP, e->soa_bay2);
    }
}

/* ------------------------------------------------------------------------
 * Resolve the address of the group's first cell into cell_reg, for the
 * color buffer or the Z buffer: ptr_off, base_off and lim_off are the
 * r128_jit_tri_t fields of that buffer and row_r its row-start register.
 * The four cells are 4*bpp contiguous bytes. With a staged arena (ptr
 * not NULL) the group bails when its end passes the arena limit or the
 * 32-bit offset wraps. In local VRAM it bails when the group crosses the
 * point where the address wraps through vram_mask: the first and last
 * byte addresses then differ in a bit above the mask. A bail changes no
 * scalar-loop register, and the scalar loop then applies its own
 * per-pixel bounds skip to these pixels. Mirrors r128_a64_soa_group_addr.
 * ---------------------------------------------------------------------- */
static void
r128_x64_soa_group_addr(r128_x64_emit_t *e, int ptr_off, int base_off,
                        int lim_off, int row_r, int bppsh, int bpp,
                        int cell_reg, int *bails, int *nbail)
{
    int b_vram, b_res;

    r128_x64_mov_r_r(e, 0, X64_RAX, X64_R12);
    r128_x64_shift_imm(e, 4, 0, X64_RAX, bppsh);
    r128_x64_alu_r_r(e, 0x01, 0, X64_RAX, row_r); /* addr0        */
    r128_x64_ld(e, 1, cell_reg, X64_RBX, ptr_off);
    r128_x64_alu_r_r(e, 0x85, 1, cell_reg, cell_reg);
    b_vram = r128_x64_jcc(e, X64_CC_E);
    r128_x64_alu_r_mem(e, 0x2B, 0, X64_RAX, X64_RBX, base_off); /* off */
    r128_x64_mov_r_r(e, 0, X64_RCX, X64_RAX);
    r128_x64_alu_r_imm(e, 0, 0, X64_RCX, 4 * bpp); /* end          */
    r128_x64_alu_r_mem(e, 0x3B, 0, X64_RCX, X64_RBX, lim_off);
    bails[(*nbail)++] = r128_x64_jcc(e, X64_CC_A);  /* out of arena */
    r128_x64_alu_r_r(e, 0x39, 0, X64_RCX, X64_RAX); /* cmp ecx, eax */
    bails[(*nbail)++] = r128_x64_jcc(e, X64_CC_B);  /* 32-bit wrap  */
    r128_x64_mov_r_r(e, 0, X64_RCX, X64_RAX);       /* zero-extend  */
    r128_x64_lea_add(e, cell_reg, cell_reg, X64_RCX);
    b_res = r128_x64_jmp(e);
    r128_x64_patch32(e, b_vram, r128_x64_here(e));
    /* local VRAM: bail if the group crosses the wrap point */
    r128_x64_mov_r_r(e, 0, X64_RCX, X64_RAX);
    r128_x64_alu_r_imm(e, 0, 0, X64_RCX, 4 * bpp - 1);
    r128_x64_alu_r_r(e, 0x31, 0, X64_RCX, X64_RAX); /* xor ecx, eax */
    r128_x64_ld(e, 0, X64_RDX, X64_RBX,
                (int) offsetof(r128_jit_tri_t, vram_mask));
    r128_x64_not_r32(e, X64_RDX);
    r128_x64_alu_r_r(e, 0x85, 0, X64_RCX, X64_RDX); /* test         */
    bails[(*nbail)++] = r128_x64_jcc(e, X64_CC_NE);
    r128_x64_alu_r_mem(e, 0x23, 0, X64_RAX, X64_RBX,
                       (int) offsetof(r128_jit_tri_t, vram_mask));
    r128_x64_lea_add(e, cell_reg, X64_R8, X64_RAX);
    r128_x64_patch32(e, b_res, r128_x64_here(e));
}

/* One color channel ch on four lanes: col = w0*vca[ch] + w1*vcb[ch] +
   w2*vcc[ch], added left to right as in the C, then quantized as
   (uint32_t) (col * 255.0f + 0.5f). With dith_on the Bayer add vector at
   frame offset bay_off is added and the sum saturates at 255 (PMINUD),
   which is r3d_dq. The result is left in xd as integer lanes. xt is
   scratch and must differ from xd and xmm0. */
static void
r128_x64_soa_channel(r128_x64_emit_t *e, int xd, int xt, int ch, int dith_on,
                     int bay_off)
{
    uint8_t sel = (uint8_t) (ch * 0x55);

    r128_x64_movaps_rr(e, xd, 8);
    r128_x64_pshufd(e, 0, 5, sel);
    r128_x64_mulps(e, xd, 0);
    r128_x64_movaps_rr(e, xt, 9);
    r128_x64_pshufd(e, 0, 6, sel);
    r128_x64_mulps(e, xt, 0);
    r128_x64_addps(e, xd, xt);
    r128_x64_movaps_rr(e, xt, 10);
    r128_x64_pshufd(e, 0, 7, sel);
    r128_x64_mulps(e, xt, 0);
    r128_x64_addps(e, xd, xt);
    r128_x64_sse_rip(e, 0, 0x59, xd, R128_X64_CP_255F);
    r128_x64_sse_rip(e, 0, 0x58, xd, R128_X64_CP_HALFF);
    r128_x64_cvttps2dq(e, xd, xd);
    if (dith_on) {
        r128_x64_sse_rm(e, 0x66, 0xFE, xd, X64_RSP, bay_off); /* paddd */
        r128_x64_sse_rip(e, 0, 0x28, 0, R128_X64_CP_255I);
        r128_x64_pminud(e, xd, 0);
    }
}

/* Quantize the float channel lanes in xd as r128_x64_soa_channel does,
   optionally dither, shift right by pre_shr and left by shl into the
   channel's field, and OR the result into the pack accumulator acc
   (first = 1 copies instead). acc must not be xd or xmm0. Mirrors
   r128_a64_soa_quant_pack. */
static void
r128_x64_soa_quant_pack(r128_x64_emit_t *e, int xd, int acc, int first,
                        int pre_shr, int shl, int dith_on, int bay_off)
{
    r128_x64_sse_rip(e, 0, 0x59, xd, R128_X64_CP_255F);
    r128_x64_sse_rip(e, 0, 0x58, xd, R128_X64_CP_HALFF);
    r128_x64_cvttps2dq(e, xd, xd);
    if (dith_on) {
        r128_x64_sse_rm(e, 0x66, 0xFE, xd, X64_RSP, bay_off);
        r128_x64_sse_rip(e, 0, 0x28, 0, R128_X64_CP_255I);
        r128_x64_pminud(e, xd, 0);
    }
    if (pre_shr)
        r128_x64_psrld(e, xd, pre_shr);
    if (shl)
        r128_x64_pslld(e, xd, shl);
    if (first)
        r128_x64_movaps_rr(e, acc, xd);
    else
        r128_x64_por(e, acc, xd);
}

/* Vertex fog factor lanes into xmm14: f = w0*fga + w1*fgb + w2*fgc from
   the tri's fog vector, with the weights still live in xmm8/9/10. The
   clamp to [0, 1] selects on ordered comparisons, as in
   r128_a64_soa_fogf: "f < 0" selects 0, and "1 < f" selects 1.
   Both comparisons are false for NaN, so a NaN factor stays NaN
   to match the interpreter. */
static void
r128_x64_soa_fogf(r128_x64_emit_t *e)
{
    r128_x64_movups_ld(e, 12, X64_RBX, (int) offsetof(r128_jit_tri_t, fog));
    r128_x64_movaps_rr(e, 14, 8);
    r128_x64_pshufd(e, 0, 12, 0x00);
    r128_x64_mulps(e, 14, 0);
    r128_x64_movaps_rr(e, 1, 9);
    r128_x64_pshufd(e, 0, 12, 0x55);
    r128_x64_mulps(e, 1, 0);
    r128_x64_addps(e, 14, 1);
    r128_x64_movaps_rr(e, 1, 10);
    r128_x64_pshufd(e, 0, 12, 0xAA);
    r128_x64_mulps(e, 1, 0);
    r128_x64_addps(e, 14, 1);
    r128_x64_xorps(e, 0, 0);
    r128_x64_movaps_rr(e, 1, 14);
    r128_x64_cmpltps(e, 1, 0); /* f < 0, false on NaN    */
    r128_x64_pandn(e, 1, 14);
    r128_x64_movaps_rr(e, 14, 1);
    r128_x64_sse_rip(e, 0, 0x28, 1, R128_X64_CP_ONEF);
    r128_x64_movaps_rr(e, 0, 1);
    r128_x64_cmpltps(e, 0, 14); /* m = 1 < f              */
    r128_x64_pand(e, 1, 0);     /* 1.0 & m                */
    r128_x64_pandn(e, 0, 14);   /* f & ~m                 */
    r128_x64_por(e, 0, 1);
    r128_x64_movaps_rr(e, 14, 0);
}

/* Table fog factor lanes into xmm14, the interpreter's blend between
   adjacent table entries. Only untextured states with Z on get here
   (r128_jit_soa_can). The raw zline lanes come from the frame slots
   R128_X64_SP_SOA_TFA/TFB as two pairs of doubles and are clamped to
   [0, 1] here, as the interpreter's zc is. zc * 255.0 * 2^32 is then
   converted per lane by the 64-bit CVTTSD2SI, which gives q exactly:
   the entry i in the high dword and the fraction fr in the low dword.
   With T the fog table, t = (float) fr * 2^-32 and i1 = min(i + 1, 255):
     f = (T[i] + (T[i1] - T[i]) * t) / 255.0f
   Lanes: xmm1 (float) fr, then t; xmm14 fd = T[i1] - T[i]; xmm15
   fa = T[i]. Mirrors r128_a64_soa_tfogf. */
static void
r128_x64_soa_tfogf(r128_x64_emit_t *e)
{
    double d255  = 255.0;
    double dd[2] = { d255, d255 };
    double d2p32 = 4294967296.0;
    double dp[2] = { d2p32, d2p32 };
    float  f2m32 = 1.0f / 4294967296.0f;
    float  fm[4] = { f2m32, f2m32, f2m32, f2m32 };
    int    c255  = r128_x64_pool_add(e, dd, 16, 16);
    int    c2p32 = r128_x64_pool_add(e, dp, 16, 16);
    int    c2m32 = r128_x64_pool_add(e, fm, 16, 16);

    r128_x64_movaps_ld(e, 12, X64_RSP, R128_X64_SP_SOA_TFA);
    r128_x64_movaps_ld(e, 13, X64_RSP, R128_X64_SP_SOA_TFB);
    for (int h = 0; h < 2; h++) {
        int zv = h ? 13 : 12;

        r128_x64_xorps(e, 0, 0);
        r128_x64_cmpltpd(e, 0, zv); /* 0 < z                  */
        r128_x64_pand(e, zv, 0);
        r128_x64_sse_rip(e, 0, 0x28, 1, R128_X64_CP_ONED2);
        r128_x64_movaps_rr(e, 0, 1);
        r128_x64_cmpltpd(e, 0, zv); /* m = 1 < z              */
        r128_x64_pand(e, 1, 0);
        r128_x64_pandn(e, 0, zv);
        r128_x64_por(e, 0, 1);                     /* zc                     */
        r128_x64_sse_rip(e, 0x66, 0x59, 0, c255);  /* mulpd */
        r128_x64_sse_rip(e, 0x66, 0x59, 0, c2p32); /* *2^32 */
        r128_x64_movaps_rr(e, zv, 0);
    }
    r128_x64_ld(e, 1, X64_RCX, X64_RBX,
                (int) offsetof(r128_jit_tri_t, fog_table));
    for (int k = 0; k < 4; k++) {
        int zv = (k < 2) ? 12 : 13;

        if (k & 1) {
            r128_x64_movaps_rr(e, 0, zv);
            r128_x64_unpckhpd(e, 0, 0);
            r128_x64_cvttsd2si_r64(e, X64_RAX, 0);
        } else
            r128_x64_cvttsd2si_r64(e, X64_RAX, zv); /* q       */
        r128_x64_mov_r_r(e, 0, X64_RDX, X64_RAX);   /* fr      */
        r128_x64_cvtsi2ss_r64(e, 0, X64_RDX);
        r128_x64_insertps(e, 1, 0, k, 0);         /* (float) */
        r128_x64_shift_imm(e, 5, 1, X64_RAX, 32); /* i       */
        r128_x64_mov_r_r(e, 1, X64_RDX, X64_RAX);
        r128_x64_alu_r_imm(e, 0, 1, X64_RDX, 1);
        r128_x64_alu_r_imm(e, 7, 1, X64_RAX, 255);
        r128_x64_cmov(e, X64_CC_AE, 1, X64_RDX, X64_RAX); /* i1      */
        r128_x64_ldzx8_sib(e, X64_RAX, X64_RCX, X64_RAX); /* T[i]    */
        r128_x64_ldzx8_sib(e, X64_RDX, X64_RCX, X64_RDX); /* T[i1]   */
        r128_x64_alu_r_r(e, 0x29, 0, X64_RDX, X64_RAX);
        r128_x64_cvtsi2ss_r32(e, 0, X64_RDX);
        r128_x64_insertps(e, 14, 0, k, 0); /* fd      */
        r128_x64_cvtsi2ss_r32(e, 0, X64_RAX);
        r128_x64_insertps(e, 15, 0, k, 0); /* fa      */
    }
    r128_x64_sse_rip(e, 0, 0x59, 1, c2m32);             /* t       */
    r128_x64_mulps(e, 14, 1);                           /* fd*t    */
    r128_x64_addps(e, 14, 15);                          /* +fa     */
    r128_x64_sse_rip(e, 0, 0x5E, 14, R128_X64_CP_255F); /* divps   */
}

/* Specular add and fog on the red, green and blue float channels in the
   SRC slots, updated in place; alpha is not touched. Per channel, in
   the interpreter's order:
     col = col + (w0*spa + w1*spb + w2*spc), then min(col, 1.0)
     col = col * f + fogc * (1.0 - f)
   The fog factor f is in xmm14 when fog is on, and the weights are live
   in xmm8/9/10. MINPS would return 1.0 for a NaN where the C keeps it;
   all inputs are finite. Mirrors r128_a64_soa_specfog. */
static void
r128_x64_soa_specfog(r128_x64_emit_t *e, const rage128_draw_state_t *ds)
{
    static const int slo[3]  = { R128_X64_SP_SOA_SRCR, R128_X64_SP_SOA_SRCG,
                                 R128_X64_SP_SOA_SRCB };
    static const int spo[3]  = { (int) offsetof(r128_jit_tri_t, spa),
                                 (int) offsetof(r128_jit_tri_t, spb),
                                 (int) offsetof(r128_jit_tri_t, spc) };
    const float      fogc[3] = { ds->fogr, ds->fogg, ds->fogb };

    for (int ch = 0; ch < 3; ch++) {
        uint8_t sel = (uint8_t) (ch * 0x55);

        r128_x64_movaps_ld(e, 12, X64_RSP, slo[ch]);
        if (ds->spec_en) {
            r128_x64_movups_ld(e, 15, X64_RBX, spo[0]);
            r128_x64_pshufd(e, 15, 15, sel);
            r128_x64_mulps(e, 15, 8);
            r128_x64_movups_ld(e, 1, X64_RBX, spo[1]);
            r128_x64_pshufd(e, 1, 1, sel);
            r128_x64_mulps(e, 1, 9);
            r128_x64_addps(e, 15, 1);
            r128_x64_movups_ld(e, 1, X64_RBX, spo[2]);
            r128_x64_pshufd(e, 1, 1, sel);
            r128_x64_mulps(e, 1, 10);
            r128_x64_addps(e, 15, 1);
            r128_x64_addps(e, 12, 15);
            r128_x64_sse_rip(e, 0, 0x5D, 12, R128_X64_CP_ONEF); /* minps */
        }
        if (ds->fog_en) {
            r128_x64_sse_rip(e, 0, 0x28, 1, R128_X64_CP_ONEF);
            r128_x64_subps(e, 1, 14);  /* 1 - f              */
            r128_x64_mulps(e, 12, 14); /* col * f            */
            r128_x64_splat_fimm(e, 15, fogc[ch], X64_RCX);
            r128_x64_mulps(e, 15, 1);
            r128_x64_addps(e, 12, 15);
        }
        r128_x64_movaps_st(e, 12, X64_RSP, slo[ch]);
    }
}

/* Quantize and pack the float channels in the SRC slots into xmm9
   for groups with specular or fog and no blend. ARGB 8888 packs to one
   dword per lane. RGB 565 is narrowed to four words in the low half by
   PACKUSDW, which saturates; every packed 565 value fits in 16 bits, so
   the result equals the plain truncation of the ARM64 XTN. */
static void
r128_x64_soa_pack_slots(r128_x64_emit_t *e, const rage128_draw_state_t *ds)
{
    int dith_on = ds->dither && ds->dst_dt == 4;

    if (ds->dst_dt == 6) {
        r128_x64_movaps_ld(e, 14, X64_RSP, R128_X64_SP_SOA_SRCR);
        r128_x64_soa_quant_pack(e, 14, 9, 1, 0, 16, 0, 0);
        r128_x64_movaps_ld(e, 14, X64_RSP, R128_X64_SP_SOA_SRCG);
        r128_x64_soa_quant_pack(e, 14, 9, 0, 0, 8, 0, 0);
        r128_x64_movaps_ld(e, 14, X64_RSP, R128_X64_SP_SOA_SRCB);
        r128_x64_soa_quant_pack(e, 14, 9, 0, 0, 0, 0, 0);
        r128_x64_movaps_ld(e, 14, X64_RSP, R128_X64_SP_SOA_SRCA);
        r128_x64_soa_quant_pack(e, 14, 9, 0, 0, 24, 0, 0);
    } else {
        static const int p565[3][2] = {
            { 3, 11 },
            { 2, 5  },
            { 3, 0  }
        };
        static const int slo[3] = { R128_X64_SP_SOA_SRCR,
                                    R128_X64_SP_SOA_SRCG,
                                    R128_X64_SP_SOA_SRCB };

        for (int ch = 0; ch < 3; ch++) {
            r128_x64_movaps_ld(e, 14, X64_RSP, slo[ch]);
            r128_x64_soa_quant_pack(e, 14, 9, ch == 0, p565[ch][0],
                                    p565[ch][1], dith_on,
                                    (ch == 1) ? e->soa_bay2 : e->soa_bay1);
        }
        r128_x64_packusdw(e, 9, 9);
    }
}

/* Destination alpha lanes as floats into xd. ARGB 8888 takes the top
   byte of the raw destination group in xmm8 and divides by 255.0f. RGB
   565 has no alpha: r3d_dst_read returns 255 for it, so da is exactly
   1.0f. */
static void
r128_x64_soa_blend_da(r128_x64_emit_t *e, const rage128_draw_state_t *ds,
                      int xd)
{
    if (ds->dst_dt == 6) {
        r128_x64_movaps_rr(e, xd, 8);
        r128_x64_psrld(e, xd, 24);
        r128_x64_cvtdq2ps(e, xd, xd);
        r128_x64_sse_rip(e, 0, 0x5E, xd, R128_X64_CP_255F);
    } else
        r128_x64_sse_rip(e, 0, 0x28, xd, R128_X64_CP_ONEF);
}

/* r3d_blend_factor on four lanes for one channel ch: source channel in
   xmm10, destination channel in xmm11, source alpha from the
   R128_X64_SP_SOA_SRCA slot, result in xd (xmm12 or xmm13); xmm0 and
   xmm1 are scratch. Codes 0xb and 0xc do not reach here: they set both
   factors at once, and r128_x64_emit_soa_blend builds them itself.
   Other codes the interpreter does not decode give 1.0, as in
   r3d_blend_factor. */
static void
r128_x64_soa_blend_factor(r128_x64_emit_t *e, const rage128_draw_state_t *ds,
                          int xd, uint32_t code, int ch)
{
    switch (code & 0xf) {
        case 0x0:
            r128_x64_xorps(e, xd, xd);
            break;
        case 0x2:
            r128_x64_movaps_rr(e, xd, 10);
            break;
        case 0x3:
            r128_x64_sse_rip(e, 0, 0x28, xd, R128_X64_CP_ONEF);
            r128_x64_subps(e, xd, 10);
            break;
        case 0x4:
            r128_x64_movaps_ld(e, xd, X64_RSP, R128_X64_SP_SOA_SRCA);
            break;
        case 0x5:
            r128_x64_movaps_ld(e, 0, X64_RSP, R128_X64_SP_SOA_SRCA);
            r128_x64_sse_rip(e, 0, 0x28, xd, R128_X64_CP_ONEF);
            r128_x64_subps(e, xd, 0);
            break;
        case 0x6:
            r128_x64_soa_blend_da(e, ds, xd);
            break;
        case 0x7:
            r128_x64_soa_blend_da(e, ds, 0);
            r128_x64_sse_rip(e, 0, 0x28, xd, R128_X64_CP_ONEF);
            r128_x64_subps(e, xd, 0);
            break;
        case 0x8:
            r128_x64_movaps_rr(e, xd, 11);
            break;
        case 0x9:
            r128_x64_sse_rip(e, 0, 0x28, xd, R128_X64_CP_ONEF);
            r128_x64_subps(e, xd, 11);
            break;
        case 0xa:
            /* SRCALPHASAT, valid as source or destination factor */
            if (ch == 3) {
                /* its alpha component is 1.0 */
                r128_x64_sse_rip(e, 0, 0x28, xd, R128_X64_CP_ONEF);
                break;
            }
            /* f = sa < 1-da ? sa : 1-da, as in the C: a tie or a NaN
               makes the CMPLTPS mask false and selects 1-da */
            r128_x64_soa_blend_da(e, ds, 0);
            r128_x64_sse_rip(e, 0, 0x28, xd, R128_X64_CP_ONEF);
            r128_x64_subps(e, xd, 0); /* 1-da              */
            r128_x64_movaps_ld(e, 0, X64_RSP, R128_X64_SP_SOA_SRCA);
            r128_x64_movaps_rr(e, 1, 0);
            r128_x64_cmpltps(e, 1, xd); /* m = sa < 1-da     */
            r128_x64_pand(e, 0, 1);     /* sa & m            */
            r128_x64_pandn(e, 1, xd);   /* (1-da) & ~m       */
            r128_x64_por(e, 1, 0);
            r128_x64_movaps_rr(e, xd, 1);
            break;
        case 0x1:
        default:
            r128_x64_sse_rip(e, 0, 0x28, xd, R128_X64_CP_ONEF);
            break;
    }
}

/* Alpha blend on the group, one channel at a time, as in the
   interpreter. The source channels wait as floats in the SRC slots.
   The destination group loads in one piece through r11, and each
   destination channel is unpacked the way r3d_dst_read expands it and
   divided by 255.0f. Then
     v = col * fs + dc * fd    (bfcn bit 1 set: col * fs - dc * fd)
   clamped to [0, 1], or with bfcn bit 0 set wrapped to 8 bits. The
   result is quantized and packed into xmm9. RGB 565 stores no alpha, so
   only three channels are blended, and the packed lanes are narrowed to
   words by PACKUSDW at the end.
   Registers: xmm8 raw destination, xmm9 pack accumulator, xmm10 source
   channel, xmm11 destination channel, xmm12/xmm13 source/destination
   factor, xmm0/1/14/15 scratch. The weights in xmm8-xmm10 are not read
   again after this point, so their registers are free. */
static void
r128_x64_emit_soa_blend(r128_x64_emit_t *e, const rage128_draw_state_t *ds)
{
    static const int slo[4]  = { R128_X64_SP_SOA_SRCR, R128_X64_SP_SOA_SRCG,
                                 R128_X64_SP_SOA_SRCB, R128_X64_SP_SOA_SRCA };
    int              dith_on = ds->dither && ds->dst_dt == 4;
    int              forced  = ds->bsrc == 0xb || ds->bsrc == 0xc;
    int              nch     = (ds->dst_dt == 6) ? 4 : 3;

    if (ds->dst_dt == 6)
        r128_x64_movups_ld(e, 8, X64_R11, 0);
    else {
        r128_x64_movq_ld(e, 8, X64_R11, 0);
        r128_x64_pmovzxwd(e, 8, 8);
    }

    if (forced) {
        /* codes 0xb and 0xc set both factors: fs = sa (0xb) or 1-sa
           (0xc) on every channel, and fd = 1-fs. xmm12/xmm13 keep them
           for the whole channel loop. */
        r128_x64_movaps_ld(e, 12, X64_RSP, R128_X64_SP_SOA_SRCA);
        r128_x64_sse_rip(e, 0, 0x28, 0, R128_X64_CP_ONEF);
        if (ds->bsrc == 0xc) {
            r128_x64_movaps_rr(e, 1, 0);
            r128_x64_subps(e, 1, 12);
            r128_x64_movaps_rr(e, 12, 1);
        }
        r128_x64_movaps_rr(e, 13, 0);
        r128_x64_subps(e, 13, 12);
    }

    for (int ch = 0; ch < nch; ch++) {
        static const int p565[3][2] = {
            { 3, 11 },
            { 2, 5  },
            { 3, 0  }
        };
        static const int f565[3][3] = {
            /* shr, mask, shl */
            { 11, 0x1f, 3 },
            { 5,  0x3f, 2 },
            { 0,  0x1f, 3 }
        };
        int bay_off = (ch == 1) ? e->soa_bay2 : e->soa_bay1;

        r128_x64_movaps_ld(e, 10, X64_RSP, slo[ch]); /* src channel */
        r128_x64_movaps_rr(e, 11, 8);
        if (ds->dst_dt == 6) {
            if (ch == 3)
                r128_x64_psrld(e, 11, 24);
            else {
                if (ch < 2)
                    r128_x64_psrld(e, 11, 16 - 8 * ch);
                r128_x64_sse_rip(e, 0, 0x28, 0, R128_X64_CP_255I);
                r128_x64_pand(e, 11, 0);
            }
        } else {
            if (f565[ch][0])
                r128_x64_psrld(e, 11, f565[ch][0]);
            r128_x64_splat_imm(e, 0, (uint32_t) f565[ch][1], X64_RCX);
            r128_x64_pand(e, 11, 0);
            r128_x64_pslld(e, 11, f565[ch][2]);
        }
        r128_x64_cvtdq2ps(e, 11, 11);
        r128_x64_sse_rip(e, 0, 0x5E, 11, R128_X64_CP_255F);
        if (!forced) {
            r128_x64_soa_blend_factor(e, ds, 12, ds->bsrc, ch);
            r128_x64_soa_blend_factor(e, ds, 13, ds->bdst, ch);
        }
        r128_x64_mulps(e, 10, 12);
        r128_x64_mulps(e, 11, 13);
        if (ds->bfcn & 2)
            r128_x64_subps(e, 10, 11);
        else
            r128_x64_addps(e, 10, 11);
        if (ds->bfcn & 1) {
            /* no clamp: (float) (lrintf(v * 255) & 0xff) / 255. CVTPS2DQ
               and lrintf both round in the current mode, round to
               nearest even by default */
            r128_x64_sse_rip(e, 0, 0x59, 10, R128_X64_CP_255F);
            r128_x64_cvtps2dq(e, 10, 10);
            r128_x64_sse_rip(e, 0, 0x28, 0, R128_X64_CP_255I);
            r128_x64_pand(e, 10, 0);
            r128_x64_cvtdq2ps(e, 10, 10);
            r128_x64_sse_rip(e, 0, 0x5E, 10, R128_X64_CP_255F);
        } else {
            /* clamp to [0, 1]. MAXPS/MINPS differ from the C compares
               only for NaN, which finite factors never produce, and for
               -0.0, which packs the same as +0.0 */
            r128_x64_sse_rip(e, 0, 0x5F, 10, R128_X64_CP_ZERO);
            r128_x64_sse_rip(e, 0, 0x5D, 10, R128_X64_CP_ONEF);
        }
        if (ds->dst_dt == 6)
            r128_x64_soa_quant_pack(e, 10, 9, ch == 0, 0,
                                    (ch == 3) ? 24 : 16 - 8 * ch, 0, 0);
        else
            r128_x64_soa_quant_pack(e, 10, 9, ch == 0, p565[ch][0],
                                    p565[ch][1], dith_on, bay_off);
    }
    if (ds->dst_dt != 6)
        r128_x64_packusdw(e, 9, 9);
}

/* Update rx0/rx1 from the lane mask in eax, which must be nonzero. The
   lowest set bit is the leftmost written pixel: it becomes rx0 only
   while rx0 is still -1. The highest set bit gives rx1, stored to
   R128_X64_SP_RX1. Same result as the interpreter's per-pixel update. */
static void
r128_x64_soa_rx(r128_x64_emit_t *e)
{
    r128_x64_bsf(e, X64_RCX, X64_RAX);
    r128_x64_alu_r_r(e, 0x01, 0, X64_RCX, X64_R12); /* px + first  */
    r128_x64_alu_r_imm(e, 7, 0, X64_R9, -1);
    r128_x64_cmov(e, X64_CC_E, 0, X64_R9, X64_RCX);
    r128_x64_bsr(e, X64_RCX, X64_RAX);
    r128_x64_alu_r_r(e, 0x01, 0, X64_RCX, X64_R12); /* px + last   */
    r128_x64_st(e, 0, X64_RCX, X64_RSP, R128_X64_SP_RX1);
}

/* Stencil Z block, used for stencil states in place of the Z test. It
   loads the four 32-bit cell words of the Z group (r10) and computes per
   lane the depth result zres and the stencil result
   sres = r3d_cmp(sfn, sref & svmask, sbuf & svmask) as values. Neither
   result touches the lane mask: as in the interpreter, a failed test
   discards the pixel only after the alpha test, because the stencil
   fail operation must still run. Each lane is packed as
   sbuf | sres << 8 | zres << 9, the layout of the scalar loop's
   R128_X64_SP_STEN word, into the frame slot e->soa_stq. With Z off or
   Z function 7, zres is 1; with function 0 it is 0. The zi lanes come
   from xmm11; whenever the Z compare runs, zquant is set and
   r128_x64_soa_lp_z has built them. Scratch: xmm0, xmm1, xmm12..xmm15,
   rcx. */
static void
r128_x64_soa_sten_zblock(r128_x64_emit_t *e, const rage128_draw_state_t *ds)
{
    uint32_t sva = (ds->sref & ds->svmask) & 0xff;
    int      inv, mreg;

    r128_x64_movups_ld(e, 12, X64_R10, 0); /* cell words   */
    /* zres as bit 9 -> xmm13 */
    if (!ds->z_en || ds->zfn == 7)
        r128_x64_splat_imm(e, 13, 0x200, X64_RCX);
    else if (ds->zfn == 0)
        r128_x64_pxor(e, 13, 13);
    else {
        r128_x64_movaps_rr(e, 13, 12);
        if (ds->zshift)
            r128_x64_psrld(e, 13, ds->zshift);
        if (ds->zmax != 0xffffffffu) {
            r128_x64_splat_imm(e, 0, ds->zmax, X64_RCX);
            r128_x64_pand(e, 13, 0);
        }
        r128_x64_movaps_rr(e, 14, 11); /* zi copy      */
        mreg = r128_x64_soa_ucmp(e, ds->zfn, 14, 13, 0, &inv);
        r128_x64_splat_imm(e, 1, 0x200, X64_RCX);
        if (inv) {
            r128_x64_pandn(e, mreg, 1);
            r128_x64_movaps_rr(e, 13, mreg);
        } else {
            r128_x64_pand(e, mreg, 1);
            if (mreg != 13)
                r128_x64_movaps_rr(e, 13, mreg);
        }
    }
    /* sbuf = (word >> sshift) & 0xff -> xmm14, ORed into the low byte */
    r128_x64_movaps_rr(e, 14, 12);
    if (ds->sshift)
        r128_x64_psrld(e, 14, ds->sshift);
    r128_x64_sse_rip(e, 0, 0x28, 0, R128_X64_CP_255I);
    r128_x64_pand(e, 14, 0);
    r128_x64_por(e, 13, 14);
    /* sres as bit 8: r3d_cmp(sfn, sref&svmask, sbuf&svmask) */
    if (ds->sfn == 7) {
        r128_x64_splat_imm(e, 0, 0x100, X64_RCX);
        r128_x64_por(e, 13, 0);
    } else if (ds->sfn != 0) {
        r128_x64_movaps_rr(e, 15, 14);
        r128_x64_splat_imm(e, 0, ds->svmask, X64_RCX);
        r128_x64_pand(e, 15, 0);                 /* b            */
        r128_x64_splat_imm(e, 12, sva, X64_RCX); /* a            */
        mreg = r128_x64_soa_ucmp(e, ds->sfn, 12, 15, 0, &inv);
        r128_x64_splat_imm(e, 1, 0x100, X64_RCX);
        if (inv)
            r128_x64_pandn(e, mreg, 1);
        else
            r128_x64_pand(e, mreg, 1);
        r128_x64_por(e, 13, mreg);
    }
    r128_x64_movaps_st(e, 13, X64_RSP, e->soa_stq);
}

/* One stencil operation on the sbuf lanes (integers 0..255): result in
   xd, xs is kept, xmm1 is scratch. The saturating increment and
   decrement use PMINUD and PMAXUD; on 0..255 they give the same values
   as the interpreter's compare and select. INVERT and the wrapping ops
   can leave bits above bit 7 set. The swmask merge in
   r128_x64_emit_soa_sten keeps only the low 8 bits, which does the
   interpreter's & 0xff. */
static void
r128_x64_soa_sten_op_vec(r128_x64_emit_t *e, int xd, int xs, uint32_t op,
                         uint32_t sref)
{
    switch (op & 7) {
        case 0: /* KEEP    */
            r128_x64_movaps_rr(e, xd, xs);
            break;
        case 1: /* ZERO    */
            r128_x64_pxor(e, xd, xd);
            break;
        case 2: /* REPLACE */
            r128_x64_splat_imm(e, xd, sref & 0xff, X64_RCX);
            break;
        case 3: /* INC sat: min(sbuf + 1, 0xff) */
            r128_x64_splat_imm(e, 1, 1, X64_RCX);
            r128_x64_movaps_rr(e, xd, xs);
            r128_x64_paddd(e, xd, 1);
            r128_x64_sse_rip(e, 0, 0x28, 1, R128_X64_CP_255I);
            r128_x64_pminud(e, xd, 1);
            break;
        case 4: /* DEC sat: max(sbuf, 1) - 1 */
            r128_x64_splat_imm(e, 1, 1, X64_RCX);
            r128_x64_movaps_rr(e, xd, xs);
            r128_x64_pmaxud(e, xd, 1);
            r128_x64_psubd(e, xd, 1);
            break;
        case 5: /* INVERT (high bits cleared by the swmask merge) */
            r128_x64_pcmpeqd(e, 1, 1);
            r128_x64_movaps_rr(e, xd, xs);
            r128_x64_pxor(e, xd, 1);
            break;
        case 6: /* INC wrap */
            r128_x64_splat_imm(e, 1, 1, X64_RCX);
            r128_x64_movaps_rr(e, xd, xs);
            r128_x64_paddd(e, xd, 1);
            break;
        default: /* 7 DEC wrap */
            r128_x64_splat_imm(e, 1, 1, X64_RCX);
            r128_x64_movaps_rr(e, xd, xs);
            r128_x64_psubd(e, xd, 1);
            break;
    }
}

/* Stencil stage, at the interpreter's position: after the alpha test and
   before the color store. On entry R128_X64_SP_KMASK holds the op mask:
   the lanes that passed coverage, the aux scissors, the alpha test and,
   in a textured group, the chroma key, at least one of them. The slot
   e->soa_stq holds the packed values from r128_x64_soa_sten_zblock, and
   r10 is the Z group base.

   R128_X64_SP_KMASK is replaced by the write mask (op mask AND sres AND
   zres), which the store stage and the rx update use. Per lane,
   snew = sres ? (zres ? zpass : zfail) : sfail, merged as
   (sbuf & ~swmask) | (snew & swmask), is written into the stencil byte
   of the cell word for every op-mask lane. The interpreter skips that
   write when the byte is unchanged; writing back the same word leaves
   memory the same. xmm9, the packed color, is not touched. */
static void
r128_x64_emit_soa_sten(r128_x64_emit_t *e, const rage128_draw_state_t *ds)
{
    uint32_t keep = ~(0xffu << ds->sshift);

    r128_x64_movaps_ld(e, 12, X64_RSP, e->soa_stq);
    /* write mask: lanes with bits 8 and 9 both set, AND the op mask, to
       R128_X64_SP_KMASK. The write-back below still uses the op mask,
       which stays in eax. */
    r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_SP_KMASK);
    r128_x64_splat_imm(e, 0, 0x300, X64_RCX);
    r128_x64_movaps_rr(e, 1, 12);
    r128_x64_pand(e, 1, 0);
    r128_x64_pcmpeqd(e, 1, 0);
    r128_x64_movmskps(e, X64_RCX, 1);
    r128_x64_alu_r_r(e, 0x21, 0, X64_RCX, X64_RAX);
    r128_x64_st(e, 0, X64_RCX, X64_RSP, R128_X64_SP_KMASK);
    /* sbuf lanes -> xmm13 */
    r128_x64_sse_rip(e, 0, 0x28, 0, R128_X64_CP_255I);
    r128_x64_movaps_rr(e, 13, 12);
    r128_x64_pand(e, 13, 0);
    /* nested select: t = zres ? zpass : zfail; snew = sres ? t : sfail */
    r128_x64_soa_sten_op_vec(e, 14, 13, ds->zfail_op, ds->sref);
    r128_x64_soa_sten_op_vec(e, 15, 13, ds->zpass_op, ds->sref);
    r128_x64_splat_imm(e, 0, 0x200, X64_RCX);
    r128_x64_movaps_rr(e, 1, 12);
    r128_x64_pand(e, 1, 0);
    r128_x64_pcmpeqd(e, 1, 0); /* zres mask    */
    r128_x64_pand(e, 15, 1);
    r128_x64_pandn(e, 1, 14);
    r128_x64_por(e, 15, 1); /* t            */
    r128_x64_soa_sten_op_vec(e, 14, 13, ds->sfail_op, ds->sref);
    r128_x64_splat_imm(e, 0, 0x100, X64_RCX);
    r128_x64_movaps_rr(e, 1, 12);
    r128_x64_pand(e, 1, 0);
    r128_x64_pcmpeqd(e, 1, 0); /* sres mask    */
    r128_x64_pand(e, 15, 1);
    r128_x64_pandn(e, 1, 14);
    r128_x64_por(e, 15, 1); /* snew         */
    /* merged = (sbuf & ~swmask) | (snew & swmask) */
    r128_x64_splat_imm(e, 0, ds->swmask & 0xff, X64_RCX);
    r128_x64_pand(e, 15, 0);
    r128_x64_splat_imm(e, 0, (~ds->swmask) & 0xff, X64_RCX);
    r128_x64_pand(e, 13, 0);
    r128_x64_por(e, 15, 13);
    r128_x64_movaps_st(e, 15, X64_RSP, e->soa_stq);
    /* per lane under the op mask: replace the stencil byte of the cell
       word and keep its other bits */
    for (int k = 0; k < 4; k++) {
        int b1;

        r128_x64_test_r32_imm(e, X64_RAX, 1u << k);
        b1 = r128_x64_jcc(e, X64_CC_E);
        r128_x64_ld(e, 0, X64_RCX, X64_RSP, e->soa_stq + 4 * k);
        r128_x64_ld(e, 0, X64_RDX, X64_R10, 4 * k);
        r128_x64_alu_r_imm(e, 4, 0, X64_RDX, (int32_t) keep);
        if (ds->sshift)
            r128_x64_shift_imm(e, 4, 0, X64_RCX, ds->sshift);
        r128_x64_alu_r_r(e, 0x09, 0, X64_RDX, X64_RCX);
        r128_x64_st(e, 0, X64_RDX, X64_R10, 4 * k);
        r128_x64_patch32(e, b1, r128_x64_here(e));
    }
}

/* Textured group body, defined in vid_ati_rage128_codegen_x86_64_soa_tex.h.
   That file is included after the scalar texture stage, whose texel
   fetch it calls, so it comes after this one. */
static void r128_x64_emit_soa_texstage(r128_x64_emit_t            *e,
                                       const rage128_draw_state_t *ds);

/* ------------------------------------------------------------------------
 * The vector loop, emitted between the row prologue and the scalar loop.
 * Bail jumps are collected in bails[]; the caller
 * (r128_x64_gen_loop_head) patches them to the scalar loop head, which
 * finishes the row pixel by pixel. Each phase below is its own emitter,
 * sharing the loop state through r128_x64_soa_lp_t, and
 * r128_x64_emit_soa_loop calls them in order.
 * ---------------------------------------------------------------------- */
typedef struct {
    r128_x64_emit_t            *e;
    const rage128_draw_state_t *ds;
    int                        *bails, *nbail;
    int                         bppsh, zbppsh, z_on, sten, at_on, defer;
    int                         dith_on, zquant;
    int                         head, b_adv0, b_adv1, b_adv2, b_part, b_full;
} r128_x64_soa_lp_t;

static void
r128_x64_soa_lp_setup(r128_x64_soa_lp_t *l, r128_x64_emit_t *e,
                      const rage128_draw_state_t *ds, int *bails, int *nbail)
{
    int z_on  = ds->z_en;
    int sten  = ds->sten_on;
    int at_on = r128_x64_soa_atest_on(ds);

    l->e      = e;
    l->ds     = ds;
    l->bails  = bails;
    l->nbail  = nbail;
    l->bppsh  = (ds->bpp == 4) ? 2 : 1;
    l->zbppsh = (ds->zbpp == 4) ? 2 : 1;
    l->z_on   = z_on;
    l->sten   = sten;
    l->at_on  = at_on;
    /* In a textured group the alpha test and the chroma key run inside
       the texture stage, which ANDs its pass lanes into
       R128_X64_SP_KMASK, and stencil builds its write mask after that.
       The rx0/rx1 update then waits until the final mask is known. */
    l->defer   = ds->tex_en && (at_on || ds->need_ck || sten);
    l->dith_on = ds->dither && ds->dst_dt != 6;
    l->zquant  = z_on && (ds->z_wr || ds->zfn != 7);
    l->b_adv1  = -1;
    l->b_adv2  = -1;
}

/* Color/Z row overlap guard, once per row, for stencil blocks and for
   every block that reads or writes the Z cell (zquant). The interpreter
   does the Z/stencil access and the color write pixel by pixel; the
   vector loop does them per group of four. If the color row and the Z
   row overlap, the two orders give different results. So the whole row
   bails to the scalar loop when the masked row starts lie within one
   row extent of each other in either direction: (x1 - px) * 4 plus 72
   bytes of slack, where 4 bytes per pixel covers both buffers. The
   distance is taken modulo the VRAM size (vram_mask + 1): a row that
   wraps past the end of VRAM continues at the start and can overlap the
   other row there. The group that straddles the wrap point bails on its
   own, but the groups after it would not. A false positive only costs
   the vector loop for that row. Mirrors r128_a64_soa_lp_rowguard. */
static void
r128_x64_soa_lp_rowguard(r128_x64_soa_lp_t *l)
{
    r128_x64_emit_t *e     = l->e;
    int             *bails = l->bails, *nbail = l->nbail;

    r128_x64_mov_r_r(e, 0, X64_RAX, X64_R12);
    r128_x64_shift_imm(e, 4, 0, X64_RAX, l->bppsh);
    r128_x64_alu_r_r(e, 0x01, 0, X64_RAX, X64_R14);
    r128_x64_alu_r_mem(e, 0x23, 0, X64_RAX, X64_RBX,
                       (int) offsetof(r128_jit_tri_t, vram_mask));
    r128_x64_mov_r_r(e, 0, X64_RCX, X64_R12);
    r128_x64_shift_imm(e, 4, 0, X64_RCX, l->zbppsh);
    r128_x64_alu_r_r(e, 0x01, 0, X64_RCX, X64_R15);
    r128_x64_alu_r_mem(e, 0x23, 0, X64_RCX, X64_RBX,
                       (int) offsetof(r128_jit_tri_t, vram_mask));
    r128_x64_alu_r_r(e, 0x29, 0, X64_RAX, X64_RCX); /* d = c - z  */
    r128_x64_alu_r_mem(e, 0x23, 0, X64_RAX, X64_RBX,
                       (int) offsetof(r128_jit_tri_t, vram_mask)); /* d mod size */
    r128_x64_mov_r_r(e, 0, X64_RCX, X64_R13);
    r128_x64_alu_r_r(e, 0x29, 0, X64_RCX, X64_R12);
    r128_x64_shift_imm(e, 4, 0, X64_RCX, 2);
    r128_x64_alu_r_imm(e, 0, 0, X64_RCX, 72); /* + slack    */
    r128_x64_alu_r_r(e, 0x39, 0, X64_RAX, X64_RCX);
    bails[(*nbail)++] = r128_x64_jcc(e, X64_CC_B);
    r128_x64_neg_r32(e, X64_RAX); /* size - d   */
    r128_x64_alu_r_mem(e, 0x03, 0, X64_RAX, X64_RBX,
                       (int) offsetof(r128_jit_tri_t, vram_mask));
    r128_x64_alu_r_imm(e, 0, 0, X64_RAX, 1);
    r128_x64_alu_r_r(e, 0x39, 0, X64_RAX, X64_RCX);
    bails[(*nbail)++] = r128_x64_jcc(e, X64_CC_B);
}

/* Loop head: a full group needs px+3 <= x1; otherwise bail and let the
   scalar loop draw the remaining pixels. The group back-branch returns
   here, so the row guard above runs once per row. */
static void
r128_x64_soa_lp_head(r128_x64_soa_lp_t *l)
{
    r128_x64_emit_t *e     = l->e;
    int             *bails = l->bails, *nbail = l->nbail;

    l->head = r128_x64_here(e);
    r128_x64_mov_r_r(e, 0, X64_RAX, X64_R12);
    r128_x64_alu_r_imm(e, 0, 0, X64_RAX, 3);
    r128_x64_alu_r_r(e, 0x39, 0, X64_RAX, X64_R13);
    bails[(*nbail)++] = r128_x64_jcc(e, X64_CC_G);
}

/* Lane pass: the coverage mask and the weight lanes. Copies of e0..e2
   in rax/rcx/rdx step through the four lanes by e0dxi..e2dxi, so the
   accumulators in rsi/rdi/rbp are not changed. A lane is covered when
   e0|e1|e2 has the sign bit clear, the scalar loop's coverage test;
   the mask builds in r10d with r11 as scratch, both free until the
   cells resolve, and is stored to R128_X64_SP_KMASK. Each weight is
   (float) (e + b) * invs, with b the fill-rule bias flag from tri: the
   bias decides coverage only, so the mask reads the biased lane value
   and the weight adds b back in r11 before CVTSI2SS from the 64-bit
   sum, the scalar loop's convert, then a multiply by the invs
   lanes. */
static void
r128_x64_soa_lp_lanes(r128_x64_soa_lp_t *l)
{
    r128_x64_emit_t *e = l->e;

    r128_x64_mov_r_r(e, 1, X64_RAX, X64_RSI);
    r128_x64_mov_r_r(e, 1, X64_RCX, X64_RDI);
    r128_x64_mov_r_r(e, 1, X64_RDX, X64_RBP);
    for (int k = 0; k < 4; k++) {
        r128_x64_mov_r_r(e, 1, X64_R11, X64_RAX);
        r128_x64_alu_r_r(e, 0x09, 1, X64_R11, X64_RCX);
        r128_x64_alu_r_r(e, 0x09, 1, X64_R11, X64_RDX);
        r128_x64_shift_imm(e, 5, 1, X64_R11, 63);
        r128_x64_alu_r_imm(e, 6, 0, X64_R11, 1); /* xor 1      */
        if (k) {
            r128_x64_shift_imm(e, 4, 0, X64_R11, k);
            r128_x64_alu_r_r(e, 0x09, 0, X64_R10, X64_R11);
        } else
            r128_x64_mov_r_r(e, 0, X64_R10, X64_R11);
        r128_x64_mov_r_r(e, 1, X64_R11, X64_RAX);
        r128_x64_alu_r_mem(e, 0x03, 1, X64_R11, X64_RBX,
                           (int) offsetof(r128_jit_tri_t, e0b));
        r128_x64_cvtsi2ss_r64(e, 0, X64_R11);
        r128_x64_insertps(e, 8, 0, k, 0);
        r128_x64_mov_r_r(e, 1, X64_R11, X64_RCX);
        r128_x64_alu_r_mem(e, 0x03, 1, X64_R11, X64_RBX,
                           (int) offsetof(r128_jit_tri_t, e1b));
        r128_x64_cvtsi2ss_r64(e, 0, X64_R11);
        r128_x64_insertps(e, 9, 0, k, 0);
        r128_x64_mov_r_r(e, 1, X64_R11, X64_RDX);
        r128_x64_alu_r_mem(e, 0x03, 1, X64_R11, X64_RBX,
                           (int) offsetof(r128_jit_tri_t, e2b));
        r128_x64_cvtsi2ss_r64(e, 0, X64_R11);
        r128_x64_insertps(e, 10, 0, k, 0);
        if (k < 3) {
            r128_x64_alu_r_mem(e, 0x03, 1, X64_RAX, X64_RBX,
                               (int) offsetof(r128_jit_tri_t, e0dxi));
            r128_x64_alu_r_mem(e, 0x03, 1, X64_RCX, X64_RBX,
                               (int) offsetof(r128_jit_tri_t, e1dxi));
            r128_x64_alu_r_mem(e, 0x03, 1, X64_RDX, X64_RBX,
                               (int) offsetof(r128_jit_tri_t, e2dxi));
        }
    }
    r128_x64_st(e, 0, X64_R10, X64_RSP, R128_X64_SP_KMASK);
    r128_x64_mulps(e, 8, 4);
    r128_x64_mulps(e, 9, 4);
    r128_x64_mulps(e, 10, 4);
}

/* Group cell bases and bounds: color into r11, then Z into r10. Any
   failure bails to the scalar loop, which is safe here because the lane
   pass changed none of its registers. */
static void
r128_x64_soa_lp_groups(r128_x64_soa_lp_t *l)
{
    r128_x64_emit_t            *e     = l->e;
    const rage128_draw_state_t *ds    = l->ds;
    int                        *bails = l->bails, *nbail = l->nbail;

    r128_x64_soa_group_addr(e, (int) offsetof(r128_jit_tri_t, cptr),
                            (int) offsetof(r128_jit_tri_t, c_base),
                            (int) offsetof(r128_jit_tri_t, c_lim),
                            X64_R14, l->bppsh, ds->bpp, X64_R11, bails, nbail);
    if (l->z_on || l->sten)
        /* stencil without Z still resolves the Z group: the stencil
           byte lives in the Z cell */
        r128_x64_soa_group_addr(e, (int) offsetof(r128_jit_tri_t, zptr),
                                (int) offsetof(r128_jit_tri_t, z_base),
                                (int) offsetof(r128_jit_tri_t, z_lim),
                                X64_R15, l->zbppsh, ds->zbpp, X64_R10,
                                bails, nbail);
}

/* Aux scissors, in the interpreter's order: after coverage, before Z.
   R128_X64_SP_AUX has bit i set when rect i covers this row in y, so
   only the x range is tested per lane. A y-active subtractive rect
   clears from R128_X64_SP_KMASK the lanes inside its x range. When
   additive rects are enabled, a lane survives only if it lies inside
   the x range of at least one y-active additive rect; a row where none
   is y-active never reaches the vector loop (r128_x64_gen_row_aux
   exits it). This is rage128_aux_sc_pass per lane. The lane x values
   px+k are in xmm11; the signed PCMPGTD is correct because the screen
   coordinates are signed 32-bit values. */
static void
r128_x64_soa_lp_aux(r128_x64_soa_lp_t *l)
{
    r128_x64_emit_t            *e       = l->e;
    const rage128_draw_state_t *ds      = l->ds;
    int                         aux_add = 0;

    for (int i = 0; i < 3; i++)
        if ((ds->aux_cntl & (1u << (i * 2)))
            && !(ds->aux_cntl & (2u << (i * 2))))
            aux_add |= 1 << i;
    r128_x64_movd_x_r(e, 11, X64_R12);
    r128_x64_pshufd(e, 11, 11, 0x00);
    r128_x64_sse_rip(e, 0x66, 0xFE, 11, R128_X64_CP_LANEIDX);
    r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_SP_AUX);
    for (int i = 0; i < 3; i++) {
        int b1;

        if (!(ds->aux_cntl & (2u << (i * 2))))
            continue; /* enabled subtractive rects only */
        r128_x64_test_r32_imm(e, X64_RAX, 1u << i);
        b1 = r128_x64_jcc(e, X64_CC_E);
        r128_x64_splat_imm(e, 12, (uint32_t) ds->aux_x0[i], X64_RCX);
        r128_x64_pcmpgtd(e, 12, 11); /* x < x0 (outside-left)  */
        r128_x64_splat_imm(e, 0, (uint32_t) ds->aux_x1[i], X64_RCX);
        r128_x64_movaps_rr(e, 13, 11);
        r128_x64_pcmpgtd(e, 13, 0); /* x > x1 (outside-right) */
        r128_x64_por(e, 12, 13);
        r128_x64_movmskps(e, X64_RCX, 12);
        r128_x64_alu_mem_r(e, 0x21, 0, X64_RCX, X64_RSP,
                           R128_X64_SP_KMASK); /* keep outside   */
        r128_x64_patch32(e, b1, r128_x64_here(e));
    }
    if (aux_add) {
        /* additive: a lane must sit inside a y-active rect */
        r128_x64_mov_r32_imm32(e, X64_RDX, 0);
        for (int i = 0; i < 3; i++) {
            int b1;

            if (!(aux_add & (1 << i)))
                continue;
            r128_x64_test_r32_imm(e, X64_RAX, 1u << i);
            b1 = r128_x64_jcc(e, X64_CC_E);
            r128_x64_splat_imm(e, 12, (uint32_t) ds->aux_x0[i], X64_RCX);
            r128_x64_pcmpgtd(e, 12, 11);
            r128_x64_splat_imm(e, 0, (uint32_t) ds->aux_x1[i], X64_RCX);
            r128_x64_movaps_rr(e, 13, 11);
            r128_x64_pcmpgtd(e, 13, 0);
            r128_x64_por(e, 12, 13);
            r128_x64_movmskps(e, X64_RCX, 12);
            r128_x64_alu_r_imm(e, 6, 0, X64_RCX, 0xf); /* inside */
            r128_x64_alu_r_r(e, 0x09, 0, X64_RDX, X64_RCX);
            r128_x64_patch32(e, b1, r128_x64_here(e));
        }
        r128_x64_alu_mem_r(e, 0x21, 0, X64_RDX, X64_RSP,
                           R128_X64_SP_KMASK);
    }
}

/* Z stage. The four zline lanes come from the same serial chain of
   ADDSD as the C loop's zline += dZdx, so they are bit-exact: {z0, z1}
   in xmm12 and {z2, z3} in xmm13. xmm2 advances to the next group's
   zline here, so no bail may follow this point; the bails before it
   leave xmm2 at the group base the scalar loop expects. Then the
   quantize to zi and, without stencil, the Z test. */
static void
r128_x64_soa_lp_z(r128_x64_soa_lp_t *l)
{
    r128_x64_emit_t            *e    = l->e;
    const rage128_draw_state_t *ds   = l->ds;
    int                         sten = l->sten, zquant = l->zquant;

    r128_x64_movaps_rr(e, 12, 2);
    r128_x64_movaps_rr(e, 0, 2);
    r128_x64_addsd(e, 0, 3); /* z1         */
    r128_x64_unpcklpd(e, 12, 0);
    r128_x64_movaps_rr(e, 13, 0);
    r128_x64_addsd(e, 13, 3); /* z2         */
    r128_x64_movaps_rr(e, 1, 13);
    r128_x64_addsd(e, 1, 3); /* z3         */
    r128_x64_movaps_rr(e, 2, 1);
    r128_x64_addsd(e, 2, 3); /* next zline */
    r128_x64_unpcklpd(e, 13, 1);

    if (ds->fog_en && ds->fog_table_en) {
        /* table fog: save the raw zline lanes before the quantize
           overwrites them; r128_x64_soa_tfogf clamps them again */
        r128_x64_movaps_st(e, 12, X64_RSP, R128_X64_SP_SOA_TFA);
        r128_x64_movaps_st(e, 13, X64_RSP, R128_X64_SP_SOA_TFB);
    }

    if (zquant) {
        /* zc: NaN and values <= 0 become 0 through an AND with the
           "0 < z" mask, values above 1 become 1 through an
           AND/ANDN/OR select. Then zq = zc * zmax + 0.5, clamped to
           zmax, and zi = (uint32_t) zq by a per-lane 64-bit CVTTSD2SI,
           the scalar loop's convert. */
        for (int h = 0; h < 2; h++) {
            int zv = h ? 13 : 12;

            r128_x64_xorps(e, 0, 0);
            r128_x64_cmpltpd(e, 0, zv);
            r128_x64_pand(e, zv, 0);
            r128_x64_sse_rip(e, 0, 0x28, 1, R128_X64_CP_ONED2);
            r128_x64_movaps_rr(e, 0, 1);
            r128_x64_cmpltpd(e, 0, zv); /* m = 1 < z  */
            r128_x64_pand(e, 1, 0);
            r128_x64_pandn(e, 0, zv);
            r128_x64_por(e, 0, 1); /* zc         */
            r128_x64_sse_rip(e, 0x66, 0x59, 0, R128_X64_CP_ZMAXD2);
            r128_x64_sse_rip(e, 0x66, 0x58, 0, R128_X64_CP_HALFD2);
            r128_x64_sse_rip(e, 0, 0x28, 1, R128_X64_CP_ZMAXD2);
            r128_x64_movaps_rr(e, 14, 1);
            r128_x64_cmpltpd(e, 14, 0); /* zmax < zq  */
            r128_x64_pand(e, 1, 14);
            r128_x64_pandn(e, 14, 0);
            r128_x64_por(e, 14, 1); /* zq         */
            r128_x64_movaps_rr(e, zv, 14);
        }
        for (int k = 0; k < 4; k++) {
            int zv = (k < 2) ? 12 : 13;

            if (k & 1) {
                r128_x64_movaps_rr(e, 0, zv);
                r128_x64_unpckhpd(e, 0, 0);
                r128_x64_cvttsd2si_r64(e, X64_RAX, 0);
            } else
                r128_x64_cvttsd2si_r64(e, X64_RAX, zv);
            r128_x64_pinsrd(e, 11, X64_RAX, k);
        }
        r128_x64_movaps_st(e, 11, X64_RSP, R128_X64_SP_SOA_ZIV);
    }

    if (!sten && ds->zfn != 7) {
        int inv, mreg;

        /* Z buffer lanes. The whole group is in bounds, so the 8- or
           16-byte load is safe; values on masked-out lanes are not
           used. */
        if (ds->zbpp == 2) {
            r128_x64_movq_ld(e, 13, X64_R10, 0);
            r128_x64_pmovzxwd(e, 13, 13); /* zmax is 0xffff: no AND */
        } else {
            r128_x64_movups_ld(e, 13, X64_R10, 0);
            if (ds->zshift)
                r128_x64_psrld(e, 13, ds->zshift);
            if (ds->zmax != 0xffffffffu) {
                r128_x64_splat_imm(e, 0, ds->zmax, X64_RCX);
                r128_x64_pand(e, 13, 0);
            }
        }
        /* pass mask of r3d_cmp(zfn, zi, zbuf), ANDed into the lane
           mask */
        r128_x64_movaps_rr(e, 14, 11);
        mreg = r128_x64_soa_ucmp(e, ds->zfn, 14, 13, 0, &inv);
        r128_x64_movmskps(e, X64_RCX, mreg);
        if (inv)
            r128_x64_alu_r_imm(e, 6, 0, X64_RCX, 0xf);
        r128_x64_alu_mem_r(e, 0x21, 0, X64_RCX, X64_RSP,
                           R128_X64_SP_KMASK);
    }
}

/* Untextured alpha test. col[3] is the plain weight dot product
   (specular and fog change only red, green and blue), so it can be
   computed here, quantized as (uint32_t) (col[3] * 255.0f + 0.5f) and
   compared with the reference. The pass mask is ANDed into the lane
   mask so failed lanes never store or widen rx0/rx1. On covered lanes
   the value is near 0..255, where the signed CVTTPS2DQ and the C's
   unsigned cast agree; whatever the convert gives on uncovered lanes is
   masked off. */
static void
r128_x64_soa_lp_atest(r128_x64_soa_lp_t *l)
{
    r128_x64_emit_t            *e  = l->e;
    const rage128_draw_state_t *ds = l->ds;
    int                         inv, mreg;

    r128_x64_movaps_rr(e, 12, 8);
    r128_x64_pshufd(e, 0, 5, 0xFF);
    r128_x64_mulps(e, 12, 0);
    r128_x64_movaps_rr(e, 1, 9);
    r128_x64_pshufd(e, 0, 6, 0xFF);
    r128_x64_mulps(e, 1, 0);
    r128_x64_addps(e, 12, 1);
    r128_x64_movaps_rr(e, 1, 10);
    r128_x64_pshufd(e, 0, 7, 0xFF);
    r128_x64_mulps(e, 1, 0);
    r128_x64_addps(e, 12, 1);
    r128_x64_sse_rip(e, 0, 0x59, 12, R128_X64_CP_255F);
    r128_x64_sse_rip(e, 0, 0x58, 12, R128_X64_CP_HALFF);
    r128_x64_cvttps2dq(e, 12, 12);
    r128_x64_splat_imm(e, 13, ds->atest_ref & 0xff, X64_RCX);
    mreg = r128_x64_soa_ucmp(e, ds->atest_fn, 12, 13, 0, &inv);
    r128_x64_movmskps(e, X64_RCX, mreg);
    if (inv)
        r128_x64_alu_r_imm(e, 6, 0, X64_RCX, 0xf);
    r128_x64_alu_mem_r(e, 0x21, 0, X64_RCX, X64_RSP, R128_X64_SP_KMASK);
}

/* If no lane is left, advance to the next group with no store and no
   rx update. Otherwise update rx0/rx1 here, unless a later stage
   decides the final lanes: stencil updates rx from its write mask, and
   a textured alpha test or chroma key from the texture stage's mask. */
static void
r128_x64_soa_lp_kmask(r128_x64_soa_lp_t *l)
{
    r128_x64_emit_t *e = l->e;

    r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_SP_KMASK);
    r128_x64_alu_r_r(e, 0x85, 0, X64_RAX, X64_RAX);
    l->b_adv0 = r128_x64_jcc(e, X64_CC_E);

    if (!l->sten && !l->defer)
        r128_x64_soa_rx(e);
}

/* Textured group. The per-lane texel fetches use r10 as scratch and
   return the texel in r11d, so both cell bases wait in the frame
   (R128_X64_SP_SOAT_DC, R128_X64_SP_SOAT_ZC); zi is already saved at
   R128_X64_SP_SOA_ZIV. The stage leaves the packed lanes in xmm9 or,
   when specular, fog or blend follow, the float channels in the SRC
   slots. The weights in xmm8-xmm10 survive it. */
static void
r128_x64_soa_lp_color_tex(r128_x64_soa_lp_t *l)
{
    r128_x64_emit_t            *e    = l->e;
    const rage128_draw_state_t *ds   = l->ds;
    int                         z_on = l->z_on, sten = l->sten;
    int                         defer = l->defer;

    r128_x64_st(e, 1, X64_R11, X64_RSP, R128_X64_SP_SOAT_DC);
    if (z_on || sten)
        r128_x64_st(e, 1, X64_R10, X64_RSP, R128_X64_SP_SOAT_ZC);
    r128_x64_emit_soa_texstage(e, ds);
    r128_x64_ld(e, 1, X64_R11, X64_RSP, R128_X64_SP_SOAT_DC);
    if (z_on || sten)
        r128_x64_ld(e, 1, X64_R10, X64_RSP, R128_X64_SP_SOAT_ZC);
    if (defer) {
        /* mask after the stage's tests: with no lane left the group
           advances with no store, no rx update and no stencil
           write-back, since the interpreter drops a pixel that fails
           the alpha test or chroma key before the stencil update */
        r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_SP_KMASK);
        r128_x64_alu_r_r(e, 0x85, 0, X64_RAX, X64_RAX);
        l->b_adv1 = r128_x64_jcc(e, X64_CC_E);
        if (!sten)
            r128_x64_soa_rx(e);
    }
    if (ds->spec_en || ds->fog_en) {
        if (ds->fog_en)
            /* always vertex fog: textured states with table fog are
               not compiled inline (tex_inline in r128_x64_gen_setup),
               so they never reach the vector loop */
            r128_x64_soa_fogf(e);
        r128_x64_soa_specfog(e, ds);
        if (!ds->alpha_en)
            r128_x64_soa_pack_slots(e, ds);
    }
    if (ds->alpha_en)
        r128_x64_emit_soa_blend(e, ds);
}

/* Untextured group with blend, specular or fog. The four float channel
   dot products go to the SRC slots; then come the fog factor (table or
   vertex), the specular/fog stage, and either the blend stage or, with
   blend off, the slot pack. */
static void
r128_x64_soa_lp_color_slots(r128_x64_soa_lp_t *l)
{
    r128_x64_emit_t            *e       = l->e;
    const rage128_draw_state_t *ds      = l->ds;
    static const int            bslo[4] = { R128_X64_SP_SOA_SRCR,
                                            R128_X64_SP_SOA_SRCG,
                                            R128_X64_SP_SOA_SRCB,
                                            R128_X64_SP_SOA_SRCA };

    for (int k = 0; k < 4; k++) {
        uint8_t sel = (uint8_t) (k * 0x55);

        r128_x64_movaps_rr(e, 12, 8);
        r128_x64_pshufd(e, 0, 5, sel);
        r128_x64_mulps(e, 12, 0);
        r128_x64_movaps_rr(e, 1, 9);
        r128_x64_pshufd(e, 0, 6, sel);
        r128_x64_mulps(e, 1, 0);
        r128_x64_addps(e, 12, 1);
        r128_x64_movaps_rr(e, 1, 10);
        r128_x64_pshufd(e, 0, 7, sel);
        r128_x64_mulps(e, 1, 0);
        r128_x64_addps(e, 12, 1);
        r128_x64_movaps_st(e, 12, X64_RSP, bslo[k]);
    }
    if (ds->fog_en) {
        if (ds->fog_table_en)
            r128_x64_soa_tfogf(e);
        else
            r128_x64_soa_fogf(e);
    }
    if (ds->spec_en || ds->fog_en)
        r128_x64_soa_specfog(e, ds);
    if (ds->alpha_en)
        r128_x64_emit_soa_blend(e, ds);
    else
        r128_x64_soa_pack_slots(e, ds);
}

/* Plain pack, for untextured groups without blend, specular or fog:
   ARGB 8888 out = a<<24 | r<<16 | g<<8 | b, or RGB 565
   raw = (r>>3)<<11 | (g>>2)<<5 | (b>>3) with alpha unused. Every channel
   is at most 255 here (the shading weights sum to 1 within a few ulp;
   see the dither note in r128_a64_gen_pack_store), so this equals the
   interpreter's masked 565 build from the bytes of out. The accumulator
   is xmm11, not xmm9, because every channel dot reads the weight
   vectors in xmm8/9/10; xmm11 is free since zi is saved at
   R128_X64_SP_SOA_ZIV. Alpha-bearing 16-bit formats use their own
   field shifts and alpha quantization below. */
static void
r128_x64_soa_lp_color_pack(r128_x64_soa_lp_t *l)
{
    r128_x64_emit_t            *e       = l->e;
    const rage128_draw_state_t *ds      = l->ds;
    int                         dith_on = l->dith_on;

    if (ds->dst_dt == 6) {
        r128_x64_soa_channel(e, 11, 1, 3, 0, 0); /* a          */
        r128_x64_pslld(e, 11, 24);
        r128_x64_soa_channel(e, 12, 1, 0, 0, 0); /* r          */
        r128_x64_pslld(e, 12, 16);
        r128_x64_por(e, 11, 12);
        r128_x64_soa_channel(e, 12, 1, 1, 0, 0); /* g          */
        r128_x64_pslld(e, 12, 8);
        r128_x64_por(e, 11, 12);
        r128_x64_soa_channel(e, 12, 1, 2, 0, 0); /* b          */
        r128_x64_por(e, 11, 12);
        r128_x64_movaps_rr(e, 9, 11);
    } else if (ds->dst_dt == 3 || ds->dst_dt == 15) {
        /* Plain shading quantizes to bytes before the field shifts.
           The 1555 alpha threshold uses bit seven without dithering;
           4444 adds the full Bayer threshold to every channel. The
           narrowed lanes use the common 16-bit masked store paths. */
        int bits = ds->dst_dt == 3 ? 5 : 4;

        r128_x64_soa_channel(e, 11, 1, 3,
                             dith_on && ds->dst_dt == 15, e->soa_bay1);
        r128_x64_psrld(e, 11, ds->dst_dt == 3 ? 7 : 4);
        r128_x64_pslld(e, 11, bits * 3);
        for (int ch = 0; ch < 3; ch++) {
            r128_x64_soa_channel(e, 12, 1, ch, dith_on, e->soa_bay1);
            r128_x64_psrld(e, 12, 8 - bits);
            if (ch != 2)
                r128_x64_pslld(e, 12, (2 - ch) * bits);
            r128_x64_por(e, 11, 12);
        }
        r128_x64_movaps_rr(e, 9, 11);
        r128_x64_packusdw(e, 9, 9);
    } else {
        r128_x64_soa_channel(e, 11, 1, 0, dith_on, e->soa_bay1);
        r128_x64_psrld(e, 11, 3);
        r128_x64_pslld(e, 11, 11);
        r128_x64_soa_channel(e, 12, 1, 1, dith_on, e->soa_bay2);
        r128_x64_psrld(e, 12, 2);
        r128_x64_pslld(e, 12, 5);
        r128_x64_por(e, 11, 12);
        r128_x64_soa_channel(e, 12, 1, 2, dith_on, e->soa_bay1);
        r128_x64_psrld(e, 12, 3);
        r128_x64_por(e, 11, 12);
        r128_x64_movaps_rr(e, 9, 11);
        r128_x64_packusdw(e, 9, 9);
    }
}

/* Stores. A group with all four lanes set takes one contiguous vector
   store of color and of Z; any other group goes to the per-lane path
   below. A partial PLANE_3D_MASK_C write mask (ds->wmask) merges
   against the packed pixels after dithering, as r3d_dst_write does:
   the masked bits come from the new pixels, the rest from the old ones.
   At 16 bpp the 16-bit mask is repeated in both halves of each dword.
   A 32-bit Z write keeps the cell bits outside zmax << zshift, which
   hold the stencil byte when there is one, as the interpreter's Z write
   does. The lane mask is in eax. */
static void
r128_x64_soa_lp_store_full(r128_x64_soa_lp_t *l)
{
    r128_x64_emit_t            *e    = l->e;
    const rage128_draw_state_t *ds   = l->ds;
    int                         z_on = l->z_on;

    if (!l->sten) /* with stencil, eax already holds the write mask */
        r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_SP_KMASK);
    r128_x64_alu_r_imm(e, 7, 0, X64_RAX, 0xf);
    l->b_part = r128_x64_jcc(e, X64_CC_NE);
    if (ds->dst_dt == 6) {
        if (ds->wmask != 0xffffffffu) {
            r128_x64_movups_ld(e, 0, X64_R11, 0);
            r128_x64_splat_imm(e, 1, ds->wmask, X64_RCX);
            r128_x64_pand(e, 9, 1);
            r128_x64_pandn(e, 1, 0);
            r128_x64_por(e, 9, 1);
        }
        r128_x64_sse_rm(e, 0, 0x11, 9, X64_R11, 0); /* movups st  */
    } else {
        if ((ds->wmask & 0xffff) != 0xffff) {
            uint32_t m = ds->wmask & 0xffff;

            r128_x64_movq_ld(e, 0, X64_R11, 0);
            r128_x64_splat_imm(e, 1, m | (m << 16), X64_RCX);
            r128_x64_pand(e, 9, 1);
            r128_x64_pandn(e, 1, 0);
            r128_x64_por(e, 9, 1);
        }
        r128_x64_movq_st(e, 9, X64_R11, 0);
    }
    if (z_on && ds->z_wr) {
        r128_x64_movaps_ld(e, 0, X64_RSP, R128_X64_SP_SOA_ZIV);
        if (ds->zbpp == 2) {
            r128_x64_packusdw(e, 0, 0); /* zi <= zmax = 0xffff        */
            r128_x64_movq_st(e, 0, X64_R10, 0);
        } else {
            uint32_t keep = ~(ds->zmax << ds->zshift);

            if (ds->zshift)
                r128_x64_pslld(e, 0, ds->zshift);
            if (keep != 0) {
                r128_x64_movups_ld(e, 1, X64_R10, 0);
                r128_x64_splat_imm(e, 12, keep, X64_RCX);
                r128_x64_pand(e, 1, 12);
                r128_x64_por(e, 0, 1);
            }
            r128_x64_sse_rm(e, 0, 0x11, 0, X64_R10, 0);
        }
    }
    l->b_full = r128_x64_jmp(e);
}

/* Partial group: the packed color goes to R128_X64_SP_COL (and 16-bit
   zi to R128_X64_SP_ZL), then each lane whose mask bit is set stores
   its pixel and Z value with the same merges as the full path. eax is
   not changed. */
static void
r128_x64_soa_lp_store_partial(r128_x64_soa_lp_t *l)
{
    r128_x64_emit_t            *e    = l->e;
    const rage128_draw_state_t *ds   = l->ds;
    int                         z_on = l->z_on;

    r128_x64_patch32(e, l->b_part, r128_x64_here(e));
    if (ds->dst_dt == 6)
        r128_x64_movaps_st(e, 9, X64_RSP, R128_X64_SP_COL);
    else
        r128_x64_movq_st(e, 9, X64_RSP, R128_X64_SP_COL);
    if (z_on && ds->z_wr && ds->zbpp == 2) {
        r128_x64_movaps_ld(e, 0, X64_RSP, R128_X64_SP_SOA_ZIV);
        r128_x64_packusdw(e, 0, 0);
        r128_x64_movq_st(e, 0, X64_RSP, R128_X64_SP_ZL);
    }
    for (int k = 0; k < 4; k++) {
        int b1;

        r128_x64_test_r32_imm(e, X64_RAX, 1u << k);
        b1 = r128_x64_jcc(e, X64_CC_E);
        if (ds->dst_dt == 6) {
            r128_x64_ld(e, 0, X64_RCX, X64_RSP, R128_X64_SP_COL + 4 * k);
            if (ds->wmask != 0xffffffffu) {
                r128_x64_ld(e, 0, X64_RDX, X64_R11, 4 * k);
                r128_x64_alu_r_imm(e, 4, 0, X64_RCX, (int32_t) ds->wmask);
                r128_x64_alu_r_imm(e, 4, 0, X64_RDX,
                                   (int32_t) ~ds->wmask);
                r128_x64_alu_r_r(e, 0x09, 0, X64_RCX, X64_RDX);
            }
            r128_x64_st(e, 0, X64_RCX, X64_R11, 4 * k);
        } else {
            r128_x64_ldzx16(e, X64_RCX, X64_RSP, R128_X64_SP_COL + 2 * k);
            if ((ds->wmask & 0xffff) != 0xffff) {
                r128_x64_ldzx16(e, X64_RDX, X64_R11, 2 * k);
                r128_x64_alu_r_imm(e, 4, 0, X64_RCX,
                                   (int32_t) (ds->wmask & 0xffff));
                r128_x64_alu_r_imm(e, 4, 0, X64_RDX,
                                   (int32_t) (~ds->wmask & 0xffff));
                r128_x64_alu_r_r(e, 0x09, 0, X64_RCX, X64_RDX);
            }
            r128_x64_st16(e, X64_RCX, X64_R11, 2 * k);
        }
        if (z_on && ds->z_wr) {
            if (ds->zbpp == 2) {
                r128_x64_ldzx16(e, X64_RCX, X64_RSP, R128_X64_SP_ZL + 2 * k);
                r128_x64_st16(e, X64_RCX, X64_R10, 2 * k);
            } else {
                uint32_t keep = ~(ds->zmax << ds->zshift);

                r128_x64_ld(e, 0, X64_RCX, X64_RSP,
                            R128_X64_SP_SOA_ZIV + 4 * k);
                if (ds->zshift)
                    r128_x64_shift_imm(e, 4, 0, X64_RCX, ds->zshift);
                if (keep != 0) {
                    r128_x64_ld(e, 0, X64_RDX, X64_R10, 4 * k);
                    r128_x64_alu_r_imm(e, 4, 0, X64_RDX, (int32_t) keep);
                    r128_x64_alu_r_r(e, 0x09, 0, X64_RCX, X64_RDX);
                }
                r128_x64_st(e, 0, X64_RCX, X64_R10, 4 * k);
            }
        }
        r128_x64_patch32(e, b1, r128_x64_here(e));
    }
    r128_x64_patch32(e, l->b_full, r128_x64_here(e));
}

/* Advance to the next group: e0..e2 += 4 * dxi (integer, so equal to
   four single steps) and px += 4, then back to the loop head. With Z
   on, the Z stage has already advanced xmm2. All "no lane left"
   branches land here. */
static void
r128_x64_soa_lp_advance(r128_x64_soa_lp_t *l)
{
    r128_x64_emit_t *e   = l->e;
    int              adv = r128_x64_here(e);

    r128_x64_patch32(e, l->b_adv0, adv);
    if (l->b_adv1 >= 0)
        r128_x64_patch32(e, l->b_adv1, adv);
    if (l->b_adv2 >= 0)
        r128_x64_patch32(e, l->b_adv2, adv);
    r128_x64_ld(e, 1, X64_RAX, X64_RBX, (int) offsetof(r128_jit_tri_t, e0dxi));
    r128_x64_lea_scaled(e, X64_RSI, X64_RSI, X64_RAX, 2);
    r128_x64_ld(e, 1, X64_RAX, X64_RBX, (int) offsetof(r128_jit_tri_t, e1dxi));
    r128_x64_lea_scaled(e, X64_RDI, X64_RDI, X64_RAX, 2);
    r128_x64_ld(e, 1, X64_RAX, X64_RBX, (int) offsetof(r128_jit_tri_t, e2dxi));
    r128_x64_lea_scaled(e, X64_RBP, X64_RBP, X64_RAX, 2);
    r128_x64_alu_r_imm(e, 0, 0, X64_R12, 4);
    r128_x64_jmp_to(e, l->head);
}

static void
r128_x64_emit_soa_loop(r128_x64_emit_t *e, const rage128_draw_state_t *ds,
                       int *bails, int *nbail)
{
    r128_x64_soa_lp_t l;

    r128_x64_soa_lp_setup(&l, e, ds, bails, nbail);

    if (l.sten || l.zquant)
        r128_x64_soa_lp_rowguard(&l);
    r128_x64_soa_lp_head(&l);
    r128_x64_soa_lp_lanes(&l);
    r128_x64_soa_lp_groups(&l);
    if (ds->aux_on)
        r128_x64_soa_lp_aux(&l);
    if (l.z_on)
        r128_x64_soa_lp_z(&l);
    if (l.sten)
        /* the Z and stencil results do not drop lanes from the mask:
           failed lanes still get their fail operation in the stencil
           stage */
        r128_x64_soa_sten_zblock(e, ds);
    if (l.at_on && !ds->tex_en)
        r128_x64_soa_lp_atest(&l);
    r128_x64_soa_lp_kmask(&l);

    /* color channels -> packed lanes in xmm9 */
    if (ds->tex_en)
        r128_x64_soa_lp_color_tex(&l);
    else if (ds->alpha_en || ds->spec_en || ds->fog_en)
        r128_x64_soa_lp_color_slots(&l);
    else
        r128_x64_soa_lp_color_pack(&l);

    if (l.sten) {
        /* stencil write-back on the op-mask lanes, then the write mask
           in R128_X64_SP_KMASK. A group with no lane left in it
           advances with the write-back done and no store or rx update,
           as the interpreter's continue does per pixel */
        r128_x64_emit_soa_sten(e, ds);
        r128_x64_ld(e, 0, X64_RAX, X64_RSP, R128_X64_SP_KMASK);
        r128_x64_alu_r_r(e, 0x85, 0, X64_RAX, X64_RAX);
        l.b_adv2 = r128_x64_jcc(e, X64_CC_E);
        r128_x64_soa_rx(e);
    }

    r128_x64_soa_lp_store_full(&l);
    r128_x64_soa_lp_store_partial(&l);
    r128_x64_soa_lp_advance(&l);
}

#endif /* VIDEO_ATI_RAGE128_CODEGEN_X86_64_SOA_H */
