// Dear ImGui view: widgets only. Every OS touchpoint (clipboard,
// browser, dialogs, processes) goes through platform.hpp / state.hpp;
// this TU never includes <cstdio>, never spawns, never touches env.
// Same layout and strings as before: path+Browse, Patch/Restore/
// Download row, status, Settings, console, Copy/Clear/Report, footer.
//
// NOTE: imgui's default font covers ASCII only - keep every literal in
// this file plain ASCII.
#pragma once

#include "state.hpp"

namespace wemod::gui
{

// Draw the whole window. One frame = one call.
void draw_ui(app_state& state);

} // namespace wemod::gui
