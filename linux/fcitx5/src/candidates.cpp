#include "candidates.h"

#include <string>

#include "engine.h"

namespace fcitx {

namespace {

class ShanjieCandidateWord : public CandidateWord {
public:
    ShanjieCandidateWord(ShanjieEngine *engine, uint32_t index, const std::string &text)
        : CandidateWord(Text(text)), engine_(engine), index_(index) {}

    void select(InputContext *ic) const override {
        // The pick replaces the list this word belongs to: copy what is needed first and touch
        // nothing of `this` afterwards.
        ShanjieEngine *engine = engine_;
        const uint32_t index = index_;
        engine->pick(ic, index);
    }

private:
    ShanjieEngine *engine_;
    uint32_t index_;
};

} // namespace

ShanjieCandidateList::ShanjieCandidateList(ShanjieEngine *engine, const shanjie::Output &out)
    : cursor_(out.selected) {
    // selected -1 with candidates is the prediction row before it is entered: no highlight, and only
    // the first cell is marked, with the Tab glyph (docs/spec/prediction.md §8).
    const bool passiveRow = out.selected < 0;
    for (uint32_t i = 0; i < out.candidates.size(); i++) {
        words_.push_back(std::make_unique<ShanjieCandidateWord>(engine, i, out.candidates[i]));
        if (passiveRow) {
            labels_.emplace_back(i == 0 ? "\xe2\x87\xa5 " : "");
        } else {
            labels_.emplace_back(std::to_string(i + 1) + ". ");
        }
    }
}

} // namespace fcitx
