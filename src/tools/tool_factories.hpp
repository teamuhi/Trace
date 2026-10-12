#pragma once

#include <memory>

#include "tools/tool.hpp"

namespace trace {

// One factory per implementation file; createTool() in toolbox.cpp dispatches between them.
std::unique_ptr<Tool> createFreehandTool(ToolId id); // Pencil, Brush, Eraser
std::unique_ptr<Tool> createShapeTool(ToolId id);    // Line, Rectangle, Ellipse
std::unique_ptr<Tool> createClickTool(ToolId id);    // Fill, Picker
std::unique_ptr<Tool> createSelectTool();            // Select

} // namespace trace
