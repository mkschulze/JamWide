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
