#include "session.h"

#include <chrono>

#include "utf.h"

namespace shanjie {

Session::Session(Paths paths, Config config, RerankRunner::Post post, CoreEngine::Log log)
    : paths_(std::move(paths)), config_(config), log_(std::move(log)), engine_(log_) {
    engine_.build(paths_, config_);
    runner_ = std::make_unique<RerankRunner>(std::move(post),
                                             [this](ShanjieRerankJob *job) { onRerankDone(job); });
}

Session::~Session() = default;

void Session::claim(Client &client) {
    if (owner_ == &client) return;
    if (composing_) {
        if (owner_) {
            finish(*owner_, true); // the composition goes back to its own client
        } else {
            engine_.reset(1);
            composing_ = false;
            lastPreedit_.reset();
        }
    }
    owner_ = &client;
}

bool Session::handleKey(Client &client, const KeyInput &in) {
    if (!engine_.ready()) return false;
    // A modifier pressed alone is not a key for the core and must not end the composition.
    if (isModifierKey(in.evdev)) return false;
    claim(client);
    if (client.isPassword()) {
        finish(client, false);
        return false;
    }
    engine_.setLearning(config_.learning);

    std::optional<ShanjieKey> key = translate(in);
    if (!key && isPageKey(in.evdev)) {
        // Page Up / Down go to the core only for an open vertical window and without modifiers. In
        // every other state they belong to the application and leave the composition as it is, which
        // is what the core itself does with them (shanjie.h, PAGE_UP / PAGE_DOWN).
        if (lastVertical_ == 1) key = translatePage(in);
        if (!key) return false;
    }
    if (!key) {
        // Outside the tables: send the composition, then let the application have the key.
        finish(client, true);
        return false;
    }

    // Letter selection keys: with the candidate window open or the prediction row entered, an unmodified
    // selection key picks that item through the core's pick, the path a click takes, so learning is the
    // same as for a digit. A key beyond the items shown is eaten, as the core does with a digit. The
    // digits keep working. A prediction row that is only showing is not a selection: typing goes on.
    if (key->kind == KindChar && key->modifiers == 0 && lastSelected_ >= 0) {
        if (const std::optional<uint32_t> index = selectionIndex(config_.selectionKeys, key->ch)) {
            if (*index < lastCount_) pick(client, *index);
            return true;
        }
    }

    if (!composing_) {
        const std::optional<std::string> left = client.leftContext();
        engine_.setLeftContext(left ? left->c_str() : nullptr);
    }
    if (key->kind == KindEnter && composing_ && lastSelected_ < 0) {
        // Let the rerank catch up, so what is sent is what it would have shown. One deadline covers a
        // running job and the newer one queued behind it; the newest result comes back here and is not
        // delivered a second time. Not with a selection open: the core would not apply it.
        if (ShanjieRerankJob *job = runner_->waitFor(std::chrono::milliseconds(enterRerankWaitMs_))) {
            onRerankDone(job);
            if (!composing_) return false; // the apply failed and the composition is gone
        }
    }

    const Result r = engine_.key(*key);
    if (r.code != 0) {
        fail(client, "shanjie_engine_key", r.code);
        return false;
    }
    // Pure pass-through: the core changed nothing, so the client is not touched.
    if (!r.out.handled && r.out.commit.empty()) return false;
    apply(client, r.out);
    return r.out.handled;
}

void Session::pick(Client &client, uint32_t index) {
    if (!engine_.ready() || owner_ != &client) return;
    if (client.isPassword()) {
        finish(client, false); // the field became sensitive after the list was shown
        return;
    }
    const Result r = engine_.pick(index);
    if (r.code == 2) return; // stale click: state unchanged
    if (r.code != 0) {
        fail(client, "shanjie_engine_pick", r.code);
        return;
    }
    apply(client, r.out);
}

void Session::finish(Client &client, bool commit) {
    if (!engine_.ready() || owner_ != &client || !composing_) return;
    runner_->cancelAll();
    if (client.isPassword()) commit = false;
    const Result r = engine_.reset(commit ? 0 : 1);
    if (r.code != 0) {
        fail(client, "shanjie_engine_reset", r.code);
        return;
    }
    apply(client, r.out);
}

void Session::capabilityChanged(Client &client) {
    if (owner_ == &client && client.isPassword()) finish(client, false);
}

void Session::clientDestroyed(Client &client) {
    if (owner_ != &client) return;
    owner_ = nullptr;
    runner_->cancelAll();
    if (composing_ && engine_.ready()) engine_.reset(1);
    composing_ = false;
    lastVertical_ = 0;
    lastSelected_ = -1;
    lastCount_ = 0;
    lastPreedit_.reset();
}

void Session::setConfig(const Config &config) {
    if (owner_) finish(*owner_, true);
    const bool rebuild = CoreEngine::needsRebuild(config_, config) || !engine_.ready();
    config_ = config;
    if (rebuild) {
        runner_->cancelAll(); // jobs of the old engine must not reach the new one
        engine_.build(paths_, config_);
        composing_ = false;
        lastVertical_ = 0;
        lastSelected_ = -1;
        lastCount_ = 0;
    lastCount_ = 0;
        lastPreedit_.reset();
    } else {
        engine_.applySettings(config_);
    }
}

void Session::apply(Client &client, const Output &out) {
    if (!out.commit.empty()) client.commit(out.commit);

    if (out.preedit.empty()) {
        if (lastPreedit_) {
            client.clearPreedit();
            lastPreedit_.reset();
        }
    } else {
        const size_t cursor = utf16ToByteOffset(out.preedit, out.cursorUtf16);
        const size_t enStart = utf16ToByteOffset(out.preedit, out.englishStartUtf16);
        const size_t enEnd = utf16ToByteOffset(out.preedit, out.englishStartUtf16 + out.englishLenUtf16);
        auto now = std::make_tuple(out.preedit, cursor, enStart, enEnd);
        // A commit replaced the client's preedit, so it is always sent again after one.
        if (!out.commit.empty() || !lastPreedit_ || *lastPreedit_ != now) {
            client.setPreedit(out.preedit, cursor, enStart, enEnd);
            lastPreedit_ = std::move(now);
        }
    }
    composing_ = !out.preedit.empty();
    lastVertical_ = out.vertical;
    lastSelected_ = out.candidates.empty() ? -1 : out.selected;
    lastCount_ = static_cast<uint32_t>(out.candidates.size());

    if (out.candidates.empty()) {
        client.hideCandidates();
    } else {
        client.showCandidates(out, selectionKeyChars(config_.selectionKeys));
    }
    ShanjieRerankJob *job = nullptr;
    const int32_t code = engine_.takeJob(&job);
    if (code != 0) {
        // Code 4: the core has discarded the composition, so what is on screen must go too.
        fail(client, "shanjie_engine_rerank_take", code);
        return;
    }
    if (job) runner_->submit(job);
}

void Session::fail(Client &client, const char *what, int32_t code) {
    if (log_) log_(what, code);
    engine_.reset(1); // the core resets itself only on code 4
    if (lastPreedit_) client.clearPreedit();
    client.hideCandidates();
    lastPreedit_.reset();
    composing_ = false;
    lastVertical_ = 0;
    lastSelected_ = -1;
    lastCount_ = 0;
    runner_->cancelAll();
}

void Session::onRerankDone(ShanjieRerankJob *job) {
    if (owner_ && owner_->isPassword()) {
        // The field became sensitive while the job ran: no update reaches it, the composition goes.
        shanjie_rerank_free(job);
        finish(*owner_, false);
        return;
    }
    const Result r = engine_.ready() ? engine_.applyJob(job) : Result{SHANJIE_NO_CHANGE, {}};
    shanjie_rerank_free(job);
    if (r.code == SHANJIE_NO_CHANGE) return;
    if (!owner_) return;
    if (r.code != 0) {
        fail(*owner_, "shanjie_engine_rerank_apply", r.code);
        return;
    }
    apply(*owner_, r.out);
}

} // namespace shanjie
