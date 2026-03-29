#!/usr/bin/env bash
# build-wasm.sh — build the OpenSCAD WASM artifacts from scratch using Docker.
#
# Run from the repository root:
#   ./openscad-lib/build-wasm.sh
#
# The produced openscad.js and openscad.wasm are placed in web/openscad-wasm-dist/
# and will be picked up by the CMake WASM build of parametrix.
#
# Pass --debug to build a debug (unoptimised) build.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${REPO_ROOT}/web/openscad-wasm-dist"
IMAGE_TAG="parametrix-openscad-wasm"

BUILD_TYPE=Release
if [[ "${1:-}" == "--debug" ]]; then
  BUILD_TYPE=Debug
  IMAGE_TAG="${IMAGE_TAG}-debug"
fi

echo "==> Building OpenSCAD WASM (CMAKE_BUILD_TYPE=${BUILD_TYPE})"
echo "    Docker image: ${IMAGE_TAG}"
echo "    Output:       ${DIST_DIR}"

# Make sure we're in the repo root so the Docker context is the whole repo
cd "${REPO_ROOT}"

# Ensure the nested submodules that the Docker build needs are initialised
if [[ ! -f openscad-lib/upstream/openscad/submodules/manifold/CMakeLists.txt ]] || \
   [[ ! -f openscad-lib/upstream/openscad/submodules/sanitizers-cmake/CMakeLists.txt ]]; then
  echo "==> Initialising OpenSCAD nested submodules..."
  git -C openscad-lib/upstream/openscad submodule update --init \
      submodules/manifold submodules/Clipper2 submodules/sanitizers-cmake
fi

docker build \
  --target artifacts \
  --build-arg CMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -t "${IMAGE_TAG}" \
  -f openscad-lib/Dockerfile \
  .

echo "==> Extracting artifacts to ${DIST_DIR}/"
mkdir -p "${DIST_DIR}"

# Use `docker cp` to extract artifacts without bind-mount permission issues.
# (Podman rootless containers run as a non-root user and cannot write to
#  host directories mounted without --userns=keep-id.)
CID=$(docker create "${IMAGE_TAG}")
docker cp "${CID}:/out/openscad.js"   "${DIST_DIR}/openscad.js"
docker cp "${CID}:/out/openscad.wasm" "${DIST_DIR}/openscad.wasm"
docker rm "${CID}" > /dev/null

echo "==> Done: ${DIST_DIR}/openscad.js  ${DIST_DIR}/openscad.wasm"
