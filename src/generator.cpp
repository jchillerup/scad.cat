#include "generator.h"
#include <fstream>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static ParamType parse_type(const std::string& s)
{
    if (s == "int")            return ParamType::Int;
    if (s == "float")          return ParamType::Float;
    if (s == "bool")           return ParamType::Bool;
    if (s == "enum")           return ParamType::Enum;
    if (s == "gridfinity_dim") return ParamType::GridfinityDim;
    return ParamType::Float;
}

// ---------------------------------------------------------------------------
// load_generator
// ---------------------------------------------------------------------------

GeneratorDesc load_generator(const std::string& json_path)
{
    std::ifstream f(json_path);
    if (!f)
        throw std::runtime_error("load_generator: cannot open " + json_path);

    nlohmann::json j;
    f >> j;

    GeneratorDesc desc;
    desc.id         = j.value("id",         "");
    desc.title      = j.value("title",      "");
    desc.author     = j.value("author",     "");
    desc.author_url = j.value("author_url", "");
    desc.license    = j.value("license",    "");
    desc.scad_file  = j.value("scad_file",  "");

    // sections
    for (const auto& sec : j.value("sections", nlohmann::json::array())) {
        Section s;
        s.label = sec.value("label", "");
        for (const auto& pid : sec.value("params", nlohmann::json::array()))
            s.param_ids.push_back(pid.get<std::string>());
        desc.sections.push_back(std::move(s));
    }

    // params
    for (const auto& p : j.value("params", nlohmann::json::array())) {
        ParamDef pd;
        pd.id       = p.value("id",      "");
        pd.label    = p.value("label",   "");
        pd.scad_var = p.value("scad_var","");
        pd.tooltip  = p.value("tooltip", "");
        pd.type     = parse_type(p.value("type", "float"));
        pd.default_val = p.value("default", nlohmann::json{});
        if (p.contains("min"))  pd.min_val = p["min"];
        if (p.contains("max"))  pd.max_val = p["max"];
        if (p.contains("step")) pd.step    = p["step"];
        if (p.contains("options"))
            for (const auto& opt : p["options"])
                pd.options.push_back(opt.get<std::string>());
        desc.params.push_back(std::move(pd));
    }

    return desc;
}

// ---------------------------------------------------------------------------
// default_values
// ---------------------------------------------------------------------------

ParamValues default_values(const GeneratorDesc& desc)
{
    ParamValues vals;
    for (const auto& pd : desc.params)
        vals[pd.id] = pd.default_val;
    return vals;
}
