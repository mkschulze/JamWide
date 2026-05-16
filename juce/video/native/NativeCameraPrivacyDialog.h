#pragma once
// Phase 19-02 Task 3 — D-22 first-launch privacy modal for native camera.
//
// Shown ONCE per install after permission is granted, before the user's first
// frame can be broadcast. Records acknowledgement in cameraPrivacyAck so the
// modal does not fire on subsequent sessions.
//
// HIGH-7 prep — JUCE's MessageBoxOptions::withButton only accepts a label,
// not a return code; juce::AlertWindow::showAsync delivers a fixed int per
// the documented mapping (juce_AlertWindow.h:457-466). For a 2-button dialog:
//
//   button[0]  →  JUCE returns 1
//   button[1]  →  JUCE returns 0
//
// The static isAckResult() helper centralises that mapping so callers (and
// the unit-test) do not duplicate the magic number. The same label-keyed
// dispatch pattern is applied at scale in 19-03's 3-button CameraStatusDialog.
#include <functional>

namespace juce { class String; }

namespace jamwide {

class NativeCameraPrivacyDialog {
public:
    NativeCameraPrivacyDialog() = default;

    // Shows the modal. Invokes onAck(true) if the user clicks "I understand",
    // false if they click "Cancel" or the dialog is dismissed. Safe to call
    // on the message thread only — the underlying juce::AlertWindow::showAsync
    // requires it.
    void show(std::function<void(bool)> onAck);

    // HIGH-7 prep — see header comment. Centralised mapping is verified by
    // tests/test_plugin_state_v3_v4 Test 5.
    static bool isAckResult(int juceResult) noexcept { return juceResult == 1; }
};

} // namespace jamwide
