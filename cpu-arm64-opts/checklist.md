# ARM64 CPU Optimizations — Master Checklist

## Guards

All C-level changes guarded with `#if defined(__aarch64__) || defined(_M_ARM64)`.
JIT backend files are inherently ARM64-only — no additional guards needed.

## Phase 1: C-Level Interpreter/Dispatch Optimizations

### Opt 1: Branch Hints on Instruction Fetch (`386_common.h`)

- [x] `fastreadl_fetch()` — `LIKELY` on page-no-cross check `(a & 0xFFF) < 0xFFD`
- [x] `fastreadl_fetch()` — `LIKELY` on pccache hit `(a >> 12) == pccache`
- [x] `fastreadl_fetch()` — `UNLIKELY` on `cpu_state.abrt` after `getpccache()`
- [x] `fastreadw_fetch()` — `UNLIKELY` on page-crossing `(a & 0xFFF) > 0xFFE`

### Opt 2: Branch Hints on Memory Read/Write Macros (`386_common.h`)

- [x] `readmemb` — `UNLIKELY` on slow-path condition
- [x] `readmemw` — `UNLIKELY` on slow-path condition
- [x] `readmeml` — `UNLIKELY` on slow-path condition
- [x] `readmemq` — `UNLIKELY` on slow-path condition
- [x] `readmemb_n`, `readmemw_n`, `readmeml_n` — same treatment
- [x] `writememb` — `UNLIKELY` on slow-path condition
- [x] `writememw` — `UNLIKELY` on slow-path condition
- [x] `writememl` — `UNLIKELY` on slow-path condition
- [x] `writememq` — `UNLIKELY` on slow-path condition
- [x] `writememb_n`, `writememw_n`, `writememl_n` — same treatment

### Opt 3: Branch Hints on Interpreter Execution Loop (`386_dynarec.c`)

- [x] `exec386_dynarec_int()` — `LIKELY` on `!cpu_state.abrt`
- [x] `exec386_dynarec_int()` — `UNLIKELY` on page boundary crossing
- [x] `exec386_dynarec_int()` — `UNLIKELY` on `cpu_state.abrt` (post-exec)
- [x] `exec386_dynarec_int()` — `UNLIKELY` on `smi_line`
- [x] `exec386_dynarec_int()` — `UNLIKELY` on `trap`
- [x] `exec386_dynarec_int()` — `UNLIKELY` on NMI
- [x] `exec386_dynarec_int()` — `UNLIKELY` on interrupt pending
- [x] `exec386_dynarec_dyn()` — `LIKELY` on `valid_block`
- [x] `exec386_dynarec_dyn()` — `LIKELY` on `CODEBLOCK_WAS_RECOMPILED`

### Opt 4: Software Prefetch for Block Dispatch (`386_dynarec.c`)

- [x] Prefetch `block` struct after hash lookup (ARM64-guarded)
- [x] Prefetch `block->data` before calling compiled code (ARM64-guarded)

### Opt 5: Branchless Block Validation (`386_dynarec.c`)

- [x] Replace `&&` with bitwise `&` in block validation at first site (ARM64-guarded)
- [x] Replace `&&` with bitwise `&` in block validation at second site (ARM64-guarded)

### Opt 6: Remove Redundant `__builtin_available` Checks (`386_dynarec.c`)

- [x] Remove `__builtin_available(macOS 11.0, *)` at first W^X site
- [x] Remove `__builtin_available(macOS 11.0, *)` at second W^X site

### Opt 7: TLB Prefetch During EA Calculation (`386_dynarec.c`)

- [x] Prefetch `readlookup2` entry in `fetch_ea_32_long()` (ARM64-guarded)

### Phase 1 Testing

- [ ] **BUILD**: Compiles on ARM64
- [ ] **RUN TEST**: Boot Windows 98 VM, verify normal operation
- [ ] **RUN TEST**: Run 3DMark 99 or similar workload
- [ ] Create PR for Phase 1

## Phase 2: JIT Backend Code Quality

### Phase 2A: Sub-register BFI Reduction (`codegen_backend_arm64_uops.c`)

- [ ] Audit all BFI usage in byte/word ALU handlers
- [ ] Replace BFI with BFXIL where `dest_reg == src_reg_a`
- [ ] Optimize byte-high (BH) operations: AND+shift → UBFX
- [ ] Verify no regressions on x86-64 (guards keep original code path)

### Phase 2B: New Instruction Emitters (`codegen_backend_arm64_ops.c/.h`)

- [x] Add `host_arm64_CSEL` variants (NE, LT, GE, GT, LE, HI, LS)
- [x] Add `host_arm64_ADDS_REG` / `host_arm64_SUBS_REG`
- [x] Add `host_arm64_ADDS_IMM` / `host_arm64_SUBS_IMM`
- [x] Add `host_arm64_CSINC` / `host_arm64_CSNEG`
- [x] Add `host_arm64_CLZ`
- [x] Add `host_arm64_RBIT`
- [x] Add `host_arm64_UDIV` / `host_arm64_SDIV`
- [x] Add `host_arm64_MADD` / `host_arm64_MSUB`
- [x] Declarations in `codegen_backend_arm64_ops.h`

### Phase 2C: Fused Shift-ALU (`codegen_backend_arm64_uops.c`)

- [x] Identify shift+ALU sequences that can use fused forms
- [x] Update uop handlers to pass shift parameter to ALU emitters
- [x] Verify changes are ARM64-guarded

**Note**: Already complete in existing codebase. All shift+ALU sequences already use fused forms (ADD_REG_LSR, SUB_REG_LSR, AND_REG_ASR, etc.) where ARM64 ISA permits.

### Phase 2D: ADRP+ADD Immediate Loading (`codegen_backend_arm64_ops.c`)

- [x] Implement `host_arm64_ADRP_ADD()` helper
- [x] Verify JIT code address is available at emit time
- [x] Replace `MOVX_IMM` with `ADRP_ADD` for pointer loads in stubs
- [x] Range check: ensure ±4GB coverage is sufficient

### Phase 2E: Cached Lookup Table Registers (`codegen_backend_arm64.c`, `_defs.h`)

- [x] Decide which 2 callee-saved regs to dedicate (X27, X28 proposed)
- [x] Update `codegen_backend_arm64_defs.h` register assignments
- [x] Load `readlookup2`/`writelookup2` in prologue
- [x] Update memory load/store stubs to use cached registers
- [x] Reduce `CODEGEN_HOST_REGS` from 10 to 8
- [ ] Verify register pressure doesn't cause spill regressions

### Phase 2 Testing

- [ ] **BUILD**: Compiles on ARM64
- [ ] **RUN TEST**: Boot Windows 98 VM
- [ ] **RUN TEST**: DOS games (heavy 16-bit byte/word ops)
- [ ] **RUN TEST**: Windows 3.1/95 (mixed 16/32-bit)
- [ ] Create PR for Phase 2

## Phase 3: Benchmarking

- [ ] Define benchmark workload (specific game/app + measured metric)
- [ ] Measure before/after on representative workloads
- [ ] Document results
