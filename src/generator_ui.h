#pragma once
#include "generator.h"

// Render an ImGui parameter panel for the given generator.
// Returns true if any value changed this frame.
bool render_generator_ui(const GeneratorDesc& desc, ParamValues& vals);
