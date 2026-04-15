---
status: complete
quick_id: 260415-s8b
date: "2026-04-15"
one_liner: "Fix BPM/BPI keyboard input stolen by chat focus handler, remove DBG hot-path logging causing CPU spikes"
commits:
  - 782f7ef
  - 6b5d003
key_files:
  modified:
    - juce/JamWideJuceEditor.cpp
    - juce/midi/MidiMapper.cpp
---

# Quick Task: Fix BPM/BPI Keyboard Input and CPU Spikes

## Bug 1: BPM/BPI keyboard input broken

**Root cause:** The editor's `mouseDown` handler (`addMouseListener(this, true)`) intercepts all child component clicks to redirect focus to the chat input. It excludes known interactive types (TextEditor, Button, Slider, ComboBox, VbFader) but BeatBar was not in the exclusion list. When the user clicked the BPM/BPI label, BeatBar's `mouseDown` created a vote TextEditor, but the editor's handler immediately called `chatPanel.focusChatInput()`, stealing keyboard focus.

**Fix:** Added `dynamic_cast<BeatBar*>(e.eventComponent) == nullptr` to the exclusion chain in `JamWideJuceEditor::mouseDown`.

## Bug 2: CPU spikes

**Root cause:** MidiMapper had 3 `DBG()` calls in hot paths:
- `handleIncomingMidiMessage`: logged every standalone MIDI CC/Note on the device thread
- `timerCallback`: logged every remote parameter sync with 6-field string concatenation at 50Hz

While `DBG` compiles out in release, the `juce::String` construction still executes. In debug builds, this produced continuous output that was both CPU-wasteful and noisy.

**Fix:** Removed all 3 `DBG()` calls from MidiMapper hot paths.

## Self-Check: PASSED
- VST3 builds cleanly
- 23/23 MIDI mapping tests pass
- BeatBar mouseDown no longer intercepted by editor
- No DBG calls remain in MidiMapper.cpp
