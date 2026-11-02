#pragma once

#include <imgui.h>

#include <memory>
#include <optional>
#include <string>

#include "core/color.hpp"
#include "core/image.hpp"
#include "flow/flow_types.hpp"

struct SDL_Renderer;

namespace trace {

// Flowchart export (docs/PLAN_FLOWCHART.md, F6). SVG is built by the pure flow::exportSvg; PNG is the board drawn
// off-screen: the workspace records the chart into a standalone ImDrawList during a normal frame (so the glyphs it
// needs are baked into the font atlas by that frame's Render), and after the main RenderDrawData the App hands the
// renderer to OffscreenImage::render, which draws that list into a render-target texture, reads it back and returns
// an Image.

enum class FlowExportFormat : unsigned char { Png, Svg };
enum class FlowExportBackground : unsigned char { White, Transparent, Board };

// What the Export dialog collects (kept for the session by the workspace).
struct FlowExportOptions {
    FlowExportFormat format = FlowExportFormat::Png;
    int scale = 2;                                                 // PNG only: 1, 2 or 4 pixels per board unit
    FlowExportBackground background = FlowExportBackground::White;
    bool selectionOnly = false;
    int padding = 24;                                              // board units around the content
};

inline constexpr int kExportMaxPadding = 200;
inline constexpr int kExportMaxPixels = 16384; // per side

// 1, 2 or 4: anything else becomes the nearest allowed value.
int exportScale(int scale) noexcept;

// Where the content lands in the image: the content rectangle plus padding, at `scale` pixels per board unit.
struct FlowExportPlan {
    flow::Rect area;    // board rectangle shown by the whole image
    float scale = 1.0f; // pixels per board unit
    int width = 0;      // image size in pixels
    int height = 0;
};

// Nullopt when the image would exceed kExportMaxPixels on a side (or the numbers are not finite).
std::optional<FlowExportPlan> planExport(const flow::Rect& content, int padding, int scale);

// A chart recorded into an ImDrawList of its own, and the means to render it to pixels. Typical use, within one
// frame: begin() between NewFrame and Render, draw into the returned list, then render() after the main frame was
// rendered. The list is registered with the ImGui context, so it must never outlive it: render() and discard()
// free it, and the destructor does too.
class OffscreenImage {
public:
    OffscreenImage() = default;
    ~OffscreenImage() { discard(); }
    OffscreenImage(const OffscreenImage&) = delete;
    OffscreenImage& operator=(const OffscreenImage&) = delete;

    // Starts a recording of `width` x `height` pixels, clipped to that size; coordinates are image pixels. `clear` is
    // what the image is filled with before the list is drawn (alpha 0 = transparent). Replaces an earlier recording.
    // Inside an ImGui frame only.
    ImDrawList* begin(int width, int height, Rgba8 clear);
    bool recorded() const noexcept { return static_cast<bool>(list_); }
    // Frees the recording without rendering it.
    void discard() noexcept { list_.reset(); }

    // Renders the recording (always freeing it). Draws in tiles when the image exceeds the renderer's maximum
    // texture size. The renderer's target, scale and draw color are left as they were found. Straight (not
    // premultiplied) alpha in the result. Nullopt with `error` set on failure. After the main RenderDrawData,
    // inside the same ImGui frame scope.
    std::optional<Image> render(SDL_Renderer* renderer, std::string& error);

private:
    std::unique_ptr<ImDrawList> list_;
    int width_ = 0;
    int height_ = 0;
    Rgba8 clear_{};
};

} // namespace trace
