#pragma once
// Phase 20-01 — VideoEncoderListener: optional notification interface.
//
// Plan 20-03 (the JamWideJuceProcessor encoder owner) attaches a concrete
// listener that logs via juce::Logger::writeToLog on the message thread
// (D-23 pattern from Phase 19). All notification methods are invoked on
// the encoder thread; the listener implementation is responsible for
// marshalling to a UI / log thread if needed.
//
// All methods are virtual with empty default implementations so subclasses
// override only what they need. CONTEXT.md "Claude's Discretion: Debug
// logging surface" — planner picks granularity; default is open/close /
// reconfigure / fatal error / SPS-PPS-published.

namespace jamwide {

struct VideoEncoderConfig;  // forward — VideoEncoderConfig.h is a sibling header.

class VideoEncoderListener {
public:
    virtual ~VideoEncoderListener() = default;

    // Encoder successfully opened with the given config (libavcodec context
    // allocated, sws_scale context allocated, slab pool ready, encoder
    // thread started).
    virtual void onEncoderOpened(const VideoEncoderConfig& /*cfg*/) {}

    // Encoder closed cleanly (7-step teardown completed; all resources
    // freed; encoder thread joined).
    virtual void onEncoderClosed() {}

    // Encoder swapped libavcodec instance to a new config (preset change,
    // resolution change, or fatal-error self-heal). The Subscription
    // survives reconfigure — no frame gap. Called from the encoder thread
    // after the new libavcodec instance is open and SPS/PPS are republished.
    virtual void onEncoderReconfigured(const VideoEncoderConfig& /*cfg*/) {}

    // Encoder hit a fatal libavcodec error. Called from the encoder thread.
    // Plan 20-03's owner-attached listener triggers a reconfigure attempt
    // as a self-heal path (D-13 — encoder thread drains current instance,
    // opens new instance, publishes new SPS/PPS, continues; Subscription
    // preserved per R4 H9).
    virtual void onEncoderFatalError(const char* /*reason*/) {}

    // SPS/PPS bytes were published via the publishSpsPps callback. `len`
    // is the total raw [SPS-NAL][PPS-NAL] payload size in bytes.
    virtual void onSpsPpsPublished(int /*spsPpsLen*/) {}
};

} // namespace jamwide
