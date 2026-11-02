#pragma once

#include <string>

#include "flow/flow_document.hpp"

namespace trace::flow {

// The built-in starting points of File > New from Template. They are generated in code, so they need no data
// files and follow the shape set of the model.
//
// - Process: Start, three steps in a row, End, joined by straight connectors.
// - DecisionTree: Start, a decision (diamond) with a Yes and a No branch (labelled connectors), and an End that
//   both branches join.
// - Swimlane: three lanes (containers with a title band) with a step in each, connected across the lanes.
enum class TemplateKind : unsigned char { Process, DecisionTree, Swimlane };
constexpr int kTemplateCount = 3;

// The words a template writes into its shapes. The English defaults are what the tests see; the app fills the
// struct from the language pack. "Step" and "Lane" get a number appended ("Step 1"), "Action" a letter ("Action A").
struct TemplateText {
    std::string start = "Start";
    std::string end = "End";
    std::string step = "Step";
    std::string decision = "Decision?";
    std::string yes = "Yes";
    std::string no = "No";
    std::string action = "Action";
    std::string lane = "Lane";
};

// A fresh document holding the template (an empty one for a kind out of range). Every connector's ends dock to
// shapes that exist, every shape inside a lane is a child of that lane, and the content starts near the origin
// (the view fits it when it is shown).
FlowDocument makeTemplate(TemplateKind kind, const TemplateText& text = {});

} // namespace trace::flow
