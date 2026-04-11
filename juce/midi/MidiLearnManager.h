#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <functional>

class MidiLearnManager
{
public:
    void startLearning(const juce::String& paramId,
                       std::function<void(int cc, int ch)> onLearned);
    bool isLearning() const;
    juce::String getLearningParamId() const;
    // Returns true if learning was completed (CC assigned)
    bool tryLearn(int ccNumber, int midiChannel);
    void cancelLearning();

private:
    std::atomic<bool> learning_{false};
    juce::String learningParamId_;
    std::function<void(int, int)> onLearnedCallback_;
};
