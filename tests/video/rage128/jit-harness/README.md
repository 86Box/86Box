# Rage 128 span-JIT host test harness

`jit_host_test.c` generates ARM64 JIT blocks, and on an x86-64 host x86-64
blocks as well, for a matrix of supported draw states, runs native blocks on
synthetic buffers, and compares
output byte-for-byte against a C reference. The reference mirrors the
interpreter's pixel loop (`ref_span`) and sampler (`rr_*`). Helper-call states
also exercise call glue with a synthetic texture helper that clobbers
registers and rejects some pixels. Hosts without a matching native backend
run reference-only checks and print ordered checksums. Exit zero means the
selected checks pass.

The harness includes the device and emitter headers directly from
`../../../../src/include/86box/`, including `vid_ati_rage128.h`,
`vid_ati_rage128_codegen_arm64.h`, and `vid_ati_rage128_codegen_x86_64.h`.
There are no vendored header copies. The C reference is a hand-mirrored copy
of the interpreter and sampler.

## Build and run

Run these commands in this directory. `build.sh` uses `CC` (default `clang`)
and `-I ../../../../src/include` to build `jit_host_test`.

    ./build.sh
    ./jit_host_test              # full matrix
    ./jit_host_test 0xcafe0001    # alternate fuzz seed, hex or decimal
    ./shard.sh 4                 # four processes, default seed
    ./shard.sh 4 0xcafe0001       # four processes, alternate seed
    JHT_NOSOA=1 ./shard.sh 4      # scalar-emitter checks

An unsharded run takes about 20 minutes on one core of an arm64 Mac and
about 35 on an x86-64 workstation. `shard.sh` splits the
fuzz gate across cores by calling the shared runner at `../shard.sh` in
`jit` mode. With no count argument, it uses the detected CPU count.

From the repository root, the CMake route is:

    cmake -S . -B build -D BUILD_TESTING=ON
    cmake --build build
    ctest --test-dir build --output-on-failure -R '^Rage128\.JitHost'

### Floating-point contraction

`-ffp-contract=off` is mandatory and `build.sh` supplies it. The emitted code
mirrors the interpreter's separate floating-point multiply and add operations.
Contraction in the host C reference fuses these operations and can produce
false mismatches against both the interpreter and the JIT.

### Standalone modes

Invoke probes directly, for example:

    ./jit_host_test bench [npx]  # per-shape time per pixel
    ./jit_host_test dump         # emit a block for disassembly
    ./jit_host_test branchpatch  # skip patcher's branch classifier
    JHT_BIGPX_REPRO=1 JHT_BIGPX_REPRO_ITERS=20 ./jit_host_test

Other standalone modes are `sizes`, `benchdump`, `contend`, `rttone`,
`texone`, `persp`, `persp-dump`, `weights`, and `weights-dump`.
The shard runner accepts only the fuzz gate and an optional seed. With
`JHT_SHARD` set, standalone modes run once in shard zero with row selection
disabled; other shards exit without running the probe.

### Environment switches

Presence switches activate whenever set, including an empty value or `0`.
Leave them unset to select the default behavior.

| Switch | Effect |
| --- | --- |
| `JHT_SHARD=k/N` | Select zero-based shard `k` of `N`, with `0 <= k < N`. |
| `JHT_NOSOA` | Generate scalar blocks in checks that select an emitter lane. |
| `JHT_MINB=<code>` | Pin randomized stages' minification code and enable mipmapping, while preserving the random draws. |
| `JHT_RTT` | Bind texture slots to each side's own render target and select scalar pixel order for aliasing draws. |
| `JHT_BIGPX_REPRO` | Run the standalone large-pixel probe: dithered RGB565, one/two stages, nearest/bilinear, depth testing on/off, screen x in 500..949. |
| `JHT_BIGPX_REPRO_ITERS=<rounds>` | Set large-pixel probe rounds; default 400. |
| `JHT_XLEN` | Print generated x86-64 block lengths on a native x86-64 host. |
| `TEXPROBE` | Print texture mismatch details, including initial color/depth cells for spans of at most 64 pixels. |
| `CONTEND_QOS=ui\|in\|bg` | Select contention-worker priority: user-interactive, user-initiated (default), or background. |
| `CONTEND_MS=<milliseconds>` | Set each contention measurement's duration; default 2000. |
| `CONTEND_SHARED` | Make contention workers share the image read by the scanout proxy. |

The `texone` probe reads these switches in the order listed:

| Switch | Effect |
| --- | --- |
| `TEXONE_Z` | Select the depth-enabled base state. |
| `TEXONE_NOSEC` | Disable the secondary texture stage. |
| `TEXONE_NOCK` | Disable the texture chroma-key comparison. |
| `TEXONE_NOPERSP` | Disable perspective correction. |
| `TEXONE_DT4` | Select primary-stage RGB565 without S3TC. |
| `TEXONE_WRAP` | Set both stages' coordinate wrap modes to repeat. |
| `TEXONE_SAME` | Set both stages' minification and magnification to bilinear. |
| `TEXONE_NEAREST` | Set both stages' minification and magnification to nearest. |
| `TEXONE_MIP=<code>`, `TEXONE_MIP2=<code>` | Set primary/secondary minification, enable mipmapping, and match magnification to the code's low bit. |
| `TEXONE_MAG=<code>`, `TEXONE_MAG2=<code>` | Override primary/secondary magnification after minification selection. |
| `TEXONE_STEN` | Enable packed depth/stencil with partial masks and mixed pass/fail lanes. |

### Sharded fuzz gate

Each process visits the complete draw-state and row-input stream. Row `i`
executes only in shard `i % N`; skipped rows consume the same random draws.
The buffer and fog-table contents of a skipped row are discarded using an
exact advance of the linear congruential generator, without filling buffers.
This preserves later row inputs and state configurations. Every executed row
starts with fresh buffers, so execution does not feed back into the stream.
For a single shard without the runner, use `JHT_SHARD=2/4 ./jit_host_test`.

The shared runner prints phase/configuration lines once, from shard zero,
and sums the final `rows=`/`fail=` summaries. Configuration matrices are
identical in every shard. Reference-only checksums stay in each child's
output: ordered hashes cannot be summed into an unsharded checksum.
All lane and probe environment variables are inherited.

The runner preserves child output, exit statuses, and isolated working
directories in a temporary directory announced on stderr. Set
`SHARD_LOG_DIR` to choose a fresh destination. A nonzero child status, crash,
missing or duplicate final summary, or nonzero failure total fails the gate.
The scripts use bash syntax supported by macOS bash 3.2 and MSYS2 bash.
CPU detection tries `sysctl`, `nproc`, and `getconf`, then
`NUMBER_OF_PROCESSORS`, falling back to one process. `TMPDIR` selects the
parent directory for temporary output when `SHARD_LOG_DIR` is unset.
