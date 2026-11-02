#include "app/app_icon.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

#include "core/image.hpp"
#include "io/image_io.hpp"

extern const unsigned char kAppIconPngData[];
extern const unsigned long long kAppIconPngSize;

namespace trace {

namespace {

constexpr int kMinIconSide = 16;

SDL_Texture* g_texture = nullptr;
SDL_Renderer* g_renderer = nullptr;
bool g_textureFailed = false; // do not decode again every frame

std::optional<Image> decodeIcon() {
    const std::vector<unsigned char> bytes(kAppIconPngData, kAppIconPngData + static_cast<std::size_t>(kAppIconPngSize));
    LoadResult result = decodeImage(bytes, "app icon");
    if (!result.image || result.image->empty())
        return std::nullopt;
    return std::move(result.image);
}

// A surface of the decoded picture (RGBA bytes in memory order), owned by the caller.
SDL_Surface* surfaceOf(const Image& image) {
    SDL_Surface* surface = SDL_CreateSurface(image.width(), image.height(), SDL_PIXELFORMAT_RGBA32);
    if (!surface)
        return nullptr;
    const auto pixels = image.pixels();
    for (int y = 0; y < image.height(); ++y) {
        std::memcpy(static_cast<unsigned char*>(surface->pixels) + static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->pitch),
                    pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width()),
                    static_cast<std::size_t>(image.width()) * sizeof(Rgba8));
    }
    return surface;
}

} // namespace

bool setWindowIcon(SDL_Window* window) {
    if (!window)
        return false;
    const std::optional<Image> image = decodeIcon();
    if (!image)
        return false;
    SDL_Surface* base = surfaceOf(*image);
    if (!base)
        return false;
    // Halving keeps each smaller copy a plain average of the previous one, which stays sharp; a picture that is not
    // square or not a power of two still works (the sizes just follow the halving).
    SDL_Surface* previous = base;
    for (int side = std::min(image->width(), image->height()) / 2; side >= kMinIconSide; side /= 2) {
        SDL_Surface* smaller = SDL_ScaleSurface(previous, side, side, SDL_SCALEMODE_LINEAR);
        if (!smaller)
            break;
        SDL_AddSurfaceAlternateImage(base, smaller); // the base takes its own reference, so ours can go
        SDL_DestroySurface(smaller);
        previous = smaller; // still alive through the base; the next size is halved from it
    }
    const bool ok = SDL_SetWindowIcon(window, base);
    if (!ok)
        SDL_Log("SDL_SetWindowIcon failed: %s", SDL_GetError());
    SDL_DestroySurface(base);
    return ok;
}

void initAppIcon(SDL_Renderer* renderer) noexcept {
    g_renderer = renderer;
}

SDL_Texture* appIconTexture() {
    if (g_texture || g_textureFailed || !g_renderer)
        return g_texture;
    g_textureFailed = true;
    if (const std::optional<Image> image = decodeIcon()) {
        if (SDL_Surface* surface = surfaceOf(*image)) {
            g_texture = SDL_CreateTextureFromSurface(g_renderer, surface);
            SDL_DestroySurface(surface);
        }
    }
    if (g_texture)
        SDL_SetTextureScaleMode(g_texture, SDL_SCALEMODE_LINEAR);
    return g_texture;
}

void releaseAppIcon() noexcept {
    if (g_texture)
        SDL_DestroyTexture(g_texture);
    g_texture = nullptr;
    g_renderer = nullptr;
    g_textureFailed = false;
}

} // namespace trace
