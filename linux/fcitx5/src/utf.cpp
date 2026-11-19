#include "utf.h"

#include <vector>

namespace shanjie {

namespace {
// Length in bytes of the UTF-8 character starting with `c`; 1 for a stray continuation byte.
size_t charLen(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}
} // namespace

size_t utf16ToByteOffset(std::string_view s, uint32_t units) {
    size_t i = 0;
    uint32_t u = 0;
    while (i < s.size() && u < units) {
        const size_t len = charLen(static_cast<unsigned char>(s[i]));
        const uint32_t width = len == 4 ? 2 : 1;
        if (u + width > units) break;
        if (i + len > s.size()) return s.size();
        i += len;
        u += width;
    }
    return i;
}

std::string textBefore(std::string_view text, uint32_t cursorChars, uint32_t maxChars) {
    std::vector<size_t> starts;
    size_t i = 0;
    while (i < text.size() && starts.size() < cursorChars) {
        starts.push_back(i);
        i += charLen(static_cast<unsigned char>(text[i]));
    }
    const size_t end = i < text.size() ? i : text.size();
    if (starts.empty() || maxChars == 0) return {};
    const size_t from = starts.size() > maxChars ? starts[starts.size() - maxChars] : 0;
    return std::string(text.substr(from, end - from));
}

namespace {

bool isHan(uint32_t c) {
    return (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) ||
           (c >= 0x20000 && c <= 0x2A6DF) || (c >= 0x2A700 && c <= 0x2EBEF) || (c >= 0x30000 && c <= 0x3134F) ||
           (c >= 0x2F800 && c <= 0x2FA1F) || c == 0x3007;
}

// Decodes the character at `i`; 0xFFFFFFFF for a malformed or truncated one. Sets `len` to its bytes.
uint32_t decodeAt(std::string_view s, size_t i, size_t &len) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    len = charLen(c);
    if (len == 1) return c < 0x80 ? c : 0xFFFFFFFFu;
    if (i + len > s.size()) {
        len = 1;
        return 0xFFFFFFFFu;
    }
    uint32_t cp = c & (0xFFu >> (len + 1));
    for (size_t k = 1; k < len; k++) {
        const unsigned char cont = static_cast<unsigned char>(s[i + k]);
        if ((cont & 0xC0) != 0x80) {
            len = 1;
            return 0xFFFFFFFFu;
        }
        cp = (cp << 6) | (cont & 0x3Fu);
    }
    return cp;
}

} // namespace

std::string trailingHan(std::string_view text) {
    size_t start = text.size(); // start of the current run of Han characters, text.size() when none
    size_t i = 0;
    while (i < text.size()) {
        size_t len = 1;
        const uint32_t cp = decodeAt(text, i, len);
        if (isHan(cp)) {
            if (start == text.size()) start = i;
        } else {
            start = text.size();
        }
        i += len;
    }
    return std::string(text.substr(start));
}

namespace {

// Swift's Character.isNewline (the macOS shell's rule, macos/Sources/ShanjieKit/Learning.swift) plus NUL.
bool isLineBreak(uint32_t c) {
    return c == 0x0A || c == 0x0B || c == 0x0C || c == 0x0D || c == 0x85 || c == 0x2028 || c == 0x2029 || c == 0;
}

// The part of `text` after its last line break or NUL (a NUL is a break, not deleted: 中\0文 must not become 中文).
std::string_view lineTail(std::string_view text) {
    size_t start = 0;
    size_t i = 0;
    while (i < text.size()) {
        size_t len = 1;
        const uint32_t cp = decodeAt(text, i, len);
        i += len;
        if (isLineBreak(cp)) start = i;
    }
    return text.substr(start);
}

} // namespace

std::string leftContextFor(std::string_view text, uint32_t cursorChars, uint32_t maxChars, bool terminal) {
    const std::string before = std::string(lineTail(textBefore(text, cursorChars, maxChars)));
    return terminal ? trailingHan(before) : before;
}

} // namespace shanjie
