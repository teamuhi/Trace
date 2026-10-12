#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include "app/texture_mirror.hpp"
#include "core/cel_grid.hpp"

struct SDL_Renderer;

namespace trace {

class Document;
class Toasts;

// The layer operations of the Layer menu, the Layers panel and the New Layer shortcut. Each is one undo step
// on the active layer. Keeping them in one place means the three entry points refuse and explain the same way.
enum class LayerCommand { New, Duplicate, Delete, MergeDown, Flatten, MoveUp, MoveDown };

// Whether the command can do anything right now (Delete needs a second layer, Merge Down a layer below, ...).
// Limits that need a message (too many layers, a locked layer) are not checked here: the command explains them.
bool canRunLayerCommand(const Document& doc, LayerCommand command);

// Runs `command`, with new layers named in the current language. A refusal (the layer limit, the memory
// budget, a locked layer) or an out-of-memory result shows a toast and leaves the document untouched.
// Returns whether the document changed. The caller makes sure no tool gesture is in progress.
bool runLayerCommand(Document& doc, LayerCommand command, Toasts& toasts);

// The contents of the "Layers" window: one row per layer (top layer first) with an eye, a lock, a thumbnail
// and the name; the active layer's opacity; and buttons for New, Duplicate, Delete and Merge Down.
//
// - Click a row to make it active; double-click the name to rename it; drag a row to reorder.
// - The opacity slider applies when it is released, so one drag is one undo step.
// - While a tool gesture is running (`gestureActive`) nothing here changes the document; a rename or opacity
//   that finished meanwhile is applied as soon as the gesture ends.
//
// Thumbnails are small copies of each cel, drawn for visible rows only and rebuilt only when that cel changed.
class LayersPanel {
public:
    LayersPanel() = default;
    ~LayersPanel() { release(); }

    LayersPanel(const LayersPanel&) = delete;
    LayersPanel& operator=(const LayersPanel&) = delete;

    void init(SDL_Renderer* renderer) noexcept { renderer_ = renderer; }
    // Must run before the renderer is destroyed (thumbnail textures). Safe to call repeatedly.
    void release() noexcept;
    // The document was replaced (New, Open): forget the thumbnails and any rename in progress.
    void reset() noexcept;

    // Call between ImGui::Begin and ImGui::End.
    void draw(Document& doc, bool gestureActive, Toasts& toasts);

private:
    struct Thumb {
        TextureMirror mirror;
        std::uint64_t revision = 0; // Document::revision() when the active cel was last copied
        std::uint64_t epoch = 0;    // staleEpoch_ when it was last copied
        bool valid = false;
    };

    Thumb& thumbFor(const Document& doc, CelId id, int layer, int frame);
    void pruneThumbs(const Document& doc);

    SDL_Renderer* renderer_ = nullptr;
    std::unordered_map<CelId, Thumb> thumbs_;
    int lastFrame_ = -1;          // ImGui frame of the previous draw, to notice a gap (hidden panel)
    std::uint64_t staleEpoch_ = 1;

    // Rename
    std::uint32_t renamingId_ = 0; // layer id; 0 when not renaming
    bool renameFocus_ = false;
    char renameBuffer_[96] = {};
    std::optional<std::pair<std::uint32_t, std::string>> pendingRename_; // finished while a gesture ran

    // Opacity slider
    int opacityPercent_ = 100;
    std::optional<std::pair<std::uint32_t, int>> pendingOpacity_; // layer id and 0..100
};

} // namespace trace
