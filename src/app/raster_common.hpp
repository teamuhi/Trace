#pragma once

#include "core/keymap.hpp"

namespace trace {

class Document;
class Toasts;
class Toolbox;
struct Settings;

// What the two bitmap workspaces, Paint and Pixel Art, do the same way. Each stays in charge of its own menus
// and windows; these are the pieces that would otherwise be written twice.

// The shortcuts of the Raster context: Deselect, the pixel grid, ruler, guide and snap switches, choosing a
// tool, swapping the colors and changing the brush or outline size (kept within maxBrush / maxStroke). The
// ones that change the picture wait for `idle` (no tool gesture in progress). `mode` is the active mode, so a
// chord belonging to another mode never fires.
void handleRasterShortcuts(const Keymap& keys, ModeContext mode, Settings& settings, bool& settingsDirty, Toolbox& toolbox,
                           Document& doc, bool idle, int maxBrush, int maxStroke);

// The View menu entries both modes share: pixel grid, rulers, guides, snap to guides and clear guides.
void drawRasterViewItems(Settings& settings, bool& settingsDirty, Document& doc);

// Puts the selection on the system clipboard and shows the "Copied" or "Cut" toast (cut only changes the
// wording; the caller deletes the pixels). False when there is no selection, it lies outside the image, or the
// clipboard refused (that case shows an error toast).
bool copySelectionToClipboard(const Document& doc, Toasts& toasts, bool cut);

} // namespace trace
