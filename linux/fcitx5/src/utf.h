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

// The run of Han characters at the end of `text` (the core's definition of Han, core/src/learn.rs
// is_han); empty when the text does not end in one.
std::string trailingHan(std::string_view text);

// What the shell may hand the core as left context (shanjie.h, set_left_context): the text before the
// cursor, at most `maxChars` characters; in a terminal only its trailing Han characters, so commands
// and their output never reach the core.
std::string leftContextFor(std::string_view text, uint32_t cursorChars, uint32_t maxChars, bool terminal);

} // namespace shanjie
