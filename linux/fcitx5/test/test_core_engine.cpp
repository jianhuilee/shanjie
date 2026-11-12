#include <string>

#include "check.h"
#include "core_engine.h"
#include "keymap.h"

using namespace shanjie;

static const std::string kNihao = "\xe4\xbd\xa0\xe5\xa5\xbd"; // 你好

static Paths paths() {
    Paths p;
    p.lexicon = SHANJIE_TEST_LEXICON;
    p.packs = SHANJIE_TEST_PACKS;
    p.lm = SHANJIE_TEST_LM;
    return p; // no learning file in tests
}

// ' ' is the space bar; everything else a CHAR key.
static Result type(CoreEngine &e, const char *keys) {
    Result r;
    for (; *keys; keys++) {
        ShanjieKey k = *keys == ' ' ? ShanjieKey{KindSpace, 0, 0}
                                    : ShanjieKey{KindChar, static_cast<uint32_t>(*keys), 0};
        r = e.key(k);
        if (r.code != 0) return r;
    }
    return r;
}

int main() {
    REQUIRE_FILE(SHANJIE_TEST_LM "/bigram.sjlm");
    int logged = 0;
    CoreEngine e([&](const char *, int32_t) { logged++; });
    CHECK(!e.ready());
    CHECK(e.key(ShanjieKey{KindChar, 's', 0}).code == 1); // no engine: NULL code

    // Standard layout types 你好.
    CHECK(e.build(paths(), Config{}));
    CHECK(e.ready());
    CHECK(e.hasModel());
    CHECK(logged == 0);
    Result r = type(e, "su3cl3");
    CHECK(r.code == 0 && r.out.handled);
    CHECK(r.out.preedit == kNihao);
    CHECK(r.out.cursorUtf16 == 2);
    r = e.key(ShanjieKey{KindEnter, 0, 0});
    CHECK(r.code == 0 && r.out.handled);
    CHECK(r.out.commit == kNihao);
    CHECK(r.out.preedit.empty());
    CHECK(r.out.candidates.empty() && r.out.selected == -1);

    // applySettings turned the vertical window on: Space after a syllable opens it.
    r = type(e, "su3 ");
    CHECK(r.code == 0);
    CHECK(!r.out.candidates.empty() && r.out.candidates.size() <= 9);
    CHECK(r.out.selected == 0);
    CHECK(r.out.vertical == 1);
    CHECK(r.out.columns == 0);
    r = e.pick(0);
    CHECK(r.code == 0);
    CHECK(r.out.candidates.empty());
    CHECK(e.pick(0).code == 2); // no window, no prediction row entered: invalid, state unchanged

    // reset(0) commits what is shown; reset(1) discards.
    r = e.reset(0);
    CHECK(r.code == 0 && !r.out.commit.empty() && r.out.preedit.empty());
    type(e, "su3");
    r = e.reset(1);
    CHECK(r.code == 0 && r.out.commit.empty() && r.out.preedit.empty());

    // ETen layout needs a rebuild and types 你好 with its own keys.
    Config eten;
    eten.layout = 1;
    CHECK(CoreEngine::needsRebuild(Config{}, eten));
    Config formal;
    formal.profile = 1;
    CHECK(!CoreEngine::needsRebuild(Config{}, formal));
    Config fewerPacks;
    fewerPacks.packs = 0;
    CHECK(CoreEngine::needsRebuild(Config{}, fewerPacks));
    CHECK(e.build(paths(), eten));
    type(e, "ne3hz3");
    r = e.key(ShanjieKey{KindEnter, 0, 0});
    CHECK(r.code == 0 && r.out.commit == kNihao);

    // No model directory: the engine still types.
    Paths noLm = paths();
    noLm.lm.clear();
    CHECK(e.build(noLm, Config{}));
    CHECK(!e.hasModel());
    r = type(e, "su3");
    CHECK(r.code == 0 && !r.out.preedit.empty());

    // A wrong lexicon directory: no engine, and the failure is logged by code.
    Paths bad = paths();
    bad.lexicon = SHANJIE_TEST_LEXICON "/does-not-exist";
    logged = 0;
    CHECK(!e.build(bad, Config{}));
    CHECK(!e.ready());
    CHECK(logged > 0);
    TEST_MAIN_END();
}
