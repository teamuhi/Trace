#pragma once

#include <array>
#include <span>

#include "tools/tool.hpp"

namespace trace {

class Keymap;
class Toolbox;

// Every tool, in the order of the Paint panel (and the order of its shortcuts).
inline constexpr std::array<ToolId, kToolCount> kPaintTools = {
    ToolId::Pencil, ToolId::Brush, ToolId::Eraser, ToolId::Line,   ToolId::Rectangle,
    ToolId::Ellipse, ToolId::Fill, ToolId::Picker, ToolId::Select, ToolId::Text};

// Which set of options the panel shows for each tool. Pixel: sizes stop at 16 px, no antialiasing, the
// eraser always clears, and the pencil, brush and fill get their pixel art options (pixel-perfect, round or
// square tip, contiguous or global fill).
enum class ToolOptions { Paint, Pixel };

// Contents of the "Tools" window: tool buttons, options for the selected tool, primary/secondary color
// swatches with an RGBA editor, and the palette. Must be called between ImGui::Begin and ImGui::End.
// `keymap` supplies the shortcuts shown in tooltips. This form shows every tool with the Paint options.
void drawToolsPanel(Toolbox& tools, const Keymap& keymap);

// Pixel Art only: the size of the canvas drawn on, so the Symmetry list can show a position in pixels or in percent.
// A side of 0 (no canvas) hides those fields.
struct PixelCanvasSize {
    int width = 0;
    int height = 0;
};

// A small muted title with a rule to the right edge, the heading of a section of the Tools panel (also used by the
// Symmetry list, so every section of the sidebar looks the same).
void sectionHeader(const char* text);

// The same with a chosen set of tool buttons (in this order) and option variant.
void drawToolsPanel(Toolbox& tools, const Keymap& keymap, std::span<const ToolId> toolset, ToolOptions options,
                    PixelCanvasSize canvas = {});

} // namespace trace
