# CLAUDE.md — Agent guide for scad.cat / parametrix

## What this project is

A browser-native (+ Linux/macOS desktop) **parametric 3D model configurator**
built on Dear ImGui + SDL2 + OpenGL3. Live at [scad.cat](https://scad.cat).

The goal is to give users a fast, responsive web app for configuring popular
parametric OpenSCAD projects — Gridfinity bins, Gridfinity baseplates,
OpenGrid, and many more. Each "project" is an OpenSCAD library with exposed
parameters; the app provides a tailored UI for those parameters, compiles the
resulting SCAD source on the fly, and renders the mesh in a 3D viewport.

The current codebase renders a single example SCAD file and exposes a raw
source editor — that is scaffolding for the above, not the end goal. Future
work will add per-project parameter panels that drive SCAD code generation
without exposing raw source to the user.

---

## File map

| Path | Responsibility |
|------|---------------|
| `src/main.cpp` | SDL/GL init, AppState, ImGui render loop, platform glue |
| `src/scad_eval.h/cpp` | OpenSCAD evaluation — three backends behind one interface |
| `src/viewer.h/cpp` | FBO-based 3D mesh viewer, STL parser, orbit camera |
| `src/i18n.h` | `.po` parser and `_()` underscore lookup function |
| `web/shell.html` | Emscripten HTML shell; JS clipboard + browser-shortcut handling |
| `i18n/*.po` | Translations (GNU gettext format) |
| `models/example.scad` | Default model loaded on startup |
| `CMakeLists.txt` | Single build file for desktop + WASM |
| `CMakePresets.json` | `desktop` / `desktop-release` / `wasm` presets |
| `mise.toml` | Toolchain (cmake, ninja, emsdk) + `mise run desktop/wasm` tasks |
| `.github/workflows/deploy.yml` | Build WASM → deploy to Cloudflare Pages |
| `openscad-lib/CMakeLists.txt` | Builds OpenSCAD headless as a static library for desktop |
| `openscad-lib/Dockerfile` | Multi-stage Emscripten build of OpenSCAD to WASM |
| `openscad-lib/build-wasm.sh` | Runs the Docker WASM build; deposits artifacts |
| `openscad-lib/src/openscad_api.h/cpp` | C wrapper: `openscad_evaluate()` → binary STL bytes |
| `openscad-lib/upstream/openscad/` | git submodule: `openscad/openscad` main branch |

---

## Architecture

### Three evaluation backends, one interface

`src/scad_eval.h` is unchanged regardless of backend. The backend is selected
at compile time:

| Macro / platform | Backend | How |
|---|---|---|
| `__EMSCRIPTEN__` | WASM async | `openscad.js` ES6 module loaded in `shell.html`; fresh instance per eval |
| `USE_OPENSCAD_LIB` (desktop) | Linked library | `openscad_evaluate()` in `openscad-lib/src/openscad_api.cpp` calls `openscad_main()` directly |
| neither (desktop) | Subprocess | `std::system("openscad ...")` with temp files; result read from `/tmp/*.stl` |

The linked-library and WASM backends both use the same OpenSCAD source tree
(`openscad-lib/upstream/openscad`), the same Manifold geometry kernel, and the
same compile flags — so behaviour is identical across platforms.

### openscad-lib subproject

`openscad-lib/` wraps the upstream OpenSCAD CMake build:

- **Desktop** (`cmake -DUSE_OPENSCAD_LIB=ON`):
  `add_subdirectory(openscad-lib)` builds `OpenSCADLibInternal` (OpenSCAD's
  own static library target) plus the `openscad_api` wrapper. Key flags:
  `HEADLESS=ON NULLGL=ON ENABLE_CGAL=OFF ENABLE_MANIFOLD=ON EXPERIMENTAL=ON`.
  No Qt, no OpenGL, no CGAL/GMP/MPFR.

- **WASM** (`./openscad-lib/build-wasm.sh`):
  Docker multi-stage build using Emscripten 4.0.10. Compiles all dependencies
  from source into the Emscripten sysroot, then builds OpenSCAD with `emcmake
  cmake`. Produces `openscad.js` (ES6 module factory) + `openscad.wasm`.

The OpenSCAD executable output name on Linux is `openscad`, so Emscripten
produces `openscad.js` + `openscad.wasm` — matching the ES6 import already in
`shell.html`.

### Why fresh WASM instances per evaluation

`openscad-wasm` calls `exit()` internally after `callMain()` returns. Emscripten
tears down the runtime on exit. Re-calling `callMain()` on the same instance
crashes with a cryptic integer error. **Fix:** create a new `OpenSCAD()` factory
instance for every evaluation.

The factory is stored as `globalThis._openscadFactory` in `shell.html`. It must
not be stored as `globalThis.OpenSCAD` because Emscripten's own shutdown
overwrites that name.

### Why polling instead of Asyncify

The WASM evaluation is async JS. The C++ main loop can't `await` it
without Asyncify (which adds ~40% binary size and other constraints). Instead:

- `scad_eval_start()` kicks off an async IIFE and writes status to
  `globalThis._scadEvalStatus`.
- `scad_eval_poll()` reads that global each frame (0 = running, 1 = done, -1 = error).
- `scad_eval_finish()` retrieves the result bytes when poll returns 1.

### openscad_api.cpp global-state note

OpenSCAD's evaluator has process-global state (`commandline_commands`,
`Builtins` singleton, etc.). `openscad_evaluate()` resets `commandline_commands`
before each call so `-D` variable definitions from one call don't bleed into the
next. Calls must be sequential — the API is **not reentrant**.

### 3D viewport z-order

The viewport window uses `ImGuiWindowFlags_NoBringToFrontOnFocus`. This flag
also controls where **new** windows start in ImGui's z-stack: any window created
with this flag starts at the bottom. Overlay windows (download button, etc.)
must **not** have this flag, or they will appear behind the viewport.

---

## Build system notes

### WASM artifact lifecycle

`web/openscad-wasm-dist/` holds the built WASM files. They are **not**
downloaded at CMake time anymore. Workflow:

1. `./openscad-lib/build-wasm.sh` — builds and places `openscad.js` +
   `openscad.wasm` in `web/openscad-wasm-dist/`
2. `mise run wasm` — CMake copies them to the build directory at link time.

If the files are missing, CMake fails with a message pointing to `build-wasm.sh`.

The legacy `openscad.wasm.js` (present only from the old 2022 pre-built
artifacts) is copied if it exists, silently ignored if not.

### openscad-lib submodule initialisation

`openscad-lib/upstream/openscad` is a git submodule. Its own nested submodules
(`submodules/manifold`, `submodules/Clipper2`) must be initialised before
building:

```bash
git submodule update --init openscad-lib/upstream/openscad
git -C openscad-lib/upstream/openscad submodule update --init \
    submodules/manifold submodules/Clipper2
```

`build-wasm.sh` does this automatically if needed.

### `SHELL:` prefix on `--embed-file`

CMake deduplicates repeated identical link flags. Multiple `--embed-file` flags
look identical to CMake's deduplicator. Prefix each with `SHELL:` to prevent
this:

```cmake
target_link_options(parametrix PRIVATE
    "SHELL:--embed-file ${CMAKE_SOURCE_DIR}/models/foo@/models/foo"
)
```

### `LINK_DEPENDS` for shell.html

Emscripten embeds `shell.html` at link time. CMake doesn't track it as a
dependency by default. Added explicitly:

```cmake
set_property(TARGET parametrix APPEND PROPERTY LINK_DEPENDS
    ${CMAKE_SOURCE_DIR}/web/shell.html)
```

### `MODELS_PATH` and `I18N_PATH`

Compile-time string macros:
- Desktop: absolute path to the source tree (`${CMAKE_SOURCE_DIR}/models/`)
- WASM: virtual FS path (`/models/`), with files embedded via `--embed-file`

### Desktop GL without GLAD/GLEW

`GL_GLEXT_PROTOTYPES` is defined for desktop builds. This tells Mesa's `libGL.so`
to expose OpenGL 3+ function prototypes directly, avoiding the need for a
function-pointer loader.

---

## Clipboard (WASM)

`paste` events do not fire on `<canvas>` elements. The approach:

1. `shell.html` registers a `keydown` capture listener.
2. On Ctrl+V (non-repeat), it moves focus to a hidden `<textarea>`.
3. The browser fires `paste` on the textarea; the listener captures the text
   into `globalThis._pendingPaste` and restores canvas focus.
4. Each frame, `js_take_pending_paste()` retrieves and clears the pending text,
   which is injected via `ImGui::GetIO().AddInputCharactersUTF8()`.

Copy (`SetClipboardTextFn`) calls `navigator.clipboard.writeText()` directly.

Browser shortcuts (F12, Ctrl+Shift+I, etc.) are rescued from SDL's
`preventDefault()` via a capture-phase `stopImmediatePropagation()` call.

---

## i18n

- Translation files: `i18n/<lang>.po` (GNU gettext format, UTF-8).
- The `_()` function in `src/i18n.h` looks up the key in the loaded map, falling
  back to the key itself (so English requires no file load).
- Call `i18n::set_language(Language::X, I18N_PATH)` to switch language at runtime.
- Add new languages by: (1) creating `i18n/<lang>.po`, (2) adding a `Language`
  enum value, (3) adding a menu item and `set_language` call in `main.cpp`,
  (4) embedding the file for WASM in `CMakeLists.txt`.

---

## Adding a new UI panel

1. Add any new state to `AppState` in `main.cpp`.
2. Add a `bool show_X = false` flag for toggleable panels.
3. Render the panel inside `MainLoopStep()`, before the 3D viewport section.
4. Do **not** use `ImGuiWindowFlags_NoBringToFrontOnFocus` on overlay windows
   (see z-order note above).
5. Wrap all user-visible strings with `_()`.
