#include "app/flow_export.hpp"

#include <SDL3/SDL.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

namespace trace {

namespace {

constexpr int kTilePx = 4096;   // the largest render target made; the renderer's own limit may be lower
constexpr int kMinTilePx = 64;

// SDL objects that must be given back whatever path leaves render().
struct RenderStateGuard {
    SDL_Renderer* renderer;
    SDL_Texture* target = nullptr;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    SDL_Texture* tile = nullptr;

    explicit RenderStateGuard(SDL_Renderer* r) : renderer(r) {
        target = SDL_GetRenderTarget(r);
        SDL_GetRenderScale(r, &scaleX, &scaleY);
        SDL_GetRenderDrawColorFloat(r, &color[0], &color[1], &color[2], &color[3]);
    }
    ~RenderStateGuard() {
        SDL_SetRenderTarget(renderer, target);
        SDL_SetRenderScale(renderer, scaleX, scaleY);
        SDL_SetRenderDrawColorFloat(renderer, color[0], color[1], color[2], color[3]);
        if (tile)
            SDL_DestroyTexture(tile);
    }
    RenderStateGuard(const RenderStateGuard&) = delete;
    RenderStateGuard& operator=(const RenderStateGuard&) = delete;
};

// Copies the w x h pixels of `surface` (any format) into `image` at (x, y), turning the premultiplied alpha that
// blending into a transparent target leaves behind into straight alpha.
bool copyTile(SDL_Surface* surface, Image& image, int x, int y, int w, int h) {
    SDL_Surface* rgba = surface;
    if (surface->format != SDL_PIXELFORMAT_RGBA32) {
        rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        if (!rgba)
            return false;
    }
    bool ok = rgba->w >= w && rgba->h >= h;
    for (int row = 0; ok && row < h; ++row) {
        const auto* src = static_cast<const std::uint8_t*>(rgba->pixels) + static_cast<std::size_t>(row) * static_cast<std::size_t>(rgba->pitch);
        Rgba8* dst = image.row(y + row).data() + x;
        std::memcpy(dst, src, static_cast<std::size_t>(w) * sizeof(Rgba8));
        for (int i = 0; i < w; ++i) {
            Rgba8& p = dst[i];
            if (p.a == 255)
                continue;
            if (p.a == 0) {
                p = Rgba8{};
                continue;
            }
            const unsigned a = p.a;
            const auto unmul = [a](std::uint8_t v) {
                return static_cast<std::uint8_t>(std::min(255u, (static_cast<unsigned>(v) * 255u + a / 2u) / a));
            };
            p.r = unmul(p.r);
            p.g = unmul(p.g);
            p.b = unmul(p.b);
        }
    }
    if (rgba != surface)
        SDL_DestroySurface(rgba);
    return ok;
}

} // namespace

int exportScale(int scale) noexcept {
    if (scale <= 1)
        return 1;
    return scale >= 4 ? 4 : 2;
}

std::optional<FlowExportPlan> planExport(const flow::Rect& content, int padding, int scale) {
    if (!std::isfinite(content.x) || !std::isfinite(content.y) || !std::isfinite(content.w) ||
        !std::isfinite(content.h) || content.w < 0.0f || content.h < 0.0f)
        return std::nullopt;
    const float pad = static_cast<float>(std::clamp(padding, 0, kExportMaxPadding));
    FlowExportPlan plan;
    plan.scale = static_cast<float>(exportScale(scale));
    plan.area = flow::Rect{content.x - pad, content.y - pad, content.w + 2.0f * pad, content.h + 2.0f * pad};
    // Checked in floating point first: a hostile board (coordinates up to +-1e6) must not overflow an int.
    const float w = std::ceil(plan.area.w * plan.scale);
    const float h = std::ceil(plan.area.h * plan.scale);
    const float limit = static_cast<float>(kExportMaxPixels);
    if (!(w <= limit) || !(h <= limit))
        return std::nullopt;
    plan.width = std::max(1, static_cast<int>(w));
    plan.height = std::max(1, static_cast<int>(h));
    return plan;
}

ImDrawList* OffscreenImage::begin(int width, int height, Rgba8 clear) {
    list_ = std::make_unique<ImDrawList>(ImGui::GetDrawListSharedData());
    list_->_ResetForNewFrame();
    list_->_FringeScale = 1.0f; // the image is in pixels, whatever the screen's scale is
    list_->PushTexture(ImGui::GetIO().Fonts->TexRef);
    list_->PushClipRect(ImVec2(0.0f, 0.0f), ImVec2(static_cast<float>(width), static_cast<float>(height)), false);
    width_ = width;
    height_ = height;
    clear_ = clear;
    return list_.get();
}

std::optional<Image> OffscreenImage::render(SDL_Renderer* renderer, std::string& error) {
    if (!list_)
        return std::nullopt;
    const std::unique_ptr<ImDrawList> list = std::move(list_); // freed on every path out of here
    if (!renderer || width_ <= 0 || height_ <= 0) {
        error = "no renderer";
        return std::nullopt;
    }

    const auto maxTexture = static_cast<int>(
        SDL_GetNumberProperty(SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 2048));
    const int tile = std::clamp(maxTexture, kMinTilePx, kTilePx);

    Image image;
    try {
        image = Image(width_, height_);
    } catch (const std::bad_alloc&) {
        error = "out of memory";
        return std::nullopt;
    }

    // The same recording is drawn once per tile: the vertices are shifted so the tile's corner is the origin, and
    // the backend subtracts DisplayPos from the clip rectangles to match.
    ImDrawData data;
    data.AddDrawList(list.get());
    data.Valid = true;
    data.FramebufferScale = ImVec2(1.0f, 1.0f);
    data.Textures = &ImGui::GetPlatformIO().Textures; // already current; passed so a pending update is never missed

    RenderStateGuard guard(renderer);
    const int tileW = std::min(tile, width_);
    const int tileH = std::min(tile, height_);
    guard.tile = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, tileW, tileH);
    if (!guard.tile) {
        error = SDL_GetError();
        return std::nullopt;
    }
    SDL_SetRenderTarget(renderer, guard.tile);
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);

    int shiftX = 0; // how far the recorded vertices have been moved so far
    int shiftY = 0;
    for (int ty = 0; ty < height_; ty += tile) {
        for (int tx = 0; tx < width_; tx += tile) {
            const int w = std::min(tile, width_ - tx);
            const int h = std::min(tile, height_ - ty);

            SDL_SetRenderDrawColor(renderer, clear_.r, clear_.g, clear_.b, clear_.a);
            SDL_RenderClear(renderer); // a clear replaces the pixels, so alpha 0 stays transparent

            const float dx = static_cast<float>(tx - shiftX);
            const float dy = static_cast<float>(ty - shiftY);
            if (dx != 0.0f || dy != 0.0f) {
                for (ImDrawVert& v : list->VtxBuffer) {
                    v.pos.x -= dx;
                    v.pos.y -= dy;
                }
                shiftX = tx;
                shiftY = ty;
            }
            data.DisplayPos = ImVec2(static_cast<float>(tx), static_cast<float>(ty));
            data.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
            ImGui_ImplSDLRenderer3_RenderDrawData(&data, renderer);

            const SDL_Rect area{0, 0, w, h};
            SDL_Surface* pixels = SDL_RenderReadPixels(renderer, &area); // flushes what was drawn
            if (!pixels) {
                error = SDL_GetError();
                return std::nullopt;
            }
            const bool copied = copyTile(pixels, image, tx, ty, w, h);
            SDL_DestroySurface(pixels);
            if (!copied) {
                error = SDL_GetError();
                return std::nullopt;
            }
        }
    }
    return image;
}

} // namespace trace
