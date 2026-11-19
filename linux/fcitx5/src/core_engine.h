#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "shanjie.h"

namespace shanjie {

// A copy of ShanjieOutput. Holds what the person typed: never log it, never give it a stream operator.
struct Output {
    bool handled = false;
    std::string commit, preedit;
    uint32_t cursorUtf16 = 0;
    std::vector<std::string> candidates;
    int32_t selected = -1;
    uint32_t columns = 0, first = 0, total = 0;
    uint32_t vertical = 0; // 0 none or horizontal, 1 vertical window, 2 vertical prediction row
    uint32_t englishStartUtf16 = 0, englishLenUtf16 = 0;
};

// `out` is meaningful only when `code` is 0.
struct Result {
    int32_t code = 0;
    Output out;
};

// What the shell wants from the engine. Defaults are the macOS app's defaults.
struct Config {
    uint32_t layout = 0;   // 0 standard, 1 ETen
    uint32_t packs = SHANJIE_PACK_ACG | SHANJIE_PACK_IT | SHANJIE_PACK_MEME | SHANJIE_PACK_BEAUTY;
    uint32_t profile = 0;  // 0 chat, 1 formal
    bool demote = true;
    bool prediction = true;
    bool abbreviation = false;
    bool english = true;
    bool englishOverwrite = false;
    bool englishKeyOrder = true;
    bool englishReversalGuard = true;
    uint32_t fuzzy = 0;    // bits 0-4
    bool rerank = true;
    uint32_t rerankLevel = 1;
    uint32_t keyScheme = 0;
    bool learning = true;  // the shell's gate for set_learning; not sent by applySettings
    uint32_t selectionKeys = 0; // shell only (keymap.h, selectionKeyChars): 0 digits, 1 asdfghjkl;
};

// Directories. `lexicon` is required. `lm` and `learn` may be empty (no model, no learning file).
struct Paths {
    std::string lexicon, packs, lm, learn;
};

class CoreEngine {
public:
    // Receives a static description and a return code. Never receives input text or paths.
    using Log = std::function<void(const char *what, int32_t code)>;

    explicit CoreEngine(Log log = {});
    ~CoreEngine();
    CoreEngine(const CoreEngine &) = delete;
    CoreEngine &operator=(const CoreEngine &) = delete;

    // Frees any engine, creates one, loads the models and sends every setting. False when no engine
    // could be created.
    bool build(const Paths &paths, const Config &config);
    bool ready() const { return engine_ != nullptr; }
    bool hasModel() const { return hasModel_; }

    // True when going from `a` to `b` needs build() rather than applySettings().
    static bool needsRebuild(const Config &a, const Config &b);
    // Sends every setting that does not need a rebuild. Every setter recomputes the composition, so it
    // may be called while one is open: the result is the last setter's snapshot, with the commits of all
    // of them in order (a setter can end an English run that reaches the 128th unit). Code 4 when any
    // setter answered 4 (the core has then discarded the composition), else 0.
    Result applySettings(const Config &config);
    // The same, sending only the settings that differ between `from` and `to`, as the macOS shell calls
    // one setter per change: a setter recomputes the composition even when its value is unchanged
    // (set_profile closes an open candidate window). SHANJIE_NO_CHANGE when nothing differs.
    Result applyChanges(const Config &from, const Config &to);

    Result key(ShanjieKey key);
    Result pick(uint32_t index);
    Result reset(uint32_t mode); // 0 commit what is shown, 1 discard
    void setLearning(bool on);
    void setLeftContext(const char *utf8); // nullptr for none

    // The job the last change left, or nullptr; the caller owns it. The return code matters: 4 means
    // the core has already discarded the composition (shanjie.h, rerank_take).
    int32_t takeJob(ShanjieRerankJob **job);
    Result applyJob(ShanjieRerankJob *job);  // code 5: nothing changed. Does not free the job.

private:
    ShanjieEngine *create(const Paths &paths, uint32_t layout, uint32_t packs);
    void note(const char *what, int32_t code) const;
    Result send(const Config &config, const Config *from);

    Log log_;
    ShanjieEngine *engine_ = nullptr;
    bool hasModel_ = false;
};

} // namespace shanjie
