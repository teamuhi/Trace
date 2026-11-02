#pragma once

#include <string>

#include "app/flow_state.hpp"

namespace trace {

class Keymap;

// The contents of the "Shapes" window (left): the tools (Select, Hand, Connector, Text, Sticky note, Image) in an
// icon track, then an icon-only grid of the shapes in a card. Clicking sets state.tool / state.shapeKind (the Image
// tool also raises state.importImageRequested). The active tool and shape are drawn with the accent; the shape icons
// are drawn with draw-list primitives (sharp at any scale) and follow the light and dark themes. The name of each
// button, and its shortcut when it has one (from displayShortcut in app/keybinds.hpp), is its hover tooltip.
class FlowShapesPanel {
public:
    void draw(FlowState& state, const Keymap& keys);
};

// The contents of the "Properties" window (right), in grouped cards under section headings. For the selection:
// fill, stroke, width, dash, corner radius, and a Text card (color, size, horizontal and vertical alignment as icon
// groups; each color has an Auto checkbox). For edges the route (with a reset to automatic routing), both
// arrowheads, the label and its placement. Then the Arrange card, all icon-only with tooltips (align, distribute,
// match size, order, group, ungroup). Handles multi-selection and
// mixed values. Every change is committed to state.history as one undo step; a slider or color picker that is
// being dragged applies on release.
class FlowPropertiesPanel {
public:
    void draw(FlowState& state);

private:
    bool gestureOpen_ = false; // a history gesture this panel began for a slider, picker or text drag
    std::string labelBuf_;     // the connector label being edited
    bool labelEditing_ = false; // the label field was active last frame
};

} // namespace trace
