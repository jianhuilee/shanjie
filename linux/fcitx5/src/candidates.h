#pragma once
#include <memory>
#include <vector>

#include <fcitx/candidatelist.h>
#include <fcitx/text.h>

#include "core_engine.h"

namespace fcitx {

class ShanjieEngine;

// The core's current page as an fcitx5 list. The core owns paging and selection; this only displays
// them and turns a click into a pick. No paging buttons (design §4.5).
class ShanjieCandidateList : public CandidateList {
public:
    ShanjieCandidateList(ShanjieEngine *engine, const shanjie::Output &out);

    const Text &label(int idx) const override { return labels_[idx]; }
    const CandidateWord &candidate(int idx) const override { return *words_[idx]; }
    int size() const override { return static_cast<int>(words_.size()); }
    int cursorIndex() const override { return cursor_; }
    CandidateLayoutHint layoutHint() const override { return CandidateLayoutHint::Vertical; }

private:
    std::vector<std::unique_ptr<CandidateWord>> words_;
    std::vector<Text> labels_;
    int cursor_;
};

} // namespace fcitx
