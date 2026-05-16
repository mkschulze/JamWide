#include "NativeCameraPrivacyDialog.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <utility>

namespace jamwide {

void NativeCameraPrivacyDialog::show(std::function<void(bool)> onAck)
{
    auto options = juce::MessageBoxOptions{}
        .withIconType(juce::MessageBoxIconType::InfoIcon)
        .withTitle("Camera privacy notice")
        .withMessage(
            "JamWide broadcasts your camera to the NINJAM server and peers "
            "in your room. Peers can save or redistribute their view. "
            "There's no separate IP exposure beyond what audio already does.")
        .withButton("I understand")   // button[0] → JUCE returns 1
        .withButton("Cancel");        // button[1] → JUCE returns 0

    // HIGH-7 prep — dispatch via the centralised label-keyed mapping.
    // The static helper pins the JUCE 2-button convention so callers (and
    // the unit-test) don't duplicate the magic-number "1 == ack" check.
    juce::AlertWindow::showAsync(options,
        [onAck = std::move(onAck)](int juceResult) {
            if (onAck) onAck(NativeCameraPrivacyDialog::isAckResult(juceResult));
        });
}

} // namespace jamwide
