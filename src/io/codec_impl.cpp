// Single-header codecs are compiled here, once, so their implementation macros and options stay out of
// every other translation unit. stb is restricted to the formats Trace supports, which also keeps the
// attack surface of the decoders as small as it can be.

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_GIF
#define STBI_NO_STDIO // images are decoded from memory; paths are handled with std::filesystem
#define STBI_MAX_DIMENSIONS 16384
#include <stb_image.h>

#include "io/image_io.hpp"
static_assert(trace::kMaxImageDimension == STBI_MAX_DIMENSIONS, "keep the decoder limit in sync with the app limit");

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO // output goes through callbacks into a std::ofstream
#include <stb_image_write.h>

#define MSF_GIF_IMPL
#include <msf_gif.h>
