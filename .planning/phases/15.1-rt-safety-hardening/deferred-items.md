# Phase 15.1 — Deferred Items (out-of-scope discoveries during execution)

## test_encryption.cpp pre-existing build break

**Discovered during:** 15.1-02 execution, attempting to run `./scripts/build.sh --tests`.
**Symptom:** `tests/test_encryption.cpp:321:28: error: use of undeclared identifier 'encrypt_payload_with_iv'`
**Status:** PRE-EXISTING on HEAD prior to 15.1-02 (no diff produced by this plan; last touched in commit `8f1250b` from Phase 15).
**Scope decision:** Out of scope for 15.1-02 per executor scope-boundary rule. The error is in `tests/test_encryption.cpp`, which is unrelated to the atomic-promotion of `m_misc_cs` fields. The `njclient` library and all other test targets (`test_video_sync`, `test_flac_codec`, `test_osc_loopback`, `test_midi_mapping`, `JamWideJuce`) all build cleanly with the atomic-promotion changes.
**Recommendation:** Track as a separate quick-task or fold into 15.1-10 verification cleanup. The test likely needs `nj_crypto::` namespace qualifier added to the symbol or restoration of an exposed deterministic-IV helper.

## test_flac_codec roundtrip failures (pre-existing)

**Discovered during:** 15.1-02 execution, running `ctest`.
**Symptom:** `flac_codec` test target fails 3 of 8 sub-tests:
  - "Roundtrip mono: encode/decode preserves audio within 16-bit tolerance" → FAILED: Decoded 0 samples, expected >= 4096
  - "Roundtrip stereo: encode/decode preserves audio within 16-bit tolerance" → FAILED: Decoded 0 samples, expected >= 8192
  - "FlacEncoder advance/spacing matches VorbisEncoder calling convention" → FAILED
**Status:** PRE-EXISTING. test_flac_codec.cpp last touched in commit `9b020e2` (Phase 1 RED test). STATE.md "Known Issues (v1.1 pre-release)" already lists "FLAC audio not yet working — needs debugging".
**Scope decision:** Out of scope for 15.1-02. The atomic-promotion of `m_misc_cs` fields cannot affect FLAC codec encode/decode roundtrip behavior. Tests that DO exercise areas adjacent to the atomic-promotion changes (`midi_mapping`, `osc_loopback`, `video_sync`) all PASS.
**Recommendation:** Fold into the existing tracked `Known Issues` debugging effort.

## Architecture-mirror audit dead-but-live finding (`njclient.cpp:1648-1653`)

**Discovered during:** 15.1-MIRROR-AUDIT.md (pre-15.1-07a sweep), re-verified in 15.1-10
phase-verification audit.
**Symptom:** auth-reply lobby renormalize at lines 1648-1653 (originally cited as line 1405 in the
mirror-audit; line numbers shifted with subsequent edits) silently overwrites
`Local_Channel.channel_idx` on canonical Local_Channels:

```cpp
if (!m_max_localch)
{
  // went from lobby to room, normalize channel indices
  for (int x = 0; x < m_locchannels.GetSize(); x ++)
    m_locchannels.Get(x)->channel_idx = x;
}
```

**Status:** **VERIFIED-UNREACHABLE in modern JamWide.** Same Bug-A shape as the bug fixed in
`_reinit` by commit `3799e8a` (15.1-07b stabilization). Without protection, if this path ever fired,
the audio-thread mirror entries indexed BY `channel_idx` would be silently stranded — exactly the
class of bug that took 4 UAT cycles to find in 15.1-07b.

**Why unreachable today:** `_reinit()` resets `m_max_localch = MAX_LOCAL_CHANNELS` (njclient.cpp:745)
on every connect cycle, so the `!m_max_localch` gate at line 1648 is always false on a normal first
auth reply. The path activates only on a server-side lobby→room handoff that no JamWide deployment
triggers, because the modern JamWide UI requires the user to specify a room before Connect.

**Scope decision (15.1-10 plan executor):** Per plan executor prompt rule "this plan does NOT
modify production code" + the architecture-mirror audit's accept-option-(b) recommendation, this
finding is documented here as deferred technical debt rather than fixed surgically.

**Recommendation:** When ready to clean up (any future quick-task or plan touching this region),
delete the renormalize block entirely (matches the `3799e8a` `_reinit` fix). If lobby→room handoff
is ever re-introduced, replace with `RemovedUpdate{old_idx} + AddedUpdate{new_idx}` per renumbered
channel (the same correct pattern that `SetLocalChannelInfo`'s explicit-add path uses).

**Risk:** Live latent. If a future server protocol or feature ever introduces lobby→room
transitions, this would silently corrupt the audio-thread mirror exactly like Bug A. Same shape,
same blast radius (dead VU on affected channels).

## TSan ctest "Not Run" build-config issue for JUCE-app tests

**Discovered during:** 15.1-10 Signal 1a TSan ctest run.
**Symptom:** 5 tests under TSan ctest report "Not Run" — `osc_loopback`, `midi_mapping`,
`flac_codec`, `encryption`, `video_sync`. The error message reads:

```
Could not find executable /Users/cell/dev/JamWide/build-tsan/test_*_artefacts/Debug/test_*
Looked in the following places: ... (10 candidate paths) ...
Unable to find executable
```

**Status:** PRE-EXISTING TSan-build-config interaction. The 5 affected tests are JUCE-side tests
(registered via `juce_add_console_app` rather than plain `add_executable`). The artefact-output
directory layout differs between standard build-test/ and the TSan build-tsan/ — JUCE looks for
the executable in `*_artefacts/Debug/test_*` but TSan's output layout doesn't match.

**Scope decision:** Out of scope for 15.1-10. None of the 5 affected tests exercise the audio-path
RT-safety surface that 15.1 hardens (osc_loopback / midi_mapping / video_sync are UI / OSC paths;
flac_codec / encryption are codec / crypto paths). The 9 SPSC RT-safety tests
(`njclient_atomics`, `spsc_state_updates`, `deferred_delete`, `local_channel_mirror`,
`block_queue_spsc`, `remote_user_mirror`, `decode_media_buffer_spsc`, `decode_state_arming`,
`decoder_prealloc`, plus the new `peer_churn_simulation` from 15.1-10) all build and run cleanly
under TSan with zero ThreadSanitizer reports.

**Recommendation:** Investigate the JUCE artefact-path mismatch under JAMWIDE_TSAN=ON in a future
infrastructure plan (out of phase 15.1 scope; could be a quick-task).

