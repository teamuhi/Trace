#include "app/image_dialogs.hpp"

#include <imgui.h>

#include <algorithm>
#include <string>

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
constexpr ImGuiWindowFlags kModalFlags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
constexpr int kMinPercent = 1;
constexpr int kMaxPercent = 10000;

void centerNextWindow() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}

// A number field that reports whether the user changed it, with the value kept inside [lo, hi].
bool inputInt(const char* label, int& value, int lo, int hi) {
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
    const bool edited = ImGui::InputInt(label, &value, 1, 100);
    value = std::clamp(value, lo, hi);
    return edited;
}

ImVec4 toImVec4(Rgba8 c) {
    return ImVec4(static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f,
                  static_cast<float>(c.b) / 255.0f, static_cast<float>(c.a) / 255.0f);
}

} // namespace

void ImageDialogs::openResize(const Document& doc) {
    if (busy() || doc.image().empty())
        return;
    sourceWidth_ = doc.image().width();
    sourceHeight_ = doc.image().height();
    width_ = sourceWidth_;
    height_ = sourceHeight_;
    percentX_ = 100;
    percentY_ = 100;
    kind_ = Kind::Resize;
    openRequested_ = true;
}

void ImageDialogs::openCanvasSize(const Document& doc) {
    if (busy() || doc.image().empty())
        return;
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
    centerNextWindow();
    if (!ImGui::BeginPopupModal(resizeTitle().c_str(), nullptr, kModalFlags)) {
        kind_ = Kind::None;
        return;
    }

    ImGui::TextUnformatted(
        trf(Str::DialogCurrentSize, {std::to_string(sourceWidth_), std::to_string(sourceHeight_)}).c_str());
    ImGui::Spacing();

    ImGui::RadioButton(tr(Str::DialogPercent), &byPercent_, 1);
    ImGui::SameLine();
    ImGui::RadioButton(tr(Str::DialogPixels), &byPercent_, 0);

    if (byPercent_) {
        if (inputInt(tr(Str::DialogHorizontalPercent), percentX_, kMinPercent, kMaxPercent) && keepAspect_)
            percentY_ = percentX_;
        if (inputInt(tr(Str::DialogVerticalPercent), percentY_, kMinPercent, kMaxPercent) && keepAspect_)
            percentX_ = percentY_;
    } else {
        if (inputInt(tr(Str::WordWidthPx), width_, 1, kMaxImageSide) && keepAspect_) {
            height_ = scaledSide(sourceHeight_, static_cast<double>(width_) / static_cast<double>(sourceWidth_));
        }
        if (inputInt(tr(Str::WordHeightPx), height_, 1, kMaxImageSide) && keepAspect_) {
            width_ = scaledSide(sourceWidth_, static_cast<double>(height_) / static_cast<double>(sourceHeight_));
        }
    }
    ImGui::Checkbox(tr(Str::DialogKeepAspect), &keepAspect_);

    ImGui::Spacing();
    ImGui::TextUnformatted(tr(Str::DialogSmoothing));
    ImGui::RadioButton(tr(Str::DialogSharp), &filter_, static_cast<int>(ResizeFilter::Nearest));
    ImGui::RadioButton(tr(Str::DialogSmooth), &filter_, static_cast<int>(ResizeFilter::Bilinear));

    const int targetW = byPercent_ ? scaledSide(sourceWidth_, percentX_ / 100.0) : width_;
    const int targetH = byPercent_ ? scaledSide(sourceHeight_, percentY_ / 100.0) : height_;
    ImGui::Spacing();
    ImGui::TextUnformatted(trf(Str::DialogNewSize, {std::to_string(targetW), std::to_string(targetH)}).c_str());
    if (targetW == kMaxImageSide || targetH == kMaxImageSide)
        ImGui::TextDisabled("%s", trf(Str::DialogMaxSide, {std::to_string(kMaxImageSide)}).c_str());
    ImGui::Spacing();

    const bool changes = targetW != sourceWidth_ || targetH != sourceHeight_;
    bool apply = false;
    bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    ImGui::BeginDisabled(!changes);
    if (ImGui::Button(tr(Str::DialogResizeButton)))
        apply = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::WordCancel)))
        cancel = true;

    if (apply || cancel) {
        ImGui::CloseCurrentPopup();
        kind_ = Kind::None;
        if (apply) {
            const auto result = doc.resizeImage(targetW, targetH, static_cast<ResizeFilter>(filter_));
            if (result == Document::TransformResult::NoMemory)
                outcome = Outcome{tr(Str::ErrImageMemory), true};
            else if (result == Document::TransformResult::Applied)
                outcome = Outcome{trf(Str::ToastResized, {std::to_string(targetW), std::to_string(targetH)}), false};
        }
    }
    ImGui::EndPopup();
}

void ImageDialogs::drawCanvasSize(Document& doc, Rgba8 secondary, std::optional<Outcome>& outcome) {
    if (openRequested_) {
        ImGui::OpenPopup(canvasTitle().c_str());
        openRequested_ = false;
    }
    centerNextWindow();
    if (!ImGui::BeginPopupModal(canvasTitle().c_str(), nullptr, kModalFlags)) {
        kind_ = Kind::None;
        return;
    }

    ImGui::TextUnformatted(
        trf(Str::DialogCurrentSize, {std::to_string(sourceWidth_), std::to_string(sourceHeight_)}).c_str());
    ImGui::Spacing();
    inputInt(tr(Str::WordWidthPx), width_, 1, kMaxImageSide);
    inputInt(tr(Str::WordHeightPx), height_, 1, kMaxImageSide);

    ImGui::Spacing();
    ImGui::TextUnformatted(tr(Str::DialogAnchor));
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            if (column > 0)
                ImGui::SameLine();
            ImGui::PushID(row * 3 + column);
            ImGui::RadioButton("##anchor", &anchor_, row * 3 + column);
            ImGui::PopID();
        }
    }
    ImGui::TextDisabled("%s", tr(Str::DialogAnchorHint));

    ImGui::Spacing();
    ImGui::TextUnformatted(tr(Str::DialogNewArea));
    int transparent = fillTransparent_ ? 1 : 0;
    ImGui::RadioButton(tr(Str::DialogFillSecondary), &transparent, 0);
    ImGui::SameLine();
    ImGui::ColorButton("##fill", toImVec4(secondary),
                       ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_AlphaPreview, ImVec2(0.0f, 0.0f));
    ImGui::RadioButton(tr(Str::DialogFillTransparent), &transparent, 1);
    fillTransparent_ = transparent != 0;

    const bool cuts = width_ < sourceWidth_ || height_ < sourceHeight_;
    if (cuts)
        ImGui::TextDisabled("%s", tr(Str::DialogCutsOff));
    ImGui::Spacing();

    const bool changes = width_ != sourceWidth_ || height_ != sourceHeight_;
    bool apply = false;
    bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    ImGui::BeginDisabled(!changes);
    if (ImGui::Button(tr(Str::DialogApply)))
        apply = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::WordCancel)))
        cancel = true;

    if (apply || cancel) {
        ImGui::CloseCurrentPopup();
        kind_ = Kind::None;
        if (apply) {
            const Rgba8 fill = fillTransparent_ ? Rgba8{} : secondary;
            const auto result = doc.resizeCanvas(width_, height_, static_cast<Anchor>(anchor_), fill);
            if (result == Document::TransformResult::NoMemory)
                outcome = Outcome{tr(Str::ErrImageMemory), true};
            else if (result == Document::TransformResult::Applied)
                outcome = Outcome{trf(Str::ToastCanvasSize, {std::to_string(width_), std::to_string(height_)}), false};
        }
    }
    ImGui::EndPopup();
}

} // namespace trace
