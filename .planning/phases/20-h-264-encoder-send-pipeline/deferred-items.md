# Phase 20 — Deferred Items

Issues discovered during Phase 20 plan execution that are OUT OF SCOPE for the
current plan and need their own follow-up.

---

## 2026-05-16 — Plan 20-00 execution discovery

**`tests/test_encryption.cpp:321` references undeclared `encrypt_payload_with_iv`**

- **Discovered by:** Plan 20-00 Task 2 — full-build regression run after the
  njclient substrate revision and the test_rawdata_send.cpp rewrite.
- **Symptom:** `ctest --output-on-failure` cannot complete because the
  `test_encryption` target fails to compile.
- **Reproduction:** Build at the spawn-time base commit
  `152007fc427394495866a8b3059ca1a22f9ad8e3` (BEFORE any Plan 20-00 changes).
  The same error appears, confirming this is pre-existing, unrelated to
  Plan 20-00.
- **Scope:** Phase 15 encryption module — the helper `encrypt_payload_with_iv`
  was renamed or removed in a prior commit but `tests/test_encryption.cpp:321`
  was not updated. Plan 20-00 has no business touching the encryption test;
  per scope-boundary rules, Plan 20-00 does not fix this.
- **Action:** Surface this as a Phase 15 or Phase 23 hardening item; a quick
  follow-up commit can rename the call to whatever the current helper name
  is. Plan 20-00's `ctest -R rawdata_send` gate is green (8/8 sub-tests pass);
  the regression-check gate ("ctest --output-on-failure exits 0") is recorded
  as a known-broken-baseline, not a Plan 20-00 deviation.

---

## 2026-05-16 — Plan 20-00 execution discovery (second)

**`test_flac_codec` roundtrip tests fail at baseline**

- **Discovered by:** same regression run as the encryption issue above.
- **Symptom:** 3 of 8 sub-tests fail with "Decoded 0 samples, expected >= N":
    - `Roundtrip mono: encode/decode preserves audio within 16-bit tolerance`
    - `Roundtrip stereo: encode/decode preserves audio within 16-bit tolerance`
    - `FlacEncoder advance/spacing matches VorbisEncoder calling convention`
- **Reproduction:** the failure is in the FLAC codec round-trip path; Plan
  20-00 does not touch FLAC encoder/decoder code at all. The 12-target
  regression-relevant subset (`rawdata_send`, `video_fourcc`, `video_sync`,
  `block_queue_spsc`, `spsc_state_updates`, `njclient_atomics`,
  `remote_user_mirror`, `local_channel_mirror`, `deferred_delete`,
  `peer_churn_simulation`, `decode_media_buffer_spsc`, `decode_state_arming`)
  is 12/12 green after Plan 20-00, so the substrate revision did NOT cause
  this regression.
- **Scope:** Phase 14 / 14.3 FLAC integration follow-up. Plan 20-00 does not
  touch FLAC paths.
- **Action:** Surface as a Phase 23 or beta hardening item. Plan 20-00's
  acceptance gates (`ctest -R rawdata_send`, the 12-target regression
  subset) are all green.
