#pragma once
#include <memory>
#include <optional>
#include <string>

#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/handlertable.h>
#include <fcitx/event.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

#include "session.h"
#include "settings.h"

namespace fcitx {

class ShanjieEngine;

// One per input context: the session's view of the application.
class ShanjieClient : public InputContextProperty, public shanjie::Client {
public:
    ShanjieClient(ShanjieEngine *engine, InputContext *ic) : engine_(engine), ic_(ic) {}
    ~ShanjieClient() override;

    void commit(const std::string &text) override;
    void setPreedit(const std::string &text, size_t cursor, size_t englishStart, size_t englishEnd) override;
    void clearPreedit() override;
    void showCandidates(const shanjie::Output &out, const std::string &selectionKeys) override;
    void hideCandidates() override;
    bool isPassword() const override;
    std::optional<std::string> leftContext() const override;

private:
    ShanjieEngine *engine_;
    InputContext *ic_;
};

class ShanjieEngine : public InputMethodEngineV2 {
public:
    explicit ShanjieEngine(Instance *instance);
    ~ShanjieEngine() override;

    void keyEvent(const InputMethodEntry &entry, KeyEvent &event) override;
    void deactivate(const InputMethodEntry &entry, InputContextEvent &event) override;
    void reset(const InputMethodEntry &entry, InputContextEvent &event) override;

    const Configuration *getConfig() const override { return &config_; }
    void setConfig(const RawConfig &raw) override;
    void reloadConfig() override;

    void pick(InputContext *ic, uint32_t index);
    shanjie::Session *session() { return session_.get(); }

private:
    Instance *instance_;
    ShanjieConfig config_;
    EventDispatcher dispatcher_;                // before session_: outlives it
    std::unique_ptr<shanjie::Session> session_; // before factory_: outlives the per-context clients
    FactoryFor<ShanjieClient> factory_;
    std::unique_ptr<HandlerTableEntry<EventHandler>> capabilityAboutToChangeWatcher_; // last two: removed first
    std::unique_ptr<HandlerTableEntry<EventHandler>> capabilityWatcher_;
};

class ShanjieFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override { return new ShanjieEngine(manager->instance()); }
};

} // namespace fcitx
