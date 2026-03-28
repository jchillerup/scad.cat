# Build a minimal image containing OpenSCAD (CLI only, headless).
# Uses --appimage-extract, which requires seccomp=unconfined at build time:
#
#   podman build --security-opt seccomp=unconfined \
#       -t parametrix-openscad -f docker/openscad.dockerfile .
#
# The resulting image needs no special runtime privileges.

FROM ubuntu:22.04

ARG OPENSCAD_VERSION=2026.03.22
ARG OPENSCAD_URL=https://files.openscad.org/snapshots/OpenSCAD-${OPENSCAD_VERSION}-x86_64.AppImage

# Runtime libs needed by the extracted OpenSCAD binary
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
        wget \
        libglu1-mesa \
        libxi6 \
        libxrender1 \
        libfontconfig1 \
        libharfbuzz0b \
        libgl1 \
        libegl1 \
        libxext6 \
        libx11-6 \
    && rm -rf /var/lib/apt/lists/*

# Download AppImage and extract it.
# AppRun is a loader binary that doesn't work in a container; instead we
# create a thin wrapper script that sets the library path and calls the real binary.
RUN wget -q "$OPENSCAD_URL" -O /tmp/openscad.AppImage \
    && chmod +x /tmp/openscad.AppImage \
    && cd /opt \
    && /tmp/openscad.AppImage --appimage-extract \
    && mv /opt/squashfs-root /opt/openscad \
    && rm /tmp/openscad.AppImage

RUN printf '#!/bin/sh\nexec env LD_LIBRARY_PATH=/opt/openscad/usr/lib:"$LD_LIBRARY_PATH" /opt/openscad/usr/bin/openscad "$@"\n' \
    > /usr/local/bin/openscad \
    && chmod +x /usr/local/bin/openscad

# Smoke test
RUN openscad --version
