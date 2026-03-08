#!/bin/bash
#
# 86Box AppImage Builder — Host-side launcher
#
# Usage:
#   ./appimage/build.sh              Build the AppImage using Docker
#   ./appimage/build.sh shell        Drop into the container for debugging
#   ./appimage/build.sh clean        Remove build cache volume
#
# Output: ./appimage/output/86Box-*.AppImage
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_DIR="$SCRIPT_DIR/output"
IMAGE_NAME="86box-appimage-arm64"
VOLUME_NAME="86box-appimage-buildcache"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

# Check Docker
command -v docker &>/dev/null || error "Docker not found. Install Docker Desktop or colima."

# Detect platform — need linux/arm64 for native build
ARCH=$(uname -m)
if [[ "$ARCH" == "arm64" || "$ARCH" == "aarch64" ]]; then
    PLATFORM="linux/arm64"
else
    error "This script must run on ARM64 (Apple Silicon or aarch64 Linux)."
fi

# Handle 'clean' command
if [[ "${1:-}" == "clean" ]]; then
    info "Removing build cache volume..."
    docker volume rm "$VOLUME_NAME" 2>/dev/null && info "Done." || info "No cache to remove."
    exit 0
fi

# Build Docker image (cached after first run)
info "Building Docker image ($IMAGE_NAME)..."
docker build --platform "$PLATFORM" -t "$IMAGE_NAME" "$SCRIPT_DIR"

# Create persistent build cache volume (cmake won't rebuild from scratch)
docker volume create "$VOLUME_NAME" >/dev/null 2>&1 || true

# Prepare output directory
mkdir -p "$OUTPUT_DIR"

if [[ "${1:-}" == "shell" ]]; then
    info "Dropping into container shell..."
    info "  Source mounted at:  /src (read-only)"
    info "  Build cache at:     /build/cmake-build (persistent)"
    info "  Output at:          /output"
    docker run --rm -it \
        --platform "$PLATFORM" \
        -v "$REPO_ROOT:/src:ro" \
        -v "$OUTPUT_DIR:/output" \
        -v "$VOLUME_NAME:/build/cmake-build" \
        --entrypoint /bin/bash \
        "$IMAGE_NAME"
else
    info "Building AppImage..."
    docker run --rm \
        --platform "$PLATFORM" \
        -v "$REPO_ROOT:/src:ro" \
        -v "$OUTPUT_DIR:/output" \
        -v "$VOLUME_NAME:/build/cmake-build" \
        "$IMAGE_NAME"

    APPIMAGE=$(ls "$OUTPUT_DIR"/*.AppImage 2>/dev/null | head -1)
    if [[ -n "$APPIMAGE" ]]; then
        info "Done! AppImage at:"
        echo "  $APPIMAGE"
        ls -lh "$APPIMAGE"
    else
        error "Build failed — no AppImage produced"
    fi
fi
