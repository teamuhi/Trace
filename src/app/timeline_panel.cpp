#include "app/timeline_panel.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <new>
#include <string>
#include <utility>

#include "app/icon_widgets.hpp"
#include "app/pixel_panels.hpp"
#include "app/theme.hpp"
#include "app/toasts.hpp"
#include "core/document.hpp"
#include "core/i18n.hpp"
#include "core/transform.hpp"

namespace trace {

namespace {

constexpr const char* kDragType = "TRACE_FRAME";
constexpr int kThumbMaxSide = 128; // the preview texture; the cell shows it smaller

ImU32 colorOf(ImVec4 c, float alpha = 1.0f) {
    c.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(c);
}

void reportNoMemory(Toasts& toasts) {
    toasts.push(tr(Str::ErrLayerMemory), Toasts::Kind::Error);
}

// What the user did this frame, collected while the cells are drawn and applied afterwards (applying one changes
// the frame list the cells are being drawn from).
struct Op {
    enum class Kind { Select, Move, Duration, Command };
    explicit Op(Kind what) : kind(what) {}

    Kind kind;
    int frame = 0;      // Select, Duration: the frame; Move: the dragged frame
    int target = 0;     // Move: the cell dropped on
    bool right = false; // Move: the right half of that cell
    int ms = 0;         // Duration
    LayerCommand command = LayerCommand::NewFrame;
};

struct CommandButton {
    LayerCommand command;
    Icon icon;
    Str label;
    Str tip;
};
constexpr CommandButton kCommandButtons[] = {
    {LayerCommand::NewFrame, Icon::AddLayer, Str::TimelineBtnNew, Str::MenuNewFrame},
    {LayerCommand::DuplicateFrame, Icon::Duplicate, Str::TimelineBtnDuplicate, Str::MenuDuplicateFrame},
    {LayerCommand::DeleteFrame, Icon::Delete, Str::TimelineBtnDelete, Str::MenuDeleteFrame},
};

} // namespace

// --- lifetime and playback -----------------------------------------------------------------------

void TimelinePanel::release() noexcept {
    thumbs_.clear(); // destroys the textures
}

void TimelinePanel::reset() noexcept {
    thumbs_.clear();
    playback_.stop();
    editFrame_ = -1;
    lastShownFrame_ = -1;
    scrollTries_ = 0;
    ++staleEpoch_;
}

void TimelinePanel::togglePlay(const Document& doc) {
    if (playback_.playing()) {
        playback_.stop();
        return;
    }
    if (doc.frameCount() < 2)
        return;
    playback_.start(playbackStartFrame(doc.activeFrame(), doc.frameCount(), loop_));
    expectedRevision_ = doc.revision();
    fresh_ = true;
}

void TimelinePanel::setActiveFrame(Document& doc, int frame, Toasts& toasts) {
    if (doc.setActive(doc.activeLayer(), frame) == Document::OpResult::NoMemory)
        reportNoMemory(toasts);
}

void TimelinePanel::stepFrame(Document& doc, int delta, Toasts& toasts) {
    playback_.stop();
    setActiveFrame(doc, steppedFrame(doc.activeFrame(), doc.frameCount(), delta, loop_), toasts);
}

void TimelinePanel::update(Document& doc, bool gestureActive) {
    // Frames that were not drawn in between mean the mode was left (or the window was hidden): stop, so coming
    // back does not find the animation running.
    const int frame = ImGui::GetFrameCount();
    const bool gap = lastUpdateFrame_ >= 0 && frame - lastUpdateFrame_ > 1;
    lastUpdateFrame_ = frame;
    if (!playback_.playing())
        return;
    // Anything but this clock changing the document (an edit, undo, a layer or frame command, a new file) ends
    // playback: the revision no longer matches what the last tick left.
    if (gap || gestureActive || doc.frameCount() < 2 || doc.revision() != expectedRevision_) {
        playback_.stop();
        return;
    }

    const double dtMs = fresh_ ? 0.0 : clampedFrameMs(static_cast<double>(ImGui::GetIO().DeltaTime));
    fresh_ = false;
    durations_.resize(static_cast<std::size_t>(doc.frameCount()));
    for (int i = 0; i < doc.frameCount(); ++i)
        durations_[static_cast<std::size_t>(i)] = doc.frame(i).durationMs;

    const int next = playback_.tick(dtMs, durations_, loop_);
    if (next != doc.activeFrame()) {
        if (doc.setActive(doc.activeLayer(), next) == Document::OpResult::NoMemory)
            playback_.stop();
        expectedRevision_ = doc.revision();
    }
}

// --- thumbnails ----------------------------------------------------------------------------------

TimelinePanel::Thumb& TimelinePanel::thumbFor(const Document& doc, int frame, std::uint64_t signature, bool active,
                                              bool gestureActive) {
    const auto [it, inserted] = thumbs_.try_emplace(signature);
    Thumb& thumb = it->second;
    if (inserted)
        thumb.mirror.init(renderer_);
    thumb.used = drawCount_;

    // Only the active frame's cel can change under its id, so only that frame is checked against the revision.
    // One that was the active frame when it was built is rebuilt once more after it stops being so, since its last
    // change may have come after the build. While playing nothing is edited, and during a stroke the preview waits.
    bool stale = !thumb.valid || thumb.epoch != staleEpoch_;
    if (!stale && !playback_.playing() && !gestureActive)
        stale = active ? thumb.revision != doc.revision() : thumb.live;
    if (stale) {
        try {
            const Image small = thumbnailOf(doc.compositeOfFrame(frame), kThumbMaxSide);
            if (!small.empty())
                thumb.mirror.sync(small, IRect{0, 0, small.width(), small.height()});
        } catch (const std::bad_alloc&) {
            // No preview this time; the cell still works.
        }
        thumb.valid = true;
        thumb.live = active;
        thumb.revision = doc.revision();
        thumb.epoch = staleEpoch_;
    }
    return thumb;
}

void TimelinePanel::pruneThumbs(std::size_t visible) {
    if (thumbs_.size() <= visible + 16)
        return;
    for (auto it = thumbs_.begin(); it != thumbs_.end();)
        it = it->second.used == drawCount_ ? std::next(it) : thumbs_.erase(it);
}

// --- the window ----------------------------------------------------------------------------------

void TimelinePanel::draw(Document& doc, bool gestureActive, Toasts& toasts) {
    // A frame in which the panel was not drawn (hidden, collapsed) may have missed edits: rebuild every thumbnail.
    const int imguiFrame = ImGui::GetFrameCount();
    if (lastDrawFrame_ < 0 || imguiFrame - lastDrawFrame_ > 1)
        ++staleEpoch_;
    lastDrawFrame_ = imguiFrame;
    ++drawCount_;

    const ImGuiStyle& style = ImGui::GetStyle();
    const ThemeTokens& t = tokens();
    const float em = ImGui::GetFontSize();
    const float frameHeight = ImGui::GetFrameHeight();
    const float gap = style.ItemSpacing.x;
    const int count = doc.frameCount();
    const int active = doc.activeFrame();
    const bool playing = playback_.playing();
    std::vector<Op> ops;

    // --- controls ---------------------------------------------------------------------------------
    // Items sit on one line and wrap to the next when the window is narrow.
    const float avail = ImGui::GetContentRegionAvail().x;
    float lineWidth = 0.0f;
    const auto place = [&](float width) {
        if (lineWidth > 0.0f && lineWidth + gap + width <= avail) {
            ImGui::SameLine();
            lineWidth += gap + width;
        } else {
            lineWidth = width;
        }
    };
    const auto buttonWidth = [&](const char* label) { return iconTextButtonWidth(label); };

    const char* playLabel = tr(Str::TimelinePlay);
    const char* pauseLabel = tr(Str::TimelinePause);
    const float playWidth = std::max(buttonWidth(playLabel), buttonWidth(pauseLabel));
    place(playWidth);
    ImGui::BeginDisabled(gestureActive || (!playing && count < 2));
    if (iconTextButton("##play", playing ? Icon::VideoPause : Icon::VideoPlay, playing ? pauseLabel : playLabel,
                       ImVec2(playWidth, 0.0f)))
        togglePlay(doc);
    ImGui::EndDisabled();

    // Loop is a toggle: pressed in while on.
    place(buttonWidth(tr(Str::TimelineLoop)));
    if (iconTextButton("##loop", Icon::VideoLoop, tr(Str::TimelineLoop), ImVec2(0.0f, 0.0f), loop_))
        loop_ = !loop_;
    ImGui::SetItemTooltip("%s", tr(Str::TimelineTipLoop));

    for (const CommandButton& b : kCommandButtons) {
        const char* label = tr(b.label);
        place(buttonWidth(label));
        ImGui::BeginDisabled(gestureActive || !canRunLayerCommand(doc, b.command));
        if (iconTextButton(label, b.icon, label)) {
            Op op{Op::Kind::Command};
            op.command = b.command;
            ops.push_back(std::move(op));
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tr(b.tip));
    }

    const std::string frameText = trf(Str::TimelineFrameOf, {std::to_string(active + 1), std::to_string(count)});
    place(ImGui::CalcTextSize(frameText.c_str()).x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(frameText.c_str());
    const std::string fpsText = trf(Str::TimelineFps, {fpsNumber(doc.frame(active).durationMs)});
    place(ImGui::CalcTextSize(fpsText.c_str()).x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(fpsText.c_str());
    ImGui::SetItemTooltip("%s", tr(Str::TimelineTipFps));

    // --- cells ------------------------------------------------------------------------------------
    const float pad = std::round(em * 0.35f);
    const float cellWidth = std::round(em * 6.4f);
    const float thumbBox = std::round(em * 4.2f);
    const float numberHeight = ImGui::GetTextLineHeight();
    const float cellHeight = pad + numberHeight + pad + thumbBox + pad + frameHeight + pad;
    const float barHeight = std::max(2.0f, std::round(3.0f * uiScale()));

    std::size_t visibleCount = 0;
    if (ImGui::BeginChild("##frames", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float viewWidth = std::max(1.0f, ImGui::GetWindowWidth() - style.ScrollbarSize);

        // Keep the shown frame in view after it changed (a click, the arrow shortcuts, playback, a new frame). A few
        // frames are tried because the strip may only have grown this frame.
        if (active != lastShownFrame_) {
            lastShownFrame_ = active;
            scrollTries_ = 3;
        }
        if (scrollTries_ > 0) {
            --scrollTries_;
            const float left = static_cast<float>(active) * cellWidth;
            const float right = left + cellWidth;
            const float scroll = ImGui::GetScrollX();
            if (left < scroll)
                ImGui::SetScrollX(left);
            else if (right > scroll + viewWidth)
                ImGui::SetScrollX(right - viewWidth);
        }

        // Sets the strip's width, so the horizontal scroll bar covers every frame.
        ImGui::SetCursorPos(ImVec2(static_cast<float>(count) * cellWidth - 1.0f, 0.0f));
        ImGui::Dummy(ImVec2(1.0f, 1.0f));

        const CellRange range = visibleCells(ImGui::GetScrollX(), viewWidth, cellWidth, count);
        visibleCount = static_cast<std::size_t>(range.end - range.first);
        for (int f = range.first; f < range.end; ++f) {
            const FrameInfo info = doc.frame(f);
            const bool isActive = f == active;
            ImGui::PushID(f);
            ImGui::SetCursorPos(ImVec2(static_cast<float>(f) * cellWidth, 0.0f));
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max(min.x + cellWidth, min.y + cellHeight);

            // The whole cell is the click and drag target; the duration field sits on top of it.
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("##cell", ImVec2(cellWidth, cellHeight));
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemActivated() && !gestureActive) {
                Op op{Op::Kind::Select};
                op.frame = f;
                ops.push_back(std::move(op));
            }
            if (hovered && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::GetMousePos().y < max.y - pad - frameHeight)
                ImGui::SetItemTooltip("%s", tr(Str::TimelineTipCell));

            // Reordering: drag a cell onto another; a line shows where it will land.
            if (!gestureActive && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                ImGui::SetDragDropPayload(kDragType, &f, sizeof(f));
                ImGui::TextUnformatted(std::to_string(f + 1).c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                const ImGuiDragDropFlags flags =
                    ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kDragType, flags)) {
                    int from = -1;
                    std::memcpy(&from, payload->Data, std::min<std::size_t>(sizeof(from), static_cast<std::size_t>(payload->DataSize)));
                    const bool right = ImGui::GetMousePos().x >= min.x + cellWidth * 0.5f;
                    if (from != f) {
                        const float x = right ? max.x : min.x;
                        dl->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), colorOf(t.accent), std::max(2.0f, barHeight));
                    }
                    if (payload->IsDelivery()) {
                        Op op{Op::Kind::Move};
                        op.frame = from;
                        op.target = f;
                        op.right = right;
                        ops.push_back(std::move(op));
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // Background: the shown frame in the accent, a hovered one in the theme's hover color.
            if (isActive) {
                dl->AddRectFilled(min, max, colorOf(t.accent, 0.30f));
                dl->AddRectFilled(min, ImVec2(max.x, min.y + barHeight), colorOf(t.accent));
            } else if (hovered) {
                dl->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_HeaderHovered, 0.5f));
            }
            dl->AddLine(ImVec2(max.x - 1.0f, min.y), ImVec2(max.x - 1.0f, max.y), ImGui::GetColorU32(ImGuiCol_Separator, 0.5f));

            // The frame number, then the thumbnail of its composite.
            const std::string number = std::to_string(f + 1);
            dl->AddText(ImVec2(std::round(min.x + pad), std::round(min.y + pad)), colorOf(t.text), number.c_str());

            std::uint64_t signature =
                mixHash(kHashSeed, (static_cast<std::uint64_t>(doc.image().width()) << 32) | static_cast<std::uint64_t>(doc.image().height()));
            for (int l = 0; l < doc.layerCount(); ++l) {
                const LayerInfo& layer = doc.layer(l);
                signature = mixHash(signature, doc.celId(l, f));
                signature = mixHash(signature, (static_cast<std::uint64_t>(layer.visible) << 8) | layer.opacity);
            }
            Thumb& thumb = thumbFor(doc, f, signature, isActive, gestureActive);
            drawThumbnail(dl, thumb.mirror,
                          ImVec2(std::round(min.x + (cellWidth - thumbBox) * 0.5f), std::round(min.y + pad + numberHeight + pad)),
                          thumbBox);

            // The duration: a drag or typed number that applies on release.
            ImGui::SetCursorScreenPos(ImVec2(std::round(min.x + pad), std::round(max.y - pad - frameHeight)));
            ImGui::SetNextItemWidth(cellWidth - 2.0f * pad);
            int value = editFrame_ == f ? editValue_ : info.durationMs;
            ImGui::BeginDisabled(gestureActive);
            ImGui::DragInt("##ms", &value, 1.0f, kMinFrameMs, kMaxFrameMs, "%d ms", ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("%s", tr(Str::TimelineTipDuration));
            if (ImGui::IsItemActive() || ImGui::IsItemEdited()) {
                editFrame_ = f;
                editValue_ = value;
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                Op op{Op::Kind::Duration};
                op.frame = f;
                op.ms = value;
                ops.push_back(std::move(op));
                editFrame_ = -1;
            } else if (ImGui::IsItemDeactivated()) {
                editFrame_ = -1;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    // --- apply what the user did ------------------------------------------------------------------
    if (editFrame_ >= count)
        editFrame_ = -1;
    for (const Op& op : ops) {
        // A gesture on the canvas owns the document until it ends.
        if (gestureActive)
            break;
        playback_.stop(); // every change here ends playback
        switch (op.kind) {
        case Op::Kind::Select:
            setActiveFrame(doc, op.frame, toasts);
            break;
        case Op::Kind::Move: {
            const int to = frameDropIndex(op.frame, op.target, op.right);
            if (op.frame != to && doc.moveFrame(op.frame, to) == Document::OpResult::NoMemory)
                reportNoMemory(toasts);
            break;
        }
        case Op::Kind::Duration:
            if (doc.setFrameDuration(op.frame, op.ms) == Document::OpResult::NoMemory)
                reportNoMemory(toasts);
            break;
        case Op::Kind::Command:
            runLayerCommand(doc, op.command, toasts);
            break;
        }
    }
    pruneThumbs(visibleCount);
}

} // namespace trace
