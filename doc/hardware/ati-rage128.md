# ATI Rage 128 graphics cards

Three devices are available in the Display settings:

| Device | Configuration name | Bus | Chip | Video memory |
| --- | --- | --- | --- | --- |
| ATI Xpert 128 PCI | `ati_xpert128_pci` | PCI | Rage 128 GL, PCI id 1002:5245 | 16 MB or 32 MB |
| ATI Rage Fury Pro AGP | `ati_rage128_pro` | AGP | Rage 128 Pro, PCI id 1002:5046 | 16 MB or 32 MB |
| ATI Rage Fury MAXX AGP | `ati_rage128_maxx` | AGP | two Rage 128 Pro chips, each 1002:5046 | 16 MB or 32 MB per chip |

The three boards share one option table: memory size (default 32 MB),
render threads (1, 2, 3, 4, 6 or 8, default 2), realtime pacing (off by
default), the span recompiler (on by default, offered only on arm64 and
x86-64 hosts) and the GPU raster backend (off by default, offered only on
builds with the Vulkan headers). A Rage Fury Pro or Xpert 128 added as a
second video card is strapped as "not the system VGA controller": it
decodes no legacy VGA I/O or memory and the driver reaches it through its
PCI apertures only. The MAXX's first chip always decodes VGA.

## Supplied ROMs

Place the files under `video/ati_rage128/` in an 86Box ROM directory:

| Card | Filename | Dump size | SHA-256 | Subsystem id |
| --- | --- | --- | --- | --- |
| ATI Xpert 128 PCI | `Rage128gl_xpert128pci_zerostate.VBI` | 36864 bytes | `81a6e7807a3b7c03bbdd6b65ddcb5764bf00838e06026dd7f9384c451f994207` | 1002:0008 |
| ATI Rage Fury Pro AGP | `Rage128progl_zerostate.VBI` | 49152 bytes | `30728a143bc6fd8069eed50993463c4f0fa7d8289a241700541c054fa321e407` | 1002:0018 |
| ATI Rage Fury MAXX AGP | `FuryMAXX_zerostate.VBI` | 34816 bytes | `bacc8f370075256421a771f85543b954b529e974ed2ec4ba1470988da2f3957a` | 1002:2000 |

The 86Box/roms repository records where each image came from. The AGP
image is a Rage 128 Pro GL board, part number 113-63211-100, dumped from
a real card. The PCI image is a Rage 128 GL board, part number
113-57403-102, BIOS version BK1.0.13, from the theretroweb "ATI RAGE 128
GL PCI" BIOS dump. The MAXX image is part number 113-67308-100, from the
vgamuseum Rage Fury MAXX BIOS dump. All three are shadow-RAM copies taken
after the BIOS had run, restored to power-on state: the words the BIOS
writes into its own image at POST (the card's bus, device and function,
its ROM segment and its I/O base, and on the MAXX the second chip's bus,
device and function) are zeroed and the checksum byte is corrected. With
those words zero the BIOS takes its first-run path, which asks the PCI
BIOS for the card's I/O base. The supplied files are used unchanged; the
ROM binaries are not part of the source patch.

After PCI reset the chip loads its subsystem vendor and id from bytes
0x70 to 0x73 of the ROM (the Chip Specification, "ROM Based Straps"), so
each board reports the id its image holds. ATI's Windows 98 and Windows
2000 drivers list 1002:0018 as "Rage Fury Pro/Xpert 2000 Pro" and
1002:0008 as "Xpert 128". Each image sits behind the PCI expansion ROM
BAR in a 64 KB window padded with 0xff, mapped by the system BIOS during
POST.

The MAXX puts two chips on one AGP board. On AGP a chip's IDSEL strap
picks AD16 or AD17, that is device 0 or 1 on the AGP bus (the Chip
Specification, "External Straps"); the board is modeled with chip A as
device 0 and chip B as device 1. Since a machine registers one AGP slot,
the board registers a sibling slot at the next device number on the same
bus (an addition to `src/pci.c`), and chip B takes it. Chip A is the
system VGA and has the one BIOS image behind its expansion ROM BAR; chip
B is strapped as an extended-mode-only controller with no ROM, so the
board model supplies its subsystem id, 1002:2001, the id ATI's Windows
98 INF binds as the secondary device.

## Implementation

The device lives in `src/video/vid_ati_rage128*.c` with its headers in
`src/include/86box/vid_ati_rage128*.h`, one file per functional block:
the device shell, the legacy VGA path, the display block, surface
access, the overlay, the 2D engine, the command processor, the 3D
engine, the parallel rasterizer, the span recompiler, the GPU backend
and its four compute kernels, the MPEG block and the MAXX pairing, plus
two diagnostic files (a draw-state census and fence telemetry). Register
behavior follows the RAGE 128 PRO Register Reference Guide, the RAGE 128
Software Development Guide, the RAGE 128 VR / RAGE 128 GL Graphics
Controller Specifications (the chip specification, for the board straps
and the block overviews) and the two 1999 register supplements
(Multimedia Registers, and Registers for CCE 3D Packets). Where those
leave a datapath detail out, the sources say which earlier document (the
RAGE PRO Programmer's Guide) or which open-source driver supplied it:
the Linux r128 DRM, Mesa's r128 driver and xf86-video-r128, the XFree86
driver. Behavior that no public document describes is marked "modeled"
or "not documented" in the source and below.

`vid_ati_rage128.c` is the device shell: PCI configuration space, the
three base address registers (a 64 MB linear framebuffer aperture whose
lower half is local memory and whose upper half is the AGP window, a
256-byte I/O register block and a 16 KB memory-mapped register block),
the AGP and power management capability blocks (the PCI board lists only
power management), the expansion ROM BAR and the ROM straps, chip reset,
the dispatch that hands each register offset to the block that owns it,
the interrupt line, and the device definitions and options. Interrupts
are raised on INTA for vertical blank, vertical sync, the programmed
scan line, the snapshot and engine idle; the engine-idle interrupt is
raised on the engine's busy-to-idle edge, which the guide does not
describe and is modeled.

`vid_ati_rage128_vga.c` is the legacy VGA path. The card answers the
standard VGA ports and the A0000h window as a plain VGA through the
emulator's SVGA core, so the video BIOS POSTs and INT 10h text and
graphics modes render. The two 32 KB halves of the window are paged
independently from the chip's VGA page-select registers, and the chip's
own registers reach into the VGA core only where the guide says they do.
Extended overscan in VGA modes is not modeled.

`vid_ati_rage128_display.c` is the display block: the PLL register file
and clocks (the reference oscillator and dividers come from the PLL
information block of each board's BIOS image, 27 MHz on the AGP boards
and 29.5 MHz on the PCI board), the extended CRTC, the DAC and palette,
the hardware cursor, page flips, scanout and the realtime pacer. In VGA
mode the core's own timings drive the display; in extended mode this
file derives the core's timings, pixel format and scan address from the
chip's CRTC registers, including interlaced and double-scanned modes and
tiled scanout. The 256-entry palette stays in the pixel path at every
depth, which is how gamma ramps reach 15 and 16 bpp modes. The 64x64
hardware cursor uses the image format the Software Development Guide
gives. A page flip through the CRTC offset register is latched at the
flip point, with the offset lock honored. The monitor
DDC channel is bit-banged through the monitor id GPIO register and
serves 86Box's shared monitor EDID, so the driver reads the monitor's
mode list. The video input port is not emulated: its field counters read
zero and automatic snapshots never fire, while a manual snapshot works.
The memory checkerboarding controls are stored and read back only.

`vid_ati_rage128_mem.c` and the bus helpers in `vid_ati_rage128_pm4.c`
give the engines one 64 MB card address space: the lower 32 MB is the
frame buffer and the upper 32 MB is system memory at the AGP base plus
the low 25 bits of the address. Bus mastering is gated by the PCI
command register's bus master enable and by the bus control register's
bus master disable bit, and goes one of two ways, as the driver set it
up: through the chipset's AGP aperture, or through the chip's own PCI
GART, an 8192-entry table of 4 KB pages at the page-table base register
that the PCI board's drivers use and that the Windows drivers also use
for the command buffers. The 3D raster workers run on threads that may
not touch the bus, so a color, depth or texture surface in system
memory, and any tiled 2D window, is copied into a host-side staging
arena on the submitting thread and written back after the work.

`vid_ati_rage128_ov0.c` is the OV0 video overlay: the register latch
model (a shadow set and an active set with the register-load lock
between them) and a per-scanline compositor that scales the YUV or RGB
source window onto the screen under the graphics and video color keys,
with the brightness and saturation controls and the DVD subpicture
blend. The Pro register guide describes only the color conversion
register from this block; the field layouts come from the Multimedia
Registers supplement and from what xf86-video-r128 writes for Xv, and
the subpicture registers, which no public document lays out, are modeled
from what ATI's Windows driver writes. The compositor is a model of the
result, not of the chip's datapath: it takes the nearest source pixel
and line, where the chip filters with two- and four-tap filters. The
filter coefficients, the programmable color conversion matrix, the
deinterlacing pattern and tiled source surfaces are not modeled.

`vid_ati_rage128_2d.c` is the 2D GUI engine, a C interpreter for the 2D
datapath: solid and pattern paints, screen-to-screen and host-data
blits, mono expansion, the scaler and stretch blits, Bresenham lines and
dashed lines, polyscanlines, gradient fills, color compare, the write
mask and the full ROP3 stage. It runs both from direct register writes,
the path the XFree86 driver uses, and from the command processor's
type-3 packets, whose grammar is modeled from captured Windows traffic
where the Software Development Guide leaves it open. The packed YUV
byte order, the YUV to RGB equations and the effect of the destination
tile bits are taken from the RAGE PRO Programmer's Guide, which the
Rage 128 guides leave out, and the source says so at each spot.

`vid_ati_rage128_pm4.c` is the CCE command processor. The drivers submit
2D and 3D work as PM4 packets in a ring buffer in system memory; the
device reads the ring by bus mastering, parses type 0, 1, 2 and 3
packets, follows indirect buffers, and writes its read pointer back to
the host address the driver polls. The written-back pointer is the
retire pointer, where execution is, not the fetch pointer, so a driver
that recycles a command buffer once the pointer has passed it does not
overwrite work not yet drawn. Execution is split across two threads, as
the Voodoo command FIFO is: on every write to the ring's write pointer
the CPU thread copies the new ring dwords into a FIFO inside the device,
and a CCE thread pops, parses and runs them; engine idle and reset, the
busy bits the drivers poll and the engine-idle interrupt come from that
FIFO's state. An indirect-buffer chain that submits itself again is
treated as a hung engine until the driver resets it. The CCE microcode
RAM is not modeled: the drivers' microcode loads are accepted and read
back as zero, and packets run as C code. The write-pointer delay
register is stored but the delay is not modeled, since the fetch happens
at the write itself.

`vid_ati_rage128_3d.c` is the 3D engine: the CCE 3D state registers and
the 3D type-3 packets, the register latch, the vertex walks of the draw
packets (inline vertices and indexed vertex buffers read over the bus),
triangle, line and point setup with exact integer edge functions, and
the per-pixel pipeline: two texture stages (4 and 8 bpp palette, ARGB
1555, RGB 565, ARGB 8888, ARGB 4444, 24-bit RGB, RGB 332, 8-bit gray,
packed YUV 422, AYUV 444 and S3TC texels; the 16-bit pseudocolor type
has no decoder and samples as opaque white), mipmapping with a per-pixel
level of detail, the texture combine and lighting pass, specular, vertex
and table fog, the alpha test, 16-, 24- and 32-bit depth with stencil,
alpha blending, dithering, polygon stipple, chroma key, scissors and
plane masks, into ARGB 1555, RGB 565, ARGB 4444 and ARGB 8888 targets.
Setup driven by CPU writes of the vertex registers is not modeled, since
every driver seen submits 3D work through packets. Every submitted
primitive carries a snapshot of the engine state it was submitted under,
and a flat decode of every register field the rasterizer consumes is
derived from that snapshot; identical decodes compare equal, so the
decode is both the batch's state key and the recompiler's block key.
`vid_ati_rage128_raster.c` spreads the pixel work over the render
threads: every worker walks the whole batch in submission order and
draws only the rows it owns, so the output is bit-exact with the serial
path.

`vid_ati_rage128_mpeg.c` is the MPEG-2 macroblock assist, the IDCT and
motion-compensation back end that ATI's DVD player uses. One type-3
packet carries one macroblock: writes to the four motion-compensation
control registers followed by the run-level coefficients of its six 8x8
blocks; the device expands them, runs the inverse DCT, fetches the
prediction from the frame buffer, adds the two and writes the result
back. It also models the authentication port between the DVD decoder
and the chip. The chip specification gives the block overview and the
register guide only the field bit ranges; what the fields mean, the
packet grammar and the prediction arithmetic are reverse engineered from
the software model inside ATI's Windows 98 motion-compensation driver,
and the source cites the routine for each piece. The inverse DCT is the
double-precision reference transform IEEE 1180 measures against: the
driver's model carries two different integer routines, so neither is
taken as the chip's.

`vid_ati_rage128_maxx.c` is the MAXX board: two chip instances plus the
logic between them. The two chips share one monitor connector, and the
chip documents do not describe the board logic that picks which chip's
CRTC drives it, so it is modeled from what ATI's Windows 98 driver does
with the general-purpose I/O pads of both chips: a flip strobes a pad
to hand the connector to the chip that rendered the frame, and the
driver's inter-chip module locks chip B's raster to chip A's. The switch
is made at the wanted chip's next vertical sync so every frame on the
connector is whole; the two rasters are not phase-locked, so the new
chip starts at its own frame boundary. In the emulator Windows 98
re-POSTs chip B from chip A's image.

## Render lanes

The 3D pixel pipeline has three implementations, kept independent and
bit-exact against one another.

The interpreter is the reference: the C pixel loop in
`vid_ati_rage128_3d.c`, run serially or on the parallel rasterizer. It
handles every state the device models and is the lane the other two are
checked against; whatever they refuse comes back to it.

The span recompiler (`vid_ati_rage128_jit.c` with the emitters in
`vid_ati_rage128_codegen_arm64*.h` and
`vid_ati_rage128_codegen_x86_64*.h`) compiles one host function per 3D
draw state. The function rasterizes one pixel row of a primitive and
must write exactly the bytes the interpreter writes for that row. There
are arm64 and x86-64 emitters, each with a scalar loop and a vector
loop, and texture sampling either emitted inline or reached through a
helper call; the x86-64 emitters need SSE4.1 and the recompiler turns
itself off on a host without it. Blocks live in a cache of 2048
entries keyed by the decoded draw state. The compile gate refuses a
destination outside the four direct-color formats, a depth buffer that
is neither 16-bit with the full depth range nor 32-bit and, on arm64, a
32-bit depth layout whose write mask the instruction set cannot encode;
a refused state is remembered and its rows run on the interpreter. A
draw that reads a tiled texture level the device has not staged untiled
runs on the interpreter without reaching the cache. On any other host the file builds as a stub and
every row runs on the interpreter.

The GPU raster backend (`vid_ati_rage128_gpu.c` and the four compute
kernels beside it) is optional and off by default. Triangle setup still
runs on the CPU; for a draw state the kernels support, the per-row span
seeds are captured into host-visible buffers instead of rasterized, many
draws collect into a segment, and each segment is one dispatch per run
of consecutive draws that share a kernel variant and a texture-combine
tuple. One workgroup owns one framebuffer row and walks its spans in
submission order, so overlapping draws land in draw order. The span
shading kernel has twenty variants (untextured, one or two texture
stages, with and without a level-of-detail computation, destination
blending and a live depth cell), and folded pipelines pin the combine
tuple of a run as constants; a separate pre-pass kernel computes the
depth of every pixel with software binary64 arithmetic so the depth
matches the interpreter's rounded double addition bit for bit; a
read-modify-write kernel runs queued 2D operations under any ROP3 and
write mask; an overlay kernel composes the OV0 source window. Video
memory is imported into Vulkan without a copy. States a kernel does not
support are handed back to the CPU rasterizer, which must then run in
program order, so the backend forces the render thread count to one.

The backend needs Vulkan 1.2 and a physical device; it ranks the devices
it finds (discrete, integrated, virtual, other) and logs each one with
the choice. The library is opened at run time, so a build carries no
Vulkan link and a host without it only gets a log line: on Windows
`vulkan-1.dll`, on Linux `libvulkan.so.1`, on macOS MoltenVK, opened
directly as the driver when a copy is bundled in the application's
Frameworks folder or installed under Homebrew or `/usr/local`, with the
Vulkan loader as the fallback. The per-machine option turns the backend
on, and the Vulkan bring-up is put off until the guest first writes an
acceleration register, since creating the device while the host window
is still being set up has held up the application's event handling on
some hosts. The first run on a given driver compiles the twenty unfolded
pipelines behind a progress dialog, "Preparing Rage 128 graphics
acceleration (first run only)", and then the folded pipelines in the
background on a pool of compile threads with "Compiling Shaders" and a
count in the status bar; a pipeline a title needs that is not yet built
runs on the unfolded kernel until its folded one is ready. The Vulkan
pipeline cache and the learned list of pipelines are saved in the 86Box
global configuration directory as `r128gpu.plcache` and
`r128gpu.tuples`, so later runs start quickly; the pipeline cache is
used only when its vendor, device and cache id match the driver's, and
the list only when its header carries the current format version. With
no Vulkan library, no usable device, or a failure bring-up cannot
recover from, the backend logs why and turns itself off and the CPU
lanes carry on, still on the one render thread the GPU request set.
Some steps are optional: without the device-local VRAM mirror the
kernels address the host import instead.

## Environment switches

Every Rage 128 environment switch the device reads, taken from the
source. All are development switches; none is needed to use the device.

Lane selection:

| Variable | Values | Effect |
| --- | --- | --- |
| `R128_JIT` | `0`, `1`, `verify` | Overrides the recompiler option. `verify` draws every row with the compiled block, rolls its writes back, draws it again with the interpreter and compares the bytes, keeping the interpreter's; it forces one render thread and disables the GPU backend. |
| `R128_GPU` | `0`, `1`, `verify` | Overrides the GPU raster option; any value other than `0` and `verify` means on. `verify` replays each flushed segment through the interpreter, compares the bytes and keeps the interpreter's result. |

Verify modes:

| Variable | Values | Effect |
| --- | --- | --- |
| `R128_GPU_2D` | `verify` | Every queued GPU fill is waited for and checked byte by byte against its color, and the queue paths resolved on the CPU compare their GPU result with the CPU walk. Slow. |
| `R128_CONV_VERIFY` | set to anything | Each scanout line the conversion worker produced is converted again on the render thread and compared; a difference is logged. |

Performance tuning:

| Variable | Values | Effect |
| --- | --- | --- |
| `R128_GPU_DEFER` | `0` | Creates the Vulkan device during device init instead of at the guest's first write to an acceleration register. |
| `R128_GPU_ASYNC` | `0` | Turns off asynchronous present. On by default; off in verify mode. |
| `R128_GPU_FOLD` | `0` | Turns off the folded pipelines: every draw runs the unfolded kernel with its selectors read at run time, and the boot precompile is skipped. |
| `R128_GPU_PRECOMP` | `0`, `1`, `list`, `2`, `full` | Boot precompile policy: `0` skips it, `1` or `list` (the default) builds the learned list, `2` or `full` builds every seeded tuple for every kernel, which on a cold cache has taken hours with some drivers. Anything else means the default. |
| `R128_GPU_PRECOMP_THREADS` | integer | Number of compile threads, taken verbatim and clamped to 1 to 16. The default is the processor count minus two, bounded by free memory and by six. |
| `R128_GPU_DEVICE` | index | Forces the physical device at that index in enumeration order; an index out of range is logged and ignored. |
| `R128_GPU_LOADER` | nonzero | On macOS, uses the Vulkan loader instead of opening MoltenVK directly, to diagnose or work around a problem with the direct open. |
| `R128_PACE_BAND` | percent, 0 to 100 | How far emulated time may fall behind real time before the realtime pacer acts; default 0.5. |

Profiling and telemetry:

| Variable | Values | Effect |
| --- | --- | --- |
| `R128_JIT_PROF` | nonzero integer | Logs the first 32 draw states the compile gate refuses, with their fields, and counts the rows and pixels drawn through compiled blocks. |
| `R128_GPU_PROF` | nonzero | Collects the backend's timing profile (wall time and the capture, sort, record, wait and drain phases) and logs it at close. Also on whenever telemetry is on. |
| `R128_GPU_TELEMETRY` | nonzero | Writes every line the backend logs to `r128-gpu-telemetry.log` in the machine's folder as well, flushed per line, starting with the 86Box version, the host platform and the guest CPU and including the Vulkan device choice, so a report needs no log launch and a hang still leaves everything up to its last line. |
| `R128_GPU_SYNC_TELEMETRY` | nonzero; a value containing `/` is the CSV path | Counts, on the CPU renderer paths, how often a segment-batched GPU backend would have to wait, by class, with one CSV row per vertical blank that counted anything (`r128_synctel.csv` in the working directory by default) and a summary at close. With the GPU backend also on it turns off asynchronous present, present snapshots and the GPU 2D queue, so that run does not behave like a normal one. |
| `R128_MMIO_PROF` | nonzero | Times register reads and writes into the register fields of the pacer's per-window log line. |
| `R128_SLICE_PROF` | nonzero | Times scanout rendering, the SVGA poll and the ring pump into the matching fields of the pacer's per-window log line. |
| `R128_STATE_CENSUS` | nonzero; a value containing `/` is the output path | Records up to 16384 distinct 3D draw states the guest uses (states past that are only counted), how many primitives used it and which lane would take it (the recompiler's verdict and the GPU kernel id), and writes one line per state at close, to `r128_census.txt` in the machine's folder by default; a relative path is taken from the machine's folder too. |
| `R128_MAXX_LOG` | nonzero | On the MAXX, logs the pad writes, connector switches, raster-lock changes and the BIOS scratch register of both chips. |

Capture and dumps:

| Variable | Values | Effect |
| --- | --- | --- |
| `RAGE128_CAPTURE` | output path | Records the device's command stream and bus reads from the start of the session for the offline replay tool; a path ending in `.zst` streams through `zstd`, a multi-chip board writes one tagged file per chip, and an armed tap forces one render thread. See `tests/video/rage128/replay-harness/README.md`. |
| `RAGE128_MCDUMP` | output path | Writes every MPEG macroblock packet of an authenticated session as the device receives it, one line per packet with the key in force; a relative path is taken from the machine's folder. Diagnostic only. |

## Guests and limits

The source records what has been run against the device, not a
guarantee for any title: the video BIOS and DOS programs through INT 10h
and the legacy VGA path; Windows 98 with ATI's driver package (the
display driver, the Direct3D and DirectDraw drivers and the DVD player's
motion-compensation driver); Windows 2000 with ATI's display driver,
miniport and OpenGL driver, Quake III included; the Windows XP driver;
and Linux with the XFree86 r128 driver, the r128 DRM and Mesa's r128
driver under the DRI, including the PCI GART mode the PCI board's
drivers use. The comments in the sources name the driver and the
observed behavior wherever a modeling decision rests on one.

Known limits:

- Timing is not cycle accurate. The device models the busy and idle
  states the drivers poll, not the chip's throughput, and the command
  fetch happens at the write-pointer write.
- The CCE microcode RAM is not modeled; packets run as C code.
- Setup driven by CPU writes of the 3D vertex registers is not modeled.
- The overlay takes the nearest source sample; its filters, color matrix,
  deinterlacing and tiled sources are not modeled.
- The video input port is not emulated, and extended overscan in VGA
  modes and memory checkerboarding are not modeled.
- The 16-bit pseudocolor texture type samples as opaque white.
- The MAXX connector logic and raster lock are modeled from the Windows
  98 driver's use of the pads; the chip documents do not describe them.
- The Rage Fury MAXX is particular about the machine it sits in, as the
  real board was. In 86Box it is developed and verified on the Intel
  440BX machines; on other AGP chipsets it may not come up at all, and
  that is not a bug report for the emulator. It needs the MAXX release of
  ATI's Windows 98 driver (4.12.7942) to drive both chips; the generic
  Rage 128 drivers run one chip.
- The recompiler needs an arm64 host, or an x86-64 host with SSE4.1; the
  GPU backend needs a build with the Vulkan headers and a host with a
  Vulkan 1.2 device.

## Verification

With `BUILD_TESTING=ON`, `tests/README.md` describes the six CTest rows:
`Rage128.Registers.Stock`, `Rage128.Registers.JitVerify` and
`Rage128.Registers.GpuVerify` run the host-side register harness, which
instantiates the device on the host and checks register readback, reset
state and 2D and 3D output against the documentation and the open-source
drivers, in the interpreter lane and with each fast lane verified
against it (the GPU lane also compares the 2D and overlay kernels'
output with the CPU path); `Rage128.JitHost` compiles a fuzzed matrix
of draw states with the host's span emitter and compares every pixel
with a C copy of the interpreter's loop; `Rage128.GpuKernels` runs the
production span-shading and depth pre-pass kernels on the host's Vulkan
device against the same reference, with fault injection on every span
variant; `Rage128.ReplaySynth` replays synthetic command
streams, generated without a guest, through the device sources and
checks their pixels. An
emulator-run test proves that the device is consistent with the
documentation, the drivers and its own other lanes; it does not
establish what real silicon does.
