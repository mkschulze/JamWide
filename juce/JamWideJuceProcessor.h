#pragma once
#include <JuceHeader.h>
#include <memory>

#include "threading/spsc_ring.h"
#include "threading/ui_command.h"

class JamWideJuceEditor;
class NinjamRunThread;
class NJClient;

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

    NJClient* getClient() { return client.get(); }
    juce::CriticalSection& getClientLock() { return clientLock; }

    juce::AudioProcessorValueTreeState apvts;
    jamwide::SpscRing<jamwide::UiCommand, 256> cmd_queue;

private:
    std::unique_ptr<NJClient> client;
    juce::CriticalSection clientLock;
    juce::AudioBuffer<float> inputScratch;
    double storedSampleRate = 48000.0;

    std::unique_ptr<NinjamRunThread> runThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JamWideJuceProcessor)
};
