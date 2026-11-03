#pragma once
#include <algorithm>
#include <cstddef>
#include <string_view>

namespace noteplug {
constexpr bool lower(unsigned char c) noexcept { return c >= 'a' && c <= 'z'; }
constexpr bool upper(unsigned char c) noexcept { return c >= 'A' && c <= 'Z'; }
constexpr bool letter(unsigned char c) noexcept { return lower(c) || upper(c); }
constexpr char uppercase(char c) noexcept { return lower(static_cast<unsigned char>(c)) ? char(c - 'a' + 'A') : c; }
constexpr char lowercase(char c) noexcept { return upper(static_cast<unsigned char>(c)) ? char(c - 'A' + 'a') : c; }
constexpr char transition(char c, bool before, bool after) noexcept {
    return before == after ? c : after ? uppercase(c) : lowercase(c);
}
// Exact logical-line prefix, not a visually wrapped line.
constexpr bool capitalize(std::string_view prefix) noexcept {
    if (prefix.empty()) return true;
    if (prefix.size() < 2 || prefix.back() != ' ') return false;
    const char boundary = prefix[prefix.size() - 2];
    return boundary == '.' || boundary == '?' || boundary == ':' || boundary == '!'
        || (boundary == '-' && prefix.size() == 2);
}
// Only a directly adjacent uppercase successor or one separated by one ASCII space.
constexpr std::size_t upper_successor(std::string_view text, std::size_t caret) noexcept {
    if (caret >= text.size()) return std::string_view::npos;
    if (text[caret] == ' ') ++caret;
    return caret < text.size() && upper(static_cast<unsigned char>(text[caret]))
        ? caret : std::string_view::npos;
}
struct SpaceWindow { std::size_t start{}, end{}; };
// These are only the two nearest ASCII spaces on either side; outer spaces are retained.
constexpr SpaceWindow space_window(std::string_view line, std::size_t caret) noexcept {
    SpaceWindow result{caret, caret};
    for (int n = 0; n < 2 && result.start > 0 && line[result.start - 1] == ' '; ++n) --result.start;
    for (int n = 0; n < 2 && result.end < line.size() && line[result.end] == ' '; ++n) ++result.end;
    return result;
}
constexpr std::size_t enter_trim(std::string_view line_prefix) noexcept {
    std::size_t count = 0;
    while (count < 3 && count < line_prefix.size() && line_prefix[line_prefix.size() - count - 1] == ' ') ++count;
    return count;
}
} // namespace noteplug
