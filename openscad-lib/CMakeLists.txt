cmake_minimum_required(VERSION 3.20)
project(openscad_lib CXX C)

# ─── Set all OpenSCAD options before add_subdirectory so they land in the
#     cache before OpenSCAD's own option() declarations see them. ────────────
set(HEADLESS              ON  CACHE BOOL "" FORCE)
set(NULLGL                ON  CACHE BOOL "" FORCE)
set(ENABLE_CGAL           OFF CACHE BOOL "" FORCE)
set(ENABLE_MANIFOLD       ON  CACHE BOOL "" FORCE)
set(EXPERIMENTAL          ON  CACHE BOOL "" FORCE)  # enables Manifold code paths
set(USE_BUILTIN_MANIFOLD  ON  CACHE BOOL "" FORCE)  # use submodules/manifold
set(USE_BUILTIN_CLIPPER2  ON  CACHE BOOL "" FORCE)  # use submodules/Clipper2
set(USE_MIMALLOC          OFF CACHE BOOL "" FORCE)  # skip mimalloc submodule
set(ENABLE_TESTS          OFF CACHE BOOL "" FORCE)
set(ENABLE_GUI_TESTS      OFF CACHE BOOL "" FORCE)
set(OFFLINE_DOCS          OFF CACHE BOOL "" FORCE)
set(ENABLE_PYTHON         OFF CACHE BOOL "" FORCE)
set(ENABLE_CAIRO          OFF CACHE BOOL "" FORCE)
set(SNAPSHOT              OFF CACHE BOOL "" FORCE)
set(INFO                  OFF CACHE BOOL "" FORCE)
# Lib3MF is optional for our use case (we only export STL, not 3MF).
set(CMAKE_REQUIRE_FIND_PACKAGE_Lib3MF OFF CACHE BOOL "" FORCE)
# Manifold parallelism: keep TBB enabled on desktop for performance.
# (Emscripten overrides this to OFF automatically in OpenSCAD's CMakeLists.txt)

# ─── OpenSCAD upstream ───────────────────────────────────────────────────────
# EXCLUDE_FROM_ALL: only build targets that our openscad_api target depends on
# (OpenSCADLibInternal + svg).  OpenSCADExe is never linked and won't be built.
add_subdirectory(upstream/openscad EXCLUDE_FROM_ALL)

# ─── Our thin C wrapper ──────────────────────────────────────────────────────
add_library(openscad_api STATIC
    src/openscad_api.cpp
)

# OpenSCADLibInternal already exposes upstream/openscad/src as a PUBLIC include
# dir, so openscad.h is reachable.  We only need our own src/ for the header.
target_include_directories(openscad_api
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(openscad_api PUBLIC OpenSCADLibInternal)

set_target_properties(openscad_api PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
