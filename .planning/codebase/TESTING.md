# Testing Patterns

**Analysis Date:** 2026-04-30

## Test Framework

JamWide uses a **bespoke header-light test pattern**, not Catch2 / GoogleTest / JUCE UnitTest. Each test executable is a self-contained `int main()` that defines three macros and a pair of counters:

```cpp
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  TEST: %s ... ", name); fflush(stdout); } while(0)
#define PASS()     do { tests_passed++; printf("PASSED\n"); } while(0)
#define FAIL(msg)  do { printf("FAILED: %s\n", msg); } while(0)
```

The pattern lives at `tests/test_encryption.cpp:25-44` and is **copied verbatim** into every other C++ test file (the comment `// Test framework — verbatim from tests/test_encryption.cpp:25-44` appears in `tests/test_spsc_state_updates.cpp:39-40`).

A `main()` simply calls each `static void test_<name>()` function in sequence and exits with `(tests_passed == tests_run) ? 0 : 1` (`tests/test_video_sync.cpp:147-166`).

**Why no framework?**
- TSan-friendly: no header dependencies that could mask data races.
- Fast compile: each test executable links the minimum it needs (often nothing beyond `<atomic>`, `<thread>`, `<cstdio>`).
- Process isolation: each test is a standalone executable; one crashing won't take the others down.

When adding a new test: copy the macro block from an existing test rather than introducing a new framework.

**Companion (TS) tests** use Vitest + Playwright — see [Companion test stack](#companion-test-stack) below.

## Test File Locations

```
tests/                                              # C++ tests (15 files, ~7,260 lines)
├── test_block_queue_spsc.cpp                       # 324 lines
├── test_decode_media_buffer_spsc.cpp               # 468 lines
├── test_decode_state_arming.cpp                    # 546 lines
├── test_decoder_prealloc.cpp                       # 187 lines
├── test_deferred_delete.cpp                        # 247 lines
├── test_encryption.cpp                             # 746 lines
├── test_flac_codec.cpp                             # 438 lines
├── test_local_channel_mirror.cpp                   # 504 lines
├── test_midi_mapping.cpp                           # 1,647 lines (largest — JUCE-linked)
├── test_njclient_atomics.cpp                       # 253 lines
├── test_osc_loopback.cpp                           # 107 lines
├── test_peer_churn_simulation.cpp                  # 541 lines
├── test_remote_user_mirror.cpp                     # 531 lines
├── test_spsc_state_updates.cpp                     # 557 lines
└── test_video_sync.cpp                             # 166 lines

companion/src/__tests__/                            # TypeScript unit tests (Vitest)
├── bandwidth-profile.test.ts
├── beat-heartbeat.test.ts
├── buffer-delay.test.ts
├── instamode-sync.test.ts
├── popout-url.test.ts
├── popout-window.test.ts
├── roster-labels.test.ts
├── url-builder.test.ts
└── video-sync.test.ts

companion/e2e/                                      # E2E (Playwright)
├── actual-vdo-buffer.spec.ts
├── mock-ws-server.ts
└── video-sync.spec.ts
```

**Naming:** `test_<area>.cpp` for C++, `<area>.test.ts` for Vitest, `<area>.spec.ts` for Playwright.

## Run Commands

### C++ tests (default build)

```bash
./scripts/build.sh --tests       # Configures + builds in build-test/ with JAMWIDE_BUILD_TESTS=ON, JAMWIDE_BUILD_JUCE=OFF
cd build-test && ctest           # Run all tests via CTest
cd build-test && ctest -V        # Verbose (show output of each test)
cd build-test && ctest -R spsc   # Run only tests matching 'spsc'
./build-test/test_encryption     # Run a single test directly
```

`./scripts/build.sh:46-49` wires the `--tests` flag to:
- `BUILD_DIR="build-test"`
- `-DJAMWIDE_BUILD_TESTS=ON -DJAMWIDE_BUILD_JUCE=OFF`

The `-DJAMWIDE_BUILD_JUCE=OFF` is intentional: most C++ tests are pure-C++ and link against `njclient` only — they don't need a JUCE plugin host. (Two tests, `test_osc_loopback` and `test_midi_mapping`, are gated under `JAMWIDE_BUILD_JUCE` and use `juce_add_console_app` instead — see CMake config below.)

### ThreadSanitizer build

```bash
./scripts/build.sh --tsan        # build-tsan/, Debug, -fsanitize=thread, JAMWIDE_TSAN=ON
cd build-tsan && ctest           # Run all tests under TSan
```

`./scripts/build.sh:53-65` wires `--tsan` to:
- `BUILD_DIR="build-tsan"`, `BUILD_TYPE="Debug"`
- `-DJAMWIDE_BUILD_TESTS=ON -DJAMWIDE_BUILD_JUCE=ON -DJAMWIDE_TSAN=ON`
- `-DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1 -fno-omit-frame-pointer"`
- `-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"`
- `-DJAMWIDE_HARDENED_RUNTIME=OFF` — TSan injects a runtime that ad-hoc codesigning does not cover (per `15.1-RESEARCH` macOS caveat #1).

The default `--tsan` target is `JamWideJuce_Standalone` — a single TSan-instrumented binary that exercises both NJClient core and the JUCE callback boundary in one process (`./scripts/build.sh:111-114`).

### TypeScript companion tests

```bash
cd companion
npm run test                     # Vitest unit tests (verbose reporter)
npm run test:e2e                 # Playwright (auto-starts vite at port 5173)
```

Configured in `companion/package.json:7-10`, `companion/vitest.config.ts`, `companion/playwright.config.ts`.

## CMake Wiring

All C++ tests are added under one `if(JAMWIDE_BUILD_TESTS)` block in `CMakeLists.txt:337-481`. Each follows the same shape:

```cmake
add_executable(test_foo tests/test_foo.cpp)
target_include_directories(test_foo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(test_foo PRIVATE <minimal-deps>)
add_test(NAME foo COMMAND test_foo)
```

**Linkage groups:**

| Test | Links |
|------|-------|
| `test_encryption` | `njclient` (the only `njclient`-linked test) |
| `test_flac_codec` | `FLAC wdl` (with `-DWDL_VORBIS_INTERFACE_ONLY` to avoid Vorbis link) |
| `test_decoder_prealloc` | `wdl vorbis vorbisenc ogg FLAC` (constructs `VorbisDecoder` directly) |
| `test_video_sync` | (none — pure arithmetic) |
| `test_njclient_atomics` | (none — atomic patterns isolated) |
| All SPSC/mirror tests | (none — pattern exercised in isolation) |

The deliberate "stand-in" pattern at `tests/test_spsc_state_updates.cpp:20-24` defines minimal `class DecodeState`, `class RemoteUser`, `class Local_Channel` inside `namespace jamwide` so the SPSC payload contracts can be exercised without linking the real types. **Use this pattern when adding a test for a new SPSC payload.**

Two JUCE-linked tests sit inside the `JAMWIDE_BUILD_JUCE` block (`CMakeLists.txt:295-333`):

- `test_osc_loopback` (`tests/test_osc_loopback.cpp`) — uses `juce_add_console_app` with `juce::juce_osc`.
- `test_midi_mapping` (`tests/test_midi_mapping.cpp`) — uses `juce_add_console_app` with `juce::juce_audio_processors`. Largest test in the repo (1,647 lines).

## CI Configuration

**Workflow:** `.github/workflows/juce-build.yml` (one workflow, three matrix-style jobs: macOS / Windows / Linux).

**What CI runs:**
- Builds JUCE plugin (VST3, AU, CLAP, Standalone) on macOS, Windows, Linux (`.github/workflows/juce-build.yml:14-277`).
- Validates each plugin format with **pluginval** at `--strictness-level 5` (`.github/workflows/juce-build.yml:79-103, 185-187, 255-262`).
- Force re-scans AU on macOS via `killall -9 AudioComponentRegistrar` before validation.
- On version tags (`v*`): imports a Developer ID cert, signs/notarizes via `scripts/notarize.sh`, packages, uploads to GitHub Releases.

**What CI does NOT run:** **the C++ unit tests are not invoked in CI.** `JAMWIDE_BUILD_TESTS` is never passed in `.github/workflows/juce-build.yml`. Tests run only locally via `./scripts/build.sh --tests` and `ctest`. **This is a coverage gap** — see [Coverage Gaps](#coverage-gaps).

`pluginval` is the only automated quality gate that runs in CI. It exercises the JUCE plugin host contract (parameter ranges, state save/load, sample-rate changes, prepareToPlay/processBlock churn) but does NOT exercise NINJAM-protocol logic, crypto, SPSC primitives, or audio-thread mirror invariants.

## ThreadSanitizer Setup

**Build:** `./scripts/build.sh --tsan` → `build-tsan/`.

**Coverage targets** (every test file under `JAMWIDE_BUILD_TESTS=ON` is compiled with `-fsanitize=thread` when the flag is also `JAMWIDE_TSAN=ON`):

- `test_njclient_atomics` (`tests/test_njclient_atomics.cpp:1-12`) — release/acquire pattern for BPM/BPI atomic-bundle publication. 100,000-iteration concurrent producer/consumer.
- `test_spsc_state_updates` — every variant alternative of `RemoteUserUpdate`, `LocalChannelUpdate`, plus concurrent push/pop.
- `test_deferred_delete` — 256-burst push + drain stress + 50 Hz audio-rate producer/consumer + overflow counter (Codex M-8).
- `test_local_channel_mirror`, `test_remote_user_mirror`, `test_block_queue_spsc`, `test_decode_media_buffer_spsc`, `test_decode_state_arming` — concurrent mutation/apply for each mirror, generation-gate deferred-free coverage.
- `test_peer_churn_simulation` (`tests/test_peer_churn_simulation.cpp`) — three concurrent threads (audio + run + reaper) cycle 1000 peer-churn patterns; acceptance gates: drop counters == 0, TSan reports == 0.
- The TSan-instrumented `JamWideJuce_Standalone` binary covers the JUCE callback boundary in addition to the unit-test surface.

**TSan acceptance gates** (per phase plans):
- Zero TSan warnings.
- Zero drop-counter increments at the end of the test (read via `Get*DropCount()` / `Get*OverflowCount()` `noexcept` getters on `NJClient`).

**TSan caveats:**
- macOS hardened runtime is incompatible with TSan; `JAMWIDE_HARDENED_RUNTIME=OFF` is forced in the TSan build (`CMakeLists.txt`, see scripts/build.sh:62).
- TSan instruments `std::atomic`, so atomic-only tests still produce useful TSan output.

## Test Structure

A typical test file follows this skeleton (from `tests/test_encryption.cpp` and `tests/test_spsc_state_updates.cpp`):

```cpp
/*
    test_<area>.cpp - <one-line purpose>
    <multi-line description>
*/

// (Optional stand-in types in namespace jamwide for forward-declared classes)
namespace jamwide { class DecodeState { public: int marker = 0; }; }

#include "<module-under-test>"
#include <atomic>
#include <cstdio>
#include <thread>

// === Test framework macros (verbatim copy from test_encryption.cpp:25-44) ===
static int tests_run = 0;
static int tests_passed = 0;
#define TEST(name) ...
#define PASS()     ...
#define FAIL(msg)  ...

// === Test 1: <name> ===
static void test_<area>_<scenario>() {
    TEST("<scenario description>");
    // ... arrange + act + assert ...
    if (ok) PASS(); else FAIL("<reason>");
}

// === main ===
int main() {
    printf("=== <Area> Tests ===\n\n");
    test_<area>_<scenario>();
    // ...
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
```

**Each test is a `static void`** that calls `TEST("name")` once and `PASS()` or `FAIL(msg)` once. Multi-step tests use a single `bool ok = true; ok &= ...;` pattern to fail fast (see `tests/test_spsc_state_updates.cpp:67-115`).

## What Is Tested

| Area | Test file | Coverage |
|------|-----------|----------|
| **Crypto (AES-256-CBC + SHA-256 KDF)** | `tests/test_encryption.cpp` | Round-trip (varied sizes), zero-length, IV randomness, known-vectors with deterministic IV (test-only `encrypt_payload_with_iv`), failure modes (wrong key, truncated, non-aligned, corrupted padding), no-partial-plaintext-on-failure invariant, max-payload guard. **Most thoroughly tested module in the repo.** |
| **FLAC codec** | `tests/test_flac_codec.cpp` | FLAC encoder/decoder round-trip via `WDL_VORBIS_INTERFACE_ONLY` interface. |
| **Atomic publication pattern** | `tests/test_njclient_atomics.cpp` | CR-03 release/acquire for `m_bpm`/`m_bpi`/`m_beatinfo_updated`. Single-threaded round-trip + 100k-iter concurrent publish/consume + Codex L-10 edge-triggered semantics. |
| **SPSC primitives** | `tests/test_spsc_state_updates.cpp` | Every variant alternative of `RemoteUserUpdate` and `LocalChannelUpdate` round-trips through the ring; concurrent producer/consumer for every payload. Codex M-9 acceptance: every payload (incl. `DecodeArmRequest`) exercised at Wave 0. Codex HIGH-3: deferred-free transports for `RemoteUser*` and `Local_Channel*`. |
| **Audio-thread mirrors** | `test_local_channel_mirror.cpp`, `test_remote_user_mirror.cpp` | Concurrent mutation/apply, no-back-pointer (HIGH-2), generation-gate deferred-free (HIGH-3). |
| **Per-channel block queues** | `test_block_queue_spsc.cpp`, `test_decode_media_buffer_spsc.cpp` | Fill/drain integrity, overflow counter, bounds-check (Codex M-7), partial-read consumer buffer, atomic refcnt UAF-safety. |
| **Decoder arming** | `test_decode_state_arming.cpp` | Arm-request round-trip, capacity bound, concurrent producer/consumer, `decode_fp == nullptr` post-publish (Codex HIGH-1 audit invariant), refill-loop byte-stream integrity, dead-entry reaper (CR-08, H-04). |
| **Decoder prealloc** | `test_decoder_prealloc.cpp` | Sanity: `VorbisDecoder` + `FlacDecoder` construct cleanly post-`Prealloc`; `SetMaxAudioBlockSize` bound enforcement (Codex M-7) reimplemented in-test via stand-in lambda. **Strict allocation-count verification deferred to Instruments UAT.** |
| **Deferred-delete SPSC** | `test_deferred_delete.cpp` | 256-burst push + drain, 50 Hz audio-rate producer/consumer, overflow-counter mechanism (Codex M-8). |
| **Peer-churn simulation** | `tests/test_peer_churn_simulation.cpp` | NINJAM-like end-to-end automated coverage: 3 threads × 1000 peer-churn cycles, all production SPSC primitives, drop counters == 0 acceptance gate. |
| **Video sync formula** | `tests/test_video_sync.cpp` | Pure-arithmetic test of `delay_ms = (60 / bpm) * bpi * 1000`. Edge cases: NaN, negative, zero, fractional truncation. Formula duplicated from `juce/video/VideoCompanion.cpp:470` because the header is frozen. |
| **OSC loopback (JUCE)** | `tests/test_osc_loopback.cpp` | OSC sender/receiver smoke test using JUCE's `juce::OSCSender`/`juce::OSCReceiver`. |
| **MIDI mapping (JUCE)** | `tests/test_midi_mapping.cpp` | Largest test in the repo (1,647 lines) — MIDI Learn, CC routing, APVTS-parameter mapping, persistence. |
| **Companion buffer-delay relay (TS)** | `companion/src/__tests__/buffer-delay.test.ts` | `setLastAutoDelay` / `getActiveDelayMs` / `reapplyActiveDelay` plus iframe `postMessage` to VDO.Ninja origin. |
| **Companion video sync E2E** | `companion/e2e/video-sync.spec.ts` | Playwright integration with mock WS server (`mock-ws-server.ts`). |

## Common Patterns

### Test-only crypto symbols

`src/crypto/nj_crypto.h:36-43` exposes `encrypt_payload_with_iv` (deterministic-IV variant) **only** when `JAMWIDE_BUILD_TESTS` is defined. When you need a deterministic version of an otherwise random-IV API, follow this pattern: declare under `#ifdef JAMWIDE_BUILD_TESTS` and document loudly that production code MUST NOT use it.

The CMake glue is at `CMakeLists.txt:130-132`:
```cmake
if(JAMWIDE_BUILD_TESTS)
    target_compile_definitions(njclient PRIVATE JAMWIDE_BUILD_TESTS=1)
endif()
```

### Stand-in types for forward-declared classes

When a header forward-declares types (`class DecodeState; class RemoteUser; class Local_Channel;` in `src/threading/spsc_payloads.h:48-50`), tests that exercise the SPSC contract in isolation define minimal stand-ins:

```cpp
namespace jamwide {
class DecodeState   { public: int marker = 0; };
class RemoteUser    { public: int marker = 0; };
class Local_Channel { public: int marker = 0; };
}  // include header AFTER these definitions
#include "threading/spsc_payloads.h"
```

(`tests/test_spsc_state_updates.cpp:20-27`)

This avoids linking the real `njclient` library and keeps the test fast + TSan-friendly.

### Async / threaded test pattern

Concurrent producer/consumer tests use `std::thread`, `std::atomic<bool> stop{false}`, and a fixed-iteration loop with `100000` typical iterations:

```cpp
constexpr int kIters = 100000;
std::atomic<bool> stop{false};
std::thread producer([&]{ /* push for kIters */ });
std::thread consumer([&]{ /* pop until stop */ });
producer.join();
stop.store(true);
consumer.join();
```

(`tests/test_njclient_atomics.cpp:75-99`)

### Drop-counter / overflow-counter assertion

After a stress test, read counters via the `noexcept` getters and assert they remain at 0 (or at the expected drop count):

- `NJClient::GetDeferredDeleteOverflowCount()` (`src/core/njclient.h:541`)
- `NJClient::GetBlockQueueDropCount()` (`src/core/njclient.h:551`)
- `NJClient::GetLocalChannelUpdateOverflowCount()` (`src/core/njclient.h:854`)
- `NJClient::GetRemoteUserUpdateOverflowCount()` (`src/core/njclient.h:877`)
- `NJClient::GetArmRequestDropCount()` (`src/core/njclient.h:886`)
- `NJClient::GetSessionmodeRefillDropCount()` (`src/core/njclient.h:896`)

### Documenting WHY each test exists

Every C++ test file's leading comment cites the phase plan + Codex review item that motivated it (e.g. "Codex M-9 acceptance", "Codex HIGH-2 architectural fix", "15.1-04 SPSC infrastructure"). Continue this — when adding a test, link it back to the plan/review item it discharges.

## Companion Test Stack

The `companion/` directory has its own toolchain:

- **Unit tests:** Vitest 4.x, jsdom environment, `companion/vitest.config.ts:1-6`. Glob: `src/__tests__/**/*.test.ts`. 9 unit-test files exist as of 2026-04-30.
- **E2E tests:** Playwright 1.50, auto-starts `vite` dev server on port 5173, `companion/playwright.config.ts:1-13`. Includes a `mock-ws-server.ts` for WebSocket fixtures.
- **TypeScript:** strict mode (`companion/tsconfig.json`).

Vitest tests use `describe` / `it` / `expect` / `vi.fn()` (`companion/src/__tests__/buffer-delay.test.ts:1-2`). DOM mutation is allowed in `beforeEach` for setting up isolated test fixtures.

## Coverage Gaps

The following areas have **no automated coverage** and rely on manual UAT:

1. **CI doesn't run `ctest`.** The unit tests exist and run locally, but `.github/workflows/juce-build.yml` builds plugins and runs `pluginval` only. Adding `cmake -DJAMWIDE_BUILD_TESTS=ON … && ctest --output-on-failure` to a separate CI job would close the largest gap. **High priority.**
2. **No fuzzing of network protocol.** `src/core/netmsg.cpp` and `src/core/mpb.cpp` parse untrusted server input. Crypto-failure paths in `tests/test_encryption.cpp` cover authenticated-decryption rejection, but the broader NJ-message parser is not fuzzed.
3. **Strict allocation-count verification on the audio thread.** `tests/test_decoder_prealloc.cpp:13-21` notes "Strict allocation-count verification deferred to 15.1-10 Instruments UAT" — currently a manual Instruments run, not an automated test.
4. **`NJClient::Run()` integration / state-machine coverage.** The run thread's logic (interval boundaries, encoder swap, codec switch, reconnect) has no end-to-end test. `test_peer_churn_simulation` exercises the SPSC primitives in isolation, not the real `Run()` body.
5. **UI / `JamWideJuceEditor` interaction tests.** No JUCE UnitTest harness for editor behavior. `test_midi_mapping` covers MIDI Learn at the model layer, not at the UI layer.
6. **Standalone-app launch / login / save-load happy path.** No automated coverage; manual UAT only (memory: "feedback_uat_scope_redflags" — phase 15.1-06 broke broadcast and was caught only by UAT).
7. **VDO.ninja video bridge (companion).** Vitest covers buffer-delay relay logic; Playwright E2E exercises a mock WS server. Real VDO.ninja iframe behavior is not tested in CI.

When adding a phase, **prefer adding tests that close one of these gaps over adding a new test in an already-well-covered area** (crypto, SPSC, mirrors).

---

*Testing analysis: 2026-04-30*
