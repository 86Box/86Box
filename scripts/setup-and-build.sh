#!/bin/bash
#
# 86Box macOS ARM64 (Apple Silicon) — Setup & Build Script
#
# Usage:
#   ./scripts/setup-and-build.sh deps      Install required Homebrew dependencies
#   ./scripts/setup-and-build.sh build      Clean configure + build + codesign .app
#   ./scripts/setup-and-build.sh            Show help
#

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

# ---------------------------------------------------------------------------
# deps — install Homebrew packages
# ---------------------------------------------------------------------------
cmd_deps() {
    info "Checking for Homebrew..."
    if ! command -v brew &>/dev/null; then
        error "Homebrew not found. Install it from https://brew.sh"
    fi

    info "Installing required dependencies..."
    brew install cmake sdl2 rtmidi openal-soft fluidsynth libslirp vde \
                 libserialport qt@5

    info "Dependencies installed."
    echo ""
    info "Next step:  ./scripts/setup-and-build.sh build"
}

# ---------------------------------------------------------------------------
# build — configure, compile, codesign
# ---------------------------------------------------------------------------
cmd_build() {
    cd "$REPO_ROOT"

    # Verify ARM64 macOS
    if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
        error "This script targets macOS on Apple Silicon (arm64)."
    fi

    NCPU="$(sysctl -n hw.ncpu)"
    BUILD_DIR="build"

    # Resolve Homebrew prefixes
    QT5_ROOT="$(brew --prefix qt@5)"
    OPENAL_ROOT="$(brew --prefix openal-soft)"
    LIBSERIALPORT_ROOT="$(brew --prefix libserialport)"

    # Sanity-check that key deps exist
    for pkg in "$QT5_ROOT" "$OPENAL_ROOT" "$LIBSERIALPORT_ROOT"; do
        [[ -d "$pkg" ]] || error "Missing dependency at $pkg — run:  ./scripts/setup-and-build.sh deps"
    done

    # Clean previous build
    if [[ -d "$BUILD_DIR" ]]; then
        info "Removing old build directory..."
        rm -rf "$BUILD_DIR"
    fi

    # Configure
    info "Configuring (CMake)..."
    cmake -S . -B "$BUILD_DIR" --preset regular \
        --toolchain ./cmake/llvm-macos-aarch64.cmake \
        -D NEW_DYNAREC=ON \
        -D QT=ON \
        -D Qt5_ROOT="$QT5_ROOT" \
        -D Qt5LinguistTools_ROOT="$QT5_ROOT" \
        -D OpenAL_ROOT="$OPENAL_ROOT" \
        -D LIBSERIALPORT_ROOT="$LIBSERIALPORT_ROOT"

    # Build
    info "Building with $NCPU parallel jobs..."
    cmake --build "$BUILD_DIR" -j"$NCPU"

    # Codesign with JIT entitlements
    info "Codesigning 86Box.app (ad-hoc with JIT entitlement)..."
    codesign -s - \
        --entitlements src/mac/entitlements.plist \
        --force \
        "$BUILD_DIR/src/86Box.app"

    echo ""
    info "Build complete!  App is at:"
    echo "  $REPO_ROOT/$BUILD_DIR/src/86Box.app"
    echo ""
    info "To run:  open $BUILD_DIR/src/86Box.app"
}

# ---------------------------------------------------------------------------
# help
# ---------------------------------------------------------------------------
cmd_help() {
    echo "86Box macOS ARM64 Build Script"
    echo ""
    echo "Usage:"
    echo "  ./scripts/setup-and-build.sh deps    Install Homebrew dependencies"
    echo "  ./scripts/setup-and-build.sh build   Clean build + codesign .app"
    echo ""
    echo "Requirements:"
    echo "  - macOS on Apple Silicon (arm64)"
    echo "  - Homebrew (https://brew.sh)"
    echo "  - Xcode Command Line Tools (xcode-select --install)"
}

# ---------------------------------------------------------------------------
# dispatch
# ---------------------------------------------------------------------------
case "${1:-help}" in
    deps)  cmd_deps  ;;
    build) cmd_build ;;
    *)     cmd_help  ;;
esac
