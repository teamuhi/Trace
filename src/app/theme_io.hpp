#pragma once

#include <string>

#include "app/theme.hpp"

namespace trace {

class IniFile;

// Theme colors in ini form: [theme.dark] and [theme.light], `key = #RRGGBBAA`, only for colors the user
// changed. Used by settings.ini and by theme files, so a theme exported from one machine imports on another.
void readThemeOverrides(const IniFile& ini, ThemeOverrides& out);
void writeThemeOverrides(IniFile& ini, const ThemeOverrides& overrides);

// Theme files. Both return an error message for the user, empty on success. Loading leaves `out` untouched
// unless the file held at least one valid color.
std::string saveThemeFile(const std::string& pathUtf8, const ThemeOverrides& overrides);
std::string loadThemeFile(const std::string& pathUtf8, ThemeOverrides& out);

} // namespace trace
