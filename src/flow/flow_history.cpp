#include "flow/flow_history.hpp"

#include <algorithm>
#include <utility>

namespace trace::flow {

FlowHistory::FlowHistory(std::size_t maxBytes, std::size_t maxSteps)
    : maxBytes_(maxBytes), maxSteps_(std::max<std::size_t>(maxSteps, 1)) {
    reset(FlowDocument{});
}

void FlowHistory::reset(const FlowDocument& initial) {
    State first{initial, costOf(initial)}; // copy first: a throw leaves the history as it was
    states_.clear();
    bytes_ = first.bytes;
    states_.push_back(std::move(first));
    cursor_ = 0;
    gestureDepth_ = 0;
    saved_ = 0;
}

void FlowHistory::setMaxBytes(std::size_t maxBytes) {
    maxBytes_ = maxBytes;
    evict();
}

void FlowHistory::setMaxSteps(std::size_t maxSteps) {
    maxSteps_ = std::max<std::size_t>(maxSteps, 1);
    evict();
}

bool FlowHistory::commit(const FlowDocument& current) {
    if (gestureActive()) return false;
    return commitNow(current);
}

bool FlowHistory::endGesture(const FlowDocument& current) {
    if (gestureDepth_ == 0) return false;
    if (--gestureDepth_ > 0) return false;
    return commitNow(current);
}

const FlowDocument* FlowHistory::cancelGesture() noexcept {
    if (gestureDepth_ == 0) return nullptr;
    gestureDepth_ = 0;
    return &states_[cursor_].doc;
}

const FlowDocument* FlowHistory::undo() {
    if (!canUndo()) return nullptr;
    --cursor_;
    return &states_[cursor_].doc;
}

const FlowDocument* FlowHistory::redo() {
    if (!canRedo()) return nullptr;
    ++cursor_;
    return &states_[cursor_].doc;
}

bool FlowHistory::commitNow(const FlowDocument& current) {
    if (current.revision() == states_[cursor_].doc.revision()) return false;

    State next{current, costOf(current)}; // may throw; nothing has changed yet
    // Drop the redo steps.
    for (std::size_t i = cursor_ + 1; i < states_.size(); ++i) bytes_ -= states_[i].bytes;
    states_.erase(states_.begin() + static_cast<std::ptrdiff_t>(cursor_ + 1), states_.end());
    if (saved_ && *saved_ > cursor_) saved_.reset();

    bytes_ += next.bytes;
    states_.push_back(std::move(next));
    ++cursor_;
    evict();
    return true;
}

void FlowHistory::evict() {
    // Drop the oldest states. The current state is never dropped, and the newest step (the state before
    // it) survives the byte limit. Redo states are kept, so a history parked at its start drops nothing.
    std::size_t drop = 0;
    std::size_t bytes = bytes_;
    while (drop < cursor_) {
        const std::size_t remaining = cursor_ - drop; // undo steps left
        const bool overSteps = remaining > maxSteps_;
        const bool overBytes = bytes > maxBytes_ && remaining > 1;
        if (!overSteps && !overBytes) break;
        bytes -= states_[drop].bytes;
        ++drop;
    }
    if (drop == 0) return;
    states_.erase(states_.begin(), states_.begin() + static_cast<std::ptrdiff_t>(drop));
    bytes_ = bytes;
    cursor_ -= drop;
    if (saved_) {
        if (*saved_ < drop) saved_.reset();
        else *saved_ -= drop;
    }
}

} // namespace trace::flow
