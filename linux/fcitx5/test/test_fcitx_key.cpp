#include <fcitx-utils/key.h>

#include "check.h"
#include "fcitx_key.h"

using namespace fcitx;
using namespace shanjie;

// Keys as fcitx5 delivers them: X keycodes, i.e. evdev + 8.
int main() {
    // The S key (evdev 31, X keycode 39).
    auto s = translate(toKeyInput(Key(FcitxKey_s, KeyStates(), 39)));
    CHECK(s && s->kind == KindChar && s->ch == 's' && s->modifiers == 0);
    // Passing the X keycode straight into the table would give another key: 39 is ';' in evdev terms.
    KeyInput raw;
    raw.evdev = 39;
    CHECK(translate(raw) && translate(raw)->ch == ';');

    // The physical key decides, not the layout's keysym.
    auto dvorak = translate(toKeyInput(Key(FcitxKey_o, KeyStates(), 39)));
    CHECK(dvorak && dvorak->ch == 's');

    // Return (evdev 28, X 36), Page Down (evdev 109, X 117), the 3 key (evdev 4, X 12), Escape (1, 9).
    auto enter = translate(toKeyInput(Key(FcitxKey_Return, KeyStates(), 36)));
    CHECK(enter && enter->kind == KindEnter);
    auto page = translatePage(toKeyInput(Key(FcitxKey_Page_Down, KeyStates(), 117)));
    CHECK(page && page->kind == KindPageDown);
    auto three = translate(toKeyInput(Key(FcitxKey_3, KeyStates(), 12)));
    CHECK(three && three->ch == '3');
    auto esc = translate(toKeyInput(Key(FcitxKey_Escape, KeyStates(), 9)));
    CHECK(esc && esc->kind == KindEsc);

    // Modifier state.
    KeyStates held = KeyState::Shift;
    held |= KeyState::Ctrl;
    held |= KeyState::Alt;
    held |= KeyState::Super;
    held |= KeyState::CapsLock;
    CHECK(modifierBits(toKeyInput(Key(FcitxKey_BackSpace, held, 22))) == 31);
    CHECK(toKeyInput(Key(FcitxKey_BackSpace, KeyStates(KeyState::Super2), 22)).super);
    CHECK(isModifierKey(toKeyInput(Key(FcitxKey_Shift_L, KeyStates(), 50)).evdev));

    // No keycode: the keysym is read as a US layout; Shift comes from the state.
    auto noCode = translate(toKeyInput(Key(FcitxKey_S, KeyStates(KeyState::Shift), 0)));
    CHECK(noCode && noCode->ch == 's' && noCode->modifiers == ModShift);
    auto lowCode = translate(toKeyInput(Key(FcitxKey_Return, KeyStates(), 5)));
    CHECK(lowCode && lowCode->kind == KindEnter);
    // No keycode and a keysym with no key: outside the tables.
    CHECK(!translate(toKeyInput(Key(FcitxKey_F1, KeyStates(), 0))));
    CHECK(!translate(toKeyInput(Key(FcitxKey_None, KeyStates(), 0))));

    // A modifier with no keycode is still a modifier: it must not reach the session as an unknown key.
    for (KeySym sym : {FcitxKey_Shift_L, FcitxKey_Shift_R, FcitxKey_Control_L, FcitxKey_Control_R, FcitxKey_Alt_L,
                       FcitxKey_Alt_R, FcitxKey_Super_L, FcitxKey_Super_R, FcitxKey_ISO_Level3_Shift,
                       FcitxKey_Caps_Lock}) {
        CHECK(toKeyInput(Key(sym, KeyStates(), 0)).modifier);
    }
    CHECK(toKeyInput(Key(FcitxKey_Shift_L, KeyStates(), 50)).modifier);
    CHECK(!toKeyInput(Key(FcitxKey_s, KeyStates(), 39)).modifier);
    CHECK(!toKeyInput(Key(FcitxKey_S, KeyStates(KeyState::Shift), 0)).modifier);

    // AltGr: the layout reports its level-3 characters with Mod5. The physical Q (evdev 16, X 24) with
    // Mod5 is `@` on such a layout; it goes to the core as Option, which passes it through.
    auto altGr = translate(toKeyInput(Key(FcitxKey_at, KeyStates(KeyState::Mod5), 24)));
    CHECK(altGr && altGr->ch == 'q' && (altGr->modifiers & ModAlt));
    auto altGrNoCode = translate(toKeyInput(Key(FcitxKey_at, KeyStates(KeyState::Mod5), 0)));
    CHECK(altGrNoCode && (altGrNoCode->modifiers & ModAlt));
    TEST_MAIN_END();
}
