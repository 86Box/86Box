# Plan: ARM64 CPU Emulation Performance Optimizations

## What This Is

86Box's CPU emulation core (interpreter loop, memory TLB, instruction fetch, JIT block dispatch) was written for x86-64 hosts and lacks ARM64-specific tuning. The NEW_DYNAREC ARM64 JIT backend (`codegen_new/codegen_backend_arm64*.c`) is functional but conservative — it doesn't exploit ARM64 instruction features that would improve generated code quality.

This plan covers three scopes:
1. **C-level**: Branch hints, prefetch, branchless patterns in the interpreter/dispatch hot paths
2. **JIT backend**: Better ARM64 code generation in the NEW_DYNAREC ARM64 backend
3. **JIT call/pointer optimization**: PC-relative addressing to reduce call and pointer load instruction counts

All changes are guarded for ARM64 with `#if defined(__aarch64__) || defined(_M_ARM64)`.

## Progress Summary

| Phase | Status | Description |
|-------|--------|-------------|
| Phase 1 | **DONE** | C-level interpreter/dispatch hot-path optimizations |
| Phase 2 | **DONE** | JIT backend code quality (emitters, ADRP+ADD, cached TLB regs) |
| Phase 3 | **DONE** | JIT call and pointer load optimization (BL-relative, ADRP+ADD+BLR) |
| Phase 4 | TODO | 3DNow FRECPE/FRSQRTE + PFRSQRT bug fix |
| Phase 5 | TODO | Benchmarking |

## Guards

All changes guarded with `#if defined(__aarch64__) || defined(_M_ARM64)`:

- `LIKELY`/`UNLIKELY` hints are portable and harmless on all architectures, so they don't strictly need guards — but we guard them anyway for consistency and to keep the diff clean
- `__builtin_prefetch` → ARM64-guarded
- Branchless validation → ARM64-guarded
- Opt 6 (`__builtin_available` removal) → macOS+ARM64 only (`__APPLE__ && __aarch64__`), since it only affects existing macOS-specific W^X code
- JIT backend changes → inherently ARM64-only files, no additional guards needed

All optimizations target ARMv8 baseline — no Apple Silicon-specific features used. Testing happens on macOS but the code is portable to Linux/Windows ARM64.

## Existing Infrastructure

- `LIKELY()`/`UNLIKELY()` macros already in `src/include/86box/86box.h` (lines 94-95) — portable, just rarely used in CPU/mem paths
- ARM64 JIT backend files are already gated by `#if defined __aarch64__ || defined _M_ARM64` at the build level
- No test suite — verification is manual VM boot + test workloads

---

## Phase 1: C-Level Interpreter/Dispatch Optimizations — DONE

Apply branch hints, prefetch, and branchless patterns to the two CPU hot-path files. All changes wrapped in `#if defined(__aarch64__) || defined(_M_ARM64)`. Low risk, high confidence, easy to verify.

**Files modified**: 2 (`386_common.h`, `386_dynarec.c`)

| # | Optimization | File | Risk | Status |
|---|-------------|------|------|--------|
| 1 | Branch hints on instruction fetch | `src/cpu/386_common.h` | Very Low | **DONE** |
| 2 | Branch hints on memory read/write macros | `src/cpu/386_common.h` | Very Low | **DONE** |
| 3 | Branch hints on interpreter execution loop | `src/cpu/386_dynarec.c` | Very Low | **DONE** |
| 4 | Software prefetch for block dispatch | `src/cpu/386_dynarec.c` | Very Low | **DONE** |
| 5 | Branchless block validation | `src/cpu/386_dynarec.c` | Low | **DONE** |
| 6 | Remove redundant `__builtin_available` checks | `src/cpu/386_dynarec.c` | Very Low | **DONE** |
| 7 | TLB prefetch during EA calculation | `src/cpu/386_dynarec.c` | Very Low | **DONE** |

---

## Phase 2: JIT Backend Code Quality — DONE

Improve generated ARM64 code in the NEW_DYNAREC backend. Changes are in ARM64-only files so no additional guards needed.

**Files modified**: 5 (`codegen_backend_arm64.c`, `_uops.c`, `_ops.c`, `_ops.h`, `_defs.h`)

| # | Optimization | Status | Notes |
|---|-------------|--------|-------|
| 2A | Sub-register BFI reduction | **SKIPPED** | Audited — BFI already used optimally, no patterns found |
| 2B | New instruction emitters (CSEL, ADDS/SUBS, CLZ, UDIV/SDIV, MADD, etc.) | **DONE** | 18 new emitters added |
| 2C | Fused shift-ALU operations | **SKIPPED** | Already complete in existing codebase |
| 2D | ADRP+ADD immediate loading | **DONE** | `host_arm64_ADRP_ADD()` with MOVX_IMM fallback |
| 2E | Cached lookup table registers (X27=readlookup2, X28=writelookup2) | **DONE** | Reduces HOST_REGS 10→8, eliminates TLB pointer loads |

---

## Phase 3: JIT Call and Pointer Load Optimization — TODO

Reduce JIT code size and improve execution speed by optimizing function calls and pointer immediate loads in the ARM64 backend. The JIT currently emits up to 5 instructions per function call and up to 4 instructions per pointer load — both can be significantly reduced using ARM64 PC-relative addressing.

**Key insight**: The JIT code pool is a single contiguous ~120MB mmap (`MEM_BLOCK_NR=131072 × MEM_BLOCK_SIZE=0x3c0`). All stubs and generated blocks live in this region, guaranteeing BL (±128MB) works for intra-pool calls. For external C functions, ADRP+ADD (±4GB) works reliably.

**Files modified**: 3 (`codegen_backend_arm64_ops.c`, `_ops.h`, `_uops.c`)

### Problem: Expensive Function Calls in JIT Code

Every function call emitted by the JIT uses `host_arm64_call()` (`codegen_backend_arm64_ops.c:1654`), which generates:

```armasm
; MOVX_IMM — up to 4 instructions for a 64-bit address
MOVZ X16, #imm16_0
MOVK X16, #imm16_1, LSL #16
MOVK X16, #imm16_2, LSL #32
MOVK X16, #imm16_3, LSL #48
; BLR — indirect call
BLR X16
```

That's up to **5 instructions per call**. There are 32 call sites in `codegen_backend_arm64_uops.c` alone, plus 9 in `codegen_backend_arm64.c`.

The hottest call sites are memory load/store stubs (`codegen_mem_load_byte/word/long/quad`, `codegen_mem_store_*`), called from every `MEM_LOAD_ABS`, `MEM_LOAD_REG`, `MEM_STORE_ABS`, `MEM_STORE_REG` uop — the most common operations in JIT-compiled code.

### Opt 3A: BL-Relative Calls for JIT Stubs

**Targets**: Calls to JIT-resident stubs built during `codegen_backend_init()`:
- `codegen_mem_load_byte/word/long/quad/single/double` (6 stubs)
- `codegen_mem_store_byte/word/long/quad/single/double` (6 stubs)
- `codegen_fp_round`, `codegen_fp_round_quad` (2 stubs)
- `codegen_gpf_rout`, `codegen_exit_rout` (2 stubs — called via B/CBNZ, not BL)

**Current**: `MOVX_IMM + BLR` = 2-5 instructions

**Proposed**: `BL offset26` = **1 instruction**

Since all JIT code lives in the same contiguous ~120MB mmap, the stub addresses are always within BL's ±128MB range. No fallback needed.

**Implementation**:
1. Add `host_arm64_call_rel(block, dest)` in `codegen_backend_arm64_ops.c`:
   - Compute offset from current emission point to `dest`
   - Assert offset is within ±128MB (guaranteed by pool layout)
   - Emit `BL offset26`
2. Replace `host_arm64_call(block, codegen_mem_load_*)` with `host_arm64_call_rel()` in uop handlers
3. Similarly replace `host_arm64_call(block, codegen_mem_store_*)` and `codegen_fp_round*`

**Savings**: 1-4 instructions per memory load/store (the hottest JIT path). With ~30 call sites targeting stubs, this reduces JIT code size significantly.

**Risk**: Very Low. The ~120MB pool guarantee means BL range is never exceeded. If it ever were (pool size increase), the assert would catch it during development.

### Opt 3B: ADRP+ADD+BLR for External C Function Calls

**Targets**: Calls to C functions outside the JIT pool:
- `readmembl/wl/ll/ql` — TLB miss slow path (called from stubs, not uop handlers)
- `writemembl/wl/ll/ql` — TLB miss slow path
- `loadseg` — segment register load
- `x86_int` — interrupt dispatch
- `x86gpf` — general protection fault

**Current**: `MOVX_IMM + BLR` = 2-5 instructions

**Proposed**: `ADRP+ADD+BLR` = **3 instructions** (with MOVX_IMM fallback if out of ±4GB range)

**Implementation**:
1. Add `host_arm64_call_adrp(block, dest)` in `codegen_backend_arm64_ops.c`:
   - Try ADRP+ADD to load target address into X16
   - If within ±4GB: ADRP + ADD + BLR = 3 insns
   - If out of range: fall back to MOVX_IMM + BLR = 2-5 insns
2. Update `host_arm64_call()` to use this new logic by default
3. Update `build_load_routine()` and `build_store_routine()` — these call C slow-path functions
4. Update `codegen_FP_ENTER`, `codegen_MMX_ENTER`, `codegen_LOAD_SEG` — these call C functions

**Savings**: 2 instructions per external call. These are slow-path calls (TLB miss, segment load, exception), so the per-call savings are less impactful than 3A, but they still reduce code size.

**Risk**: Low. ADRP+ADD is already battle-tested (Phase 2D/2E). Falls back to MOVX_IMM if out of range.

### Opt 3C: ADRP+ADD for Pointer Immediate Loads

**Targets**: `host_arm64_MOVX_IMM()` calls that load pointer constants (not function calls):
- `codegen_MOV_PTR` — loads a 64-bit pointer into a register
- `codegen_MOV_REG_PTR` — loads pointer then dereferences it
- `codegen_MOVZX_REG_PTR_8` — loads pointer, reads byte
- `codegen_MOVZX_REG_PTR_16` — loads pointer, reads halfword
- `codegen_LOAD_FUNC_ARG0_IMM` through `_ARG3_IMM` — loads 64-bit immediates as function arguments
- `codegen_LOAD_SEG` — loads segment descriptor pointer

**Current**: `MOVX_IMM` = 1-4 instructions (typically 4 for 64-bit pointers)

**Proposed**: `ADRP+ADD` = **2 instructions** (with MOVX_IMM fallback)

**Implementation**:
1. In each affected uop handler, replace `host_arm64_MOVX_IMM(block, reg, (uint64_t) ptr)` with:
   ```c
   host_arm64_ADRP_ADD(block, reg, ptr);
   ```
   `host_arm64_ADRP_ADD` already has the MOVX_IMM fallback (Phase 2D).
2. For `LOAD_FUNC_ARG*_IMM`: only use ADRP+ADD when the immediate is a pointer (high bits set). Small integer immediates should keep using `host_arm64_mov_imm()`.

**Savings**: 2 instructions per pointer load. These are less frequent than memory ops but still add up, especially for `MOV_REG_PTR` (used for global variable access) and `LOAD_SEG`.

**Risk**: Very Low. ADRP+ADD already has the built-in MOVX_IMM fallback.

**Test**: Boot Windows 98 VM, run 3DMark 99, run DOS/Win games. Verify no regressions.

---

## Phase 4: 3DNow FRECPE/FRSQRTE Optimization + Bug Fix — TODO

Replace scalar FP division in 3DNow `PFRCP` and `PFRSQRT` uop handlers with ARM64 NEON fast estimate + Newton-Raphson iteration. Also fix a register clobber bug in `PFRSQRT`.

**Files modified**: 1 (`codegen_backend_arm64_uops.c`)

### Bug Fix: PFRSQRT Register Clobber

The current `codegen_PFRSQRT` (`codegen_backend_arm64_uops.c:1869`) has a bug where `FMOV_S_ONE` overwrites the sqrt result in `REG_V_TEMP`:

```c
// CURRENT (BUGGY):
host_arm64_FSQRT_S(block, REG_V_TEMP, src_reg_a);   // V_TEMP = sqrt(src[0])
host_arm64_FMOV_S_ONE(block, REG_V_TEMP);            // V_TEMP = 1.0  ← OVERWRITES sqrt!
host_arm64_FDIV_S(block, dest_reg, dest_reg, REG_V_TEMP); // dest = dest / 1.0  ← WRONG
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);
```

The x86-64 reference (`codegen_backend_x86-64_uops.c:1950`) puts `1.0` in `dest_reg` and divides by `TEMP`:

```c
// x86-64 reference (CORRECT):
host_x86_SQRTSS_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);  // TEMP = sqrt(src)
host_x86_MOV32_REG_IMM(block, REG_ECX, 1);
host_x86_CVTSI2SS_XREG_REG(block, dest_reg, REG_ECX);        // dest = 1.0
host_x86_DIVSS_XREG_XREG(block, dest_reg, REG_XMM_TEMP);     // dest = 1.0 / sqrt(src)
host_x86_UNPCKLPS_XREG_XREG(block, dest_reg, dest_reg);
```

**Fix**: Write `1.0` to `dest_reg`, keep sqrt in `REG_V_TEMP`:

```c
// FIXED:
host_arm64_FSQRT_S(block, REG_V_TEMP, src_reg_a);    // V_TEMP = sqrt(src[0])
host_arm64_FMOV_S_ONE(block, dest_reg);                // dest = 1.0
host_arm64_FDIV_S(block, dest_reg, dest_reg, REG_V_TEMP); // dest = 1.0 / sqrt(src[0])
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);
```

### Opt 4A: FRECPE for PFRCP

**Current** (`codegen_backend_arm64_uops.c:1851`):

```c
host_arm64_FMOV_S_ONE(block, REG_V_TEMP);            // V_TEMP = 1.0
host_arm64_FDIV_S(block, dest_reg, REG_V_TEMP, src_reg_a); // dest = 1.0 / src
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);    // broadcast
```

`FDIV_S` has ~10-15 cycle latency on Apple M-series and Cortex-A cores.

**Proposed**: Use `FRECPE` (fast reciprocal estimate, ~1 cycle) + `FRECPS` (Newton-Raphson step, ~4 cycles):

```c
host_arm64_FRECPE_S(block, dest_reg, src_reg_a);     // dest ≈ 1/src (8-bit precision)
host_arm64_FRECPS_S(block, REG_V_TEMP, dest_reg, src_reg_a); // step = 2 - dest*src
host_arm64_FMUL_S(block, dest_reg, dest_reg, REG_V_TEMP);    // dest *= step (16-bit precision)
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);    // broadcast
```

3DNow PFRCP only guarantees 14-bit mantissa precision (AMD spec). One Newton-Raphson iteration gives ~16-bit precision — more than sufficient.

**Savings**: ~10 cycles → ~6 cycles per PFRCP. 3 instructions vs 2, but avoids the high-latency FDIV.

### Opt 4B: FRSQRTE for PFRSQRT

**Current** (after bug fix):

```c
host_arm64_FSQRT_S(block, REG_V_TEMP, src_reg_a);    // V_TEMP = sqrt(src)  (~12 cycles)
host_arm64_FMOV_S_ONE(block, dest_reg);                // dest = 1.0
host_arm64_FDIV_S(block, dest_reg, dest_reg, REG_V_TEMP); // dest = 1/sqrt(src) (~12 cycles)
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);
```

`FSQRT_S` + `FDIV_S` = ~24 cycles total.

**Proposed**: Use `FRSQRTE` (fast reciprocal square root estimate) + `FRSQRTS` (Newton-Raphson step):

```c
host_arm64_FRSQRTE_S(block, dest_reg, src_reg_a);     // dest ≈ 1/sqrt(src) (8-bit precision)
host_arm64_FRSQRTS_S(block, REG_V_TEMP, dest_reg, src_reg_a); // step = (3 - dest*dest*src) / 2
host_arm64_FMUL_S(block, dest_reg, dest_reg, REG_V_TEMP);     // dest *= step (16-bit precision)
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);
```

3DNow PFRSQRT only guarantees 15-bit mantissa precision. One iteration gives ~16-bit — sufficient.

**Savings**: ~24 cycles → ~6 cycles per PFRSQRT. Eliminates both FSQRT and FDIV.

### New Emitters Needed

| Instruction | Encoding | Purpose |
|-------------|----------|---------|
| `FRECPE S` | `5EA1D800` | Scalar float reciprocal estimate |
| `FRECPS S` | `5E20FC00` | Scalar float reciprocal step (Newton-Raphson) |
| `FRSQRTE S` | `7EA1D800` | Scalar float reciprocal square root estimate |
| `FRSQRTS S` | `5EA0FC00` | Scalar float reciprocal square root step (Newton-Raphson) |

**Risk**: Low. 3DNow is used by a small number of games (Unreal, Quake III Arena on AMD K6-2/K6-III). The precision is sufficient per AMD specs. If precision issues arise, add a second Newton-Raphson iteration (still faster than FDIV).

**Test**: Boot Windows 98 VM with AMD K6-2 CPU, run 3DNow-enabled games.

---

## Phase 5: Benchmarking — TODO

Compare performance before and after on representative workloads to measure actual impact.

### Methodology

- **Platform**: M2 MacBook Air, macOS 15.x
- **Workloads**:
  1. Windows 98 SE cold boot to desktop (Pentium config)
  2. 3DMark 99 CPU test (Pentium config)
  3. Quake timedemo demo1 (Pentium config)
  4. DOS games — Prince of Persia, Doom, SimCity 2000 (various CPU types)
  5. Windows 3.1 Program Manager UI responsiveness (486 config)
  6. (Optional) Quake III Arena timedemo with AMD K6-2 (3DNow test)
- **Metrics**: MIPS (reported by 86Box), frames per second, boot time

### Measurements needed

- Baseline: master branch (no optimizations)
- After Phase 1: C-level only
- After Phase 2: + JIT backend
- After Phase 3: + call optimization
- After Phase 4: + FRECPE/FRSQRTE (3DNow workload only)

---

## Areas Investigated and Rejected

| Area | Finding | Why Not |
|------|---------|---------|
| TBZ/TBNZ for sign-bit tests | `TEST_JS_DEST`/`TEST_JNS_DEST` use TST+Bcc (2 insns). Existing `host_arm64_TBNZ` wrapper also emits 2 insns (TBZ+B for ±128MB range). | No savings — both approaches are 2 instructions. Direct TBZ/TBNZ with 14-bit offset (±32KB) would save 1 insn but requires new patching infrastructure for marginal gain. |
| NEON for x87 FPU | FPU ops (`codegen_FADD/FMUL/FDIV/FSUB/FSQRT/FABS/FCHS`) already use native ARM64 FP instructions (`FADD_D`, `FMUL_D`, etc.). | Already optimal — no C function call overhead, no vectorization opportunity (x87 is scalar double). |
| Further MMX NEON optimization | MMX ops (`PADDB`, `PMULLW`, `PMADDWD`, etc.) already use NEON (`ADD_V8B`, `MUL_V4H`, `SMULL_V4S_4H`, `ADDP_V4S`). | Already optimal — full NEON mapping, no scalar fallbacks. |
| SQSHRN for MMX PMULHW | `PMULHW` uses `SMULL_V4S_4H` + `SHRN_V4H_4S` (2 insns). `SQSHRN` could combine shift+narrow. | `SQSHRN` adds saturation semantics that `SHRN` doesn't — behavior change. The existing 2-instruction sequence is correct and already fast (both are 1-cycle on M-series). |
| BFI reduction (Phase 2A) | Sub-register handlers already use BFI optimally. No `dest==src` patterns that could use BFXIL. | Audited, confirmed no optimization opportunity. |
| Peephole optimizer | IR compiler already has dead code elimination (`codegen_reg_process_dead_list`) and MOV register renaming. | Adding more passes (constant folding, common subexpression) would increase compile time for modest runtime gains. Not worth the complexity. |
| CSEL for comparisons | FCOM/FTST already use CSEL chains optimally. CMP+branch handlers are correct as-is. | No identified patterns where CSEL would replace branches in ALU uop handlers. |
| CCMP chaining | Complex multi-flag comparisons use separate CMP + branch sequences. | Investigated the uop handlers — the IR already decomposes complex conditions into individual comparisons. CCMP would require IR-level changes to recognize fusible condition pairs. Not worth the complexity. |
| Constant pool for pointers | `LDR (literal)` = 1 insn (vs ADRP+ADD = 2 insns) for pointer loads near a constant pool. | Requires new infrastructure (pool allocation, alignment, overflow handling). Marginal gain over ADRP+ADD. |
| Register pressure (restore to 10 GPRs) | Phase 2E reduced pool from 10→8 GPRs for TLB caching. | No observable spill increase. TLB caching benefit far exceeds register pressure cost. |
| STP/LDP for adjacent cpu_state accesses | `codegen_direct_read/write_*` functions use single LDR/STR with cpu_state offsets. | Would need profiling to identify hot adjacent-field access pairs. Very case-specific, unlikely to produce measurable gains without profiling data. |
| Flags computation optimization | Flags are already lazy — `flags_rebuild()` only runs when flags are read. JIT handles this via IR (dead flag elimination). | No additional optimization possible without changing the IR flag model. |

## Key Reference Files

| File | What to look at |
|------|----------------|
| `src/include/86box/86box.h:92-97` | Existing LIKELY/UNLIKELY macros |
| `src/cpu/386_common.h` | Instruction fetch, memory macros, EA calculation |
| `src/cpu/386_dynarec.c` | Interpreter loop, JIT dispatch, block validation |
| `src/cpu/x86_flags.h` | Lazy flag computation (context for JIT opts) |
| `src/include/86box/mem.h` | readlookup2/writelookup2 declarations |
| `src/codegen_new/codegen_backend_arm64.c` | JIT prologue/epilogue, memory stubs, `build_load_routine`, `build_store_routine` |
| `src/codegen_new/codegen_backend_arm64_uops.c` | IR-to-native translation handlers — 32 `host_arm64_call` sites, 9 `MOVX_IMM` sites for pointers |
| `src/codegen_new/codegen_backend_arm64_ops.c` | ARM64 instruction emission — `host_arm64_call` (line 1654), `host_arm64_MOVX_IMM` (line 1128), `host_arm64_ADRP_ADD` (line 1147) |
| `src/codegen_new/codegen_backend_arm64_ops.h` | Instruction emitter declarations |
| `src/codegen_new/codegen_backend_arm64_defs.h` | Register definitions |
| `src/codegen_new/codegen_backend_arm64_imm.c` | Immediate encoding table |
| `src/codegen_new/codegen_allocator.c` | JIT pool allocation — single ~120MB mmap (line 92) |
| `src/codegen_new/codegen_allocator.h` | `MEM_BLOCK_NR=131072`, `MEM_BLOCK_SIZE=0x3c0` |
| `src/codegen_new/codegen_ir.c` | IR compiler — dead code elimination, MOV renaming |
