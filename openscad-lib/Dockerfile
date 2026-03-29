# ─────────────────────────────────────────────────────────────────────────────
# Multi-stage Dockerfile: builds OpenSCAD to WebAssembly
#
# Build the WASM artifacts by running build-wasm.sh from the repo root.
#
# Produces openscad.js + openscad.wasm, compatible with the ES6 import in
# web/shell.html.
#
# Base image: openscad/wasm-base has all dependencies pre-built for
# Emscripten 4.0.10 (GMP, MPFR, CGAL, Boost, Freetype, HarfBuzz, FontConfig,
# libzip, double-conversion, Eigen, etc.)
# ─────────────────────────────────────────────────────────────────────────────

FROM docker.io/openscad/wasm-base:latest AS openscad

ARG CMAKE_BUILD_TYPE=Release

# Copy OpenSCAD source (and its already-initialised submodules)
COPY openscad-lib/upstream/openscad /openscad

WORKDIR /openscad

# pkg-config must find the WASM sysroot packages
ENV SYSROOT=/emsdk/upstream/emscripten/cache/sysroot
ENV PKG_CONFIG_PATH="${SYSROOT}/lib/pkgconfig"

RUN emcmake cmake -B /build/openscad \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
        -DEXPERIMENTAL=ON \
        -DSNAPSHOT=ON \
        -DENABLE_TESTS=OFF \
        -DENABLE_PYTHON=OFF \
        -DOFFLINE_DOCS=OFF \
        -DINFO=OFF \
        -DCMAKE_REQUIRE_FIND_PACKAGE_Lib3MF=OFF && \
    cmake --build /build/openscad --parallel

# ─── artifacts ───────────────────────────────────────────────────────────────
# The final build outputs are openscad.js and openscad.wasm (OpenSCAD's
# CMakeLists.txt sets OUTPUT_NAME "openscad" on Linux, and Emscripten appends
# .js / .wasm).  These are copied to /out so build-wasm.sh can extract them.
FROM docker.io/openscad/wasm-base:latest AS artifacts
COPY --from=openscad /build/openscad/openscad.js   /out/openscad.js
COPY --from=openscad /build/openscad/openscad.wasm /out/openscad.wasm

CMD ["/bin/sh", "-c", "echo 'Mount /out to extract artifacts'"]
