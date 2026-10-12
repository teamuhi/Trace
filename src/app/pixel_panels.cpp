#include "app/pixel_panels.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <new>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "app/sampled_image.hpp"
#include "app/theme.hpp"
#include "app/toasts.hpp"
#include "app/tool_icons.hpp"
#include "core/document.hpp"
#include "core/i18n.hpp"
#include "core/transform.hpp"

namespace trace {

namespace {

constexpr const char* kDragType = "TRACE_LAYER";
constexpr int kThumbMaxSide = 64; // the preview texture; the row shows it smaller

ImU32 colorOf(ImVec4 c, float alpha = 1.0f) {
    c.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(c);
}

int percentOf(std::uint8_t opacity) {
    return (static_cast<int>(opacity) * 100 + 127) / 255;
}

std::uint8_t opacityOf(int percent) {
    return static_cast<std::uint8_t>(std::clamp((percent * 255 + 50) / 100, 0, 255));
}

int indexOfLayer(const Document& doc, std::uint32_t id) {
    for (int i = 0; i < doc.layerCount(); ++i) {
        if (doc.layer(i).id == id)
            return i;
    }
    return -1;
}

void reportNoMemory(Toasts& toasts) {
    toasts.push(tr(Str::ErrLayerMemory), Toasts::Kind::Error);
}

// Changes the properties of one layer; an out-of-memory result is reported.
void setProps(Document& doc, int layer, const LayerInfo& props, Toasts& toasts) {
    if (doc.setLayerProps(layer, props) == Document::OpResult::NoMemory)
        reportNoMemory(toasts);
}

// Where a dragged layer ends up when dropped on the upper or lower half of the row of layer `target`. Rows
// are listed top layer first, so the upper half means "above the target" (a higher index). The index is the
// one moveLayer() takes: the position after the dragged layer has been taken out of the stack.
int dropIndex(int from, int target, bool upperHalf) {
    if (upperHalf)
        return from < target ? target : target + 1;
    return from < target ? target - 1 : target;
}

// A checkerboard under a thumbnail, from the theme's transparency colors.
void drawChecker(ImDrawList* dl, ImVec2 min, ImVec2 max, float cell) {
    const ThemeTokens& t = tokens();
    dl->AddRectFilled(min, max, colorOf(t.checkerA));
    const ImU32 dark = colorOf(t.checkerB);
    int row = 0;
    for (float y = min.y; y < max.y; y += cell, ++row) {
        int col = 0;
        for (float x = min.x; x < max.x; x += cell, ++col) {
            if ((row + col) % 2 == 1)
                dl->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + cell, max.x), std::min(y + cell, max.y)), dark);
        }
    }
}

struct Button {
    LayerCommand command;
    Str label;
    Str tip;
};
constexpr Button kButtons[] = {
    {LayerCommand::New, Str::LayerBtnNew, Str::MenuNewLayer},
    {LayerCommand::Duplicate, Str::LayerBtnDuplicate, Str::MenuDuplicateLayer},
    {LayerCommand::Delete, Str::LayerBtnDelete, Str::MenuDeleteLayer},
    {LayerCommand::MergeDown, Str::LayerBtnMerge, Str::MenuMergeDown},
};

// What the user did this frame. Collected while the rows are drawn and applied afterwards, because applying
// one changes the layer list the rows are being drawn from.
struct Op {
    enum class Kind { Select, ToggleVisible, ToggleLock, Rename, Move, Opacity, Command };
    explicit Op(Kind what, std::uint32_t layer = 0) : kind(what), layerId(layer) {}

    Kind kind;
    std::uint32_t layerId;
    std::uint32_t targetId = 0; // Move: the row dropped on
    bool upperHalf = false;     // Move: the upper half of that row
    std::string text;           // Rename
    int percent = 100;          // Opacity
    LayerCommand command = LayerCommand::New;
};

} // namespace

// --- commands ----------------------------------------------------------------------------------

bool canRunLayerCommand(const Document& doc, LayerCommand command) {
    const int active = doc.activeLayer();
    switch (command) {
    case LayerCommand::New:
    case LayerCommand::Duplicate:
        return true;
    case LayerCommand::Delete:
    case LayerCommand::Flatten:
        return doc.layerCount() > 1;
    case LayerCommand::MergeDown:
    case LayerCommand::MoveDown:
        return active > 0;
    case LayerCommand::MoveUp:
        return active < doc.layerCount() - 1;
    }
    return false;
}

bool runLayerCommand(Document& doc, LayerCommand command, Toasts& toasts) {
    if (!canRunLayerCommand(doc, command))
        return false;
    const int active = doc.activeLayer();
    const int width = doc.image().width();
    const int height = doc.image().height();

    // A new layer needs a cel in every frame: stay inside the layer limit and the memory budget.
    const auto roomForLayer = [&] {
        if (doc.layerCount() >= kMaxLayers) {
            toasts.push(trf(Str::ToastLayerLimit, {std::to_string(kMaxLayers)}));
            return false;
        }
        if (!fitsDocumentBudget(width, height, doc.layerCount() + 1, doc.frameCount())) {
            toasts.push(tr(Str::ToastLayerTooBig));
            return false;
        }
        return true;
    };
    const auto locked = [&](int layer) {
        if (!doc.layer(layer).locked)
            return false;
        toasts.push(tr(Str::ToastLayerLocked));
        return true;
    };

    Document::OpResult result = Document::OpResult::Nothing;
    switch (command) {
    case LayerCommand::New:
        if (!roomForLayer())
            return false;
        result = doc.addLayer(trf(Str::LayerNameDefault, {std::to_string(doc.layerCount() + 1)}));
        break;
    case LayerCommand::Duplicate:
        if (!roomForLayer())
            return false;
        result = doc.duplicateLayer(active, trf(Str::LayerNameCopy, {doc.layer(active).name}));
        break;
    case LayerCommand::Delete:
        if (locked(active))
            return false;
        result = doc.deleteLayer(active);
        break;
    case LayerCommand::MergeDown:
        if (locked(active) || locked(active - 1))
            return false;
        result = doc.mergeDown(active);
        break;
    case LayerCommand::Flatten:
        for (int i = 0; i < doc.layerCount(); ++i) {
            if (locked(i))
                return false;
        }
        result = doc.flatten();
        break;
    case LayerCommand::MoveUp:
        result = doc.moveLayer(active, active + 1);
        break;
    case LayerCommand::MoveDown:
        result = doc.moveLayer(active, active - 1);
        break;
    }
    if (result == Document::OpResult::NoMemory)
        reportNoMemory(toasts);
    return result == Document::OpResult::Applied;
}

// --- the Layers panel --------------------------------------------------------------------------

void LayersPanel::release() noexcept {
    thumbs_.clear(); // destroys the textures
}

void LayersPanel::reset() noexcept {
    thumbs_.clear();
    renamingId_ = 0;
    pendingRename_.reset();
    pendingOpacity_.reset();
    ++staleEpoch_;
}

LayersPanel::Thumb& LayersPanel::thumbFor(const Document& doc, CelId id, int layer, int frame) {
    const auto [it, inserted] = thumbs_.try_emplace(id);
    Thumb& thumb = it->second;
    if (inserted)
        thumb.mirror.init(renderer_);
    // Only the active cel can change under us; any other cel keeps its id until it is replaced.
    const bool stale = inserted || !thumb.valid || thumb.epoch != staleEpoch_ ||
                       (id == doc.activeCel() && thumb.revision != doc.revision());
    if (stale) {
        try {
            const Image small = thumbnailOf(doc.cel(layer, frame), kThumbMaxSide);
            thumb.mirror.sync(small, IRect{0, 0, small.width(), small.height()});
        } catch (const std::bad_alloc&) {
            // No preview this time; the row still works.
        }
        thumb.valid = true;
        thumb.revision = doc.revision();
        thumb.epoch = staleEpoch_;
    }
    return thumb;
}

void LayersPanel::pruneThumbs(const Document& doc) {
    if (thumbs_.size() <= static_cast<std::size_t>(doc.layerCount()) + 8)
        return;
    std::unordered_set<CelId> live;
    for (int l = 0; l < doc.layerCount(); ++l)
        live.insert(doc.celId(l, doc.activeFrame()));
    for (auto it = thumbs_.begin(); it != thumbs_.end();)
        it = live.count(it->first) ? std::next(it) : thumbs_.erase(it);
}

void LayersPanel::draw(Document& doc, bool gestureActive, Toasts& toasts) {
    // A frame in which the panel was not drawn (hidden, collapsed) may have missed edits: rebuild every thumbnail.
    const int frame = ImGui::GetFrameCount();
    if (lastFrame_ < 0 || frame - lastFrame_ > 1)
        ++staleEpoch_;
    lastFrame_ = frame;

    // Things that finished while a gesture was running wait for it to end.
    if (!gestureActive) {
        if (pendingRename_) {
            const int index = indexOfLayer(doc, pendingRename_->first);
            if (index >= 0) {
                LayerInfo props = doc.layer(index);
                props.name = pendingRename_->second;
                setProps(doc, index, props, toasts);
            }
            pendingRename_.reset();
        }
        if (pendingOpacity_) {
            const int index = indexOfLayer(doc, pendingOpacity_->first);
            if (index >= 0) {
                LayerInfo props = doc.layer(index);
                props.opacity = opacityOf(pendingOpacity_->second);
                setProps(doc, index, props, toasts);
            }
            pendingOpacity_.reset();
        }
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const ThemeTokens& t = tokens();
    const float em = ImGui::GetFontSize();
    const int count = doc.layerCount();
    const int active = doc.activeLayer();
    const int activeFrame = doc.activeFrame();
    std::vector<Op> ops;

    // The footer under the list: opacity slider and the four buttons, which wrap to more rows in a narrow panel.
    const float frameHeight = ImGui::GetFrameHeight();
    const float gap = style.ItemSpacing.x;
    const float available = ImGui::GetContentRegionAvail().x;
    float buttonNeed = 0.0f;
    for (const Button& b : kButtons)
        buttonNeed = std::max(buttonNeed, ImGui::CalcTextSize(tr(b.label)).x + style.FramePadding.x * 2.0f);
    const int perRow = std::clamp(static_cast<int>((available + gap) / (buttonNeed + gap)), 1, 4);
    const int buttonRows = (4 + perRow - 1) / perRow;
    const float buttonWidth = std::floor((available - gap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow));
    const float footer = ImGui::GetTextLineHeight() + frameHeight + static_cast<float>(buttonRows) * frameHeight +
                         style.ItemSpacing.y * static_cast<float>(buttonRows + 2);

    // --- rows -------------------------------------------------------------------------------------
    const float rowHeight = std::round(em * 2.7f);
    const float pad = std::round(em * 0.3f);
    const float iconSize = std::round(em * 1.1f);
    const float thumbBox = rowHeight - 2.0f * pad;
    const float barWidth = std::max(2.0f, std::round(3.0f * uiScale()));

    if (ImGui::BeginChild("##layerRows", ImVec2(0.0f, -footer), ImGuiChildFlags_Borders)) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.0f));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiListClipper clipper;
        clipper.Begin(count, rowHeight);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const int i = count - 1 - row; // the top layer is listed first
                const LayerInfo info = doc.layer(i); // a copy: the list may change before the next row
                const bool isActive = i == active;
                ImGui::PushID(static_cast<int>(info.id));

                const ImVec2 min = ImGui::GetCursorScreenPos();
                const float width = ImGui::GetContentRegionAvail().x;
                const ImVec2 max(min.x + width, min.y + rowHeight);
                const float eyeX = min.x + pad + barWidth;
                const float lockX = eyeX + iconSize + pad;
                const float thumbX = lockX + iconSize + pad;
                const float nameX = thumbX + thumbBox + pad * 2.0f;

                ImGui::SetNextItemAllowOverlap();
                ImGui::InvisibleButton("##row", ImVec2(width, rowHeight));
                const bool rowHovered = ImGui::IsItemHovered();
                if (ImGui::IsItemActivated() && !isActive)
                    ops.push_back(Op{Op::Kind::Select, info.id});
                if (rowHovered && ImGui::GetMousePos().x >= nameX && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    ImGui::SetItemTooltip("%s\n%s", tr(Str::LayerTipRename), tr(Str::LayerTipDrag));
                const bool startRename = rowHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                                         ImGui::GetMousePos().x >= nameX && !gestureActive;

                // Reordering: drag a row onto another; a line shows where it will land.
                if (!gestureActive && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    ImGui::SetDragDropPayload(kDragType, &info.id, sizeof(info.id));
                    ImGui::TextUnformatted(info.name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget()) {
                    const ImGuiDragDropFlags flags =
                        ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kDragType, flags)) {
                        std::uint32_t fromId = 0;
                        std::memcpy(&fromId, payload->Data, std::min<std::size_t>(sizeof(fromId), static_cast<std::size_t>(payload->DataSize)));
                        const bool upper = ImGui::GetMousePos().y < min.y + rowHeight * 0.5f;
                        if (fromId != info.id) {
                            const float y = upper ? min.y : max.y;
                            dl->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), colorOf(t.accent), std::max(2.0f, barWidth));
                        }
                        if (payload->IsDelivery()) {
                            Op op{Op::Kind::Move, fromId};
                            op.targetId = info.id;
                            op.upperHalf = upper;
                            ops.push_back(std::move(op));
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // Background: the active row in the accent, a hovered one in the theme's hover color.
                if (isActive) {
                    dl->AddRectFilled(min, max, colorOf(t.accent, 0.30f));
                    dl->AddRectFilled(min, ImVec2(min.x + barWidth, max.y), colorOf(t.accent));
                } else if (rowHovered) {
                    dl->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_HeaderHovered, 0.5f));
                }
                dl->AddLine(ImVec2(min.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), ImGui::GetColorU32(ImGuiCol_Separator, 0.5f));

                // Eye and lock: small buttons laid over the row.
                const float iconY = std::round(min.y + (rowHeight - iconSize) * 0.5f);
                const auto iconButton = [&](const char* id, float x, UiIcon icon, bool dim, Str tip) {
                    ImGui::SetCursorScreenPos(ImVec2(x - pad * 0.5f, min.y));
                    const bool clicked = ImGui::InvisibleButton(id, ImVec2(iconSize + pad, rowHeight));
                    const bool hovered = ImGui::IsItemHovered();
                    ImVec4 color = t.text;
                    color.w *= dim ? (hovered ? 0.8f : 0.38f) : (hovered ? 1.0f : 0.85f);
                    drawUiIcon(dl, icon, ImVec2(std::round(x), iconY), iconSize, ImGui::ColorConvertFloat4ToU32(color));
                    ImGui::SetItemTooltip("%s", tr(tip));
                    return clicked;
                };
                if (iconButton("##eye", eyeX, info.visible ? UiIcon::Eye : UiIcon::EyeOff, !info.visible,
                               info.visible ? Str::LayerTipHide : Str::LayerTipShow))
                    ops.push_back(Op{Op::Kind::ToggleVisible, info.id});
                if (iconButton("##lock", lockX, info.locked ? UiIcon::Lock : UiIcon::Unlock, !info.locked,
                               info.locked ? Str::LayerTipUnlock : Str::LayerTipLock))
                    ops.push_back(Op{Op::Kind::ToggleLock, info.id});

                // Thumbnail of this layer's cel in the shown frame.
                const CelId cel = doc.celId(i, activeFrame);
                if (cel != kNoCel) {
                    Thumb& thumb = thumbFor(doc, cel, i, activeFrame);
                    const float top = std::round(min.y + pad);
                    const ImVec2 boxMin(std::round(thumbX), top);
                    const ImVec2 boxMax(boxMin.x + thumbBox, top + thumbBox);
                    if (SDL_Texture* texture = thumb.mirror.texture()) {
                        const float w = static_cast<float>(thumb.mirror.width());
                        const float h = static_cast<float>(thumb.mirror.height());
                        const float scale = std::min(thumbBox / w, thumbBox / h);
                        const float dw = std::max(1.0f, std::floor(w * scale));
                        const float dh = std::max(1.0f, std::floor(h * scale));
                        const ImVec2 a(std::round(boxMin.x + (thumbBox - dw) * 0.5f), std::round(boxMin.y + (thumbBox - dh) * 0.5f));
                        const ImVec2 b(a.x + dw, a.y + dh);
                        drawChecker(dl, a, b, std::max(2.0f, std::round(thumbBox / 8.0f)));
                        // Enlarged previews keep hard pixels; reduced ones are averaged smoothly.
                        const bool enlarged = dw >= w;
                        thumb.mirror.setNearest(enlarged);
                        addImageSampled(dl, static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(texture)), a, b,
                                        ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), enlarged);
                        dl->AddRect(ImVec2(a.x - 1.0f, a.y - 1.0f), ImVec2(b.x + 1.0f, b.y + 1.0f), colorOf(t.canvasBorder));
                    } else {
                        dl->AddRect(boxMin, boxMax, colorOf(t.canvasBorder));
                    }
                }

                // Name: plain text, or a text field while renaming.
                const float textY = std::round(min.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
                if (renamingId_ == info.id) {
                    ImGui::SetCursorScreenPos(ImVec2(nameX, std::round(min.y + (rowHeight - frameHeight) * 0.5f)));
                    ImGui::SetNextItemWidth(std::max(max.x - nameX - pad, em * 3.0f));
                    if (renameFocus_) {
                        ImGui::SetKeyboardFocusHere();
                        renameFocus_ = false;
                    }
                    ImGui::InputText("##rename", renameBuffer_, sizeof(renameBuffer_),
                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                    if (ImGui::IsItemDeactivated()) { // Enter, a click elsewhere, or Escape (which restores the old text)
                        Op op{Op::Kind::Rename, info.id};
                        op.text = renameBuffer_;
                        ops.push_back(std::move(op));
                        renamingId_ = 0;
                    }
                } else {
                    dl->PushClipRect(ImVec2(nameX, min.y), ImVec2(max.x - pad, max.y), true);
                    dl->AddText(ImVec2(nameX, textY), colorOf(t.text, info.visible ? 1.0f : 0.5f), info.name.c_str());
                    dl->PopClipRect();
                }
                if (startRename) {
                    renamingId_ = info.id;
                    renameFocus_ = true;
                    std::snprintf(renameBuffer_, sizeof(renameBuffer_), "%s", info.name.c_str());
                }

                ImGui::SetCursorScreenPos(ImVec2(min.x, max.y)); // the next row starts right below this one
                ImGui::PopID();
            }
        }
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();

    // --- opacity ----------------------------------------------------------------------------------
    const LayerInfo activeInfo = doc.layer(active);
    ImGui::TextUnformatted(tr(Str::LayerOpacity));
    if (!ImGui::IsAnyItemActive())
        opacityPercent_ = percentOf(activeInfo.opacity);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt("##opacity", &opacityPercent_, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemDeactivatedAfterEdit()) { // applied on release: one drag is one undo step
        Op op{Op::Kind::Opacity, activeInfo.id};
        op.percent = opacityPercent_;
        ops.push_back(std::move(op));
    }

    // --- buttons ----------------------------------------------------------------------------------
    int shown = 0;
    for (const Button& b : kButtons) {
        if (shown % perRow != 0)
            ImGui::SameLine();
        ++shown;
        ImGui::BeginDisabled(!canRunLayerCommand(doc, b.command));
        if (ImGui::Button(tr(b.label), ImVec2(buttonWidth, 0.0f))) {
            Op op{Op::Kind::Command};
            op.command = b.command;
            ops.push_back(std::move(op));
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tr(b.tip));
    }

    // --- apply what the user did ------------------------------------------------------------------
    for (Op& op : ops) {
        const int index = indexOfLayer(doc, op.layerId);
        // A gesture on the canvas owns the document until it ends. Finished text and opacity are kept for later.
        if (gestureActive) {
            if (op.kind == Op::Kind::Rename)
                pendingRename_ = std::make_pair(op.layerId, std::move(op.text));
            else if (op.kind == Op::Kind::Opacity)
                pendingOpacity_ = std::make_pair(op.layerId, op.percent);
            continue;
        }
        switch (op.kind) {
        case Op::Kind::Select:
            if (index >= 0 && doc.setActive(index, doc.activeFrame()) == Document::OpResult::NoMemory)
                reportNoMemory(toasts);
            break;
        case Op::Kind::ToggleVisible:
        case Op::Kind::ToggleLock:
            if (index >= 0) {
                LayerInfo props = doc.layer(index);
                if (op.kind == Op::Kind::ToggleVisible)
                    props.visible = !props.visible;
                else
                    props.locked = !props.locked;
                setProps(doc, index, props, toasts);
            }
            break;
        case Op::Kind::Rename:
            if (index >= 0) {
                LayerInfo props = doc.layer(index);
                props.name = op.text; // an empty or blank name keeps the old one
                setProps(doc, index, props, toasts);
            }
            break;
        case Op::Kind::Opacity:
            if (index >= 0) {
                LayerInfo props = doc.layer(index);
                props.opacity = opacityOf(op.percent);
                if (percentOf(props.opacity) != percentOf(doc.layer(index).opacity)) // untouched values stay exact
                    setProps(doc, index, props, toasts);
            }
            break;
        case Op::Kind::Move: {
            const int target = indexOfLayer(doc, op.targetId);
            if (index >= 0 && target >= 0 && index != target) {
                if (doc.moveLayer(index, dropIndex(index, target, op.upperHalf)) == Document::OpResult::NoMemory)
                    reportNoMemory(toasts);
            }
            break;
        }
        case Op::Kind::Command:
            runLayerCommand(doc, op.command, toasts);
            break;
        }
    }
    pruneThumbs(doc);
}

} // namespace trace
