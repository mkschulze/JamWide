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

    // Vorbis default for compatibility (most NINJAM clients use Vorbis)
    // User can switch to FLAC via codec selector in ConnectionBar
    client->config_autosubscribe = 1;

    // OSC server (created after NJClient because it takes a processor reference
    // that may call getClient()). Does NOT start automatically -- user enables via dialog.
    oscServer = std::make_unique<OscServer>(*this);

    // Video companion (created after NJClient, same ownership pattern as OscServer)
    videoCompanion = std::make_unique<jamwide::VideoCompanion>(*this);

    // MIDI mapper (created after NJClient and OscServer, same ownership pattern)
    midiMapper = std::make_unique<MidiMapper>(*this);
}

JamWideJuceProcessor::~JamWideJuceProcessor()
{
    // Order matters: midiMapper stops timer, videoCompanion stops WS server,
    // oscServer stops before runThread, runThread stops before client.
    midiMapper.reset();
    videoCompanion.reset();
    oscServer.reset();
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

    // Remote user group controls (D-14) -- 64 new parameters
    for (int i = 0; i < 16; ++i)
    {
        juce::String suffix = juce::String(i);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"remoteVol_" + suffix, 3},
            "Remote " + juce::String(i + 1) + " Volume",
            juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"remotePan_" + suffix, 3},
            "Remote " + juce::String(i + 1) + " Pan",
            juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{"remoteMute_" + suffix, 3},
            "Remote " + juce::String(i + 1) + " Mute", false));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{"remoteSolo_" + suffix, 3},
            "Remote " + juce::String(i + 1) + " Solo", false));
    }

    // Local solo (D-15) -- 4 new parameters
    for (int ch = 0; ch < 4; ++ch)
    {
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{"localSolo_" + juce::String(ch), 3},
            "Local Ch" + juce::String(ch + 1) + " Solo", false));
    }

    // Metro pan (D-16) -- 1 new parameter
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"metroPan", 3},
        "Metronome Pan",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
void JamWideJuceProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    storedSampleRate = sampleRate;

    // Pre-allocate output scratch buffer for 17 stereo pairs (34 channels)
    // REVIEW FIX: Pre-allocate here, NOT in processBlock, to avoid audio-thread allocation.
    // Use samplesPerBlock from the host. AudioProc zeros the buffer internally.
    outputScratch.setSize(kTotalOutChannels, samplesPerBlock, false, true, false);

    // Initialize standalone MIDI collector with host sample rate
    if (midiMapper)
        midiMapper->setSampleRate(sampleRate, samplesPerBlock);

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

//==============================================================================
// processBlock helpers (audio-thread safe, no allocations)
//==============================================================================

void JamWideJuceProcessor::syncApvtsToAtomics()
{
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
}

int JamWideJuceProcessor::collectInputChannels(juce::AudioBuffer<float>& buffer,
                                                float* inPtrs[], int numSamples)
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
    for (int ch = 0; ch < numInputChannels && ch < 8; ++ch)
        inPtrs[ch] = inputScratch.getWritePointer(ch);

    return numInputChannels;
}

bool JamWideJuceProcessor::handleTransportSync(int numSamples)
{
    // --- DAW Transport Sync (D-01, D-02, SYNC-01, SYNC-02) ---
    bool hostPlaying = true;  // Default: playing (standalone behavior per D-16)

    if (auto* playHead = getPlayHead())
    {
        if (auto pos = playHead->getPosition())
        {
            bool rawPlaying = pos->getIsPlaying();

            // Cache host BPM for sync validation on UI thread
            if (auto bpm = pos->getBpm())
                cachedHostBpm_.store(static_cast<float>(*bpm),
                                    std::memory_order_relaxed);

            // Edge detection using RAW transport state (not overridden value)
            // Addresses Claude review: wasPlaying_ was set to overridden hostPlaying
            // which could cause spurious edge after WAITING->ACTIVE in same block.
            bool transportJustStarted = rawPlaying && !rawHostPlaying_;
            rawHostPlaying_ = rawPlaying;

            hostPlaying = rawPlaying;

            // --- Seek/loop discontinuity detection ---
            // Addresses Codex HIGH concern: no handling for loop wrap, seek,
            // punch-in, tempo automation, sample-position reset.
            double ppqPos = 0.0;
            bool hasPpq = false;
            if (auto ppq = pos->getPpqPosition())
            {
                ppqPos = *ppq;
                hasPpq = true;
            }

            // Detect seek: large PPQ jump (> 1 beat) that is not forward-continuous
            // A seek while sync is ACTIVE invalidates the alignment
            if (hasPpq && rawPlaying)
            {
                double ppqDelta = ppqPos - prevPpqPos_;
                double expectedDelta = (numSamples / storedSampleRate) *
                    (cachedHostBpm_.load(std::memory_order_relaxed) / 60.0);

                // If PPQ moved backward or jumped more than 2x expected, it's a seek/loop
                if (ppqDelta < -0.01 || ppqDelta > expectedDelta * 2.0 + 1.0)
                {
                    int expected = kSyncActive;
                    if (syncState_.compare_exchange_strong(expected, kSyncIdle,
                            std::memory_order_acq_rel))
                    {
                        // Sync was active, now auto-disabled due to seek
                        evt_queue.try_push(jamwide::SyncStateChangedEvent{
                            kSyncIdle, jamwide::SyncReason::TransportSeek});
                    }
                }
            }
            if (hasPpq)
                prevPpqPos_ = ppqPos;

            // --- Sync state machine: WAITING -> ACTIVE transition ---
            int currentSync = syncState_.load(std::memory_order_acquire);
            if (currentSync == kSyncWaiting)
            {
                // Trigger sync on transport start OR if transport was already
                // playing when Sync was clicked (first WAITING processBlock)
                bool justEnteredWaiting = (prevSyncState_ != kSyncWaiting);
                if (transportJustStarted || (justEnteredWaiting && rawPlaying))
                {
                    // Calculate PPQ sync offset (JamTaba algorithm adapted for JUCE)
                    double hostBpm = cachedHostBpm_.load(std::memory_order_relaxed);
                    double barStart = 0.0;
                    int timeSigNum = 4;

                    if (auto bar = pos->getPpqPositionOfLastBarStart()) barStart = *bar;
                    if (auto sig = pos->getTimeSignature()) timeSigNum = sig->numerator;

                    if (hostBpm > 0.0)
                    {
                        double samplesPerBeat = (60.0 * storedSampleRate) / hostBpm;
                        int startPosition = 0;

                        if (ppqPos > 0.0)
                        {
                            double cursorInMeasure = ppqPos - barStart;
                            if (cursorInMeasure > 0.00000001)  // Float tolerance (Reaper workaround)
                            {
                                double samplesUntilNextMeasure =
                                    (timeSigNum - cursorInMeasure) * samplesPerBeat;
                                startPosition = -static_cast<int>(samplesUntilNextMeasure);
                            }
                        }
                        else
                        {
                            startPosition = static_cast<int>(ppqPos * samplesPerBeat);
                        }

                        // Apply offset to NJClient interval position
                        int iPos, iLen;
                        client->GetPosition(&iPos, &iLen);
                        if (iLen > 0)
                        {
                            int newPos;
                            if (startPosition >= 0)
                                newPos = startPosition % iLen;
                            else
                                newPos = iLen - std::abs(startPosition % iLen);
                            client->SetIntervalPosition(newPos);
                        }
                    }

                    // Atomic transition: WAITING -> ACTIVE (race-safe)
                    // If run thread already set to IDLE (BPM change), this fails safely
                    int expectedWaiting = kSyncWaiting;
                    if (syncState_.compare_exchange_strong(expectedWaiting, kSyncActive,
                            std::memory_order_acq_rel))
                    {
                        evt_queue.try_push(jamwide::SyncStateChangedEvent{
                            kSyncActive, jamwide::SyncReason::TransportStarted});
                    }
                }

                // While WAITING, send silence (D-04)
                hostPlaying = false;
            }
            prevSyncState_ = currentSync;
        }
    }

    return hostPlaying;
}

void JamWideJuceProcessor::accumulateBusesToMainMix(float* outPtrs[], int numSamples)
{
    // Accumulate individual buses into main mix (D-08: Main Mix always has everything)
    // NJClient applies master volume to outbuf[0..1] only (channels 0-1).
    // Individual remote buses need master vol applied when accumulated into main mix.
    // REVIEW FIX: Metronome bus (kMetronomeBus) is accumulated WITHOUT master volume.
    // In original NJClient, metronome is mixed into outbuf[0..1] AFTER master vol is applied,
    // so it is independent of master volume. Preserve this behavior.
    float masterVol = client->config_mastermute.load(std::memory_order_relaxed)
                      ? 0.0f
                      : client->config_mastervolume.load(std::memory_order_relaxed);
    float masterPan = client->config_masterpan.load(std::memory_order_relaxed);
    float mvL = masterVol, mvR = masterVol;
    if (masterPan > 0.0f) mvL *= 1.0f - masterPan;
    else if (masterPan < 0.0f) mvR *= 1.0f + masterPan;

    float* mainL = outPtrs[0];
    float* mainR = outPtrs[1];

    // Accumulate remote user buses (1 through kMetronomeBus-1) WITH master volume
    for (int bus = 1; bus < kMetronomeBus; ++bus)
    {
        const float* busL = outPtrs[bus * 2];
        const float* busR = outPtrs[bus * 2 + 1];
        for (int s = 0; s < numSamples; ++s)
        {
            mainL[s] += busL[s] * mvL;
            mainR[s] += busR[s] * mvR;
        }
    }

    // Accumulate metronome bus WITHOUT master volume (preserves original NJClient behavior)
    {
        const float* metroL = outPtrs[kMetronomeBus * 2];
        const float* metroR = outPtrs[kMetronomeBus * 2 + 1];
        for (int s = 0; s < numSamples; ++s)
        {
            mainL[s] += metroL[s];
            mainR[s] += metroR[s];
        }
    }
}

void JamWideJuceProcessor::routeOutputsToJuceBuses(juce::AudioBuffer<float>& buffer,
                                                    int numSamples)
{
    // Copy outputScratch to JUCE output buses (per Pitfall 1: check isEnabled)
    const int numOutputBuses = getBusCount(false);
    for (int bus = 0; bus < numOutputBuses && bus < kNumOutputBuses; ++bus)
    {
        auto* outputBus = getBus(false, bus);
        if (outputBus == nullptr || !outputBus->isEnabled())
            continue;
        auto busBuffer = getBusBuffer(buffer, false, bus);
        int busCh = busBuffer.getNumChannels();
        if (busCh >= 1)
            busBuffer.copyFrom(0, 0, outputScratch, bus * 2, 0, numSamples);
        if (busCh >= 2)
            busBuffer.copyFrom(1, 0, outputScratch, bus * 2 + 1, 0, numSamples);
    }
}

void JamWideJuceProcessor::measureMasterVu(int numSamples)
{
    // Master VU from main mix (post-accumulation for accurate measurement)
    float masterPeakL = outputScratch.getMagnitude(0, 0, numSamples);
    float masterPeakR = outputScratch.getMagnitude(1, 0, numSamples);
    uiSnapshot.master_vu_left.store(masterPeakL, std::memory_order_relaxed);
    uiSnapshot.master_vu_right.store(masterPeakR, std::memory_order_relaxed);
}

//==============================================================================
void JamWideJuceProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    syncApvtsToAtomics();

    // Call AudioProc when connected
    if (client && client->cached_status.load(std::memory_order_acquire)
                  == NJClient::NJC_STATUS_OK)
    {
        float* inPtrs[8] = {};
        int numInputChannels = collectInputChannels(buffer, inPtrs, numSamples);

        // Expanded output: 17 stereo pairs = 34 mono channels
        // REVIEW FIX: outputScratch pre-allocated in prepareToPlay(). If host sends a larger
        // block than expected, resize here with avoidReallocating=true as a safety net.
        if (outputScratch.getNumSamples() < numSamples)
            outputScratch.setSize(kTotalOutChannels, numSamples, false, false, true);
        outputScratch.clear();

        float* outPtrs[kTotalOutChannels];
        for (int ch = 0; ch < kTotalOutChannels; ++ch)
            outPtrs[ch] = outputScratch.getWritePointer(ch);

        bool hostPlaying = handleTransportSync(numSamples);
        wasPlaying_ = hostPlaying;

        client->AudioProc(inPtrs, numInputChannels, outPtrs, kTotalOutChannels,
                          numSamples, static_cast<int>(storedSampleRate),
                          false, hostPlaying);

        accumulateBusesToMainMix(outPtrs, numSamples);
        routeOutputsToJuceBuses(buffer, numSamples);
        measureMasterVu(numSamples);
    }
    else
    {
        // Not connected: silence all outputs
        const int totalChannels = buffer.getNumChannels();
        for (int ch = 0; ch < totalChannels; ++ch)
            buffer.clear(ch, 0, numSamples);
    }

    // MIDI CC processing runs unconditionally (not just when connected)
    // so MIDI Learn and CC control work even without a NINJAM session.
    // Order: processIncomingMidi FIRST, appendFeedbackMidi SECOND (per research Pitfall 4/5)
    if (midiMapper)
    {
        midiMapper->processIncomingMidi(midiMessages, numSamples);
        midiMapper->appendFeedbackMidi(midiMessages);
    }
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
bool JamWideJuceProcessor::acceptsMidi() const { return true; }
bool JamWideJuceProcessor::producesMidi() const { return true; }
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
    state.setProperty("infoStripVisible", infoStripVisible, nullptr);

    // Local channel input selectors and transmit state (D-21, D-14, D-15)
    for (int ch = 0; ch < 4; ++ch)
    {
        state.setProperty("localCh" + juce::String(ch) + "Input",
                          localInputSelector[static_cast<size_t>(ch)], nullptr);
        state.setProperty("localCh" + juce::String(ch) + "Tx",
                          localTransmit[static_cast<size_t>(ch)], nullptr);
    }

    // Routing mode (per D-12: persist mode, not individual assignments)
    state.setProperty("routingMode", routingMode.load(std::memory_order_relaxed), nullptr);

    // OSC config persistence (D-21: state version 2)
    state.setProperty("oscEnabled", oscEnabled, nullptr);
    state.setProperty("oscReceivePort", oscReceivePort, nullptr);
    state.setProperty("oscSendIP", oscSendIP, nullptr);
    state.setProperty("oscSendPort", oscSendPort, nullptr);

    // MIDI mapping persistence (state version 3)
    if (midiMapper)
        midiMapper->saveToState(state);

    // MIDI standalone device persistence (stable identifiers per review feedback)
    // Read from midiMapper (source of truth) rather than processor fields
    state.setProperty("midiInputDeviceId",
                      midiMapper ? midiMapper->getInputDeviceId() : juce::String(), nullptr);
    state.setProperty("midiOutputDeviceId",
                      midiMapper ? midiMapper->getOutputDeviceId() : juce::String(), nullptr);

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
    juce::ignoreUnused(version);  // v1->v2 migration: OSC fields default gracefully

    // STEP 1: Extract non-APVTS state BEFORE replaceState
    // (replaceState may strip unknown properties depending on JUCE version)
    lastServerAddress = tree.getProperty("lastServer", "ninbot.com").toString();
    lastUsername = tree.getProperty("lastUsername", "anonymous").toString();

    // Always default to 1.0x on plugin load — persisted scale caused UI/label mismatch
    scaleFactor = 1.0f;

    chatSidebarVisible = tree.getProperty("chatSidebarVisible", true);

    if (tree.hasProperty("infoStripVisible"))
        infoStripVisible = static_cast<bool>(tree.getProperty("infoStripVisible"));

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

    // Routing mode (per D-13: default Manual on first load)
    int rawRoutingMode = tree.getProperty("routingMode", 0);
    routingMode.store(juce::jlimit(0, 2, rawRoutingMode), std::memory_order_relaxed);

    // OSC config (version 2 fields -- default to disabled with standard ports for v1 states)
    oscEnabled = static_cast<bool>(tree.getProperty("oscEnabled", false));
    oscReceivePort = static_cast<int>(tree.getProperty("oscReceivePort", 9000));
    oscSendIP = tree.getProperty("oscSendIP", "127.0.0.1").toString();
    oscSendPort = static_cast<int>(tree.getProperty("oscSendPort", 9001));

    // Validate port ranges (per T-09-05 input validation)
    oscReceivePort = juce::jlimit(1, 65535, oscReceivePort);
    oscSendPort = juce::jlimit(1, 65535, oscSendPort);

    // If OSC was enabled when saved, restart it
    if (oscEnabled && oscServer)
    {
        if (!oscServer->start(oscReceivePort, oscSendIP, oscSendPort))
        {
            // Port bind failed on restore — keep oscEnabled true so the status dot
            // shows red (error) instead of grey (disabled), signaling to the user
            // that OSC was supposed to be on but couldn't start
        }
    }

    // STEP 2: Restore APVTS parameters
    // replaceState handles masterVol, masterMute, metroVol, metroMute,
    // localVol_0..3, localPan_0..3, localMute_0..3
    // Missing parameters get their defaults (forward-compatible, per pitfall 3)
    apvts.replaceState(tree);

    // STEP 3: Restore MIDI mappings (state version 3)
    // Must happen after replaceState so APVTS parameters exist for validation
    if (midiMapper)
        midiMapper->loadFromState(tree);

    // STEP 4: Restore MIDI standalone device selection (stable identifiers)
    midiInputDeviceId = tree.getProperty("midiInputDeviceId", "").toString();
    midiOutputDeviceId = tree.getProperty("midiOutputDeviceId", "").toString();
    if (midiMapper && midiInputDeviceId.isNotEmpty())
        midiMapper->openMidiInput(midiInputDeviceId);
    if (midiMapper && midiOutputDeviceId.isNotEmpty())
        midiMapper->openMidiOutput(midiOutputDeviceId);
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JamWideJuceProcessor();
}
