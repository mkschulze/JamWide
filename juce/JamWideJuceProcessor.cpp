#include "JamWideJuceProcessor.h"
#include "JamWideJuceEditor.h"
#include "NinjamRunThread.h"
#include "core/njclient.h"

#ifndef MAKE_NJ_FOURCC
#define MAKE_NJ_FOURCC(A,B,C,D) ((A) | ((B)<<8) | ((C)<<16) | ((D)<<24))
#endif

//==============================================================================
JamWideJuceProcessor::JamWideJuceProcessor()
    : AudioProcessor(BusesProperties()
        // 4 stereo inputs (local NINJAM channels)
        .withInput("Local 1",  juce::AudioChannelSet::stereo(), true)
        .withInput("Local 2",  juce::AudioChannelSet::stereo(), false)
        .withInput("Local 3",  juce::AudioChannelSet::stereo(), false)
        .withInput("Local 4",  juce::AudioChannelSet::stereo(), false)
        // 17 stereo outputs (main mix + 16 routing slots)
        .withOutput("Main Mix",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Remote 1",  juce::AudioChannelSet::stereo(), false)
        .withOutput("Remote 2",  juce::AudioChannelSet::stereo(), false)
        .withOutput("Remote 3",  juce::AudioChannelSet::stereo(), false)
        .withOutput("Remote 4",  juce::AudioChannelSet::stereo(), false)
        .withOutput("Remote 5",  juce::AudioChannelSet::stereo(), false)
        .withOutput("Remote 6",  juce::AudioChannelSet::stereo(), false)
        .withOutput("Remote 7",  juce::AudioChannelSet::stereo(), false)
        .withOutput("Remote 8",  juce::AudioChannelSet::stereo(), false)
        .withOutput("Remote 9",  juce::AudioChannelSet::stereo(), false)
        .withOutput("Remote 10", juce::AudioChannelSet::stereo(), false)
        .withOutput("Remote 11", juce::AudioChannelSet::stereo(), false)
        .withOutput("Remote 12", juce::AudioChannelSet::stereo(), false)
        .withOutput("Remote 13", juce::AudioChannelSet::stereo(), false)
        .withOutput("Remote 14", juce::AudioChannelSet::stereo(), false)
        .withOutput("Remote 15", juce::AudioChannelSet::stereo(), false)
        .withOutput("Remote 16", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    client = std::make_unique<NJClient>();

    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("JamWide");
    tempDir.createDirectory();
    client->SetWorkDir(const_cast<char*>(tempDir.getFullPathName().toRawUTF8()));

    client->SetEncoderFormat(MAKE_NJ_FOURCC('F','L','A','C'));
    client->config_autosubscribe = 1;
}

JamWideJuceProcessor::~JamWideJuceProcessor()
{
    // Order matters: runThread must stop before client is destroyed,
    // because the run loop accesses client via getClient().
    runThread.reset();
    client.reset();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
JamWideJuceProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"masterVol", 1}, "Master Volume",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"masterMute", 1}, "Master Mute", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"metroVol", 1}, "Metronome Volume",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"metroMute", 1}, "Metronome Mute", false));

    return { params.begin(), params.end() };
}

//==============================================================================
void JamWideJuceProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    storedSampleRate = sampleRate;

    // Start the NinjamRun thread for NJClient::Run() loop
    runThread = std::make_unique<NinjamRunThread>(*this);
    runThread->startThread(juce::Thread::Priority::normal);
}

void JamWideJuceProcessor::releaseResources()
{
    // Stop and destroy the NinjamRun thread cleanly
    if (runThread)
    {
        runThread->signalThreadShouldExit();
        runThread->stopThread(5000);
        runThread.reset();
    }
}

void JamWideJuceProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int totalChannels = buffer.getNumChannels();

    // Sync APVTS parameters to NJClient atomics
    if (client)
    {
        client->config_mastervolume.store(
            *apvts.getRawParameterValue("masterVol"),
            std::memory_order_relaxed);
        client->config_mastermute.store(
            *apvts.getRawParameterValue("masterMute") >= 0.5f,
            std::memory_order_relaxed);
        client->config_metronome.store(
            *apvts.getRawParameterValue("metroVol"),
            std::memory_order_relaxed);
        client->config_metronome_mute.store(
            *apvts.getRawParameterValue("metroMute") >= 0.5f,
            std::memory_order_relaxed);
    }

    // Call AudioProc when connected
    if (client && client->cached_status.load(std::memory_order_acquire)
                  == NJClient::NJC_STATUS_OK)
    {
        // In-place buffer safety: copy input to scratch before AudioProc
        // (AudioProc may write to outbuf which is the same buffer object)
        const int nch = juce::jmin(2, totalChannels);
        inputScratch.setSize(2, numSamples, false, false, true);
        for (int ch = 0; ch < nch; ++ch)
            inputScratch.copyFrom(ch, 0, buffer, ch, 0, numSamples);
        // Zero any scratch channels beyond what buffer provides
        for (int ch = nch; ch < 2; ++ch)
            inputScratch.clear(ch, 0, numSamples);

        float* inPtrs[2] = { inputScratch.getWritePointer(0),
                              inputScratch.getWritePointer(1) };
        float* outPtrs[2] = { buffer.getWritePointer(0),
                               totalChannels > 1 ? buffer.getWritePointer(1)
                                                 : buffer.getWritePointer(0) };

        client->AudioProc(inPtrs, 2, outPtrs, 2, numSamples,
                          static_cast<int>(storedSampleRate));

        // Clear any remaining output channels beyond stereo
        for (int ch = 2; ch < totalChannels; ++ch)
            buffer.clear(ch, 0, numSamples);

        return;
    }

    // Not connected: silence all outputs
    for (int ch = 0; ch < totalChannels; ++ch)
        buffer.clear(ch, 0, numSamples);
}

//==============================================================================
bool JamWideJuceProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Main output must be stereo
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Main input must be stereo
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // All other output buses must be stereo or disabled
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& bus = layouts.outputBuses[i];
        if (!bus.isDisabled() && bus != juce::AudioChannelSet::stereo())
            return false;
    }

    // All other input buses must be stereo or disabled
    for (int i = 1; i < layouts.inputBuses.size(); ++i)
    {
        const auto& bus = layouts.inputBuses[i];
        if (!bus.isDisabled() && bus != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

//==============================================================================
juce::AudioProcessorEditor* JamWideJuceProcessor::createEditor()
{
    return new JamWideJuceEditor(*this);
}

bool JamWideJuceProcessor::hasEditor() const { return true; }

//==============================================================================
const juce::String JamWideJuceProcessor::getName() const { return "JamWide JUCE"; }
bool JamWideJuceProcessor::acceptsMidi() const { return false; }
bool JamWideJuceProcessor::producesMidi() const { return false; }
bool JamWideJuceProcessor::isMidiEffect() const { return false; }
double JamWideJuceProcessor::getTailLengthSeconds() const { return 0.0; }

//==============================================================================
int JamWideJuceProcessor::getNumPrograms() { return 1; }
int JamWideJuceProcessor::getCurrentProgram() { return 0; }
void JamWideJuceProcessor::setCurrentProgram(int /*index*/) {}
const juce::String JamWideJuceProcessor::getProgramName(int /*index*/) { return {}; }
void JamWideJuceProcessor::changeProgramName(int /*index*/, const juce::String& /*newName*/) {}

//==============================================================================
void JamWideJuceProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("stateVersion", currentStateVersion, nullptr);
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void JamWideJuceProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
    {
        auto tree = juce::ValueTree::fromXml(*xml);
        int version = tree.getProperty("stateVersion", 0);
        // Future phases: migrate state based on version
        juce::ignoreUnused(version);
        apvts.replaceState(tree);
    }
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JamWideJuceProcessor();
}
