# x86-64 → ARM64 Instruction Mapping for Voodoo Codegen

This document maps every x86-64 instruction emitted in `vid_voodoo_codegen_x86-64.h` to its ARM64 equivalent(s). Instructions are grouped by category. Where a single x86-64 instruction requires multiple ARM64 instructions, the expansion is noted.

Reference: ARM Architecture Reference Manual (ARMv8-A), A64 ISA.

---

## Notation

- **Wn** = 32-bit register view (w0–w30)
- **Xn** = 64-bit register view (x0–x30)
- **Vn** = 128-bit NEON register (v0–v31)
- **Dn** = 64-bit NEON lane (d0–d31, lower half of Vn)
- **#imm** = immediate value
- **[Xn, #off]** = base+offset addressing
- 1:1 = direct 1-to-1 mapping, 1:N = one x86 instruction requires N ARM64 instructions

---

## 1. Data Movement — GPR

### MOV reg, reg (32-bit)
- **x86-64**: `MOV EAX, EBX` (opcode `89 xx` / `8b xx`)
- **ARM64**: `MOV Wd, Ws` (alias for `ORR Wd, WZR, Ws`)
- **Encoding**: `0x2A0003E0 | Rm(src) | Rd(dst)`
- **Ratio**: 1:1

### MOV reg, reg (64-bit)
- **x86-64**: `MOV RAX, RBX` (REX.W prefix, opcode `48 89 xx` / `48 8b xx`)
- **ARM64**: `MOV Xd, Xs` (alias for `ORR Xd, XZR, Xs`)
- **Encoding**: `0xAA0003E0 | Rm(src) | Rd(dst)`
- **Ratio**: 1:1

### MOV reg, imm32
- **x86-64**: `MOV EAX, imm32` (opcode `b8+rd`, 5 bytes)
- **ARM64 (imm fits 16 bits)**: `MOVZ Wd, #imm16`
- **ARM64 (imm > 16 bits)**: `MOVZ Wd, #(imm & 0xFFFF)` + `MOVK Wd, #(imm >> 16), LSL #16`
- **Encoding MOVZ**: `0x52800000 | IMM16(imm) | Rd(dst)`
- **Encoding MOVK**: `0x72A00000 | IMM16(imm>>16) | Rd(dst)`
- **Ratio**: 1:1 or 1:2 depending on value

### MOV reg, imm64
- **x86-64**: `MOV RAX, imm64` (REX.W + opcode `b8+rd`, 10 bytes)
- **ARM64**: Up to 4 instructions: `MOVZ Xd, #imm[15:0]` + `MOVK Xd, #imm[31:16], LSL #16` + `MOVK Xd, #imm[47:32], LSL #32` + `MOVK Xd, #imm[63:48], LSL #48`
- **Encoding MOVZ(X)**: `0xD2800000 | IMM16(imm) | Rd(dst)`
- **Encoding MOVK(X) hw=1**: `0xF2A00000 | IMM16(imm) | Rd(dst)` (hw=2: `0xF2C00000`, hw=3: `0xF2E00000`)
- **Ratio**: 1:2 to 1:4 (use ADRP+ADD for PC-relative table addresses where possible)
- **Note**: The x86-64 codegen uses this heavily for loading pointers to lookup tables (alookup, aminuslookup, bilinear_lookup, logtable, etc.). On ARM64, prefer ADRP+ADD for code-relative addresses, or pre-load into callee-saved registers in the prologue.

### MOVZX reg32, reg8/reg16 (zero-extend)
- **x86-64**: `MOVZX EAX, BL` / `MOVZX EAX, BX` (opcode `0F B6` / `0F B7`)
- **ARM64 (byte)**: `UXTB Wd, Ws` (alias for `UBFX Wd, Ws, #0, #8`)
- **ARM64 (halfword)**: `UXTH Wd, Ws` (alias for `UBFX Wd, Ws, #0, #16`)
- **ARM64 (from memory, byte)**: `LDRB Wd, [Xbase, Xoff]`
- **ARM64 (from memory, halfword)**: `LDRH Wd, [Xbase, Xoff]`
- **Encoding UBFX(byte)**: `0x53001C00 | Rn(src) | Rd(dst)` (immr=0, imms=7)
- **Encoding UBFX(half)**: `0x53003C00 | Rn(src) | Rd(dst)` (immr=0, imms=15)
- **Ratio**: 1:1

### MOVZX from memory (16-bit)
- **x86-64**: `MOVZX EBX, [ECX+EBX*2]` — used for reading depth buffer
- **ARM64**: `LDRH Wd, [Xbase, Xoff, LSL #1]`
- **Encoding**: `0x78607800 | Rm(off) | Rn(base) | Rt(dst)` (option=011, S=1 for LSL #1)
- **Ratio**: 1:1

---

## 2. Load / Store — GPR

### MOV reg, [base+disp32] (32-bit load)
- **x86-64**: `MOV EAX, [RDI+disp32]` (opcode `8b 87 disp32`) — used extensively for `state->field` access
- **ARM64 (disp < 16380, 4-aligned)**: `LDR Wd, [Xn, #imm12_scaled]`
- **ARM64 (disp > 16380 or unaligned)**: `MOV Xtmp, #disp` + `LDR Wd, [Xn, Xtmp]`
- **Encoding LDR(imm)**: `0xB9400000 | (imm12 << 10) | Rn(base) | Rt(dst)` where imm12 = disp/4
- **Note**: `voodoo_state_t` offsets are typically < 4096 bytes, fitting unsigned offset. `voodoo_params_t` can be larger — check at codegen time.
- **Ratio**: 1:1 (common case) or 1:2 (large offset)

### MOV reg, [base+disp32] (64-bit load)
- **x86-64**: `MOV RAX, [RDI+disp32]` (REX.W prefix)
- **ARM64**: `LDR Xd, [Xn, #imm12_scaled]` (scaled by 8)
- **Encoding**: `0xF9400000 | (imm12 << 10) | Rn(base) | Rt(dst)` where imm12 = disp/8
- **Ratio**: 1:1

### MOV [base+disp32], reg32 (32-bit store)
- **x86-64**: `MOV [RDI+disp32], EAX` (opcode `89 87 disp32`)
- **ARM64**: `STR Wd, [Xn, #imm12_scaled]`
- **Encoding**: `0xB9000000 | (imm12 << 10) | Rn(base) | Rt(src)`
- **Ratio**: 1:1

### MOV [base+disp32], reg64 (64-bit store)
- **x86-64**: `MOV [RDI+disp32], RAX` (REX.W)
- **ARM64**: `STR Xd, [Xn, #imm12_scaled]`
- **Encoding**: `0xF9000000 | (imm12 << 10) | Rn(base) | Rt(src)`
- **Ratio**: 1:1

### MOV [base+index*2], reg16 (16-bit store)
- **x86-64**: `MOV [RSI+EDX*2], AX` (prefix `66`, opcode `89 04 56`) — framebuffer/depth write
- **ARM64**: `STRH Wd, [Xn, Xm, LSL #1]`
- **Encoding**: `0x78207800 | Rm(idx) | Rn(base) | Rt(src)`
- **Ratio**: 1:1

### MOV reg32, [base+index*4] (32-bit indexed load)
- **x86-64**: `MOV EAX, [RBP+RBX*4]` — texture memory read
- **ARM64**: `LDR Wd, [Xbase, Xidx, LSL #2]`
- **Encoding**: `0xB8607800 | Rm(idx) | Rn(base) | Rt(dst)`
- **Ratio**: 1:1

### LEA (load effective address)
- **x86-64**: `LEA RSI, [RSI+RCX*4]` / `LEA RBX, [RBP+RBX*4]` / `LEA EAX, 1[EDX,EBX]`
- **ARM64**: `ADD Xd, Xn, Xm, LSL #shift` (for scaled index)
- **ARM64**: `ADD Wd, Wn, Wm` then `ADD Wd, Wd, #1` (for 3-operand + constant)
- **Ratio**: 1:1 (simple) or 1:2 (with constant addend)
- **Note**: ARM64 has no LEA instruction. Use ADD with shifted register or MADD.

### PUSH / POP
- **x86-64**: `PUSH RBP` / `POP RBP` (prologue/epilogue)
- **ARM64**: `STP X29, X30, [SP, #-imm]!` (pre-index) for save, `LDP X29, X30, [SP], #imm` (post-index) for restore
- **Encoding STP**: `0xA9800000 | IMM7(imm) | Rt2(reg2) | Rn(SP=31) | Rt(reg1)`
- **Encoding LDP**: `0xA8C00000 | IMM7(imm) | Rt2(reg2) | Rn(SP=31) | Rt(reg1)`
- **Note**: ARM64 saves/restores registers in pairs. The prologue should STP all callee-saved registers (x19-x28, x29/x30, d8-d15) in a single sequence. The x86-64 codegen pushes RBP, RDI, RSI, RBX, R12-R15 (8 regs).
- **Ratio**: 8 x86 PUSH → ~4 STP instructions

---

## 3. Arithmetic — GPR

### ADD reg, reg (32-bit)
- **x86-64**: `ADD EAX, EBX` (opcode `01 xx`)
- **ARM64**: `ADD Wd, Wn, Wm`
- **Encoding**: `0x0B000000 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Ratio**: 1:1
- **Note**: x86 ADD is destructive (dst = dst + src). ARM64 ADD is 3-operand. When porting `ADD EAX, EBX`, emit `ADD Wd_eax, Wd_eax, Wd_ebx`.

### ADD reg, imm8 (32-bit)
- **x86-64**: `ADD EAX, imm8` / `ADD EBX, 1` (opcode `83 c0 imm8`)
- **ARM64**: `ADD Wd, Wn, #imm12`
- **Encoding**: `0x11000000 | (imm12 << 10) | Rn(src) | Rd(dst)`
- **Ratio**: 1:1

### ADD reg, [base+disp] (memory-source add)
- **x86-64**: `ADD EAX, state->lod` / `ADD EAX, params->zaColor[ESI]` (opcode `03 87 disp32`)
- **ARM64**: `LDR Wtmp, [Xn, #off]` + `ADD Wd, Wd, Wtmp`
- **Ratio**: 1:2

### ADD mem32, reg (memory-destination add)
- **x86-64**: `ADD [RDI+disp], EAX` / `ADD state->z[EDI], EAX` — per-pixel state update
- **ARM64**: `LDR Wtmp, [Xn, #off]` + `ADD Wtmp, Wtmp, Wsrc` + `STR Wtmp, [Xn, #off]`
- **Ratio**: 1:3
- **Note**: ARM64 is load-store architecture — no memory-destination arithmetic.

### ADD mem32, imm8 (memory-destination add immediate)
- **x86-64**: `ADD state->pixel_count[EDI], 1` (opcode `83 87 disp32 imm8`)
- **ARM64**: `LDR Wtmp, [Xn, #off]` + `ADD Wtmp, Wtmp, #imm` + `STR Wtmp, [Xn, #off]`
- **Ratio**: 1:3

### SUB reg, reg (32-bit)
- **x86-64**: `SUB EAX, EBX` (opcode `29 xx`)
- **ARM64**: `SUB Wd, Wn, Wm`
- **Encoding**: `0x4B000000 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Ratio**: 1:1

### SUB reg, [base+disp] (memory-source subtract)
- **x86-64**: `SUB EAX, state->lod` (opcode `2B 87 disp32`)
- **ARM64**: `LDR Wtmp, [Xn, #off]` + `SUB Wd, Wd, Wtmp`
- **Ratio**: 1:2

### SUB reg, imm8
- **x86-64**: `SUB EDX, 19` (opcode `83 ea 13`)
- **ARM64**: `SUB Wd, Wn, #imm12`
- **Encoding**: `0x51000000 | (imm12 << 10) | Rn(src) | Rd(dst)`
- **Ratio**: 1:1

### SUB mem32, reg
- **x86-64**: `SUB state->x[EDI], 1` (opcode `83 af disp32 01`)
- **ARM64**: `LDR Wtmp, [Xn, #off]` + `SUB Wtmp, Wtmp, Wsrc` + `STR Wtmp, [Xn, #off]`
- **Ratio**: 1:3

### NEG reg
- **x86-64**: `NEG EAX` (opcode `F7 D8`)
- **ARM64**: `NEG Wd, Ws` (alias for `SUB Wd, WZR, Ws`)
- **Encoding**: `0x4B0003E0 | Rm(src) | Rd(dst)`
- **Ratio**: 1:1

### NOT reg
- **x86-64**: `NOT EAX` (opcode `F7 D0`)
- **ARM64**: `MVN Wd, Ws` (alias for `ORN Wd, WZR, Ws`)
- **Encoding**: `0x2A2003E0 | Rm(src) | Rd(dst)`
- **Ratio**: 1:1

### IMUL reg, reg (signed multiply, 32-bit)
- **x86-64**: `IMUL EAX, EBX` (opcode `0F AF C3`)
- **ARM64**: `MUL Wd, Wn, Wm`
- **Encoding**: `0x1B007C00 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Ratio**: 1:1

### IMUL reg64, reg64 (signed multiply, 64-bit)
- **x86-64**: `IMUL RBX, RAX` (REX.W + `0F AF xx`)
- **ARM64**: `MUL Xd, Xn, Xm`
- **Encoding**: `0x9B007C00 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Ratio**: 1:1

### IDIV reg64 (signed divide)
- **x86-64**: `IDIV state->tmu_w` (REX.W + `F7 BF disp32`) — divides RDX:RAX by operand, quotient in RAX
- **ARM64**: `SDIV Xd, Xn, Xm`
- **Encoding**: `0x9AC00C00 | Rm(divisor) | Rn(dividend) | Rd(dst)`
- **Note**: x86 IDIV uses implicit RDX:RAX as 128-bit dividend. The Voodoo codegen always zeroes RDX first (`XOR RDX, RDX`), so the effective dividend is just RAX. Direct translation to `SDIV Xd, Xdividend, Xdivisor`. The W division in texture fetch uses `(1<<48) / tmu_w` — this fits in a 64-bit SDIV.
- **Ratio**: 1:1

### MUL reg8 (unsigned 8-bit multiply)
- **x86-64**: `MUL params->fogTable+1[RSI+RBX*2]` (opcode `F6 A4 5E disp32`) — multiplies AL by byte operand, result in AX
- **ARM64**: `LDRB Wtmp, [Xbase, Xoff]` + `MUL Wd, Wd_al, Wtmp` (after zero-extending AL)
- **Ratio**: 1:2 (load + multiply separate)
- **Note**: ARM64 has no implicit accumulator multiply. Extract AL value explicitly.

---

## 4. Bitwise / Logic — GPR

### AND reg, reg
- **x86-64**: `AND EAX, EBX`
- **ARM64**: `AND Wd, Wn, Wm`
- **Encoding**: `0x0A000000 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Ratio**: 1:1

### AND reg, imm
- **x86-64**: `AND EAX, 0xff` (opcode `25 imm32`) / `AND EBX, 0xf` (opcode `83 e3 0f`)
- **ARM64**: `AND Wd, Wn, #bitmask_imm` (if encodable as logical immediate)
- **ARM64 (non-encodable)**: `MOV Wtmp, #imm` + `AND Wd, Wn, Wtmp`
- **Encoding**: `0x12000000 | IMM_LOGICAL(N:immr:imms) | Rn(src) | Rd(dst)`
- **Note**: ARM64 logical immediates must be bitmask patterns (repeated, contiguous bit patterns). Common masks like `0xFF`, `0xFFFF`, `0xF`, `0x3F`, `0xFFF`, `0x1000`, `0x80000000` are all encodable. `0xFFFFFF` and `0x3FC` are also encodable. Verify each mask at codegen time.
- **Ratio**: 1:1 (most cases) or 1:2 (rare non-encodable patterns)

### AND reg, [base+disp]
- **x86-64**: `AND EAX, params->tex_w_mask[ESI]`
- **ARM64**: `LDR Wtmp, [Xparams, #off]` + `AND Wd, Wn, Wtmp`
- **Ratio**: 1:2

### OR reg, reg
- **x86-64**: `OR EAX, EBX` (opcode `09 xx`)
- **ARM64**: `ORR Wd, Wn, Wm`
- **Encoding**: `0x2A000000 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Ratio**: 1:1

### OR reg, imm
- **x86-64**: `OR EAX, EDX` — sometimes with immediate
- **ARM64**: `ORR Wd, Wn, #bitmask_imm` (if encodable)
- **Ratio**: 1:1

### XOR reg, reg
- **x86-64**: `XOR EAX, EAX` (opcode `31 c0`) / `XOR EBX, EAX` (opcode `31 c3`)
- **ARM64 (zero)**: `MOV Wd, WZR` (alias `MOVZ Wd, #0`)
- **ARM64 (general)**: `EOR Wd, Wn, Wm`
- **Encoding EOR**: `0x4A000000 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Note**: `XOR reg, reg` (self-XOR for zeroing) maps to `MOV Wd, WZR` or `MOVZ Wd, #0`.
- **Ratio**: 1:1

### XOR reg, [base+index] (memory-source XOR)
- **x86-64**: `XOR EAX, R13(i_00_ff_w)[ECX*4]` — used in trilinear LOD blend
- **ARM64**: `LDR Wtmp, [Xbase, Xidx, LSL #2]` + `EOR Wd, Wd, Wtmp`
- **Ratio**: 1:2

### XOR reg, imm
- **x86-64**: `XOR EAX, 0xff` (opcode `35 ff000000`)
- **ARM64**: `EOR Wd, Wn, #bitmask_imm`
- **Encoding**: `0x52000000 | IMM_LOGICAL(N:immr:imms) | Rn(src) | Rd(dst)`
- **Note**: `0xFF` is encodable as a logical immediate.
- **Ratio**: 1:1

### TEST reg, reg
- **x86-64**: `TEST EAX, EAX` / `TEST EBX, EBX` (opcode `85 xx`) — sets flags only
- **ARM64**: `TST Wn, Wm` (alias for `ANDS WZR, Wn, Wm`) or `CMP Wn, #0` for simple zero-test
- **Encoding TST(reg)**: `0x6A000000 | Rm(src2) | Rn(src1) | Rd(WZR=31)`
- **Ratio**: 1:1

### TEST reg, imm
- **x86-64**: `TEST EAX, 0x1000` (opcode `A9 imm32`) / `TEST [RDI+disp], imm32`
- **ARM64**: `TST Wn, #bitmask_imm` (if encodable) or `MOV Wtmp, #imm` + `TST Wn, Wtmp`
- **For single-bit tests**: Use `TBZ` / `TBNZ` (test bit and branch) directly — more efficient
- **Encoding TST(imm)**: `0x72000000 | IMM_LOGICAL(N:immr:imms) | Rn(src) | Rd(WZR=31)`
- **Encoding TBZ**: `0x36000000 | BIT(bit) | OFFSET14(off) | Rt(src)`
- **Encoding TBNZ**: `0x37000000 | BIT(bit) | OFFSET14(off) | Rt(src)`
- **Ratio**: 1:1

### TEST mem, imm
- **x86-64**: `TEST state->stipple[EDI], EAX` / `TEST state->stipple[EDI], 0x80000000`
- **ARM64**: `LDR Wtmp, [Xn, #off]` + `TST Wtmp, Wreg`/`TST Wtmp, #imm`
- **Ratio**: 1:2

---

## 5. Shifts — GPR

### SHL reg, CL (variable left shift)
- **x86-64**: `SHL EAX, CL` / `SHL EBX, CL` / `SHL EBP, CL` (opcode `D3 E0-EF`)
- **ARM64**: `LSL Wd, Wn, Wshift`
- **Encoding**: `0x1AC02000 | Rm(shift_reg) | Rn(src) | Rd(dst)`
- **Ratio**: 1:1

### SHL reg, imm
- **x86-64**: `SHL EBX, 3` / `SHL EBP, 3` / `SHL EBX, 5` (opcode `C1 E3 imm8`)
- **ARM64**: `LSL Wd, Wn, #imm` (alias for `UBFM Wd, Wn, #(-imm mod 32), #(31-imm)`)
- **Encoding**: `0x53000000 | IMMR((-imm) & 0x1F) | IMMS(31-imm) | Rn(src) | Rd(dst)`
- **Ratio**: 1:1

### SHL reg64, imm (64-bit)
- **x86-64**: `SHL RAX, 8` (REX.W + `C1 E0 08`)
- **ARM64**: `LSL Xd, Xn, #imm`
- **Encoding**: `0xD3400000 | IMMR((-imm) & 0x3F) | IMMS(63-imm) | Rn(src) | Rd(dst)`
- **Ratio**: 1:1

### SHR reg, CL (variable logical right shift)
- **x86-64**: `SHR EAX, CL` / `SHR EBX, CL` (opcode `D3 E8-EF`)
- **ARM64**: `LSR Wd, Wn, Wshift`
- **Encoding**: `0x1AC02400 | Rm(shift_reg) | Rn(src) | Rd(dst)`
- **Ratio**: 1:1

### SHR reg, imm
- **x86-64**: `SHR EAX, 24` / `SHR EBX, 8` / `SHR EDX, 16` (opcode `C1 E8 imm8`)
- **ARM64**: `LSR Wd, Wn, #imm` (alias for `UBFM Wd, Wn, #imm, #31`)
- **Encoding**: `0x53000000 | IMMR(imm) | IMMS(31) | Rn(src) | Rd(dst)` (note: IMMS=0x1F for 32-bit)
- **Ratio**: 1:1

### SHR reg64, imm (64-bit)
- **x86-64**: `SHR RAX, 28` / `SHR RCX, 28` (REX.W + `C1 E8 1C`)
- **ARM64**: `LSR Xd, Xn, #imm`
- **Encoding**: `0xD340FC00 | IMMR(imm) | Rn(src) | Rd(dst)` (IMMS=0x3F for 64-bit)
- **Ratio**: 1:1

### SAR reg, CL (variable arithmetic right shift)
- **x86-64**: `SAR EAX, CL` / `SAR EBX, CL` (opcode `D3 F8-FF`)
- **ARM64**: `ASR Wd, Wn, Wshift`
- **Encoding**: `0x1AC02800 | Rm(shift_reg) | Rn(src) | Rd(dst)`
- **Ratio**: 1:1

### SAR reg, imm
- **x86-64**: `SAR EAX, 12` / `SAR ECX, 12` (opcode `C1 F8 0C`)
- **ARM64**: `ASR Wd, Wn, #imm` (alias for `SBFM Wd, Wn, #imm, #31`)
- **Encoding**: `0x13007C00 | IMMR(imm) | Rn(src) | Rd(dst)` (IMMS=0x1F for 32-bit)
- **Ratio**: 1:1

### SAR reg64, imm (64-bit)
- **x86-64**: `SAR RBX, 14` / `SAR RCX, 14` / `SAR RBX, 30` (REX.W + `C1 FB 0E`)
- **ARM64**: `ASR Xd, Xn, #imm`
- **Encoding**: `0x9340FC00 | IMMR(imm) | Rn(src) | Rd(dst)` (IMMS=0x3F for 64-bit)
- **Ratio**: 1:1

### ROR mem32, 1 (rotate right by 1)
- **x86-64**: `ROR state->stipple[EDI], 1` (opcode `D1 8F disp32`) — rotating stipple pattern
- **ARM64**: `LDR Wtmp, [Xn, #off]` + `ROR Wtmp, Wtmp, #1` + `STR Wtmp, [Xn, #off]`
- **ARM64 ROR imm**: `EXTR Wd, Wn, Wn, #1`
- **Encoding EXTR**: `0x13800000 | IMMR(1) | Rm(src) | Rn(src) | Rd(dst)` (Rm==Rn for ROR)
- **Ratio**: 1:3 (load + rotate + store)

---

## 6. Bit Scan / Count Leading Zeros

### BSR reg, reg (Bit Scan Reverse — find highest set bit)
- **x86-64**: `BSR EDX, EAX` / `BSR EAX, EDX` (opcode `0F BD xx`) — returns bit index of highest set bit
- **ARM64**: `CLZ Wd, Ws` then `SUB Wd, #31, Wd_clz` (or `MOV Wd, #31` + `SUB Wd, Wd, Wclz`)
- **Rationale**: BSR returns the bit position of the highest set bit (0-31). ARM64 CLZ returns the number of leading zeros (0-32). The relationship is: `BSR(x) = 31 - CLZ(x)` (for x != 0).
- **Encoding CLZ**: `0x5AC01000 | Rn(src) | Rd(dst)`
- **Ratio**: 1:2 or 1:3

### BSR reg64, reg64 (64-bit)
- **x86-64**: `BSR RDX, RAX` (REX.W + `0F BD D0`) — used in LOD calculation and W-depth
- **ARM64**: `CLZ Xd, Xs` then `SUB Wd, #63, Wd_clz` (or `MOV Wd, #63` + `SUB Wd, Wd, Wclz`)
- **Encoding CLZ(64)**: `0xDAC01000 | Rn(src) | Rd(dst)`
- **Ratio**: 1:2 or 1:3

---

## 7. Compare and Conditional Move

### CMP reg, reg
- **x86-64**: `CMP EAX, EBX` / `CMP EDX, ECX` (opcode `39 xx`)
- **ARM64**: `CMP Wn, Wm` (alias for `SUBS WZR, Wn, Wm`)
- **Encoding**: `0x6B000000 | Rm(src2) | Rn(src1) | Rd(WZR=31)`
- **Ratio**: 1:1

### CMP reg, [base+disp] (compare with memory)
- **x86-64**: `CMP EAX, state->lod_min` / `CMP EAX, state->x2[EDI]` (opcode `3B 87 disp32`)
- **ARM64**: `LDR Wtmp, [Xn, #off]` + `CMP Wn, Wtmp`
- **Ratio**: 1:2
- **Note**: Very common pattern — used for LOD clamping, texture coordinate clamping, and loop termination.

### CMP reg, imm
- **x86-64**: `CMP EAX, 0xff` / `CMP EDX, 0xff` (opcode `3D imm32` / `81 FA imm32`)
- **ARM64 (imm fits 12 bits)**: `CMP Wn, #imm12` (alias for `SUBS WZR, Wn, #imm12`)
- **ARM64 (imm > 12 bits)**: `MOV Wtmp, #imm` + `CMP Wn, Wtmp`
- **Encoding**: `0x71000000 | (imm12 << 10) | Rn(src) | Rd(WZR=31)`
- **Ratio**: 1:1 or 1:2

### CMP mem, imm
- **x86-64**: `CMP state->tmu_w[RDI], 0` (opcode `48 83 BF disp32 00`)
- **ARM64**: `LDR Xtmp, [Xn, #off]` + `CMP Xtmp, #0` (or `CBZ`/`CBNZ` if followed by branch)
- **Ratio**: 1:2 (or 1:1 using CBZ/CBNZ pattern)

### CMOVcc reg, reg/mem (conditional move)
- **x86-64**: `CMOVS EAX, ECX` / `CMOVA EAX, EBX` / `CMOVL EAX, [mem]` / `CMOVAE EAX, EBP` — used extensively for clamping
- **ARM64**: `CSEL Wd, Wn, Wm, cond`
- **Mapping of x86 condition codes to ARM64 conditions**:

| x86-64 CMOVcc | x86 Condition | ARM64 CSEL cond | ARM64 Cond Code |
|---------------|---------------|-----------------|-----------------|
| CMOVS         | SF=1 (negative) | MI            | 0x4             |
| CMOVNS        | SF=0           | PL             | 0x5             |
| CMOVL / CMOVNGE | SF≠OF       | LT             | 0xB             |
| CMOVNL / CMOVGE | SF=OF       | GE             | 0xA             |
| CMOVA / CMOVNBE | CF=0, ZF=0  | HI             | 0x8             |
| CMOVAE / CMOVNB | CF=0        | CS (HS)        | 0x2             |
| CMOVB / CMOVNAE | CF=1        | CC (LO)        | 0x3             |
| CMOVBE / CMOVNA | CF=1 or ZF=1| LS             | 0x9             |
| CMOVE / CMOVZ  | ZF=1         | EQ             | 0x0             |
| CMOVNE / CMOVNZ | ZF=0        | NE             | 0x1             |

- **Encoding CSEL**: `0x1A800000 | CSEL_COND(cond) | Rm(false_val) | Rn(true_val) | Rd(dst)`
- **Note**: CSEL selects Rn if condition TRUE, Rm if condition FALSE. For `CMOVS EAX, ECX`: the x86 semantics are "if SF=1, then EAX := ECX; else EAX unchanged". This becomes `CSEL Wd, Wsrc, Wd_orig, MI` — select Wsrc when MI (negative), keep Wd_orig otherwise.
- **Note**: When CMOVcc loads from memory on x86-64 (e.g., `CMOVL EAX, state->lod_min`), ARM64 must unconditionally load, then CSEL: `LDR Wtmp, [Xn, #off]` + `CSEL Wd, Wtmp, Wd, cond`
- **Ratio**: 1:1 (reg-reg) or 1:2 (mem source)

---

## 8. Branches and Control Flow

### Jcc rel8 / Jcc rel32 (conditional branch)
- **x86-64**: `JZ +N` / `JNZ +N` / `JAE skip` / `JA skip` / etc.
- **ARM64**: `B.cond label`
- **Encoding**: `0x54000000 | OFFSET19(off) | cond`

| x86-64 Jcc | Condition | ARM64 B.cond | cond |
|------------|-----------|--------------|------|
| JZ / JE    | ZF=1      | B.EQ         | 0x0  |
| JNZ / JNE  | ZF=0      | B.NE         | 0x1  |
| JB / JC / JNAE | CF=1 | B.CC (LO)    | 0x3  |
| JAE / JNC / JNB | CF=0 | B.CS (HS)   | 0x2  |
| JA / JNBE  | CF=0,ZF=0 | B.HI        | 0x8  |
| JBE / JNA  | CF=1 or ZF=1 | B.LS     | 0x9  |
| JS         | SF=1      | B.MI         | 0x4  |
| JNS        | SF=0      | B.PL         | 0x5  |
| JL / JNGE  | SF≠OF     | B.LT         | 0xB  |
| JGE / JNL  | SF=OF     | B.GE         | 0xA  |

- **Ratio**: 1:1
- **Note on offsets**: x86-64 uses byte offsets from the end of the branch instruction. ARM64 B.cond uses a signed 19-bit offset in units of 4 bytes (±1 MB range). The codegen patches forward jumps by writing the offset after the target is known — same pattern works for ARM64, but write a 32-bit instruction word instead of patching a byte or dword offset field.

### JMP rel8 / rel32 (unconditional branch)
- **x86-64**: `JMP +N` (opcode `EB rel8` / `E9 rel32`)
- **ARM64**: `B label`
- **Encoding**: `0x14000000 | OFFSET26(off)`
- **Ratio**: 1:1
- **Note**: B has a 26-bit offset (±128 MB range). More than sufficient for JIT blocks.

### RET
- **x86-64**: `RET` (opcode `C3`)
- **ARM64**: `RET` (returns via X30/LR)
- **Encoding**: `0xD65F03C0`
- **Ratio**: 1:1
- **Note**: ARM64 RET branches to the address in X30 (link register). The prologue must save X30 and the epilogue must restore it before RET.

### Forward-branch patching
- **x86-64 pattern**:
  ```c
  addbyte(0x0F); addbyte(0x84);  // JE rel32
  skip_pos = block_pos;
  addlong(0);                     // placeholder
  // ... code ...
  *(uint32_t *)&code_block[skip_pos] = (block_pos - skip_pos) - 4;
  ```
- **ARM64 pattern**:
  ```c
  skip_pos = block_pos;
  addlong(0x54000000 | COND_EQ);  // B.EQ placeholder
  // ... code ...
  int32_t offset = block_pos - skip_pos;
  *(uint32_t *)&code_block[skip_pos] = 0x54000000 | OFFSET19(offset) | COND_EQ;
  ```
- **Note**: ARM64 offset is from the branch instruction itself (not from end of instruction). The 19-bit field encodes offset/4. The entire instruction word is rewritten, not just an offset field.

### Loop structure
- **x86-64**: `JNZ loop_jump_pos` (backward branch, opcode `0F 85 rel32`)
- **ARM64**: `B.NE loop_jump_pos`
- **Encoding**: `0x54000001 | OFFSET19(loop_jump_pos - block_pos)`
- **Note**: Backward branches naturally have negative offsets. OFFSET19 handles sign extension.

---

## 9. SSE2 / XMM → NEON / V Register Mapping

### Overview

The x86-64 codegen uses SSE2 instructions operating on XMM registers (128-bit). ARM64 uses NEON instructions operating on V registers (128-bit). The data types used in the Voodoo pipeline are primarily:
- **4 × int16** (colors as unpacked 16-bit channels: B, G, R, A)
- **8 × uint8** (packed BGRA pixels)
- **2 × int64** (S/T texture coordinates, W values)
- **4 × int32** (iterated color values ib, ig, ir, ia)

### Register correspondence

| x86-64 XMM | Usage in Voodoo codegen | ARM64 V reg |
|-------------|------------------------|-------------|
| XMM0        | Primary color / result | V0          |
| XMM1        | Secondary color / local color | V1   |
| XMM2        | Zero constant (PXOR XMM2,XMM2) | V2 (or use explicit zero) |
| XMM3        | TMU1 color / fog color / blend factor | V3 |
| XMM4        | Blend multiply factor / dest color | V4 |
| XMM5        | Temp for multiply high-half | V5 |
| XMM6        | Dest color (saved for src_afunc A_COLOR) | V6 |
| XMM7        | TMU0 raw color (saved) | V7 |
| XMM8        | `xmm_01_w` constant {1,1,1,1} | V8 (callee-saved) |
| XMM9        | `xmm_ff_w` constant {0xFF,0xFF,0xFF,0xFF} | V9 (callee-saved) |
| XMM10       | `xmm_ff_b` constant {0xFFFFFF,0,0,0} | V10 (callee-saved) |
| XMM11       | `minus_254` constant {0xFF02,0xFF02,0xFF02,0xFF02} | V11 (callee-saved) |
| XMM15       | `colbfog` (color before fog, for ACOLORBEFOREFOG) | V15 |

---

### SSE2 → NEON Instruction Mapping

#### PXOR XMM, XMM (bitwise XOR / zero register)
- **x86-64**: `PXOR XMM0, XMM0` (zero) / `PXOR XMM0, XMM9` (XOR with constant)
- **NEON (zero)**: `MOVI Vd.4S, #0` or `EOR Vd.16B, Vd.16B, Vd.16B`
- **NEON (XOR)**: `EOR Vd.16B, Vn.16B, Vm.16B`
- **Encoding MOVI(zero)**: `0x4F000400 | Rd(dst)` (op=0, cmode=0000, imm8=0)
- **Encoding EOR**: `0x6E201C00 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Ratio**: 1:1

#### PXOR XMM, [base+index] (XOR with memory operand)
- **x86-64**: `PXOR XMM0, R12(xmm_00_ff_w)[EBX]` — used in trilinear texture combine
- **NEON**: `LDR Qtmp, [Xbase, Xidx, LSL #4]` + `EOR Vd.16B, Vn.16B, Vtmp.16B`
- **Ratio**: 1:2
- **Note**: ARM64 NEON has no memory-operand forms. Always load first, then operate.

#### MOVD XMM, reg32 (move GPR to low 32 bits of XMM, zero upper)
- **x86-64**: `MOVD XMM0, EAX` (opcode `66 0F 6E C0`)
- **NEON**: `FMOV Sd, Wn` (moves W to low 32 bits, zeros upper 96 bits)
- **Encoding**: `0x1E270000 | Rn(gpr) | Rd(vreg)`
- **Ratio**: 1:1

#### MOVD reg32, XMM (move low 32 bits of XMM to GPR)
- **x86-64**: `MOVD EAX, XMM0` (opcode `66 0F 7E C0`)
- **NEON**: `FMOV Wn, Sd` (or `UMOV Wd, Vn.S[0]`)
- **Encoding FMOV**: `0x1E260000 | Rn(vreg) | Rd(gpr)`
- **Ratio**: 1:1

#### MOVQ XMM, XMM (move low 64 bits, zero upper — used as MOV)
- **x86-64**: `MOVQ XMM0, XMM1` (opcode `F3 0F 7E C1`) — effectively copies low 64 bits
- **NEON**: `MOV Vd.16B, Vs.16B` (full copy) or `FMOV Dd, Ds` (64-bit copy)
- **Note**: The x86-64 codegen uses MOVQ as a cheap register copy. On ARM64, use full 128-bit MOV since it's the same cost.
- **Encoding MOV(vector)**: `0x4EA01C00 | Rm(src) | Rn(src) | Rd(dst)` (ORR Vd, Vn, Vm with n=m)
- **Ratio**: 1:1

#### MOVQ XMM, [mem] (load 64 bits from memory)
- **x86-64**: `MOVQ XMM0, [RBX+RAX*4]` (opcode `F3 0F 7E 04 83`) — texture bilinear sample pair load
- **NEON**: `LDR Dd, [Xbase, Xoff]` (loads 64 bits into low half of Vd)
- **Encoding**: `0xFC606800 | Rm(off) | Rn(base) | Rt(vreg)`
- **Ratio**: 1:1

#### MOVDQA XMM, [mem] (aligned 128-bit load)
- **x86-64**: `MOVDQA XMM8, [R15]` (opcode `66 45 0F 6F 07`) — load SIMD constants
- **NEON**: `LDR Qd, [Xbase]` or `LDR Qd, [Xbase, #imm]`
- **Encoding**: `0x3DC00000 | (imm12 << 10) | Rn(base) | Rt(vreg)` (scaled by 16)
- **Ratio**: 1:1

#### MOVDQU XMM, [mem] (unaligned 128-bit load)
- **x86-64**: `MOVDQU XMM1, state->ib[RDI]` (opcode `F3 0F 6F 8F disp32`)
- **NEON**: `LDR Qd, [Xbase, #imm]` (ARM64 LDR is unaligned-safe)
- **Ratio**: 1:1
- **Note**: ARM64 doesn't distinguish aligned/unaligned loads at instruction level. LDR Q always works.

#### MOVDQU [mem], XMM (unaligned 128-bit store)
- **x86-64**: `MOVDQU state->ib[RDI], XMM1` (opcode `F3 0F 7F 8F disp32`)
- **NEON**: `STR Qd, [Xbase, #imm]`
- **Encoding**: `0x3D800000 | (imm12 << 10) | Rn(base) | Rt(vreg)` (scaled by 16)
- **Ratio**: 1:1

#### MOVQ [mem], XMM (store 64 bits)
- **x86-64**: `MOVQ state->tmu0_w, XMM4` (opcode `66 0F D6 A7 disp32`)
- **NEON**: `STR Dd, [Xbase, #imm]`
- **Encoding**: `0xFD000000 | (imm12 << 10) | Rn(base) | Rt(vreg)` (scaled by 8)
- **Ratio**: 1:1

#### PUNPCKLBW XMM, XMM (unpack low bytes to words)
- **x86-64**: `PUNPCKLBW XMM0, XMM2` (opcode `66 0F 60 C2`) — zero-extends each byte to 16-bit
- **When XMM2 is zero**: This is effectively byte-to-halfword zero extension
- **NEON**: `UXTL Vd.8H, Vs.8B` (alias for `USHLL Vd.8H, Vs.8B, #0`)
- **Encoding USHLL**: `0x2F08A400 | Rn(src) | Rd(dst)` (shift=0, size=byte)
- **Alternative**: `ZIP1 Vd.16B, Vs.16B, Vzero.16B` (interleave with zero)
- **Ratio**: 1:1
- **Note**: If the second operand is NOT zero, this is a true interleave. Use `ZIP1 Vd.16B, Vn.16B, Vm.16B`.

#### PUNPCKLDQ XMM, XMM (unpack low dwords)
- **x86-64**: `PUNPCKLDQ XMM0, XMM0` (opcode `66 0F 62 C0`) — duplicate low dword to next dword
- **NEON (self-interleave)**: `ZIP1 Vd.4S, Vn.4S, Vn.4S` or `DUP Vd.2S, Vn.S[0]`
- **Encoding DUP**: `0x0E040400 | Rn(src) | Rd(dst)` (DUP Vd.2S, Vn.S[0])
- **Ratio**: 1:1

#### PUNPCKLWD XMM, XMM (unpack low words to dwords)
- **x86-64**: `PUNPCKLWD XMM0, XMM5` (opcode `66 0F 61 C5`) — interleave low words
- **NEON**: `ZIP1 Vd.4H, Vn.4H, Vm.4H` (interleaves the low 4 halfwords of each source)
- **Encoding**: `0x0E403800 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Ratio**: 1:1
- **Note**: Used in the signed 16×16→32 multiply sequence. See PMULLW/PMULHW combo below.

#### PSHUFLW XMM, XMM, imm8 (shuffle low words)
- **x86-64**: `PSHUFLW XMM0, XMM3, 0xFF` (opcode `F2 0F 70 C3 FF`) — broadcast word[3] to all 4 low words
- **NEON (broadcast word[3])**: `DUP Vd.4H, Vs.H[3]`
- **NEON (broadcast word[0])**: `DUP Vd.4H, Vs.H[0]`
- **NEON (arbitrary shuffle)**: May require TBL or multiple INS instructions
- **Encoding DUP(H[3])**: `0x0E1E0400 | Rn(src) | Rd(dst)` (imm5 encodes lane)
- **Encoding DUP(H[0])**: `0x0E020400 | Rn(src) | Rd(dst)`
- **Note**: The x86-64 codegen only uses two shuffle patterns: `0xFF` (broadcast word[3], the alpha channel) and `0x00` (broadcast word[0]). Both map to NEON DUP.
- **Ratio**: 1:1

#### PSRLDQ XMM, imm (byte-shift right)
- **x86-64**: `PSRLDQ XMM0, 8` (opcode `66 0F 73 D8 08`) — shift right by 8 bytes (moves high 64 bits to low)
- **NEON**: `EXT Vd.16B, Vn.16B, Vzero.16B, #8` (or use specific lane operations)
- **Encoding EXT**: `0x6E004000 | (imm4 << 11) | Rm(zero_or_src) | Rn(src) | Rd(dst)` where imm4=8
- **Ratio**: 1:1
- **Note**: Used in bilinear filtering to add the high and low halves of a 128-bit accumulator.

#### PADDW XMM, XMM (packed add 16-bit words)
- **x86-64**: `PADDW XMM0, XMM1` (opcode `66 0F FD C1`)
- **NEON**: `ADD Vd.8H, Vn.8H, Vm.8H` (or `.4H` for 64-bit operation)
- **Encoding(8H)**: `0x4E608400 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Encoding(4H)**: `0x0E608400 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Ratio**: 1:1
- **Note**: Use `.4H` (64-bit) when only the low 4 words matter (most cases in this codegen).

#### PSUBW XMM, XMM (packed subtract 16-bit words)
- **x86-64**: `PSUBW XMM0, XMM1` / `PSUBW XMM3, XMM0` (opcode `66 0F F9 xx`)
- **NEON**: `SUB Vd.8H, Vn.8H, Vm.8H` (or `.4H`)
- **Encoding(8H)**: `0x6E608400 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Encoding(4H)**: `0x2E608400 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Ratio**: 1:1

#### PADDD XMM, XMM (packed add 32-bit dwords)
- **x86-64**: `PADDD XMM1, XMM0` (opcode `66 0F FE C8`) — used for per-pixel state increment (ib/ig/ir/ia += dBdX/dGdX/dRdX/dAdX)
- **NEON**: `ADD Vd.4S, Vn.4S, Vm.4S`
- **Encoding**: `0x4EA08400 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Ratio**: 1:1

#### PSUBD XMM, XMM (packed subtract 32-bit dwords)
- **x86-64**: `PSUBD XMM1, XMM0` (opcode `66 0F FA C8`) — used when xdir < 0
- **NEON**: `SUB Vd.4S, Vn.4S, Vm.4S`
- **Encoding**: `0x6EA08400 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Ratio**: 1:1

#### PADDQ XMM, XMM (packed add 64-bit qwords)
- **x86-64**: `PADDQ XMM3, XMM5` (opcode `66 0F D4 DD`) — used for tmu_s/tmu_t/tmu_w/w increment
- **NEON**: `ADD Vd.2D, Vn.2D, Vm.2D`
- **Encoding**: `0x4EE08400 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Ratio**: 1:1

#### PSUBQ XMM, XMM (packed subtract 64-bit qwords)
- **x86-64**: `PSUBQ XMM3, XMM5` (opcode `66 0F FB DD`) — used when xdir < 0
- **NEON**: `SUB Vd.2D, Vn.2D, Vm.2D`
- **Encoding**: `0x6EE08400 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Ratio**: 1:1

#### PMULLW XMM, XMM (packed multiply low 16-bit)
- **x86-64**: `PMULLW XMM0, XMM3` (opcode `66 0F D5 C3`) — 16×16→16 (low half of result)
- **NEON**: `MUL Vd.8H, Vn.8H, Vm.8H` (or `.4H`)
- **Encoding(4H)**: `0x0E609C00 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Encoding(8H)**: `0x4E609C00 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Ratio**: 1:1

#### PMULHW XMM, XMM (packed multiply high 16-bit, signed)
- **x86-64**: `PMULHW XMM5, XMM3` (opcode `66 0F E5 EB`)
- **NEON**: No direct equivalent. Must use widening multiply then extract high halves.
- **Expansion**: Use `SMULL Vtmp.4S, Vn.4H, Vm.4H` then `SHRN Vd.4H, Vtmp.4S, #16`
- **Ratio**: 1:2
- **Note**: However, the x86-64 codegen always uses PMULLW + PMULHW together, followed by PUNPCKLWD + PSRAD + PACKSSDW to compute a signed 16×16→32→16 multiply-shift. On ARM64, this entire 5-instruction x86 sequence can be replaced by: `SMULL Vd.4S, Vn.4H, Vm.4H` (signed widening multiply to 32-bit) + `SSHR Vd.4S, Vd.4S, #8` (arithmetic right shift) + `SQXTN Vd.4H, Vd.4S` (signed saturating narrow). This is 3 ARM64 instructions vs 5 x86-64 instructions — a net win.

#### PMULLW + PMULHW + PUNPCKLWD + PSRAD + PACKSSDW combo (signed 16×16→32 multiply then shift and repack)
- **x86-64 sequence** (color combine multiply, ~5 instructions):
  ```
  PMULLW XMM1, XMM4    ; low 16 bits of 16×16 products
  PMULHW XMM5, XMM4    ; high 16 bits of 16×16 products
  PUNPCKLWD XMM1, XMM5  ; interleave to form 32-bit products
  PSRAD XMM1, 8         ; arithmetic right shift by 8
  PACKSSDW XMM1, XMM1   ; pack back to 16-bit with saturation
  ```
- **ARM64 equivalent** (3 instructions):
  ```
  SMULL Vd.4S, Vn.4H, Vm.4H   ; signed widening multiply: 4×(16×16→32)
  SSHR Vd.4S, Vd.4S, #8        ; arithmetic right shift each 32-bit lane by 8
  SQXTN Vd.4H, Vd.4S            ; signed saturating narrow 4×32→4×16
  ```
- **Encoding SMULL**: `0x0E60C000 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Encoding SSHR(4S,#8)**: `0x0F200400 | ((32-8)<<16) | Rn(src) | Rd(dst)` → `0x0F180400`
- **Encoding SQXTN**: `0x0E614800 | Rn(src) | Rd(dst)` (4H from 4S)
- **Ratio**: 5:3 (ARM64 wins)

#### PSRLW XMM, imm (packed shift right logical, words)
- **x86-64**: `PSRLW XMM0, 8` (opcode `66 0F 71 D0 08`) — used after multiply for fixed-point normalization
- **NEON**: `USHR Vd.8H, Vn.8H, #imm` (or `.4H`)
- **Encoding(4H)**: `0x2F100400 | ((16-imm)<<16) | Rn(src) | Rd(dst)` — note: encoded as `16+shift` for right shifts
- **Correct encoding**: `0x2F000400 | SHIFT_IMM_V4H(16-imm) | Rn(src) | Rd(dst)`
- **Ratio**: 1:1

#### PSRAW XMM, imm (packed shift right arithmetic, words)
- **x86-64**: `PSRAW XMM3, 1` / `PSRAW XMM3, 7` (opcode `66 0F 71 E3 01`)
- **NEON**: `SSHR Vd.8H, Vn.8H, #imm` (or `.4H`)
- **Encoding(4H)**: `0x0F100400 | ((16-imm)<<16) | Rn(src) | Rd(dst)`
- **Ratio**: 1:1

#### PSRAD XMM, imm (packed shift right arithmetic, dwords)
- **x86-64**: `PSRAD XMM0, 12` / `PSRAD XMM1, 8` (opcode `66 0F 72 E0 0C`)
- **NEON**: `SSHR Vd.4S, Vn.4S, #imm`
- **Encoding**: `0x0F200400 | ((32-imm)<<16) | Rn(src) | Rd(dst)`
- **Ratio**: 1:1

#### PSRLD XMM, imm (packed shift right logical, dwords)
- **x86-64**: `PSRLD XMM0, 8` (opcode `66 0F 72 E0 08`) — note: differs from PSRAD in being unsigned
- **NEON**: `USHR Vd.4S, Vn.4S, #imm`
- **Encoding**: `0x2F200400 | ((32-imm)<<16) | Rn(src) | Rd(dst)`
- **Ratio**: 1:1

#### PACKUSWB XMM, XMM (pack with unsigned saturation, words→bytes)
- **x86-64**: `PACKUSWB XMM0, XMM0` (opcode `66 0F 67 C0`) — clamp 16-bit to [0,255] and pack to bytes
- **NEON**: `SQXTUN Vd.8B, Vn.8H` (signed saturating extract unsigned narrow)
- **Encoding**: `0x2E212800 | Rn(src) | Rd(dst)`
- **Ratio**: 1:1
- **Note**: SQXTUN treats input as signed and clamps to unsigned range [0,255], which matches PACKUSWB semantics exactly.

#### PACKSSDW XMM, XMM (pack with signed saturation, dwords→words)
- **x86-64**: `PACKSSDW XMM0, XMM0` / `PACKSSDW XMM1, XMM1` (opcode `66 0F 6B C0`)
- **NEON**: `SQXTN Vd.4H, Vn.4S` (signed saturating extract narrow)
- **Encoding**: `0x0E614800 | Rn(src) | Rd(dst)`
- **Ratio**: 1:1

#### PADDUSB XMM, XMM (packed add unsigned saturating, bytes)
- **x86-64**: `PADDUSB XMM0, XMM3` (opcode `66 0F DC C3`) — used in FOG_CONSTANT mode
- **NEON**: `UQADD Vd.16B, Vn.16B, Vm.16B`
- **Encoding**: `0x6E200C00 | Rm(src2) | Rn(src1) | Rd(dst)`
- **Ratio**: 1:1

#### PINSRW XMM, reg/mem, imm (insert word into XMM)
- **x86-64**: `PINSRW XMM0, [RBX], 2` / `PINSRW XMM3, ECX, 3` (opcode `66 0F C4 xx imm8`)
- **NEON**: `INS Vd.H[lane], Wn` (from GPR) or `LD1 {Vd.H}[lane], [Xaddr]` (from memory)
- **Encoding INS(from GPR)**: `0x4E020400 | (lane<<1<<16) | Rn(gpr) | Rd(vreg)`
- **Ratio**: 1:1 (from GPR) or 1:1 (from memory via LD1)
- **Note**: Lane indices: H[0]=0x02, H[1]=0x06, H[2]=0x0A, H[3]=0x0E in the imm5 field.

#### PMULLW XMM, [mem] (multiply from memory operand)
- **x86-64**: `PMULLW XMM0, [RSI]` / `PMULLW XMM0, bilinear_lookup[ESI]` — bilinear filter weight multiply
- **ARM64**: `LDR Qtmp, [Xaddr]` + `MUL Vd.4H, Vn.4H, Vtmp.4H` (or `.8H`)
- **Ratio**: 1:2
- **Note**: ARM64 NEON doesn't have memory-operand forms for arithmetic. Always load first.

#### PMULLW XMM, [base+index*8] (indexed memory multiply)
- **x86-64**: `PMULLW XMM4, R10(alookup)[EDX*8]` / `PMULLW XMM0, R11(aminuslookup)[EDX*8]`
- **ARM64**: `ADD Xtmp, Xbase, Xidx, LSL #3` + `LDR Qtmp, [Xtmp]` + `MUL Vd.4H, Vn.4H, Vtmp.4H`
- **Ratio**: 1:3
- **Optimization**: If the index is known at codegen time, compute the address and use `LDR Qtmp, [Xbase, #off]`.

---

## 10. Bilinear Texture Filtering — Full Sequence

The bilinear filter is the most complex SSE2 sequence. Here's the full x86-64 → ARM64 mapping:

### x86-64 sequence (simplified):
```
MOVQ XMM0, [RBX+RAX*4]      ; load 2 texels (row 0, adjacent S)
MOVQ XMM1, [RDX+RAX*4]      ; load 2 texels (row 1, adjacent S)
PUNPCKLBW XMM0, XMM2         ; unpack to 16-bit (XMM2=zero)
PUNPCKLBW XMM1, XMM2         ; unpack to 16-bit
PMULLW XMM0, [bilinear+0]    ; multiply by weights d[0],d[1]
PMULLW XMM1, [bilinear+16]   ; multiply by weights d[2],d[3]
PADDW XMM0, XMM1             ; add weighted samples
MOVQ XMM1, XMM0              ; copy
PSRLDQ XMM0, 8               ; shift to get high pair
PADDW XMM0, XMM1             ; add high+low pairs
PSRLW XMM0, 8                ; normalize (divide by 256)
PACKUSWB XMM0, XMM0          ; pack to bytes
```

### ARM64 equivalent:
```
LDR D0, [Xrow0, Xs, LSL #2]   ; load 2 texels row 0
LDR D1, [Xrow1, Xs, LSL #2]   ; load 2 texels row 1
UXTL V0.8H, V0.8B              ; unpack to 16-bit
UXTL V1.8H, V1.8B              ; unpack to 16-bit
LDR Q16, [Xbilinear]           ; load weights d[0],d[1]
LDR Q17, [Xbilinear, #16]      ; load weights d[2],d[3]
MUL V0.8H, V0.8H, V16.8H      ; multiply row 0 by weights
MUL V1.8H, V1.8H, V17.8H      ; multiply row 1 by weights
ADD V0.8H, V0.8H, V1.8H       ; sum rows
ADDP V0.4S, V0.4S, V0.4S      ; horizontal pairwise add (or EXT+ADD approach)
USHR V0.4H, V0.4H, #8         ; normalize
SQXTUN V0.8B, V0.8H            ; pack to bytes
```

**Alternative using ADDP**: Use `ADDP V0.8H, V0.8H, V0.8H` to horizontally add pairs of 16-bit values, collapsing 8 values to 4. This replaces the PSRLDQ+PADDW pattern.

---

## 11. Alpha Blending — Multiply+Round Pattern

The alpha blend uses a repeating pattern for approximate division by 255:

### x86-64 pattern (appears ~20 times):
```
PMULLW XMM4, factor            ; multiply color × alpha
MOVQ XMM5, XMM4               ; copy
PADDW XMM4, alookup[1*8]      ; add rounding term (value=1)
PSRLW XMM5, 8                 ; high byte of product
PADDW XMM4, XMM5              ; add high byte (approximates +product/256)
PSRLW XMM4, 8                 ; final shift (result ≈ product/255)
```

### ARM64 equivalent:
```
MUL V4.4H, V4.4H, Vfactor.4H    ; multiply
MOV V5.16B, V4.16B               ; copy
ADD V4.4H, V4.4H, V_one.4H      ; add rounding term
USHR V5.4H, V5.4H, #8           ; high byte
ADD V4.4H, V4.4H, V5.4H         ; add correction
USHR V4.4H, V4.4H, #8           ; final normalize
```

- **Ratio**: 6:6 (same count, direct mapping)
- **Optimization opportunity**: ARM64 has `URSHR` (unsigned rounding shift right) and `URSRA` (unsigned rounding shift right and accumulate). Could simplify: `MUL` + `URSHR Vtmp, Vd, #8` + `ADD Vd, Vd, Vtmp` + `USHR Vd, Vd, #8`. Same count but might schedule better.

---

## 12. Summary — Instruction Count Impact

| Category | x86-64 Instructions | ARM64 Instructions | Notes |
|----------|--------------------|--------------------|-------|
| MOV reg,reg | 1 | 1 | Direct |
| MOV reg,imm32 | 1 | 1-2 | MOVZ+MOVK if >16 bits |
| MOV reg,imm64 | 1 | 2-4 | Use ADRP+ADD for pointers |
| MOV reg,[mem] | 1 | 1 | Direct (if offset fits) |
| MOV [mem],reg | 1 | 1 | Direct |
| ADD/SUB [mem],reg | 1 | 3 | Load-store arch penalty |
| ADD/SUB [mem],imm | 1 | 3 | Load-store arch penalty |
| LEA | 1 | 1-2 | ADD with shift |
| PUSH/POP (×8) | 8 | 4 | STP/LDP pair advantage |
| CMP + CMOVcc | 2 | 2 | CMP + CSEL |
| BSR | 1 | 2-3 | CLZ + SUB |
| IDIV | 1 | 1 | SDIV (simpler — no RDX:RAX) |
| TEST + Jcc (bit test) | 2 | 1 | TBZ/TBNZ |
| SSE MOV/load | 1 | 1 | Direct |
| PMULLW+PMULHW+unpack+shift+pack | 5 | 3 | SMULL+SSHR+SQXTN (ARM wins) |
| PACKUSWB | 1 | 1 | SQXTUN |
| Bilinear filter (full) | ~12 | ~12 | Roughly equal |
| Alpha blend multiply | 6 | 6 | Direct mapping |

### Net assessment

The ARM64 code will be **roughly the same total instruction count** as x86-64. Some areas are slightly worse (memory-destination ops become 3 instructions; loading 64-bit constants needs more instructions) but others are significantly better (the signed multiply combo is 3 vs 5 instructions; TBZ/TBNZ combines test+branch; CSEL is more flexible than CMOVcc). The much larger register file (31 GPR + 32 SIMD vs 16+16) means fewer spills and more constants can be pinned.

---

## 13. Special Considerations

### W^X (Write XOR Execute) on macOS
After writing ARM64 instructions to the code block, must call:
1. `pthread_jit_write_protect_np(1)` — make executable, non-writable
2. `sys_icache_invalidate(code_block, block_size)` — flush I-cache

Before writing:
1. `pthread_jit_write_protect_np(0)` — make writable, non-executable

### I-cache coherency
ARM64 has a non-coherent I-cache. After modifying code, MUST issue `sys_icache_invalidate()` (macOS) or `__builtin___clear_cache()` (Linux). Without this, the CPU may execute stale instructions.

### Alignment
ARM64 instructions must be 4-byte aligned. The `addlong()` macro naturally ensures this since every instruction is exactly 4 bytes. The code block buffer should be 4-byte aligned (guaranteed by mmap).

### Condition flags
x86-64 sets flags on most ALU operations implicitly. ARM64 only sets flags on explicit flag-setting variants (ADDS, SUBS, ANDS, CMP, CMN, TST). When porting sequences that rely on flags from a prior ALU op (e.g., `SUB EAX, EBP` followed by `JS`), either:
1. Use the flag-setting variant (`SUBS`) if the result is still needed
2. Insert an explicit `CMP` or `TST` before the branch
3. Use `CBNZ`/`CBZ`/`TBZ`/`TBNZ` when testing for zero/non-zero/specific bit

### Sub-register operations (8-bit / 16-bit partial register access)
x86-64 allows direct manipulation of 8-bit sub-registers (AL, AH, BL, CL, DL) and 16-bit sub-registers (AX, BX). ARM64 has no sub-register access — all GPR operations are on full 32-bit (Wn) or 64-bit (Xn) registers.

**Patterns in the codegen and their ARM64 equivalents:**

| x86-64 | Purpose | ARM64 |
|--------|---------|-------|
| `MOV CL, DL` | Copy low byte | `AND Wcl, Wdl, #0xFF` (or just use full register if upper bits don't matter) |
| `MOV CL, AL` | Copy low byte | `AND Wcl, Wal, #0xFF` |
| `SUB DL, CL` | 8-bit subtract | `SUB Wdl, Wdl, Wcl` (then `AND Wdl, Wdl, #0xFF` if needed) |
| `ADD CL, 4` | 8-bit add | `ADD Wcl, Wcl, #4` |
| `MOV DL, 8` | Load 8-bit imm | `MOV Wdl, #8` |
| `MOVZX EBX, AH` | Extract bits [15:8] | `UBFX Wbx, Wax, #8, #8` |
| `MOVZX ECX, AL` | Extract bits [7:0] | `UXTB Wcx, Wax` (or `AND Wcx, Wax, #0xFF`) |
| `XOR AL, 0xFF` | Complement low byte | `EOR Wal, Wal, #0xFF` (then mask if needed) |
| `MOV [RSI+EDX*2], AX` | Store 16-bit | `STRH Wax, [Xsi, Xdx, LSL #1]` |
| `MUL byte [mem]` | AL × byte → AX | `LDRB Wtmp, [mem]` + `MUL Wax, Wax, Wtmp` (use AND #0xFF to isolate AL first) |

**Key insight**: Since ARM64 always operates on full 32-bit registers, we can often skip the masking when the upper bits will be discarded anyway (e.g., the value is about to be shifted or masked by subsequent instructions). Analyze the data flow to determine when masking is truly required.

### Addressing modes
x86-64's complex addressing modes (base+index*scale+disp) require decomposition on ARM64. ARM64 supports:
- `[Xn, #imm12]` — scaled unsigned offset
- `[Xn, Xm]` — register offset
- `[Xn, Xm, LSL #shift]` — register offset with shift (shift matches access size)
- `[Xn, Wm, SXTW]` / `[Xn, Wm, UXTW]` — 32-bit index extended to 64-bit

For `[base + index*scale + disp]`, typically: `ADD Xtmp, Xbase, Xidx, LSL #log2(scale)` then `LDR Wd, [Xtmp, #disp]`.

---

## 14. Upstream Bugs Found During Audit

### Bug: `0x8E` instead of `0x83` at line 1303

**File**: `vid_voodoo_codegen_x86-64.h`, line 1303
**Comment says**: `ADD EAX, 1`
**Bytes emitted**: `0x8E 0xC0 0x01`
**Decodes as**: `MOV ES, AX` (move to segment register) — **wrong instruction**
**Should be**: `0x83 0xC0 0x01` = `ADD EAX, 1`

**Context**: Dual-TMU texture combine path (`tc_add_clocal_1` / `tc_add_alocal_1`). Only triggers when both TMUs are active with specific blend modes, which is likely why it hasn't been caught — the code path is infrequently hit.

**Impact on x86-64**: Corrupts instruction stream. `0x01` gets parsed as the start of the next instruction (`ADD`), mis-decoding everything that follows. Would crash or produce garbage rendering when this TMU blend config is used.

**Action for ARM64 port**: Emit the correct `ADD Wd, Wd, #1` instruction. Do NOT replicate this bug. Fix upstream separately.
