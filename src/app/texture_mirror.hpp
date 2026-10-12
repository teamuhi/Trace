#pragma once

#include "core/dirty_rect.hpp"
#include "core/image.hpp"

struct SDL_Renderer;
struct SDL_Texture;

namespace trace {

// A GPU texture that mirrors an Image. It is created at the image's size, recreated when that size changes,
// and kept current by uploading only the rectangle that changed. The canvas uses one for the document;
// onion skins, image nodes and previews use more.
class TextureMirror {
public:
    // What sync() did, so the caller can react (the canvas refits the view when the size changed).
    enum class Sync {
        Unchanged, // nothing to upload
        Updated,   // the dirty rectangle was uploaded
        Recreated, // the size changed: a new texture was made and the whole image uploaded
    };

    TextureMirror() = default;
    ~TextureMirror() { release(); }

    TextureMirror(const TextureMirror&) = delete;
    TextureMirror& operator=(const TextureMirror&) = delete;

    void init(SDL_Renderer* renderer) noexcept { renderer_ = renderer; }

    // Must run before the renderer is destroyed. Safe to call repeatedly.
    void release() noexcept;

    // Brings the texture in line with `image`. `dirty` is the part of the image that changed since the last
    // call (it is clipped to the image; empty means nothing). When the size differs from the texture's, the
    // texture is recreated and the whole image is uploaded regardless of `dirty`. Does nothing without a
    // renderer or for an empty image. A texture that could not be created is not retried until the image
    // changes size.
    Sync sync(const Image& image, const IRect& dirty);

    // Null until the first successful sync, and after a texture could not be created.
    SDL_Texture* texture() const noexcept { return texture_; }
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }

    // Smooth or nearest-neighbour sampling. Cheap to call every frame; only a change reaches the renderer.
    void setNearest(bool nearest) noexcept;

private:
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool nearest_ = true;
};

} // namespace trace
