#pragma once

#include <imgui.h>

namespace trace {

// Draws a texture with nearest-neighbour or linear sampling. ImGui's SDL renderer backend sets the sampling for
// every draw command itself (linear unless asked otherwise) and ignores the texture's own scale mode, so the
// request goes through ImGui's standard draw callbacks, bracketing just this one image. A backend that does not
// offer the callbacks draws with its default.
inline void addImageSampled(ImDrawList* dl, ImTextureRef texture, ImVec2 a, ImVec2 b, ImVec2 uv0 = ImVec2(0.0f, 0.0f),
                            ImVec2 uv1 = ImVec2(1.0f, 1.0f), bool nearest = true) {
    const ImGuiPlatformIO& io = ImGui::GetPlatformIO();
    const bool switched = nearest && io.DrawCallback_SetSamplerNearest && io.DrawCallback_SetSamplerLinear;
    if (switched)
        dl->AddCallback(io.DrawCallback_SetSamplerNearest, nullptr);
    dl->AddImage(texture, a, b, uv0, uv1);
    if (switched)
        dl->AddCallback(io.DrawCallback_SetSamplerLinear, nullptr);
}

} // namespace trace
