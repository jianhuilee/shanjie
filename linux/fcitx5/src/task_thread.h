#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace shanjie {

// One background thread that runs tasks in the order given: the word-pack builds and the freeing of
// engines nobody uses (docs/spec/macos-shell.md §2.1, the builder queue). run() is called on the main
// thread. The destructor drops the tasks not started (their captures are destroyed, freeing what they
// own) and waits for the running one.
class TaskThread {
public:
    using Task = std::function<void()>;

    TaskThread();
    ~TaskThread();
    TaskThread(const TaskThread &) = delete;
    TaskThread &operator=(const TaskThread &) = delete;

    void run(Task task);

private:
    void loop();

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Task> tasks_;
    bool stop_ = false;
    std::thread thread_;
};

} // namespace shanjie
