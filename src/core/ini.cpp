#include "core/ini.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>

namespace trace {

namespace {

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && isSpace(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back()))
        s.remove_suffix(1);
    return s;
}

std::string sanitize(std::string_view value) {
    std::string out(value);
    std::replace_if(out.begin(), out.end(), [](char c) { return c == '\r' || c == '\n'; }, ' ');
    return out;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

} // namespace

IniFile IniFile::parse(std::string_view text) {
    IniFile ini;
    if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF")
        text.remove_prefix(3);

    std::string current;
    while (!text.empty()) {
        const std::size_t eol = text.find('\n');
        const std::string_view line = trim(text.substr(0, eol));
        text = eol == std::string_view::npos ? std::string_view() : text.substr(eol + 1);

        if (line.empty() || line.front() == ';' || line.front() == '#')
            continue;

        if (line.front() == '[') {
            const std::size_t close = line.find(']');
            if (close == std::string_view::npos)
                continue;
            current = std::string(trim(line.substr(1, close - 1)));
            ini.sectionRef(current); // keep empty sections too
            continue;
        }

        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos)
            continue;
        const std::string_view key = trim(line.substr(0, eq));
        if (key.empty())
            continue;
        ini.set(current, key, trim(line.substr(eq + 1)));
    }
    return ini;
}

std::string IniFile::serialize() const {
    std::string out;
    const auto write = [&out](const Section& section) {
        if (!section.name.empty()) {
            if (!out.empty())
                out += '\n';
            out += '[' + section.name + "]\n";
        }
        for (const Entry& entry : section.entries)
            out += entry.key + " = " + entry.value + '\n';
    };
    // Entries of the unnamed section have no header, so they must come first to stay outside the others.
    for (const Section& section : sections_) {
        if (section.name.empty())
            write(section);
    }
    for (const Section& section : sections_) {
        if (!section.name.empty())
            write(section);
    }
    return out;
}

IniFile::Section& IniFile::sectionRef(std::string_view name) {
    for (Section& section : sections_) {
        if (section.name == name)
            return section;
    }
    sections_.push_back(Section{std::string(name), {}});
    return sections_.back();
}

const IniFile::Section* IniFile::section(std::string_view name) const {
    for (const Section& section : sections_) {
        if (section.name == name)
            return &section;
    }
    return nullptr;
}

const std::string* IniFile::get(std::string_view sectionName, std::string_view key) const {
    const Section* s = section(sectionName);
    if (!s)
        return nullptr;
    for (const Entry& entry : s->entries) {
        if (entry.key == key)
            return &entry.value;
    }
    return nullptr;
}

void IniFile::set(std::string_view sectionName, std::string_view key, std::string_view value) {
    Section& s = sectionRef(sectionName);
    for (Entry& entry : s.entries) {
        if (entry.key == key) {
            entry.value = sanitize(value);
            return;
        }
    }
    s.entries.push_back(Entry{sanitize(key), sanitize(value)});
}

std::optional<int> parseInt(std::string_view text, int minValue, int maxValue) {
    text = trim(text);
    int value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || end != text.data() + text.size())
        return std::nullopt;
    if (value < minValue || value > maxValue)
        return std::nullopt;
    return value;
}

std::optional<bool> parseBool(std::string_view text) {
    text = trim(text);
    for (const char* yes : {"true", "yes", "on", "1"}) {
        if (equalsIgnoreCase(text, yes))
            return true;
    }
    for (const char* no : {"false", "no", "off", "0"}) {
        if (equalsIgnoreCase(text, no))
            return false;
    }
    return std::nullopt;
}

std::optional<float> parseFloat(std::string_view text, float minValue, float maxValue) {
    text = trim(text);
    if (text.empty())
        return std::nullopt;
    // strtod instead of std::from_chars: floating point from_chars is missing from some standard libraries.
    const std::string copy(text);
    char* end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !std::isfinite(value))
        return std::nullopt;
    if (value < static_cast<double>(minValue) || value > static_cast<double>(maxValue))
        return std::nullopt;
    return static_cast<float>(value);
}

} // namespace trace
