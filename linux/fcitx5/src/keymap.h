#pragma once
#include <cstdint>
#include <optional>

#include "shanjie.h"

namespace shanjie {

// ShanjieKey.kind values (core/include/shanjie.h).
enum Kind : uint32_t {
    KindChar = 1, KindSpace = 2, KindEnter = 3, KindBackspace = 4, KindDelete = 5, KindEsc = 6,
    KindLeft = 7, KindRight = 8, KindUp = 9, KindDown = 10, KindHome = 11, KindEnd = 12, KindTab = 13,
    KindPageUp = 14, KindPageDown = 15,
};

// ShanjieKey.modifiers bits. Alt is the core's Option, Super is its Command.
enum Mod : uint32_t { ModShift = 1, ModControl = 2, ModAlt = 4, ModSuper = 8, ModCapsLock = 16 };

// A physical key press: the evdev code (linux/input-event-codes.h, i.e. the X keycode minus 8) and
// the modifier state. 0 means "unknown key".
struct KeyInput {
    uint32_t evdev = 0;
    bool shift = false, ctrl = false, alt = false, super = false, caps = false;
};

uint32_t modifierBits(const KeyInput &in);

// The key for the core, or nullopt for a key outside the tables (keypad, function keys, Page Up/Down,
// modifiers, unknown). The character is the unshifted US-ANSI keycap.
std::optional<ShanjieKey> translate(const KeyInput &in);

// Page Up / Page Down as kinds 14 / 15, only without any modifier. The caller sends them only while the
// vertical candidate window is open.
std::optional<ShanjieKey> translatePage(const KeyInput &in);

// The keys that pick a candidate, in order, for a selection-key choice: 0 is the digits (the core's own
// rule, so the string is empty and the shell does nothing), 1 the home row "asdfghjkl", 2 the eight
// resting keys "asdfjkl;". Unknown choices are treated as 0.
const char *selectionKeyChars(uint32_t choice);

// The candidate position `ch` picks under `choice`, or nullopt when it is not a selection key.
std::optional<uint32_t> selectionIndex(uint32_t choice, uint32_t ch);

// True for the Page Up and Page Down keys, whatever the modifiers.
bool isPageKey(uint32_t evdev);

// True for Shift, Control, Alt, Super and Caps Lock themselves.
bool isModifierKey(uint32_t evdev);

// For frontends that give no keycode: the evdev code of the US-layout key that produces this X keysym,
// or 0. Shifted symbols map to their key ('!' to the 1 key).
uint32_t evdevFromKeysym(uint32_t keysym);

} // namespace shanjie
