#pragma once

#include <atomic>
#include <memory>

#include "core/image.hpp"
#include "io/image_io.hpp"

namespace trace {

// The system clipboard as seen by the paint program: one image at a time. Copies are offered as PNG (keeps
// transparency) and BMP (for older programs); pastes accept either. SDL maps these to the native formats
// ("PNG" and CF_DIB on Windows), so images move to and from other applications.

// A flag that tells its holder whether the clipboard still holds what it put there: set to true by a successful
// setClipboardImage and back to false the moment the clipboard is cleared or taken over (by Trace or, where the
// system reports it, by another program).
using ClipboardToken = std::shared_ptr<std::atomic<bool>>;

// False when the clipboard could not be written. `token`, when given, follows the copy as described above; pass a
// fresh token per copy.
bool setClipboardImage(const Image& image, ClipboardToken token = nullptr);

bool clipboardHasImage();

// An image from the clipboard, or an explanation of why there is none.
LoadResult getClipboardImage();

} // namespace trace
