#include "app/raster_common.hpp"

#include <imgui.h>

#include <algorithm>

#include "app/clipboard.hpp"
#include "app/keybinds.hpp"
#include "app/settings.hpp"
#include "app/toasts.hpp"
#include "core/document.hpp"
#include "core/i18n.hpp"
#include "tools/toolbox.hpp"

namespace trace {

void handleRasterShortcuts(const Keymap& keys, ModeContext mode, Settings& settings, bool& settingsDirty, Toolbox& toolbox,
                           Document& doc, bool idle, int maxBrush, int maxStroke) {
    const auto fire = [&](Action action, bool repeat = false) { return triggeredIn(keys, action, mode, repeat); };

    // Ctrl+D belongs to a focused text field (a hex color, a number) when there is one.
    if (idle && !ImGui::GetIO().WantTextInput && fire(Action::Deselect))
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

    for (int i = 0; i < kToolCount; ++i) {
        const ToolId id = static_cast<ToolId>(i);
        if (fire(toolAction(id)))
            toolbox.select(id);
    }
    if (fire(Action::SwapColors))
        toolbox.swapColors();

    const int step = (fire(Action::IncreaseSize, true) ? 1 : 0) - (fire(Action::DecreaseSize, true) ? 1 : 0);
    if (step != 0) {
        ToolSettings& s = toolbox.settings();
        const ToolId tool = toolbox.current();
        const bool shape = tool == ToolId::Line || tool == ToolId::Rectangle || tool == ToolId::Ellipse;
        int& size = shape ? s.strokeWidth : s.brushSize;
        size = std::clamp(size + step, ToolSettings::kMinSize, shape ? maxStroke : maxBrush);
    }
}

void drawRasterViewItems(Settings& settings, bool& settingsDirty, Document& doc) {
    const Keymap& keys = settings.keymap;
    if (actionMenuItem(keys, tr(Str::MenuPixelGrid), Action::TogglePixelGrid, &settings.pixelGrid))
        settingsDirty = true;
    if (actionMenuItem(keys, tr(Str::MenuRulers), Action::ToggleRulers, &settings.showRulers))
        settingsDirty = true;
    if (actionMenuItem(keys, tr(Str::MenuGuides), Action::ToggleGuides, &settings.showGuides))
        settingsDirty = true;
    if (actionMenuItem(keys, tr(Str::MenuSnapToGuides), Action::ToggleSnapToGuides, &settings.snapToGuides,
                       settings.showGuides))
        settingsDirty = true;
    if (ImGui::MenuItem(tr(Str::MenuClearGuides), nullptr, false, !doc.guides().empty()))
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

} // namespace trace
