#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>

#include "core_engine.h"
#include "keymap.h"
#include "rerank_runner.h"
#include "task_thread.h"

namespace shanjie {

// The application side, one per input context. Implemented by the fcitx5 adapter and by a fake in tests.
class Client {
public:
    virtual ~Client() = default;
    virtual void commit(const std::string &text) = 0;
    // Offsets are bytes in `text`. The English run is [englishStart, englishEnd); equal means none.
    virtual void setPreedit(const std::string &text, size_t cursor, size_t englishStart, size_t englishEnd) = 0;
    virtual void clearPreedit() = 0;
    // `selectionKeys` are the keys that pick the items, in order; empty means the digits 1-9. An item
    // beyond the string has no key.
    virtual void showCandidates(const Output &out, const std::string &selectionKeys) = 0;
    virtual void hideCandidates() = 0;
    virtual bool isPassword() const = 0;
    // The text before the cursor, or nullopt when the application does not provide it.
    virtual std::optional<std::string> leftContext() const = 0;
};

// The shell: one engine, and the client that owns the open composition (docs/spec/macos-shell.md §2.2).
class Session {
public:
    // How long Enter waits for a running rerank (docs/spec/semantic-rerank.md, ENTER_RERANK_WAIT_MS).
    static constexpr int kEnterRerankWaitMs = 100;

    // Runs a task on the background thread of the word-pack builds. Empty: the session's own TaskThread;
    // tests hold the tasks and run them by hand.
    using Spawn = std::function<void(std::function<void()>)>;

    Session(Paths paths, Config config, RerankRunner::Post post, CoreEngine::Log log, Spawn spawn = {});
    ~Session();

    bool ready() const { return engine_.ready(); }
    // True when the key was consumed. False: let the application have it.
    bool handleKey(Client &client, const KeyInput &in);
    // A click on candidate `index` of the list last shown to `client`.
    void pick(Client &client, uint32_t index);
    // Ends the composition if `client` owns it: commit sends what is shown, otherwise it is dropped.
    // Nothing is ever committed into a password or sensitive field: there it is always dropped.
    // Also the answer to a reset the application asks for while it keeps the focus. Pending rerank jobs
    // are cancelled, so a late result cannot bring the text back.
    void finish(Client &client, bool commit);
    // `client`'s field changed kind. If it became a password or sensitive field while it owns a
    // composition, the composition is dropped and pending rerank jobs are cancelled.
    void capabilityChanged(Client &client);
    // The preedit changes place (inline in the application, or in fcitx5's panel). preeditMoving is
    // called while the old place still applies and takes the owner's preedit out of it; preeditMoved puts
    // it back, unchanged, in the new place. Otherwise the old place would keep a copy, and focus-out
    // would commit it twice or not at all.
    void preeditMoving(Client &client);
    void preeditMoved(Client &client);
    // `client` is going away; drops a composition it owns.
    void clientDestroyed(Client &client);
    // A layout change commits an open composition and rebuilds the engine here and now. A word-pack
    // change builds the new engine in the background and swaps it in when no composition is open; the
    // current engine keeps typing meanwhile. Every other change goes to the live engine and leaves the
    // composition open.
    void setConfig(const Config &config);
    // Background builds started, engines swapped in, and swaps that had to open the learning file again
    // (tests).
    int engineBuilds() const { return engineBuilds_; }
    int engineSwaps() const { return engineSwaps_; }
    int learningReopens() const { return engine_.learningReopens(); }
    // How long Enter waits for the rerank; tests lengthen it.
    void setEnterRerankWaitMs(int ms) { enterRerankWaitMs_ = ms; }

private:
    void claim(Client &client);
    void apply(Client &client, const Output &out);
    void fail(Client &client, const char *what, int32_t code);
    void onRerankDone(ShanjieRerankJob *job);

    // What an engine was built for: a result is current only if this still matches what is wanted.
    struct Wanted {
        uint32_t layout = 0, packs = 0;
        bool operator==(const Wanted &) const = default;
    };
    Wanted wanted() const { return {config_.layout, config_.packs}; }
    void reconcile();
    void buildDone(Wanted forWhat, BuiltEngine built);
    void installIfReady();
    void scheduleInstall();
    void discardReady();
    void dispose(BuiltEngine built);

    Paths paths_;
    Config config_;
    RerankRunner::Post post_;
    CoreEngine::Log log_;
    CoreEngine engine_;
    std::unique_ptr<RerankRunner> runner_; // after engine_: destroyed first, so the worker is joined
    Client *owner_ = nullptr;
    bool composing_ = false;
    uint32_t lastVertical_ = 0;
    int32_t lastSelected_ = -1;
    uint32_t lastCount_ = 0;
    int enterRerankWaitMs_ = kEnterRerankWaitMs;
    // What the owner was last sent, to avoid resending an unchanged preedit.
    std::optional<std::tuple<std::string, size_t, size_t, size_t>> lastPreedit_;
    // The list last shown to the owner (empty when none), to relabel it when the selection keys change.
    Output lastShown_;

    // Word-pack switches (design §4.7). At most one build runs; a finished one waits in ready_ until no
    // composition is open. Every result is compared with what is wanted now, by what it was asked for.
    Wanted requested_;                    // what the current engine was built for
    bool buildInFlight_ = false;
    std::unique_ptr<BuiltEngine> ready_;  // built, current, not yet swapped in
    Wanted readyFor_;
    int engineBuilds_ = 0;
    int engineSwaps_ = 0;
    // Callbacks posted to the main loop check this before touching the session.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    // The two below are destroyed before every member above, so no task runs once those go.
    std::unique_ptr<TaskThread> thread_;
    Spawn spawn_;
};

} // namespace shanjie
