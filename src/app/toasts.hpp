#pragma once

#include <string>
#include <vector>

namespace trace {

// Short messages that appear in the bottom-right corner of the window and fade out by themselves: "Saved
// photo.png", "Copied selection". They never take input or focus. Failures the user must act on stay modal
// dialogs; a toast is for things that are fine, or a minor problem that needs no decision.
class Toasts {
public:
    enum class Kind { Info, Error };

    static constexpr std::size_t kMaxVisible = 4;

    // Safe to call at any time, also while the app sleeps: the clock starts when the toast is first drawn.
    void push(std::string text, Kind kind = Kind::Info);

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
};

} // namespace trace
