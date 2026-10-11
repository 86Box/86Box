# What the NVIDIA Windows 9x drivers expect from an NV3

Reverse-engineered from the RIVA 128 Windows 98 driver set (NV3RM.VXD resource
manager, NV3DISP.DRV display driver, NV3DD32.DLL DirectDraw/D3D) while bringing up
this emulation. File offsets are into the files as shipped; NV3RM.VXD is an LE
VxD whose code object starts at file offset 0xAC00 (runtime address = file offset
+ a per-boot load delta). Table pointers in the VxD are relocation fixups (zero in
the file): resolve them through the LE fixup table.

## Resource manager (NV3RM.VXD)

### Objects, binding and lazy instantiation
- The resman keeps its own record per channel and subchannel of the object bound
  there (`[chan+0x2c + subch*0x40]`). It cannot see hardware binds (method 0 that
  hit RAMHT), so it **re-reads the PFIFO CACHE1 context registers 0x3280-0x32F0**
  (stride 0x10, one per subchannel) whenever it needs to (0x26950 "sync and zero",
  0x272EE "object changed"), looks each live context up by (class, instance)
  (0x422E4) and records it. A context register that reads back wrong desynchronises
  the resman from the driver (this was the mode-switch bug: subch 7 read as 0).
- A mode set reloads PFIFO state (0x24FB0): it clears all of RAMHT and the resman's
  RAMHT shadow, zeroes every subchannel context (after the sync above), and marks
  the instances of most graphics objects stale (`obj+0x334 &= 0xffff0000`).
- From then on objects are re-instantiated lazily:
  - a driver bind (method 0) misses RAMHT -> PFIFO CACHE_ERROR (hash failure) ->
    the resman's bind handler (0x19D00) records the handle for the subchannel and
    writes context 0 (PULL_STATE bit 4 "dirty" set);
  - the next method on that subchannel traps as a software method (context bit 23
    clear) -> the resman instantiates the object (writes its RAMIN words,
    re-inserts its RAMHT entry), writes the real context into CACHE1 and **leaves
    the method queued** so the hardware re-executes it.
  - Objects that are never put in RAMHT (the readback M2MF, handle 0x1020) always
    take this path.
- While a subchannel's context is 0, the resman executes that subchannel's methods
  in software through its class tables (dispatcher 0x19BF6): method found in the
  recorded object's class table -> handler(obj, entry, method, data).
- PFIFO cache-error handler: 0x27EB6. It reads the entry at GET (0x3300 + GET*2,
  method at +0, data at +4) on rev A/B. On rev C (64-entry cache, `[dev+0x34] == 0x20`) GET
  is a 7-bit Gray pointer: it steps it as gray((binary + 1) & 0x7F) (0x25A54) and reads
  0x3400 + 8 * slot, slot = (g & 0x1F) | (((g >> 1) ^ g) & 0x20) -- the 6-bit Gray code of
  the binary pointer (0x281F0-0x28378). handles methods < 0x100
  through a small table (0x0-0x3 bind -> 0x19D00, 0x20-0x2C, 0x30-0x37, 0x38-0x4B),
  others through the object's class table; then keeps going while the next
  entries also belong to software objects, advancing GET (0x3270) itself.

### Object options word (RAMIN word 0)
- Created objects get options 0xFFFFFFFF ("not validated", 0x2E6E4); the
  validator 0x32D5B computes them from the object's colour format (+0x33C, -1 =
  unset), the patch (+0x340, +0x348, surface +0x3AC ...) and writes them to RAMIN
  and every CTX_CACHE entry bound to it (0x371AD -> 0x429A3, 0x272EE).
- Image classes (IFC, SIFC, bitmap, ...) route their data methods (0x304, 0x400+)
  through 0x371AD in software, which validates and commits the object first.

### PGRAPH interrupt handler (0x2D2B0)
- Reads INTR (0x400100 & 0x11111011), INVALID (0x400104), CTX_SWITCH (0x400180),
  TRAPPED_ADDR (0x4006B4: chid 30:24, class 20:16, subch 15:13, method 12:2),
  TRAPPED_DATA (0x4006B8), TRAPPED_INSTANCE (0x4006BC) and looks the object up by
  (chid, class|0x40, instance).
- Services: INVALID with INVALID_METHOD (software methods: 0x100, 0x104 notify,
  0x200, 0x300...), MISSING_METHOD (bit 16: error report, writes 0x400508=0x33),
  NOTIFY (bit 28), CONTEXT_SWITCH (bit 4, load routine near 0x42040 in the file).
  XY_RANGE, MISSING_FORMAT and CLIP_SOFTWARE are ignored.
- Waits for PGRAPH idle: polls 0x4006B0 == 0; FIFO access 0x4006A4.
- PDMA faults (0x401100) are only acknowledged.

### M2MF (class 0x0D)
- Class method table (file 0xE130): 0x100, 0x104 notify, 0x108, 0x300 (0x5A979),
  0x304 (0x5AB3A), 0x308 notify DMA (0x5ACFB, field +0x350), 0x30C-0x32B (0x5ADAB
  -> commit 0x5A579).
- Commit 0x5A579 checks the three DMA objects (+0x34C, +0x348, +0x350) with
  0x1E3FC (non-null, instantiated, range) and instantiates them (0x1E480), sets
  word 2 bit 16 when the source DMA object is the framebuffer (+0x480 == 3 type),
  writes 0x401400 = DMA limit, then binds. Any failure = transfer dropped,
  notifier never written.

### DMA objects
- Global list `[dev+0x4586]`, next at +0x484, handle +0, channel +0x47C; lookup
  0x1E36E, create 0x1DF40, destroy 0x1E072.
- Instance written by 0x1EFAD: word 0 = page offset (11:0) | 0x10000 (page table)
  | target (0x2000000 PCI, 0x3000000 AGP, type from +0x480), word 1 = limit, then
  one PTE per page (address | 3), terminated by 0xFFFFFFFF.
- Instance heap: bitmap allocator 0x22DED in 16-byte units, base `[dev+0x11C]`,
  size `[dev+0x120]`; returns 0x1000106 when full.

### Instance memory layout (state load 0x21530, size from 0x22221)
- Instance memory = VRAM - (bpp/8 * width * height * buffers), capped at 64 KB,
  plus 0x3000 and 0x5000 (special-cased 960x720 / 1920x1080) -> normally 96 KB at
  the top of VRAM.
- >= 64 KB: RAMHT 16 KB at 0 (0x2210 = 0x20000), RAMRO 0x4000 (0x2218), RAMFC
  0x4200 (0x2214), heap from 0x4600.
  Smaller: RAMHT 8 KB (0x10000) / RAMRO 0x2000 / RAMFC 0x2200 / heap 0x2600, or
  RAMHT 4 KB / RAMRO 0x1000 / RAMFC 0x1200 / heap 0x1600.
- RAMHT hash: ((h ^ h>>8 ^ h>>16 ^ h>>24) & 0xFF) ^ chid; 8 bytes per entry
  (handle, context), `[dev+0x110]` entries per bucket.

### Class method tables
Every class the resman knows has a table of (handler, first method, last
method) triples between file offsets 0xD6C0 and 0xE4A0 (pointers via fixups).
These list exactly which methods each class accepts (a method outside its table
is an error), and which handler runs when the method is executed in software.

## Display driver (NV3DISP.DRV)
- Subchannel use: 0 surfaces/rop (0x31...), 2 the destination surface (handles
  0x1F00/0x1F05, rebinds constantly), 3 ROP/pattern/patchcords, 4 image (0x11),
  5 text (0x0C), 6 rectangles, **7 shared**: blit 0x1700, stretched image 0x1B00,
  readback M2MF 0x1020. The driver caches what it bound to subch 7 in one dword
  (`[0x440188]` in the 32-bit part, also used by the 16-bit BitBlt at 0x17A8)
  and only binds when it differs.
- GetPixel (0x5BA07): sets the notifier status byte to 0xFF, programs the M2MF
  readback (0x5B3C0: bind 0x1020 if needed, OFFSET_IN = y*pitch + x*2, pitch,
  length, count 1, format 0x101, BUFFER_NOTIFY), then spins on the status byte.
- Free FIFO space read from USER +0x10 of the subchannel (e.g. 0xE010) before
  each burst.
- Object setup at init (0x6F380, 0x70C20): binds and patchcord/DMA setup through
  subchannel 3 software objects (methods 0x300/0x304 with object handles).

## Direct3D driver (NV3DD32.DLL)

PE image base 0xB00B0000, .text at 0xB00B1000 (file 0x400), .data at 0xB00E9000.
Disassemble with tools/nv3/pedis.py (VA) or search with tools/nv3/xref.py.

- Push buffer, subchannel 7 = the D3D triangle object (class 0x17). Before a
  batch the driver writes `0x0004E000, <texture DMA object>` (SET_OBJECT on
  subchannel 7) and `0x0018E304` + six words for methods 0x304-0x318: texture
  offset, format, FILTER, FOG_COLOR, CONFIG, ALPHA (e.g. 0xB00BE19B, 0xB00D95AE).
  Vertices follow as `0x0020F000` headers (8 words at method 0x1000: fog/indices,
  colour, sx, sy, sz, rhw, tu, tv), always at vertex 0's address; the low nibble
  of the first word names the slot, the nibbles above it the triangle(s) to draw.
- The state words come from per-texture arrays indexed by the current texture
  slot [0xB00EC140]: offset 0xB00EC214[], format 0xB00EC1D4[], filter
  0xB00EC1F4[], DMA object 0xB00EC234[], config 0xB00EC1B4[] ORed with the
  global word [0xB00EC118]; fog colour [0xB00EC114], alpha [0xB00EC124].
- The global config word is the context's `[ctx+8]` (render targets, 0xB00C0705):
  bits 14:0 kept, Z func / Z write / colour write (`and 0xF8807FFF`) rebuilt.
- Texture stage states: per-stage config `[ctx+0xA4+stage*4]` and filter
  `[ctx+0xE4+stage*4]` (0xB00C230B): the filter setter clears config bits 1:0,
  sets 2 for linear (FOH), keeps a default otherwise; the filter word is a
  default from the context (`[+0x7BC]`, 0x00EC0000 here) with bit 31 set on the
  point path. Observed in 3DMark 99: FILTER is 0x80EC0000 for every pass; the
  point and bilinear filtering subtests differ only in CONFIG bits 1:0 (0 vs 2).
- CONFIG bits 11:8 seen: 0xC (opaque textured geometry, MODULATE), 0x1 (title
  text, alpha-blended), 0x0 (multiply-with-destination pass). rnndb lists 1/2/3/6
  for SOURCE_COLOR; where 0xC comes from is not found yet (0xB00C35FF clears bits
  11:10 of the stage word in the DX6 colour-op code at 0xB00C3585).
- Depth: the driver passes D3DCMP values straight into CONFIG 19:16, with sz
  as given (0 near); the zeta buffer must hold z * 0xFFFF for LESSEQUAL to work.

## Video overlay (PVIDEO, 0x680200-0x6802FF)

Programmed by NV3RM.VXD (init 0x5696A, UpdateOverlay 0x56AFB, interrupt handler 0x57719,
colour key 0x57296); NV3DD32.DLL never touches it. DirectDraw reports one overlay, stretch
0.062-20x, FOURCCs UYVY UYNV YUY2 YUNV YV12 YVU9 IF09 IV32 IV31 RAW8.

- 0x200 scale: Y step 31:16, X step 15:0, (src-1)*2048/(dst-1) (0x0553054E for 320x240
  to 480x360; the driver widens the destination by 2).
- 0x204 bit 4 set when the source is at most 384 wide; 0x208 = 0x110; 0x28C = 0x10000 (init).
- 0x20C/0x210 buffer 0/1 start in VRAM; 0x214/0x218 pitch (& 0x7FF0; doubled with the start
  moved one line for one field of interlaced video); 0x21C/0x220 bits 5:4 = the low bits of
  the driver's pitch word.
- 0x224 status, 0x228 ready, 0x22C ack: buffer n is submitted by toggling 0x228 bit 16+4n so
  it equals 0x224's; the hardware takes the buffer other than 0x224 bit 24 at a retrace,
  toggling 0x224 bits 16+4n and 4n, and raises PVIDEO interrupt bit 0 (0x680100, enable
  0x680140). The handler sees (0x228^0x224) bit 16+4n and (0x22C^0x224) bit 4n set, calls
  DirectDraw back, toggles 0x22C bit 4n, and re-arms: it writes 0x224 bit 24 = 1 and submits
  buffer 0 again, every frame. So bit 24 is a steering bit, not the buffer on screen.
- 0x230 position (y 31:16, x 15:0), 0x234 size (h 31:16, w 15:0).
- 0x240 key colour (& 0x7FFF at 16 bpp), 0x244: bit 0 on, bit 4 destination colour key,
  bit 8 YUY2 (clear: UYVY).
- 0x238/0x23C FIFO threshold/burst; 0x280-0x288 = 0x69, 0x3E, 0x89 at boot (unknown, maybe
  colour-space coefficients).

## Windows 2000 display driver (nv3.dll 5.00.2146.0343, inbox)

NVIDIA's "RIVA 128/RIVA 128 ZX Windows 2000 Display driver, Version 3.43" (nv3.sys miniport
1999-10-27, nv3.dll 1999-12-07; the INF takes DEV_0018 and DEV_0019). Image base 0x69AE0000,
no exports; DrvEnableDriver's DRVFN table at file 0x1D738 (33 entries).
- DrvGetDirectDrawInfo (0x69AE6B66): DD_HALINFO 0x1A0 bytes; caps 0x14427940 (no DDCAPS_3D),
  FOURCCs YUY2, UYVY, YV12, I420... (IF09 when [pdev+0x4208]); GetDriverInfo = 0x69AE6F2C,
  DDHALINFO_GETDRIVERINFOSET. lpD3DGlobalDriverData / lpD3DHALCallbacks / lpD3DBufCallbacks
  (+0x194..0x19C) are never set: **no Direct3D HAL on Windows 2000**.
- GetDriverInfo answers only GUID_MiscellaneousCallbacks (GetAvailDriverMemory 0x69AE6EE8);
  every other GUID gets DDERR_CURRENTLYNOTAVAIL (0x88760028).
- It names an OpenGL ICD, nv3oglnt.dll / nvoglnt.dll, which Windows 2000 does not ship.

## Register details (2026-10-10)
- D3D CONTROL0 (0x314): 1:0 interpolator (ZOH, ZOH_MS, FOH), 5:4/7:6 wrap (cylindrical, wrap,
  mirror, clamp), 8 colour format (X8R8G8B8 ignores vertex alpha), 11:10 SRCCOLOR (normal,
  colour inverse, alpha inverse, alpha one), 13:12 culling, 15 linear zeta, then zeta compare/
  write, colour write, ROP/beta/inputs. NV3DD32: blending off =
  ALPHA_ONE + src x srcalpha + dst x 0 with X8R8G8B8; colour key = COLOR_WRITE_ALPHA(_ZETA) and a
  blend mode without ALPHA_ONE; alpha test off =
  CONTROL1 compare TRUE (0x800). ZOH = texel origin upper-left, ZOH_MS = centre; FOH has its
  origin at the upper-left too and the driver subtracts 0.4375 texel from u/v (FOHMODE default).
  FILTER default 0x80EC0000: sizeadj S4.4 = -1.25, turbo 0x80 = off for point sampling. sizeadj
  is not applied (no reference for its scale).
- CONTROL0/CONTROL1 are PGRAPH CONTROL_OUT (0x400644) and ALPHACNTRL (0x4006C8), shared by
  classes 0x17 and 0x18.
- Channel switch: the NV3 resman reads SOURCE_COLOR, MONO_COLOR0, CONTROL_OUT,
  D3D_XY/UV/ZETA/RGB/FOG/M, PASSTHRU, MISC24_0/1 and writes them back; it zeroes ABS_X/Y RAM,
  ICLIP, XY_LOGIC_MISC*, X/Y_MISC, CLIPX/Y, ZFOG RAM and EXCEPTIONS.
- PVIDEO: the resman's init writes CONTROL_Y BLUR, CONTROL_X SMOOTHING|SHARPENING, OFFSET 0,
  OE_STATE CURRENT_BUFFER 1, SU_STATE both IN_USE, CSC 0x69/0x3E/0x89, CSC_ADJUST 0x10000; keys
  masked 0x7FFF at 16 bpp. NV3DD32's brightness adds d to red/blue CSC and subtracts it from green.
- Straps: the STB V128ZX BIOS writes 0xDB3 to PEXTDEV_BOOT_0 unconditionally (file 0x3A77; AGP,
  66 MHz, 16 Mbit, 128-bit, TV off, overwrite), then PFB_BOOT 0x24.
- BIOS VBE reads NV_PBUS_PCI_NV_5 (PBUS 0x1814, BAR1) for the LFB address.
