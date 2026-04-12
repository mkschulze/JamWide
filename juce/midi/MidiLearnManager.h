#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include "MidiTypes.h"

class MidiLearnManager
{
public:
    void startLearning(const juce::String& paramId,
                       std::function<void(int number, int ch, MidiMsgType type)> onLearned);
    bool isLearning() const;
    juce::String getLearningParamId() const;
    // Returns true if learning was completed (CC or Note assigned)
    bool tryLearn(int number, int midiChannel, MidiMsgType type);
    void cancelLearning();

private:
    std::atomic<bool> learning_{false};
    juce::String learningParamId_;
    std::function<void(int, int, MidiMsgType)> onLearnedCallback_;
};
