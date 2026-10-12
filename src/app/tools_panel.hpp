#pragma once

namespace trace {

class Keymap;
class Toolbox;

// Contents of the "Tools" window: tool buttons, options for the selected tool, primary/secondary color
// swatches with an RGBA editor, and the palette. Must be called between ImGui::Begin and ImGui::End.
// `keymap` supplies the shortcuts shown in tooltips.
void drawToolsPanel(Toolbox& tools, const Keymap& keymap);

} // namespace trace
