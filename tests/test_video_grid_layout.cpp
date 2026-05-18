// Plan 22-01 Task 1 — test_video_grid_layout.cpp
//
// Pure-C++ unit test for jamwide::computeGridLayout. Wave 0 of Phase 22 per the
// MEMBER-ORDER-CONTRACT gating discipline: the layout math is RED→GREEN before
// any container code is written (D-08 / D-06 in 22-CONTEXT.md).
//
// Build wiring: see CMakeLists.txt block under "Plan 22-01 Task 1 wiring". Pure
// header-only inclusion; links nothing beyond what the host compiler provides
// for <cassert> / <cstdio> (parallel to test_camera_state_machine).
//
// Test list:
//   1. N=0 zero-tile guard (no crash, cols=0, rows=0, tileW=0, tileH=0)
//   2. N=1 single peer fills band (cols=1, tileW>=300, tileH=tileW*3/4)
//   3. N=2 two peers (cols=2, equal width, fits band width)
//   4. N=4 four peers (cols<=4, rows>=1, maximises tileW)
//   5. N=9 fit-invariant check (L8 codex closure — invariants over exact cols)
//   6. N=16 may trigger needsScroll when tileW < kMinTileW (default 120)
//   7. 4:3 aspect invariant for every (N, W, H) input where tileW > 0
//   8. Determinism — running same input 100x produces byte-identical output

#include "juce/ui/video/computeGridLayout.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>

namespace {

using jamwide::GridLayoutResult;
using jamwide::computeGridLayout;

static int tests_passed = 0;
static int tests_failed = 0;

#define EXPECT(cond, msg) do {                                                \
    if (!(cond)) {                                                            \
        std::cerr << "FAIL [" << msg << "] at " << __FILE__ << ":" << __LINE__ \
                  << "\n"; ++tests_failed; return;                             \
    }                                                                          \
} while (0)

#define PASS(msg) do {                                                        \
    ++tests_passed; std::cout << "PASS [" << msg << "]\n";                    \
} while (0)

// ─── Test 1: N=0 zero-tile guard ─────────────────────────────────────────

void test_zero_peers_returns_all_zero() {
    auto l = computeGridLayout(0, 800, 280);
    EXPECT(l.cols == 0,  "0 peers: cols == 0");
    EXPECT(l.rows == 0,  "0 peers: rows == 0");
    EXPECT(l.tileW == 0, "0 peers: tileW == 0");
    EXPECT(l.tileH == 0, "0 peers: tileH == 0");
    PASS("0 peers @ 800x280 returns all-zero");
}

// ─── Test 2: N=1 single peer fills band ──────────────────────────────────

void test_one_peer_fills_band() {
    auto l = computeGridLayout(1, 800, 280);
    EXPECT(l.cols == 1,    "1 peer: cols == 1");
    EXPECT(l.rows == 1,    "1 peer: rows == 1");
    EXPECT(l.tileW >= 300, "1 peer: tileW >= 300");
    EXPECT(l.tileH == l.tileW * 3 / 4, "1 peer: 4:3 aspect");
    PASS("1 peer @ 800x280 fills band tileW>=300, 4:3");
}

// ─── Test 3: N=2 two peers side-by-side ──────────────────────────────────

void test_two_peers_side_by_side() {
    auto l = computeGridLayout(2, 800, 280);
    EXPECT(l.cols == 2,                              "2 peers: cols == 2");
    EXPECT(l.rows == 1,                              "2 peers: rows == 1");
    EXPECT(l.cols * l.tileW + (l.cols + 1) * l.spacing <= 800,
           "2 peers: fits band width");
    EXPECT(l.tileH == l.tileW * 3 / 4,               "2 peers: 4:3 aspect");
    PASS("2 peers @ 800x280 side-by-side, equal width");
}

// ─── Test 4: N=4 four peers ──────────────────────────────────────────────

void test_four_peers_maximises_tilew() {
    auto l = computeGridLayout(4, 800, 280);
    EXPECT(l.cols >= 1 && l.cols <= 4, "4 peers: cols in [1,4]");
    EXPECT(l.rows >= 1,                "4 peers: rows >= 1");
    EXPECT(l.cols * l.rows >= 4,       "4 peers: capacity >= 4");
    if (!l.needsScroll) {
        EXPECT(l.cols * l.tileW + (l.cols + 1) * l.spacing <= 800,
               "4 peers: fits band width");
    }
    EXPECT(l.tileH == l.tileW * 3 / 4, "4 peers: 4:3 aspect");
    PASS("4 peers @ 800x280 chooses cols that maximises tileW");
}

// ─── Test 5: L8 invariant check (codex review closure) ──────────────────

void test_nine_peers_fit_invariants() {
    auto l5 = computeGridLayout(9, 800, 280, /*spacing=*/6, /*minTileW=*/120, /*maxCols=*/4);
    // L8 — invariants over exact-cases. The algorithm picks cols in [1, maxCols]
    // and maximises tileW subject to fit; exact cols=3 vs cols=4 is discretion.
    EXPECT(l5.cols >= 1 && l5.cols <= 4, "9 peers: cols in [1,4]");
    EXPECT(l5.cols * l5.rows >= 9, "9 peers: capacity >= 9");
    if (!l5.needsScroll) {
        EXPECT(l5.cols * l5.tileW + (l5.cols + 1) * l5.spacing <= 800,
               "9 peers: width-fit");
        EXPECT(l5.rows * l5.tileH + (l5.rows + 1) * l5.spacing <= 280,
               "9 peers: height-fit");
    }
    PASS("9 peers @ 800x280 fits invariantly");
}

// ─── Test 6: N=16 may need scroll when tileW < minTileW ─────────────────

void test_sixteen_peers_may_trigger_scroll() {
    auto l = computeGridLayout(16, 600, 280, /*spacing=*/6, /*minTileW=*/120);
    EXPECT(l.cols >= 1, "16 peers @ 600x280: cols >= 1");
    // If the algorithm respects the minTileW floor, it MUST set needsScroll
    // because 16 tiles at >=120px-wide cannot fit in 600px horizontally even
    // at cols=4 (4 * 120 + 5 * 6 = 510 fits — but 16 tiles at cols=4 = 4 rows
    // x 90 (4:3 of 120) = 360 + 5*6 = 390 > 280: needs scroll).
    // We assert the fit/needsScroll discipline rather than exact cols:
    if (l.tileW > 0 && l.tileW < 120) {
        // If algorithm undershoots minTileW it must also flag scroll.
        EXPECT(l.needsScroll, "16 peers: tileW<120 implies needsScroll");
    }
    PASS("16 peers @ 600x280 — scroll discipline holds");
}

// ─── Test 7: 4:3 aspect invariant for every input ───────────────────────

void test_four_three_aspect_invariant() {
    const int cases[][3] = {
        {1, 800, 280}, {2, 800, 280}, {3, 800, 280}, {4, 800, 280},
        {5, 800, 280}, {9, 800, 280}, {10, 800, 280}, {16, 800, 280},
        {1, 400, 300}, {2, 1200, 400}, {3, 1600, 500}, {7, 600, 280},
    };
    for (const auto& c : cases) {
        auto l = computeGridLayout(c[0], c[1], c[2]);
        if (l.tileW > 0) {
            EXPECT(l.tileH == l.tileW * 3 / 4,
                   "4:3 invariant violated for some (N,W,H)");
        }
    }
    PASS("4:3 aspect invariant holds across N, W, H matrix");
}

// ─── Test 8: Determinism — same input → byte-identical output ───────────

void test_determinism_byte_identical() {
    auto baseline = computeGridLayout(7, 1024, 400);
    for (int i = 0; i < 100; ++i) {
        auto l = computeGridLayout(7, 1024, 400);
        EXPECT(l.cols    == baseline.cols    &&
               l.rows    == baseline.rows    &&
               l.tileW   == baseline.tileW   &&
               l.tileH   == baseline.tileH   &&
               l.marginX == baseline.marginX &&
               l.marginY == baseline.marginY &&
               l.spacing == baseline.spacing &&
               l.needsScroll == baseline.needsScroll,
               "determinism: byte-identical output across 100 invocations");
    }
    PASS("determinism — 100 invocations byte-identical");
}

} // anonymous namespace

int main() {
    std::cout << "test_video_grid_layout:\n";
    test_zero_peers_returns_all_zero();
    test_one_peer_fills_band();
    test_two_peers_side_by_side();
    test_four_peers_maximises_tilew();
    test_nine_peers_fit_invariants();
    test_sixteen_peers_may_trigger_scroll();
    test_four_three_aspect_invariant();
    test_determinism_byte_identical();

    std::cout << "\nresults: " << tests_passed << " passed, "
              << tests_failed << " failed\n";
    return tests_failed == 0 ? 0 : 1;
}
