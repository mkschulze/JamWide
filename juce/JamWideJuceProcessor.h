#pragma once
#include <JuceHeader.h>
#include <memory>

class JamWideJuceEditor;
class NinjamRunThread;

class JamWideJuceProcessor : public juce::AudioProcessor
{
public:
    JamWideJuceProcessor();
    ~JamWideJuceProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static constexpr int currentStateVersion = 1;

    juce::AudioProcessorValueTreeState apvts;

private:
    std::unique_ptr<NinjamRunThread> runThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JamWideJuceProcessor)
};
