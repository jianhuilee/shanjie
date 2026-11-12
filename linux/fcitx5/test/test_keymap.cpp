#include <string>

#include "check.h"
#include "keymap.h"

using namespace shanjie;

static KeyInput key(uint32_t evdev) {
    KeyInput in;
    in.evdev = evdev;
    return in;
}

int main() {
    // Every ANSI key is its unshifted character. Rows by evdev code.
    const struct { uint32_t first; const char *chars; } rows[] = {
        {2, "1234567890-="}, {16, "qwertyuiop[]"}, {30, "asdfghjkl;'`"}, {43, "\\"}, {44, "zxcvbnm,./"},
    };
    int seen = 0;
    for (const auto &row : rows) {
        for (uint32_t i = 0; row.chars[i]; i++) {
            auto k = translate(key(row.first + i));
            CHECK(k.has_value());
            CHECK(k && k->kind == KindChar);
            CHECK(k && k->ch == static_cast<uint32_t>(static_cast<unsigned char>(row.chars[i])));
            CHECK(k && k->modifiers == 0);
            seen++;
        }
    }
    CHECK(seen == 47);

    // Shift does not change the character.
    KeyInput shifted = key(30);
    shifted.shift = true;
    auto a = translate(shifted);
    CHECK(a && a->ch == 'a' && a->modifiers == ModShift);

    // Special keys.
    const struct { uint32_t evdev; uint32_t kind; } specials[] = {
        {57, KindSpace}, {28, KindEnter}, {96, KindEnter}, {14, KindBackspace}, {111, KindDelete},
        {1, KindEsc}, {105, KindLeft}, {106, KindRight}, {103, KindUp}, {108, KindDown},
        {102, KindHome}, {107, KindEnd}, {15, KindTab},
    };
    for (const auto &s : specials) {
        auto k = translate(key(s.evdev));
        CHECK(k && k->kind == s.kind);
    }

    // Modifier bits.
    KeyInput all = key(14);
    all.shift = all.ctrl = all.alt = all.super = all.caps = true;
    CHECK(modifierBits(all) == 31);
    KeyInput superOnly = key(14);
    superOnly.super = true;
    auto forget = translate(superOnly);
    CHECK(forget && forget->kind == KindBackspace && forget->modifiers == ModSuper);

    // Outside the tables: F1, keypad 1, Page Up/Down, modifiers, unknown.
    for (uint32_t e : {59u, 79u, 104u, 109u, 42u, 29u, 0u, 999u}) CHECK(!translate(key(e)));

    // Page keys: only without modifiers.
    CHECK(translatePage(key(104)) && translatePage(key(104))->kind == KindPageUp);
    CHECK(translatePage(key(109)) && translatePage(key(109))->kind == KindPageDown);
    KeyInput shiftPage = key(104);
    shiftPage.shift = true;
    CHECK(!translatePage(shiftPage));
    KeyInput capsPage = key(109);
    capsPage.caps = true;
    CHECK(!translatePage(capsPage));
    CHECK(!translatePage(key(30)));

    CHECK(isPageKey(104) && isPageKey(109) && !isPageKey(108) && !isPageKey(0));

    // Selection keys.
    CHECK(std::string(selectionKeyChars(0)).empty() && std::string(selectionKeyChars(2)).empty());
    CHECK(std::string(selectionKeyChars(1)) == "asdfghjkl;");
    CHECK(!selectionIndex(0, 'a') && !selectionIndex(0, '1'));
    CHECK(selectionIndex(1, 'a') == 0u && selectionIndex(1, 'g') == 4u && selectionIndex(1, 'l') == 8u);
    CHECK(selectionIndex(1, ';') == 9u && !selectionIndex(1, '1') && !selectionIndex(1, 'q'));
    CHECK(!selectionIndex(2, 'a'));

    // Modifier keys themselves.
    for (uint32_t e : {29u, 42u, 54u, 56u, 58u, 97u, 100u, 125u, 126u}) CHECK(isModifierKey(e));
    CHECK(!isModifierKey(30));

    // Keysym fallback.
    CHECK(evdevFromKeysym('a') == 30);
    CHECK(evdevFromKeysym('A') == 30);
    CHECK(evdevFromKeysym('3') == 4);
    CHECK(evdevFromKeysym('#') == 4);
    CHECK(evdevFromKeysym('?') == 53);
    CHECK(evdevFromKeysym(' ') == 57);
    CHECK(evdevFromKeysym(0xff0d) == 28);  // Return
    CHECK(evdevFromKeysym(0xff08) == 14);  // BackSpace
    CHECK(evdevFromKeysym(0xff55) == 104); // Page_Up
    CHECK(evdevFromKeysym(0xffbe) == 0);   // F1
    TEST_MAIN_END();
}
