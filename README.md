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
| CAD kernel | [OpenSCAD](https://openscad.org/) (desktop subprocess / [openscad-wasm](https://github.com/openscad/openscad-wasm) in browser) |
| Platform | SDL2 + Emscripten (single codebase, desktop + WASM) |
| Build | CMake + Ninja, toolchain managed by [mise](https://mise.jdx.dev/) |

---

## Building

### Prerequisites

```bash
# Fedora
sudo dnf install SDL2-devel openscad

# Ubuntu/Debian
sudo apt install libsdl2-dev openscad

# macOS
brew install sdl2 openscad
```

[mise](https://mise.jdx.dev/) manages the rest (CMake, Ninja, Emscripten):

```bash
mise install
```

### Desktop

```bash
mise run desktop
# Opens the app. Subsequent runs are incremental.
```

### Browser (WASM)

```bash
mise run wasm
# Serves at http://localhost:8080/parametrix.html
```

The first `mise run wasm` downloads the openscad-wasm pre-built assets (~8 MB, cached in `web/openscad-wasm-dist/`).

---

## How it works

- Write or paste OpenSCAD source in the **SCAD Source** panel
- The model rebuilds automatically ~100 ms after you stop typing
- Orbit the result in the **3D Viewport** (drag to rotate, scroll to zoom)
- The view cube (top-right corner) snaps to axis-aligned views

On desktop, OpenSCAD runs as a subprocess. In the browser, a fresh [openscad-wasm](https://github.com/openscad/openscad-wasm) instance evaluates each change entirely client-side — no server, no data sent anywhere.

---

## Project structure

```
src/
  main.cpp          # app loop, SCAD editor UI, platform glue
  viewer.h/cpp      # FBO-based 3D mesh viewer
models/
  example.scad      # starter model (cube + cylinder)
external/
  imgui/            # Dear ImGui (submodule)
  ImGuizmo/         # view gizmo (submodule)
  glm/              # math (submodule)
web/
  shell.html        # Emscripten HTML shell
  openscad-wasm-dist/  # downloaded at configure time, gitignored
```

---

## Contributing

This is early and moving fast. If you have a Gridfinity bin, an Underware clip, or any other parametric design that would be a good built-in example — open a PR. If the code offends you architecturally, open an issue. If you want to help build the parameter UI, the community library browser, or the slicer integration, get in touch.

---

## License

TBD
