# Visual Latency Compensation Guide - Implementation Plan

## Overview

A real-time visual tool showing users where their audio transients land relative to NINJAM beat boundaries, helping them adjust their timing or DAW latency settings.

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Threshold | User-configurable | Different instruments/dynamics need different sensitivity |
| Placement | Local Channel section | Keeps all local input controls together |
| Visibility | Only when connected | No meaningful beat reference when disconnected |
| Timing reference | Beat-level | More useful for musical timing than interval boundaries |

---

## Phase 1: Data Collection (Audio Thread)

### 1.1 Add Transient/Peak Detection

**Files:** `src/plugin/jamwide_plugin.h`, `src/plugin/clap_entry.cpp` (audio thread)

- Simple peak/onset detection in the audio processing path (not Run thread)
- Track when significant audio events occur relative to beat position
- Store as a ratio: `beat_phase - 0.5` in range [-0.5, +0.5]

**Algorithm (simple onset detection + hysteresis + debounce):**
```cpp
// Envelope follower + hysteresis + min-gap
env = max(mono_sample, env * release_coeff);
if (gate_open && env > threshold && samples_since_trigger > min_gap_samples) {
    record transient at current beat_phase;
    gate_open = false;
    samples_since_trigger = 0;
}
if (!gate_open && env < threshold * 0.6f) {
    gate_open = true;
}
```

**Precise integration (current code layout):**
- Add a small `TransientDetector` state to `JamWidePlugin` (audio-thread owned).
- Drive it inside `plugin_process()` in `src/plugin/clap_entry.cpp` right after `AudioProc()` returns.
- Use `ui_snapshot` atomics for `bpm`, `bpi`, `interval_position`, `interval_length` to resync beat phase per block.

Example mapping:
```cpp
// JamWidePlugin (in src/plugin/jamwide_plugin.h)
struct TransientDetector {
    float env = 0.0f;
    bool gate_open = true;
    int samples_since_trigger = 0;
    double beat_phase = 0.0;        // 0..1
    double samples_per_beat = 48000.0;
};
TransientDetector transient;
```

```cpp
// plugin_process (in src/plugin/clap_entry.cpp)
const float threshold = plugin->ui_snapshot.transient_threshold.load(...);
const int min_gap_samples = (int)(plugin->sample_rate * 0.05f); // 50 ms

// Resync beat_phase from run-thread snapshot once per block
const int pos = plugin->ui_snapshot.interval_position.load(...);
const int len = plugin->ui_snapshot.interval_length.load(...);
const int bpi = plugin->ui_snapshot.bpi.load(...);
const float bpm = plugin->ui_snapshot.bpm.load(...);
if (bpm > 1.0f) {
    plugin->transient.samples_per_beat = (plugin->sample_rate * 60.0) / bpm;
}
if (len > 0 && bpi > 0) {
    const double interval_phase = (double)pos / (double)len;
    plugin->transient.beat_phase = fmod(interval_phase * bpi, 1.0);
}
// per-sample: envelope, gate, debounce, then advance beat_phase
```

**Tighter alignment + smooth resync (beat phase):**
- Maintain `beat_phase` continuously in the audio thread (advance every sample).
- Each block, compute a `snapshot_phase` from `interval_position/interval_length` and `bpi`.
- Compute shortest signed drift in beats (wrap to [-0.5, +0.5]).
- If `abs(drift) > 0.1` beats, snap to `snapshot_phase`; otherwise apply a gentle correction.
 - Normalize drift correction by block duration so behavior stays consistent across buffer sizes.
 - Optional: add a simple rising-edge check to reduce false triggers on sustained tones.

Example drift logic:
```cpp
auto wrap = [](double x) {
    while (x > 0.5) x -= 1.0;
    while (x < -0.5) x += 1.0;
    return x;
};
double drift = wrap(snapshot_phase - beat_phase);
if (fabs(drift) > 0.1) {
    beat_phase = snapshot_phase;
} else {
    // Scale correction by block duration to keep behavior stable across buffer sizes.
    const double correction = 1.0 - exp(-block_ms / 120.0); // ~120ms time constant
    beat_phase += drift * correction;
}
```

**Suggested defaults (tunable constants):**
- `drift_snap_threshold_beats`: `0.08` (techno/EDM: tighter grid, faster recovery)
- `drift_correction_tau_ms`: `120` (time constant for drift correction; scales with block size)
- `min_gap_ms`: `40` ms (denser transient patterns)
- `env_release_coeff`: `0.985` per sample (slightly longer tail for kicks/snares)
- `threshold_hysteresis`: `0.6 * threshold`
 - `edge_check_ratio`: `0.7` (trigger only if `prev_env < threshold * 0.7`)

**Rationale (brief):**
- Tight snap threshold matches stricter timing in 4/4 dance material.
- Time-constant correction keeps alignment stable across different buffer sizes.
- Shorter debounce helps with fast repeated hits (e.g., hats/claps).

### 1.2 Add Atomic State for UI

**File:** `src/ui/ui_state.h`

Add to `UiAtomicSnapshot`:
```cpp
// Latency visualization
std::atomic<float> last_transient_beat_offset{0.0f};  // -0.5 to +0.5 (fraction of beat)
std::atomic<bool>  transient_detected{false};          // flag for UI to consume
std::atomic<float> transient_threshold{0.3f};          // audio thread reads
```

Add to `UiState` (user settings):
```cpp
// Latency guide settings
float transient_threshold = 0.3f;  // 0.0-1.0, user configurable
bool  show_latency_guide = true;   // toggle visibility
```

**Note:** keep `UiState::transient_threshold` as the UI-facing value, but mirror it into
`UiAtomicSnapshot::transient_threshold` each UI frame so the audio thread reads atomics only.

---

## Phase 2: UI Widget

### 2.1 Create New UI Component

**New file:** `src/ui/ui_latency_guide.h`
```cpp
#pragma once

namespace jamwide {
class JamWidePlugin;
}

void ui_render_latency_guide(jamwide::JamWidePlugin* plugin);
```

**New file:** `src/ui/ui_latency_guide.cpp`

**Visual Design:**
```
┌─────────────────────────────────────────────────────────────┐
│  Timing Guide                              [Threshold: 0.3] │
│  ┌─────────────────────────────────────────────────────────┐│
│  │    1    │    2    │    3    │    4    │                 ││
│  │    |    │    |    │    |    │    |    │  ← beat centers ││
│  │   ●●●   │         │         │         │  ← your hits    ││
│  └─────────────────────────────────────────────────────────┘│
│  Avg offset: +8ms (slightly early)  ██████░░░░              │
└─────────────────────────────────────────────────────────────┘
```

**Features:**
- Horizontal bar divided into BPI beats (from `ui_snapshot.bpi`)
- Beat center lines as vertical markers
- Detected transients shown as dots relative to beat center
- Rolling history (last 8-16 transients) for pattern visualization
- Numeric offset display in milliseconds
- Configurable threshold slider

### 2.2 Integration Point

**File:** `src/ui/ui_local.cpp`

At end of `ui_render_local_channel()`, add:
```cpp
// Only show when connected
if (state.status == NJClient::NJC_STATUS_OK) {
    ui_render_latency_guide(plugin);
}
```

---

## Phase 3: History & Averaging

### 3.1 Transient History Buffer

**File:** `src/ui/ui_latency_guide.cpp`

```cpp
namespace {
    // Ring buffer for transient history
    constexpr int kHistorySize = 16;
    float transient_history[kHistorySize] = {0};
    int history_index = 0;
    int history_count = 0;
}
```

**Calculations:**
- Store beat offset for each detected transient
- Calculate running average offset
- Calculate standard deviation (consistency indicator)

### 3.2 Display Modes

The widget shows:
1. **Dot cluster**: Visual scatter of recent transients around beat center
2. **Average indicator**: Single marker showing mean position
3. **Numeric readout**: Offset in milliseconds

---

## Phase 4: User Guidance

### 4.1 Offset Calculation

Convert beat fraction to milliseconds:
```cpp
float ms_per_beat = 60000.0f / bpm;
float offset_ms = beat_offset * ms_per_beat;  // beat_offset is -0.5 to +0.5
```

### 4.2 Visual Feedback Colors

| Offset | Color | Message |
|--------|-------|---------|
| ±5ms | Green | "On beat" |
| ±5-15ms | Yellow | "Slightly early/late" |
| ±15ms+ | Red | "Adjust timing or DAW latency" |

### 4.3 Threshold Slider

- Range: 0.05 to 0.8 (linear)
- Default: 0.3
- Tooltip: "Lower = more sensitive, Higher = only loud transients"

---

## File Changes Summary

| File | Change Type | Description |
|------|-------------|-------------|
| `src/ui/ui_state.h` | Modify | Add transient atomics + threshold setting |
| `src/ui/ui_latency_guide.h` | **New** | Header for guide widget |
| `src/ui/ui_latency_guide.cpp` | **New** | Implementation (~150-200 lines) |
| `src/ui/ui_local.cpp` | Modify | Add render call when connected |
| `src/plugin/clap_entry.cpp` | Modify | Add transient detection in audio loop |
| `src/plugin/jamwide_plugin.h` | Modify | Add transient detector state |
| `CMakeLists.txt` | Modify | Add new source files to ninjam-ui target |

---

## Implementation Order

### Step 1: State Additions
- Add atomics to `UiAtomicSnapshot` in `ui_state.h`
- Add threshold setting to `UiState`

### Step 2: Basic UI Widget  
- Create `ui_latency_guide.h` and `ui_latency_guide.cpp`
- Render empty beat grid based on BPI
- Add threshold slider
- Integrate into `ui_local.cpp`

### Step 3: Transient Detection
- Add simple peak detection in `plugin_process()` in `src/plugin/clap_entry.cpp`
- Calculate beat-relative offset
- Write to atomic state
### Step 3.1: Beat Phase Smoothing
- Maintain `beat_phase` continuously in the audio thread (advance per sample).
- Resync to `snapshot_phase` only when drift > 0.1 beats.
- Apply small drift correction otherwise to avoid jitter.

### Step 4: Wire Detection to UI
- Read transient flag in UI
- Add to history buffer
- Render dots on beat grid

### Step 5: History & Statistics
- Implement averaging
- Calculate millisecond offset
- Show guidance text/colors

### Step 6: Polish
- Fine-tune colors and sizing
- Add enable/disable toggle
- Test with different BPM/BPI settings

---

## Estimated Effort

| Phase | Time | Notes |
|-------|------|-------|
| Step 1 | 15 min | Simple state additions |
| Step 2 | 1.5 hr | ImGui widget with beat grid |
| Step 3 | 45 min | Audio thread detection |
| Step 4 | 30 min | Atomic communication |
| Step 5 | 45 min | Statistics and display |
| Step 6 | 30 min | Polish and testing |

**Total: ~4 hours**

---

## Testing Checklist

- [ ] Widget appears in Local Channel section when connected
- [ ] Widget hidden when disconnected
- [ ] Beat grid correctly reflects current BPI
- [ ] Transients detected at various threshold levels
- [ ] History displays correctly and scrolls
- [ ] Offset calculation accurate (verify with known latency)
- [ ] Colors change appropriately based on offset
- [ ] No audio glitches from detection code
- [ ] Performance acceptable (< 1% CPU overhead)
