#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "shanjie.h"

namespace shanjie {

// Runs rerank jobs off the key path (docs/spec/semantic-rerank.md §6). submit, waitFor, cancelAll and
// the destructor are called on the main thread. A job is touched by one thread at a time.
class RerankRunner {
public:
    // Runs the callback on the main thread, later. Must be callable from any thread.
    using Post = std::function<void(std::function<void()>)>;
    // Receives a finished job on the main thread. The callee owns it and must free it.
    using Done = std::function<void(ShanjieRerankJob *)>;

    RerankRunner(Post post, Done done);
    ~RerankRunner();
    RerankRunner(const RerankRunner &) = delete;
    RerankRunner &operator=(const RerankRunner &) = delete;

    // Takes ownership. Replaces (and frees) a job that is queued but not started.
    void submit(ShanjieRerankJob *job);
    // Waits until no job is queued or running, at most `timeout`. Returns the finished job if there is
    // one (the caller owns it), else nullptr. A job returned here is not delivered through Done.
    ShanjieRerankJob *waitFor(std::chrono::milliseconds timeout);
    // Frees queued and finished jobs; a running job is freed when it ends and never delivered.
    void cancelAll();

private:
    void loop();
    void drain();

    Post post_;
    Done done_;
    std::mutex mutex_;
    std::condition_variable cv_;
    ShanjieRerankJob *queued_ = nullptr;
    ShanjieRerankJob *finished_ = nullptr;
    bool running_ = false;
    bool stop_ = false;
    uint64_t epoch_ = 0;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    std::thread thread_;
};

} // namespace shanjie
