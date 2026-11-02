#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "flow/flow_document.hpp"

namespace trace::flow {

// Undo/redo for a FlowDocument as a list of whole-document snapshots.
//
// Every state the caller commits is kept as a copy of the document. Images are shared pointers, so a copy
// costs the nodes and edges only, which FlowDocument::approxBytes() estimates. The list holds the oldest
// state still reachable, every state up to the current one, and any redo states after it.
//
// The caller owns the live document. After each edit (or at the end of a gesture) it calls commit(doc);
// the history records a step when the document's revision differs from the last committed state. undo() and
// redo() hand back the state to copy into the live document. The returned pointer stays valid until the next
// call that changes the history.
class FlowHistory {
public:
    static constexpr std::size_t kDefaultMaxBytes = 256u * 1024u * 1024u;
    static constexpr std::size_t kDefaultMaxSteps = 1000;
    static constexpr std::size_t kStepOverhead = 256; // added to approxBytes() for each stored state

    explicit FlowHistory(std::size_t maxBytes = kDefaultMaxBytes, std::size_t maxSteps = kDefaultMaxSteps);

    // Forgets every step, drops any open gesture and starts from `initial`, which becomes the saved state.
    void reset(const FlowDocument& initial);
    void clear() { reset(FlowDocument{}); }

    // Oldest steps are dropped to stay under the limits, but the newest step is always kept.
    // A step limit of 0 is treated as 1.
    void setMaxBytes(std::size_t maxBytes);
    void setMaxSteps(std::size_t maxSteps);
    std::size_t maxBytes() const noexcept { return maxBytes_; }
    std::size_t maxSteps() const noexcept { return maxSteps_; }
    std::size_t bytes() const noexcept { return bytes_; }

    bool canUndo() const noexcept { return !gestureActive() && cursor_ > 0; }
    bool canRedo() const noexcept { return !gestureActive() && cursor_ + 1 < states_.size(); }
    std::size_t undoCount() const noexcept { return cursor_; }
    std::size_t redoCount() const noexcept { return states_.size() - 1 - cursor_; }

    // Records `current` as a new step when its revision differs from the current committed state. Discards
    // any redo steps. Returns false, changing nothing, when nothing changed or a gesture is open (the change
    // is picked up by the outermost endGesture). May throw std::bad_alloc, again without changing anything.
    bool commit(const FlowDocument& current);

    // A gesture (a drag, a resize) is any number of edits recorded as one step. Calls nest; only the
    // outermost endGesture commits. endGesture without a matching beginGesture does nothing and returns
    // false, as does an endGesture whose edits left the document as it was.
    void beginGesture() noexcept { ++gestureDepth_; }
    bool endGesture(const FlowDocument& current);
    bool gestureActive() const noexcept { return gestureDepth_ > 0; }
    // Abandons an open gesture without recording it and returns the last committed state, which the caller
    // restores into the live document. Returns nullptr when no gesture was open.
    const FlowDocument* cancelGesture() noexcept;

    // The state to restore, or nullptr when there is nothing to undo/redo (also while a gesture is open).
    const FlowDocument* undo();
    const FlowDocument* redo();

    // The last committed state (what the live document should equal once pending edits are committed).
    const FlowDocument& committed() const noexcept { return states_[cursor_].doc; }

    // The saved state is the one a file on disk matches. It stops being reachable once a new commit
    // discards it or eviction drops it; the document then stays modified.
    void markSaved() noexcept { saved_ = cursor_; }
    bool atSavedState() const noexcept { return saved_ && *saved_ == cursor_; }
    bool isModified() const noexcept { return !atSavedState(); }
    // As isModified(), and also true while `current` holds edits that are not committed yet.
    bool isModified(const FlowDocument& current) const noexcept {
        return isModified() || current.revision() != states_[cursor_].doc.revision();
    }

private:
    struct State {
        FlowDocument doc;
        std::size_t bytes = 0;
    };

    static std::size_t costOf(const FlowDocument& doc) noexcept { return doc.approxBytes() + kStepOverhead; }
    bool commitNow(const FlowDocument& current);
    void evict();

    std::vector<State> states_; // never empty; states_[cursor_] is the current committed state
    std::size_t cursor_ = 0;
    std::size_t bytes_ = 0;
    std::size_t maxBytes_;
    std::size_t maxSteps_;
    std::size_t gestureDepth_ = 0;
    std::optional<std::size_t> saved_ = 0; // cursor_ value of the saved state, if still reachable
};

} // namespace trace::flow
