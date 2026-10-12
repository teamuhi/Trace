#pragma once

#include "core/image.hpp"
#include "io/image_io.hpp"

namespace trace {

// The system clipboard as seen by the paint program: one image at a time. Copies are offered as PNG (keeps
// transparency) and BMP (for older programs); pastes accept either. SDL maps these to the native formats
// ("PNG" and CF_DIB on Windows), so images move to and from other applications.

// False when the clipboard could not be written.
bool setClipboardImage(const Image& image);

bool clipboardHasImage();

// An image from the clipboard, or an explanation of why there is none.
LoadResult getClipboardImage();

} // namespace trace
