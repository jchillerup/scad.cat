#pragma once
#include "viewer.h"
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// EvalStatus — state of the OpenSCAD evaluation pipeline
// ---------------------------------------------------------------------------

enum class EvalStatus { Idle, Pending, Ok, Error };

// ---------------------------------------------------------------------------
// WASM: async two-step API
// ---------------------------------------------------------------------------
#ifdef __EMSCRIPTEN__

// Start an async evaluation. Safe to call when no eval is running; a new
// OpenSCAD instance is created each time (avoids Emscripten post-exit crash).
void scad_eval_start(const char* source);

// Poll the in-flight evaluation. Call each frame while status == Pending.
// When Ok/Error is returned the result is consumed: on Ok, viewer and out_stl
// are populated; on Error, error_msg is set. Returns Idle if called after the
// result has already been consumed (shouldn't happen in normal use).
EvalStatus scad_eval_poll(MeshViewer& viewer,
                          std::vector<uint8_t>& out_stl,
                          std::string& error_msg);

// ---------------------------------------------------------------------------
// Desktop: synchronous API
// ---------------------------------------------------------------------------
#else

// Run openscad as a subprocess and block until it exits.
// On Ok, viewer and out_stl are populated; on Error, error_msg is set.
EvalStatus scad_eval_sync(const char* source,
                          MeshViewer& viewer,
                          std::vector<uint8_t>& out_stl,
                          std::string& error_msg);

#endif // __EMSCRIPTEN__
