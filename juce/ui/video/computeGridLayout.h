#pragma once
// Plan 22-01 Task 1 — pure-C++ grid layout solver.
//
// Pure deterministic function: given peer count N and available band geometry
// (W, H), compute the column count and per-tile size that maximises tileW
// while satisfying:
//   (a) cols * tileW + (cols+1) * spacing <= W    (width fit)
//   (b) tileH = tileW * 3 / 4                      (4:3 aspect)
//   (c) rows * tileH + (rows+1) * spacing <= H     (height fit)
//   (d) cols <= maxCols                            (very wide bands don't collapse rows)
//
// If no candidate satisfies (c) at minTileW or above, the function picks the
// best-effort largest-cols layout and sets needsScroll=true; the caller wraps
// the resulting band in a juce::Viewport.
//
// Edge cases:
//   - N == 0 returns all-zero (zero-tile guard); caller skips paint.
//   - W <= 0 || H <= 0 returns all-zero (degenerate geometry guard).
//
// Single-header pure-C++. Includes only juce_core for juce::jlimit per
// PATTERNS.md "no analog needed — greenfield pure helper". No JUCE Component
// or graphics headers; the function is called from VideoGridBand::resized()
// (Plan 22-02), exercised by a pure-C++ test (Plan 22-01 Task 1).

#include <juce_core/juce_core.h>

namespace jamwide {

struct GridLayoutResult {
    int  cols      = 0;
    int  rows      = 0;
    int  tileW     = 0;
    int  tileH     = 0;
    int  marginX   = 0;
    int  marginY   = 0;
    int  spacing   = 0;
    bool needsScroll = false;
};

// Pure deterministic function. Threadsafe (no globals, no clock, no random).
//
// Parameters:
//   N        — peer count to lay out (>= 0)
//   bandW    — available band width in px (> 0 produces non-zero output)
//   bandH    — available band height in px
//   spacing  — gap between tiles AND outer margin between tiles and band edges
//              (default 6px per VB-Banana chrome feel; see CONTEXT.md specifics)
//   minTileW — floor before falling back to needsScroll (default 120 per D-06)
//   maxCols  — column-count cap (default 4 per D-06 "5-9 peers = 3 cols; 10+ = scroll")
inline GridLayoutResult computeGridLayout(int N, int bandW, int bandH,
                                          int spacing = 6,
                                          int minTileW = 120,
                                          int maxCols = 4) noexcept
{
    GridLayoutResult result{};
    result.spacing = spacing;

    // Zero-tile and degenerate-geometry guards return all-zero.
    if (N <= 0 || bandW <= 0 || bandH <= 0) {
        return result;
    }

    // Search [1, min(N, maxCols)] for the column count that maximises tileW
    // while satisfying the width AND height fit constraints. Prefer the
    // candidate with largest tileW; ties broken by lower cols (smaller layout
    // is less visually busy at the same per-tile size).
    const int colsMax = juce::jlimit(1, maxCols, N);

    int  bestCols     = 0;
    int  bestRows     = 0;
    int  bestTileW    = 0;
    int  bestTileH    = 0;
    bool bestFitsHeight = false;

    for (int cols = 1; cols <= colsMax; ++cols) {
        const int rows = (N + cols - 1) / cols; // ceil(N / cols)
        // tileW from width-fit: cols * tileW + (cols+1) * spacing == bandW
        const int tileW = (bandW - spacing * (cols + 1)) / cols;
        if (tileW <= 0) continue;
        const int tileH = tileW * 3 / 4; // 4:3 aspect
        const int totalH = rows * tileH + (rows + 1) * spacing;

        const bool fitsHeight = (totalH <= bandH);

        // Update best with two-tier selection:
        //   (a) prefer any layout that satisfies height-fit over one that
        //       doesn't (a "fitting" layout is always preferred);
        //   (b) within the same bucket:
        //       - fits-bucket: prefer LARGER tileW (more visible per-peer);
        //       - doesn't-fit bucket: prefer LARGER cols (more peers visible
        //         horizontally, less vertical overflow, more natural scroll).
        bool upgrade = (bestCols == 0);
        if (!upgrade) {
            const bool upgradeBucket = fitsHeight && !bestFitsHeight;
            if (upgradeBucket) {
                upgrade = true;
            } else if (fitsHeight == bestFitsHeight) {
                if (fitsHeight) {
                    // Both fit — prefer larger tileW.
                    upgrade = (tileW > bestTileW);
                } else {
                    // Neither fits — prefer larger cols (more horizontal spread).
                    upgrade = (cols > bestCols);
                }
            }
        }
        if (upgrade) {
            bestCols       = cols;
            bestRows       = rows;
            bestTileW      = tileW;
            bestTileH      = tileH;
            bestFitsHeight = fitsHeight;
        }
    }

    // If no candidate yielded a positive tileW (e.g. bandW too small even at cols=1),
    // return all-zero (caller skips paint or wraps in scroll viewport).
    if (bestCols == 0 || bestTileW <= 0) {
        return result;
    }

    // Decide needsScroll: the algorithm's height-fit failed OR the chosen tileW
    // is below the minTileW floor. The caller wraps in a Viewport when set.
    const bool floorViolated = (bestTileW < minTileW);
    const bool needsScroll   = (!bestFitsHeight) || floorViolated;

    // Compute centring margins. Width margin is the unused band space
    // distributed evenly. Height margin only meaningful when the layout fits.
    const int usedW = bestCols * bestTileW + (bestCols + 1) * spacing;
    const int usedH = bestRows * bestTileH + (bestRows + 1) * spacing;
    const int marginX = (bandW > usedW) ? (bandW - usedW) / 2 : 0;
    const int marginY = (bandH > usedH && !needsScroll) ? (bandH - usedH) / 2 : 0;

    result.cols        = bestCols;
    result.rows        = bestRows;
    result.tileW       = bestTileW;
    result.tileH       = bestTileH;
    result.marginX     = marginX;
    result.marginY     = marginY;
    result.spacing     = spacing;
    result.needsScroll = needsScroll;
    return result;
}

} // namespace jamwide
