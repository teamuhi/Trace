#pragma once

#include <filesystem>
#include <optional>

#include "app/board_export_dialog.hpp"
#include "app/canvas_view.hpp"
#include "app/file_session.hpp"
#include "app/image_dialogs.hpp"
#include "app/paint_file_adapter.hpp"
#include "app/pixel_panels.hpp"
#include "app/workspace.hpp"
#include "core/document.hpp"
#include "tools/toolbox.hpp"

namespace trace {

// Paint mode: the raster tools, the Tools panel, the canvas and a Layers panel. A picture starts as a single layer
// (so it looks and saves exactly as it always did); more layers can be added, and saving to an image format writes
// them flattened. Its windows keep the names "Tools" and "Canvas" and its dockspace the id "TraceDockSpace", so
// layouts saved by earlier versions still apply; "Layers" ("###PaintLayers") is the one added since.
class PaintWorkspace final : public Workspace {
public:
    explicit PaintWorkspace(WorkspaceContext& context);

    Mode mode() const noexcept override { return Mode::Paint; }
    void init(SDL_Window* window, SDL_Renderer* renderer) override;
    void release() noexcept override {
        canvas_.release();
        layers_.release();
    }

    ImGuiID dockSpaceId() const override;
    void buildDefaultLayout(ImGuiID dockId, ImVec2 size) override;
    bool layoutStale(ImGuiID dockId) override;
    void drawWindows() override;
    void drawDialogs() override;

    void drawFileMenuExtras() override;
    void drawModeMenus() override;
    void drawEditMenuExtras() override;
    void drawViewMenuItems() override;

    void handleShortcuts(const Keymap& keys) override;

    std::string statusLeft() const override;
    bool busy() const override { return files_.busy() || imageDialogs_.busy() || boardExport_.busy(); }
    // An open text box counts as a gesture: undo, cut, paste and the file shortcuts leave the picture alone until it
    // is applied (the box's own text field takes those keys meanwhile).
    bool gestureActive() const override { return toolbox_.gestureActive() || toolbox_.textEditing(); }
    // A dropped picture lands on the canvas as a floating selection, centered on the drop point (the view center when
    // the point is not over the canvas).
    void dropImage(const std::filesystem::path& path, ImVec2 screen, int index) override;
    void commitPending() override { toolbox_.commitText(document_); }
    bool canLeave() const override { return Workspace::canLeave() && !canvas_.draggingGuide(); }

    FileSession& fileSession() override { return files_; }
    // Whiteboards (.tboard) open here; the pictures route to Paint through the normal Open.
    bool opensProjects() const override { return true; }

    // File > Export: the picture of an endless whiteboard (see BoardExportDialog). Ordinary pictures are saved with Save As.
    bool canExport() const override { return document_.endless(); }
    void requestExport() override;
    void onNativeDialog(const NativeDialogs::Result& result) override;

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

    // The tools: Settings reads and writes their defaults.
    Toolbox& toolbox() noexcept { return toolbox_; }
    const Toolbox& toolbox() const noexcept { return toolbox_; }

private:
    void showAllWindows() override;
    bool idle() const noexcept { return !gestureActive(); }
    bool hasSelection() const noexcept { return idle() && document_.selection().active(); }
    Rgba8 clearColor() const;
    // False (with a toast saying why) when the active layer is hidden or locked, so pixels cannot change.
    bool ensureEditable();
    void copySelection(bool cut);
    void cropToSelection();
    void runCommand(LayerCommand command);
    void beginBoardExport(const BoardExportRun& run);

    // --- endless whiteboard --------------------------------------------------------------------------
    void newWhiteboard();
    // Switches the paper; the pen and background colors that matched the old paper follow it.
    void setBoardTheme(bool dark);
    // After a board is created or opened: the primary color must not equal the paper.
    void fixColorsForPaper(bool dark);
    Rgba8 paperColor() const noexcept;
    // Grows the board so it holds the area around a press (the view plus a margin); runs before the tool sees it.
    void growBoardForPress(Document& doc, Vec2f at);
    // Grows the board until `want` (image pixels) is inside. False when it could not (limit or memory; a toast says so).
    bool ensureBoardRoom(const IRect& want);
    // Puts `image` on the canvas as a floating selection with its top-left corner at (x, y) in image pixels, growing a
    // whiteboard first, and selects the Select tool so it can be dragged into place. False when it could not.
    bool placeImage(Image image, int x, int y);
    bool endlessBoard() const noexcept { return document_.endless(); }

    Document document_;
    CanvasView canvas_;
    Toolbox toolbox_;
    ImageDialogs imageDialogs_;
    BoardExportDialog boardExport_;
    std::optional<BoardExportRun> boardRun_; // chosen in the dialog, waiting for the file name
    LayersPanel layers_;
    PaintFileAdapter adapter_;
    FileSession files_;

    double boardLimitToastAt_ = -100.0; // ImGui time of the last "cannot grow" toast
    std::uint32_t seenRefusals_ = 0; // Toolbox::refusalCount() already reported
    bool layoutChecked_ = false;     // layoutStale() answers once, and not after the layout was just built
};

} // namespace trace
