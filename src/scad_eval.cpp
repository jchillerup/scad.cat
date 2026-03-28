#include "scad_eval.h"
#include <cstdlib>
#include <fstream>

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <GLES3/gl3.h>

// ---------------------------------------------------------------------------
// JS glue: async OpenSCAD evaluation
// ---------------------------------------------------------------------------

// Kick off an async evaluation. A fresh OpenSCAD instance is created each
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

// Poll: 0 = still running, 1 = done OK, -1 = error
EM_JS(int, js_scad_eval_status, (), {
    if (globalThis._scadEvalStatus === 'running') return 0;
    if (globalThis._scadEvalStatus === 'done')    return 1;
    return -1;
});

// Retrieve result (only call when status == 1).
// Allocates buffer in the Wasm heap; caller must free(). Returns byte count.
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

// ---------------------------------------------------------------------------
// C++ wrappers
// ---------------------------------------------------------------------------

void scad_eval_start(const char* source)
{
    js_start_scad_eval(source);
}

EvalStatus scad_eval_poll(MeshViewer& viewer,
                          std::vector<uint8_t>& out_stl,
                          std::string& error_msg)
{
    int st = js_scad_eval_status();
    if (st == 0) return EvalStatus::Pending;

    if (st == 1) {
        uint8_t* ptr = nullptr;
        int len = js_scad_get_result(&ptr);
        if (len > 0 && ptr) {
            out_stl.assign(ptr, ptr + len);
            viewer.LoadSTLFromMemory(ptr, (size_t)len);
            free(ptr);
            return EvalStatus::Ok;
        }
        error_msg = "Compilation failed";
        return EvalStatus::Error;
    }

    // st == -1
    error_msg = "Compilation failed";
    return EvalStatus::Error;
}

#else // desktop

// ---------------------------------------------------------------------------
// Desktop: synchronous subprocess evaluation
// ---------------------------------------------------------------------------

EvalStatus scad_eval_sync(const char* source,
                          MeshViewer& viewer,
                          std::vector<uint8_t>& out_stl,
                          std::string& error_msg)
{
    const std::string tmp_scad = "/tmp/parametrix_eval.scad";
    const std::string tmp_stl  = "/tmp/parametrix_eval.stl";

    { std::ofstream f(tmp_scad);
      if (!f) { error_msg = "Error: cannot write " + tmp_scad; return EvalStatus::Error; }
      f << source; }

    std::string cmd = "openscad --export-format binstl -o " + tmp_stl + " " + tmp_scad + " 2>&1";
    if (std::system(cmd.c_str()) != 0) {
        error_msg = "Compilation failed (see terminal)";
        return EvalStatus::Error;
    }

    if (!viewer.LoadSTL(tmp_stl.c_str())) {
        error_msg = "Error: could not load output STL";
        return EvalStatus::Error;
    }

    { std::ifstream f(tmp_stl, std::ios::binary);
      out_stl.assign(std::istreambuf_iterator<char>(f), {}); }

    return EvalStatus::Ok;
}

#endif // __EMSCRIPTEN__
