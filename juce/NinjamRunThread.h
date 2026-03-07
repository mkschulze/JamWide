#pragma once
#include <JuceHeader.h>

class JamWideJuceProcessor;

/**
 * NinjamRunThread -- juce::Thread subclass that runs the NJClient::Run() loop.
 *
 * Lifecycle: Created and started in JamWideJuceProcessor::prepareToPlay(),
 * stopped and destroyed in releaseResources(). Destructor has a safety-net
 * stopThread() call in case releaseResources() was not invoked by the host.
 *
 * Phase 2: Skeleton only -- proves lifecycle management works.
 * Phase 3: Will add NJClient::Run() call and command queue processing.
 */
class NinjamRunThread : public juce::Thread
{
public:
    explicit NinjamRunThread(JamWideJuceProcessor& processor);
    ~NinjamRunThread() override;

    void run() override;

private:
    JamWideJuceProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NinjamRunThread)
};
