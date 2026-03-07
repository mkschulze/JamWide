#pragma once
#include <JuceHeader.h>

class JamWideJuceProcessor;
class NJClient;

/**
 * NinjamRunThread -- juce::Thread subclass that runs the NJClient::Run() loop.
 *
 * Lifecycle: Created and started in JamWideJuceProcessor::prepareToPlay(),
 * stopped and destroyed in releaseResources(). Destructor has a safety-net
 * stopThread() call in case releaseResources() was not invoked by the host.
 *
 * The run loop:
 *   1. Drains cmd_queue from Processor and dispatches commands under clientLock
 *   2. Calls NJClient::Run() under clientLock for network I/O
 *   3. Tracks status changes and sets up default local channel on connect
 *   4. Adaptive sleep: 20ms connected, 50ms disconnected
 */
class NinjamRunThread : public juce::Thread
{
public:
    explicit NinjamRunThread(JamWideJuceProcessor& processor);
    ~NinjamRunThread() override;

    void run() override;

private:
    void processCommands(NJClient* client);

    JamWideJuceProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NinjamRunThread)
};
