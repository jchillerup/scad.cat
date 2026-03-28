#include "i18n.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "ImGuizmo.h"
#include "viewer.h"
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif

#ifndef __EMSCRIPTEN__
#  include <SDL_opengl.h>
#else
#  include <GLES3/gl3.h>
#endif

// ---------------------------------------------------------------------------
// OpenSCAD integration
// ---------------------------------------------------------------------------

#ifdef __EMSCRIPTEN__

// Kick off an async evaluation. A *fresh* OpenSCAD instance is created each
// time so the Emscripten runtime never hits the post-exit state on re-use.
// State is written to globalThis._scadEvalStatus / _scadStlBytes.
EM_JS(void, js_start_scad_eval, (const char* scad), {
    if (globalThis._scadEvalStatus === 'running') return;
    globalThis._scadEvalStatus = 'running';
    globalThis._scadStlBytes   = null;
    const text = UTF8ToString(scad);
    (async () => {
        try {
            const os   = await globalThis._openscadFactory({ noInitialRun: true });
            os.FS.writeFile('/model.scad', text);
            const code = os.callMain([
                '/model.scad',
                '--enable=manifold',
                '--export-format', 'binstl',
                '-o', '/out.stl'
            ]);
            if (code === 0) {
                globalThis._scadStlBytes   = os.FS.readFile('/out.stl', { encoding: 'binary' });
                globalThis._scadEvalStatus = 'done';
            } else {
                globalThis._scadEvalStatus = 'error';
            }
        } catch(e) {
            console.error('[openscad]', e);
            globalThis._scadEvalStatus = 'error';
        }
    })();
});

// Clipboard: take any pending paste text (returns malloc'd C string or null).
// Caller must free(). Clears _pendingPaste so each chunk is delivered once.
EM_JS(char*, js_take_pending_paste, (), {
    const text = globalThis._pendingPaste;
    if (!text || !text.length) return 0;
    globalThis._pendingPaste = null;
    const len  = lengthBytesUTF8(text) + 1;
    const ptr  = _malloc(len);
    stringToUTF8(text, ptr, len);
    return ptr;
});

// Clipboard: write text to the browser clipboard (best-effort async).
EM_JS(void, js_set_clipboard, (const char* str), {
    const text = UTF8ToString(str);
    if (navigator.clipboard && navigator.clipboard.writeText)
        navigator.clipboard.writeText(text).catch(function(){});
});

// Trigger a browser file download of raw bytes.
EM_JS(void, js_download_bytes, (const uint8_t* data, int len, const char* filename), {
    const bytes = HEAPU8.slice(data, data + len);
    const blob  = new Blob([bytes], { type: 'application/octet-stream' });
    const url   = URL.createObjectURL(blob);
    const a     = document.createElement('a');
    a.href     = url;
    a.download = UTF8ToString(filename);
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
});

// Poll: 0 = still running, 1 = done OK, -1 = error
EM_JS(int, js_scad_eval_status, (), {
    if (globalThis._scadEvalStatus === 'running') return 0;
    if (globalThis._scadEvalStatus === 'done')    return 1;
    return -1;
});

// Retrieve result (only call when status == 1).
// Allocates buffer in our heap; caller must free(). Returns byte count.
EM_JS(int, js_scad_get_result, (uint8_t** out_ptr), {
    const bytes = globalThis._scadStlBytes;
    if (!bytes || !bytes.length) return 0;
    const ptr = _malloc(bytes.length);
    HEAPU8.set(bytes, ptr);
    setValue(out_ptr, ptr, 'i32');
    globalThis._scadEvalStatus = null;
    globalThis._scadStlBytes   = null;
    return bytes.length;
});

#else // desktop

// Run openscad as a subprocess, write result into viewer.
// Returns true on success.
static bool desktop_evaluate_scad(const char* scad_source,
                                   MeshViewer& viewer,
                                   std::string& status,
                                   std::vector<uint8_t>& out_stl)
{
    const std::string tmp_scad = "/tmp/parametrix_eval.scad";
    const std::string tmp_stl  = "/tmp/parametrix_eval.stl";

    { std::ofstream f(tmp_scad);
      if (!f) { status = "Error: cannot write " + tmp_scad; return false; }
      f << scad_source; }

    std::string cmd = "openscad --export-format binstl -o " + tmp_stl + " " + tmp_scad + " 2>&1";
    int ret = std::system(cmd.c_str());
    if (ret != 0) { status = "Compilation failed (see terminal)"; return false; }

    if (!viewer.LoadSTL(tmp_stl.c_str())) {
        status = "Error: could not load output STL";
        return false;
    }

    { std::ifstream f(tmp_stl, std::ios::binary);
      out_stl.assign(std::istreambuf_iterator<char>(f), {}); }

    status = "OK";
    return true;
}

#endif // __EMSCRIPTEN__

// ---------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------

static constexpr int SCAD_BUF_SIZE = 32 * 1024;

struct AppState {
    SDL_Window*   window     = nullptr;
    SDL_GLContext gl_context = nullptr;
    bool          done       = false;
    ImVec4        clear_color{0.18f, 0.18f, 0.20f, 1.00f};
    MeshViewer    viewer;

    char        scad_buf[SCAD_BUF_SIZE] = {};
    std::string scad_status;
    double      scad_last_edit    = -1.0;  // ImGui time of last keystroke, -1 = idle
    bool        scad_eval_pending = false; // WASM: async eval in flight
    bool        show_catalonia    = false;
    Language    language          = Language::English;
    std::vector<uint8_t> current_stl;
};

static constexpr double DEBOUNCE_SECS = 0.1;

static AppState g_app;

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

static void MainLoopStep()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT)
            g_app.done = true;
        if (event.type == SDL_WINDOWEVENT &&
            event.window.event == SDL_WINDOWEVENT_CLOSE &&
            event.window.windowID == SDL_GetWindowID(g_app.window))
            g_app.done = true;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

#ifdef __EMSCRIPTEN__
    // Inject any clipboard paste text that arrived via the JS paste event.
    {
        char* paste_text = js_take_pending_paste();
        if (paste_text) {
            ImGui::GetIO().AddInputCharactersUTF8(paste_text);
            free(paste_text);
        }
    }
#endif

    // ---- Menu bar ----------------------------------------------------------
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu(_("Preferences"))) {
            if (ImGui::BeginMenu(_("Language"))) {
                if (ImGui::MenuItem(_("English"), nullptr, g_app.language == Language::English)) {
                    g_app.language = Language::English;
                    i18n::set_language(Language::English, I18N_PATH);
                }
                if (ImGui::MenuItem(_("Catalan"), nullptr, g_app.language == Language::Catalan)) {
                    g_app.language = Language::Catalan;
                    i18n::set_language(Language::Catalan, I18N_PATH);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(_("About"))) {
            if (ImGui::MenuItem(_("Information about Catalonia")))
                g_app.show_catalonia = true;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // ---- Catalonia info window ---------------------------------------------
    if (g_app.show_catalonia) {
        ImGui::SetNextWindowSize({520, 400}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(_("Information about Catalonia"), &g_app.show_catalonia)) {
            ImGui::TextWrapped("%s", _("Catalonia (Catalunya) is an autonomous community in northeastern Spain, "
                "bordered by France and Andorra to the north, the Mediterranean Sea to the east, "
                "and the regions of Aragon and Valencia to the west and south."));
            ImGui::Spacing();
            ImGui::Text("%s", _("BARCELONA"));
            ImGui::TextWrapped("%s", _("The capital is a world-class city famed for Antoni Gaudi's extraordinary "
                "architecture: the still-unfinished Sagrada Familia basilica, the undulating "
                "Casa Batllo and Casa Mila (La Pedrera), and the fairy-tale Park Guell. The Gothic "
                "Quarter, Barceloneta beach, and the boulevard of La Rambla draw millions of visitors "
                "every year."));
            ImGui::Spacing();
            ImGui::Text("%s", _("NATURE"));
            ImGui::TextWrapped("%s", _("Beyond the city you will find the volcanic landscape of La Garrotxa, "
                "the jagged peaks of Montserrat with its revered Black Madonna, the wild Costa Brava "
                "coves, the Pyrenean valleys of the Vall d'Aran, and the Ebro Delta wetlands "
                "— a paradise for birdwatchers."));
            ImGui::Spacing();
            ImGui::Text("%s", _("CULTURE & FOOD"));
            ImGui::TextWrapped("%s", _("Catalan is a co-official language with Spanish. The region has its own "
                "distinct culture, traditions (castellers, sardana dancing, human towers), and one of "
                "the great cuisines of Europe — from pa amb tomaquet (bread rubbed with tomato) to "
                "world-renowned restaurants like El Celler de Can Roca in Girona."));
            ImGui::Spacing();
            ImGui::Text("%s", _("HISTORY"));
            ImGui::TextWrapped("%s", _("Catalonia has a rich history stretching back to Roman Tarraco (modern "
                "Tarragona), through the medieval Crown of Aragon, to the modern era. The region has "
                "a strong sense of its own national identity and a lively contemporary cultural scene."));
        }
        ImGui::End();
    }

    // ---- Info window -------------------------------------------------------
    ImGui::Begin(_("Info"));
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("Viewport: %.0f x %.0f", io.DisplaySize.x, io.DisplaySize.y);
        ImGui::Text("%.1f FPS", io.Framerate);
    }
    ImGui::End();

    // ---- WASM: poll for completed async evaluation -------------------------
#ifdef __EMSCRIPTEN__
    if (g_app.scad_eval_pending) {
        int st = js_scad_eval_status();
        if (st == 1) {
            uint8_t* ptr = nullptr;
            int      len = js_scad_get_result(&ptr);
            if (len > 0 && ptr) {
                g_app.current_stl.assign(ptr, ptr + len);
                g_app.viewer.LoadSTLFromMemory(ptr, (size_t)len);
                free(ptr);
                g_app.scad_status = "OK";
            } else {
                g_app.scad_status = "Compilation failed";
            }
            g_app.scad_eval_pending = false;
        } else if (st == -1) {
            g_app.scad_status     = "Compilation failed";
            g_app.scad_eval_pending = false;
        }
        // st == 0: still running — keep polling
    }
#endif

    // ---- Evaluate helper (button + debounce) --------------------------------
    auto do_evaluate = [&]() {
#ifdef __EMSCRIPTEN__
        js_start_scad_eval(g_app.scad_buf);
        g_app.scad_status       = "Compiling...";
        g_app.scad_eval_pending = true;
#else
        desktop_evaluate_scad(g_app.scad_buf, g_app.viewer, g_app.scad_status, g_app.current_stl);
#endif
    };

    // ---- Debounce: trigger evaluate 1 s after the last keystroke -----------
    bool can_evaluate = !g_app.scad_eval_pending;
    if (can_evaluate && g_app.scad_last_edit >= 0.0 &&
        (ImGui::GetTime() - g_app.scad_last_edit) >= DEBOUNCE_SECS)
    {
        g_app.scad_last_edit = -1.0;
        do_evaluate();
    }

    // ---- SCAD source editor ------------------------------------------------
    ImGui::SetNextWindowSize({480, 400}, ImGuiCond_FirstUseEver);
    ImGui::Begin(_("SCAD Source"));
    {
        ImGui::InputTextMultiline(
            "##scad", g_app.scad_buf, SCAD_BUF_SIZE,
            ImVec2(-1.0f, -ImGui::GetFrameHeightWithSpacing() - 4));

        if (ImGui::IsItemEdited())
            g_app.scad_last_edit = ImGui::GetTime();

        if (!can_evaluate) ImGui::BeginDisabled();
        if (ImGui::Button(_("Compile"))) {
            g_app.scad_last_edit = -1.0; // cancel pending debounce
            do_evaluate();
        }
        if (!can_evaluate) ImGui::EndDisabled();

        ImGui::SameLine();
        if (g_app.scad_last_edit >= 0.0)
            ImGui::TextDisabled("...");
        else if (g_app.scad_status.empty())
            ImGui::TextDisabled("—");
        else if (g_app.scad_status == "OK")
            ImGui::TextColored({0.4f, 1.0f, 0.4f, 1.0f}, "%s", _("OK"));
        else
            ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "%s", _(g_app.scad_status.c_str()));
    }
    ImGui::End();

    // ---- 3D viewport: fills the entire OS window --------------------------
    {
        ImGuiIO& io2 = ImGui::GetIO();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io2.DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::Begin(_("3D Viewport"), nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove       |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNav);
        ImGui::PopStyleVar();
        g_app.viewer.Draw();
        ImGui::End();
    }

    // ---- Download STL button (floating, lower-right) -----------------------
    if (!g_app.current_stl.empty()) {
        const float pad = 16.0f;
        ImGuiIO& fio = ImGui::GetIO();
        ImGui::SetNextWindowPos(
            {fio.DisplaySize.x - pad, fio.DisplaySize.y - pad},
            ImGuiCond_Always, {1.0f, 1.0f});
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin("##dl_btn", nullptr,
            ImGuiWindowFlags_NoDecoration      |
            ImGuiWindowFlags_NoSavedSettings   |
            ImGuiWindowFlags_NoFocusOnAppearing|
            ImGuiWindowFlags_NoNav             |
            ImGuiWindowFlags_NoMove            |
            ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.50f, 0.90f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.60f, 1.00f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.40f, 0.75f, 1.00f));

        if (ImGui::Button(_("Download STL"))) {
#ifdef __EMSCRIPTEN__
            js_download_bytes(g_app.current_stl.data(),
                              (int)g_app.current_stl.size(),
                              "model.stl");
#else
            const std::string out = "model.stl";
            std::ofstream f(out, std::ios::binary);
            f.write(reinterpret_cast<const char*>(g_app.current_stl.data()),
                    (std::streamsize)g_app.current_stl.size());
            fprintf(stdout, "STL saved to %s\n", out.c_str());
#endif
        }

        ImGui::PopStyleColor(3);
        ImGui::End();
    }

    // ---- Render ------------------------------------------------------------
    ImGui::Render();
    ImGuiIO& io = ImGui::GetIO();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(g_app.clear_color.x, g_app.clear_color.y,
                 g_app.clear_color.z, g_app.clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(g_app.window);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int, char**)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        printf("SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

#ifdef __EMSCRIPTEN__
    const char* glsl_version = "#version 300 es";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_WindowFlags window_flags =
        (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    g_app.window = SDL_CreateWindow(
        "parametrix", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720, window_flags);
    if (!g_app.window) {
        printf("SDL_CreateWindow error: %s\n", SDL_GetError());
        return 1;
    }

    g_app.gl_context = SDL_GL_CreateContext(g_app.window);
    SDL_GL_MakeCurrent(g_app.window, g_app.gl_context);
#ifndef __EMSCRIPTEN__
    SDL_GL_SetSwapInterval(1); // vsync; on WASM rAF handles this
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

#ifdef __EMSCRIPTEN__
    // Wire up clipboard functions for WASM (SDL2 clipboard API doesn't work in browsers).
    io.GetClipboardTextFn = [](void*) -> const char* { return ""; };
    io.SetClipboardTextFn = [](void*, const char* text) { js_set_clipboard(text); };
#endif

    ImGui_ImplSDL2_InitForOpenGL(g_app.window, g_app.gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    g_app.viewer.Init(glsl_version);

    // Load pre-generated example STL
    const char* stl_path = MODELS_PATH "example.stl";
    if (!g_app.viewer.LoadSTL(stl_path))
        fprintf(stderr, "Could not load %s\n", stl_path);

    // Populate the SCAD editor with the example source
    const char* scad_path = MODELS_PATH "example.scad";
    { std::ifstream f(scad_path);
      if (f) {
          std::string src((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
          strncpy(g_app.scad_buf, src.c_str(), SCAD_BUF_SIZE - 1);
      } }

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(MainLoopStep, 0, true);
#else
    while (!g_app.done)
        MainLoopStep();
#endif

    g_app.viewer.Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(g_app.gl_context);
    SDL_DestroyWindow(g_app.window);
    SDL_Quit();
    return 0;
}
