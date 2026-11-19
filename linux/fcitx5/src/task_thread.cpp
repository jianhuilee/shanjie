#include "task_thread.h"

namespace shanjie {

TaskThread::TaskThread() : thread_([this] { loop(); }) {}

TaskThread::~TaskThread() {
    std::deque<Task> dropped;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        dropped.swap(tasks_);
    }
    cv_.notify_all();
    thread_.join();
    // `dropped` is destroyed here, on the main thread, after the worker has stopped.
}

void TaskThread::run(Task task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push_back(std::move(task));
    }
    cv_.notify_one();
}

void TaskThread::loop() {
    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
            if (stop_) return;
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        task();
    }
}

} // namespace shanjie
