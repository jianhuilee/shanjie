#include "check.h"
#include "shanjie.h"

int main() {
    ShanjieEngine *e = nullptr;
    CHECK(shanjie_engine_new(SHANJIE_TEST_LEXICON, 0, &e) == 0);
    CHECK(e != nullptr);
    shanjie_engine_free(e);
    shanjie_engine_free(nullptr);
    TEST_MAIN_END();
}
