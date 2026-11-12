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

} // namespace shanjie
