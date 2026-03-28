#include "generator_ui.h"
#include "imgui.h"
#include <cstdio>

// ---------------------------------------------------------------------------
// render_generator_ui
// ---------------------------------------------------------------------------

bool render_generator_ui(const GeneratorDesc& desc, ParamValues& vals)
{
    bool changed = false;

    for (const auto& section : desc.sections) {
        bool open = ImGui::CollapsingHeader(section.label.c_str(),
                                            ImGuiTreeNodeFlags_DefaultOpen);
        if (!open) continue;

        ImGui::Indent(8.0f);
        for (const auto& param_id : section.param_ids) {
            // Find param definition
            const ParamDef* pd = nullptr;
            for (const auto& p : desc.params)
                if (p.id == param_id) { pd = &p; break; }
            if (!pd) continue;

            nlohmann::json& val = vals[pd->id];
            ImGui::PushID(pd->id.c_str());

            switch (pd->type) {

                case ParamType::GridfinityDim: {
                    // Two side-by-side inputs: grid units and mm override
                    float units = (float)val[0].get<double>();
                    float mm    = (float)val[1].get<double>();

                    ImGui::Text("%-16s", pd->label.c_str());
                    ImGui::SameLine();

                    ImGui::SetNextItemWidth(50.0f);
                    if (ImGui::InputFloat("##u", &units, 0, 0, "%.0f")) {
                        float minv = pd->min_val.is_null() ? 1.0f
                                                           : (float)pd->min_val.get<double>();
                        if (units < minv) units = minv;
                        val = nlohmann::json::array({units, mm});
                        changed = true;
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("×");
                    ImGui::SameLine();

                    if (mm > 0.0f)
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.6f, 1.0f, 1.0f));
                    ImGui::SetNextItemWidth(60.0f);
                    if (ImGui::InputFloat("mm##m", &mm, 0, 0, "%.1f")) {
                        if (mm < 0.0f) mm = 0.0f;
                        val = nlohmann::json::array({units, mm});
                        changed = true;
                    }
                    if (mm > 0.0f)
                        ImGui::PopStyleColor();
                    break;
                }

                case ParamType::Enum: {
                    std::string cur = val.get<std::string>();
                    int idx = 0;
                    for (int i = 0; i < (int)pd->options.size(); i++)
                        if (pd->options[i] == cur) { idx = i; break; }

                    ImGui::SetNextItemWidth(160.0f);
                    if (ImGui::BeginCombo(pd->label.c_str(), cur.c_str())) {
                        for (int i = 0; i < (int)pd->options.size(); i++) {
                            bool sel = (i == idx);
                            if (ImGui::Selectable(pd->options[i].c_str(), sel)) {
                                val     = pd->options[i];
                                changed = true;
                            }
                            if (sel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    break;
                }

                case ParamType::Bool: {
                    bool b = val.get<bool>();
                    if (ImGui::Checkbox(pd->label.c_str(), &b)) {
                        val     = b;
                        changed = true;
                    }
                    break;
                }

                case ParamType::Float: {
                    float v    = (float)val.get<double>();
                    float step = pd->step.is_null() ? 0.1f
                                                    : (float)pd->step.get<double>();
                    ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::InputFloat(pd->label.c_str(), &v, step, step * 10.0f,
                                          "%.2f")) {
                        if (!pd->min_val.is_null()) {
                            float lo = (float)pd->min_val.get<double>();
                            if (v < lo) v = lo;
                        }
                        if (!pd->max_val.is_null()) {
                            float hi = (float)pd->max_val.get<double>();
                            if (v > hi) v = hi;
                        }
                        val     = (double)v;
                        changed = true;
                    }
                    break;
                }

                case ParamType::Int: {
                    int v = val.get<int>();
                    ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::InputInt(pd->label.c_str(), &v)) {
                        if (!pd->min_val.is_null()) {
                            int lo = pd->min_val.get<int>();
                            if (v < lo) v = lo;
                        }
                        if (!pd->max_val.is_null()) {
                            int hi = pd->max_val.get<int>();
                            if (v > hi) v = hi;
                        }
                        val     = v;
                        changed = true;
                    }
                    break;
                }
            }

            if (!pd->tooltip.empty())
                ImGui::SetItemTooltip("%s", pd->tooltip.c_str());

            ImGui::PopID();
        }
        ImGui::Unindent(8.0f);
    }

    return changed;
}
