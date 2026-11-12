#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "session.h"

using namespace shanjie;
using namespace std::chrono_literals;

static const std::string kNihao = "\xe4\xbd\xa0\xe5\xa5\xbd"; // 你好

struct FakeClient : Client {
    std::vector<std::string> commits;
    std::string preedit;
    size_t cursor = 0;
    int preeditSets = 0, preeditClears = 0, shows = 0, hides = 0;
    Output shown;
    bool candidatesVisible = false;
    bool password = false;
    std::optional<std::string> left;

    void commit(const std::string &text) override { commits.push_back(text); }
    void setPreedit(const std::string &text, size_t c, size_t, size_t) override {
        preedit = text;
        cursor = c;
        preeditSets++;
    }
    void clearPreedit() override {
        preedit.clear();
        preeditClears++;
    }
    void showCandidates(const Output &out) override {
        shown = out;
        candidatesVisible = true;
        shows++;
    }
    void hideCandidates() override {
        candidatesVisible = false;
        hides++;
    }
    bool isPassword() const override { return password; }
    std::optional<std::string> leftContext() const override { return left; }
};

struct Pump {
    std::mutex m;
    std::vector<std::function<void()>> q;
    void post(std::function<void()> f) {
        std::lock_guard<std::mutex> l(m);
        q.push_back(std::move(f));
    }
    void run() {
        std::vector<std::function<void()>> take;
        {
            std::lock_guard<std::mutex> l(m);
            take.swap(q);
        }
        for (auto &f : take) f();
    }
};

// evdev codes of the keys the tests press.
enum : uint32_t {
    K3 = 4, KE = 18, KU = 22, KEnter = 28, KS = 31, KH = 35, KL = 38, KLeftShift = 42, KZ = 44, KC = 46,
    KN = 49, KSpace = 57, KF1 = 59,
};

static KeyInput key(uint32_t evdev) {
    KeyInput in;
    in.evdev = evdev;
    return in;
}

static void press(Session &s, Client &c, std::initializer_list<uint32_t> keys) {
    for (uint32_t k : keys) s.handleKey(c, key(k));
}

int main() {
    REQUIRE_FILE(SHANJIE_TEST_LM "/bigram.sjlm");
    Paths paths;
    paths.lexicon = SHANJIE_TEST_LEXICON;
    paths.packs = SHANJIE_TEST_PACKS;
    paths.lm = SHANJIE_TEST_LM;
    Pump pump;
    Session s(paths, Config{}, [&](std::function<void()> f) { pump.post(std::move(f)); }, {});
    CHECK(s.ready());

    // Typing 你好 and Enter.
    {
        FakeClient c;
        CHECK(s.handleKey(c, key(KS)));
        CHECK(!c.preedit.empty());
        press(s, c, {KU, K3, KC, KL, K3});
        CHECK(c.preedit == kNihao);
        CHECK(c.cursor == 6); // bytes, not UTF-16 units
        CHECK(c.commits.empty());
        CHECK(s.handleKey(c, key(KEnter)));
        CHECK(c.commits.size() == 1 && c.commits[0] == kNihao);
        CHECK(c.preedit.empty() && c.preeditClears == 1);
        // With nothing composed, Enter belongs to the application and the client is not touched.
        const int sets = c.preeditSets, clears = c.preeditClears;
        CHECK(!s.handleKey(c, key(KEnter)));
        CHECK(c.commits.size() == 1 && c.preeditSets == sets && c.preeditClears == clears);
    }

    // Space opens the vertical window; a click picks.
    {
        FakeClient c;
        press(s, c, {KS, KU, K3, KSpace});
        CHECK(c.candidatesVisible);
        CHECK(c.shown.vertical == 1 && c.shown.selected == 0 && !c.shown.candidates.empty());
        s.pick(c, 0);
        CHECK(!c.candidatesVisible);
        CHECK(!c.preedit.empty());
        FakeClient other;
        s.pick(other, 0); // not the owner: ignored
        CHECK(other.commits.empty());
        s.finish(c, false);
        CHECK(c.commits.empty() && c.preedit.empty());
    }

    // A key outside the tables commits the composition and is passed on.
    {
        FakeClient c;
        press(s, c, {KS, KU, K3});
        CHECK(!s.handleKey(c, key(KF1)));
        CHECK(c.commits.size() == 1 && !c.commits[0].empty());
        CHECK(c.preedit.empty());
        CHECK(!s.handleKey(c, key(KF1))); // nothing composed: just passed on
        CHECK(c.commits.size() == 1);
    }

    // A modifier pressed alone neither commits nor is consumed.
    {
        FakeClient c;
        press(s, c, {KS, KU, K3});
        CHECK(!s.handleKey(c, key(KLeftShift)));
        CHECK(c.commits.empty() && !c.preedit.empty());
        s.finish(c, true);
        CHECK(c.commits.size() == 1 && c.preedit.empty());
        s.finish(c, true); // nothing open: nothing happens
        CHECK(c.commits.size() == 1);
    }

    // A key in another client first commits to the owner.
    {
        FakeClient a, b;
        press(s, a, {KS, KU, K3});
        CHECK(s.handleKey(b, key(KC)));
        CHECK(a.commits.size() == 1 && a.preedit.empty());
        CHECK(b.commits.empty() && !b.preedit.empty());
        s.finish(a, true); // a no longer owns anything
        CHECK(a.commits.size() == 1);
        s.clientDestroyed(b); // the owner goes away: its composition is dropped
        CHECK(b.commits.empty());
        FakeClient c;
        press(s, c, {KS, KU, K3});
        s.handleKey(c, key(KEnter));
        CHECK(c.commits.size() == 1 && !c.commits[0].empty()); // no leftover from b
    }

    // A password field is never composed in.
    {
        FakeClient c;
        c.password = true;
        CHECK(!s.handleKey(c, key(KS)));
        CHECK(c.commits.empty() && c.preedit.empty() && c.preeditSets == 0);
    }

    // An unchanged preedit is not resent.
    {
        FakeClient c;
        press(s, c, {KS, KU, K3});
        const int sets = c.preeditSets;
        s.handleKey(c, key(KLeftShift));
        CHECK(c.preeditSets == sets);
        s.finish(c, false);
    }

    // Rerank: jobs run and come back through the pump without disturbing a later commit.
    {
        FakeClient c;
        // rup wu0 wu0 fu4 cp3 cl3 (今天天氣很好) by evdev code
        press(s, c, {19, 22, 25, KSpace, 17, 22, 11, KSpace, 17, 22, 11, KSpace, 33, 22, 5, KC, 25, K3, KC, KL, K3});
        CHECK(!c.preedit.empty());
        for (int i = 0; i < 100; i++) {
            pump.run();
            std::this_thread::sleep_for(5ms);
        }
        CHECK(!c.preedit.empty());
        CHECK(s.handleKey(c, key(KEnter)));
        CHECK(c.commits.size() == 1 && !c.commits[0].empty());
        CHECK(c.preedit.empty());
        pump.run(); // a late result applies to nothing
        CHECK(c.commits.size() == 1 && c.preedit.empty());
    }

    // The ETen layout after a settings change; an open composition is committed first.
    {
        FakeClient c;
        press(s, c, {KS, KU, K3});
        Config eten;
        eten.layout = 1;
        s.setConfig(eten);
        CHECK(c.commits.size() == 1 && c.preedit.empty());
        CHECK(s.ready());
        press(s, c, {KN, KE, K3, KH, KZ, K3, KEnter});
        CHECK(c.commits.size() == 2 && c.commits[1] == kNihao);
        Config formal = eten;
        formal.profile = 1;
        s.setConfig(formal); // no rebuild
        press(s, c, {KN, KE, K3, KH, KZ, K3, KEnter});
        CHECK(c.commits.size() == 3 && c.commits[2] == kNihao);
    }

    // No lexicon: not ready, every key is passed on.
    {
        Paths bad = paths;
        bad.lexicon += "/does-not-exist";
        Session broken(bad, Config{}, [&](std::function<void()> f) { pump.post(std::move(f)); }, {});
        FakeClient c;
        CHECK(!broken.ready());
        CHECK(!broken.handleKey(c, key(KS)));
        broken.finish(c, true);
        broken.pick(c, 0);
        CHECK(c.commits.empty());
    }
    TEST_MAIN_END();
}
