FROM debian:bullseye

ENV DEBIAN_FRONTEND=noninteractive

# Build tools
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    wget \
    file \
    python3 \
    python3-pip \
    python3-setuptools \
    patchelf \
    desktop-file-utils \
    libgdk-pixbuf2.0-dev \
    squashfs-tools \
    zsync \
    && rm -rf /var/lib/apt/lists/*

# 86Box build dependencies (libs we DON'T compile from source)
RUN apt-get update && apt-get install -y \
    libfreetype-dev \
    libpng-dev \
    libc6-dev \
    libslirp-dev \
    libvdeplug-dev \
    libserialport-dev \
    libevdev-dev \
    libxi-dev \
    libxkbcommon-x11-dev \
    libsndfile1-dev \
    qtbase5-dev \
    qtbase5-private-dev \
    qttools5-dev \
    qttranslations5-l10n \
    qtwayland5 \
    && rm -rf /var/lib/apt/lists/*

# Dev packages needed to compile OpenAL, SDL2, rtmidi, FluidSynth from source
RUN apt-get update && apt-get install -y \
    libasound2-dev \
    libpipewire-0.3-dev \
    libjack-jackd2-dev \
    libsamplerate-dev \
    libdrm-dev \
    libudev-dev \
    libdbus-1-dev \
    libwayland-dev \
    libinstpatch-dev \
    && rm -rf /var/lib/apt/lists/*

# Install appimage-builder (pinned to same commit as official 86Box)
RUN pip3 install git+https://github.com/AppImageCrafters/appimage-builder.git@22fefa298f9cee922a651a6f65a46fe0ccbfa34e

# Download appimagetool for aarch64
RUN wget -q "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-aarch64.AppImage" \
    -O /usr/local/bin/appimagetool && \
    chmod +x /usr/local/bin/appimagetool && \
    cd /opt && /usr/local/bin/appimagetool --appimage-extract 2>/dev/null || true

WORKDIR /build
COPY build-appimage.sh /build/
RUN chmod +x /build/build-appimage.sh

ENTRYPOINT ["/build/build-appimage.sh"]
