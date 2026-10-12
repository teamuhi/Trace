#include "app/placeholder_workspace.hpp"

#include <SDL3/SDL.h>
#include <imgui_internal.h>

#include <algorithm>
#include <utility>

#include "app/paint_file_adapter.hpp"
#include "app/settings.hpp"
#include "app/theme.hpp"
#include "core/i18n.hpp"

namespace trace {

FileAdapter::LoadOutcome PlaceholderFileAdapter::load(const std::filesystem::path&) {
    LoadOutcome outcome;
    outcome.error = trf(Str::ErrModeNotReady, {tr(modeString(mode_))});
    return outcome;
}

std::span<const FileFilter> PlaceholderFileAdapter::openFilters() const {
    return imageOpenFilters();
}

PlaceholderWorkspace::PlaceholderWorkspace(WorkspaceContext& context, Mode mode)
    : Workspace(context), mode_(mode), adapter_(mode), files_(adapter_, context.settings, {}) {}

void PlaceholderWorkspace::init(SDL_Window* window, SDL_Renderer*) {
    FileSession::Hooks hooks;
    hooks.routeOpen = ctx_.routeOpen;
    files_.setHooks(std::move(hooks));
    if (!files_.init(window))
        SDL_Log("Native file dialogs are unavailable: no SDL event type could be registered");
}

ImGuiID PlaceholderWorkspace::dockSpaceId() const {
    return ImHashStr(mode_ == Mode::Flowchart ? "TraceDockSpace.Flow" : "TraceDockSpace.Pixel");
}

std::string PlaceholderWorkspace::windowName() const {
    return std::string(tr(modeString(mode_))) + (mode_ == Mode::Flowchart ? "###FlowPlaceholder" : "###PixelPlaceholder");
}

void PlaceholderWorkspace::buildDefaultLayout(ImGuiID dockId, ImVec2 size) {
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, size);
    ImGui::DockBuilderDockWindow(windowName().c_str(), dockId);
    if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockId))
        node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
    ImGui::DockBuilderFinish(dockId);
}

void PlaceholderWorkspace::drawWindows() {
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin(windowName().c_str(), nullptr, flags)) {
        const std::string body = trf(Str::ModeSoonBody, {tr(modeString(mode_))});
        const char* hint = tr(Str::ModeSoonHint);

        // A centered block of wrapped text, as wide as is comfortable to read.
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float wrap = std::max(std::min(avail.x - ImGui::GetFontSize() * 2.0f, ImGui::GetFontSize() * 30.0f), 1.0f);
        const ImVec2 bodySize = ImGui::CalcTextSize(body.c_str(), nullptr, false, wrap);
        const ImVec2 hintSize = ImGui::CalcTextSize(hint, nullptr, false, wrap);
        const float gap = ImGui::GetFontSize() * 0.75f;
        const float height = bodySize.y + gap + hintSize.y;
        const ImVec2 origin = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(origin.x + std::max((avail.x - wrap) * 0.5f, 0.0f),
                                   origin.y + std::max((avail.y - height) * 0.5f, 0.0f)));

        const float startX = ImGui::GetCursorPosX();
        ImGui::PushTextWrapPos(startX + wrap);
        ImGui::PushStyleColor(ImGuiCol_Text, tokens().text);
        ImGui::TextUnformatted(body.c_str());
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, gap));
        ImGui::SetCursorPosX(startX); // Dummy() starts a new line at the left edge
        ImGui::TextDisabled("%s", hint);
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}

std::string PlaceholderWorkspace::statusLeft() const {
    return trf(Str::StatusModeSoon, {tr(modeString(mode_))});
}

} // namespace trace
