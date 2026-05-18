#include "BotFilter.h"

namespace jamwide {

bool isBot(const juce::String& name)
{
    int atIdx = name.lastIndexOfChar('@');
    juce::String cleanName = (atIdx > 0) ? name.substring(0, atIdx) : name;
    return cleanName.startsWithIgnoreCase("ninbot")
        || cleanName.startsWithIgnoreCase("jambot")
        || cleanName.startsWithIgnoreCase("ninjam");
}

juce::String stripAtSuffix(const juce::String& name)
{
    int atIdx = name.lastIndexOfChar('@');
    return (atIdx > 0) ? name.substring(0, atIdx) : name;
}

} // namespace jamwide
