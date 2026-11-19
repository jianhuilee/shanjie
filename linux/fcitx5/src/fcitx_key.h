#pragma once
#include <fcitx-utils/key.h>

#include "keymap.h"

namespace fcitx {

// The adapter boundary for keys. fcitx5 reports the X keycode, which is the evdev code plus 8 (the
// Wayland and X11 frontends both do); keymap's tables are in evdev codes. A frontend that gives no
// keycode (0, or anything below 8) falls back to the keysym read as a US layout; a keysym that maps to
// no key leaves evdev 0, which keymap treats as a key outside its tables. A modifier keysym is marked as
// one, so that it never counts as such an unknown key.
//
// AltGr: layouts report their level-3 characters with Mod5. Such a key is the layout's character, not
// the physical key's Zhuyin symbol, so it goes to the core as Option (macOS's level-3 key), which passes
// it through and keeps the composition (docs/spec/keys-and-composition.md §4, rule 1).
// fcitx5's Key::isModifier, plus the lock and level-shift keys it leaves out (AltGr is ISO_Level3_Shift).
inline bool isModifierSym(const Key &key) {
    switch (key.sym()) {
    case FcitxKey_Caps_Lock:
    case FcitxKey_Shift_Lock:
    case FcitxKey_ISO_Level3_Shift:
    case FcitxKey_ISO_Level5_Shift:
    case FcitxKey_Mode_switch:
        return true;
    default:
        return key.isModifier();
    }
}

inline shanjie::KeyInput toKeyInput(const Key &key) {
    shanjie::KeyInput in;
    in.evdev = key.code() >= 8 ? static_cast<uint32_t>(key.code() - 8)
                               : shanjie::evdevFromKeysym(static_cast<uint32_t>(key.sym()));
    const KeyStates states = key.states();
    in.shift = states.test(KeyState::Shift);
    in.ctrl = states.test(KeyState::Ctrl);
    in.alt = states.test(KeyState::Alt) || states.test(KeyState::Mod5);
    in.super = states.test(KeyState::Super) || states.test(KeyState::Super2);
    in.caps = states.test(KeyState::CapsLock);
    in.modifier = isModifierSym(key);
    return in;
}

} // namespace fcitx
