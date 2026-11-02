#include "app/board_export_dialog.hpp"

#include <imgui.h>

#include <algorithm>
#include <string>

#include "app/modal_ui.hpp"
#include "core/document.hpp"
#include "core/i18n.hpp"
#include "core/raster.hpp"

namespace trace {

namespace {

std::string exportTitle() {
    return std::string(tr(Str::BoardExpTitle)) + "###BoardExport";
}

// The drawn area of the visible layers of the active frame.
IRect drawnArea(const Document& doc) {
    IRect bounds;
    for (int layer = 0; layer < doc.layerCount(); ++layer) {
        const LayerInfo& info = doc.layer(layer);
        if (info.visible && info.opacity > 0)
            bounds = unionOf(bounds, opaqueBounds(doc.cel(layer, doc.activeFrame())));
    }
    return bounds;
}

} // namespace

void BoardExportDialog::open(const Document& doc, const IRect& view) {
    if (busy())
        return;
    content_ = drawnArea(doc);
    view_ = view;
    selection_ = doc.selection().active() ? doc.selection().rect() : IRect{};
    BoardExportOptions& o = options_;
    // A region that has nothing to show falls back to one that does.
    if (o.region == BoardRegion::Content && content_.empty())
        o.region = BoardRegion::View;
    if (o.region == BoardRegion::Selection && selection_.empty())
        o.region = content_.empty() ? BoardRegion::View : BoardRegion::Content;
    if (o.region == BoardRegion::Custom && o.customW < 1)
        o.customW = 1;
    open_ = true;
    openRequested_ = true;
}

IRect BoardExportDialog::region(const Document& doc) const {
    const BoardExportOptions& o = options_;
    switch (o.region) {
    case BoardRegion::Content:
        return content_;
    case BoardRegion::View:
        return view_;
    case BoardRegion::Board:
        return IRect{0, 0, doc.image().width(), doc.image().height()};
    case BoardRegion::Selection:
        return selection_;
    case BoardRegion::Custom: {
        // Typed in world coordinates; the board's origin turns them into image pixels.
        const int x = o.customX - doc.originX();
        const int y = o.customY - doc.originY();
        return IRect{x, y, x + std::max(o.customW, 0), y + std::max(o.customH, 0)};
    }
    }
    return {};
}

std::optional<BoardExportRun> BoardExportDialog::draw(const Document& doc) {
    if (!open_)
        return std::nullopt;

    const std::string title = exportTitle();
    if (openRequested_) {
        ImGui::OpenPopup(title.c_str());
        openRequested_ = false;
    }
    bool close = false;
    if (!modal::begin(title.c_str(), 30.0f, &close)) {
        open_ = false;
        return std::nullopt;
    }
    if (close) {
        ImGui::CloseCurrentPopup();
        open_ = false;
        modal::end();
        return std::nullopt;
    }

    BoardExportOptions& o = options_;
    modal::Form form({tr(Str::BoardExpArea), tr(Str::BoardExpCorner), tr(Str::BoardExpBorder), tr(Str::BoardExpPadLeft),
                      tr(Str::BoardExpPadRight), tr(Str::BoardExpSize), tr(Str::BoardExpBackground), tr(Str::BoardExpFormat),
                      tr(Str::BoardExpSmooth), tr(Str::BoardExpPadSame)});

    // --- area ---
    form.row(tr(Str::BoardExpArea));
    unsigned disabled = 0;
    if (content_.empty())
        disabled |= 1u << static_cast<int>(BoardRegion::Content);
    if (selection_.empty())
        disabled |= 1u << static_cast<int>(BoardRegion::Selection);
    const int regionPick = modal::choices("##boardArea", static_cast<int>(o.region),
                                      {tr(Str::BoardExpAreaDrawn), tr(Str::BoardExpAreaView), tr(Str::BoardExpAreaBoard),
                                       tr(Str::BoardExpAreaSelection), tr(Str::BoardExpAreaCustom)},
                                      disabled);
    if (regionPick >= 0) {
        o.region = static_cast<BoardRegion>(regionPick);
        if (o.region == BoardRegion::Custom) { // start from the area the user sees most likely wants
            const IRect seed = !content_.empty() ? content_ : view_;
            o.customX = seed.x0 + doc.originX();
            o.customY = seed.y0 + doc.originY();
            o.customW = std::max(seed.width(), 1);
            o.customH = std::max(seed.height(), 1);
        }
    }
    if (o.region == BoardRegion::Custom) {
        constexpr int kLimit = 1'000'000;
        form.row(tr(Str::BoardExpCorner));
        float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        modal::numberField("##boardX", o.customX, -kLimit, kLimit, "", 1, 100, half);
        ImGui::SameLine();
        modal::numberField("##boardY", o.customY, -kLimit, kLimit, "", 1, 100, half);
        form.row(tr(Str::BoardExpSize));
        half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        modal::numberField("##boardW", o.customW, 1, kLimit, "", 1, 100, half);
        ImGui::SameLine();
        modal::numberField("##boardH", o.customH, 1, kLimit, "", 1, 100, half);
    }
    if (!content_.empty()) {
        const std::string hint = trf(Str::BoardExpDrawnArea,
                                     {std::to_string(content_.x0 + doc.originX()), std::to_string(content_.y0 + doc.originY()),
                                      std::to_string(content_.width()), std::to_string(content_.height())});
        modal::paragraph(hint.c_str(), true);
    }

    // --- border ---
    ImGui::Spacing();
    modal::heading(tr(Str::BoardExpBorder));
    form.row(tr(Str::BoardExpPadSame));
    modal::checkbox("##boardPadSame", o.uniformPad);
    constexpr int kPad = BoardExportOptions::kMaxPad;
    if (o.uniformPad) {
        form.row(tr(Str::BoardExpBorder));
        modal::numberField("##boardPad", o.padAll, 0, kPad, "px", 1, 50);
    } else {
        form.row(tr(Str::BoardExpPadLeft));
        modal::numberField("##boardPadL", o.padLeft, 0, kPad, "px", 1, 50);
        form.row(tr(Str::BoardExpPadTop));
        modal::numberField("##boardPadT", o.padTop, 0, kPad, "px", 1, 50);
        form.row(tr(Str::BoardExpPadRight));
        modal::numberField("##boardPadR", o.padRight, 0, kPad, "px", 1, 50);
        form.row(tr(Str::BoardExpPadBottom));
        modal::numberField("##boardPadB", o.padBottom, 0, kPad, "px", 1, 50);
    }

    // --- size ---
    ImGui::Spacing();
    modal::heading(tr(Str::BoardExpSize));
    form.row(tr(Str::BoardExpSize));
    const int mode = modal::choices("##boardSize", static_cast<int>(o.size),
                                    {tr(Str::BoardExpScale), tr(Str::BoardExpWidth), tr(Str::BoardExpHeight),
                                     tr(Str::BoardExpLongEdge)});
    if (mode >= 0)
        o.size = static_cast<BoardSizeMode>(mode);
    form.row(nullptr);
    if (o.size == BoardSizeMode::Scale)
        modal::numberField("##boardScale", o.scalePct, BoardExportOptions::kMinScalePct, BoardExportOptions::kMaxScalePct, "%", 5,
                           25);
    else
        modal::numberField("##boardTarget", o.target, 1, BoardExportOptions::kMaxTarget, "px", 1, 100);
    form.row(tr(Str::BoardExpSmooth));
    modal::checkbox("##boardSmooth", o.smooth);

    // --- background and file ---
    ImGui::Spacing();
    form.row(tr(Str::BoardExpBackground));
    const int background = modal::choices("##boardBg", static_cast<int>(o.background),
                                          {tr(Str::BoardExpBgPaper), tr(Str::WordWhite), tr(Str::WordBlack),
                                           tr(Str::BoardExpBgTransparent)});
    if (background >= 0)
        o.background = static_cast<BoardBackground>(background);
    form.row(tr(Str::BoardExpFormat));
    const int format = modal::choices("##boardFormat", static_cast<int>(o.format), {"PNG", "JPEG"});
    if (format >= 0)
        o.format = static_cast<BoardExportFormat>(format);

    // --- result ---
    ImGui::Spacing();
    const IRect area = region(doc);
    const BoardExportPlan plan = planBoardExport(area, o);
    if (plan.ok()) {
        const std::string text = trf(Str::BoardExpResult, {std::to_string(plan.width), std::to_string(plan.height),
                                                           std::to_string(plan.source.width()),
                                                           std::to_string(plan.source.height())});
        modal::paragraph(text.c_str());
        if (o.format == BoardExportFormat::Jpeg && o.background == BoardBackground::Transparent)
            modal::notice(tr(Str::BoardExpJpegAlpha), true);
    } else if (plan.problem == BoardExportPlan::Problem::Empty) {
        modal::notice(tr(Str::BoardExpEmpty), true);
    } else {
        modal::notice(tr(Str::BoardExpTooLarge), true);
    }

    const int pressed = modal::footer({{tr(Str::WordCancel), modal::ButtonKind::Secondary, true, false, true},
                                       {tr(Str::ActionExport), modal::ButtonKind::Primary, plan.ok(), true}});
    std::optional<BoardExportRun> result;
    if (pressed >= 0) {
        ImGui::CloseCurrentPopup();
        open_ = false;
        if (pressed == 1)
            result = BoardExportRun{o, plan};
    }
    modal::end();
    return result;
}

} // namespace trace
