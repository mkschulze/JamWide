---
phase: 05-mixer-ui-and-channel-controls
plan: 03
subsystem: ui
tags: [juce, mixer, fader, apvts, multi-bus, metronome, local-channels, input-selector]

# Dependency graph
requires:
  - phase: 05-02
    provides: ChannelStrip with VbFader, pan slider, mute/solo buttons, mixer callbacks, stable identity lookup
  - phase: 05-01
    provides: VbFader component, drawLinearSlider for pan/metronome, APVTS local channel parameters
provides:
  - Local 4-channel expand/collapse with child strips
  - Multi-bus processBlock collecting from all 4 stereo input buses
  - Input bus selector per local channel strip
  - Metronome slider and mute button in master strip footer
  - APVTS attachments for all 4 local channel faders/pan/mute
  - 4-channel NJClient setup on connect with persisted input/transmit state
  - 4-channel local VU polling and master VU from processBlock output
affects: [05-04]

# Tech tracking
tech-stack:
  added: []
  patterns: [multi-bus-input-collection, apvts-attachment-lifecycle-in-destructor, input-bus-selector-srcch-mapping]

key-files:
  created: []
  modified:
    - juce/JamWideJuceProcessor.h
    - juce/JamWideJuceProcessor.cpp
    - juce/NinjamRunThread.cpp
    - src/threading/ui_command.h
    - src/ui/ui_state.h
    - juce/ui/ChannelStripArea.h
    - juce/ui/ChannelStripArea.cpp
    - juce/ui/ChannelStrip.h
    - juce/ui/ChannelStrip.cpp

key-decisions:
  - "processBlock collects from all 4 stereo input buses (up to 8 mono channels) fixing Codex HIGH review concern"
  - "Metronome has volume + mute only (no pan) per D-18 locked decision, overriding roadmap text"
  - "APVTS attachments destroyed before strip components in destructor to prevent dangling references"
  - "Master VU computed from processBlock output buffer (post-AudioProc) for accurate peak measurement"
  - "Local channel input bus stored as 0-based stereo pair index, converted to srcch with bit 10 stereo flag on command dispatch"
  - "chatSidebarVisible, localTransmit, localInputSelector stored on processor (not APVTS) per D-21 review guidance"

patterns-established:
  - "Multi-bus input collection: iterate getBusCount(true), getBusBuffer per bus, copy to scratch, pass all channels to AudioProc"
  - "Input bus selector: ComboBox with Input 1-2 through 7-8, converts selectedId to NJClient srcch with stereo bit"
  - "APVTS attachment lifecycle: destroy attachments explicitly before component destruction in parent destructor"

requirements-completed: [UI-05, UI-06, UI-08]

# Metrics
duration: 6min
completed: 2026-04-04
---

# Phase 5 Plan 03: Local 4-Channel Mixer, Metronome Controls, Multi-Bus Audio Path Summary

**Local 4-channel expand/collapse with APVTS-attached controls, metronome slider/mute in master strip, and multi-bus processBlock collecting all 4 stereo input buses**

## Performance

- **Duration:** 6 min
- **Started:** 2026-04-04T16:38:09Z
- **Completed:** 2026-04-04T16:44:09Z
- **Tasks:** 2
- **Files modified:** 9

## Accomplishments
- processBlock expanded from hardcoded 2-channel to full 4-bus collection (up to 8 mono channels) for NJClient AudioProc, resolving Codex HIGH review concern
- Local strip shows expand button; clicking reveals 3 child strips (Ch2-Ch4) each with VbFader, pan slider, mute/solo buttons, input bus selector, and transmit toggle
- All 4 local channels set up in NJClient on connect with persisted input bus and transmit state from processor
- Metronome horizontal yellow slider and MUTE button in master strip footer, both APVTS-attached (no pan per D-18)
- APVTS attachments for all 4 local channel faders/pan/mute with proper teardown in destructor before component destruction

## Task Commits

Each task was committed atomically:

1. **Task 1: Multi-bus processBlock + 4-channel NJClient setup + extended VU + command extensions** - `1284a8a` (feat)
2. **Task 2: Local 4-channel expand/collapse + metronome + APVTS attachments** - `4ffd899` (feat)

## Files Created/Modified
- `juce/JamWideJuceProcessor.h` - Added chatSidebarVisible, localTransmit[4], localInputSelector[4] persistent state; added #include <array>
- `juce/JamWideJuceProcessor.cpp` - processBlock rewritten for multi-bus input collection from all 4 stereo buses; master VU from output buffer
- `juce/NinjamRunThread.cpp` - 4-channel local setup on connect with persisted state; 4-channel VU polling; srcch passthrough in command handler
- `src/threading/ui_command.h` - SetLocalChannelInfoCommand extended with set_srcch/srcch fields for input bus selection
- `src/ui/ui_state.h` - UiAtomicSnapshot gains local_ch_vu_left/right arrays for 4-channel VU
- `juce/ui/ChannelStripArea.h` - Added localChildStrips, localExpanded_, metroSlider, metroMuteBtn, LocalChannelAttachments struct, APVTS attachment members
- `juce/ui/ChannelStripArea.cpp` - Full local expand/collapse, child strip creation with all callbacks, APVTS attachments for channels 0-3, metronome controls, 4-channel VU polling, proper destructor teardown
- `juce/ui/ChannelStrip.h` - Added inputBusSelector ComboBox, onInputBusChanged callback, getInputBusSelector/setInputBus methods
- `juce/ui/ChannelStrip.cpp` - InputBusSelector initialization with 4 input pairs, layout in header zone, getter/setter implementations; Local strip expand button visibility for multi-channel

## Decisions Made
- processBlock collects all 4 stereo input buses (up to 8 mono channels) instead of hardcoded 2, fixing the Codex HIGH review concern about 2-in/2-out limitation
- Metronome has volume + mute only (no pan) per D-18 locked decision -- roadmap text mentioning pan is overridden by the authoritative user decision
- APVTS attachments are explicitly destroyed before strip components in the ChannelStripArea destructor to prevent dangling reference bugs
- Master VU is computed from processBlock output buffer after AudioProc writes to it, giving accurate post-mix peak measurement
- chatSidebarVisible, localTransmit, and localInputSelector are stored on the processor (not as APVTS parameters) per D-21 review guidance (UI-only state should not pollute host automation)
- Local strip expand button shown when channelCount > 1, consistent with Remote strip behavior (deviation: configure() for StripType::Local previously hardcoded expandButton to false)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Local strip expand button visibility in configure()**
- **Found during:** Task 2 (local expand/collapse)
- **Issue:** ChannelStrip::configure() for StripType::Local had expandButton.setVisible(false) hardcoded, preventing the expand button from showing even when channelCount=4 was passed
- **Fix:** Changed to expandButton.setVisible(channelCount > 1) with matching button text, consistent with Remote strip behavior
- **Files modified:** juce/ui/ChannelStrip.cpp
- **Verification:** Local strip now shows expand button when configured with channelCount=4
- **Committed in:** 4ffd899 (Task 2 commit)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Minimal change to existing configure() switch case, necessary for correct local expand/collapse behavior. No scope creep.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- All local channel mixer controls functional with APVTS persistence and command queue dispatch
- processBlock audio path handles all 4 stereo input buses for NJClient multichannel input
- VU meters for all 4 local channels + master operational at 30Hz
- Plan 04 can add state persistence (ValueTree save/restore for non-APVTS state) and any remaining polish

## Self-Check: PASSED

All 9 files verified present. Both commit hashes (1284a8a, 4ffd899) found in git log.

---
*Phase: 05-mixer-ui-and-channel-controls*
*Completed: 2026-04-04*
