#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace trace {

// The user's symmetry setup: an ordered list of up to kMaxSymmetryItems items, each a mirror line or a rotation
// center with its own position, shown as a list in the Pixel Tools panel (add, remove, edit each one). Pure data;
// the drawing math is in core/symmetry.hpp. Session state: never saved in settings.ini.
//
// The copies of a drawing are built by applying the enabled items in list order, each one to every copy made so
// far: a mirror doubles the copies (the originals plus their reflections), a rotation of N repeats all of them N
// times around its center. So Mirror X + Mirror Y give 4 copies, Mirror X + Rotate 4 give 8, and two Mirror X
// lines at different positions give 4. The total is capped at kMaxSymmetryCopies; identical copies are dropped.

inline constexpr int kSymmetryCenter = -1; // a stored position meaning "the middle of the canvas"
inline constexpr int kMaxSymmetryItems = 8;
inline constexpr std::size_t kMaxSymmetryCopies = 64;
inline constexpr std::size_t kMaxSymmetryNameBytes = 32; // a custom item name, in UTF-8 bytes

// A custom item name: UTF-8 text, NUL-terminated, the unused tail zeroed (so equal names compare equal).
using SymmetryName = std::array<char, kMaxSymmetryNameBytes + 1>;

// The text of a name buffer: up to the first NUL, never past the buffer.
constexpr std::string_view symmetryNameView(const SymmetryName& name) noexcept {
    std::size_t len = 0;
    while (len < name.size() && name[len] != '\0')
        ++len;
    return std::string_view(name.data(), len);
}

// Bytes of the UTF-8 sequence a lead byte starts; 0 for a stray continuation byte or an invalid lead.
constexpr std::size_t utf8SequenceLength(unsigned char lead) noexcept {
    if (lead < 0x80)
        return 1;
    if (lead >= 0xC2 && lead <= 0xDF)
        return 2;
    if (lead >= 0xE0 && lead <= 0xEF)
        return 3;
    if (lead >= 0xF0 && lead <= 0xF4)
        return 4;
    return 0;
}

// `text` as a custom name: invalid UTF-8 and control characters (C0, DEL, C1) dropped, leading and trailing spaces
// trimmed, and cut at kMaxSymmetryNameBytes on a character boundary. Empty means "no custom name".
constexpr SymmetryName cleanSymmetryName(std::string_view text) noexcept {
    SymmetryName out{};
    std::size_t n = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const std::size_t len = utf8SequenceLength(lead);
        if (len == 0) { // a stray continuation byte or an invalid lead
            ++i;
            continue;
        }
        bool valid = i + len <= text.size();
        for (std::size_t k = 1; valid && k < len; ++k)
            valid = (static_cast<unsigned char>(text[i + k]) & 0xC0u) == 0x80u;
        if (!valid) { // a truncated sequence: drop its lead byte
            ++i;
            continue;
        }
        const bool control = len == 1 ? (lead < 0x20 || lead == 0x7F)
                                      : (lead == 0xC2 && static_cast<unsigned char>(text[i + 1]) < 0xA0);
        if (control || (lead == ' ' && n == 0)) {
            i += len;
            continue;
        }
        if (n + len > kMaxSymmetryNameBytes)
            break;
        for (std::size_t k = 0; k < len; ++k)
            out[n++] = text[i + k];
        i += len;
    }
    while (n > 0 && out[n - 1] == ' ')
        out[--n] = '\0';
    return out;
}

enum class SymmetryKind : std::uint8_t { Mirror, Rotate };

struct SymmetryItem {
    SymmetryKind kind = SymmetryKind::Mirror;
    bool enabled = true;
    // Mirror: names the item in the list, "Mirror X" (0) or "Mirror Y" (1); the angle is what decides the line.
    std::uint8_t preset = 0;
    // A point the mirror line passes through, or the rotation center, in half pixels from the image's top-left
    // (position p is p / 2 in image coordinates). kSymmetryCenter (or any negative value) is the canvas middle.
    int posX = kSymmetryCenter;
    int posY = kSymmetryCenter;
    // Mirror: the direction points are flipped across, in degrees clockwise on screen, 0..179. 0 flips left <->
    // right (the line is vertical: "Mirror X"), 90 flips top <-> bottom (a horizontal line: "Mirror Y"), 45 and
    // 135 are the diagonals. Only multiples of 45 are exact on the pixel grid; other angles are rounded.
    int angle = 0;
    // Rotate: how many copies are spread around the center (2, 3, 4, 6 or 8).
    int count = 4;
    // A name the user gave the item (empty: the automatic "Mirror X", "Mirror X (2)", ...). Kept clean by sanitizedItem.
    SymmetryName name{};

    constexpr bool operator==(const SymmetryItem&) const = default;
};

struct SymmetrySet {
    std::array<SymmetryItem, kMaxSymmetryItems> items{};
    int count = 0;

    constexpr bool operator==(const SymmetrySet&) const = default;
};

// Rotation counts that are accepted; anything else becomes 4.
constexpr int sanitizeRotateCount(int n) noexcept {
    return (n == 2 || n == 3 || n == 4 || n == 6 || n == 8) ? n : 4;
}
// A mirror angle folded into 0..179 (a mirror line has no direction, so 200 is 20 and -45 is 135).
constexpr int normalizeMirrorAngle(int degrees) noexcept {
    const int m = degrees % 180;
    return m < 0 ? m + 180 : m;
}

constexpr SymmetryItem makeMirrorX() noexcept { return SymmetryItem{SymmetryKind::Mirror, true, 0, kSymmetryCenter, kSymmetryCenter, 0, 4}; }
constexpr SymmetryItem makeMirrorY() noexcept { return SymmetryItem{SymmetryKind::Mirror, true, 1, kSymmetryCenter, kSymmetryCenter, 90, 4}; }
constexpr SymmetryItem makeRotate(int count = 4) noexcept {
    return SymmetryItem{SymmetryKind::Rotate, true, 0, kSymmetryCenter, kSymmetryCenter, 0, sanitizeRotateCount(count)};
}

// An item with every field forced into range (angle folded, rotation count valid, preset 0 or 1).
constexpr SymmetryItem sanitizedItem(SymmetryItem it) noexcept {
    it.angle = normalizeMirrorAngle(it.angle);
    it.count = sanitizeRotateCount(it.count);
    it.preset = it.preset == 1 ? 1 : 0;
    it.name = cleanSymmetryName(symmetryNameView(it.name));
    return it;
}

// Sets (cleaned) or, with empty text, clears the custom name of `it`.
constexpr void setSymmetryName(SymmetryItem& it, std::string_view text) noexcept {
    it.name = cleanSymmetryName(text);
}

// The position among the items of the same kind (and, for mirrors, the same preset) in list order: 1 for the first.
// This numbers the automatic names, "Mirror X", "Mirror X (2)", whatever the earlier items are called. 0 for an
// index outside the list.
constexpr int symmetryOrdinal(const SymmetrySet& set, int index) noexcept {
    if (index < 0 || index >= set.count || index >= kMaxSymmetryItems)
        return 0;
    const SymmetryItem& it = set.items[static_cast<std::size_t>(index)];
    int ordinal = 1;
    for (int j = 0; j < index; ++j) {
        const SymmetryItem& other = set.items[static_cast<std::size_t>(j)];
        if (other.kind == it.kind && (it.kind == SymmetryKind::Rotate || (other.preset == 1) == (it.preset == 1)))
            ++ordinal;
    }
    return ordinal;
}

// How many copies the enabled items make, ignoring that coincident copies are dropped: 2 per mirror, N per
// rotation, 1 when nothing is enabled. Saturates just above kMaxSymmetryCopies so it cannot overflow.
constexpr std::size_t symmetryCopyEstimate(const SymmetrySet& set) noexcept {
    std::size_t n = 1;
    for (int i = 0; i < std::clamp(set.count, 0, kMaxSymmetryItems); ++i) {
        const SymmetryItem& it = set.items[static_cast<std::size_t>(i)];
        if (!it.enabled)
            continue;
        n *= it.kind == SymmetryKind::Mirror ? 2u : static_cast<std::size_t>(sanitizeRotateCount(it.count));
        if (n > kMaxSymmetryCopies)
            return kMaxSymmetryCopies + 1;
    }
    return n;
}

// Whether `item` can be appended: the list has room and the copies stay within kMaxSymmetryCopies.
constexpr bool canAddSymmetryItem(const SymmetrySet& set, const SymmetryItem& item) noexcept {
    if (set.count >= kMaxSymmetryItems || set.count < 0)
        return false;
    SymmetrySet next = set;
    next.items[static_cast<std::size_t>(next.count++)] = item;
    return symmetryCopyEstimate(next) <= kMaxSymmetryCopies;
}

// Appends `item` (sanitized) when canAddSymmetryItem allows; returns whether it was added.
constexpr bool addSymmetryItem(SymmetrySet& set, SymmetryItem item) noexcept {
    item = sanitizedItem(item);
    if (!canAddSymmetryItem(set, item))
        return false;
    set.items[static_cast<std::size_t>(set.count++)] = item;
    return true;
}

// Removes item `index` (later ones move up). Returns whether anything was removed.
constexpr bool removeSymmetryItem(SymmetrySet& set, int index) noexcept {
    if (index < 0 || index >= set.count)
        return false;
    for (int i = index; i + 1 < set.count; ++i)
        set.items[static_cast<std::size_t>(i)] = set.items[static_cast<std::size_t>(i) + 1];
    set.items[static_cast<std::size_t>(--set.count)] = SymmetryItem{};
    return true;
}

// True when at least one enabled item is present.
constexpr bool symmetryActive(const SymmetrySet& set) noexcept {
    for (int i = 0; i < std::clamp(set.count, 0, kMaxSymmetryItems); ++i)
        if (set.items[static_cast<std::size_t>(i)].enabled)
            return true;
    return false;
}

} // namespace trace
