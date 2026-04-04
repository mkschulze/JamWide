#include "JamWideJuceProcessor.h"
#include "JamWideJuceEditor.h"
#include "NinjamRunThread.h"
#include "core/njclient.h"
#include "ui/ui_state.h"

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

    // Local channel parameters (4 channels x 3 params = 12 new params)
    for (int ch = 0; ch < 4; ++ch)
    {
        juce::String suffix = juce::String(ch);

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"localVol_" + suffix, 1},
            "Local Ch" + juce::String(ch + 1) + " Volume",
            juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"localPan_" + suffix, 1},
            "Local Ch" + juce::String(ch + 1) + " Pan",
            juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));

        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{"localMute_" + suffix, 1},
            "Local Ch" + juce::String(ch + 1) + " Mute",
            false));
    }

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

        // Unblock any pending license wait before stopping thread
        license_response.store(-1, std::memory_order_release);
        license_cv.notify_one();

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
        // Collect inputs from all active stereo input buses
        // BusesProperties declares 4 stereo inputs: Local 1..4
        // NJClient::AudioProc expects mono channel pointers
        const int numInputBuses = getBusCount(true);
        const int maxInputChannels = numInputBuses * 2;  // up to 8 mono channels

        // Size scratch buffer for all input channels
        inputScratch.setSize(maxInputChannels, numSamples, false, false, true);

        int numInputChannels = 0;
        for (int bus = 0; bus < numInputBuses; ++bus)
        {
            auto* inputBus = getBus(true, bus);
            if (inputBus == nullptr || !inputBus->isEnabled())
            {
                // Disabled bus: zero its channels in scratch
                inputScratch.clear(bus * 2,     0, numSamples);
                inputScratch.clear(bus * 2 + 1, 0, numSamples);
                numInputChannels += 2;  // Still count them for NJClient channel mapping
                continue;
            }
            // Get the buffer range for this input bus
            auto busBuffer = getBusBuffer(buffer, true, bus);
            int busChannels = busBuffer.getNumChannels();
            for (int ch = 0; ch < 2; ++ch)
            {
                int scratchCh = bus * 2 + ch;
                if (ch < busChannels)
                    inputScratch.copyFrom(scratchCh, 0, busBuffer, ch, 0, numSamples);
                else
                    inputScratch.clear(scratchCh, 0, numSamples);
            }
            numInputChannels += 2;
        }

        // Build input pointer array for AudioProc
        float* inPtrs[8] = {};
        for (int ch = 0; ch < numInputChannels && ch < 8; ++ch)
            inPtrs[ch] = inputScratch.getWritePointer(ch);

        // Output remains stereo on bus 0 (Phase 6 adds multichannel output)
        float* outPtrs[2] = {
            buffer.getWritePointer(0),
            totalChannels > 1 ? buffer.getWritePointer(1) : buffer.getWritePointer(0)
        };

        client->AudioProc(inPtrs, numInputChannels, outPtrs, 2, numSamples,
                          static_cast<int>(storedSampleRate));

        // Master VU (after AudioProc writes output)
        float masterPeakL = buffer.getMagnitude(0, 0, numSamples);
        float masterPeakR = totalChannels > 1 ? buffer.getMagnitude(1, 0, numSamples) : masterPeakL;
        uiSnapshot.master_vu_left.store(masterPeakL, std::memory_order_relaxed);
        uiSnapshot.master_vu_right.store(masterPeakR, std::memory_order_relaxed);

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
    // Start with APVTS state tree (contains all 16 parameter values)
    auto state = apvts.copyState();

    // Add state version for forward-compatible migration
    state.setProperty("stateVersion", currentStateVersion, nullptr);

    // Non-APVTS persistent state (D-22: server/username)
    state.setProperty("lastServer", lastServerAddress, nullptr);
    state.setProperty("lastUsername", lastUsername, nullptr);

    // Non-APVTS persistent state (D-23: UI prefs)
    // NOTE: These are NOT APVTS params per review concern -- stored as ValueTree
    // properties only. They will NOT appear in host automation lanes.
    state.setProperty("scaleFactor", static_cast<double>(scaleFactor), nullptr);
    state.setProperty("chatSidebarVisible", chatSidebarVisible, nullptr);

    // Local channel input selectors and transmit state (D-21, D-14, D-15)
    for (int ch = 0; ch < 4; ++ch)
    {
        state.setProperty("localCh" + juce::String(ch) + "Input",
                          localInputSelector[static_cast<size_t>(ch)], nullptr);
        state.setProperty("localCh" + juce::String(ch) + "Tx",
                          localTransmit[static_cast<size_t>(ch)], nullptr);
    }

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void JamWideJuceProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (!xml || !xml->hasTagName(apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml(*xml);
    int version = tree.getProperty("stateVersion", 0);
    juce::ignoreUnused(version);  // Future: migrate state based on version

    // STEP 1: Extract non-APVTS state BEFORE replaceState
    // (replaceState may strip unknown properties depending on JUCE version)
    lastServerAddress = tree.getProperty("lastServer", "ninbot.com").toString();
    lastUsername = tree.getProperty("lastUsername", "anonymous").toString();

    // Validate scaleFactor to one of the allowed values (1.0, 1.5, 2.0)
    double rawScale = tree.getProperty("scaleFactor", 1.0);
    if (rawScale >= 1.9)
        scaleFactor = 2.0f;
    else if (rawScale >= 1.4)
        scaleFactor = 1.5f;
    else
        scaleFactor = 1.0f;

    chatSidebarVisible = tree.getProperty("chatSidebarVisible", true);

    // Restore and validate local channel settings (D-21, D-14)
    for (int ch = 0; ch < 4; ++ch)
    {
        int rawInput = tree.getProperty(
            "localCh" + juce::String(ch) + "Input", ch);
        // Validate: clamp to valid range 0-3 (review concern: input bus validation)
        localInputSelector[static_cast<size_t>(ch)] = juce::jlimit(0, 3, rawInput);

        localTransmit[static_cast<size_t>(ch)] = tree.getProperty(
            "localCh" + juce::String(ch) + "Tx", ch == 0);
    }

    // STEP 2: Restore APVTS parameters
    // replaceState handles masterVol, masterMute, metroVol, metroMute,
    // localVol_0..3, localPan_0..3, localMute_0..3
    // Missing parameters get their defaults (forward-compatible, per pitfall 3)
    apvts.replaceState(tree);
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JamWideJuceProcessor();
}
