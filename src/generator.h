#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Parameter types
// ---------------------------------------------------------------------------

enum class ParamType { Int, Float, Bool, Enum, GridfinityDim };

struct ParamDef {
    std::string id, label, scad_var, tooltip;
    ParamType   type      = ParamType::Float;
    nlohmann::json default_val;
    nlohmann::json min_val, max_val, step;
    std::vector<std::string> options;   // enum only
};

struct Section {
    std::string              label;
    std::vector<std::string> param_ids;
};

struct GeneratorDesc {
    std::string id, title, author, author_url, license;
    std::string scad_file;              // path relative to SOURCE_ROOT
    std::vector<Section>  sections;
    std::vector<ParamDef> params;
};

// Runtime values: param id → current value (same JSON type as default_val)
using ParamValues = std::unordered_map<std::string, nlohmann::json>;

// ---------------------------------------------------------------------------
// Load a generator descriptor from a JSON file.
// ---------------------------------------------------------------------------
GeneratorDesc load_generator(const std::string& json_path);

// Build a ParamValues map seeded with each param's default value.
ParamValues   default_values(const GeneratorDesc& desc);
