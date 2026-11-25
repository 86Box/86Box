# 86Box, headless, in a container: the machine runs with no display, and a
# program (a test harness, a script, an AI agent) drives it through the
# automation socket. See container/README.md.
#
#   docker build -f container/Dockerfile -t 86box-headless .
#
# Build arguments:
#   WITH_ROMS=1        clone the ROM set (github.com/86Box/roms) into the image;
#                      otherwise mount one at /roms
#   ROMS_REF=master    the ROM repository's branch or tag to clone
#   BUILD_PRESET       the CMake preset (regular, optimized, debug, ...)

ARG BASE_IMAGE=ubuntu:26.04

# ---------------------------------------------------------------------------
FROM ${BASE_IMAGE} AS build

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config ca-certificates git \
        libfreetype-dev libsdl3-dev libpng-dev librtmidi-dev libopenal-dev \
        libslirp-dev libfluidsynth-dev libvdeplug-dev libserialport-dev \
        libzstd-dev libarchive-dev libsndfile1-dev libevdev-dev \
    && rm -rf /var/lib/apt/lists/*

ARG BUILD_PRESET=regular
WORKDIR /src
COPY . .
# The SDL frontend: no Qt to ship, and no window, display or GPU needed with
# --headless. AUTOMATION is the control socket.
RUN cmake -S . -B /build -G Ninja --preset ${BUILD_PRESET} \
        -D QT=OFF -D AUTOMATION=ON -D CMAKE_INSTALL_PREFIX=/opt/86box \
    && cmake --build /build \
    && cmake --install /build --strip \
    && for lib in $(ldd /opt/86box/bin/86Box | awk '/=> \// { print $3 }'); do \
           dpkg -S "$lib" 2>/dev/null || dpkg -S "$(realpath "$lib")"; \
       done | cut -d: -f1 | sort -u > /opt/86box/runtime-packages \
    && echo "Runtime packages:" && cat /opt/86box/runtime-packages

ARG WITH_ROMS=0
ARG ROMS_REF=master
RUN mkdir -p /roms && if [ "${WITH_ROMS}" = "1" ]; then \
        git clone --depth 1 --branch "${ROMS_REF}" https://github.com/86Box/roms.git /roms \
        && rm -rf /roms/.git; \
    fi

# ---------------------------------------------------------------------------
FROM ${BASE_IMAGE}

ARG DEBIAN_FRONTEND=noninteractive
# Runtime libraries: the packages that own the libraries the binary links,
# listed in the build stage, so the list can't fall behind the build.
COPY --from=build /opt/86box /opt/86box
RUN apt-get update \
    && xargs -a /opt/86box/runtime-packages apt-get install -y --no-install-recommends tini \
    && missing="$(ldd /opt/86box/bin/86Box | grep 'not found' || true)" \
    && if [ -n "$missing" ]; then echo "Missing libraries:"; echo "$missing"; exit 1; fi \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /roms /roms
COPY container/entrypoint.sh /usr/local/bin/86box-entrypoint

# Unprivileged: UID 1000 by default. Run with --user to match the owner of
# the volumes (the UNIX socket is owner-only, 0600); any UID works, as HOME
# (where 86Box keeps its global settings) is writable by all.
RUN mkdir -p /vm /run/86box /home/86box \
    && chmod 1777 /vm /run/86box /home/86box \
    && chmod 755 /usr/local/bin/86box-entrypoint
USER 1000:1000
# OpenAL's null backend: there is no sound device to find, and probing for
# one fills the log. Set ALSOFT_DRIVERS=pulse (and mount the socket) to hear it.
ENV HOME=/home/86box \
    PATH=/opt/86box/bin:$PATH \
    ALSOFT_DRIVERS=null \
    EMU_AUTOMATION=unix:/run/86box/86box.sock

VOLUME ["/vm", "/run/86box"]
EXPOSE 7086
# tini reaps children and passes SIGTERM (docker stop) on to 86Box, which
# shuts the machine down cleanly with --headless.
ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/86box-entrypoint"]
