#pragma once

#include "app/canvas_view.hpp"
#include "app/file_session.hpp"
#include "app/image_dialogs.hpp"
#include "app/pixel_file_adapter.hpp"
#include "app/pixel_panels.hpp"
#include "app/workspace.hpp"
#include "core/document.hpp"
#include "tools/toolbox.hpp"

namespace trace {

// Pixel Art mode: a layered (and later animated) canvas of at most 1024 x 1024 pixels, the nine raster tools in
// their whole-pixel form, a Layers panel, and `.tpix` project files. It owns its own Document, Toolbox (with its
// own colors and sizes, saved under [pixel]), canvas and file session, so Paint is untouched by anything here.
//
// Windows: "Pixel Tools" (left), "Pixel Canvas", "Layers" (right), in the dockspace "TraceDockSpace.Pixel".
// Their ids (after ###) never change, so a saved layout survives a language change.
class PixelWorkspace final : public Workspace {
public:
    explicit PixelWorkspace(WorkspaceContext& context);

    Mode mode() const noexcept override { return Mode::PixelArt; }
    void init(SDL_Window* window, SDL_Renderer* renderer) override;
    void release() noexcept override;

    ImGuiID dockSpaceId() const override;
    void buildDefaultLayout(ImGuiID dockId, ImVec2 size) override;
    bool layoutStale(ImGuiID dockId) override;
    void drawWindows() override;
    void drawDialogs() override;

    void drawModeMenus() override;
    void drawEditMenuExtras() override;
    void drawViewMenuItems() override;

    void handleShortcuts(const Keymap& keys) override;
    void onNativeDialog(const NativeDialogs::Result& result) override;

    std::string statusLeft() const override;
    bool busy() const override { return files_.busy() || imageDialogs_.busy(); }
    bool gestureActive() const override { return toolbox_.gestureActive(); }
    bool canLeave() const override { return Workspace::canLeave() && !canvas_.draggingGuide(); }

    FileSession& fileSession() override { return files_; }
    bool opensProjects() const override { return true; }
    // Export writes the composite of the shown frame as a PNG (at x1); a project is never flattened by Save.
    bool canExport() const override { return idle(); }
    void requestExport() override;

    void undo() override { document_.undo(); }
    void redo() override { document_.redo(); }
    void cut() override { copySelection(true); }
    void copy() override { copySelection(false); }
    void paste() override;
    void selectAll() override;
    void deselect() override { document_.clearSelection(); }
    void deleteSelection() override;
    bool canUndo() const override { return idle() && document_.canUndo(); }
    bool canRedo() const override { return idle() && document_.canRedo(); }
    bool canCut() const override { return hasSelection(); }
    bool canCopy() const override { return hasSelection(); }
    bool canPaste() const override;
    bool canSelectAll() const override { return idle(); }
    bool canDeselect() const override { return hasSelection(); }
    bool canDeleteSelection() const override { return hasSelection(); }

    bool canZoom() const override { return true; }
    void zoomIn() override { canvas_.zoomIn(); }
    void zoomOut() override { canvas_.zoomOut(); }
    void fit() override { canvas_.fitToWindow(); }
    void actualSize() override { canvas_.actualSize(); }

    // For the phases that build on this workspace (import, palette, symmetry, timeline).
    Document& document() noexcept { return document_; }
    const Document& document() const noexcept { return document_; }
    Toolbox& toolbox() noexcept { return toolbox_; }
    CanvasView& canvas() noexcept { return canvas_; }
    LayersPanel& layersPanel() noexcept { return layers_; }

private:
    bool idle() const noexcept { return !toolbox_.gestureActive(); }
    bool hasSelection() const noexcept { return idle() && document_.selection().active(); }
    // False (with a toast saying why) when the active layer is hidden or locked, so pixels cannot change.
    bool ensureEditable();
    void copySelection(bool cut);
    void cropToSelection();
    void runCommand(LayerCommand command);
    // Keeps the pixel tools within pixel art's limits and in step with the saved settings.
    void syncToolSettings();

    Document document_;
    CanvasView canvas_;
    Toolbox toolbox_;
    ImageDialogs imageDialogs_;
    LayersPanel layers_;
    PixelFileAdapter adapter_;
    FileSession files_;

    ToolSettings synced_;                 // the tool settings as last compared with Settings::pixel.tools
    std::uint32_t seenRefusals_ = 0;      // Toolbox::refusalCount() already reported
    bool layoutChecked_ = false;          // layoutStale() answers once, and not after the layout was just built
};

} // namespace trace
