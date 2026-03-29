# scad.cat
This line of text is the only thing written by a human. Hi! I love the different parametric generators out there for Gridfinity, OpenGrid, and so on, but I don't particularly like the fact that the best one is deep into the Bambu ecosystem.


> ⚠️ This is a vibe coded project — built fast, iterated faster, held together with enthusiasm and OpenSCAD. Contributions, chaos, and opinions welcome.

Live at [scad.cat](https://scad.cat)

---

## What is this?

scad.cat is a browser-based (and desktop-native) editor aimed at making parametric 3D printing designs fast and approachable. The goal is a tight feedback loop: tweak parameters, see the model update in real time, download your STL and go print it.

The initial focus is on the ecosystem of modular organisation systems — [Gridfinity](https://gridfinity.xyz/), [OpenGrid](https://www.printables.com/model/578710), [Underware](https://www.printables.com/model/527382), and the many community extensions built on top of them — where the same base model gets printed dozens of times with slightly different dimensions. That kind of workflow is exactly what parametric SCAD is good at, and exactly what this tool is designed to make painless.

Longer term: in-browser slicing and direct print dispatch. Not yet — but it's the direction.

---

## Stack

| Layer | Technology |
|---|---|
| UI | [Dear ImGui](https://github.com/ocornut/imgui) |
| 3D viewport | OpenGL 3 / WebGL 2, FBO rendering, orbit camera |
| View gizmo | [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) |
| Math | [GLM](https://github.com/g-truc/glm) |
| CAD kernel | OpenSCAD built from source (desktop: linked library or subprocess; browser: WASM compiled via `openscad-lib/`) |
| Geometry backend | [Manifold](https://github.com/elalish/manifold) — no CGAL |
| Platform | SDL2 + Emscripten (single codebase, desktop + WASM) |
| Build | CMake + Ninja, toolchain managed by [mise](https://mise.jdx.dev/) |

---

## Building

### Prerequisites

[mise](https://mise.jdx.dev/) manages CMake, Ninja, and Emscripten:

```bash
mise install
```

#### Desktop (subprocess mode — default, simplest)

Requires a system OpenSCAD and SDL2:

```bash
# Fedora
sudo dnf install SDL2-devel openscad

# Ubuntu/Debian
sudo apt install libsdl2-dev openscad

# macOS
brew install sdl2 openscad
```

#### Desktop (linked-library mode — feature-identical to WASM)

Requires SDL2 plus the OpenSCAD build dependencies:

```bash
# Fedora
sudo dnf install SDL2-devel \
    boost-devel eigen3-devel harfbuzz-devel fontconfig-devel glib2-devel \
    double-conversion-devel libzip-devel freetype-devel libxml2-devel \
    flex bison

# Ubuntu/Debian
sudo apt install libsdl2-dev \
    libboost-dev libeigen3-dev libharfbuzz-dev libfontconfig1-dev libglib2.0-dev \
    libdouble-conversion-dev libzip-dev libfreetype-dev libxml2-dev \
    flex bison
```

#### WASM (Docker or Podman)

```bash
# Fedora — podman-docker provides a docker shim
sudo dnf install podman-docker

# Ubuntu/Debian
sudo apt install docker.io
```

---

### Desktop

#### Subprocess mode (default)

OpenSCAD runs as a system subprocess. Fastest to get started.

```bash
mise run desktop
```

Override the command (e.g. for a container-wrapped OpenSCAD):

```bash
cmake --preset desktop -DOPENSCAD_CMD="podman run --rm -v /tmp:/tmp my-openscad openscad"
```

#### Linked-library mode

OpenSCAD is compiled from the `openscad-lib/upstream/openscad` submodule and
linked directly into the parametrix binary. No subprocess, no system OpenSCAD
required, identical behaviour to the WASM build.

```bash
cmake --preset desktop -DUSE_OPENSCAD_LIB=ON
ninja -C build/desktop
./build/desktop/parametrix
```

The first configure will build OpenSCAD and its dependencies (Manifold,
Clipper2) from source — this takes a while but is fully incremental after that.

### Browser (WASM)

#### Step 1 — build the OpenSCAD WASM module (once)

```bash
./openscad-lib/build-wasm.sh
```

This builds OpenSCAD to WebAssembly using the `openscad/wasm-base` Docker
image maintained by the OpenSCAD project, which ships all dependencies
(Boost, CGAL, GMP, MPFR, Freetype, HarfBuzz, FontConfig, etc.) pre-built for
Emscripten 4.0.10. The resulting `openscad.js` and `openscad.wasm` are placed
in `web/openscad-wasm-dist/`.

Pass `--debug` for an unoptimised build.

#### Step 2 — build and serve parametrix

```bash
mise run wasm
# Serves at http://localhost:8080/parametrix.html
```

---

## How it works

- Write or paste OpenSCAD source in the **SCAD Source** panel
- The model rebuilds automatically ~100 ms after you stop typing
- Orbit the result in the **3D Viewport** (drag to rotate, scroll to zoom)
- The view cube (top-right corner) snaps to axis-aligned views

On desktop (subprocess mode), OpenSCAD runs as a child process. On desktop
(linked-library mode) and in the browser, OpenSCAD runs inside the same
process — compiled with the Manifold geometry backend and no Qt.

---

## Project structure

```
src/
  main.cpp            # app loop, UI, platform glue
  scad_eval.h/cpp     # OpenSCAD evaluation (three backends, same interface)
  viewer.h/cpp        # FBO-based 3D mesh viewer
openscad-lib/
  CMakeLists.txt      # builds OpenSCADLibInternal + openscad_api for desktop
  Dockerfile          # Docker build: openscad/wasm-base + OpenSCAD source → WASM
  build-wasm.sh       # runs the Docker build, deposits artifacts
  src/
    openscad_api.h/cpp  # thin C wrapper: openscad_evaluate() → binary STL
  upstream/
    openscad/           # git submodule: openscad/openscad (+ manifold, Clipper2)
models/
  example.scad        # starter model
external/
  imgui/              # Dear ImGui (submodule)
  ImGuizmo/           # view gizmo (submodule)
  glm/                # math (submodule)
web/
  shell.html          # Emscripten HTML shell
  openscad-wasm-dist/ # WASM build output (gitignored after first build-wasm.sh)
```

---

## Contributing

This is early and moving fast. If you have a Gridfinity bin, an Underware clip, or any other parametric design that would be a good built-in example — open a PR. If the code offends you architecturally, open an issue. If you want to help build the parameter UI, the community library browser, or the slicer integration, get in touch.

---

## License

TBD
