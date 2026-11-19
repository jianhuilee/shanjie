#pragma once
#include <fcitx-utils/capabilityflags.h>
#include <fcitx/event.h>
#include <fcitx/surroundingtext.h>

#include <algorithm>

namespace fcitx {

// Whether the composition is committed (true) or dropped (false) when fcitx5 deactivates the input
// method for a context. `flags` are the context's capability flags at the time of the call.
//
// Nothing is ever committed on the way into a password or sensitive field. fcitx5 announces that
// transition before the flags change: as a capability event that carries the new flags, or as an input
// method switch whose reason is the capability change (the field stops accepting this input method).
// The current flags alone would still look harmless at that point.
inline bool commitOnDeactivate(const InputContextEvent &event, CapabilityFlags flags) {
    if (flags.testAny(CapabilityFlag::PasswordOrSensitive)) return false;
    switch (event.type()) {
    case EventType::InputContextCapabilityAboutToChange:
    case EventType::InputContextCapabilityChanged:
        if (static_cast<const CapabilityEvent &>(event).newFlags().testAny(CapabilityFlag::PasswordOrSensitive))
            return false;
        break;
    case EventType::InputContextSwitchInputMethod:
        // A switch the person asked for sends what is shown; one forced by a capability change does not.
        return static_cast<const InputContextSwitchInputMethodEvent &>(event).reason() !=
               InputMethodSwitchedReason::CapabilityChanged;
    default:
        break;
    }
    // Focus-out and the rest: fcitx5 (or the client) commits a client preedit itself, so committing
    // here too would insert the text twice. Commit only a preedit shown in the panel.
    return !flags.test(CapabilityFlag::Preedit);
}

// Whether a capability change moves the preedit between the application (inline) and fcitx5's panel.
inline bool preeditMoves(const CapabilityEvent &event) {
    return event.oldFlags().test(CapabilityFlag::Preedit) != event.newFlags().test(CapabilityFlag::Preedit);
}

// Where typed text goes in: the start of the selection, whichever way it was made, since the selected
// text is about to be replaced. The left context is the text before this point.
inline unsigned int insertionPoint(const SurroundingText &st) { return std::min(st.cursor(), st.anchor()); }

} // namespace fcitx
