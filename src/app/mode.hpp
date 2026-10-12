#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "core/i18n.hpp"
#include "core/keymap.hpp"

namespace trace {

// The three ways of working in Trace. Each has its own workspace (document, windows and dock layout); only
// one is shown at a time.
enum class Mode { Paint, Flowchart, PixelArt };
inline constexpr int kModeCount = 3;

// The order of the switcher in the menu bar.
inline constexpr std::array<Mode, kModeCount> kAllModes = {Mode::Paint, Mode::Flowchart, Mode::PixelArt};

// The name stored in settings.ini ([ui] mode).
inline constexpr const char* modeKey(Mode mode) noexcept {
    switch (mode) {
    case Mode::Paint:
        return "paint";
    case Mode::Flowchart:
        return "flowchart";
    case Mode::PixelArt:
        return "pixelart";
    }
    return "paint";
}

inline std::optional<Mode> modeFromKey(std::string_view key) noexcept {
    for (const Mode mode : kAllModes) {
        if (key == modeKey(mode))
            return mode;
    }
    return std::nullopt;
}

inline Str modeString(Mode mode) noexcept {
    switch (mode) {
    case Mode::Paint:
        return Str::ModeNamePaint;
    case Mode::Flowchart:
        return Str::ModeNameFlowchart;
    case Mode::PixelArt:
        return Str::ModeNamePixelArt;
    }
    return Str::ModeNamePaint;
}

// The keymap context that is live while `mode` is shown.
inline constexpr ModeContext modeContext(Mode mode) noexcept {
    switch (mode) {
    case Mode::Paint:
        return ModeContext::Paint;
    case Mode::Flowchart:
        return ModeContext::Flow;
    case Mode::PixelArt:
        return ModeContext::Pixel;
    }
    return ModeContext::Paint;
}

// The action that switches to `mode`.
inline constexpr Action modeAction(Mode mode) noexcept {
    switch (mode) {
    case Mode::Paint:
        return Action::ModePaint;
    case Mode::Flowchart:
        return Action::ModeFlowchart;
    case Mode::PixelArt:
        return Action::ModePixelArt;
    }
    return Action::ModePaint;
}

} // namespace trace
