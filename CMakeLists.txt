cmake_minimum_required(VERSION 3.20)
project(parametrix CXX)
set(CMAKE_CXX_STANDARD 17)

# ---------------------------------------------------------------------------
# Dear ImGui
# ---------------------------------------------------------------------------
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# ---------------------------------------------------------------------------
# ImGuizmo  (single .cpp, no CMakeLists upstream)
# ---------------------------------------------------------------------------
set(IMGUIZMO_DIR ${CMAKE_SOURCE_DIR}/external/ImGuizmo)
set(IMGUIZMO_SOURCES ${IMGUIZMO_DIR}/ImGuizmo.cpp)

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(parametrix
    src/main.cpp
    src/scad_eval.cpp
    src/viewer.cpp
    src/generator.cpp
    src/generator_ui.cpp
    src/scad_builder.cpp
    ${IMGUI_SOURCES}
    ${IMGUIZMO_SOURCES}
)

target_include_directories(parametrix PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${IMGUIZMO_DIR}
    ${CMAKE_SOURCE_DIR}/external/glm
    ${CMAKE_SOURCE_DIR}/external/nlohmann_json/single_include
    ${CMAKE_SOURCE_DIR}/src
)

# ---------------------------------------------------------------------------
# Platform split
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
    set_target_properties(parametrix PROPERTIES OUTPUT_NAME "index")
    target_compile_options(parametrix PRIVATE -sUSE_SDL=2)
    target_link_options(parametrix PRIVATE
        -sUSE_SDL=2
        -sUSE_WEBGL2=1
        -sFULL_ES3=1
        -sALLOW_MEMORY_GROWTH=1
        -sNO_EXIT_RUNTIME=0
        -sDISABLE_EXCEPTION_CATCHING=1
        -sASSERTIONS=1
        --shell-file ${CMAKE_SOURCE_DIR}/web/shell.html
        # setValue / UTF8ToString used inside EM_JS blocks
        -sEXPORTED_RUNTIME_METHODS=UTF8ToString,setValue,lengthBytesUTF8,stringToUTF8
    )
    # Relink when shell.html changes (Emscripten embeds it at link time)
    set_property(TARGET parametrix APPEND PROPERTY LINK_DEPENDS
        ${CMAKE_SOURCE_DIR}/web/shell.html
    )
    target_compile_definitions(parametrix PRIVATE
        MODELS_PATH="/models/"
        I18N_PATH="/i18n/"
        SOURCE_ROOT="/"
    )

    # Embed the generators directory into the virtual FS.
    target_link_options(parametrix PRIVATE
        "SHELL:--embed-file ${CMAKE_SOURCE_DIR}/generators@/generators"
    )

    # Embed the example model files into the virtual FS.
    # SHELL: prefix prevents CMake from deduplicating repeated --embed-file flags.
    foreach(model_file example.stl example.scad)
        if(EXISTS ${CMAKE_SOURCE_DIR}/models/${model_file})
            target_link_options(parametrix PRIVATE
                "SHELL:--embed-file ${CMAKE_SOURCE_DIR}/models/${model_file}@/models/${model_file}"
            )
        endif()
    endforeach()

    # Embed translation files.
    foreach(lang_file en.po cat.po)
        target_link_options(parametrix PRIVATE
            "SHELL:--embed-file ${CMAKE_SOURCE_DIR}/i18n/${lang_file}@/i18n/${lang_file}"
        )
    endforeach()

    # ---------------------------------------------------------------------------
    # OpenSCAD WASM assets
    # Built from source by running:  ./openscad-lib/build-wasm.sh
    # Placed in web/openscad-wasm-dist/ and copied to the build dir at build time.
    # ---------------------------------------------------------------------------
    set(OSCAD_WASM_DIST "${CMAKE_SOURCE_DIR}/web/openscad-wasm-dist")

    # Required files from either the legacy 2022 build or a freshly compiled build.
    foreach(fname openscad.js openscad.wasm)
        if(NOT EXISTS "${OSCAD_WASM_DIST}/${fname}")
            message(FATAL_ERROR
                "Missing WASM artifact: ${OSCAD_WASM_DIST}/${fname}\n"
                "Build it first:  ./openscad-lib/build-wasm.sh"
            )
        endif()
        add_custom_command(TARGET parametrix POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${OSCAD_WASM_DIST}/${fname}"
                "${CMAKE_BINARY_DIR}/${fname}"
            COMMENT "Deploying OpenSCAD WASM: ${fname}"
        )
    endforeach()

    # openscad.wasm.js exists only in the legacy 2022 pre-built artifacts.
    # The from-source build (build-wasm.sh) produces a single ES6 module, so
    # this file will be absent after a fresh build.
    if(EXISTS "${OSCAD_WASM_DIST}/openscad.wasm.js")
        add_custom_command(TARGET parametrix POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${OSCAD_WASM_DIST}/openscad.wasm.js"
                "${CMAKE_BINARY_DIR}/openscad.wasm.js"
            COMMENT "Deploying OpenSCAD WASM (legacy): openscad.wasm.js"
        )
    endif()

else()
    find_package(SDL2 REQUIRED)
    find_package(OpenGL REQUIRED)
    target_link_libraries(parametrix PRIVATE OpenGL::GL ${CMAKE_DL_LIBS})
    if(TARGET SDL2::SDL2)
        target_link_libraries(parametrix PRIVATE SDL2::SDL2)
    else()
        target_include_directories(parametrix PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(parametrix PRIVATE ${SDL2_LIBRARIES})
    endif()

    # ---------------------------------------------------------------------------
    # OpenSCAD evaluation backend for desktop
    #
    # Option A (default): subprocess — system "openscad" binary, or a container.
    #   cmake -DUSE_OPENSCAD_LIB=OFF -DOPENSCAD_CMD="openscad" ..
    #
    # Option B: linked library — builds OpenSCAD from openscad-lib/upstream/openscad
    # with Manifold backend (no CGAL), no Qt.  Requires system packages:
    #   Fedora:  boost-devel eigen3-devel harfbuzz-devel fontconfig-devel glib2-devel
    #            double-conversion-devel libzip-devel freetype-devel libxml2-devel
    #            flex bison
    #   Ubuntu:  same with -dev suffix
    #   cmake -DUSE_OPENSCAD_LIB=ON ..
    # ---------------------------------------------------------------------------
    option(USE_OPENSCAD_LIB
        "Link OpenSCAD directly as a static library instead of shelling out"
        OFF)

    if(USE_OPENSCAD_LIB)
        message(STATUS "OpenSCAD backend: linked library (openscad-lib)")
        add_subdirectory(openscad-lib)
        target_link_libraries(parametrix PRIVATE openscad_api)
        target_compile_definitions(parametrix PRIVATE USE_OPENSCAD_LIB)
    else()
        # ---------------------------------------------------------------------------
        # OpenSCAD command for desktop evaluation
        # Default: system "openscad".  Override via -DOPENSCAD_CMD=... to use a
        # container, e.g.:
        #   -DOPENSCAD_CMD="podman run --rm -v /tmp:/tmp parametrix-openscad openscad"
        # ---------------------------------------------------------------------------
        set(OPENSCAD_CMD "openscad" CACHE STRING
            "Command used to invoke OpenSCAD on desktop (can be a container wrapper)")
        message(STATUS "OpenSCAD backend: subprocess (${OPENSCAD_CMD})")
        target_compile_definitions(parametrix PRIVATE OPENSCAD_CMD="${OPENSCAD_CMD}")
    endif()

    # GL_GLEXT_PROTOTYPES: declare GL3+ functions directly from Mesa's libGL.so
    target_compile_definitions(parametrix PRIVATE
        GL_GLEXT_PROTOTYPES
        MODELS_PATH="${CMAKE_SOURCE_DIR}/models/"
        I18N_PATH="${CMAKE_SOURCE_DIR}/i18n/"
        SOURCE_ROOT="${CMAKE_SOURCE_DIR}/"
    )
endif()

# ---------------------------------------------------------------------------
# Optional: generate models/example.stl at build time via OpenSCAD
# ---------------------------------------------------------------------------
find_program(OPENSCAD openscad)
if(OPENSCAD)
    add_custom_command(
        OUTPUT  ${CMAKE_SOURCE_DIR}/models/example.stl
        COMMAND ${OPENSCAD} --export-format binstl
                            -o ${CMAKE_SOURCE_DIR}/models/example.stl
                            ${CMAKE_SOURCE_DIR}/models/example.scad
        DEPENDS ${CMAKE_SOURCE_DIR}/models/example.scad
        COMMENT "Generating models/example.stl from OpenSCAD"
    )
    add_custom_target(gen_stl DEPENDS ${CMAKE_SOURCE_DIR}/models/example.stl)
    add_dependencies(parametrix gen_stl)
else()
    message(STATUS "OpenSCAD not found — skipping STL generation. "
                   "Install openscad and re-run cmake to enable.")
endif()
