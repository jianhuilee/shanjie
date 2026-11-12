#include "check.h"
#include "utf.h"

using namespace shanjie;

int main() {
    // 你好: two 3-byte characters, one UTF-16 unit each.
    const std::string nihao = "\xe4\xbd\xa0\xe5\xa5\xbd";
    CHECK(utf16ToByteOffset(nihao, 0) == 0);
    CHECK(utf16ToByteOffset(nihao, 1) == 3);
    CHECK(utf16ToByteOffset(nihao, 2) == 6);
    CHECK(utf16ToByteOffset(nihao, 99) == 6);

    // a, U+1F600 (4 bytes, 2 units), b
    const std::string emoji = "a\xf0\x9f\x98\x80" "b";
    CHECK(utf16ToByteOffset(emoji, 1) == 1);
    CHECK(utf16ToByteOffset(emoji, 2) == 1); // inside the pair: stop before it
    CHECK(utf16ToByteOffset(emoji, 3) == 5);
    CHECK(utf16ToByteOffset(emoji, 4) == 6);

    // A truncated trailing character is clamped to the end.
    CHECK(utf16ToByteOffset("a\xe4\xbd", 2) == 3);
    CHECK(utf16ToByteOffset("", 3) == 0);

    const std::string mixed = "abc" + nihao; // 5 characters
    CHECK(textBefore(mixed, 4, 64) == "abc\xe4\xbd\xa0");
    CHECK(textBefore(mixed, 4, 2) == "c\xe4\xbd\xa0");
    CHECK(textBefore(mixed, 99, 64) == mixed);
    CHECK(textBefore(mixed, 0, 64).empty());
    CHECK(textBefore("", 3, 64).empty());
    // Trailing Han characters only.
    CHECK(trailingHan("ls -la " + nihao) == nihao);
    CHECK(trailingHan(nihao + " $ ").empty());
    CHECK(trailingHan(nihao + "a" + nihao) == nihao);
    CHECK(trailingHan(nihao) == nihao);
    CHECK(trailingHan("").empty());
    CHECK(trailingHan("abc").empty());
    CHECK(trailingHan("x\xf0\xa0\x80\x80") == "\xf0\xa0\x80\x80"); // U+20000, a 4-byte Han character
    CHECK(trailingHan("\xe3\x80\x82").empty());                        // U+3002, a full-width full stop
    CHECK(trailingHan(nihao + "\xe4\xbd").empty());                     // a truncated character is not Han
    CHECK(trailingHan("\xe3\x80\x87") == "\xe3\x80\x87");             // U+3007, counted as Han by the core

    // A terminal gives the core no command text, only the Han characters right before the cursor.
    const std::string shell = "user@host:~$ git commit -m \"" + nihao;
    CHECK(leftContextFor(shell, 99, 64, false) == shell);
    CHECK(leftContextFor(shell, 99, 64, true) == nihao);
    CHECK(leftContextFor("user@host:~$ make", 99, 64, true).empty());
    CHECK(leftContextFor(nihao + " ok", 2, 64, true) == nihao); // the cursor is right after the Han run
    TEST_MAIN_END();
}
