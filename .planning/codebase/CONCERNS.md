# Codebase Concerns

**Analysis Date:** 2026-04-30
**Milestone context:** v1.2 (Phase 15.1 RT-Safety Hardening) — 9/10 sub-plans landed; 15.1-07a stabilization in progress; awaiting bundled UAT post build 290.

This document inventories technical debt, known bugs, security concerns, real-time safety risks, performance hot spots, fragile areas, and TODO/FIXME density. Each finding cites verified file:line evidence. Severity tags help triage: HIGH (release-blocker / data-loss / RT-violation), MED (correctness or maintainability), LOW (nice-to-have).

---

## Tech Debt

### Audio-thread `inputScratch` not preallocated [HIGH]

- Issue: `juce/JamWideJuceProcessor.cpp:248` calls `inputScratch.setSize(maxInputChannels, numSamples, false, false, true)` from inside `processBlock`. `inputScratch` is declared at `juce/JamWideJuceProcessor.h:212` but is NOT pre-sized in `prepareToPlay` (line 151–189 only calls `outputScratch.setSize` at line 158).
- Files: `juce/JamWideJuceProcessor.cpp:248`, `juce/JamWideJuceProcessor.cpp:151-189`, `juce/JamWideJuceProcessor.h:212`
- Impact: First `processBlock` call after a `prepareToPlay` allocates on the audio thread. Any host that bumps the buffer size mid-stream (Logic, Reaper resizing) reallocates on the audio thread. JUCE's `avoidReallocating=true` only avoids realloc when SHRINKING.
- Mirror fix: 15.1-08 closed M-01/M-03 for `outputScratch` + `tmpblock` but did NOT cover `inputScratch`. The asymmetry slipped through the prealloc audit.
- Fix approach: In `prepareToPlay`, after line 158, add `inputScratch.setSize(8, samplesPerBlock, false, true, false);` (8 = `getBusCount(true) * 2` for 4 stereo input buses). The `processBlock` call site keeps its safety-net resize for oversize-host-block paranoia (mirrors `outputScratch` at line 513-514).

### `JAMWIDE_DEV_BUILD=ON` is the default [HIGH]

- Issue: `CMakeLists.txt:30` defaults `JAMWIDE_DEV_BUILD` to ON, gating verbose `fopen("/tmp/jamwide.log", "a")` blocks at `src/core/njclient.cpp:1533, 1553, 1569, 1625, 1639` (auth challenge + license + encryption negotiation). These run on the run thread (network message handler), not the audio thread, so RT-safety is fine.
- Files: `CMakeLists.txt:30`, `src/core/njclient.cpp:1530-1645`
- Impact: Release/CI builds that don't override `-DJAMWIDE_DEV_BUILD=OFF` write to a hardcoded world-readable `/tmp/jamwide.log` containing username, password length, and license callback identity. Local-only privilege issue (single-user macOS) but unsuitable for shipped packages and a privacy/forensics leak vector.
- Fix approach: Flip default to `OFF` in `CMakeLists.txt:30`; explicitly enable for local dev via `scripts/build.sh --dev`. Or replace `fopen("/tmp/jamwide.log")` with `juce::Logger::writeToLog` so it lands in the platform-appropriate log dir and respects log level.

### `test_encryption` build break (deferred since 15.1-02) [MED]

- Issue: `tests/test_encryption.cpp:321` calls `encrypt_payload_with_iv` which is gated by `#ifdef JAMWIDE_BUILD_TESTS` in `src/crypto/nj_crypto.h:36-43`. The njclient lib gets the define (`CMakeLists.txt:130-132`), but `add_executable(test_encryption ...)` at `CMakeLists.txt:350-356` does NOT pass `-DJAMWIDE_BUILD_TESTS=1` to its own translation unit, so the declaration is invisible at compile time.
- Files: `tests/test_encryption.cpp:321`, `src/crypto/nj_crypto.h:36-43`, `CMakeLists.txt:350-356`
- Impact: `./scripts/build.sh --tests` fails on the test_encryption target. Documented in `.planning/phases/15.1-rt-safety-hardening/deferred-items.md` since 15.1-02. Encryption code is shipped without the deterministic-known-vector test running in CI.
- Fix approach: Add `target_compile_definitions(test_encryption PRIVATE JAMWIDE_BUILD_TESTS=1)` after line 355.

### `test_flac_codec` roundtrip failures (3 of 8 sub-tests) [MED]

- Issue: FLAC roundtrip mono/stereo decode returns 0 samples; advance/spacing convention test fails. Documented in `.planning/phases/15.1-rt-safety-hardening/deferred-items.md` and STATE.md "Known Issues (v1.1 pre-release)".
- Files: `tests/test_flac_codec.cpp`, `wdl/flacencdec.h`
- Impact: FLAC encode path doesn't produce decodable output. Aligns with commit `af2a668` reframing FLAC as "in development" and STATE.md line 117-119 flagging "FLAC audio not yet working — needs debugging".
- Fix approach: Tracked under FLAC integration; see `FLAC_INTEGRATION_PLAN.md` (untracked, see "Untracked planning docs" below).

### Bot filter `isBot()` produces empty mixer slots [MED]

- Issue: `juce/ui/ChannelStripArea.cpp:10-17` filters known bot prefixes (`ninbot`, `jambot`, `ninjam`). Filter is applied at strip rebuild (`refreshFromUsers`, line 555) AND at VU update (line 403) so they stay in sync. However, the canonical user list at `processorRef.cachedUsers` still contains bots — only the visible strips are filtered.
- Files: `juce/ui/ChannelStripArea.cpp:10-17`, `juce/ui/ChannelStripArea.cpp:395-430`, `juce/ui/ChannelStripArea.cpp:540-565`
- Impact: User memory `project_ninbot_still_visible.md` flags ninbot_ still showing despite the filter. Verified the filter calls `name.startsWithIgnoreCase("ninbot")` — case-insensitive, prefix-match. Expected to catch `ninbot_<something>`. Possible cause: the user-reported sighting was on a strip layout snapshot that mismatches the filtered roster (e.g. `remoteSlotToUserIndex` mapping is computed once at strip rebuild but the mixer redraws stale slot count). Or some bot names start with `_ninbot` (underscore prefix would defeat `startsWithIgnoreCase("ninbot")`).
- Fix approach: Add a print-stmt or breakpoint at `juce/ui/ChannelStripArea.cpp:14` showing every `cleanName` evaluated; confirm the actual visible-bot username doesn't match any of the three prefixes. Likely fixes are (a) extend prefix list, (b) trim leading whitespace/punct in `stripAtSuffix`, or (c) add a "starts with _" → strip underscore preprocessing step before the prefix check.

### Linear scan in remote user lookup (`m_remoteusers` is unsorted list) [LOW]

- Issue: `src/core/njclient.cpp:4877`, `:4914` carry "todo: binary search" / "todo: binary search?" comments. Lookups by username via strcmp loop happen at lines 1720, 1953, 2108 — every PeerAddedUpdate / DOWNLOAD_INTERVAL_BEGIN / chat message.
- Files: `src/core/njclient.cpp:1720`, `:1953`, `:2108`, `:4877`, `:4914`
- Impact: O(N) lookup × O(N) peers per message = O(N²) message-handling cost on the run thread. Acceptable up to MAX_PEERS=64; would matter only at much larger jam sizes.
- Fix approach: Either sort `m_remoteusers` and binary-search, or maintain a side-map `WDL_StringKeyedArray<RemoteUser*>` keyed by username. Defer until Phase 15.2+ if peer count remains bounded by NINJAM protocol limit.

### Inert `// XXXXXXXXX` placeholder in filename builder [LOW]

- Issue: `src/core/njclient.cpp:2498` does `s.Append(".XXXXXXXXX")` then immediately overwrites the suffix with the actual codec extension at line 2502-2505. Reads as legacy mktemp-style placeholder.
- Files: `src/core/njclient.cpp:2496-2509`
- Impact: Cosmetic only. The `.XXXXXXXXX` is never written to disk.
- Fix approach: Rewrite as `s.Append("."); int oldl=s.GetLength();` then append the codec extension into the now-pre-grown buffer. Or just leave it — vendored from upstream NINJAM and works.

### Hardcoded `// hack` interval-length quantization comment [LOW]

- Issue: `src/core/njclient.cpp:1185` retains commented-out code `//m_interval_length-=m_interval_length%1152;//hack`. Vorbis-specific window-alignment workaround that no longer applies.
- Files: `src/core/njclient.cpp:1185`
- Impact: None functional; informational dead code.
- Fix approach: Delete the comment line.

### `m_locchan_cs` still acquired by run-thread `drainBroadcastBlocks` [LOW]

- Issue: `src/core/njclient.cpp:3637` does `WDL_MutexLock lock(&m_locchan_cs);` inside `drainBroadcastBlocks`. The audit (15.1-AUDIT-final.md CR-02) marked this as expected — run-thread acquisition NOT an audio-path violation since 15.1-06 closed audio-path use of the mutex.
- Files: `src/core/njclient.cpp:3637`
- Impact: None for RT-safety. Still a serialization bottleneck between run-thread mutators and the broadcast-block drain (every NJClient::Run tick).
- Fix approach: Long-term, replace canonical `m_locchannels` list with an SPSC-published mirror similar to `m_remoteuser_slot_table[MAX_PEERS]` (`src/core/njclient.h:763`). Out of scope for v1.2.

### Vestigial `JAMWIDE_BUILD_JUCE` toggle [LOW]

- Issue: `CMakeLists.txt:31` exposes `JAMWIDE_BUILD_JUCE` defaulting ON, gating the entire JUCE plugin build at line 142–335. Per user memory `project_legacy_clap_flag.md`, this was originally an architectural boundary between a removed pre-JUCE CLAP build (commit `0d82641`) and the current JUCE flow; today it's a fast-test convenience only — there is no non-JUCE plugin target.
- Files: `CMakeLists.txt:31`, `CMakeLists.txt:142-335`
- Impact: Misleading future maintainers — name implies an alternative non-JUCE path that no longer exists.
- Fix approach: Either remove the option entirely (always build JUCE) or rename to `JAMWIDE_FAST_TESTS_ONLY` so the intent is clear.

---

## Known Bugs

### `m_remoteusers` lobby-renormalize is latent Bug-A clone [MED]

- Symptoms: If a server protocol path ever drives `_reinit()` to NOT reset `m_max_localch=MAX_LOCAL_CHANNELS`, the auth-reply lobby→room handoff at `src/core/njclient.cpp:1648-1653` overwrites every `Local_Channel.channel_idx` to its list position. The audio-thread mirror is keyed BY `channel_idx` (`m_locchan_mirror[ch].active`), so every mirror entry would silently strand and remain visually present without audio.
- Files: `src/core/njclient.cpp:1648-1653`
- Trigger: Currently structurally unreachable because `_reinit()` resets `m_max_localch=MAX_LOCAL_CHANNELS` on every connect cycle (njclient.cpp:745) — `!m_max_localch` is always false on first auth reply.
- Workaround: None needed today; the hazard is "live latent" (deferred-items.md classification).
- Fix approach: Delete the renormalize block entirely (matches commit `3799e8a` `_reinit` fix). If lobby→room handoff is ever re-introduced, replace with `RemovedUpdate{old_idx}` + `AddedUpdate{new_idx}` per renumbered channel — same correct pattern that `SetLocalChannelInfo` explicit-add uses.

### TSan ctest "Not Run" for 5 JUCE-side tests [LOW]

- Symptoms: Under `--tsan` build, ctest reports `osc_loopback`, `midi_mapping`, `flac_codec`, `encryption`, `video_sync` as "Not Run" because JUCE's `juce_add_console_app` artefact path layout doesn't match what TSan's build dir expects.
- Files: `CMakeLists.txt:295-332`, `build-tsan/test_*_artefacts/`
- Trigger: `./scripts/build.sh --tsan && ctest`.
- Workaround: Run those 5 tests under the Release build (`build-test/`) — they're not RT-safety tests, so TSan isn't required.
- Fix approach: Investigate JUCE artefact path under JAMWIDE_TSAN=ON in a future infra task. Quick-task scope.

---

## Security Considerations

### AES-256-CBC + SHA-256 key derivation: no PBKDF2, password used directly [HIGH]

- Risk: `src/crypto/nj_crypto.cpp:234-258` (`derive_encryption_key`) is `SHA-256(password || 8-byte-server-challenge)`. Per D-03 design decision, no key-stretching (PBKDF2/Argon2). An attacker who captures the 8-byte challenge AND a single ciphertext can mount an offline brute-force attack on the password at native SHA-256 speed (~10 GH/s on a single GPU).
- Files: `src/crypto/nj_crypto.cpp:234-258`, `src/crypto/nj_crypto.h:53-56`
- Current mitigation: 8-byte challenge prevents rainbow tables; the protocol is GPL-NINJAM heritage; password strength is the user's responsibility.
- Recommendations: Document in user-facing docs that NINJAM passwords need to be HIGH-entropy random tokens (not human-memorable). Long-term: add PBKDF2-HMAC-SHA256 with a server-supplied iteration count (negotiated via existing capability flags `SERVER_CAP_ENCRYPT_SUPPORTED`).

### AES-CBC mode is not authenticated (no MAC, no AEAD) [HIGH]

- Risk: `src/core/netmsg.cpp:129-155` encrypts with `encrypt_payload` and decrypts with `decrypt_payload`. `nj_crypto.cpp` uses `EVP_aes_256_cbc()` with `BCRYPT_BLOCK_PADDING` / OpenSSL PKCS#7 padding. There is NO MAC/HMAC verification step — an attacker on the wire can flip ciphertext bits to corrupt plaintext after decryption (CBC malleability) and the receiver has no way to detect it. Padding-oracle exposure is partially mitigated by the comment at `src/core/netmsg.cpp:244-245` ("Generic error code — do NOT reveal padding details") which collapses ALL decrypt failures to error code -5.
- Files: `src/crypto/nj_crypto.cpp:153-232`, `src/core/netmsg.cpp:233-265`
- Current mitigation: Generic error code on decrypt failure (no padding-oracle), tear-down on first decrypt fail (`src/core/netmsg.cpp:243-247`).
- Recommendations: Migrate to AES-256-GCM (authenticated AEAD) — `EVP_aes_256_gcm()` on OpenSSL, `BCRYPT_CHAIN_MODE_GCM` on Windows. Wire format becomes `[12-byte nonce][ciphertext][16-byte tag]`. Existing wire framing tolerates the size delta (`NET_MESSAGE_MAX_SIZE_ENCRYPTED = NET_MESSAGE_MAX_SIZE + 32` — exactly the tag+nonce budget).

### Encryption key zeroization on `ClearEncryption` is best-effort [MED]

- Risk: `src/core/netmsg.h:139-142` does `memset(m_encryption_key, 0, 32)` on `ClearEncryption`. Compilers are allowed to elide `memset` when the buffer is "dead" after the call. The key is also briefly held on stack at `src/core/njclient.cpp:1582-1585` (`unsigned char enc_key[32]`) and zeroed via `memset(enc_key, 0, 32)` — same elision risk.
- Files: `src/core/netmsg.h:139-142`, `src/core/njclient.cpp:1582-1585`
- Current mitigation: `memset` calls present at every site.
- Recommendations: Use `OPENSSL_cleanse` (already linked) or the C11 `memset_s` shim. On Windows, use `SecureZeroMemory`. Wrap as `nj_crypto_secure_zero(void*, size_t)` to avoid divergence.

### IV uniqueness depends on platform RNG [MED]

- Risk: `src/crypto/nj_crypto.cpp:128-132` (Windows BCryptGenRandom) and `:135-139` (OpenSSL RAND_bytes) generate per-message random IVs. Both are correct sources, but a fork-after-RAND_bytes scenario or RNG-state corruption could cause IV reuse. CBC IV reuse breaks IND-CPA (identical plaintext blocks produce identical ciphertext, leaking equality).
- Files: `src/crypto/nj_crypto.cpp:124-142`
- Current mitigation: Standard platform RNGs; failure surface is `result.ok=false` (line 131-132, 137-138).
- Recommendations: GCM migration (above) makes nonce-uniqueness explicit (uses 12-byte nonce, can be a counter). For now, document the assumption that the host RNG is healthy; consider a self-test on first encrypt that two consecutive `encrypt_payload` calls produce different IVs.

### Plaintext in-flight for legacy/non-encrypting servers [MED]

- Risk: `src/core/netmsg.cpp:129` only encrypts when `m_encryption_active && sendm->get_size() > 0`. The encryption flag is set only after server advertises `SERVER_CAP_ENCRYPT_SUPPORTED` (`src/core/njclient.cpp:1581`). Connecting to a legacy NINJAM server proceeds unencrypted INCLUDING the `AUTH_USER` reply (which carries a SHA1(password+challenge) hash, not the password itself, but still leakable).
- Files: `src/core/netmsg.cpp:129`, `src/core/njclient.cpp:1581-1592`
- Current mitigation: SHA1 of password+challenge is the original NINJAM auth design — the wire never carries plaintext password.
- Recommendations: Add a UI warning when connecting to a server that doesn't advertise encryption ("This session is unencrypted — your audio and chat may be observable on the network"). Consider an opt-in setting "require encryption" that aborts the connection if the server doesn't support it.

### `fopen("/tmp/jamwide.log")` leaks credentials metadata [MED]

- Risk: `src/core/njclient.cpp:1533-1540, 1553-1558, 1569-1574, 1625-1629, 1639-1644` write `user='%s' pass_len=%d` and license details to a hardcoded world-readable `/tmp/jamwide.log` when `JAMWIDE_DEV_BUILD` is defined.
- Files: `src/core/njclient.cpp:1533-1644`
- Current mitigation: Gated by `JAMWIDE_DEV_BUILD` macro, but that defaults to ON (`CMakeLists.txt:30`).
- Recommendations: See "JAMWIDE_DEV_BUILD=ON is the default" tech-debt entry above. At minimum, never log `pass_len` — even just length is an entropy oracle.

### Zero-length plaintext IS encrypted (intended but unusual) [LOW]

- Risk: `src/crypto/nj_crypto.cpp:27-29` documents that `plaintext_len == 0` produces 32 bytes of output (16-byte IV + 16-byte PKCS#7 padding of empty block). Keepalives skip encryption entirely (`src/core/netmsg.cpp:127-128`).
- Files: `src/crypto/nj_crypto.cpp:27-29`, `src/core/netmsg.cpp:127-128`
- Current mitigation: Zero-length skip is explicit and tested.
- Recommendations: Verify with a targeted unit test that zero-length encrypt/decrypt round-trips (i.e. produces empty `data` with `ok=true`).

---

## Real-Time Safety Risks (Audio Path)

**Audio path entry points** (per `15.1-AUDIT-final.md`):
- `JamWideJuceProcessor::processBlock` (`juce/JamWideJuceProcessor.cpp:486`)
- `NJClient::AudioProc` (`src/core/njclient.cpp:1087`)
- `NJClient::process_samples` (`src/core/njclient.cpp:2562`)
- `NJClient::on_new_interval` (`src/core/njclient.cpp:4002`)
- `NJClient::mixInChannel` (`src/core/njclient.cpp:3718`)
- `DecodeState::runDecode` (`src/core/njclient.cpp:468`)

### Heap allocation in `processBlock` via `inputScratch.setSize` [HIGH]

See "Audio-thread `inputScratch` not preallocated" under Tech Debt above. This is the single open RT-safety regression identified in the current pass.

### `lcm.cbf(...)` runs arbitrary user-callback on audio thread [MED]

- Risk: `src/core/njclient.cpp:2614-2617` calls `lcm.cbf(src, len, lcm.cbf_inst)` from inside `process_samples`. `cbf` is set via `SetLocalChannelProcessor` (the Instatalk PTT mute lambda registered at `juce/NinjamRunThread.cpp:374` per H-03 disposition). Today the registered lambda is RT-safe (atomic load + branch + memset), but the API exposes a foot-gun for any future caller.
- Files: `src/core/njclient.cpp:2614-2617`, `juce/NinjamRunThread.cpp:374`
- Current mitigation: Documented in 15.1-AUDIT-final.md H-03. The mirror copies `cbf` BY VALUE into `LocalChannelMirror` so the callback pointer itself doesn't race.
- Recommendations: Add a doxygen warning to `NJClient::SetLocalChannelProcessor` declaration in `src/core/njclient.h` mandating "callback runs on the audio thread; MUST be RT-safe — no allocations, no locks, no syscalls". Track in v1.3 backlog.

### `DecodeState::runDecode` `fread` is structurally unreachable but path remains [LOW]

- Risk: `src/core/njclient.cpp:478` `fread(srcbuf, 1, sz, decode_fp)` — pre-15.1 reachable from audio thread, now structurally unreachable per 15.1-09 Codex HIGH-1 invariant: every audio-thread-visible `DecodeState` has `decode_fp == nullptr` because `inversionAttachSessionmodeReader` (`src/core/njclient.cpp:3386-3450`) nulls it BEFORE every `PeerNextDsUpdate` publish.
- Files: `src/core/njclient.cpp:478`, `src/core/njclient.cpp:3386-3450`
- Current mitigation: Locked by `tests/test_decode_state_arming.cpp` (5 cases including HIGH-1 invariant). Audit-final disposition: CLEARED IN STEADY STATE.
- Recommendations: Future cleanup could split `DecodeState` into `DecodeStateNetwork` (decode_buf only) and `DecodeStateFile` (decode_fp only) with a sealed compile-time barrier so the `if (decode_fp)` branch at line 476 cannot exist in audio-path translation units. Out of scope for v1.2.

### CBC encrypt/decrypt not on audio path but heap-allocates per message [LOW]

- Risk: `encrypt_payload` and `decrypt_payload` (`src/crypto/nj_crypto.cpp:33-122`, `:153-232`) allocate `result.data` (std::vector) per call. They run on the run thread (`src/core/netmsg.cpp:130, 236`), NOT on the audio thread.
- Files: `src/crypto/nj_crypto.cpp:33-122`, `:153-232`, `src/core/netmsg.cpp:129-155`, `:233-265`
- Current mitigation: Off the audio path; allocations are normal for run-thread message handling.
- Recommendations: None for v1.2. If GCM migration happens, reuse `EVP_CIPHER_CTX*` per `Net_Connection` instance to avoid the per-message Init/Free pair.

### `m_remoteuser_update_q` overflow → leaked `RemoteUser` [MED]

- Risk: 7 sites at `src/core/njclient.cpp:1878, 1889, 1901, 1912, 1922, 2042, 5111` log "WARNING: m_remoteuser_update_q full ..." and intentionally LEAK rather than block the audio thread. This is the documented RT-safety > memory-hygiene trade-off.
- Files: `src/core/njclient.cpp:1878-2042, 5111`
- Current mitigation: `m_remoteuser_update_overflows` counter exposed via getter; 15.1-10 Signal 1b verifies counter == 0 in normal operation. Queue is capacity-64 (MAX_PEERS).
- Recommendations: Wire the overflow counter into a UI status indicator (red dot). Long-running sessions with rapid peer-churn could silently leak peers, so a visible signal is valuable.

---

## Performance Bottlenecks

### `m_remoteusers` is a `WDL_PtrList` with linear lookup [LOW]

- Problem: O(N) username lookups at message-handler sites (covered above under "Linear scan in remote user lookup").
- Files: `src/core/njclient.cpp:1720, 1953, 2108`
- Cause: Vendored NINJAM data structure, not yet refactored.
- Improvement path: Either sort or add a side-map. Defer; bounded by MAX_PEERS=64.

### `drainBroadcastBlocks` acquires `m_locchan_cs` under per-record loop [LOW]

- Problem: `src/core/njclient.cpp:3637` takes `m_locchan_cs` per-record inside the SPSC drain lambda (line 3630-3678). The drain runs on every `NJClient::Run` tick, so the lock is taken `(records-this-tick)` times not once.
- Files: `src/core/njclient.cpp:3623-3679`
- Cause: Convenience — the canonical Local_Channel lookup happens inside the lambda body for clarity.
- Improvement path: Hoist the `WDL_MutexLock lock(&m_locchan_cs);` outside the `for (int ch = 0; ...)` loop and just look up `lc` once per channel. Trivial micro-optimization.

### Codec encoder allocation on first audio block [MED]

- Problem: `src/core/njclient.cpp:2253-2259` constructs a fresh Vorbis or FLAC encoder via `new` on the run thread (encoder thread, not audio thread) the first time encoded data flows. The audio path's pushBlockRecord is RT-safe, but the run-thread encoder Init has libogg/libFLAC internal allocations. Documented as CR-11 PARTIAL in 15.1-AUDIT-final.md — first 3-5 frames per stream after peer-join or codec reset retain libogg/libFLAC algorithmic-residual allocs.
- Files: `src/core/njclient.cpp:2253-2259`, `wdl/vorbisencdec.h`, `wdl/flacencdec.h`
- Cause: Codec libraries do their own internal allocation; out of WDL's Prealloc surface.
- Improvement path: Signal 7 (Instruments perf measurement) is the gate per audit-final. If the residual proves audible (xrun-correlated), the libs need patching or a warm-up pre-construct in `prepareToPlay`.

---

## Fragile Areas

### Slot-picking decision-relocation pattern [HIGH]

- Files: `src/core/njclient.cpp:3306-3320` (apply visitor for PeerNextDsUpdate)
- Why fragile: Three consecutive 15.1-07a stabilization fixes (commits `b9899a0`, `e827453`, `e151dd8`, builds 288/289/290) all stem from one architectural mistake in the original 15.1-07a: slot-picking (`useidx = !!next_ds[0]`) was placed on the publishing run thread, but the run thread cannot read the audio-thread mirror state. Final fix moved slot pick into the apply visitor (which runs on audio thread via `drainRemoteUserUpdates` from `AudioProc`). User memory `feedback_legacy_invariant_audit.md` updated with the new "Direction-C: decision-relocation drift" pattern.
- Safe modification: Any decision predicate that reads audio-thread-owned state MUST live on the audio thread. The publisher provides INPUT (the new ds pointer); the consumer provides DECISION (where to put it).
- Test coverage: `tests/test_remote_user_mirror.cpp` (8 cases) + `tests/test_peer_churn_simulation.cpp` (1000-pattern stress under TSan). Real-DAW UAT exposed all three regressions; mechanical signals had been GREEN for the wrong-thread version.

### Shadow-state writes outside migrated code paths [HIGH]

- Files: `src/core/njclient.cpp:1648-1653` (lobby renormalize — Bug-A live latent — see "Known Bugs" above)
- Why fragile: Per user memory `feedback_legacy_invariant_audit.md` and 15.1-AUDIT-final.md, the audit pattern is: when a refactor adds a "shadow representation" of state (here: `m_locchan_mirror[ch]` keyed by `channel_idx`), grep ALL writes to the indexing field, not just inside migrated functions. Phase 15.1-06 shipped with `_reinit` renormalize uncaught (later fixed in commit `3799e8a`); the 1648-1653 lobby-renormalize is the same shape, currently unreachable but not deleted.
- Safe modification: Before introducing any new mirror, run `grep -nE 'lc->channel_idx\s*=|chan->channel_idx\s*='` and verify every hit either (a) goes through the publish API or (b) is unreachable from the audio thread.
- Test coverage: None — both the 15.1-06 and 15.1-07b regressions were "mechanical signals GREEN, real-DAW UAT RED". Adding a `WDL_PtrList` mutation tracer in Debug builds would help.

### `RemoteUser_Channel::ds` is owned by audio thread but vivified by run thread [MED]

- Files: `src/core/njclient.cpp:528-540` (declaration), `:3306-3320` (apply visitor)
- Why fragile: The `chan_mirror.ds` and `chan_mirror.next_ds[2]` slots are audio-thread-owned in the mirror. Run-thread sets them via `PeerNextDsUpdate` publish. The pointer-shuffle ordering (capture-old-FIRST, advance, defer-delete-old) is documented at every shuffle site (`mixInChannel` sites 4/5; `on_new_interval` site 7) but a future contributor adding a sixth advance site could swap the order and silently UAF.
- Safe modification: Always use the `// 15.1-05 CR-06/07 (site N/7): ...` comment block as a checklist. The deferred-delete helper (`deferDecodeStateDelete`) exists specifically so callers don't write `delete old_ds;` directly.
- Test coverage: `tests/test_decode_state_arming.cpp` covers the publish→audio-thread-pickup roundtrip; `test_deferred_delete.cpp` covers the SPSC overflow path.

### `Local_Channel.cbf` callback contract is audio-thread-RT-safety-critical [MED]

- Files: `src/core/njclient.h` (Local_Channel + LocalChannelMirror), `src/core/njclient.cpp:2614-2617`, `juce/NinjamRunThread.cpp:374`
- Why fragile: H-03 disposition was "ACCEPTED — documented as deferred follow-up". Today's only registered callback is the Instatalk PTT mute lambda, which IS RT-safe. A future PTT, sidechain, or processor plugin could register a non-RT-safe callback and the current API doesn't reject it.
- Safe modification: New `SetLocalChannelProcessor` callers must self-audit for: no allocation (`new`/`malloc`), no locks, no I/O, no system calls. Document the contract at the API site.
- Test coverage: None. A unit test that registers a `[](float*,int,void*){ /*allowed*/ }` could compile-time-fail if someone writes `new int{}` inside, but only a Debug-build allocation tracer hooked to the audio thread (`mtrace`-style) would catch it at runtime.

### `inputScratch` not preallocated [HIGH]

See "Audio-thread `inputScratch` not preallocated" under Tech Debt — listed here to capture the fragility angle (asymmetry with `outputScratch`).

---

## Scaling Limits

### `MAX_PEERS = 64` slot table [LOW]

- Current capacity: `src/core/njclient.h:117-118` defines `MAX_PEERS` as 64.
- Limit: Server can advertise more than 64 connected users; the 65th peer's PeerAddedUpdate fails to allocate a slot and is silently dropped (`writeLog` at `src/core/njclient.cpp:1878`).
- Scaling path: NINJAM protocol supports more, but the wire format and audio-mix budget make 64 a practical limit. Increase to 128 if needed by changing the macro and rebuilding — `RemoteUserMirror m_remoteuser_mirror[MAX_PEERS]` and `RemoteUserSlotEntry m_remoteuser_slot_table[MAX_PEERS]` both auto-resize.

### `MAX_LOCAL_CHANNELS` (likely 8 or 16) [LOW]

- Current capacity: hoisted above `LocalChannelMirror` in `src/core/njclient.h` per 15.1-06 deviation note.
- Limit: User can request more local channels via `SetLocalChannelInfo(idx)`; out-of-range logs warning and drops the update.
- Scaling path: Bump the macro; mirror auto-resizes.

### `MAX_BLOCK_SAMPLES = 2048` [LOW]

- Current capacity: `src/threading/spsc_payloads.h:196` `inline constexpr int MAX_BLOCK_SAMPLES = 2048`.
- Limit: Hosts driving samplesPerBlock > 2048 are rejected at `prepareToPlay` via `SetMaxAudioBlockSize` throw + Debug `jassert(numSamples <= prevPreparedSize)` at processBlock + Release per-callsite bounds check in `pushBlockRecord` (drops with counter bump).
- Scaling path: Bump the constant; per-channel `BlockRecord.samples[]` array grows with it (memory cost: 2048 * 2 * 4 bytes = 16 KB per record × 16 ring slots × MAX_LOCAL_CHANNELS).

---

## Dependencies at Risk

### `IEMPluginSuite/` is an UNTRACKED git checkout, not a submodule [MED]

- Risk: `git status` shows `?? IEMPluginSuite/` (untracked). NOT in `.gitmodules`. NOT referenced in `CMakeLists.txt`. Last commit in the checkout is `976b2a9 2024-05-06 16:16:30 +0200 "Activated distance compensator."` (its own upstream history).
- Files: `IEMPluginSuite/` (top-level checkout directory)
- Impact: The directory provides reference patterns (`SetEncryptionKey` style, OSC plumbing) for the JamWide implementation but is NOT compiled in. Its presence in the working tree clutters search results, may be accidentally committed, and creates ambiguity — readers may assume it's vendored code.
- Migration plan: Either (a) move the checkout outside the repo (`~/dev/IEMPluginSuite/`) and reference it via README, (b) add to `.gitignore` if it should remain locally, or (c) make it a true submodule if it ever gets compiled in. Today, option (a) or (b).

### Vendored `wdl/` (Cockos WDL) [LOW]

- Risk: `wdl/jnetlib/`, `wdl/sha.cpp`, `wdl/rng.cpp` are vendored Cockos WDL sources. No upstream tracking; security fixes to the upstream WDL won't flow.
- Files: `wdl/`, `CMakeLists.txt:75-90`
- Impact: WDL is GPL like JamWide; dormant upstream means stable but no security advisories.
- Migration plan: Long-term, replace `JNL_Connection` with a JUCE-native socket layer (`juce::SocketStream` or `juce::URL::createInputStream`) and drop the WDL networking. Out of scope for v1.2.

### `libs/libflac` submodule [MED]

- Risk: Submodule pinned at `1507800de4b70e21be71f38caa0d9079d0bc6e45` (`1.3.1-1078-g1507800d`). FLAC integration in development per commit `af2a668` and `FLAC_INTEGRATION_PLAN.md` (untracked).
- Files: `libs/libflac`, `wdl/flacencdec.h`, `tests/test_flac_codec.cpp`
- Impact: 3 of 8 FLAC roundtrip tests failing (deferred-items.md). FLAC documented as "in development" — not user-facing in v1.2.
- Migration plan: Tracked under `FLAC_INTEGRATION_PLAN.md`. Decision pending whether v1.2 ships with FLAC stub-disabled or with the full encode/decode path stabilized.

---

## Missing Critical Features

### FLAC encode/decode end-to-end not yet working [HIGH]

- Problem: Per STATE.md "Known Issues (v1.1 pre-release)" and commit `af2a668`, FLAC was claimed as a feature but the codec roundtrip fails (`tests/test_flac_codec.cpp` 3/8). Currently shipping OGG/Vorbis only.
- Blocks: User-facing "lossless mode" advertised on the project landing page. Reframed in `af2a668` to honest "in development".
- Status: Plan in `FLAC_INTEGRATION_PLAN.md` (untracked) and `CODEC_REDESIGN_PLAN.md` (untracked).

### OSC control not yet working [MED]

- Problem: Per STATE.md line 117 "OSC control not yet working — needs debugging".
- Blocks: Phase 9–10 OSC-driven external control (Lemur, TouchOSC, hardware controllers).
- Status: Code present (`juce/osc/OscServer.cpp`, 1124 lines) but UAT-failing.

### MIDI Learn not working [MED]

- Problem: Per STATE.md line 119 "MIDI Learn not working — currently under investigation".
- Blocks: Phase 14 MIDI Learn UI promise.
- Status: Code present (`juce/midi/MidiMapper.cpp`, `juce/midi/MidiLearnManager.cpp`) but learn-then-trigger UAT failing.

### VDO.Ninja interop is research-stage [LOW]

- Problem: Per user memory `project_vdoninja_interop.md`: "Share join links + join external rooms, research done, needs design decisions."
- Blocks: Cross-tool video collab. Not v1.2 scope.
- Status: Companion is built (`juce/video/VideoCompanion.cpp`, 633 lines) and works for self-hosted rooms; external-room interop is open-ended.

---

## Test Coverage Gaps

### `inputScratch` allocation in processBlock [HIGH]

- What's not tested: No test asserts `prepareToPlay` pre-sizes `inputScratch` to the host-promised samplesPerBlock × maxInputChannels. The 15.1-08 prealloc-hardening tests cover `outputScratch` and `tmpblock` only.
- Files: `tests/test_decoder_prealloc.cpp` (covers the WDL prealloc path), `juce/JamWideJuceProcessor.cpp:151-189` (would need a JUCE-side test).
- Risk: First processBlock call after prepareToPlay allocates without warning.
- Priority: HIGH (it's the only open RT-safety regression in this audit).

### FLAC encode/decode roundtrip [HIGH]

- What's not tested: Working FLAC encode→decode roundtrip. 3/8 sub-tests in `tests/test_flac_codec.cpp` fail today.
- Files: `tests/test_flac_codec.cpp`
- Risk: Shipping FLAC-claim regressions (already de-claimed in `af2a668`).
- Priority: HIGH for FLAC re-introduction; MED today since FLAC is reframed in-development.

### Encryption known-vector test compile break [MED]

- What's not tested: `encrypt_payload_with_iv` against OpenSSL-CLI-generated golden vectors. `test_encryption.cpp:321` doesn't compile because `JAMWIDE_BUILD_TESTS` isn't passed to the test target's compile flags.
- Files: `tests/test_encryption.cpp:321`, `CMakeLists.txt:350-356`
- Risk: AES-256-CBC implementation could regress (e.g. wrong block mode, wrong padding) without CI catching it.
- Priority: MED — fix the build break before shipping v1.2.

### Live-attacker MAC test for AES-CBC malleability [LOW]

- What's not tested: Bit-flip a ciphertext byte and verify the receiver rejects (which it CAN'T today — no MAC). This is by design pending GCM migration.
- Files: `src/crypto/nj_crypto.cpp`, `src/core/netmsg.cpp`
- Risk: Active MITM corruption goes undetected.
- Priority: LOW once GCM migration is scheduled (v1.3).

### `m_locchan_processor_q` (Instatalk PTT) end-to-end [MED]

- What's not tested: SetLocalChannelProcessor with a non-trivial cbf, PTT toggle, audio-thread cbf invocation, then unregister. The 15.1-06 deviation #1 noted "AUDIT H-03 said zero callers — that was INCORRECT" (real caller at `juce/NinjamRunThread.cpp:374`).
- Files: `juce/NinjamRunThread.cpp:374`, `src/core/njclient.cpp:2614-2617, 4582-4597`
- Risk: PTT mute regressions (Phase 14.2 functionality) caught only in real-DAW UAT.
- Priority: MED — add a unit test that exercises the callback context lifecycle.

### Lobby-renormalize Bug-A latent path [LOW]

- What's not tested: Server-driven `_reinit()`-without-`m_max_localch=MAX_LOCAL_CHANNELS` simulation. The path is unreachable today, so a test would have to mock the server protocol.
- Files: `src/core/njclient.cpp:1648-1653`
- Risk: Live-latent; not exploitable today.
- Priority: LOW — track in deferred-items.md until the path is deleted.

---

## Untracked Planning Documents

Two repo-root planning docs exist as **untracked files** (not yet in git):

- `CODEC_REDESIGN_PLAN.md` (~150 lines) — Opus-as-default + FLAC + Vorbis-fallback architecture for low-latency online jamming. Contains a stray inline question "what about the clap version?" at line 32 (mid-document) that suggests the doc is mid-revision. Status: **DRAFT, not committed.** Future v1.3 milestone material.
- `FLAC_INTEGRATION_PLAN.md` (~350+ lines based on first-50-line preview) — Client-only FLAC integration via FOURCC-tagged interval upload, server is a "dumb pipe". Status: **DRAFT, not committed.** Linked to `tests/test_flac_codec.cpp` and the ongoing FLAC stabilization.

Recommendation: Either commit these into `.planning/research/` so they're preserved + reviewable, or move to a personal scratch directory if they're not yet shareable. Their presence in the repo-root working tree means a future `git add .` could accidentally commit them in an inconsistent state.

---

## Summary Counts

| Category | HIGH | MED | LOW |
|---|---|---|---|
| Tech Debt | 2 | 3 | 5 |
| Known Bugs | 0 | 1 | 1 |
| Security | 2 | 4 | 1 |
| RT-Safety | 1 | 2 | 2 |
| Performance | 0 | 1 | 2 |
| Fragile Areas | 2 | 2 | 0 |
| Scaling | 0 | 0 | 3 |
| Dependencies | 0 | 2 | 1 |
| Missing Features | 1 | 2 | 1 |
| Test Gaps | 2 | 2 | 1 |

**Top 5 to triage next:**

1. **`inputScratch` heap-alloc in processBlock** — HIGH RT-safety regression (mirror of 15.1-08 M-01/M-03 missed for inputs) — `juce/JamWideJuceProcessor.cpp:248`
2. **`JAMWIDE_DEV_BUILD=ON` default** — HIGH security (credentials length to `/tmp/jamwide.log`) — `CMakeLists.txt:30`
3. **Lobby-renormalize Bug-A latent** — MED known-bug (unreachable today, would silently kill mirror) — `src/core/njclient.cpp:1648-1653`
4. **`test_encryption` build break** — MED test gap (deferred since 15.1-02) — `CMakeLists.txt:350-356`
5. **AES-CBC has no MAC** — HIGH security (CBC malleability) — needs GCM migration plan for v1.3

---

*Concerns audit: 2026-04-30*
