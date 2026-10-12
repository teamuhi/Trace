#pragma once

#include <optional>
#include <string>

#include "core/color.hpp"
#include "core/transform.hpp"

namespace trace {

class Document;

// The Image menu's two dialogs: Resize (scale the picture) and Canvas Size (grow or cut the canvas around
// it, with an anchor). Both are ImGui modals; while one is open busy() is true and the app ignores shortcuts.
class ImageDialogs {
public:
    // `transparentFill` is what a grown canvas starts with selected (Pixel Art grows into transparency).
    explicit ImageDialogs(bool transparentFill = false) : fillTransparent_(transparentFill) {}

    // `maxSide` is the largest width or height the dialog will accept (Paint: kMaxImageSide; Pixel Art 1024).
    void openResize(const Document& doc, int maxSide = kMaxImageSide);
    void openCanvasSize(const Document& doc, int maxSide = kMaxImageSide);

    bool busy() const noexcept { return kind_ != Kind::None; }

    // What happened when a dialog was confirmed, for the app to show.
    struct Outcome {
        std::string text;
        bool error = false; // out of memory; the picture is untouched
    };

    // Draws whichever dialog is open and applies it when confirmed. `secondary` is the color a grown canvas
    // can be filled with. Returns an outcome only on the frame an operation ran.
    std::optional<Outcome> draw(Document& doc, Rgba8 secondary);

private:
    enum class Kind { None, Resize, CanvasSize };

    void drawResize(Document& doc, std::optional<Outcome>& outcome);
    void drawCanvasSize(Document& doc, Rgba8 secondary, std::optional<Outcome>& outcome);

    Kind kind_ = Kind::None;
    bool openRequested_ = false;

    int sourceWidth_ = 1; // the image when the dialog opened
    int sourceHeight_ = 1;
    int maxSide_ = kMaxImageSide;

    // Resize
    int byPercent_ = 1; // 1: percent, 0: pixels (an int because ImGui radio buttons edit an int)
    bool keepAspect_ = true;
    int percentX_ = 100;
    int percentY_ = 100;
    int width_ = 1;
    int height_ = 1;
    int filter_ = 0; // ResizeFilter

    // Canvas size (width_ / height_ are shared)
    int anchor_ = static_cast<int>(Anchor::Center);
    bool fillTransparent_;
};

} // namespace trace
