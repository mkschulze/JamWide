# Coding Conventions

**Analysis Date:** 2026-04-30

JamWide is a C++20 JUCE/CLAP audio plugin built atop the Cockos NINJAM core (`src/core/njclient.cpp` — 4700+ lines, GPLv2). The conventions split cleanly into three regions:

1. **Legacy NINJAM core** (`src/core/`, `wdl/`) — Cockos house style, `m_` member prefix, K&R braces, WDL helpers (`WDL_PtrList`, `WDL_HeapBuf`, `WDL_MutexLock`).
2. **JamWide additions** (`src/threading/`, `src/crypto/`, `src/debug/`, `src/net/`, `src/ui/`) — modern C++20, `namespace jamwide`, snake_case + trailing underscore (`head_`, `tail_`), Allman braces, `std::atomic` / `std::optional` / `std::variant`.
3. **JUCE plugin layer** (`juce/`) — JUCE house style, PascalCase classes, camelCase methods, trailing-underscore private members, JUCE macros (`JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR`, `jassert`).

When adding new code, **match the region**: extending NINJAM core uses NINJAM style; new SPSC primitives go in `namespace jamwide` with modern style; UI components follow JUCE style.

## Tooling

**No `.clang-format`, `.clang-tidy`, or `.editorconfig` in the repo.** Style is enforced by reviewer convention, not tooling. There is no auto-formatter; do not introduce one without team agreement (the legacy NINJAM core is hand-formatted in a way `clang-format` would mass-rewrite).

**Build flags** (`CMakeLists.txt:23-26`):
- `CMAKE_CXX_STANDARD 20` (required)
- `CMAKE_POSITION_INDEPENDENT_CODE ON`
- `CMAKE_EXPORT_COMPILE_COMMANDS ON` — `compile_commands.json` is emitted; clangd works out of the box.
- Windows: `NOMINMAX`, `WIN32_LEAN_AND_MEAN` (`CMakeLists.txt:7`)

**Compile-time defines** that gate features:
- `JAMWIDE_DEV_BUILD` — verbose logging via `NLOG_VERBOSE` (`src/debug/logging.h:75`)
- `JAMWIDE_BUILD_TESTS` — exposes test-only crypto symbols (`src/crypto/nj_crypto.h:36-43`) and adds tests to CMake
- `JAMWIDE_TSAN` — ThreadSanitizer build (skips codesign/hardened runtime)
- `JAMWIDE_BUILD_JUCE` — gate JUCE plugin targets (default ON)

## Naming Patterns

### Files

| Region | Pattern | Example |
|--------|---------|---------|
| NINJAM core | lowercase, underscore | `src/core/njclient.cpp`, `src/core/netmsg.h` |
| jamwide modern | lowercase, underscore | `src/threading/spsc_ring.h`, `src/crypto/nj_crypto.cpp` |
| JUCE plugin | PascalCase | `juce/JamWideJuceProcessor.cpp`, `juce/ui/VbFader.h` |
| JUCE platform-suffixed | `_mac.mm`, `_win.cpp` | `juce/video/BrowserDetect_mac.mm` |
| Tests | `test_<area>.cpp` | `tests/test_spsc_state_updates.cpp` |
| Companion (TS) | kebab-case | `companion/src/__tests__/buffer-delay.test.ts` |

### Classes / structs

- **NINJAM core:** PascalCase or partial caps (`NJClient`, `RemoteUser`, `Local_Channel` — note the underscore is legacy Cockos), all in the global namespace.
- **`namespace jamwide`:** PascalCase (`SpscRing`, `BlockRecord`, `LocalChannelMirror`, `RemoteUserMirror`, `PeerAddedUpdate`).
- **JUCE plugin:** PascalCase (`JamWideJuceProcessor`, `VbFader`, `ChannelStripArea`, `MidiMapper`).

### Methods / functions

- **NINJAM core:** PascalCase (Cockos style): `NJClient::Connect`, `NJClient::AudioProc`, `GetLocalChannelInfo`. Free functions in `njclient.cpp` are `camelCase` or lowercase: `currentMillis`, `pushBlockRecord` (`src/core/njclient.cpp:39, 59`).
- **JUCE plugin:** camelCase override of JUCE method names: `prepareToPlay`, `processBlock`, `syncApvtsToAtomics`, `collectInputChannels` (`juce/JamWideJuceProcessor.cpp:151, 488, 214, 238`).
- **`namespace jamwide` / threading:** snake_case: `try_push`, `try_pop`, `drain`, `empty`, `size` (`src/threading/spsc_ring.h:51-138`).

### Member variables

- **NINJAM core:** `m_` prefix, snake_case after: `m_errstr`, `m_workdir`, `m_users_cs`, `m_locchan_cs`, `m_remoteuser_mirror` (`src/core/njclient.h:307, 657, 718`).
- **`namespace jamwide`:** trailing underscore for private: `head_`, `tail_`, `mask_`, `buffer_` (`src/threading/spsc_ring.h:140-147`).
- **JUCE plugin:** trailing underscore for private members of UI components: `value_`, `defaultValue_`, `dragStartY_`, `attachment_`, `midiMapper_` (`juce/ui/VbFader.h:78-93`). Public-by-design members on the `AudioProcessor` are bare names: `apvts`, `cmd_queue`, `evt_queue`, `chat_queue` (`juce/JamWideJuceProcessor.h:106-111`).

### Constants

- `kFooBar` style for static/class constants: `NJClient::kRemoteNameMax`, `kThumbDiameter`, `kTotalOutChannels`, `kInstaMeasIdle`, `kRemoteNameMax` (`src/core/njclient.h:296`, `juce/ui/VbFader.h:97`, `juce/JamWideJuceProcessor.h:89-91`).
- ALL_CAPS for legacy `#define` macros that survive: `MAX_LOCAL_CHANNELS`, `MAX_USER_CHANNELS`, `MAX_PEERS`, `NJ_CRYPTO_MAX_PLAINTEXT` (`src/core/njclient.h:101-119`, `src/crypto/nj_crypto.h:12`).

### Phase / commit annotations

Almost every recent change is annotated with the GSD phase number it landed in. Examples: `// 15.1-02 CR-03:`, `// 15.1-04 SPSC infrastructure:`, `// 15.1-07a CR-01:`, `// Codex HIGH-2 architectural fix:`, `// Codex M-7 response:`. These survive in source as the canonical "why" record. When extending these regions, add your own phase tag (e.g. `// 15.1-08 M-01:`) so future readers can trace the design decision.

## Header Style

- **JamWide-original headers** use `#pragma once` (all of `juce/`, including `juce/JamWideJuceProcessor.h:1`, `juce/ui/VbFader.h:1`).
- **`namespace jamwide` and `src/` headers** use traditional include guards: `#ifndef SPSC_RING_H` / `#define SPSC_RING_H` / `#endif // SPSC_RING_H` (`src/threading/spsc_ring.h:9-152`, `src/threading/spsc_payloads.h:27-28`, `src/debug/logging.h:9-10`, `src/crypto/nj_crypto.h:1-58`).
- **NINJAM core** uses underscore-bracketed include guards in Cockos style: `#ifndef _NJCLIENT_H_` (`src/core/njclient.h:58`).

When adding a new header: `#pragma once` in `juce/`, traditional guards everywhere else.

## Copyright Headers

There are three header conventions in active use:

**1. NINJAM core (Cockos GPLv2):**
```cpp
/*
    NINJAM - njclient.h
    Copyright (C) 2005 Cockos Incorporated
    NINJAM is free software; you can redistribute it and/or modify
    ...
*/
```
Used in `src/core/njclient.{cpp,h}`, `src/core/mpb.{cpp,h}`, `src/core/netmsg.{cpp,h}`. **Do not modify these headers.** The GPLv2 attribution must remain.

**2. JamWide-original (GPLv2-compatible):**
```cpp
/*
    JamWide Plugin - <filename>
    <one-line purpose>

    Copyright (C) 2024 JamWide Contributors
    Licensed under GPLv2+
*/
```
Used in `src/threading/spsc_ring.h:1-7`, `src/threading/spsc_payloads.h:1-25`, `src/debug/logging.h:1-7`.

**3. Short purpose-only header (no copyright line):**
```cpp
/*
    OscServer.cpp - Bidirectional OSC server for JamWide
    <multi-line description>
*/
```
Common in `juce/` files (`juce/osc/OscServer.cpp:1-9`). The repo `LICENSE` (GPLv2+) is the canonical license; per-file copyright lines are optional for new files. **Match the surrounding file when extending an area.**

JUCE component `.cpp` files often skip the comment block entirely and start with includes (`juce/ui/VbFader.cpp:1-4`). That is acceptable for short component files where the header carries the description.

## Brace / Indentation Style

- **NINJAM core (`src/core/njclient.cpp`):** K&R-ish, opening brace on the same line for `if`/`while`, but Allman for function bodies. 2-space indent. No trailing space discipline. Tight (often single-line) `if` bodies with implicit braces. **Do not reformat existing NINJAM code** — diffs become unreadable.
- **`namespace jamwide` and JUCE plugin code:** Allman braces, 4-space indent. Example: `src/threading/spsc_ring.h:51-63`, `juce/JamWideJuceProcessor.cpp:151-189`.

## `const` and `noexcept`

- **NINJAM core:** Sparse `const` discipline. Many getters return non-const pointers/references for legacy reasons. Don't tighten existing signatures without a reason.
- **JamWide additions:** Strong `const`-correctness:
  - Trivial getters are `const`: `static constexpr std::size_t capacity() { return N; }` (`src/threading/spsc_ring.h:138`).
  - Counter accessors are `const noexcept`: `uint64_t GetDeferredDeleteOverflowCount() const noexcept` (`src/core/njclient.h:541`, `:551`, `:854`, `:877`, `:886`, `:896`).
  - SPSC primitives are explicitly non-copyable, non-movable: `SpscRing(const SpscRing&) = delete; SpscRing& operator=(const SpscRing&) = delete;` (`src/threading/spsc_ring.h:41-44`).
- **JUCE plugin classes** add `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(<ClassName>)` as the last line of the class body (`juce/ui/VbFader.h:104`, `juce/JamWideJuceProcessor.h:247`, `juce/ui/BeatBar.h:43`, etc.). **Always add this to new JUCE components.**

## Error Handling

JamWide uses **return codes and `bool ok` payload structs**, not exceptions, for all hot-path and run-thread code. Exceptions are reserved for one explicit setup-time contract violation.

### Run-thread / hot path: return codes

NINJAM-core convention: integer error codes, `NULL`/`nullptr`, `-1`, or `false`. Examples from `src/core/njclient.cpp`:

- `NJClient::GetUserSessionPos` returns `-1.0` on bad input (`src/core/njclient.cpp:551`).
- `NJClient::GetUserState` returns `NULL` if `useridx` is out of range (`:4134, :4259, :4274`).
- `NJClient::EnumLocalChannels` / `EnumUserChannels` return `-1` to signal end-of-iteration (`:4259, :4267, :4698`).

### Crypto module: tagged result struct

`src/crypto/nj_crypto.h:14-22` defines:
```cpp
struct EncryptedPayload { std::vector<unsigned char> data; bool ok = false; };
struct DecryptedPayload { std::vector<unsigned char> data; bool ok = false; };
```
Documented contract: **on failure, `ok=false` AND `data` is empty (no partial plaintext EVER)** (`src/crypto/nj_crypto.h:29, :48`). Crypto callers MUST check `.ok` before using `.data`. This pattern is preferred for any new module that has a clear success/failure binary.

### Setup-time only: exceptions

The single exception in the codebase is `NJClient::SetMaxAudioBlockSize` (`src/core/njclient.cpp:995-1007`). It throws `std::runtime_error` if the host's `samplesPerBlock` exceeds `jamwide::MAX_BLOCK_SAMPLES`. The contract:

- **Caller:** non-audio thread (JUCE `prepareToPlay`).
- **Caught by:** `JamWideJuceProcessor::prepareToPlay` (`juce/JamWideJuceProcessor.cpp:167-177`) — logs via `juce::Logger::writeToLog` and degrades to the previous-prepared size.
- **NEVER** call this from the audio thread, and NEVER add new `throw` sites in the audio path.

### Audio thread: assertions + bounds-check + drop-counter

Audio-thread invariants are enforced by a three-layer defense:

1. **`jassert(numSamples <= prevPreparedSize);`** at the top of `processBlock` (`juce/JamWideJuceProcessor.cpp:499`) — Debug-build catch for host contract violations.
2. **Per-callsite bounds check before `memcpy`** in `pushBlockRecord` and `pushWaveBlockRecord` (`src/core/njclient.cpp:70-78, :117-121`) — Release-build defense.
3. **Drop counter** (`std::atomic<uint64_t>`) bumped via `fetch_add(1, std::memory_order_relaxed)` on out-of-bounds or full-ring (`src/core/njclient.cpp:76, :103`). Read off the audio thread via `noexcept` getters (e.g. `GetBlockQueueDropCount` `src/core/njclient.h:551`).

When adding a new audio-thread producer, follow this exact pattern: bounds-check, `try_push`, `fetch_add` on failure. **Never** `throw`, **never** `assert(false)` (it can abort the host process), **never** allocate.

## Real-time-Safety Conventions

The audio thread is the dominant correctness concern in this codebase. The 15.1-* phases (`src/core/njclient.h:121-287` for the architectural commentary) were a multi-month project to remove every `WDL_MutexLock` from the `AudioProc` path.

### Hard rules — audio thread MUST NOT

- **Allocate**: no `new`, no `malloc`, no `std::vector::push_back`, no `juce::String` construction. All buffers are pre-grown in `prepareToPlay` (`juce/JamWideJuceProcessor.cpp:158, 169`) via `NJClient::SetMaxAudioBlockSize` → `tmpblock.Prealloc` (`src/core/njclient.cpp:1006`) and `outputScratch.setSize(..., avoidReallocating=true)` (`juce/JamWideJuceProcessor.cpp:514`).
- **Lock**: as of 15.1-07a, `AudioProc` no longer takes `m_users_cs` or `m_locchan_cs`. The state mirrors (`m_remoteuser_mirror[MAX_PEERS]`, `m_localchan_mirror[MAX_LOCAL_CHANNELS]`) are owned exclusively by the audio thread and updated by draining SPSC queues at the top of `AudioProc` (`src/core/njclient.cpp:1094, 1100`).
- **Syscall**: no file I/O, no `printf`. Logging on the audio thread is forbidden.
- **`throw`**: see error-handling section above.
- **Dereference run-thread-owned pointers**: enforced by the **mirror-by-value** pattern (Codex HIGH-2). The audio thread reads only POD/`std::atomic` fields. No `RemoteUser*`, `Local_Channel*`, or other run-thread-owned back-pointers leak into the mirror (`src/core/njclient.h:124-135` and `:208-218`).

### Soft rules

- `juce::ScopedNoDenormals noDenormals;` at the top of `processBlock` (`juce/JamWideJuceProcessor.cpp:489`).
- Atomic ordering is documented inline. The pattern is **release-store on the writer, acquire-load on the reader**, with `relaxed` for ordinary scalar payload (e.g. `m_bpm`, `m_bpi`) and an acquire-flag (`m_beatinfo_updated`) gating the bundle. Documented inline at `src/core/njclient.h:354-374` and exercised by `tests/test_njclient_atomics.cpp`.
- Drop counters exist for every cross-thread queue. Read them in UAT to detect overflow regressions.

### When you MUST update audio-thread state from the run thread

Use a **SPSC ring of trivially-copyable variant payloads**. The pattern is:

1. Define a `struct FooUpdate { ... };` with POD fields only — no `std::string`, no smart pointers (POD pointers like `DecodeState*` are OK only when ownership transfers).
2. Add it to a `std::variant` (e.g. `RemoteUserUpdate`, `LocalChannelUpdate` in `src/threading/spsc_payloads.h:109-116`).
3. Add a `static_assert(std::is_trivially_copyable_v<...Update>, "...");` (`src/threading/spsc_payloads.h:118`).
4. Producer (run thread) does `ring.try_push(FooUpdate{...});`; bump a drop counter on `try_push==false`.
5. Consumer (audio thread) drains in the corresponding `drainXxxUpdates` member at the top of `AudioProc`.

Reference implementation: every payload in `src/threading/spsc_payloads.h`. Reference test: `tests/test_spsc_state_updates.cpp`.

### Generation-gate for deferred-free

When the audio thread mirror "drops" a reference to a run-thread-owned object (peer remove, local channel delete), the run thread MUST NOT immediately free the object. Instead it bumps `m_audio_drain_generation` (audio side, `src/core/njclient.cpp:1108`, release-store) and the run thread waits with `std::this_thread::yield()` until two generations have passed (proves the audio thread has drained the queue at least once after publication). See `src/core/njclient.h:686-708` for the contract.

## Threading Conventions

The threading model is documented authoritatively in `juce/JamWideJuceProcessor.h:24-54` (the "THREADING CONTRACT" comment). Three threads:

| Thread | Owner | Holds |
|--------|-------|-------|
| **Message thread** (JUCE UI) | `JamWideJuceEditor` + APVTS | NEVER `clientLock`. Pushes `UiCommand` via `cmd_queue.try_push`; drains `UiEvent` from `evt_queue`. |
| **Run thread** (`NinjamRunThread`) | `NJClient::Run()` loop, ~50 ms | Holds `clientLock` (`juce::CriticalSection`) during `Run()` and command processing. Acquires `m_users_cs`, `m_locchan_cs` for canonical state. |
| **Audio thread** (`processBlock`) | `NJClient::AudioProc` | Holds NOTHING. Reads atomics + drains SPSC mirror queues. |

### Mutex types in use

- `juce::CriticalSection` for the JUCE plugin's `clientLock` (`juce/JamWideJuceProcessor.cpp` — used to serialize `NinjamRunThread` against APVTS-driven config).
- `WDL_Mutex` + `WDL_MutexLock` (RAII) for NINJAM-core canonical state: `m_users_cs`, `m_locchan_cs`, `m_misc_cs` (`src/core/njclient.cpp:1027, 1237`).
- `std::mutex` + `std::condition_variable` for one-shot license-acceptance handshake between run thread and message thread (`juce/JamWideJuceProcessor.cpp:202-207`).
- `std::atomic<T>` for everything else cross-thread: peak meters, BPM/BPI, status, all APVTS-mirror config (`src/core/njclient.h:319-330`).

When adding a new cross-thread field, **prefer atomic over mutex** if it is a scalar; **prefer SPSC variant queue over mutex** if it is structured.

## Logging

**Framework:** `src/debug/logging.h` — a tiny `fopen`-once, `vfprintf`-on-call wrapper. Two macros:

- `NLOG(fmt, ...)` — always-compiled. For important runtime events. Logs to `/tmp/jamwide.log` (or `~/Library/Logs/jamwide.log` if `/tmp` is unwritable).
- `NLOG_VERBOSE(fmt, ...)` — compiles to `((void)0)` unless `JAMWIDE_DEV_BUILD` is defined (`src/debug/logging.h:75-79`).

**Audio thread:** **never** call `NLOG`. The macro path goes through `vfprintf`/`fflush`, which can block on I/O.

**JUCE plugin:** uses `juce::Logger::writeToLog(juce::String(...))` for plugin-host-visible messages (`juce/JamWideJuceProcessor.cpp:175`).

## Comments

Comment density in JamWide is **high and intentional** — the codebase tracks years of phase-numbered design decisions inline. Three comment patterns to follow:

### 1. Phase-tagged inline notes

Every non-obvious change carries a phase tag. Use these so future maintainers can find the plan that introduced or modified the code.

```cpp
// 15.1-06 CR-02: m_locchan_cs.Enter/Leave removed. Audio thread reads from
// the per-channel mirror[m_max_localch] populated by drainLocalChannelUpdates at
// top of AudioProc. Mirror entries are by-VALUE (Codex HIGH-2: no Local_Channel*
// back-pointer).
```
(`src/core/njclient.cpp:2568-2570`)

### 2. Threading contracts as block comments

When a function's threading semantics are non-obvious, write a block comment describing reader/writer/lock requirements. The canonical examples:

- `juce/JamWideJuceProcessor.h:24-54` — the THREADING CONTRACT block.
- `src/core/njclient.h:354-374` — the BPM/BPI publication protocol.
- `src/threading/spsc_ring.h:21-31` — class-doc threading model.

Always describe: which thread reads, which thread writes, what synchronizes them, what they may NOT do.

### 3. Doxygen-style on jamwide-namespace headers

Pure-`namespace jamwide` headers use `/** ... */` Doxygen blocks (`src/threading/spsc_ring.h:46-50, 84-87, 100-106`). Continue this style in new threading/utility headers.

### Avoid

- TODO/FIXME comments. There are **zero** TODO/FIXME/XXX/HACK markers in `src/`, `juce/`, and `tests/` as of 2026-04-30 — this is a deliberate convention. Issues become phase items in `.planning/`, not source markers.
- "what" comments restating the next line. Prefer "why" — the architectural reason this code exists.

## Imports / Includes

**Order** (observed across `src/core/njclient.cpp:26-37`, `juce/JamWideJuceProcessor.h:1-17`):

1. C standard library (`<math.h>`, `<stdio.h>`, `<cstdint>`, `<cstring>`)
2. C++ standard library (`<atomic>`, `<chrono>`, `<thread>`, `<variant>`, `<vector>`)
3. Platform headers (`<windows.h>` under `#ifdef _WIN32`)
4. Third-party / vendored (`"../wdl/wdlstring.h"`, `<JuceHeader.h>`)
5. Project headers (`"njclient.h"`, `"core/njclient.h"`, `"threading/spsc_ring.h"`)

**Path style:** project headers use either `"core/..."` (when the include path is `src/`) or `"../wdl/..."` (relative). Prefer `"core/..."` / `"threading/..."` / `"crypto/..."` for new includes — `target_include_directories(... PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)` is set globally (`CMakeLists.txt:117-120`).

JUCE files use `<JuceHeader.h>` (auto-generated by `juce_generate_juce_header`).

## Function Design

- **Audio-thread functions** are split into small helpers grouped by concern. See `JamWideJuceProcessor`'s `syncApvtsToAtomics`, `collectInputChannels`, `routeOutputsToJuceBuses`, `accumulateBusesToMainMix`, `measureMasterVu` (`juce/JamWideJuceProcessor.cpp:214, 238`). Each is a single-purpose, no-allocation method.
- **Run-thread NJClient methods** are large and historic. `NJClient::Run()` is many hundreds of lines. **Do not** refactor monolithically — extract small helpers when introducing new logic and tag with the phase number.
- **Crypto / SPSC / mirror code** uses small focused free functions or methods (`pushBlockRecord` 47 lines, `SetMaxAudioBlockSize` 13 lines, `SpscRing::try_push` 12 lines).

## Module Design

- `namespace jamwide` for all NEW reusable utilities (`src/threading/`, `src/debug/logging.h`, `src/crypto/nj_crypto.h` — note: nj_crypto is NOT in the namespace, but the threading/logging code is).
- No barrel-files / index headers. Each header is included directly.
- Forward-declare in headers, include in `.cpp` (`src/core/njclient.h:89-96` declares `class I_NJEncoder; class RemoteDownload;` etc.).
- `class ::DecodeState* ds = nullptr;` — explicit global-namespace qualifier when using a forward-declared global type from inside `namespace jamwide` (`src/threading/spsc_payloads.h:240`).

## TypeScript Companion (`companion/`)

The `companion/` directory is a separate TypeScript/Vite app for the video bridge. Conventions there are unrelated to the C++ codebase:

- camelCase functions, PascalCase types, `import { describe, it, expect } from 'vitest'`.
- Tests in `companion/src/__tests__/*.test.ts` (Vitest, jsdom).
- E2E tests in `companion/e2e/*.spec.ts` (Playwright).

This area is fully self-contained — its style/test setup does not propagate into `src/` or `juce/`.

---

*Convention analysis: 2026-04-30*
