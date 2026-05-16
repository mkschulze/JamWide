// Phase 19-03 Task 1 — CameraStatusDialog implementation.
//
// See CameraStatusDialog.h for the Codex HIGH-7 + MEDIUM-6 closure rationale.
//
// Note: this file is COMPILED both into JamWideJuce (production dialog) and
// into the test_camera_cause_mapping executable (pure-C++, static helpers
// only — show() never runs in test). The juce::AlertWindow::showAsync path
// in show() is therefore gated by the message thread; the static helpers
// have no dependency on the message thread and can be exercised freely.
#include "CameraStatusDialog.h"

#include <utility>

namespace jamwide {

// ─── copyFor (MEDIUM-6 softened strings — NO DAW-specific blame) ────────────
juce::String CameraStatusDialog::copyFor(CameraFallbackCause cause,
                                          const juce::String& hostName)
{
    switch (cause) {
        case CameraFallbackCause::TCCDenied:
            return "macOS has denied camera access to JamWide. "
                   "Grant permission in System Settings -> Privacy & Security "
                   "-> Camera, then click Recheck.";

        case CameraFallbackCause::HostLacksEntitlement: {
            // MEDIUM-6: softened copy with {HostName} placeholder. The two
            // {HostName} occurrences are substituted via juce::String::replace
            // so a future change to the placeholder format is a single edit.
            const juce::String tmpl =
                "The host application ({HostName}) can't access the camera "
                "right now. This usually means either the host hasn't "
                "requested camera permission for itself, or you've denied "
                "it for the host in System Settings.\n\n"
                "Check System Settings -> Privacy & Security -> Camera and "
                "confirm {HostName} is in the list and enabled.\n\n"
                "Tip: JamWide standalone has direct camera access.";
            return tmpl.replace("{HostName}", hostName);
        }

        case CameraFallbackCause::CameraInUse:
            return "Another app appears to be using the camera, or no frames "
                   "are reaching JamWide. Close other apps that use the "
                   "camera, then click Recheck.";

        case CameraFallbackCause::NoHardware:
            return "No camera detected. Connect a webcam and click Recheck.";

        case CameraFallbackCause::WindowsPrivacyBlock:
            return "Windows has blocked camera access for JamWide or its host "
                   "application. Enable camera access for desktop apps in "
                   "Settings -> Privacy & Security -> Camera, then click "
                   "Recheck.";

        case CameraFallbackCause::None:
        default:
            return "Camera unavailable.";
    }
}

// ─── buttonsFor (HostLacksEntitlement == TCCDenied per MEDIUM-6) ────────────
juce::StringArray CameraStatusDialog::buttonsFor(CameraFallbackCause cause)
{
    switch (cause) {
        case CameraFallbackCause::TCCDenied:
        case CameraFallbackCause::HostLacksEntitlement:
            return { "Open System Settings", "Recheck permission", "OK" };

        case CameraFallbackCause::CameraInUse:
        case CameraFallbackCause::NoHardware:
            return { "Recheck permission", "OK" };

        case CameraFallbackCause::WindowsPrivacyBlock:
            return { "Open Camera Privacy Settings", "Recheck permission", "OK" };

        case CameraFallbackCause::None:
        default:
            return { "OK" };
    }
}

// ─── iconFor ────────────────────────────────────────────────────────────────
juce::MessageBoxIconType CameraStatusDialog::iconFor(CameraFallbackCause cause) noexcept
{
    switch (cause) {
        case CameraFallbackCause::TCCDenied:
        case CameraFallbackCause::HostLacksEntitlement:
        case CameraFallbackCause::WindowsPrivacyBlock:
            return juce::MessageBoxIconType::WarningIcon;

        case CameraFallbackCause::CameraInUse:
        case CameraFallbackCause::NoHardware:
            return juce::MessageBoxIconType::InfoIcon;

        case CameraFallbackCause::None:
        default:
            return juce::MessageBoxIconType::NoIcon;
    }
}

// ─── actionFor — HIGH-7 closure ─────────────────────────────────────────────
//
// JUCE's MessageBoxOptions::withButton(label) does NOT support a return-code
// argument; juce::AlertWindow::showAsync hands back a fixed int per the
// documented mapping (juce_AlertWindow.h:457-466):
//
//   1-button:   button[0] -> 0
//   2-button:   button[0] -> 1, button[1] -> 0
//   3-button:   button[0] -> 1, button[1] -> 2, button[2] -> 0
//
// This switch is the SINGLE source of truth for the int -> Action mapping.
// Tested in tests/test_camera_cause_mapping.cpp Test 3 (14 cells).
//
// Note on URL launching: the dialog does NOT itself open System Settings.
// It only returns the Action enum; the editor's onCameraFallback lambda
// translates Action::OpenSystemSettings into the platform-specific deep-link
// URL launch. The expected URLs are:
//
//   macOS:   x-apple.systempreferences:com.apple.preference.security?Privacy_Camera
//   Windows: ms-settings:privacy-webcam
//
// Keeping the URL launch out of the dialog itself lets test_camera_cause_mapping
// stay pure-C++ (no platform branches, no juce::URL dependency in test path).
CameraStatusDialog::Action
CameraStatusDialog::actionFor(CameraFallbackCause cause, int juceResult) noexcept
{
    switch (cause) {
        // 3-button: TCCDenied / HostLacksEntitlement / WindowsPrivacyBlock.
        // Order is { Open*, Recheck, OK } so:
        //   juceResult==1 -> OpenSystemSettings
        //   juceResult==2 -> RecheckPermission
        //   anything else -> Dismiss
        case CameraFallbackCause::TCCDenied:
        case CameraFallbackCause::HostLacksEntitlement:
        case CameraFallbackCause::WindowsPrivacyBlock:
            if (juceResult == 1) return Action::OpenSystemSettings;
            if (juceResult == 2) return Action::RecheckPermission;
            return Action::Dismiss;

        // 2-button: CameraInUse / NoHardware. Order { Recheck, OK } so:
        //   juceResult==1 -> RecheckPermission
        //   anything else -> Dismiss
        case CameraFallbackCause::CameraInUse:
        case CameraFallbackCause::NoHardware:
            if (juceResult == 1) return Action::RecheckPermission;
            return Action::Dismiss;

        // 1-button defensive (None): only Dismiss makes sense.
        case CameraFallbackCause::None:
        default:
            return Action::Dismiss;
    }
}

// ─── show — runtime dispatch ────────────────────────────────────────────────
//
// Suppress-after-first-show (D-14): if cause matches lastShownCause_ AND
// lastShownCause_ is not None, return Dismiss immediately. Otherwise we
// stamp lastShownCause_ and fire juce::AlertWindow::showAsync.
//
// hostName is supplied by the caller (typically
// juce::PluginHostType().getHostDescription() from the editor's plugin
// context). Kept as an argument so the dialog does NOT depend on
// juce_audio_processors — that keeps test_camera_cause_mapping pure-C++
// and lets the dialog be used in unit tests without dragging in the plugin
// host detection machinery.
void CameraStatusDialog::show(CameraFallbackCause cause,
                                const juce::String& hostName,
                                std::function<void(Action)> onResult)
{
    // D-14 — suppress duplicate shows. None is the "never shown" sentinel
    // so we must NOT suppress when lastShownCause_ is None (or this would
    // silently swallow the very first show).
    if (cause == lastShownCause_ && lastShownCause_ != CameraFallbackCause::None) {
        if (onResult) onResult(Action::Dismiss);
        return;
    }
    lastShownCause_ = cause;

    const juce::String message  = copyFor(cause, hostName);
    const juce::StringArray buttons = buttonsFor(cause);

    juce::MessageBoxOptions options
        = juce::MessageBoxOptions{}
              .withIconType(iconFor(cause))
              .withTitle("Camera unavailable")
              .withMessage(message);

    for (const auto& label : buttons)
        options = options.withButton(label);

    juce::AlertWindow::showAsync(options,
        [cause, onResult = std::move(onResult)](int juceResult) {
            if (onResult) onResult(actionFor(cause, juceResult));
        });
}

void CameraStatusDialog::reset() noexcept
{
    lastShownCause_ = CameraFallbackCause::None;
}

} // namespace jamwide
