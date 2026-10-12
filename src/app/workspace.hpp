#pragma once

#include <imgui.h>

#include <filesystem>
#include <functional>
#include <string>

#include "app/mode.hpp"
#include "app/native_dialogs.hpp"

struct SDL_Renderer;
struct SDL_Window;

namespace trace {

class FileSession;
class Keymap;
class Toasts;
struct Settings;

// What every workspace needs from the App: shared state it may change, and the one thing it cannot do
// alone. The references stay valid for the life of the App.
struct WorkspaceContext {
    Settings& settings;
    bool& settingsDirty; // set when a workspace changed `settings` (so they are saved)
    Toasts& toasts;
    NativeDialogs& dialogs; // the dialogs for exports and palettes; their results come back to onNativeDialog()
    // Hands a file that belongs to another workspace over to it. Returns true when it did, so the caller must
    // not open the file itself. Set by the App.
    std::function<bool(const std::filesystem::path&)> routeOpen;
};

// One way of working: Paint, Flowchart, Pixel Art. A workspace owns its document, its tools and its windows;
// the App shows exactly one at a time (only the active workspace's dockspace and windows are submitted) and
// forwards the generic File, Edit and View menu items and shortcuts to it. Switching modes never touches a
// workspace's document, so each is found as it was left.
//
// Virtuals with a body are optional; a workspace overrides what it supports.
class Workspace {
public:
    explicit Workspace(WorkspaceContext& context) : ctx_(context) {}
    virtual ~Workspace() = default;

    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;

    virtual Mode mode() const noexcept = 0;

    // Called once, after SDL, ImGui and the settings are ready.
    virtual void init(SDL_Window* window, SDL_Renderer* renderer) = 0;
    // Must run before the renderer is destroyed (GPU textures). Safe to call repeatedly.
    virtual void release() noexcept {}

    // --- dock layout and windows ---------------------------------------------------------------------
    // The id of this workspace's dockspace. Fixed forever: a saved imgui.ini layout is keyed by it.
    virtual ImGuiID dockSpaceId() const = 0;
    // Docks the windows the first time the dockspace is shown (when no saved layout exists).
    virtual void buildDefaultLayout(ImGuiID dockId, ImVec2 size) = 0;
    // True when a dockspace that already exists (from a saved layout) does not hold this workspace's windows, so
    // the default layout must be built over it. Asked while the dockspace is shown; a workspace answers true
    // at most once. Needed when a mode that used to show a placeholder gets real windows.
    virtual bool layoutStale(ImGuiID dockId) { (void)dockId; return false; }
    // The workspace's windows. Called once per frame, inside the dockspace, while the workspace is active.
    virtual void drawWindows() = 0;
    // Modals and floating dialogs, drawn after the windows and the Settings window.
    virtual void drawDialogs() {}

    // --- menus ---------------------------------------------------------------------------------------
    // Menus between Edit and View (Paint: Image).
    virtual void drawModeMenus() {}
    // Items at the end of the Edit menu, each group starting with its own separator.
    virtual void drawEditMenuExtras() {}
    // The items of the View menu after the zoom commands.
    virtual void drawViewMenuItems() {}

    // --- input ---------------------------------------------------------------------------------------
    // The shortcuts of this mode (tools, toggles). The App has already handled the global ones.
    virtual void handleShortcuts(const Keymap& keys) { (void)keys; }
    // A result from one of the non-Settings native dialogs.
    virtual void onNativeDialog(const NativeDialogs::Result& result) { (void)result; }

    // --- state ---------------------------------------------------------------------------------------
    // The left part of the status bar.
    virtual std::string statusLeft() const = 0;
    // A modal or dialog of this workspace is open: shortcuts must not act behind it.
    virtual bool busy() const = 0;
    // A pointer gesture is in progress: file and history shortcuts wait until it ends.
    virtual bool gestureActive() const { return false; }
    // False while the user is in the middle of something that would break if the workspace were hidden.
    virtual bool canLeave() const { return !gestureActive() && !busy(); }

    // --- files ---------------------------------------------------------------------------------------
    virtual FileSession& fileSession() = 0;
    // Whether this mode's project files (.tflow, .tpix) can be opened by it; Paint's images route to it
    // through the normal Open instead.
    virtual bool opensProjects() const { return false; }
    // Export is separate from Save: a project is never silently flattened. Paint has nothing to export.
    virtual bool canExport() const { return false; }
    virtual void requestExport() {}

    // --- edit verbs ----------------------------------------------------------------------------------
    virtual void undo() {}
    virtual void redo() {}
    virtual void cut() {}
    virtual void copy() {}
    virtual void paste() {}
    virtual void selectAll() {}
    virtual void deselect() {}
    virtual void deleteSelection() {}
    virtual bool canUndo() const { return false; }
    virtual bool canRedo() const { return false; }
    virtual bool canCut() const { return false; }
    virtual bool canCopy() const { return false; }
    virtual bool canPaste() const { return false; }
    virtual bool canSelectAll() const { return false; }
    virtual bool canDeselect() const { return false; }
    virtual bool canDeleteSelection() const { return false; }

    // --- view ----------------------------------------------------------------------------------------
    virtual bool canZoom() const { return false; }
    virtual void zoomIn() {}
    virtual void zoomOut() {}
    virtual void fit() {}
    virtual void actualSize() {}

protected:
    WorkspaceContext& ctx_;
};

} // namespace trace
