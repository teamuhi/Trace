#include "app/clipboard.hpp"

#include "core/i18n.hpp"

#include <SDL3/SDL.h>

#include <cstring>
#include <new>
#include <vector>

namespace trace {

namespace {

constexpr const char kPng[] = "image/png";
constexpr const char kBmp[] = "image/bmp";

// Encoded once and never modified afterwards: SDL may ask for the data from another thread.
struct ClipData {
    std::vector<unsigned char> png;
    std::vector<unsigned char> bmp;
};

const void* SDLCALL provide(void* userdata, const char* mime, size_t* size) {
    const ClipData* data = static_cast<const ClipData*>(userdata);
    const std::vector<unsigned char>* bytes = nullptr;
    if (mime && std::strcmp(mime, kPng) == 0)
        bytes = &data->png;
    else if (mime && std::strcmp(mime, kBmp) == 0)
        bytes = &data->bmp;
    if (!bytes || bytes->empty()) {
        *size = 0;
        return nullptr;
    }
    *size = bytes->size();
    return bytes->data();
}

void SDLCALL release(void* userdata) {
    delete static_cast<ClipData*>(userdata);
}

LoadResult failure(const char* message) {
    LoadResult r;
    r.error = message;
    return r;
}

} // namespace

bool setClipboardImage(const Image& image) {
    if (image.empty())
        return false;
    auto* data = new (std::nothrow) ClipData;
    if (!data)
        return false;
    try {
        data->png = encodeImage(image, ImageFormat::Png);
        data->bmp = encodeImage(image, ImageFormat::Bmp);
    } catch (const std::bad_alloc&) {
        delete data;
        return false;
    }
    if (data->png.empty() && data->bmp.empty()) {
        delete data;
        return false;
    }

    // From here SDL owns `data` and calls `release` when the clipboard changes, even if this call fails.
    static const char* const mimes[] = {kPng, kBmp};
    if (!SDL_SetClipboardData(provide, release, data, mimes, 2)) {
        SDL_Log("SDL_SetClipboardData failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool clipboardHasImage() {
    return SDL_HasClipboardData(kPng) || SDL_HasClipboardData(kBmp);
}

LoadResult getClipboardImage() {
    for (const char* mime : {kPng, kBmp}) {
        if (!SDL_HasClipboardData(mime))
            continue;
        size_t size = 0;
        void* raw = SDL_GetClipboardData(mime, &size);
        if (!raw)
            continue;
        if (size > kMaxImageFileBytes) {
            SDL_free(raw);
            return failure(tr(Str::ClipboardTooLarge));
        }
        try {
            const unsigned char* bytes = static_cast<const unsigned char*>(raw);
            std::vector<unsigned char> copy(bytes, bytes + size);
            SDL_free(raw);
            LoadResult result = decodeImage(copy);
            if (result.image)
                return result;
        } catch (const std::bad_alloc&) {
            return failure(tr(Str::ClipboardMemory));
        }
    }
    return failure(tr(Str::ClipboardNoImage));
}

} // namespace trace
