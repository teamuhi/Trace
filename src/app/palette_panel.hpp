#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "app/native_dialogs.hpp"
#include "app/workspace.hpp"
#include "core/palette_edit.hpp"

namespace trace {

class Document;
class Toolbox;
struct ToolSettings;

// The contents of the Pixel Art "Palette" window and its "Replace Color" dialog. The palette itself is a
// PaletteState owned by the workspace (it is saved in the `.tpix` file); this class is only the view on it.
//
// - Click a swatch: primary color. Right click: secondary color. Drag a swatch onto another to reorder.
// - The clicked swatch is highlighted for as long as its color is still the primary or secondary one; Remove and
//   the Replace dialog's starting color use it.
// - Add, Remove, Load, Save, From canvas and Sort edit the palette (no undo, but the file is marked modified).
// - Replace color is one undo step of the document (Document::replaceColor), refused while a tool gesture runs.
//
// Load and Save go through NativeDialogs (tags PaletteLoad and PaletteSave); the workspace forwards the results
// to onNativeDialog(). Problems are shown as toasts.
class PalettePanel {
public:
    PalettePanel(WorkspaceContext& context, PaletteState& state) : ctx_(context), state_(state) {}

    // The document was replaced (New, Open): forget the selection and close the dialog.
    void reset() noexcept;

    // Call between ImGui::Begin and ImGui::End.
    void draw(Document& doc, Toolbox& tools);
    // The Replace Color dialog; call every frame, outside any window.
    void drawDialogs(Document& doc, Toolbox& tools);

    // Handles the result of a PaletteLoad or PaletteSave dialog. Returns false for any other dialog.
    bool onNativeDialog(const NativeDialogs::Result& result);

    // The Replace Color dialog is open: shortcuts must not act behind it.
    bool busy() const noexcept { return replaceActive_ || openReplace_; }

private:
    void drawButtons(Document& doc, Toolbox& tools);
    void drawSwatches(Toolbox& tools);
    // The highlighted swatch, or -1: the clicked one while its color is still the primary or secondary color.
    int markedIndex(const ToolSettings& settings) const noexcept;

    void addPrimary(const ToolSettings& settings);
    void removeSelected(const ToolSettings& settings);
    void sortBy(PaletteSort by);
    void fromCanvas(const Document& doc);
    void requestReplace(const ToolSettings& settings);
    void loadFrom(const std::string& utf8Path);
    void saveTo(const std::string& utf8Path);
    void rememberPath(const std::filesystem::path& path);

    WorkspaceContext& ctx_;
    PaletteState& state_;

    int selected_ = -1;
    std::optional<std::pair<int, int>> pendingMove_; // from, to: applied after the swatches were drawn

    // Replace Color dialog
    bool openReplace_ = false;   // asked for; the popup opens in the next drawDialogs()
    bool replaceActive_ = false; // the popup is open
    bool closeReplace_ = false;  // the document was replaced behind it: close it
    float replaceFrom_[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float replaceTo_[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    int scope_ = 0; // index into the scope list; kept between openings
};

} // namespace trace
