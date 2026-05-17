# tests/fixtures/ — Phase 21-02 H.264 decoder test fixtures

Binary fixtures consumed by `tests/test_video_decoder.cpp` (the 8
COD-03 sub-tests). Regenerate via the
`tests/fixtures/gen-baseline-fixtures.cpp` generator (CMake target
`gen_baseline_fixtures`, marked `EXCLUDE_FROM_ALL`):

```
./scripts/build.sh gen_baseline_fixtures
./build-juce/gen_baseline_fixtures tests/fixtures
```

The generator uses the vendored libopenh264 encoder (libs/ffmpeg/macos-x86_64)
to encode a synthetic flat-color YUV420P frame at the requested
resolution + color, then splits the Annex-B stream into separate
SPS/PPS and IDR fixtures.

## Fixtures

### sps_pps_baseline_320x240.bin
- **Content:** Annex-B framed SPS + PPS (+ optional SEI) for H.264
  Baseline profile, 320×240, 10 fps, 100 kbps, IDR period 1.
- **Generator:** `encode_one_idr(320, 240, 128, 128, 128)` (gray)
  followed by `split_sps_pps_and_idr`. NAL types 7 (SPS) + 8 (PPS) +
  6 (SEI) concatenated.
- **Consumers:** all 8 sub-tests use the 320×240 SPS/PPS as the
  baseline parameter set.
- **SHA-256:** see `sha256.txt` in this directory (generated alongside).

### idr_baseline_320x240.bin
- **Content:** Annex-B framed IDR slice NAL (NAL type 5) for the
  matching 320×240 Baseline configuration.
- **Generator:** same `encode_one_idr` call as above; IDR NAL extracted
  by `split_sps_pps_and_idr`.
- **Consumers:** sub-tests 1, 2, 3, 4, 6, 7.

### sps_pps_baseline_640x480.bin
- **Content:** Annex-B framed SPS + PPS for H.264 Baseline 640×480.
- **Consumers:** sub-test 4 (`test_source_resolution_change_no_crash`).

### idr_baseline_640x480.bin
- **Content:** Annex-B framed IDR slice for 640×480.
- **Consumers:** sub-test 4.

### idr_baseline_320x240_red.bin
- **Content:** Annex-B framed IDR slice for 320×240 with the Y/U/V
  planes filled to BT.601 red (`Y≈82, U≈90, V≈240`). After
  `sws_scale YUV420P → BGRA` the center pixel decodes to a red BGRA
  value (R high, G/B low).
- **Generator:** `encode_one_idr(320, 240, 82, 90, 240)`.
- **Consumers:** sub-test 8 (`test_back_to_back_push_preserves_order`).
  This fixture pairs with the green variant — the test pushes both
  back-to-back without polling and asserts the FIRST decoded center
  pixel is red, proving the 4-slot snapshot ring preserves push order.

### idr_baseline_320x240_green.bin
- **Content:** Annex-B framed IDR slice for 320×240 with Y/U/V filled
  to BT.601 green (`Y≈145, U≈54, V≈34`).
- **Generator:** `encode_one_idr(320, 240, 145, 54, 34)`.
- **Consumers:** sub-test 8 (paired with red above).

### marker_payload_outer20.bin
- **Content:** 24 bytes — the EXACT on-wire layout the
  `handleVideoRecvWrite_` accumulator reads off a NinjamZap-shape
  H.264 marker frame:
  ```
  [4B BE outer_len = 0x00000014 = 20]
  [4B BE sender_seq = 0xDEAD0042]
  [16B audio_guid  = 0xAA × 16]
  ```
  The outer 4-byte prefix value MUST equal 20 — this byte fixture
  is the Phase 20 commit 6d23b5c regression guard. The payload
  (bytes 4..23) is what the snapshot's `bytes` buffer stores;
  `handleVideoRecvWrite_`'s accumulator consumes the outer 4B prefix
  before storing.
- **Generator:** `build_marker_payload_outer20()` — pure constant
  byte layout, no codec calls.
- **Consumers:** sub-tests 6, 7, 8 (all use the 20-byte payload
  portion as frame 0 of a full NinjamZap-shape slot snapshot).
- **Source-truth reference:**
  `ninjamzap-core/tests/video-sync/harness/TestClient.cpp:120-176`
  (`sendVideoFrame` builds the outer 4B BE length prefix;
  `sendFakeSPSPPS` builds the SPS/PPS inner wire format).

## Provenance + tampering detection

SHA-256 of each fixture (recorded at Plan 21-02 Task 3 commit time):

```
28878a407d534ca140c275f73815d85c1ffd5a7c99929912248669104b223950  idr_baseline_320x240.bin
09f4dcd99c7e32639d35afa403909934aa78f58419b12a5c4a66bf72585860a3  idr_baseline_320x240_green.bin
c292b257508ae1f38694ae529ca5786c4cb581ae181205ba19b2734d9b201f5c  idr_baseline_320x240_red.bin
0ba1c43bbb3a563f0e7b181ad36f3e0f345609660f0e3e743f4ecfeb968fa594  idr_baseline_640x480.bin
f04000d6359ac6d7ee169b5a8bf5a9ceafb9e4ea4e421ce790efab0e4ea09b76  marker_payload_outer20.bin
4e5be6890cd51ca986c13c9289bf754c4ed90298170101f19a290867e34cf049  sps_pps_baseline_320x240.bin
22e1ef56dc7931d7629477f2d2c33779f86eee376b85060d074035e22ad33f15  sps_pps_baseline_640x480.bin
```

To verify a fixture is unmodified:

```
shasum -a 256 tests/fixtures/*.bin
```

If any fixture's content drifts (e.g., after a libopenh264 / ffmpeg
vendored-library upgrade), regenerate from source via the generator
and update both the fixture bytes AND the SHA-256 list above:

```
./scripts/build.sh gen_baseline_fixtures
./build-juce/gen_baseline_fixtures tests/fixtures
shasum -a 256 tests/fixtures/*.bin >> /tmp/fixture-hashes.txt
# Then edit this README to match.
```

The generator output is deterministic only IF the ffmpeg/openh264
library bytes are identical (vendored versions in `libs/ffmpeg/`).
Drift across ffmpeg versions IS expected — that is what the
regeneration step covers.

## Why these fixtures live in the repo

- The libopenh264 encoder takes ~200-500 ms to construct + encode one
  IDR frame; generating fixtures at test-runtime would multiply the
  test wall-clock by 6× (one encoder per sub-test).
- Pre-built .bin fixtures make the tests deterministic across
  developer machines (avoids "works on my libopenh264 build" drift).
- Total fixture size is ~30 KB — well within reasonable git repo
  overhead. The largest single fixture is `sps_pps_baseline_640x480.bin`
  (~50 B) + `idr_baseline_640x480.bin` (~3-4 KB).
