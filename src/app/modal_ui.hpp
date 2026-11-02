#pragma once

#include <imgui.h>

#include <initializer_list>

#include "app/svg_icons.hpp"

namespace trace::modal {

// The shared look of Trace's modal dialogs: one frame (width, padding, rounding, title), form rows with the label
// on the left, number fields with steppers, choices, radio rows and a right-aligned footer of buttons. All sizes
// follow the font size, so the dialogs hold up at any UI scale and in any language.
//
//   if (modal::begin("Title###Id", 26.0f)) {
//       modal::Form form({"Width", "Height"});
//       form.row("Width");   modal::numberField("##w", w, 1, 1024, "px");
//       ...
//       const int pressed = modal::footer({{"Cancel", ButtonKind::Secondary, true, false, true},
//                                          {"Apply", ButtonKind::Primary, canApply, true}});
//       ...
//       modal::end();
//   }

// Colors of the dialog widgets, derived from the theme tokens like the panels' (a fixed red per theme for danger).
struct Colors {
    ImVec4 panel;    // the dialog's own background
    ImVec4 row;      // option rows and value plates
    ImVec4 rowHover;
    ImVec4 pill;     // resting choice and button
    ImVec4 input;    // inside of fields
    ImVec4 border;
    ImVec4 danger;
    ImVec4 muted;    // labels and hints
};
Colors colors();

// Opens (when requested earlier with ImGui::OpenPopup) and begins the modal named `label` ("Visible title###Id"),
// centered, with the title drawn at the top. `widthEm` is the content width in font sizes (shrunk to fit narrow
// windows). Returns false when the popup is not open; call end() only after a true. With `close` the title row gets
// an X at the right; clicking it or pressing Escape sets *close (the caller dismisses the dialog).
bool begin(const char* label, float widthEm = 26.0f, bool* close = nullptr);
void end();

// The width of the dialog's content in pixels (valid between begin and end).
float contentWidth();

// --- text --------------------------------------------------------------------------------------

// A section title with a thin rule running to the right.
void heading(const char* text);
// Wrapped text, muted for hints.
void paragraph(const char* text, bool muted = false);
// A wrapped message beside a colored bar: the warning color for problems, the accent otherwise.
void notice(const char* text, bool warning);
// A read-only value on a plate that fills the control column (the "16 x 16 -> 32 x 24 px" line).
void valuePlate(const char* text);

// --- form rows ---------------------------------------------------------------------------------

// Rows of "label on the left, control on the right" with the controls aligned. The label column is as wide as the
// longest of `labels` (within 40% of the dialog), so the controls line up across rows; a longer label is clipped
// and shows in full as a tooltip.
class Form {
public:
    explicit Form(std::initializer_list<const char*> labels);
    // Starts a row: the muted label (nullptr for none) and the cursor on the control column. Returns the width
    // left for the control.
    float row(const char* label);

private:
    float labelX_ = 0.0f;
    float labelWidth_ = 0.0f;
    float gap_ = 0.0f;
    float width_ = 0.0f;
};

// A number field "[-] value unit [+]" kept inside [lo, hi]. The arrows step by `step` (Ctrl: `fastStep`) and
// repeat while held. `width` 0 fills the rest of the line (up to a limit). Returns true when the user changed it.
bool numberField(const char* id, int& value, int lo, int hi, const char* unit = "", int step = 1, int fastStep = 100,
                 float width = 0.0f);
// A slider with the field styling, kept inside [lo, hi]. Returns true when the user changed it.
bool slider(const char* id, int& value, int lo, int hi, const char* format = "%d");
bool checkbox(const char* label, bool& value);

// Mutually exclusive choices as pills in a row, wrapping onto more lines when they do not fit. Bit i of
// `disabledMask` greys out choice i. Returns the index clicked, or -1.
int choices(const char* id, int selected, std::initializer_list<const char*> labels, unsigned disabledMask = 0);
// A whole-width option row with a radio mark, optionally with a color swatch (transparency shows a checkerboard)
// at the right. Returns true when clicked.
bool radioRow(const char* id, bool selected, const char* label, const ImVec4* swatch = nullptr);
// A small color swatch, checkerboard behind transparency.
void drawSwatch(ImDrawList* dl, ImVec2 min, ImVec2 max, const ImVec4& color);

// --- buttons -----------------------------------------------------------------------------------

// DangerQuiet is the outlined red of an action that loses work but is not the one being pushed (Don't Save).
enum class ButtonKind { Secondary, Primary, Danger, DangerQuiet };

// A stand-alone button of the dialog style (fits its text; `width` 0) for use inside a form.
bool button(const char* label, ButtonKind kind = ButtonKind::Secondary, float width = 0.0f);

struct FooterButton {
    const char* label;
    ButtonKind kind = ButtonKind::Secondary;
    bool enabled = true;
    bool enter = false;  // Enter (when no field is being typed in) presses it; it also gets the default focus
    bool escape = false; // Escape presses it
    Icon icon = Icon::None; // drawn before the label (dropped when the button is too narrow for both)
};

// The row of buttons at the bottom, right-aligned in the given order (the primary action goes last), all of one
// size; stacked full width when they do not fit. Returns the index pressed (by click, Enter or Escape), or -1.
int footer(std::initializer_list<FooterButton> buttons);

} // namespace trace::modal
