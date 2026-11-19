// Word-pack switches build the new engine in the background and swap it in between compositions
// (design §4.7, as docs/spec/macos-shell.md §2.1). The background tasks are held here and run by hand, so
// every ordering is under the test's control; results come back through a hand-pumped queue.
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "session.h"

using namespace shanjie;

namespace {

struct FakeClient : Client {
    std::vector<std::string> commits;
    std::string preedit;
    bool candidatesVisible = false;
    void commit(const std::string &text) override { commits.push_back(text); }
    void setPreedit(const std::string &text, size_t, size_t, size_t) override { preedit = text; }
    void clearPreedit() override { preedit.clear(); }
    void showCandidates(const Output &, const std::string &) override { candidatesVisible = true; }
    void hideCandidates() override { candidatesVisible = false; }
    bool isPassword() const override { return false; }
    std::optional<std::string> leftContext() const override { return std::nullopt; }
};

struct Pump {
    std::mutex m;
    std::vector<std::function<void()>> q;
    void post(std::function<void()> f) {
        std::lock_guard<std::mutex> l(m);
        q.push_back(std::move(f));
    }
    void run() {
        for (;;) {
            std::vector<std::function<void()>> now;
            {
                std::lock_guard<std::mutex> l(m);
                now.swap(q);
            }
            if (now.empty()) return;
            for (auto &f : now) f();
        }
    }
};

// The background thread, held: tasks wait until the test runs them.
struct Held {
    std::vector<std::function<void()>> tasks;
    void runAll() {
        while (!tasks.empty()) {
            auto t = std::move(tasks.front());
            tasks.erase(tasks.begin());
            t();
        }
    }
};

KeyInput key(uint32_t evdev) {
    KeyInput in;
    in.evdev = evdev;
    return in;
}

// ㄕㄣˊ ㄧㄡˊ ㄕˇ: 榊遊矢 with the anime pack (a name from it), 神遊使 without.
const std::initializer_list<uint32_t> kPackKeys = {34, 25, 7, 22, 52, 7, 34, 4};
const std::string kWithPack = "\xe6\xa6\x8a\xe9\x81\x8a\xe7\x9f\xa2";
const std::string kWithoutPack = "\xe7\xa5\x9e\xe9\x81\x8a\xe4\xbd\xbf";
constexpr uint32_t kSpace = 57;

std::string typed(Session &s, FakeClient &c) {
    for (uint32_t k : kPackKeys) s.handleKey(c, key(k));
    const std::string shown = c.preedit;
    s.finish(c, false);
    return shown;
}

} // namespace

int main() {
    REQUIRE_FILE(SHANJIE_TEST_LM "/bigram.sjlm");
    Paths paths;
    paths.lexicon = SHANJIE_TEST_LEXICON;
    paths.packs = SHANJIE_TEST_PACKS;
    paths.lm = SHANJIE_TEST_LM;
    Config withPack;
    Config withoutPack = withPack;
    withoutPack.packs &= ~SHANJIE_PACK_ACG;

    Pump pump;
    auto post = [&](std::function<void()> f) { pump.post(std::move(f)); };

    // Idle: the switch returns at once and the current engine keeps typing; the new one comes in when
    // its build is done.
    {
        Held held;
        Session s(paths, withPack, post, {}, [&](std::function<void()> t) { held.tasks.push_back(std::move(t)); });
        FakeClient c;
        CHECK(typed(s, c) == kWithPack);
        s.setConfig(withoutPack);
        CHECK(held.tasks.size() == 1 && s.engineBuilds() == 1);
        CHECK(typed(s, c) == kWithPack); // still the old engine
        held.runAll();
        pump.run();
        CHECK(typed(s, c) == kWithoutPack);
        CHECK(c.commits.empty());
        CHECK(held.tasks.size() == 1); // the old engine, freed in the background
        held.runAll();
        // The same switch again changes nothing: no build.
        s.setConfig(withoutPack);
        CHECK(held.tasks.empty() && s.engineBuilds() == 1);
    }

    // During a composition: nothing is committed, the list stays usable, and the new engine comes in only
    // after the composition ends.
    {
        Held held;
        Session s(paths, withPack, post, {}, [&](std::function<void()> t) { held.tasks.push_back(std::move(t)); });
        FakeClient c;
        for (uint32_t k : kPackKeys) s.handleKey(c, key(k));
        s.handleKey(c, key(kSpace)); // the candidate window
        CHECK(c.candidatesVisible);
        s.setConfig(withoutPack);
        CHECK(c.commits.empty() && c.candidatesVisible && c.preedit == kWithPack);
        held.runAll();
        pump.run();
        CHECK(c.commits.empty() && c.candidatesVisible && c.preedit == kWithPack); // not swapped yet
        s.finish(c, true); // the composition ends
        CHECK(c.commits.size() == 1 && c.commits[0] == kWithPack);
        CHECK(typed(s, c) == kWithPack); // the swap is posted, never done inside the call that ended it
        pump.run();
        CHECK(typed(s, c) == kWithoutPack);
    }

    // Rapid switches: one build at a time; a result that no longer matches is dropped and the latest wish
    // is built.
    {
        Held held;
        Session s(paths, withPack, post, {}, [&](std::function<void()> t) { held.tasks.push_back(std::move(t)); });
        FakeClient c;
        Config itOff = withPack;
        itOff.packs &= ~SHANJIE_PACK_IT;
        s.setConfig(withoutPack);
        s.setConfig(itOff); // while the first build is held
        CHECK(held.tasks.size() == 1 && s.engineBuilds() == 1);
        held.runAll(); // the first result is stale: dropped, and itOff is built
        pump.run();
        CHECK(s.engineBuilds() == 2);
        held.runAll();
        pump.run();
        CHECK(typed(s, c) == kWithPack); // itOff keeps the anime pack
        CHECK(s.engineBuilds() == 2);
    }

    // Back to the current packs before the build ends: the result is dropped and nothing is rebuilt.
    {
        Held held;
        Session s(paths, withPack, post, {}, [&](std::function<void()> t) { held.tasks.push_back(std::move(t)); });
        FakeClient c;
        s.setConfig(withoutPack);
        s.setConfig(withPack);
        held.runAll();
        pump.run();
        held.runAll();
        pump.run();
        CHECK(s.engineBuilds() == 1);
        CHECK(typed(s, c) == kWithPack);
    }

    // A layout change is synchronous and makes a pack build in flight stale.
    {
        Held held;
        Session s(paths, withPack, post, {}, [&](std::function<void()> t) { held.tasks.push_back(std::move(t)); });
        FakeClient c;
        s.setConfig(withoutPack);
        Config eten = withoutPack;
        eten.layout = 1;
        s.setConfig(eten); // builds here and now, with the new packs
        held.runAll();
        pump.run();
        held.runAll();
        pump.run();
        CHECK(s.engineBuilds() == 1); // the stale result was not followed by another build
        CHECK(s.ready());
    }

    // The session goes away with a build held or its result still queued: nothing is touched afterwards.
    {
        Held held;
        {
            Session s(paths, withPack, post, {}, [&](std::function<void()> t) { held.tasks.push_back(std::move(t)); });
            s.setConfig(withoutPack);
        }
        held.runAll(); // builds, posts its result
        pump.run();    // the result finds no session and frees its engine
        CHECK(held.tasks.empty());
    }

    // The real background thread: a switch is built off this thread and swapped in here.
    {
        Session s(paths, withPack, post, {});
        FakeClient c;
        s.setConfig(withoutPack);
        std::string got;
        for (int i = 0; i < 400 && got != kWithoutPack; i++) {
            pump.run();
            got = typed(s, c);
            if (got != kWithoutPack) std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        CHECK(got == kWithoutPack);
    }
    TEST_MAIN_END();
}
