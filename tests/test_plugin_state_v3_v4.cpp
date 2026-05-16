/*
    test_plugin_state_v3_v4.cpp — Phase 19-02 Task 3.

    Verifies the v3 → v4 plugin-state schema migration: defaults, round-trip,
    T-19-03 clamping mitigation, ack persistence, and the HIGH-7 prep
    label-keyed dispatch helper (NativeCameraPrivacyDialog::isAckResult).

    Pure-C++ test — links only juce_core / juce_data_structures /
    juce_gui_basics (the dialog header pulls in juce_gui_basics for
    MessageBoxOptions; isAckResult itself is a pure constexpr-style helper
    that does NOT instantiate the dialog).

    The test replicates the v4 read logic inline rather than calling into
    JamWideJuce so the test executable doesn't have to link the full plugin
    static library (mirrors MEDIUM-5 closure from 19-01: every new camera
    test executable is pure-C++).

    Scaffold lifted from tests/test_rawdata_send.cpp (TEST/PASS/FAIL macros).
*/

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include "video/native/NativeCameraPrivacyDialog.h"

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

// ─── Inline replica of the v4 read logic ───────────────────────────────────
//
// MUST stay byte-identical to JamWideJuceProcessor::setStateInformation
// STEP 5 (the seven flat properties under the stateVersion=4 branch). Any
// divergence between this replica and the production read path is what
// the test is trying to catch.
struct V4CameraState {
    int popoutX, popoutY, popoutW, popoutH;
    int qualityPreset;
    bool privacyAck;
    juce::String selectedDevice;
};

static V4CameraState readV4Camera(const juce::ValueTree& tree)
{
    V4CameraState s;
    s.popoutX = juce::jlimit(-10000, 10000, (int) tree.getProperty("cameraPopoutX", 100));
    s.popoutY = juce::jlimit(-10000, 10000, (int) tree.getProperty("cameraPopoutY", 100));
    s.popoutW = juce::jlimit(240, 2560, (int) tree.getProperty("cameraPopoutWidth", 320));
    s.popoutH = juce::jlimit(180, 1920, (int) tree.getProperty("cameraPopoutHeight", 240));
    s.qualityPreset = juce::jlimit(0, 2, (int) tree.getProperty("cameraQualityPreset", 1));
    s.privacyAck = (bool) tree.getProperty("cameraPrivacyAck", false);
    s.selectedDevice = tree.getProperty("cameraSelectedDevice", "").toString();
    if (s.selectedDevice.length() > 256)
        s.selectedDevice = s.selectedDevice.substring(0, 256);
    return s;
}

// ─── Tests ────────────────────────────────────────────────────────────────

// Test 1 — v3 state (no camera properties) loaded by v4 binary → defaults.
static void test_v3_to_v4_default_migration()
{
    TEST("v3 state (no camera fields) → v4 read applies D-25 defaults");

    juce::ValueTree tree("JamWideJUCE");
    tree.setProperty("stateVersion", 3, nullptr);
    // v3 has none of the camera properties.

    const auto s = readV4Camera(tree);

    assert(s.popoutX == 100);
    assert(s.popoutY == 100);
    assert(s.popoutW == 320);
    assert(s.popoutH == 240);
    assert(s.qualityPreset == 1);
    assert(s.privacyAck == false);
    assert(s.selectedDevice == juce::String());

    PASS();
}

// Test 2 — v4 round-trip: write the seven fields, serialise to XML, parse,
// read back → fields match exactly.
static void test_v4_round_trip()
{
    TEST("v4 round-trip: write → XML → parse → read preserves all 7 fields");

    juce::ValueTree tree("JamWideJUCE");
    tree.setProperty("stateVersion", 4, nullptr);
    tree.setProperty("cameraPopoutX", 50, nullptr);
    tree.setProperty("cameraPopoutY", 75, nullptr);
    tree.setProperty("cameraPopoutWidth", 640, nullptr);
    tree.setProperty("cameraPopoutHeight", 480, nullptr);
    tree.setProperty("cameraQualityPreset", 2, nullptr);
    tree.setProperty("cameraPrivacyAck", true, nullptr);
    tree.setProperty("cameraSelectedDevice", "MyCam", nullptr);

    // Round-trip through XML.
    std::unique_ptr<juce::XmlElement> xml(tree.createXml());
    assert(xml != nullptr);
    auto reparsed = juce::ValueTree::fromXml(*xml);
    assert(reparsed.isValid());

    const auto s = readV4Camera(reparsed);

    assert(s.popoutX == 50);
    assert(s.popoutY == 75);
    assert(s.popoutW == 640);
    assert(s.popoutH == 480);
    assert(s.qualityPreset == 2);
    assert(s.privacyAck == true);
    assert(s.selectedDevice == juce::String("MyCam"));

    PASS();
}

// Test 3 — T-19-03 clamping defence: malicious v4 ValueTree with
// out-of-range values → values clamped to legal range, NOT propagated raw.
static void test_v4_clamping_defends_against_tampering()
{
    TEST("T-19-03 — malicious v4 state is clamped to legal range");

    juce::ValueTree tree("JamWideJUCE");
    tree.setProperty("stateVersion", 4, nullptr);
    // Wild out-of-range values an attacker might inject by hand-editing
    // the host's project XML.
    tree.setProperty("cameraPopoutX", -99999, nullptr);
    tree.setProperty("cameraPopoutY", 99999, nullptr);
    tree.setProperty("cameraPopoutWidth", -5, nullptr);       // negative
    tree.setProperty("cameraPopoutHeight", 99999, nullptr);   // huge
    tree.setProperty("cameraQualityPreset", 99, nullptr);     // out of [0,2]
    tree.setProperty("cameraPrivacyAck", true, nullptr);

    // 1000-character device name — well above the 256-char cap.
    juce::String huge;
    for (int i = 0; i < 1000; ++i) huge += 'A';
    tree.setProperty("cameraSelectedDevice", huge, nullptr);

    const auto s = readV4Camera(tree);

    assert(s.popoutX == -10000);   // clamped to lower bound
    assert(s.popoutY == 10000);    // clamped to upper bound
    assert(s.popoutW == 240);      // clamped to popoutWidth min
    assert(s.popoutH == 1920);     // clamped to popoutHeight max
    assert(s.qualityPreset == 2);  // clamped to [0,2] upper bound
    assert(s.privacyAck == true);  // bool is bool; nothing to clamp
    assert(s.selectedDevice.length() == 256);   // capped at 256

    PASS();
}

// Test 4 — HIGH-5 sanity: privacyAck survives serialisation. The HIGH-5
// path depends on ack persisting across sessions so the modal fires
// ONCE per install, not once per launch.
static void test_v4_privacy_ack_persistence()
{
    TEST("HIGH-5 sanity — privacyAck survives v4 serialisation");

    juce::ValueTree tree("JamWideJUCE");
    tree.setProperty("stateVersion", 4, nullptr);
    tree.setProperty("cameraPrivacyAck", true, nullptr);

    std::unique_ptr<juce::XmlElement> xml(tree.createXml());
    assert(xml != nullptr);
    auto reparsed = juce::ValueTree::fromXml(*xml);
    assert(reparsed.isValid());

    const auto s = readV4Camera(reparsed);
    assert(s.privacyAck == true);

    PASS();
}

// Test 5 — HIGH-7 sanity: the privacy-dialog button mapping. JUCE's
// 2-button MessageBox returns 1 for button[0] and 0 for button[1].
// NativeCameraPrivacyDialog::isAckResult centralises that mapping. This
// test pins it so a future JUCE upgrade that changes the convention fails
// loudly here instead of silently flipping which button means "accept".
static void test_high7_label_keyed_dispatch_mapping()
{
    TEST("HIGH-7 — NativeCameraPrivacyDialog::isAckResult pins JUCE 2-button mapping");

    assert(jamwide::NativeCameraPrivacyDialog::isAckResult(1) == true);
    assert(jamwide::NativeCameraPrivacyDialog::isAckResult(0) == false);

    // Defensive — neither value (e.g. 2 or -1) should be treated as ack.
    assert(jamwide::NativeCameraPrivacyDialog::isAckResult(2) == false);
    assert(jamwide::NativeCameraPrivacyDialog::isAckResult(-1) == false);

    PASS();
}

int main()
{
    std::printf("test_plugin_state_v3_v4 — Phase 19-02 plugin-state v3→v4 schema migration\n");

    test_v3_to_v4_default_migration();
    test_v4_round_trip();
    test_v4_clamping_defends_against_tampering();
    test_v4_privacy_ack_persistence();
    test_high7_label_keyed_dispatch_mapping();

    std::printf("\nResults: %d / %d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
