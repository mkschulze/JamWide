#pragma once
#include <JuceHeader.h>
#include <array>
#include <string>
#include <vector>

// Parameter entry types
enum class OscParamType
{
    ApvtsFloat,     // APVTS float param: use setValueNotifyingHost()
    ApvtsBool,      // APVTS bool param: use setValueNotifyingHost()
    NjclientAtomic, // NJClient atomic: direct store
    CmdQueue,       // Dispatch via cmd_queue (solo)
    ReadOnly        // Telemetry/VU: send-only
};

struct OscParamEntry
{
    juce::String oscAddress;     // e.g., "/JamWide/local/1/volume"
    juce::String apvtsId;        // e.g., "localVol_0" (empty for non-APVTS)
    OscParamType type;
    float rangeMin = 0.0f;       // OSC value range min
    float rangeMax = 1.0f;       // OSC value range max
    int channelIndex = -1;       // For local channels: 0-3; for cmd_queue solo
    bool isDbVariant = false;    // True for /volume/db addresses
};

class OscAddressMap
{
public:
    OscAddressMap();

    // Resolve an incoming OSC address to a param index. Returns -1 if not found.
    int resolve(const juce::String& address) const;

    // Get entry by index
    const OscParamEntry& getEntry(int index) const;

    // Get OSC address string for outgoing messages
    const juce::String& oscAddress(int index) const;

    // Get APVTS param ID for APVTS-backed params
    const juce::String& apvtsId(int index) const;

    // Total number of controllable parameters (OSC receive + send)
    int getControllableCount() const { return controllableCount; }

    // Total entries including read-only telemetry
    int getTotalCount() const { return static_cast<int>(entries.size()); }

    // Index ranges for iteration
    int getTelemetryStartIndex() const { return controllableCount; }

    // dB conversion helpers (per D-06: standard linear dB, not VbFader curve)
    static float linearToDb(float linear);
    static float dbToLinear(float db);

    // Pan conversion (APVTS -1..1 <-> OSC 0..1)
    static float apvtsPanToOsc(float apvtsPan);
    static float oscPanToApvts(float oscPan);

private:
    std::vector<OscParamEntry> entries;
    juce::HashMap<juce::String, int> addressLookup;
    int controllableCount = 0;  // Entries before this index are controllable

    void buildMap();
};
