#include "keymap.h"

#include <cstring>

namespace shanjie {

namespace {

// Rows of the US-ANSI keyboard by evdev code: the first code of the row and its unshifted characters.
struct Row { uint32_t first; const char *plain; const char *shifted; };
const Row kRows[] = {
    {2, "1234567890-=", "!@#$%^&*()_+"},
    {16, "qwertyuiop[]", "QWERTYUIOP{}"},
    {30, "asdfghjkl;'`", "ASDFGHJKL:\"~"},
    {43, "\\", "|"},
    {44, "zxcvbnm,./", "ZXCVBNM<>?"},
};

uint32_t charFor(uint32_t evdev) {
    for (const Row &row : kRows) {
        const uint32_t n = static_cast<uint32_t>(std::strlen(row.plain));
        if (evdev >= row.first && evdev < row.first + n)
            return static_cast<unsigned char>(row.plain[evdev - row.first]);
    }
    return 0;
}

uint32_t kindFor(uint32_t evdev) {
    switch (evdev) {
    case 57: return KindSpace;
    case 28: case 96: return KindEnter;
    case 14: return KindBackspace;
    case 111: return KindDelete;
    case 1: return KindEsc;
    case 105: return KindLeft;
    case 106: return KindRight;
    case 103: return KindUp;
    case 108: return KindDown;
    case 102: return KindHome;
    case 107: return KindEnd;
    case 15: return KindTab;
    default: return 0;
    }
}

} // namespace

uint32_t modifierBits(const KeyInput &in) {
    return (in.shift ? ModShift : 0u) | (in.ctrl ? ModControl : 0u) | (in.alt ? ModAlt : 0u) |
           (in.super ? ModSuper : 0u) | (in.caps ? ModCapsLock : 0u);
}

std::optional<ShanjieKey> translate(const KeyInput &in) {
    if (const uint32_t ch = charFor(in.evdev)) return ShanjieKey{KindChar, ch, modifierBits(in)};
    if (const uint32_t kind = kindFor(in.evdev)) return ShanjieKey{kind, 0, modifierBits(in)};
    return std::nullopt;
}

std::optional<ShanjieKey> translatePage(const KeyInput &in) {
    if (modifierBits(in) != 0) return std::nullopt;
    if (in.evdev == 104) return ShanjieKey{KindPageUp, 0, 0};
    if (in.evdev == 109) return ShanjieKey{KindPageDown, 0, 0};
    return std::nullopt;
}

const char *selectionKeyChars(uint32_t choice) {
    switch (choice) {
    case 1: return "asdfghjkl;";
    default: return "";
    }
}

std::optional<uint32_t> selectionIndex(uint32_t choice, uint32_t ch) {
    const char *keys = selectionKeyChars(choice);
    for (uint32_t i = 0; keys[i]; i++)
        if (static_cast<unsigned char>(keys[i]) == ch) return i;
    return std::nullopt;
}

bool isPageKey(uint32_t evdev) { return evdev == 104 || evdev == 109; }

bool isModifierKey(uint32_t evdev) {
    switch (evdev) {
    case 29: case 97:   // Control
    case 42: case 54:   // Shift
    case 56: case 100:  // Alt
    case 125: case 126: // Super
    case 58:            // Caps Lock
        return true;
    default:
        return false;
    }
}

uint32_t evdevFromKeysym(uint32_t keysym) {
    switch (keysym) {
    case 0x20: return 57;                 // space
    case 0xff08: return 14;               // BackSpace
    case 0xff09: case 0xfe20: return 15;  // Tab, ISO_Left_Tab
    case 0xff0d: return 28;               // Return
    case 0xff8d: return 96;               // KP_Enter
    case 0xff1b: return 1;                // Escape
    case 0xff50: return 102;              // Home
    case 0xff51: return 105;              // Left
    case 0xff52: return 103;              // Up
    case 0xff53: return 106;              // Right
    case 0xff54: return 108;              // Down
    case 0xff55: return 104;              // Page_Up
    case 0xff56: return 109;              // Page_Down
    case 0xff57: return 107;              // End
    case 0xffff: return 111;              // Delete
    default: break;
    }
    if (keysym < 0x21 || keysym > 0x7e) return 0;
    const char c = static_cast<char>(keysym);
    for (const Row &row : kRows) {
        for (uint32_t i = 0; row.plain[i]; i++)
            if (row.plain[i] == c || row.shifted[i] == c) return row.first + i;
    }
    return 0;
}

} // namespace shanjie
