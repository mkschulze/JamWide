#pragma once

#include <juce_core/juce_core.h>

namespace jamwide {

// Phase 22 codex H1 — extracted from ChannelStripArea.cpp's former anon
// namespace (which gave these internal linkage and made them unreachable
// from VideoGridBand.cpp + JamWideJuceEditor.cpp). Now namespace-scoped
// with external linkage; the implementations live in BotFilter.cpp.
//
// `isBot` matches NINJAM bot prefixes (case-insensitive): "ninbot",
// "jambot", "ninjam". `stripAtSuffix` removes any `@server.suffix`.

bool         isBot(const juce::String& name);
juce::String stripAtSuffix(const juce::String& name);

} // namespace jamwide
