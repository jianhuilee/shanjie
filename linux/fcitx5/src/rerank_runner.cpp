#include "rerank_runner.h"

namespace shanjie {

RerankRunner::RerankRunner(Post post, Done done)
    : post_(std::move(post)), done_(std::move(done)), thread_([this] { loop(); }) {}

RerankRunner::~RerankRunner() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
    *alive_ = false; // callbacks already posted must not touch this object
    shanjie_rerank_free(queued_);
    shanjie_rerank_free(finished_);
}

void RerankRunner::submit(ShanjieRerankJob *job) {
    if (!job) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shanjie_rerank_free(queued_);
        queued_ = job;
    }
    cv_.notify_all();
}

ShanjieRerankJob *RerankRunner::waitFor(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, timeout, [this] { return !running_ && !queued_; });
    ShanjieRerankJob *job = finished_;
    finished_ = nullptr;
    return job;
}

void RerankRunner::cancelAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++epoch_;
    shanjie_rerank_free(queued_);
    queued_ = nullptr;
    shanjie_rerank_free(finished_);
    finished_ = nullptr;
}

void RerankRunner::loop() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        cv_.wait(lock, [this] { return stop_ || queued_; });
        if (stop_) return;
        ShanjieRerankJob *job = queued_;
        queued_ = nullptr;
        running_ = true;
        const uint64_t epoch = epoch_;
        lock.unlock();

        shanjie_rerank_run(job); // a failed run leaves no result; apply then reports "no change"

        lock.lock();
        running_ = false;
        const bool publish = epoch == epoch_ && !stop_;
        if (publish) {
            shanjie_rerank_free(finished_); // an older result is stale once a newer job ran
            finished_ = job;
        } else {
            shanjie_rerank_free(job);
        }
        cv_.notify_all();
        if (publish) {
            lock.unlock();
            post_([this, alive = alive_] {
                if (*alive) drain();
            });
            lock.lock();
        }
    }
}

void RerankRunner::drain() {
    ShanjieRerankJob *job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job = finished_;
        finished_ = nullptr;
    }
    if (job) done_(job);
}

} // namespace shanjie
