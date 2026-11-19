#include "engine.h"

#include <filesystem>

#include <fcitx-config/iniparser.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/fs.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/surroundingtext.h>
#include <fcitx/userinterface.h>

#include "candidates.h"
#include "fcitx_key.h"
#include "fcitx_policy.h"
#include "utf.h"

namespace fcitx {

namespace {

const char kConfPath[] = "conf/shanjie.conf";
const uint32_t kLeftContextChars = 64; // shanjie.h, set_left_context

// The directory that holds `file` under fcitx5's data directories (user first, then system), or "".
std::string dirHolding(const char *file) {
    const std::filesystem::path found = StandardPaths::global().locate(StandardPathsType::PkgData, file);
    return found.empty() ? std::string() : found.parent_path().string();
}

shanjie::Paths findPaths() {
    shanjie::Paths p;
    p.lexicon = dirHolding("shanjie/lexicon/mcbpmf-data.txt");
    p.packs = dirHolding("shanjie/packs/acg-add.tsv");
    // A missing pack file adds nothing, but the directory argument must not be empty.
    if (p.packs.empty() && !p.lexicon.empty())
        p.packs = (std::filesystem::path(p.lexicon).parent_path() / "packs").string();
    // The model files are paired by hash, so one directory serves them all.
    p.lm = dirHolding("shanjie/lm/bigram.sjlm");
    const std::filesystem::path user = StandardPaths::global().userDirectory(StandardPathsType::PkgData);
    if (!user.empty() && fs::makePath(user / "shanjie")) p.learn = (user / "shanjie" / "learn").string();
    return p;
}

// R2: a static description and a return code. Never text, never a path.
void logCode(const char *what, int32_t code) { FCITX_ERROR() << "shanjie: " << what << " code " << code; }

} // namespace

// ---- ShanjieClient

ShanjieClient::~ShanjieClient() {
    if (auto *session = engine_->session()) session->clientDestroyed(*this);
}

void ShanjieClient::commit(const std::string &text) { ic_->commitString(text); }

void ShanjieClient::setPreedit(const std::string &text, size_t cursor, size_t englishStart, size_t englishEnd) {
    const TextFormatFlags plain = TextFormatFlag::Underline;
    Text preedit;
    if (englishEnd > englishStart && englishEnd <= text.size()) {
        TextFormatFlags english = TextFormatFlag::Underline;
        english |= TextFormatFlag::Bold;
        preedit.append(text.substr(0, englishStart), plain);
        preedit.append(text.substr(englishStart, englishEnd - englishStart), english);
        preedit.append(text.substr(englishEnd), plain);
    } else {
        preedit.append(text, plain);
    }
    preedit.setCursor(static_cast<int>(cursor));
    auto &panel = ic_->inputPanel();
    if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) {
        panel.setClientPreedit(preedit);
        ic_->updatePreedit();
    } else {
        panel.setPreedit(preedit);
    }
    ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void ShanjieClient::clearPreedit() {
    auto &panel = ic_->inputPanel();
    panel.setClientPreedit(Text());
    panel.setPreedit(Text());
    ic_->updatePreedit();
    ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void ShanjieClient::showCandidates(const shanjie::Output &out, const std::string &selectionKeys) {
    ic_->inputPanel().setCandidateList(std::make_unique<ShanjieCandidateList>(engine_, out, selectionKeys));
    ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void ShanjieClient::hideCandidates() {
    ic_->inputPanel().setCandidateList(nullptr);
    ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
}

bool ShanjieClient::isPassword() const {
    return ic_->capabilityFlags().testAny(CapabilityFlag::PasswordOrSensitive);
}

std::optional<std::string> ShanjieClient::leftContext() const {
    if (!ic_->capabilityFlags().test(CapabilityFlag::SurroundingText)) return std::nullopt;
    const SurroundingText &st = ic_->surroundingText();
    if (!st.isValid()) return std::nullopt;
    // A terminal's surrounding text is commands and their output: only the trailing Han characters
    // may go to the core (shanjie.h, set_left_context).
    const bool terminal = ic_->capabilityFlags().test(CapabilityFlag::Terminal);
    return shanjie::leftContextFor(st.text(), insertionPoint(st), kLeftContextChars, terminal);
}

// ---- ShanjieEngine

ShanjieEngine::ShanjieEngine(Instance *instance)
    : instance_(instance), factory_([this](InputContext &ic) { return new ShanjieClient(this, &ic); }) {
    dispatcher_.attach(&instance_->eventLoop());
    readAsIni(config_, kConfPath);
    session_ = std::make_unique<shanjie::Session>(
        findPaths(), toCoreConfig(config_),
        [this](std::function<void()> f) { dispatcher_.schedule(std::move(f)); }, logCode);
    if (!session_->ready()) FCITX_ERROR() << "shanjie: no engine; every key is passed through";
    instance_->inputContextManager().registerProperty("shanjieClient", &factory_);
    // Inline preedit turned on or off: the preedit leaves its old place while the old flags still hold
    // (InputContext::updatePreedit sends nothing once the Preedit flag is gone) ...
    capabilityAboutToChangeWatcher_ = instance_->watchEvent(
        EventType::InputContextCapabilityAboutToChange, EventWatcherPhase::Default, [this](Event &event) {
            auto &capEvent = static_cast<CapabilityEvent &>(event);
            if (preeditMoves(capEvent))
                session_->preeditMoving(*capEvent.inputContext()->propertyFor(&factory_));
        });
    // ... and comes back in the new one. A field can also turn into a password or sensitive one while it
    // holds the focus; then the composition is dropped instead.
    capabilityWatcher_ = instance_->watchEvent(
        EventType::InputContextCapabilityChanged, EventWatcherPhase::Default, [this](Event &event) {
            auto &capEvent = static_cast<CapabilityEvent &>(event);
            auto &client = *capEvent.inputContext()->propertyFor(&factory_);
            session_->capabilityChanged(client);
            if (preeditMoves(capEvent)) session_->preeditMoved(client);
        });
}

ShanjieEngine::~ShanjieEngine() = default;

void ShanjieEngine::keyEvent(const InputMethodEntry &, KeyEvent &event) {
    if (event.isRelease()) return;
    InputContext *ic = event.inputContext();
    const shanjie::KeyInput in = toKeyInput(event.rawKey());
    if (session_->handleKey(*ic->propertyFor(&factory_), in)) event.filterAndAccept();
}

void ShanjieEngine::deactivate(const InputMethodEntry &, InputContextEvent &event) {
    InputContext *ic = event.inputContext();
    // fcitx5 deactivates the input method on the way into a password field, before the flags change;
    // the policy reads the event so that nothing is committed there (fcitx_policy.h).
    session_->finish(*ic->propertyFor(&factory_), commitOnDeactivate(event, ic->capabilityFlags()));
}

// The application asks for a reset, possibly while it keeps the focus (it has dropped its preedit).
// Only the owner's composition is discarded; the session also clears what it showed and cancels the
// pending rerank jobs, so neither the next key nor a late result brings the old text back.
void ShanjieEngine::reset(const InputMethodEntry &, InputContextEvent &event) {
    session_->finish(*event.inputContext()->propertyFor(&factory_), false);
}

void ShanjieEngine::setConfig(const RawConfig &raw) {
    config_.load(raw, true);
    safeSaveAsIni(config_, kConfPath);
    session_->setConfig(toCoreConfig(config_));
}

void ShanjieEngine::reloadConfig() {
    readAsIni(config_, kConfPath);
    session_->setConfig(toCoreConfig(config_));
}

void ShanjieEngine::pick(InputContext *ic, uint32_t index) {
    session_->pick(*ic->propertyFor(&factory_), index);
}

} // namespace fcitx

FCITX_ADDON_FACTORY_V2_BACKWARDS(shanjie, fcitx::ShanjieFactory)
