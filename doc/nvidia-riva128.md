# The NVIDIA RIVA 128 (NV3) in 86Box-Next

This document explains how 86Box-Next emulates the NVIDIA RIVA 128, what works, how the pieces
fit together, and how it was made to work with NVIDIA's own Windows 98 drivers. It is written
for people: someone who wants to use the card, someone who wants to understand the code, or
someone who picks the work up later. File names are relative to the repository root.

- [1. What you get](#1-what-you-get)
- [2. The card in brief](#2-the-card-in-brief)
- [3. How the emulation is organised](#3-how-the-emulation-is-organised)
- [4. From the CPU to the screen: how a command travels](#4-from-the-cpu-to-the-screen-how-a-command-travels)
- [5. 2D drawing](#5-2d-drawing)
- [6. 3D: the Direct3D triangle](#6-3d-the-direct3d-triangle)
- [7. The display: CRTC, RAMDAC, cursor, clocks](#7-the-display-crtc-ramdac-cursor-clocks)
- [8. The video overlay](#8-the-video-overlay)
- [9. Memory sizes, variants and BIOSes](#9-memory-sizes-variants-and-bioses)
- [10. Working with NVIDIA's drivers](#10-working-with-nvidias-drivers)
- [11. What has been tested](#11-what-has-been-tested)
- [12. Known limitations and open questions](#12-known-limitations-and-open-questions)
- [13. Debugging and test tools](#13-debugging-and-test-tools)
- [14. The bugs that mattered](#14-the-bugs-that-mattered)
- [15. References](#15-references)

---

## 1. What you get

Pick **"nVIDIA RIVA 128 (NV3) PCI"** in Settings > Display. With NVIDIA's Windows 98 driver
(the rig uses version 4.11.01.0337, NV3DISP.DRV / NV3RM.VXD / NV3DD32.DLL / NV3OGL.DLL) you get:

- **The Windows desktop** at 640x480 up to 1280x1024 in 8, 16 and 32 bits per pixel, with
  resolution and colour-depth changes in both directions without restarting, and the hardware
  mouse cursor.
- **2D acceleration** for everything the display driver draws: rectangles, blits, text,
  lines, patterns, image uploads, stretched images, reading pixels back.
- **DirectDraw**: dxdiag's DirectDraw tests pass (windowed, full screen, page flipping).
- **Direct3D** (DirectX 5-8 era, through the card's one 3D object): dxdiag's Direct3D 7 and 8
  tests, 3DMark 99 MAX from start to finish (about 1465 3DMarks on the Pentium 233 MMX rig),
  with textures, mipmaps, bilinear filtering, fog, alpha blending and a depth buffer.
- **OpenGL** through NVIDIA's OpenGL driver (NV3OGL.DLL): all of Windows 98's 3D
  screensavers (Pipes, Maze, Text, Flower Box, Flying Objects) render on the card.
- **The video overlay** that media players use: YUY2 and UYVY video windows, scaled up or
  down with filtering, with colour-key cut-outs.
- **2 MB and 4 MB** memory configurations (Settings > Display > Configure).

## 2. The card in brief

The RIVA 128 (1997) put a VGA core, a 2D engine, a Direct3D triangle engine and a video
scaler on one chip with a 128-bit memory bus. Software does not poke drawing registers
directly. Instead it talks to **objects**:

- An object is an instance of a **class**: "rectangle", "blit", "image from CPU",
  "Direct3D textured triangle", and so on. Its settings live in a small block of on-card
  memory called **instance memory** (RAMIN).
- Software owns up to 128 **channels** (one per program, roughly); each channel has 8
  **subchannels**. Writing method 0 of a subchannel with an object's **handle** binds that
  object to the subchannel; every other write is a **method** call on the bound object
  (method number = register offset, the value = its argument).
- Writes go into a **FIFO** (PFIFO). Its puller looks the handle up in a **hash table**
  (RAMHT, also in instance memory), loads the object's context, and hands each method to
  the graphics engine (**PGRAPH**), which draws.
- Instead of the CPU writing every method, a channel can point the card at a **push
  buffer** in system memory; the **DMA pusher** fetches the commands itself. Direct3D uses
  this.
- When the hardware cannot do something itself (an unknown handle, a "software" object, a
  method it does not implement) it raises an interrupt and NVIDIA's **resource manager**
  (NV3RM.VXD, "the resman") finishes the job in software. Emulating a RIVA 128 is largely
  about producing exactly the interrupts and register values the resman expects.

The chip's register blocks, all in PCI BAR0 (16 MB of MMIO):

| Block | Address | Job |
|---|---|---|
| PMC | 0x000000 | chip ID, interrupt summary, enabling blocks |
| PBUS | 0x001000 | PCI configuration mirror, bus interrupts |
| PFIFO | 0x002000 | the command FIFOs, RAMHT/RAMFC/RAMRO setup, DMA pusher |
| PTIMER | 0x009000 | a 56-bit nanosecond timer and its alarm interrupt |
| PFB | 0x100000 | framebuffer configuration, memory size |
| PSTRAPS | 0x101000 | board straps (crystal, bus, BIOS) |
| PGRAPH | 0x400000 | the 2D/3D engine's state, its interrupts and traps |
| PVIDEO | 0x680100 | the video overlay |
| PRAMDAC | 0x680300 | clocks (PLLs), cursor, palette, DAC |
| USER | 0x800000 | the channels' method windows (channel x 64 KB, subchannel x 8 KB) |

PCI BAR1 (16 MB) is the **linear framebuffer**: VRAM at the start (mirrored), and the
instance memory window at +12 MB.

## 3. How the emulation is organised

All the code is in `src/video/nv/` with the header `src/include/86box/nv/vid_nv3.h`.

```
src/video/nv/
  nv_base.c, nv_rivatimer.c         shared NVIDIA helpers: logging, a precise timer
  nv3/
    nv3_core.c                      the device: PCI/AGP config, BARs, mappings, VGA hooks,
                                    init, the debug hook ("dev nv3 ...")
    nv3_core_arbiter.c              routes BAR0 accesses to the right block (and traces them)
    nv3_core_config.c               the Settings > Configure options (BIOS, VRAM, revision)
    subsystems/                     one file per register block:
      nv3_pmc.c  nv3_pbus.c  nv3_pbus_dma.c  nv3_pfifo.c  nv3_pfb.c  nv3_pgraph.c
      nv3_pme.c  nv3_pramdac.c  nv3_pramin.c  nv3_pstraps.c  nv3_ptimer.c  nv3_pvideo.c
      nv3_user.c
    classes/                        one file per object class (methods -> state)
      nv3_class_001_beta_factor.c ... nv3_class_01c_image_in_memory.c
    render/                         the actual drawing
      nv3_render_core.c             pixel pipeline: formats, ROPs, clipping, dither
      nv3_render_primitives.c       rectangles, lines, triangles, text
      nv3_render_blit.c             blits, image and bitmap uploads, stretching
      nv3_render_d3d.c              the Direct3D triangle and Z-point classes
    NV3-DRIVER-NOTES.md             what the drivers expect (reverse-engineering notes)
```

The VGA part (text modes, the BIOS, standard VGA registers) is 86Box's common SVGA core
(`src/video/vid_svga.c`); the NV3 code adds the extended CRTC registers, its own scanout
settings, the hardware cursor and the overlay through the core's hooks.

The device is created in `nv3_init()`: it loads the chosen BIOS, sets up the PCI or AGP
config space, maps BAR0/BAR1 and the VGA ranges, and initialises the blocks (straps, PMC,
PFB, RAMDAC, PGRAPH). Everything after that is driven by register writes from the guest.

## 4. From the CPU to the screen: how a command travels

### 4.1 Into the FIFO

A driver writes, say, `0x00010000` to `USER + channel*0x10000 + subchannel*0x2000 + 0x304`.
`nv3_user.c` turns that into a FIFO entry (channel, subchannel, method 0x304, data) and
pushes it into **CACHE1** (`nv3_pfifo_cache1_push`). CACHE1 holds 32 entries on the original
chip, readable at 0x3300; the ZX has 64 at 0x3400. Its PUT and GET registers are Gray-coded
entry pointers: 5 bits on the original chip; 7 bits on the ZX, one more than its 64 entries
need, so that a full ring differs from an empty one. If the entry belongs to another channel than the one CACHE1 is serving,
PFIFO switches channels first: it saves the old channel's eight subchannel contexts and
CACHE1 pointers into **RAMFC** and loads the new one's (`nv3_pfifo_context_switch`).

**CACHE0** is a one-entry side cache the resman uses to inject methods itself.

The driver checks the free space before each burst by reading `USER + 0x10` of the
subchannel; the emulation reports CACHE1's free entries there.

### 4.2 The puller and RAMHT

The puller (`nv3_pfifo_cache1_pull`) takes entries in order:

- **Method 0 (bind)**: the handle is hashed — `((h ^ h>>8 ^ h>>16 ^ h>>24) & 0xFF) ^ channel`
  — and looked up in RAMHT (`nv3_ramin_find_object`, `nv3_pramin.c`). RAMHT is 4, 8 or 16 KB
  of 8-byte entries (handle, context) in buckets of 2, 4 or 8. A hit loads the object's
  **context** (class, instance address, flags) into the subchannel. A miss raises the PFIFO
  CACHE_ERROR interrupt (hash failure) and stops the puller; the resman deals with it.
- **Other methods**: if the subchannel's object is a hardware object (context bit 23 set)
  the method goes to PGRAPH (`nv3_pgraph_submit`). If it is a software object, the puller
  stops with CACHE_ERROR and the resman executes the method itself, then restarts the puller.
- RAMRO ("runout") records methods that could not be queued (FIFO overflow, protection
  errors) for the resman to inspect.

Instance memory is the top of VRAM read backwards in 16-byte units: instance address `x` is
VRAM address `x XOR (VRAM size - 16)`. The resman sizes it at mode set — whatever VRAM the
screen does not need, up to 64 KB, plus a fixed 32 KB — and lays out RAMHT, RAMRO, RAMFC and
an allocation heap inside it.

### 4.3 The DMA pusher

For a push buffer the driver writes the buffer's page table address, a start (GET) and a
length into CACHE1's DMA registers and enables the pusher (`nv3_pfifo_trigger_dma_if_required`).
The pusher reads dwords through the page table: a **header** word gives the method,
subchannel and count (`count<<18 | subchannel<<13 | method`), followed by that many data words
for successive methods; it pauses when CACHE1 is full and resumes as the puller drains it.
The pusher runs a moment after it is kicked, as the hardware's fetches are asynchronous (the
D3D driver arms its completion notifier just after kicking). NV3DD32.DLL double-buffers a
~256 KB push buffer this way.

### 4.4 PGRAPH and the classes

`nv3_pgraph_submit` looks at the object's class and calls that class's method handler
(`classes/`). A handler stores state (colours, points, sizes, formats) and, when a method
completes a primitive, calls the renderer. Each object's context also carries:

- the **colour format** of the data it receives (R5G6B5, X1R5G5B5, R8G8B8, Y8, YUV ...),
- the **patch** it belongs to: which ROP, beta (blend factor), chroma key, clip rectangle,
  pattern and destination surfaces apply,
- which of the four **surfaces** (three colour buffers and the depth buffer) it writes.

Methods the engine does not know, or that need software help (notifications, some
configuration methods), raise PGRAPH interrupts with the method and data latched in
TRAPPED_ADDR/TRAPPED_DATA; the resman's interrupt handler reads those and finishes the job.

**Notifiers**: method 0x104 asks for a notification. When the operation completes, the card
writes a 16-byte status record into a DMA object (`nv3_write_notifier`, `nv3_pbus_dma.c`)
and/or raises an interrupt. Drivers wait on these: GetPixel, the D3D driver's buffer
recycling and screen-to-memory copies all spin on a notifier.

**DMA objects** describe memory the engine may touch outside VRAM: a target (VRAM, PCI or
AGP), a limit and a page table. `nv3_dma_map_pages` resolves them for the M2MF, image and
texture paths.

### 4.5 Interrupts

Each block keeps its own pending and enable bits; PMC ORs them into the PCI interrupt
(`nv3_pmc_handle_interrupts`). The resman's handlers are strict about the order and the
register values they read back, which is why several parts of the emulation follow its
code rather than the documentation (see [section 10](#10-working-with-nvidias-drivers)).

## 5. 2D drawing

All 2D drawing ends in `nv3_render_pixel_ex` (`nv3_render_core.c`), which takes one pixel
through the full pipeline:

1. **Clipping**: the destination canvas, the user clip rectangle (class 0x05), and the
   clip of image classes.
2. **Source colour** converted from the object's format into the engine's internal 10 bits
   per channel.
3. **Raster operation**: the patch's operation picks between plain source, the 256 ROP3s
   (source/pattern/destination on bit vectors, class 0x02), and **beta blending** (class
   0x01, source × beta + destination × (1 − beta)). The pattern (class 0x06) is an 8x8
   monochrome or colour pattern.
4. **Chroma key** (class 0x03): source pixels of the key colour are skipped.
5. **Dither** from 10 to 5 bits (the hardware's ordered dither) when writing 15/16-bit
   surfaces.
6. Written to every enabled destination surface in its format (8, 16 or 32 bits).

The classes and what they do:

| Class | Name | Notes |
|---|---|---|
| 0x01 | Beta factor | blend factor for beta blending |
| 0x02 | ROP | the ternary raster operation |
| 0x03 | Chroma key | transparent colour |
| 0x04 | Plane mask | accepted; does nothing on NV3 |
| 0x05 | Clipping rectangle | user clip |
| 0x06 | Pattern | 8x8 mono/colour pattern, shapes |
| 0x07 | Rectangle | solid rectangles (many per call) |
| 0x08 | Point | single pixels |
| 0x09 / 0x0A | Line / Lin | lines, polylines, colour polylines (Lin leaves out the last pixel) |
| 0x0B | Triangle | flat-shaded 2D triangles |
| 0x0C | GDI text | Windows' text: types A (rectangles), B (clipped rectangles), C, D, E (1 bpp glyphs, transparent or two-colour) |
| 0x0D | M2MF | memory-to-memory copies between DMA objects (screen read-back, uploads) |
| 0x0E | Scaled image from memory | scaling with YUV sources (used for video stretch) |
| 0x10 | Blit | screen-to-screen copies |
| 0x11 | Image from CPU | colour image uploads |
| 0x12 | Bitmap | monochrome image uploads |
| 0x14 | Image to memory | screen-to-memory copies |
| 0x15 | Stretched image from CPU | scaled uploads |
| 0x17 | Direct3D triangle | see section 6 |
| 0x18 | Z point | pixels with a depth test |
| 0x1C | Image in memory | surface descriptions (offset, pitch, format) |

## 6. 3D: the Direct3D triangle

The RIVA 128 has one 3D object: class 0x17, "Direct3D 5 textured triangle with zeta buffer"
(`nv3_render_d3d.c`). Its state, set by NV3DD32.DLL (and by NV3OGL.DLL for OpenGL) before
every batch:

| Method | Meaning |
|---|---|
| 0x304 | texture offset in the texture DMA object |
| 0x308 | texture format: colour key, A1R5G5B5 / X1R5G5B5 / A4R4G4B4 / R5G6B5, largest and smallest mip level |
| 0x30C | filter word (level-of-detail bias, kernel size; NVIDIA's driver sends one fixed value) |
| 0x310 | fog colour |
| 0x314 | config: filtering, wrap modes, texture blend, culling, depth function, depth/colour write rules, blend factors |
| 0x318 | alpha test function and reference |
| 0x1000+ | vertices: 8 words each (fog + triangle indices, colour, x, y, z, 1/w, u, v) |

Vertices go into 16 slots; the first word of each vertex names its slot and, in the nibbles
above, up to two triangles to draw from slots already filled. So a strip or a list costs one
vertex per triangle.

Each triangle is rasterised in software:

- **Coverage** with barycentric weights in double precision (multipass effects redraw the
  same plane with different triangles and compare with Z EQUAL; single precision broke them)
  and the top-left fill rule.
- **Perspective-correct** colour, texture coordinates and fog.
- **Textures** read through the texture DMA object, stored swizzled (Morton order) as
  NVIDIA's driver writes them; wrap, mirror and clamp per axis; point sampling or bilinear
  (config bits 1:0 = 0 or 2); **mipmapping per pixel** from the screen-space derivatives of
  the texture coordinates.
- **Texture blend**: modulate texture by vertex colour; in the mode NVIDIA's drivers use for
  "modulate", the alpha comes from the texture alone. Colour keys make texels transparent.
- **Fog**: the per-vertex fog byte is the amount of fog colour (0 = none).
- **Depth**: 16-bit zeta buffer holding z × 65535 (0 = near), with the eight D3D compare
  functions as the driver passes them, separate rules for depth and colour writes, and the
  alpha test.
- **Blending**: source × beta + destination × (1 − beta), with beta the source alpha or the
  destination colour, either side zeroed or added — enough for D3D's alpha blending,
  additive light effects and multiply (lightmap) passes.
- **Dither** to the 15/16-bit surface.

Class 0x18 (Z point) draws single pixels with a depth test; drivers use it for clears and
small sprites.

Hardware limits match the real chip: 16-bit colour and 16-bit depth only, no stencil, one
texture per pass, no trilinear filtering. In a 32-bit desktop the drivers fall back to
software rendering (Microsoft's OpenGL, Direct3D's emulation) and blit the result, which also
works.

## 7. The display: CRTC, RAMDAC, cursor, clocks

- **Scanout** is the SVGA core's, driven by the NV3's extended CRTC registers
  (`nv3_recalc_timings`): the start address extends into CR19 (page flips are just start
  address writes), the row offset into CR19's top bits, the vertical and horizontal
  extension bits into CR25/CR2D, and CR28 picks 8, 15/16 or 32 bits per pixel. 16-bit modes
  are X1R5G5B5 unless the RAMDAC selects R5G6B5.
- **Hardware cursor**: 32x32 A1R5G5B5 from instance memory (CR30/CR31), positioned by the
  RAMDAC.
- **Clocks**: the RAMDAC's three PLLs (`nv3_pramdac.c`): the pixel clock (refresh rate), the
  memory clock and the core clock, each `crystal × N / (M × 2^P)` with the crystal from the
  straps (13.5 or 14.318 MHz). The core clock paces PTIMER and the FIFO pullers.
- **Monitor detection**: DDC over the CRTC's I2C pins (CR3E/CR3F), so Windows sees a Plug and
  Play monitor.
- **Vertical blank** raises the PGRAPH VBLANK interrupt and is when the overlay changes
  buffers.

## 8. The video overlay

The RIVA 128 can show a YUV video window on top of the desktop, scaled by hardware. NVIDIA's
DirectDraw driver offers it as one overlay surface (YUY2, UYVY and some other FOURCCs, stretch
from 0.06x to 20x). The resman programs it (`nv3_pvideo.c` implements it; the register map
came from the resman's code and is in `NV3-DRIVER-NOTES.md`):

- two **buffers** (start and pitch each), the **scale** (source step per screen pixel in
  1/2048), the **position and size** on screen, the **format** (YUY2 or UYVY) and the
  **destination colour key**;
- a **handshake**: the driver marks a buffer ready by toggling a bit; at the next vertical
  blank the hardware takes it, toggles its own status and notify bits and raises the PVIDEO
  interrupt; the driver's handler acknowledges and tells DirectDraw the flip is done.

On screen, each line of the overlay is drawn through the SVGA core's overlay hook: 4:2:2
samples interpolated in both directions, converted with the BT.601 matrix, and drawn only
where the desktop pixel equals the key colour when keying is on.

## 9. Memory sizes, variants and BIOSes

- **RIVA 128, PCI** (`nv3_pci`), 4 MB (the usual card) or 2 MB (NEC G7AGK). The 2 MB card
  reports its size in PFB_BOOT and has a 2 MB framebuffer window.
- BIOS choices (Settings > Display > Configure): Diamond Viper V330 1.62, ASUS AGP/3DP-V3000
  1.51B, ELSA VICTORY Erazor 1.47/1.54/1.55, STB Velocity 128 1.60/1.82. The ROMs come from
  86Box's ROM set (`roms/video/nvidia/nv3/`).
- The chip revision (A, B, C) is selectable; revision C is the RIVA 128 ZX.
- **RIVA 128 ZX, PCI** (`nv3t_pci`): 8 MB, revision C, 64-entry FIFO. BIOS choices: STB
  Velocity 128 ZX 1.20, ASUS AGP-V3000 ZX, AGP-300S, Chaintech AGP-RI20, Creative CT6730, NVIDIA's
  reference BIOS and ELSA VICTORY Erazor/LT. With NVIDIA's Windows 98 driver it reaches the desktop
  (8/16 bit, up to 1024x768 tried) and passes dxdiag's Direct3D 7 and 8 tests.
- The **AGP** variants (`nv3_agp`, `nv3t_agp`) are in the list but untested.

## 10. Working with NVIDIA's drivers

The resman is the real specification of this card: it reads registers back, checks
interrupt causes and sometimes reads the FIFO's internals. The emulation was brought up by
running the drivers, finding where they went wrong, and reading their code (NV3RM.VXD,
NV3DISP.DRV and NV3DD32.DLL were disassembled). The notes are in
`src/video/nv/nv3/NV3-DRIVER-NOTES.md`; the main lessons:

- **Lazy objects.** After every mode set the resman empties RAMHT and re-creates objects only
  when used: a bind misses, the resman records the handle; the next method on that subchannel
  traps as a software method; the resman then writes the object into instance memory and
  RAMHT and leaves the method queued for the hardware to re-execute. The FIFO must stop and
  restart at exactly the right entry for this to work.
- **The resman reads CACHE1's subchannel contexts back** (0x3280-0x32F0) to learn what is
  bound where. A wrong read-back desynchronises it from the display driver; that was the
  "black screen / hang when changing resolution" bug.
- **Notifier timing.** Drivers arm a notifier and then kick the work, so the work must not
  complete before the notifier is armed.
- **The push buffer's GET already includes the DMA object's offset**; adding it again made
  the pusher read 0x80 bytes into a command.
- **The overlay must change buffers only at vertical blank**; doing it at once made the
  resman's handler re-submit and re-interrupt forever.

## 11. What has been tested

All on Windows 98 SE, a Pentium 233 MMX, with NVIDIA's 4.11.01.0337 driver:

| Test | Result |
|---|---|
| Desktop 640x480 to 1280x1024, 8/16/32 bpp, switching both ways | works |
| dxdiag DirectDraw tests | all pass |
| dxdiag Direct3D 7 and 8 tests | textured cube renders |
| 3DMark 99 MAX, default benchmark | runs through, ~1465 3DMarks |
| 3DMark 99 Race, First Person, fill rate, texture, filtering, polygon tests | render correctly |
| OpenGL screensavers (5) | render on the card (16 bpp); software fallback at 32 bpp |
| DirectDraw overlay (YUY2, scaled, 1:1, colour-keyed) | correct (test program in `tools/nv3/ovltest`) |
| 2 MB configuration | boots, DirectDraw tests pass |

The RIVA 128 ZX (8 MB) on Windows 98 SE: desktop at 640x480 and 1024x768 (8/16 bpp), dxdiag
Direct3D 7 and 8 cubes. On Windows 2000 SP4 with its inbox NVIDIA driver (`nv3.dll`/`nv3.sys`
3.43), both cards reach the desktop at 1024x768x32 with 2D acceleration and pass dxdiag's
DirectDraw tests.

## 12. Known limitations and open questions

- Not compared with a real card: exact filtering, blending rounding, dithering and the
  meaning of some 3D config bits (one texture-blend mode was inferred from the drivers'
  behaviour).
- 3DMark's texture-filtering tunnel shows a hatched band: its second pass needs Z EQUAL on
  geometry whose depth differs from the first pass by 1-3 units in the program's own data.
  Probably the same on real hardware.
- The overlay's interlaced (bob) mode and planar formats are not exercised.
- The few pixels of the overscan border right of the picture can hold stray pixels; they are
  only visible with "Show overscan".
- Speed: the 3D rasteriser is software and per pixel; fine for the era's resolutions.
- The AGP variants are untested; the ZX has had less testing than the RIVA 128 (section 9).
- Windows 2000 has no Direct3D for these cards: its driver (NVIDIA's "RIVA 128/RIVA 128 ZX
  Windows 2000 Display driver" 3.43) fills in DirectDraw only -- no `DDCAPS_3D`, no D3D HAL
  pointers in `DD_HALINFO`, and its GetDriverInfo answers only GUID_MiscellaneousCallbacks.
  dxdiag shows "Direct3D Acceleration: Not Available" and its Direct3D tests run in software,
  as on a real card. Its OpenGL ICD (`nv3oglnt.dll`) is not part of Windows 2000.

## 13. Debugging and test tools

- **`dev nv3 ...`** (through 86Box-Next's debug command file, `BOX86NEXT_DEBUG_CMD`): a state
  dump; traces of MMIO accesses, FIFO software methods and RAMHT binds, M2MF transfers,
  notifiers, push buffer headers, Direct3D triangles, texture mip levels; the last 256
  methods; VRAM/RAMIN write watches.
- Other debug commands: key presses and typing, screenshots, CPU state, linear-address peeks,
  breakpoints and write watchpoints, forcing the CPU interpreter.
- **`tools/nv3/`**: scripts that drive a private copy of the test rig (start, type, screenshot,
  change resolution, run dxdiag, 3DMark, the OpenGL screensavers, the overlay test), helpers
  to disassemble the drivers, and `ovltest`, a small DirectDraw overlay test program for
  Windows 98.
- `handoffnvidia.md` at the repository root is the working log: state, rules, open items.

## 14. The bugs that mattered

A short history of what made the difference, for anyone wondering why the code does what it
does:

| Symptom | Cause |
|---|---|
| Hang / black screen on resolution change | CACHE1 context register 0x32F0 read back wrong; the resman lost track of subchannel 7 |
| dxdiag Direct3D test black | push buffer GET got the DMA offset added twice; fog byte inverted |
| 3DMark geometry black | alpha must come from the texture in the drivers' modulate mode |
| 3DMark sky drawn over the track | depth stored inverted; the drivers' compare functions need 0 = near |
| Blurry/aliased textures | one mip level per triangle; now per pixel |
| Overlay froze the guest | buffers handed over at once instead of at vertical blank |
| 2 MB option crashed the emulator | the BAR layout only knew 4 and 8 MB |

## 15. References

- **starfrost (Connor Hyde)'s NV3 emulation for 86Box**, which this builds on: the device,
  its subsystem and class structure, much of the register handling.
  <https://github.com/starfrost013/86Box> (branch `nv3_reintegration`).
- **envytools**: the rnndb register database (especially `rnndb/graph/nv3_pgraph.xml`,
  `rnndb/graph/nv3_3d.xml` and `nv_vga.xml`), hardware tests and the nvhw reference code the 2D
  pixel pipeline is modelled on. <https://github.com/envytools/envytools>
- **nvwiki**: NV3 memory mapping, configuration and hardware errata. <https://nvwiki.org>
- The NVIDIA RIVA 128 datasheet (straps, subsystem IDs).
- Linux's rivafb driver (`drivers/video/fbdev/riva/riva_hw.c`, `fbdev.c`) for the CRTC and PLL
  setup. <https://github.com/torvalds/linux/tree/master/drivers/video/fbdev/riva>
- Ralf Brown's Interrupt List, VGA CRTC port pages, for the extended CRTC registers.
  <https://fd.lod.bz/rbil/ports/other/p03c403c5.html>
- NVIDIA's drivers, read in a disassembler (nothing copied): Windows 98 4.11.01.0337
  (NV3RM.VXD, NV3DISP.DRV, NV3DD32.DLL) and Windows 2000's inbox nv3.dll / nv3.sys 3.43.
- 86Box, whose SVGA, PCI and device frameworks the card plugs into. <https://github.com/86Box/86Box>
