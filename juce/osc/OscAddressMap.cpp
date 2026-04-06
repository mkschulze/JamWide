/*
    OscAddressMap.cpp - OSC address-to-parameter mapping

    Maps OSC addresses to APVTS parameters, NJClient atomics, cmd_queue commands,
    and read-only telemetry/VU paths.

    Address namespace: /JamWide/{section}/{param}
    Local channels use 1-based indexing in OSC, 0-based in APVTS.
*/

#include "osc/OscAddressMap.h"
#include <cmath>

OscAddressMap::OscAddressMap()
{
    buildMap();
}

void OscAddressMap::buildMap()
{
    entries.clear();
    addressLookup.clear();

    // ── Controllable parameters (receive + send) ──

    // Master volume (linear 0-1)
    entries.push_back({"/JamWide/master/volume", "masterVol",
                       OscParamType::ApvtsFloat, 0.0f, 1.0f, -1, false});

    // Master volume dB variant
    entries.push_back({"/JamWide/master/volume/db", "masterVol",
                       OscParamType::ApvtsFloat, -100.0f, 6.0f, -1, true});

    // Master mute
    entries.push_back({"/JamWide/master/mute", "masterMute",
                       OscParamType::ApvtsBool, 0.0f, 1.0f, -1, false});

    // Metronome volume (linear 0-1)
    entries.push_back({"/JamWide/metro/volume", "metroVol",
                       OscParamType::ApvtsFloat, 0.0f, 1.0f, -1, false});

    // Metronome volume dB variant
    entries.push_back({"/JamWide/metro/volume/db", "metroVol",
                       OscParamType::ApvtsFloat, -100.0f, 6.0f, -1, true});

    // Metronome pan (NJClient atomic, OSC 0..1 normalized like local pan)
    entries.push_back({"/JamWide/metro/pan", "",
                       OscParamType::NjclientAtomic, 0.0f, 1.0f, -1, false, true});

    // Metronome mute
    entries.push_back({"/JamWide/metro/mute", "metroMute",
                       OscParamType::ApvtsBool, 0.0f, 1.0f, -1, false});

    // Local channels (4 channels, 1-based OSC addressing)
    for (int ch = 0; ch < 4; ++ch)
    {
        juce::String chNum(ch + 1);  // 1-based for OSC
        juce::String suffix(ch);      // 0-based for APVTS

        // Volume (linear 0-1)
        entries.push_back({"/JamWide/local/" + chNum + "/volume",
                           "localVol_" + suffix,
                           OscParamType::ApvtsFloat, 0.0f, 1.0f, ch, false});

        // Volume dB variant
        entries.push_back({"/JamWide/local/" + chNum + "/volume/db",
                           "localVol_" + suffix,
                           OscParamType::ApvtsFloat, -100.0f, 6.0f, ch, true});

        // Pan (OSC 0-1, APVTS -1..1)
        entries.push_back({"/JamWide/local/" + chNum + "/pan",
                           "localPan_" + suffix,
                           OscParamType::ApvtsFloat, 0.0f, 1.0f, ch, false, true});

        // Mute
        entries.push_back({"/JamWide/local/" + chNum + "/mute",
                           "localMute_" + suffix,
                           OscParamType::ApvtsBool, 0.0f, 1.0f, ch, false});

        // Solo (cmd_queue dispatch, not APVTS)
        entries.push_back({"/JamWide/local/" + chNum + "/solo",
                           "",
                           OscParamType::CmdQueue, 0.0f, 1.0f, ch, false});
    }

    // Record controllable count before adding read-only entries
    controllableCount = static_cast<int>(entries.size());

    // ── Read-only telemetry (send-only) ──

    entries.push_back({"/JamWide/session/bpm", "",
                       OscParamType::ReadOnly, 0.0f, 999.0f, -1, false});

    entries.push_back({"/JamWide/session/bpi", "",
                       OscParamType::ReadOnly, 0.0f, 999.0f, -1, false});

    entries.push_back({"/JamWide/session/beat", "",
                       OscParamType::ReadOnly, 0.0f, 999.0f, -1, false});

    entries.push_back({"/JamWide/session/status", "",
                       OscParamType::ReadOnly, 0.0f, 10.0f, -1, false});

    entries.push_back({"/JamWide/session/users", "",
                       OscParamType::ReadOnly, 0.0f, 999.0f, -1, false});

    entries.push_back({"/JamWide/session/codec", "",
                       OscParamType::ReadOnly, 0.0f, 0.0f, -1, false});

    entries.push_back({"/JamWide/session/samplerate", "",
                       OscParamType::ReadOnly, 0.0f, 192000.0f, -1, false});

    // Master VU meters
    entries.push_back({"/JamWide/master/vu/left", "",
                       OscParamType::ReadOnly, 0.0f, 1.0f, -1, false});

    entries.push_back({"/JamWide/master/vu/right", "",
                       OscParamType::ReadOnly, 0.0f, 1.0f, -1, false});

    // Local channel VU meters (1-based)
    for (int ch = 0; ch < 4; ++ch)
    {
        juce::String chNum(ch + 1);

        entries.push_back({"/JamWide/local/" + chNum + "/vu/left", "",
                           OscParamType::ReadOnly, 0.0f, 1.0f, ch, false});

        entries.push_back({"/JamWide/local/" + chNum + "/vu/right", "",
                           OscParamType::ReadOnly, 0.0f, 1.0f, ch, false});
    }

    // Build HashMap for O(1) address lookup
    for (int i = 0; i < static_cast<int>(entries.size()); ++i)
        addressLookup.set(entries[static_cast<size_t>(i)].oscAddress, i);
}

int OscAddressMap::resolve(const juce::String& address) const
{
    if (addressLookup.contains(address))
        return addressLookup[address];
    return -1;
}

const OscParamEntry& OscAddressMap::getEntry(int index) const
{
    jassert(index >= 0 && index < static_cast<int>(entries.size()));
    return entries[static_cast<size_t>(index)];
}

const juce::String& OscAddressMap::oscAddress(int index) const
{
    return getEntry(index).oscAddress;
}

const juce::String& OscAddressMap::apvtsId(int index) const
{
    return getEntry(index).apvtsId;
}

// ── dB conversion (standard linear dB, per D-06) ──

float OscAddressMap::linearToDb(float linear)
{
    return linear <= 0.0001f ? -100.0f : 20.0f * std::log10(linear);
}

float OscAddressMap::dbToLinear(float db)
{
    return db <= -100.0f ? 0.0f : std::pow(10.0f, db / 20.0f);
}

// ── Pan conversion (APVTS -1..1 <-> OSC 0..1) ──

float OscAddressMap::apvtsPanToOsc(float apvtsPan)
{
    return (apvtsPan + 1.0f) / 2.0f;
}

float OscAddressMap::oscPanToApvts(float oscPan)
{
    return oscPan * 2.0f - 1.0f;
}
