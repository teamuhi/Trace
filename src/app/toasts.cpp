#include "app/toasts.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include "app/svg_icons.hpp"
#include "app/theme.hpp"

namespace trace {

namespace {

constexpr double kInfoSeconds = 3.0;
constexpr double kErrorSeconds = 6.0;
constexpr double kFadeSeconds = 0.4;
constexpr std::size_t kMaxTextLength = 300; // a runaway message must not fill the screen

ImU32 withAlpha(const ImVec4& c, float alpha) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

} // namespace

void Toasts::push(std::string text, Kind kind) {
    if (text.empty())
        return;
    if (text.size() > kMaxTextLength)
        text.resize(kMaxTextLength);
    // The same message again restarts its timer instead of stacking.
    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(),
                                 [&](const Toast& t) { return t.text == text && t.kind == kind; }),
                  toasts_.end());
    toasts_.push_back(Toast{std::move(text), kind, -1.0});
    if (toasts_.size() > kMaxVisible)
        toasts_.erase(toasts_.begin(), toasts_.end() - static_cast<std::ptrdiff_t>(kMaxVisible));
}

void Toasts::setCanvasRect(ImVec2 pos, ImVec2 size) {
    canvasPos_ = pos;
    canvasSize_ = size;
    canvasFrame_ = ImGui::GetFrameCount();
}

void Toasts::draw() {
    if (toasts_.empty())
        return;
    const double now = ImGui::GetTime();
    for (Toast& t : toasts_) {
        if (t.expires < 0.0)
            t.expires = now + (t.kind == Kind::Error ? kErrorSeconds : kInfoSeconds);
    }
    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [now](const Toast& t) { return now >= t.expires; }),
                  toasts_.end());
    if (toasts_.empty())
        return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList(viewport);
    const ThemeTokens& tk = tokens();
    const float unit = ImGui::GetFontSize();
    const ImVec2 pad(unit * 0.8f, unit * 0.55f);
    const float barWidth = unit * 0.3f;
    const float gap = unit * 0.5f;

    // The canvas window when one was reported this frame and has room, else the whole main window.
    ImVec2 areaPos = viewport->WorkPos;
    ImVec2 areaSize = viewport->WorkSize;
    if (canvasFrame_ == ImGui::GetFrameCount() && canvasSize_.x > unit * 4.0f && canvasSize_.y > unit * 4.0f) {
        areaPos = canvasPos_;
        areaSize = canvasSize_;
    }
    const float centerX = areaPos.x + areaSize.x * 0.5f;
    const float areaBottom = areaPos.y + areaSize.y;
    const float wrap = std::max(unit * 10.0f, std::min(areaSize.x - unit * 2.0f, viewport->WorkSize.x * 0.4f));

    // Below the top ruler (1.5 em tall) with a small margin; newest on top, older ones stacked beneath it.
    float top = areaPos.y + unit * 2.2f;

    for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it) {
        const Toast& t = *it;
        const float remaining = static_cast<float>(t.expires - now);
        const float alpha = std::clamp(remaining / static_cast<float>(kFadeSeconds), 0.0f, 1.0f);

        const ImVec2 textSize = ImGui::CalcTextSize(t.text.c_str(), nullptr, false, wrap);
        const float iconWidth = std::round(unit) + gap; // the icon and the space behind it
        const ImVec2 size(textSize.x + pad.x * 2.0f + barWidth + iconWidth, textSize.y + pad.y * 2.0f);
        const ImVec2 min(std::round(centerX - size.x * 0.5f), top);
        const ImVec2 max(min.x + size.x, top + size.y);

        const ImVec4 accent = t.kind == Kind::Error ? tk.warning : tk.accent;
        const ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
        const float rounding = ImGui::GetStyle().PopupRounding;
        dl->AddRectFilled(min, max, withAlpha(background, 0.96f * alpha), rounding);
        dl->AddRect(min, max, withAlpha(tk.canvasBorder, alpha), rounding);
        dl->AddRectFilled(min, ImVec2(min.x + barWidth, max.y), withAlpha(accent, alpha), rounding,
                          ImDrawFlags_RoundCornersLeft);
        drawIcon(dl, t.kind == Kind::Error ? Icon::Error : Icon::Info,
                 ImVec2(min.x + barWidth + pad.x, min.y + pad.y + std::round((ImGui::GetTextLineHeight() - unit) * 0.5f)),
                 std::round(unit), withAlpha(accent, alpha));
        dl->AddText(nullptr, 0.0f, ImVec2(min.x + barWidth + pad.x + iconWidth, min.y + pad.y), withAlpha(tk.text, alpha),
                    t.text.c_str(), nullptr, wrap);

        top = max.y + gap;
        if (top > areaBottom)
            break; // no room left below; the rest are still timed and will appear as space frees
    }
}

} // namespace trace
