#pragma once
#include "generator.h"
#include <string>

// Read the generator's SCAD file and append variable overrides so that the
// caller's parameter values take effect via OpenSCAD's last-assignment-wins
// semantics.  Returns a self-contained SCAD source string (no include needed).
//
// scad_full_path – absolute (or VFS-rooted) path to the .scad file.
std::string build_scad(const GeneratorDesc& desc,
                       const ParamValues&   vals,
                       const std::string&   scad_full_path);
