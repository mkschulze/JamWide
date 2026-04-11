#pragma once
#include <JuceHeader.h>
#include <functional>

class MidiMapper;
class MidiLearnManager;

/**
 * VbFader — Custom vertical fader component.
 *
 * Fully custom painted juce::Component (NOT a juce::Slider subclass).
 * Supports both APVTS-attached mode (local channels) and plain callback
 * mode (remote channels).
 *
 * Value range: 0.0 (silence/-inf) to 2.0 (+6 dB) linear.
 * Default: 1.0 (0 dB).
 */
class VbFader : public juce::Component
{
public:
    VbFader();
    ~VbFader() override;

    /** Set the fader value (linear 0.0 to 2.0). Clamps automatically. */
    void setValue(float newValue);

    /** Get the current fader value (linear). */
    float getValue() const { return value_; }

    /** Set the value that double-click resets to (default: 1.0 = 0 dB). */
    void setDefaultValue(float val) { defaultValue_ = val; }

    /** Callback for non-APVTS use (remote channels). */
    std::function<void(float)> onValueChanged;

    /** Attach to an APVTS parameter for local channels. */
    void attachToParameter(juce::RangedAudioParameter& param,
                           juce::UndoManager* um = nullptr);

    /**
     * Detach from the current parameter attachment.
     * CRITICAL: Must be called BEFORE destroying the VbFader when strips
     * are rebuilt, to ensure ParameterAttachment lifetime safety.
     */
    void detachFromParameter();

    /** Adjust value by a dB delta. Used by ChannelStrip scroll wheel forwarding. */
    void adjustByDb(float dbDelta);

    // MIDI Learn support (Phase 14 D-01, D-02)
    void setMidiLearnContext(MidiMapper* mapper, MidiLearnManager* learnMgr,
                             const juce::String& paramId);
    bool isMidiLearning() const { return midiLearning_; }

    // Component overrides
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e,
                        const juce::MouseWheelDetails& wheel) override;

private:
    /** Format a linear value as a dB string for display on the thumb. */
    static juce::String formatDb(float linear);

    /** Map linear value (0.0-2.0) to Y pixel coordinate. */
    float valueToY(float val) const;

    /** Map Y pixel coordinate to linear value (0.0-2.0). */
    float yToValue(int y) const;

    float value_ = 1.0f;
    float defaultValue_ = 1.0f;
    int dragStartY_ = 0;
    float dragStartValue_ = 0.0f;
    bool gestureActive_ = false;

    std::unique_ptr<juce::ParameterAttachment> attachment_;

    // MIDI Learn state (Phase 14)
    MidiMapper* midiMapper_ = nullptr;
    MidiLearnManager* midiLearnMgr_ = nullptr;
    juce::String midiParamId_;
    bool midiLearning_ = false;   // true when waiting for CC
    int learnedCc_ = -1;          // set on learn completion for display
    int learnedCh_ = -1;
    int64_t learnCompletedTime_ = 0;  // for auto-dismiss timing

public:
    static constexpr int kThumbDiameter = 44;

private:
    static constexpr int kTrackWidth = 10;
    static constexpr float kMinLinear = 0.0f;
    static constexpr float kMaxLinear = 2.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VbFader)
};
