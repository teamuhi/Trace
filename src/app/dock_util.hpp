#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <initializer_list>
#include <string>

namespace trace {

// Shared by the workspaces that have a central canvas window docked with its tab bar hidden.
//
// A panel dropped on the middle of such a node would become a tab behind the canvas and, with the tab bar hidden,
// vanish. Call this right before Begin() of the canvas window: its node then refuses drops on its middle (the
// center target is not offered), while the arrows to its sides, and the dockspace's outer edges, still work. The
// flag is set from the window every frame, so it needs no saved layout and holds for every mode.
inline void refuseDockingOverCanvas() {
    ImGuiWindowClass canvasClass;
    canvasClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoDockingOverMe;
    ImGui::SetNextWindowClass(&canvasClass);
}

// True when the saved layout (before the windows were first shown) docks the canvas window together with any of
// `panels` in one node: an imgui.ini written before the fix above, with a panel hidden behind the canvas. The
// workspace answers "stale" once and the default layout is built over it.
inline bool panelHiddenBehindCanvas(const std::string& canvas, std::initializer_list<std::string> panels) {
    const ImGuiWindowSettings* canvasSettings = ImGui::FindWindowSettingsByID(ImHashStr(canvas.c_str()));
    if (canvasSettings == nullptr || canvasSettings->DockId == 0)
        return false;
    for (const std::string& panel : panels) {
        const ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID(ImHashStr(panel.c_str()));
        if (settings != nullptr && settings->DockId == canvasSettings->DockId)
            return true;
    }
    return false;
}

} // namespace trace
