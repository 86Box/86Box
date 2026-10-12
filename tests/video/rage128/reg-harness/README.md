# Rage 128 host-side register harness

This harness is a consistency and regression tool for the modeled Rage 128
device. A pass checks the emulator against the documented constraints and
independent expectations encoded in the vectors. It does not establish new
hardware behavior or replace guest, driver, scanout, or hardware validation.

`reg_harness.c` includes the production device shell and MAXX wrapper from
`../../../../src/video/` and builds the sibling device units directly.
It calls the register, aperture, and PCI handlers without a VM, guest, or
ROM. The vectors check reset state, register masks, readback, packet
parsing, engine effects, display composition, and agreement between the
interpreter, JIT, and GPU lanes.

A lane gap is a place where the interpreter, JIT, and GPU lanes can disagree
with each other or with the documentation. The `lanegap` vectors pin each
such place using isolated devices, register witnesses, pixels, and counters.

`host_stubs.c` supplies host memory, I/O, PCI, timer, ROM, and video services.
The SVGA core is a shell, so generic VGA rendering and timing belong in
other tests; timers do not fire. Threads use the production pthread-backed
primitives in `src/unix/unix_thread.c`. The real I2C, GPIO, EEPROM, and DDC
units serve the default EDID, making pad drive visible on the bus.

`gpu_probe.c` builds the GPU backend when Vulkan headers are available,
matching the production build gate. Host-side probes use a fabricated
backend with host memory for its buffers. They check state acceptance,
span bounds, staging, hazard ranges, and counters without creating Vulkan
objects. Real GPU verification requires a usable Vulkan runtime and device.
Without headers, unavailable probes print SKIP rows.

## Build and run

Run these commands from this directory. `build.sh` finds the production
sources four levels up at `../../../../src` and uses clang by default.
`CC` selects another compiler and `VULKAN_SDK` supplies a Vulkan include
location. The floating-point contraction flag matches production.

```sh
./build.sh
./reg_harness
./reg_harness -v
R128_JIT=verify ./reg_harness
R128_GPU=verify ./reg_harness
./shard.sh 8
```

A successful ordinary run exits zero and ends with `pass=` and `fail=`
totals. `-v` exposes device logging. GPU verify replays accepted draws
through the interpreter and compares the results. A device that requests
verification and touches acceleration must have a verifying backend at
close; initialization failure produces a failure row. On macOS the GPU
lane requires MoltenVK.

The repository also provides a CMake route:

```sh
cmake -S . -B build -D BUILD_TESTING=ON
cmake --build build --target rage128_reg_harness
ctest --test-dir build --output-on-failure -R '^Rage128\.Registers'
```

Run the CMake commands at the repository root.

| Environment switch | Purpose |
|---|---|
| `R128_JIT` | `0` selects the interpreter, `1` enables the JIT, and `verify` compares JIT rows with the interpreter. |
| `R128_GPU` | `0` disables the GPU lane; `verify` compares accepted GPU work with the interpreter. |
| `R128_GPU_2D` | `verify` compares the queued GPU 2D operations with the CPU path; the dedicated 2D verification runner sets it while its device initializes. |
| `R128_GROUPS` | Selects complete device-owning units with a comma-separated list. |
| `R128_LANEGAP` | When set, adds the expected-failure polyline scissor-clamp vector. The modeled device does not implement that documented clamp. |
| `R128_LANEGAP_LOG` | When set, exposes close-time JIT texture counters for the lane-gap and exceptional-coordinate devices. |
| `R128_LANEGAP_TSAN` | When set, runs the two-writer texture-cache concurrency vector. Use a ThreadSanitizer build to check its synchronization. |
| `R128_X64LEGS` | When set, runs only the exceptional-coordinate device, honoring the caller's JIT mode. |
| `R128_GPU_SYNC_TELEMETRY` | Selects the device's synchronization telemetry output. The telemetry runner redirects its own device to the null sink while testing counters. |

Vector-specific lane overrides restore the caller's environment after
initialization. In particular, `lanegap` enables the JIT for its compilation
checks. Use `x64legs` to retain verify mode for the exceptional-coordinate
checks independently.

Pixel reads from `svga.vram` follow explicit drains, flushes, and aperture
read fences so queued GPU work is visible. Interpreter batch-hazard rows
skip with a real GPU backend because they inspect deferred CPU batch state.
Arena-lifecycle rows also skip with a real backend because they require a
pristine fabricated arena. Fabricated-backend vectors save and restore the
real backend around their probes.

## Regression expectations

The render-target geometry vector saves three serial-interpreter surfaces
and compares the threaded device with them byte for byte. This checks a
race-sensitive result without treating an unsynchronized pixel as an oracle.

Hazard-range vectors submit captured raster state directly. A pixel read
and `cce_batch_pending` then expose whether the dependency flush occurs.
No GUI register changes between the two draws of a leg: a register write
in the GUI block drains the engine and would flush the batch independently.
Textured output carries a marker distinct from the seeded target, making
a rendered pixel distinguishable from untouched storage.

The CCE-ring vectors fetch harness-owned guest RAM through the production
PCI-GART walk. They check primitive counts, retirement, draw inhibits,
indirect buffers, and executor synchronization within this regression tool.

## Sharded gate

`shard.sh` uses the shared driver one directory up. Without a process count
it detects the available CPUs; a count and optional `-v` select concurrency
and verbose mode.

```sh
./shard.sh
R128_JIT=0 R128_GPU=0 ./shard.sh 8
R128_JIT=verify ./shard.sh 8
R128_GPU=verify ./shard.sh 8
./reg_harness --list-groups
R128_GROUPS=agp-oracle,lanegap ./reg_harness
```

The selector preserves device-owning units and their check order. Unset
selects the ordinary full run; an empty string selects none. Unknown units
and empty list elements are errors. `--list-groups` reports active units,
including optional modes enabled by the environment.

Units include `gpu-tuple-reload`, `atomic-pace`, `inline-pace`, `agp-oracle`,
`agp-16mb`, `pci-xpert128`, `pci-variant`, `monid-mask`, `amcgpio`, `maxx`,
`vga-decode`, `lanegap`, `lanegap-16mb`, `gpu2d-verify`, `x64legs`,
`synctel`, `yuv-border`, `texture-alpha`, `dither-init`, `sec-persp`,
`sec-only`, `dst-1555-4444`, `zoff-tfog`, `stipple-fast`, and the optional
`lanegap-expectedfail`.

A unit retains each device's registers, buffers, counters, and close-time
verification. `agp-oracle` selects both the serial reference device and its
threaded comparison device. The 16 MB dirty-alias unit explicitly sets a
nonzero repaint generation for its dirty-page assertions.

The driver distributes active units round-robin. It inherits lane settings,
including `R128_JIT`, `R128_GPU`, `R128_GPU_2D`, and `VK_ICD_FILENAMES`.
Each child must exit successfully and produce one final summary. The driver
prints diagnostics and mismatch counters, then summed `pass=` / `fail=`
totals. Logs and child statuses remain in the temporary directory announced
on stderr, or a fresh directory selected by `SHARD_LOG_DIR`. Separate child
working directories isolate temporary probe outputs.

The shared driver supports macOS bash 3.2 and ordinary awk, including MSYS2.
CPU detection tries `sysctl`, `nproc`, `getconf`, and the Windows processor
count. Extra processes beyond the unit count form empty successful shards.
The largest indivisible unit limits speed. GPU sharding also initializes
multiple Vulkan backends on one device; an unsharded GPU run usually makes
the clearer gate.
