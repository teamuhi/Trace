#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/image.hpp"
#include "io/image_io.hpp"

namespace trace {

// The encoders for the formats stb does not write, built here without further dependencies:
//  - TIFF: baseline, one uncompressed strip, 8-bit RGBA with unassociated alpha.
//  - ICO: one 32-bit PNG entry. A picture larger than 256 pixels on a side is shrunk to fit (area average) and
//    every picture is centred on a transparent square, which is what icon viewers expect.
//  - QOI: the Quite OK Image format, four channels.
//  - PPM: binary P6; transparency is flattened onto white.
//  - SVG: the picture embedded as a PNG data URI; with SaveOptions::pixelArt one path of rectangles per color
//    instead (falling back to the embedded picture when the image has too many runs for that to stay small).
//  - PDF: one page the size of the picture at SaveOptions::dpi (kept within the 200 inch limit of PDF viewers),
//    the picture as a Flate image with a soft mask when it has transparency.
//  - DOCX: a Word document with the picture on an A4 page, landscape for a wide one, shrunk to fit the page.
// Returns the file's bytes, or an empty vector for an unsupported format, an empty image or a failure.
std::vector<std::uint8_t> encodeExtra(const Image& image, ImageFormat format, const SaveOptions& options);

// zlib stream of `size` bytes at `data` (the compressor stb uses for PNG). Empty on failure.
std::vector<std::uint8_t> zlibCompress(const std::uint8_t* data, std::size_t size);

} // namespace trace
