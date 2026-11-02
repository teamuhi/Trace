#include "app/pixel_export_dialog.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "app/modal_ui.hpp"
#include "core/document.hpp"
#include "core/i18n.hpp"
#include "io/pixel_export.hpp"

namespace trace {

namespace {

// The whole numbers in `text`, in order (the sizes and limits inside the refusal messages).
std::vector<std::string> numbersIn(const std::string& text) {
    std::vector<std::string> numbers;
    for (std::size_t i = 0; i < text.size();) {
        if (std::isdigit(static_cast<unsigned char>(text[i])) == 0) {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])) != 0)
            ++end;
        numbers.push_back(text.substr(i, end - i));
        i = end;
    }
    return numbers;
}

std::string exportTitle() {
    return std::string(tr(Str::ActionExport)) + "###PixelExport";
}

// What the chosen options would produce, from the same functions that refuse to write what is too big.
ExportSize resultSize(const Document& doc, const PixelExportOptions& o) {
    switch (o.format) {
    case PixelExportFormat::Png:
        return pngExportSize(doc, o.scale);
    case PixelExportFormat::Gif:
        return gifExportSize(doc, o.firstFrame, o.lastFrame, o.scale);
    case PixelExportFormat::Sheet:
        break;
    }
    SheetOptions sheet = o.sheet;
    sheet.scale = o.scale;
    return sheetExportSize(doc, o.firstFrame, o.lastFrame, sheet);
}

} // namespace

std::string localizedExportMessage(const std::string& message) {
    struct Known {
        const char* start; // how the English message begins
        Str text;
        int numbers;       // how many numbers of the message the translation takes
    };
    static constexpr Known kKnown[] = {
        {"There is nothing to export.", Str::PixExpErrNothing, 0},
        {"The frames are empty.", Str::PixExpErrNothing, 0},
        {"There is not enough memory", Str::PixExpErrMemory, 0},
        {"The scale must be between", Str::PixExpErrScale, 1},
        {"The result would be", Str::PixExpErrSize, 3},
        {"The sheet would be", Str::PixExpErrSheetSize, 3},
        {"An animated GIF can hold", Str::PixExpErrGifFrames, 1},
        {"This animation is too large", Str::PixExpErrGifLarge, 0},
        {"That frame does not exist", Str::PixExpErrNoFrame, 0},
        {"There are no frames to put", Str::PixExpErrSheetFrames, 0},
        {"There are too many frames", Str::PixExpErrSheetFrames, 0},
        {"The sheet arrangement", Str::PixExpErrSheetLayout, 0},
        {"The export failed", Str::PixExpErrFailed, 0},
    };
    for (const Known& known : kKnown) {
        if (message.rfind(known.start, 0) != 0)
            continue;
        std::vector<std::string> numbers = numbersIn(message);
        if (known.text == Str::PixExpErrScale && numbers.size() >= 2)
            numbers.erase(numbers.begin()); // "between 1 and 32": only the 32 is a value
        if (static_cast<int>(numbers.size()) < known.numbers)
            break;
        switch (known.numbers) {
        case 0:
            return tr(known.text);
        case 1:
            return trf(known.text, {numbers[0]});
        default:
            return trf(known.text, {numbers[0], numbers[1], numbers.size() > 2 ? numbers[2] : std::string()});
        }
    }
    return message; // not one of ours (for example a file error already in the user's language)
}

void PixelExportDialog::open(const Document& doc) {
    if (busy())
        return;
    const int frames = std::max(1, doc.frameCount());
    PixelExportOptions& o = options_;
    if (frames < 2)
        o.format = PixelExportFormat::Png;
    o.firstFrame = 0;
    o.lastFrame = frames - 1;
    o.scale = std::clamp(o.scale, 1, PixelExportOptions::kMaxScale);
    o.alphaThreshold = std::clamp(o.alphaThreshold, 1, 255);
    o.sheet.padding = std::clamp(o.sheet.padding, 0, PixelExportOptions::kMaxPadding);
    o.sheet.columns = std::clamp(o.sheet.columns, 1, frames);
    open_ = true;
    openRequested_ = true;
}

std::optional<PixelExportOptions> PixelExportDialog::draw(const Document& doc) {
    if (!open_)
        return std::nullopt;

    const std::string title = exportTitle();
    if (openRequested_) {
        ImGui::OpenPopup(title.c_str());
        openRequested_ = false;
    }
    if (!modal::begin(title.c_str(), 26.0f)) {
        open_ = false;
        return std::nullopt;
    }

    PixelExportOptions& o = options_;
    const int frames = std::max(1, doc.frameCount());

    modal::Form form({tr(Str::PixExpFormat), tr(Str::PixExpScale), tr(Str::PixExpFrameFirst), tr(Str::PixExpFrameLast),
                      tr(Str::PixExpKeepAlpha), tr(Str::PixExpThreshold), tr(Str::PixExpLayout), tr(Str::PixExpColumns),
                      tr(Str::PixExpPadding)});
    form.row(tr(Str::PixExpFormat));
    const int format = modal::choices("##pixelExportFormat", static_cast<int>(o.format),
                                      {tr(Str::PixExpPng), tr(Str::PixExpGif), tr(Str::PixExpSheet)});
    if (format >= 0)
        o.format = static_cast<PixelExportFormat>(format);
    const bool animated = o.format != PixelExportFormat::Png;

    form.row(tr(Str::PixExpScale));
    modal::slider("##pixelExportScale", o.scale, 1, PixelExportOptions::kMaxScale, "x%d");

    if (animated && frames > 1) {
        int first = o.firstFrame + 1; // shown from 1
        int last = o.lastFrame + 1;
        form.row(tr(Str::PixExpFrameFirst));
        const bool firstEdited = modal::numberField("##pxFirst", first, 1, frames, "", 1, 10);
        form.row(tr(Str::PixExpFrameLast));
        const bool lastEdited = modal::numberField("##pxLast", last, 1, frames, "", 1, 10);
        if (firstEdited && first > last)
            last = first;
        else if (lastEdited && last < first)
            first = last;
        o.firstFrame = first - 1;
        o.lastFrame = last - 1;
    }

    if (o.format == PixelExportFormat::Gif) {
        form.row(tr(Str::PixExpKeepAlpha));
        modal::checkbox("##pixelExportKeepAlpha", o.keepTransparency);
        ImGui::SetItemTooltip("%s", tr(Str::PixExpKeepAlphaTip));
        form.row(tr(Str::PixExpThreshold));
        ImGui::BeginDisabled(!o.keepTransparency);
        modal::slider("##pixelExportThreshold", o.alphaThreshold, 1, 255, "%d");
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("%s", tr(Str::PixExpThresholdTip));
    }

    if (o.format == PixelExportFormat::Sheet) {
        form.row(tr(Str::PixExpLayout));
        const int mode = modal::choices("##pixelExportLayout", static_cast<int>(o.sheet.mode),
                                        {tr(Str::PixExpRow), tr(Str::PixExpColumn), tr(Str::PixExpGrid)});
        if (mode >= 0)
            o.sheet.mode = static_cast<SheetMode>(mode);
        if (o.sheet.mode == SheetMode::Grid) {
            form.row(tr(Str::PixExpColumns));
            modal::numberField("##pixelExportColumns", o.sheet.columns, 1, frames, "", 1, 4);
        }
        form.row(tr(Str::PixExpPadding));
        modal::slider("##pixelExportPadding", o.sheet.padding, 0, PixelExportOptions::kMaxPadding, "%d px");
    }

    // The size of the result, or why there will be none.
    ImGui::Spacing();
    const ExportSize size = resultSize(doc, o);
    if (size.ok) {
        const std::string w = std::to_string(size.width);
        const std::string h = std::to_string(size.height);
        std::string text;
        if (!animated)
            text = trf(Str::PixExpResultPng, {w, h});
        else if (size.frames == 1)
            text = trf(Str::PixExpResultOne, {w, h});
        else
            text = trf(Str::PixExpResultMany, {w, h, std::to_string(size.frames)});
        modal::paragraph(text.c_str());
    } else {
        modal::notice(localizedExportMessage(size.error).c_str(), true);
    }

    const int pressed = modal::footer({{tr(Str::WordCancel), modal::ButtonKind::Secondary, true, false, true},
                                       {tr(Str::ActionExport), modal::ButtonKind::Primary, size.ok, true}});
    std::optional<PixelExportOptions> result;
    if (pressed >= 0) {
        ImGui::CloseCurrentPopup();
        open_ = false;
        if (pressed == 1)
            result = o;
    }
    modal::end();
    return result;
}

} // namespace trace
