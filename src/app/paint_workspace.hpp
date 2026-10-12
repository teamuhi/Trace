#pragma once

#include "app/canvas_view.hpp"
#include "app/file_session.hpp"
#include "app/image_dialogs.hpp"
#include "app/paint_file_adapter.hpp"
#include "app/workspace.hpp"
#include "core/document.hpp"
#include "tools/toolbox.hpp"

namespace trace {

// Paint mode: one flat image, the nine raster tools, the Tools panel and the canvas. This is everything the
// app was before modes existed; its windows keep the names "Tools" and "Canvas" and its dockspace the id
// "TraceDockSpace", so layouts saved by earlier versions still apply.
class PaintWorkspace final : public Workspace {
public:
    explicit PaintWorkspace(WorkspaceContext& context);

    Mode mode() const noexcept override { return Mode::Paint; }
    void init(SDL_Window* window, SDL_Renderer* renderer) override;
    void release() noexcept override { canvas_.release(); }

    ImGuiID dockSpaceId() const override;
    void buildDefaultLayout(ImGuiID dockId, ImVec2 size) override;
    void drawWindows() override;
    void drawDialogs() override;

    void drawModeMenus() override;
    void drawEditMenuExtras() override;
    void drawViewMenuItems() override;

    void handleShortcuts(const Keymap& keys) override;

    std::string statusLeft() const override;
    bool busy() const override { return files_.busy() || imageDialogs_.busy(); }
    bool gestureActive() const override { return toolbox_.gestureActive(); }
    bool canLeave() const override { return Workspace::canLeave() && !canvas_.draggingGuide(); }

    FileSession& fileSession() override { return files_; }

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
    bool idle() const noexcept { return !toolbox_.gestureActive(); }
    bool hasSelection() const noexcept { return idle() && document_.selection().active(); }
    Rgba8 clearColor() const;
    void copySelection(bool cut);
    void cropToSelection();

    Document document_;
    CanvasView canvas_;
    Toolbox toolbox_;
    ImageDialogs imageDialogs_;
    PaintFileAdapter adapter_;
    FileSession files_;
};

} // namespace trace
