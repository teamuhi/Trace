#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "app/clipboard.hpp"
#include "app/file_session.hpp"
#include "app/flow_export.hpp"
#include "app/flow_file_adapter.hpp"
#include "app/settings.hpp"
#include "app/flow_panels.hpp"
#include "app/flow_state.hpp"
#include "app/flow_view.hpp"
#include "app/workspace.hpp"

namespace trace {

// Flowchart mode: an unbounded board of shapes, connectors, text and images (see docs/PLAN_FLOWCHART.md). The
// workspace owns the FlowState (document, undo history, selection, tool), the board view, the Shapes and
// Properties panels and the file session. The view and the panels edit the state; the workspace adds what needs
// the rest of the app: windows and menus, shortcuts, the edit verbs with their internal clipboard, image import
// and the "Delete contents too?" prompt.
//
// Windows: "Shapes" (left), "Flow Canvas" and "Properties" (right), in the dockspace "TraceDockSpace.Flow".
// Their ids (after ###) never change, so a saved layout survives a language change.
class FlowWorkspace final : public Workspace {
public:
    explicit FlowWorkspace(WorkspaceContext& context);

    Mode mode() const noexcept override { return Mode::Flowchart; }
    void init(SDL_Window* window, SDL_Renderer* renderer) override;
    void release() noexcept override;

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
    void onNativeDialog(const NativeDialogs::Result& result) override;

    std::string statusLeft() const override;
    bool busy() const override { return files_.busy() || askDelete_ || exportBusy(); }
    bool gestureActive() const override { return view_.gestureActive(); }

    FileSession& fileSession() override { return files_; }
    bool opensProjects() const override { return true; }

    // Export (PNG and SVG): the Export dialog, then the native save dialog, then the file (flow_export.hpp).
    bool canExport() const override { return idle() && !files_.busy() && !state_.doc.empty(); }
    void requestExport() override;
    void afterRender(SDL_Renderer* renderer) override;
    bool wantsFrame() const override { return exportStage_ == ExportStage::Queued; }

    void undo() override;
    void redo() override;
    void cut() override;
    void copy() override;
    void paste() override;
    void selectAll() override;
    void deselect() override;
    void deleteSelection() override;
    bool canUndo() const override { return idle() && state_.history.canUndo(); }
    bool canRedo() const override { return idle() && state_.history.canRedo(); }
    bool canCut() const override { return canCopy(); }
    bool canCopy() const override { return idle() && !state_.selNodes.empty(); }
    bool canPaste() const override;
    bool canSelectAll() const override { return idle() && !state_.doc.empty(); }
    bool canDeselect() const override { return idle() && state_.hasSelection(); }
    bool canDeleteSelection() const override { return idle() && state_.hasSelection(); }

    bool canZoom() const override { return true; }
    void zoomIn() override { view_.zoomIn(); }
    void zoomOut() override { view_.zoomOut(); }
    void fit() override { view_.fit(state_); }
    void actualSize() override { view_.actualSize(); }

    FlowState& state() noexcept { return state_; }
    const FlowState& state() const noexcept { return state_; }
    FlowView& view() noexcept { return view_; }

private:
    void showAllWindows() override;
    // What Copy keeps: the copied nodes (in paint order), the edges among them and the images they use. Paste
    // inserts fresh copies, so the clipboard survives edits and deletes of the originals.
    struct Clip {
        std::vector<flow::Node> nodes;
        std::vector<flow::Edge> edges;
        std::unordered_map<flow::ImageId, flow::ImagePtr> images;
        flow::Rect bounds;
        bool empty() const noexcept { return nodes.empty(); }
    };
    enum class Inserted { Ok, Full, TooManyImages };
    enum class Arrange { Forward, Backward, Front, Back, Group, Ungroup };
    using Edit = std::function<bool(flow::FlowDocument&, std::span<const flow::NodeId>)>;

    // Nothing is being dragged, typed or asked: the verbs and shortcuts may act.
    bool idle() const noexcept { return !view_.gestureActive() && !askDelete_ && !exportBusy(); }

    Clip capture(std::span<const flow::NodeId> ids) const;
    static Inserted insertClip(flow::FlowDocument& doc, const Clip& clip, float dx, float dy,
                               std::vector<flow::NodeId>& created);
    // Inserts the clip shifted by (dx, dy) as one undo step and selects the new shapes. Reports a full document.
    bool insertAndSelect(const Clip& clip, float dx, float dy);
    bool addImageNode(Image image, flow::Pt center);
    void pasteSystemImage();
    void dropImage(const std::filesystem::path& path, ImVec2 screen, int index) override;
    // The system clipboard holds the raster of what Copy last kept in `clip_`, so Paste inside Flowchart still
    // inserts the shapes, not that picture.
    bool clipboardIsOurs() const noexcept { return !clip_.empty() && clipAlive_ && clipAlive_->load(); }
    // Queues a transparent picture of the selected shapes for the system clipboard (other modes, other programs).
    // `snapshot`: keep a copy of the board to draw from, because the shapes are about to be removed (Cut).
    void queueClipboardRaster(bool snapshot);
    void duplicateSelection();
    // Removes the selection as one undo step. `withContents`: the children of removed containers go too.
    void removeSelection(bool withContents);
    bool selectionHasFilledContainer() const;
    void drawDeletePrompt();

    // Runs `edit` on the selected shapes and commits when it changed something.
    void editSelection(const Edit& edit);
    void arrange(Arrange op);
    // Auto layout of the selected shapes (two or more), or of the whole board.
    void autoLayoutBoard();
    // Keeps FlowState and Settings::flow in step: a change in the Settings page reaches the board, and a change made on
    // the board (View menu toggles, the defaults of new connectors in Properties) is saved.
    void syncSettings();
    bool canGroup() const noexcept { return idle() && state_.selNodes.size() >= 2; }
    bool canUngroup() const noexcept;
    void commit();
    void requestImport();

    // --- export (flow_workspace.cpp, "export" section) ---
    enum class ExportStage : unsigned char {
        Idle,
        AwaitingSave, // the native save dialog is open
        Queued,       // the path is known; the chart is recorded in the next rendered frame and saved after it
    };
    // The Export dialog is open or an export is under way.
    bool exportBusy() const noexcept { return exportAsk_ || exportStage_ != ExportStage::Idle; }
    void drawExportDialog();
    void beginExport();
    void writeSvg(const std::filesystem::path& path);
    // File > Save As in a picture or page format: the whole chart with the options of the Export dialog. SVG is
    // written at once; the others are queued like a PNG export and written, in `format`, after the next frame.
    std::string exportFromSave(const std::filesystem::path& path, ImageFormat format, const SaveOptions& options);
    void recordPngExport();
    void onExportDialog(const NativeDialogs::Result& result);
    void cancelExport() noexcept;

    FlowState state_; // declared before the adapter that refers to it
    FlowSettings synced_; // the board settings as last compared with Settings::flow (see syncSettings)
    FlowView view_;
    FlowShapesPanel shapesPanel_;
    FlowPropertiesPanel propertiesPanel_;
    FlowFileAdapter adapter_;
    FileSession files_;

    Clip clip_;
    flow::Pt lastPaste_;     // where the last paste landed, so pasting again steps aside instead of stacking
    int pasteStep_ = 0;
    bool askDelete_ = false;   // the "Delete contents too?" prompt is wanted or open
    bool deleteShown_ = false; // its popup has been opened
    bool showShapes_ = true;   // the panels are open (not saved: they are open at every start)
    bool showProperties_ = true;
    bool layoutChecked_ = false; // layoutStale() answers once, and not after the layout was just built

    // Export. `exportOptions_` is what the dialog edits and is kept for the session; `exportRun_` is the copy an
    // export under way uses, together with the shapes "Selection only" meant when Export... was pressed.
    FlowExportOptions exportOptions_;
    FlowExportOptions exportRun_;
    ExportStage exportStage_ = ExportStage::Idle;
    bool exportAsk_ = false;   // the Export dialog is wanted or open
    bool exportShown_ = false; // its popup has been opened
    std::vector<flow::NodeId> exportOnly_;
    std::filesystem::path exportPath_;
    ImageFormat exportFormat_ = ImageFormat::Png; // what the queued picture is written as
    SaveOptions exportSave_;                      // its JPEG quality and page resolution
    OffscreenImage exportImage_;
    // A queued export that goes to the system clipboard (Copy, Cut) instead of a file, and the board it is drawn
    // from when that is not the live one.
    bool exportToClipboard_ = false;
    std::optional<flow::FlowDocument> exportDoc_;
    ClipboardToken clipAlive_; // true while the system clipboard still holds the raster of `clip_`
};

} // namespace trace
