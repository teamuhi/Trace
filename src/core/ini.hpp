#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trace {

// A minimal INI document: [sections] holding key = value pairs. Sections and keys keep insertion order,
// so a file written back out stays stable. Parsing never fails: junk lines are skipped.
class IniFile {
public:
    struct Entry {
        std::string key;
        std::string value;
    };
    struct Section {
        std::string name;
        std::vector<Entry> entries;
    };

    // Accepts CRLF or LF, a UTF-8 BOM, ';' and '#' comment lines and whitespace around '='. Lines without
    // '=' are ignored, keys before the first header land in the unnamed "" section, and for a duplicate
    // key the last one wins.
    static IniFile parse(std::string_view text);

    std::string serialize() const;

    // Null when the section or key does not exist.
    const std::string* get(std::string_view section, std::string_view key) const;

    // Creates the section and key as needed. Line breaks in `value` are replaced by spaces so one entry
    // always stays on one line.
    void set(std::string_view section, std::string_view key, std::string_view value);

    const Section* section(std::string_view name) const;
    const std::vector<Section>& sections() const noexcept { return sections_; }

private:
    Section& sectionRef(std::string_view name);

    std::vector<Section> sections_;
};

// Typed readers. Each returns nullopt for malformed or out-of-range input so the caller keeps its default.
std::optional<int> parseInt(std::string_view text, int minValue, int maxValue);
std::optional<bool> parseBool(std::string_view text); // true/false, yes/no, on/off, 1/0 (any case)
std::optional<float> parseFloat(std::string_view text, float minValue, float maxValue);

} // namespace trace
