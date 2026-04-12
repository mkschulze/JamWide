/*
    test_video_sync.cpp - Buffer delay formula unit test

    Duplicates the formula from VideoCompanion.cpp:470 intentionally.
    Pure arithmetic — no JUCE or external dependencies.

    Formula: delay_ms = (60.0 / bpm) * bpi * 1000, truncated to int
*/

#include <cassert>
#include <cmath>
#include <cstdio>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  TEST: %s ... ", name); \
        fflush(stdout); \
    } while(0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("PASSED\n"); \
    } while(0)

#define FAIL(msg) \
    do { \
        printf("FAILED: %s\n", msg); \
    } while(0)

// Duplicated from VideoCompanion.cpp:470 — header is frozen, so we test
// the formula directly rather than importing it.
static int computeBufferDelayMs(double bpm, int bpi)
{
    if (bpm <= 0.0 || bpi <= 0 || std::isnan(bpm)) return 0;
    int result = static_cast<int>((60.0 / bpm) * bpi * 1000.0);
    return result > 0 ? result : 0;
}

// ── Standard Cases ──

static void test_120bpm_16bpi()
{
    TEST("120 BPM / 16 BPI = 8000ms");
    int delay = computeBufferDelayMs(120.0, 16);
    if (delay == 8000) { PASS(); }
    else { FAIL("expected 8000"); printf("    got: %d\n", delay); }
}

static void test_120bpm_8bpi()
{
    TEST("120 BPM / 8 BPI = 4000ms");
    int delay = computeBufferDelayMs(120.0, 8);
    if (delay == 4000) { PASS(); }
    else { FAIL("expected 4000"); printf("    got: %d\n", delay); }
}

static void test_60bpm_16bpi()
{
    TEST("60 BPM / 16 BPI = 16000ms");
    int delay = computeBufferDelayMs(60.0, 16);
    if (delay == 16000) { PASS(); }
    else { FAIL("expected 16000"); printf("    got: %d\n", delay); }
}

static void test_240bpm_4bpi()
{
    TEST("240 BPM / 4 BPI = 1000ms");
    int delay = computeBufferDelayMs(240.0, 4);
    if (delay == 1000) { PASS(); }
    else { FAIL("expected 1000"); printf("    got: %d\n", delay); }
}

static void test_300bpm_2bpi()
{
    TEST("300 BPM / 2 BPI = 400ms");
    int delay = computeBufferDelayMs(300.0, 2);
    if (delay == 400) { PASS(); }
    else { FAIL("expected 400"); printf("    got: %d\n", delay); }
}

static void test_40bpm_64bpi()
{
    TEST("40 BPM / 64 BPI = 96000ms");
    int delay = computeBufferDelayMs(40.0, 64);
    if (delay == 96000) { PASS(); }
    else { FAIL("expected 96000"); printf("    got: %d\n", delay); }
}

// ── Fractional BPM (truncation, not rounding) ──

static void test_96_5bpm_12bpi()
{
    TEST("96.5 BPM / 12 BPI = 7461ms (truncation)");
    int delay = computeBufferDelayMs(96.5, 12);
    // (60.0 / 96.5) * 12 * 1000 = 7461.139... → truncated to 7461
    if (delay == 7461) { PASS(); }
    else { FAIL("expected 7461"); printf("    got: %d\n", delay); }
}

// ── Edge Cases ──

static void test_zero_bpm()
{
    TEST("BPM=0 returns 0");
    int delay = computeBufferDelayMs(0.0, 16);
    if (delay == 0) { PASS(); }
    else { FAIL("expected 0"); printf("    got: %d\n", delay); }
}

static void test_zero_bpi()
{
    TEST("BPI=0 returns 0");
    int delay = computeBufferDelayMs(120.0, 0);
    if (delay == 0) { PASS(); }
    else { FAIL("expected 0"); printf("    got: %d\n", delay); }
}

static void test_nan_bpm()
{
    TEST("BPM=NaN returns 0");
    int delay = computeBufferDelayMs(std::nan(""), 16);
    if (delay == 0) { PASS(); }
    else { FAIL("expected 0"); printf("    got: %d\n", delay); }
}

static void test_negative_bpm()
{
    TEST("BPM=-120 returns 0");
    int delay = computeBufferDelayMs(-120.0, 16);
    if (delay == 0) { PASS(); }
    else { FAIL("expected 0"); printf("    got: %d\n", delay); }
}

static void test_negative_bpi()
{
    TEST("BPI=-8 returns 0");
    int delay = computeBufferDelayMs(120.0, -8);
    if (delay == 0) { PASS(); }
    else { FAIL("expected 0"); printf("    got: %d\n", delay); }
}

int main()
{
    printf("=== Video Sync Buffer Delay Formula Tests ===\n\n");

    test_120bpm_16bpi();
    test_120bpm_8bpi();
    test_60bpm_16bpi();
    test_240bpm_4bpi();
    test_300bpm_2bpi();
    test_40bpm_64bpi();
    test_96_5bpm_12bpi();
    test_zero_bpm();
    test_zero_bpi();
    test_nan_bpm();
    test_negative_bpm();
    test_negative_bpi();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
