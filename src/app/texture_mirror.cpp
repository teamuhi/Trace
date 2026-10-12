#include "app/texture_mirror.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>

namespace trace {

void TextureMirror::release() noexcept {
    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    width_ = 0;
    height_ = 0;
}

void TextureMirror::setNearest(bool nearest) noexcept {
    if (!texture_ || nearest == nearest_)
        return;
    SDL_SetTextureScaleMode(texture_, nearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
    nearest_ = nearest;
}

TextureMirror::Sync TextureMirror::sync(const Image& image, const IRect& dirty) {
    if (!renderer_ || image.empty())
        return Sync::Unchanged;

    IRect upload = dirty;
    bool recreated = false;
    if (image.width() != width_ || image.height() != height_) {
        if (texture_) {
            SDL_DestroyTexture(texture_);
            texture_ = nullptr;
        }
        width_ = image.width();
        height_ = image.height();
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width_, height_);
        if (texture_) {
            SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
            nearest_ = true;
            SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
        } else {
            SDL_Log("SDL_CreateTexture(%dx%d) failed: %s", width_, height_, SDL_GetError());
        }
        upload = IRect{0, 0, width_, height_};
        recreated = true;
    } else {
        upload.x0 = std::max(upload.x0, 0);
        upload.y0 = std::max(upload.y0, 0);
        upload.x1 = std::min(upload.x1, width_);
        upload.y1 = std::min(upload.y1, height_);
    }

    if (upload.empty() || !texture_)
        return recreated ? Sync::Recreated : Sync::Unchanged;

    const SDL_Rect rect{upload.x0, upload.y0, upload.width(), upload.height()};
    const std::size_t offset =
        static_cast<std::size_t>(upload.y0) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(upload.x0);
    const int pitch = width_ * static_cast<int>(sizeof(Rgba8));
    if (!SDL_UpdateTexture(texture_, &rect, image.pixels().data() + offset, pitch))
        SDL_Log("SDL_UpdateTexture failed: %s", SDL_GetError());
    return recreated ? Sync::Recreated : Sync::Updated;
}

} // namespace trace
