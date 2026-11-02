#include "app/svg_icons.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <unordered_map>

#include "app/texture_mirror.hpp"
#include "core/svg_icon.hpp"

namespace trace {

namespace {

constexpr std::size_t kIconCount = static_cast<std::size_t>(Icon::Count) - 1;

// Textures by icon and pixel size. An icon that could not be rasterized is kept as an empty entry so it is not
// retried every frame.
class IconCache {
public:
    void init(SDL_Renderer* renderer) {
        release();
        renderer_ = renderer;
    }

    void release() noexcept {
        textures_.clear(); // each mirror destroys its texture
        renderer_ = nullptr;
    }

    SDL_Texture* texture(Icon icon, int px) {
        if (!renderer_)
            return nullptr;
        const std::uint32_t key = (static_cast<std::uint32_t>(icon) << 16) | static_cast<std::uint32_t>(px);
        auto [it, added] = textures_.try_emplace(key);
        if (added) {
            it->second.init(renderer_);
            const SvgIconSource& source = kSvgIconSources[static_cast<std::size_t>(icon) - 1];
            const Image image = rasterizeSvgCoverage(
                std::string_view(reinterpret_cast<const char*>(source.data), source.size), px);
            if (!image.empty()) {
                it->second.sync(image, IRect{0, 0, image.width(), image.height()});
                it->second.setNearest(false);
            }
        }
        return it->second.texture();
    }

private:
    SDL_Renderer* renderer_ = nullptr;
    std::unordered_map<std::uint32_t, TextureMirror> textures_;
};

IconCache& cache() {
    static IconCache instance;
    return instance;
}

} // namespace

void initSvgIcons(SDL_Renderer* renderer) {
    cache().init(renderer);
}

void releaseSvgIcons() noexcept {
    cache().release();
}

const char* iconName(Icon icon) noexcept {
    const auto index = static_cast<std::size_t>(icon);
    return index >= 1 && index <= kIconCount ? kSvgIconSources[index - 1].name : "";
}

void drawIcon(ImDrawList* dl, Icon icon, ImVec2 min, float size, ImU32 color, int quarterTurns) {
    const auto index = static_cast<std::size_t>(icon);
    if (!dl || index < 1 || index > kIconCount || !(size > 0.0f))
        return;

    // The framebuffer can be denser than ImGui's coordinates (a Retina display): rasterize at real pixels and snap
    // the corner to a real pixel too.
    const float scale = std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.x);
    const int px = std::max(1, static_cast<int>(std::lround(size * scale)));
    SDL_Texture* texture = cache().texture(icon, px);
    if (!texture)
        return;

    const ImVec2 a(std::round(min.x * scale) / scale, std::round(min.y * scale) / scale);
    const float side = static_cast<float>(px) / scale;
    const ImVec2 p[4] = {a, ImVec2(a.x + side, a.y), ImVec2(a.x + side, a.y + side), ImVec2(a.x, a.y + side)};
    const ImVec2 uv[4] = {ImVec2(0.0f, 0.0f), ImVec2(1.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec2(0.0f, 1.0f)};
    const int turns = ((quarterTurns % 4) + 4) % 4;
    const ImTextureID id = static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(texture));
    if (turns == 0) {
        dl->AddImage(id, p[0], p[2], uv[0], uv[2], color);
        return;
    }
    // Clockwise: each corner shows the texture corner that is `turns` places before it.
    dl->AddImageQuad(id, p[0], p[1], p[2], p[3], uv[(0 + 4 - turns) % 4], uv[(1 + 4 - turns) % 4],
                     uv[(2 + 4 - turns) % 4], uv[(3 + 4 - turns) % 4], color);
}

} // namespace trace
