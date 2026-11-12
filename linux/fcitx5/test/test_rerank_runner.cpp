#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "check.h"
#include "core_engine.h"
#include "keymap.h"
#include "rerank_runner.h"

using namespace shanjie;
using namespace std::chrono_literals;

// A stand-in for the main loop: callbacks queue up and run when the test pumps.
struct Pump {
    std::mutex m;
    std::vector<std::function<void()>> q;
    void post(std::function<void()> f) {
        std::lock_guard<std::mutex> l(m);
        q.push_back(std::move(f));
    }
    int run() {
        std::vector<std::function<void()>> take;
        {
            std::lock_guard<std::mutex> l(m);
            take.swap(q);
        }
        for (auto &f : take) f();
        return static_cast<int>(take.size());
    }
    // Pumps until `done` or two seconds pass.
    template <typename P> bool until(P done) {
        for (int i = 0; i < 400 && !done(); i++) {
            run();
            if (!done()) std::this_thread::sleep_for(5ms);
        }
        return done();
    }
};

static ShanjieRerankJob *jobAfterTyping(CoreEngine &e, const char *keys) {
    ShanjieRerankJob *last = nullptr;
    for (; *keys; keys++) {
        ShanjieKey k = *keys == ' ' ? ShanjieKey{KindSpace, 0, 0}
                                    : ShanjieKey{KindChar, static_cast<uint32_t>(*keys), 0};
        if (e.key(k).code != 0) break;
        ShanjieRerankJob *j = nullptr;
        if (e.takeJob(&j) == 0 && j) {
            shanjie_rerank_free(last);
            last = j;
        }
    }
    return last;
}

int main() {
    REQUIRE_FILE(SHANJIE_TEST_LM "/tinylm.sjtl");
    Paths p;
    p.lexicon = SHANJIE_TEST_LEXICON;
    p.packs = SHANJIE_TEST_PACKS;
    p.lm = SHANJIE_TEST_LM;
    CoreEngine e;
    CHECK(e.build(p, Config{}));

    Pump pump;
    std::vector<ShanjieRerankJob *> delivered;
    {
        RerankRunner runner([&](std::function<void()> f) { pump.post(std::move(f)); },
                            [&](ShanjieRerankJob *j) { delivered.push_back(j); });

        // A job goes out and comes back once, on the pumping thread.
        // 今天天氣很好: rerank is on by default, so a composition change leaves a job.
        ShanjieRerankJob *job = jobAfterTyping(e, "rup wu0 wu0 fu4cp3cl3");
        CHECK(job != nullptr);
        if (job) {
            runner.submit(job);
            CHECK(pump.until([&] { return !delivered.empty(); }));
            CHECK(delivered.size() == 1 && delivered[0] == job);
            const int32_t code = e.applyJob(job).code;
            CHECK(code == 0 || code == SHANJIE_NO_CHANGE);
            CHECK(e.applyJob(job).code == SHANJIE_NO_CHANGE); // a second apply never changes anything
            shanjie_rerank_free(job);
        }
        e.reset(1);
        delivered.clear();

        // waitFor hands the job back directly; Done is not called for it.
        job = jobAfterTyping(e, "rup wu0 wu0 fu4cp3cl3");
        CHECK(job != nullptr);
        if (job) {
            runner.submit(job);
            ShanjieRerankJob *got = runner.waitFor(5000ms);
            CHECK(got == job);
            pump.run();
            CHECK(delivered.empty());
            shanjie_rerank_free(got);
        }
        e.reset(1);

        // One wait covers a stale job and the current one behind it: the newest job comes back, once.
        ShanjieRerankJob *stale = jobAfterTyping(e, "rup wu0 wu0 fu4cp3cl3");
        ShanjieRerankJob *current = jobAfterTyping(e, "su3cl3");
        CHECK(stale != nullptr && current != nullptr && stale != current);
        if (stale && current) {
            runner.submit(stale);
            runner.submit(current); // stale is running or was replaced in the queue
            ShanjieRerankJob *got = runner.waitFor(5000ms);
            CHECK(got == current);
            pump.run(); // the posted completions find nothing left to deliver
            CHECK(delivered.empty());
            shanjie_rerank_free(got);
        } else {
            shanjie_rerank_free(stale);
            shanjie_rerank_free(current);
        }
        e.reset(1);

        // Nothing queued: waitFor returns at once with nothing.
        CHECK(runner.waitFor(10ms) == nullptr);

        // cancelAll: the job is never delivered.
        job = jobAfterTyping(e, "rup wu0 wu0 fu4cp3cl3");
        if (job) {
            runner.submit(job);
            runner.cancelAll();
            runner.waitFor(5000ms);
            pump.run();
            CHECK(delivered.empty());
        }
        e.reset(1);

        // Destroying the runner with a job in flight neither leaks nor delivers.
        job = jobAfterTyping(e, "rup wu0 wu0 fu4cp3cl3");
        if (job) runner.submit(job);
    }
    pump.run(); // a callback posted before destruction must do nothing
    CHECK(delivered.empty());
    TEST_MAIN_END();
}
