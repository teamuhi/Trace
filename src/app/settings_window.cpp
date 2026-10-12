#include "app/settings_window.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string_view>

#include "io/path_utf8.hpp"

#include "app/keybinds.hpp"
#include "app/languages.hpp"
#include "app/native_dialogs.hpp"
#include "app/theme_io.hpp"
#include "app/pattern_draw.hpp"
#include "app/theme.hpp"
#include "core/action_text.hpp"
#include "core/i18n.hpp"
#include "tools/tool_text.hpp"
#include "tools/toolbox.hpp"


namespace trace {

namespace {

constexpr float kDefaultWidth = 760.0f;
constexpr float kDefaultHeight = 480.0f;
constexpr float kViewportFill = 0.9f; // never open larger than this share of the viewport

constexpr std::array<Str, kSettingsPageCount> kPageNames = {
    Str::SettingsPageGeneral, Str::SettingsPageTheme,    Str::SettingsPageView,  Str::SettingsPageTools,
    Str::SettingsPageScripts, Str::SettingsPageKeybinds, Str::SettingsPageAbout};
constexpr std::array<const char*, 5> kGroups = {"File", "Edit", "Image", "View", "Tools"};
constexpr std::array<int, 5> kScalePresets = {75, 100, 125, 150, 200};

// Visible titles are translated; the part after ### keeps the window's and popup's identity (and so its saved
// position) the same in every language.
std::string settingsTitle() {
    return std::string(tr(Str::SettingsTitle)) + "###Settings";
}
std::string restoreTitle() {
    return std::string(tr(Str::SettingsRestoreTitle)) + "###RestoreDefaults";
}

ImVec2 scaled(float x, float y) {
    return ImVec2(x * uiScale(), y * uiScale());
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

// A color edit that keeps its float state between frames, so dragging through gray does not lose the hue
// to 8-bit rounding. Follows `target` when something else changes it.
bool colorEdit(const char* label, float rgba[4], Rgba8& target) {
    if (fromFloats(rgba) != target)
        toFloats(target, rgba);
    if (ImGui::ColorEdit4(label, rgba, ImGuiColorEditFlags_AlphaPreviewHalf)) {
        target = fromFloats(rgba);
        return true;
    }
    return false;
}

// The label of token `index`, in the order of tokenInfos().
Str tokenString(std::size_t index) {
    static constexpr Str kLabels[kTokenCount] = {
        Str::TokenAccent,        Str::TokenButton,      Str::TokenText,        Str::TokenWindowBg,
        Str::TokenStatusBarBg,   Str::TokenCanvasBackdrop, Str::TokenPattern,  Str::TokenCheckerA,
        Str::TokenCheckerB,      Str::TokenCanvasBorder, Str::TokenPixelGrid,  Str::TokenRulerBg,
        Str::TokenRulerText,     Str::TokenGuide,       Str::TokenSelection,   Str::TokenWarning};
    return kLabels[index < static_cast<std::size_t>(kTokenCount) ? index : 0];
}

Rgba8 fromVec4(const ImVec4& v) {
    const float rgba[4] = {v.x, v.y, v.z, v.w};
    return fromFloats(rgba);
}

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Percent-encodes everything except unreserved characters and the path separators.
std::string fileUrl(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    std::string url = !path.empty() && path.front() == '/' ? "file://" : "file:///";
    for (const char c : path) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~') {
            url += c;
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", u);
            url += buf;
        }
    }
    return url;
}

void labeledText(const char* label, const char* fmt, ...) IM_FMTARGS(2);
void labeledText(const char* label, const char* fmt, ...) {
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(ImGui::GetFontSize() * 9.0f);
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
}

} // namespace

void SettingsWindow::open() {
    open_ = true;
    editInit_ = false;
    focusRequested_ = true;
}

void SettingsWindow::startCapture(SlotRef target) {
    endCapture();
    capture_ = target;
    ImGuiIO& io = ImGui::GetIO();
    // Arrow keys, Tab and Enter would otherwise move focus while the user is trying to bind them.
    navWasEnabled_ = (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    setCapturing(true);
}

void SettingsWindow::endCapture() {
    if (capture_ && navWasEnabled_ && ImGui::GetCurrentContext())
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    capture_.reset();
    conflict_.reset();
    setCapturing(false);
}

bool SettingsWindow::draw(Settings& settings, Toolbox& toolbox, const std::string& settingsPath, NativeDialogs& dialogs,
                          LanguageManager& languages) {
    dialogs_ = &dialogs;
    languages_ = &languages;
    if (!open_) {
        endCapture(); // closed by the title bar button or File > Settings
        wasOpen_ = false;
        return false;
    }
    if (!wasOpen_)
        languages.refresh(); // packs may have been added to the folder since the last time
    wasOpen_ = true;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 size(std::min(kDefaultWidth * uiScale(), viewport->Size.x * kViewportFill),
                      std::min(kDefaultHeight * uiScale(), viewport->Size.y * kViewportFill));
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(scaled(480.0f, 320.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (focusRequested_) {
        ImGui::SetNextWindowFocus();
        focusRequested_ = false;
    }

    bool changed = false;
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin(settingsTitle().c_str(), &open_, flags)) {
        // Losing focus (alt-tab, clicking the canvas) abandons a pending key capture.
        if ((capture_ || conflict_) && !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            endCapture();

        const ImGuiStyle& style = ImGui::GetStyle();
        const float footer = ImGui::GetFrameHeight() + style.ItemSpacing.y * 2.0f;
        settings.lastPage = std::clamp(settings.lastPage, 0, kSettingsPageCount - 1);

        if (ImGui::BeginChild("##categories", ImVec2(ImGui::GetFontSize() * 9.0f, -footer),
                              ImGuiChildFlags_Borders)) {
            for (int i = 0; i < kSettingsPageCount; ++i) {
                if (ImGui::Selectable(tr(kPageNames[static_cast<std::size_t>(i)]), settings.lastPage == i) &&
                    settings.lastPage != i) {
                    settings.lastPage = i;
                    endCapture();
                    changed = true;
                }
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();
        if (ImGui::BeginChild("##page", ImVec2(0.0f, -footer), ImGuiChildFlags_Borders)) {
            switch (settings.lastPage) {
            case 0:
                changed |= drawGeneral(settings);
                break;
            case 1:
                changed |= drawTheme(settings);
                break;
            case 2:
                changed |= drawView(settings);
                break;
            case 3:
                changed |= drawTools(settings, toolbox);
                break;
            case 4:
                drawScripts();
                break;
            case 5:
                changed |= drawKeybinds(settings);
                break;
            default:
                drawAbout(settingsPath);
                break;
            }
        }
        ImGui::EndChild();

        if (ImGui::Button(tr(Str::SettingsRestoreDefaults)))
            ImGui::OpenPopup(restoreTitle().c_str());
        ImGui::SameLine();
        if (ImGui::Button(tr(Str::WordClose)))
            open_ = false;

        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal(restoreTitle().c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(tr(Str::SettingsRestoreQuestion));
            if (ImGui::Button(tr(Str::SettingsRestore))) {
                const int page = settings.lastPage;
                settings = Settings::defaults();
                settings.lastPage = page;
                endCapture();
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(tr(Str::WordCancel)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    ImGui::End();

    if (!open_)
        endCapture();
    return changed;
}

bool SettingsWindow::drawGeneral(Settings& s) {
    bool changed = false;
    char preview[16];
    if (s.uiScalePercent == 0)
        std::snprintf(preview, sizeof(preview), "%s", tr(Str::WordAuto));
    else
        std::snprintf(preview, sizeof(preview), "%d%%", s.uiScalePercent);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    if (ImGui::BeginCombo(tr(Str::GeneralInterfaceScale), preview)) {
        if (ImGui::Selectable(tr(Str::WordAuto), s.uiScalePercent == 0)) {
            s.uiScalePercent = 0;
            changed = true;
        }
        for (const int percent : kScalePresets) {
            char label[16];
            std::snprintf(label, sizeof(label), "%d%%", percent);
            if (ImGui::Selectable(label, s.uiScalePercent == percent)) {
                s.uiScalePercent = percent;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", tr(Str::GeneralScaleHint));

    ImGui::SeparatorText(tr(Str::GeneralFont));
    {
        const std::string customLabel =
            s.customFontPath.empty()
                ? std::string(tr(Str::GeneralFontCustom))
                : trf(Str::GeneralFontCustomNamed, {utf8FromPath(pathFromUtf8(s.customFontPath).filename())});
        const char* classicLabel = tr(Str::GeneralFontClassic);
        const auto label = [&](int i) -> const char* {
            switch (static_cast<FontChoice>(i)) {
            case FontChoice::Classic:
                return classicLabel;
            case FontChoice::Inter:
                return "Inter"; // font names are not translated
            case FontChoice::Mono:
                return "JetBrains Mono";
            case FontChoice::Custom:
                return customLabel.c_str();
            }
            return "";
        };

        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
        if (ImGui::BeginCombo(tr(Str::GeneralInterfaceFont), label(static_cast<int>(s.font)))) {
            for (int i = 0; i < kFontChoiceCount; ++i) {
                const bool isCustom = i == static_cast<int>(FontChoice::Custom);
                if (isCustom && s.customFontPath.empty())
                    continue; // nothing to pick until a file has been loaded
                if (ImGui::Selectable(label(i), static_cast<int>(s.font) == i)) {
                    s.font = static_cast<FontChoice>(i);
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(dialogs_ == nullptr || dialogs_->busy());
        if (ImGui::Button(tr(Str::GeneralLoadFont)) && dialogs_)
            dialogs_->open(NativeDialogs::Tag::FontFile);
        ImGui::EndDisabled();

        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
        changed |= ImGui::SliderInt(tr(Str::GeneralFontSize), &s.fontSize, kMinFontSize, kMaxFontSize, "%d px",
                                    ImGuiSliderFlags_AlwaysClamp);
        ImGui::TextDisabled("%s", tr(Str::GeneralFontSample));
        ImGui::TextDisabled("%s", tr(Str::GeneralFontHint));
    }

    ImGui::SeparatorText(tr(Str::GeneralLanguage));
    if (languages_) {
        const auto& packs = languages_->available();
        std::string current = tr(Str::GeneralLanguageEnglish);
        if (!s.language.empty()) {
            current = s.language;
            for (const auto& pack : packs) {
                if (pack.info.code == s.language)
                    current = pack.info.name;
            }
        }
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
        if (ImGui::BeginCombo(tr(Str::GeneralLanguageLabel), current.c_str())) {
            if (ImGui::Selectable(tr(Str::GeneralLanguageEnglish), s.language.empty())) {
                s.language.clear();
                changed = true;
            }
            for (const auto& pack : packs) {
                const std::string label = pack.info.name + " (" + pack.info.code + ")";
                if (ImGui::Selectable(label.c_str(), s.language == pack.info.code)) {
                    s.language = pack.info.code;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(dialogs_ == nullptr || dialogs_->busy());
        if (ImGui::Button(tr(Str::GeneralImportLanguage)) && dialogs_)
            dialogs_->open(NativeDialogs::Tag::LanguageImport);
        ImGui::SameLine();
        if (ImGui::Button(tr(Str::GeneralExportTemplate)) && dialogs_)
            dialogs_->save(NativeDialogs::Tag::LanguageTemplate, "trace-language-template.ini");
        ImGui::EndDisabled();

        if (const LanguagePack* active = languages_->active()) {
            if (!active->info.author.empty())
                ImGui::TextDisabled("%s", trf(Str::GeneralLanguageAuthor, {active->info.author}).c_str());
            ImGui::TextDisabled("%s", trf(Str::GeneralLanguageCoverage,
                                          {std::to_string(active->translated), std::to_string(kStrCount)})
                                          .c_str());
        }
        ImGui::TextDisabled("%s", tr(Str::GeneralLanguageHint));
    }

    ImGui::SeparatorText(tr(Str::GeneralNewCanvas));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    changed |= ImGui::InputInt(tr(Str::WordWidthPx), &s.newWidth, 10, 100);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    changed |= ImGui::InputInt(tr(Str::WordHeightPx), &s.newHeight, 10, 100);
    s.newWidth = std::clamp(s.newWidth, kMinCanvasSize, kMaxCanvasSize);
    s.newHeight = std::clamp(s.newHeight, kMinCanvasSize, kMaxCanvasSize);
    ImGui::TextDisabled("%s", trf(Str::GeneralNewCanvasHint, {std::to_string(kMinCanvasSize), std::to_string(kMaxCanvasSize)}).c_str());

    ImGui::SeparatorText(tr(Str::GeneralUndoHistory));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    changed |= ImGui::InputInt(tr(Str::GeneralMemoryLimit), &s.historyLimitMb, 64, 256);
    s.historyLimitMb = std::clamp(s.historyLimitMb, kMinHistoryLimitMb, kMaxHistoryLimitMb);
    ImGui::TextDisabled("%s", tr(Str::GeneralHistoryHint));
    return changed;
}

namespace {

struct AccentPreset {
    Str name;
    Rgba8 dark;  // lighter, for dark windows
    Rgba8 light; // deeper, for light windows
};

constexpr AccentPreset kAccentPresets[] = {
    {Str::AccentBlue, {66, 133, 244, 255}, {25, 103, 210, 255}},
    {Str::AccentTeal, {38, 166, 154, 255}, {0, 121, 107, 255}},
    {Str::AccentGreen, {76, 175, 80, 255}, {46, 125, 50, 255}},
    {Str::AccentOrange, {255, 152, 0, 255}, {216, 108, 0, 255}},
    {Str::AccentRed, {239, 83, 80, 255}, {198, 40, 40, 255}},
    {Str::AccentPink, {236, 64, 122, 255}, {194, 24, 91, 255}},
    {Str::AccentPurple, {171, 71, 188, 255}, {123, 31, 162, 255}},
    {Str::AccentSlate, {144, 164, 174, 255}, {84, 110, 122, 255}},
};

} // namespace

bool SettingsWindow::drawTheme(Settings& s) {
    bool changed = false;
    if (!editInit_) {
        editDark_ = isDark();
        editInit_ = true;
    }

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    static constexpr Str kModeLabels[] = {Str::WordSystem, Str::WordLight, Str::WordDark};
    if (ImGui::BeginCombo(tr(Str::ThemeAppearance), tr(kModeLabels[static_cast<int>(s.theme)]))) {
        for (int i = 0; i < 3; ++i) {
            if (ImGui::Selectable(tr(kModeLabels[i]), static_cast<int>(s.theme) == i)) {
                s.theme = static_cast<ThemeMode>(i);
                editDark_ = resolveDark(s.theme);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", tr(Str::ThemeSystemHint));

    // Accent presets recolor both palettes at once.
    ImGui::SeparatorText(tr(Str::ThemeAccentColor));
    const float swatch = ImGui::GetFrameHeight() * 1.2f;
    int index = 0;
    for (const AccentPreset& preset : kAccentPresets) {
        if (index++ > 0)
            ImGui::SameLine();
        ImGui::PushID(index);
        if (ImGui::ColorButton("##accent", toImVec4(editDark_ ? preset.dark : preset.light),
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha, ImVec2(swatch, swatch))) {
            const std::size_t accentIndex = 0; // "accent" is the first token
            s.themeOverrides.dark[accentIndex] = toImVec4(preset.dark);
            s.themeOverrides.light[accentIndex] = toImVec4(preset.light);
            changed = true;
        }
        ImGui::SetItemTooltip("%s", tr(preset.name));
        ImGui::PopID();
    }

    ImGui::SeparatorText(tr(Str::ThemeColors));
    ImGui::TextUnformatted(tr(Str::ThemeEditingThe));
    ImGui::SameLine();
    int which = editDark_ ? 1 : 0;
    ImGui::RadioButton(tr(Str::WordDark), &which, 1);
    ImGui::SameLine();
    ImGui::RadioButton(tr(Str::WordLight), &which, 0);
    editDark_ = which == 1;

    // A live strip drawn with the palette being edited, whichever one is applied right now.
    const ThemeTokens preview = resolveTokens(editDark_, s.themeOverrides);
    {
        const float height = ImGui::GetFontSize() * 3.2f;
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 size(ImGui::GetContentRegionAvail().x, height);
        ImGui::Dummy(size);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const auto col = [](const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); };
        const float unit = ImGui::GetFontSize();
        const ImVec2 max(min.x + size.x, min.y + size.y);
        dl->AddRectFilled(min, max, col(preview.windowBg), 4.0f);
        dl->AddRect(min, max, col(preview.canvasBorder), 4.0f);
        // canvas, with a guide and a selection on it
        const ImVec2 cMin(min.x + size.x * 0.52f, min.y + unit * 0.4f);
        const ImVec2 cMax(max.x - unit * 0.4f, max.y - unit * 0.4f);
        dl->AddRectFilled(cMin, cMax, col(preview.canvasBackdrop));
        dl->AddRectFilled(ImVec2(cMin.x + unit * 0.5f, cMin.y + unit * 0.4f), ImVec2(cMax.x - unit * 0.5f, cMax.y - unit * 0.4f),
                          col(preview.checkerA));
        dl->AddLine(ImVec2(cMin.x + unit * 2.0f, cMin.y), ImVec2(cMin.x + unit * 2.0f, cMax.y), col(preview.guide));
        dl->AddRect(ImVec2(cMin.x + unit * 2.6f, cMin.y + unit * 0.9f), ImVec2(cMin.x + unit * 4.4f, cMax.y - unit * 0.9f),
                    col(preview.selection));
        // a button, an accent button, text
        const ImVec2 b1(min.x + unit * 0.6f, min.y + unit * 0.5f);
        dl->AddRectFilled(b1, ImVec2(b1.x + unit * 3.4f, b1.y + unit * 1.3f), col(preview.button), 4.0f);
        dl->AddText(ImVec2(b1.x + unit * 0.5f, b1.y + unit * 0.2f), col(preview.text), tr(Str::ThemePreviewButton));
        const ImVec2 b2(b1.x + unit * 3.8f, b1.y);
        dl->AddRectFilled(b2, ImVec2(b2.x + unit * 3.4f, b2.y + unit * 1.3f), col(preview.accent), 4.0f);
        dl->AddText(ImVec2(b2.x + unit * 0.5f, b2.y + unit * 0.2f), col(contrastText(preview.accent)), tr(Str::ThemePreviewSelected));
        dl->AddText(ImVec2(b1.x, b1.y + unit * 1.6f), col(preview.warning), tr(Str::ThemePreviewWarning));
        dl->AddText(ImVec2(b1.x + unit * 4.2f, b1.y + unit * 1.6f), col(preview.text), tr(Str::ThemePreviewText));
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(dialogs_ == nullptr || dialogs_->busy());
    if (ImGui::Button(tr(Str::ThemeImport)) && dialogs_)
        dialogs_->open(NativeDialogs::Tag::ThemeImport);
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::ThemeExport)) && dialogs_)
        dialogs_->save(NativeDialogs::Tag::ThemeExport, "trace-theme.ini");
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", tr(Str::ThemeExportTip));

    ImGui::Spacing();
    const std::span<const TokenInfo> infos = tokenInfos();
    auto& overrides = s.themeOverrides.side(editDark_);
    const ThemeTokens& base = baseTokens(editDark_);
    for (std::size_t i = 0; i < infos.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const ImVec4 builtIn = base.*(infos[i].member);
        Rgba8 current = fromVec4(overrides[i].value_or(builtIn));
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
        if (colorEdit(tr(tokenString(i)), themeRows_[i], current)) {
            overrides[i] = toImVec4(current);
            changed = true;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!overrides[i]);
        if (ImGui::SmallButton(tr(Str::WordReset))) {
            overrides[i].reset();
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    ImGui::Spacing();
    const bool anyEdited = std::any_of(overrides.begin(), overrides.end(), [](const auto& v) { return v.has_value(); });
    ImGui::BeginDisabled(!anyEdited);
    if (ImGui::Button(tr(editDark_ ? Str::ThemeResetDark : Str::ThemeResetLight))) {
        overrides = {};
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(s.themeOverrides.empty());
    if (ImGui::Button(tr(Str::ThemeResetBoth))) {
        s.themeOverrides = ThemeOverrides{};
        changed = true;
    }
    ImGui::EndDisabled();
    return changed;
}

namespace {

constexpr Str kPatternLabels[kPatternStyleCount] = {Str::PatternSolid, Str::PatternChecker,  Str::PatternGrid,
                                                    Str::PatternDots,  Str::PatternDiagonal, Str::PatternCross};

// A combo of pattern styles with a small preview of the chosen one, drawn with the real pattern code and
// the colors of the applied theme.
bool patternCombo(Str labelText, PatternStyle& style, bool transparency, const Settings& s) {
    bool changed = false;
    const float swatch = ImGui::GetFrameHeight();
    const ThemeTokens& t = tokens();
    const auto col = [](const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); };
    const ImU32 base = col(transparency ? t.checkerA : t.canvasBackdrop);
    const ImU32 marks = col(transparency ? t.checkerB : t.pattern);

    ImGui::PushID(stringKey(labelText));
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(swatch, swatch));
    drawPattern(ImGui::GetWindowDrawList(), style, min, ImVec2(min.x + swatch, min.y + swatch), min,
                std::max(6.0f, static_cast<float>(s.patternSpacing) * 0.5f), base, marks, marks);
    ImGui::GetWindowDrawList()->AddRect(min, ImVec2(min.x + swatch, min.y + swatch), col(t.canvasBorder));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    if (ImGui::BeginCombo(tr(labelText), tr(kPatternLabels[static_cast<int>(style)]))) {
        for (int i = 0; i < kPatternStyleCount; ++i) {
            if (ImGui::Selectable(tr(kPatternLabels[i]), static_cast<int>(style) == i)) {
                style = static_cast<PatternStyle>(i);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}

} // namespace

bool SettingsWindow::drawView(Settings& s) {
    bool changed = false;
    changed |= ImGui::Checkbox(tr(Str::ViewPixelGrid), &s.pixelGrid);
    changed |= ImGui::Checkbox(tr(Str::ViewRulers), &s.showRulers);
    changed |= ImGui::Checkbox(tr(Str::ViewGuides), &s.showGuides);
    ImGui::BeginDisabled(!s.showGuides);
    changed |= ImGui::Checkbox(tr(Str::ViewSnapToGuides), &s.snapToGuides);
    ImGui::EndDisabled();
    changed |= ImGui::Checkbox(tr(Str::ViewToolsPanel), &s.showToolsPanel);
    changed |= ImGui::Checkbox(tr(Str::ViewStatusBar), &s.showStatusBar);
    ImGui::BeginDisabled(!s.showStatusBar);
    changed |= ImGui::Checkbox(tr(Str::ViewSysStats), &s.showSysStats);
    ImGui::EndDisabled();

    ImGui::SeparatorText(tr(Str::ViewBackgrounds));
    changed |= patternCombo(Str::ViewBehindImage, s.backdropPattern, false, s);
    changed |= patternCombo(Str::ViewBehindTransparent, s.transparencyPattern, true, s);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    changed |= ImGui::SliderInt(tr(Str::ViewPatternSpacing), &s.patternSpacing, kMinPatternSpacing, kMaxPatternSpacing, "%d px",
                                ImGuiSliderFlags_AlwaysClamp);

    ImGui::SeparatorText(tr(Str::ViewNewCanvasOpensAt));
    int zoom = static_cast<int>(s.newDocZoom);
    changed |= ImGui::RadioButton(tr(Str::ViewFitToWindow), &zoom, static_cast<int>(Settings::NewDocZoom::Fit));
    changed |= ImGui::RadioButton("100%", &zoom, static_cast<int>(Settings::NewDocZoom::ActualSize));
    s.newDocZoom = static_cast<Settings::NewDocZoom>(zoom);
    return changed;
}

bool SettingsWindow::drawTools(Settings& s, Toolbox& toolbox) {
    bool changed = false;
    changed |= ImGui::Checkbox(tr(Str::ToolsRestoreLast), &s.restoreLastToolSettings);
    ImGui::TextDisabled("%s", tr(Str::ToolsRestoreHint));

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    if (ImGui::BeginCombo(tr(Str::ToolsStartTool), tr(toolString(s.startTool)))) {
        for (int i = 0; i < kToolCount; ++i) {
            const ToolId id = static_cast<ToolId>(i);
            if (ImGui::Selectable(tr(toolString(id)), s.startTool == id)) {
                s.startTool = id;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText(tr(Str::ToolsStartingValues));
    ImGui::BeginDisabled(s.restoreLastToolSettings);
    ToolSettings& t = s.toolDefaults;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    changed |= ImGui::SliderInt(tr(Str::ToolsBrushSize), &t.brushSize, ToolSettings::kMinSize, ToolSettings::kMaxSize, "%d px",
                                ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    changed |= ImGui::SliderInt(tr(Str::ToolsStrokeWidth), &t.strokeWidth, ToolSettings::kMinSize,
                                ToolSettings::kMaxStrokeWidth, "%d px", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::Checkbox(tr(Str::WordAntialias), &t.antialias);
    changed |= ImGui::Checkbox(tr(Str::ToolsEraserTransparent), &t.eraseToTransparent);

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    int mode = static_cast<int>(t.shapeMode);
    static constexpr Str kShapeLabels[] = {Str::WordOutline, Str::WordFilled, Str::WordOutlineAndFill};
    if (ImGui::BeginCombo(tr(Str::ToolsShapeMode), tr(kShapeLabels[mode]))) {
        for (int i = 0; i < 3; ++i) {
            if (ImGui::Selectable(tr(kShapeLabels[i]), mode == i)) {
                t.shapeMode = static_cast<ShapeMode>(i);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    changed |= ImGui::SliderInt(tr(Str::ToolsFillTolerance), &t.fillTolerance, 0, 255, "%d", ImGuiSliderFlags_AlwaysClamp);

    changed |= colorEdit(tr(Str::WordPrimaryColor), primary_, t.primary);
    changed |= colorEdit(tr(Str::WordSecondaryColor), secondary_, t.secondary);

    if (ImGui::Button(tr(Str::ToolsUseCurrent))) {
        t = toolbox.settings();
        changed = true;
    }
    ImGui::SetItemTooltip("%s", tr(Str::ToolsUseCurrentTip));
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::ToolsApplyNow)))
        toolbox.settings() = t;
    ImGui::SetItemTooltip("%s", tr(Str::ToolsApplyNowTip));
    ImGui::EndDisabled();
    return changed;
}

void SettingsWindow::drawScripts() {
    ImGui::TextUnformatted(tr(Str::ScriptsPlanned));
}

bool SettingsWindow::pollKeyCapture(Keymap& keymap) {
    if (!capture_)
        return false;

    Chord chord;
    switch (pollCapture(chord)) {
    case CaptureResult::None:
        return false;
    case CaptureResult::Cancelled:
        endCapture();
        return false;
    case CaptureResult::Captured:
        break;
    }

    const SlotRef target = *capture_;
    if (keymap.slots(target.action)[static_cast<std::size_t>(target.slot)] == chord) {
        endCapture();
        return false;
    }
    if (const auto owner = keymap.findConflict(chord, target.action)) {
        // Hold the decision until the user picks Replace or Cancel; shortcuts stay suspended meanwhile.
        const Conflict conflict{target, chord, *owner};
        endCapture();
        conflict_ = conflict;
        setCapturing(true);
        return false;
    }
    const bool applied = keymap.set(target.action, target.slot, chord);
    endCapture();
    return applied;
}

bool SettingsWindow::drawConflictBanner(Keymap& keymap) {
    if (!conflict_)
        return false;

    bool changed = false;
    const Conflict conflict = *conflict_;
    ImGui::PushStyleColor(ImGuiCol_Text, tokens().warning);
    ImGui::TextWrapped("%s", trf(Str::KeysConflict, {displayChord(conflict.chord), tr(actionString(conflict.owner))}).c_str());
    ImGui::PopStyleColor();

    if (ImGui::Button(tr(Str::WordReplace))) {
        const Keymap::Slots& ownerSlots = keymap.slots(conflict.owner);
        for (int i = 0; i < kChordSlots; ++i) {
            if (ownerSlots[static_cast<std::size_t>(i)] == conflict.chord)
                keymap.set(conflict.owner, i, std::nullopt);
        }
        changed = keymap.set(conflict.target.action, conflict.target.slot, conflict.chord);
        endCapture();
    } else {
        ImGui::SameLine();
        if (ImGui::Button(tr(Str::WordCancel)))
            endCapture();
    }
    ImGui::Spacing();
    return changed;
}

bool SettingsWindow::drawKeySlot(Keymap& keymap, Action action, int slot) {
    bool changed = false;
    const auto& chord = keymap.slots(action)[static_cast<std::size_t>(slot)];
    const bool listening = capture_ && capture_->action == action && capture_->slot == slot;

    ImGui::PushID(slot);
    const float clearWidth = ImGui::GetFrameHeight();
    const bool showClear = chord && !listening;
    const float width = ImGui::GetContentRegionAvail().x -
                        (showClear ? clearWidth + ImGui::GetStyle().ItemSpacing.x : 0.0f);

    std::string label = listening ? std::string(tr(Str::KeysPressKeys))
                        : chord   ? displayChord(*chord)
                                  : std::string(tr(Str::KeysUnbound));
    label += "##slot";
    if (listening)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
    else if (!chord)
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (ImGui::Button(label.c_str(), ImVec2(width, 0.0f)))
        startCapture(SlotRef{action, slot});
    if (listening || !chord)
        ImGui::PopStyleColor();
    if (chord && ImGui::IsItemClicked(ImGuiMouseButton_Right) && !listening) {
        keymap.set(action, slot, std::nullopt);
        changed = true;
    }

    if (showClear && chord) {
        ImGui::SameLine();
        if (ImGui::Button("x", ImVec2(clearWidth, 0.0f))) {
            keymap.set(action, slot, std::nullopt);
            changed = true;
        }
        ImGui::SetItemTooltip("%s", tr(Str::KeysClearTip));
    }
    ImGui::PopID();
    return changed;
}

bool SettingsWindow::drawKeybinds(Settings& s) {
    Keymap& keymap = s.keymap;
    bool changed = pollKeyCapture(keymap); // before any button, so the click that starts a capture is not read

    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 9.0f,
                                     ImGui::GetFontSize() * 6.0f));
    ImGui::InputTextWithHint("##filter", tr(Str::KeysFilter), filter_, sizeof(filter_));
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::KeysResetAll))) {
        keymap.resetToDefaults();
        endCapture();
        changed = true;
    }

    changed |= drawConflictBanner(keymap);

    const std::string needle = lower(filter_);
    const auto matches = [&](Action action) {
        if (needle.empty())
            return true;
        const ActionInfo& info = actionInfo(action);
        std::string haystack = lower(info.label) + ' ' + lower(info.group) + ' ' + lower(tr(actionString(action))) + ' ' +
                               lower(tr(groupString(info.group)));
        for (const Chord& chord : keymap.chords(action))
            haystack += ' ' + lower(formatChord(chord));
        return haystack.find(needle) != std::string::npos;
    };

    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                           ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##keybinds", 4, tableFlags)) {
        ImGui::TableSetupColumn(tr(Str::KeysColumnAction), ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn(tr(Str::KeysColumnShortcut), ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn(tr(Str::KeysColumnAlternate), ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn(tr(Str::WordReset), ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.5f);
        ImGui::TableHeadersRow();

        bool any = false;
        for (const char* group : kGroups) {
            bool headerDrawn = false;
            for (int i = 0; i < kActionCount; ++i) {
                const Action action = static_cast<Action>(i);
                const ActionInfo& info = actionInfo(action);
                if (std::string_view(info.group) != group || !matches(action))
                    continue;
                any = true;

                if (!headerDrawn) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%s", tr(groupString(group)));
                    headerDrawn = true;
                }

                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(tr(actionString(action)));
                ImGui::TableSetColumnIndex(1);
                changed |= drawKeySlot(keymap, action, 0);
                ImGui::TableSetColumnIndex(2);
                changed |= drawKeySlot(keymap, action, 1);
                ImGui::TableSetColumnIndex(3);
                ImGui::BeginDisabled(keymap.isDefault(action));
                if (ImGui::Button(tr(Str::WordReset))) {
                    keymap.reset(action);
                    endCapture();
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
        if (!any)
            ImGui::TextDisabled("%s", tr(Str::KeysNoMatch));
    }
    ImGui::TextDisabled("%s", tr(Str::KeysHelp));
    return changed;
}

void SettingsWindow::drawAbout(const std::string& settingsPath) {
    ImGui::TextUnformatted("Trace");
    ImGui::TextDisabled("%s", tr(Str::AboutTagline));
    ImGui::Spacing();

#ifdef NDEBUG
    constexpr const char* build = "Release";
#else
    constexpr const char* build = "Debug";
#endif
    const int sdl = SDL_GetVersion();
    labeledText(tr(Str::AboutVersion), "%s", TRACE_VERSION);
    labeledText(tr(Str::AboutBuild), "%s", build);
    labeledText(tr(Str::AboutPlatform), "%s", SDL_GetPlatform());
    labeledText("SDL", "%d.%d.%d", SDL_VERSIONNUM_MAJOR(sdl), SDL_VERSIONNUM_MINOR(sdl),
                SDL_VERSIONNUM_MICRO(sdl));
    labeledText("Dear ImGui", "%s", IMGUI_VERSION);
    labeledText(tr(Str::AboutLicense), "MIT");

    ImGui::SeparatorText(tr(Str::AboutSettingsFile));
    if (settingsPath.empty()) {
        ImGui::TextWrapped("%s", tr(Str::AboutNoDataDir));
        return;
    }
    ImGui::TextWrapped("%s", settingsPath.c_str());
    if (ImGui::Button(tr(Str::AboutOpenFolder))) {
        const std::string folder = std::filesystem::path(settingsPath).parent_path().string();
        if (!SDL_OpenURL(fileUrl(folder).c_str()))
            SDL_Log("SDL_OpenURL failed: %s", SDL_GetError());
    }
}

} // namespace trace
