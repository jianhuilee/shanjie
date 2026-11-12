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
    TEST_MAIN_END();
}
