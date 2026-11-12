#include <fcitx-utils/capabilityflags.h>
#include <fcitx/event.h>

#include "check.h"
#include "fcitx_policy.h"

using namespace fcitx;

// The events as fcitx5 builds them; none of these paths touches the input context.
int main() {
    const CapabilityFlags plain = CapabilityFlag::Preedit;
    const CapabilityFlags panelOnly = CapabilityFlag::NoFlag;
    CapabilityFlags password = plain;
    password |= CapabilityFlag::Password;
    CapabilityFlags sensitive = plain;
    sensitive |= CapabilityFlag::Sensitive;
    CapabilityFlags panelPassword = panelOnly;
    panelPassword |= CapabilityFlag::Password;

    // Into a password or sensitive field: never committed, although the flags still look harmless.
    CHECK(!commitOnDeactivate(CapabilityAboutToChangeEvent(nullptr, plain, password), plain));
    CHECK(!commitOnDeactivate(CapabilityAboutToChangeEvent(nullptr, plain, sensitive), plain));
    CHECK(!commitOnDeactivate(CapabilityAboutToChangeEvent(nullptr, panelOnly, panelPassword), panelOnly));
    CHECK(!commitOnDeactivate(CapabilityChangedEvent(nullptr, panelOnly, panelPassword), panelOnly));
    CHECK(!commitOnDeactivate(
        InputContextSwitchInputMethodEvent(InputMethodSwitchedReason::CapabilityChanged, "shanjie", nullptr), plain));
    CHECK(!commitOnDeactivate(
        InputContextSwitchInputMethodEvent(InputMethodSwitchedReason::CapabilityChanged, "shanjie", nullptr),
        panelOnly));

    // The flags already say password: nothing commits, whatever the event.
    CHECK(!commitOnDeactivate(
        InputContextSwitchInputMethodEvent(InputMethodSwitchedReason::Trigger, "shanjie", nullptr), password));
    CHECK(!commitOnDeactivate(FocusOutEvent(nullptr), panelPassword));
    CHECK(!commitOnDeactivate(FocusOutEvent(nullptr), sensitive));

    // A switch the person asked for sends the composition.
    for (auto reason : {InputMethodSwitchedReason::Trigger, InputMethodSwitchedReason::Enumerate,
                        InputMethodSwitchedReason::Deactivate, InputMethodSwitchedReason::GroupChange,
                        InputMethodSwitchedReason::Other}) {
        CHECK(commitOnDeactivate(InputContextSwitchInputMethodEvent(reason, "shanjie", nullptr), plain));
    }

    // Focus-out: the client preedit is committed by fcitx5 or the client; only a panel preedit is ours.
    CHECK(!commitOnDeactivate(FocusOutEvent(nullptr), plain));
    CHECK(commitOnDeactivate(FocusOutEvent(nullptr), panelOnly));

    // A capability change that is not about secrecy follows the focus rule.
    CapabilityFlags email = plain;
    email |= CapabilityFlag::Email;
    CHECK(!commitOnDeactivate(CapabilityAboutToChangeEvent(nullptr, plain, email), plain));
    CHECK(commitOnDeactivate(CapabilityAboutToChangeEvent(nullptr, panelOnly, CapabilityFlags(CapabilityFlag::Email)),
                             panelOnly));
    // Leaving a password field: the flags are still the password ones.
    CHECK(!commitOnDeactivate(CapabilityAboutToChangeEvent(nullptr, password, plain), password));
    TEST_MAIN_END();
}
