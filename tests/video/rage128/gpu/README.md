# Rage 128 GPU kernel tests

`gputri` checks the production span-shading kernel (all twenty variants)
embedded in `src/include/86box/vid_ati_rage128_gpu_spv.h`,
the same bytes the device uses. The header's other two kernels, 2D
read-modify-write and the overlay compositor, are compared with the CPU path by
the register harness's GPU verify lane.
It drives them through the device's rows/order/spans/tris ABI and compares
VRAM and guard bytes against a C copy of the interpreter's pixel loop from
`../jit-harness/jit_host_test.c`. The reference includes texture sampling,
both texture stages, combine and lighting, blend, scissors, depth, stencil,
fog, chroma key, and destination masks.

The default gate includes per-state fuzz for every span variant, with
randomized runtime selectors and folded tuples. Fault injection corrupts span
row addresses and must produce a mismatch on every span variant. Mixed-kernel segment
fuzz checks row sorting, draw order, kernel and tuple run splits, barriers,
and the modeled alias/hazard acceptance rules against sequential reference
replay. Directed checks cover serial feedback, mask-edge cells, top-column
ranges, span-cap headroom, and the production kernel matrix.

## Build and run

From this directory:

```sh
./build.sh
./gputri [iters-per-state] [seed]
./fdivtest [batches] [seed]
```

`build.sh` builds `gputri` and `fdivtest`, and uses `glslc` to compile
`fdivtest.comp`. It builds the C references with `-ffp-contract=off`.
Vulkan headers and the loader come from `VULKAN_SDK` when set, or a
compiler/system installation; `glslc` comes from the SDK or `PATH`. The
script does not regenerate the production SPIR-V header.

The CMake route, from the repository root, is:

```sh
cmake -S . -B build -D BUILD_TESTING=ON
cmake --build build
ctest --test-dir build -R '^Rage128\.GpuKernels' --output-on-failure
```

The GPU targets need the Vulkan loader and `glslc`. `gputri` exits with `GPUTRI_EXIT_SKIP`
(77) when no Vulkan driver or physical device is available; CTest reports
that status as skipped. A gate mismatch exits 1; a passing gate exits 0.
`fdivtest` needs a Vulkan device too, but exits 1 when none is available.

`gputri` defaults to 150 iterations per state. An omitted or zero seed runs
three standard seeds: `0x12345678`, `0xcafe0001`, and `0xdeadbeef`. A nonzero
seed runs one seed; decimal and hexadecimal input are accepted. Run from
this directory so the selftest kernel is available. The pipeline cache
`gputri.plcache` keeps repeated runs warm.

On macOS, `gputri` sets `MVK_CONFIG_FAST_MATH_ENABLED=0` itself unless the
environment already supplies a value. Keep fast math off: contraction or
reordering changes rounding steps that the C reference performs separately
and breaks byte-exact comparison. For `fdivtest` on macOS, supply the same
zero value in the environment; that program does not set it itself.

## gputri switches and diagnostic modes

These are all the `GPUTRI_*` environment variables the program reads.
Presence switches are enabled by any supplied value, including an empty
value or `0`; value switches use the parsing described below.

| Variable | Effect |
| --- | --- |
| `GPUTRI_THREADS` | A positive decimal integer sets the pipeline-priming worker limit. Otherwise the limit is the processor count minus two, clamped to the range from one to `PRIME_THR_MAX` by the priming code. |
| `GPUTRI_PIXDUMP` | A comma-separated list of up to eight VRAM byte addresses, parsed as decimal or hexadecimal. Prints reference sampler and pre-quantization diagnostics for destination cells containing those addresses. |
| `GPUTRI_IMPORT` | Exactly `1` imports host allocations for the VRAM, texture-stage, and color/depth-stage arenas through Vulkan's external host-memory extension. Other values use the default allocator. Unsupported import requests exit 1. |
| `GPUTRI_STG_RESIDENT` | Presence makes the staged color/depth fuzz block use resident surfaces with the same generated case content, isolating staged addressing. |
| `GPUTRI_COMB` | Selects the folded combine book for timing pipelines: `0` copies incoming color and permits texturing to be eliminated, `1` modulates by the texel, and `2` copies the texel. The default and out-of-range fallback are `1`; parsing uses decimal integer conversion. |
| `GPUTRI_NOBARRIER` | Presence removes barriers between timing dispatches/repetitions. Repetitions can race; these timing legs do not compare output. |
| `GPUTRI_PROF_ROWS` | In `profbench`, a positive decimal row count up to `MAX_PY` overrides every leg's workgroup count. Other values leave each leg's row count unchanged. Span counts stay fixed. |
| `GPUTRI_PROF_BENCH` | Presence turns `profbench` into timed legs with a median of nine measurements and scaled repetitions. Without it, each leg has one warmup and one measured execution for profiling. |

The first argument also selects these modes:

| Argument | Checks or measurements |
| --- | --- |
| `headroom` | Span-cap headroom checks only. |
| `bench` | Intra-kernel timing with fixed, variable-width, rejection, packing, and folding workloads. |
| `profbench` | Folded narrow-span workloads with wide-span and unfolded controls for profiling. |
| `menubench` | Stacked alpha-blended workloads and controls for width, overlap, and chain depth. |
| `plbench` | Pipeline-creation timing with the device's precompile list and disjoint synthetic tuple slices. |
| `lvbench` | Host row-sort and batch-leveling timing. Vulkan initialization still runs. |

Timing modes do not compare pixels. Barrier removal and bench-only selector
folds can produce incorrect output; use the default gate to check correctness.

## Arithmetic tools and bit-exact requirements

`fdivtest` checks the kernels' correctly rounded float divide and the
divide-by-255 helper through one standalone pipeline, so arithmetic can be
iterated without the full gate. It includes the production divide source,
checks the complete divide-by-255 byte domain, and compares randomized
quotients with host float division. It skips cases with NaN operands or a
NaN reference result and compares other results bit-for-bit. The default
is 64 batches of 1,048,576 operand pairs;
an optional seed makes failures reproducible. It loads `fdivtest.spv` from
the working directory.

The kernels and references honor these requirements:

- Depth is the fixed-point plane `r128_zfx` (`vid_ati_rage128.h`): a
  64-bit integer, so `zline + i*dZdx` equals the per-pixel sum exactly and
  each kernel thread computes its own pixel's depth.
- Fast math stays off, shader expressions preserve separate rounding, and
  C builds use `-ffp-contract=off`.
- Float-to-integer helpers truncate and saturate, with NaN mapped to zero,
  matching the modeled interpreter conversion rather than relying on an
  out-of-range shader cast.
- Edge functions use 64-bit integers. Framebuffer and depth cells use
  16-bit storage where appropriate; wide cells also use word views.
- Texture addressing extracts bytes from word buffers. A cell base wraps
  through the VRAM mask once, and its remaining bytes stay linear. The
  one-past-mask 16-bit read includes the guard byte, and the comparison
  covers all four allocated guard bytes.
