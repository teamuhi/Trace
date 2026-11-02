#include "app/image_dialogs.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "app/modal_ui.hpp"
#include "app/theme.hpp"
#include "core/dialog_logic.hpp"
#include "core/document.hpp"
#include "core/i18n.hpp"

namespace trace {

namespace {

// The visible title is translated; the part after ### keeps the popup's identity the same in every language.
std::string resizeTitle() {
    return std::string(tr(Str::DialogResizeTitle)) + "###ResizeImage";
}
std::string canvasTitle() {
    return std::string(tr(Str::DialogCanvasTitle)) + "###CanvasSize";
}
constexpr int kMinPercent = 1;
constexpr int kMaxPercent = 10000;

ImVec4 toImVec4(Rgba8 c) {
    return ImVec4(static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f,
                  static_cast<float>(c.b) / 255.0f, static_cast<float>(c.a) / 255.0f);
}

// The anchor grid and, beside it, the old picture on the new canvas. A cell is a square; the chosen one is filled with
// the accent and carries a small picture mark, the others a faint dot. Returns the (possibly new) anchor index.
int anchorPicker(int anchor, int oldW, int oldH, int newW, int newH, const ImVec4& fill) {
    const modal::Colors c = modal::colors();
    const ThemeTokens& t = tokens();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float e = ImGui::GetFontSize();
    const float cell = std::round(e * 2.1f);
    const float gap = std::round(e * 0.3f);
    const float side = 3.0f * cell + 2.0f * gap;
    const float round = std::round(e * 0.3f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float right = origin.x + modal::contentWidth();

    int result = anchor;
    ImGui::PushID("##anchorGrid");
    for (int i = 0; i < 9; ++i) {
        const Anchor a = static_cast<Anchor>(i);
        const float x = origin.x + static_cast<float>(anchorColumn(a)) * (cell + gap);
        const float y = origin.y + static_cast<float>(anchorRow(a)) * (cell + gap);
        const ImVec2 min(x, y);
        const ImVec2 max(x + cell, y + cell);
        ImGui::SetCursorScreenPos(min);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##cell", ImVec2(cell, cell)))
            result = i;
        const bool hovered = ImGui::IsItemHovered();
        const bool focused = ImGui::IsItemFocused();
        ImGui::PopID();

        if (i == anchor) {
            dl->AddRectFilled(min, max, ImGui::GetColorU32(t.accent), round);
            const float mark = std::round(cell * 0.17f);
            const ImVec2 mid(x + cell * 0.5f, y + cell * 0.5f);
            dl->AddRectFilled(ImVec2(mid.x - mark, mid.y - mark), ImVec2(mid.x + mark, mid.y + mark),
                              ImGui::GetColorU32(contrastText(t.accent)), std::round(mark * 0.35f));
        } else {
            ImVec4 plate = c.pill;
            if (hovered) {
                plate.x += (t.accent.x - plate.x) * 0.3f;
                plate.y += (t.accent.y - plate.y) * 0.3f;
                plate.z += (t.accent.z - plate.z) * 0.3f;
            }
            dl->AddRectFilled(min, max, ImGui::GetColorU32(plate), round);
            dl->AddRect(min, max, ImGui::GetColorU32(c.border), round);
            ImVec4 dot = t.text;
            dot.w *= hovered ? 0.8f : 0.4f;
            dl->AddCircleFilled(ImVec2(x + cell * 0.5f, y + cell * 0.5f), std::max(2.0f, std::round(cell * 0.07f)),
                                ImGui::GetColorU32(dot));
        }
        if (focused)
            dl->AddRect(ImVec2(min.x - 1.0f, min.y - 1.0f), ImVec2(max.x + 1.0f, max.y + 1.0f), ImGui::GetColorU32(t.accent),
                        round + 1.0f, 0, 2.0f);
    }
    ImGui::PopID();

    // The preview: the new canvas (its new area in the fill color) with what is left of the old picture on it.
    const float boxX = origin.x + side + std::round(e * 1.0f);
    const float boxW = std::min(right - boxX, e * 13.0f);
    if (boxW >= e * 3.0f) {
        const ImVec2 boxMin(boxX, origin.y);
        const ImVec2 boxMax(boxX + boxW, origin.y + side);
        dl->AddRectFilled(boxMin, boxMax, ImGui::GetColorU32(c.row), std::round(e * 0.4f));
        const float margin = std::round(e * 0.5f);
        const double scale = std::min(static_cast<double>(boxW - 2.0f * margin) / static_cast<double>(newW),
                                      static_cast<double>(side - 2.0f * margin) / static_cast<double>(newH));
        const float canvasW = std::max(3.0f, static_cast<float>(static_cast<double>(newW) * scale));
        const float canvasH = std::max(3.0f, static_cast<float>(static_cast<double>(newH) * scale));
        const ImVec2 cmin(std::round(boxMin.x + (boxW - canvasW) * 0.5f), std::round(boxMin.y + (side - canvasH) * 0.5f));
        const ImVec2 cmax(cmin.x + canvasW, cmin.y + canvasH);
        modal::drawSwatch(dl, cmin, cmax, fill);
        const KeptRect kept = keptPictureRect(oldW, oldH, newW, newH, static_cast<Anchor>(anchor));
        if (!kept.empty()) {
            const ImVec2 pmin(cmin.x + static_cast<float>(kept.x0 * scale), cmin.y + static_cast<float>(kept.y0 * scale));
            ImVec2 pmax(cmin.x + static_cast<float>(kept.x1 * scale), cmin.y + static_cast<float>(kept.y1 * scale));
            pmax.x = std::max(pmax.x, pmin.x + 2.0f);
            pmax.y = std::max(pmax.y, pmin.y + 2.0f);
            ImVec4 body = t.accent;
            body.w = 0.75f;
            dl->AddRectFilled(pmin, pmax, ImGui::GetColorU32(body));
            dl->AddRect(pmin, pmax, ImGui::GetColorU32(t.accent));
        }
    }

    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(right - origin.x, side));
    return result;
}

} // namespace

void ImageDialogs::openResize(const Document& doc, int maxSide) {
    if (busy() || doc.image().empty())
        return;
    maxSide_ = std::clamp(maxSide, 1, kMaxImageSide);
    sourceWidth_ = doc.image().width();
    sourceHeight_ = doc.image().height();
    width_ = sourceWidth_;
    height_ = sourceHeight_;
    percentX_ = 100;
    percentY_ = 100;
    kind_ = Kind::Resize;
    openRequested_ = true;
}

void ImageDialogs::openCanvasSize(const Document& doc, int maxSide) {
    if (busy() || doc.image().empty())
        return;
    maxSide_ = std::clamp(maxSide, 1, kMaxImageSide);
    sourceWidth_ = doc.image().width();
    sourceHeight_ = doc.image().height();
    width_ = sourceWidth_;
    height_ = sourceHeight_;
    kind_ = Kind::CanvasSize;
    openRequested_ = true;
}

std::optional<ImageDialogs::Outcome> ImageDialogs::draw(Document& doc, Rgba8 secondary) {
    std::optional<Outcome> outcome;
    switch (kind_) {
    case Kind::None:
        break;
    case Kind::Resize:
        drawResize(doc, outcome);
        break;
    case Kind::CanvasSize:
        drawCanvasSize(doc, secondary, outcome);
        break;
    }
    return outcome;
}

void ImageDialogs::drawResize(Document& doc, std::optional<Outcome>& outcome) {
    if (openRequested_) {
        ImGui::OpenPopup(resizeTitle().c_str());
        openRequested_ = false;
    }
    if (!modal::begin(resizeTitle().c_str(), 26.0f)) {
        kind_ = Kind::None;
        return;
    }

    const int mode = modal::choices("##mode", byPercent_ ? 0 : 1, {tr(Str::DialogPercent), tr(Str::DialogPixels)});
    if (mode >= 0)
        byPercent_ = mode == 0 ? 1 : 0;

    // One form for the whole dialog, so the labels line up whichever rows are showing.
    modal::Form form({tr(Str::DialogHorizontal), tr(Str::DialogVertical), tr(Str::DialogWidth), tr(Str::DialogHeight),
                      tr(Str::DialogNewSizeLabel)});
    if (byPercent_) {
        form.row(tr(Str::DialogHorizontal));
        if (modal::numberField("##percentX", percentX_, kMinPercent, kMaxPercent, "%") && keepAspect_)
            percentY_ = percentX_;
        form.row(tr(Str::DialogVertical));
        if (modal::numberField("##percentY", percentY_, kMinPercent, kMaxPercent, "%") && keepAspect_)
            percentX_ = percentY_;
    } else {
        form.row(tr(Str::DialogWidth));
        if (modal::numberField("##width", width_, 1, maxSide_, "px") && keepAspect_) {
            height_ = std::min(scaledSide(sourceHeight_, static_cast<double>(width_) / static_cast<double>(sourceWidth_)),
                               maxSide_);
        }
        form.row(tr(Str::DialogHeight));
        if (modal::numberField("##height", height_, 1, maxSide_, "px") && keepAspect_) {
            width_ = std::min(scaledSide(sourceWidth_, static_cast<double>(height_) / static_cast<double>(sourceHeight_)),
                              maxSide_);
        }
    }
    form.row(nullptr);
    modal::checkbox(tr(Str::DialogKeepAspect), keepAspect_);

    modal::heading(tr(Str::DialogSmoothing));
    if (modal::radioRow("##sharp", filter_ == static_cast<int>(ResizeFilter::Nearest), tr(Str::DialogSharp)))
        filter_ = static_cast<int>(ResizeFilter::Nearest);
    if (modal::radioRow("##smooth", filter_ == static_cast<int>(ResizeFilter::Bilinear), tr(Str::DialogSmooth)))
        filter_ = static_cast<int>(ResizeFilter::Bilinear);

    const int targetW = byPercent_ ? std::min(scaledSide(sourceWidth_, percentX_ / 100.0), maxSide_) : width_;
    const int targetH = byPercent_ ? std::min(scaledSide(sourceHeight_, percentY_ / 100.0), maxSide_) : height_;
    ImGui::Spacing();
    modal::Form result({tr(Str::DialogNewSizeLabel)});
    result.row(tr(Str::DialogNewSizeLabel));
    modal::valuePlate((sizeChangeText(sourceWidth_, sourceHeight_, targetW, targetH) + " px").c_str());
    if (targetW == maxSide_ || targetH == maxSide_)
        modal::paragraph(trf(Str::DialogMaxSide, {std::to_string(maxSide_)}).c_str(), true);

    const bool changes = targetW != sourceWidth_ || targetH != sourceHeight_;
    const int pressed = modal::footer({{tr(Str::WordCancel), modal::ButtonKind::Secondary, true, false, true},
                                       {tr(Str::DialogResizeButton), modal::ButtonKind::Primary, changes, true, false}});
    if (pressed >= 0) {
        ImGui::CloseCurrentPopup();
        kind_ = Kind::None;
        if (pressed == 1) {
            const auto applied = doc.resizeImage(targetW, targetH, static_cast<ResizeFilter>(filter_));
            if (applied == Document::TransformResult::NoMemory)
                outcome = Outcome{tr(Str::ErrImageMemory), true};
            else if (applied == Document::TransformResult::Applied)
                outcome = Outcome{trf(Str::ToastResized, {std::to_string(targetW), std::to_string(targetH)}), false};
        }
    }
    modal::end();
}

void ImageDialogs::drawCanvasSize(Document& doc, Rgba8 secondary, std::optional<Outcome>& outcome) {
    if (openRequested_) {
        ImGui::OpenPopup(canvasTitle().c_str());
        openRequested_ = false;
    }
    if (!modal::begin(canvasTitle().c_str(), 27.0f)) {
        kind_ = Kind::None;
        return;
    }

    modal::Form form({tr(Str::DialogWidth), tr(Str::DialogHeight), tr(Str::DialogNewSizeLabel)});
    form.row(tr(Str::DialogWidth));
    modal::numberField("##width", width_, 1, maxSide_, "px");
    form.row(tr(Str::DialogHeight));
    modal::numberField("##height", height_, 1, maxSide_, "px");
    form.row(tr(Str::DialogNewSizeLabel));
    modal::valuePlate((sizeChangeText(sourceWidth_, sourceHeight_, width_, height_) + " px").c_str());
    if (maxSide_ < kMaxImageSide && (width_ == maxSide_ || height_ == maxSide_))
        modal::paragraph(trf(Str::DialogMaxSide, {std::to_string(maxSide_)}).c_str(), true);

    modal::heading(tr(Str::DialogAnchor));
    const ImVec4 transparent(0.0f, 0.0f, 0.0f, 0.0f);
    const ImVec4 secondaryColor = toImVec4(secondary);
    anchor_ = anchorPicker(anchor_, sourceWidth_, sourceHeight_, width_, height_, fillTransparent_ ? transparent : secondaryColor);
    modal::paragraph(tr(Str::DialogAnchorHint), true);

    modal::heading(tr(Str::DialogNewArea));
    if (modal::radioRow("##fillTransparent", fillTransparent_, tr(Str::DialogFillTransparent), &transparent))
        fillTransparent_ = true;
    if (modal::radioRow("##fillSecondary", !fillTransparent_, tr(Str::DialogFillSecondary), &secondaryColor))
        fillTransparent_ = false;

    if (width_ < sourceWidth_ || height_ < sourceHeight_)
        modal::notice(tr(Str::DialogCutsOff), true);

    const bool changes = width_ != sourceWidth_ || height_ != sourceHeight_;
    const int pressed = modal::footer({{tr(Str::WordCancel), modal::ButtonKind::Secondary, true, false, true},
                                       {tr(Str::DialogApply), modal::ButtonKind::Primary, changes, true, false}});
    if (pressed >= 0) {
        ImGui::CloseCurrentPopup();
        kind_ = Kind::None;
        if (pressed == 1) {
            const Rgba8 fill = fillTransparent_ ? Rgba8{} : secondary;
            const auto result = doc.resizeCanvas(width_, height_, static_cast<Anchor>(anchor_), fill);
            if (result == Document::TransformResult::NoMemory)
                outcome = Outcome{tr(Str::ErrImageMemory), true};
            else if (result == Document::TransformResult::Applied)
                outcome = Outcome{trf(Str::ToastCanvasSize, {std::to_string(width_), std::to_string(height_)}), false};
        }
    }
    modal::end();
}

} // namespace trace
