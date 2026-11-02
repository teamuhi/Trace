#include "app/raster_common.hpp"

#include <imgui.h>

#include <algorithm>

#include "app/clipboard.hpp"
#include "app/icon_widgets.hpp"
#include "app/keybinds.hpp"
#include "app/settings.hpp"
#include "app/toasts.hpp"
#include "core/document.hpp"
#include "core/i18n.hpp"
#include "tools/toolbox.hpp"

namespace trace {

void handleRasterShortcuts(const Keymap& keys, ModeContext mode, Settings& settings, bool& settingsDirty, Toolbox& toolbox,
                           Document& doc, bool idle, std::span<const ToolId> toolset, int maxBrush, int maxStroke) {
    const auto fire = [&](Action action, bool repeat = false) { return triggeredIn(keys, action, mode, repeat); };

    // Ctrl+D belongs to a focused text field (a hex color, a number) when there is one.
    if (idle && !ImGui::GetIO().WantTextInput && fire(Action::Deselect))
        doc.clearSelection();
    // Escape also deselects, but only when nothing else wants it: an open text box, a popup or a dialog (they close
    // on Escape), a key being rebound, or a gesture (which Escape cancels instead of dropping the selection).
    if (idle && doc.selection().active() && !toolbox.textEditing() && !isCapturing() && !ImGui::GetIO().WantTextInput &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        doc.clearSelection();

    if (fire(Action::TogglePixelGrid)) {
        settings.pixelGrid = !settings.pixelGrid;
        settingsDirty = true;
    }
    if (fire(Action::ToggleRulers)) {
        settings.showRulers = !settings.showRulers;
        settingsDirty = true;
    }
    if (fire(Action::ToggleGuides)) {
        settings.showGuides = !settings.showGuides;
        settingsDirty = true;
    }
    if (fire(Action::ToggleSnapToGuides)) {
        settings.snapToGuides = !settings.snapToGuides;
        settingsDirty = true;
    }

    for (const ToolId id : toolset) {
        if (fire(toolAction(id)))
            toolbox.select(id);
    }
    if (fire(Action::SwapColors))
        toolbox.swapColors();

    const int step = (fire(Action::IncreaseSize, true) ? 1 : 0) - (fire(Action::DecreaseSize, true) ? 1 : 0);
    if (step != 0) {
        ToolSettings& s = toolbox.settings();
        const ToolId tool = toolbox.current();
        if (tool == ToolId::Text) {
            s.textSize = std::clamp(s.textSize + step, ToolSettings::kMinTextSize, ToolSettings::kMaxTextSize);
        } else {
            const bool shape = tool == ToolId::Line || tool == ToolId::Rectangle || tool == ToolId::Ellipse;
            int& size = shape ? s.strokeWidth : s.brushSize;
            size = std::clamp(size + step, ToolSettings::kMinSize, shape ? maxStroke : maxBrush);
        }
    }
}

void drawRasterViewItems(Settings& settings, bool& settingsDirty, Document& doc) {
    const Keymap& keys = settings.keymap;
    if (actionMenuItem(keys, Icon::BorderInner, tr(Str::MenuPixelGrid), Action::TogglePixelGrid, &settings.pixelGrid))
        settingsDirty = true;
    if (actionMenuItem(keys, Icon::RulerTool, tr(Str::MenuRulers), Action::ToggleRulers, &settings.showRulers))
        settingsDirty = true;
    if (actionMenuItem(keys, Icon::SingleLine, tr(Str::MenuGuides), Action::ToggleGuides, &settings.showGuides))
        settingsDirty = true;
    if (actionMenuItem(keys, Icon::Anchor, tr(Str::MenuSnapToGuides), Action::ToggleSnapToGuides, &settings.snapToGuides,
                       settings.showGuides))
        settingsDirty = true;
    if (iconMenuItem(Icon::BorderClear, tr(Str::MenuClearGuides), nullptr, nullptr, !doc.guides().empty()))
        doc.guides().clear();
}

bool copySelectionToClipboard(const Document& doc, Toasts& toasts, bool cut) {
    if (!doc.selection().active())
        return false;
    const Image pixels = doc.copySelection();
    if (pixels.empty())
        return false; // the selection is entirely off the image
    if (!setClipboardImage(pixels)) {
        toasts.push(tr(Str::ErrClipboardCopy), Toasts::Kind::Error);
        return false;
    }
    toasts.push(tr(cut ? Str::ToastCut : Str::ToastCopied));
    return true;
}

bool copyPictureToClipboard(Document& doc, Toasts& toasts) {
    if (!setClipboardImage(doc.displayImage())) {
        toasts.push(tr(Str::ErrClipboardCopy), Toasts::Kind::Error);
        return false;
    }
    toasts.push(tr(Str::ToastCopied));
    return true;
}

} // namespace trace
