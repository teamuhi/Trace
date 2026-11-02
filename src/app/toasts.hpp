#pragma once

#include <imgui.h>

#include <string>
#include <vector>

namespace trace {

// Short messages that appear at the top center of the canvas and fade out by themselves: "Saved photo.png",
// "Copied selection". They never take input or focus. Failures the user must act on stay modal
// dialogs; a toast is for things that are fine, or a minor problem that needs no decision.
class Toasts {
public:
    enum class Kind { Info, Error };

    static constexpr std::size_t kMaxVisible = 4;

    // Safe to call at any time, also while the app sleeps: the clock starts when the toast is first drawn.
    void push(std::string text, Kind kind = Kind::Info);

    // The canvas window's rectangle, which the toasts center on. Each workspace calls it every frame from inside its
    // canvas window; when it was not called this frame (no canvas is showing) the toasts use the top center of the
    // main window instead.
    void setCanvasRect(ImVec2 pos, ImVec2 size);

    // Draws the live toasts over everything else. Call once per frame, inside the ImGui frame.
    void draw();

    // True while anything is showing, so the render loop keeps going until the last toast has faded.
    bool active() const noexcept { return !toasts_.empty(); }

private:
    struct Toast {
        std::string text;
        Kind kind = Kind::Info;
        double expires = -1.0; // ImGui time; negative until the first draw
    };

    std::vector<Toast> toasts_; // oldest first
    ImVec2 canvasPos_{0.0f, 0.0f};
    ImVec2 canvasSize_{0.0f, 0.0f};
    int canvasFrame_ = -1; // ImGui frame count of the last setCanvasRect
};

} // namespace trace
