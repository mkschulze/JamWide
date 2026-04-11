#include "midi/MidiMapper.h"
#include "JamWideJuceProcessor.h"
#include "threading/ui_command.h"
#include "core/njclient.h"

MidiMapper::MidiMapper(JamWideJuceProcessor& proc)
    : processor(proc)
{
    // Initialize published_ with empty MappingTable
    published_ = std::make_shared<const MappingTable>();

    // Initialize sync tracking to defaults
    lastSyncedRemoteVol_.fill(1.0f);   // default volume
    lastSyncedRemotePan_.fill(0.0f);   // center pan
    lastSyncedRemoteMute_.fill(false);  // unmuted
    lastSyncedLocalSolo_.fill(false);
    lastSyncedMetroPan_ = 0.0f;

    // Start timer at 20ms for responsive mixer control
    startTimer(20);
}

MidiMapper::~MidiMapper()
{
    stopTimer();
    closeMidiInput();
    closeMidiOutput();
}

//==============================================================================
// Audio thread: parse incoming CC

void MidiMapper::processIncomingMidi(const juce::MidiBuffer& buffer)
{
    auto table = std::atomic_load(&published_);
    bool hasTable = table && !table->ccToParam.empty();
    bool isLearning = processor.midiLearnManager.isLearning();

    // Early return only if no mappings AND not learning
    if (!hasTable && !isLearning)
        return;

    for (const auto metadata : buffer)
    {
        auto msg = metadata.getMessage();
        if (!msg.isController())
            continue;

        int cc = msg.getControllerNumber();
        int ch = msg.getChannel();  // 1-based in JUCE
        int value = msg.getControllerValue();

        // Validate (T-14-01)
        if (!isValidCc(cc) || !isValidChannel(ch))
            continue;

        // Update activity tracking
        receivingMidi_.store(true, std::memory_order_relaxed);
        lastMidiReceivedTime_.store(juce::Time::currentTimeMillis(),
                                     std::memory_order_relaxed);

        // Check MIDI Learn (must check BEFORE mapping lookup, so learn works with zero mappings)
        if (processor.midiLearnManager.isLearning())
        {
            if (processor.midiLearnManager.tryLearn(cc, ch))
                continue;  // Consumed by learn, skip normal dispatch
        }

        if (!hasTable)
            continue;

        int key = makeKey(cc, ch);
        auto it = table->ccToParam.find(key);
        if (it == table->ccToParam.end())
            continue;

        const auto& mapping = it->second;
        auto* param = processor.apvts.getParameter(mapping.paramId);
        if (param == nullptr)
            continue;

        // For AudioParameterBool (mute/solo per D-08): toggle on value > 0, ignore value == 0
        if (dynamic_cast<juce::AudioParameterBool*>(param))
        {
            if (value > 0)
                param->setValueNotifyingHost(param->getValue() >= 0.5f ? 0.0f : 1.0f);
        }
        else
        {
            // For float params (vol/pan per D-10): CC 0-127 -> 0.0-1.0 normalized
            param->setValueNotifyingHost(static_cast<float>(value) / 127.0f);
        }

        // Set per-mapping echo suppression (suppress current + next feedback cycle)
        echoSuppression_[key] = 2;
    }
}

//==============================================================================
// Audio thread: append CC feedback

void MidiMapper::appendFeedbackMidi(juce::MidiBuffer& buffer)
{
    auto table = std::atomic_load(&published_);
    if (!table || table->ccToParam.empty())
        return;

    for (const auto& [key, mapping] : table->ccToParam)
    {
        // Per-mapping echo suppression check
        auto suppIt = echoSuppression_.find(key);
        if (suppIt != echoSuppression_.end() && suppIt->second > 0)
        {
            suppIt->second--;
            continue;
        }

        auto* param = processor.apvts.getParameter(mapping.paramId);
        if (!param)
            continue;

        float currentNorm = param->getValue();
        int ccValue;

        // For bool params: 127 = on, 0 = off (per D-08 feedback)
        if (dynamic_cast<juce::AudioParameterBool*>(param))
            ccValue = (currentNorm >= 0.5f) ? 127 : 0;
        else
            ccValue = juce::roundToInt(currentNorm * 127.0f);

        // Dirty check: only send if changed
        if (ccValue != lastSentCcValues_[key])
        {
            lastSentCcValues_[key] = ccValue;
            int channel = (key >> 7) + 1;  // back to 1-based
            int cc = key & 0x7F;
            buffer.addEvent(
                juce::MidiMessage::controllerEvent(channel, cc, ccValue),
                0);  // sample offset 0
        }
    }
}

//==============================================================================
// Mapping management (message thread)

bool MidiMapper::addMapping(const juce::String& paramId, int ccNumber, int midiChannel)
{
    // Validate
    if (!isValidCc(ccNumber) || !isValidChannel(midiChannel))
        return false;
    if (paramId.isEmpty())
        return false;
    if (static_cast<int>(staging_.ccToParam.size()) >= kMaxMappings)
        return false;

    int key = makeKey(ccNumber, midiChannel);

    // Duplicate conflict handling: last-write-wins
    // If the CC+Ch key already maps to a different paramId, remove that old mapping
    auto ccIt = staging_.ccToParam.find(key);
    if (ccIt != staging_.ccToParam.end())
    {
        // Remove old paramId -> key reverse mapping
        staging_.paramToCc.erase(ccIt->second.paramId);
        staging_.ccToParam.erase(ccIt);
    }

    // If the paramId already has a mapping to a different CC+Ch, remove the old one
    auto paramIt = staging_.paramToCc.find(paramId);
    if (paramIt != staging_.paramToCc.end())
    {
        staging_.ccToParam.erase(paramIt->second);
        staging_.paramToCc.erase(paramIt);
    }

    // Add the new mapping
    staging_.ccToParam[key] = Mapping{paramId, ccNumber, midiChannel};
    staging_.paramToCc[paramId] = key;

    publishMappings();
    return true;
}

void MidiMapper::removeMapping(const juce::String& paramId)
{
    auto it = staging_.paramToCc.find(paramId);
    if (it != staging_.paramToCc.end())
    {
        staging_.ccToParam.erase(it->second);
        staging_.paramToCc.erase(it);
        publishMappings();
    }
}

void MidiMapper::removeMappingByCc(int ccNumber, int midiChannel)
{
    int key = makeKey(ccNumber, midiChannel);
    auto it = staging_.ccToParam.find(key);
    if (it != staging_.ccToParam.end())
    {
        staging_.paramToCc.erase(it->second.paramId);
        staging_.ccToParam.erase(it);
        publishMappings();
    }
}

void MidiMapper::clearAllMappings()
{
    staging_.ccToParam.clear();
    staging_.paramToCc.clear();
    echoSuppression_.clear();
    lastSentCcValues_.clear();
    publishMappings();
}

int MidiMapper::getMappingCount() const
{
    return static_cast<int>(staging_.ccToParam.size());
}

//==============================================================================
// Query

bool MidiMapper::hasMapping(const juce::String& paramId) const
{
    return staging_.paramToCc.find(paramId) != staging_.paramToCc.end();
}

bool MidiMapper::hasMappingForCc(int ccNumber, int midiChannel) const
{
    int key = makeKey(ccNumber, midiChannel);
    return staging_.ccToParam.find(key) != staging_.ccToParam.end();
}

std::vector<MidiMapper::MappingInfo> MidiMapper::getAllMappings() const
{
    std::vector<MappingInfo> result;
    result.reserve(staging_.ccToParam.size());
    for (const auto& [key, mapping] : staging_.ccToParam)
        result.push_back({mapping.paramId, mapping.ccNumber, mapping.midiChannel});
    return result;
}

std::optional<MidiMapper::MappingInfo> MidiMapper::getMappingForParam(const juce::String& paramId) const
{
    auto it = staging_.paramToCc.find(paramId);
    if (it == staging_.paramToCc.end())
        return std::nullopt;
    auto ccIt = staging_.ccToParam.find(it->second);
    if (ccIt == staging_.ccToParam.end())
        return std::nullopt;
    return MappingInfo{ccIt->second.paramId, ccIt->second.ccNumber, ccIt->second.midiChannel};
}

//==============================================================================
// State persistence

void MidiMapper::saveToState(juce::ValueTree& state) const
{
    auto midiMappings = juce::ValueTree("MidiMappings");
    for (const auto& [key, mapping] : staging_.ccToParam)
    {
        auto entry = juce::ValueTree("Mapping");
        entry.setProperty("paramId", mapping.paramId, nullptr);
        entry.setProperty("cc", mapping.ccNumber, nullptr);
        entry.setProperty("channel", mapping.midiChannel, nullptr);
        midiMappings.addChild(entry, -1, nullptr);
    }
    state.addChild(midiMappings, -1, nullptr);
}

void MidiMapper::loadFromState(const juce::ValueTree& state)
{
    auto midiMappings = state.getChildWithName("MidiMappings");
    if (!midiMappings.isValid())
        return;

    // Clear existing mappings
    staging_.ccToParam.clear();
    staging_.paramToCc.clear();

    for (int i = 0; i < midiMappings.getNumChildren(); ++i)
    {
        auto entry = midiMappings.getChild(i);
        juce::String paramId = entry.getProperty("paramId", "").toString();
        int rawCc = static_cast<int>(entry.getProperty("cc", -1));
        int rawCh = static_cast<int>(entry.getProperty("channel", 0));

        // Validate: reject entries with empty paramId
        if (paramId.isEmpty())
            continue;

        // Validate: reject entries with out-of-range values
        // cc must be 0-127, channel must be 1-16
        if (rawCc < 0 || rawCc > 127)
            continue;
        if (rawCh < 1 || rawCh > 16)
            continue;

        // Clamp to valid range (defensive)
        int cc = juce::jlimit(0, 127, rawCc);
        int ch = juce::jlimit(1, 16, rawCh);

        // Validate: paramId must exist in APVTS
        if (processor.apvts.getParameter(paramId) == nullptr)
            continue;

        // Check capacity
        if (static_cast<int>(staging_.ccToParam.size()) >= kMaxMappings)
            break;

        int key = makeKey(cc, ch);
        staging_.ccToParam[key] = Mapping{paramId, cc, ch};
        staging_.paramToCc[paramId] = key;
    }

    publishMappings();
}

//==============================================================================
// Publish (atomic swap for thread-safe audio access)

void MidiMapper::publishMappings()
{
    auto newTable = std::make_shared<const MappingTable>(staging_);
    std::atomic_store(&published_, newTable);
}

//==============================================================================
// Echo suppression API

void MidiMapper::setEchoSuppression(const juce::String& paramId)
{
    auto table = std::atomic_load(&published_);
    if (!table)
        return;
    auto it = table->paramToCc.find(paramId);
    if (it != table->paramToCc.end())
        echoSuppression_[it->second] = 2;
}

//==============================================================================
// Remote slot reset (per D-17)

void MidiMapper::resetRemoteSlotDefaults(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= 16)
        return;

    juce::String suffix = juce::String(slotIndex);
    if (auto* p = processor.apvts.getParameter("remoteVol_" + suffix))
        p->setValueNotifyingHost(p->convertTo0to1(1.0f));  // default volume
    if (auto* p = processor.apvts.getParameter("remotePan_" + suffix))
        p->setValueNotifyingHost(p->convertTo0to1(0.0f));  // center pan
    if (auto* p = processor.apvts.getParameter("remoteMute_" + suffix))
        p->setValueNotifyingHost(0.0f);  // unmuted
    if (auto* p = processor.apvts.getParameter("remoteSolo_" + suffix))
        p->setValueNotifyingHost(0.0f);  // unsoloed

    // Reset sync tracking so timer doesn't re-dispatch stale state
    lastSyncedRemoteVol_[static_cast<size_t>(slotIndex)] = 1.0f;
    lastSyncedRemotePan_[static_cast<size_t>(slotIndex)] = 0.0f;
    lastSyncedRemoteMute_[static_cast<size_t>(slotIndex)] = false;
}

//==============================================================================
// Standalone device management

void MidiMapper::openMidiInput(const juce::String& deviceId)
{
    closeMidiInput();

    auto devices = juce::MidiInput::getAvailableDevices();
    juce::MidiDeviceInfo found;
    bool deviceFound = false;

    // Try stable identifier first, fall back to name matching
    for (const auto& d : devices)
    {
        if (d.identifier == deviceId)
        {
            found = d;
            deviceFound = true;
            break;
        }
    }
    if (!deviceFound)
    {
        for (const auto& d : devices)
        {
            if (d.name == deviceId)
            {
                found = d;
                deviceFound = true;
                break;
            }
        }
    }

    if (!deviceFound)
    {
        deviceError_.store(true, std::memory_order_relaxed);
        return;
    }

    // Open with a MidiInputCallback
    // Note: In a real implementation, this would use a callback to feed
    // received messages into processIncomingMidi. For now, we open the device.
    midiInput_ = juce::MidiInput::openDevice(found.identifier, nullptr);
    if (midiInput_)
    {
        midiInput_->start();
        currentInputDeviceId_ = found.identifier;
        deviceError_.store(false, std::memory_order_relaxed);
    }
    else
    {
        currentInputDeviceId_.clear();
        deviceError_.store(true, std::memory_order_relaxed);
    }
}

void MidiMapper::openMidiOutput(const juce::String& deviceId)
{
    closeMidiOutput();

    auto devices = juce::MidiOutput::getAvailableDevices();
    juce::MidiDeviceInfo found;
    bool deviceFound = false;

    for (const auto& d : devices)
    {
        if (d.identifier == deviceId)
        {
            found = d;
            deviceFound = true;
            break;
        }
    }
    if (!deviceFound)
    {
        for (const auto& d : devices)
        {
            if (d.name == deviceId)
            {
                found = d;
                deviceFound = true;
                break;
            }
        }
    }

    if (!deviceFound)
    {
        deviceError_.store(true, std::memory_order_relaxed);
        return;
    }

    midiOutput_ = juce::MidiOutput::openDevice(found.identifier);
    if (midiOutput_)
    {
        currentOutputDeviceId_ = found.identifier;
        deviceError_.store(false, std::memory_order_relaxed);
    }
    else
    {
        currentOutputDeviceId_.clear();
        deviceError_.store(true, std::memory_order_relaxed);
    }
}

void MidiMapper::closeMidiInput()
{
    if (midiInput_)
    {
        midiInput_->stop();
        midiInput_.reset();
    }
    currentInputDeviceId_.clear();
    deviceError_.store(false, std::memory_order_relaxed);
}

void MidiMapper::closeMidiOutput()
{
    midiOutput_.reset();
    currentOutputDeviceId_.clear();
    deviceError_.store(false, std::memory_order_relaxed);
}

//==============================================================================
// Status

bool MidiMapper::hasActiveMappings() const
{
    return !staging_.ccToParam.empty();
}

bool MidiMapper::isReceiving() const
{
    auto now = juce::Time::currentTimeMillis();
    auto lastRecv = lastMidiReceivedTime_.load(std::memory_order_relaxed);
    return lastRecv > 0 && (now - lastRecv) < 2000;
}

bool MidiMapper::hasError() const
{
    return deviceError_.load(std::memory_order_relaxed);
}

MidiMapper::Status MidiMapper::getStatus() const
{
    if (deviceError_.load(std::memory_order_relaxed))
        return Status::Failed;
    if (staging_.ccToParam.empty())
        return Status::Disabled;
    auto now = juce::Time::currentTimeMillis();
    auto lastRecv = lastMidiReceivedTime_.load(std::memory_order_relaxed);
    if (lastRecv > 0 && (now - lastRecv) < 2000)
        return Status::Healthy;
    return Status::Degraded;
}

//==============================================================================
// Timer callback: centralized APVTS-to-NJClient bridge + standalone feedback

void MidiMapper::timerCallback()
{
    // === CENTRALIZED APVTS-to-NJClient remote sync (SOLE cmd_queue path) ===
    const int count = juce::jmin(processor.userCount.load(std::memory_order_relaxed), 16);
    for (int i = 0; i < count; ++i)
    {
        juce::String suffix = juce::String(i);
        float vol = *processor.apvts.getRawParameterValue("remoteVol_" + suffix);
        float pan = *processor.apvts.getRawParameterValue("remotePan_" + suffix);
        bool mute = *processor.apvts.getRawParameterValue("remoteMute_" + suffix) >= 0.5f;

        bool volChanged = std::abs(vol - lastSyncedRemoteVol_[static_cast<size_t>(i)]) > 0.001f;
        bool panChanged = std::abs(pan - lastSyncedRemotePan_[static_cast<size_t>(i)]) > 0.001f;
        bool muteChanged = mute != lastSyncedRemoteMute_[static_cast<size_t>(i)];

        if (volChanged || panChanged || muteChanged)
        {
            jamwide::SetUserStateCommand cmd;
            cmd.user_index = i;
            cmd.set_vol = volChanged;
            cmd.volume = vol;
            cmd.set_pan = panChanged;
            cmd.pan = pan;
            cmd.set_mute = muteChanged;
            cmd.mute = mute;
            processor.cmd_queue.try_push(std::move(cmd));

            if (volChanged) lastSyncedRemoteVol_[static_cast<size_t>(i)] = vol;
            if (panChanged) lastSyncedRemotePan_[static_cast<size_t>(i)] = pan;
            if (muteChanged) lastSyncedRemoteMute_[static_cast<size_t>(i)] = mute;
        }
    }

    // === Local solo APVTS-to-NJClient sync (D-15) ===
    for (int ch = 0; ch < 4; ++ch)
    {
        bool solo = *processor.apvts.getRawParameterValue("localSolo_" + juce::String(ch)) >= 0.5f;
        if (solo != lastSyncedLocalSolo_[static_cast<size_t>(ch)])
        {
            lastSyncedLocalSolo_[static_cast<size_t>(ch)] = solo;
            // Dispatch local solo via existing cmd_queue command
            jamwide::SetLocalChannelMonitoringCommand cmd;
            cmd.channel = ch;
            cmd.set_solo = true;
            cmd.solo = solo;
            processor.cmd_queue.try_push(std::move(cmd));
        }
    }

    // === Metro pan APVTS-to-NJClient sync (D-16) ===
    float metroPan = *processor.apvts.getRawParameterValue("metroPan");
    if (std::abs(metroPan - lastSyncedMetroPan_) > 0.001f)
    {
        lastSyncedMetroPan_ = metroPan;
        // Write directly to NJClient atomic (same as existing OscServer metro pan path)
        if (auto* client = processor.getClient())
            client->config_metronome_pan.store(metroPan, std::memory_order_relaxed);
    }

    // === Standalone MIDI output feedback ===
    if (midiOutput_)
    {
        auto table = std::atomic_load(&published_);
        if (table)
        {
            for (const auto& [key, mapping] : table->ccToParam)
            {
                // Check per-mapping echo suppression
                auto suppIt = echoSuppression_.find(key);
                if (suppIt != echoSuppression_.end() && suppIt->second > 0)
                {
                    suppIt->second--;
                    continue;
                }

                auto* param = processor.apvts.getParameter(mapping.paramId);
                if (!param) continue;

                float currentNorm = param->getValue();
                int ccValue;
                if (dynamic_cast<juce::AudioParameterBool*>(param))
                    ccValue = (currentNorm >= 0.5f) ? 127 : 0;
                else
                    ccValue = juce::roundToInt(currentNorm * 127.0f);

                if (ccValue != lastSentCcValues_[key])
                {
                    lastSentCcValues_[key] = ccValue;
                    int channel = (key >> 7) + 1;
                    int cc = key & 0x7F;
                    midiOutput_->sendMessageNow(
                        juce::MidiMessage::controllerEvent(channel, cc, ccValue));
                }
            }
        }
    }
}
