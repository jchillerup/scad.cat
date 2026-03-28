#include "scad_builder.h"
#include <fstream>
#include <sstream>
#include <cstdio>

// ---------------------------------------------------------------------------
// Format a single parameter value as an OpenSCAD literal
// ---------------------------------------------------------------------------

static std::string format_value(const ParamDef& pd, const nlohmann::json& val)
{
    switch (pd.type) {
        case ParamType::Int:
            return std::to_string(val.get<int>());

        case ParamType::Float: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%g", val.get<double>());
            return buf;
        }

        case ParamType::Bool:
            return val.get<bool>() ? "true" : "false";

        case ParamType::Enum:
            return "\"" + val.get<std::string>() + "\"";

        case ParamType::GridfinityDim: {
            // val is a two-element array [grid_units, mm_override]
            char buf[64];
            std::snprintf(buf, sizeof(buf), "[%g, %g]",
                          val[0].get<double>(), val[1].get<double>());
            return buf;
        }
    }
    return "undef";
}

// ---------------------------------------------------------------------------
// build_scad
// ---------------------------------------------------------------------------

std::string build_scad(const GeneratorDesc& desc,
                       const ParamValues&   vals,
                       const std::string&   scad_full_path)
{
    // Read the base SCAD file
    std::ifstream f(scad_full_path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    // Append override variables — OpenSCAD's last-assignment-wins rule means
    // these will shadow the defaults declared earlier in the file.
    content += "\n// ---- parametrix overrides ----\n";
    for (const auto& pd : desc.params) {
        auto it = vals.find(pd.id);
        const nlohmann::json& v = (it != vals.end()) ? it->second : pd.default_val;
        content += pd.scad_var + " = " + format_value(pd, v) + ";\n";
    }

    return content;
}
