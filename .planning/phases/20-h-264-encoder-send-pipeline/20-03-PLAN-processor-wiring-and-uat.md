---
phase: 20
plan: 03
slug: processor-wiring-and-uat
type: execute
wave: 3
depends_on:
  - 20-00
  - 20-01
  - 20-02
files_modified:
  - juce/JamWideJuceProcessor.h
  - juce/JamWideJuceProcessor.cpp
  - juce/ui/ConnectionBar.h
  - juce/ui/ConnectionBar.cpp
  - juce/NinjamRunThread.cpp
  - tests/test_processor_video_lifecycle.cpp
  - tests/uat/phase-20-broadcast-uat.sh
  - CMakeLists.txt
  - CHANGELOG.md
autonomous: false
requirements:
  - WIRE-03
threat_refs:
  - T-20-03
  - T-20-SC
  - T-20-OBS-UAT
review_refs:
  - R3-MF4-queue-observability

must_haves:
  truths:
    - "JamWideJuceProcessor owns the Openh264Encoder via std::unique_ptr<jamwide::VideoEncoder>; the encoder instance is constructed when the JamWideCameraDevice transitions to Capturing (Phase 19 state) and is destroyed when the camera goes Idle; the encoder thread starts only on Broadcast=on per CONTEXT.md D-13"
    - "ConnectionBar gains a Broadcast toggle that calls JamWideJuceProcessor::setBroadcastVideo(bool); the button is a secondary state on the existing Camera button (right-click menu adds a 'Start/Stop Broadcast' entry) to keep the right-cluster width budget within the 1200 px kBaseWidth set by 5250ff1 — adding a separate top-level button would push the right cluster over the breathing-room margin"
    - "JamWideJuceProcessor::setBroadcastVideo(true) opens the encoder with the current camera preset's VideoEncoderConfig, attaches publishSpsPps→NJClient::SetVideoSPSPPS and publishEncodedNal→NJClient::QueueVideoFrame callbacks, passes NJClient::getAudioIntervalSeqPtr() as the IDR-sync atomic, and calls client->SetVideoBroadcastActive(true); setBroadcastVideo(false) calls client->SetVideoBroadcastActive(false) FIRST (so the next on_new_interval emits END at deactivate per Plan 20-02) THEN closes the encoder (so any in-flight NAL publish has somewhere to land); ordering matters per the layered review pattern from memory feedback_phase19_review_layers"
    - "NinjamRunThread connect-up callback (currently lines 322-389) gains BOTH a SetLocalChannelInfo(1, \"video\", ..., flags=0x10) call AND a SetVideoChannel(1, MAKE_NJ_FOURCC('H','2','6','4')) call AFTER the existing 4 SetLocalChannelInfo calls + Instatalk channel 4 wiring AND BEFORE NotifyServerOfChannelChange — per D-18 BOTH calls are required (SetLocalChannelInfo announces the channel name/flags so receivers identify the video capability, SetVideoChannel announces the fourCC); channel registration is unconditional at connect-up regardless of broadcast state per D-18; bandwidth cost is zero when not broadcasting (no payload sent)"
    - "NinjamRunThread connect-up calls SetLocalChannelInfo(1, 'video', ..., flags=0x10) + SetVideoChannel(1, H264) + NotifyServerOfChannelChange unconditionally per D-18 — video channel metadata (name='video', flags=0x10) and fourCC (H264) are both announced from the moment of connect regardless of camera/broadcast state, so other clients see 'user has video capability' with the proper channel name from connect-time onward"
    - "Plan 20-00's queue-observability atomics (m_rawdata_sendq_high_water_mark + m_rawdata_cs_contention_count + m_rawdata_sendq_total_enqueues) are exposed via accessors that JamWideJuceProcessor surfaces to the UAT harness; the UAT script reads these via a JAMWIDE_BUILD_TESTS-gated debug log channel OR a lldb breakpoint readout (planner picks based on what's most reliable for 5-minute populated runs); all three counters are owned by Plan 20-00 unconditionally — Plan 20-03 only reads them"
    - "tests/uat/phase-20-broadcast-uat.sh implements the 5-minute 2-peer broadcast UAT at each of 3 presets (Low/Medium/High) against `video.ninjamzap.com:2049`; observable acceptance thresholds per R3 MF4: m_rawdata_sendq_high_water_mark < 32 items AND m_rawdata_cs_contention_count / m_rawdata_sendq_total_enqueues < 1%, both at every preset; m_encoder_input_drops == 0 at every preset (D-07); zero audio glitches subjectively (UAT script captures human-observer checklist); ninjamzap-server VideoCongestionThreshold counter readable from server logs if accessible (best-effort, not a hard gate since we don't operate the server)"
    - "TSan dual-scope verification per Phase 15.1 D-07 runs on the broadcast happy path: --tsan build of JamWide standalone + a 5-minute local broadcast against the public server (or a local ninjamzap-server-docker if the public server is too lossy under TSan); zero TSan-reported races on m_video_cs / m_video_spspps_cs / m_rawdata_cs / m_curwritefile_guid_seq counters"
    - "Audio-thread budget measurement: wrap on_new_interval's video block in mach_absolute_time() (macOS) / QueryPerformanceCounter (Windows) probes; sample worst-case duration over a 5-minute 2-peer populated broadcast at HD (High preset); target ≤ 200 µs worst-case per call (NinjamZap-literal budget per CONTEXT.md Deferred Ideas + Assumption A5); if exceeded by ≥3× → escalate as substrate-tuning subplan per CONTEXT.md (NOT a sync-architecture change per D-19 rationale + feedback_proven_over_pure)"
    - "tests/test_processor_video_lifecycle.cpp validates the lifecycle wiring in pure-C++ without a NINJAM session: camera-open → encoder construction; broadcast-on → encoder.open() + client.SetVideoBroadcastActive(true) wired; broadcast-off → SetVideoBroadcastActive(false) before encoder.close(); camera-off → encoder destruction; nothing should crash, leak, or assert"
    - "CHANGELOG.md gets a v1.3 Phase 20 entry per Phase 19 D-26 pattern: '## Unreleased — v1.3 alpha (Phase 20) — H.264 native video broadcast' with bullets covering encoder bring-up + Broadcast button + channel 1 registration + UAT acceptance thresholds + known limitations (cold-start option (b) marker-only first interval)"
  artifacts:
    - path: "juce/JamWideJuceProcessor.h"
      provides: "std::unique_ptr<jamwide::VideoEncoder> videoEncoder member; bool broadcastVideoEnabled state; setBroadcastVideo(bool) public API + getter; encoder lifecycle hooks driven from the existing camera-state callback in Phase 19"
      contains: "videoEncoder"
    - path: "juce/JamWideJuceProcessor.cpp"
      provides: "Lifecycle wiring: encoder construction on camera-open; open/close + NJClient API wiring on Broadcast toggle; encoder destruction on camera-off; VideoEncoderListener::onEncoderFatalError handler that logs via juce::Logger::writeToLog and triggers a reconfigure attempt"
      min_lines: 100
    - path: "juce/ui/ConnectionBar.h"
      provides: "Broadcast toggle declaration (planner picks: secondary state on Camera button right-click menu vs separate button); cameraIsBroadcasting_ tracking; onBroadcastToggleRequested callback"
    - path: "juce/ui/ConnectionBar.cpp"
      provides: "Broadcast UI wiring; popup-menu item that calls onBroadcastToggleRequested; live-tracks broadcast state via setCameraIsBroadcasting(bool)"
    - path: "juce/NinjamRunThread.cpp"
      provides: "SetLocalChannelInfo(1, \"video\", ..., flags=0x10) + SetVideoChannel(1, H264) + NotifyServerOfChannelChange call wired into the existing connect-up block at lines 322-389 per D-18; placement: AFTER all existing audio SetLocalChannelInfo calls and Instatalk channel registration, BEFORE NotifyServerOfChannelChange; BOTH calls are required per D-18 (name/flags announcement + fourCC announcement)"
      contains: "SetVideoChannel"
    - path: "tests/test_processor_video_lifecycle.cpp"
      provides: "Lifecycle wiring validation without NINJAM session"
      min_lines: 100
    - path: "tests/uat/phase-20-broadcast-uat.sh"
      provides: "5-min 2-peer broadcast UAT harness; per-preset thresholds per R3 MF4; TSan dual-scope on the broadcast happy path; audio-thread budget probe"
      min_lines: 200
    - path: "CMakeLists.txt"
      provides: "test_processor_video_lifecycle wiring; ensure the broadcast UAT shell script is executable + listed in docs as part of the Phase 20 acceptance suite"
      contains: "test_processor_video_lifecycle"
    - path: "CHANGELOG.md"
      provides: "v1.3 Phase 20 entry"
      contains: "Phase 20"
  key_links:
    - from: "JamWideJuceProcessor camera-state callback (Phase 19 ownership)"
      to: "videoEncoder lifecycle"
      via: "Capturing → construct Openh264Encoder; Idle/Failed/Unavailable → destroy encoder"
      pattern: "videoEncoder = std::make_unique<jamwide::Openh264Encoder>"
    - from: "ConnectionBar onBroadcastToggleRequested"
      to: "JamWideJuceProcessor::setBroadcastVideo"
      via: "std::function callback wired in the JamWideJuceEditor (per existing CameraButton / cameraIsActive_ pattern)"
      pattern: "setBroadcastVideo"
    - from: "JamWideJuceProcessor::setBroadcastVideo(true)"
      to: "Openh264Encoder::open + NJClient::SetVideoBroadcastActive(true)"
      via: "open() with publishSpsPps→client.SetVideoSPSPPS, publishEncodedNal→client.QueueVideoFrame, audioIntervalSeq=client.getAudioIntervalSeqPtr()"
      pattern: "videoEncoder->open"
    - from: "NinjamRunThread connect-up (line ~389 area)"
      to: "NJClient::SetLocalChannelInfo(1, 'video', ..., flags=0x10) + NJClient::SetVideoChannel(1, H264) + NotifyServerOfChannelChange (unconditional per D-18)"
      via: "called once at NJC_STATUS_OK + non-prelisten branch AFTER existing audio SetLocalChannelInfo block AND Instatalk wiring AND BEFORE NotifyServerOfChannelChange; BOTH SetLocalChannelInfo and SetVideoChannel are required per D-18 — name/flags announcement makes receivers identify the channel by name, fourCC announcement makes receivers identify the codec"
      pattern: "SetLocalChannelInfo\\s*\\(\\s*1\\s*,\\s*\"video\"|SetVideoChannel\\s*\\(\\s*1\\s*,"
    - from: "Plan 20-00 m_rawdata_sendq_high_water_mark + m_rawdata_cs_contention_count + m_rawdata_sendq_total_enqueues atomics"
      to: "Plan 20-03 UAT acceptance thresholds (R3 MF4)"
      via: "JAMWIDE_BUILD_TESTS-gated debug log channel emits the atomics every 30s during broadcast; UAT script parses the log + asserts the thresholds; contention ratio denominator is m_rawdata_sendq_total_enqueues owned by Plan 20-00"
      pattern: "GetRawDataSendQueueHighWaterMark|GetRawDataMutexContentionCount|GetRawDataSendQueueTotalEnqueueCount"
---

<objective>
Plan 20-03 wires Plan 20-01's `Openh264Encoder` and Plan 20-02's `NJClient` video state machine into the `JamWideJuceProcessor` + `ConnectionBar` UX shell, adds the connect-up channel registration in `NinjamRunThread` per D-18, exposes Plan 20-00's queue-observability atomics to the UAT harness, and runs the 5-minute 2-peer populated-server UAT at each of 3 presets against `video.ninjamzap.com:2049` per R3 must-fix item 4. The acceptance thresholds for queue observability (high-water < 32 items, contention < 1% of enqueues at each preset), `m_encoder_input_drops == 0`, TSan dual-scope clean, and audio-thread budget ≤ 200 µs worst-case under HD populated load are concrete and locked here (NOT vague "if needed" wording per R3 must-fix item 4).

Purpose: this is the user-visible end of Phase 20. The lifecycle ordering matters per `feedback_phase19_review_layers` — broadcast-off must call `SetVideoBroadcastActive(false)` BEFORE closing the encoder so the next `on_new_interval` emits END at deactivate while the encoder is still alive to receive any in-flight NAL publishes safely. Channel registration at connect-up (D-18) is unconditional regardless of broadcast state and requires BOTH `SetLocalChannelInfo(1, "video", ..., flags=0x10)` (announces the channel name + flags so receivers identify the video capability with proper metadata) AND `SetVideoChannel(1, MAKE_NJ_FOURCC('H','2','6','4'))` (announces the fourCC). The UAT discipline per `feedback_uat_scope_redflags` requires verifying the user-visible happy path end-to-end (2 peers connect, broadcast video at each preset, 5-minute session, audio glitch-free, drop counters zero, TSan clean) — not a stripped-down "verify only X, skip Y" cell.

This plan has at least one checkpoint (the 5-minute UAT requires human-in-the-loop interaction with two JamWide instances + the live server), so `autonomous: false`.

Output: A buildable JamWide standalone + AU/VST3 plugin where the user can: open camera (Phase 19 path), toggle Broadcast on (this plan's UX), see encoded video frames hit the NINJAM channel 1 wire (Plan 20-02's wire format), have a second peer's JamWide successfully accumulate the chunks (using Phase 14.3-03's receive-side dispatch; full per-peer decode and tile rendering land in Phase 21 + 22 — Phase 20 UAT verifies the SEND path is bit-for-bit NinjamZap-compatible by capturing wire traces). Full Phase 20 acceptance gate per CONTEXT.md `<specifics>` audio glitch test signature + R3 MF4 thresholds.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/REQUIREMENTS.md
@.planning/phases/20-h-264-encoder-send-pipeline/20-CONTEXT.md
@.planning/phases/20-h-264-encoder-send-pipeline/20-VALIDATION.md
@.planning/phases/20-h-264-encoder-send-pipeline/20-REVIEWS.md
@.planning/phases/20-h-264-encoder-send-pipeline/20-00-PLAN-substrate-revision.md
@.planning/phases/20-h-264-encoder-send-pipeline/20-01-PLAN-video-encoder.md
@.planning/phases/20-h-264-encoder-send-pipeline/20-02-PLAN-video-state-machine.md
@.planning/phases/19-camera-capture-permission-ux/19-01-capture-pipeline-PLAN.md
@.planning/phases/19-camera-capture-permission-ux/19-02-ui-and-persistence-PLAN.md
@juce/JamWideJuceProcessor.h
@juce/JamWideJuceProcessor.cpp
@juce/ui/ConnectionBar.h
@juce/ui/ConnectionBar.cpp
@juce/NinjamRunThread.cpp

<interfaces>
<!-- Key contracts the executor needs. Extracted from JamWide as-shipped + the upstream Plans 20-01 and 20-02 surfaces. -->

From juce/JamWideJuceProcessor.h (existing, line ~125-128 area):
  std::unique_ptr<jamwide::JamWideFrameDistributor> frameDistributor;
  std::unique_ptr<jamwide::JamWideCameraDevice>     nativeCamera_;
  // Phase 20 additions in this plan:
  std::unique_ptr<jamwide::VideoEncoder>            videoEncoder;
  std::atomic<bool>                                 broadcastVideoEnabled{false};
  void setBroadcastVideo(bool enabled);             // message-thread API; called by ConnectionBar
  bool isBroadcastingVideo() const noexcept { return broadcastVideoEnabled.load(std::memory_order_acquire); }

From juce/JamWideJuceProcessor.cpp existing camera-state callback path (Phase 19, see 19-01-capture-pipeline-PLAN.md key_links):
  // The processor receives camera state transitions via nativeCamera_->getFallbackListener() or a similar
  // callback hookup. Encoder lifecycle is driven from this callback:
  //   CameraState::Capturing → construct videoEncoder (or no-op if already constructed)
  //   CameraState::Idle / Failed / Unavailable → destroy videoEncoder (no-op if null)
  // Phase 20 piggybacks on this callback — the encoder is constructed BUT NOT OPENED on Capturing.
  // open() happens only on setBroadcastVideo(true) per D-13.

setBroadcastVideo(bool enabled) impl (DEFINE IN THIS PLAN):
  void JamWideJuceProcessor::setBroadcastVideo(bool enabled) {
    if (enabled) {
      if (!videoEncoder || broadcastVideoEnabled.load()) return;        // camera not Capturing OR already broadcasting
      auto cfg = jamwide::makeConfigForPreset(getCurrentCameraPreset()); // Phase 19 preset accessor
      jamwide::VideoEncoderConfig vcfg = cfg;
      auto publishSps = [this](const void* data, int len) { client->SetVideoSPSPPS(data, len); };
      auto publishNal = [this](const void* data, int len) { client->QueueVideoFrame(data, len); };
      if (!videoEncoder->open(vcfg, frameDistributor.get(),
                              client->getAudioIntervalSeqPtr(),
                              publishSps, publishNal, /*listener*/ this)) {
        juce::Logger::writeToLog("Phase 20: videoEncoder->open() failed");
        return;
      }
      client->SetVideoBroadcastActive(true);                            // next on_new_interval emits BEGIN+marker(+SPS-PPS)
      broadcastVideoEnabled.store(true, std::memory_order_release);
    } else {
      if (!broadcastVideoEnabled.load()) return;
      // ORDER: deactivate FIRST so the next on_new_interval emits END at deactivate
      // while the encoder thread is still alive (publishEncodedNal calls between
      // SetVideoBroadcastActive(false) and videoEncoder->close() are gated by
      // QueueVideoFrame's `if (!m_video_active || !m_video_interval_open) return;`
      // path inside m_video_cs — safe).
      client->SetVideoBroadcastActive(false);
      // Drive at least one interval to let on_new_interval emit END (or accept that the
      // END will go out on the next natural interval; planner picks based on whether
      // a synchronous drive is feasible from the message thread — likely NO, just let
      // it happen naturally).
      videoEncoder->close();
      broadcastVideoEnabled.store(false, std::memory_order_release);
    }
  }

VideoEncoderListener implementation (DECLARE in JamWideJuceProcessor.h; DEFINE IN JamWideJuceProcessor.cpp):
  // JamWideJuceProcessor implements VideoEncoderListener for fatal-error / open-close / SPS-PPS-published log events.
  void onEncoderOpened(const jamwide::VideoEncoderConfig& cfg) override {
    juce::Logger::writeToLog("Phase 20: encoder opened " + juce::String(cfg.width) + "x" + juce::String(cfg.height));
  }
  void onEncoderClosed() override { juce::Logger::writeToLog("Phase 20: encoder closed"); }
  void onEncoderReconfigured(const jamwide::VideoEncoderConfig& cfg) override { juce::Logger::writeToLog("Phase 20: encoder reconfigured to preset " + ...); }
  void onEncoderFatalError(const char* reason) override {
    juce::Logger::writeToLog(juce::String("Phase 20: encoder fatal error: ") + reason);
    // Self-heal attempt: schedule a reconfigure on the message thread via juce::MessageManager::callAsync.
    juce::MessageManager::callAsync([this]() {
      if (videoEncoder) videoEncoder->reconfigure(jamwide::makeConfigForPreset(getCurrentCameraPreset()));
    });
  }
  void onSpsPpsPublished(int spsPpsLen) override { juce::Logger::writeToLog("Phase 20: SPS/PPS published " + juce::String(spsPpsLen) + " bytes"); }

From juce/ui/ConnectionBar.cpp existing CameraButton popup (line 25-50 area):
  // Existing pattern: PopupMenu opens on left-click; calls onCameraQualitySelected or onCameraStopRequested.
  // Plan 20-03 extends the popup to add a "Start Broadcast" / "Stop Broadcast" item (label flips based on state).
  // When selected: call parent.onBroadcastToggleRequested() (new lambda hook).

ConnectionBar additions (DECLARE in juce/ui/ConnectionBar.h):
  std::function<void()> onBroadcastToggleRequested;
  void setCameraIsBroadcasting(bool b) noexcept { cameraIsBroadcasting_ = b; }
  bool cameraIsBroadcasting_ = false;
  // The Camera button's popup menu (CameraButton::mouseDown) adds an item:
  //   menu.addItem(100, cameraIsBroadcasting_ ? "Stop Broadcast" : "Start Broadcast", broadcastEnabledInState, false);
  // result==100 → parent.onBroadcastToggleRequested()

JamWideJuceEditor wiring (REFERENCE; the editor code patches needed to thread the callback are minor — same pattern as the existing onCameraQualitySelected wiring):
  connectionBar_->onBroadcastToggleRequested = [this]() {
    processor.setBroadcastVideo(!processor.isBroadcastingVideo());
    connectionBar_->setCameraIsBroadcasting(processor.isBroadcastingVideo());
  };

From juce/NinjamRunThread.cpp existing connect-up block (lines 322-389; the SetLocalChannelInfo + Instatalk + NotifyServerOfChannelChange block):
  // Existing tail:
  //   client->SetLocalChannelInfo(4, "Instatalk", ...);        // line ~361-366
  //   client->SetLocalChannelProcessor(4, ...);                // line ~374-381
  //   client->NotifyServerOfChannelChange();                   // line 389
  // Plan 20-03 inserts BETWEEN the SetLocalChannelProcessor call and the NotifyServerOfChannelChange call,
  // per D-18 which requires BOTH SetLocalChannelInfo(chidx=1, name="video", flags=0x10) AND
  // SetVideoChannel(chidx=1, fourcc=H264) to be called at NINJAM connect-up:
  //   // Phase 20: register the video channel (chidx=1, name="video", flags=0x10, fourcc=H264) at connect-up per D-18.
  //   // BOTH calls are required:
  //   //  - SetLocalChannelInfo announces the channel metadata (name + flags) so receivers
  //   //    can identify the video capability by name (without it, the channel index is
  //   //    announced as fourcc-only and other clients have no display name).
  //   //  - SetVideoChannel announces the fourCC (H264).
  //   // Channel registration is unconditional regardless of broadcast state — receivers
  //   // see "user has video capability" with proper channel name from connect-time onward;
  //   // payload only flows after SetVideoBroadcastActive(true) lands and the next on_new_interval fires.
  //   client->SetLocalChannelInfo(1, "video", /* srate */ 0, /* bps */ 0, /* nch */ 0,
  //                               /* muted */ false, /* solo */ false, /* flags */ 0x10);
  //   client->SetVideoChannel(1, MAKE_NJ_FOURCC('H','2','6','4'));
  // chidx=1 placement: JamWide audio channels occupy 0-3, Instatalk is at 4; chidx=1 is THE convention
  // documented in CONTEXT.md `<integration_points>` and matches the ROADMAP's "broadcast video on
  // NINJAM channel index 1" wording from the Phase 20 success criteria.
  // SetLocalChannelInfo signature: confirm at execution time against the existing 4-arg / 8-arg variants
  // used for audio channels 0-3 + Instatalk channel 4 — the variant that takes name + flags is the
  // applicable overload (NinjamZap-literal usage; mirror the existing Instatalk call site at ~361-366
  // adjusting only chidx=1, name="video", and the flags=0x10 argument per D-18).

Phase 19 camera-preset accessor (REFERENCE; planner uses the existing API or adds a slim accessor):
  // Phase 19's camera state has a preset field exposed somewhere — confirm location at execution
  // time. Most likely: nativeCamera_->getCurrentPreset() returning int 0/1/2, OR a stored
  // qualityPreset field in the JamWideJuceProcessor's <camera> ValueTree subtree per Phase 19 D-25.
  // If not directly exposed, add a JamWideJuceProcessor::getCurrentCameraPreset() helper that
  // reads from the ValueTree.

UAT harness shape (tests/uat/phase-20-broadcast-uat.sh — pseudo-spec):
  #!/usr/bin/env bash
  set -euo pipefail
  # Prerequisites: two JamWide builds (one regular, one --tsan); video.ninjamzap.com:2049 reachable;
  # macOS or Windows test host; reference NinjamZap mobile peer optional.
  #
  # Step 1: build the standalone with JAMWIDE_BUILD_TESTS=ON so observability accessors are wired.
  # Step 2: launch instance A, connect to video.ninjamzap.com:2049, broadcast at preset=Low for 5 min.
  # Step 3: same for instance B (separate session-id to avoid name collision).
  # Step 4: every 30s, lldb-attach (macOS) or remote-debug (Windows) and read:
  #   - client->GetRawDataSendQueueHighWaterMark()
  #   - client->GetRawDataMutexContentionCount()
  #   - client->GetRawDataSendQueueTotalEnqueueCount()  (owned by Plan 20-00 unconditionally; denominator for contention ratio)
  #   - videoEncoder->getInputDropCount()
  #   - videoEncoder->getFrameOutputCount()
  # Step 5: at end of 5 min, assert: high-water-mark < 32 items; contention/total < 1%; drops == 0.
  # Step 6: repeat at preset=Medium and preset=High.
  # Step 7: rebuild as --tsan; re-run the happy-path 5-min broadcast at Medium preset; assert zero TSan reports.
  # Step 8: audio-thread budget probe — capture mach_absolute_time() samples wrapped around on_new_interval's
  #         video block; require JAMWIDE_BUILD_TESTS-gated probe code in NJClient (add it in Task 2 below);
  #         export samples to a CSV; awk-compute worst-case; assert ≤ 200 µs.
  # Step 9: write tests/uat/phase-20-broadcast-uat-report.md with all numbers + pass/fail per gate.
  # The script need not fully automate steps 4-6 lldb readout — a manual-fallback procedure is acceptable
  # if lldb scripting proves brittle. Per feedback_uat_scope_redflags, the user-visible happy-path
  # verification (2-peer 5-min broadcast) must occur regardless of script automation level.
</interfaces>
</context>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| ConnectionBar message thread → processor.setBroadcastVideo | message-thread API; processor toggles encoder + NJClient state |
| JamWideJuceProcessor → nativeCamera_->state | Phase 19 callback drives encoder construction/destruction |
| videoEncoder thread → NJClient (via Plan 20-02 callbacks) | publishSpsPps and publishEncodedNal callbacks captured at open(); processor lifetime outlives encoder per Phase 19 ownership pattern |
| 5-minute UAT runner host → live ninjamzap-server `video.ninjamzap.com:2049` | community-operated server; no SLA; UAT acceptance allows for transient network issues but not for sync correctness regressions |

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-20-03 | Tampering (UAF in lifecycle ordering — encoder closes before SetVideoBroadcastActive(false) lands) | setBroadcastVideo(false) | mitigate | Ordering rule in setBroadcastVideo: call client.SetVideoBroadcastActive(false) FIRST (next on_new_interval emits END at deactivate while the encoder is still alive); then close encoder; publishEncodedNal callbacks between these two calls are gated by QueueVideoFrame's active+open check inside m_video_cs (Plan 20-02). test_processor_video_lifecycle stresses this ordering in a tight loop |
| T-20-OBS-UAT | Information disclosure (silent regressions in queue/contention under populated load) | UAT acceptance thresholds | mitigate | R3 MF4 concretization: high-water-mark < 32 items, contention < 1% of enqueues at every preset; m_encoder_input_drops == 0; thresholds enforced by tests/uat/phase-20-broadcast-uat.sh and recorded in tests/uat/phase-20-broadcast-uat-report.md; exceed → substrate-tuning subplan, NOT a sync-arch change; total-enqueue counter (denominator) owned by Plan 20-00 unconditionally |
| T-20-AUDIO-RT | Real-time safety (audio-thread budget under populated HD broadcast) | on_new_interval video block | mitigate | Per-call budget probe (mach_absolute_time / QueryPerformanceCounter wrap) added under JAMWIDE_BUILD_TESTS; samples captured during UAT step 8; worst-case threshold ≤ 200 µs (Assumption A5 + CONTEXT.md Deferred Ideas) |
| T-20-TSAN | Tampering (data races invisible without TSan dual-scope) | NJClient + Openh264Encoder + JamWideFrameDistributor | mitigate | UAT step 7 runs --tsan build broadcast 5-min at Medium preset; zero TSan-reported races required; tests/test_video_state_machine + test_curwritefile_guid_seqlock are TSan-clean per Plan 20-02 acceptance — this UAT extends coverage to the live broadcast path including JamWideFrameDistributor onFrame + Openh264Encoder thread + NJClient on_new_interval |
| T-20-CONN-RACE | Tampering (SetVideoChannel called before AUTH completes, server rejects per Pitfall 6) | NinjamRunThread connect-up | mitigate | SetLocalChannelInfo(1, "video", flags=0x10) + SetVideoChannel(1, H264) call sites are INSIDE the existing `if (currentStatus == NJClient::NJC_STATUS_OK)` non-prelisten branch (line 322-389 area), AFTER all existing audio SetLocalChannelInfo calls + Instatalk wiring and BEFORE NotifyServerOfChannelChange — same ordering as existing audio channel registration which is proven to land post-AUTH |
| T-20-SC | Tampering (supply chain) | no new packages | n/a | No package installs; existing vendored ffmpeg path |
</threat_model>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: Processor + Editor + ConnectionBar wiring — Broadcast toggle, encoder lifecycle, NinjamRunThread connect-up (SetLocalChannelInfo + SetVideoChannel per D-18), audio-thread budget probe (lifecycle ordering per T-20-03)</name>
  <files>
    juce/JamWideJuceProcessor.h,
    juce/JamWideJuceProcessor.cpp,
    juce/ui/ConnectionBar.h,
    juce/ui/ConnectionBar.cpp,
    juce/NinjamRunThread.cpp,
    src/core/njclient.h,
    src/core/njclient.cpp
  </files>
  <behavior>
    A) JamWideJuceProcessor — add `std::unique_ptr<jamwide::VideoEncoder> videoEncoder;` (initialized to `std::make_unique<jamwide::Openh264Encoder>()` when the camera transitions to Capturing — piggyback on the existing Phase 19 camera-state callback; reset to nullptr when camera Idle/Failed). Add `std::atomic<bool> broadcastVideoEnabled{false};`. Add `setBroadcastVideo(bool)` and `isBroadcastingVideo()` per `<interfaces>`. Implement VideoEncoderListener overrides — log via juce::Logger::writeToLog; onEncoderFatalError schedules a reconfigure via juce::MessageManager::callAsync. Add `int getCurrentCameraPreset() const` helper that reads from the ValueTree's `<camera>` subtree per Phase 19 D-25 (or from nativeCamera_ if it exposes the field directly).

    B) ConnectionBar — extend the existing CameraButton popup (the PopupMenu in mouseDown at line 25-50) to add a "Start Broadcast" / "Stop Broadcast" item; menu result 100 → fires onBroadcastToggleRequested callback. Add `cameraIsBroadcasting_` member + setter; the menu item's label flips on the setter. Live-track the broadcast state so the popup label is always consistent. The Broadcast item is gated by `broadcastEnabledInState` = (cameraIsActive_ && client connected); when greyed out, the popup item is disabled-but-visible (existing patterns in the menu for Stop Camera at line 37).

    C) JamWideJuceEditor — wire connectionBar_->onBroadcastToggleRequested = [this]() { processor.setBroadcastVideo(...); connectionBar_->setCameraIsBroadcasting(...); }; mirroring the existing onCameraQualitySelected pattern.

    D) NinjamRunThread connect-up — D-18 requires BOTH `SetLocalChannelInfo(chidx=1, name="video", flags=0x10)` AND `SetVideoChannel(chidx=1, fourcc=H264)` at NINJAM connect-up. Insert BOTH calls AFTER the existing Instatalk channel registration block (after line ~381 SetLocalChannelProcessor) and BEFORE the existing `client->NotifyServerOfChannelChange();` at line 389, in this exact order:
        // Phase 20: register the video channel (chidx=1) at NINJAM connect-up per D-18.
        // Both calls are required (T-20-CONN-RACE mitigation; cite D-18 + Pitfall 6 inline):
        //   - SetLocalChannelInfo announces channel metadata (name="video", flags=0x10) so
        //     receivers can identify the video capability by name.
        //   - SetVideoChannel announces the fourCC so receivers know it's H264.
        client->SetLocalChannelInfo(1, "video", /* srate */ 0, /* bps */ 0, /* nch */ 0,
                                    /* muted */ false, /* solo */ false, /* flags */ 0x10);
        client->SetVideoChannel(1, MAKE_NJ_FOURCC('H','2','6','4'));
      Confirm the SetLocalChannelInfo overload signature at execution time by mirroring the existing Instatalk call site at line ~361-366 — the argument list MUST match the variant used for audio channels 0-3 + Instatalk channel 4, with only chidx=1, name="video", and flags=0x10 differing from the Instatalk template. If the existing overload does not accept a `flags` parameter, locate the variant that does (NinjamZap-literal usage; see CONTEXT.md `<domain>` item 5 wording "SetLocalChannelInfo(chidx=1, name='video', flags=0x10)" — flags is a required argument per the D-18 spec). Add a brief comment citing D-18 + Pitfall 6.

    E) Audio-thread budget probe — under JAMWIDE_BUILD_TESTS, add a `std::atomic<uint64_t> m_on_new_interval_video_block_worst_case_ns{0};` member on NJClient + an accessor `GetOnNewIntervalVideoBlockWorstCaseNs() const`. Inside on_new_interval, wrap the video block (the WDL_MutexLock vlock scope added by Plan 20-02) in `auto t0 = std::chrono::steady_clock::now();` / `auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();` ; update worst-case via CAS loop:
        uint64_t prev = m_on_new_interval_video_block_worst_case_ns.load(std::memory_order_relaxed);
        while ((uint64_t)dt > prev && !m_on_new_interval_video_block_worst_case_ns.compare_exchange_weak(prev, (uint64_t)dt, std::memory_order_relaxed)) {}
       NOTE: `steady_clock::now()` is an audio-thread call — std::chrono is generally heap-free but the macOS implementation uses clock_gettime which is a vDSO-resident user-space call (~50ns); explicitly accept this under JAMWIDE_BUILD_TESTS-only build per the existing realtime-audio-reviewer convention (the probe is OFF in production builds). Decorate with a `#ifdef JAMWIDE_BUILD_TESTS` guard.

    F) The queue total-enqueue counter (`m_rawdata_sendq_total_enqueues` + accessor `GetRawDataSendQueueTotalEnqueueCount()`) is owned by Plan 20-00 unconditionally — Plan 20-03 only READS it from the UAT harness. Do NOT add it here. The UAT's contention ratio is `contention_count / total_enqueues` computed from Plan 20-00's atomics: `client->GetRawDataMutexContentionCount() / client->GetRawDataSendQueueTotalEnqueueCount()`.
  </behavior>
  <action>
    Implement parts A-F. Mirror the existing JUCE patterns: Phase 19's Subscription RAII (HIGH-2) is the model for any frame-distributor binding; the existing ConnectionBar PopupMenu pattern from lines 25-50 is the model for the Broadcast UI; the existing connect-up SetLocalChannelInfo block in NinjamRunThread is the model for the new SetLocalChannelInfo(1, "video", ..., flags=0x10) + SetVideoChannel(1, H264) placement (D-18 requires BOTH calls). For the lifecycle-ordering rule (T-20-03 mitigation), include an inline source-code comment at the setBroadcastVideo(false) site explicitly citing T-20-03 + feedback_phase19_review_layers so future reviewers see the reason.
  </action>
  <verify>
    <automated>cd build-juce &amp;&amp; cmake --build . --target JamWideJuce_Standalone JamWideJuce_AU JamWideJuce_VST3 -- -j8 2>&amp;1 | tail -20</automated>
    All three plugin/standalone targets build clean with the new Broadcast UX + lifecycle wiring.
  </verify>
  <done>
    Processor / Editor / ConnectionBar / NinjamRunThread / NJClient changes compile. The audio-thread budget probe is wired under JAMWIDE_BUILD_TESTS. BOTH SetLocalChannelInfo(1, "video", ..., flags=0x10) AND SetVideoChannel(1, H264) are in the NinjamRunThread connect-up block, in that order, between Instatalk and NotifyServerOfChannelChange (per D-18). Total-enqueue counter + accessor are owned by Plan 20-00 (not added here); Plan 20-03 reads them from the UAT harness.
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: tests/test_processor_video_lifecycle.cpp — encoder + NJClient lifecycle wiring validation without NINJAM session</name>
  <files>
    tests/test_processor_video_lifecycle.cpp,
    CMakeLists.txt
  </files>
  <behavior>
    Sub-tests (TEST/PASS/FAIL macros pattern):

    1. test_lifecycle_camera_open_constructs_encoder — instantiate a stripped-down test harness (Phase 19's pure-C++ camera test pattern: construct JamWideJuceProcessor on heap, drive nativeCamera_'s state machine to Capturing via the existing test hook); assert videoEncoder != nullptr.
    2. test_lifecycle_setBroadcastVideo_true_opens_encoder_and_activates — with camera Capturing, call setBroadcastVideo(true); assert: videoEncoder->getFrameOutputCount() may still be 0 (no frames consumed yet) but the encoder thread is running; client->isBroadcastingVideo()-equivalent returns true (check via the m_video_active member exposed under JAMWIDE_BUILD_TESTS via a test accessor `bool GetVideoActiveForTest() const`).
    3. test_lifecycle_setBroadcastVideo_false_deactivates_before_close — with broadcast on, call setBroadcastVideo(false); assert: SetVideoBroadcastActive(false) was called BEFORE videoEncoder->close() — instrument via a JAMWIDE_BUILD_TESTS test hook that records the call order in an std::atomic<int> sequence counter (each event bumps and records its index; test reads back the indices and asserts order). This is the T-20-03 mitigation enforcement test.
    4. test_lifecycle_camera_idle_destroys_encoder — drive nativeCamera_ to Idle; assert videoEncoder == nullptr.
    5. test_lifecycle_rapid_toggle_no_crash — bounce setBroadcastVideo true/false 100 times in 1 second; assert no crash, no assert, no race; videoEncoder->getInputDropCount() may grow but encoder doesn't leak (verify by checking encoder thread terminated cleanly at the end).

    Test linking: same shape as Phase 19's pure-C++ test discipline — link against the relevant source files DIRECTLY (Openh264Encoder.cpp + NJClient via njclient lib + JamWideJuceProcessor.cpp). JUCE plugin host integration is NOT exercised at this layer; the UAT harness in Task 4 is where the actual DAW/standalone broadcasts happen.
  </behavior>
  <action>
    Implement the 5 sub-tests + the CMake wiring. The test-only accessors needed (`GetVideoActiveForTest`, lifecycle sequence-recording hook) should be added under existing `#ifdef JAMWIDE_BUILD_TESTS` blocks in NJClient and JamWideJuceProcessor.
  </action>
  <verify>
    <automated>cd build-juce &amp;&amp; cmake --build . --target test_processor_video_lifecycle -- -j8 &amp;&amp; ctest -R processor_video_lifecycle --output-on-failure 2>&amp;1 | tail -30</automated>
    test_processor_video_lifecycle builds + executes; 5/5 sub-tests green.
  </verify>
  <done>
    test_processor_video_lifecycle.cpp passes 5/5 sub-tests, including the T-20-03 lifecycle-ordering test that records call-order indices. Full suite green.
  </done>
</task>

<task type="auto">
  <name>Task 3: Create tests/uat/phase-20-broadcast-uat.sh harness + procedure docs</name>
  <files>
    tests/uat/phase-20-broadcast-uat.sh,
    tests/uat/phase-20-broadcast-uat-procedure.md,
    CMakeLists.txt
  </files>
  <action>
    Write `tests/uat/phase-20-broadcast-uat.sh` per the `<interfaces>` shape above. The shell script is the orchestrating harness: it builds the standalone with JAMWIDE_BUILD_TESTS=ON, opens two JamWide standalone instances (or guides the operator with on-screen prompts since lldb-attach to 2 separate macOS GUI apps may be brittle), and runs the 5-minute broadcast at each preset. The acceptance thresholds (high-water < 32, contention < 1%, drops == 0, audio-thread budget ≤ 200 µs worst-case) are encoded as `if`-checks in the script with explicit failure messages citing the R3 MF4 spec.

    Also write `tests/uat/phase-20-broadcast-uat-procedure.md` — a human-runnable procedure document detailing each step + screenshot placement + expected UI states + recovery procedure if the public server is down (fallback to local ninjamzap-server-docker on `localhost:2049`).

    CMakeLists.txt: ensure the shell script is marked executable (`chmod +x` post-configure step or installed in a way that preserves the executable bit). Add a custom target `phase20-uat` that runs the script for convenience: `add_custom_target(phase20-uat COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/uat/phase-20-broadcast-uat.sh ...)`.
  </action>
  <verify>
    <automated>chmod +x tests/uat/phase-20-broadcast-uat.sh; bash -n tests/uat/phase-20-broadcast-uat.sh &amp;&amp; echo "syntax OK" &amp;&amp; ls -la tests/uat/phase-20-broadcast-uat-procedure.md</automated>
    Shell-script syntax check passes (no run; the actual UAT is the next checkpoint task); procedure markdown file exists.
  </verify>
  <done>
    Shell harness + procedure markdown both exist. Bash syntax check passes. Custom CMake target `phase20-uat` exists.
  </done>
</task>

<task type="checkpoint:human-verify" gate="blocking">
  <name>Task 4: 5-minute populated-server UAT against video.ninjamzap.com:2049 — manual checkpoint (R3 MF4 + feedback_uat_scope_redflags + Phase 20 acceptance gate)</name>
  <what-built>
    Phase 20 send-side video broadcast: encoder + NJClient state machine + channel registration + Broadcast button + queue/contention observability. Plans 20-00 / 20-01 / 20-02 / 20-03 Tasks 1-3 are complete; tests are green; the standalone + plugin targets build clean. This checkpoint exercises the user-visible happy path end-to-end on a populated server.
  </what-built>
  <how-to-verify>
    Required environment: macOS or Windows host with JamWide standalone built from this branch; an internet connection that can reach `video.ninjamzap.com:2049`; ideally a second JamWide host (a different machine OR a NinjamZap mobile peer) for the 2-peer test.

    Run `bash tests/uat/phase-20-broadcast-uat.sh` and follow the procedure in `tests/uat/phase-20-broadcast-uat-procedure.md`.

    Specific checks (R3 MF4 + Phase 20 success criteria):

    1. Connect instance A to `video.ninjamzap.com:2049`; verify NINJAM auth succeeds and channel 1 is registered with BOTH name "video" and fourCC H264 (verifiable via NinjamRunThread log lines or `lldb` on the GetLocalChannelInfo + video-channel accessors per D-18); audio is broadcasting normally (existing Phase 14 audio path is unaffected).

    2. Open the camera on instance A (Phase 19 path); right-click Camera button → "Start Broadcast"; verify the menu label changes to "Stop Broadcast" and Phase 20's log lines appear:
       - "Phase 20: encoder opened 320x240" (or 640x480 / 1280x720 depending on preset)
       - "Phase 20: SPS/PPS published N bytes"

    3. Run for 5 minutes at preset=Low. Every 30s, sample (via lldb or the JAMWIDE_BUILD_TESTS log channel) the four counters:
       - client->GetRawDataSendQueueHighWaterMark()                       — assert < 32 items at end
       - client->GetRawDataMutexContentionCount() / GetRawDataSendQueueTotalEnqueueCount()  — assert < 1% at end (denominator owned by Plan 20-00)
       - videoEncoder->getInputDropCount()                                — assert == 0 at end
       - (under JAMWIDE_BUILD_TESTS) client->GetOnNewIntervalVideoBlockWorstCaseNs() — assert ≤ 200,000 ns at end

    4. Connect instance B (or use NinjamZap mobile peer); verify instance B sees instance A as a participant with video capability on channel 1 (named "video", fourCC H264 per D-18); observe that instance B is RECEIVING the video data on channel 1 — for Phase 20 we only assert wire-level reception (Phase 21 owns decode + display, so instance B will not render the video; this is expected and DOCUMENTED in the procedure). Validation that the wire is bit-for-bit NinjamZap-compatible can use Wireshark or tcpdump capture filtered for the JamWide src port + the NINJAM dst port + a payload-byte sniff matching the 24-byte marker pattern `00 00 00 14 XX XX XX XX <16-byte GUID>`.

    5. Subjective audio-glitch check during the 5-min Low-preset run: operator listens for audible audio dropouts in both instance A's local monitoring AND any peer audio if a second JamWide instance is connected. Note any glitches in the report.

    6. Repeat steps 2-5 at preset=Medium (right-click Camera → Medium → Start Broadcast).

    7. Repeat steps 2-5 at preset=High (right-click Camera → High → Start Broadcast).

    8. TSan dual-scope: rebuild standalone as `--tsan` (per Phase 15.1 D-07 — `./scripts/build.sh --tsan`); re-run the 5-min broadcast at preset=Medium against `video.ninjamzap.com:2049`; assert: zero TSan-reported races (terminal output should be free of `WARNING: ThreadSanitizer: data race`).

    9. Capture the final UAT report at `tests/uat/phase-20-broadcast-uat-report.md` — one row per preset, columns: high-water-mark, contention ratio, drops, audio-thread budget worst-case, audio-glitch subjective note, TSan status (only at Medium).

    Pass criteria (must ALL hold for Phase 20 to close):
    - All 3 presets: high-water < 32, contention < 1%, drops == 0, audio-thread budget ≤ 200 µs worst-case.
    - TSan dual-scope at Medium: zero races.
    - Subjective audio: no glitches at any preset.
    - Channel registration: instance A surfaces channel 1 with BOTH name "video" (via SetLocalChannelInfo) AND fourCC H264 (via SetVideoChannel) within 1s of connection.
    - Wire-format check (best-effort via tcpdump or NinjamZap mobile receive): first chunk of each interval is 24 bytes matching the marker spec.
  </how-to-verify>
  <resume-signal>Type "approved — all gates pass" with the path to `tests/uat/phase-20-broadcast-uat-report.md`, OR describe issues (e.g., "high-water hit 47 items at preset=High, contention 0.3%, no glitches — investigate substrate sizing").</resume-signal>
</task>

<task type="auto">
  <name>Task 5: CHANGELOG.md entry + Phase 20 closeout summary</name>
  <files>
    CHANGELOG.md
  </files>
  <action>
    Add a `## Unreleased — v1.3 alpha (Phase 20) — H.264 native video broadcast` section per the Phase 19 D-26 pattern. Bullets:
    - "Add native H.264 send-side video broadcast over NINJAM, bit-for-bit wire-compatible with NinjamZap mobile and the ninjamzap-core reference. Channel 1, fourCC `H264`, 24-byte interval marker, SPS/PPS chunk #2, per-frame 4-byte BE length prefix."
    - "Add abstract `VideoEncoder` interface + `Openh264Encoder` libavcodec-backed implementation; per-preset bitrate ladder (Low 100 kbps / Medium 300 kbps / High 800 kbps); one IDR per NINJAM interval via atomic interval-sync counter."
    - "Add Broadcast toggle on the Camera button popup menu (Phase 19 UX extension)."
    - "Add unconditional video-channel registration at NINJAM connect-up — receivers see 'user has video capability' with channel name 'video' (SetLocalChannelInfo + flags=0x10) and fourCC H264 (SetVideoChannel) from connection time onward; payload only flows after Broadcast is enabled."
    - "Revise Phase 14.3-02's RawData send substrate to NinjamZap-literal `WDL_PtrList<RawDataQueueItem> + WDL_Mutex` (Plan 20-00) — multi-producer-correct under HYBRID emission model (audio thread + encoder thread)."
    - "Add per-channel atomic seqlock on `Local_Channel::m_curwritefile.guid` for deterministic audio-thread 16-byte GUID read at marker construction (Plan 20-02 Must-fix 1 closure)."
    - "Known limitations (v1.3 beta): cold-start may produce a marker-only first interval if encoder warm-up exceeds the broadcast-on→first-interval window (subsequent intervals carry SPS/PPS); 'Auto' adaptive-bitrate preset deferred to v1.4+; VideoToolbox/MediaFoundation backends architected via the abstract interface but deferred to a follow-up phase."
    - "UAT acceptance gates (5-min 2-peer broadcast at `video.ninjamzap.com:2049` per preset): `m_rawdata_sendq_high_water_mark < 32`, `contention_ratio < 1%`, `m_encoder_input_drops == 0`, audio-thread budget ≤ 200 µs worst-case, TSan dual-scope clean."
  </action>
  <verify>
    <automated>grep -c "Phase 20" CHANGELOG.md | grep -vq "^0$" &amp;&amp; echo "CHANGELOG OK"</automated>
    CHANGELOG.md has a Phase 20 entry.
  </verify>
  <done>
    CHANGELOG.md updated. Phase 20 entry exists with all bullets per the Phase 19 D-26 pattern.
  </done>
</task>

</tasks>

<verification>
- `cd build-juce && cmake --build . --target JamWideJuce_Standalone JamWideJuce_AU JamWideJuce_VST3 test_processor_video_lifecycle -- -j8` exits 0
- `ctest -R "processor_video_lifecycle|video_state_machine|curwritefile_guid_seqlock|video_encoder|rawdata_send" --output-on-failure` exits 0
- `cd build-juce && ctest --output-on-failure` exits 0 (full-suite regression check)
- `bash -n tests/uat/phase-20-broadcast-uat.sh && echo "uat syntax OK"`
- `grep -c "SetVideoChannel\\s*(\\s*1\\s*," juce/NinjamRunThread.cpp` returns ≥ 1 (channel registration fourCC announcement at connect-up exists per D-18)
- `grep -cE "SetLocalChannelInfo\\s*\\(\\s*1\\s*,\\s*\"video\"" juce/NinjamRunThread.cpp` returns ≥ 1 (channel registration name/flags announcement at connect-up exists per D-18)
- `grep -c "setBroadcastVideo" juce/JamWideJuceProcessor.cpp` returns ≥ 2 (definition + at least one internal callsite)
- `grep -c "onBroadcastToggleRequested" juce/ui/ConnectionBar.cpp` returns ≥ 1 (popup menu wires the broadcast toggle)
- `grep -c "Phase 20" CHANGELOG.md` returns ≥ 1
- Task 4 manual UAT report (`tests/uat/phase-20-broadcast-uat-report.md`) exists and records pass-status for all 3 presets + TSan + audio-glitch + wire-format check
</verification>

<success_criteria>
- Plan 20-03 delivers WIRE-03 (concurrent audio+video producer arbitration verified end-to-end over 5 minutes at each preset) and implicitly re-asserts COD-01 + COD-02 + WIRE-01 via the UAT.
- R3 must-fix item 4 (queue observability concretization) is closed: high-water-mark + contention counter + total-enqueue counter exist (all owned by Plan 20-00 unconditionally); UAT acceptance thresholds (<32 items, <1% contention, 0 drops) are encoded in the script + procedure doc + report.
- T-20-03 lifecycle ordering (SetVideoBroadcastActive(false) BEFORE encoder.close()) is enforced in code + asserted in test_processor_video_lifecycle.
- T-20-CONN-RACE (Pitfall 6) is mitigated by placing BOTH SetLocalChannelInfo(1, "video", ..., flags=0x10) AND SetVideoChannel(1, H264) inside the existing post-AUTH branch in NinjamRunThread connect-up per D-18.
- TSan dual-scope per Phase 15.1 D-07 is part of Plan 20-03 UAT acceptance per CONTEXT.md `<specifics>` audio-glitch test signature.
- The Phase 20 ROADMAP success criteria are all met: (1) user sees video broadcast at spike baseline ~98 kbps; (2) bit-for-bit NinjamZap-compatible wire format on channel 1 with both name "video" and fourCC H264 announced at connect-up per D-18; (3) 5-min populated broadcast no audio glitches no Send race; (4) toggle camera off → clean END.
</success_criteria>

<output>
On completion (including Task 4 manual UAT checkpoint approval), write `.planning/phases/20-h-264-encoder-send-pipeline/20-03-SUMMARY.md` per the get-shit-done summary template. Capture in the summary: (a) the final per-preset UAT numbers (high-water, contention ratio, drops, audio-thread budget worst-case); (b) whether the TSan dual-scope run produced any reports (and how they were dispositioned if so); (c) any subjective audio glitches observed and their suspected cause; (d) whether the public server `video.ninjamzap.com:2049` was reachable for the full run, or whether the local-docker fallback was needed; (e) the actual line numbers where SetLocalChannelInfo(1, "video", ..., flags=0x10) AND SetVideoChannel(1, H264) landed in NinjamRunThread.cpp (so Phase 24's per-DAW UAT documentation can cite them); (f) any deferred Phase 20 follow-up tasks that need a quick-task or v1.4 escalation.
</output>
</content>
</invoke>