#include "JamWideJuceProcessor.h"
#include "JamWideJuceEditor.h"

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
void JamWideJuceProcessor::prepareToPlay(double /*sampleRate*/, int /*samplesPerBlock*/)
{
    // Phase 2: nothing to prepare yet
    // Phase 3 will set up NJClient sample rate and buffer size here
}

void JamWideJuceProcessor::releaseResources()
{
    // Phase 2: nothing to release
}

void JamWideJuceProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    // Phase 2: produce silence on all outputs
    // Phase 3 will integrate NJClient::AudioProc here
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());
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
