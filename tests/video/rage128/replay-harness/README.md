# Rage 128 offline capture replay harness

This tool replays a device command stream against the production Rage 128
sources without an emulator core, guest, or virtual machine. It supports
repeatable command execution, stream cuts, VRAM dumps, and bus-read
accounting. Its main use is iterating on a device fix against a recorded
session: record a guest session once with the capture tap, then rebuild
the replay and rerun the capture after each change, in seconds and
without a virtual machine. A replay needs a capture supplied by the
developer, so the tool itself is not a CTest test; `Rage128.ReplaySynth`
replays the synthetic captures that `synth/` generates from nothing.

## Build

From this directory:

```sh
./build.sh
```

The script writes `./replay`. It compiles the device sources four levels
up at `../../../../src`, with `host_stubs.c` and `include/` from the sibling
`../reg-harness`. The stubs supply host services, while the production
Unix thread primitives provide real threads. A C compiler, pthreads, and
the math library are required. `CC` selects the compiler; Clang is the
default. The build uses `-O2` and `-ffp-contract=off`, matching the device's
separate rounding of floating-point operations. Vulkan headers enable
the GPU backend; without them the backend is stubbed.

The CMake route, from the repository root, is:

```sh
cmake -S . -B build -D BUILD_TESTING=ON
cmake --build build --target rage128_replay
```

The target writes the replay binary under `build/tests/video/rage128/`.
The normal emulator build prerequisites also apply when configuring the
project. Building this target does not replay a capture. With Python 3
present, `ctest --test-dir build -R '^Rage128\.ReplaySynth'` runs
`synth/selftest.py` against the built binary.

## Record a capture

Set `RAGE128_CAPTURE` to an absolute output path before launching 86Box.
In the examples, `capture` is that path, and `vram` and `recap` are output
paths you choose. Use a writable directory and a new path for each run:
the tap opens its output for writing, replacing any existing content.

```sh
RAGE128_CAPTURE="$capture" ./86Box
```

For a macOS GUI launch, set the launch environment, launch 86Box normally,
and unset the variable when the session ends:

```sh
launchctl setenv RAGE128_CAPTURE "$capture"
launchctl unsetenv RAGE128_CAPTURE
```

The tap opens at command-thread initialization and records the session
from its start; a preamble register dump is not required. A multi-chip
board uses a separate tagged path for each chip. Replay each chip's
stream separately.

On macOS and Linux, a path ending in `.zst` selects streaming compression
through `zstd`. If compression is unavailable, the tap writes plain text
to the path with the `.zst` suffix removed. On Windows the tap writes
plain text using that same suffix-removal rule. The device's capture
message identifies the actual output and whether compression is active.
Replay accepts plain text or `.zst`; compressed replay requires `zstd`
on the command search path and starts one decompressor per reader.

An armed tap forces one render thread. Replay serves bus reads in record
order, so interleaved texture reads from multiple render workers cannot
be reconstructed as a single execution stream. Preserve this setting
when recording.

Plan for hundreds of megabytes of uncompressed data for a boot, desktop,
3D workload, and shutdown session. Workloads that gather textures from
host memory can be much larger: each `B` record uses two hex characters
per payload byte, in addition to its header. Measure a short run's size
and write rate before recording a long session. Keep guest captures out
of version control; the local ignore rules cover `.cap` and `.cap.zst`
files, and other output names need an appropriate location or local
ignore rule.

The tap flushes its stdio stream every 1024 records. A forced process
exit can lose buffered records and a pending contiguous bus-read run.
With compression, flushing hands bytes to the compressor's pipe and
does not ensure that the compressor writes them to disk. A forced exit
can leave a truncated compressed frame; `zstd -t` checks its integrity.

## Replay

```sh
./replay "$capture"
./replay -v --dump-vram "$vram" "$capture"
env R128_JIT=verify ./replay -v --dump-vram "$vram" "$capture"
env R128_GPU=verify ./replay -v --dump-vram "$vram" "$capture"
```

The default lane uses the CPU interpreter. `R128_JIT=1` selects the JIT
on a supported host, and `R128_JIT=verify` compares its rows against the
interpreter. `R128_GPU=1` selects the GPU backend, and `R128_GPU=verify`
compares GPU segments against CPU execution. GPU replay requires the
Vulkan headers at build time and a usable Vulkan device at run time (the
kernels are the committed SPIR-V, so no shader tools); check verbose
initialization output to confirm that the selected backend runs.

| Flag | Effect |
| --- | --- |
| `-v` | Enables device logging and record progress messages. |
| `--memory MB` | Sets guest RAM, default 64 MB. In PCI-GART mode this must cover the captured page-table base and its 32 KB table. Match the guest's RAM size when possible. |
| `--vram MB` | Sets card VRAM, default 32 MB. Match the captured board's configuration. |
| `--streamcut N` | Limits both capture readers to the first N records; zero means the whole stream. A cut can leave a packet unfinished. |
| `--tail-budget N` | Supplies all-ones for N bus reads after the command stream ends, then zeros so indirect walks can terminate. Default 65536; tail reads are counted separately from gate underruns. |
| `--dump-vram FILE` | Writes local VRAM after final accounting. The GPU lane first runs the production CPU-read barrier over VRAM. |
| `--recap FILE` | Records replay execution through the production capture taps into a plain-text stream. |
| `--apply-display` | Applies display and overlay register writes to the device instead of holding them back. The VGA core remains stubbed. |
| `--pick-thread` | Runs a modeled scanout consumer that picks a snapshot and checks its checksum across a frame interval; reports slot mutations. |
| `--pick-flip N` | Requests a synthetic display-base flip every N main-loop records, deferred until the engine is idle. Two additional flips to the alternate base exercise snapshot slot replacement. Zero disables the cadence. Use with `--pick-thread`. |
| `--scan-thread` | Runs a modeled 60 Hz beam with horizontal-blank, vertical-blank, and vsync callbacks. Scores supported RGB565 frames for rows with at least 95 percent pixels equal to `0x4288`. |

A GPU VRAM dump is refused if the engine remains active at the end of the
stream, such as an unfinished packet or a `WAIT_UNTIL` stall. Draining
then is unbounded and can advance execution beyond the requested cut.
The tool prints `vram dump refused`, writes no dump, and returns status 3
unless a replay gate also fails. A coherent GPU dump prints
`vram coherent`. CPU dumps use the completed raster flush.

Status 1 means a nonzero `mismatch`, `underrun`, ring-window count, or
`ring writes` count. Status 2 reports an argument, input-opening,
allocation, or thread-start error. A successful replay returns 0; this
status is an execution check, not a verdict on every displayed pixel.
Tail underruns, unconsumed records, and short payload counts need separate
inspection. A dump-file write error is reported but does not set a
separate failure status, so also check that the output exists.

A recap changes record representation: injected `D` records execute as
`J`, and consecutive bus dwords can coalesce into a `B` run. Compare
served values and VRAM as well as record text. A record-level comparison
needs to account for these forms and incomplete final records.

## Record grammar

Each line starts with a record letter. Numeric fields are hexadecimal
without a prefix; a `B` payload has two hex digits per byte. The tap
serializes whole records, with command dwords recorded when consumed,
direct register writes recorded at their execution boundary, and
contiguous bus reads grouped into a run.

| Record | Meaning |
| --- | --- |
| `D <dword>` | Ring dword consumed by the executor. |
| `J <dword>` | MMIO-injected dword consumed by the executor. |
| `R <off> <val> <mask>` | Direct CPU register write: byte offset, lane-shifted value, and write mask. |
| `B <addr> <len> <hex>` | Bus data with its payload, from a gathered block or contiguous dword-read run. Length is in bytes. |
| `I <addr> <val>` | Accepted bus dword record with its value. |
| `X <vm> <len>` | Accepted payload-free block read, served as all-ones. |
| `W <addr> <len>` | Observed device bus write, with address and byte length but no data. |
| `A <card_off> <bus> <n> <pos> <agp_base> <gart_page> <viaBus> <viaRam>` | Accepted address discriminator, counted without interpreting its fields. |

## Replay model and limits

Command dwords enter the production FIFO directly, without a ring in
guest RAM. A second capture reader supplies `I`, `B`, and `X` bus records
in order. The independent bus producer lets register writes drain the
executor without blocking the producer they need. Its queue holds up to
four million records; on a host with 24-byte entries this is 96 MiB.
Outstanding `B` payload storage has a separate 1 GiB threshold; the
producer stalls above it, so a single block can exceed the threshold.
Device memory and parser buffers add to these allocations. Memory use
does not require preloading the complete capture.

A `B` record serves a whole block of matching length or successive dwords
for the modeled PCI-GART walk. The head record remains until its payload
is exhausted. Matching uses length and payload position, not address:
addresses in a mismatch diagnostic describe the request and the recorded
read, but an address difference alone does not fail replay. A short or
malformed payload is padded with `0xff` and counted as short. Payload-free
`X` records supply all-ones; a dword request skips preceding `X` blocks
and counts them as skipped. Such records cannot establish pixel fidelity.

In PCI-GART mode (`PCI_GART_PAGE` bit 0 clear), the modeled device reads
in-RAM page-table targets directly, bypassing the hook. Replay plants a
table at the captured base with all targets beyond RAM, routing data
through the device's bus fallback into the recorded stream. If no
`BM_CHUNK_0_VAL` write is present when the first command dword arrives,
replay sets the modeled pointer, PM4-read, and global force-to-PCI bits
so the AGP device selects the on-chip walk. A captured write controls its
own routing.

Register writes use the production handler's ordering against command
execution. Replay shadows `PM4_BUFFER_OFFSET`, `PM4_BUFFER_CNTL`,
`PM4_BUFFER_DL_RPTR`, and `PM4_BUFFER_DL_WPTR` rather than driving a
nonexistent guest ring. Display and overlay timing writes are held back
unless `--apply-display` is set. The optional pick and scan threads derive
geometry from snooped CRTC state; their timing is modeled and depends on
host scheduling.

The tool does not capture guest CPU stores through the framebuffer
aperture. Desktop pixels and textures uploaded by that path are absent;
register writes through `MM_INDEX` and `MM_DATA`, command-stream uploads,
and recorded bus gathers supply their own data. The legacy VGA core is a
shell, so plane and full VGA timing semantics are absent. PCI configuration
writes are absent from the grammar; replay enables bus mastering itself.
Out-of-RAM device-to-guest writes have no backing store and are counted as
dropped. Input `W` records are counted without comparing write addresses
or data against replay. Backend selection is independent of capture, so
check the chosen lane when investigating a rendering difference.

`ring window WARNING:` reports an `I` or `B` address inside the shadowed
ring window at `AGP_BASE + (PM4_BUFFER_OFFSET & 0x01ffffff)`. Its size
follows the modeled `rage128_pm4_ring_mask` rule: the low six bits of
`PM4_BUFFER_CNTL` encode `2^(encode + 1)` dwords, saturating at `2^32`
dwords for encodes of 31 or greater. The replay does not fetch ring
memory, so pump-fetch records in that bus window shift sequential
serving. Recapture a stream that triggers the warning. `ring writes`
counts replay bus writes overlapping the shadowed window and must be
zero; writes of at least 4096 bytes also increment its block count.

## Generate a capture without a guest

The scripts in `synth/` print complete text streams to standard output.
They set registers, upload or supply texture data, inject triangle
commands, and use register writes to drain and flush drawing. Python 3
is sufficient to generate them; no recorded guest session is needed.
Both generators write `DP_DATATYPE = 0x00000004` for an RGB565 destination
and `PM4_VC_FPU_SETUP = 0x0000005e` for solid Gouraud faces before drawing.
`synth/selftest.py <replay binary> <work directory>` generates all three
streams, replays each with a VRAM dump and checks the pixels; CTest runs
it as `Rage128.ReplaySynth`.

```sh
python3 synth/gen_agptex.py agp > "$capture"
./replay -v --dump-vram "$vram" "$capture"
python3 synth/sample_agptex.py "$vram"

python3 synth/gen_agptex.py gart > "$capture"
./replay -v --dump-vram "$vram" "$capture"
python3 synth/sample_agptex.py "$vram"

python3 synth/gen_ckfn.py > "$capture"
./replay -v --dump-vram "$vram" "$capture"
python3 synth/sample_ckfn.py "$vram"
```

`gen_agptex.py` defaults to `agp`. It draws one triangle using an 8x8
ARGB8888 texture whose first four columns are green and last four are
red. A `B` record supplies the 256 texture bytes. `agp` serves the block
whole through the aperture; `gart` plants an on-chip table and serves it
as 64 dwords. The sampler expects green at `(4, 4)`, red at `(24, 4)`,
and background at `(40, 4)`, and exits 0 with `PASS` when all three match.

`gen_ckfn.py` uploads the same two-color texture into local VRAM and
sweeps the legacy `CLR_CMP_FCN_3D` field across six triangle bands. Its
arguments are decimal function codes or `function:mask`, with a hex mask;
the defaults are `0`, `3`, `1`, `2`, `1:0`, and `3:0`. Supply the same band
arguments after the VRAM path when running `sample_ckfn.py`. The sampler
prints two sample dwords and green, red, background, and other pixel
counts per band; it does not assert a pass/fail verdict. With the default
bands, triangles appear in bands 0, 1, 3, and 5. Function 1 rejects every
texel, leaving bands 2 and 4 as background.

The synthetic streams have no ring fetches or indirect buffers. The
local-VRAM sweep serves no bus reads, while the host-texture stream
serves its `B` payload. Use the sampler and accounting together to assess
the result.
