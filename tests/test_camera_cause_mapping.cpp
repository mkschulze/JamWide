/*
    test_camera_cause_mapping.cpp — Phase 19-03 Task 1.

    Verifies the cause-aware fallback dialog (CameraStatusDialog) for:

      Test 1 — 5 causes × cause-to-copy mapping (softened HostLacksEntitlement
               copy per MEDIUM-6 — no DAW-specific blame).
      Test 2 — 5 causes × button mapping (HostLacksEntitlement uses the SAME
               3-button set as TCCDenied per MEDIUM-6).
      Test 3 — HIGH-7 button-index to Action mapping for every (cause, juceResult)
               cell. 14 assertions covering JUCE's documented mapping
               (juce_AlertWindow.h:457-466):
                 1-button:   button[0] returns 0
                 2-button:   button[0] returns 1, button[1] returns 0
                 3-button:   button[0] returns 1, button[1] returns 2, button[2] returns 0
      Test 4 — HostLacksEntitlement {HostName} template interpolation.
      Test 5 — None defensive fallback (1-button OK with Dismiss action).

    Pure-C++ test — links only juce_core / juce_gui_basics (the dialog header
    pulls in MessageBoxOptions). MEDIUM-5 closure from 19-01 (no link against
    JamWideJuce).

    Scaffold lifted from tests/test_video_fourcc.cpp + tests/test_plugin_state_v3_v4.cpp
    (TEST/PASS/FAIL macros).
*/

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "video/native/CameraStatusDialog.h"
#include "video/native/CameraFallbackCause.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        std::printf("  TEST: %s ... ", name); \
        std::fflush(stdout); \
    } while (0)

#define PASS() \
    do { \
        tests_passed++; \
        std::printf("PASSED\n"); \
    } while (0)

#define FAIL(msg) \
    do { \
        std::printf("FAILED: %s\n", msg); \
    } while (0)

using jamwide::CameraFallbackCause;
using jamwide::CameraStatusDialog;

// ─── Test 1 — 5 causes × copy mapping (softened MEDIUM-6 strings) ───────────
static void test_cause_to_copy_mapping()
{
    TEST("Test 1 — 5 causes × cause-to-copy mapping (MEDIUM-6 softened)");

    const juce::String tcc = CameraStatusDialog::copyFor(
        CameraFallbackCause::TCCDenied, "TestHost");
    assert(tcc.contains("macOS has denied"));

    const juce::String host = CameraStatusDialog::copyFor(
        CameraFallbackCause::HostLacksEntitlement, "TestHost");
    assert(host.contains("TestHost"));
    assert(host.contains("host application"));
    assert(host.contains("System Settings"));
    // MEDIUM-6 — must NOT use DAW-blame language.
    assert(! host.contains("doesn't request camera access for itself"));

    const juce::String inuse = CameraStatusDialog::copyFor(
        CameraFallbackCause::CameraInUse, "TestHost");
    assert(inuse.contains("Another app"));

    const juce::String noh = CameraStatusDialog::copyFor(
        CameraFallbackCause::NoHardware, "TestHost");
    assert(noh.contains("No camera detected"));

    const juce::String wpb = CameraStatusDialog::copyFor(
        CameraFallbackCause::WindowsPrivacyBlock, "TestHost");
    assert(wpb.contains("Windows has blocked"));

    PASS();
}

// ─── Test 2 — 5 causes × button mapping ─────────────────────────────────────
static void test_cause_to_buttons_mapping()
{
    TEST("Test 2 — 5 causes × button mapping (HostLacksEntitlement == TCCDenied)");

    const auto tcc = CameraStatusDialog::buttonsFor(CameraFallbackCause::TCCDenied);
    assert(tcc.size() == 3);
    assert(tcc[0] == "Open System Settings");
    assert(tcc[1] == "Recheck permission");
    assert(tcc[2] == "OK");

    // MEDIUM-6 — HostLacksEntitlement uses the SAME 3-button set as TCCDenied.
    const auto host = CameraStatusDialog::buttonsFor(
        CameraFallbackCause::HostLacksEntitlement);
    assert(host.size() == 3);
    assert(host[0] == "Open System Settings");
    assert(host[1] == "Recheck permission");
    assert(host[2] == "OK");
    // Stronger structural equality assertion.
    assert(host == tcc);

    const auto inuse = CameraStatusDialog::buttonsFor(CameraFallbackCause::CameraInUse);
    assert(inuse.size() == 2);
    assert(inuse[0] == "Recheck permission");
    assert(inuse[1] == "OK");

    const auto noh = CameraStatusDialog::buttonsFor(CameraFallbackCause::NoHardware);
    assert(noh.size() == 2);
    assert(noh[0] == "Recheck permission");
    assert(noh[1] == "OK");

    const auto wpb = CameraStatusDialog::buttonsFor(
        CameraFallbackCause::WindowsPrivacyBlock);
    assert(wpb.size() == 3);
    assert(wpb[0] == "Open Camera Privacy Settings");
    assert(wpb[1] == "Recheck permission");
    assert(wpb[2] == "OK");

    PASS();
}

// ─── Test 3 — HIGH-7 button-index to Action mapping (14 cells) ──────────────
static void test_high7_action_mapping()
{
    TEST("Test 3 — HIGH-7 actionFor(cause, juceResult) — 14 cells");

    using Action = CameraStatusDialog::Action;

    // 3-button: TCCDenied
    assert(CameraStatusDialog::actionFor(CameraFallbackCause::TCCDenied, 1)
           == Action::OpenSystemSettings);
    assert(CameraStatusDialog::actionFor(CameraFallbackCause::TCCDenied, 2)
           == Action::RecheckPermission);
    assert(CameraStatusDialog::actionFor(CameraFallbackCause::TCCDenied, 0)
           == Action::Dismiss);

    // 3-button: HostLacksEntitlement
    assert(CameraStatusDialog::actionFor(CameraFallbackCause::HostLacksEntitlement, 1)
           == Action::OpenSystemSettings);
    assert(CameraStatusDialog::actionFor(CameraFallbackCause::HostLacksEntitlement, 2)
           == Action::RecheckPermission);
    assert(CameraStatusDialog::actionFor(CameraFallbackCause::HostLacksEntitlement, 0)
           == Action::Dismiss);

    // 3-button: WindowsPrivacyBlock
    assert(CameraStatusDialog::actionFor(CameraFallbackCause::WindowsPrivacyBlock, 1)
           == Action::OpenSystemSettings);
    assert(CameraStatusDialog::actionFor(CameraFallbackCause::WindowsPrivacyBlock, 2)
           == Action::RecheckPermission);
    assert(CameraStatusDialog::actionFor(CameraFallbackCause::WindowsPrivacyBlock, 0)
           == Action::Dismiss);

    // 2-button: CameraInUse
    assert(CameraStatusDialog::actionFor(CameraFallbackCause::CameraInUse, 1)
           == Action::RecheckPermission);
    assert(CameraStatusDialog::actionFor(CameraFallbackCause::CameraInUse, 0)
           == Action::Dismiss);

    // 2-button: NoHardware
    assert(CameraStatusDialog::actionFor(CameraFallbackCause::NoHardware, 1)
           == Action::RecheckPermission);
    assert(CameraStatusDialog::actionFor(CameraFallbackCause::NoHardware, 0)
           == Action::Dismiss);

    // 1-button: None (defensive)
    assert(CameraStatusDialog::actionFor(CameraFallbackCause::None, 0)
           == Action::Dismiss);

    PASS();
}

// ─── Test 4 — HostLacksEntitlement {HostName} template interpolation ────────
static void test_host_name_interpolation()
{
    TEST("Test 4 — HostLacksEntitlement {HostName} substitution");

    const juce::String reaper = CameraStatusDialog::copyFor(
        CameraFallbackCause::HostLacksEntitlement, "REAPER");
    assert(reaper.contains("REAPER"));

    // Token must NOT survive — interpolation actually happens.
    assert(! reaper.contains("{HostName}"));

    PASS();
}

// ─── Test 5 — None defensive fallback ───────────────────────────────────────
static void test_none_defensive_fallback()
{
    TEST("Test 5 — None defensive fallback (1-button + Dismiss action)");

    const auto buttons = CameraStatusDialog::buttonsFor(CameraFallbackCause::None);
    // At minimum an "OK" button so the dialog never has a zero-button row.
    assert(buttons.size() >= 1);
    assert(buttons[0] == "OK");

    assert(CameraStatusDialog::actionFor(CameraFallbackCause::None, 0)
           == CameraStatusDialog::Action::Dismiss);

    PASS();
}

int main()
{
    std::printf("test_camera_cause_mapping — Phase 19-03 cause-aware fallback dialog\n");

    test_cause_to_copy_mapping();
    test_cause_to_buttons_mapping();
    test_high7_action_mapping();
    test_host_name_interpolation();
    test_none_defensive_fallback();

    std::printf("\nResults: %d / %d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
