# Voodoo ARM64 JIT Testing Guide

A comprehensive guide for building, testing, and verifying the Voodoo ARM64 JIT port on macOS Apple Silicon.

## Table of Contents

- [Prerequisites](#prerequisites)
- [Building 86Box](#building-86box)
- [Setting Up a Test VM](#setting-up-a-test-vm)
- [Voodoo Card Configuration](#voodoo-card-configuration)
- [Testing Scenarios](#testing-scenarios)
- [Debug Logging](#debug-logging)
- [What to Look For](#what-to-look-for)
- [Reporting Issues](#reporting-issues)

---

## Prerequisites

### Required Software

1. **Xcode Command Line Tools**
   ```bash
   xcode-select --install
   ```

2. **Homebrew** (if not already installed)
   ```bash
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```

3. **Build Dependencies**
   ```bash
   brew install sdl2 rtmidi openal-soft fluidsynth libslirp vde libserialport qt@5
   ```

### System Requirements

- macOS 11.0 (Big Sur) or later
- Apple Silicon Mac (M1/M2/M3/M4 or later)
- At least 8GB RAM recommended
- 2GB free disk space for VM and ROM files

---

## Building 86Box

### 1. Clone the Repository

```bash
git clone https://github.com/yourusername/86Box-voodoo-arm64.git
cd 86Box-voodoo-arm64
```

### 2. Configure the Build (Release)

```bash
cmake -S . -B build --preset regular \
  --toolchain ./cmake/llvm-macos-aarch64.cmake \
  -D NEW_DYNAREC=ON -D QT=ON \
  -D Qt5_ROOT=$(brew --prefix qt@5) \
  -D Qt5LinguistTools_ROOT=$(brew --prefix qt@5) \
  -D OpenAL_ROOT=$(brew --prefix openal-soft) \
  -D LIBSERIALPORT_ROOT=$(brew --prefix libserialport)
```

Or use the convenient preset:
```bash
cmake --preset llvm-macos-aarch64.cmake
```

### 3. Build

```bash
cmake --build build
```

### 4. Install

```bash
cmake --install build
```

The executable will be in `build/artifacts/86Box.app`.

### Debug Build (Optional)

For development and troubleshooting:

```bash
cmake --preset llvm-macos-aarch64-debug
cmake --build out/build/llvm-macos-aarch64-debug
```

### Code Signing with JIT Entitlements

**macOS requires special entitlements for JIT compilation.** The build process handles this automatically, but if you need to manually sign:

```bash
codesign --force --sign - \
  --entitlements ./src/unix/assets/86Box.entitlements \
  --deep build/artifacts/86Box.app
```

**Why this matters:** Without JIT entitlements, macOS blocks write+execute memory pages, causing the JIT to fail silently and fall back to the interpreter.

---

## Setting Up a Test VM

### 1. Obtain Windows 98 SE

You'll need a legal copy of Windows 98 Second Edition. The ISO can be installed directly through 86Box.

### 2. Create a New VM

1. Launch `86Box.app`
2. Click **Tools → Settings** (or press `Ctrl+Alt+PgDn`)
3. Navigate to **Machine**:
   - **Machine type**: Socket 7 (Intel P54C)
   - **Machine**: Intel Advanced/EV (Endeavor)
   - **CPU**: Intel Pentium 166 MHz
   - **Memory**: 128 MB
   - **FPU**: Built-in

### 3. Configure Video (2D Display)

For the primary 2D video card (Windows desktop, BIOS, etc.):

- Navigate to **Display → Video**
- Choose: **S3 ViRGE/DX** or **S3 Trio64**
- Set **Video Memory**: 4 MB

**Why not Voodoo for 2D?** Voodoo 1 and 2 are 3D-only pass-through cards. You need a separate 2D card for desktop and non-3D applications. Voodoo 3/Banshee combine 2D+3D.

### 4. Add a Voodoo Card (3D Acceleration)

- Navigate to **Display → 3D Graphics Card**
- Choose:
  - **Voodoo Graphics** (original, 1 TMU)
  - **Voodoo Graphics (2 TMUs)** (enhanced)
  - **Voodoo 2** (most common for testing)
  - **Voodoo Banshee** (2D+3D combined)
  - **Voodoo 3 2000** (2D+3D combined)

### 5. Configure Storage

- **Hard disk controller**: Standard IDE
- **Hard disk**: Create new (at least 1 GB for Windows 98 + games)
- **CD-ROM**: Attach Windows 98 ISO

### 6. Install Windows 98

Boot from the CD-ROM and follow the standard Windows 98 installation.

---

## Voodoo Card Configuration

### Dynamic Recompiler Toggle

**New in this port:** The Voodoo JIT can now be toggled at runtime.

1. Go to **Settings → Display → 3D Graphics Card**
2. Click **Configure** next to the Voodoo card
3. Check/uncheck **Dynamic Recompiler**:
   - **ON** (checked): Uses ARM64 JIT (fast)
   - **OFF** (unchecked): Uses interpreter (slow, reference)

This matches the existing behavior on x86-64 builds.

### JIT Debug Logging

For developers and bug reporters:

1. In the Voodoo card configuration dialog:
2. Set **JIT Debug Logging**:
   - **0** (default): Disabled
   - **1**: Logging enabled (writes to `voodoo_jit.log`)
   - **2**: Verify mode (runs JIT + interpreter, compares output)

**Log location:** `<vm_directory>/voodoo_jit.log`

**Verify mode (level 2):** Runs both JIT and interpreter for every scanline, comparing pixel output. Catches rendering differences but is VERY slow. Use only when debugging visual corruption.

### Render Thread Count

Experiment with different thread counts for performance testing:

- 1 thread: Single-threaded (baseline)
- 2 threads: Dual-threaded
- 4 threads: Quad-threaded (best on M2/M3/M4)

---

## Testing Scenarios

### Test Matrix

| Card | Type | SLI | Bilinear | Threads | Recompiler | Notes |
|------|------|-----|----------|---------|------------|-------|
| Voodoo 1 | 0 | No | OFF | 1 | ON | Basic 3D |
| Voodoo 1 | 0 | No | ON | 1 | ON | Bilinear filtering |
| Voodoo 2 | 2 | No | OFF | 2 | ON | Detection test |
| Voodoo 2 | 2 | Yes | ON | 2 | ON | SLI mode |
| Voodoo 3 | 3 | No | ON | 4 | ON | 2D+3D combined |
| Any | Any | - | ON | 1 | OFF | Interpreter reference |

### Software to Test

#### 3D Games

- **Quake** (software + GLQuake)
- **Unreal** (Glide renderer)
- **Turok: Dinosaur Hunter**
- **Tomb Raider** (Glide patch)
- **Descent II** (Glide)
- **Carmageddon** (3Dfx version)

#### Benchmarks

- **3DMark 99** (comprehensive test suite)
- **Final Reality** (stress test)
- **Quake Timedemo** (`timedemo demo1`)

#### Demos

- **3Dfx demo disc** (if available)
- **Glide SDK demos**

### Key Test Cases

1. **Voodoo 2 Detection**
   - Install drivers
   - Check Device Manager for "3Dfx Voodoo2"
   - Run 3DMark 99 and verify detection

2. **Texture Rendering**
   - Look for texture corruption (striping, wrong colors)
   - Check point-sampled vs bilinear filtering
   - Test different texture sizes (64x64, 128x128, 256x256)

3. **Alpha Blending**
   - Transparency effects (smoke, glass, water)
   - Overlays and UI elements in 3D

4. **Fog Effects**
   - Distance fog in games that support it
   - Fog color and density

5. **Depth Testing**
   - Z-fighting artifacts
   - Proper occlusion (near objects hide far objects)

6. **Dithering**
   - 4x4 vs 2x2 dither patterns
   - Color banding in gradients

7. **Framerate**
   - Compare JIT ON vs OFF
   - Expected: JIT should be 5-10x faster than interpreter

---

## Debug Logging

### Enabling JIT Debug Logs

1. Set **JIT Debug Logging = 1** in Voodoo configuration
2. Run your test (game, benchmark, etc.)
3. Close 86Box
4. Open `<vm_directory>/voodoo_jit.log`

### Log Contents

```
VOODOO JIT: GENERATE #1 odd_even=0 block=0 code=0x123456 recomp=1
  fbzMode=0x00000000 fbzColorPath=0x00000000 alphaMode=0x00000000
  textureMode[0]=0x00000000 fogMode=0x00000000 xdir=1
VOODOO JIT: cache HIT #0 odd_even=0 block=0 code=0x123456
  fbzMode=0x00000000 fbzColorPath=0x00000000 alphaMode=0x00000000
```

- **GENERATE**: JIT compiled new code block
- **cache HIT**: Reused existing compiled block
- **recomp count**: Total JIT compilations (increases over time)

### Verify Mode (Level 2)

**Warning:** Very slow, only use for debugging visual corruption.

When enabled:
- Runs JIT code for a scanline
- Runs interpreter for the same scanline
- Compares pixel output
- Logs mismatches to `voodoo_jit.log`

---

## What to Look For

### Visual Issues

- **Horizontal striping**: Repeating patterns across scanlines
- **Color distortion**: Wrong RGB values, tinting
- **Missing textures**: Black or solid-color polygons
- **Texture shimmer**: Flickering/unstable textures
- **Z-fighting**: Polygons flicker when overlapping
- **Alpha artifacts**: Incorrect transparency

### Performance Issues

- **Low framerate**: JIT should match or exceed interpreter performance
- **Stuttering**: Pauses during rendering (recompilation overhead)
- **Memory leaks**: VM grows over time

### Stability Issues

- **Crashes**: Emulator quits unexpectedly
- **Hangs**: Emulator freezes (force quit required)
- **Corruption after long sessions**: Gradual degradation

---

## Reporting Issues

### Information to Include

1. **System Info**
   - macOS version (e.g., macOS 14.2 Sonoma)
   - Mac model (e.g., M2 MacBook Pro)
   - 86Box version/commit hash

2. **VM Configuration**
   - Machine type (e.g., Socket 7, Pentium 166)
   - RAM (e.g., 128 MB)
   - 2D video card (e.g., S3 ViRGE/DX)
   - Voodoo card type (e.g., Voodoo 2)
   - Voodoo settings:
     - Recompiler: ON/OFF
     - Bilinear: ON/OFF
     - Render threads: 1/2/4
     - JIT debug level: 0/1/2

3. **Reproduction Steps**
   - Game/benchmark name and version
   - Exact steps to trigger the issue
   - Screenshot or screen recording (if visual)

4. **Logs**
   - Set **JIT Debug Logging = 1**
   - Reproduce the issue
   - Attach `voodoo_jit.log`

### Where to Report

Open an issue at: https://github.com/yourusername/86Box-voodoo-arm64/issues

**Title format:** `[Voodoo ARM64] Brief description`

**Example:** `[Voodoo ARM64] Horizontal striping in Quake with Voodoo 2`

---

## Performance Expectations

### Interpreter vs JIT

| Scenario | Interpreter (OFF) | JIT (ON) | Speedup |
|----------|-------------------|----------|---------|
| 3DMark 99 | ~5 FPS | ~40 FPS | 8x |
| Quake timedemo | ~8 FPS | ~60 FPS | 7.5x |
| Unreal | ~3 FPS | ~25 FPS | 8x |

**Note:** Actual performance varies by game, resolution, and Mac model. M3/M4 Macs with more performance cores see better results.

### JIT Compilation Overhead

First frame in a new scene may stutter briefly (1-3 frames) as the JIT compiles new render states. This is normal and expected. After warm-up, performance should be smooth.

---

## Troubleshooting

### Issue: Voodoo 2 not detected in Windows

**Cause:** Driver installation issue or JIT bug.

**Solution:**
1. Enable **JIT Debug Logging = 2** (verify mode)
2. Check log for pixel mismatches
3. Try **Recompiler = OFF** to confirm hardware works
4. Report issue with logs

### Issue: Visual corruption (striping, wrong colors)

**Cause:** JIT codegen bug.

**Solution:**
1. Enable **JIT Debug Logging = 1**
2. Capture screenshot of corruption
3. Report with log and screenshot

### Issue: Crash on launch

**Cause:** Missing JIT entitlements or segmentation fault.

**Solution:**
1. Re-sign the app with entitlements:
   ```bash
   codesign --force --sign - \
     --entitlements ./src/unix/assets/86Box.entitlements \
     --deep build/artifacts/86Box.app
   ```
2. If crash persists, run from terminal to see crash log:
   ```bash
   ./build/artifacts/86Box.app/Contents/MacOS/86Box
   ```

### Issue: JIT extremely slow (slower than interpreter)

**Cause:** Likely running on Intel Mac (Rosetta 2) or bad build.

**Solution:**
1. Verify you're on Apple Silicon:
   ```bash
   uname -m
   # Should output: arm64
   ```
2. Verify 86Box is ARM64:
   ```bash
   file ./build/artifacts/86Box.app/Contents/MacOS/86Box
   # Should include: arm64
   ```

---

## Additional Resources

- **86Box Wiki:** https://86box.readthedocs.io/
- **3Dfx Archive:** http://www.3dfxarchive.com/
- **Glide API Reference:** Available in project docs

---

**Happy testing!** Report bugs, share screenshots, and help make this port solid. 🚀
