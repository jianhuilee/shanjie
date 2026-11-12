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
    std::string shownKeys;
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
    void showCandidates(const Output &out, const std::string &keys) override {
        shown = out;
        shownKeys = keys;
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
    KN = 49, KSpace = 57, KF1 = 59, KPageUp = 104, KPageDown = 109,
};

// ji3 y94 tj/6 283: the n-gram alone decodes it as 我在重打, the semantic rerank makes it 我再重打.
static const std::initializer_list<uint32_t> kRerankKeys = {36, 23, 4, 21, 10, 5, 20, 36, 53, 7, 3, 9, 4};
static const std::string kBeforeRerank = "\xe6\x88\x91\xe5\x9c\xa8\xe9\x87\x8d\xe6\x89\x93";
static const std::string kAfterRerank = "\xe6\x88\x91\xe5\x86\x8d\xe9\x87\x8d\xe6\x89\x93";

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

    // Page Up / Down outside a vertical window, or with a modifier, reach the application and leave
    // the composition alone; in the window, unmodified, they page it.
    {
        FakeClient c;
        press(s, c, {KS, KU, K3});
        const std::string before = c.preedit;
        const int sets = c.preeditSets;
        CHECK(!s.handleKey(c, key(KPageDown)));
        CHECK(!s.handleKey(c, key(KPageUp)));
        KeyInput shiftPage = key(KPageDown);
        shiftPage.shift = true;
        CHECK(!s.handleKey(c, shiftPage));
        CHECK(c.commits.empty() && c.preedit == before && c.preeditSets == sets && c.preeditClears == 0);

        s.handleKey(c, key(KSpace)); // opens the vertical window
        CHECK(c.candidatesVisible && c.shown.vertical == 1 && c.shown.first == 0);
        CHECK(c.shown.total > 9); // enough homophones of this syllable for a second page
        KeyInput ctrlPage = key(KPageDown);
        ctrlPage.ctrl = true;
        CHECK(!s.handleKey(c, ctrlPage));
        CHECK(c.commits.empty() && c.candidatesVisible && c.shown.first == 0);
        CHECK(s.handleKey(c, key(KPageDown)));
        CHECK(c.commits.empty() && c.candidatesVisible && c.shown.first == 9);
        CHECK(s.handleKey(c, key(KPageUp)));
        CHECK(c.candidatesVisible && c.shown.first == 0);
        s.finish(c, false);
        CHECK(c.commits.empty());
    }

    // With nothing composed a page key is simply passed on.
    {
        FakeClient c;
        CHECK(!s.handleKey(c, key(KPageDown)));
        CHECK(c.commits.empty() && c.preeditSets == 0);
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

    // A field that turns sensitive while it owns a composition: a click, a late rerank result, an
    // owner change and a settings change all drop the composition and send the field nothing more.
    {
        FakeClient c;
        press(s, c, {KS, KU, K3, KSpace});
        CHECK(c.candidatesVisible && !c.preedit.empty());
        const int sets = c.preeditSets;
        c.password = true;
        s.pick(c, 0);
        CHECK(c.commits.empty() && c.preedit.empty() && !c.candidatesVisible && c.preeditSets == sets);
        c.password = false;
        press(s, c, {KS, KU, K3, KC, KL, K3, KEnter});
        CHECK(c.commits.size() == 1 && c.commits[0] == kNihao); // nothing of the dropped composition
    }
    {
        FakeClient c;
        press(s, c, kRerankKeys);
        CHECK(c.preedit == kBeforeRerank);
        const int sets = c.preeditSets;
        c.password = true;
        for (int i = 0; i < 1000 && !c.preedit.empty(); i++) {
            pump.run();
            std::this_thread::sleep_for(5ms);
        }
        CHECK(c.preedit.empty() && c.commits.empty() && c.preeditSets == sets); // dropped, never updated
    }
    {
        FakeClient c;
        press(s, c, kRerankKeys);
        const int sets = c.preeditSets;
        s.capabilityChanged(c); // not sensitive: nothing happens
        CHECK(c.preedit == kBeforeRerank);
        c.password = true;
        s.capabilityChanged(c);
        CHECK(c.preedit.empty() && c.commits.empty() && c.preeditSets == sets);
        for (int i = 0; i < 60; i++) {
            pump.run();
            std::this_thread::sleep_for(5ms);
        }
        CHECK(c.preedit.empty() && c.commits.empty() && c.preeditSets == sets); // jobs were cancelled
    }
    {
        FakeClient a, b;
        press(s, a, {KS, KU, K3});
        a.password = true;
        CHECK(s.handleKey(b, key(KC))); // the owner changes; nothing is committed into the sensitive field
        CHECK(a.commits.empty() && a.preedit.empty());
        s.finish(b, false);
        FakeClient c;
        press(s, c, {KS, KU, K3});
        c.password = true;
        s.setConfig(Config{}); // a settings change commits an open composition, but not there
        CHECK(c.commits.empty() && c.preedit.empty());
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

    // Rerank, asynchronous completion: the n-gram result shows first, the reranked one replaces it when
    // the job comes back through the main loop, and Enter then sends the reranked text.
    {
        FakeClient c;
        press(s, c, kRerankKeys);
        CHECK(c.preedit == kBeforeRerank); // nothing was pumped yet
        for (int i = 0; i < 1000 && c.preedit != kAfterRerank; i++) {
            pump.run();
            std::this_thread::sleep_for(5ms);
        }
        CHECK(c.preedit == kAfterRerank);
        CHECK(c.cursor == kAfterRerank.size());
        CHECK(c.commits.empty());
        CHECK(s.handleKey(c, key(KEnter)));
        CHECK(c.commits.size() == 1 && c.commits[0] == kAfterRerank);
        CHECK(c.preedit.empty());
        pump.run(); // nothing late changes it
        CHECK(c.commits.size() == 1 && c.preedit.empty());
    }

    // Rerank, Enter's wait: Enter arrives before any completion ran on the main loop. It waits for the
    // job and sends the reranked text.
    {
        FakeClient c;
        s.setEnterRerankWaitMs(10000);
        press(s, c, kRerankKeys);
        CHECK(c.preedit == kBeforeRerank);
        CHECK(s.handleKey(c, key(KEnter))); // no pump.run() in between
        CHECK(c.commits.size() == 1 && c.commits[0] == kAfterRerank);
        CHECK(c.preedit.empty());
        pump.run(); // the completions posted meanwhile find nothing to deliver
        CHECK(c.commits.size() == 1 && c.preedit.empty());
        s.setEnterRerankWaitMs(Session::kEnterRerankWaitMs);
    }

    // Rerank, no wait: with a zero deadline Enter sends what is shown, and the late result is dropped.
    {
        FakeClient c;
        s.setEnterRerankWaitMs(0);
        press(s, c, kRerankKeys);
        CHECK(s.handleKey(c, key(KEnter)));
        CHECK(c.commits.size() == 1 && c.commits[0] == kBeforeRerank);
        for (int i = 0; i < 60; i++) {
            pump.run();
            std::this_thread::sleep_for(5ms);
        }
        CHECK(c.commits.size() == 1 && c.preedit.empty());
        s.setEnterRerankWaitMs(Session::kEnterRerankWaitMs);
    }

    // A reset the application asks for while it keeps the focus: the composition is dropped for good.
    {
        FakeClient c;
        press(s, c, {19, 22, 25, KSpace, 17, 22, 11, KSpace, 17, 22, 11, KSpace});
        CHECK(!c.preedit.empty());
        s.finish(c, false);
        CHECK(c.commits.empty() && c.preedit.empty() && !c.candidatesVisible);
        const int sets = c.preeditSets;
        for (int i = 0; i < 60; i++) {
            pump.run();
            std::this_thread::sleep_for(5ms);
        }
        CHECK(c.preeditSets == sets && c.preedit.empty() && c.commits.empty());
        press(s, c, {KS, KU, K3, KC, KL, K3, KEnter});
        CHECK(c.commits.size() == 1 && c.commits[0] == kNihao); // nothing left over from before
    }

    // Letter selection keys. By default (digits) a letter is not a selection key.
    {
        FakeClient c;
        press(s, c, {KS, KU, K3, KSpace});
        CHECK(c.candidatesVisible && c.shownKeys.empty() && c.shown.candidates.size() == 9);
        const std::string third = c.shown.candidates[2];
        CHECK(s.handleKey(c, key(4))); // the 3 key picks the third item
        CHECK(!c.candidatesVisible && c.preedit == third);
        s.finish(c, false);
    }
    {
        Config homeRow;
        homeRow.selectionKeys = 2; // a s d f j k l ;
        s.setConfig(homeRow);
        CHECK(s.ready());
        FakeClient c;
        press(s, c, {KS, KU, K3, KSpace});
        CHECK(c.candidatesVisible && c.shownKeys == "asdfjkl;" && c.shown.candidates.size() == 9);
        const std::string third = c.shown.candidates[2], eighth = c.shown.candidates[7];
        KeyInput shiftD = key(32);
        shiftD.shift = true;
        s.handleKey(c, shiftD); // a modified key is not a selection key
        CHECK(c.preedit != third);
        s.finish(c, false);

        press(s, c, {KS, KU, K3, KSpace});
        CHECK(s.handleKey(c, key(32))); // d: the third item
        CHECK(!c.candidatesVisible && c.preedit == third && c.commits.empty());
        s.finish(c, false);

        press(s, c, {KS, KU, K3, KSpace});
        CHECK(s.handleKey(c, key(39))); // ; : the eighth item
        CHECK(!c.candidatesVisible && c.preedit == eighth);
        s.finish(c, false);

        press(s, c, {KS, KU, K3, KSpace});
        CHECK(s.handleKey(c, key(4))); // the digits still pick
        CHECK(!c.candidatesVisible && c.preedit == third);
        s.finish(c, false);

        // With no selection open the same keys type Zhuyin as always.
        press(s, c, {KS, KU, K3, KC, KL, K3, KEnter});
        CHECK(c.commits.size() == 1 && c.commits[0] == kNihao);

        // The prediction row: only showing, a letter keeps typing; entered with Tab, it picks.
        FakeClient p;
        press(s, p, {KS, KU, K3, KC}); // 你 and an unfinished syllable: a row is showing
        CHECK(p.candidatesVisible && p.shown.selected == -1 && p.shown.vertical == 2);
        const std::string first = p.shown.candidates[0];
        const std::string beforeTab = p.preedit;
        CHECK(s.handleKey(p, key(15))); // Tab enters the row
        CHECK(p.shown.selected == 0 && p.preedit == beforeTab);
        CHECK(s.handleKey(p, key(30))); // a: the first item
        CHECK(p.preedit != beforeTab && p.preedit.find(first) != std::string::npos);
        s.finish(p, false);

        // A key beyond the items shown is eaten.
        FakeClient few;
        press(s, few, {KS, KU, K3, KC});
        s.handleKey(few, key(15));
        if (few.shown.candidates.size() < 8) {
            const std::string was = few.preedit;
            CHECK(s.handleKey(few, key(39)));
            CHECK(few.preedit == was && few.shown.selected == 0);
        }
        s.finish(few, false);
        s.setConfig(Config{});
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
