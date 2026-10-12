#pragma once

#include <span>

#include "tools/tool.hpp"

namespace trace {

class Keymap;
class Toolbox;

// Which set of options the panel shows for each tool. Pixel: sizes stop at 16 px, no antialiasing, the
// eraser always clears, and the pencil, brush and fill get their pixel art options (pixel-perfect, round or
// square tip, contiguous or global fill).
enum class ToolOptions { Paint, Pixel };

// Contents of the "Tools" window: tool buttons, options for the selected tool, primary/secondary color
// swatches with an RGBA editor, and the palette. Must be called between ImGui::Begin and ImGui::End.
// `keymap` supplies the shortcuts shown in tooltips. This form shows every tool with the Paint options.
void drawToolsPanel(Toolbox& tools, const Keymap& keymap);

// The same with a chosen set of tool buttons (in this order) and option variant.
void drawToolsPanel(Toolbox& tools, const Keymap& keymap, std::span<const ToolId> toolset, ToolOptions options);

} // namespace trace
