# Building the 86Box Linux ARM64 AppImage

Step-by-step guide to building a fully-bundled AppImage on your Mac (Apple Silicon).

---

## Prerequisites

You need **Docker Desktop** installed and running. You already have it — verify with:

```bash
docker --version
```

That's it. Everything else (Debian, compilers, libraries) lives inside the container.

---

## Step 1: Build the AppImage

From the repo root, run:

```bash
./appimage/build.sh
```

**First run** takes ~15-20 minutes:
1. Builds the Docker image (downloads Debian Bullseye ARM64, installs build deps)
2. Compiles OpenAL, rtmidi, FluidSynth, and SDL2 from source (minimal features)
3. Compiles 86Box linked against those custom libraries
4. Bundles everything into an AppImage with appimage-builder

**Subsequent runs** are much faster:
- Docker image is cached (skips step 1)
- cmake build cache is stored in a persistent Docker volume (only recompiles changed files)

## Step 2: Grab the output

The AppImage lands in:

```
appimage/output/86Box-*.AppImage
```

Copy it to your Pi 5 (or any Linux ARM64 machine):

```bash
scp appimage/output/86Box-*.AppImage pi@raspberrypi.local:~/
```

## Step 3: Run on the Pi

On the Pi:

```bash
chmod +x 86Box-*.AppImage
./86Box-*.AppImage
```

No dependencies to install. Everything is bundled.

---

## Commands

| Command | What it does |
|---------|-------------|
| `./appimage/build.sh` | Build the AppImage |
| `./appimage/build.sh shell` | Open a bash shell inside the container for debugging |
| `./appimage/build.sh clean` | Delete the cmake build cache (forces full rebuild next time) |

---

## How the persistent cache works

The cmake build directory is stored in a Docker volume (`86box-appimage-buildcache`). This means:

- First build: full compile (~5-10 min depending on your Mac)
- Code change + rebuild: only changed files recompile (~30 sec)
- `./appimage/build.sh clean`: deletes the cache, next build is full

The volume survives container restarts, Docker Desktop restarts, and reboots. It only goes away if you explicitly clean it or delete Docker volumes.

Check it exists:

```bash
docker volume ls | grep 86box
```

---

## What's bundled

The AppImage includes everything needed to run on any Linux ARM64 distro.
Matches the official 86Box AppImage approach — four key libraries are compiled
from source with minimal features to avoid transitive dependencies (libnsl,
libjack, libpulse) that break on distros like Fedora.

**Compiled from source** (matching official):
- **OpenAL Soft 1.23.1** — sndio backend disabled
- **rtmidi 4.0.0** — JACK API disabled
- **FluidSynth 2.3.0** — all sound backends disabled
- **SDL2 2.0.20** — audio/video off, joystick only (Qt handles display)

**Bundled from Debian packages**:
- **glibc 2.31** (Debian Bullseye) — wide distro compatibility
- **Qt 5.15** — core, gui, widgets, Wayland plugin
- **Audio** — libsndfile, libinstpatch (soundfont loading)
- **MIDI** — mdsx.so synthesizer plugin (if built)
- **Networking** — libslirp, libvdeplug (VDE)
- **Serial** — libserialport
- **Printing** — Ghostscript for PostScript printer emulation

**Not bundled** (provided by host OS):
- Crypto/TLS, systemd, udev, ALSA — these must come from the host

Compression: gzip (same as official 86Box releases).

---

## Troubleshooting

### Build fails with "exec format error"

You're trying to run an ARM64 container on x86. This only works on Apple Silicon Macs or ARM64 Linux.

### appimage-builder fails to download packages

The recipe pulls from Debian Bullseye repos. If a package version has been removed, update the version pin in `AppImageBuilder.yml`.

### AppImage won't launch on the Pi

```bash
# Check it's actually ARM64
file 86Box-*.AppImage

# Run with debug output
QT_DEBUG_PLUGINS=1 ./86Box-*.AppImage
```

### Need a full rebuild

```bash
./appimage/build.sh clean
./appimage/build.sh
```

### Want to poke around inside the build environment

```bash
./appimage/build.sh shell
# Now inside the container:
ls /src                    # Your source code (read-only)
ls /build/cmake-build      # Persistent cmake build cache
ls /output                 # Where the AppImage goes
```
