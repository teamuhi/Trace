#include "app/palette_panel.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <new>
#include <span>
#include <string>
#include <vector>

#include "app/icon_widgets.hpp"
#include "app/modal_ui.hpp"
#include "app/settings.hpp"
#include "app/theme.hpp"
#include "app/toasts.hpp"
#include "core/document.hpp"
#include "core/i18n.hpp"
#include "io/file_write.hpp"
#include "io/path_utf8.hpp"
#include "tools/toolbox.hpp"

namespace trace {

namespace {

constexpr const char* kDragType = "TRACE_SWATCH";
constexpr const char* kSortPopup = "##paletteSort";

// The visible title is translated; the part after ### keeps the popup's identity the same in every language.
std::string replaceTitle() {
    return std::string(tr(Str::PaletteReplaceTitle)) + "###PaletteReplace";
}

struct ScopeItem {
    ReplaceScope scope;
    Str label;
};
constexpr std::array<ScopeItem, 3> kScopes = {{{ReplaceScope::Cel, Str::PaletteScopeCel},
                                               {ReplaceScope::Layer, Str::PaletteScopeLayer},
                                               {ReplaceScope::All, Str::PaletteScopeAll}}};

ImU32 colorOf(ImVec4 c, float alpha = 1.0f) {
    c.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(c);
}

ImVec4 toImVec4(Rgba8 c) {
    return ImVec4(static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f,
                  static_cast<float>(c.b) / 255.0f, static_cast<float>(c.a) / 255.0f);
}

std::uint8_t quantize(float v) {
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(v * 255.0f)), 0, 255));
}

Rgba8 fromFloats(const float rgba[4]) {
    return Rgba8{quantize(rgba[0]), quantize(rgba[1]), quantize(rgba[2]), quantize(rgba[3])};
}

void toFloats(Rgba8 c, float out[4]) {
    const ImVec4 v = toImVec4(c);
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
    out[3] = v.w;
}

// "#RRGGBB", with the alpha appended when the color is not opaque.
std::string hexText(Rgba8 c) {
    char text[16];
    if (c.a == 255)
        std::snprintf(text, sizeof(text), "#%02X%02X%02X", c.r, c.g, c.b);
    else
        std::snprintf(text, sizeof(text), "#%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
    return text;
}

std::string lowerExtension(const std::filesystem::path& path) {
    std::string extension = utf8FromPath(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

// At most kMaxPaletteFileBytes of the file: a larger file is cut there, never read whole.
bool readCapped(const std::filesystem::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    out.resize(kMaxPaletteFileBytes);
    in.read(out.data(), static_cast<std::streamsize>(out.size()));
    if (in.bad())
        return false;
    out.resize(static_cast<std::size_t>(std::max<std::streamsize>(in.gcount(), 0)));
    return true;
}

// Buttons in a row that wraps at the panel's right edge. Each is as wide as its label, so long translations
// take more rows instead of being cut.
class ButtonFlow {
public:
    ButtonFlow() : available_(ImGui::GetContentRegionAvail().x), gap_(ImGui::GetStyle().ItemSpacing.x) {}

    // `icon` goes before the label; Icon::None makes a plain text button.
    bool button(Icon icon, const char* label, Str tip, bool enabled = true) {
        const float width = std::min(icon == Icon::None ? ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f
                                                        : iconTextButtonWidth(label),
                                     available_);
        if (used_ > 0.0f) {
            if (used_ + gap_ + width <= available_) {
                ImGui::SameLine();
                used_ += gap_;
            } else {
                used_ = 0.0f;
            }
        }
        used_ += width;
        ImGui::BeginDisabled(!enabled);
        const bool clicked = icon == Icon::None ? ImGui::Button(label, ImVec2(width, 0.0f))
                                                : iconTextButton(label, icon, label, ImVec2(width, 0.0f));
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tr(tip));
        return clicked && enabled;
    }

private:
    float available_;
    float gap_;
    float used_ = 0.0f;
};

// One color of the Replace dialog: a swatch that opens the picker, its hex text, and shortcuts to the tool colors.
void colorRow(modal::Form& form, Str label, const char* id, float rgba[4], const ToolSettings& tools) {
    form.row(tr(label));
    ImGui::PushID(id);
    constexpr ImGuiColorEditFlags flags =
        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoInputs;
    ImGui::ColorEdit4("##color", rgba, flags);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(hexText(fromFloats(rgba)).c_str());
    form.row(nullptr);
    if (modal::button(tr(Str::PaletteUsePrimary)))
        toFloats(tools.primary, rgba);
    ImGui::SameLine();
    if (modal::button(tr(Str::PaletteUseSecondary)))
        toFloats(tools.secondary, rgba);
    ImGui::PopID();
}

} // namespace

void PalettePanel::reset() noexcept {
    selected_ = -1;
    pendingMove_.reset();
    openReplace_ = false;
    closeReplace_ = replaceActive_; // the popup, if open, is closed the next time it is drawn
}

int PalettePanel::markedIndex(const ToolSettings& settings) const noexcept {
    const Palette& palette = state_.palette;
    if (selected_ < 0 || static_cast<std::size_t>(selected_) >= palette.colors.size())
        return -1;
    const Rgba8 color = palette.colors[static_cast<std::size_t>(selected_)];
    return color == settings.primary || color == settings.secondary ? selected_ : -1;
}

// --- the window --------------------------------------------------------------------------------

void PalettePanel::draw(Document& doc, Toolbox& tools) {
    const Palette& palette = state_.palette;
    selected_ = clampSelection(selected_, palette.colors.size());

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(palette.name.empty() ? tr(Str::PaletteUnnamed) : palette.name.c_str());
    ImGui::TextDisabled("%s", trf(Str::PaletteCount, {std::to_string(palette.colors.size()),
                                                      std::to_string(kMaxPaletteColors)})
                                  .c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    drawButtons(doc, tools);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    drawSwatches(tools);
}

void PalettePanel::drawButtons(Document& doc, Toolbox& tools) {
    const ToolSettings& settings = tools.settings();
    const bool gesture = tools.gestureActive();
    const bool dialogFree = !ctx_.dialogs.busy();
    ButtonFlow flow;

    if (flow.button(Icon::None, tr(Str::PaletteBtnAdd), Str::PaletteTipAdd))
        addPrimary(settings);
    if (flow.button(Icon::Delete, tr(Str::PaletteBtnRemove), Str::PaletteTipRemove, markedIndex(settings) >= 0))
        removeSelected(settings);
    if (flow.button(Icon::Folder, tr(Str::PaletteBtnLoad), Str::PaletteTipLoad, dialogFree)) {
        if (!ctx_.dialogs.open(NativeDialogs::Tag::PaletteLoad))
            ctx_.toasts.push(tr(Str::ErrDialogUnavailable), Toasts::Kind::Error);
    }
    if (flow.button(Icon::Save, tr(Str::PaletteBtnSave), Str::PaletteTipSave, dialogFree)) {
        const std::string name = paletteFileStem(state_.palette.name) + ".gpl";
        if (!ctx_.dialogs.save(NativeDialogs::Tag::PaletteSave, name))
            ctx_.toasts.push(tr(Str::ErrDialogUnavailable), Toasts::Kind::Error);
    }
    if (flow.button(Icon::EyeDropperTool, tr(Str::PaletteBtnFromCanvas), Str::PaletteTipFromCanvas, !gesture))
        fromCanvas(doc);
    if (flow.button(Icon::SortVertical, tr(Str::PaletteBtnSort), Str::PaletteTipSort, state_.palette.colors.size() > 1))
        ImGui::OpenPopup(kSortPopup);
    if (flow.button(Icon::PaintSplash, tr(Str::PaletteBtnReplace), Str::PaletteTipReplace, !gesture))
        requestReplace(settings);

    if (ImGui::BeginPopup(kSortPopup)) {
        if (ImGui::MenuItem(tr(Str::PaletteSortHue)))
            sortBy(PaletteSort::Hue);
        if (ImGui::MenuItem(tr(Str::PaletteSortBrightness)))
            sortBy(PaletteSort::Luminance);
        ImGui::EndPopup();
    }
}

void PalettePanel::drawSwatches(Toolbox& tools) {
    ToolSettings& settings = tools.settings();
    Palette& palette = state_.palette;
    const int count = static_cast<int>(palette.colors.size());
    if (count == 0) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tr(Str::PaletteEmptyHint));
        ImGui::PopTextWrapPos();
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const ThemeTokens& t = tokens();
    const float size = ImGui::GetFrameHeight();
    const float spacing = std::round(style.ItemSpacing.x * 0.5f);
    const int perRow = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (size + spacing)));
    const float ring = std::max(2.0f, std::round(2.0f * uiScale()));
    const int marked = markedIndex(settings);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));
    for (int i = 0; i < count; ++i) {
        if (i % perRow != 0)
            ImGui::SameLine();

        const Rgba8 color = palette.colors[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + size, min.y + size);
        const bool clicked = ImGui::InvisibleButton("##swatch", ImVec2(size, size));
        const bool hovered = ImGui::IsItemHovered();
        if (clicked) {
            settings.primary = Rgba8{color.r, color.g, color.b, 255};
            selected_ = i;
        } else if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            settings.secondary = Rgba8{color.r, color.g, color.b, 255};
            selected_ = i;
        }

        // The swatch itself is always opaque; its border comes from the theme so near-white and near-black colors
        // stay visible in both light and dark windows.
        dl->AddRectFilled(min, max, IM_COL32(color.r, color.g, color.b, 255));
        dl->AddRect(min, max, hovered ? colorOf(t.text, 0.85f) : ImGui::GetColorU32(ImGuiCol_Border));
        if (i == marked) {
            // An accent ring with a thin inner line in black or white, so it shows on any swatch color.
            const float half = ring * 0.5f;
            dl->AddRect(ImVec2(min.x + half, min.y + half), ImVec2(max.x - half, max.y - half), colorOf(t.accent), 0.0f, 0, ring);
            const float inner = ring + 1.0f;
            if (size > inner * 2.0f + 2.0f)
                dl->AddRect(ImVec2(min.x + inner, min.y + inner), ImVec2(max.x - inner, max.y - inner),
                            colorOf(contrastText(toImVec4(color))), 0.0f, 0, 1.0f);
        }
        if (ImGui::IsItemFocused()) // keyboard navigation: a thin outline
            dl->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_NavHighlight), 0.0f, 0, 1.0f);

        // Reordering: drag a swatch onto another; a line shows on which side it will land.
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            ImGui::SetDragDropPayload(kDragType, &i, sizeof(i));
            ImGui::ColorButton("##drag", toImVec4(Rgba8{color.r, color.g, color.b, 255}),
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(size, size));
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            const ImGuiDragDropFlags flags =
                ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kDragType, flags)) {
                int from = -1;
                if (payload->DataSize == static_cast<int>(sizeof(from)))
                    std::memcpy(&from, payload->Data, sizeof(from));
                if (from >= 0 && from != i) {
                    const bool after = ImGui::GetMousePos().x >= min.x + size * 0.5f;
                    const float x = after ? max.x + spacing * 0.5f : min.x - spacing * 0.5f;
                    dl->AddLine(ImVec2(x, min.y - 1.0f), ImVec2(x, max.y + 1.0f), colorOf(t.accent), ring);
                    if (payload->IsDelivery())
                        pendingMove_ = std::make_pair(from, paletteDropIndex(from, i, after));
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SetItemTooltip("%s", trf(Str::PaletteSwatchTip, {hexText(color)}).c_str());
        ImGui::PopID();
    }
    ImGui::PopStyleVar();

    // Applied after the loop: moving a color while the swatches are being drawn would change what is drawn.
    if (pendingMove_) {
        const auto [from, to] = *pendingMove_;
        pendingMove_.reset();
        if (movePaletteColor(palette, from, to)) {
            state_.touch();
            selected_ = indexAfterMove(selected_, from, to);
        }
    }
}

// --- editing -----------------------------------------------------------------------------------

void PalettePanel::addPrimary(const ToolSettings& settings) {
    Palette& palette = state_.palette;
    Rgba8 color = settings.primary;
    if (color.a == 0) {
        ctx_.toasts.push(tr(Str::ToastPaletteTransparent), Toasts::Kind::Error);
        return;
    }
    color.a = 255;
    if (const int existing = findPaletteColor(palette, color); existing >= 0) {
        selected_ = existing;
        ctx_.toasts.push(tr(Str::ToastPaletteHas));
        return;
    }
    if (!addPaletteColor(palette, color)) {
        ctx_.toasts.push(trf(Str::ToastPaletteFull, {std::to_string(kMaxPaletteColors)}), Toasts::Kind::Error);
        return;
    }
    state_.touch();
    selected_ = static_cast<int>(palette.colors.size()) - 1;
}

void PalettePanel::removeSelected(const ToolSettings& settings) {
    const int index = markedIndex(settings);
    if (index < 0 || !removePaletteColor(state_.palette, index))
        return;
    state_.touch();
    selected_ = indexAfterRemove(selected_, index);
}

void PalettePanel::sortBy(PaletteSort by) {
    Palette& palette = state_.palette;
    const bool hadSelection = selected_ >= 0 && static_cast<std::size_t>(selected_) < palette.colors.size();
    const Rgba8 selectedColor = hadSelection ? palette.colors[static_cast<std::size_t>(selected_)] : Rgba8{};
    const Palette before = palette;
    sortPalette(palette, by);
    if (palette == before)
        return;
    state_.touch();
    if (hadSelection)
        selected_ = findPaletteColor(palette, selectedColor);
}

void PalettePanel::fromCanvas(const Document& doc) {
    // Every cel of every visible layer, in every frame: the colors of the whole picture, not just the shown frame.
    std::vector<const Image*> images;
    for (int layer = 0; layer < doc.layerCount(); ++layer) {
        if (!doc.layer(layer).visible)
            continue;
        for (int frame = 0; frame < doc.frameCount(); ++frame) {
            const Image& cel = doc.cel(layer, frame);
            if (!cel.empty())
                images.push_back(&cel);
        }
    }
    try {
        Palette found = paletteFromImages(images, kMaxPaletteColors);
        if (found.colors.empty()) {
            ctx_.toasts.push(tr(Str::ToastPaletteCanvasEmpty));
            return;
        }
        const std::size_t count = found.colors.size();
        state_.palette = std::move(found);
        state_.touch();
        selected_ = -1;
        ctx_.toasts.push(trf(Str::ToastPaletteFromCanvas, {std::to_string(count)}));
    } catch (const std::bad_alloc&) {
        ctx_.toasts.push(tr(Str::ErrLayerMemory), Toasts::Kind::Error);
    }
}

// --- files -------------------------------------------------------------------------------------

bool PalettePanel::onNativeDialog(const NativeDialogs::Result& result) {
    const bool load = result.tag == NativeDialogs::Tag::PaletteLoad;
    if (!load && result.tag != NativeDialogs::Tag::PaletteSave)
        return false;
    if (!result.error.empty()) {
        ctx_.toasts.push(trf(Str::ErrDialogFailedReason, {result.error}), Toasts::Kind::Error);
        return true;
    }
    if (result.cancelled || result.path.empty())
        return true;
    if (load)
        loadFrom(result.path);
    else
        saveTo(result.path);
    return true;
}

void PalettePanel::rememberPath(const std::filesystem::path& path) {
    const std::string text = utf8FromPath(path);
    std::string& stored = ctx_.settings.pixel.lastPalettePath;
    if (text.size() > kMaxPalettePathBytes || text == stored)
        return;
    stored = text;
    ctx_.settingsDirty = true;
}

void PalettePanel::loadFrom(const std::string& utf8Path) {
    const std::filesystem::path path = pathFromUtf8(utf8Path);
    const std::string fileName = utf8FromPath(path.filename());
    try {
        std::string text;
        if (!readCapped(path, text)) {
            ctx_.toasts.push(trf(Str::IoCannotRead, {fileName}), Toasts::Kind::Error);
            return;
        }
        Palette loaded = lowerExtension(path) == ".hex" ? parseHex(text) : parseGpl(text);
        if (loaded.colors.empty()) {
            ctx_.toasts.push(trf(Str::ToastPaletteNoColors, {fileName}), Toasts::Kind::Error);
            return; // the palette in use stays
        }
        if (loaded.name.empty())
            loaded.name = cleanPaletteName(utf8FromPath(path.stem()));
        const std::size_t count = loaded.colors.size();
        const std::string name = loaded.name.empty() ? fileName : loaded.name;
        state_.palette = std::move(loaded);
        state_.touch();
        selected_ = -1;
        rememberPath(path);
        ctx_.toasts.push(trf(Str::ToastPaletteLoaded, {name, std::to_string(count)}));
    } catch (const std::exception&) {
        ctx_.toasts.push(trf(Str::IoCannotRead, {fileName}), Toasts::Kind::Error);
    }
}

void PalettePanel::saveTo(const std::string& utf8Path) {
    std::filesystem::path path = pathFromUtf8(utf8Path);
    std::string extension = lowerExtension(path);
    if (extension != ".gpl" && extension != ".hex") {
        path += ".gpl"; // the dialog's filter is GIMP palettes first, so a name without a known extension gets it
        extension = ".gpl";
    }
    try {
        Palette out = state_.palette;
        if (out.name.empty())
            out.name = cleanPaletteName(utf8FromPath(path.stem())); // a .gpl names itself after its file
        const std::string text = extension == ".hex" ? formatHex(out) : formatGpl(out);
        const std::string error = writeFileAtomic(
            path, std::span<const unsigned char>(reinterpret_cast<const unsigned char*>(text.data()), text.size()));
        if (!error.empty()) {
            ctx_.toasts.push(error, Toasts::Kind::Error);
            return;
        }
        rememberPath(path);
        ctx_.toasts.push(trf(Str::ToastSaved, {utf8FromPath(path.filename())}));
    } catch (const std::exception&) {
        ctx_.toasts.push(trf(Str::IoCannotWriteTo, {utf8FromPath(path.filename())}), Toasts::Kind::Error);
    }
}

// --- Replace Color -----------------------------------------------------------------------------

void PalettePanel::requestReplace(const ToolSettings& settings) {
    const int marked = markedIndex(settings);
    const Rgba8 from = marked >= 0 ? state_.palette.colors[static_cast<std::size_t>(marked)] : settings.primary;
    toFloats(from, replaceFrom_);
    toFloats(from == settings.secondary ? settings.primary : settings.secondary, replaceTo_);
    openReplace_ = true;
}

void PalettePanel::drawDialogs(Document& doc, Toolbox& tools) {
    if (openReplace_) {
        ImGui::OpenPopup(replaceTitle().c_str());
        openReplace_ = false;
        replaceActive_ = true;
        closeReplace_ = false;
    }
    if (!replaceActive_) {
        closeReplace_ = false;
        return;
    }

    if (!modal::begin(replaceTitle().c_str(), 26.0f)) {
        replaceActive_ = false;
        return;
    }

    const ToolSettings& settings = tools.settings();
    modal::Form form({tr(Str::PaletteReplaceFrom), tr(Str::PaletteReplaceTo), tr(Str::PaletteReplaceIn)});
    colorRow(form, Str::PaletteReplaceFrom, "##replaceFrom", replaceFrom_, settings);
    colorRow(form, Str::PaletteReplaceTo, "##replaceTo", replaceTo_, settings);

    scope_ = std::clamp(scope_, 0, static_cast<int>(kScopes.size()) - 1);
    form.row(tr(Str::PaletteReplaceIn));
    const int picked = modal::choices("##replaceScope", scope_, {tr(kScopes[0].label), tr(kScopes[1].label), tr(kScopes[2].label)});
    if (picked >= 0)
        scope_ = picked;

    modal::paragraph(tr(Str::PaletteReplaceHint), true);

    const Rgba8 from = fromFloats(replaceFrom_);
    const Rgba8 to = fromFloats(replaceTo_);
    // A tool gesture owns the document until it ends; it cannot start behind this modal, but check anyway.
    const bool canReplace = from != to && !tools.gestureActive();
    const int pressed = modal::footer({{tr(Str::WordCancel), modal::ButtonKind::Secondary, true, false, true},
                                       {tr(Str::WordReplace), modal::ButtonKind::Primary, canReplace, true}});
    const bool apply = pressed == 1;
    const bool cancel = closeReplace_ || pressed == 0;

    if (apply || cancel) {
        ImGui::CloseCurrentPopup();
        replaceActive_ = false;
        closeReplace_ = false;
        if (apply && !cancel) {
            switch (doc.replaceColor(from, to, kScopes[static_cast<std::size_t>(scope_)].scope)) {
            case Document::OpResult::Applied:
                ctx_.toasts.push(trf(Str::ToastPaletteReplaced, {hexText(from), hexText(to)}));
                break;
            case Document::OpResult::Nothing:
                ctx_.toasts.push(tr(Str::ToastPaletteNothing));
                break;
            case Document::OpResult::NoMemory:
                ctx_.toasts.push(tr(Str::ErrLayerMemory), Toasts::Kind::Error);
                break;
            }
        }
    }
    modal::end();
}

} // namespace trace
