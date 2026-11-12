#include "core_engine.h"

#include <bit>

namespace shanjie {

namespace {

// Copies every field and frees the core's output in the same call (docs/spec/c-abi.md §5.1).
Output copyAndFree(ShanjieOutput *o) {
    Output out;
    out.handled = o->handled == 1;
    out.commit = o->commit;
    out.preedit = o->preedit;
    out.cursorUtf16 = o->cursor_utf16;
    out.candidates.reserve(o->candidate_count);
    for (uint32_t i = 0; i < o->candidate_count; i++) out.candidates.emplace_back(o->candidates[i]);
    out.selected = o->candidate_selected;
    out.columns = o->candidate_columns;
    out.first = o->candidate_first;
    out.total = o->candidate_total;
    out.vertical = o->candidate_vertical;
    out.englishStartUtf16 = o->english_run_utf16_start;
    out.englishLenUtf16 = o->english_run_utf16_len;
    shanjie_output_free(o);
    return out;
}

template <typename F>
Result call(F &&f) {
    ShanjieOutput *o = nullptr;
    Result r;
    r.code = f(&o);
    if (r.code == 0 && o) r.out = copyAndFree(o);
    return r;
}

} // namespace

CoreEngine::CoreEngine(Log log) : log_(std::move(log)) {}

CoreEngine::~CoreEngine() { shanjie_engine_free(engine_); }

void CoreEngine::note(const char *what, int32_t code) const {
    if (code != 0 && log_) log_(what, code);
}

ShanjieEngine *CoreEngine::create(const Paths &paths, uint32_t layout, uint32_t packs) {
    auto make = [&](uint32_t mask) -> ShanjieEngine * {
        ShanjieEngine *e = nullptr;
        const int32_t code =
            shanjie_engine_new_packs(paths.lexicon.c_str(), layout, paths.packs.c_str(), mask, &e);
        note("shanjie_engine_new_packs", code);
        return code == 0 ? e : nullptr;
    };
    if (ShanjieEngine *e = make(packs)) return e;
    if (packs == 0) return nullptr;
    // A pack may be the problem (docs/spec/macos-shell.md §2.1): try without packs, then drop one
    // enabled pack at a time and keep the first set that builds.
    ShanjieEngine *bare = make(0);
    if (!bare) return nullptr;
    if (std::popcount(packs) == 1) return bare;
    for (uint32_t bit = 1; bit <= SHANJIE_PACK_BEAUTY; bit <<= 1) {
        if (!(packs & bit)) continue;
        shanjie_engine_free(bare);
        bare = nullptr;
        if (ShanjieEngine *e = make(packs & ~bit)) return e;
    }
    return make(0);
}

bool CoreEngine::build(const Paths &paths, const Config &config) {
    shanjie_engine_free(engine_);
    engine_ = nullptr;
    hasModel_ = false;
    engine_ = create(paths, config.layout, config.packs);
    if (!engine_) return false;

    if (!paths.lm.empty()) {
        int32_t code = shanjie_engine_load_lm(engine_, (paths.lm + "/bigram.sjlm").c_str());
        note("shanjie_engine_load_lm", code);
        if (code == 0) {
            hasModel_ = true;
            code = shanjie_engine_load_tinylm(engine_, (paths.lm + "/tinylm.sjtl").c_str());
            note("shanjie_engine_load_tinylm", code);
            if (code == 0) {
                note("shanjie_engine_load_colloquial",
                     shanjie_engine_load_colloquial(engine_, (paths.lm + "/colloquial.sjlm").c_str()));
            }
        }
    }
    applySettings(config);
    if (!paths.learn.empty())
        note("shanjie_engine_learning_open", shanjie_engine_learning_open(engine_, paths.learn.c_str()));
    return true;
}

bool CoreEngine::needsRebuild(const Config &a, const Config &b) {
    return a.layout != b.layout || a.packs != b.packs;
}

void CoreEngine::applySettings(const Config &c) {
    if (!engine_) return;
    ShanjieEngine *e = engine_;
    auto send = [&](const char *what, auto &&f) { note(what, call(f).code); };
    send("set_demote", [&](ShanjieOutput **o) { return shanjie_engine_set_demote(e, c.demote, o); });
    send("set_prediction", [&](ShanjieOutput **o) { return shanjie_engine_set_prediction(e, c.prediction, o); });
    send("set_abbreviation", [&](ShanjieOutput **o) { return shanjie_engine_set_abbreviation(e, c.abbreviation, o); });
    send("set_english", [&](ShanjieOutput **o) { return shanjie_engine_set_english(e, c.english, o); });
    send("set_english_overwrite", [&](ShanjieOutput **o) { return shanjie_engine_set_english_overwrite(e, c.englishOverwrite, o); });
    send("set_english_key_order", [&](ShanjieOutput **o) { return shanjie_engine_set_english_key_order(e, c.englishKeyOrder, o); });
    send("set_english_reversal_guard", [&](ShanjieOutput **o) { return shanjie_engine_set_english_reversal_guard(e, c.englishReversalGuard, o); });
    send("set_fuzzy", [&](ShanjieOutput **o) { return shanjie_engine_set_fuzzy(e, c.fuzzy, o); });
    // fcitx5's panel has no grid: always the core's vertical window (design §5.2).
    send("set_candidate_vertical", [&](ShanjieOutput **o) { return shanjie_engine_set_candidate_vertical(e, 1, o); });
    send("set_key_scheme", [&](ShanjieOutput **o) { return shanjie_engine_set_key_scheme(e, c.keyScheme, o); });
    send("set_rerank", [&](ShanjieOutput **o) { return shanjie_engine_set_rerank(e, c.rerank, o); });
    send("set_rerank_level", [&](ShanjieOutput **o) { return shanjie_engine_set_rerank_level(e, c.rerankLevel, o); });
    send("set_profile", [&](ShanjieOutput **o) { return shanjie_engine_set_profile(e, c.profile, o); });
}

Result CoreEngine::key(ShanjieKey key) {
    return call([&](ShanjieOutput **o) { return shanjie_engine_key(engine_, key, o); });
}

Result CoreEngine::pick(uint32_t index) {
    return call([&](ShanjieOutput **o) { return shanjie_engine_pick(engine_, index, o); });
}

Result CoreEngine::reset(uint32_t mode) {
    return call([&](ShanjieOutput **o) { return shanjie_engine_reset(engine_, mode, o); });
}

void CoreEngine::setLearning(bool on) {
    if (engine_) note("shanjie_engine_set_learning", shanjie_engine_set_learning(engine_, on ? 1 : 0));
}

void CoreEngine::setLeftContext(const char *utf8) {
    // Code 2 (not UTF-8) clears the context in the core; nothing more to do.
    if (engine_) shanjie_engine_set_left_context(engine_, utf8);
}

int32_t CoreEngine::takeJob(ShanjieRerankJob **job) {
    *job = nullptr;
    if (!engine_) return 1;
    return shanjie_engine_rerank_take(engine_, job);
}

Result CoreEngine::applyJob(ShanjieRerankJob *job) {
    return call([&](ShanjieOutput **o) { return shanjie_engine_rerank_apply(engine_, job, o); });
}

} // namespace shanjie
