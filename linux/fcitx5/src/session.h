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

namespace shanjie {

// The application side, one per input context. Implemented by the fcitx5 adapter and by a fake in tests.
class Client {
public:
    virtual ~Client() = default;
    virtual void commit(const std::string &text) = 0;
    // Offsets are bytes in `text`. The English run is [englishStart, englishEnd); equal means none.
    virtual void setPreedit(const std::string &text, size_t cursor, size_t englishStart, size_t englishEnd) = 0;
    virtual void clearPreedit() = 0;
    virtual void showCandidates(const Output &out) = 0;
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

    Session(Paths paths, Config config, RerankRunner::Post post, CoreEngine::Log log);
    ~Session();

    bool ready() const { return engine_.ready(); }
    // True when the key was consumed. False: let the application have it.
    bool handleKey(Client &client, const KeyInput &in);
    // A click on candidate `index` of the list last shown to `client`.
    void pick(Client &client, uint32_t index);
    // Ends the composition if `client` owns it: commit sends what is shown, otherwise it is dropped.
    void finish(Client &client, bool commit);
    // `client` is going away; drops a composition it owns.
    void clientDestroyed(Client &client);
    // Commits an open composition, then rebuilds the engine or resends the settings.
    void setConfig(const Config &config);

private:
    void claim(Client &client);
    void apply(Client &client, const Output &out);
    void fail(Client &client, const char *what, int32_t code);
    void onRerankDone(ShanjieRerankJob *job);

    Paths paths_;
    Config config_;
    CoreEngine::Log log_;
    CoreEngine engine_;
    std::unique_ptr<RerankRunner> runner_; // after engine_: destroyed first, so the worker is joined
    Client *owner_ = nullptr;
    bool composing_ = false;
    uint32_t lastVertical_ = 0;
    // What the owner was last sent, to avoid resending an unchanged preedit.
    std::optional<std::tuple<std::string, size_t, size_t, size_t>> lastPreedit_;
};

} // namespace shanjie
