#pragma once
#include <fcitx-utils/key.h>

#include "keymap.h"

namespace fcitx {

// The adapter boundary for keys. fcitx5 reports the X keycode, which is the evdev code plus 8 (the
// Wayland and X11 frontends both do); keymap's tables are in evdev codes. A frontend that gives no
// keycode (0, or anything below 8) falls back to the keysym read as a US layout; a keysym that maps to
// no key leaves evdev 0, which keymap treats as a key outside its tables.
inline shanjie::KeyInput toKeyInput(const Key &key) {
    shanjie::KeyInput in;
    in.evdev = key.code() >= 8 ? static_cast<uint32_t>(key.code() - 8)
                               : shanjie::evdevFromKeysym(static_cast<uint32_t>(key.sym()));
    const KeyStates states = key.states();
    in.shift = states.test(KeyState::Shift);
    in.ctrl = states.test(KeyState::Ctrl);
    in.alt = states.test(KeyState::Alt);
    in.super = states.test(KeyState::Super) || states.test(KeyState::Super2);
    in.caps = states.test(KeyState::CapsLock);
    return in;
}

} // namespace fcitx
