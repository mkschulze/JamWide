#include "midi/MidiLearnManager.h"

void MidiLearnManager::startLearning(const juce::String& paramId,
                                      std::function<void(int cc, int ch)> onLearned)
{
    learningParamId_ = paramId;
    onLearnedCallback_ = std::move(onLearned);
    learning_.store(true, std::memory_order_release);
}

bool MidiLearnManager::isLearning() const
{
    return learning_.load(std::memory_order_acquire);
}

juce::String MidiLearnManager::getLearningParamId() const
{
    return learningParamId_;
}

bool MidiLearnManager::tryLearn(int ccNumber, int midiChannel)
{
    if (!learning_.load(std::memory_order_acquire))
        return false;

    learning_.store(false, std::memory_order_release);

    if (onLearnedCallback_)
    {
        onLearnedCallback_(ccNumber, midiChannel);
        onLearnedCallback_ = nullptr;
    }

    learningParamId_.clear();
    return true;
}

void MidiLearnManager::cancelLearning()
{
    learning_.store(false, std::memory_order_release);
    onLearnedCallback_ = nullptr;
    learningParamId_.clear();
}
