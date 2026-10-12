#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "tools/tool.hpp"

namespace trace {

// Owns the tools and the settings they share (colors, sizes, options) and routes pointer gestures to the
// selected tool. It guarantees that a gesture's press, drags and release all reach the same tool, and that
// a gesture is dropped safely if the document is replaced underneath it. Every finished gesture is committed
// to the document's history as one undo step.
class Toolbox {
public:
    Toolbox();

    ToolSettings& settings() noexcept { return settings_; }
    const ToolSettings& settings() const noexcept { return settings_; }

    ToolId current() const noexcept { return current_; }
    // Returns false (and keeps the current tool) while a gesture is in progress.
    bool select(ToolId id) noexcept;

    bool gestureActive() const noexcept { return active_; }

    void press(Document& doc, const PointerEvent& ev);
    void drag(Document& doc, const PointerEvent& ev);
    void release(Document& doc, const PointerEvent& ev);
    void cancel(Document& doc);

    void swapColors() noexcept { std::swap(settings_.primary, settings_.secondary); }

    // Brush ring radius for the current tool in image pixels (0 for none).
    float cursorRadius() const noexcept;

    static const char* name(ToolId id) noexcept;

private:
    Tool& tool() noexcept { return *tools_[static_cast<std::size_t>(current_)]; }
    const Tool& tool() const noexcept { return *tools_[static_cast<std::size_t>(current_)]; }
    // True when the gesture's document is still the one being drawn on; otherwise the gesture is aborted.
    bool stillValid(const Document& doc);

    std::array<std::unique_ptr<Tool>, kToolCount> tools_;
    ToolSettings settings_;
    ToolId current_ = ToolId::Pencil;
    bool active_ = false;
    std::uint64_t gestureGeneration_ = 0;
};

} // namespace trace
