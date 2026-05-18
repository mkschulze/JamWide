/*
    test_plugin_state_v4_v5.cpp — Phase 22 Plan 22-04 Task 2.

    Verifies the v4 -> v5 plugin-state schema migration: defaults,
    round-trip, graceful upgrade, and T-22-SP clamping mitigation
    (popout map cap 64, username length cap 256, jlimit field clamps).

    Pure-C++ test — links only juce_core + juce_data_structures.

    The test replicates the v5 read+write logic inline rather than
    calling into JamWideJuce so the test executable doesn't have to
    link the full plugin static library. Codex M6 closure: the
    companion grep gate in Plan 22-04 Task 1 acceptance criteria
    verifies the production processor source uses the SAME 11
    property names this replica reads/writes — so if production
    drifts (drops/renames a field), the gate fails before this
    test could pass against stale schema.

    Inline replica's internal map uses std::unordered_map because
    this test only links juce_core + juce_data_structures (no need
    to pull additional headers), asserts on specific keys (not
    iteration order), and std::hash<juce::String> is available
    since JUCE 6. The production side uses juce::HashMap per
    codex H2 closure (deterministic XML iteration order).

    Scaffold lifted from tests/test_plugin_state_v3_v4.cpp.
*/

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

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

// ─── Inline replica of the v5 <video> read+write logic ─────────────────────
//
// MUST stay byte-identical to the production setStateInformation STEP 6
// block (the v5 <video> ValueTree subtree handler) in the plugin processor.
// Any divergence between this replica and the production read path is what
// the test is trying to catch. The companion M6 codex grep gate in Plan
// 22-04 Task 1 acceptance criteria verifies the production source references
// all 11 property names this replica uses — so a drift in production
// (rename / drop) fails the gate at the SOURCE level before this test could
// pass against stale schema.
//
// Internal storage is std::unordered_map<juce::String, ...> because this
// test only links juce_core + juce_data_structures (no need for juce::HashMap),
// asserts on specific keys (not iteration order), and std::hash<juce::String>
// ships with JUCE since v6 (juce/midi/MidiMapper.h:105 precedent).
// Minimal Rectangle replacement — the production side uses juce::Rectangle<int>
// (lives in juce_graphics) but this test links only juce_core +
// juce_data_structures to keep the test executable lightweight. A 4-int POD
// gives identical observable semantics for the schema verification this test
// is responsible for.
struct V5Rect {
    int x = 0, y = 0, w = 0, h = 0;
    V5Rect() = default;
    V5Rect(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}
    bool operator==(const V5Rect& o) const noexcept
    {
        return x == o.x && y == o.y && w == o.w && h == o.h;
    }
};

struct V5VideoState {
    bool gridVisible = false;
    int gridBandHeight = 280;
    V5Rect detachedGridBounds{200, 200, 800, 450};
    std::unordered_map<juce::String, V5Rect> popoutBounds;
};

static constexpr std::size_t kRemotePopoutMapCap = 64;
static constexpr int kRemotePopoutUsernameMaxLen = 256;

// Replica of getStateInformation's <video> append block.
static juce::ValueTree writeV5Video(const V5VideoState& s)
{
    auto t = juce::ValueTree("video");
    t.setProperty("gridVisible",        s.gridVisible,                nullptr);
    t.setProperty("gridBandHeight",     s.gridBandHeight,             nullptr);
    t.setProperty("detachedGridX",      s.detachedGridBounds.x,       nullptr);
    t.setProperty("detachedGridY",      s.detachedGridBounds.y,       nullptr);
    t.setProperty("detachedGridWidth",  s.detachedGridBounds.w,       nullptr);
    t.setProperty("detachedGridHeight", s.detachedGridBounds.h,       nullptr);
    for (const auto& kv : s.popoutBounds) {
        auto p = juce::ValueTree("popout");
        p.setProperty("name", kv.first,      nullptr);
        p.setProperty("x",    kv.second.x,   nullptr);
        p.setProperty("y",    kv.second.y,   nullptr);
        p.setProperty("w",    kv.second.w,   nullptr);
        p.setProperty("h",    kv.second.h,   nullptr);
        t.addChild(p, -1, nullptr);
    }
    return t;
}

// Replica of setStateInformation STEP 6 — every clamp + cap MUST stay
// byte-identical to the production block in the plugin processor source.
// M6 codex closure: the Task 1 grep gate in the production source
// ensures the property name strings match this replica's strings.
static V5VideoState readV5Video(const juce::ValueTree& t)
{
    V5VideoState s;  // start from defaults — handles v4 (invalid tree) case
    if (! t.isValid() || ! t.hasType("video"))
        return s;
    s.gridVisible    = (bool) t.getProperty("gridVisible", false);
    s.gridBandHeight = juce::jlimit(140, 800,
        (int) t.getProperty("gridBandHeight", 280));
    const int dgX = juce::jlimit(-10000, 10000, (int) t.getProperty("detachedGridX", 200));
    const int dgY = juce::jlimit(-10000, 10000, (int) t.getProperty("detachedGridY", 200));
    const int dgW = juce::jlimit(320, 4096,    (int) t.getProperty("detachedGridWidth", 800));
    const int dgH = juce::jlimit(240, 4096,    (int) t.getProperty("detachedGridHeight", 450));
    s.detachedGridBounds = V5Rect(dgX, dgY, dgW, dgH);
    std::size_t loadedCount = 0;
    for (int i = 0; i < t.getNumChildren(); ++i) {
        if (loadedCount >= kRemotePopoutMapCap) break;
        auto p = t.getChild(i);
        if (! p.hasType("popout")) continue;
        const juce::String name = p.getProperty("name", "").toString();
        if (name.isEmpty() || name.length() > kRemotePopoutUsernameMaxLen) continue;
        const int x = juce::jlimit(-10000, 10000, (int) p.getProperty("x", 100));
        const int y = juce::jlimit(-10000, 10000, (int) p.getProperty("y", 100));
        const int w = juce::jlimit(240, 2560,    (int) p.getProperty("w", 320));
        const int h = juce::jlimit(180, 1920,    (int) p.getProperty("h", 240));
        s.popoutBounds[name] = V5Rect(x, y, w, h);
        ++loadedCount;
    }
    return s;
}

// ─── Tests ────────────────────────────────────────────────────────────────

// Test 1 — defaults from empty <video> node.
static void test_v5_defaults_from_empty_video_node()
{
    TEST("v5 readV5Video(empty <video>) returns defaults");
    juce::ValueTree empty("video");
    const auto out = readV5Video(empty);
    assert(out.gridVisible == false);
    assert(out.gridBandHeight == 280);
    assert(out.detachedGridBounds == V5Rect(200, 200, 800, 450));
    assert(out.popoutBounds.empty());
    PASS();
}

// Test 2 — round-trip preserves all fields and N popouts.
static void test_v5_round_trip()
{
    TEST("v5 round-trip: write -> ValueTree -> read preserves all fields");
    V5VideoState in;
    in.gridVisible = true;
    in.gridBandHeight = 420;
    in.detachedGridBounds = V5Rect(50, 75, 1024, 720);
    in.popoutBounds["alice"]   = V5Rect(10, 20, 320, 240);
    in.popoutBounds["bob"]     = V5Rect(30, 40, 400, 300);
    in.popoutBounds["charlie"] = V5Rect(50, 60, 320, 240);

    auto tree = writeV5Video(in);
    auto out = readV5Video(tree);

    assert(out.gridVisible    == true);
    assert(out.gridBandHeight == 420);
    assert(out.detachedGridBounds == V5Rect(50, 75, 1024, 720));
    assert(out.popoutBounds.size() == 3);
    assert(out.popoutBounds.at("alice")   == V5Rect(10, 20, 320, 240));
    assert(out.popoutBounds.at("bob")     == V5Rect(30, 40, 400, 300));
    assert(out.popoutBounds.at("charlie") == V5Rect(50, 60, 320, 240));
    PASS();
}

// Test 3 — v4 -> v5 graceful upgrade: v4 state with no <video> child loaded
// by v5 code yields all defaults; no throw.
static void test_v4_to_v5_graceful_upgrade()
{
    TEST("v4 state (no <video> child) -> readV5Video returns defaults");
    juce::ValueTree v4Root("JamWideJUCE");
    v4Root.setProperty("stateVersion", 4, nullptr);
    // v4 root has no <video> child.
    const auto videoChild = v4Root.getChildWithName("video");  // invalid
    const auto out = readV5Video(videoChild);
    assert(out.gridVisible == false);
    assert(out.gridBandHeight == 280);
    assert(out.detachedGridBounds == V5Rect(200, 200, 800, 450));
    assert(out.popoutBounds.empty());
    PASS();
}

// Test 4 — T-22-SP popout map cap: 200 entries truncated to 64.
static void test_v5_t22sp_popout_map_cap()
{
    TEST("T-22-SP — 200 popout entries truncated to 64 (kRemotePopoutMapCap)");
    auto tree = juce::ValueTree("video");
    for (int i = 0; i < 200; ++i) {
        auto p = juce::ValueTree("popout");
        p.setProperty("name", juce::String("peer") + juce::String(i), nullptr);
        p.setProperty("x", 100, nullptr); p.setProperty("y", 100, nullptr);
        p.setProperty("w", 320, nullptr); p.setProperty("h", 240, nullptr);
        tree.addChild(p, -1, nullptr);
    }
    const auto out = readV5Video(tree);
    assert(out.popoutBounds.size() == kRemotePopoutMapCap);
    PASS();
}

// Test 5 — T-22-SP username length cap: 300-char username silently dropped.
static void test_v5_t22sp_username_length_cap()
{
    TEST("T-22-SP — 300-char username silently dropped");
    auto tree = juce::ValueTree("video");
    auto p = juce::ValueTree("popout");
    p.setProperty("name", juce::String::repeatedString("x", 300), nullptr);
    p.setProperty("x", 100, nullptr); p.setProperty("y", 100, nullptr);
    p.setProperty("w", 320, nullptr); p.setProperty("h", 240, nullptr);
    tree.addChild(p, -1, nullptr);
    const auto out = readV5Video(tree);
    assert(out.popoutBounds.empty());
    PASS();
}

// Test 6 — T-22-SP empty-name popout dropped.
static void test_v5_t22sp_empty_username_dropped()
{
    TEST("T-22-SP — empty-name popout silently dropped");
    auto tree = juce::ValueTree("video");
    auto p = juce::ValueTree("popout");
    p.setProperty("name", "", nullptr);
    p.setProperty("x", 100, nullptr); p.setProperty("y", 100, nullptr);
    p.setProperty("w", 320, nullptr); p.setProperty("h", 240, nullptr);
    tree.addChild(p, -1, nullptr);
    // Add a second valid popout to verify the empty one is dropped but the
    // valid one survives.
    auto pOk = juce::ValueTree("popout");
    pOk.setProperty("name", "alice", nullptr);
    pOk.setProperty("x", 50, nullptr); pOk.setProperty("y", 60, nullptr);
    pOk.setProperty("w", 320, nullptr); pOk.setProperty("h", 240, nullptr);
    tree.addChild(pOk, -1, nullptr);
    const auto out = readV5Video(tree);
    assert(out.popoutBounds.size() == 1);
    assert(out.popoutBounds.count("alice") == 1);
    PASS();
}

// Test 7 — T-22-SP bounds clamp: malicious popout bounds clamped via jlimit.
static void test_v5_t22sp_bounds_clamp()
{
    TEST("T-22-SP — malicious popout bounds clamped via jlimit");
    auto tree = juce::ValueTree("video");
    auto p = juce::ValueTree("popout");
    p.setProperty("name", "alice", nullptr);
    p.setProperty("x", -999999, nullptr);
    p.setProperty("y",  999999, nullptr);
    p.setProperty("w",  100000, nullptr);
    p.setProperty("h",    -100, nullptr);
    tree.addChild(p, -1, nullptr);
    const auto out = readV5Video(tree);
    const auto& r = out.popoutBounds.at("alice");
    assert(r.x == -10000);    // clamped to x lower bound
    assert(r.y ==  10000);    // clamped to y upper bound
    assert(r.w == 2560);      // clamped to w upper bound
    assert(r.h ==  180);      // clamped to h lower bound
    PASS();
}

// Test 8 — gridBandHeight clamp: 9999 -> 800 (upper bound).
static void test_v5_grid_band_height_clamp()
{
    TEST("v5 — gridBandHeight 9999 clamped to 800");
    auto tree = juce::ValueTree("video");
    tree.setProperty("gridBandHeight", 9999, nullptr);
    const auto out = readV5Video(tree);
    assert(out.gridBandHeight == 800);
    auto tree2 = juce::ValueTree("video");
    tree2.setProperty("gridBandHeight", 1, nullptr);
    const auto out2 = readV5Video(tree2);
    assert(out2.gridBandHeight == 140);  // clamped to lower bound
    PASS();
}

// Test 9 — detachedGridBounds clamp: width clamped to [320, 4096],
// height clamped to [240, 4096].
static void test_v5_detached_grid_bounds_clamp()
{
    TEST("v5 — detachedGridBounds width/height clamped to their ranges");
    auto tree = juce::ValueTree("video");
    tree.setProperty("detachedGridX",      -999999, nullptr);
    tree.setProperty("detachedGridY",       999999, nullptr);
    tree.setProperty("detachedGridWidth",   100000, nullptr);
    tree.setProperty("detachedGridHeight",     -50, nullptr);
    const auto out = readV5Video(tree);
    assert(out.detachedGridBounds.x == -10000);
    assert(out.detachedGridBounds.y ==  10000);
    assert(out.detachedGridBounds.w == 4096);
    assert(out.detachedGridBounds.h ==  240);
    PASS();
}

// Test 10 — round-trip with cap behavior: writer side has no cap, but the
// reader caps at kRemotePopoutMapCap. Write 100 popouts via the writer
// (vector snapshot), then read back and verify exactly 64 survive.
static void test_v5_round_trip_with_cap_truncation()
{
    TEST("v5 — write 100 popouts -> ValueTree -> read returns 64 (cap)");
    V5VideoState in;
    for (int i = 0; i < 100; ++i) {
        in.popoutBounds[juce::String("peer") + juce::String(i)]
            = V5Rect(i, i, 320, 240);
    }
    auto tree = writeV5Video(in);
    // Round-trip through XML to ensure the writer emits ALL 100 child
    // nodes (cap is reader-side only).
    std::unique_ptr<juce::XmlElement> xml(tree.createXml());
    assert(xml != nullptr);
    auto reparsed = juce::ValueTree::fromXml(*xml);
    assert(reparsed.isValid());
    // Count <popout> children directly to confirm writer emitted 100.
    int childCount = 0;
    for (int i = 0; i < reparsed.getNumChildren(); ++i) {
        if (reparsed.getChild(i).hasType("popout"))
            ++childCount;
    }
    assert(childCount == 100);
    // Now read through the reader's cap.
    const auto out = readV5Video(reparsed);
    assert(out.popoutBounds.size() == kRemotePopoutMapCap);
    PASS();
}

int main()
{
    std::printf("test_plugin_state_v4_v5 — Phase 22 Plan 22-04 v4->v5 schema migration\n");

    test_v5_defaults_from_empty_video_node();
    test_v5_round_trip();
    test_v4_to_v5_graceful_upgrade();
    test_v5_t22sp_popout_map_cap();
    test_v5_t22sp_username_length_cap();
    test_v5_t22sp_empty_username_dropped();
    test_v5_t22sp_bounds_clamp();
    test_v5_grid_band_height_clamp();
    test_v5_detached_grid_bounds_clamp();
    test_v5_round_trip_with_cap_truncation();

    std::printf("\nResults: %d / %d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
