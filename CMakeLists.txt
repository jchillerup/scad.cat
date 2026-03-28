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
    # openscad-wasm pre-built assets
    # Downloaded once to web/openscad-wasm-dist/, then copied to the build dir.
    # ---------------------------------------------------------------------------
    set(OSCAD_WASM_TAG "2022.03.20")
    set(OSCAD_WASM_BASE "https://github.com/openscad/openscad-wasm/releases/download/${OSCAD_WASM_TAG}")
    set(OSCAD_WASM_CACHE "${CMAKE_SOURCE_DIR}/web/openscad-wasm-dist")
    file(MAKE_DIRECTORY "${OSCAD_WASM_CACHE}")

    foreach(fname openscad.js openscad.wasm.js openscad.wasm)
        if(NOT EXISTS "${OSCAD_WASM_CACHE}/${fname}")
            message(STATUS "Downloading openscad-wasm: ${fname} ...")
            file(DOWNLOAD
                "${OSCAD_WASM_BASE}/${fname}"
                "${OSCAD_WASM_CACHE}/${fname}"
                SHOW_PROGRESS
                STATUS dl_status
            )
            list(GET dl_status 0 dl_ok)
            if(NOT dl_ok EQUAL 0)
                list(GET dl_status 1 dl_err)
                message(FATAL_ERROR "Failed to download ${fname}: ${dl_err}")
            endif()
        endif()
        # Copy to build output so files are served alongside parametrix.html
        add_custom_command(TARGET parametrix POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${OSCAD_WASM_CACHE}/${fname}"
                "${CMAKE_BINARY_DIR}/${fname}"
            COMMENT "Deploying openscad-wasm: ${fname}"
        )
    endforeach()

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
    # OpenSCAD command for desktop evaluation
    # Default: system "openscad".  Override via -DOPENSCAD_CMD=... to use a
    # container, e.g.:
    #   -DOPENSCAD_CMD="podman run --rm -v /tmp:/tmp parametrix-openscad openscad"
    # ---------------------------------------------------------------------------
    set(OPENSCAD_CMD "openscad" CACHE STRING
        "Command used to invoke OpenSCAD on desktop (can be a container wrapper)")
    message(STATUS "OpenSCAD command: ${OPENSCAD_CMD}")

    # GL_GLEXT_PROTOTYPES: declare GL3+ functions directly from Mesa's libGL.so
    target_compile_definitions(parametrix PRIVATE
        GL_GLEXT_PROTOTYPES
        MODELS_PATH="${CMAKE_SOURCE_DIR}/models/"
        I18N_PATH="${CMAKE_SOURCE_DIR}/i18n/"
        SOURCE_ROOT="${CMAKE_SOURCE_DIR}/"
        OPENSCAD_CMD="${OPENSCAD_CMD}"
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
