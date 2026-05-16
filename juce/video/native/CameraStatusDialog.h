#pragma once
// Phase 19-03 Task 1 — Cause-aware fallback dialog for the native camera path.
//
// Closes Codex HIGH-7 + MEDIUM-6 (review revision 2):
//
//   HIGH-7  — JUCE's MessageBoxOptions::withButton accepts only a label, not a
//             return code. juce::AlertWindow::showAsync delivers a fixed int
//             per the documented mapping (juce_AlertWindow.h:457-466):
//                 1-button:   button[0] returns 0
//                 2-button:   button[0] returns 1, button[1] returns 0
//                 3-button:   button[0] returns 1, button[1] returns 2, button[2] returns 0
//             The actionFor() static helper centralises that per-cause mapping
//             into a semantic Action enum so the editor's onCameraFallback
//             dispatch is label-keyed, not raw-int.
//
//   MEDIUM-6 — HostLacksEntitlement copy softened: no DAW-specific blame and
//             the same 3-button set as TCCDenied (Open System Settings /
//             Recheck permission / OK) so the user has actionable next steps.
//
// Suppress-after-first-show (D-14): if show() is invoked again with the same
// cause as the last successful show, the dialog returns Dismiss without
// re-displaying. A different cause re-shows. reset() clears the suppression
// so the next denial fires the dialog again — invoked by the editor when
// state transitions out of Unavailable (Opening / Capturing).
#include "CameraFallbackCause.h"
#include <functional>
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace jamwide {

class CameraStatusDialog {
public:
    // Semantic action returned to the editor — replaces raw juce::AlertWindow
    // return codes per Codex HIGH-7.
    enum class Action {
        OpenSystemSettings,
        RecheckPermission,
        Dismiss,
    };

    // Shows the cause-aware dialog. The callback fires on the message thread
    // after the user dismisses the dialog (or immediately with Dismiss if
    // the dialog is suppressed because the cause matches the prior show).
    //
    // hostName is interpolated into the HostLacksEntitlement {HostName}
    // placeholders — the caller (typically the editor) passes
    // juce::PluginHostType().getHostDescription(). Kept as an argument rather
    // than queried internally so the dialog implementation does NOT depend
    // on juce_audio_processors (keeps test_camera_cause_mapping pure-C++).
    void show(CameraFallbackCause cause,
              const juce::String& hostName,
              std::function<void(Action)> onResult);

    // Clears the suppression state so the next show() will display even if
    // the cause matches the prior show. Called by the editor when state
    // transitions out of Unavailable (Opening / Capturing).
    void reset() noexcept;

    // ─── Static helpers (HIGH-7 + MEDIUM-6 — testable in isolation) ─────────
    //
    // Maps JUCE's int return code to a semantic Action per cause's button
    // count. The mapping is the SINGLE source of truth for the dialog's
    // label-keyed dispatch and is exercised exhaustively by
    // tests/test_camera_cause_mapping.cpp Test 3 (14 cells).
    static Action actionFor(CameraFallbackCause cause, int juceResult) noexcept;

    // Returns the dialog body string for the given cause. {HostName} is
    // substituted with the supplied hostName (typically
    // juce::PluginHostType().getHostDescription() at runtime).
    static juce::String copyFor(CameraFallbackCause cause,
                                const juce::String& hostName);

    // Returns the button labels for the given cause. The order MUST match
    // the actionFor() mapping above. HostLacksEntitlement uses the SAME
    // 3-button set as TCCDenied per MEDIUM-6.
    static juce::StringArray buttonsFor(CameraFallbackCause cause);

    // Returns the icon hint for the given cause. WarningIcon for outright
    // denials (TCC / Host / Windows); InfoIcon for transient/hardware issues
    // (CameraInUse / NoHardware); NoIcon for None.
    static juce::MessageBoxIconType iconFor(CameraFallbackCause cause) noexcept;

private:
    CameraFallbackCause lastShownCause_ = CameraFallbackCause::None;
};

} // namespace jamwide
