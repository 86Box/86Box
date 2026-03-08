#!/bin/bash
#
# Build a fully-bundled 86Box AppImage for Linux ARM64 (aarch64)
#
# This script is designed to run inside the Docker container.
# From the host: ./appimage/build.sh
#
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

SRC_DIR="/src"
BUILD_DIR="/build/cmake-build"
APPDIR="/build/AppDir"
OUTPUT_DIR="/output"
LIBPREFIX="/build/custom-libs"

# Verify source is mounted
[[ -f "$SRC_DIR/CMakeLists.txt" ]] || error "Source not mounted at $SRC_DIR"

# -----------------------------------------------------------------------
# Step 0: Build custom libraries from source
# -----------------------------------------------------------------------
# Matches the official 86Box AppImage approach — compile OpenAL, rtmidi,
# FluidSynth, and SDL2 from source with minimal features to avoid pulling
# in transitive dependencies (libnsl, libjack, libpulse, etc.)
# -----------------------------------------------------------------------

LIBSRC="/build/lib-src"
LIBBUILD="/build/lib-build"
mkdir -p "$LIBSRC" "$LIBBUILD" "$LIBPREFIX"

# --- OpenAL Soft 1.23.1 ---
if [[ ! -f "$LIBPREFIX/lib/libopenal.so" ]]; then
    info "Building OpenAL Soft 1.23.1..."
    cd "$LIBSRC"
    if [[ ! -d openal-soft-1.23.1 ]]; then
        wget -q "https://openal-soft.org/openal-releases/openal-soft-1.23.1.tar.bz2"
        tar xf openal-soft-1.23.1.tar.bz2
    fi
    cmake -S openal-soft-1.23.1 -B "$LIBBUILD/openal" -G Ninja \
        -D CMAKE_BUILD_TYPE=Release \
        -D CMAKE_INSTALL_PREFIX="$LIBPREFIX" \
        -D ALSOFT_EXAMPLES=OFF \
        -D ALSOFT_UTILS=OFF \
        -D ALSOFT_BACKEND_SNDIO=OFF
    cmake --build "$LIBBUILD/openal" -j"$(nproc)"
    cmake --install "$LIBBUILD/openal"
    info "OpenAL Soft installed."
fi

# --- rtmidi 4.0.0 ---
if [[ ! -f "$LIBPREFIX/lib/librtmidi.so" ]]; then
    info "Building rtmidi 4.0.0..."
    cd "$LIBSRC"
    if [[ ! -d rtmidi-4.0.0 ]]; then
        wget -q "https://github.com/thestk/rtmidi/archive/refs/tags/4.0.0.tar.gz" -O rtmidi-4.0.0.tar.gz
        tar xf rtmidi-4.0.0.tar.gz
    fi
    cmake -S rtmidi-4.0.0 -B "$LIBBUILD/rtmidi" -G Ninja \
        -D CMAKE_BUILD_TYPE=Release \
        -D CMAKE_INSTALL_PREFIX="$LIBPREFIX" \
        -D RTMIDI_API_JACK=OFF
    cmake --build "$LIBBUILD/rtmidi" -j"$(nproc)"
    cmake --install "$LIBBUILD/rtmidi"
    # Fix rtmidi.pc — cmake sometimes generates broken includedir without prefix
    if [[ -f "$LIBPREFIX/lib/pkgconfig/rtmidi.pc" ]]; then
        sed -i "s|^prefix=.*|prefix=$LIBPREFIX|" "$LIBPREFIX/lib/pkgconfig/rtmidi.pc"
        sed -i "s|^includedir=.*|includedir=\${prefix}/include|" "$LIBPREFIX/lib/pkgconfig/rtmidi.pc"
    fi
    info "rtmidi installed."
fi

# --- FluidSynth 2.3.0 ---
if [[ ! -f "$LIBPREFIX/lib/libfluidsynth.so" ]]; then
    info "Building FluidSynth 2.3.0..."
    cd "$LIBSRC"
    if [[ ! -d fluidsynth-2.3.0 ]]; then
        wget -q "https://github.com/FluidSynth/fluidsynth/archive/refs/tags/v2.3.0.tar.gz" -O fluidsynth-2.3.0.tar.gz
        tar xf fluidsynth-2.3.0.tar.gz
    fi
    cmake -S fluidsynth-2.3.0 -B "$LIBBUILD/fluidsynth" -G Ninja \
        -D CMAKE_BUILD_TYPE=Release \
        -D CMAKE_INSTALL_PREFIX="$LIBPREFIX" \
        -D enable-jack=OFF \
        -D enable-oss=OFF \
        -D enable-sdl2=OFF \
        -D enable-pulseaudio=OFF \
        -D enable-pipewire=OFF \
        -D enable-alsa=OFF \
        -D enable-aufile=OFF \
        -D enable-dbus=OFF \
        -D enable-network=OFF \
        -D enable-ipv6=OFF
    cmake --build "$LIBBUILD/fluidsynth" -j"$(nproc)"
    cmake --install "$LIBBUILD/fluidsynth"
    # ABI compat: 86Box links against libfluidsynth.so.2 (Bullseye), we built .so.3
    if [[ -f "$LIBPREFIX/lib/libfluidsynth.so.3" && ! -f "$LIBPREFIX/lib/libfluidsynth.so.2" ]]; then
        ln -s libfluidsynth.so.3 "$LIBPREFIX/lib/libfluidsynth.so.2"
    fi
    info "FluidSynth installed."
fi

# --- SDL2 2.0.20 ---
if [[ ! -f "$LIBPREFIX/lib/libSDL2.so" ]]; then
    info "Building SDL2 2.0.20..."
    cd "$LIBSRC"
    if [[ ! -d SDL2-2.0.20 ]]; then
        wget -q "https://github.com/libsdl-org/SDL/releases/download/release-2.0.20/SDL2-2.0.20.tar.gz"
        tar xf SDL2-2.0.20.tar.gz
    fi
    # Minimal SDL2 for Qt build — we only need joystick/gamepad support.
    # Disable audio/video backends to avoid PulseAudio, libdrm, libnsl deps.
    cmake -S SDL2-2.0.20 -B "$LIBBUILD/sdl2" -G Ninja \
        -D CMAKE_BUILD_TYPE=Release \
        -D CMAKE_INSTALL_PREFIX="$LIBPREFIX" \
        -D SDL_AUDIO=OFF \
        -D SDL_VIDEO=OFF \
        -D SDL_JOYSTICK=ON \
        -D SDL_HAPTIC=ON \
        -D SDL_SENSOR=ON \
        -D SDL_RENDER=OFF \
        -D SDL_POWER=OFF \
        -D SDL_THREADS=ON \
        -D SDL_TIMERS=ON \
        -D SDL_FILE=ON \
        -D SDL_LOADSO=ON \
        -D SDL_CPUINFO=ON \
        -D SDL_FILESYSTEM=ON \
        -D SDL_DLOPEN=ON \
        -D SDL_SHARED=ON \
        -D SDL_STATIC=OFF \
        -D SDL_TEST=OFF
    cmake --build "$LIBBUILD/sdl2" -j"$(nproc)"
    cmake --install "$LIBBUILD/sdl2"
    info "SDL2 installed."
fi

# -----------------------------------------------------------------------
# Step 1: Build 86Box
# -----------------------------------------------------------------------
# Point pkg-config at our custom libs FIRST so CMake doesn't pick up stale
# system .pc files (e.g. rtmidi, fluidsynth from old -dev packages).
export PKG_CONFIG_PATH="$LIBPREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

info "Configuring 86Box..."
cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja \
    -D CMAKE_TOOLCHAIN_FILE="$SRC_DIR/cmake/flags-gcc-aarch64.cmake" \
    -D CMAKE_BUILD_TYPE=Release \
    -D NEW_DYNAREC=ON \
    -D QT=ON \
    -D CMAKE_INSTALL_PREFIX=/usr/local \
    -D CMAKE_PREFIX_PATH="$LIBPREFIX" \
    -D CMAKE_EXPORT_COMPILE_COMMANDS=ON

info "Building 86Box..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

# -----------------------------------------------------------------------
# Step 2: Prepare AppDir structure
# -----------------------------------------------------------------------
info "Preparing AppDir..."
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/local/bin"
mkdir -p "$APPDIR/usr/lib"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"
mkdir -p "$APPDIR/usr/share/metainfo"

# Install binary
cp "$BUILD_DIR/src/86Box" "$APPDIR/usr/local/bin/"
strip "$APPDIR/usr/local/bin/86Box"

# Install mdsx plugin if built
if [[ -f "$BUILD_DIR/src/mdsx.so" ]]; then
    cp "$BUILD_DIR/src/mdsx.so" "$APPDIR/usr/lib/"
    info "Included mdsx.so MIDI plugin"
fi

# Install custom-built libraries into AppDir
for lib in libopenal librtmidi libfluidsynth libSDL2; do
    for f in "$LIBPREFIX/lib/${lib}"*.so*; do
        [[ -e "$f" ]] && cp -a "$f" "$APPDIR/usr/lib/"
    done
done
info "Installed custom-built libraries into AppDir"

# Desktop file — use source tree version
cp "$SRC_DIR/src/unix/assets/net.86box.86Box.desktop" \
   "$APPDIR/usr/share/applications/"

# Metainfo — use source tree version
cp "$SRC_DIR/src/unix/assets/net.86box.86Box.metainfo.xml" \
   "$APPDIR/usr/share/metainfo/"

# Icon — use the 256x256 from source tree
if [[ -f "$SRC_DIR/src/unix/assets/256x256/net.86box.86Box.png" ]]; then
    cp "$SRC_DIR/src/unix/assets/256x256/net.86box.86Box.png" \
       "$APPDIR/usr/share/icons/hicolor/256x256/apps/"
else
    warn "No icon found — AppImage will work but have no icon"
fi

# NOTE: Do NOT create top-level symlinks (.DirIcon, .desktop, icon.png)
# appimage-builder handles those automatically and will error on conflicts.

# -----------------------------------------------------------------------
# Step 3: Run appimage-builder
# -----------------------------------------------------------------------
info "Running appimage-builder..."
cd /build
cp "$SRC_DIR/appimage/AppImageBuilder.yml" /build/

appimage-builder --recipe AppImageBuilder.yml --skip-tests

# -----------------------------------------------------------------------
# Step 4: Copy output
# -----------------------------------------------------------------------
info "Copying AppImage to output..."
mkdir -p "$OUTPUT_DIR"
cp /build/*.AppImage "$OUTPUT_DIR/" 2>/dev/null || true
cp /build/*.AppImage.zsync "$OUTPUT_DIR/" 2>/dev/null || true

APPIMAGE=$(ls "$OUTPUT_DIR"/*.AppImage 2>/dev/null | head -1)
if [[ -n "$APPIMAGE" ]]; then
    SIZE=$(du -h "$APPIMAGE" | cut -f1)
    info "AppImage built: $(basename "$APPIMAGE") ($SIZE)"
    info "Output at: $OUTPUT_DIR/"
else
    error "No AppImage produced — check build output above"
fi
