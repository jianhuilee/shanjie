#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace shanjie {

// Byte offset in UTF-8 `s` of the position `units` UTF-16 code units from its start. Clamped to the
// string; an offset inside a surrogate pair stops before that character. Never splits a character.
size_t utf16ToByteOffset(std::string_view s, uint32_t units);

// The text before the cursor: at most `maxChars` characters ending at character index `cursorChars`
// (clamped to the text).
std::string textBefore(std::string_view text, uint32_t cursorChars, uint32_t maxChars);

} // namespace shanjie
